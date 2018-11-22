#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>
#include <bitset>
#include <curses.h>
#include <stdarg.h>

#include <stdtype.h>
#include "MidiPlay.hpp"
#include "NoteVis.hpp"
#include "vis_sc-lcd.hpp"


/*
	Part    A01	<--------- page title [16-32 chars] ---------->
	Ins     001	Instrument Name
	Level   100	-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	Pan     000	-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	Expr    000	-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	Reverb  000	-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	Chorus  000	-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	Delay   000	-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	K Shift 000	-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	MIDI CH A01	-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	           	01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16
*/

#define MATRIX_BASE_X	16
#define MATRIX_BASE_Y	2
#define MAT_XY2IDX(x, y)	((y) * 0x10 + (x))
#define DEFAULT_NOTE_AGE	800.0f	// in msec
static int MATRIX_COL_SIZE = 3;
static bool doLiveVis = true;

LCDDisplay::LCDDisplay() :
	_hWin(NULL),
	_mPlr(NULL),
	_nVis(NULL)
{
}

LCDDisplay::~LCDDisplay()
{
	Deinit();
	
	return;
}

void LCDDisplay::Init(int winPosX, int winPosY)
{
	_longLineMode = LLM_NO_ANIM;
	_pageMode = PAGEMODE_ALL;
	_barVisLayout = BVL_SINGLE;
	_ttMode = TTMODE_NONE;
	
	ResetDisplay();
	
	_hWin = newwin(MATRIX_BASE_Y + 10, MATRIX_BASE_X + 16 * 3, winPosY, winPosX);
	
	return;
}

void LCDDisplay::Deinit(void)
{
	if (_hWin != NULL)
	{
		delwin(_hWin);
		_hWin = NULL;
	}
	return;
}

void LCDDisplay::SetMidiPlayer(MidiPlayer* mPlr)
{
	_mPlr = mPlr;
	return;
}

void LCDDisplay::SetNoteVis(NoteVisualization* nVis)
{
	_nVis = nVis;
	return;
}

void LCDDisplay::GetSize(int* sizeX, int* sizeY) const
{
	if (sizeX != NULL)
		*sizeX = MATRIX_BASE_X + 16 * 3;
	if (sizeY != NULL)
		*sizeY = 10;
	
	return;
}

WINDOW* LCDDisplay::GetWindow(void)
{
	return _hWin;
}

void LCDDisplay::ResetDisplay(void)
{
	_ttMode = TTMODE_NONE;
	
	/*for (UINT8 y = 0; y < 16; y ++)
	{
		for (UINT8 x = 0; x < 16; x++)
			_dotMatrix[MAT_XY2IDX(x, y)] = ((x ^ y) & 0x01) ? true : false;
	}*/
	_dotMatrix.reset();
	
	_modName = "-Visualization- ";
	_allPage.partID = 0x00;
	_allPage.chnID = 0x00;
	_allPage.insID = 0xFF;
	_allPage.title = _modName.c_str();
	_allPage.vol = 127;
	_allPage.pan = 0x00;
	_allPage.expr = 127;
	_allPage.reverb = 64;
	_allPage.chorus = 64;
	_allPage.delay = 0;
	_allPage.transp = 0;
	_chnPage = _allPage;
	_chnPage.insID = 0x00;
	_chnPage.vol = 100;
	_chnPage.reverb = 40;
	_chnPage.chorus = 0;
	_chnPage.title = "- Channel Vis. -";
	
	_ttTimeout = 0;
	_tdmTimeout = 0;
	_tbTimeout = 0;
	
	if (_nVis != NULL && _nVis->GetChnGroupCount() >= 2)
		_barVisLayout = BVL_DOUBLE;
	else
		_barVisLayout = BVL_SINGLE;
	
	if (_hWin != NULL)
	{
		RedrawBitmap(_dotMatrix);
		RedrawDotMatrix(_dotMatrix);
	}
	
	return;
}

