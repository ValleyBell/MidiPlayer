cmake_minimum_required(VERSION 2.8)
project(MidiPlayer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")
endif(CMAKE_COMPILER_IS_GNUCC)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake_modules/")


set(INCLUDES)
set(LIBRARIES)

set(HEADERS
		MidiLib.hpp
		MidiPlay.hpp
		NoteVis.hpp
		MidiBankScan.hpp
		MidiInsReader.h
		MidiPortAliases.hpp
		MidiModules.hpp
		utils.hpp
		m3uargparse.hpp
		OSTimer.h
		MidiOut.h
		vis.hpp
		vis_sc-lcd.hpp
		)

set(SOURCES
		main.cpp
		MidiLib.cpp
		MidiPlay.cpp
		NoteVis.cpp
		MidiBankScan.cpp
		MidiInsReader.c
		MidiPortAliases.cpp
		MidiModules.cpp
		utils.cpp
		m3uargparse.cpp
		)

# --- INI reading ---
set(INIH_DIR libs/inih)
set(HEADERS ${HEADERS}
		${INIH_DIR}/ini.h
		INIReader.hpp
	)
set(SOURCES ${SOURCES}
		${INIH_DIR}/ini.c
		INIReader.cpp
	)
set(INCLUDES ${INCLUDES}
		${INIH_DIR}
	)

# --- MIDI output ---
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
	set(INCLUDES ${INCLUDES} ${ALSA_INCLUDE_DIRS})
	set(LIBRARIES ${LIBRARIES} ${ALSA_LIBRARY})
endif()

# --- character set conversion and graphical output ---
find_package(Iconv REQUIRED)
set(LIBRARIES ${LIBRARIES} Iconv::Iconv)

set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)
set(SOURCES ${SOURCES}
	vis_curses.cpp
	vis_sc-lcd.cpp
	)
set(INCLUDES ${INCLUDES} ${CURSES_INCLUDE_DIR})
set(LIBRARIES ${LIBRARIES} ${CURSES_LIBRARIES} panel)


add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR} ${INCLUDES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBRARIES})
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin")
