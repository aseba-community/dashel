cmake_minimum_required(VERSION 2.6)

project(dashel)

# libudev
find_path(UDEV_INCLUDE_DIR libudev.h)
find_library(UDEV_LIBS udev)

if (UDEV_INCLUDE_DIR AND UDEV_LIBS)
	message(STATUS "libudev found")
	set(UDEV_FOUND 1)
endif (UDEV_INCLUDE_DIR AND UDEV_LIBS)

# libraries
if (WIN32)
	set(DASHEL_SOURCES dashel/dashel-win32.cpp dashel/dashel-common.cpp)
	set(DASHEL_INSTALL_HEADER dashel/dashel.h)
	set(EXTRA_LIBS winspool ws2_32 setupapi)
else (WIN32)
	set(DASHEL_SOURCES dashel/dashel-posix.cpp dashel/dashel-common.cpp)
	set(DASHEL_INSTALL_HEADER dashel/dashel.h dashel/dashel-posix.h)
	if (APPLE)
		set(DASHEL_SOURCES ${DASHEL_SOURCES} dashel/poll_emu.c)
		find_library(COREFOUNDATION_LIBRARY CoreFoundation)
		find_library(IOKIT_LIBRARY IOKit)
		mark_as_advanced(COREFOUNDATION_LIBRARY)
		mark_as_advanced(IOKIT_LIBRARY)
		find_library(POLL_LIBRARY poll)
		set(EXTRA_LIBS ${COREFOUNDATION_LIBRARY} ${IOKIT_LIBRARY} ${POLL_LIBRARY})
	else (APPLE)
		if (UDEV_FOUND)
			include_directories(${UDEV_INCLUDE_DIR})
			set(EXTRA_LIBS ${UDEV_LIBS})
			add_definitions(-DUSE_LIBUDEV)
		else (UDEV_FOUND)
			find_package(PkgConfig)
			pkg_check_modules(HAL hal)
			if (${HAL_FOUND})
				include_directories(${HAL_INCLUDE_DIRS})
				set(EXTRA_LIBS ${HAL_LIBRARIES})
				add_definitions(-DUSE_HAL)
			endif (${HAL_FOUND})
		endif (UDEV_FOUND)
	endif (APPLE)
endif (WIN32)

add_library(dashel ${DASHEL_SOURCES})

# install target
install(FILES ${DASHEL_INSTALL_HEADER}
        DESTINATION include/dashel)
install(TARGETS dashel
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)

include_directories(${dashel_SOURCE_DIR})

# examples
foreach (example microterm chat portlist udp)
	add_executable(${example} examples/${example}.cpp)
	target_link_libraries(${example} dashel ${EXTRA_LIBS})
endforeach (example)

# test cases
#foreach (test none-for-now)
#	add_executable(${test} tests/${test}.cpp)
#	target_link_libraries(${test} dashel ${EXTRA_LIBS})
#endforeach (test)