void LCDDisplay::AdvanceTime(UINT32 time)
{
	if (_ttTimeout > 0)
	{
		_ttTimeout -= time;
		if (_ttMode == TTMODE_SCROLL && _ttTimeout <= 0)
		{
			if (! AdvanceScrollingText())
				_ttTimeout = 0;
		}
		if (_ttTimeout <= 0)
		{
			_ttTimeout = 0;
			wmove(_hWin, 0, MATRIX_BASE_X);	wclrtoeol(_hWin);
			wmove(_hWin, 1, MATRIX_BASE_X);	wclrtoeol(_hWin);
			_ttMode = TTMODE_NONE;
		}
	}
	if (_tdmTimeout > 0)
	{
		_tdmTimeout -= time;
		if (_tdmTimeout <= 0)
		{
			_tdmTimeout = 0;
			RedrawDotMatrix(_dotMatrix);
		}
	}
	if (_tbTimeout > 0)
	{
		_tbTimeout -= time;
		if (_tbTimeout < 0)
		{
			_tbTimeout = 0;
			RedrawBitmap(std::bitset<0x100>());
			if (_tdmTimeout > 0)
				RedrawDotMatrix(_tDotMatrix);
			else
				RedrawDotMatrix(_dotMatrix);
		}
	}
	
	return;
}

bool LCDDisplay::AdvanceScrollingText(void)
{
	if (_longLineMode == LLM_SCROLL_SIMPLE)
	{
		while(_ttTimeout <= 0 && _ttScrollPos < _ttScrollEnd)
		{
			_ttScrollPos ++;
			if (_ttScrollPos < _ttScrollEnd - 1)
				_ttTimeout += 300;
			else
				_ttTimeout += 1800;	// larger timeout for the last character
		}
	}
	else
	{
		while(_ttTimeout <= 0 && _ttScrollPos < _ttScrollEnd)
		{
			_ttScrollPos ++;
			_ttTimeout += 300;
		}
	}
	if (_ttScrollPos >= _ttScrollEnd)
		return false;
	DrawTitleText();
	
	return true;
}

static void DrawDotBar(std::bitset<0x100>& matrix, int posX, int fillY, int startY, int sizeY)
{
	if (posX < 0 || posX >= 0x10 || startY < 0 || startY + sizeY > 0x10)
		return;
	
	for (int posY = 0x00; posY < sizeY; posY ++)
		matrix[MAT_XY2IDX(posX, 0x0F - startY - posY)] = (posY <= fillY);
	
	return;
}

