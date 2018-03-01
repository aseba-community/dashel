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

#ifndef INCLUDED_DASHEL_PRIVATE_H
#define INCLUDED_DASHEL_PRIVATE_H

#include "dashel.h"
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <cassert>

namespace Dashel
{
	//! A simple buffer that can expand when data is added (like std::vector), but that can also return a pointer to the underlying data (like std::valarray).
	class ExpandableBuffer
	{
	private:
		unsigned char* _data; //!< data buffer. Its size is increased when required.
		size_t _size; //!< allocated size of data
		size_t _pos; //!< size of used part of data

	public:
		//! Construct an expandable buffer of specific size
		explicit ExpandableBuffer(size_t size = 0);
		//! Destroy the buffer and frees the allocated memory
		~ExpandableBuffer();
		//! Remove all data from the buffer, the allocated memory is not freed to speed-up further reduce
		void clear();
		//! Append data to the buffer
		void add(const void* data, const size_t size);

		//! Return a pointer to the underlying data
		unsigned char* get() { return _data; }
		//! Return the actual amount of data stored
		size_t size() const { return _pos; }
		//! Return the amount of allocated memory, always >= size(), to reduce the amount of reallocation required
		size_t reservedSize() const { return _size; }
	};

	//! The system-neutral part of packet stream that implement the actual memory buffers
	class MemoryPacketStream : public PacketStream
	{
	protected:
		//! The buffer collecting data to send
		ExpandableBuffer sendBuffer;
		//! The buffer holding data from last receive
		std::deque<unsigned char> receptionBuffer;

	public:
		//! Constructor
		explicit MemoryPacketStream(const std::string& protocolName) :
			Stream(protocolName),
			PacketStream(protocolName) {}

		virtual void write(const void* data, const size_t size);

		// clang-format off
		virtual void flush() { /* hook for use by derived classes */ }
		// clang-format on

		virtual void read(void* data, size_t size);
	};


	template<typename T>
	T ParameterSet::get(const char* key) const
	{
		T t;
		std::map<std::string, std::string>::const_iterator it = values.find(key);
		if (it == values.end())
		{
			std::string r = std::string("Parameter missing: ").append(key);
			throw Dashel::DashelException(DashelException::InvalidTarget, 0, r.c_str());
		}
		std::istringstream iss(it->second);
		iss >> std::boolalpha >> t;
		return t;
	}
}

#endif
