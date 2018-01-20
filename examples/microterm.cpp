#include <dashel/dashel.h>
#include <iostream>
#include <cassert>

using namespace std;
using namespace Dashel;

#define LOCAL_ECHO

class MicroTerm : public Hub
{
public:
	MicroTerm(const char* t0, const char* t1)
	{
		s0 = connect(t0);
		s1 = connect(t1);
	}

protected:
	Stream* s0;
	Stream* s1;

	void connectionCreated(Stream* stream)
	{
		cout << "Incoming connection " << stream->getTargetName() << " (" << stream << ")" << endl;
	}

	void incomingData(Stream* stream)
	{
		assert(s0);
		assert(s1);

		char c;
		stream->read(&c, 1);
		if (stream == s0)
		{
#ifdef LOCAL_ECHO
			if (c == 4)
				stop();
#ifdef WIN32
			if (c == '\r')
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
			if (c == '\r')
				cout << std::endl;
			else
#endif
				cout << c;
			cout.flush();
		}
	}

	void connectionClosed(Stream* stream, bool abnormal)
	{
		cout << "Closed connection " << stream->getTargetName() << " (" << stream << ")";
		if (abnormal)
			cout << " : " << stream->getFailReason();
		cout << endl;
		stop();
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
		MicroTerm microTerm(argv[1], argv[2]);

		microTerm.run();
	}
	catch (const DashelException& e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