void LCDDisplay::RefreshDisplay(void)
{
	size_t chnCnt;
	size_t curChn;
	const NoteVisualization::MidiModifiers& modAttr = _nVis->GetAttributes();
	const NoteVisualization::ChnInfo* chnInfo;
	const NoteVisualization::MidiModifiers* chnAttr;
	const std::vector<MidiPlayer::ChannelState>& chnStates = _mPlr->GetChannelStates();
	
	_allPage.vol = modAttr.volume;
	_allPage.pan = modAttr.pan;
	_allPage.expr = modAttr.expression;
	//_allPage.reverb = 0;
	//_allPage.chorus = 0;
	//_allPage.delay = 0;
	_allPage.transp = modAttr.detune[1] >> 8;
	
	if (_chnPage.chnID < chnStates.size())
	{
		const MidiPlayer::ChannelState& chnSt = chnStates[_chnPage.chnID];
		chnAttr = &_nVis->GetChannel(_chnPage.chnID)->_attr;
		_chnPage.insID = chnSt.insState[2];
		if (_chnPage.insID == 0xFF)
			_chnPage.insID = 0x00;
		//_chnPage.title = (chnSt.insSend.bankPtr != NULL) ? chnSt.insSend.bankPtr->insName : "--unknown--";
		_chnPage.vol =  chnAttr->volume;
		_chnPage.pan =  chnAttr->pan;
		_chnPage.expr = chnAttr->expression;
		_chnPage.reverb = chnSt.ctrls[0x5B];
		_chnPage.chorus = chnSt.ctrls[0x5D];
		_chnPage.delay = chnSt.ctrls[0x5E];
		_chnPage.transp = chnAttr->detune[1] >> 8;
	}
	
	chnCnt = _nVis->GetChnGroupCount() * 0x10;
	for (curChn = 0; curChn < chnCnt; curChn ++)
	{
		chnInfo = _nVis->GetChannel(curChn);
		chnAttr = &chnInfo->_attr;
		const std::list<NoteVisualization::NoteInfo>& noteList = chnInfo->GetNoteList();
		std::list<NoteVisualization::NoteInfo>::const_iterator nlIt;
		float chnVol = (modAttr.volume * modAttr.expression * chnAttr->volume * chnAttr->expression) / (float)0x0F817E01;
		float barHeight = 0.0;
		int barYHeight;
		
		for (nlIt = noteList.begin(); nlIt != noteList.end(); ++nlIt)
		{
			float noteVol = nlIt->velocity / 127.0f * chnVol;
			float ageAttenuate;
			if (nlIt->maxAge)
				ageAttenuate = 1.0f - nlIt->curAge / (float)nlIt->maxAge;
			else
				ageAttenuate = 1.0f - nlIt->curAge / DEFAULT_NOTE_AGE;
			if (ageAttenuate < 0.0f)
				ageAttenuate = 0.0f;
			noteVol *= ageAttenuate;
			if (barHeight < noteVol)
				barHeight = noteVol;
		}
		if (doLiveVis)
		{
			int chnID = curChn & 0x0F;
			int chnGrp = curChn >> 4;
			if (_barVisLayout == BVL_SINGLE)
			{
				// 1x 16 channels
				barYHeight = static_cast<int>(barHeight * 15 + 0.5);
				DrawDotBar(_dotMatrix, chnID, barYHeight, 0x00, 0x10);
			}
			else
			{
				// 2x 16 channels (half height)
				//  1st set: upper half
				//  2nd set: lower half
				barYHeight = static_cast<int>(barHeight * 15 / 2 + 0.5);
				DrawDotBar(_dotMatrix, chnID, barYHeight, (~chnGrp & 0x01) * 0x08, 0x08);
			}
		}
	}
	
	if (_pageMode == PAGEMODE_ALL)
		DrawPage(_allPage);
	else if (_pageMode == PAGEMODE_CHN)
		DrawPage(_chnPage);
	if (! _tdmTimeout && doLiveVis)
		RedrawDotMatrix(_dotMatrix);
	if (_tbTimeout > 0)
		RedrawBitmap(_tBitmap);
	
	return;
}

void LCDDisplay::FullRedraw(void)
{
	werase(_hWin);
	DrawLayout();
	if (_pageMode == PAGEMODE_ALL)
		DrawPage(_allPage);
	else if (_pageMode == PAGEMODE_CHN)
		DrawPage(_chnPage);
	DrawTitleText();
	if (_tdmTimeout > 0)
		RedrawDotMatrix(_tDotMatrix);
	else
		RedrawDotMatrix(_dotMatrix);
	if (_tbTimeout > 0)
		RedrawBitmap(_tBitmap);
	
	return;
}

