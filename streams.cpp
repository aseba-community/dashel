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
#include <algorithm>

#ifndef WIN32
	#include <netdb.h>
	#include <signal.h>
	#include <arpa/inet.h>
	#include <sys/select.h>
	#include <sys/time.h>
	#include <sys/types.h>
	#include <unistd.h>
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

	#ifndef WIN32
	
	//! Stream with a file descriptor that is selectable
	class SelectableStream: public Stream
	{
	public:
		int fd; //!< associated file descriptor
		string targetName; //!< name of the target
		
		//! Constructor. The file descriptor is initially invalid and will be set by caller
		SelectableStream() : fd(-1) { }
		
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
		SocketStream()
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
	
	//! Global variables to signal SIGTERM.
	bool sigTermReceived = false;
	
	//! Called when SIGTERM arrives, halts all running clients or servers in all threads
	void termHandler(int t)
	{
		sigTermReceived = true;
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
	
	
	Client::Client(const std::string &target) :
		stream(0),
		isRunning(false)
	{
		// TODO: construct client
	}
	
	Client::~Client()
	{
		delete stream;
	}
	
	void Client::run()
	{
		sigTermReceived = false;
		isRunning = true;
		while (!sigTermReceived && isRunning)
			step(-1);
	}
	
	bool Client::step(int timeout)
	{
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
		// TODO
	}
	
	bool Server::step(int timeout)
	{
		// TODO
	}
	
	#else
	
	// TODO: add implementation
	
	#endif
	
	
}
