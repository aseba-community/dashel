/*
	Dashel
	A cross-platform DAta Stream Helper Encapsulation Library
	Copyright (C) 2007 -- 2018:

		Stephane Magnenat <stephane at magnenat dot net>
			(http://stephane.magnenat.net)
		Mobots group - Laboratory of Robotics Systems, EPFL, Lausanne
			(http://mobots.epfl.ch)

		Sebastian Gerlach
		Kenzan Technologies
			(http://www.kenzantech.com)

		and other contributors, see readme.md file for details.

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

#include <string.h>
#include <cassert>
#include <cstdlib>
#include <map>
#include <vector>
#include <valarray>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <netinet/in.h>

// clang-format off
#ifdef __APPLE__
	#define MACOSX
#endif

#ifdef MACOSX
	#define USE_POLL_EMU
#endif

#ifdef MACOSX
	#include <CoreFoundation/CoreFoundation.h>
	#include "TargetConditionals.h"
	#if TARGET_OS_IPHONE == 0
		#include <IOKit/IOKitLib.h>
		#include <IOKit/serial/IOSerialKeys.h>
	#endif
#endif

#ifdef USE_LIBUDEV
extern "C" {
	#include <libudev.h>
}
#endif

#ifdef USE_HAL
	#include <hal/libhal.h>
#endif

#ifndef USE_POLL_EMU
	#include <poll.h>
#else
	#include "poll_emu.h"
#endif
// clang-format on

#include "dashel-private.h"
#include "dashel-posix.h"

#define RECV_BUFFER_SIZE 4096


/*!	\file streams.cpp
	\brief Implementation of Dashel, A cross-platform DAta Stream Helper Encapsulation Library
*/

namespace Dashel
{
	using namespace std;

	// Exception

	void Stream::fail(DashelException::Source s, int se, const char* reason)
	{
		string sysMessage;
		failedFlag = true;

		if (se)
			sysMessage = strerror(errno);

		failReason = reason;
		failReason += " ";
		failReason += sysMessage;

		throw DashelException(s, se, failReason.c_str(), this);
	}

	// Serial port enumerator

	std::map<int, std::pair<std::string, std::string> > SerialPortEnumerator::getPorts()
	{
		std::map<int, std::pair<std::string, std::string> > ports;


#if defined MACOSX && TARGET_OS_IPHONE == 0
		// use IOKit to enumerates devices

		// get a matching dictionary to specify which IOService class we're interested in
		CFMutableDictionaryRef classesToMatch = IOServiceMatching(kIOSerialBSDServiceValue);
		if (classesToMatch == NULL)
			throw DashelException(DashelException::EnumerationError, 0, "IOServiceMatching returned a NULL dictionary");

		// specify all types of serial devices
		CFDictionarySetValue(classesToMatch, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDAllTypes));

		// get an iterator to serial port services
		io_iterator_t matchingServices;
		kern_return_t kernResult = IOServiceGetMatchingServices(kIOMasterPortDefault, classesToMatch, &matchingServices);
		if (KERN_SUCCESS != kernResult)
			throw DashelException(DashelException::EnumerationError, kernResult, "IOServiceGetMatchingServices failed");

