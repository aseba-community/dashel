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

#include <string.h>
#include <cassert>
#include <cstdlib>
#include <map>
#include <vector>
#include <valarray>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
// TODO: add support for OS X serial port enumeration
#include <hal/libhal.h>

#include "dashel-private.h"

#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif


/*!	\file streams.cpp
	\brief Implementation of DaSHEL, A cross-platform DAta Stream Helper Encapsulation Library
*/

namespace Dashel
{
	using namespace std;
	
	// Exception
	
	DashelException::DashelException(Source s, int se, const char *reason, Stream* stream) :
		source(s), sysError(se), reason(reason), stream(stream)
	{
		if (se)
			sysMessage = strerror(se);
	}
	
	// Serial port enumerator
	
	std::map<int, std::pair<std::string, std::string> > SerialPortEnumerator::getPorts()
	{
		std::map<int, std::pair<std::string, std::string> > ports;
		
		// use HAL to enumerates devices
		DBusConnection* dbusConnection = dbus_bus_get(DBUS_BUS_SYSTEM, 0);
		if (!dbusConnection)
			throw DashelException(DashelException::EnumerationError, 0, "cannot connect to D-BUS.");
		
		LibHalContext* halContext = libhal_ctx_new();
		if (!halContext)
			throw DashelException(DashelException::EnumerationError, 0, "cannot create HAL context: cannot create context");
		if (!libhal_ctx_set_dbus_connection(halContext, dbusConnection))
			throw DashelException(DashelException::EnumerationError, 0, "cannot create HAL context: cannot connect to D-BUS");
		if (!libhal_ctx_init(halContext, 0))
			throw DashelException(DashelException::EnumerationError, 0, "cannot create HAL context: cannot init context");
		
		int devicesCount;
		char** devices = libhal_find_device_by_capability(halContext, "serial", &devicesCount, 0);
		for (int i = 0; i < devicesCount; i++)
		{
			char* devFileName = libhal_device_get_property_string(halContext, devices[i], "serial.device", 0);
			char* info = libhal_device_get_property_string(halContext, devices[i], "info.product", 0);
			int port = libhal_device_get_property_int(halContext, devices[i], "serial.port", 0);
			
			ostringstream oss;
			oss << info << " " << port;
			ports[devicesCount - i] = std::make_pair<std::string, std::string>(devFileName, oss.str());
			
			libhal_free_string(info);
			libhal_free_string(devFileName);
		}
		
		libhal_free_string_array(devices);
		libhal_ctx_shutdown(halContext, 0);
		libhal_ctx_free(halContext);
		
		return ports;
	};
	
	// Asserted dynamic cast
	
	//! Asserts a dynamic cast.	Similar to the one in boost/cast.hpp
	template<typename Derived, typename Base>
	inline Derived polymorphic_downcast(Base base)
	{
		Derived derived = dynamic_cast<Derived>(base);
		assert(derived);
		return derived;
	}
	
	// Streams
	
	void Stream::fail(DashelException::Source s, int se, const char* reason)
	{
		string sysMessage;
		failedFlag = true;
		if (se)
			sysMessage = strerror(errno);
		failReason += reason;
		failReason += " ";
		failReason += sysMessage;
		throw DashelException(s, se, reason, this);
	}

	//! Stream with a file descriptor that is selectable
	class SelectableStream: public Stream
	{
	protected:
		int fd; //!< associated file descriptor
		bool writeOnly;	//!< true if we can only write on this stream
		friend class Hub;
	
	public:
		//! Create the stream and associates a file descriptor
		SelectableStream(const string& targetName): Stream(targetName), fd(-1), writeOnly(false) { }
		
		virtual ~SelectableStream()
		{
			if (fd >= 0)
				close(fd);
		}
	};
	
	//! Socket, uses send/recv for read/write
	class SocketStream: public SelectableStream
	{
	protected:
		#ifndef TCP_CORK
		//! Socket constants
		enum Consts
		{
			SEND_BUFFER_SIZE_INITIAL = 256, //!< initial size of the socket send buffer
			SEND_BUFFER_SIZE_LIMIT = 65536 //!< when the socket send buffer reaches this size, a flush is forced
		};
		
