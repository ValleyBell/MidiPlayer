// Note: The general view and layout is heavily inspired by "Playmidi" by Nathan Laredo.
//       In fact, I copied the colour scheme and note placement algorithm from Playmidi.
#include <stddef.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <curses.h>
#include <stdarg.h>

#include <stdtype.h>
#include "MidiLib.hpp"
#include "MidiPlay.hpp"
#include "NoteVis.hpp"
#include "vis.hpp"
#include "utils.hpp"
#include "MidiInsReader.h"	// for MIDI module type

static const char* notes[12] =
	{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Roland Symbols:
//	Map:	' - SC-55 map (LSB 1), " - SC-88 map (LSB 2)
//	Bank:	* - drum bank, [space] - GM bank, + - variation bank (LSB 1..63), # - MT-32/CM-64 bank (MSB 126/127)
//	Order is [bank] [map] [instrument name].
//	The display reserves 1 character for the [bank] symbol.
//	Selecting the non-native instrument causes the [map] character to be prepended to the
//	instrument name, shifting it to the right by 1 character.
//
// Yamaha Symbols:
//	Bank:	+ - SFX bank (MSB 64), # - drum bank (MSB 126/127)
//	Order is [bank] [instrument name]
//	The [bank] symbol is prepended to the instrument name.

static const char SC_MAP_SYMBOLS[4] =
{
	'\'',	// SC-55 map (as seen on SC-88/SC-88Pro)
	'\"',	// SC-88 map (as seen on SC-88Pro)
	'^',	// SC-88Pro map
	'|',	// SC-8850 map
};
static const char MU_MAP_SYMBOLS[6] =
{
	'\'',	// MU-50
	'\"',	// MU-80
	'^',	// MU-90
	'/',	// MU-100
	'-',	// MU-128
	'|',	// MU-1000
};


class ChannelData
{
public:
	struct NoteDisplay
	{
		UINT8 note;
		UINT8 vol;	// 0 - off, 1 - soft, 2 - loud
		UINT8 subcol;	// sub-position in column
	};
	
	UINT8 _flags;
	UINT16 _chnID;
	int _posY;
	int _color;
	std::string _insName;
	INT8 _pan;	// -1 - left, 0 - centre, +1 - right
	UINT8 _noteFlags;
	std::vector<NoteDisplay> _noteSlots;
	
	void Initialize(UINT16 chnID, size_t screenWidth);
	void ShowInsName(const char* insName, bool grey = false);
	void ShowPan(INT8 pan);
	static int CalcNoteSlot(UINT8 note, UINT8* inColPos, int ncols);
	void RefreshNotes(const NoteVisualization* noteVis, const NoteVisualization::ChnInfo* chnInfo);
	static void PadString(char* str, size_t padlen, char padchar, UINT8 padleft);
	void DrawNoteName(size_t slot);
	void RedrawAll(void);
};


static void vis_printms(double time);
static void vis_mvprintms(int row, int col, double time);


#define CHN_BASE_LINE	2
#define INS_COL_SIZE	14
#define NOTE_BASE_COL	16
#define NOTE_NAME_SPACE	3	// number of characters reserved for note names
#define NOTES_PER_COL	2	// number of notes that share the same column
#define CENTER_NOTE		60	// middle C
//static char textbuf[1024];
static int TEXT_BASE_LINE = 0;
static int curYline = 0;

static MidiFile* midFile = NULL;
static std::vector<iconv_t> hLocales;
static UINT32 trackNo = 0;
static UINT32 trackCnt = 0;
static UINT32 trackNoDigits = 1;
static const char* midFName = NULL;
static MidiPlayer* midPlay = NULL;
static const char* midFType = NULL;
static const char* midDevType = NULL;

static std::vector<ChannelData> dispChns;
static UINT64 lastUpdateTime = 0;

static std::string lastMeta01;
static std::string lastMeta03;
static std::string lastMeta04;
       std::vector<UINT8> optShowMeta;
       UINT8 optShowInsChange;

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
	
	TEXT_BASE_LINE = 0;
	curYline = 0;
	
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
	move(curYline, 0);	clrtoeol();
	addstr(text);
	curYline ++;
	if (curYline >= LINES)
		curYline = TEXT_BASE_LINE;
	move(curYline, 0);	clrtoeol();
	
	return;
}

void vis_printf(const char* format, ...)
{
	va_list args;
	
	move(curYline, 0);	clrtoeol();
	
	va_start(args, format);
	vwprintw(stdscr, format, args);
	va_end(args);
	
	curYline ++;
	if (curYline >= LINES)
		curYline = TEXT_BASE_LINE;
	move(curYline, 0);	clrtoeol();
	
	return;
}

