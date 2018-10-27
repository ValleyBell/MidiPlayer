#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>
#include <bitset>
#include <curses.h>
#include <stdarg.h>

#include <stdtype.h>
#include "NoteVis.hpp"
#include "vis_sc-lcd.hpp"


/*
	Part    A01	Instrument  <-- instrument name [16 chars]  -->
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
#define MATRIX_BASE_Y	1
#define MAT_XY2IDX(x, y)	((y) * 0x10 + (x))

LCDDisplay::LCDDisplay() :
	_hWin(NULL),
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
	_pageMode = PAGEMODE_ALL;
	_ttMode = TTMODE_NONE;
	
	ResetDisplay();
	
	_hWin = newwin(MATRIX_BASE_Y + 9, MATRIX_BASE_X + 16 * 3, winPosY, winPosX);
	
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

void LCDDisplay::SetNoteVis(NoteVisualization* nVis)
{
	_nVis = nVis;
	return;
}

void LCDDisplay::GetSize(int* sizeX, int* sizeY)
{
	if (sizeX != NULL)
		*sizeX = 16 + 16*3;
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
	_allPage.insID = 0x01;
	_allPage.insName = _modName.c_str();
	_allPage.vol = 127;
	_allPage.pan = 0x00;
	_allPage.expr = 127;
	_allPage.reverb = 40;
	_allPage.chorus = 0;
	_allPage.delay = 0;
	_allPage.transp = 0;
	_chnPage = _allPage;
	_chnPage.vol = 100;
	_chnPage.insName = "- Channel Vis. -";
	
	_ttTimeout = 0;
	_tdmTimeout = 0;
	
	return;
}

void LCDDisplay::AdvanceTime(UINT32 time)
{
	if (_ttTimeout > 0)
	{
		_ttTimeout -= time;
		if (_ttMode == TTMODE_SCROLL && _ttTimeout <= 0)
		{
			while(_ttTimeout <= 0)
			{
				_ttScrollPos ++;
				_ttTimeout += 480;
			}
			// TODO: no scrolling here yet
			_ttTimeout = 0;
		}
		if (_ttTimeout <= 0)
		{
			_ttTimeout = 0;
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
	
	return;
}

static void DrawDotBar(std::bitset<0x100>& matrix, int barID, int barHeight)
{
	int posY;
	
	for (posY = 0x00; posY < 0x10; posY ++)
		matrix[MAT_XY2IDX(barID, 0x0F - posY)] = (posY <= barHeight);
	
	return;
}

void LCDDisplay::RefreshDisplay(void)
{
	size_t curChn;
	const NoteVisualization::MidiModifiers& modAttr = _nVis->GetAttributes();
	const NoteVisualization::ChnInfo* chnInfo;
	const NoteVisualization::MidiModifiers* chnAttr;
	
	_allPage.vol = modAttr.volume;
	_allPage.pan = modAttr.pan;
	_allPage.expr = modAttr.expression;
	//_allPage.reverb = 0;
	//_allPage.chorus = 0;
	//_allPage.delay = 0;
	//_allPage.transp = 0;
	
	chnAttr = &_nVis->GetChannel(_chnPage.chnID)->_attr;
	//_chnPage.insID = 0x01;
	//_chnPage.insName = NULL;
	_chnPage.vol =  chnAttr->volume;
	_chnPage.pan =  chnAttr->pan;
	_chnPage.expr = chnAttr->expression;
	//_chnPage.reverb = 0;
	//_chnPage.chorus = 0;
	//_chnPage.delay = 0;
	_chnPage.transp = chnAttr->detune[1] >> 8;
	
	for (curChn = 0; curChn < 16; curChn ++)
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
				ageAttenuate = 1.0f - nlIt->curAge / 1000.0f;
			if (ageAttenuate < 0.0f)
				ageAttenuate = 0.0f;
			noteVol *= ageAttenuate;
			if (barHeight < noteVol)
				barHeight = noteVol;
		}
		barYHeight = static_cast<int>(barHeight * 15 + 0.5);
		DrawDotBar(_dotMatrix, curChn, barYHeight);
	}
	
	if (_pageMode == PAGEMODE_ALL)
		DrawPage(_allPage);
	else if (_pageMode == PAGEMODE_CHN)
		DrawPage(_chnPage);
	if (! _tdmTimeout)
		RedrawDotMatrix(_dotMatrix);
	
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
	if (_tdmTimeout > 0)
		RedrawDotMatrix(_tDotMatrix);
	else
		RedrawDotMatrix(_dotMatrix);
	
	return;
}

void LCDDisplay::DrawLayout(void)
{
	mvwprintw(_hWin, 0, 0, "Part");
	mvwprintw(_hWin, 0, 16, "Instrument");
	mvwprintw(_hWin, 1, 0, "Level");
	mvwprintw(_hWin, 2, 0, "Pan");
	mvwprintw(_hWin, 3, 0, "Expr");
	mvwprintw(_hWin, 4, 0, "Reverb");
	mvwprintw(_hWin, 5, 0, "Chorus");
	mvwprintw(_hWin, 6, 0, "Delay");
	mvwprintw(_hWin, 7, 0, "K.Shift");
	mvwprintw(_hWin, 8, 0, "MIDI CH");
	
	for (UINT8 curChn = 0; curChn < 16; curChn ++)
		mvwprintw(_hWin, 9, 16 + (curChn * 3), "%02u", 1 + curChn);
	
	return;
}

void LCDDisplay::DrawPage(const LCDPage& page)
{
	wattron(_hWin, A_BOLD);
	if (_pageMode == PAGEMODE_ALL)
	{
		mvwprintw(_hWin, 0, 8, "ALL");
		mvwprintw(_hWin, 8, 8, "%3u", page.chnID);
	}
	else //if (_pageMode == PAGEMODE_CHN)
	{
		mvwprintw(_hWin, 0, 8, "%c%02u", 'A' + (page.partID >> 4), 1 + (page.partID & 0x0F));
		mvwprintw(_hWin, 8, 8, "%c%02u", 'A' + (page.chnID >> 4), 1 + (page.chnID & 0x0F));
	}
	if (_ttMode == TTMODE_NONE)
	{
		mvwprintw(_hWin, 0, 32, "%s", page.insName);
		wclrtoeol(_hWin);
	}
	
	mvwprintw(_hWin, 1, 8, "%3u", page.vol);
	if (page.pan == -0x40 && false)
		mvwprintw(_hWin, 2, 8, "Rnd");
	else if (page.pan < 0x00)
		mvwprintw(_hWin, 2, 8, "L%2u", -page.pan);
	else if (page.pan == 0x00)
		mvwprintw(_hWin, 2, 8, " %2u", page.pan);
	else if (page.pan > 0x00)
		mvwprintw(_hWin, 2, 8, "R%2u", page.pan);
	mvwprintw(_hWin, 3, 8, "%3u", page.expr);
	mvwprintw(_hWin, 4, 8, "%3u", page.reverb);
	mvwprintw(_hWin, 5, 8, "%3u", page.chorus);
	mvwprintw(_hWin, 6, 8, "%3u", page.delay);
	if (page.transp < 0)
		mvwprintw(_hWin, 7, 8, "-%2d", -page.transp);
	else if (page.transp > 0)
		mvwprintw(_hWin, 7, 8, "+%2d", page.transp);
	else
	{
		mvwaddch(_hWin, 7, 8, ACS_PLMINUS);
		mvwprintw(_hWin, 7, 9, "%2d", page.transp);
	}
	wattroff(_hWin, A_BOLD);
	
	return;
}

void LCDDisplay::SetTemporaryText(const char* text)
{
	strncpy(_tempText, text, 0x20);
	_tempText[0x20] = '\0';
	_ttMode = (strlen(_tempText) > 16) ? TTMODE_SCROLL : TTMODE_SHOW;
	_ttScrollPos = 0;
	_ttTimeout = 2880;
	
	wattron(_hWin, A_BOLD);
	mvwprintw(_hWin, 0, 32, "%s", _tempText);
	if (getcury(_hWin) == 0)
		wclrtoeol(_hWin);
	wattroff(_hWin, A_BOLD);
	
	return;
}

void LCDDisplay::SetTemporaryDotMatrix(const std::bitset<0x100>& matrix)
{
	_tDotMatrix = matrix;
	_tdmTimeout = 2880;
	RedrawDotMatrix(_tDotMatrix);
	
	return;
}

void LCDDisplay::RedrawDotMatrix(const std::bitset<0x100>& matrix)
{
	//static const chtype DRAW_CHRS[0x04] = {' ', ACS_S9, ACS_S3, ACS_BLOCK};
	static const char* DRAW_WCHRS[0x04] = {" ", "\xE2\x96\x84", "\xE2\x96\x80", "\xE2\x96\x88"};
	
	for (UINT8 y = 0; y < 8; y ++)
	{
		for (UINT8 x = 0; x < 16; x++)
		{
			bool dotH = matrix[MAT_XY2IDX(x, y * 2 + 1)];
			bool dotL = matrix[MAT_XY2IDX(x, y * 2 + 0)];
			UINT8 pixMask = (dotH << 0) | (dotL << 1);
			//mvwhline(_hWin, MATRIX_BASE_Y + y, MATRIX_BASE_X + x * 3, DRAW_CHRS[pixMask], 2);
			mvwprintw(_hWin, MATRIX_BASE_Y + y, MATRIX_BASE_X + x * 3, "%s%s", DRAW_WCHRS[pixMask], DRAW_WCHRS[pixMask]);
		}
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

