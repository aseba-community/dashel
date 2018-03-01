/*
	Dashel
	A cross-platform DAta Stream Helper Encapsulation Library
	Copyright (C) 2007 -- 2018:

		Stephane Magnenat <stephane at magnenat dot net>
			(http://stephane.magnenat.net)
		Mobots group - Laboratory of Robotics Systems, EPFL, Lausanne
			(http://mobots.epfl.ch)

		Sebastian Gerlach
		Kenzan Technologies
			(http://www.kenzantech.com)

		and other contributors, see readme.md file for details.

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

// clang-format off
#ifdef _MSC_VER
	#pragma comment(lib, "ws2_32.lib")
	#pragma comment(lib, "wbemuuid.lib")
	#pragma comment(lib, "comsuppw.lib")
#endif // _MSC_VER

#ifndef _WIN32_WINNT
	#define _WIN32_WINNT 0x0501
#endif // _WIN32_WINNT
// clang-format on
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <regstr.h>
#include <winnls.h>
#include <cfgmgr32.h>

#pragma warning(disable : 4996)

#include "dashel-private.h"

/*!	\file dashel-win32.cpp
	\brief Win32 implementation of Dashel, A cross-platform DAta Stream Helper Encapsulation Library
*/

static const int DEFAULT_WAIT_TIMEOUT = 1000; //ms

namespace Dashel
{
	// clang-format off
	//! Event types that can be waited on.
	typedef enum {
		EvData,				//!< Data available.
		EvPotentialData,	//!< Maybe some data or maybe not.
		EvClosed,			//!< Closed by remote.
		EvConnect,			//!< Incoming connection detected.
	} EvType;
	// clang-format on

	//! Asserts a dynamic cast.	Similar to the one in boost/cast.hpp
	template<typename Derived, typename Base>
	inline Derived polymorphic_downcast(Base base)
	{
		Derived derived = dynamic_cast<Derived>(base);
		assert(derived);
		return derived;
	}

	void Stream::fail(DashelException::Source s, int se, const char* reason)
	{
		char sysMessage[1024] = { 0 };
		failedFlag = true;

		if (se)
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, se, 0, sysMessage, 1024, NULL);

		failReason = reason;
		failReason += " ";
		failReason += sysMessage;

