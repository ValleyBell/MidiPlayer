// Note: The general view and layout is heavily inspired by "Playmidi" by Nathan Laredo.
//       In fact, I copied the colour scheme and note placement algorithm from Playmidi.
#include <stddef.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <curses.h>
#include <panel.h>
#include <stdarg.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINCON_	// wincon.h from being included (want to redefine curses constants)
#include <Windows.h>
#else
#include <unistd.h>
#define Sleep(x)	usleep(x * 1000)
#endif

#include <stdtype.h>
#include "MidiModules.hpp"
#include "MidiLib.hpp"
#include "MidiPlay.hpp"
#include "NoteVis.hpp"
#include "vis.hpp"
#include "utils.hpp"
#include "MidiInsReader.h"	// for MIDI module type
#include "vis_sc-lcd.hpp"

static const char* notes[12] =
	{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Roland Symbols:
//	Map:	" - SC-55 map (LSB 1), ' - SC-88 map (LSB 2)
//	Bank:	* - drum bank, _ - GM map (MSB 0 after GM reset),
//			[space] - capital bank (LSB 0), + - variation bank (LSB 1..125), # - MT-32/CM-64 bank (MSB 126/127)
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
	'\'',	// SC-55 map
	'\"',	// SC-88 map
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
	void ShowPan(INT8 pan, bool grey = false);
	static int CalcNoteSlot(UINT8 note, UINT8* inColPos, int ncols);
	void RefreshNotes(const NoteVisualization* noteVis, const NoteVisualization::ChnInfo* chnInfo);
	static void PadString(char* str, size_t padlen, char padchar, UINT8 padleft);
	void DrawNoteName(size_t slot);
	void RedrawAll(void);
};


typedef int (*KEYHANDLER)(void);

static void refresh_cursor_y(void);
static void vis_printms(double time);
static void vis_mvprintms(int row, int col, double time);
static int vis_keyhandler_normal(void);
static int vis_keyhandler_mapsel(void);
static void vis_show_map_selection(void);


#define CHN_BASE_LINE	3
#define INS_COL_SIZE	14
#define NOTE_BASE_COL	16
#define NOTE_NAME_SPACE	3	// number of characters reserved for note names
#define NOTES_PER_COL	2	// number of notes that share the same column
#define CENTER_NOTE		60	// middle C
//static char textbuf[1024];
static int curYline = 0;

static MidiModuleCollection* midiModColl = NULL;
static MidiFile* midFile = NULL;
static std::vector<iconv_t> hLocales;
static UINT32 trackNo = 0;	// 1 = first track
static UINT32 trackCnt = 0;
static UINT32 trackNoDigits = 1;
static const char* midFName = NULL;
static MidiPlayer* midPlay = NULL;
static const char* midDevType = NULL;

static std::vector<ChannelData> dispChns;
static UINT64 lastUpdateTime = 0;

static std::string lastMeta01;
static std::string lastMeta03;
static std::string lastMeta04;
       std::vector<UINT8> optShowMeta;
       UINT8 optShowInsChange;

static std::vector<KEYHANDLER> currentKeyHandler;

// Note Visualiztion
static WINDOW* nvWin = NULL;
static PANEL* nvPan = NULL;

// Log Window
static WINDOW* logWin = NULL;
static PANEL* logPan = NULL;

// Sound Canvas Display
static LCDDisplay lcdDisp;
static PANEL* lcdPan = NULL;

// MIDI map selection
static WINDOW* mmsWin = NULL;
static PANEL* mmsPan = NULL;
static int mmsSelection;
static std::vector<UINT8> mapSelTypes;