void vis_set_locales(size_t numLocales, void* localeArrPtr)
{
	iconv_t* localeObjPtr = (iconv_t*)localeArrPtr;
	size_t curLoc;
	
	hLocales.resize(numLocales);
	for (curLoc = 0; curLoc < numLocales; curLoc ++)
		hLocales[curLoc] = localeObjPtr[curLoc];
	
	return;
}

void vis_set_track_number(UINT32 trkNo)
{
	trackNo = trkNo;
}

void vis_set_track_count(UINT32 trkCnt)
{
	trackCnt = trkCnt;
	
	trackNoDigits = 0;
	do
	{
		trkCnt /= 10;
		trackNoDigits ++;
	} while(trkCnt > 0);
	
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

void vis_set_type_str(UINT8 key, const char* typeStr)
{
	switch(key)
	{
	case 0:
		midDevType = typeStr;
		break;
	case 1:
		midFType = typeStr;
		break;
	}
	
	return;
}

void vis_new_song(void)
{
	unsigned int nTrks;
	unsigned int chnCnt;
	unsigned int curChn;
	int titlePosX;
	size_t maxTitleLen;
	char chnNameStr[0x10];
	
	nTrks = (midFile != NULL) ? midFile->GetTrackCount() : 0;
	chnCnt = (midPlay != NULL) ? midPlay->GetChannelStates().size() : 0x10;
	dispChns.clear();
	dispChns.resize(chnCnt);
	
	clear();
	curs_set(0);
	attrset(A_NORMAL);
	mvprintw(0, 0, "MIDI Player");
	mvprintw(0, 32, "Now Playing: ");
	titlePosX = getcurx(stdscr);
	mvprintw(1, 32, "[Q]uit [ ]Pause [B]Previous [N]ext");
	//mvprintw(1, 0, "00:00.0 / 00:00.0, %u %s", nTrks, (nTrks == 1) ? "track" : "tracks");
	mvprintw(1, 0, "00:00.0 / 00:00.0");
	if (midPlay != NULL)
		vis_mvprintms(1, 10, midPlay->GetSongLength());
	
	if (midDevType != NULL)
		mvprintw(0, 16, "Dev: %.10s", midDevType);
	if (midFType != NULL)
		mvprintw(1, 20, "(%.9s)", midFType);
	
	curYline = CHN_BASE_LINE;
	for (curChn = 0; curChn < chnCnt; curChn ++, curYline ++)
	{
		sprintf(chnNameStr, "Channel %2u", 1 + (curChn & 0x0F));
		dispChns[curChn].Initialize(curChn, COLS);
		dispChns[curChn].ShowInsName(chnNameStr, true);
		dispChns[curChn].ShowPan(0);
		mvaddch(curYline, NOTE_BASE_COL - 1, ACS_VLINE);
		//dispChns[curChn].RedrawAll();
	}
	mvhline(curYline, 0, ACS_HLINE, NOTE_BASE_COL - 1);
	mvaddch(curYline, NOTE_BASE_COL - 1, ACS_LRCORNER);
	curYline ++;
	TEXT_BASE_LINE = curYline;
	
	attron(A_BOLD);
	if (trackCnt > 0)
	{
		mvprintw(0, titlePosX, "%0*u / %u ", trackNoDigits, trackNo, trackCnt);
		titlePosX = getcurx(stdscr);
	}
	if (midFName != NULL)
	{
		size_t nameLen;
		
		move(0, titlePosX);
		maxTitleLen = COLS - titlePosX;
		nameLen = utf8strlen(midFName);
		if (nameLen <= maxTitleLen)
		{
			addstr(midFName);
		}
		else
		{
			size_t startChr;
			
			maxTitleLen -= 3;	// for the 3 dots
			startChr = nameLen - maxTitleLen;
			printw("...%s", utf8strseek(midFName, startChr));
		}
	}
	mvaddch(1, 33, 'Q');
	mvaddch(1, 40, ' ');
	mvaddch(1, 49, 'B');
	mvaddch(1, 61, 'N');
	attroff(A_BOLD);
	move(curYline, 0);
	refresh();
	
	lastMeta01 = "";
	lastMeta03 = "";
	lastMeta04 = "";
	
	lastUpdateTime = 0;
	
	return;
}

void vis_do_channel_event(UINT16 chn, UINT8 action, UINT8 data)
{
	size_t curChn;
	
	switch(action)
	{
	case 0x01:	// redraw all notes:
		for (curChn = 0; curChn < dispChns.size(); curChn ++)
			dispChns[curChn].RedrawAll();
		break;
	case 0x7B:	// stop all notes
		// TODO: I think this shouldn't be required anymore.
		for (curChn = 0; curChn < dispChns.size(); curChn ++)
		{
			dispChns[curChn].RefreshNotes(NULL, NULL);
			dispChns[curChn].RedrawAll();
		}
		break;
	}
	
	return;
}

void vis_do_ins_change(UINT16 chn)
{
	const MidiPlayer::ChannelState* chnSt = &midPlay->GetChannelStates()[chn];
	int posY = CHN_BASE_LINE + chn;
	int color = (chn % 6) + 1;
	std::string insName;
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
	// The symbol order is [map] [bank] [name] for all modules.
	if (MMASK_TYPE(midPlay->GetModuleType()) == MODULE_TYPE_GS)
	{
		// The order on actual Roland Sound Canvas modules would be [bank] [map] [name].
		insName = "  " + insName;
		if (chnSt->insBank[1] >= 0x01 && chnSt->insBank[1] <= 0x04)
			insName[0] = SC_MAP_SYMBOLS[chnSt->insBank[1] - 0x01];
		else
			insName[0] = ' ';
		if (chnSt->flags & 0x80)
			insName[1] = '*';	// drum channel
		else if (chnSt->insBank[0] >= 0x7E)
			insName[1] = '#';	// CM-64 sound
		else if (chnSt->insBank[0] > 0x00)
			insName[1] = '+';	// variation sound
		else
			insName[1] = ' ';	// capital sound
	}
	else if (MMASK_TYPE(midPlay->GetModuleType()) == MODULE_TYPE_XG)
	{
		insName = "  " + insName;
		if ((chnSt->flags & 0x80) && (chnSt->curIns & 0x7F) == 0x00)
			insName[0] = ' ';	// GM drums
		else if (chnSt->insBank[0] == 0x00 && chnSt->insBank[1] == 0x00)
			insName[0] = ' ';	// GM instrument
		else if (chnSt->insMapPPtr != NULL && chnSt->insMapPPtr->moduleID < 6)
			insName[0] = MU_MAP_SYMBOLS[chnSt->insMapPPtr->moduleID];
		if ((chnSt->flags & 0x80) || chnSt->insBank[0] >= 0x7E)
			insName[1] = '#';	// drum bank
		else if (chnSt->insBank[0] == 0x40)
			insName[1] = '+';	// SFX bank
		else
			insName[1] = ' ';
	}
	dispChns[chn].ShowInsName(insName.c_str());
	
	return;
}

void vis_do_ctrl_change(UINT16 chn, UINT8 ctrl, UINT8 value)
{
	const MidiPlayer::ChannelState* chnSt = &midPlay->GetChannelStates()[chn];
	const NoteVisualization::ChnInfo* nvChn = midPlay->GetNoteVis()->GetChannel(chn);
	
	switch(ctrl)
	{
	case 0x0A:	// Pan
		if (nvChn->_attr.pan < -0x15)
			dispChns[chn].ShowPan(-1);
		else if (nvChn->_attr.pan > 0x15)
			dispChns[chn].ShowPan(+1);
		else
			dispChns[chn].ShowPan(0);
		break;
	}
	
	return;
}

void vis_do_note(UINT16 chn, UINT8 note, UINT8 volume)
{
	return;
}

void str_locale_conv(std::string& text)
{
	std::string newtxt;
	size_t curLoc;
	char retVal;
	
	for (curLoc = 0; curLoc < hLocales.size(); curLoc ++)
	{
		retVal = StrCharsetConv(hLocales[curLoc], newtxt, text);
		if (! (retVal & 0x80))
		{
			text = newtxt;
			return;
		}
	}
	
	return;
}

void vis_print_meta(UINT16 trk, UINT8 metaType, size_t dataLen, const char* data)
{
	std::string text(data, &data[dataLen]);
	
	if (! optShowMeta[0] && (metaType != 1 && metaType != 6))
		return;
	
	move(curYline, 0);	clrtoeol();
	attron(COLOR_PAIR(metaType % 8));
	switch(metaType)
	{
	case 0x01:	// Text
		if (! optShowMeta[1])
			break;
		if (lastMeta01 == text)
			break;
		lastMeta01 = text;
		
		str_locale_conv(text);
		printw("Text: %s", text.c_str());
		curYline ++;
		break;
	case 0x02:	// Copyright Notice
		str_locale_conv(text);
		printw("Copyright: %s", text.c_str());
		curYline ++;
		break;
	case 0x03:	// Sequence/Track Name
		if (lastMeta03 == text)
			break;
		lastMeta03 = text;
		
		if (trk == 0 || midFile->GetMidiFormat() == 2)
		{
			attron(A_BOLD);
			str_locale_conv(text);
			printw("Title: %s", text.c_str());
			attroff(A_BOLD);
			curYline ++;
		}
		break;
	case 0x04:	// Instrument Name
		if (lastMeta04 == text)
			break;
		lastMeta04 = text;
		
		str_locale_conv(text);
		printw("Instrument Name: %s", text.c_str());
		curYline ++;
		break;
	case 0x05:	// Lyric
		// don't print for now
		break;
	case 0x06:	// Marker
		if (! optShowMeta[6])
			break;
		str_locale_conv(text);
		printw("Marker: %s", text.c_str());
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
	if (curYline >= LINES)
		curYline = TEXT_BASE_LINE;
	move(curYline, 0);	clrtoeol();
	
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
	NoteVisualization* noteVis;
	
	if (midPlay == NULL)
	{
		refresh();
		return;
	}
	noteVis = midPlay->GetNoteVis();
	
	newUpdateTime = (UINT64)(midPlay->GetPlaybackPos() * 1000.0);
	if (newUpdateTime < lastUpdateTime)
		lastUpdateTime = 0;	// fix looping
	updateTicks = (int)(newUpdateTime - lastUpdateTime);
	if (updateTicks < 20)
		return;	// update with 50 Hz maximum
	lastUpdateTime = newUpdateTime;
	
	noteVis->AdvanceAge(updateTicks);
	for (curChn = 0; curChn < dispChns.size(); curChn ++)
		dispChns[curChn].RefreshNotes(noteVis, noteVis->GetChannel(curChn));
	
	vis_mvprintms(1, 0, midPlay->GetPlaybackPos());
	vis_mvprintms(1, 10, midPlay->GetSongLength());
	move(curYline, 0);
	refresh();
	
	return;
}


void ChannelData::Initialize(UINT16 chnID, size_t screenWidth)
{
	_flags = 0x00;
	_chnID = chnID;
	_posY = CHN_BASE_LINE + _chnID;
	_color = (_chnID % 6) + 1;
	_insName = std::string(INS_COL_SIZE, ' ');
	_pan = 0;
	
	_noteFlags = 0x00;
	_noteSlots.resize((screenWidth - NOTE_BASE_COL) / NOTE_NAME_SPACE);
	for (size_t curNote = 0; curNote < _noteSlots.size(); curNote ++)
	{
		_noteSlots[curNote].note = 0xFF;
		_noteSlots[curNote].vol = 0x00;
	}
	
	return;
}

void ChannelData::ShowInsName(const char* insName, bool grey)
{
	sprintf(&_insName[0], "%-*.*s", (int)_insName.size(), (int)_insName.size(), insName);
	
	if (grey)
	{
		mvaddstr(_posY, 0, _insName.c_str());
		_flags |= 0x01;
	}
	else
	{
		_flags &= ~0x01;
		attron(A_BOLD | COLOR_PAIR(_color));
		mvaddstr(_posY, 0, _insName.c_str());
		attroff(A_BOLD | COLOR_PAIR(_color));
	}
	
	return;
}

void ChannelData::ShowPan(INT8 pan)
{
	char pChar;
	
	_pan = pan;
	
	if (_pan < 0)
		pChar = '<';
	else if (_pan > 0)
		pChar = '>';
	else
		pChar = ' ';
	attron(A_BOLD | COLOR_PAIR(_color));
	mvaddch(_posY, NOTE_BASE_COL - 2, pChar);
	attroff(A_BOLD | COLOR_PAIR(_color));
	
	return;
}

/*static*/ int ChannelData::CalcNoteSlot(UINT8 note, UINT8* inColPos, int ncols)
{
	int colpos;
	int posX;
	
#if 0
	colpos = note % NOTES_PER_COL;
	posX = note / NOTES_PER_COL;
	posX = posX % ncols;
#else
	ncols *= NOTES_PER_COL;
	// middle C = center of the screen (at ncols / 2)
	posX = note - CENTER_NOTE + (ncols / 2);
	posX += (CENTER_NOTE / ncols + 1) * ncols;	// prevent negative note values
	// scale down notes to column slots
	colpos = posX % NOTES_PER_COL;
	posX = (posX % ncols) / NOTES_PER_COL;
#endif
	
	if (inColPos != NULL)
		*inColPos = (UINT8)colpos;
	return posX;
}

void ChannelData::RefreshNotes(const NoteVisualization* noteVis, const NoteVisualization::ChnInfo* chnInfo)
{
	std::vector<NoteDisplay> newNS(_noteSlots.size());
	std::list<NoteVisualization::NoteInfo> noteList;
	std::list<NoteVisualization::NoteInfo>::const_iterator nlIt;
	size_t curNote;
	
	for (curNote = 0; curNote < newNS.size(); curNote ++)
	{
		newNS[curNote].note = 0xFF;
		newNS[curNote].vol = 0;
	}
	
	if (chnInfo != NULL && noteVis != NULL)
	{
		noteList = chnInfo->GetProcessedNoteList(noteVis->GetAttributes());
		_noteFlags = chnInfo->_chnMode;
	}
	
	for (nlIt = noteList.begin(); nlIt != noteList.end(); ++nlIt)
	{
		NoteDisplay nDisp;
		int slot;
		
		slot = CalcNoteSlot(nlIt->height, &nDisp.subcol, _noteSlots.size());
		nDisp.note = nlIt->height;
		if (nlIt->velocity < 16)	// treat very-low-velocity notes as "note off"
			nDisp.vol = 0;
		else if (nlIt->velocity <= 50)
			nDisp.vol = 1;
		else
			nDisp.vol = 2;
		
		if (nDisp.vol > 0)	// let's ignore zero-volume notes for now
			newNS[slot] = nDisp;
	}
	
	for (curNote = 0; curNote < _noteSlots.size(); curNote ++)
	{
		if (_noteSlots[curNote].note != newNS[curNote].note ||
			_noteSlots[curNote].vol != newNS[curNote].vol)
		{
			_noteSlots[curNote] = newNS[curNote];
			DrawNoteName(curNote);
		}
	}
	
	return;
}

/*static*/ void ChannelData::PadString(char* str, size_t padlen, char padchar, UINT8 padleft)
{
	size_t slen;
	size_t pos;
	
	slen = strlen(str);
	if (! padleft)
	{
		// padding - right side
		for (pos = slen; pos < padlen; pos ++)
			str[pos] = padchar;
	}
	else
	{
		// padding - left side
		if (slen < padlen)
		{
			pos = padlen - slen;
			memmove(&str[pos], &str[0], slen);
			while(pos > 0)
			{
				pos --;
				str[pos] = padchar;
			}
		}
		else
		{
			memmove(&str[0], &str[slen - padlen], padlen);
		}
	}
	str[padlen] = '\0';
	
	return;
}

void ChannelData::DrawNoteName(size_t slot)
{
	const ChannelData::NoteDisplay& nDisp = _noteSlots[slot];
	int posX = NOTE_BASE_COL + slot * NOTE_NAME_SPACE;
	int note = nDisp.note;
	char noteName[8];
	
	if (nDisp.vol == 0)
	{
		mvhline(_posY, posX, ' ', NOTE_NAME_SPACE);
		return;
	}
	
	if (_noteFlags & 0x01)
	{
		// show drum name
		// C# (crash cym 1) || G (splash cym) || A (crash cym 2)
		if (note == 0x31 || note == 0x37 || note == 0x39)
			strcpy(noteName, "cym");
		// D# (ride cym 1) || F (ride bell) || B (ride cym 2)
		else if (note == 0x33 || note == 0x35 || note == 0x3B)
			strcpy(noteName, "cym");
		else if (note == 0x2A || note == 0x2C || note == 0x2E)	// hi-hats
			strcpy(noteName, "hh");
		else
			strcpy(noteName, "drm");
	}
	else
	{
		// show note name
		sprintf(noteName, "%s%u", notes[note % 12], note / 12);
	}
	
	PadString(noteName, NOTE_NAME_SPACE, ' ', nDisp.subcol);
	attron(COLOR_PAIR(_color));
	if (nDisp.vol >= 2)
		attron(A_BOLD);
	mvaddstr(_posY, posX, noteName);
	attroff(A_BOLD | COLOR_PAIR(_color));
	
	return;
}

void ChannelData::RedrawAll(void)
{
	//move(_posY, 0);	clrtoeol();
	
	if (_flags & 0x01)
	{
		mvaddstr(_posY, 0, _insName.c_str());
	}
	else
	{
		attron(A_BOLD | COLOR_PAIR(_color));
		mvaddstr(_posY, 0, _insName.c_str());
		attroff(A_BOLD | COLOR_PAIR(_color));
	}
	ShowPan(_pan);
	mvaddch(_posY, NOTE_BASE_COL - 1, ACS_VLINE);
	for (size_t curNote = 0; curNote < _noteSlots.size(); curNote ++)
		DrawNoteName(curNote);
	
	return;
}
