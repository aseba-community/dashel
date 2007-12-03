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

#ifndef __DaSHEL_H
#define __DaSHEL_H

#include <string>
#include <list>

/*!	\file streams.h
	\brief Public interface of DaSHEL, A cross-platform DAta Stream Helper Encapsulation Library
*/

/**
	\mainpage Streams
	
	Stéphane Magnenat (http://stephane.magnenat.net),
	Mobots group - Laboratory of Robotics Systems, EPFL, Lausanne (http://mobots.epfl.ch) \n
	Sebastion Gerlach,
	Kenzan Technologies (http://www.kenzantech.com)
	
	\section IntroSec Introduction
	
	DaSHEL is a cross-platform DAta Stream Helper Encapsulation Library.
	It provides a unified access to TCP/IP, serial port, and files streams.
	It also allows a server application to wait for any activity
	on any combination of those streams.
	
	Streams is licensed under a modified BSD license, which is a permissive open source license.
	Yet, if you find bugs or do some improvements, please let us know.
	
	\section TargetNamingSec Targets Naming
	
	To construct a new Client, or to listen connections in a Server, you have to specify a target.
	A target is a string which describes a file, a TCP/IP address/port, or a serial port.
	This string consists of the type of the target followed by a comma separated list of parameters.
	This list is contains key-values pair, with a predifined order such that keys can be omitted (but if a key is present, all subsequent entries must have an explicit key).
	Its general syntax is thus \c "protocol:[param1key=]param1value:...:[paramNkey=]paramNvalue".
	
	The following protocols are available:
	\li \c file : local files
	\li \c tcp : TCP/IP
	\li \c ser : serial port
	
	The file protocol accepts only one key:
	\li \c name : name of the file, including the path
	
	The tcp protocol accepts the following keys, in this implicit order:
	\li \c address : IPV4 address
	\li \c port : port
	
	The ser protocol accepts the following keys, in this implicit order:
	\li \c port : serial port number, starting from 1, default 1
	\li \c baud : baud rate, default 115200
	\li \c stop : stop bits count (1 or 2), default 1
	\li \c parity : parity type (none, even, odd, mark, space), default none
	\li \c fc : flow control type, (none, soft, hard), default none
	
	The serial port is always configured for 8 bits transfers
	
	\section UsageSec Usage
	
	Streams is very easy to use. Just add streams.h and streams.cpp to your project and enjoy.
*/

//! Streams, a cross-platform stream abstraction library
namespace Streams
{
	//! A data stream, with low-level (not-endian safe) read/write functions
	class Stream
	{
	public:
		//! Virtual destructor, to ensure calls to destructors of sub-classes.
		virtual ~Stream() {}
		
		/**
			Writes data to the stream.
			Blocks until it writes all requested data or until an InputOutputError exception occurs.
			May not write data to physical media until a call to flush().
			
			\param data pointer to the data to write
			\param size amount of data to write
		*/
		virtual void write(const void *data, const size_t size) = 0;
		
		/**
			Flushes stream.
			A call to this function is required to ensure a physical writing of data;
			it may return an InputOutputError exception.
		*/
		virtual void flush() = 0;
		
		/**
			Reads data from the stream.
			Blocks until it reads all requested data or until an InputOutputError exception occurs.
			
			\param data pointer to store the data to.
			\param size amount of data to read.
		*/
		virtual void read(void *data, size_t size) = 0;
		
		/**
			Returns the name of the target
			
			\return name of the target
		*/
		virtual std::string getTargetName() = 0;
	};
	
	//! An exception related to a stream
	struct StreamException
	{
		/**
			Constructor.
			
			\param stream faulty stream
		*/
		StreamException(Stream *stream) : stream(stream) { }
		Stream *stream; //!< faulty stream
	};
	
	//! An input/output operation was attempted on a closed stream
	struct ConnectionClosed: StreamException
	{
		/**
			Constructor.
			
			\param stream faulty stream
		*/
		ConnectionClosed(Stream *stream): StreamException(stream) { }
	};
	
	//! An input/output operation on a stream produced an error
	struct InputOutputError: StreamException
	{
		/**
			Constructor.
			
			\param stream faulty stream
		*/
		InputOutputError(Stream *stream): StreamException(stream) { }
	};
	
