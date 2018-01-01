// Note: The general view and layout is heavily inspired by "Playmidi" by Nathan Laredo.
//       In fact, I copied the colour scheme and note placement algorithm from Playmidi.
#include <stddef.h>
#include <string>
#include <math.h>
#include <curses.h>

#include <stdtype.h>
#include "MidiLib.hpp"
#include "MidiPlay.hpp"
#include "vis.hpp"
#include <vector>
#include <map>

static const char* notes[12] =
	{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "B#", "B"};

struct ChnNoteDisp
{
	int posX;
	int remTime;
};

static int calc_note_posx(UINT8 note);
static void str_padding(char* str, size_t padlen, char padchar);
static void vis_printms(double time);
static void vis_mvprintms(int row, int col, double time);


#define CHN_BASE_LINE	2
#define INS_COL_SIZE	14
#define NOTE_BASE_COL	16
#define NOTE_NAME_SPACE	3	// number of characters reserved for note names
#define CENTER_NOTE		60	// middle C
static char textbuf[1024];
static int TEXT_BASE_LINE = 0;
static int curYline = 0;

static MidiFile* midFile = NULL;
static const char* midFName = NULL;
static MidiPlayer* midPlay = NULL;

// std::vector< Channel std::map< posX, remTime> >
static std::vector< std::map<int, int> > dispNotes;
static UINT64 lastUpdateTime = 0;

void vis_init(void)
{
	initscr();
	cbreak();
	keypad(stdscr, TRUE);
	noecho();
	nodelay(stdscr, TRUE);
	
	start_color();
	init_pair(1, COLOR_RED, COLOR_BLACK);
	init_pair(2, COLOR_GREEN, COLOR_BLACK);
	init_pair(3, COLOR_YELLOW, COLOR_BLACK);
	init_pair(4, COLOR_BLUE, COLOR_BLACK);
	init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(6, COLOR_CYAN, COLOR_BLACK);
	init_pair(7, COLOR_WHITE, COLOR_BLACK);
	attrset(A_NORMAL);
	
	return;
}

void vis_deinit(void)
{
	attrset(A_NORMAL);
	refresh();
	endwin();
	
	return;
}

int vis_getch(void)
{
	int key;
	
	//if (! _kbhit())
	//	return 0;
	key = getch();
	if (key == ERR)
		key = 0;
	return key;
}

int vis_getch_wait(void)
{
	int key;
	
	nodelay(stdscr, FALSE);
	key = getch();
	nodelay(stdscr, TRUE);
	if (key == ERR)
		key = 0;
	return key;
}

void vis_addstr(const char* text)
{
	if (curYline >= LINES)
		curYline = TEXT_BASE_LINE;
	mvaddstr(curYline, 0, text);
	curYline ++;
	
	return;
}

void vis_set_midi_file(const char* fileName, MidiFile* mFile)
{
	midFName = fileName;
	midFile = mFile;
}

void vis_set_midi_player(MidiPlayer* mPlay)
{
	midPlay = mPlay;
}

void vis_new_song(void)
{
	unsigned int nTrks;
	unsigned int chnCnt;
	unsigned int curChn;
	int titlePosX;
	int maxTitleLen;
	char chnNameStr[0x10];
	
	nTrks = (midFile != NULL) ? midFile->GetTrackCount() : 0;
	chnCnt = (midPlay != NULL) ? midPlay->GetChannelStates().size() : 0x10;
	dispNotes.clear();
	dispNotes.resize(chnCnt);
	
	clear();
	curs_set(0);
	attrset(A_NORMAL);
	mvprintw(0, 0, "MIDI Player");
	mvprintw(0, 32, "Now Playing: ");
	titlePosX = getcurx(stdscr);
	mvprintw(1, 32, "[Q]uit [ ]Pause [B]Previous [N]ext");
	mvprintw(1, 0, "00:00.0 - 00:00.0, %u %s", nTrks, (nTrks == 1) ? "track" : "tracks");
	if (midPlay != NULL)
		vis_mvprintms(1, 10, midPlay->GetSongLength());
	
	curYline = CHN_BASE_LINE;
	for (curChn = 0; curChn < chnCnt; curChn ++, curYline ++)
	{
		sprintf(chnNameStr, "Channel %2u", 1 + (curChn & 0x0F));
		mvprintw(curYline, 0, "%-*.*s", INS_COL_SIZE, INS_COL_SIZE, chnNameStr);
		mvaddch(curYline, INS_COL_SIZE, ACS_VLINE);
	}
	mvhline(curYline, 0, ACS_HLINE, INS_COL_SIZE);
	mvaddch(curYline, INS_COL_SIZE, ACS_LRCORNER);
	curYline ++;
	TEXT_BASE_LINE = curYline;
	
	attron(A_BOLD);
	maxTitleLen = COLS - titlePosX;
	if (midFName != NULL)
	{
		sprintf(textbuf, "%-*.*s", maxTitleLen, maxTitleLen, midFName);
		mvaddstr(0, titlePosX, textbuf);
	}
	mvaddch(1, 33, 'Q');
	mvaddch(1, 40, ' ');
	mvaddch(1, 49, 'B');
	mvaddch(1, 61, 'N');
	attroff(A_BOLD);
	refresh();
	
	lastUpdateTime = 0;
	
	return;
}

