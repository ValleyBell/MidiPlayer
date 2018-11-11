#ifndef __VIS_SC_LCD_HPP__
#define __VIS_SC_LCD_HPP__

#include <stdtype.h>
#include <bitset>

class NoteVisualization;

class LCDDisplay
{
public:
	enum	// bar visualization layout
	{
		BVL_SINGLE = 0,	// 1x 16 channels
		BVL_DOUBLE = 1,	// 2x 16 channels
	};
	enum
	{
		PAGEMODE_ALL = 0,
		PAGEMODE_CHN = 1,
	};
	enum
	{
		TTMODE_NONE = 0,
		TTMODE_SHOW1 = 1,	// show on 1 line
		TTMODE_SHOW2 = 2,	// show on 2 lines
		TTMODE_SCROLL = 3,	// scroll on 1 line
	};
	struct LCDPage
	{
		const char* title;
		UINT8 partID;
		UINT8 chnID;
		UINT8 insID;
		UINT8 vol;
		INT8 pan;
		UINT8 expr;
		UINT8 reverb;
		UINT8 chorus;
		UINT8 delay;
		INT8 transp;
	};
	WINDOW* _hWin;
	NoteVisualization* _nVis;
	LCDPage _allPage;
	LCDPage _chnPage;
	UINT8 _barVisLayout;
	UINT8 _pageMode;
	std::string _modName;
	char _tempText[0x21];	// 32 characters + \0
	UINT8 _ttMode;
	UINT8 _ttScrollPos;
	UINT8 _ttScrollEnd;
	INT32 _ttTimeout;	// in ms
	std::bitset<0x100> _tDotMatrix;
	INT32 _tdmTimeout;	// in ms
	std::bitset<0x100> _dotMatrix;
	INT32 _tbTimeout;	// in ms
	std::bitset<0x100> _tBitmap;

	LCDDisplay();
	~LCDDisplay();
	void Init(int winPosX, int winPosY);
	void Deinit(void);
	void SetNoteVis(NoteVisualization* nVis);
	void GetSize(int* sizeX, int* sizeY);
	WINDOW* GetWindow(void);
	void ResetDisplay(void);
	void AdvanceTime(UINT32 time);
	void RefreshDisplay(void);
	void FullRedraw(void);
	void DrawLayout(void);
	void DrawPage(const LCDPage& page);
	void DrawTitleText(void);
	void SetTemporaryText(const char* text, UINT8 ttMode);
	void SetTemporaryDotMatrix(const std::bitset<0x100>& matrix);
	void SetTemporaryBitmap(const std::bitset<0x100>& bitmap);
	void RedrawDotMatrix(const std::bitset<0x100>& matrix);
	void RedrawBitmap(const std::bitset<0x100>& bitmap);
	static void SCSysEx2DotMatrix(size_t syxLen, const UINT8* syxData, std::bitset<0x100>& matrix);
	static void MUSysEx2Bitmap(size_t syxLen, const UINT8* syxData, std::bitset<0x100>& matrix);
};

#endif	// __VIS_SC_LCD_HPP__
