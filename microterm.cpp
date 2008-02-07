#include "dashel.h"
#include <iostream>
#include <cassert>

using namespace std;
using namespace Dashel;

#define LOCAL_ECHO

class MicroTerm: public Hub
{
public:
	MicroTerm() : s0(0), s1(0) { }
	
protected:
	Stream* s0;
	Stream* s1;
	
	void incomingConnection(Stream *stream)
	{
		cout << "Incoming connection " << stream->getTargetName() << " (" << stream << ")" << endl;
		if (s0 == 0)
			s0 = stream;
		else
			s1 = stream;
	}
	
	void incomingData(Stream *stream)
	{
		assert(s0);
		assert(s1);
		
		char c;
		stream->read(&c, 1);
		if (stream == s0)
		{
			#ifdef LOCAL_ECHO
			#ifdef WIN32
			if(c == '\r')
					cout << std::endl;
			else
			#endif
				cout << c;
			#endif
			s1->write(&c, 1);
		}
		else
		{
			#ifdef WIN32
			if(c == '\r')
				cout << std::endl;
			else
			#endif
				cout << c;
			cout.flush();
		}
	}
	
	void connectionClosed(Stream *stream)
	{
		cout << "Closed connection " << stream->getTargetName() << " (" << stream << ")" << endl;
	}
};

int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		cerr << "Usage: " << argv[0] << " inputTarget destTarget" << endl;
		return 1;
	}
	
	try
	{
		MicroTerm microTerm;
		
		microTerm.connect(argv[1]);
		microTerm.connect(argv[2]);
		
		microTerm.run();
	}
	catch(StreamException e)
	{
		std::cerr << e.reason << " - " << e.sysMessage << " (" << e.sysError << ")" << std::endl;
	}
	
	return 0;
}