void LCDDisplay::DrawLayout(void)
{
	mvwprintw(_hWin, 0, 0, "Part");
	mvwprintw(_hWin, 1, 0, "Ins");
	mvwprintw(_hWin, 2, 0, "Level");
	mvwprintw(_hWin, 3, 0, "Pan");
	mvwprintw(_hWin, 4, 0, "Expr");
	mvwprintw(_hWin, 5, 0, "Reverb");
	mvwprintw(_hWin, 6, 0, "Chorus");
	mvwprintw(_hWin, 7, 0, "Delay");
	mvwprintw(_hWin, 8, 0, "K.Shift");
	mvwprintw(_hWin, 9, 0, "MIDI CH");
	
	if (doLiveVis)
	{
		if (MATRIX_COL_SIZE < 3)
		{
			for (UINT8 curChn = 0; curChn < 16; curChn ++)
			{
				char chnLetter = (curChn < 9) ? ('1' + curChn) : ('A' - 9 + curChn);
				mvwprintw(_hWin, 10, MATRIX_BASE_X + (curChn * MATRIX_COL_SIZE), "%c", chnLetter);
			}
		}
		else
		{
			for (UINT8 curChn = 0; curChn < 16; curChn ++)
				mvwprintw(_hWin, 10, MATRIX_BASE_X + (curChn * MATRIX_COL_SIZE), "%02u", 1 + curChn);
		}
	}
	
	return;
}

void LCDDisplay::DrawPage(const LCDPage& page)
{
	wattron(_hWin, A_BOLD);
	if (_pageMode == PAGEMODE_ALL)
	{
		mvwprintw(_hWin, 0, 8, "ALL");
		mvwprintw(_hWin, 9, 8, "%3u", page.chnID);
	}
	else //if (_pageMode == PAGEMODE_CHN)
	{
		mvwprintw(_hWin, 0, 8, "%c%02u", 'A' + (page.partID >> 4), 1 + (page.partID & 0x0F));
		mvwprintw(_hWin, 9, 8, "%c%02u", 'A' + (page.chnID >> 4), 1 + (page.chnID & 0x0F));
	}
	if (_ttMode == TTMODE_NONE)
	{
		wmove(_hWin, 0, MATRIX_BASE_X + (16 * MATRIX_COL_SIZE - strlen(page.title)) / 2);
		wprintw(_hWin, "%s", page.title);
		//wclrtoeol(_hWin);
	}
	
	if (page.insID == 0xFF)
	{
		mvwprintw(_hWin, 1, 8, "---");
	}
	else
	{
		mvwprintw(_hWin, 1, 8, "%03u", 1 + page.insID);
	}
	mvwprintw(_hWin, 2, 8, "%3u", page.vol);
	if (page.pan == -0x40)
		mvwprintw(_hWin, 3, 8, "Rnd");
	else if (page.pan < 0x00)
		mvwprintw(_hWin, 3, 8, "L%2u", -page.pan);
	else if (page.pan == 0x00)
		mvwprintw(_hWin, 3, 8, " %2u", page.pan);
	else if (page.pan > 0x00)
		mvwprintw(_hWin, 3, 8, "R%2u", page.pan);
	mvwprintw(_hWin, 4, 8, "%3u", page.expr);
	mvwprintw(_hWin, 5, 8, "%3u", page.reverb);
	mvwprintw(_hWin, 6, 8, "%3u", page.chorus);
	mvwprintw(_hWin, 7, 8, "%3u", page.delay);
	if (page.transp < 0)
		mvwprintw(_hWin, 8, 8, "-%2d", -page.transp);
	else if (page.transp > 0)
		mvwprintw(_hWin, 8, 8, "+%2d", page.transp);
	else
	{
		mvwaddch(_hWin, 8, 8, ACS_PLMINUS);
		mvwprintw(_hWin, 8, 9, "%2d", page.transp);
	}
	wattroff(_hWin, A_BOLD);
	
	return;
}

