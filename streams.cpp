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

#include "streams.h"

#include <cassert>
#include <cstdlib>
#include <map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <sstream>

#ifndef WIN32
	#include <unistd.h>
	#include <termios.h>
	#include <fcntl.h>
	#include <netdb.h>
	#include <signal.h>
	#include <arpa/inet.h>
	#include <sys/select.h>
	#include <sys/time.h>
	#include <sys/types.h>
	#include <sys/types.h>
	#include <sys/stat.h>
#else
	#include <winsock2.h>
#endif


/*!	\file streams.cpp
	\brief Implementation of DaSHEL, A cross-platform DAta Stream Helper Encapsulation Library
*/

namespace Streams
{
	using namespace std;
	
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

	#ifndef WIN32
	
	//! Stream with a file descriptor that is selectable
	class SelectableStream: public Stream
	{
	public:
		string targetName; //!< name of the target
		int fd; //!< associated file descriptor
		
		//! Create the stream and associates a file descriptor
		SelectableStream(const string& targetName, int fd): targetName(targetName), fd(fd) { }
		
		virtual ~SelectableStream()
		{
			if (fd >= 0)
				close(fd);
		}
		
		virtual std::string getTargetName() const { return targetName; }
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
		SocketStream(const string& targetName, int fd) :
			SelectableStream(targetName, fd)
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
				throw ConnectionClosed(this);
			
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
					throw InputOutputError(this);
				}
				else if (len == 0)
				{
					close(fd);
					fd = -1;
					throw ConnectionClosed(this);
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
				throw ConnectionClosed(this);
			
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
				throw ConnectionClosed(this);
			
			unsigned char *ptr = (unsigned char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = recv(fd, ptr, left, 0);
				
				if (len < 0)
				{
					close(fd);
					fd = -1;
					throw InputOutputError(this);
				}
				else if (len == 0)
				{
					close(fd);
					fd = -1;
					throw ConnectionClosed(this);
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
		FileDescriptorStream(const string& targetName, int fd) :
			SelectableStream(targetName, fd)
		{ }
		
		virtual void write(const void *data, const size_t size)
		{
			if (fd < 0)
				throw ConnectionClosed(this);
			
			const char *ptr = (const char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = ::write(fd, ptr, left);
				
				if (len < 0)
				{
					close(fd);
					fd = -1;
					throw InputOutputError(this);
				}
				else if (len == 0)
				{
					close(fd);
					fd = -1;
					throw ConnectionClosed(this);
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
				throw ConnectionClosed(this);
			
			fdatasync(fd);
		}
		
		virtual void read(void *data, size_t size)
		{
			if (fd < 0)
				throw ConnectionClosed(this);
		
			char *ptr = (char *)data;
			size_t left = size;
			
			while (left)
			{
				ssize_t len = ::read(fd, ptr, left);
				
				if (len < 0)
				{
					close(fd);
					fd = -1;
					throw InputOutputError(this);
				}
				else if (len == 0)
				{
					close(fd);
					fd = -1;
					throw ConnectionClosed(this);
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}
	};
	
	//! Stream for serial port, in addition to FileDescriptorStream, save old state of serial port
	class SerialStream: public FileDescriptorStream
	{
	protected:
		struct termios oldtio;	//!< old serial port state
		
	public:
		//! Create the stream and associates a file descriptor
		SerialStream(const string& targetName, int fd, const struct termios* oldtio) :
			FileDescriptorStream(targetName, fd)
		{
			memcpy(&this->oldtio, oldtio, sizeof(struct termios));
		}
		
		//! Destructor, restore old serial port state
		virtual ~SerialStream()
		{
			 tcsetattr(fd, TCSANOW, &oldtio);
		}
	};
	
	
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
	
	#else // WIN32
	
	// TODO: add Win32 implementation
	
	#endif // WIN32
	
	
	//! A TCP/IP version 4 address
	class TCPIPV4Address
	{
	public:
		unsigned address; //!< IP host address. Stored in local byte order.
		unsigned short port; //!< IP port. Stored in local byte order.
	
	public:
		//! Constructor. Numeric argument
		TCPIPV4Address(unsigned addr = INADDR_ANY, unsigned short prt = 0)
		{
			address = addr;
			port = prt;
		}
		
		//! Constructor. String address, do resolution
		TCPIPV4Address(const std::string& name, unsigned short port) :
			port(port)
		{
			hostent *he = gethostbyname(name.c_str());
			
			if (he == NULL)
			{
				#ifndef WIN32
				struct in_addr addr;
				if (inet_aton(name.c_str(), &addr))
				{
					address = ntohl(addr.s_addr);
				}
				else
				{
					address = INADDR_ANY;
				}
				#else // WIN32
				address = INADDR_ANY;
				#endif // WIN32
			}
			else
			{
				address = ntohl(*((unsigned *)he->h_addr));
			}
		}
	
		//! Equality operator
		bool operator==(const TCPIPV4Address& o) const
		{
			return address==o.address && port==o.port;
		}
		
		//! Less than operator
		bool operator<(const TCPIPV4Address& o) const
		{
			return address<o.address || (address==o.address && port<o.port);
		}
		
		//! Return string form
		std::string format() const
		{
			std::stringstream buf;
			unsigned a2 = htonl(address);
			struct hostent *he = gethostbyaddr((const char *)&a2, 4, AF_INET);
			
			if (he == NULL)
			{
				struct in_addr addr;
				addr.s_addr = a2;
				buf << "tcp:" << inet_ntoa(addr) << ":" << port;
			}
			else
			{
				buf << "tcp:" << he->h_name << ":" << port;
			}
			
			return buf.str();
		}
		
		//! Is the address valid?
		bool valid() const
		{
			return address != INADDR_ANY && port != 0;
		}
	};
	
	//! Parse target names.
	//! Instanciate an object of this class with the target description to get its type and parameters
	class TargetNameParser
	{
	protected:
		//! An interface for a target parameter
		struct TargetParameter
		{
			string name; //!< name of the parameter
			string value; //!< name of the parameter
			bool mandatory; //!< is it mandatory to be filled
			bool filled; //!< was the parameter filled by the target description
			
			//! Virtual destructor, call child destructors
			virtual ~TargetParameter() { }
			
			//! Return the value is a specific type
			template<typename V>
			V getValue()
			{
				istringstream iss(value);
				V v;
				iss >> v;
				return v;
			}
			
			//! Constructor, with no default value
			TargetParameter(string name) :
				name(name),
				mandatory(true),
				filled(false)
			{ }
			
			//! Constructor, with a default value
			template<typename T>
			TargetParameter(string name, T defaultValue) :
				name(name),
				mandatory(false),
				filled(false)
			{
				ostringstream oss;
				oss << defaultValue;
				value = oss.str();
			}
			
			//! Set a value
			void setValue(const string& value)
			{
				this->value = value;
				filled = true;
			}
			
			//! Dump the content
			void dump(ostream& dump)
			{
				if (filled)
				{
					dump << name << " (filled) = " << value << ", mandatory: " << mandatory;
				}
				else
				{
					dump << name << " (unfilled), default = " << value << ", mandatory: " << mandatory;
				}
			}
		};
		
		//! A vector of parameters. Ordered so that the parser can fill anonymous ones
		struct TargetParameters:public vector<TargetParameter *>
		{
			//! Returns a parameter of a specific name. Returns 0 if unknown
			TargetParameter* getParameter(const string& name)
			{
				for (size_t parameter = 0; parameter < size(); ++parameter)
					if ((*this)[parameter]->name == name)
						return (*this)[parameter];
				return 0;
			}
			
			//! Returns a parameter of a specific name. Abort if unknown
			TargetParameter* getParameterForced(const string& name)
			{
				TargetParameter* parameters = getParameter(name);
				if (parameters)
					return parameters;
				else
					abort();
			}
			
			//! Set a specific parameter
			void setParameter(const string& name, const string& value, const string& target)
			{
				for (size_t parameter = 0; parameter < size(); ++parameter)
					if ((*this)[parameter]->name == name)
					{
						(*this)[parameter]->setValue(value);
						return;
					}
				throw InvalidTargetDescription(target);
			}
			
			//! Runs through parameters, and throw InvalidTarget if a mandatory one is not filled
			void checkMandatoryParameters(const string& target)
			{
				for (size_t parameter = 0; parameter < size(); ++parameter)
					if ((*this)[parameter]->mandatory && !(*this)[parameter]->filled)
						throw InvalidTargetDescription(target);
			}
			
			//! Dump the content
			void dump(ostream& dump)
			{
				for (size_t parameter = 0; parameter < size(); ++parameter)
				{
					(*this)[parameter]->dump(dump);
					dump << "\n";
				}
			}
		};
	
		//! A map of type to parameters
		typedef map<string, TargetParameters> TargetsTypes;
		
		string target; //!< name of the target
		TargetsTypes targetsTypes; //!< known target types and parameter
		string type; //!< actual target type
		TargetParameters* parameters; //!< actual target parameters
		
	public:
		//! Constructor, fills parameters and parse target description. Throws an InvalidTarget on parse error
		TargetNameParser(const string &target) :
			target(target)
		{
			// TODO: add option to restrict strings possible values
			targetsTypes["file"].push_back(new TargetParameter("name"));
			targetsTypes["file"].push_back(new TargetParameter("mode", "read"));
			
			targetsTypes["tcp"].push_back(new TargetParameter("host", "0.0.0.0"));
			targetsTypes["tcp"].push_back(new TargetParameter("port"));
			
			targetsTypes["ser"].push_back(new TargetParameter("port", 1));
			targetsTypes["ser"].push_back(new TargetParameter("baud", 115200));
			targetsTypes["ser"].push_back(new TargetParameter("stop", 1));
			targetsTypes["ser"].push_back(new TargetParameter("parity", "none"));
			targetsTypes["ser"].push_back(new TargetParameter("fc", "none"));
			
			parse();
			dump(cerr);
		}
		
		//! Destructor, deletes all parameters
		~TargetNameParser()
		{
			for (TargetsTypes::iterator targetType = targetsTypes.begin(); targetType != targetsTypes.end(); ++targetType)
			{
				for (size_t parameter = 0; parameter < targetType->second.size(); ++parameter)
					delete targetType->second[parameter];
			}
		}
		
		//! Dump the content
		void dump(ostream& dump)
		{
			for (TargetsTypes::iterator targetType = targetsTypes.begin(); targetType != targetsTypes.end(); ++targetType)
			{
				dump << targetType->first << ":\n";
				targetType->second.dump(dump);
				dump << "\n";
			}
		}
		
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
		
		//! Creates a file stream
		Stream* createFileStream()
		{
			// get parameters
			const string& name = parameters->getParameterForced("name")->value;
			const string& mode = parameters->getParameterForced("mode")->value;
			
			#ifndef WIN32
			int fd;
			
			// open file
			if (mode == "read")
				fd = open(name.c_str(), O_RDONLY);
			else if (mode == "write")
				fd = creat(name.c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
			// as we currently have no seek, read/write is useless
			/*else if (mode == "readwrite") 
				fd = open(name.c_str(), O_RDONLY);*/
			else
				throw InvalidTargetDescription(target);
			
			if (fd == -1)
				throw ConnectionError(target);
			
			// create stream and associate fd
			return new FileDescriptorStream(target, fd);
			
			#else // WIN32
			
			// TODO: add Win32 implementation
			
			#endif // WIN32
		}
		
		/**
			Creates a listen stream for incoming connections.
			
			\param host target host name
			\param port target port
		*/
		Stream* createTCPListenStream(const std::string& host, unsigned short port)
		{
			TCPIPV4Address bindAddress(host, port);
			
			#ifndef WIN32
			
			// create socket
			int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (fd < 0)
				throw ConnectionError(target);
			
			// reuse address
			int flag = 1;
			if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof (flag)) < 0)
				throw ConnectionError(target);
			
			// bind
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(bindAddress.port);
			addr.sin_addr.s_addr = htonl(bindAddress.address);
			if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
				throw ConnectionError(target);
			
			// listen
			listen(fd, 16); // backlog of 16 is a pure blind guess
			return new SocketStream(target, fd);
			
			#else // WIN32
			
			// TODO: add Win32 implementation
			
			#endif // WIN32
		}
		
		/**
			Creates a data stream.
			
			\param host target host name
			\param port target port
		*/
		Stream* createTCPDataStream(const std::string& host, unsigned short port)
		{
			TCPIPV4Address remoteAddress(host, port);
			
			#ifndef WIN32
			
			// create socket
			int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (fd < 0)
				throw ConnectionError(target);
			
			// connect
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(remoteAddress.port);
			addr.sin_addr.s_addr = htonl(remoteAddress.address);
			if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
				throw ConnectionError(target);
			
			return new SocketStream(target, fd);
			
			#else // WIN32
			
			// TODO: add Win32 implementation
			
			#endif // WIN32
		}
		
		//! Creates a serial port stream.
		Stream* createSerialStream()
		{
			// TODO: implement this
			throw ConnectionError(target);
			
			#ifndef WIN32
			
			// TODO: open device
			int fd = 0;
			
			struct termios newtio, oldtio;
			
			// save old serial port state and clear new one
			tcgetattr(fd, &oldtio);
			memset(&newtio, 0, sizeof(newtio));
			
			// TODO: set generic baud rate
			newtio.c_cflag |= CS8;				// 8 bits characters
			newtio.c_cflag |= CLOCAL;			// ignore modem control lines.
			newtio.c_cflag |= CREAD;			// enable receiver.
			if (parameters->getParameterForced("fc")->value == "hard")
				newtio.c_cflag |= CRTSCTS;		// enable hardware flow control
			if (parameters->getParameterForced("parity")->value != "none")
			{
				newtio.c_cflag |= PARENB;		// enable parity generation on output and parity checking for input.
				if (parameters->getParameterForced("parity")->value == "odd")
					newtio.c_cflag |= PARODD;	// parity for input and output is odd.
			}
			
			newtio.c_iflag = IGNPAR;			// ignore parity on input
			
			newtio.c_oflag = 0;
			
			newtio.c_lflag = 0;
			
			newtio.c_cc[VTIME] = 0;			// block forever if no byte
			newtio.c_cc[VMIN] = 1;				// one byte is sufficient to return
			
			// set attributes
			if ((tcflush(fd, TCIFLUSH) < 0) || (tcsetattr(fd, TCSANOW, &newtio) < 0))
				throw ConnectionError(target);
			
			return new SerialStream(target, fd, &oldtio);
			
			#endif
		}
		
		//! Parse target description. Throws an InvalidTarget on parse error
		void parse()
		{
			string::size_type colonPos;
			
			// get type
			colonPos = target.find_first_of(':');
			type = target.substr(0, colonPos);
			
			// check if it exists, get parameters
			TargetsTypes::iterator typeIt = targetsTypes.find(type);
			if (typeIt == targetsTypes.end()) throw InvalidTargetDescription(target);
			parameters = &typeIt->second;
			
			// iterate on all parameters
			int implicitParamPos = 0; // position in array when using implicit parameters, as soon as we see an explicit one, this is set to -1 and implicit parameters must not be used anymore
			while (colonPos != string::npos)
			{
				string::size_type nextColon = target.find_first_of(':', colonPos+1);
				string::size_type equalPos = target.find_first_of('=', colonPos+1);
				
				if (equalPos == string::npos)
				{
					string::size_type valueLength = nextColon == string::npos ? string::npos : nextColon - colonPos - 1;
					
					// implicit parameter
					if ((implicitParamPos < 0) || (implicitParamPos >= parameters->size()))
						throw InvalidTargetDescription(target);
					(*parameters)[implicitParamPos++]->setValue(target.substr(colonPos+1, valueLength));
				}
				else
				{
					string::size_type nameLength = equalPos - colonPos - 1;
					string::size_type valueLength = nextColon == string::npos ? string::npos : nextColon - equalPos - 1;
					
					// explicit parameter, extract
					parameters->setParameter(
						target.substr(colonPos+1, nameLength),
						target.substr(equalPos+1, valueLength),
						target
					);
					
					// we cannot use implicit parameters any more
					implicitParamPos = -1;
				}
				colonPos = nextColon;
			}
			
			// make sure that everything that must be filled is filled
			parameters->checkMandatoryParameters(target);
		}
	};
	
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
		#ifndef WIN32
		
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
		
		#else
		
		// TODO: add Win32 implementation
		
		#endif
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
		#ifndef WIN32
		
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
		
		#else
		
		// TODO: add Win32 implementation
		
		#endif
	}
}
