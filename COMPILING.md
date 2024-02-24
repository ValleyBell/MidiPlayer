# Compilation notes

The program is built using CMake on all platforms.

There are also Visual Studio projects in the repository, which you can use at your own risk.

## Dependencies

- Windows:
  - a C++ compiler (Visual Studio 2010 and higher work. VC++6 doesn't.)
  - [PDCurses](https://github.com/wmcbrine/PDCurses), compiled with UTF-8 support
    - compiled with UTF-8 support: `nmake -f vcwin32.mak WIDE=Y UTF8=Y`
  - iconv (from "libiconv-win-build" [GitHub repository](https://github.com/kiyolee/libiconv-win-build))
  - *\[optional/recommended\]* uchardet
  - *\[optional\]* zlib (see [GitHub repository](https://github.com/madler/zlib))
  - *\[optional\]* FFmpeg (development files, e.g. from [gyan.dev](https://www.gyan.dev/ffmpeg/builds/#release-builds))
- Linux:
  - a C++ compiler
  - NCurses (Debian/Ubuntu package: `libncurses-dev`)
  - ALSA (Debian/Ubuntu package: `libasound2-dev`)
  - iconv (should be part of the C library on Debian/Ubuntu)
  - *\[optional/recommended\]* uchardet (Debian/Ubuntu package: `libuchardet-dev`)
  - *\[optional\]* zlib (Debian/Ubuntu package: `zlib1g-dev`)
  - *\[optional\]* X11 (Debian/Ubuntu package: `libx11-dev`)
  - *\[optional\]* FFmpeg (Debian/Ubuntu packages: `libavcodec-dev`, `libavformat-dev`, `libavutil-dev`, `libswscale-dev`)
  - *\[optional\]* pkg-config (Debian/Ubuntu package: `pkg-config`), *required when using FFmpeg*

About the optional dependencies:

- zlib is used for extracting ZIP files
- X11 and FFmpeg are used for screen recording

## Build process

- Windows with MS Visual C++:

  1. get source code

      ```batch
      git clone "https://github.com/ValleyBell/MidiPlayer.git"
      cd MidiPlayer
      git submodule update --init --recursive
      ```

  2. compile [PDCurses](https://github.com/wmcbrine/PDCurses)
      1. compile with UTF-8 support
          - in a Visual Studio developer console, run

            ```batch
            git clone "https://github.com/wmcbrine/PDCurses.git" -b "PDCurses_3_4"
            cd PDCurses\win32
            nmake -f vcwin32.mak WIDE=Y UTF8=Y
            ```

      2. copy files into `MidiPlayer/libs/pdcurses` directory

          ```batch
          mkdir MidiPlayer\libs\pdcurses\include
          copy PDCurses\*.h MidiPlayer\libs\pdcurses\include
          del MidiPlayer\libs\pdcurses\include\curspriv.h
          mkdir MidiPlayer\libs\pdcurses\lib
          copy PDCurses\win32\*.lib MidiPlayer\libs\pdcurses\lib\
          ```

  3. compile [libiconv-win](https://github.com/kiyolee/libiconv-win-build)
      1. compile
          - in a Visual Studio developer console, run

            ```batch
            git clone "https://github.com/kiyolee/libiconv-win-build.git" -b "v1.15"
            cd libiconv-win-build\build-VS2010
            msbuild /t:dll\libiconv /p:Configuration=Release
            ```

      2. copy files into `MidiPlayer/libs/iconv` directory

          ```batch
          mkdir MidiPlayer\libs\iconv\include
          copy libiconv-win-build\include\ MidiPlayer\libs\iconv\include\
          mkdir MidiPlayer\libs\iconv\lib
          copy libiconv-win-build\build-VS2010\Release MidiPlayer\libs\iconv\lib
          ```

  4. *\[optional/recommended\]* compile [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/)
      1. compile
          - in a Visual Studio developer console, run

            ```batch
            git clone "https://gitlab.freedesktop.org/uchardet/uchardet.git" -b "v0.0.7"
            cd uchardet
            mkdir build-win
            cd build-win
            cmake .. -DBUILD_BINARY=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC=ON
            cmake --build . --config Release --target libuchardet
            ```

      2. copy files into `MidiPlayer/libs/uchardet` directory

          ```batch
          mkdir MidiPlayer\libs\uchardet\include
          copy uchardet\src\uchardet.h MidiPlayer\libs\uchardet\include\
          mkdir MidiPlayer\libs\uchardet\lib
          copy uchardet\build-win\src\Release MidiPlayer\libs\uchardet\lib
          ```

  5. compile the program

      ```batch
      cd MidiPlayer
      mkdir build
      cd build
      cmake .. -DCHARSET_DETECTION=OFF ^
        -DIconv_INCLUDE_DIR="..\libs\iconv\include" -DIconv_LIBRARY="..\libs\iconv\lib\libiconv.lib" ^
        -DCURSES_INCLUDE_PATH:PATH="%CD%\..\libs\pdcurses\include" -DCURSES_CURSES_LIBRARY="..\libs\pdcurses\lib\pdcurses.lib" -DCURSES_PANEL_LIBRARY="..\libs\pdcurses\lib\panel.lib" ^
        -DUCHARDET_INCLUDE_DIR="..\libs\uchardet\include" -DUCHARDET_LIBRARY="..\libs\uchardet\lib\uchardet.lib"
      cmake --build . --config Release
      copy Release\MidiPlayer.exe ..\
      copy ..\libs\iconv\lib\*.dll ..\
      ```

- Linux with GCC:

  1. install required dependencies
  2. get source code

      ```bash
      git clone "https://github.com/ValleyBell/MidiPlayer.git"
      cd MidiPlayer
      git submodule update --init --recursive
      ```

  3. compile the program

      ```bash
      mkdir build
      cd build
      cmake .. -DCMAKE_BUILD_TYPE=Release -DCHARSET_DETECTION=OFF
      make
      cd ..
      ln -s build/MidiPlayer
      ```

- enabling additional features:
  - Change `CHARSET_DETECTION` from `OFF` to `ON` in the first CMake call to enable detection of used character encodings.
  - Add `-DSCREEN_RECORDING=ON` to the first CMake call in order to enable screen recording.
  - Add `-DREMOTE_CONTROL=ON` to the first CMake call in order to remote control (Linux only) using a pipe file
