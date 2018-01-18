CC = gcc
CPP = g++
CFLAGS = -I.
CPPFLAGS = -std=c++11 -Wno-psabi
LDFLAGS = 

CURSES_LIBS_WIN = `ncursesw5-config --libs`
CURSES_LIBS_LINUX = `ncursesw5-config --libs`

SRC = .
OBJ = obj

OBJ_ALL = \
	$(OBJ)/main.o \
	$(OBJ)/MidiLib.o \
	$(OBJ)/MidiPlay.o \
	$(OBJ)/MidiBankScan.o \
	$(OBJ)/MidiInsReader.o \
	$(OBJ)/m3uargparse.o \
	$(OBJ)/inih/ini.o \
	$(OBJ)/inih/cpp/INIReader.o
OBJ_WIN = \
	$(OBJ)/OSTimer_Win.o \
	$(OBJ)/MidiOut_WinMM.o
OBJ_LINUX = \
	$(OBJ)/OSTimer_POSIX.o \
	$(OBJ)/vis_curses.o \
	$(OBJ)/MidiOut_ALSA.o


default:	linux

win:	$(OBJ) $(OBJ_ALL) $(OBJ_WIN)
	@echo "Linking ..."
	@$(CPP) $(LDFLAGS) $(OBJ_ALL) $(OBJ_WIN) $(CURSES_LIBS_WIN) $(LDFLAGS) -lwinmm -o midiplayer

linux:	$(OBJ) $(OBJ_ALL) $(OBJ_LINUX)
	@echo "Linking ..."
	@$(CPP) $(LDFLAGS) $(OBJ_ALL) $(OBJ_LINUX) $(CURSES_LIBS_LINUX) $(LDFLAGS) -lasound -o midiplayer

$(OBJ)/%.o: $(SRC)/%.cpp
	@echo "Compiling $< ..."
	@$(CPP) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/%.o: $(SRC)/%.c
	@echo "Compiling $< ..."
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJ):
	@mkdir -p $(OBJ)
	@mkdir -p $(OBJ)/inih/cpp

clean:
	rm -f $(OBJ)/*.o
