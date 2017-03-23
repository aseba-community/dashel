# DaSHEL

a cross-platform DAta Stream Helper Encapsulation Library

## Usage

To learn how to use Dashel, visit http://aseba-community.github.io/dashel.

## Compilation

To compile Dashel, you need CMake 2.6 or later (http://www.cmake.org/).
The CMake website provides documentation on how to use CMake on the different
platforms at https://cmake.org/runningcmake.

### Unix

If you are on Unix and lazy, and just want to compile Dashel without any
additional thinking, type the following commands in Dashel sources directory:

	cmake . && make

If this does not work, then read the aforementioned web page.

On Linux, to enumerate serial ports properly, install libudev (http://www.kernel.org/pub/linux/utils/kernel/hotplug/libudev/).

## Contribute

Feel free to report bugs, fork Dashel and submit pull requests through [github](https://github.com/aseba-community/dashel).
If you want to reach us, you can join the [development mailing list](https://mail.gna.org/listinfo/dashel-dev/).

## Authors and license

Copyright (C) 2007–2017:
	
* [Stephane Magnenat](http://stephane.magnenat.net), [Mobots group](http://mobots.epfl.ch), [EPFL](http://www.epfl.ch/)
* Sebastian Gerlach, [Kenzan Technologies](http://www.kenzantech.com)
* [Antoine Beyeler](http://www.ab-ware.com), [Laboratory of Intelligent Systems](http://lis.epfl.ch), [EPFL](http://www.epfl.ch/)
* [David James Sherman](http://www.labri.fr/perso/david/Site/David_James_Sherman.html), [Inria](http://inria.fr)

All rights reserved. Released under a [modified BSD license](https://opensource.org/licenses/BSD-3-Clause):

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
