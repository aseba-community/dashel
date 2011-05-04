/*
	Dashel
	A cross-platform DAta Stream Helper Encapsulation Library
	Copyright (C) 2007 -- 2011:
		
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

#ifndef __Dashel_H
#define __Dashel_H

#include <string>
#include <set>
#include <map>
#include <vector>
#include <deque>
#include <stdexcept>

/*!	\file dashel.h
	\brief Public interface of Dashel, A cross-platform DAta Stream Helper Encapsulation Library
*/

/**
	\mainpage Dashel
	
	Stéphane Magnenat (http://stephane.magnenat.net),
	Mobots group - Laboratory of Robotics Systems, EPFL, Lausanne (http://mobots.epfl.ch) \n
	Sebastion Gerlach,
	Kenzan Technologies (http://www.kenzantech.com)\n
	Antoine Beyeler (http://www.ab-ware.com),
	Laboratory of Intelligent Systems, EPFL, Lausanne (http://lis.epfl.ch)
	
	
	\section IntroSec Introduction
	
	Dashel is a cross-platform data stream helper encapsulation library.
	It provides a unified access to TCP/UDP sockets, serial ports, console, and files streams.
	It also allows a server application to wait for any activity on any combination of these streams.
	
	Dashel is licensed under a modified BSD license, which is a permissive open source license.
	Yet, if you find bugs or do some improvements, please let us know.
	
	\section Usage
	
	To use Dashel, you have to instanciate a Dashel::Hub.
	The Hub is your connection with the data streams.
	It is the place where you create, destroy, and synchronize them.
	
	The \c example directory in Dashel distribution provides several working examples that
	you can read to learn to use Dashel.
	
	\section TargetNamingSec Targets Naming
	
	In Dashel, streams connect to targets.
	A target is a string that describes a file, a TCP/UDP address/port, or a serial port.
	This string consists of the type of the target, a colon, followed by a semicolon separated list of parameters.
	This list contains key-values pairs, with a predifined order such that keys can be omitted (but if a key is present, all subsequent entries must have an explicit key).
	Its general syntax is thus \c "protocol:[param1key=]param1value;...;[paramNkey=]paramNvalue".
	
	The following protocols are available:
	\li \c file : local files
	\li \c tcp : TCP/IP client
	\li \c tcpin : TCP/IP server
	\li \c udp : UDP/IP
	\li \c ser : serial port
	\li \c stdin : standard input
	\li \c stdout : standard output
	
	The file protocol accepts the following parameters, in this implicit order:
	\li \c name : name of the file, including the path
	\li \c mode : mode (read, write)
	
	The tcp protocol accepts the following parameters, in this implicit order:
	\li \c host : host
	\li \c port : port
	
	The tcpin protocol accepts the following parameters, in this implicit order:
	\li \c port : port
	\li \c address : if the computer possesses multiple network addresses, the one to listen on, default 0.0.0.0 (any)
	
	The udp protocol accepts the following parameters, in this implicit order:
	\li \c port : port
	\li \c address : if the computer possesses multiple network addresses, the one to connect to, default 0.0.0.0 (any)
	
	The ser protocol accepts the following parameters, in this implicit order:
	\li \c device : serial port device name, system specific; either port or device must be given, device has priority if both are given.
	\li \c name : select the port by matching part of the serial port "user-friendly" description. The match is case-sensitive. Works on Linux and Windows (note: on Linux, this feature requires libudev).
	\li \c port : serial port number, starting from 1, default 1; either port or device must be given, device has priority if both are given (note: on Linux, this feature requires libhal).
	\li \c baud : baud rate, default 115200
	\li \c stop : stop bits count (1 or 2), default 1
	\li \c parity : parity type (none, even, odd), default none
	\li \c fc : flow control type, (none, hard), default none
	\li \c bits : number of bits per character, default 8
	
	Protocols \c stdin and \c stdout do not take any parameter.
*/

//! Dashel, a cross-platform stream abstraction library
namespace Dashel
{
	class Stream;
	
