#include <dashel/dashel.h>
#include <iostream>
#include <cassert>

using namespace std;
using namespace Dashel;

string readLine(Stream* stream)
{
	char c;
	string line;
	do
	{
		stream->read(&c, 1);
		line += c;
	} while (c != '\n' && c != '\r');
	return line;
}

void sendString(Stream* stream, const string& line)
{
	stream->write(line.c_str(), line.length());
	stream->flush();
}


class ChatServer : public Hub
{
public:
	ChatServer()
	{
		listenStream = connect("tcpin:port=8765");
	}

protected:
	Stream* listenStream;
	map<Stream*, string> nicks;

protected:
	void connectionCreated(Stream* stream)
	{
		cout << "+ Incoming connection from " << stream->getTargetName() << " (" << stream << ")" << endl;
		string nick = readLine(stream);
		nick.erase(nick.length() - 1);
		nicks[stream] = nick;
		cout << "+ User " << nick << " is connected." << endl;
	}

	void incomingData(Stream* stream)
	{
		string line = readLine(stream);
		const string& nick = nicks[stream];
		line = nick + " : " + line;
		cout << "* Message from " << line;

		for (StreamsSet::iterator it = dataStreams.begin(); it != dataStreams.end(); ++it)
			sendString((*it), line);
	}

	void connectionClosed(Stream* stream, bool abnormal)
	{
		cout << "- Connection closed to " << stream->getTargetName() << " (" << stream << ")";
		if (abnormal)
			cout << " : " << stream->getFailReason();
		cout << endl;
		string nick = nicks[stream];
		nicks.erase(stream);
		cout << "- User " << nick << " is disconnected." << endl;
	}
};

class ChatClient : public Hub
{
public:
	ChatClient(string remoteTarget, const string& nick) :
		inputStream(0),
		nick(nick)
	{
		remoteTarget += ";port=8765";
		inputStream = connect("stdin:");
		remoteStream = connect(remoteTarget);
		this->nick += '\n';
		sendString(remoteStream, this->nick);
	}

protected:
	Stream* inputStream;
	Stream* remoteStream;
	string nick;

	void connectionCreated(Stream* stream)
	{
		cout << "Incoming connection " << stream->getTargetName() << " (" << stream << ")" << endl;
	}

	void incomingData(Stream* stream)
	{
		assert(inputStream);
		assert(remoteStream);

		if (stream == inputStream)
		{
			string line = readLine(inputStream);
			sendString(remoteStream, line);
		}
		else
		{
			string line = readLine(remoteStream);
			cout << line;
			cout.flush();
		}
	}

	void connectionClosed(Stream* stream, bool abnormal)
	{
		cout << "Connection closed to " << stream->getTargetName() << " (" << stream << ")";
		if (abnormal)
			cout << " : " << stream->getFailReason();
		cout << endl;
		stop();
	}
};

int main(int argc, char* argv[])
{
	try
	{
		if (argc > 2)
		{
			ChatClient client(argv[1], argv[2]);
			client.run();
		}
		else
		{
			ChatServer server;
			server.run();
		}
	}
	catch (const DashelException& e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