void LCDDisplay::DrawTitleText(void)
{
	wmove(_hWin, 0, MATRIX_BASE_X);	wclrtoeol(_hWin);
	
	wattron(_hWin, A_BOLD);
	switch(_ttMode)
	{
	case TTMODE_SHOW1:
		wmove(_hWin, 0, MATRIX_BASE_X + (16 * MATRIX_COL_SIZE - strlen(_tempText)) / 2);
		wprintw(_hWin, "%s", _tempText);
		break;
	case TTMODE_SHOW2:
		wmove(_hWin, 0, MATRIX_BASE_X + (16 * MATRIX_COL_SIZE - 16) / 2);
		wprintw(_hWin, "%.16s", &_tempText[0x00]);
		wmove(_hWin, 1, MATRIX_BASE_X + (16 * MATRIX_COL_SIZE - 16) / 2);
		wprintw(_hWin, "%.16s", &_tempText[0x10]);
		break;
	case TTMODE_SCROLL:
		wmove(_hWin, 0, MATRIX_BASE_X + (16 * MATRIX_COL_SIZE - 16) / 2);
		if (_longLineMode == LLM_SCROLL_ROLAND)
		{
			// Roland-style scrolling takes the "Page Title" and scrolls the text in and out.
			// Simplified, it scrolls through "Page Title<Temp Text>Page Title".
			const char* pageTitle = (_pageMode == PAGEMODE_ALL) ? _allPage.title : _chnPage.title;
			char fullStr[0x40];
			if (_ttScrollPos < 17)
			{
				sprintf(fullStr, "%.16s<%.16s", pageTitle, _tempText);
				wprintw(_hWin, "%.16s", &fullStr[_ttScrollPos]);
			}
			else
			{
				sprintf(fullStr, "%.32s>%.16s", _tempText, pageTitle);
				wprintw(_hWin, "%.16s", &fullStr[_ttScrollPos - 17]);
			}
		}
		else
		{
			wprintw(_hWin, "%.16s", &_tempText[_ttScrollPos]);
		}
		break;
	}
	wattroff(_hWin, A_BOLD);
	
	return;
}

void LCDDisplay::SetTemporaryText(const char* text, UINT8 ttMode)
{
	size_t textLen;
	
	strncpy(_tempText, text, 0x20);
	_tempText[0x20] = '\0';
	textLen = strlen(_tempText);
	
	_ttMode = ttMode;
	_ttTimeout = 3000;
	_ttScrollPos = 0;
	
	if (_ttMode == TTMODE_SCROLL)
	{
		if (textLen <= 16 || _longLineMode == LLM_NO_ANIM)
		{
			_ttMode = TTMODE_SHOW1;
		}
		else if (_longLineMode == LLM_SCROLL_SIMPLE)
		{
			_ttScrollEnd = textLen - 15;
			_ttTimeout = 1800;	// wait a bit before scrolling starts
		}
		else //if (_longLineMode == LLM_SCROLL_ROLAND)
		{
			_ttScrollEnd = textLen + 18;
			_ttTimeout = 300;
		}
	}
	else if (_ttMode == TTMODE_SHOW2)
	{
		if (textLen <= 16)
			_ttMode = TTMODE_SHOW1;
	}
	
	DrawTitleText();
	
	return;
}

void LCDDisplay::SetTemporaryDotMatrix(const std::bitset<0x100>& matrix)
{
	_tDotMatrix = matrix;
	_tdmTimeout = 2880;
	RedrawDotMatrix(_tDotMatrix);
	
	return;
}

void LCDDisplay::SetTemporaryBitmap(const std::bitset<0x100>& bitmap)
{
	_tBitmap = bitmap;
	_tbTimeout = 2500;
	RedrawBitmap(_tBitmap);
	
	return;
}

void LCDDisplay::RedrawDotMatrix(const std::bitset<0x100>& matrix)
{
	static const char* DRAW_WCHRS[0x04] = {" ", "\xE2\x96\x80", "\xE2\x96\x84", "\xE2\x96\x88"};
	
	for (UINT8 y = 0; y < 8; y ++)
	{
		for (UINT8 x = 0; x < 16; x++)
		{
			bool dotU = matrix[MAT_XY2IDX(x, y * 2 + 0)];
			bool dotL = matrix[MAT_XY2IDX(x, y * 2 + 1)];
			UINT8 pixMask = (dotU << 0) | (dotL << 1);
			
			wmove(_hWin, MATRIX_BASE_Y + y, MATRIX_BASE_X + x * MATRIX_COL_SIZE);
			if (MATRIX_COL_SIZE < 3)
				wprintw(_hWin, "%s", DRAW_WCHRS[pixMask]);
			else
				wprintw(_hWin, "%s%s", DRAW_WCHRS[pixMask], DRAW_WCHRS[pixMask]);
		}
	}
	
	return;
}

