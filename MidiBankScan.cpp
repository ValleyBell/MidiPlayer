#include <set>
#include <string.h>	// for memset()

// for outputting all texts
#include <string>
#include <list>

#if CHARSET_DETECTION
#include <uchardet.h>
#endif

#include <stdtype.h>
#include "MidiLib.hpp"
#include "MidiInsReader.h"
#include "MidiBankScan.hpp"


struct SCAN_VARS
{
	std::set<UINT8> portIDs;
	UINT16 drumChnMask;	// 16 bits, set = drum, clear = melody channel
	UINT16 chnUseMask;	// temporary hack, required for MIDIs that set up instruments in a separate track after all notes
	UINT8 insBankBuf[16][2];	// [0] Bank MSB, [1] Bank LSB
	UINT8 insBank[16][3];	// [0] Bank MSB, [1] Bank LSB, [2] instrument
	UINT8 lastPortID;
	UINT8 curPortID;
	UINT8 syxReset;	// keeps track of the most recent Reset message type
	bool insChkOnNote;
};

// possible bonus: detect GM MIDIs with XG drums (i.e. not Bank MSB, except for MSB=127 on drum channel)
//	-> XG (compatible with GM)

// Function Prototypes
static UINT8 GetInsModuleID(const INS_BANK* insBank, UINT8 ins, UINT8 msb, UINT8 lsb);
static UINT8 GetGSInsModuleMask(const INS_BANK* insBank, UINT8 ins, UINT8 msb);
static void DoInsCheck_XG(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb);
static void DoInsCheck_GS(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb);
static void DoInstrumentCheck(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb);
static void MayDoInsCheck(MODULE_CHECK* modChk, SCAN_VARS* sv, UINT8 evtChn, bool isNote);
static void HandleSysEx_MT32(UINT32 syxLen, const UINT8* syxData, MODULE_CHECK* modChk, SCAN_VARS* sv);
static void HandleSysEx_GS(UINT32 syxLen, const UINT8* syxData, MODULE_CHECK* modChk, SCAN_VARS* sv);
static void HandleSysEx_XG(UINT32 syxLen, const UINT8* syxData, MODULE_CHECK* modChk, SCAN_VARS* sv);
static UINT8 GetMSBit(UINT32 value);
static UINT8 InsMask2ModuleID(UINT32 featureMask, UINT8 notInsMask);


static const UINT8 PART_ORDER[0x10] =
{	0x9, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};


#define SYX_RESET_UNDEF	0xFF


static const INS_BANK* insBankGM2 = NULL;
static const INS_BANK* insBankGS = NULL;
static const INS_BANK* insBankXG = NULL;
static std::string tmpCharsetStr;

void SetBankScanInstruments(UINT8 moduleID, const INS_BANK* insBank)
{
	switch(moduleID)
	{
	case MODULE_GM_2:
		insBankGM2 = insBank;
		break;
	case MODULE_TYPE_GS:
		insBankGS = insBank;
		break;
	case MODULE_TYPE_XG:
		insBankXG = insBank;
		break;
	}
	
	return;
}

static UINT8 GetInsModuleID(const INS_BANK* insBank, UINT8 ins, UINT8 msb, UINT8 lsb)
{
	const INS_PRG_LST* insPrg;
	const INS_DATA* insData;
	UINT32 curIns;
	
	if (insBank == NULL)
		return 0xFF;
	
	insPrg = &insBank->prg[ins];
	for (curIns = 0; curIns < insPrg->count; curIns ++)
	{
		insData = &insPrg->instruments[curIns];
		if (msb == 0xFF || insData->bankMSB == msb)
		{
			if (lsb == 0xFF || insData->bankLSB == lsb)
			{
				return insData->moduleID;
			}
		}
	}
	
	return 0xFF;
}

static UINT8 GetGSInsModuleMask(const INS_BANK* insBank, UINT8 ins, UINT8 msb)
{
	const INS_PRG_LST* insPrg;
	const INS_DATA* insData;
	UINT32 curIns;
	UINT8 insMask;
	UINT8 maxLsbMask;
	
	if (insBank == NULL)
		return 0x00;
	
	insPrg = &insBank->prg[ins];
	insMask = 0x00;
	for (curIns = 0; curIns < insPrg->count; curIns ++)
	{
		insData = &insPrg->instruments[curIns];
		if (msb == 0xFF || insData->bankMSB == msb)
			insMask |= ((1 << insData->bankLSB) - 1);
	}
	// copy highest used bit to all "unused" bits
	// Note: BankLSB is 1-based (LSB 1 requires us to check bit 0)
	maxLsbMask = 1 << insBank->maxBankLSB;
	if (insMask & (maxLsbMask >> 1))	// check for bit (lsb-1) being set
		insMask |= ~(maxLsbMask - 1);	// set bits (lsb) and higher
	
	return insMask;
}

