CC = gcc
CPP = g++
CFLAGS = -I.
CPPFLAGS = -std=c++98 -Wno-psabi
LDFLAGS =

SRC = .
OBJ = obj

OBJ_ALL = \
	$(OBJ)/main.o \
	$(OBJ)/MidiLib.o \
	$(OBJ)/MidiPlay.o \
	$(OBJ)/MidiBankScan.o \
	$(OBJ)/MidiInsReader.o
OBJ_WIN = \
	$(OBJ)/OSTimer_Win.o \
	$(OBJ)/MidiOut_WinMM.o
OBJ_LINUX = \
	$(OBJ)/OSTimer_POSIX.o \
	$(OBJ)/MidiOut_ALSA.o


default:	linux

win:	$(OBJ) $(OBJ_ALL) $(OBJ_WIN)
	@echo "Linking ..."
	@$(CPP) $(LDFLAGS) $(OBJ_ALL) $(OBJ_WIN) -lwinmm -o midiplayer

linux:	$(OBJ) $(OBJ_ALL) $(OBJ_LINUX)
	@echo "Linking ..."
	@$(CPP) $(LDFLAGS) $(OBJ_ALL) $(OBJ_LINUX) -lasound -o midiplayer

$(OBJ)/%.o: $(SRC)/%.cpp
	@echo "Compiling $< ..."
	@$(CPP) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/%.o: $(SRC)/%.c
	@echo "Compiling $< ..."
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJ):
	@mkdir $(OBJ)

clean:
	rm -f $(OBJ)/*.o
