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

	#ifndef WIN32
	
	//! Stream with a file descriptor
	class FileDescriptorStream: public Stream
	{
	public:
		int fd; //!< associated file descriptor
		string targetName; //!< name of the target
		
		FileDescriptorStream() : fd(-1) { }
		virtual std::string getTargetName()	{ return targetName; }
	};
	
	
	
	//! Socket, uses send/recv for read/write
	class SocketStream: public FileDescriptorStream
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
			if (fd < 0)
				throw ConnectionClosed(this);
			
			shutdown(fd, SHUT_RDWR);
			close(fd);
			
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
	
	#else
	
	// TODO: add implementation
	
	#endif
	
	
}