	//! An exception during a stream creation (connection to target)
	struct ConnectionError
	{
		/**
			Constructor.
			
			\param target faulty target
		*/
		ConnectionError(const std::string &target) : target(target) { }
		std::string target; //!< faulty target
	};
	
	//! The specified target is invalid
	struct InvalidTargetDescription: ConnectionError
	{
		/**
			Constructor.
			
			\param target invalid target description
		*/
		InvalidTargetDescription(const std::string &target) : ConnectionError(target) { }
	};
	
	//! The system-specific synchronization primitive produced an error
	struct SynchronizationError
	{
	};
	
	/**
		A client that connects to a target.
		To create a client connection, users of the library have to subclass Client
		and implement connectionEstablished(), incomingData(), and connectionClosed().
	*/
	class Client
	{
	protected:
		Stream *stream; //!< stream to the target
		bool isRunning; //!< true while inside run()
		
	public:
		/**
			Constructor, connects to a target.
			May throw a ConnectionError exception if the target does not exists or is not ready.
			
			\param target destination of connection (see Section \ref TargetNamingSec)
		*/
		Client(const std::string &target);
		
		//! Destructor, closes connections with the target.
		virtual ~Client();
	
		//! Runs and returns only when the target closes the connection.
		void run();
		
		/**
			Waits for data from the stream.
			
			\param timeout if -1, waits until data arrive. If 0, do not wait, just poll for activity. If positive, waits at maximum timeout us.
			\return true if there was activity on the stream, false otherwise
		*/
		bool step(int timeout = 0);

	protected:
		/**
			Called when connect succeeds.
			If the stream is closed during this method, an exception occurs: Client stops the execution of this
			method and calls connectionClosed().
			Subclass must implement this method.
			
			\param stream stream to the target
		*/
		virtual void connectionEstablished(Stream *stream) = 0;
		
		/**
			Called when data is available for reading on the stream.
			If the stream is closed during this method, an exception occurs: Client stops the execution of this
			method and calls connectionClosed().
			Subclass must implement this method.
			
			\param stream stream to the target
		*/
		virtual void incomingData(Stream *stream) = 0;
		
		/**
			Called when connection is closed on socket.
			The only valid method to call on the stream is getTargetName(), input/output operations are forbidden.
			Subclass must implement this method.
			
			\param stream stream to the target
		*/
		virtual void connectionClosed(Stream *stream) = 0;
	};
	
	/**
		A server that listens for incoming connections and maintains a list of
		targets.
		To create a client connection, users of the library have to subclass Server
		and implement incomingConnection(), incomingData(), and connectionClosed().
	*/
	class Server
	{
	protected:
		std::list<Stream*> listenStreams; //!< streams listening for incoming connections.
		std::list<Stream*> transferStreams; //!< streams for transfering data with targets.
	
	public:
		//! Destructor, closes all connections.
		virtual ~Server();
		
		/**
			Listens for incoming connections on a target.
			Some targets, such as a serial ports and files may directly generate a new connection;
			others, such as network interfaces, will only generate news connections when a peer
			connects.
			May throw a ConnectionError exception if the target does not exists or is not ready.
			
			\param target destination to listen connections from (see Section \ref TargetNamingSec)
		*/
		void listen(const std::string &target);
		
		//! Runs and returns only when an external event requests the application to stop.
		void run(void);
		
		/**
			Waits for data from the transfers streams or connections from the listening streams.
		
			\param timeout if -1, waits until data arrive. If 0, do not wait, just poll for activity. If positive, waits at maximum timeout us.
			\return true if there was activity on the stream, false otherwise
		*/
		bool step(int timeout = 0);

	protected:
		/**
			Called when a new connection is established.
			If the stream is closed during this method, an exception occurs: Server stops the execution of this
			method and calls connectionClosed().
			Subclass must implement this method.
			
			\param stream stream to the target
		*/
		virtual void incomingConnection(Stream *stream) = 0;
		
		/**
			Called when data is available for reading on the stream.
			If the stream is closed during this method, an exception occurs: Server stops the execution of this
			method and calls connectionClosed().
			Subclass must implement this method.
			
			\param stream stream to the target
		*/
		virtual void incomingData(Stream *stream) = 0;
		
		/**
			Called when connection is closed on socket.
			The only valid method to call on the stream is getTargetName(), input/output operations are forbidden.
			Subclass must implement this method.
			
			\param stream stream to the target
		*/
		virtual void connectionClosed(Stream *stream) = 0;
	};
}

#endif