static void DoInsCheck_XG(MODULE_CHECK* modChk, UINT8 ins, UINT8 ccMsb, UINT8 lsb)
{
	UINT8 xgIns;
	UINT8 msb;
	UINT8 msbNibH;
	UINT8 msbNibL;
	UINT8 vmSel;	// "voice map selection" value
	
	if (ccMsb >= 0x80)	// TODO: maybe do for LSB 7F as well? [needs HW checking]
	{
		xgIns = ins;	// use "default" map for unset MSB (drums on channel 10, melody on 1-9, 11-16)
		msb = (ins & 0x80) ? 0x7F : 0x00;
	}
	else
	{
		xgIns = ins & 0x7F;	// use master map (melody/drum selection via directly MSB)
		msb = ccMsb;
	}
	msbNibH = msb & 0xF0;
	msbNibL = msb & 0x0F;
	
	if (msb == 0x00)
		vmSel = lsb;	// bank 0: GM/MU50/MU100 bank selection based on Bank LSB
	else if (msb == 0x7F)
		vmSel = xgIns & 0x7F;	// drum bank 127: Standard Kit 1 has GM/MU50/MU100 variations
	else
		vmSel = 0xFF;	// voice map selection is not relevant for other banks
	if (vmSel == 0x00)
	{
		// LSB 0 is the GM instrument map- (MU50/MU100 voice map selection depends on device setting.)
		modChk->fmXG |= (1 << FMBXG_GM_MAP);
	}
	else if (vmSel == 0x7E)
	{
		// LSB 126 is like the GM bank, except that it enforces the MU100 voice map.
		// Note: Some MU100 voices match MU50 ones and thus might be missing from the instrument list.
		modChk->fmXG |= (1 << FMBXG_MU100_MAP);
		modChk->fmXG |= 1 << (FMBALL_INSSET + MTXG_MU100);
	}
	else if (vmSel == 0x7F)
	{
		// LSB 127 is like the GM bank with MU50 ("MU basic") voices enforced.
		modChk->fmXG |= (1 << FMBXG_BASIC_MAP);
	}
	else if (msb == 0x00 && lsb >= 0x70)
	{
		// Yamaha keyboard panel voices
		modChk->fmXG |= (1 << FMBXG_PANEL);
	}
	else if (msb == 0x3F)
	{
		// QS300 user voices and custom voices for various models
		modChk->fmXG |= (1 << FMBALL_USER_INS);
	}
	else if (msbNibH >= 0x20 && msbNibH <= 0x60 && msbNibL >= 0x01 && msbNibL <= 0x03)
	{
		// known banks:
		// PLG150-DR:
		//	MSB 79: DR preset voice
		//	MSB 47: DR user voice
		// PLG150-PF:
		//	MSB 80: PF-XG/A
		//	MSB 96: PF-XG/B
		// PLG150-AP:
		//	MSB 80: AP-XG/A
		//	MSB 96: AP-XG/B
		//	MSB 32: preset
		// PLG100-VL:
		//	MSB 81: VL-XG/A
		//	MSB 97: VL-XG/B
		//	MSB 33: preset (LSB 0..1), custom (LSB 2), internal (LSB 3)
		// PLG100-SG:
		//	MSB 82: SG non-proxy
		//	MSB 98: SG proxy
		// PLG100-DX:
		//	MSB 83: DX-XG/A
		//	MSB 99: DX-XG/B
		//	MSB 67: SFX voices
		//	MSB 35: custom voices
		// PLG150-AN
		//	MSB 84: AN-XG/A
		//	MSB 100: AN-XG/B
		//	MSB 36: preset (LSB 0..1), user (LSB 2)
		if ((msb & 0x0F) == 0x01)
		{
			modChk->fmXG |= (1 << FMBXG_PLG_VL);
			if (msb == 0x21 && lsb == 0x02)
				modChk->fmXG |= (1 << FMBALL_USER_INS);
		}
		else if ((msb & 0x0F) == 0x03)
		{
			modChk->fmXG |= (1 << FMBXG_PLG_DX);
			if (msb == 0x23)
				modChk->fmXG |= (1 << FMBALL_USER_INS);
		}
		modChk->fmXG |= 1 << (FMBALL_INSSET + MTXG_MU100);	// PLG100 boards are MU100+
	}
	else
	{
		UINT8 insModule;
		
		insModule = GetInsModuleID(insBankXG, xgIns, msb, lsb);
		if (insModule < 0x80)
		{
			modChk->fmXG |= 1 << (FMBALL_INSSET + insModule);
		}
		else
		{
			insModule = GetInsModuleID(insBankXG, xgIns, msb, 0x00);	// do the usual XG fallback
			if (insModule < 0x80)
				modChk->fmXG |= (1 << FMBXG_NEEDS_CTF);
			else
				modChk->fmXG |= (1 << FMBALL_BAD_INS);
		}
	}
	
	return;
}

