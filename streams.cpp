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
	inline Derived polymorphic_downcast(Base *base)
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
		int fd; //!< associated file descriptor
		string targetName; //!< name of the target
		
		//! Create the stream and associates a file descriptor
		SelectableStream(int fd): fd(fd) { }
		
		~SelectableStream()
		{
			if (fd)
				close(fd);
		}
		
		virtual std::string getTargetName()	{ return targetName; }
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
		SocketStream(int fd) :
			SelectableStream(fd)
		{
			#ifndef TCP_CORK
			bufferSize = SEND_BUFFER_SIZE_INITIAL;
			buffer = (unsigned char*)malloc(bufferSize);
			bufferPos = 0;
			#endif
		}
		
		virtual ~SocketStream()
		{
			if (fd)
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
		FileDescriptorStream(int fd) : SelectableStream(fd) { }
		
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
		};
	
	public:
		//! A map of type to parameters
		typedef map<string, TargetParameters> TargetsTypes;
		
		TargetsTypes targetsTypes; //!< known target types and parameter
		string type; //!< actual target type
		TargetParameters* parameters; //!< actual target parameters
		
	public:
		//! Constructor, fills parameters and parse target description. Throws an InvalidTarget on parse error
		TargetNameParser(const string &target)
		{
			targetsTypes["file"].push_back(new TargetParameter("name"));
			targetsTypes["file"].push_back(new TargetParameter("mode", "read"));
			
			targetsTypes["tcp"].push_back(new TargetParameter("address"));
			targetsTypes["tcp"].push_back(new TargetParameter("port"));
			
			targetsTypes["ser"].push_back(new TargetParameter("port", 1));
			targetsTypes["ser"].push_back(new TargetParameter("baud", 115200));
			targetsTypes["ser"].push_back(new TargetParameter("stop", 1));
			targetsTypes["ser"].push_back(new TargetParameter("parity", "none"));
			targetsTypes["ser"].push_back(new TargetParameter("fc", "none"));
			
			parse(target);
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
		
		//! Parse target description. Throws an InvalidTarget on parse error
		void parse(const string &target)
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
				string::size_type nextColon = target.find_first_of(':', colonPos);
				string::size_type equalPos = target.find_first_of('=', colonPos);
				
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
		
		Stream* createStream(const string& target)
		{
			if (type == "file")
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
				
				// create stream and associate fd
				return new FileDescriptorStream(fd);
				
				#else
				
				// TODO: add Win32 implementation
				
				#endif
			}
			else if (type == "tcp")
			{
				// TODO: construct client
			}
			else if (type == "ser")
			{
				// TODO: construct client
			}
			else
				throw InvalidTargetDescription(target);
		}
	};
	
	#else
	
	// TODO: add Win32 implementation
	
	#endif
	
	Client::Client(const std::string &target) :
		stream(0),
		isRunning(false)
	{
		TargetNameParser parser(target);
		stream = parser.createStream(target);
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
		SelectableStream* stream = polymorphic_downcast<SelectableStream*>(stream);
		if (stream->fd < 0)
			return false;
		
		// setup file descriptor sets
		fd_set rfds;
		int nfds = stream->fd;
		FD_ZERO(&rfds);
		FD_SET(stream->fd, &rfds);
		
		// do select
		int ret;
		if (timeout < 0)
		{
			ret = select(nfds+1, &rfds, NULL, NULL, NULL);
		}
		else
		{
			struct timeval t;
			t.tv_sec = 0;
			t.tv_usec = timeout;
			ret = select(nfds+1, &rfds, NULL, NULL, &t);
		}
		
		// check for error
		if (ret < 0)
			throw SynchronizationError();
		
		// check if data is available. If so, get it
		if (FD_ISSET(stream->fd, &rfds))
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
		// TODO
	}
	
	void Server::listen(const std::string &target)
	{
		// TODO
	}
	
	void Server::run(void)
	{
		runTerminationReceived = false;
		while (!runTerminationReceived)
			step(-1);
	}
	
	bool Server::step(int timeout)
	{
		// TODO
	}
	
	
	
	
}
