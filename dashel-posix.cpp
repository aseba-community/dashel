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

#include "dashel-private.h"

#include <cassert>
#include <cstdlib>
#include <map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
// TODO: add support for OS X serial port enumeration
#include <hal/libhal.h>


/*!	\file streams.cpp
	\brief Implementation of DaSHEL, A cross-platform DAta Stream Helper Encapsulation Library
*/

namespace Dashel
{
	using namespace std;
	
	std::map<int, std::pair<std::string, std::string> > SerialPortEnumerator::getPorts()
	{
		std::map<int, std::pair<std::string, std::string> > ports;
		
		// use HAL to enumerates devices
		DBusConnection* dbusConnection = dbus_bus_get(DBUS_BUS_SYSTEM, 0);
		if (!dbusConnection)
			throw EnumerationException("cannot connect to D-BUS.");
		
		LibHalContext* halContext = libhal_ctx_new();
		if (!halContext)
			throw EnumerationException("cannot create HAL context: cannot create context");
		if (!libhal_ctx_set_dbus_connection(halContext, dbusConnection))
			throw EnumerationException("cannot create HAL context: cannot connect to D-BUS");
		if (!libhal_ctx_init(halContext, 0))
			throw EnumerationException("cannot create HAL context: cannot init context");
		
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
	
	//! Asserts a dynamic cast.	Similar to the one in boost/cast.hpp
	template<typename Derived, typename Base>
	inline Derived polymorphic_downcast(Base base)
	{
		Derived derived = dynamic_cast<Derived>(base);
		assert(derived);
		return derived;
	}
	
	//! Global variables to signal the continuous run must terminates.
	bool runTerminationReceived = false;

	//! Stream with a file descriptor that is selectable
	class SelectableStream: public Stream
	{
	protected:
		int fd; //!< associated file descriptor
	
	public:
		//! Create the stream and associates a file descriptor
		SelectableStream(const string& targetName): Stream(targetName), fd(-1) { }
		
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
		SocketStream(const string& targetName) :
			SelectableStream(targetName)
		{
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
			if (fd < 0)
				throw StreamException(StreamException::ConnectionLost, 0, this, "Invalid file descriptor.");
			
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
				ssize_t len = ::send(fd, ptr, left, 0);
				
				if (len < 0)
				{
					close(fd);
					fd = -1;
					throw StreamException(StreamException::IOError, errno, this, "Socket write I/O error.");
				}
				else if (len == 0)
				{
					close(fd);
					fd = -1;
					throw StreamException(StreamException::ConnectionLost, 0, this, "Connection lost.");
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
			if (fd < 0)
				throw StreamException(StreamException::ConnectionLost, 0, this, "Invalid file descriptor.");
			
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
			if (fd < 0)
				throw StreamException(StreamException::ConnectionLost, 0, this, "Invalid file descriptor.");
			
			unsigned char *ptr = (unsigned char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = recv(fd, ptr, left, 0);
				
				if (len < 0)
				{
					close(fd);
					fd = -1;
					throw StreamException(StreamException::IOError, errno, this, "Socket read I/O error.");
				}
				else if (len == 0)
				{
					close(fd);
					fd = -1;
					throw StreamException(StreamException::ConnectionLost, 0, this, "Connection lost.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}
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
			if (fd < 0)
				throw StreamException(StreamException::ConnectionLost, 0, this, "Invalid file descriptor.");
			
			const char *ptr = (const char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = ::write(fd, ptr, left);
				
				if (len < 0)
				{
					close(fd);
					fd = -1;
					throw StreamException(StreamException::IOError, errno, this, "File write I/O error.");
				}
				else if (len == 0)
				{
					close(fd);
					fd = -1;
					throw StreamException(StreamException::ConnectionLost, 0, this, "File full.");
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
			if (fd < 0)
				throw StreamException(StreamException::ConnectionLost, 0, this, "Invalid file descriptor.");
			
			fdatasync(fd);
		}
		
		virtual void read(void *data, size_t size)
		{
			if (fd < 0)
				throw StreamException(StreamException::ConnectionLost, 0, this, "Invalid file descriptor.");
		
			char *ptr = (char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = ::read(fd, ptr, left);
				
				if (len < 0)
				{
					close(fd);
					fd = -1;
					throw StreamException(StreamException::IOError, errno, this, "File read I/O error.");
				}
				else if (len == 0)
				{
					close(fd);
					fd = -1;
					throw StreamException(StreamException::ConnectionLost, 0, this, "Reached end of file.");
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
			ps.add("file:mode=read");
			ps.add(targetName.c_str());
			std::string name = ps.get("name");
			std::string mode = ps.get("mode");
			
			// open file
			if (mode == "read")
				fd = open(name.c_str(), O_RDONLY);
			else if (mode == "write")
				fd = creat(name.c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
			else if (mode == "readwrite")
				fd = open(name.c_str(), O_RDWR);
			else
 				throw StreamException(StreamException::InvalidTarget, 0, NULL, "Invalid file mode.");
			
			if (fd == -1)
				throw StreamException(StreamException::ConnectionFailed, errno, NULL, "Cannot open file.");
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
					throw StreamException(StreamException::ConnectionFailed, 0, NULL, "The specified serial port does not exists.");
			}
		
			fd = open(devFileName.c_str(), O_RDWR);
			
			if (fd == -1)
				throw StreamException(StreamException::ConnectionFailed, 0, NULL, "Cannot open serial port.");
			
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
				default: throw StreamException(StreamException::InvalidTarget, 0, NULL, "Invalid number of bits per character, must be 5, 6, 7, or 8.");
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
				default: throw StreamException(StreamException::ConnectionFailed, 0, NULL, "Invalid baud rate.");
			}
			
			newtio.c_iflag = IGNPAR;			// ignore parity on input
			
			newtio.c_oflag = 0;
			
			newtio.c_lflag = 0;
			
			newtio.c_cc[VTIME] = 0;				// block forever if no byte
			newtio.c_cc[VMIN] = 1;				// one byte is sufficient to return
			
			// set attributes
			if ((tcflush(fd, TCIFLUSH) < 0) || (tcsetattr(fd, TCSANOW, &newtio) < 0))
				throw StreamException(StreamException::ConnectionFailed, 0, NULL, "Cannot setup serial port. The requested baud rate might not be supported.");
		}
		
		//! Destructor, restore old serial port state
		virtual ~SerialStream()
		{
			 tcsetattr(fd, TCSANOW, &oldtio);
		}
	};
	
	// TODO: zzz: continue from here
	
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
	
	
	// TODO: do this
	{
		
		/**
			Requests the creation of a stream for a target
			
			\param isListen if pointed value is true, try to create a listen stream. Change the value to false if the listen stream creation was not possible and a normal data stream was returned instead. If 0, creates a data stream
		*/
		Stream* createStream(bool* isListen = 0)
		{
			if (type == "file")
			{
				if (isListen)
					*isListen = false;
				return createFileStream();
			}
			else if (type == "tcp")
			{
				// get parameters
				const string& host = parameters->getParameterForced("host")->value;
				unsigned short port = atoi(parameters->getParameterForced("port")->value.c_str());
				
				// create stream
				if (!isListen || (*isListen == false))
					return createTCPDataStream(host, port);
				else
					return createTCPListenStream(host, port);
			}
			else if (type == "ser")
			{
				if (isListen)
					*isListen = false;
				return createSerialStream();
			}
			else
				throw InvalidTargetDescription(target);
		}
		
	protected:
		//! Returns the name of this host
		string getHostName()
		{
			char hostName[256];
			gethostname(hostName, 256);
			hostName[255] = 0;
			return string(hostName);
		}
		
		/**
			Creates a listen stream for incoming connections.
			
			\param host target host name
			\param port target port
		*/
		Stream* createTCPListenStream(const std::string& host, unsigned short port)
		{
			TCPIPV4Address bindAddress(host, port);
			
			// create socket
			int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (fd < 0)
				throw ConnectionError(target, "cannot create socket");
			
			// reuse address
			int flag = 1;
			if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof (flag)) < 0)
				throw ConnectionError(target, "target already in use");
			
			// bind
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(bindAddress.port);
			addr.sin_addr.s_addr = htonl(bindAddress.address);
			if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
				throw ConnectionError(target, "cannot bind target");
			
			// listen
			listen(fd, 16); // backlog of 16 is a pure blind guess
			return new SocketStream(target, fd);
		}
		
		/**
			Creates a data stream.
			
			\param host target host name
			\param port target port
		*/
		Stream* createTCPDataStream(const std::string& host, unsigned short port)
		{
			TCPIPV4Address remoteAddress(host, port);
			
			// create socket
			int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (fd < 0)
				throw ConnectionError(target, "cannot create socket");
			
			// connect
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(remoteAddress.port);
			addr.sin_addr.s_addr = htonl(remoteAddress.address);
			if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
				throw ConnectionError(target, "cannot connect to target");
			
			return new SocketStream(target, fd);
		}
		
		//! Creates a serial port stream.
		Stream* createSerialStream()
		{
			
		}
		
	
	Client::Client(const std::string &target) :
		stream(TargetNameParser(target).createStream()),
		isRunning(false)
	{
		
	}
	
	Client::~Client()
	{
		delete stream;
	}
	
	void Client::run()
	{
		runTerminationReceived = false;
		isRunning = true;
		while (!runTerminationReceived && isRunning)
			step(-1);
	}
	
	bool Client::step(int timeout)
	{
		// locally overload the object by a pointer to its physical class instead of its interface
		SelectableStream* stream = polymorphic_downcast<SelectableStream*>(this->stream);
		if (stream->fd < 0)
			return false;
		
		// setup file descriptor sets
		fd_set rfds, efds;
		int nfds = stream->fd;
		FD_ZERO(&rfds);
		FD_ZERO(&efds);
		FD_SET(stream->fd, &rfds);
		FD_SET(stream->fd, &efds);
		
		// do select
		int ret;
		if (timeout < 0)
		{
			ret = select(nfds+1, &rfds, NULL, &efds, NULL);
		}
		else
		{
			struct timeval t;
			t.tv_sec = 0;
			t.tv_usec = timeout;
			ret = select(nfds+1, &rfds, NULL, &efds, &t);
		}
		
		// check for error
		if (ret < 0)
			throw SynchronizationError();
		
		// check if data is available. If so, get it
		if (FD_ISSET(stream->fd, &efds))
		{
			connectionClosed(stream);
			isRunning = false;
		}
		else if (FD_ISSET(stream->fd, &rfds))
		{
			try
			{
				incomingData(stream);
			}
			catch (StreamException e)
			{
				connectionClosed(stream);
				isRunning = false;
			}
			return true;
		}
		else
			return false;
	}
	
	
	Server::~Server()
	{
		for (list<Stream*>::iterator it = listenStreams.begin(); it != listenStreams.end(); ++it)
			delete *it;
		for (list<Stream*>::iterator it = transferStreams.begin(); it != transferStreams.end(); ++it)
			delete *it;
	}
	
	void Server::listen(const std::string &target)
	{
		bool isListen = true;
		Stream* stream = TargetNameParser(target).createStream(&isListen);
		if (isListen)
		{
			listenStreams.push_back(stream);
		}
		else
		{
			transferStreams.push_back(stream);
			incomingConnection(stream);
		}
	}
	
	void Server::run(void)
	{
		runTerminationReceived = false;
		while (!runTerminationReceived)
			step(-1);
	}
	
	bool Server::step(int timeout)
	{
		fd_set rfds, efds;
		int nfds = 0;
		FD_ZERO(&rfds);
		FD_ZERO(&efds);
	
		// add listen streams
		for (list<Stream*>::iterator it = listenStreams.begin(); it != listenStreams.end(); ++it)
		{
			SelectableStream* stream = polymorphic_downcast<SelectableStream*>(*it);
			FD_SET(stream->fd, &rfds);
			FD_SET(stream->fd, &efds);
			nfds = max(stream->fd, nfds);
		}
		
		// add transfer streams
		for (list<Stream*>::iterator it = transferStreams.begin(); it != transferStreams.end(); ++it)
		{
			SelectableStream* stream = polymorphic_downcast<SelectableStream*>(*it);
			FD_SET(stream->fd, &rfds);
			FD_SET(stream->fd, &efds);
			nfds = max(stream->fd, nfds);
		}
		
		// do select
		int ret;
		if (timeout < 0)
		{
			ret = select(nfds+1, &rfds, NULL, &efds, NULL);
		}
		else
		{
			struct timeval t;
			t.tv_sec = 0;
			t.tv_usec = timeout;
			ret = select(nfds+1, &rfds, NULL, &efds, &t);
		}
		
		// check for error
		if (ret < 0)
			throw SynchronizationError();
		
		// check transfer streams
		for (list<Stream*>::iterator it = transferStreams.begin(); it != transferStreams.end();)
		{
			SelectableStream* stream = polymorphic_downcast<SelectableStream*>(*it);
			++it;
			
			if (FD_ISSET(stream->fd, &efds))
			{
				connectionClosed(stream);
				transferStreams.remove(stream);
				delete stream;
			}
			else if (FD_ISSET(stream->fd, &rfds))
			{
				try
				{
					incomingData(stream);
				}
				catch (StreamException e)
				{
					// make sure we do not handle a stream which has produced an exception
					if ((it != transferStreams.end()) && (*it == e.stream))
						++it;
					connectionClosed(e.stream);
					transferStreams.remove(e.stream);
					delete e.stream;
				}
			}
		}
		
		// check listen streams
		for (list<Stream*>::iterator it = listenStreams.begin(); it != listenStreams.end();)
		{
			SelectableStream* stream = polymorphic_downcast<SelectableStream*>(*it);
			++it;
			
			if (FD_ISSET(stream->fd, &efds))
			{
				listenStreams.remove(stream);
				delete stream;
			}
			else if (FD_ISSET(stream->fd, &rfds))
			{
				// accept connection
				struct sockaddr_in targetAddr;
				socklen_t l = sizeof (targetAddr);
				int targetFD = accept (stream->fd, (struct sockaddr *)&targetAddr, &l);
				if (targetFD < 0)
					throw SynchronizationError();
				
				// create stream
				const string& targetName = TCPIPV4Address(ntohl(targetAddr.sin_addr.s_addr), ntohs(targetAddr.sin_port)).format();
				SocketStream* socketStream = new SocketStream(targetName, targetFD);
				transferStreams.push_back(socketStream);
				
				// notify application
				try
				{
					incomingConnection(socketStream);
				}
				catch (StreamException e)
				{
					connectionClosed(e.stream);
					transferStreams.remove(e.stream);
					delete e.stream;
				}
			}
		}
	}
	
	void Server::stop()
	{
		runTerminationReceived = true;
	}
}
