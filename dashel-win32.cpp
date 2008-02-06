/*
	DaSHEL
	A cross-platform DAta Stream Helper Encapsulation Library
	Copyright (C) 2007:
		
		Stephane Magnenat <stephane at magnenat dot net>
			(http://stephane.magnenat.net)
		Mobots group - Laboratory of Robotics Systems, EPFL, Lausanne
			(http://mobots.epfl.ch)
		
		Sebastian Gerlach
		Kenzan Technologies
			(http://www.kenzantech.com)
	
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
		* Redistributions of source code must retain the above copyright
		  notice, this list of conditions and the following disclaimer.
		* Redistributions in binary form must reproduce the above copyright
		  notice, this list of conditions and the following disclaimer in the
		  documentation and/or other materials provided with the distribution.
		* Neither the names of "Mobots", "Laboratory of Robotics Systems", "EPFL",
		  "Kenzan Technologies" nor the names of the contributors may be used to
		  endorse or promote products derived from this software without specific
		  prior written permission.
	
	THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS ``AS IS'' AND ANY
	EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
	WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS BE LIABLE FOR ANY
	DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
	(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
	ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "dashel.h"

#include <cassert>
#include <cstdlib>
#include <map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

#include <winsock2.h>
#include <windows.h>

#include "dashel-private.h"

/*!	\file dashel-win32.cpp
	\brief Win32 implementation of DaSHEL, A cross-platform DAta Stream Helper Encapsulation Library
*/
namespace Streams
{
	//! Asserts a dynamic cast.	Similar to the one in boost/cast.hpp
	template<typename Derived, typename Base>
	inline Derived polymorphic_downcast(Base base)
	{
		Derived derived = dynamic_cast<Derived>(base);
		assert(derived);
		return derived;
	}