static void DoInsCheck_GS(MODULE_CHECK* modChk, UINT8 ins, UINT8 ccMsb, UINT8 lsb)
{
	INT16 insModule;
	UINT8 msb;
	bool isUserIns;
	
	if (ins & 0x80)
	{
		msb = 0x00;	// Bank MSB is ignored for drum kits
		isUserIns = (ins == (0x80|0x40) || ins == (0x80|0x41));	// user drum (patch 64/65)
	}
	else
	{
		msb = (ccMsb == 0xFF) ? 0x00 : ccMsb;
		isUserIns = (msb == 0x40 || msb == 0x41);	// user instrument (bank 64/65)
	}
	
	if (isUserIns && lsb != 0x01)
	{
		// user patches/drums are supported by SC-88 and later
		// (Note: We explicitly excluded Bank LSB 1 above, because that's the SC-55 map.)
		// user patch: bank MSB == 0x40/0x41
		// user drum: instrument == 0x40/0x41
		if (lsb >= 0x02 && lsb <= 0x04)
			insModule = MTGS_SC55 + (lsb - 0x01);
		else
			insModule = MTGS_SC88;
		if (ins & 0x80)
			modChk->fmGS |= (1 << FMBALL_USER_DRM);
		else
			modChk->fmGS |= (1 << FMBALL_USER_INS);
		modChk->fmGS |= 1 << (FMBALL_INSSET + insModule);
		modChk->fmGS |= (lsb == 0x00) ? (1 << FMBGS_DEF_MAP) : (1 << FMBGS_SC_MAP);
		modChk->gsimAllMap |= 1 << (FMBALL_INSSET + insModule);
		if (modChk->gsMaxLSB < lsb)
			modChk->gsMaxLSB = lsb;
	}
	else if (lsb == 0x00)
	{
		// default/native instrument map
		UINT8 insMask;
		
		// search on all instrument maps to guess the right one
		insModule = GetInsModuleID(insBankGS, ins, msb, 0xFF);
		if (insModule < 0x80)
		{
			modChk->fmGS |= 1 << (FMBALL_INSSET + insModule);
			modChk->fmGS |= (1 << FMBGS_DEF_MAP);
			modChk->gsimAllMap |= 1 << (FMBALL_INSSET + insModule);
		}
		else
		{
			modChk->fmGS |= (1 << FMBALL_BAD_INS);
			modChk->gsimAllMap |= (1 << FMBALL_BAD_INS);
		}
		
		// get mask of modules that CAN use this instrument
		insMask = GetGSInsModuleMask(insBankGS, ins, msb);
		if (insMask)
			modChk->gsimNot |= ~insMask;	// take note of the modules that can NOT use it
	}
	else
	{
		// explicit instrument map
		
		// test for the defined map
		insModule = GetInsModuleID(insBankGS, ins, msb, lsb);
		if (insModule < 0x80)
		{
			modChk->fmGS |= 1 << (FMBALL_INSSET + insModule);
			modChk->fmGS |= (1 << FMBGS_SC_MAP);
			if (modChk->gsMaxLSB < lsb)
				modChk->gsMaxLSB = lsb;
		}
		else
		{
			modChk->fmGS |= (1 << FMBALL_BAD_INS);
		}
		
		// test for the minimal map (e.g. Bank LSB 0 is present on all maps)
		insModule = GetInsModuleID(insBankGS, ins, msb, 0xFF);
		if (insModule < 0x80)
			modChk->gsimAllMap |= 1 << (FMBALL_INSSET + insModule);
		else
			modChk->gsimAllMap |= (1 << FMBALL_BAD_INS);
	}
	
	return;
}

static void DoInstrumentCheck(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb)
{
	// MSB 0xFF == unset
	if ((msb == 0x00 || msb == 0xFF) && (lsb == 0x00 || lsb == 0xFF))
		modChk->fmGM |= (1 << (FMBALL_INSSET + MTGM_LVL1));
	else if (msb == 0x78 || msb == 0x79)
		modChk->fmGM |= (1 << (FMBALL_INSSET + MTGM_LVL2));
	else
		modChk->fmGM |= (1 << FMBALL_BAD_INS);
	
	DoInsCheck_GS(modChk, ins, msb, lsb);
	DoInsCheck_XG(modChk, ins, msb, lsb);
	
	if (ins & 0x80)
	{
		// for drum kits, take note of the maximum drum kit ID (for GM detection)
		if ((ins & 0x7F) > modChk->MaxDrumKit)
			modChk->MaxDrumKit = ins & 0x7F;
		
		// keep track of the highest Bank MSB on drum channels (should stay at 0 in "true" GM MIDIs)
		if (msb != 0xFF && modChk->MaxDrumMSB < msb)
			modChk->MaxDrumMSB = msb;
	}
	
	return;
}

