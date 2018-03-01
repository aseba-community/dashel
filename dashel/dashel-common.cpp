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
#include "dashel-private.h"
#include <algorithm>

#include <ostream>
#include <sstream>
// clang-format off
#ifndef _WIN32
	#include <netdb.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
#else
	#include <winsock2.h>
#endif
// clang-format on

/*!	\file dashel-commong.cpp
	\brief Implementation of Dashel, A cross-platform DAta Stream Helper Encapsulation Library
*/

namespace Dashel
{
	using namespace std;

	// frome dashe-private.h
	ExpandableBuffer::ExpandableBuffer(size_t size) :
		_data((unsigned char*)malloc(size)),
		_size(size),
		_pos(0)
	{
	}

	ExpandableBuffer::~ExpandableBuffer()
	{
		free(_data);
	}

	void ExpandableBuffer::clear()
	{
		_pos = 0;
	}

	void ExpandableBuffer::add(const void* data, const size_t size)
	{
		if (_pos + size > _size)
		{
			_size = max(_size * 2, _size + size);
			_data = (unsigned char*)realloc(_data, _size);
		}
		memcpy(_data + _pos, (unsigned char*)data, size);
		_pos += size;
	}

	// to be removed when we switch to C++11
	string _to_string(int se)
	{
		ostringstream ostr;
		ostr << se;
		return ostr.str();
	}

	// frome dashel.h
	DashelException::DashelException(Source s, int se, const char* reason, Stream* stream) :
		std::runtime_error(sourceToString(s) + " (" + _to_string(se) + "): " + reason),
		source(s),
		sysError(se),
		stream(stream)
	{
	}

	string DashelException::sourceToString(Source s)
	{
		// clang-format off
		const char* const sourceNames[] =
		{
			"Unknown cause",
			"Synchronisation error",
			"Invalid target",
			"Invalid operation",
			"Connection lost",
			"I/O error",
			"Connection failed",
			"Enumeration error",
			"Previous incoming data not read"
		};
		// clang-format on
		const size_t arrayLength(sizeof(sourceNames) / sizeof(const char*));
		if (s >= arrayLength)
			return sourceNames[0];
		else
			return sourceNames[s];
	}

	IPV4Address::IPV4Address(unsigned addr, unsigned short prt) :
		address(addr),
		port(prt) {}

	IPV4Address::IPV4Address(const std::string& name, unsigned short port) :
		port(port)
	{
		hostent* he = gethostbyname(name.c_str());

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
			if (addr != INADDR_NONE)
				address = addr;
			else
				address = INADDR_ANY;
#endif // WIN32
		}
		else
		{
#ifndef WIN32
			address = ntohl(*((unsigned*)he->h_addr));
#else
			address = ntohl(*((unsigned*)he->h_addr));
#endif
		}
	}

	bool IPV4Address::operator==(const IPV4Address& o) const
	{
		return address == o.address && port == o.port;
	}

	bool IPV4Address::operator<(const IPV4Address& o) const
	{
		return address < o.address || (address == o.address && port < o.port);
	}

	std::string IPV4Address::hostname() const
	{
		unsigned a2 = htonl(address);
		struct hostent* he = gethostbyaddr((const char*)&a2, 4, AF_INET);

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

	std::string IPV4Address::format(const bool resolveName) const
	{
		std::ostringstream buf;
		unsigned a2 = htonl(address);

		if (resolveName)
		{
			struct hostent* he = gethostbyaddr((const char*)&a2, 4, AF_INET);
			if (he != NULL)
			{
				buf << "tcp:host=" << he->h_name << ";port=" << port;
				return buf.str();
			}
		}

		struct in_addr addr;
		addr.s_addr = a2;
		buf << "tcp:host=" << inet_ntoa(addr) << ";port=" << port;
		return buf.str();
	}

	void ParameterSet::add(const char* line)
	{
		char* lc = strdup(line);
		int spc = 0;
		char* param;
		bool storeParams = (params.size() == 0);
		char* protocolName = strtok(lc, ":");

		// Do nothing with this.
		assert(protocolName);

		while ((param = strtok(NULL, ";")) != NULL)
		{
			char* sep = strchr(param, '=');
			if (sep)
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

	void ParameterSet::addParam(const char* param, const char* value, bool atStart)
	{
		if (atStart)
			params.insert(params.begin(), 1, param);
		else
			params.push_back(param);

		if (value)
			values[param] = value;
	}

	bool ParameterSet::isSet(const char* key) const
	{
		return (values.find(key) != values.end());
	}

	const std::string& ParameterSet::get(const char* key) const
	{
		std::map<std::string, std::string>::const_iterator it = values.find(key);
		if (it == values.end())
		{
			std::string r = std::string("Parameter missing: ").append(key);
			throw DashelException(DashelException::InvalidTarget, 0, r.c_str());
		}
		return it->second;
	}

	// explicit template instanciation of get() for int, unsigned, float and double
	template bool ParameterSet::get<bool>(const char* key) const;
	template int ParameterSet::get<int>(const char* key) const;
	template unsigned ParameterSet::get<unsigned>(const char* key) const;
	template float ParameterSet::get<float>(const char* key) const;
	template double ParameterSet::get<double>(const char* key) const;

	std::string ParameterSet::getString() const
	{
		std::ostringstream oss;
		std::vector<std::string>::const_iterator i = params.begin();
		while (i != params.end())
		{
			oss << *i << "=" << values.find(*i)->second;
			if (++i == params.end())
				break;
			oss << ";";
		}
		return oss.str();
	}

	void ParameterSet::erase(const char* key)
	{
		std::vector<std::string>::iterator i = std::find(params.begin(), params.end(), key);
		if (i != params.end())
			params.erase(i);

		std::map<std::string, std::string>::iterator j = values.find(key);
		if (j != values.end())
			values.erase(j);
	}


	void MemoryPacketStream::write(const void* data, const size_t size)
	{
		sendBuffer.add(data, size);
	}

	void MemoryPacketStream::read(void* data, size_t size)
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

	void StreamTypeRegistry::reg(const std::string& proto, const CreatorFunc func)
	{
		creators[proto] = func;
	}

	Stream* StreamTypeRegistry::create(const std::string& proto, const std::string& target, const Hub& hub) const
	{
		typedef CreatorMap::const_iterator ConstIt;
		ConstIt it(creators.find(proto));
		if (it == creators.end())
			return 0;
		const CreatorFunc& creatorFunc(it->second);
		return creatorFunc(target, hub);
	}

	std::string StreamTypeRegistry::list() const
	{
		std::string s;
		for (CreatorMap::const_iterator it = creators.begin(); it != creators.end();)
		{
			s += it->first;
			++it;
			if (it != creators.end())
				s += ", ";
		}
		return s;
	}
}
