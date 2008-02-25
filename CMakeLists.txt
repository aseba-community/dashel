project(dashel)

if (win32)
	set(dashel-sources dashel-win32.cpp dashel-common.cpp)
else (win32)
	set(dashel-sources dashel-posix.cpp dashel-common.cpp)
	if (apple)
		set(dashel-sources ${dashel-sources} poll_emu.cpp)
	else (apple)
		find_package(PkgConfig)
		pkg_check_modules(HAL hal)
		include_directories(${HAL_INCLUDE_DIRS})
	endif (apple)
endif (win32)

add_library(dashel ${dashel-sources})

add_executable(microterm microterm.cpp)
target_link_libraries(microterm dashel ${HAL_LIBRARIES})

add_executable(chat chat.cpp)
target_link_libraries(chat dashel ${HAL_LIBRARIES})