		unsigned char *buffer; //!< send buffer. Its size is increased when required. On flush, bufferPos is set to zero and bufferSize rest unchanged. It is freed on SocketStream destruction.
		size_t bufferSize; //!< size of send buffer
		size_t bufferPos; //!< actual position in send buffer
		#endif
		
	public:
		//! Create a socket stream to the following destination
		SocketStream(const string& targetName) :
			SelectableStream(targetName)
		{
			ParameterSet ps;
			ps.add("tcp:host;port;sock=-1");
			ps.add(targetName.c_str());

			fd = ps.get<int>("sock");
			if (fd < 0)
			{
				// create socket
				fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (fd < 0)
					throw DashelException(DashelException::ConnectionFailed, errno, "Cannot create socket.");
				
				TCPIPV4Address remoteAddress(ps.get("host"), ps.get<int>("port"));
				
				// connect
				sockaddr_in addr;
				addr.sin_family = AF_INET;
				addr.sin_port = htons(remoteAddress.port);
				addr.sin_addr.s_addr = htonl(remoteAddress.address);
				if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
					throw DashelException(DashelException::ConnectionFailed, errno, "Cannot connect to remote host.");
				
				// overwrite target name with a canonical one
				this->targetName = remoteAddress.format();
			}
			else
			{
				// remove file descriptor information from target name
				this->targetName.erase(this->targetName.rfind(";sock="));
			}
			
			// setup TCP Cork or create buffer for delayed sending
			#ifndef TCP_CORK
			bufferSize = SEND_BUFFER_SIZE_INITIAL;
			buffer = (unsigned char*)malloc(bufferSize);
			bufferPos = 0;
			#else
			int flag = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_CORK, &flag , sizeof(flag));
			#endif
		}
		
		virtual ~SocketStream()
		{
			if (fd >= 0)
				shutdown(fd, SHUT_RDWR);
			
			#ifndef TCP_CORK
			free(buffer);
			#endif
		}
		
		virtual void write(const void *data, const size_t size)
		{
			assert(fd >= 0);
			
			#ifdef TCP_CORK
			send(data, size);
			#else
			if (size >= SEND_BUFFER_SIZE_LIMIT)
			{
				flush();
				send(data, size);
			}
			else
			{
				if (bufferPos + size > bufferSize)
				{
					bufferSize = max(bufferSize * 2, bufferPos + size);
					buffer = (unsigned char*)realloc(buffer, bufferSize);
				}
				memcpy(buffer + bufferPos, (unsigned char *)data, size);
				bufferPos += size;
		
				if (bufferPos >= SEND_BUFFER_SIZE_LIMIT)
					flush();
			}
			#endif
		}
		