	//! version of the Dashel library as string
	#define DASHEL_VERSION "1.0.4"
	//! version of the Dashel library as an int
	#define DASHEL_VERSION_INT 10004
	
	//! The one size fits all exception for streams.
	/*!
		The reason of the failure is stored in the runtime error, and is returned by what()
	*/
	class DashelException: public std::runtime_error
	{
	public:
		//! The different exception causes.
		typedef enum {
			Unknown,			//!< Well, hopefully never used.
			SyncError,			//!< Some synchronisation error.
			InvalidTarget,		//!< The target string was bad.
			InvalidOperation,	//!< The operation is not valid on this stream.
			ConnectionLost,		//!< The connection was lost.
			IOError,			//!< Some I/O error.
			ConnectionFailed,	//!< The connection could not be established.
			EnumerationError,	//!< Some serial enumeration error
			PreviousIncomingDataNotRead //!< The incoming data was not read by the Hub subclass
		} Source;

		//! The exception cause.
		Source source;
		//! The reason as an OS error code.
		int sysError;
		//! The stream that caused the exception to be thrown.
		Stream *stream;

	public:
		//! Construct an stream exception with everything.
		/*!	\param s Source of failure
			\param se System error code.
			\param reason The logical reason as a human readable string.
			\param stream Stream to which exception applies.
		*/
		DashelException(Source s, int se, const char *reason, Stream* stream = NULL);
	};
	
	//! Serial port enumerator class.
	/*! This class is just a package for one static method.
	*/
	class SerialPortEnumerator
	{
	public:
		//! Retrieve list of all serial ports available on system.
		/*! This function queries the Operating System for all available serial ports.
			\return A map where the key is the port number name as passed to the ser: protocol, and
			the value is a pair of the system device name and a human readable description
			that may be displayed in a user interface.
			All strings are encoded in UTF-8.
		*/
		static std::map<int, std::pair<std::string, std::string> > getPorts();
	};
	
	//! A IP version 4 address
	class IPV4Address
	{
	public:
		unsigned address; //!< IP host address. Stored in local byte order.
		unsigned short port; //!< IP port. Stored in local byte order.
	
	public:
		//! Constructor. Numeric argument
		IPV4Address(unsigned addr = 0, unsigned short prt = 0);
		
		//! Constructor. String address, do resolution
		IPV4Address(const std::string& name, unsigned short port);
	
		//! Equality operator
		bool operator==(const IPV4Address& o) const;
		
		//! Less than operator
		bool operator<(const IPV4Address& o) const;
		
		//! Return Dashel string form
		/*!
			@param resolveName whether we should attempt resolving the host name of the address
		*/
		std::string format(const bool resolveName = true) const;
		
		//! Return the hostname corresponding to the address
		std::string hostname() const;
		
		//! Is the address valid?
		//bool isValid() const;
	};
	
	//! Parameter set.
	class ParameterSet
	{
	private:
		std::map<std::string, std::string> values;
		std::vector<std::string> params;

	public:
		//! Add values to set.
		void add(const char *line);
		
		//! Add a parameter to a set.
		/*!
		* 	@param param name of the parameter
		* 	@param value value of the parameter or NULL (then parameter will default or pre-existing value) 
		* 	@param atStart if true, insert parameter at start of the list
		*/
		void addParam(const char *param, const char *value = NULL, bool atStart = false);
		
		//! Return whether a key is set or not
		bool isSet(const char *key) const;

		//! Get a parameter value
		template<typename T> T get(const char *key) const;

		//! Get a parameter value
		const std::string& get(const char *key) const;
		
		//! Get the parameters as string.
		std::string getString() const;
		
		//! Erase the parameter from the set
		void erase(const char *key);
	};

	//! A data stream, with low-level (not-endian safe) read/write functions
	class Stream
	{
	private:
		//! A flag indicating that the stream has failed.
		bool failedFlag;
		//! The human readable reason describing why the stream has failed.
		std::string failReason;

	protected:
		//! The target description.
		ParameterSet target;
		//! The protocol name.
		std::string protocolName;
		