void LCDDisplay::RedrawBitmap(const std::bitset<0x100>& bitmap)
{
	static const char* DRAW_WCHRS[0x10] =
	{
		" ",            "\xE2\x96\x98", "\xE2\x96\x9D", "\xE2\x96\x80",	// none, upper left, upper right, ul+ur
		"\xE2\x96\x96", "\xE2\x96\x8C", "\xE2\x96\x9E", "\xE2\x96\x9B",	// lower left, ll+ul, ll+ur, ll+ul+ur
		"\xE2\x96\x97", "\xE2\x96\x9A", "\xE2\x96\x90", "\xE2\x96\x9C",	// lower right, lr+ul, lr+ur, lr+ul+ur
		"\xE2\x96\x84", "\xE2\x96\x99", "\xE2\x96\x9F", "\xE2\x96\x88",	// lower l+r, ll+lr+ul, ll+lr+ur, full
	};
	int baseX;
	int x, y;
	int barXpos;
	
	baseX = (16 * MATRIX_COL_SIZE - 8) / 2;
	for (y = 0; y < 8; y ++)
	{
		wmove(_hWin, MATRIX_BASE_Y + y, MATRIX_BASE_X + baseX);
		for (x = 0; x < 8; x ++)
		{
			bool dotUL = bitmap[MAT_XY2IDX(x * 2 + 0, y * 2 + 0)];
			bool dotUR = bitmap[MAT_XY2IDX(x * 2 + 1, y * 2 + 0)];
			bool dotLL = bitmap[MAT_XY2IDX(x * 2 + 0, y * 2 + 1)];
			bool dotLR = bitmap[MAT_XY2IDX(x * 2 + 1, y * 2 + 1)];
			UINT8 pixMask = (dotUL << 0) | (dotUR << 1) | (dotLL << 2) | (dotLR << 3);
			
			waddstr(_hWin, DRAW_WCHRS[pixMask]);
		}
	}
	
	// --- add a small margin so that channel visualization doesn't clash with the image ---
	// erase the channel bar on the left side of the image
	barXpos = baseX / MATRIX_COL_SIZE * MATRIX_COL_SIZE;
	for (y = 0; y < 8; y ++)
	{
		wmove(_hWin, MATRIX_BASE_Y + y, MATRIX_BASE_X + barXpos);
		for (x = barXpos; x < baseX; x ++)
			waddch(_hWin, ' ');
	}
	// erase the channel bar on the right side of the image
	barXpos = (baseX + 8 + MATRIX_COL_SIZE) / MATRIX_COL_SIZE * MATRIX_COL_SIZE;
	for (y = 0; y < 8; y ++)
	{
		wmove(_hWin, MATRIX_BASE_Y + y, MATRIX_BASE_X + baseX + 8);
		for (x = baseX + 8; x < barXpos; x ++)
			waddch(_hWin, ' ');
	}
	
	return;
}