		// iterate over services
		io_object_t modemService;
		int index = 0;
		while ((modemService = IOIteratorNext(matchingServices)))
		{
			// get path for device
			CFTypeRef bsdPathAsCFString = IORegistryEntryCreateCFProperty(modemService, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
			if (bsdPathAsCFString)
			{
				std::string path;
				char cStr[255];
				std::string name;

				bool res = CFStringGetCString((CFStringRef)bsdPathAsCFString, cStr, 255, kCFStringEncodingUTF8);
				if (res)
					path = cStr;
				else
					throw DashelException(DashelException::EnumerationError, 0, "CFStringGetCString failed");

				CFRelease(bsdPathAsCFString);

				CFTypeRef fn = IORegistryEntrySearchCFProperty(modemService, kIOServicePlane, CFSTR("USB Product Name"), kCFAllocatorDefault, kIORegistryIterateRecursively | kIORegistryIterateParents);
				if (fn)
				{
					res = CFStringGetCString((CFStringRef)fn, cStr, 255, kCFStringEncodingUTF8);
					if (res)
						name = cStr;
					else
						throw DashelException(DashelException::EnumerationError, 0, "CFStringGetString failed");

					CFRelease(fn);
				}
				else
					name = "Serial Port";
				name = name + " (" + path + ")";
				ports[index++] = std::make_pair(path, name);
			}
			else
				throw DashelException(DashelException::EnumerationError, 0, "IORegistryEntryCreateCFProperty returned a NULL path");

			// release service
			IOObjectRelease(modemService);
		}

		IOObjectRelease(matchingServices);


#elif defined(USE_LIBUDEV)

		struct udev* udev;
		struct udev_enumerate* enumerate;
		struct udev_list_entry *devices, *dev_list_entry;
		struct udev_device* dev;
		int index = 0;

		udev = udev_new();
		if (!udev)
			throw DashelException(DashelException::EnumerationError, 0, "Cannot create udev context");

		enumerate = udev_enumerate_new(udev);
		udev_enumerate_add_match_subsystem(enumerate, "tty");
		udev_enumerate_scan_devices(enumerate);
		devices = udev_enumerate_get_list_entry(enumerate);

		udev_list_entry_foreach(dev_list_entry, devices)
		{
			const char* sysfs_path;
			struct udev_device* usb_dev;
			const char* path;
			struct stat st;
			unsigned int maj, min;

			/* Get sysfs path and create the udev device */
			sysfs_path = udev_list_entry_get_name(dev_list_entry);
			dev = udev_device_new_from_syspath(udev, sysfs_path);

			// Some sanity check
			path = udev_device_get_devnode(dev);
			if (stat(path, &st))
				throw DashelException(DashelException::EnumerationError, 0, "Cannot stat serial port");

			if (!S_ISCHR(st.st_mode))
				throw DashelException(DashelException::EnumerationError, 0, "Serial port is not character device");

			// Get the major/minor number
			maj = major(st.st_rdev);
			min = minor(st.st_rdev);

			// Ignore all the non physical ports
			if (!(maj == 2 || (maj == 4 && min < 64) || maj == 3 || maj == 5))
			{
				ostringstream oss;

				// Check if usb, if yes get the device name
				usb_dev = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
				if (usb_dev)
					oss << udev_device_get_sysattr_value(usb_dev, "product");
				else
					oss << "Serial Port";

				oss << " (" << path << ")";

				ports[index++] = std::make_pair<std::string, std::string>(path, oss.str());
			}
			udev_device_unref(dev);
		}

		udev_enumerate_unref(enumerate);

		udev_unref(udev);

#elif defined(USE_HAL)

		// use HAL to enumerates devices
		DBusConnection* dbusConnection = dbus_bus_get(DBUS_BUS_SYSTEM, 0);
		if (!dbusConnection)
			throw DashelException(DashelException::EnumerationError, 0, "cannot connect to D-BUS.");

		LibHalContext* halContext = libhal_ctx_new();
		if (!halContext)
			throw DashelException(DashelException::EnumerationError, 0, "cannot create HAL context: cannot create context");
		if (!libhal_ctx_set_dbus_connection(halContext, dbusConnection))
			throw DashelException(DashelException::EnumerationError, 0, "cannot create HAL context: cannot connect to D-BUS");
		if (!libhal_ctx_init(halContext, 0))
			throw DashelException(DashelException::EnumerationError, 0, "cannot create HAL context: cannot init context");

		int devicesCount;
		char** devices = libhal_find_device_by_capability(halContext, "serial", &devicesCount, 0);
		for (int i = 0; i < devicesCount; i++)
		{
			char* devFileName = libhal_device_get_property_string(halContext, devices[i], "serial.device", 0);
			char* info = libhal_device_get_property_string(halContext, devices[i], "info.product", 0);
			int port = libhal_device_get_property_int(halContext, devices[i], "serial.port", 0);

			ostringstream oss;
			oss << info << " " << port;
			ports[devicesCount - i] = std::make_pair<std::string, std::string>(devFileName, oss.str());

			libhal_free_string(info);
			libhal_free_string(devFileName);
		}

		libhal_free_string_array(devices);
		libhal_ctx_shutdown(halContext, 0);
		libhal_ctx_free(halContext);
#endif

		return ports;
	};

	// Asserted dynamic cast

	//! Asserts a dynamic cast.	Similar to the one in boost/cast.hpp
	template<typename Derived, typename Base>
	inline Derived polymorphic_downcast(Base base)
	{
		Derived derived = dynamic_cast<Derived>(base);
		assert(derived);
		return derived;
	}

	// Streams

	SelectableStream::SelectableStream(const string& protocolName) :
		Stream(protocolName),
		fd(-1),
		writeOnly(false),
		pollEvent(POLLIN)
	{
	}

	SelectableStream::~SelectableStream()
	{
		// on POSIX, do not close stdin, stdout, nor stderr
		if (fd >= 3)
			close(fd);
	}

	//! In addition its parent, this stream can also make select return because of the target has disconnected
	class DisconnectableStream : public SelectableStream
	{
	protected:
		friend class Hub;
		unsigned char recvBuffer[RECV_BUFFER_SIZE]; //!< reception buffer
		size_t recvBufferPos; //!< position of read in reception buffer
		size_t recvBufferSize; //!< amount of data in reception buffer

	public:
		//! Create the stream and associates a file descriptor
		explicit DisconnectableStream(const string& protocolName) :
			Stream(protocolName),
			SelectableStream(protocolName),
			recvBufferPos(0),
			recvBufferSize(0)
		{
		}

		//! Return true while there is some unread data in the reception buffer
		virtual bool isDataInRecvBuffer() const { return recvBufferPos != recvBufferSize; }
	};

