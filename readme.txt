DaSHEL, a cross-platform DAta Stream Helper Encapsulation Library
Copyright (C) 2007 -- 2015:
	
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


Additional contributions by:

	Antoine Beyeler <abeyeler at ab-ware dot com>
		(http://www.ab-ware.com)


To compile Dashel, you need CMake 2.6 or later (http://www.cmake.org/).
The CMake website provides documentation on how to use CMake on the different
platforms:
	http://www.cmake.org/cmake/help/runningcmake.html

If you are on Unix and lazy, and just want to compile Dashel without any
additional thinking, try the following commands in Dashel sources directory:
	cmake . && make
If this does not work, then read the aforementioned web page.

Optionally, on Linux, to enumerate serial ports properly, you can install libudev:
	libudev (http://www.kernel.org/pub/linux/utils/kernel/hotplug/libudev/)

If you still have some problem to compile Dashel after reading the relevant
documentation, feel free to post your question on our development mailing
list. You can subscribe to the latter at:
	http://gna.org/mail/?group=dashel

Enjoy Dashel!

	The developers