		//! Send all data over the socket
		void send(const void *data, size_t size)
		{
			assert(fd >= 0);
			
			unsigned char *ptr = (unsigned char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = ::send(fd, ptr, left, MSG_NOSIGNAL);
				
				if (len < 0)
				{
					fail(DashelException::IOError, errno, "Socket write I/O error.");
				}
				else if (len == 0)
				{
					fail(DashelException::ConnectionLost, 0, "Connection lost.");
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
			assert(fd >= 0);
			
			#ifdef TCP_CORK
			int flag = 0;
			setsockopt(fd, IPPROTO_TCP, TCP_CORK, &flag , sizeof(flag));
			flag = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_CORK, &flag , sizeof(flag));
			#else
			send(buffer, bufferPos);
			bufferPos = 0;
			#endif
		}
		
		virtual void read(void *data, size_t size)
		{
			assert(fd >= 0);
			
			unsigned char *ptr = (unsigned char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = recv(fd, ptr, left, 0);
				
				if (len < 0)
				{
					fail(DashelException::IOError, errno, "Socket read I/O error.");
				}
				else if (len == 0)
				{
					fail(DashelException::ConnectionLost, 0, "Connection lost.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}
	};
	
	//! Socket server stream.
	/*! This stream is used for listening for incoming connections. It cannot be used for transfering
		data.
	*/
	class SocketServerStream : public SelectableStream
	{
	public:
		//! Create the stream and associates a file descriptor
		SocketServerStream(const std::string& targetName) :
			SelectableStream(targetName)
		{
			ParameterSet ps;
			ps.add("tcpin:host=0.0.0.0;port");
			ps.add(targetName.c_str());
			
			TCPIPV4Address bindAddress(ps.get("host"), ps.get<int>("port"));
			
			// create socket
			fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (fd < 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot create socket.");
			
			// reuse address
			int flag = 1;
			if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof (flag)) < 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot set address reuse flag on socket, probably the port is already in use.");
			
			// bind
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(bindAddress.port);
			addr.sin_addr.s_addr = htonl(bindAddress.address);
			if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot bind socket to port, probably the port is already in use.");
			
			// Listen on socket, backlog is sort of arbitrary.
			if(listen(fd, 16) < 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot listen on socket.");
		}
		
		virtual void write(const void *data, const size_t size) { }
		virtual void flush() { }
		virtual void read(void *data, size_t size) { }
	};
	
	
	//! File descriptor, uses send/recv for read/write
	class FileDescriptorStream: public SelectableStream
	{
	public:
		//! Create the stream and associates a file descriptor
		FileDescriptorStream(const string& targetName) :
			SelectableStream(targetName)
		{ }
		
		virtual void write(const void *data, const size_t size)
		{
			assert(fd >= 0);
			
			const char *ptr = (const char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = ::write(fd, ptr, left);
				
				if (len < 0)
				{
					fail(DashelException::IOError, errno, "File write I/O error.");
				}
				else if (len == 0)
				{
					fail(DashelException::ConnectionLost, 0, "File full.");
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
			assert(fd >= 0);
			
			if (fdatasync(fd) < 0)
			{
				fail(DashelException::IOError, errno, "File flush error.");
			}
		}
		
		virtual void read(void *data, size_t size)
		{
			assert(fd >= 0);
			
			char *ptr = (char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = ::read(fd, ptr, left);
				
				if (len < 0)
				{
					fail(DashelException::IOError, errno, "File read I/O error.");
				}
				else if (len == 0)
				{
					fail(DashelException::ConnectionLost, 0, "Reached end of file.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}
	};
	
	//! Stream for file
	class FileStream: public FileDescriptorStream
	{
	public:
		//! Parse the target name and create the corresponding file stream
		FileStream(const string& targetName) :
			FileDescriptorStream(targetName)
		{
			ParameterSet ps;
			ps.add("file:name;mode=read");
			ps.add(targetName.c_str());
			std::string name = ps.get("name");
			std::string mode = ps.get("mode");
			
			// open file
			if (mode == "read")
				fd = open(name.c_str(), O_RDONLY);
			else if (mode == "write")
				fd = creat(name.c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP), writeOnly = true;
			else if (mode == "readwrite")
				fd = open(name.c_str(), O_RDWR);
			else
 				throw DashelException(DashelException::InvalidTarget, 0, "Invalid file mode.");
			
			if (fd == -1)
			{
				string errorMessage = "Cannot open file " + name + " for " + mode + ".";
				throw DashelException(DashelException::ConnectionFailed, errno, errorMessage.c_str());
			}
		}
	};
	
	//! Stream for serial port, in addition to FileDescriptorStream, save old state of serial port
	class SerialStream: public FileDescriptorStream
	{
	protected:
		struct termios oldtio;	//!< old serial port state
		
	public:
		//! Parse the target name and create the corresponding serial stream
		SerialStream(const string& targetName) :
			FileDescriptorStream(targetName)
		{
			ParameterSet ps;
			ps.add("ser:port=1;baud=115200;stop=1;parity=none;fc=none;bits=8");
			ps.add(targetName.c_str());
			string devFileName;
			
			if (ps.isSet("device"))
			{
				devFileName = ps.get("device");
			}
			else
			{
				std::map<int, std::pair<std::string, std::string> > ports = SerialPortEnumerator::getPorts();
				std::map<int, std::pair<std::string, std::string> >::const_iterator it = ports.find(ps.get<int>("port"));
				if (it != ports.end())
				{
					devFileName = it->first;
				}
				else
					throw DashelException(DashelException::ConnectionFailed, 0, "The specified serial port does not exists.");
			}
		
			fd = open(devFileName.c_str(), O_RDWR);
			
			if (fd == -1)
				throw DashelException(DashelException::ConnectionFailed, 0, "Cannot open serial port.");
			
			struct termios newtio;
			
			// save old serial port state and clear new one
			tcgetattr(fd, &oldtio);
			memset(&newtio, 0, sizeof(newtio));
			
			newtio.c_cflag |= CLOCAL;			// ignore modem control lines.
			newtio.c_cflag |= CREAD;			// enable receiver.
			switch (ps.get<int>("bits"))		// Set amount of bits per character
			{
				case 5: newtio.c_cflag |= CS5; break;
				case 6: newtio.c_cflag |= CS6; break;
				case 7: newtio.c_cflag |= CS7; break;
				case 8: newtio.c_cflag |= CS8; break;
				default: throw DashelException(DashelException::InvalidTarget, 0, "Invalid number of bits per character, must be 5, 6, 7, or 8.");
			}
			if (ps.get("stop") == "2")
				newtio.c_cflag |= CSTOPB;		// Set two stop bits, rather than one.
			if (ps.get("fc") == "hard")
				newtio.c_cflag |= CRTSCTS;		// enable hardware flow control
			if (ps.get("parity") != "none")
			{
				newtio.c_cflag |= PARENB;		// enable parity generation on output and parity checking for input.
				if (ps.get("parity") == "odd")
					newtio.c_cflag |= PARODD;	// parity for input and output is odd.
			}
			switch (ps.get<int>("baud"))
			{
				case 50: newtio.c_cflag |= B50; break;
				case 75: newtio.c_cflag |= B75; break;
				case 110: newtio.c_cflag |= B110; break;
				case 134: newtio.c_cflag |= B134; break;
				case 150: newtio.c_cflag |= B150; break;
				case 200: newtio.c_cflag |= B200; break;
				case 300: newtio.c_cflag |= B300; break;
				case 600: newtio.c_cflag |= B600; break;
				case 1200: newtio.c_cflag |= B1200; break;
				case 1800: newtio.c_cflag |= B1800; break;
				case 2400: newtio.c_cflag |= B2400; break;
				case 4800: newtio.c_cflag |= B4800; break;
				case 9600: newtio.c_cflag |= B9600; break;
				case 19200: newtio.c_cflag |= B19200; break;
				case 38400: newtio.c_cflag |= B38400; break;
				case 57600: newtio.c_cflag |= B57600; break;
				case 115200: newtio.c_cflag |= B115200; break;
				case 230400: newtio.c_cflag |= B230400; break;
				case 460800: newtio.c_cflag |= B460800; break;
				case 500000: newtio.c_cflag |= B500000; break;
				case 576000: newtio.c_cflag |= B576000; break;
				case 921600: newtio.c_cflag |= B921600; break;
				case 1000000: newtio.c_cflag |= B1000000; break;
				case 1152000: newtio.c_cflag |= B1152000; break;
				case 1500000: newtio.c_cflag |= B1500000; break;
				case 2000000: newtio.c_cflag |= B2000000; break;
				case 2500000: newtio.c_cflag |= B2500000; break;
				case 3000000: newtio.c_cflag |= B3000000; break;
				case 3500000: newtio.c_cflag |= B3500000; break;
				case 4000000: newtio.c_cflag |= B4000000; break;
				default: throw DashelException(DashelException::ConnectionFailed, 0, "Invalid baud rate.");
			}
			
			newtio.c_iflag = IGNPAR;			// ignore parity on input
			
			newtio.c_oflag = 0;
			
			newtio.c_lflag = 0;
			
			newtio.c_cc[VTIME] = 0;				// block forever if no byte
			newtio.c_cc[VMIN] = 1;				// one byte is sufficient to return
			
			// set attributes
			if ((tcflush(fd, TCIFLUSH) < 0) || (tcsetattr(fd, TCSANOW, &newtio) < 0))
				throw DashelException(DashelException::ConnectionFailed, 0, "Cannot setup serial port. The requested baud rate might not be supported.");
		}
		
		//! Destructor, restore old serial port state
		virtual ~SerialStream()
		{
			 tcsetattr(fd, TCSANOW, &oldtio);
		}
	};
	
	// Signal handler for SIGTERM
	
	//! Global variables to signal the continuous run must terminates.
	bool runTerminationReceived = false;
	
	//! Called when SIGTERM arrives, halts all running clients or servers in all threads
	void termHandler(int t)
	{
		runTerminationReceived = true;
	}
	
	//! Class to setup SIGTERM handler
	class SigTermHandlerSetuper
	{
	public:
		//! Private constructor that redirects SIGTERM
		SigTermHandlerSetuper()
		{
			signal(SIGTERM, termHandler);
		}
	} staticSigTermHandlerSetuper;
	// TODO: check if this works in real life
	
	// Hub
	
	Hub::Hub()
	{
		hTerminate = (void*)0;
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
		if (c == std::string::npos)
			throw DashelException(DashelException::InvalidTarget, 0, "No protocol specified in target.");
		proto = target.substr(0, c);
		params = target.substr(c+1);

		SelectableStream *s = NULL;
		if(proto == "file")
			s = new FileStream(target);
		if(proto == "stdin")
			s = new FileStream("file:/dev/stdin;read");
		if(proto == "stdout")
			s = new FileStream("file:/dev/stdout;write");
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
		
		incomingConnection(s);
		
		streams.insert(s);
		if (proto != "tcpin")
			dataStreams.insert(s);
		
		return s;
	}
	
	Stream* Hub::removeStream(Stream* stream)
	{
		streams.erase(stream);
		dataStreams.erase(stream);
		return stream;
	}
	
	void Hub::run(void)
	{
		runTerminationReceived = false;
		while (!runTerminationReceived && (hTerminate == (void*)0))
			step(-1);
	}
	
	bool Hub::step(int timeout)
	{
		size_t streamsCount = streams.size();
		valarray<struct pollfd> pollFdsArray(streamsCount);
		valarray<SelectableStream*> streamsArray(streamsCount);
		
		// add streams
		size_t i = 0;
		for (StreamsSet::iterator it = streams.begin(); it != streams.end(); ++it)
		{
			SelectableStream* stream = polymorphic_downcast<SelectableStream*>(*it);
			
			streamsArray[i] = stream;
			pollFdsArray[i].fd = stream->fd;
			pollFdsArray[i].events = POLLRDHUP;
			if (!stream->writeOnly)
				pollFdsArray[i].events |= POLLIN;
			i++;
		}
		
		// do poll and check for error
		int ret = poll(&pollFdsArray[0], streamsCount, timeout);
		if (ret < 0)
			throw DashelException(DashelException::SyncError, errno, "Error during poll.");
		
		// check streams for errors
		for (i = 0; i < streamsCount; i++)
		{
			SelectableStream* stream = streamsArray[i];
			
			// make sure we do not try to handle removed streams
			if (streams.find(stream) == streams.end())
				continue;
			
			assert((pollFdsArray[i].revents & POLLNVAL) == 0);
			
			if (pollFdsArray[i].revents & (POLLERR | POLLHUP | POLLRDHUP))
			{
				try
				{
					if (pollFdsArray[i].revents & POLLERR)
					{
						stream->fail(DashelException::SyncError, 0, "Error on stream during poll.");
						connectionClosed(stream, true);
					}
					else
						connectionClosed(stream, false);
				}
				catch (DashelException e)
				{
					assert(e.stream);
				}
				
				delete removeStream(stream);
			}
			else if (pollFdsArray[i].revents & POLLIN)
			{
				// test if listen stream
				SocketServerStream* serverStream = dynamic_cast<SocketServerStream*>(stream);
				
				if (serverStream)
				{
					// accept connection
					struct sockaddr_in targetAddr;
					socklen_t l = sizeof (targetAddr);
					int targetFD = accept (stream->fd, (struct sockaddr *)&targetAddr, &l);
					if (targetFD < 0)
						throw DashelException(DashelException::SyncError, errno, "Cannot accept new stream.");
					
					// create a target stream using the new file descriptor from accept
					ostringstream targetName;
					targetName << TCPIPV4Address(ntohl(targetAddr.sin_addr.s_addr), ntohs(targetAddr.sin_port)).format();
					targetName << ";sock=";
					targetName << targetFD;
					connect(targetName.str());
				}
				else
				{
					try
					{
						incomingData(stream);
					}
					catch (DashelException e)
					{
						assert(e.stream);
					}
				}
			}
		}
		
		// remove all failed streams
		for (StreamsSet::iterator it = streams.begin(); it != streams.end();)
		{
			Stream* stream = *it;
			++it;
			if (stream->failed())
			{
				connectionClosed(stream, true);
				
				delete removeStream(stream);
			}
		}
	}
	
	void Hub::stop()
	{
		hTerminate = (void*)1;
	}
}
