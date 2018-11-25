#ifndef __VIS_SC_LCD_HPP__
#define __VIS_SC_LCD_HPP__

#include <stdtype.h>
#include <bitset>

class MidiPlayer;
class NoteVisualization;

class LCDDisplay
{
public:
	enum	// Long Line display Mode (text length > 16)
	{
		LLM_NO_ANIM = 0,		// just show it without any animation
		LLM_SCROLL_SIMPLE = 1,	// 16-char display: simple scroll
		LLM_SCROLL_ROLAND = 2,	// 16-char display: scroll Roland SC-55/88/88Pro style
	};
	enum	// Bar Visualization Layout
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
	MidiPlayer* _mPlr;
	NoteVisualization* _nVis;
	LCDPage _allPage;
	LCDPage _chnPage;
	UINT8 _longLineMode;
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
	
	// Define an area for a bitmap overlay that is not redrawn by the channel visualization.
	UINT8 _noDrawXStart;
	UINT8 _noDrawXEnd;	// Note: range is [start ... end-1]
	
	LCDDisplay();
	~LCDDisplay();
	void Init(int winPosX, int winPosY);
	void Deinit(void);
	void SetMidiPlayer(MidiPlayer* mPlr);
	void SetNoteVis(NoteVisualization* nVis);
	void GetSize(int* sizeX, int* sizeY) const;
	WINDOW* GetWindow(void);
	void ResetDisplay(void);
	void AdvanceTime(UINT32 time);
	bool AdvanceScrollingText(void);
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
	void PrepareBitmapDisplay(void);
	static void SCSysEx2DotMatrix(size_t syxLen, const UINT8* syxData, std::bitset<0x100>& matrix);
	static void MUSysEx2Bitmap(size_t syxLen, const UINT8* syxData, std::bitset<0x100>& matrix);
};

#endif	// __VIS_SC_LCD_HPP__
