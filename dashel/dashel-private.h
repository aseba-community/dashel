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

namespace Dashel
{
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
		
		//! Return whether a key is set or not
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
				throw Dashel::DashelException(DashelException::InvalidTarget, 0, r.c_str());
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
				throw DashelException(DashelException::InvalidTarget, 0, r.c_str());
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
