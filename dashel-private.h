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

// stdin: tcp:host=localhost;

#ifndef INCLUDED_DASHEL_PRIVATE_H
#define INCLUDED_DASHEL_PRIVATE_H

#include "dashel.h"

#include <sstream>
#include <vector>
#include <cassert>

#ifndef WIN32
	#include <netdb.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
#else
	#include <winsock2.h>
#endif

namespace Dashel
{
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
		
		//! Is the address valid?
		bool valid() const
		{
			return address != INADDR_ANY && port != 0;
		}
	};

	//! Parameter set.
	class ParameterSet
	{
	private:
		std::map<std::string, std::string> values;
		std::vector<std::string> params;

	public:
		//! Add values to set.
		void add(const char *line)
		{
			char *lc = strdup(line);
			int spc = 0;
			char *param;
			bool storeParams = (params.size() == 0);
			char *protocolName = strtok(lc, ":");
			
			// Do nothing with this.
			assert(protocolName);
			
			while((param = strtok(NULL, ";")) != NULL)
			{
				char *sep = strchr(param, '=');
				if(sep)
				{
					*sep++ = 0;
					values[param] = sep;
					if (storeParams)
						params.push_back(param);
				}
				else
				{
					if (storeParams)
						params.push_back(param);
					values[params[spc]] = param;
				}
				++spc;
			}
			
			free(lc);
		}
		
		//! Return wether a key is set or not
		bool isSet(const char *key)
		{
			return (values.find(key) != values.end());
		}

		//! Get a parameter value
		template<typename T> T get(const char *key)
		{
			T t;
			std::map<std::string, std::string>::iterator it = values.find(key);
			if(it == values.end())
			{
				std::string r = std::string("Parameter missing: ").append(key);
				throw Dashel::StreamException(StreamException::InvalidTarget, 0, NULL, r.c_str());
			}
			std::istringstream iss(it->second);
			iss >> t;
			return t;
		}

		//! Get a parameter value
		const std::string& get(const char *key)
		{
			std::map<std::string, std::string>::iterator it = values.find(key);
			if(it == values.end())
			{
				std::string r = std::string("Parameter missing: ").append(key);
				throw StreamException(StreamException::InvalidTarget, 0, NULL, r.c_str());
			}
			return it->second;
		}
	};

	//! Event types that can be waited on.
	typedef enum {
		EvData,				//!< Data available.
		EvPotentialData,	//!< Maybe some data or maybe not.
		EvClosed,			//!< Closed by remote.
		EvConnect,			//!< Incoming connection detected.
	} EvType;
}

#endif