static void MayDoInsCheck(MODULE_CHECK* modChk, SCAN_VARS* sv, UINT8 evtChn, bool isNote)
{
	UINT8* insData = sv->insBank[evtChn];
	
	if (sv->insChkOnNote)
	{
		// temporary hack required for MIDIs that set instruments in a track that occours after the note data
		if (! isNote && (sv->chnUseMask & (1 << evtChn)))
		{
			isNote = true;
			insData[2] |= 0x80;
			sv->chnUseMask &= ~(1 << evtChn);
		}
	}
	if (! isNote && sv->insChkOnNote)
	{
		insData[2] |= 0x80;	// next note will execute instrument check
		return;
	}
	if (isNote && ! (insData[2] & 0x80))
		return;	// we already checked the instrument for this note
	insData[2] &= ~0x80;	// remove "instrument check" flag
	
	if (sv->drumChnMask & (1 << evtChn))
		DoInstrumentCheck(modChk, 0x80 | insData[2], insData[0], insData[1]);
	else
		DoInstrumentCheck(modChk, insData[2], insData[0], insData[1]);
	
	return;
}

static void HandleSysEx_MT32(UINT32 syxLen, const UINT8* syxData, MODULE_CHECK* modChk, SCAN_VARS* sv)
{
	UINT32 addr;
	
	addr =	(syxData[0x04] << 16) |
			(syxData[0x05] <<  8) |
			(syxData[0x06] <<  0);
	switch(addr & 0xFF0000)
	{
	case 0x200000:	// Display
		if (addr < 0x200100)
			modChk->fmOther |= (1 << FMBALL_TEXT_DISP);	// MT-32: Display ASCII characters
		break;
	case 0x7F0000:	// All Parameters Reset
		sv->syxReset = MODULE_MT32;
		modChk->fmOther |= (1 << FMBOTH_MT_RESET);
		break;
	}
	
	return;
}

static void HandleSysEx_GS(UINT32 syxLen, const UINT8* syxData, MODULE_CHECK* modChk, SCAN_VARS* sv)
{
	UINT32 addr;
	UINT8 evtChn;
	UINT8 tempByt;
	
	addr =	(syxData[0x04] << 16) |
			(syxData[0x05] <<  8) |
			(syxData[0x06] <<  0);
	switch(addr & 0xFF0000)
	{
	case 0x000000:	// System
		if ((addr & 0x00FF00) == 0x000100)
			addr &= ~0x0000FF;	// remove block ID
		switch(addr)
		{
		case 0x00007F:	// SC-88 System Mode Set
			sv->syxReset = MODULE_SC55;
			modChk->fmGS |= (1 << FMBGS_SC_RESET);
			break;
		}
		break;
	case 0x400000:	// Patch (port A)
	case 0x500000:	// Patch (port B)
		addr &= ~0x100000;	// remove port bit
		if ((addr & 0x00F000) >= 0x001000)
		{
			addr &= ~0x000F00;	// remove channel ID
			evtChn = PART_ORDER[syxData[0x05] & 0x0F];
		}
		
		switch(addr)
		{
		case 0x40007F:	// GS Reset
			sv->syxReset = MODULE_SC55;
			modChk->fmGS |= (1 << FMBGS_GS_RESET);
			break;
		case 0x401000:	// Tone Number (Bank MSB + instrument ID)
			sv->insBankBuf[evtChn][0] = syxData[0x07];
			sv->insBank[evtChn][0] = sv->insBankBuf[evtChn][0];
			sv->insBank[evtChn][1] = sv->insBankBuf[evtChn][1];
			sv->insBank[evtChn][2] = syxData[0x08];
			MayDoInsCheck(modChk, sv, evtChn, false);
			break;
		case 0x401015:	// Drum Channel
			if (syxData[0x07])
				sv->drumChnMask |= (1 << evtChn);
			else
				sv->drumChnMask &= ~(1 << evtChn);
			break;
		case 0x404000:	// Tone Map Number (== Bank LSB)
			sv->insBankBuf[evtChn][1] = syxData[0x07];
			break;
		case 0x404001:	// Tone Map 0 Number (== map for Bank LSB 00)
			if (syxData[0x07] <= 0x01)
				tempByt = MTGS_SC88;
			else
				tempByt = syxData[0x07] - 0x01 + MTGS_SC55;
			// not really the correct to do it (not waiting for the actual
			// instrument change), but this will do for now
			// The actual point is to raise the requirement to SC-88+.
			modChk->fmGS |= 1 << (FMBALL_INSSET + tempByt);
			break;
		}
	}
	
	return;
}