void vis_init(void)
{
	int posX, posY, sizeY;
	
	initscr();
	cbreak();
	keypad(stdscr, TRUE);
	noecho();
	curs_set(0);
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
	
	posY = CHN_BASE_LINE;	sizeY = 16 + 1;
	nvWin = newwin(sizeY, COLS, posY, 0);
	nvPan = new_panel(nvWin);
	
	posY += sizeY;	sizeY = LINES - posY;
	logWin = newwin(sizeY, COLS, posY, 0);
	logPan = new_panel(logWin);
	
	lcdDisp.GetSize(&posX, &sizeY);
	posX = COLS - posX;
	if (posY + sizeY > LINES)
		posY = LINES - sizeY;
	lcdDisp.Init(posX, posY);
	lcdPan = new_panel(lcdDisp.GetWindow());
	
	mmsPan = NULL;
	mmsWin = NULL;
	
	curYline = 0;
	
	return;
}

static void vis_clear_all_menus(void)
{
	if (mmsPan != NULL)
	{
		del_panel(mmsPan);
		mmsPan = NULL;
	}
	if (mmsWin != NULL)
	{
		delwin(mmsWin);
		mmsWin = NULL;
	}
	update_panels();
	
	return;
}

void vis_deinit(void)
{
	attrset(A_NORMAL);
	refresh();
	
	mvcur(0, 0, getbegy(logWin) + curYline, 0);
	
	vis_clear_all_menus();
	del_panel(nvPan);	delwin(nvWin);
	del_panel(logPan);	delwin(logWin);
	del_panel(lcdPan);	lcdDisp.Deinit();
	
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
	wmove(logWin, curYline, 0);	wclrtoeol(logWin);
	waddstr(logWin, text);
	curYline ++;
	refresh_cursor_y();
	wmove(logWin, curYline, 0);	wclrtoeol(logWin);
	
	return;
}

