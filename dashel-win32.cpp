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
#include <malloc.h>
#include <map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <sstream>

#ifdef _MSC_VER
	#pragma comment(lib, "ws2_32.lib")
	#pragma comment(lib, "wbemuuid.lib")
	#pragma comment(lib, "comsuppw.lib")
#endif // _MSC_VER

#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <windows.h>
#include <setupapi.h>


#ifdef _MSC_VER
	#include <comdef.h>
	#include <Wbemidl.h>
//#else
//	#include "Wbemidl.h"
#endif // _MSC_VER

#pragma warning(disable:4996)

#include "dashel-private.h"

/*!	\file dashel-win32.cpp
	\brief Win32 implementation of DaSHEL, A cross-platform DAta Stream Helper Encapsulation Library
*/
namespace Dashel
{
	//! Asserts a dynamic cast.	Similar to the one in boost/cast.hpp
	template<typename Derived, typename Base>
	inline Derived polymorphic_downcast(Base base)
	{
		Derived derived = dynamic_cast<Derived>(base);
		assert(derived);
		return derived;
	}

	DashelException::DashelException(Source s, int se, const char *reason, Stream *stream)
	{
		source = s;
		sysError = se;
		this->reason = reason;
		this->stream = stream;
		char buf[1024];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, se, 0, buf, 1024, NULL);
		this->sysMessage = buf;
	}

	void Stream::fail(DashelException::Source s, int se, const char* reason)
	{
		char sysMessage[1024] = {0};
		failedFlag = true;

		if (se)
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, se, 0, sysMessage, 1024, NULL);

		failReason += reason;
		failReason += " ";
		failReason += sysMessage;
		throw DashelException(s, se, reason, this);
	}

		

	std::map<int, std::pair<std::string, std::string> > SerialPortEnumerator::getPorts()
	{
		std::map<int, std::pair<std::string, std::string> > ports;

		#ifndef _MSC_VER
		
		// Oldschool technique - returns too many ports...
		DWORD n, p;
		EnumPorts(NULL, 1, NULL, 0, &n, &p);
		PORT_INFO_1 *d = (PORT_INFO_1*)alloca(n);
		if(!d)
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Could not allocate buffer for port devices.");
		if(!EnumPorts(NULL, 1, (LPBYTE)d, n, &n, &p))
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Cannot get serial port devices.");

		for(n = 0; n < p; ++n)
		{
			if(!strncmp(d[n].pName, "COM", 3))
			{
				int v = atoi(&d[n].pName[3]);
				if(v > 0 && v < 256)
				{
					ports.insert(std::pair<int, std::pair<std::string, std::string> >(v, std::pair<std::string, std::string> (d[n].pName, d[n].pName)));
				}
			}
		}
		
		#else // _MSC_VER 
		
		// Newschool technique - OK behaviour now...
		// WMI should be able to return everything, but everything we are looking for is probably well
		// lost in the namespaces.
		HRESULT hr;
	    hr = CoInitializeEx(0, COINIT_MULTITHREADED); 
	    if(FAILED(hr))
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Cannot get serial port devices (could not start COM).");
	    hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
	    if(FAILED(hr))
		{
	        CoUninitialize();
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Cannot get serial port devices (could not setup COM security).");
		}

		IWbemLocator *pLoc = NULL;
		hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID *) &pLoc);
	    if(FAILED(hr))
		{
	        CoUninitialize();
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Cannot get serial port devices (could not create WBEM locator).");
		} 

		IWbemServices *pSvc = NULL;
		hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
	    if(FAILED(hr))
		{
			pLoc->Release();
	        CoUninitialize();
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Cannot get serial port devices (could not connect to root of CIMV2).");
		} 
    
		hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
	    if(FAILED(hr))
		{
			pSvc->Release();
			pLoc->Release();
	        CoUninitialize();
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Cannot get serial port devices (could not set proxy blanket).");
		} 

		IEnumWbemClassObject* pEnumerator = NULL;
		hr = pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT * FROM Win32_PnPEntity WHERE ClassGuid=\"{4D36E978-E325-11CE-BFC1-08002BE10318}\""), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
	    if(FAILED(hr))
		{
			pSvc->Release();
			pLoc->Release();
	        CoUninitialize();
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Cannot get serial port devices (could not execute WBEM query).");
		} 

		IWbemClassObject *pclsObj;
		ULONG uReturn = 0;
		while (pEnumerator)
		{
			HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
			if(0 == uReturn)
				break;

			VARIANT vtProp;
			VariantInit(&vtProp);
			char dn[1024], dcn[1024], *co;

			// Get the value of the Name property
			hr = pclsObj->Get(L"Caption", 0, &vtProp, 0, 0);
			if(!FAILED(hr))
			{
				WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, dn, 1024, NULL, NULL);
				VariantClear(&vtProp);

				//hr = pclsObj->Get(L"Name", 0, &vtProp, 0, 0);
				//WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, dcn, 1024, NULL, NULL);
				//VariantClear(&vtProp);

				if((co = strstr(dn, "(COM")))
				{
					strcpy(dcn, co+1);
					strtok(dcn, ")");

					int v = atoi(&dcn[3]);

					if(v > 0 && v < 256)
					{
						std::string name = std::string("\\\\.\\").append(dcn);
						ports.insert(std::pair<int, std::pair<std::string, std::string> >(v, std::pair<std::string, std::string> (name, dn)));
					}
				}
			}
			pclsObj->Release();
			pclsObj = NULL;
		}

		pSvc->Release();
		pLoc->Release();
		pEnumerator->Release();
		CoUninitialize();
		
		#endif // _MSC_VER
		
		return ports;
	};


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
				throw DashelException(DashelException::Unknown, rv, "Could not start WinSock service.");
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
			ps.add("tcpin:host=0.0.0.0;port=5000;");
			ps.add(params.c_str());

			startWinSock();

			TCPIPV4Address bindAddress(ps.get("host"), ps.get<int>("port"));
			
			// Create socket.
			sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (sock == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot create socket.");
			
			// Reuse address.
			int flag = 1;
			if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&flag, sizeof (flag)) < 0)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot set address reuse flag on socket, probably the port is already in use.");
			
			// Bind socket.
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(bindAddress.port);
			addr.sin_addr.s_addr = htonl(bindAddress.address);
			if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot bind socket to port, probably the port is already in use.");
			
			// Listen on socket, backlog is sort of arbitrary.
			if(listen(sock, 16) == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot listen on socket.");

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
					fail(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot accept incoming connection on socket.");
				}
				
				// create stream
				std::string ls = TCPIPV4Address(ntohl(targetAddr.sin_addr.s_addr), ntohs(targetAddr.sin_port)).format();
				
				std::ostringstream buf;
				buf << (int)trg;
				ls = ls.append(";sock=").append(buf.str());
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
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot open standard input.");

			DWORD cm;
			GetConsoleMode(hf, &cm);
			cm &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
			if(!SetConsoleMode(hf, cm))
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot change standard input mode to immediate.");

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
			throw DashelException(DashelException::InvalidOperation, GetLastError(), "Cannot write to standard input.", this);
		}
		
		//! Cannot flush stdin.
		virtual void flush() 
		{ 
			throw DashelException(DashelException::InvalidOperation, GetLastError(), "Cannot flush standard input.", this);
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
					fail(DashelException::IOError, GetLastError(), "Read error from standard input.");
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
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot open standard output.");
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
					fail(DashelException::IOError, GetLastError(), "Write error to standard output.");
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
			fail(DashelException::InvalidOperation, GetLastError(), "Cannot read from standard output.");
		}

	};

	//! File stream
	class FileStream : public WaitableStream
	{
	protected:
		//! The file handle.
		HANDLE hf;

		//! The overlapped structure used for file reads.
		OVERLAPPED ovl;

		//! The current write offset.
		DWORD writeOffset;

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
					throw DashelException(DashelException::IOError, GetLastError(), "Cannot read from file stream.");
			}
		}

	public:

		//! Create the stream and associates a file descriptor
		FileStream(const std::string& params) : WaitableStream(params)
		{ 
			ParameterSet ps;
			ps.add("file:name;mode=read");
			ps.add(params.c_str());
			std::string name = ps.get("name");
			std::string mode = ps.get("mode");

			hf = NULL;
			if (mode == "read")
			{
				hf = CreateFile(name.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
				startStream();
			}
			else if (mode == "write")
			{
				dataUsed = 0;
				writeOffset = 0;
				hf = CreateFile(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
			}
			else if (mode == "readwrite") 
			{
				writeOffset = 0;
				hf = CreateFile(name.c_str(), GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
				startStream();
			}
			if(hf == INVALID_HANDLE_VALUE)
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot open file.");


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

				o.Offset = writeOffset;

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
						fail(DashelException::IOError, GetLastError(), "Cannot write to file.");
						break;
					}
				}
				else
				{
					ptr += len;
					left -= len;
				}
			
				writeOffset += len;	
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
						fail(DashelException::ConnectionLost, GetLastError(), "Reached end of file.");
						break;

					case ERROR_IO_PENDING:
						if(!GetOverlappedResult(hf, &o, &len, TRUE))
							fail(DashelException::IOError, GetLastError(), "File read I/O error.");
						if(len == 0)
							return;

						ptr += len;
						left -= len;
						break;

					default:
						fail(DashelException::IOError, GetLastError(), "File read I/O error.");
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
					fail(DashelException::IOError, GetLastError(), "Cannot read from file stream.");
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
		bool buildDCB(HANDLE sp, int speed, int bits, const std::string& parity, const std::string& stopbits, const std::string& fc)
		{
			DCB dcb;

			memset(&dcb, 0, sizeof(dcb));
			dcb.DCBlength = sizeof(dcb);

			if(!GetCommState(sp, &dcb))
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot read current serial port state.", this);

			// Fill in the DCB
			memset(&dcb,0,sizeof(dcb));
			dcb.DCBlength = sizeof(dcb);
			if(fc == "hard")
			{
				dcb.fOutxCtsFlow = TRUE;
				dcb.fRtsControl = RTS_CONTROL_DISABLE;
			}
			else
			{
				dcb.fOutxCtsFlow = FALSE;
				dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
			}

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
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot set new serial port state.", this);

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
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot set new serial port timeouts.", this);

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

			std::string name;
			if (ps.isSet("device"))
				name = ps.get("device");
			else
				name = std::string("\\\\.\\COM").append(ps.get("port"));

			hf = CreateFile(name.c_str(), GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if(hf == INVALID_HANDLE_VALUE)
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot open serial port.");

			buildDCB(hf, ps.get<int>("baud"), ps.get<int>("bits"), ps.get("parity"), ps.get("stop"), ps.get("fc"));

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

		//! Indicates whether stream is actually ready to read.
		/*! If a read is attempted when this flag is false, we need to wait for data
			to arrive, because our user is being cruel and did not wait for the 
			notification.
		*/
		bool readyToRead;

	public:
		//! Create the stream and associates a file descriptor
		/*! \param params Parameter string.
		*/
		SocketStream(const std::string& params) : WaitableStream(params)
		{ 
			ParameterSet ps;
			ps.add("tcp:host;port;sock=0");
			ps.add(params.c_str());

			sock = ps.get<SOCKET>("sock");
			if(!sock)
			{
				startWinSock();

				// create socket
				sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (sock == SOCKET_ERROR)
					throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot create socket.");
			
				TCPIPV4Address remoteAddress(ps.get("host"), ps.get<int>("port"));
				// connect
				sockaddr_in addr;
				addr.sin_family = AF_INET;
				addr.sin_port = htons(remoteAddress.port);
				addr.sin_addr.s_addr = htonl(remoteAddress.address);
				if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
					throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot connect to remote host.");
			}

			hev = createEvent(EvPotentialData);
			hev2 = createEvent(EvData);
			hev3 = createEvent(EvClosed);

			int rv = WSAEventSelect(sock, hev, FD_READ | FD_CLOSE);
			if (rv == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot select socket events.");

			readyToRead = false;
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
			if(t == EvData)
			{
				readyToRead = true;
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
					fail(DashelException::ConnectionFailed, GetLastError(), "Connection lost on write.");
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

			if(!readyToRead)
			{
				// Block until something happens.
				WaitForSingleObject(hev, INFINITE);
			}
			readyToRead = false;
			
			while (left)
			{
				int len = recv(sock, ptr, (int)left, 0);
				
				if (len == SOCKET_ERROR)
				{
					fail(DashelException::ConnectionFailed, GetLastError(), "Connection lost on read.");
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
		for (StreamsSet::iterator it = streams.begin(); it != streams.end(); ++it)
			delete *it;
	}
	
	Stream* Hub::connect(const std::string &target)
	{
		std::string proto, params;
		size_t c = target.find_first_of(':');
		if(c == std::string::npos)
			throw DashelException(DashelException::InvalidTarget, 0, "No protocol specified in target.");
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
			throw DashelException(DashelException::InvalidTarget, 0, r.c_str());
		}
		
		streams.insert(s);
		if (proto != "tcpin")
		{
			dataStreams.insert(s);
			connectionCreated(s);
		}
		return s;
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
		DWORD ms = timeout >= 0 ? timeout : INFINITE;

		// Loop in order to consume all events.
		do
		{
			DWORD hc = 1;

			// Collect all events from all our streams.
			for(std::set<Stream*>::iterator it = streams.begin(); it != streams.end(); ++it)
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
				throw DashelException(DashelException::SyncError, 0, "Wait failed.");
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
					catch (DashelException e) { }
					if(strs[r]->failed())
					{
						connectionClosed(strs[r], true);
						streams.erase(strs[r]);
						delete strs[r];
					}
				}
				// Notify user that something happended.
				if(ets[r] == EvClosed)
				{
					try
					{
						connectionClosed(strs[r], false);
					}
					catch (DashelException e) { }
					streams.erase(strs[r]);
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
