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
set(DEFINES)

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
		RCPLoader.hpp
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
		RCPLoader.cpp
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

# --- character set detection ---
option(CHARSET_DETECTION "Detect song character charset" OFF)
if (CHARSET_DETECTION)
	find_path(UCHARDET_INCLUDE_DIR NAMES uchardet.h PATH_SUFFIXES uchardet)
	find_library(UCHARDET_LIBRARY_RELEASE NAMES uchardet)
	set(DEFINES ${DEFINES} CHARSET_DETECTION=1)
	set(INCLUDES ${INCLUDES} ${UCHARDET_INCLUDE_DIR})
	set(LIBRARIES ${LIBRARIES} ${UCHARDET_LIBRARY_RELEASE})
endif()


# --- character set conversion and graphical output ---
find_package(Iconv REQUIRED)
set(LIBRARIES ${LIBRARIES} Iconv::Iconv)

set(CURSES_NEED_WIDE TRUE)
# Note: If it segfaults during initscr(), make sure to fix CURSES_EXTRA_LIBRARY and
# CURSES_FORM_LIBRARY to use the wide-character versions of the libs.
find_package(Curses REQUIRED)
set(SOURCES ${SOURCES}
	vis_curses.cpp
	vis_sc-lcd.cpp
	)
set(INCLUDES ${INCLUDES} ${CURSES_INCLUDE_DIR})
set(LIBRARIES ${LIBRARIES} ${CURSES_LIBRARIES} panel)


# --- ZIP file reading ---
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
	set(LIBRARIES ${LIBRARIES} ZLIB::ZLIB)
	set(DEFINES ${DEFINES} ENABLE_ZIP_SUPPORT=1)
	set(HEADERS ${HEADERS} unzip.h)
	set(SOURCES ${SOURCES} unzip.c)
endif()


# --- screen recording ---
option(SCREEN_RECORDING "Enable screen recording using ffmpeg" OFF)
if (SCREEN_RECORDING)
	if (WIN32)
		find_path(AVCODEC_INCLUDE_DIRS NAMES avcodec.h PATH_SUFFIXES libavcodec)
		find_path(AVFORMAT_INCLUDE_DIRS NAMES avformat.h PATH_SUFFIXES libavformat)
		find_path(AVUTIL_INCLUDE_DIRS NAMES avutil.h PATH_SUFFIXES libavutil)
		find_path(SWSCALE_INCLUDE_DIRS NAMES swscale.h PATH_SUFFIXES libswscale)
		find_library(AVCODEC_LIBRARIES NAMES avcodec)
		find_library(AVFORMAT_LIBRARIES NAMES avformat)
		find_library(AVUTIL_LIBRARIES NAMES avutil)
		find_library(SWSCALE_LIBRARIES NAMES swscale)
	else()
		find_package(X11 REQUIRED)
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(AVCODEC REQUIRED libavcodec)
		pkg_check_modules(AVFORMAT REQUIRED libavformat)
		pkg_check_modules(AVUTIL REQUIRED libavutil)
		pkg_check_modules(SWSCALE REQUIRED libswscale)
	endif()
	
	set(FFMPEG_INCLUDE_DIRS ${AVFORMAT_INCLUDE_DIRS} ${AVCODEC_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS} ${SWSCALE_INCLUDE_DIRS})
	set(FFMPEG_LIBS ${AVFORMAT_LIBRARIES} ${AVCODEC_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES})
	
	set(DEFINES ${DEFINES} ENABLE_SCREEN_REC=1)
	set(INCLUDES ${INCLUDES} ${FFMPEG_INCLUDE_DIRS})
	set(LIBRARIES ${LIBRARIES} ${FFMPEG_LIBS})
	set(HEADERS ${HEADERS} scr-record.h)
	set(SOURCES ${SOURCES} scr-record_main.c)
	if (WIN32)
		set(SOURCES ${SOURCES} scr-record_Win.c)
		set(LIBRARIES ${LIBRARIES} gdi32)
	else()
		set(SOURCES ${SOURCES} scr-record_X11.c)
		set(LIBRARIES ${LIBRARIES} X11::X11)
	endif()
endif()


add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE ${DEFINES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR} ${INCLUDES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBRARIES})
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin")
