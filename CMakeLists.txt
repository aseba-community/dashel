project(dashel)

if (WIN32)
	set(DASHEL_SOURCES dashel-win32.cpp dashel-common.cpp)
	set(EXTRA_LIBS winspool ws2_32)
else (WIN32)
	set(DASHEL_SOURCES dashel-posix.cpp dashel-common.cpp)
	if (APPLE)
		set(DASHEL_SOURCES ${DASHEL_SOURCES} poll_emu.c)
		find_library(COREFOUNDATION_LIBRARY CoreFoundation)
		find_library(IOKIT_LIBRARY IOKit)
		mark_as_advanced(COREFOUNDATION_LIBRARY)
		mark_as_advanced(IOKIT_LIBRARY)
		set(EXTRA_LIBS ${COREFOUNDATION_LIBRARY} ${IOKIT_LIBRARY})
	else (APPLE)
		find_package(PkgConfig)
		pkg_check_modules(HAL hal)
		include_directories(${HAL_INCLUDE_DIRS})
		set(EXTRA_LIBS ${HAL_LIBRARIES})
	endif (APPLE)
endif (WIN32)

add_library(dashel ${DASHEL_SOURCES})
install(FILES dashel.h
        DESTINATION include)
install(TARGETS dashel
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)


add_executable(microterm microterm.cpp)
target_link_libraries(microterm dashel ${EXTRA_LIBS})

add_executable(chat chat.cpp)
target_link_libraries(chat dashel ${EXTRA_LIBS})

add_executable(portlist portlist.cpp)
target_link_libraries(portlist dashel ${EXTRA_LIBS})

add_executable(01-win32-aseba-switch-socket-error tests/01-win32-aseba-switch-socket-error.cpp)
target_link_libraries(01-win32-aseba-switch-socket-error dashel ${EXTRA_LIBS})