static void HandleSysEx_XG(UINT32 syxLen, const UINT8* syxData, MODULE_CHECK* modChk, SCAN_VARS* sv)
{
	UINT32 addr;
	UINT8 evtChn;
	
	addr =	(syxData[0x03] << 16) |
			(syxData[0x04] <<  8) |
			(syxData[0x05] <<  0);
	switch(addr & 0xFF0000)
	{
	case 0x000000:
		switch(addr)
		{
		case 0x00007E:	// XG Reset
			sv->syxReset = MODULE_MU50;
			modChk->fmXG |= (1 << FMBXG_XG_RESET);
			break;
		case 0x00007F:	// All Parameters Reset
			sv->syxReset = MODULE_MU50;
			modChk->fmXG |= (1 << FMBXG_ALL_RESET);
			break;
		}
		break;
	case 0x060000:
		modChk->fmXG |= (1 << FMBALL_TEXT_DISP);
		break;
	case 0x070000:
		modChk->fmXG |= (1 << FMBALL_PIXEL_ART);
		break;
	case 0x080000:
	case 0x0A0000:
		addr &= ~0x00FF00;	// remove part ID
		evtChn = syxData[0x04] & 0x0F;
		switch(addr)
		{
		case 0x080001:	// Bank MSB
			sv->insBankBuf[evtChn][0] = syxData[0x06];
			break;
		case 0x080002:	// Bank LSB
			sv->insBankBuf[evtChn][1] = syxData[0x06];
			break;
		case 0x080003:	// Program Number
			sv->insBank[evtChn][0] = sv->insBankBuf[evtChn][0];
			sv->insBank[evtChn][1] = sv->insBankBuf[evtChn][1];
			sv->insBank[evtChn][2] = syxData[0x06];
			MayDoInsCheck(modChk, sv, evtChn, false);
			break;
		case 0x080007:	// Part Mode
			// 00 - normal (melodic)
			// 01 - drum (auto)
			// 02..05 - drum S1..S4
			if (syxData[0x06] == 0x00)	// Normal
			{
				sv->drumChnMask &= ~(1 << evtChn);
			}
			else
			{
				sv->drumChnMask |= (1 << evtChn);
				if (syxData[0x06] >= 0x04)	// no drum parts 3/4 for MU50 (needs MU80/90 or higher)
					modChk->fmXG |= 1 << (FMBALL_INSSET + MTXG_MU80);
			}
			break;
		}
		break;
	}
	
	return;
}

static UINT8 GetMSBit(UINT32 value)
{
	UINT8 curBit;
	
	value >>= 1;
	for (curBit = 0; value > 0; curBit ++, value >>= 1)
		;
	return curBit;
}

static UINT8 InsMask2ModuleID(UINT32 featureMask, UINT8 notInsMask)
{
	UINT8 insMask;
	UINT8 modID;
	
	insMask = (featureMask >> FMBALL_INSSET) & 0x3F;
	modID = GetMSBit(insMask);
	while((1 << modID) & notInsMask)
		modID ++;
	
	if (featureMask & (1 << FMBALL_BAD_INS))
		modID |= 0x80;
	return modID;
}

static int MetaEvtStrCmp(midevt_const_it eventIt, const char* text)
{
	size_t textLen;
	
	textLen = strlen(text);
	if (eventIt->evtData.size() != textLen)
		return -2;	// TODO: handle in a better way
	return strncmp((const char*)&eventIt->evtData[0], text, textLen);
}

