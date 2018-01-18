cmake_minimum_required(VERSION 2.8)
project(MidiPlayer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")
endif(CMAKE_COMPILER_IS_GNUCC)


set(INCLUDES)
set(LIBRARIES)

set(HEADERS
		MidiLib.hpp
		MidiPlay.hpp
		MidiBankScan.hpp
		MidiInsReader.h
		m3uargparse.hpp
		OSTimer.h
		MidiOut.h
		vis.hpp
		inih/ini.h
		inih/cpp/INIReader.h
		)

set(SOURCES
		main.cpp
		MidiLib.cpp
		MidiPlay.cpp
		MidiBankScan.cpp
		MidiInsReader.c
		m3uargparse.cpp
		inih/ini.c
		inih/cpp/INIReader.cpp
		)

if(WIN32)
	set(SOURCES ${SOURCES}
		OSTimer_Win.c
		MidiOut_WinMM.c
		)
	set(LIBRARIES ${LIBRARIES} winmm)
elseif(UNIX)
	find_package(ALSA REQUIRED)
	
	set(SOURCES ${SOURCES}
		OSTimer_POSIX.c
		MidiOut_ALSA.c
		)
	set(INCLUDES ${INCLUDES} ${ALSA_INCLUDE_DIR})
	set(LIBRARIES ${LIBRARIES} ${ALSA_LIBRARY})
endif()

find_package(Curses REQUIRED)
set(SOURCES ${SOURCES}
	vis_curses.cpp
	)
set(INCLUDES ${INCLUDES} ${CURSES_INCLUDE_DIR})
set(LIBRARIES ${LIBRARIES} ${CURSES_LIBRARIES})


add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR} ${INCLUDES})
target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBRARIES})
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin")