void vis_printf(const char* format, ...)
{
	va_list args;
	
	wmove(logWin, curYline, 0);	wclrtoeol(logWin);
	
	va_start(args, format);
	vwprintw(logWin, format, args);
	va_end(args);
	
	curYline ++;
	refresh_cursor_y();
	wmove(logWin, curYline, 0);	wclrtoeol(logWin);
	
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

void vis_set_midi_modules(MidiModuleCollection* mmc)
{
	midiModColl = mmc;
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
	const PlayerOpts* midOpts;
	int posX, posY, sizeY;
	WINDOW* oldWin;
	
	lcdDisp.ResetDisplay();
	vis_clear_all_menus();
	clear();
	// explicit redrawing prevents graphical glitches caused by printf() commands
	clearok(stdscr, TRUE);
	refresh();
	
	nTrks = (midFile != NULL) ? midFile->GetTrackCount() : 0;
	chnCnt = (midPlay != NULL) ? midPlay->GetChannelStates().size() : 0x10;
	dispChns.clear();
	dispChns.resize(chnCnt);
	lcdDisp.SetNoteVis((midPlay != NULL) ? midPlay->GetNoteVis() : NULL);
	
	oldWin = nvWin;
	posY = CHN_BASE_LINE;
	sizeY = chnCnt + 1;
	if (posY + sizeY + 1 >= LINES)
		sizeY = 0x10 + 1;
	nvWin = newwin(sizeY, COLS, posY, 0);
	replace_panel(nvPan, nvWin);
	delwin(oldWin);
	
	oldWin = logWin;
	posY += sizeY;	sizeY = LINES - posY;
	logWin = newwin(sizeY, COLS, posY, 0);
	replace_panel(logPan, logWin);
	delwin(oldWin);
	
	lcdDisp.GetSize(&posX, &sizeY);
	posX = COLS - posX;
	if (posY + sizeY > LINES)
		posY = LINES - sizeY;
	move_panel(lcdPan, posY, posX);
	
	midOpts = (midPlay != NULL) ? &midPlay->GetOptions() : NULL;
	mapSelTypes.clear();
	for (UINT8 curMap = 0x00; curMap < 0xFF; curMap ++)
	{
		if (! midiModColl->GetShortModName(curMap).empty())
			mapSelTypes.push_back(curMap);
		else if (midOpts != NULL && (curMap == midOpts->srcType || curMap == midOpts->dstType))
			mapSelTypes.push_back(curMap);
	}
	
	attrset(A_NORMAL);
	
	mvprintw(0, 0, "MIDI Player");
	mvprintw(0, 32, "Now Playing: ");
	titlePosX = getcurx(stdscr);
	mvprintw(1, 32, "[Q]uit [ ]Pause [B]Previous [N]ext");
	attron(A_BOLD);
	mvaddch(1, 33, 'Q');
	mvaddch(1, 40, ' ');
	mvaddch(1, 49, 'B');
	mvaddch(1, 61, 'N');
	attroff(A_BOLD);
	
	mvprintw(1, 0, "00:00.0 / 00:00.0");
	if (midPlay != NULL)
		vis_mvprintms(1, 10, midPlay->GetSongLength());
	
	if (midDevType != NULL)
		mvprintw(0, 16, "Dev: %.10s", midDevType);
	if (midOpts != NULL)
	{
		const std::string& mapStr = midiModColl->GetShortModName(midOpts->srcType);
		const char* mapStr2 = (! mapStr.empty()) ? mapStr.c_str() : "unknown";
		mvprintw(1, 20, "(%.9s)", mapStr2);
	}
	
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
	attroff(A_BOLD);
	
	mvprintw(2, 0, "%u %s (Format %u), %u TpQ",
			midFile->GetTrackCount(), (midFile->GetTrackCount() == 1) ? "Track" : "Tracks",
			midFile->GetMidiFormat(), midFile->GetMidiResolution());
	
	mvwvline(nvWin, 0, NOTE_BASE_COL - 1, ACS_VLINE, chnCnt);
	mvwhline(nvWin, chnCnt, 0, ACS_HLINE, NOTE_BASE_COL - 1);
	mvwaddch(nvWin, chnCnt, NOTE_BASE_COL - 1, ACS_LRCORNER);
	for (curChn = 0; curChn < chnCnt; curChn ++)
		vis_do_channel_event(curChn, 0x00, 0x00);
	
	lcdDisp.FullRedraw();
	
	mvcur(0, 0, getbegy(logWin), 0);
	update_panels();
	refresh();
	
	lastMeta01.clear();
	lastMeta03.clear();
	lastMeta04.clear();
	
	currentKeyHandler.clear();
	currentKeyHandler.push_back(&vis_keyhandler_normal);
	lastUpdateTime = 0;
	curYline = 0;
	
	return;
}

void vis_do_channel_event(UINT16 chn, UINT8 action, UINT8 data)
{
	if (chn >= dispChns.size())
		return;
	ChannelData& dispCh = dispChns[chn];
	
	switch(action)
	{
	case 0x00:	// reinitialize
		{
			char chnNameStr[0x10];
			
			if (dispChns.size() <= 0x10)
				sprintf(chnNameStr, "Channel %2u", 1 + (chn & 0x0F));
			else
				sprintf(chnNameStr, "Channel %c%2u", 'A' + (chn >> 4), 1 + (chn & 0x0F));
			dispCh.Initialize(chn, COLS);
			dispCh.ShowInsName(chnNameStr, true);
			dispCh.ShowPan(0, true);
			dispCh.RefreshNotes(NULL, NULL);
			for (size_t curNote = 0; curNote < dispCh._noteSlots.size(); curNote ++)
				dispCh.DrawNoteName(curNote);
		}
		break;
	case 0x01:	// redraw all notes
		dispCh.RedrawAll();
		break;
	}
	
	return;
}

void vis_do_ins_change(UINT16 chn)
{
	const MidiPlayer::ChannelState* chnSt = &midPlay->GetChannelStates()[chn];
	const MidiPlayer::InstrumentInfo* insInf = &chnSt->insSend;
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
		if (insInf->bankPtr != NULL)
			insName = insInf->bankPtr->insName;
		else
			insName = "--unknown--";
	}
	// The symbol order is [map] [bank] [name] for all modules.
	if (MMASK_TYPE(midPlay->GetModuleType()) == MODULE_TYPE_GS)
	{
		// The order on actual Roland Sound Canvas modules would be [bank] [map] [name].
		insName = "  " + insName;
		if (insInf->bank[1] >= 0x01 && insInf->bank[1] <= 0x04)
			insName[0] = SC_MAP_SYMBOLS[insInf->bank[1] - 0x01];
		else
			insName[0] = ' ';
		if (chnSt->flags & 0x80)
			insName[1] = '*';	// drum channel
		else if (insInf->bank[0] >= 0x7E)
			insName[1] = '#';	// CM-64 sound
		else if (insInf->bank[0] > 0x00)
			insName[1] = '+';	// variation sound
		else
			insName[1] = ' ';	// capital sound
	}
	else if (MMASK_TYPE(midPlay->GetModuleType()) == MODULE_TYPE_XG)
	{
		insName = "  " + insName;
		if ((chnSt->flags & 0x80) && chnSt->curIns == 0x00)
			insName[0] = ' ';	// GM drums
		else if (insInf->bank[0] == 0x00 && insInf->bank[1] == 0x00)
			insName[0] = ' ';	// GM instrument
		else if (insInf->bankPtr != NULL && insInf->bankPtr->moduleID < 6)
			insName[0] = MU_MAP_SYMBOLS[insInf->bankPtr->moduleID];
		if ((chnSt->flags & 0x80) || insInf->bank[0] >= 0x7E)
			insName[1] = '#';	// drum bank
		else if (insInf->bank[0] == 0x40)
			insName[1] = '+';	// SFX bank
		else
			insName[1] = ' ';
		
		if (insName[1] == ' ')	// make [bank] optional, as XG names can be pretty long
			insName = insName[0] + insName.substr(2);
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

void vis_do_syx_text(UINT16 chn, UINT8 mode, size_t textLen, const char* text)
{
	std::string textStr(text, textLen);
	switch(mode)
	{
	case 0x16:	// Roland MT-32 Display
		break;
	case 0x42:	// Roland SC ALL Display
		lcdDisp._modName = textStr;
		return;
	case 0x43:	// Yamaha MU Display
		break;
	case 0x45:	// Roland SC Display
		break;
	}
	lcdDisp.SetTemporaryText(textStr.c_str());
	
	return;
}

void vis_do_syx_bitmap(UINT16 chn, UINT8 mode, UINT32 dataLen, const UINT8* data)
{
	switch(mode)
	{
	case 0x43:	// Yamaha MU Dot Bitmap
		break;
	case 0x45:	// Roland SC Dot Display
		if (! dataLen || data == NULL)
			return;
		if ((chn & 0x0F) > 1)
			return;	// We can't do caching yet.
		{
			std::bitset<0x100> bitmap;
			LCDDisplay::SCSysEx2DotMatrix(dataLen, data, bitmap);
			lcdDisp.SetTemporaryDotMatrix(bitmap);
		}
		break;
	}
	return;
}

static void str_locale_conv(std::string& text)
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

static bool string_is_empty(const std::string& str)
{
	if (str.empty())
		return true;
	
	size_t curChr;
	
	// return 'true' for strings that consist entirely of whitespace
	for (curChr = 0; curChr < str.length(); curChr ++)
	{
		if (! isspace((unsigned char)str[curChr]))
			return false;
	}
	return true;
}

void vis_print_meta(UINT16 trk, UINT8 metaType, size_t dataLen, const char* data)
{
	std::string text(data, &data[dataLen]);
	
	if (! optShowMeta[0] && (metaType != 1 && metaType != 6))
		return;
	
	wmove(logWin, curYline, 0);	wclrtoeol(logWin);
	wattron(logWin, COLOR_PAIR(metaType % 8));
	switch(metaType)
	{
	case 0x01:	// Text
		if (! optShowMeta[1])
			break;
		if (lastMeta01 == text)
			break;
		lastMeta01 = text;
		
		str_locale_conv(text);
		wprintw(logWin, "Text: %s", text.c_str());
		curYline ++;
		break;
	case 0x02:	// Copyright Notice
		str_locale_conv(text);
		wprintw(logWin, "Copyright: %s", text.c_str());
		curYline ++;
		break;
	case 0x03:	// Sequence/Track Name
		//if (lastMeta03 == text)
		//	break;
		lastMeta03 = text;
		
		if (trk == 0 || midFile->GetMidiFormat() == 2)
		{
			wattron(logWin, A_BOLD);
			str_locale_conv(text);
			wprintw(logWin, "Title: %s", text.c_str());
			wattroff(logWin, A_BOLD);
			curYline ++;
		}
		else if (true)
		{
			//if (! optShowMeta[3])
			//	break;
			if (string_is_empty(text))
				break;
			str_locale_conv(text);
			wprintw(logWin, "Track %u Name: %s", trk, text.c_str());
			curYline ++;
		}
		break;
	case 0x04:	// Instrument Name
		//if (! optShowMeta[4])
		//	break;
		if (lastMeta04 == text)
			break;
		lastMeta04 = text;
		
		str_locale_conv(text);
		wprintw(logWin, "Instrument Name: %s", text.c_str());
		curYline ++;
		break;
	case 0x05:	// Lyric
		// don't print for now
		break;
	case 0x06:	// Marker
		if (! optShowMeta[6])
			break;
		str_locale_conv(text);
		wprintw(logWin, "Marker: %s", text.c_str());
		curYline ++;
		break;
	case 0x51:	// Tempo
		break;
	case 0x58:	// Time Signature
		break;
	case 0x59:	// Key Signature
		break;
	}
	wattroff(logWin, COLOR_PAIR(metaType % 8));
	refresh_cursor_y();
	wmove(logWin, curYline, 0);	wclrtoeol(logWin);
	
	return;
}

static void refresh_cursor_y(void)
{
	if (curYline < getcury(logWin))
		curYline = getcury(logWin);	// for Y position after multi-line print
	
	if (curYline >= getmaxy(logWin))
		curYline = 0;	// wrap around
	
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
		update_panels();
		refresh();
		return;
	}
	
	newUpdateTime = (UINT64)(midPlay->GetPlaybackPos() * 1000.0);
	if (newUpdateTime < lastUpdateTime)
		lastUpdateTime = 0;	// fix looping
	updateTicks = (int)(newUpdateTime - lastUpdateTime);
	lastUpdateTime = newUpdateTime;
	
	noteVis = midPlay->GetNoteVis();
	noteVis->AdvanceAge(updateTicks);
	lcdDisp.AdvanceTime(updateTicks);
	for (curChn = 0; curChn < dispChns.size(); curChn ++)
		dispChns[curChn].RefreshNotes(noteVis, noteVis->GetChannel(curChn));
	lcdDisp.RefreshDisplay();
	
	vis_mvprintms(1, 0, midPlay->GetPlaybackPos());
	vis_mvprintms(1, 10, midPlay->GetSongLength());
	update_panels();
	refresh();
	
	return;
}

static int vis_keyhandler_normal(void)
{
	int inkey;
	
	inkey = vis_getch();
	if (! inkey)
		return 0;
	
	if (inkey < 0x100 && isalpha(inkey))
		inkey = toupper(inkey);
	
	switch(inkey)
	{
	case 0x1B:	// ESC
	case 'Q':
		return 9;	// quit
	case ' ':
		if (midPlay->GetState() & 0x02)
			midPlay->Resume();
		else
			midPlay->Pause();
		break;
	case 'B':
		if (trackNo > 1)
			return -1;	// previous song
		break;
	case 'N':
		if (trackNo < trackCnt)
			return +1;	// next song
		break;
	case 'R':
		midPlay->Stop();
		midPlay->Start();
		break;
	case 'M':
		vis_show_map_selection();
		break;
	}
	
	return 0;
}

int vis_main(void)
{
	// Note: returns playback command
	//	+1 - next song (default)
	//	-1 - previous song
	//	+9 - quit
	UINT64 newUpdateTime;
	int result;
	
	lastUpdateTime = 0;
	result = +1;	// default: finished normally - next song
	while(midPlay->GetState() & 0x01)
	{
		int retval = 0;
		
		midPlay->DoPlaybackStep();
		
		newUpdateTime = (UINT64)(midPlay->GetPlaybackPos() * 1000.0);
		// update after reset OR when 20 ms have passed
		if (newUpdateTime < lastUpdateTime || newUpdateTime >= lastUpdateTime + 20)
			vis_update();
		
		retval = currentKeyHandler.back()();
		if (retval != 0)
		{
			result = retval;
			break;
		}
		Sleep(1);
	}
	
	vis_clear_all_menus();
	move(getbegy(logWin) + curYline, 0);
	
	return result;
}

static int vis_keyhandler_mapsel(void)
{
	int inkey;
	int cursorPos;
	
	inkey = vis_getch();
	if (! inkey)
		return 0;
	
	if (inkey < 0x100 && isalpha(inkey))
		inkey = toupper(inkey);
	
	cursorPos = mmsSelection;
	switch(inkey)
	{
	case 0x1B:	// ESC
	case 'Q':
	case '\n':
		del_panel(mmsPan);	mmsPan = NULL;
		delwin(mmsWin);		mmsWin = NULL;
		currentKeyHandler.pop_back();
		
		if (inkey == '\n')
		{
			// confirm selection
			UINT8 mapType = mapSelTypes[mmsSelection];
			midPlay->SetSrcModuleType(mapType, true);
			
			const PlayerOpts& midOpts = midPlay->GetOptions();
			const std::string& mapStr = midiModColl->GetShortModName(midOpts.srcType);
			const char* mapStr2 = (! mapStr.empty()) ? mapStr.c_str() : "unknown";
			mvhline(1, 20, ' ', 11);
			mvprintw(1, 20, "(%.9s)", mapStr2);
		}
		
		update_panels();
		refresh();
		return 0;
	case KEY_UP:
		cursorPos --;
		if (cursorPos < 0)
			cursorPos = 0;
		break;
	case KEY_DOWN:
		cursorPos ++;
		if (cursorPos >= mapSelTypes.size())
			cursorPos = mapSelTypes.size() - 1;
		break;
	case KEY_PPAGE:
		cursorPos = 0;
		break;
	case KEY_NPAGE:
		cursorPos = mapSelTypes.size() - 1;
		break;
	}
	if (cursorPos != mmsSelection)
	{
		int sizeX = getmaxx(mmsWin);
		mvwchgat(mmsWin, 1 + mmsSelection, 1, sizeX - 2, A_NORMAL, 0, NULL);
		mmsSelection = cursorPos;
		mvwchgat(mmsWin, 1 + mmsSelection, 1, sizeX - 2, A_REVERSE, 0, NULL);
		
		wrefresh(mmsWin);
	}
	
	return 0;
}

static void vis_show_map_selection(void)
{
	static const char* menuTitle = "Select Ins. Map";
	size_t mtLen = strlen(menuTitle);
	int sizeX, sizeY;
	int wsx, wsy;
	UINT8 midMapType;
	
	midMapType = (midPlay != NULL) ? midPlay->GetOptions().srcType : 0x00;
	mmsSelection = 0;
	sizeX = 0;
	for (size_t curMap = 0; curMap < mapSelTypes.size(); curMap ++)
	{
		UINT8 mapType = mapSelTypes[curMap];
		const std::string& mapStr = midiModColl->GetLongModName(mapType);
		if (sizeX < mapStr.length())
			sizeX = mapStr.length();
		if (mapType == midMapType)
			mmsSelection = (int)curMap;
	}
	sizeX += 3;	// 3 for map ID number
	if (sizeX < mtLen)
		sizeX = mtLen;
	sizeX += 4;	// add additional space for borders and margin
	sizeX += (sizeX ^ mtLen) & 1;	// make title nicely aligned (make both even or odd)
	sizeY = mapSelTypes.size() + 2;
	
	wsx = (COLS - sizeX) / 2;	wsy = (LINES - sizeY) / 2;	// center of the screen
	mmsWin = newwin(sizeY, sizeX, wsy, wsx);
	mmsPan = new_panel(mmsWin);
	box(mmsWin, 0, 0);
	
	wattron(mmsWin, A_BOLD | COLOR_PAIR(0));
	mvwaddstr(mmsWin, 0, (sizeX - mtLen) / 2, menuTitle);
	wattroff(mmsWin, A_BOLD | COLOR_PAIR(0));
	
	for (size_t curMap = 0; curMap < mapSelTypes.size(); curMap ++)
	{
		UINT8 mapType = mapSelTypes[curMap];
		const std::string& mapStr = midiModColl->GetLongModName(mapType);
		const char* mapStr2 = (! mapStr.empty()) ? mapStr.c_str() : "unknown";
		
		mvwprintw(mmsWin, 1 + curMap, 2, "%02X %s", mapType, mapStr2);
	}
	mvwchgat(mmsWin, 1 + mmsSelection, 1, sizeX - 2, A_REVERSE, 0, NULL);
	
	update_panels();
	refresh();
	
	currentKeyHandler.push_back(&vis_keyhandler_mapsel);
	
	return;
}


void ChannelData::Initialize(UINT16 chnID, size_t screenWidth)
{
	_flags = 0x00;
	_chnID = chnID;
	_posY = _chnID;
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
		mvwaddstr(nvWin, _posY, 0, _insName.c_str());
		_flags |= 0x01;
	}
	else
	{
		_flags &= ~0x01;
		wattron(nvWin, A_BOLD | COLOR_PAIR(_color));
		mvwaddstr(nvWin, _posY, 0, _insName.c_str());
		wattroff(nvWin, A_BOLD | COLOR_PAIR(_color));
	}
	
	return;
}

