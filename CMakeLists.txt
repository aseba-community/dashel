cmake_minimum_required(VERSION 2.8.12)

project(dashel)

export(PACKAGE dashel)

option(BUILD_SHARED_LIBS "Build shared instead of static libs" ON)

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
		set(CMAKE_MACOSX_RPATH ON) # Solve the CMP0042 warning
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
target_link_libraries(dashel ${EXTRA_LIBS})


if("${CMAKE_VERSION}" VERSION_GREATER 3.0)
	target_include_directories(dashel PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
		$<INSTALL_INTERFACE:>

	)
endif()

# dynamic library version
set(LIB_INSTALL_DIR lib CACHE FILEPATH "Where to install libraries")
set(LIB_VERSION_MAJOR 1) # Must be bumped for incompatible ABI changes
set(LIB_VERSION_MINOR 3)
set(LIB_VERSION_PATCH 3)
set(LIB_VERSION_STRING ${LIB_VERSION_MAJOR}.${LIB_VERSION_MINOR}.${LIB_VERSION_PATCH})
set_target_properties(dashel PROPERTIES VERSION ${LIB_VERSION_STRING} 
                                        SOVERSION ${LIB_VERSION_MAJOR})


# generate configure file
configure_file(dashelConfig.cmake.in "${PROJECT_BINARY_DIR}/dashelConfig.cmake" @ONLY)
configure_file(dashelConfigVersion.cmake.in "${PROJECT_BINARY_DIR}/dashelConfigVersion.cmake" @ONLY)
  
# install target
install(FILES ${DASHEL_INSTALL_HEADER}
        DESTINATION include/dashel)
install(TARGETS dashel
        LIBRARY DESTINATION ${LIB_INSTALL_DIR}
        ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
)
if (WIN32 AND NOT CYGWIN)
	set(INSTALL_CMAKE_DIR CMake)
else()
	if (APPLE)
		set(INSTALL_CMAKE_DIR Frameworks/dashel.framework/Resources/CMake/)
	else()
		set(INSTALL_CMAKE_DIR share/dashel/CMake)
	endif()
endif()
install(FILES "${PROJECT_BINARY_DIR}/dashelConfig.cmake"
	DESTINATION "${INSTALL_CMAKE_DIR}" COMPONENT dev)
install(FILES "${PROJECT_BINARY_DIR}/dashelConfigVersion.cmake"
	DESTINATION "${INSTALL_CMAKE_DIR}" COMPONENT dev)


# examples
add_subdirectory(examples)

# test cases
#foreach (test none-for-now)
#	add_executable(${test} tests/${test}.cpp)
#	target_link_libraries(${test} dashel ${EXTRA_LIBS})
#endforeach (test)