void vis_do_ins_change(UINT16 chn)
{
	const MidiPlayer::ChannelState* chnSt = &midPlay->GetChannelStates()[chn];
	int posY = CHN_BASE_LINE + chn;
	int color = (chn % 6) + 1;
	const char* insName;
	char userInsName[20];
	
	if (chnSt->userInsID != 0xFFFF)
	{
		if (chnSt->userInsID & 0x8000)
			sprintf(userInsName, "User Drum %u", chnSt->userInsID & 0x7FFF);
		else
			sprintf(userInsName, "User Ins %u", chnSt->userInsID & 0x7FFF);
		insName = userInsName;
	}
	else
	{
		if (chnSt->insMapPPtr != NULL)
			insName = chnSt->insMapPPtr->insName;
		else
			insName = "--unknown--";
	}
	attron(A_BOLD | COLOR_PAIR(color));
	mvprintw(posY, 0, "%-*.*s", INS_COL_SIZE, INS_COL_SIZE, insName);
	attroff(A_BOLD | COLOR_PAIR(color));
	
	return;
}

static int calc_note_posx(UINT8 note)
{
	int ncols;
	int posX;
	
	ncols = (COLS - NOTE_BASE_COL) / NOTE_NAME_SPACE;
#if 0
	posX = NOTE_BASE_COL + ((note / 2) % ncols) * NOTE_NAME_SPACE;
#else
	// middle C = center of the screen (at ncols / 2)
	posX = (note - CENTER_NOTE) / 2 + (ncols / 2);
	posX += (CENTER_NOTE / ncols + 1) * ncols;	// prevent negative note values
	posX = NOTE_BASE_COL + (posX % ncols) * NOTE_NAME_SPACE;
#endif
	return posX;
}

static void str_padding(char* str, size_t padlen, char padchar)
{
	size_t pos;
	
	str[padlen] = '\0';
	for (pos = strlen(str); pos < padlen; pos ++)
		str[pos] = padchar;
	
	return;
}

void vis_do_note(UINT16 chn, UINT8 note, UINT8 state)
{
	const MidiPlayer::ChannelState* chnSt = &midPlay->GetChannelStates()[chn];
	int posX = calc_note_posx(note);
	int posY = CHN_BASE_LINE + chn;
	int color = (chn % 6) + 1;
	char noteName[8];
	
	if (chn >= dispNotes.size())
		return;
	std::map<int, int>& chnDisp = dispNotes[chn];
	std::map<int, int>::iterator noteIt;
	
	noteIt = chnDisp.find(posX);
	if (! (state & 0x01))
	{
		if (noteIt == chnDisp.end())
			return;
		if (noteIt->second > 0)
			return;	// ignore Note Off for drum sounds
		mvaddstr(posY, posX, "   ");
		chnDisp.erase(noteIt);
		return;
	}
	
	int forcedDurat = 0;
	if (chnSt->flags & 0x80)
	{
		// show drum name
		// C# (crash cym 1) || D# (ride cym 1) || F (ride bell) ||
		// G (splash cym) || A (crash cym 2) || B (ride cym 2)
		if (note == 0x31 || note == 0x33 || note == 0x35 ||
			note == 0x37 || note == 0x39 || note == 0x3B)
		{
			strcpy(noteName, "cym");
			forcedDurat = 600;
		}
		else if (note == 0x2A || note == 0x2C || note == 0x2E)	// hi-hats
		{
			static const UINT8 hhNotes[3] = {0x2A, 0x2C, 0x2E};
			std::map<int, int>::iterator hhIt;
			size_t curHH;
			
			// remove other hi-hat notes
			for (curHH = 0; curHH < 3; curHH ++)
			{
				if (hhNotes[curHH] == note)
					continue;
				hhIt = chnDisp.find(calc_note_posx(hhNotes[curHH]));
				if (hhIt != chnDisp.end())
				{
					mvaddstr(posY, hhIt->first, "   ");
					chnDisp.erase(hhIt);
				}
			}
			strcpy(noteName, "hh");
			forcedDurat = (note == 0x2E) ? 300 : 80;
		}
		else
		{
			strcpy(noteName, "drm");
			forcedDurat = 150;
		}
	}
	else
	{
		// show note name
		sprintf(noteName, "%s%u", notes[note % 12], note / 12);
		forcedDurat = 0;
	}
	chnDisp[posX] = forcedDurat;
	
	str_padding(noteName, 3, ' ');
	attron(A_BOLD | COLOR_PAIR(color));
	mvaddstr(posY, posX, noteName);
	// then add to note list
	attroff(A_BOLD | COLOR_PAIR(color));
	
	return;
}