	StreamException::StreamException(Source s, int se, Stream *stream, const char *reason)
	{
		source = s;
		sysError = se;
		this->reason = reason;
		this->stream = stream;
		char buf[1024];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, se, 0, buf, 1024, NULL);
		this->sysMessage = buf;
	}

	//! Ensure that the WinSock API has been started properly.
	void startWinSock()
	{
		bool started = false;
		if(!started)
		{
			WORD ver = 0x0101;
			WSADATA d;
			memset(&d, 0, sizeof(d));

			int rv = WSAStartup(ver, &d);
			if(rv)
				throw StreamException(StreamException::Unknown, rv, NULL, "Could not start WinSock service.");
			started = true;
		}
	}

	//! Stream with a handle that can be waited on.
	class WaitableStream: public Stream
	{
	public:
		//! The events on which we may want to wait.
		/*! Each element in the map is a type, handle pair.
		*/
		std::map<EvType,HANDLE> hEvents; 
		
	protected:
		//! Create a new event for this stream.
		/*! \param t Type of event to create.
		*/
		HANDLE createEvent(EvType t) 
		{
			HANDLE he = CreateEvent(NULL, FALSE, FALSE, NULL);
			hEvents[t] = he;
			return he;
		}

		//! Add an existing event for this stream.
		/*! \param t Type of event to attach to.
			\param he Event handle.
		*/
		void addEvent(EvType t, HANDLE he) 
		{
			hEvents[t] = he;
		}
		
	public:
		//! Constructor.
		WaitableStream(const std::string& params) : Stream(params) { }

		//! Destructor.
		/*! Releases all allocated handles.
		*/
		virtual ~WaitableStream()
		{
			for(std::map<EvType, HANDLE>::iterator it = hEvents.begin(); it != hEvents.end(); ++it)
				CloseHandle(it->second);
		}
		
		//! Callback when an event is notified, allowing the stream to rearm it.
		/*! \param srv Hub instance that has generated the notification.
			\param t Type of event.
		*/
		virtual void notifyEvent(Hub *srv, EvType t) { }
	};

	//! Socket server stream.
	/*! This stream is used for listening for incoming connections. It cannot be used for transfering
		data.
	*/
	class SocketServerStream : public WaitableStream
	{
	protected:
		//! The socket.
		SOCKET sock;

		//! The real data event handle.
		HANDLE hev;

	public:

		//! Create the stream and associates a file descriptor
		SocketServerStream(const std::string& params) : WaitableStream(params)
		{ 
			ParameterSet ps;
			ps.add("tcpin:port=5000;host=0.0.0.0;");
			ps.add(params.c_str());

			startWinSock();

			TCPIPV4Address bindAddress(ps.get("host"), ps.get<int>("port"));
			
			// Create socket.
			sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (sock == SOCKET_ERROR)
				throw StreamException(StreamException::ConnectionFailed, WSAGetLastError(), NULL, "Cannot create socket.");
			
			// Reuse address.
			int flag = 1;
			if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&flag, sizeof (flag)) < 0)
				throw StreamException(StreamException::ConnectionFailed, WSAGetLastError(), NULL, "Cannot set address reuse flag on socket, probably the port is already in use.");
			
			// Bind socket.
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(bindAddress.port);
			addr.sin_addr.s_addr = htonl(bindAddress.address);
			if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
				throw StreamException(StreamException::ConnectionFailed, WSAGetLastError(), NULL, "Cannot bind socket to port, probably the port is already in use.");
			
			// Listen on socket, backlog is sort of arbitrary.
			if(listen(sock, 16) == SOCKET_ERROR)
				throw StreamException(StreamException::ConnectionFailed, WSAGetLastError(), NULL, "Cannot listen on socket.");

			// Create and register event.
			hev = createEvent(EvConnect);
			WSAEventSelect(sock, hev, FD_ACCEPT);
		}

		//! Destructor
		~SocketServerStream()
		{
			if(sock)
				closesocket(sock);
		}

		//! Callback when an event is notified, allowing the stream to rearm it.
		/*! \param srv Hub instance.
			\param t Type of event.
		*/
		virtual void notifyEvent(Hub *srv, EvType t) 
		{ 
			if(t == EvConnect)
			{
				// Accept incoming connection.
				struct sockaddr_in targetAddr;
				int l = sizeof (targetAddr);
				SOCKET trg = accept (sock, (struct sockaddr *)&targetAddr, &l);
				if (trg == SOCKET_ERROR)
				{
					fail();
					throw StreamException(StreamException::ConnectionFailed, WSAGetLastError(), this, "Cannot accept incoming connection on socket.");
				}
				
				// create stream
				std::string ls = "tcp:host=";

				ls = ls.append(TCPIPV4Address(ntohl(targetAddr.sin_addr.s_addr), ntohs(targetAddr.sin_port)).format());
				
				char buf[32];
				_itoa_s((int)trg, buf, 32, 10);
				ls = ls.append(";sock=").append(buf);
				srv->connect(ls);
			}
		}

		virtual void write(const void *data, const size_t size) { }
		virtual void flush() { }
		virtual void read(void *data, size_t size) { }
	};

	//! Standard input stream.
	/*! Due to the really weird way Windows manages its consoles, normal overlapped file I/O is
		utterly useless here.
	*/
	class StdinStream : public WaitableStream
	{
	protected:
		//! The file handle.
		HANDLE hf;

		//! The real data event handle.
		HANDLE hev;

	public:

		//! Create the stream and associates a file descriptor
		StdinStream(const std::string& params) : WaitableStream(params)
		{ 
			ParameterSet ps;
			ps.add(params.c_str());

			if((hf = GetStdHandle(STD_INPUT_HANDLE)) == INVALID_HANDLE_VALUE)
				throw StreamException(StreamException::ConnectionFailed, GetLastError(), NULL, "Cannot open standard input.");

			DWORD cm;
			GetConsoleMode(hf, &cm);
			cm &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
			if(!SetConsoleMode(hf, cm))
				throw StreamException(StreamException::ConnectionFailed, GetLastError(), NULL, "Cannot change standard input mode to immediate.");

			// Create events.
			addEvent(EvPotentialData, hf);
			hev = createEvent(EvData);
		}

		//! Destructor
		~StdinStream()
		{
			CloseHandle(hf);
		}

		//! Callback when an event is notified, allowing the stream to rearm it.
		/*! \param t Type of event.
		*/
		virtual void notifyEvent(Hub *srv, EvType t) 
		{ 
			DWORD n = 0;
			if(GetNumberOfConsoleInputEvents(hf, &n))
			{
				if(n > 0)
				{
					INPUT_RECORD ir;
					PeekConsoleInput(hf, &ir, 1, &n);
					if(ir.EventType != KEY_EVENT)
						ReadConsoleInput(hf, &ir, 1, &n);
					else
						SetEvent(hev);
				}
			}
		}

		//! Cannot write to stdin.
		virtual void write(const void *data, const size_t size)
		{ 
			throw StreamException(StreamException::InvalidOperation, GetLastError(), this, "Cannot write to standard input.");
		}
		
		//! Cannot flush stdin.
		virtual void flush() 
		{ 
			throw StreamException(StreamException::InvalidOperation, GetLastError(), this, "Cannot flush standard input.");
		}
		
		virtual void read(void *data, size_t size)
		{
			char *ptr = (char *)data;
			DWORD left = (DWORD)size;

			// Quick check to make sure nobody is giving us funny 64-bit stuff.
			assert(left == size);

			while (left)
			{
				DWORD len = 0;
				BOOL r;

				// Blocking write.
				if((r = ReadFile(hf, ptr, left, &len, NULL)) == 0)
				{
					fail();
					throw StreamException(StreamException::IOError, GetLastError(), this, "Read error from standard input.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}

	};

	//! Standard output stream.
	class StdoutStream : public WaitableStream
	{
	protected:
		//! The file handle.
		HANDLE hf;

	public:

		//! Create the stream and associates a file descriptor
		StdoutStream(const std::string& params) : WaitableStream(params)
		{ 
			ParameterSet ps;
			ps.add(params.c_str());

			if((hf = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE)
				throw StreamException(StreamException::ConnectionFailed, GetLastError(), NULL, "Cannot open standard output.");
		}

		//! Destructor
		~StdoutStream()
		{
			CloseHandle(hf);
		}

		virtual void write(const void *data, const size_t size)
		{
			const char *ptr = (const char *)data;
			DWORD left = (DWORD)size;

			// Quick check to make sure nobody is giving us funny 64-bit stuff.
			assert(left == size);

			while (left)
			{
				DWORD len = 0;
				BOOL r;

				// Blocking write.
				if((r = WriteFile(hf, ptr, left, &len, NULL)) == 0)
				{
					fail();
					throw StreamException(StreamException::IOError, GetLastError(), this, "Write error to standard output.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}
		
		virtual void flush()
		{
			FlushFileBuffers(hf);
		}
		
		virtual void read(void *data, size_t size) 
		{ 
			fail();
			throw StreamException(StreamException::InvalidOperation, GetLastError(), this, "Cannot read from standard output.");
		}

	};

	//! File stream
	class FileStream : public WaitableStream
	{
	protected:
		//! The file handle.
		HANDLE hf;

		//! The overlapped structure used for serial port reads.
		OVERLAPPED ovl;

		//! The data used.
		DWORD dataUsed;

		//! Single byte for non-blocking read.
		BYTE data;

		//! Event for notifying end of file (i.e. disconnect)
		HANDLE hEOF;

	protected:
		//! Create a blank stream.
		/*! This constructor is used only by derived classes that initialize differently.
		*/
		FileStream(const std::string& params, bool dummy) : WaitableStream(params) { }

		//! Start non-blocking read on stream to get notifications when data arrives.
		void startStream(EvType et = EvData)
		{
			dataUsed = 0;
			memset(&ovl, 0, sizeof(ovl));
			ovl.hEvent = createEvent(et);
			BOOL r = ReadFile(hf, &data, 1, &dataUsed, &ovl);
			if(!r)
			{
				DWORD err = GetLastError();
				if(err != ERROR_IO_PENDING)
					throw StreamException(StreamException::IOError, GetLastError(), NULL, "Cannot read from file stream.");
			}
		}

	public:

		//! Create the stream and associates a file descriptor
		FileStream(const std::string& params) : WaitableStream(params)
		{ 
			ParameterSet ps;
			ps.add("file:mode=read");
			ps.add(params.c_str());
			std::string name = ps.get("name");
			std::string mode = ps.get("mode");

			hf = NULL;
			if (mode == "read")
				hf = CreateFile(name.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			else if (mode == "write")
				hf = CreateFile(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
			else if (mode == "readwrite") 
				hf = CreateFile(name.c_str(), GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
			if(hf == INVALID_HANDLE_VALUE)
				throw StreamException(StreamException::ConnectionFailed, GetLastError(), NULL, "Cannot open file.");

			startStream();

			hEOF = createEvent(EvClosed);
		}

		//! Destructor
		~FileStream()
		{
			CloseHandle(hf);
		}
		
		virtual void write(const void *data, const size_t size)
		{
			const char *ptr = (const char *)data;
			DWORD left = (DWORD)size;

			// Quick check to make sure nobody is giving us funny 64-bit stuff.
			assert(left == size);

			while (left)
			{
				DWORD len = 0;
				OVERLAPPED o;
				memset(&o, 0, sizeof(o));

				// Blocking write.
				BOOL r = WriteFile(hf, ptr, left, &len, &o);
				if(!r)
				{
					DWORD err;
					switch((err = GetLastError()))
					{
					case ERROR_IO_PENDING:
						GetOverlappedResult(hf, &o, &len, TRUE);
						ptr += len;
						left -= len;
						break;

					default:
						fail();
						throw StreamException(StreamException::IOError, GetLastError(), this, "Cannot write to file.");
						break;
					}
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}
		
		virtual void flush()
		{
			FlushFileBuffers(hf);
		}
		
		virtual void read(void *data, size_t size)
		{
			char *ptr = (char *)data;
			DWORD left = (DWORD)size;

			// Quick check to make sure nobody is giving us funny 64-bit stuff.
			assert(left == size);

			if(dataUsed)
			{
				*ptr++ = this->data;
				dataUsed--;
				left--;
			}

			while (left)
			{
				DWORD len = 0;
				OVERLAPPED o;
				memset(&o, 0, sizeof(o));
				o.Offset = ovl.Offset + size - left;

				// Blocking write.
				BOOL r = ReadFile(hf, ptr, left, &len, &o);
				if(!r)
				{
					DWORD err;
					switch((err = GetLastError()))
					{
					case ERROR_HANDLE_EOF:
						fail();
						throw StreamException(StreamException::ConnectionLost, GetLastError(), this, "Reached end of file.");
						break;

					case ERROR_IO_PENDING:
						if(!GetOverlappedResult(hf, &o, &len, TRUE))
							throw StreamException(StreamException::IOError, GetLastError(), this, "File read I/O error.");
						if(len == 0)
							return;

						ptr += len;
						left -= len;
						break;

					default:
						fail();
						throw StreamException(StreamException::IOError, GetLastError(), this, "File read I/O error.");
						break;
					}
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}

			// Reset our blocking read for whatever is up next.
			dataUsed = 0;
			ovl.Offset += (DWORD)size;
			BOOL r = ReadFile(hf, &this->data, 1, &dataUsed, &ovl);
			if(!r)
			{
				DWORD err = GetLastError();
				if(err == ERROR_HANDLE_EOF)
				{
					SetEvent(hEOF);
				}
				else if(err != ERROR_IO_PENDING)
				{
					fail();
					throw StreamException(StreamException::IOError, GetLastError(), this, "Cannot read from file stream.");
				}
			}

		}
	};

	//! Serial port stream
	class SerialStream : public FileStream
	{
		//! Event for real data.
		HANDLE hev;
	private:
		//! Set up a DCB for the given port parameters.
		/*! \param sp File handle to the serial port.
			\param speed Bits per second.
			\param bits Number of bits.
			\param parity Parity type.
			\param stopbits Number of stop bits.
		*/
		bool buildDCB(HANDLE sp, int speed, int bits, const std::string& parity, const std::string& stopbits)
		{
			DCB dcb;

			memset(&dcb, 0, sizeof(dcb));
			dcb.DCBlength = sizeof(dcb);

			if(!GetCommState(sp, &dcb))
				throw StreamException(StreamException::ConnectionFailed, GetLastError(), this, "Cannot read current serial port state.");

			// Fill in the DCB
			memset(&dcb,0,sizeof(dcb));
			dcb.DCBlength = sizeof(dcb);
			dcb.fOutxCtsFlow = FALSE;
			dcb.fOutxDsrFlow = FALSE;
			dcb.fDtrControl = DTR_CONTROL_DISABLE;
			dcb.fDsrSensitivity = FALSE;

			dcb.fBinary = TRUE;
			dcb.fParity = TRUE;
			dcb.BaudRate = speed;
			dcb.ByteSize = bits;
			if(parity == "even")
				dcb.Parity = EVENPARITY;
			else if(parity == "odd")
				dcb.Parity = ODDPARITY;
			else if(parity == "space")
				dcb.Parity = SPACEPARITY;
			else if(parity == "mark")
				dcb.Parity = MARKPARITY;
			else
				dcb.Parity = NOPARITY;

			if(stopbits == "1.5")
				dcb.StopBits = ONE5STOPBITS;
			else if(stopbits == "2")
				dcb.StopBits = TWOSTOPBITS;
			else 
				dcb.StopBits = ONESTOPBIT;

			// Set the com port state.
			if(!SetCommState(sp, &dcb))
				throw StreamException(StreamException::ConnectionFailed, GetLastError(), this, "Cannot set new serial port state.");

			// Set timeouts as well for good measure. Since we will effectively be woken whenever
			// this happens, keep it long.
			COMMTIMEOUTS cto;
			memset(&cto, 0, sizeof(cto));
			cto.ReadIntervalTimeout = 100000;
			cto.ReadTotalTimeoutConstant = 100000;
			cto.ReadTotalTimeoutMultiplier = 100000;
			cto.WriteTotalTimeoutConstant = 100000;
			cto.WriteTotalTimeoutMultiplier = 100000;
			if(!SetCommTimeouts(sp, &cto))
				throw StreamException(StreamException::ConnectionFailed, GetLastError(), this, "Cannot set new serial port timeouts.");

			return true;
		}

	public:
		//! Create the stream and associates a file descriptor
		/*! \param params Parameter string.
		*/
		SerialStream(const std::string& params) : FileStream(params, true)
		{ 
			ParameterSet ps;
			ps.add("ser:port=1;baud=115200;stop=1;parity=none;fc=none;bits=8");
			ps.add(params.c_str());
			std::string name = std::string("\\\\.\\COM").append(ps.get("port"));

			hf = CreateFile(name.c_str(), GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if(hf == INVALID_HANDLE_VALUE)
				throw StreamException(StreamException::ConnectionFailed, GetLastError(), NULL, "Cannot open serial port.");

			buildDCB(hf, ps.get<int>("baud"), ps.get<int>("bits"), ps.get("parity"), ps.get("stop"));

			hev = createEvent(EvData);

			startStream(EvPotentialData);
		}

		//! Callback when an event is notified, allowing the stream to rearm it.
		/*! \param t Type of event.
		*/
		virtual void notifyEvent(Hub *srv, EvType t) 
		{ 
			if(t == EvPotentialData)
			{
				GetOverlappedResult(hf, &ovl, &dataUsed, TRUE);
				if(dataUsed == 0)
					ReadFile(hf, &data, 1, &dataUsed, &ovl);
				else
					SetEvent(hev);
			}
		}

	};

	//! Serial port stream
	class SocketStream : public WaitableStream
	{
		//! Socket handle.
		SOCKET sock;

		//! Event for potential data.
		HANDLE hev;

		//! Event for real data.
		HANDLE hev2;

		//! Event for shutdown.
		HANDLE hev3;

	private:

	public:
		//! Create the stream and associates a file descriptor
		/*! \param params Parameter string.
		*/
		SocketStream(const std::string& params) : WaitableStream(params)
		{ 
			ParameterSet ps;
			ps.add("tcp:sock=0;port=5000;");
			ps.add(params.c_str());

			sock = ps.get<SOCKET>("sock");
			if(!sock)
			{
				startWinSock();

				// create socket
				sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (sock == SOCKET_ERROR)
					throw StreamException(StreamException::ConnectionFailed, WSAGetLastError(), NULL, "Cannot create socket.");
			
				TCPIPV4Address remoteAddress(ps.get("host"), ps.get<int>("port"));
				// connect
				sockaddr_in addr;
				addr.sin_family = AF_INET;
				addr.sin_port = htons(remoteAddress.port);
				addr.sin_addr.s_addr = htonl(remoteAddress.address);
				if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
					throw StreamException(StreamException::ConnectionFailed, WSAGetLastError(), NULL, "Cannot connect to remote host.");
			}

			hev = createEvent(EvPotentialData);
			hev2 = createEvent(EvData);
			hev3 = createEvent(EvClosed);

			WSAEventSelect(sock, hev, FD_READ | FD_CLOSE);
		}

		~SocketStream()
		{
			closesocket(sock);
		}

		//! Callback when an event is notified, allowing the stream to rearm it.
		/*! \param t Type of event.
		*/
		virtual void notifyEvent(Hub *srv, EvType t) 
		{ 
			if(t == EvPotentialData)
			{
				fd_set s;
				timeval tv={0,0};
				FD_ZERO(&s);
				FD_SET(sock, &s);
				if(select(1, NULL, NULL, &s, &tv))
					SetEvent(hev3);
				else
					SetEvent(hev2);
			}
		}


		virtual void write(const void *data, const size_t size)
		{
			char *ptr = (char *)data;
			size_t left = size;
			
			while (left)
			{
				int len = send(sock, ptr, (int)left, 0);
				
				if (len == SOCKET_ERROR)
				{
					fail();
					throw StreamException(StreamException::ConnectionFailed, GetLastError(), NULL, "Connection lost.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}
		
		virtual void flush() { }
		
		virtual void read(void *data, size_t size)
		{
			char *ptr = (char *)data;
			size_t left = size;
			
			while (left)
			{
				int len = recv(sock, ptr, (int)left, 0);
				
				if (len == SOCKET_ERROR)
				{
					fail();
					throw StreamException(StreamException::ConnectionFailed, GetLastError(), NULL, "Connection lost.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}
	};

	Hub::Hub()
	{
		hTerminate = CreateEvent(NULL, TRUE, FALSE, NULL);
	}
	
	Hub::~Hub()
	{
	}
	
	void Hub::connect(const std::string &target)
	{
		std::string proto, params;
		size_t c = target.find_first_of(':');
		if(c == std::string::npos)
			throw StreamException(StreamException::InvalidTarget, NULL, NULL, "No protocol specified in target.");
		proto = target.substr(0, c);
		params = target.substr(c+1);

		WaitableStream *s = NULL;
		if(proto == "file")
			s = new FileStream(target);
		if(proto == "stdin")
			s = new StdinStream(target);
		if(proto == "stdout")
			s = new StdoutStream(target);
		if(proto == "ser")
			s = new SerialStream(target);
		if(proto == "tcpin")
			s = new SocketServerStream(target);
		if(proto == "tcp")
			s = new SocketStream(target);
		
		if(!s)
		{
			std::string r = "Invalid protocol in target: ";
			r = r.append(proto);
			throw StreamException(StreamException::InvalidTarget, 0, NULL, r.c_str());
		}

		incomingConnection(s);
			
		streams.push_back(s);
	}
	
	void Hub::run(void)
	{
		while(step(-1));
	}
	
	bool Hub::step(int timeout)
	{
		HANDLE hEvs[64] = { hTerminate };
		WaitableStream *strs[64] = { NULL };
		EvType ets[64] = { EvClosed };
		
		// Wait on all our events.
		DWORD ms = INFINITE;
		if(timeout >= 0)
			ms = timeout / 1000;

		// Loop in order to consume all events.
		do
		{
			DWORD hc = 1;

			// Collect all events from all our streams.
			for(std::list<Stream*>::iterator it = streams.begin(); it != streams.end(); ++it)
			{
				WaitableStream* stream = polymorphic_downcast<WaitableStream*>(*it);
				for(std::map<EvType,HANDLE>::iterator ei = stream->hEvents.begin(); ei != stream->hEvents.end(); ++ei)
				{
					strs[hc] = stream;
					ets[hc] = ei->first;
					hEvs[hc] = ei->second;
					hc++;
				}
			}

			DWORD r = WaitForMultipleObjects(hc, hEvs, FALSE, ms);
			
			// Check for error or timeout.
			if (r == WAIT_FAILED)
				throw StreamException(StreamException::SyncError, 0, NULL);
			if (r == WAIT_TIMEOUT)
				return true;

			// Look for what we got.
			r -= WAIT_OBJECT_0;
			if(r == 0)
			{
				// Quit
				return false;
			}
			else 
			{
				// Notify the stream that its event arrived.
				strs[r]->notifyEvent(this, ets[r]);

				// Notify user that something happended.
				if(ets[r] == EvData)
				{
					try
					{
						incomingData(strs[r]);
					}
					catch (StreamException e) { }
					if(strs[r]->failed())
					{
						connectionClosed(strs[r]);
						streams.remove(strs[r]);
						delete strs[r];
					}
				}
				// Notify user that something happended.
				if(ets[r] == EvClosed)
				{
					try
					{
						connectionClosed(strs[r]);
					}
					catch (StreamException e) { }
					streams.remove(strs[r]);
					delete strs[r];
				}
			}

			// No more timeouts on following rounds.
			ms = 0;
		}
		while(true);
	}

	void Hub::stop()
	{
		SetEvent(hTerminate);
	}


}
