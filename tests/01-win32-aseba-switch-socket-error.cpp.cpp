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

/*
	Test case based on aseba switch.
	
	Launch one instance with no parameter and another one with the -c switch.
	If the bug is present, on WIN32, the socket makes an error.
*/

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <set>
#include <valarray>
#include <vector>
#include <iterator>
#include <dashel/dashel.h>

using namespace std;
using namespace Dashel;

class Switch : public Hub
{
public:
	Switch()
	{
		connect("tcpin:port=33333");
	}
	
	void connectionCreated(Stream *stream)
	{
		cout << "Incoming connection from " << stream->getTargetName() << endl;
	}
	
	void incomingData(Stream *stream)
	{
		// max packet length is 65533
		// packet source and packet type is not counted in len,
		// thus read buffer is of size len + 4
		unsigned short len;
		
		// read the transfer size
		stream->read(&len, 2);
		
		// allocate the read buffer and do socket read
		std::valarray<unsigned char> readbuff((unsigned char)0, len + 4);
		stream->read(&readbuff[0], len + 4);
		
		// write on all connected streams
		for (StreamsSet::iterator it = dataStreams.begin(); it != dataStreams.end();++it)
		{
			Stream* destStream = *it;
			try
			{
				destStream->write(&len, 2);
				destStream->write(&readbuff[0], len + 4);
				destStream->flush();
			}
			catch (DashelException e)
			{
				// if this stream has a problem, ignore it for now, and let Hub call connectionClosed later.
			}
		}
	}
	
	void connectionClosed(Stream *stream, bool abnormal)
	{
		if (abnormal)
			cout << "Abnormal connection closed to " << stream->getTargetName() << " : " << stream->getFailReason() << endl;
		else
			cout << "Normal connection closed to " << stream->getTargetName() << endl;
	}
};

class Client : public Hub
{
public:
	Client()
	{
		Stream* stream = connect("tcp:localhost;33333");
		unsigned short len = 0;
		unsigned short source = 1;
		unsigned short type = 2;
		stream->write(&len, 2);
		stream->write(&source, 2);
		stream->write(&type, 2);
		stream->flush();
	}
	
	void connectionCreated(Stream *stream)
	{
		/*unsigned short len = 0;
		unsigned short source = 1;
		unsigned short type = 2;
		stream->write(&len, 2);
		stream->write(&source, 2);
		stream->write(&type, 2);
		stream->flush();*/
	}
	
	void incomingData(Stream *stream)
	{
		unsigned short len = 0;
		unsigned short source = 1;
		unsigned short type = 2;
		stream->read(&len, 2);
		stream->read(&source, 2);
		stream->read(&type, 2);
	}
};

int main(int argc, char *argv[])
{
	std::vector<std::string> additionalTargets;
	
	int argCounter = 1;
	bool client = false;
	
	while (argCounter < argc)
	{
		const char *arg = argv[argCounter];
		if (strcmp(arg, "-c") == 0)
		{
			client = true;
		}
		else
		{
			additionalTargets.push_back(argv[argCounter]);
		}
		argCounter++;
	}
	
	try
	{
		if (client)
		{
			Client aclient;
			aclient.run();
		}
		else
		{
			Switch aswitch;
			for (size_t i = 0; i < additionalTargets.size(); i++)
				aswitch.connect(additionalTargets[i]);
			aswitch.run();
		}
	}
	catch(Dashel::DashelException e)
	{
		std::cerr << e.reason << " " << e.sysMessage << std::endl;
	}
	
	return 0;
}