void MidiBankScan(MidiFile* cMidi, bool ignoreEmptyChns, BANKSCAN_RESULT* result)
{
	UINT16 curTrk;
	MidiTrack* mTrk;
	midevt_const_it evtIt;
	UINT8 evtChn;
	UINT32 syxLen;
	const UINT8* syxData;
	
	SCAN_VARS sv;
	MODULE_CHECK modChk;
	
	UINT8 GS_Min;		// GS module ID: minimum compatibility
	UINT8 GS_Opt;		// GS module ID: optimal playback
	UINT8 XG_Opt;		// XG module ID: optimal playback
	UINT8 xgDrum;
	UINT8 spcFeature;
	
	UINT8 modTextFlags;
	
	std::list<std::string> strList;
	std::list<std::string>::const_iterator slIt;
	
	modTextFlags = 0x00;
	modChk.gsMaxLSB = 0x00;
	modChk.MaxDrumKit = 0x00;
	modChk.MaxDrumMSB = 0x00;
	modChk.gsimNot = 0x00;
	modChk.gsimAllMap = 0x00;
	modChk.xgMapSel = 0xFF;
	modChk.fmGM = 0x00;
	modChk.fmGS = 0x00;
	modChk.fmXG = 0x00;
	modChk.fmOther = 0x00;
	spcFeature = 0x00;
	
	sv.drumChnMask = (1 << 9);
	sv.chnUseMask = 0x0000;
	modChk.chnUseMask = 0x0000;
	memset(sv.insBank, 0x00, 16 * 3);
	sv.portIDs.clear();
	sv.syxReset = SYX_RESET_UNDEF;
	sv.insChkOnNote = ignoreEmptyChns;
	
	for (curTrk = 0; curTrk < cMidi->GetTrackCount(); curTrk ++)
	{
		mTrk = cMidi->GetTrack(curTrk);
		
		sv.lastPortID = 0xFF;
		sv.curPortID = 0x00;
		memset(sv.insBankBuf, 0x00, 16 * 2);
		sv.insBankBuf[9][0] = 0xFF;	// drums: ignore MSB unless set explicitly
		
		for (evtIt = mTrk->GetEventBegin(); evtIt != mTrk->GetEventEnd(); ++evtIt)
		{
			evtChn = evtIt->evtType & 0x0F;
			switch(evtIt->evtType & 0xF0)
			{
			case 0x90:	// Note On (if evtValB > 0)
				if (! evtIt->evtValB)
					break;	// Note Off
				
				if (sv.curPortID != sv.lastPortID)
				{
					sv.lastPortID = sv.curPortID;
					sv.portIDs.insert(sv.curPortID);
				}
				sv.chnUseMask |= (1 << evtChn);
				modChk.chnUseMask |= (1 << evtChn);
				MayDoInsCheck(&modChk, &sv, evtChn, true);
				break;
			case 0xB0:	// Control Change
				switch(evtIt->evtValA)
				{
				case 0x00:	// Bank Select MSB
					sv.insBankBuf[evtChn][0] = evtIt->evtValB;
					break;
				case 0x20:	// Bank Select LSB
					sv.insBankBuf[evtChn][1] = evtIt->evtValB;
					break;
				}
				break;
			case 0xC0:	// Instrument Change
				sv.insBank[evtChn][0] = sv.insBankBuf[evtChn][0];
				sv.insBank[evtChn][1] = sv.insBankBuf[evtChn][1];
				sv.insBank[evtChn][2] = evtIt->evtValA;
				if (sv.insChkOnNote)
				{
					if (sv.drumChnMask & (1 << evtChn))
					{
						// for a proper detection, I must keep track of this even when no notes are played on the channel.
						if (sv.insBank[evtChn][0] != 0xFF && modChk.MaxDrumMSB < sv.insBank[evtChn][0])
							modChk.MaxDrumMSB = sv.insBank[evtChn][0];
					}
				}
				MayDoInsCheck(&modChk, &sv, evtChn, false);
				break;
			case 0xF0:
				switch(evtIt->evtType)
				{
				case 0xF0:	// SysEx message
					if (evtIt->evtData.size() < 0x03)
						break;
					syxLen = evtIt->evtData.size();
					syxData = &evtIt->evtData[0];
					// skip repeated F0 byte (yes, there are MIDIs doing this)
					while(syxLen >= 1 && syxData[0x00] == 0xF0)
					{
						syxLen --;
						syxData ++;
					}
					if (syxLen < 0x03)
						break;
					
					// XG reset enables drums via Bank MSB == 0x7F
					// special GS message can enable drum mode
					switch(syxData[0x00])
					{
					case 0x41:	// Roland ID
						if (syxLen < 0x08)
							break;
						// Data[0x01] == 0x1n - Device Number n
						// Data[0x02] == Model ID (MT-32 = 0x16, GS = 0x42)
						// Data[0x03] == Command ID (reqest data RQ1 = 0x11, data set DT1 = 0x12)
						if (syxData[0x03] != 0x12)
							break;
						if (syxData[0x02] == 0x16)	// MT-32
						{
							HandleSysEx_MT32(syxLen, syxData, &modChk, &sv);
						}
						else if (syxData[0x02] == 0x42)	// GS
						{
							HandleSysEx_GS(syxLen, syxData, &modChk, &sv);
						}
						else if (syxData[0x02] == 0x45)	// Sound Canvas
						{
							if (syxData[0x04] == 0x10)
							{
								if (syxData[0x05] == 0x00)
									modChk.fmGS |= (1 << FMBALL_TEXT_DISP);
								else if (syxData[0x05] < 0x10)
									modChk.fmGS |= (1 << FMBALL_PIXEL_ART);
							}
						}
						break;
					case 0x43:	// YAMAHA ID
						if (syxLen < 0x06)
							break;
						if (syxData[0x02] == 0x4C)	 // XG
						{
							HandleSysEx_XG(syxLen, syxData, &modChk, &sv);
						}
						else if (syxData[0x02] == 0x49)	// MU native
						{
							if (syxData[0x03] == 0x00 && syxData[0x04] == 0x00 &&
								syxData[0x05] == 0x12)
							{
								// Select Voice Map (MU100+ only)
								// 00 = MU basic, 01 - MU100 native
								modChk.xgMapSel = syxData[0x06];
							}
						}
						break;
					case 0x7E:
						if (syxLen < 0x04)
							break;
						if (syxData[0x01] == 0x7F && syxData[0x02] == 0x09)
						{
							// GM Level 1 On: F0 7E 7F 09 01 F7
							// GM Level 2 On: F0 7E 7F 09 03 F7
							if (syxData[0x03] == 0x01)
							{
								if (sv.syxReset == SYX_RESET_UNDEF || MMASK_TYPE(sv.syxReset) == MODULE_TYPE_GM)
									sv.syxReset = MODULE_GM_1;
								modChk.fmGM |= (1 << FMBGM_L1_RESET);
							}
							else if (syxData[0x03] == 0x03)
							{
								if (sv.syxReset == SYX_RESET_UNDEF || MMASK_TYPE(sv.syxReset) == MODULE_TYPE_GM)
									sv.syxReset = MODULE_GM_2;
								modChk.fmGM |= (1 << FMBGM_L2_RESET);
							}
						}
						break;
					}
					break;
				case 0xFF:	// Meta Event
					switch(evtIt->evtValA)
					{
					case 0x01:	// Text
						if (! MetaEvtStrCmp(evtIt, "@KMIDI KARAOKE FILE"))
							spcFeature |= (1 << SPCFEAT_KARAOKE);
						break;
					case 0x21:	// MIDI Port
						if (evtIt->evtData[0x00] != sv.curPortID)
						{
							// do a basic reset (TODO: improve port handling)
							sv.drumChnMask = (1 << 9);
							memset(sv.insBank, 0x00, 16 * 3);
							sv.curPortID = evtIt->evtData[0x00];
						}
						break;
					}
					if (! evtIt->evtData.empty() && (evtIt->evtValA >= 1 && evtIt->evtValA <= 6))
					{
						char cmdStr[0x08];
						const char* dataPtr = (char*)&evtIt->evtData[0];
						size_t dataLen = evtIt->evtData.size();
						const char* nullChr = (const char*)memchr(dataPtr, '\0', dataLen);
						if (nullChr != NULL)
							dataLen = nullChr - dataPtr;
						sprintf(cmdStr, "%02X%02X: ", evtIt->evtType, evtIt->evtValA);
						strList.push_back(std::string(cmdStr) + std::string(dataPtr, dataPtr + dataLen));
					}
					break;
				}
				break;
			}
		}	// end for (evtIt)
	}
	
	result->charset = NULL;
#if CHARSET_DETECTION
	if (! strList.empty())
	{
		std::string allStr;
		
		size_t asSize = 0;
		for (slIt = strList.begin(); slIt != strList.end(); ++slIt)
			asSize += slIt->length() + 1;
		allStr.reserve(asSize);
		for (slIt = strList.begin(); slIt != strList.end(); ++slIt)
		{
			allStr += *slIt;
			allStr += '\n';
		}
		
		uchardet_t ucd = uchardet_new();
		// For some reason I don't understand, uchardet behaves differently when feeding
		// it line-by-line compared to all-at-once.
		// In the case of line-by-line, if often just produced an "ASCII" result.
		int ret = uchardet_handle_data(ucd, allStr.c_str(), allStr.length());
		uchardet_data_end(ucd);
		
		result->charset = uchardet_get_charset(ucd);
		// store result into a separate buffer, as the pointer goes out-of-scope when the detector is destroyed
		tmpCharsetStr = (result->charset != NULL) ? result->charset : "";
		result->charset = tmpCharsetStr.empty() ? NULL : tmpCharsetStr.c_str();
		uchardet_delete(ucd);
	}
#endif
	
	for (slIt = strList.begin(); slIt != strList.end(); ++slIt)
	{
		const std::string& str = *slIt;
		size_t strOfs;
		
		if (str.find("SC-55") != std::string::npos || str.find("SC-88") != std::string::npos)
			modTextFlags |= 0x01;
		strOfs = str.find("MU");
		if (strOfs != std::string::npos)
		{
			char muNum = str[strOfs + 2];
			if (muNum >= '0' && muNum <= '9')
				modTextFlags |= 0x02;
		}
		if (str.find("S-YXG") != std::string::npos)
			modTextFlags |= 0x10;
		if (str.find("TG300B") != std::string::npos)
			modTextFlags |= 0x20;
	}
	
	GS_Min = InsMask2ModuleID(modChk.gsimAllMap, 0x00);
	GS_Opt = InsMask2ModuleID(modChk.fmGS, modChk.gsimNot);
	// "SC-88 Mode Set" needs SC-88 or later
	if (modChk.fmGS & (1 << FMBGS_SC_RESET) && GS_Opt < MTGS_SC88)
		GS_Opt = MTGS_SC88;
	
	// When Bank LSB is used to select an explicit GS instrument map,
	// then we may want to increase the "optimal module" requirement.
	// (It is common to use LSB 00 for the "native" map and use LSB 01+ for maps of ancestors.)
	if (modChk.fmGS & (1 << FMBGS_SC_MAP))
	{
		UINT8 minGS;
		UINT8 defLSB;
		
		defLSB = (modChk.fmGS & (1 << FMBGS_DEF_MAP)) ? 1 : 0;
		//if (modChk.gsMaxLSB == 0x01)
		//	defLSB = 1;	// the SC-55 map is only explicitly used on the SC-88
		if (modChk.gsMaxLSB >= 0x04)
			defLSB = 0;	// disable "prefer next higher model" for SC-8850
		else if (modChk.gsMaxLSB == 0x03)
			defLSB = 0;	// for now, don't go from SC-88Pro to SC-8850 by default
		// If there is a "default map" instrument set used, assume that the MIDI
		// was made with the "next higher" module in mind.
		minGS = (modChk.gsMaxLSB - 0x01) + defLSB;
		if (minGS > MTGS_SC8850)
			minGS = MTGS_SC8850;
		if (minGS > GS_Opt)
			GS_Opt = minGS;
	}
	
	XG_Opt = InsMask2ModuleID(modChk.fmXG, 0x00);
	if (modChk.xgMapSel != 0xFF)
	{
		// Note: I only increase the requirement when the MU100 map is selected.
		//       A MIDI that selects the MU basic map might be intentionally backwards-compatible.
		if (modChk.xgMapSel > 0x00 && XG_Opt < MTXG_MU100)
			XG_Opt = MTXG_MU100;
	}
	if ((modChk.fmXG & (1 << FMBXG_BASIC_MAP)) && (modChk.fmXG & (1 << FMBXG_GM_MAP)))
	{
		// When both, the "MU basic" bank (LSB 127) and the GM bank (LSB 0) are used,
		// we *probably* want to default to MU100 voices for LSB 0.
		if (XG_Opt < MTXG_MU100)
			XG_Opt = MTXG_MU100;
	}
	
	xgDrum = (modChk.MaxDrumMSB == 0x7F);
	result->spcFeature = spcFeature;
	result->hasReset = sv.syxReset;
	result->GS_Min = GS_Min;
	result->GS_Opt = GS_Opt;
	result->XG_Opt = XG_Opt;
	result->numPorts = sv.portIDs.size();
	if (result->numPorts == 0)
		result->numPorts = 1;
	result->details = modChk;
	
	if (GS_Opt > MT_UNKNOWN)
		GS_Opt = MT_UNKNOWN;
	if (XG_Opt > MT_UNKNOWN)
		XG_Opt = MT_UNKNOWN;
	//printf("xgDrum %u, DrumMSB %u, DrumIns %u\n", xgDrum, modChk.MaxDrumMSB, modChk.MaxDrumKit);
	//printf("syxReset 0x%02X, fmGM 0x%X, fmGS 0x%X, fmXG 0x%X\n", sv.syxReset, modChk.fmGM, modChk.fmGS, modChk.fmXG);
	if (xgDrum && ! (modChk.fmXG & (1 << FMBALL_BAD_INS)))
	{
		// enforce XG detection for MIDIs with Bank MSB 127 on drum channels
		modChk.fmGM |= (1 << FMBALL_BAD_INS);
		modChk.fmGS |= (1 << FMBALL_BAD_INS);
	}
	else if (sv.syxReset == MODULE_GM_1)
	{
		// The SC-55 treats the GM reset as GS reset, so we check for
		// non-GM instruments (and drum kits) and patch it to SC-55.
		UINT8 notGM = 0x00;
		if (modChk.fmGM & (1 << FMBALL_BAD_INS))
			notGM |= 0x01;
		if (modChk.MaxDrumKit > 0x00)
			notGM |= 0x02;
		if (notGM && GS_Opt == MTGS_SC55)
			sv.syxReset = MODULE_SC55;
	}
	
	if (sv.syxReset != SYX_RESET_UNDEF)
	{
		if (sv.syxReset == MODULE_SC55)
			result->modType = MODULE_TYPE_GS | GS_Opt;
		else if (sv.syxReset == MODULE_MU50)
			result->modType = MODULE_TYPE_XG | XG_Opt;
		else if (sv.syxReset == MODULE_MT32)
			result->modType = (modChk.chnUseMask & 0xFC00) ? MODULE_CM64 : MODULE_MT32;
		else if ((modChk.fmGS & (3 << FMBGS_GS_RESET)) && GS_Opt != MT_UNKNOWN)
			result->modType = MODULE_TYPE_GS | GS_Opt;	// some SC-55 MIDIs have MT-32 *and* GS reset
		else if (sv.syxReset == MODULE_GM_1 && (modChk.fmGM & (1 << (FMBALL_INSSET + MTGM_LVL2))))
			result->modType = MODULE_GM_2;
		else
			result->modType = sv.syxReset;
		//printf("Result based on reset: 0x%02X\n", result->modType);
	}
	else
	{
		if (! (modChk.fmGM & (1 << FMBALL_BAD_INS)))
		{
			if (modChk.fmGM & (1 << (FMBALL_INSSET + MTGM_LVL2)))
				result->modType = MODULE_GM_2;
			else //if (modChk.fmGM & (1 << (FMBALL_INSSET + MTGM_LVL1)))
				result->modType = MODULE_GM_1;
		}
		else if (! (modChk.fmGS & (1 << FMBALL_BAD_INS)))
			result->modType = MODULE_TYPE_GS | GS_Opt;
		else if (! (modChk.fmXG & (1 << FMBALL_BAD_INS)))
			result->modType = MODULE_TYPE_XG | XG_Opt;
		else
			result->modType = 0xFF;
		//printf("Result based on instruments: 0x%02X\n", result->modType);
	}
	if (MMASK_TYPE(result->modType) && (modTextFlags & 0x30))
		result->modType = MODULE_TG300B;
	
	return;
}
