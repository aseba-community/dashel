#include "streams.h"
#include <iostream>
#include <cassert>

using namespace std;
using namespace Streams;

#ifndef WIN32
const char* stdinTarget = "file:/dev/stdin";
#else
// TODO: stdin in WIN32... should we add this explicitely in dashel ?
#endif

class MicroTerm: public Server
{
public:
	MicroTerm() :
		stdinStream(0),
		serialStream(0)
	{ }
	
protected:
	Stream* stdinStream;
	Stream* serialStream;
	
	void incomingConnection(Stream *stream)
	{
		if (stream->getTargetName() == stdinTarget)
			stdinStream = stream;
		else
			serialStream = stream;
	}
	
	void incomingData(Stream *stream)
	{
		assert(stdinStream);
		assert(serialStream);
		
		char c;
		stream->read(&c, 1);
		if (stream == stdinStream)
		{
			serialStream->write(&c, 1);
		}
		else
		{
			cout << c;
			cout.flush();
		}
	}
	
	void connectionClosed(Stream *stream)
	{
		stop();
	}
};

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		cerr << "Usage: " << argv[0] << " target" << endl;
		return 1;
	}
	
	MicroTerm microTerm;
	
	microTerm.listen(stdinTarget);
	microTerm.listen(argv[1]);
	
	microTerm.run();
	
	return 0;
}
