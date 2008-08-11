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
#include <algorithm>

#include <ostream>
#include <sstream>
#ifndef WIN32
	#include <netdb.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
#else
	#include <winsock2.h>
#endif

/*!	\file dashel-commong.cpp
	\brief Implementation of DaSHEL, A cross-platform DAta Stream Helper Encapsulation Library
*/

namespace Dashel
{
	DashelException::DashelException(Source s, int se, const char *reason, Stream* stream) :
		std::runtime_error(reason),
		source(s),
		sysError(se),
		stream(stream)
	{
	
	}
	
	IPV4Address::IPV4Address(unsigned addr, unsigned short prt)
	{
		address = addr;
		port = prt;
	}
	
	IPV4Address::IPV4Address(const std::string& name, unsigned short port) :
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
			unsigned long addr = inet_addr(name.c_str());
			if(addr != INADDR_NONE)
				address = addr;
			else
				address = INADDR_ANY;
			#endif // WIN32
		}
		else
		{
			#ifndef WIN32
			address = ntohl(*((unsigned *)he->h_addr));
			#else
			address = ntohl(*((unsigned *)he->h_addr));
			#endif
		}
	}
	
	bool IPV4Address::operator==(const IPV4Address& o) const
	{
		return address==o.address && port==o.port;
	}
	
	bool IPV4Address::operator<(const IPV4Address& o) const
	{
		return address<o.address || (address==o.address && port<o.port);
	}
	
	std::string IPV4Address::hostname() const
	{
		unsigned a2 = htonl(address);
		struct hostent *he = gethostbyaddr((const char *)&a2, 4, AF_INET);
		
		if (he == NULL)
		{
			struct in_addr addr;
			addr.s_addr = a2;
			return std::string(inet_ntoa(addr));
		}
		else
		{
			return std::string(he->h_name);
		}
	}
	
	std::string IPV4Address::format() const
	{
		std::ostringstream buf;
		unsigned a2 = htonl(address);
		struct hostent *he = gethostbyaddr((const char *)&a2, 4, AF_INET);
		
		if (he == NULL)
		{
			struct in_addr addr;
			addr.s_addr = a2;
			buf << "tcp:host=" << inet_ntoa(addr) << ";port=" << port;
		}
		else
		{
			buf << "tcp:host=" << he->h_name << ";port=" << port;
		}
		
		return buf.str();
	}
	
	void PacketStream::write(const void *data, const size_t size)
	{
		unsigned char* ptr = (unsigned char*)data;
		std::copy(ptr, ptr + size, std::back_inserter(sendBuffer));
	}
	
	void PacketStream::read(void *data, size_t size)
	{
		if (size > receptionBuffer.size())
			fail(DashelException::IOError, 0, "Attempt to read past available data");
		
		unsigned char* ptr = (unsigned char*)data;
		std::copy(receptionBuffer.begin(), receptionBuffer.begin() + size, ptr);
		receptionBuffer.erase(receptionBuffer.begin(), receptionBuffer.begin() + size);
	}
	
	void Hub::closeStream(Stream* stream)
	{
		streams.erase(stream);
		dataStreams.erase(stream);
		delete stream;
	}
}