	//! Assign a socket file descriptor to a target. Factored out from SocketStream::SocketStream.
	//! If the target specifies a socket with a nonnegative "sock=N" parameter, assume it is valid
	//! and use it. Otherwise, the host and port parameters are used to look up a TCP/IP host, and
	//! a new socket is created.
	//! Raises an exception if the socket cannot be created, or if the TCP/IP host cannot be reached.
	static int getOrCreateSocket(ParameterSet& target)
	{
		int fd = target.get<int>("sock");
		if (fd < 0)
		{
			// create socket
			fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (fd < 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot create socket.");

			IPV4Address remoteAddress(target.get("host"), target.get<int>("port"));

			// connect
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(remoteAddress.port);
			addr.sin_addr.s_addr = htonl(remoteAddress.address);
			if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot connect to remote host.");

			// overwrite target name with a canonical one
			target.add(remoteAddress.format().c_str());
			target.erase("connectionPort");
		}
		return fd;
	}

	//! Socket, uses send/recv for read/write
	class SocketStream : public DisconnectableStream
	{
	protected:
#ifndef TCP_CORK
		// clang-format off
		//! Socket constants
		enum Consts
		{
			SEND_BUFFER_SIZE_INITIAL = 256, //!< initial size of the socket send sendBuffer
			SEND_BUFFER_SIZE_LIMIT = 65536 //!< when the socket send sendBuffer reaches this size, a flush is forced
		};
		// clang-format on

		ExpandableBuffer sendBuffer;
#endif

	public:
		//! Create a socket stream to the following destination
		explicit SocketStream(const string& targetName) :
			Stream("tcp"),
			DisconnectableStream("tcp")
#ifndef TCP_CORK
			,
			sendBuffer(SEND_BUFFER_SIZE_INITIAL)
#endif
		{
			target.add("tcp:host;port;connectionPort=-1;sock=-1");
			target.add(targetName.c_str());

			fd = getOrCreateSocket(target);
			if (target.get<int>("sock") >= 0)
			{
				// remove file descriptor information from target name
				target.erase("sock");
			}

#ifdef TCP_CORK
			// setup TCP Cork for delayed sending
			int flag = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_CORK, &flag, sizeof(flag));
#endif
		}

		virtual ~SocketStream()
		{
			if (!failed())
				flush();

			if (fd >= 0)
				shutdown(fd, SHUT_RDWR);
		}

		virtual void write(const void* data, const size_t size)
		{
			assert(fd >= 0);

			if (size == 0)
				return;

#ifdef TCP_CORK
			send(data, size);
#else
			if (size >= SEND_BUFFER_SIZE_LIMIT)
			{
				flush();
				send(data, size);
			}
			else
			{
				sendBuffer.add(data, size);
				if (sendBuffer.size() >= SEND_BUFFER_SIZE_LIMIT)
					flush();
			}
#endif
		}

		//! Send all data over the socket
		void send(const void* data, size_t size)
		{
			assert(fd >= 0);

			unsigned char* ptr = (unsigned char*)data;
			size_t left = size;

			while (left)
			{
#ifdef MACOSX
				ssize_t len = ::send(fd, ptr, left, 0);
#else
				ssize_t len = ::send(fd, ptr, left, MSG_NOSIGNAL);
#endif

				if (len < 0)
				{
					fail(DashelException::IOError, errno, "Socket write I/O error.");
				}
				else if (len == 0)
				{
					fail(DashelException::ConnectionLost, 0, "Connection lost.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}

		virtual void flush()
		{
			assert(fd >= 0);

#ifdef TCP_CORK
			int flag = 0;
			setsockopt(fd, IPPROTO_TCP, TCP_CORK, &flag, sizeof(flag));
			flag = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_CORK, &flag, sizeof(flag));
#else
			send(sendBuffer.get(), sendBuffer.size());
			sendBuffer.clear();
#endif
		}

		virtual void read(void* data, size_t size)
		{
			assert(fd >= 0);

			if (size == 0)
				return;

			unsigned char* ptr = (unsigned char*)data;
			size_t left = size;

			if (isDataInRecvBuffer())
			{
				size_t toCopy = std::min(recvBufferSize - recvBufferPos, size);
				memcpy(ptr, recvBuffer + recvBufferPos, toCopy);
				recvBufferPos += toCopy;
				ptr += toCopy;
				left -= toCopy;
			}

			while (left)
			{
				ssize_t len = recv(fd, ptr, left, 0);

				if (len < 0)
				{
					fail(DashelException::IOError, errno, "Socket read I/O error.");
				}
				else if (len == 0)
				{
					fail(DashelException::ConnectionLost, 0, "Connection lost.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}

		virtual bool receiveDataAndCheckDisconnection()
		{
			assert(recvBufferPos == recvBufferSize);

			ssize_t len = recv(fd, &recvBuffer, RECV_BUFFER_SIZE, 0);
			if (len > 0)
			{
				recvBufferSize = len;
				recvBufferPos = 0;
				return false;
			}
			else
			{
				if (len < 0)
					fail(DashelException::IOError, errno, "Socket read I/O error.");
				return true;
			}
		}
	};

	//! Poll a socket file descriptor for either a local Unix domain socket (tcppoll:sock=N) or a
	//! remote TCP/IP socket (tcppoll:host=HOST;port=PORT). Delegates fd choice to getOrCreateSocket.
	//! Poll streams are used to include sockets that will be read or written by client code in the
	//! Dashel polling loop. Dashel itself neither reads from nor writes to the socket. PollStream will
	//! call Hub::incomingData(stream) exactly once when its socket is polled with POLLIN in Hub::step.
	class PollStream : public SelectableStream
	{
	public:
		explicit PollStream(const std::string& targetName) :
			Stream(targetName),
			SelectableStream(targetName)
		{
			target.add("tcppoll:host;port;connectionPort=-1;sock=-1");
			target.add(targetName.c_str());
			fd = getOrCreateSocket(target);
			dtorCloseSocket = target.get<int>("sock") < 0 && fd >= 0; // if getOrCreateSocket created the socket we will have to close it
		}
		~PollStream()
		{
			// if file descriptor doesn't belong to this stream, don't let the base class close it
			// note that SelectableStream::~SelectableStream only closes if fd >= 3
			if (!dtorCloseSocket)
				fd = 0;
		}
		// clang-format off
		virtual void write(const void* data, const size_t size) { /* hook for use by derived classes */ }
		virtual void flush() { /* hook for use by derived classes */ }
		virtual void read(void* data, size_t size) { /* hook for use by derived classes */ }
		virtual bool receiveDataAndCheckDisconnection() { edgeTrigger = true; return false; }
		virtual bool isDataInRecvBuffer() const { bool ret = edgeTrigger; edgeTrigger = false; return ret; }
		// clang-format on
	private:
		mutable bool edgeTrigger;
		bool dtorCloseSocket;
	};

	//! Socket server stream.
	/*! This stream is used for listening for incoming connections. It cannot be used for transfering
		data.
	*/
	class SocketServerStream : public SelectableStream
	{
	public:
		//! Create the stream and associates a file descriptor
		explicit SocketServerStream(const std::string& targetName) :
			Stream("tcpin"),
			SelectableStream("tcpin")
		{
			target.add("tcpin:port=5000;address=0.0.0.0");
			target.add(targetName.c_str());

			IPV4Address bindAddress(target.get("address"), target.get<int>("port"));

			// create socket
			fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (fd < 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot create socket.");

			// reuse address
			int flag = 1;
			if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot set address reuse flag on socket, probably the port is already in use.");

			// bind
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(bindAddress.port);
			addr.sin_addr.s_addr = htonl(bindAddress.address);
			if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot bind socket to port, probably the port is already in use.");

			// retrieve port number, if a dynamic one was requested
			if (bindAddress.port == 0)
			{
				socklen_t sizeof_addr(sizeof(addr));
				if (::getsockname(fd, (struct sockaddr*)&addr, &sizeof_addr) != 0)
					throw DashelException(DashelException::ConnectionFailed, errno, "Cannot retrieve socket port assignment.");
				target.erase("port");
				ostringstream portnum;
				portnum << ntohs(addr.sin_port);
				target.addParam("port", portnum.str().c_str(), true);
			}

			// Listen on socket, backlog is sort of arbitrary.
			if (listen(fd, 16) < 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot listen on socket.");
		}

		// clang-format off
		virtual void write(const void* data, const size_t size) { /* hook for use by derived classes */ }
		virtual void flush() { /* hook for use by derived classes */ }
		virtual void read(void* data, size_t size) { /* hook for use by derived classes */ }
		virtual bool receiveDataAndCheckDisconnection() { return false; }
		virtual bool isDataInRecvBuffer() const { return false; }
		// clang-format on
	};

	//! UDP Socket, uses sendto/recvfrom for read/write
	class UDPSocketStream : public MemoryPacketStream, public SelectableStream
	{
	private:
		mutable bool selectWasCalled;

	public:
		//! Create as UDP socket stream on a specific port
		explicit UDPSocketStream(const string& targetName) :
			Stream("udp"),
			MemoryPacketStream("udp"),
			SelectableStream("udp"),
			selectWasCalled(false)
		{
			target.add("udp:port=5000;address=0.0.0.0;sock=-1");
			target.add(targetName.c_str());

			fd = target.get<int>("sock");
			if (fd < 0)
			{
				// create socket
				fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				if (fd < 0)
					throw DashelException(DashelException::ConnectionFailed, errno, "Cannot create socket.");

				IPV4Address bindAddress(target.get("address"), target.get<int>("port"));

				// bind
				sockaddr_in addr;
				addr.sin_family = AF_INET;
				addr.sin_port = htons(bindAddress.port);
				addr.sin_addr.s_addr = htonl(bindAddress.address);
				if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
					throw DashelException(DashelException::ConnectionFailed, errno, "Cannot bind socket to port, probably the port is already in use.");

				// retrieve port number, if a dynamic one was requested
				if (bindAddress.port == 0)
				{
					socklen_t sizeof_addr(sizeof(addr));
					if (::getsockname(fd, (struct sockaddr*)&addr, &sizeof_addr) != 0)
						throw DashelException(DashelException::ConnectionFailed, errno, "Cannot retrieve socket port assignment.");
					target.erase("port");
					ostringstream portnum;
					portnum << ntohs(addr.sin_port);
					target.addParam("port", portnum.str().c_str(), true);
				}
			}
			else
			{
				// remove file descriptor information from target name
				target.erase("sock");
			}

			// enable broadcast
			int broadcastPermission = 1;
			setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcastPermission, sizeof(broadcastPermission));
		}

		virtual void send(const IPV4Address& dest)
		{
			sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_port = htons(dest.port);
			addr.sin_addr.s_addr = htonl(dest.address);

			ssize_t sent = sendto(fd, sendBuffer.get(), sendBuffer.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
			if (sent < 0 || static_cast<size_t>(sent) != sendBuffer.size())
				fail(DashelException::IOError, errno, "UDP Socket write I/O error.");

			sendBuffer.clear();
		}

		virtual void receive(IPV4Address& source)
		{
			unsigned char buf[4096];
			sockaddr_in addr;
			socklen_t addrLen = sizeof(addr);
			ssize_t recvCount = recvfrom(fd, buf, 4096, 0, (struct sockaddr*)&addr, &addrLen);
			if (recvCount <= 0)
				fail(DashelException::ConnectionLost, errno, "UDP Socket read I/O error.");

			receptionBuffer.resize(recvCount);
			std::copy(buf, buf + recvCount, receptionBuffer.begin());

			source = IPV4Address(ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port));
		}

		virtual bool receiveDataAndCheckDisconnection()
		{
			selectWasCalled = true;
			return false;
		}

		virtual bool isDataInRecvBuffer() const
		{
			bool ret = selectWasCalled;
			selectWasCalled = false;
			return ret;
		}
	};


	//! File descriptor, uses send/recv for read/write
	class FileDescriptorStream : public DisconnectableStream
	{
	public:
		//! Create the stream and associates a file descriptor
		explicit FileDescriptorStream(const string& protocolName) :
			Stream(protocolName),
			DisconnectableStream(protocolName)
		{}

		virtual void write(const void* data, const size_t size)
		{
			assert(fd >= 0);

			if (size == 0)
				return;

			const char* ptr = (const char*)data;
			size_t left = size;

			while (left)
			{
				ssize_t len = ::write(fd, ptr, left);

				if (len < 0)
				{
					fail(DashelException::IOError, errno, "File write I/O error.");
				}
				else if (len == 0)
				{
					fail(DashelException::ConnectionLost, 0, "File full.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}

		virtual void flush()
		{
			assert(fd >= 0);

#ifdef MACOSX
			if (fsync(fd) < 0)
#else
			if (fdatasync(fd) < 0)
#endif
			{
				fail(DashelException::IOError, errno, "File flush error.");
			}
		}

		virtual void read(void* data, size_t size)
		{
			assert(fd >= 0);

			if (size == 0)
				return;

			char* ptr = (char*)data;
			size_t left = size;

			if (isDataInRecvBuffer())
			{
				size_t toCopy = std::min(recvBufferSize - recvBufferPos, size);
				memcpy(ptr, recvBuffer + recvBufferPos, toCopy);
				recvBufferPos += toCopy;
				ptr += toCopy;
				left -= toCopy;
			}

			while (left)
			{
				ssize_t len = ::read(fd, ptr, left);

				if (len < 0)
				{
					fail(DashelException::IOError, errno, "File read I/O error.");
				}
				else if (len == 0)
				{
					fail(DashelException::ConnectionLost, 0, "Reached end of file.");
				}
				else
				{
					ptr += len;
					left -= len;
				}
			}
		}

		virtual bool receiveDataAndCheckDisconnection()
		{
			assert(recvBufferPos == recvBufferSize);

			ssize_t len = ::read(fd, &recvBuffer, RECV_BUFFER_SIZE);
			if (len > 0)
			{
				recvBufferSize = len;
				recvBufferPos = 0;
				return false;
			}
			else
			{
				if (len < 0)
					fail(DashelException::IOError, errno, "File read I/O error.");
				return true;
			}
		}
	};

	//! Stream for file
	class FileStream : public FileDescriptorStream
	{
	public:
		//! Parse the target name and create the corresponding file stream
		explicit FileStream(const string& targetName) :
			Stream("file"),
			FileDescriptorStream("file")
		{
			target.add("file:name;mode=read;fd=-1");
			target.add(targetName.c_str());
			fd = target.get<int>("fd");
			if (fd < 0)
			{
				const std::string name = target.get("name");
				const std::string mode = target.get("mode");

				// open file
				if (mode == "read")
					fd = open(name.c_str(), O_RDONLY);
				else if (mode == "write")
					fd = creat(name.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP), writeOnly = true;
				else if (mode == "readwrite")
					fd = open(name.c_str(), O_RDWR);
				else
					throw DashelException(DashelException::InvalidTarget, 0, "Invalid file mode.");
				if (fd == -1)
				{
					string errorMessage = "Cannot open file " + name + " for " + mode + ".";
					throw DashelException(DashelException::ConnectionFailed, errno, errorMessage.c_str());
				}
			}
			else
			{
				// remove file descriptor information from target name
				target.erase("fd");
			}
		}
	};

	//! Standard input stream, simply a FileStream with a specific target
	struct StdinStream : public FileStream
	{
		explicit StdinStream(const string& targetName) :
			Stream("file"),
			FileStream("file:name=/dev/stdin;mode=read;fd=0") {}
	};

	//! Standard output stream, simply a FileStream with a specific target
	struct StdoutStream : public FileStream
	{
		explicit StdoutStream(const string& targetName) :
			Stream("file"),
			FileStream("file:name=/dev/stdout;mode=write;fd=1") {}
	};

	//! Stream for serial port, in addition to FileDescriptorStream, save old state of serial port
	class SerialStream : public FileDescriptorStream
	{
	protected:
		struct termios oldtio; //!< old serial port state

	public:
		//! Parse the target name and create the corresponding serial stream
		explicit SerialStream(const string& targetName) :
			Stream("ser"),
			FileDescriptorStream("ser")
		{
			target.add("ser:port=1;baud=115200;stop=1;parity=none;fc=none;bits=8;dtr=true");
			target.add(targetName.c_str());
			string devFileName;

			if (target.isSet("device"))
			{
				target.addParam("device", NULL, true);
				target.erase("port");
				target.erase("name");

				devFileName = target.get("device");
			}
			else if (target.isSet("name"))
			{
				target.addParam("name", NULL, true);
				target.erase("port");
				target.erase("device");

				// Enumerates the ports
				std::string name = target.get("name");
				std::map<int, std::pair<std::string, std::string> > ports = SerialPortEnumerator::getPorts();

				// Iterate on all ports to found one with "name" in its description
				std::map<int, std::pair<std::string, std::string> >::iterator it;
				for (it = ports.begin(); it != ports.end(); it++)
				{
					if (it->second.second.find(name) != std::string::npos)
					{
						devFileName = it->second.first;
						std::cout << "Found " << name << " on port " << devFileName << std::endl;
						break;
					}
				}
				if (devFileName.size() == 0)
					throw DashelException(DashelException::ConnectionFailed, 0, "The specified name could not be find among the serial ports.");
			}
			else // port
			{
				target.erase("device");
				target.erase("name");

				std::map<int, std::pair<std::string, std::string> > ports = SerialPortEnumerator::getPorts();
				std::map<int, std::pair<std::string, std::string> >::const_iterator it = ports.find(target.get<int>("port"));
				if (it != ports.end())
				{
					devFileName = it->first;
				}
				else
					throw DashelException(DashelException::ConnectionFailed, 0, "The specified serial port does not exists.");
			}

			fd = open(devFileName.c_str(), O_RDWR);
			if (fd == -1)
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot open serial port.");

			int lockRes = flock(fd, LOCK_EX | LOCK_NB);
			if (lockRes != 0)
			{
				close(fd);
				throw DashelException(DashelException::ConnectionFailed, errno, "Cannot lock serial port.");
			}

			struct termios newtio;

			// save old serial port state and clear new one
			tcgetattr(fd, &oldtio);
			memset(&newtio, 0, sizeof(newtio));

			newtio.c_cflag |= CLOCAL; // ignore modem control lines.
			newtio.c_cflag |= CREAD; // enable receiver.
			switch (target.get<int>("bits")) // Set amount of bits per character
			{
				case 5: newtio.c_cflag |= CS5; break;
				case 6: newtio.c_cflag |= CS6; break;
				case 7: newtio.c_cflag |= CS7; break;
				case 8: newtio.c_cflag |= CS8; break;
				default: throw DashelException(DashelException::InvalidTarget, 0, "Invalid number of bits per character, must be 5, 6, 7, or 8.");
			}
			if (target.get("stop") == "2")
				newtio.c_cflag |= CSTOPB; // Set two stop bits, rather than one.
			if (target.get("fc") == "hard")
				newtio.c_cflag |= CRTSCTS; // enable hardware flow control
			if (target.get("parity") != "none")
			{
				newtio.c_cflag |= PARENB; // enable parity generation on output and parity checking for input.
				if (target.get("parity") == "odd")
					newtio.c_cflag |= PARODD; // parity for input and output is odd.
			}

#ifdef MACOSX
			if (cfsetspeed(&newtio, target.get<int>("baud")) != 0)
				throw DashelException(DashelException::ConnectionFailed, errno, "Invalid baud rate.");
#else
			switch (target.get<int>("baud"))
			{
				case 0: newtio.c_cflag |= B0; break;
				case 50: newtio.c_cflag |= B50; break;
				case 75: newtio.c_cflag |= B75; break;
				case 110: newtio.c_cflag |= B110; break;
				case 134: newtio.c_cflag |= B134; break;
				case 150: newtio.c_cflag |= B150; break;
				case 200: newtio.c_cflag |= B200; break;
				case 300: newtio.c_cflag |= B300; break;
				case 600: newtio.c_cflag |= B600; break;
				case 1200: newtio.c_cflag |= B1200; break;
				case 1800: newtio.c_cflag |= B1800; break;
				case 2400: newtio.c_cflag |= B2400; break;
				case 4800: newtio.c_cflag |= B4800; break;
				case 9600: newtio.c_cflag |= B9600; break;
				case 19200: newtio.c_cflag |= B19200; break;
				case 38400: newtio.c_cflag |= B38400; break;
				case 57600: newtio.c_cflag |= B57600; break;
				case 115200: newtio.c_cflag |= B115200; break;
				case 230400: newtio.c_cflag |= B230400; break;
				case 460800: newtio.c_cflag |= B460800; break;
				case 500000: newtio.c_cflag |= B500000; break;
				case 576000: newtio.c_cflag |= B576000; break;
				case 921600: newtio.c_cflag |= B921600; break;
				case 1000000: newtio.c_cflag |= B1000000; break;
				case 1152000: newtio.c_cflag |= B1152000; break;
				case 1500000: newtio.c_cflag |= B1500000; break;
				case 2000000: newtio.c_cflag |= B2000000; break;
#ifdef B2500000
				case 2500000: newtio.c_cflag |= B2500000; break;
#endif // B2500000
#ifdef B3000000
				case 3000000: newtio.c_cflag |= B3000000; break;
#endif // B3000000
#ifdef B3500000
				case 3500000: newtio.c_cflag |= B3500000; break;
#endif // B3500000
#ifdef B4000000
				case 4000000: newtio.c_cflag |= B4000000; break;
#endif // B4000000
				default: throw DashelException(DashelException::ConnectionFailed, 0, "Invalid baud rate.");
			}
#endif

			newtio.c_iflag = IGNPAR; // ignore parity on input

			newtio.c_oflag = 0;

			newtio.c_lflag = 0;

			newtio.c_cc[VTIME] = 0; // block forever if no byte
			newtio.c_cc[VMIN] = 1; // one byte is sufficient to return

			// set attributes
			if ((tcflush(fd, TCIOFLUSH) < 0) || (tcsetattr(fd, TCSANOW, &newtio) < 0))
				throw DashelException(DashelException::ConnectionFailed, 0, "Cannot setup serial port. The requested baud rate might not be supported.");

			// Enable or disable DTR
			int iFlags = TIOCM_DTR;
			if (target.get<bool>("dtr"))
				ioctl(fd, TIOCMBIS, &iFlags);
			else
				ioctl(fd, TIOCMBIC, &iFlags);

			//Ignore SIGHUP from this point ownward, as it may be emitted and lead to a crash on some POSIX
			//implementations, despite CLOCAL being set
			//TODO: we should probably reset the signal to its initial state when all devices are disconnected.
			struct sigaction act;
			memset(&act, 0, sizeof(act));
			act.sa_handler = SIG_IGN;
			sigaction(SIGHUP, &act, NULL);
		}

		//! Destructor, restore old serial port state
		virtual ~SerialStream()
		{
			tcsetattr(fd, TCSANOW, &oldtio);
		}

		// clang-format off
		virtual void flush() { /* hook for use by derived classes */ }
		// clang-format on
	};


	// Hub

	Hub::Hub(const bool resolveIncomingNames) :
		resolveIncomingNames(resolveIncomingNames)
	{
		int* terminationPipes = new int[2];
		if (pipe(terminationPipes) != 0)
			abort();
		hTerminate = terminationPipes;

		streamsLock = new pthread_mutex_t;

		pthread_mutex_init((pthread_mutex_t*)streamsLock, NULL);
	}

	Hub::~Hub()
	{
		int* terminationPipes = (int*)hTerminate;
		close(terminationPipes[0]);
		close(terminationPipes[1]);
		delete[] terminationPipes;

		for (StreamsSet::iterator it = streams.begin(); it != streams.end(); ++it)
			delete *it;

		pthread_mutex_destroy((pthread_mutex_t*)streamsLock);

		delete (pthread_mutex_t*)streamsLock;
	}

	Stream* Hub::connect(const std::string& target)
	{
		std::string proto;
		size_t c = target.find_first_of(':');
		if (c == std::string::npos)
			throw DashelException(DashelException::InvalidTarget, 0, "No protocol specified in target.");
		proto = target.substr(0, c);
		// N.B. params = target.substr(c+1)

		SelectableStream* s(dynamic_cast<SelectableStream*>(streamTypeRegistry.create(proto, target, *this)));
		if (!s)
		{
			std::string r = "Invalid protocol in target: ";
			r += proto;
			r += ", known protocol are: ";
			r += streamTypeRegistry.list();
			throw DashelException(DashelException::InvalidTarget, 0, r.c_str());
		}

		/* The caller must have the stream lock held */

		streams.insert(s);
		if (proto != "tcpin")
		{
			dataStreams.insert(s);
			connectionCreated(s);
		}

		return s;
	}

	void Hub::run()
	{
		while (step(-1))
			;
	}

	bool Hub::step(const int timeout)
	{
		bool firstPoll = true;
		bool wasActivity = false;
		bool runInterrupted = false;

		pthread_mutex_lock((pthread_mutex_t*)streamsLock);

		do
		{
			wasActivity = false;
			size_t streamsCount = streams.size();
			valarray<struct pollfd> pollFdsArray(streamsCount + 1);
			valarray<SelectableStream*> streamsArray(streamsCount);

			// add streams
			size_t i = 0;
			for (StreamsSet::iterator it = streams.begin(); it != streams.end(); ++it)
			{
				SelectableStream* stream = polymorphic_downcast<SelectableStream*>(*it);

				streamsArray[i] = stream;
				pollFdsArray[i].fd = stream->fd;
				pollFdsArray[i].events = 0;
				if ((!stream->failed()) && (!stream->writeOnly))
					pollFdsArray[i].events |= stream->pollEvent;

				i++;
			}
			// add pipe
			int* terminationPipes = (int*)hTerminate;
			pollFdsArray[i].fd = terminationPipes[0];
			pollFdsArray[i].events = POLLIN;

			// do poll and check for error
			int thisPollTimeout = firstPoll ? timeout : 0;
			firstPoll = false;

			pthread_mutex_unlock((pthread_mutex_t*)streamsLock);

#ifndef USE_POLL_EMU
			int ret = poll(&pollFdsArray[0], pollFdsArray.size(), thisPollTimeout);
#else
			int ret = poll_emu(&pollFdsArray[0], pollFdsArray.size(), thisPollTimeout);
#endif
			if (ret < 0)
				throw DashelException(DashelException::SyncError, errno, "Error during poll.");

			pthread_mutex_lock((pthread_mutex_t*)streamsLock);

			// check streams for errors
			for (i = 0; i < streamsCount; i++)
			{
				SelectableStream* stream = streamsArray[i];

				// make sure we do not try to handle removed streams
				if (streams.find(stream) == streams.end())
					continue;

				assert((pollFdsArray[i].revents & POLLNVAL) == 0);

				if (pollFdsArray[i].revents & POLLERR)
				{
					wasActivity = true;

					try
					{
						stream->fail(DashelException::SyncError, 0, "Error on stream during poll.");
					}
					catch (const DashelException& e)
					{
						assert(e.stream);
					}

					try
					{
						connectionClosed(stream, true);
					}
					catch (const DashelException& e)
					{
						assert(e.stream);
					}

					closeStream(stream);
				}
				else if (pollFdsArray[i].revents & POLLHUP)
				{
					wasActivity = true;

					try
					{
						connectionClosed(stream, false);
					}
					catch (const DashelException& e)
					{
						assert(e.stream);
					}

					closeStream(stream);
				}
				else if (pollFdsArray[i].revents & stream->pollEvent)
				{
					wasActivity = true;

					// test if listen stream
					SocketServerStream* serverStream = dynamic_cast<SocketServerStream*>(stream);

					if (serverStream)
					{
						// accept connection
						struct sockaddr_in targetAddr;
						socklen_t l = sizeof(targetAddr);
						int targetFD = accept(stream->fd, (struct sockaddr*)&targetAddr, &l);
						if (targetFD < 0)
						{
							pthread_mutex_unlock((pthread_mutex_t*)streamsLock);
							throw DashelException(DashelException::SyncError, errno, "Cannot accept new stream.");
						}

						// create a target stream using the new file descriptor from accept
						ostringstream targetName;
						targetName << IPV4Address(ntohl(targetAddr.sin_addr.s_addr), ntohs(targetAddr.sin_port)).format(resolveIncomingNames);
						targetName << ";connectionPort=";
						targetName << atoi(serverStream->getTargetParameter("port").c_str());
						targetName << ";sock=";
						targetName << targetFD;
						connect(targetName.str());
					}
					else
					{
						bool streamClosed = false;
						try
						{
							if (stream->receiveDataAndCheckDisconnection())
							{
								connectionClosed(stream, false);
								streamClosed = true;
							}
							else
							{
								// read all data available on this socket
								while (stream->isDataInRecvBuffer())
									incomingData(stream);
							}
						}
						catch (const DashelException& e)
						{
							assert(e.stream);
						}

						if (streamClosed)
							closeStream(stream);
					}
				}
			}
			// check pipe for termination
			if (pollFdsArray[i].revents)
			{
				char c;
				const ssize_t ret = read(pollFdsArray[i].fd, &c, 1);
				if (ret != 1)
					abort(); // poll did notify us that there was something to read, but we did not read anything, this is a bug
				runInterrupted = true;
			}

			// collect and remove all failed streams
			vector<Stream*> failedStreams;
			for (StreamsSet::iterator it = streams.begin(); it != streams.end(); ++it)
				if ((*it)->failed())
					failedStreams.push_back(*it);

			for (size_t i = 0; i < failedStreams.size(); i++)
			{
				Stream* stream = failedStreams[i];
				if (streams.find(stream) == streams.end())
					continue;
				if (stream->failed())
				{
					try
					{
						connectionClosed(stream, true);
					}
					catch (const DashelException& e)
					{
						assert(e.stream);
					}
					closeStream(stream);
				}
			}
		} while (wasActivity && !runInterrupted);

		pthread_mutex_unlock((pthread_mutex_t*)streamsLock);

		return !runInterrupted;
	}

	void Hub::lock()
	{
		pthread_mutex_lock((pthread_mutex_t*)streamsLock);
	}

	void Hub::unlock()
	{
		pthread_mutex_unlock((pthread_mutex_t*)streamsLock);
	}

	void Hub::stop()
	{
		int* terminationPipes = (int*)hTerminate;
		char c = 0;
		const ssize_t ret = write(terminationPipes[1], &c, 1);
		if (ret != 1)
			throw DashelException(DashelException::IOError, ret, "Cannot write to termination pipe.");
	}

	StreamTypeRegistry::StreamTypeRegistry()
	{
		reg("file", &createInstance<FileStream>);
		reg("stdin", &createInstance<StdinStream>);
		reg("stdout", &createInstance<StdoutStream>);
		reg("ser", &createInstance<SerialStream>);
		reg("tcpin", &createInstance<SocketServerStream>);
		reg("tcp", &createInstance<SocketStream>);
		reg("tcppoll", &createInstance<PollStream>);
		reg("udp", &createInstance<UDPSocketStream>);
	}

	StreamTypeRegistry __attribute__((init_priority(1000))) streamTypeRegistry;
}
