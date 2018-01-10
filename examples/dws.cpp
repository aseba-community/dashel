// A simple HTTP/1.0 web server using Dashel

#include <dashel/dashel.h>
#include <iostream>
#include <fstream>
#include <cassert>

using namespace std;
using namespace Dashel;

// Helper functions for stream

string readLine(Stream* stream)
{
	char c;
	string line;
	do
	{
		stream->read(&c, 1);
		line += c;
	} while (c != '\n');
	return line;
}

void sendString(Stream* stream, const string& line)
{
	stream->write(line.c_str(), line.length());
	stream->flush();
}

void shutdownStream(Stream* stream)
{
	// close the stream, this is a hack because Dashel lacks a proper shutdown mechanism
	stream->fail(DashelException::Unknown, 0, "Request handling complete");
}

// Helper functions for string

// inspired from: http://stackoverflow.com/questions/289347/using-strtok-with-a-stdstring
vector<string> split(const string& str, const string& delim)
{
	vector<string> parts;
	size_t start;
	size_t end = 0;
	while (end < str.size())
	{
		start = end;
		while (start < str.size() && (delim.find(str[start]) != string::npos))
			start++; // skip initial whitespace
		end = start;
		while (end < str.size() && (delim.find(str[end]) == string::npos))
			end++; // skip to end of word
		if (end - start != 0) // just ignore zero-length strings.
			parts.push_back(string(str, start, end - start));
	}
	return parts;
}

// Web server itself

class WebServer : public Hub
{
public:
	explicit WebServer(const string& port)
	{
		listenStream = connect("tcpin:port=" + port);
	}

	void connectionCreated(Stream* stream)
	{
		cerr << stream << " Connection created to " << stream->getTargetName() << endl;
	}

	void incomingData(Stream* stream)
	{
		// received request
		const string request(readLine(stream));
		cerr << stream << " Request: " << request;

		// read all options
		readLine(stream);
		string optionLine;
		while ((optionLine = readLine(stream)) != "\r\n")
		{
			cerr << stream << " Option: " << optionLine;
		}

		// parse request
		vector<string> requestParts(split(request, "\n\r\t "));

		// only support GET
		if ((requestParts.size() < 2) || requestParts[0] != "GET")
		{
			cerr << stream << " Unsupported HTTP request" << request << endl;
			sendString(stream, "HTTP/1.0 403\r\n\r\n");
			shutdownStream(stream);
			return;
		}

		// try to open file
		const string fileName(requestParts[1]);
		ifstream ifs(fileName.c_str());
		char c = ifs.get();
		if (!ifs.good())
		{
			cerr << stream << " Cannot open file" << fileName << endl;
			sendString(stream, "HTTP/1.0 404\r\n\r\n");
			shutdownStream(stream);
			return;
		}

		// read and send the file
		// note: this could be made much more efficient by checking the
		// file size and reading it all at once
		cerr << stream << " Serving: " << fileName << endl;
		sendString(stream, "HTTP/1.0 200\r\n\r\n");
		while (ifs.good())
		{
			stream->write(c);
			c = ifs.get();
		}
		stream->flush();

		shutdownStream(stream);
	}

	void connectionClosed(Stream* stream, bool abnormal)
	{
		cerr << stream << " Connection closed to " << stream->getTargetName() << endl;
	}

protected:
	Stream* listenStream;
};


int main(int argc, char* argv[])
{
	try
	{
		WebServer webServer(argc > 1 ? argv[1] : "8080");
		webServer.run();
	}
	catch (const DashelException& e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