	protected:
		
		friend class Hub;
		
		//! Constructor.
		Stream(const std::string& protocolName) : failedFlag(false), protocolName(protocolName) {}
	
		//! Virtual destructor, to ensure calls to destructors of sub-classes.
		virtual ~Stream() {}
	
	public:	
		//! Set stream to failed state
		/*!	\param s Source of failure
			\param se System error code
			\param reason The logical reason as a human readable string.
		*/
		void fail(DashelException::Source s, int se, const char* reason);
		
		//! Query failed state of stream.
		/*! \return true if stream has failed.
		*/
		bool failed() const { return failedFlag; }
		
		//!	Returns the reason the stream has failed.
		/*!	\return the reason the stream has failed, or an empty string if fail() is false.
		*/
		const std::string &getFailReason() const { return failReason; }
		
		//! Returns the protocol name of the stream.
		const std::string &getProtocolName() const { return protocolName; }
		
		//!	Returns the name of the target.
		/*!	The name of the target contains all parameters and the protocol name.
		
			\return Name of the target
		*/
		std::string getTargetName() const { return protocolName + ":" + target.getString(); }
		
		//! Returns the value of a parameter extracted from the target.
		/*! \param param the name of the parameter
			\return A string containing the parameter.
		*/
		const std::string &getTargetParameter(const char *param) const { return target.get(param); }
		
		//!	Write data to the stream.
		/*!	Writes all requested data to the stream, blocking until all the data has been written, or 
			some error occurs. Errors are signaled by throwing a DashelException exception. This function
			does not flush devices, therefore the data may not really have been written on return, but only
			been buffered. In order to flush the stream, call flush().
			
			\param data Pointer to the data to write.
			\param size Amount of data to write in bytes.
		*/
		virtual void write(const void *data, const size_t size) = 0;
		
		//! Write a variable of basic type to the stream
		/*! This function does not perform any endian conversion.
			\param v variable to write.
		*/
		template<typename T> void write(T v)
		{
			write(&v, sizeof(T));
		}
		
		//!	Flushes stream.
		/*!	Calling this function requests the stream to be flushed, this may ensure that data is written
			to physical media or actually sent over a wire. The exact performed function depends on the
			stream type and operating system.
		*/
		virtual void flush() = 0;
		
		//!	Reads data from the stream.
		/*!	Reads all requested data from the stream, blocking until all the data has been read, or 
			some error occurs. Errors are signaled by throwing a DashelException exception, which may
			be caused either by device errors or reaching the end of file. 
			
			\param data Pointer to the memory where the read data should be stored.
			\param size Amount of data to read in bytes.
		*/
		virtual void read(void *data, size_t size) = 0;
		
		//! Read a variable of basic type from the stream
		/*! This function does not perform any endian conversion.
		
			\return variable to read.
		*/
		template<typename T> T read()
		{
			T v;
			read(&v, sizeof(T));
			return v;
		}
	};
	
	//! A data stream, that can be later send data as at UDP packet or read data from an UDP packet
	/*!
		You can use PacketStream to write and read data as with normal stream, with the difference
		that:
		* written byte will be collected in a send buffer until send() is called with the destination
		address; if you have written too much byte for send to transmit all of them an exception will occur.
		However, the underlying operating system may pretend that all data has been transmitted while discarding some of it anyway. In any case, send less bytes than ethernet MTU minus UDP header.
		* you have to call receive() when there are bytes available on the stream to be able to read them; if your read past the received bytes an exception will occur.
	*/
	class PacketStream: virtual public Stream
	{
	public:
		//! Constructor
		PacketStream(const std::string& protocolName) : Stream(protocolName) { }
	
		//! Send all written data to an IP address in a single packet.
		/*!
			\param dest IP address to send packet to
		*/
		virtual void send(const IPV4Address& dest) = 0;
		
		//! Receive a packet and make its payload available for reading.
		/*!
			Block until a packet is available.
			
			\param source IP address from where the packet originates.
		*/
		virtual void receive(IPV4Address& source) = 0;
	};
	