/*static*/ void LCDDisplay::SCSysEx2DotMatrix(size_t syxLen, const UINT8* syxData, std::bitset<0x100>& matrix)
{
	size_t curLine;
	
	for (curLine = 0x00; curLine < 0x10; curLine ++)
	{
		matrix[(curLine << 4) | 0x00] = (syxData[0x00 | curLine] >> 4) & 0x01;
		matrix[(curLine << 4) | 0x01] = (syxData[0x00 | curLine] >> 3) & 0x01;
		matrix[(curLine << 4) | 0x02] = (syxData[0x00 | curLine] >> 2) & 0x01;
		matrix[(curLine << 4) | 0x03] = (syxData[0x00 | curLine] >> 1) & 0x01;
		matrix[(curLine << 4) | 0x04] = (syxData[0x00 | curLine] >> 0) & 0x01;
		matrix[(curLine << 4) | 0x05] = (syxData[0x10 | curLine] >> 4) & 0x01;
		matrix[(curLine << 4) | 0x06] = (syxData[0x10 | curLine] >> 3) & 0x01;
		matrix[(curLine << 4) | 0x07] = (syxData[0x10 | curLine] >> 2) & 0x01;
		matrix[(curLine << 4) | 0x08] = (syxData[0x10 | curLine] >> 1) & 0x01;
		matrix[(curLine << 4) | 0x09] = (syxData[0x10 | curLine] >> 0) & 0x01;
		matrix[(curLine << 4) | 0x0A] = (syxData[0x20 | curLine] >> 4) & 0x01;
		matrix[(curLine << 4) | 0x0B] = (syxData[0x20 | curLine] >> 3) & 0x01;
		matrix[(curLine << 4) | 0x0C] = (syxData[0x20 | curLine] >> 2) & 0x01;
		matrix[(curLine << 4) | 0x0D] = (syxData[0x20 | curLine] >> 1) & 0x01;
		matrix[(curLine << 4) | 0x0E] = (syxData[0x20 | curLine] >> 0) & 0x01;
		matrix[(curLine << 4) | 0x0F] = (syxData[0x30 | curLine] >> 4) & 0x01;
	}
	
	return;
}

/*static*/ void LCDDisplay::MUSysEx2Bitmap(size_t syxLen, const UINT8* syxData, std::bitset<0x100>& matrix)
{
	size_t curLine;
	
	for (curLine = 0x00; curLine < 0x10; curLine ++)
	{
		matrix[(curLine << 4) | 0x00] = (syxData[0x00 | curLine] >> 6) & 0x01;
		matrix[(curLine << 4) | 0x01] = (syxData[0x00 | curLine] >> 5) & 0x01;
		matrix[(curLine << 4) | 0x02] = (syxData[0x00 | curLine] >> 4) & 0x01;
		matrix[(curLine << 4) | 0x03] = (syxData[0x00 | curLine] >> 3) & 0x01;
		matrix[(curLine << 4) | 0x04] = (syxData[0x00 | curLine] >> 2) & 0x01;
		matrix[(curLine << 4) | 0x05] = (syxData[0x00 | curLine] >> 1) & 0x01;
		matrix[(curLine << 4) | 0x06] = (syxData[0x00 | curLine] >> 0) & 0x01;
		matrix[(curLine << 4) | 0x07] = (syxData[0x10 | curLine] >> 6) & 0x01;
		matrix[(curLine << 4) | 0x08] = (syxData[0x10 | curLine] >> 5) & 0x01;
		matrix[(curLine << 4) | 0x09] = (syxData[0x10 | curLine] >> 4) & 0x01;
		matrix[(curLine << 4) | 0x0A] = (syxData[0x10 | curLine] >> 3) & 0x01;
		matrix[(curLine << 4) | 0x0B] = (syxData[0x10 | curLine] >> 2) & 0x01;
		matrix[(curLine << 4) | 0x0C] = (syxData[0x10 | curLine] >> 1) & 0x01;
		matrix[(curLine << 4) | 0x0D] = (syxData[0x10 | curLine] >> 0) & 0x01;
		matrix[(curLine << 4) | 0x0E] = (syxData[0x20 | curLine] >> 6) & 0x01;
		matrix[(curLine << 4) | 0x0F] = (syxData[0x20 | curLine] >> 5) & 0x01;
	}
	
	return;
}
