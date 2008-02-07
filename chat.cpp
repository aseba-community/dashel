#include "dashel.h"
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
	}
	while (c != '\n');
	return line;
}

void sendString(Stream* stream, const string& line)
{
	stream->write(line.c_str(), line.length());
	stream->flush();
}
	

class ChatServer: public Hub
{
public:
	ChatServer() :
		listenStream(0)
	{
		connect("tcpin:port=8765");
	}

protected:
	Stream* listenStream;
	map<Stream*, string> nicks;
	
protected:
	void incomingConnection(Stream *stream)
	{
		if (listenStream == 0)
		{
			listenStream = stream;
		}
		else
		{
			cout << "+ Incoming connection from " << stream->getTargetName() << " (" << stream << ")" << endl;
			string nick = readLine(stream);
			nick.erase(nick.length() - 1);
			nicks[stream] = nick;
			cout << "+ User" << nick << " is connected." << endl;
		}
	}
	
	void incomingData(Stream *stream)
	{
		string line = readLine(stream);
		const string& nick = nicks[stream];
		line = nick + " : " + line;
		cout << "* Message" << line;
		
		for (StreamsList::iterator it = streams.begin(); it != streams.end(); ++it)
		{
			if ((*it != listenStream) && (!(*it)->failed()))
				sendString((*it), line);
		}
	}
	
	void connectionClosed(Stream *stream)
	{
		cout << "- Connection closed to " << stream->getTargetName() << " (" << stream << ")" << endl;
		string nick = nicks[stream];
		nicks.erase(stream);
		cout << "- User" << nick << " is disconnected." << endl;
	}
};

class ChatClient: public Hub
{
public:
	ChatClient(string remoteTarget, const string& nick) :
		inputStream(0),
		remoteStream(0),
		nick(nick)
	{
		remoteTarget += ";port=8765";
		connect("stdin:");
		connect(remoteTarget);
	}
	
protected:
	Stream* inputStream;
	Stream* remoteStream;
	string nick;
	
	void incomingConnection(Stream *stream)
	{
		cout << "Incoming connection " << stream->getTargetName() << " (" << stream << ")" << endl;
		if (inputStream == 0)
		{
			inputStream = stream;
		}
		else
		{
			remoteStream = stream;
			nick += '\n';
			sendString(remoteStream, nick);
		}
	}
	
	void incomingData(Stream *stream)
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
	
	void connectionClosed(Stream *stream)
	{
		cout << "Closed connection " << stream->getTargetName() << " (" << stream << ")" << endl;
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
	catch(StreamException e)
	{
		std::cerr << e.reason << " - " << e.sysMessage << " (" << e.sysError << ")" << std::endl;
	}
	
	return 0;
}