void ChannelData::ShowPan(INT8 pan, bool grey)
{
	chtype pChar;
	
	_pan = pan;
	
	if (_pan < 0)
		pChar = '<';
	else if (_pan > 0)
		pChar = '>';
	else
		pChar = ACS_BULLET;	// ' '
	if (grey)
	{
		mvwaddch(nvWin, _posY, NOTE_BASE_COL - 2, pChar);
		_flags |= 0x02;
	}
	else
	{
		_flags &= ~0x02;
		wattron(nvWin, A_BOLD | COLOR_PAIR(_color));
		mvwaddch(nvWin, _posY, NOTE_BASE_COL - 2, pChar);
		wattroff(nvWin, A_BOLD | COLOR_PAIR(_color));
	}
	
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
		mvwhline(nvWin, _posY, posX, ' ', NOTE_NAME_SPACE);
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
	wattron(nvWin, COLOR_PAIR(_color));
	if (nDisp.vol >= 2)
		wattron(nvWin, A_BOLD);
	mvwaddstr(nvWin, _posY, posX, noteName);
	wattroff(nvWin, A_BOLD | COLOR_PAIR(_color));
	
	return;
}

void ChannelData::RedrawAll(void)
{
	//wmove(nvWin, _posY, 0);	wclrtoeol(nvWin);
	
	if (_flags & 0x01)
	{
		mvwaddstr(nvWin, _posY, 0, _insName.c_str());
	}
	else
	{
		wattron(nvWin, A_BOLD | COLOR_PAIR(_color));
		mvwaddstr(nvWin, _posY, 0, _insName.c_str());
		wattroff(nvWin, A_BOLD | COLOR_PAIR(_color));
	}
	ShowPan(_pan, (_flags & 0x02) ? true : false);
	mvwaddch(nvWin, _posY, NOTE_BASE_COL - 1, ACS_VLINE);
	for (size_t curNote = 0; curNote < _noteSlots.size(); curNote ++)
		DrawNoteName(curNote);
	
	return;
}
