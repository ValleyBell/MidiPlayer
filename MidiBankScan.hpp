#ifndef __MIDIBANKSCAP_HPP__
#define __MIDIBANKSCAP_HPP__

#include <stdtype.h>
#include "MidiInsReader.h"
#include "MidiLib.hpp"


// feature mask bits
#define FMBALL_INSSET		0	// lowest bit that is used to mark instrument mask
#define FMBALL_BAD_INS		7	// found instruments unknown/invalid for the device type
#define FMBALL_USER_INS		8	// found user instrument
#define FMBALL_USER_DRM		9	// found user drum kit
#define FMBALL_TEXT_DISP	12	// writes ASCII text to the display
#define FMBALL_PIXEL_ART	13	// shows pixel art

#define FMBGM_L1_RESET		16	// [GM] GM Level 1 Reset
#define FMBGM_L2_RESET		17	// [GM] GM Level 2 Reset

#define FMBGS_GS_RESET		16	// [GS] GS Reset
#define FMBGS_SC_RESET		17	// [GS] SC-88 Sytem Mode Set
#define FMBGS_DEF_MAP		18	// [GS] uses default instrument map (Bank LSB 0)
#define FMBGS_SC_MAP		19	// [GS] uses Sound Canvas specific instrument map (Bank LSB 1+)

#define FMBXG_XG_RESET		16	// [XG] XG Reset
#define FMBXG_ALL_RESET		17	// [XG] XG All Parameters Reset
#define FMBXG_GM_MAP		18	// [XG] uses GM instrument map (Bank LSB 0)
#define FMBXG_MU100_MAP		19	// [XG] uses MU100 native instrument map (Bank LSB 126)
#define FMBXG_BASIC_MAP		20	// [XG] uses MU Basic instrument map (Bank LSB 127)
#define FMBXG_NEEDS_CTF		21	// [XG] found an instrument that uses Capital Tone Fallback
#define FMBXG_PANEL			24	// [XG] uses Yamaha keyboard panel voices
#define FMBXG_PLG_VL		25	// [XG] uses PLG100-VL voices
#define FMBXG_PLG_DX		26	// [XG] uses PLG100-DX voices

#define FMBOTH_MT_RESET		16	// MT-32 Reset

#define SPCFEAT_KARAOKE		0	// Soft Karaoke (.kar) file

typedef struct
{
	// feature masks
	UINT32 fmGM;
	UINT32 fmGS;
	UINT32 fmXG;
	UINT32 fmOther;
	// additional data
	UINT8 MaxDrumKit;
	UINT8 MaxDrumMSB;
	UINT8 gsimAllMap;	// GS module: instrument mask with ignored instrument map (Bank LSB)
	UINT8 gsimNot;		// GS module: instrument mask for "not allowed" modules
	UINT8 gsMaxLSB;		// GS module: maximum Bank LSB (instrument map)
	UINT8 xgMapSel;		// XG module: MU basic/MU 100 voice map selection (0xFF = unset)
} MODULE_CHECK;
typedef struct
{
	UINT8 modType;		// module type (GM/GS/XG)
	UINT8 numPorts;		// number of MIDI ports required for proper playback
	UINT8 hasReset;
	UINT8 spcFeature;	// special features (e.g. karaoke)
	UINT8 GS_Min;		// GS module ID: minimum compatibility (using non-GM fallback instruments)
	UINT8 GS_Opt;		// GS module ID: optimal playback
	UINT8 XG_Opt;		// XG module ID: optimal playback
	
	MODULE_CHECK details;
} BANKSCAN_RESULT;

void SetBankScanInstruments(UINT8 moduleID, const INS_BANK* insBank);
void MidiBankScan(MidiFile* cMidi, bool ignoreEmptyChns, BANKSCAN_RESULT* result);


#endif	// __MIDIBANKSCAP_HPP__