		throw DashelException(s, se, failReason.c_str(), this);
	}

	// Serial port enumerator

	std::map<int, std::pair<std::string, std::string> > SerialPortEnumerator::getPorts()
	{
		std::map<int, std::pair<std::string, std::string> > ports;

		// Mainly based on http://support.microsoft.com/kb/259695
		HDEVINFO hDevInfo;
		SP_DEVINFO_DATA DeviceInfoData;
		DWORD i;
		char* co;
		char dn[1024], dcn[1024];

		// Create a HDEVINFO with all present ports.
		hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);

		if (hDevInfo == INVALID_HANDLE_VALUE)
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Cannot list serial port devices.");

		// Enumerate through all devices in Set.
		DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
		for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData); i++)
		{
			DWORD DataT;
			LPTSTR buffer = NULL;
			DWORD buffersize = 0;

			// Call function with null to begin with, then use the returned buffer size (doubled) to Alloc the buffer. Keep calling until
			// success or an unknown failure.
			// Double the returned buffersize to correct for underlying legacy CM functions that return an incorrect buffersize value on
			// DBCS/MBCS systems.
			while (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &DeviceInfoData, SPDRP_FRIENDLYNAME, &DataT, (PBYTE)buffer, buffersize, &buffersize))
			{
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
				{
					// Change the buffer size.
					if (buffer)
						LocalFree(buffer);
					// Double the size to avoid problems on
					// W2k MBCS systems per KB 888609.
					buffer = (LPTSTR)LocalAlloc(LPTR, buffersize * 2);
				}
				else
					throw DashelException(DashelException::EnumerationError, GetLastError(), "Cannot get serial port properties.");
			}

			WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)buffer, -1, dn, 1024, NULL, NULL);

			// Filter to get only the COMx ports
			if ((co = strstr(dn, "(COM")))
			{
				strcpy(dcn, co + 1);
				strtok(dcn, ")");

				int v = atoi(&dcn[3]);

				if (v > 0 && v < 256)
				{
					std::string name = std::string("\\\\.\\").append(dcn);
					ports.insert(std::pair<int, std::pair<std::string, std::string> >(v, std::pair<std::string, std::string>(name, dn)));
				}
			}


			if (buffer)
				LocalFree(buffer);
		}

		// Error ?
		if (GetLastError() != NO_ERROR && GetLastError() != ERROR_NO_MORE_ITEMS)
			throw DashelException(DashelException::EnumerationError, GetLastError(), "Error while enumerating serial port devices.");

		// Cleanup
		SetupDiDestroyDeviceInfoList(hDevInfo);

		return ports;
	};


	//! Ensure that the WinSock API has been started properly.
	void startWinSock()
	{
		static bool started = false;
		if (!started)
		{
			WORD ver = 0x0101;
			WSADATA d;
			memset(&d, 0, sizeof(d));

			int rv = WSAStartup(ver, &d);
			if (rv)
				throw DashelException(DashelException::Unknown, rv, "Could not start WinSock service.");
			started = true;
		}
	}

	//! Stream with a handle that can be waited on.
	class WaitableStream : virtual public Stream
	{
	public:
		//! The events on which we may want to wait.
		/*! Each element in the map is a type, handle pair.
		*/
		std::map<EvType, HANDLE> hEvents;

		//! Flag indicating whether a read was performed.
		bool readDone;

	protected:
		//! Event for notifying end of stream (i.e. disconnect)
		HANDLE hEOF;

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
		WaitableStream(const std::string& protocolName) :
			Stream(protocolName)
		{
			hEOF = createEvent(EvClosed);
		}

		//! Destructor.
		/*! Releases all allocated handles.
		*/
		virtual ~WaitableStream()
		{
			for (std::map<EvType, HANDLE>::iterator it = hEvents.begin(); it != hEvents.end(); ++it)
				CloseHandle(it->second);
		}

		// clang-format off
		//! Callback when an event is notified, allowing the stream to rearm it.
		/*! \param srv Hub instance that has generated the notification.
			\param t Type of event.
		*/
		virtual void notifyEvent(Hub* srv, EvType& t) { /* hook for use by derived classes */ }

		//! Callback when incomingData is called, allowing the stream to rearm it.
		//! Used by poll streams to rearm their edge triggers.
		//! \param srv Hub instance that has generated the notification.
		//! \param t Type of event.
		virtual void notifyIncomingData(Hub* srv, EvType& t) { /* hook for use by derived classes */ }
		// clang-format on
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

		//!< Whether Dashel should try to resolve the peer's hostname of incoming TCP connections
		const bool resolveIncomingNames;

	public:
		//! Create the stream and associates a file descriptor
		SocketServerStream(const std::string& params, const Hub& hub) :
			Stream("tcpin"),
			WaitableStream("tcpin"),
			resolveIncomingNames(hub.resolveIncomingNames)
		{
			target.add("tcpin:port=5000;address=0.0.0.0");
			target.add(params.c_str());

			startWinSock();

			IPV4Address bindAddress(target.get("address"), target.get<int>("port"));

			// Create socket.
			sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (sock == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot create socket.");

			// Reuse address.
			int flag = 1;
			if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&flag, sizeof(flag)) < 0)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot set address reuse flag on socket, probably the port is already in use.");

			// Bind socket.
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(bindAddress.port);
			addr.sin_addr.s_addr = htonl(bindAddress.address);
			if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot bind socket to port, probably the port is already in use.");

			// retrieve port number, if a dynamic one was requested
			if (bindAddress.port == 0)
			{
				int sizeof_addr(sizeof(addr));
				if (getsockname(sock, (struct sockaddr*)&addr, &sizeof_addr) != 0)
					throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot retrieve socket port assignment.");
				target.erase("port");
				std::ostringstream portnum;
				portnum << ntohs(addr.sin_port);
				target.addParam("port", portnum.str().c_str(), true);
			}

			// Listen on socket, backlog is sort of arbitrary.
			if (listen(sock, 16) == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot listen on socket.");

			// Create and register event.
			hev = createEvent(EvConnect);
			WSAEventSelect(sock, hev, FD_ACCEPT);
		}

		//! Destructor
		~SocketServerStream()
		{
			if (sock)
				closesocket(sock);
		}

		//! Callback when an event is notified, allowing the stream to rearm it.
		/*! \param srv Hub instance.
			\param t Type of event.
		*/
		virtual void notifyEvent(Hub* srv, EvType& t)
		{
			if (t == EvConnect)
			{
				// Accept incoming connection.
				struct sockaddr_in targetAddr;
				int l = sizeof(targetAddr);
				SOCKET trg = accept(sock, (struct sockaddr*)&targetAddr, &l);
				if (trg == SOCKET_ERROR)
				{
					fail(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot accept incoming connection on socket.");
				}

				// create stream
				std::string ls = IPV4Address(ntohl(targetAddr.sin_addr.s_addr), ntohs(targetAddr.sin_port)).format(resolveIncomingNames);

				std::ostringstream buf;
				buf << ";connectionPort=";
				buf << atoi(getTargetParameter("port").c_str());
				buf << ";sock=";
				buf << (int)trg;
				ls.append(buf.str());
				srv->connect(ls);
			}
		}

		// clang-format off
		virtual void write(const void* data, const size_t size) { /* hook for use by derived classes */}
		virtual void flush() { /* hook for use by derived classes */}
		virtual void read(void* data, size_t size) { /* hook for use by derived classes */}
		// clang-format on
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
		StdinStream(const std::string& params) :
			Stream("stdin"),
			WaitableStream("stdin")
		{
			target.add(params.c_str());

			if ((hf = GetStdHandle(STD_INPUT_HANDLE)) == INVALID_HANDLE_VALUE)
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot open standard input.");

			DWORD cm;
			GetConsoleMode(hf, &cm);
			cm &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
			if (!SetConsoleMode(hf, cm))
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
		virtual void notifyEvent(Hub* srv, EvType& t)
		{
			DWORD n = 0;
			if (GetNumberOfConsoleInputEvents(hf, &n))
			{
				if (n > 0)
				{
					INPUT_RECORD ir;
					PeekConsoleInput(hf, &ir, 1, &n);
					if (ir.EventType != KEY_EVENT)
						ReadConsoleInput(hf, &ir, 1, &n);
					else
					{
						t = EvData;
					}
				}
			}
		}

		//! Cannot write to stdin.
		virtual void write(const void* data, const size_t size)
		{
			throw DashelException(DashelException::InvalidOperation, GetLastError(), "Cannot write to standard input.", this);
		}

		//! Cannot flush stdin.
		virtual void flush()
		{
			throw DashelException(DashelException::InvalidOperation, GetLastError(), "Cannot flush standard input.", this);
		}

		virtual void read(void* data, size_t size)
		{
			char* ptr = (char*)data;
			DWORD left = (DWORD)size;

			// Quick check to make sure nobody is giving us funny 64-bit stuff.
			assert(left == size);

			readDone = true;

			while (left)
			{
				DWORD len = 0;
				BOOL r;

				// Blocking write.
				if ((r = ReadFile(hf, ptr, left, &len, NULL)) == 0)
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
		StdoutStream(const std::string& params) :
			Stream("stdout"),
			WaitableStream("stdout")
		{
			target.add(params.c_str());

			if ((hf = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE)
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot open standard output.");
		}

		//! Destructor
		~StdoutStream()
		{
			CloseHandle(hf);
		}

		virtual void write(const void* data, const size_t size)
		{
			const char* ptr = (const char*)data;
			DWORD left = (DWORD)size;

			// Quick check to make sure nobody is giving us funny 64-bit stuff.
			assert(left == size);

			while (left)
			{
				DWORD len = 0;
				BOOL r;

				// Blocking write.
				if ((r = WriteFile(hf, ptr, left, &len, NULL)) == 0)
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

		virtual void read(void* data, size_t size)
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

		//! Indicates whether stream is actually ready to read.
		/*! If a read is attempted when this flag is false, we need to wait for data
			to arrive, because our user is being cruel and did not wait for the 
			notification.
		*/
		bool readyToRead;

		//! Byte that is read to check for disconnections. Yuck.
		char readByte;

		//! Flag indicating whether readByte is around.
		bool readByteAvailable;

	protected:
		//! Create a blank stream.
		/*! This constructor is used only by derived classes that initialize differently.
		*/
		FileStream(const std::string& protocolName, bool dummy) :
			Stream(protocolName),
			WaitableStream(protocolName) {}

		//! Start non-blocking read on stream to get notifications when data arrives.
		void startStream(EvType et = EvData)
		{
			readByteAvailable = false;
			memset(&ovl, 0, sizeof(ovl));
			ovl.hEvent = createEvent(et);
			BOOL r = ReadFile(hf, &readByte, 1, NULL, &ovl);
			if (!r)
			{
				DWORD err = GetLastError();
				if (err != ERROR_IO_PENDING)
					throw DashelException(DashelException::IOError, GetLastError(), "Cannot read from file stream.");
			}
			else
				readByteAvailable = true;
		}

	public:
		//! Create the stream and associates a file descriptor
		FileStream(const std::string& params) :
			Stream("file"),
			WaitableStream("file")
		{
			target.add("file:name;mode=read");
			target.add(params.c_str());
			std::string name = target.get("name");
			std::string mode = target.get("mode");

			hf = NULL;
			if (mode == "read")
			{
				hf = CreateFileA(name.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
				startStream();
			}
			else if (mode == "write")
			{
				writeOffset = 0;
				hf = CreateFileA(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
			}
			else if (mode == "readwrite")
			{
				writeOffset = 0;
				hf = CreateFileA(name.c_str(), GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
				startStream();
			}
			if (hf == INVALID_HANDLE_VALUE)
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot open file.");
		}

		//! Destructor
		~FileStream()
		{
			CloseHandle(hf);
		}

		virtual void write(const void* data, const size_t size)
		{
			const char* ptr = (const char*)data;
			const unsigned int RETRY_LIMIT = 3;
			DWORD left = (DWORD)size;
			unsigned int retry = 0;

			// Quick check to make sure nobody is giving us funny 64-bit stuff.
			assert(left == size);

			while (left)
			{
				DWORD len = 0;
				OVERLAPPED o;
				memset(&o, 0, sizeof(o));

				o.Offset = writeOffset;
				o.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

				// Blocking write.
				BOOL r = WriteFile(hf, ptr, left, &len, &o);
				if (!r)
				{
					DWORD err;
					switch ((err = GetLastError()))
					{
						case ERROR_IO_PENDING:
							GetOverlappedResult(hf, &o, &len, TRUE);
							if (len == 0)
							{
								if (retry++ >= RETRY_LIMIT)
								{
									SetEvent(hEOF);
									fail(DashelException::IOError, GetLastError(), "Cannot write to file (max retry reached).");
								}
								else
									continue;
							}
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

		virtual void read(void* data, size_t size)
		{
			char* ptr = (char*)data;
			DWORD left = (DWORD)size;

			// Quick check to make sure nobody is giving us funny 64-bit stuff.
			assert(left == size);

			if (size == 0)
				return;

			readDone = true;

			if (!readByteAvailable)
				WaitForSingleObject(ovl.hEvent, INFINITE);

			DWORD dataUsed;
			if (!GetOverlappedResult(hf, &ovl, &dataUsed, TRUE))
				fail(DashelException::IOError, GetLastError(), "File read I/O error.");

			if (dataUsed)
			{
				*ptr++ = readByte;
				left--;
			}
			readByteAvailable = false;

			while (left)
			{
				DWORD len = 0;
				OVERLAPPED o;
				memset(&o, 0, sizeof(o));
				o.Offset = ovl.Offset + size - left;
				o.hEvent = ovl.hEvent;

				// Non-blocking read.
				BOOL r = ReadFile(hf, ptr, left, &len, &o);
				if (!r)
				{
					DWORD err;
					switch ((err = GetLastError()))
					{
						case ERROR_HANDLE_EOF:
							fail(DashelException::ConnectionLost, GetLastError(), "Reached end of file.");
							break;

						case ERROR_IO_PENDING:
							WaitForSingleObject(ovl.hEvent, INFINITE);
							if (!GetOverlappedResult(hf, &o, &len, TRUE))
								fail(DashelException::IOError, GetLastError(), "File read I/O error.");
							if (len == 0)
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
					WaitForSingleObject(ovl.hEvent, INFINITE);
					ptr += len;
					left -= len;
				}
			}

			// Reset our blocking read for whatever is up next.
			ovl.Offset += (DWORD)size;
			BOOL r = ReadFile(hf, &readByte, 1, &dataUsed, &ovl);
			if (!r)
			{
				DWORD err = GetLastError();
				if (err == ERROR_HANDLE_EOF)
				{
					SetEvent(hEOF);
				}
				else if (err != ERROR_IO_PENDING)
				{
					fail(DashelException::IOError, GetLastError(), "Cannot read from file stream.");
				}
			}
			else
				readByteAvailable = true;
		}

		//! Callback when an event is notified, allowing the stream to rearm it.
		/*! \param t Type of event.
		*/
		virtual void notifyEvent(Hub* srv, EvType& t)
		{
			if (t == EvPotentialData)
			{
				DWORD dataUsed;
				GetOverlappedResult(hf, &ovl, &dataUsed, TRUE);
				if (dataUsed == 0)
					ReadFile(hf, &readByte, 1, NULL, &ovl);
				else
				{
					readByteAvailable = true;
					t = EvData;
				}
			}
		}
	};

	//! Serial port stream
	class SerialStream : public FileStream
	{
	protected:
		//! Device name.
		std::string devName;

	private:
		//! Set up a DCB for the given port parameters.
		/*! \param sp File handle to the serial port.
			\param speed Bits per second.
			\param bits Number of bits.
			\param parity Parity type.
			\param stopbits Number of stop bits.
		*/
		bool buildDCB(HANDLE sp, int speed, int bits, bool dtr, const std::string& parity, const std::string& stopbits, const std::string& fc)
		{
			DCB dcb;

			memset(&dcb, 0, sizeof(dcb));
			dcb.DCBlength = sizeof(dcb);

			if (!GetCommState(sp, &dcb))
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot read current serial port state.", this);

			// Fill in the DCB
			memset(&dcb, 0, sizeof(dcb));
			dcb.DCBlength = sizeof(dcb);
			if (fc == "hard")
			{
				dcb.fOutxCtsFlow = TRUE;
				dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
			}
			else
			{
				dcb.fOutxCtsFlow = FALSE;
				dcb.fRtsControl = RTS_CONTROL_DISABLE;
			}

			dcb.fOutxDsrFlow = FALSE;
			if (dtr)
				dcb.fDtrControl = DTR_CONTROL_ENABLE;
			else
				dcb.fDtrControl = DTR_CONTROL_DISABLE;
			dcb.fDsrSensitivity = FALSE;
			dcb.fBinary = TRUE;
			dcb.fParity = TRUE;
			dcb.BaudRate = speed;
			dcb.ByteSize = bits;
			if (parity == "even")
				dcb.Parity = EVENPARITY;
			else if (parity == "odd")
				dcb.Parity = ODDPARITY;
			else if (parity == "space")
				dcb.Parity = SPACEPARITY;
			else if (parity == "mark")
				dcb.Parity = MARKPARITY;
			else
				dcb.Parity = NOPARITY;

			if (stopbits == "1.5")
				dcb.StopBits = ONE5STOPBITS;
			else if (stopbits == "2")
				dcb.StopBits = TWOSTOPBITS;
			else
				dcb.StopBits = ONESTOPBIT;

			// Set the com port state.
			if (!SetCommState(sp, &dcb))
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
			if (!SetCommTimeouts(sp, &cto))
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), "Cannot set new serial port timeouts.", this);

			return true;
		}

		/*! Convert device name to port name (for example \\.\COM6 or COM6 are both converted to COM6)
			\param devName device name
			\return port name
		*/
		static std::string devNameToPortName(std::string const& devName)
		{
			size_t pos = devName.find("\\COM");
			if (pos == std::string::npos)
				return devName; // likely "COMnn"
			return devName.substr(pos + 1);
		}

	public:
		//! Create the stream and associates a file descriptor
		/*! \param params Parameter string.
		*/
		SerialStream(const std::string& params) :
			Stream("ser"),
			FileStream("ser", true)
		{
			target.add("ser:port=1;baud=115200;stop=1;parity=none;fc=none;bits=8;dtr=true");
			target.add(params.c_str());

			if (target.isSet("device"))
			{
				target.addParam("device", NULL, true);
				target.erase("port");

				devName = target.get("device");
			}
			else if (target.isSet("name"))
			{
				target.addParam("name", NULL, true);
				target.erase("port");
				target.erase("device");

				// Enumerates the ports
				std::string name = target.get("name");
				std::map<int, std::pair<std::string, std::string> > ports = SerialPortEnumerator::getPorts();

				// Iterate on all ports to found one with "name" in its description
				std::map<int, std::pair<std::string, std::string> >::iterator it;
				for (it = ports.begin(); it != ports.end(); it++)
				{
					if (it->second.second.find(name) != std::string::npos)
					{
						devName = it->second.first;
						std::cout << "Found " << name << " on port " << devName << std::endl;
						break;
					}
				}
				if (devName.size() == 0)
					throw DashelException(DashelException::ConnectionFailed, 0, "The specified name could not be find among the serial ports.");
			}
			else
			{
				target.erase("device");

				devName = std::string("\\\\.\\COM").append(target.get("port"));
			}

			hf = CreateFileA(devName.c_str(), GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if (hf == INVALID_HANDLE_VALUE)
				throw DashelException(DashelException::ConnectionFailed, GetLastError(), (std::string("Cannot open serial port ") + devName + ".").c_str());

			buildDCB(hf, target.get<int>("baud"), target.get<int>("bits"), target.get<bool>("dtr"), target.get("parity"), target.get("stop"), target.get("fc"));

			startStream(EvPotentialData);
		}

		/*!	Check if as far as the OS is aware, the device is still connected
			\return true if the device is still connected, else false
		*/
		bool checkConnection() const
		{
		// convert devName to portName as wstring
#ifdef _UNICODE
			std::string portName8 = devNameToPortName(devName);
			std::wstring portName = std::wstring(portName8.begin(), portName8.end());
#else
			std::string portName = devNameToPortName(devName);
#endif

			SP_DEVINFO_DATA deviceInfoData;
			deviceInfoData.cbSize = sizeof(deviceInfoData);
			HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
			for (int i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); i++)
			{
				DWORD dataType;
				DWORD bufSize;
				SetupDiGetDeviceRegistryProperty(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, &dataType, NULL, 0, &bufSize);
				std::vector<TCHAR> buffer(bufSize);

				// clang-format off
				if (SetupDiGetDeviceRegistryProperty(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, &dataType, (PBYTE)&buffer[0], bufSize, NULL))
				{
					// find "(COM"
#ifdef _UNICODE
					wchar_t const* p = wcsstr(&buffer[0], L"(COM");
#else
					char const* p = strstr(&buffer[0], "(COM");
#endif
					if (p)
					{
						// set port to "COMn" or "COMnn"
#ifdef _UNICODE
						std::wstring port = std::wstring(p + 1, isdigit(p[5]) ? 5 : 4);
#else
						std::string port = std::string(p + 1, isdigit(p[5]) ? 5 : 4);
#endif
						if (port == portName)
						{
							// match portName: check if still disconnected
							ULONG pulStatus, pulProblemNumber;
							bool connected = !CM_Get_DevNode_Status(&pulStatus, &pulProblemNumber, deviceInfoData.DevInst, 0)
								|| !(pulStatus & DN_WILL_BE_REMOVED);
							return connected;
						}
					}
				}
				// clang-format on
			}

			// device not found by SetupDiEnumDeviceInfo, assume it is disconnected
			return false;
		}
	};

	//! Assign a socket file descriptor to a target. Factored out from SocketStream::SocketStream.
	//! If the target specifies a socket with a nonnegative "sock=N" parameter, assume it is valid
	//! and use it. Otherwise, the host and port parameters are used to look up a TCP/IP host, and
	//! a new socket is created.
	//! Raises an exception if the socket cannot be created, or if the TCP/IP host cannot be reached.
	static int getOrCreateSocket(ParameterSet& target)
	{
		int sock = target.get<SOCKET>("sock");
		if (!sock)
		{
			startWinSock();

			// create socket
			sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (sock == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot create socket.");

			IPV4Address remoteAddress(target.get("host"), target.get<int>("port"));
			// connect
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(remoteAddress.port);
			addr.sin_addr.s_addr = htonl(remoteAddress.address);
			if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot connect to remote host.");

			// overwrite target name with a canonical one
			target.add(remoteAddress.format().c_str());
			target.erase("connectionPort");
		}
		return sock;
	}

	//! Socket stream
	class SocketStream : public WaitableStream
	{
		//! Socket handle.
		SOCKET sock;

		//! Event for potential data.
		HANDLE hev;

		//! Event for real data.
		HANDLE hev2;

		//! Indicates whether stream is actually ready to read.
		/*! If a read is attempted when this flag is false, we need to wait for data
			to arrive, because our user is being cruel and did not wait for the 
			notification.
		*/
		bool readyToRead;

		//! Byte that is read to check for disconnections. Yuck.
		char readByte;

		//! Flag indicating whether readByte is around.
		bool readByteAvailable;

	public:
		//! Create the stream and associates a file descriptor
		/*! \param params Parameter string.
		*/
		SocketStream(const std::string& params) :
			Stream("tcp"),
			WaitableStream("tcp")
		{
			target.add("tcp:host;port;connectionPort=-1;sock=0");
			target.add(params.c_str());

			sock = getOrCreateSocket(target);
			if (target.get<SOCKET>("sock") != INVALID_SOCKET)
			{
				// remove file descriptor information from target name
				target.erase("sock");
			}

			hev2 = createEvent(EvData);
			hev = createEvent(EvPotentialData);

			int rv = WSAEventSelect(sock, hev, FD_READ | FD_CLOSE);
			if (rv == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot select socket events.");

			readyToRead = false;
			readByteAvailable = false;
		}

		~SocketStream()
		{
			shutdown(sock, SD_BOTH);
			closesocket(sock);
		}

		//! Callback when an event is notified, allowing the stream to rearm it.
		/*! \param t Type of event.
		*/
		virtual void notifyEvent(Hub* srv, EvType& t)
		{
			if (t == EvPotentialData)
			{
				if (readByteAvailable)
					return;

				int rv = recv(sock, &readByte, 1, 0);
				if (rv <= 0)
				{
					t = EvClosed;
				}
				else
				{
					readByteAvailable = true;
					readyToRead = true;
					t = EvData;
				}
			}
		}

		virtual void write(const void* data, const size_t size)
		{
			char* ptr = (char*)data;
			size_t left = size;

			while (left)
			{
				int len = send(sock, ptr, (int)left, 0);

				if (len == SOCKET_ERROR)
				{
					fail(DashelException::ConnectionLost, GetLastError(), "Connection lost on write.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}

		// clang-format off
		virtual void flush() { /* hook for use by derived classes */ }
		// clang-format on

		virtual void read(void* data, size_t size)
		{
			char* ptr = (char*)data;
			size_t left = size;

			if (size == 0)
				return;

			readDone = true;

			//std::cerr << "ready to read " << readyToRead << std::endl;
			if (!readyToRead)
			{
				// Block until something happens.
				WaitForSingleObject(hev, INFINITE);
			}
			readyToRead = false;

			if (readByteAvailable)
			{
				*ptr++ = readByte;
				readByteAvailable = false;
				left--;
				if (left)
					WaitForSingleObject(hev, INFINITE);
			}

			while (left)
			{
				//std::cerr << "ready to recv " << std::endl;
				int len = recv(sock, ptr, (int)left, 0);
				//std::cerr << "recv done " << std::endl;

				if (len == SOCKET_ERROR)
				{
					//std::cerr << "socket error" << std::endl;
					fail(DashelException::ConnectionLost, GetLastError(), "Connection lost on read.");
				}
				else if (len == 0)
				{
					// We have been disconnected.
				}
				else
				{
					ptr += len;
					left -= len;
				}
				if (left)
				{
					// Wait for more data.
					WaitForSingleObject(hev, DEFAULT_WAIT_TIMEOUT);
				}
			}

			int rv = WSAEventSelect(sock, hev, FD_READ | FD_CLOSE);
			if (rv == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot select socket events.");
		}
	};

	//! Poll a socket file descriptor for either a local socket (tcppoll:sock=N) or a
	//! remote TCP/IP socket (tcppoll:host=HOST;port=PORT). Delegates fd choice to getOrCreateSocket.
	//! Poll streams are used to include sockets that will be read or written by client code in the
	//! Dashel polling loop. Dashel itself neither reads from nor writes to the socket. PollStream will
	//! call Hub::incomingData(stream) exactly once when its socket is polled with POLLIN in Hub::step.
	class PollStream : public WaitableStream
	{
		SOCKET sock; //!< Socket handle.
		HANDLE hev; //!< Event for potential data.
		HANDLE hev2; //!< Event for real data.

	public:
		PollStream(const std::string& targetName) :
			Stream(targetName),
			WaitableStream(targetName)
		{
			target.add("tcppoll:host;port;connectionPort=-1;sock=-1");
			target.add(targetName.c_str());
			sock = getOrCreateSocket(target);
			dtorCloseSocket = target.get<int>("sock") < 0 && sock >= 0; // if getOrCreateSocket created the socket we will have to close it

			hev = createEvent(EvPotentialData);
			hev2 = createEvent(EvData);

			int rv = WSAEventSelect(sock, hev, FD_READ | FD_CLOSE);
			if (rv == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot select socket events.");
		}

		~PollStream()
		{
			// if socket belongs to this stream, shut it down
			if (dtorCloseSocket)
			{
				shutdown(sock, SD_BOTH);
				closesocket(sock);
			}
		}

		//! Callback when an event is notified, allowing the stream to rearm it.
		//! \param srv Hub instance that has generated the notification.
		//! \param t Type of event.
		virtual void notifyEvent(Hub* srv, EvType& t)
		{
			if (t == EvPotentialData)
				t = EvData; // trick Hub::step into calling Hub::incomingData, once
		}

		//! Callback when incomingData is called, allowing the stream to rearm its edge trigger.
		//! \param srv Hub instance that has generated the notification.
		//! \param t Type of event.
		virtual void notifyIncomingData(Hub* srv, EvType& t)
		{
			readDone = true; // lie to Hub::step so that it doesn't raise DashelException::PreviousIncomingDataNotRead
		}

		// clang-format off
		virtual void write(const void* data, const size_t size) { /* hook for use by derived classes */ }
		virtual void flush() { /* hook for use by derived classes */ }
		virtual void read(void* data, size_t size) { /* hook for use by derived classes */ }
		// clang-format on

	private:
		bool dtorCloseSocket;
	};

	//! UDP Socket, uses sendto/recvfrom for read/write
	class UDPSocketStream : public MemoryPacketStream, public WaitableStream
	{
	private:
		//! Socket handle.
		SOCKET sock;

		//! Event for real data.
		HANDLE hev;

	public:
		//! Create as UDP socket stream on a specific port
		UDPSocketStream(const std::string& targetName) :
			Stream("udp"),
			MemoryPacketStream("udp"),
			WaitableStream("udp")
		{
			target.add("udp:port=5000;address=0.0.0.0;sock=0");
			target.add(targetName.c_str());

			sock = target.get<SOCKET>("sock");
			if (!sock)
			{
				startWinSock();

				// create socket
				sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				if (sock == SOCKET_ERROR)
					throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot create socket.");

				IPV4Address bindAddress(target.get("address"), target.get<int>("port"));

				// bind
				sockaddr_in addr;
				addr.sin_family = AF_INET;
				addr.sin_port = htons(bindAddress.port);
				addr.sin_addr.s_addr = htonl(bindAddress.address);
				if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
					throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot bind socket to port, probably the port is already in use.");

				// retrieve port number, if a dynamic one was requested
				if (bindAddress.port == 0)
				{
					socklen_t sizeof_addr(sizeof(addr));
					if (getsockname(sock, (struct sockaddr*)&addr, &sizeof_addr) != 0)
						throw DashelException(DashelException::ConnectionFailed, errno, "Cannot retrieve socket port assignment.");
					target.erase("port");
					std::ostringstream portnum;
					portnum << ntohs(addr.sin_port);
					target.addParam("port", portnum.str().c_str(), true);
				}
			}
			else
			{
				// remove file descriptor information from target name
				target.erase("sock");
			}

			// enable broadcast
			int broadcastPermission = 1;
			setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcastPermission, sizeof(broadcastPermission));

			// Create and register event.
			hev = createEvent(EvData);

			int rv = WSAEventSelect(sock, hev, FD_READ);
			if (rv == SOCKET_ERROR)
				throw DashelException(DashelException::ConnectionFailed, WSAGetLastError(), "Cannot select socket events.");
		}

		virtual ~UDPSocketStream()
		{
			closesocket(sock);
		}

		virtual void send(const IPV4Address& dest)
		{
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(dest.port);
			;
			addr.sin_addr.s_addr = htonl(dest.address);

			if (sendto(sock, (const char*)sendBuffer.get(), sendBuffer.size(), 0, (struct sockaddr*)&addr, sizeof(addr)) != sendBuffer.size())
				fail(DashelException::IOError, WSAGetLastError(), "UDP Socket write I/O error.");

			sendBuffer.clear();
		}

		virtual void receive(IPV4Address& source)
		{
			unsigned char buf[4006];
			sockaddr_in addr;
			int addrLen = sizeof(addr);
			readDone = true;

			int recvCount = recvfrom(sock, (char*)buf, 4096, 0, (struct sockaddr*)&addr, &addrLen);
			if (recvCount <= 0)
				fail(DashelException::ConnectionLost, WSAGetLastError(), "UDP Socket read I/O error.");

			receptionBuffer.resize(recvCount);
			std::copy(buf, buf + recvCount, receptionBuffer.begin());

			source = IPV4Address(ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port));
		}
	};

	Hub::Hub(const bool resolveIncomingNames) :
		resolveIncomingNames(resolveIncomingNames)
	{
		hTerminate = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (!hTerminate)
		{
			std::cerr << "Cannot create hTerminate event, error " << GetLastError() << std::endl;
			abort();
		}
		streamsLock = CreateMutex(NULL, FALSE, NULL);
		if (!streamsLock)
		{
			std::cerr << "Cannot create streamsLock mutex, error " << GetLastError() << std::endl;
			abort();
		}
	}

	Hub::~Hub()
	{
		for (StreamsSet::iterator it = streams.begin(); it != streams.end(); ++it)
			delete *it;
		CloseHandle(streamsLock);
	}

	Stream* Hub::connect(const std::string& target)
	{
		std::string proto, params;
		size_t c = target.find_first_of(':');
		if (c == std::string::npos)
			throw DashelException(DashelException::InvalidTarget, 0, "No protocol specified in target.");
		proto = target.substr(0, c);
		params = target.substr(c + 1);

		WaitableStream* s(dynamic_cast<WaitableStream*>(streamTypeRegistry.create(proto, target, *this)));
		if (!s)
		{
			std::string r = "Invalid protocol in target: ";
			r += proto;
			r += ", known protocol are: ";
			r += streamTypeRegistry.list();
			throw DashelException(DashelException::InvalidTarget, 0, r.c_str());
		}

		/* The caller must have the stream lock held */

		streams.insert(s);
		if (proto != "tcpin")
		{
			dataStreams.insert(s);
			connectionCreated(s);
		}
		return s;
	}

	void Hub::run()
	{
		while (step(-1))
			;
	}

	bool Hub::step(const int timeout)
	{
		lock();
		const std::size_t default_hc = std::max(streams.size(), std::size_t(1));

		std::vector<HANDLE> hEvs(default_hc, hTerminate);
		std::vector<EvType> ets(default_hc, EvClosed);
		std::vector<WaitableStream*> strs(default_hc, nullptr);

		// Wait on all our events.
		DWORD ms = timeout >= 0 ? timeout : INFINITE;

		// Loop in order to consume all events, mostly within lock, excepted for wait
		do
		{
			// the first object to be waited on is always the hTerminate
			DWORD hc = 1;

			// Collect all events from all our streams.
			for (std::set<Stream*>::iterator it = streams.begin(); it != streams.end(); ++it)
			{
				WaitableStream* stream = polymorphic_downcast<WaitableStream*>(*it);
				for (std::map<EvType, HANDLE>::iterator ei = stream->hEvents.begin(); ei != stream->hEvents.end(); ++ei)
				{
					if (hEvs.size() <= hc)
					{
						ets.resize(hc + 32, EvClosed);
						strs.resize(hc + 32, nullptr);
						hEvs.resize(hc + 32, hTerminate);
					}
					strs[hc] = stream;
					ets[hc] = ei->first;
					hEvs[hc] = ei->second;
					hc++;
				}
			}

			// Unlock for the wait
			unlock();

			// force finite timeout to check for serial disconnections
			DWORD r = WaitForMultipleObjects(hc, &hEvs.at(0), FALSE, ms == INFINITE ? DEFAULT_WAIT_TIMEOUT : ms);

			// Check for error or timeout.
			if (r == WAIT_FAILED)
				throw DashelException(DashelException::SyncError, 0, "Wait failed.");

			// Relock for manipulating streams and calling callbacks
			lock();

			if (r == WAIT_TIMEOUT)
			{
				for (std::set<Stream*>::iterator it = streams.begin(); it != streams.end(); ++it)
				{
					SerialStream* serialStream = dynamic_cast<SerialStream*>(*it);
					if (serialStream && !serialStream->checkConnection())
					{
						try
						{
							connectionClosed(serialStream, false);
						}
						catch (const DashelException& e)
						{
						}
						closeStream(serialStream);
						break;
					}
				}
				if (ms == INFINITE)
					continue; // hide internal timeout to caller
				unlock();
				return true;
			}

			// Look for what we got.
			r -= WAIT_OBJECT_0;
			if (r == 0)
			{
				// Quit
				ResetEvent(hTerminate);
				unlock();
				return false;
			}
			else
			{
				// Notify the stream that its event arrived.
				strs[r]->notifyEvent(this, ets[r]);

				// Notify user that something happended.
				if (ets[r] == EvData)
				{
					try
					{
						strs[r]->readDone = false;
						strs[r]->notifyIncomingData(this, ets[r]); // Poll streams need to reset their edge triggers
						incomingData(strs[r]);
					}
					catch (const DashelException& e)
					{
					}
					if (!strs[r]->readDone)
					{
						unlock();
						throw DashelException(DashelException::PreviousIncomingDataNotRead, 0, "Previous incoming data not read.", strs[r]);
					}
					if (strs[r]->failed())
					{
						connectionClosed(strs[r], true);
						closeStream(strs[r]);
					}
				}

				if (ets[r] == EvClosed)
				{
					try
					{
						connectionClosed(strs[r], false);
					}
					catch (const DashelException& e)
					{
					}
					closeStream(strs[r]);
				}
			}

			// No more timeouts on following rounds.
			ms = 0;
		} while (true);
	}

	void Hub::lock()
	{
		DWORD waitRet = WaitForSingleObject(streamsLock, INFINITE);
		if (waitRet != WAIT_OBJECT_0)
		{
			std::cerr << "Cannot lock mutex, instead got " << std::hex << waitRet << std::endl;
			abort();
		}
	}

	void Hub::unlock()
	{
		if (!ReleaseMutex(streamsLock))
		{
			std::cerr << "Cannot unlock mutex, error " << GetLastError() << std::endl;
			abort();
		}
	}

	void Hub::stop()
	{
		SetEvent(hTerminate);
	}

	StreamTypeRegistry::StreamTypeRegistry()
	{
		reg("file", &createInstance<FileStream>);
		reg("stdin", &createInstance<StdinStream>);
		reg("stdout", &createInstance<StdoutStream>);
		reg("ser", &createInstance<SerialStream>);
		reg("tcpin", &createInstanceWithHub<SocketServerStream>);
		reg("tcp", &createInstance<SocketStream>);
		reg("tcppoll", &createInstance<PollStream>);
		reg("udp", &createInstance<UDPSocketStream>);
	}

	StreamTypeRegistry streamTypeRegistry;
}
