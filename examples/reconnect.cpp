#include <dashel/dashel.h>
#include <iostream>
#include <cassert>

using namespace std;
using namespace Dashel;

class ReconnectingHub : public Hub
{
public:
	ReconnectingHub(const string& target) :
		connected(false),
		target(target)
	{
		connectToTarget();
	}

	void stepAndReconnect()
	{
		// first step
		step(1000);
		// if disconnected...
		if (!connected)
		{
			// ...attempt to reconnect
			connectToTarget();
		}
	}

protected:
	bool connected;
	string target;

protected:
	void connectToTarget()
	{
		try
		{
			connect(target);
		}
		catch (const DashelException& e)
		{
			cout << endl
				 << "Failed connecting to " << target << " : " << e.what() << endl;
		}
	}

protected:
	virtual void connectionCreated(Stream* stream)
	{
		connected = true;
		cout << endl
			 << "Connected to " << stream->getTargetName() << endl;
	}

	virtual void incomingData(Stream* stream)
	{
		char c = stream->read<char>();
		cout << ".";
	}

	virtual void connectionClosed(Stream* stream, bool abnormal)
	{
		connected = false;
		cout << endl
			 << "Lost connection to " << stream->getTargetName();
		if (abnormal)
			cout << " : " << stream->getFailReason();
		cout << endl;
	}
};

int main(int argc, char* argv[])
{
	try
	{
		if (argc > 1)
		{
			ReconnectingHub reconnectingHub(argv[1]);
			while (true)
			{
				reconnectingHub.stepAndReconnect();
			}
		}
		else
		{
			cerr << "Usage " << argv[0] << " target" << endl;
			return 1;
		}
	}
	catch (const DashelException& e)
	{
		cerr << e.what() << endl;
	}

	return 0;
}