void vis_print_meta(UINT16 trk, UINT8 metaType, size_t dataLen, const char* data)
{
	std::string text(data, &data[dataLen]);
	
	if (curYline >= LINES)
		curYline = TEXT_BASE_LINE;
	attron(COLOR_PAIR(metaType % 8));
	switch(metaType)
	{
	case 0x01:	// Text
		mvprintw(curYline, 0, "Text: %s", text.c_str());
		curYline ++;
		break;
	case 0x02:	// Copyright Notice
		mvprintw(curYline, 0, "Copyright: %s", text.c_str());
		curYline ++;
		break;
	case 0x03:	// Sequence/Track Name
		if (trk == 0 || midFile->GetMidiFormat() == 2)
		{
			attron(A_BOLD);
			mvprintw(curYline, 0, "Title: %s", text.c_str());
			attroff(A_BOLD);
			curYline ++;
		}
		break;
	case 0x04:	// Insrument Name
		mvprintw(curYline, 0, "Instrument Name: %s", text.c_str());
		curYline ++;
		break;
	case 0x05:	// Lyric
		// don't print for now
		break;
	case 0x06:	// Marker
		mvprintw(curYline, 0, "Marker: %s", text.c_str());
		curYline ++;
		break;
	case 0x51:	// Tempo
		break;
	case 0x58:	// Time Signature
		break;
	case 0x59:	// Key Signature
		break;
	}
	attroff(COLOR_PAIR(metaType % 8));
	
	return;
}

static void vis_printms(double time)
{
	// print time as mm:ss.c
	unsigned int cSec;
	unsigned int sec;
	unsigned int min;
	
	cSec = (unsigned int)floor(time * 10.0 + 0.5);
	sec = cSec / 10;
	cSec -= (sec * 10);
	min = sec / 60;
	sec -= (min * 60);
	printw("%02u:%02u.%1u", min, sec, cSec);
	
	return;
}

static void vis_mvprintms(int row, int col, double time)
{
	move(row, col);
	vis_printms(time);
	
	return;
}

void vis_update(void)
{
	UINT64 newUpdateTime;
	int updateTicks;
	size_t curChn;
	
	newUpdateTime = (UINT64)(midPlay->GetPlaybackPos() * 1000.0);
	updateTicks = (int)(newUpdateTime - lastUpdateTime);
	if (updateTicks < 20)
		return;	// update with 50 Hz maximum
	lastUpdateTime = newUpdateTime;
	
	for (curChn = 0; curChn < dispNotes.size(); curChn ++)
	{
		int posY = CHN_BASE_LINE + curChn;
		std::map<int, int>& chnDisp = dispNotes[curChn];
		std::map<int, int>::iterator noteIt;
		
		for (noteIt = chnDisp.begin(); noteIt != chnDisp.end(); )
		{
			if (noteIt->second > 0)
			{
				noteIt->second -= updateTicks;
				if (noteIt->second <= 0)
				{
					std::map<int, int>::iterator remIt = noteIt;
					++noteIt;
					mvaddstr(posY, remIt->first, "   ");
					chnDisp.erase(remIt);
					continue;
				}
			}
			++noteIt;
		}
	}
	
	vis_mvprintms(1, 0, midPlay->GetPlaybackPos());
	vis_mvprintms(1, 10, midPlay->GetSongLength());
	move(curYline, 0);
	refresh();
	
	return;
}