	/**
		The central place where to create, destroy, and synchronize streams.
		To create a client connection, users of the library have to subclass Hub
		and implement incomingConnection(), incomingData(), and connectionClosed().
	*/
	class Hub
	{
	public:
		//! A list of streams
		typedef std::set<Stream*> StreamsSet;
		
	private:
		void *hTerminate;			//!< Set when this thing goes down.
		void *streamsLock;			//!< Platform-dependant mutex to protect access to streams
		StreamsSet streams; 		//!< All our streams.
	
	protected:
		StreamsSet dataStreams;		//!< All our streams that transfer data (in opposition to streams that just listen for data).
		const bool resolveIncomingNames; //!< Whether Dashel should try to resolve the peer's hostname of incoming TCP connections
	
	public:
		/** Constructor.
			\param resolveIncomingNames if true, try to resolve the peer's hostname of incoming TCP connections
		*/
		Hub(const bool resolveIncomingNames = true);
	
		//! Destructor, closes all connections.
		virtual ~Hub();
		
		/**
			Listens for incoming connections on a target.
			Some targets, such as a serial ports and files may directly generate a new connection;
			others, such as network interfaces, will only generate news connections when a peer
			connects.
			May throw a ConnectionError exception if the target does not exists or is not ready.
			
			\param target destination to listen connections from (see Section \ref TargetNamingSec)
			\return the stream we are connected to; if connect was not possible, an exception was throw.
		*/
		Stream* connect(const std::string &target);
		
		/**
			Close a stream, remove it from the Hub, and delete it.
			If the stream is not present in the Hub, it is deleted nevertheless.
			Note that connectionClosed() is not called by closeStream() and that
			you must not call closeStream(s) from inside connectionClosed(s) for
			the same stream.
			
			\param stream stream to remove
		*/
		void closeStream(Stream* stream);
		
		/** Runs and returns only when an external event requests the application to stop.
		*/
		void run();
		
		/**
			Waits for data from the transfers streams or connections from the listening streams.
			Read all available data.
		
			\param timeout if -1, waits until data arrive. If 0, do not wait, just poll for activity. If positive, waits at maximum timeout ms.
			\return false if stop() was called or the application was requested to terminate, true otherwise.
		*/
		bool step(const int timeout = 0);
		
		//! Stops running, subclasses or external code may call this function, that is the only thread-safe function of the Hub
		void stop();
		
		/** Block any hub processing so another thread can access the streams safely.
			Currently only implemented on POSIX platform, no-op on others.
		 */
		void lock();
		
		/** Release the lock aquired by lock().
			Currently only implemented on POSIX platform, no-op on others.
		*/
		void unlock();

	protected:
		
		/**
			Called when any data connection is created.
			It is not called when a listening connection (eg tcpin:) is created.
			If the stream is closed during this method, an exception occurs: the caller is responsible to handle it.
			The stream is already inserted in the stream list when this function is called.
			Subclass can implement this method.
			Called with the stream lock held.
			
			\param stream stream to the target
		*/
		virtual void connectionCreated(Stream * /* stream */) { }
		
		/**
			Called when data is available for reading on the stream.
			If the stream is closed during this method, an exception occurs: Hub stops the execution of this
			method and calls connectionClosed(); objects dynamically allocated must thus be handled
			with auto_ptr.
			If step() is used, subclass must implement this method and call read at least once.
			Called with the stream lock held.
			
			\param stream stream to the target
		*/
		virtual void incomingData(Stream * /* stream */) { }
		
		/**
			Called when target closes connection.
			The only valid method to call on the stream is getTargetName(), input/output operations are forbidden.
			You must not call closeStream(stream) from within this method for the same stream as the
			one passed as parameter.
			Subclass can implement this method.
			Called with the stream lock held.
			
			\param stream stream to the target.
			\param abnormal whether the connection was closed during step (abnormal == false) or when an operation was performed (abnormal == true)
		*/
		virtual void connectionClosed(Stream * /* stream */, bool /* abnormal */) { }
	};
}

#endif
