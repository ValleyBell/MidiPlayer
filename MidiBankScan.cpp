#include <set>
#include <string.h>	// for memset()

#include <stdtype.h>
#include "MidiLib.hpp"
#include "MidiInsReader.h"
#include "MidiBankScan.hpp"


// possible bonus: detect GM MIDIs with XG drums (i.e. not Bank MSB, except for MSB=127 on drum channel)
//	-> XG (compatible with GM)

// Function Prototypes
static UINT8 GetInsModuleID(const INS_BANK* insBank, UINT8 ins, UINT8 msb, UINT8 lsb);
static UINT8 GetGSInsModuleMask(const INS_BANK* insBank, UINT8 ins, UINT8 msb);
static void DoInsCheck_XG(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb);
static void DoInsCheck_GS(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb);
static void DoInstrumentCheck(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb);
static UINT8 GetMSBit(UINT32 value);
static UINT8 InsMask2ModuleID(UINT32 featureMask, UINT8 notInsMask);


static const UINT8 PART_ORDER[0x10] =
{	0x9, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};


#define SYX_RESET_UNDEF	0xFF


static const INS_BANK* insBankGM2 = NULL;
static const INS_BANK* insBankGS = NULL;
static const INS_BANK* insBankXG = NULL;

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

static void DoInsCheck_XG(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb)
{
	UINT8 xgIns;
	UINT8 realMSB;
	UINT8 msbNibH;
	UINT8 msbNibL;
	
	if (msb >= 0x80)	// TODO: maybe do for LSB 7F as well? [needs HW checking]
	{
		xgIns = ins;	// use "default" map for unset MSB (drums on channel 10, melody on 1-9, 11-16)
		realMSB = (ins & 0x80) ? 0x7F : 0x00;
	}
	else
	{
		xgIns = ins & 0x7F;	// use master map (melody/drum selection via directly MSB)
		realMSB = msb;
	}
	msbNibH = msb & 0xF0;
	msbNibL = msb & 0x0F;
	
	if (msb == 0x00 && (lsb == 0x00 || lsb == 0x7E || lsb == 0x7F))
	{
		// LSB 0 is the GM instrument map
		// LSB 126/127 are MU100 variants of it (and they might be missing from the instrument list)
		if (lsb == 0x00)
			modChk->fmXG |= (1 << FMBXG_GM_MAP);
		else
			modChk->fmXG |= (lsb == 0x7F) ? (1 << FMBXG_BASIC_MAP) : (1 << FMBXG_MU100_MAP);
	}
	else if (msb == 0x00 && lsb >= 0x70)
	{
		// Yamaha keyboard panel voices
	}
	else if (msb == 0x3F)
	{
		// QS300 user voices and custom voices for various models
		modChk->fmXG |= (1 << FMBALL_USER_INS);
	}
	else if (msbNibH >= 0x20 && msbNibH <= 0x60 && msbNibL >= 0x01 && msbNibL <= 0x03)
	{
		// known banks:
		// PLG100-VL:
		//	MSB 81: VL-XG/A
		//	MSB 97: VL-XG/B
		//	MSB 33: preset (LSB 0..1), custom (LSB 2), internal (LSB 3)
		// PLG100-DX:
		//	MSB 83: DX-XG/A
		//	MSB 99: DX-XG/B
		//	MSB 67: SFX voices
		//	MSB 35: custom voices
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
		
		insModule = GetInsModuleID(insBankXG, xgIns, realMSB, lsb);
		if (insModule < 0x80)
		{
			modChk->fmXG |= 1 << (FMBALL_INSSET + insModule);
		}
		else
		{
			insModule = GetInsModuleID(insBankXG, xgIns, realMSB, 0x00);	// do the usual XG fallback
			if (insModule < 0x80)
				modChk->fmXG |= (1 << FMBXG_USES_CTF);
			else
				modChk->fmXG |= (1 << FMBALL_BAD_INS);
		}
	}
	
	return;
}

static void DoInsCheck_GS(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb)
{
	INT16 insModule;
	UINT8 realMSB;
	bool isUserIns;
	
	if (ins & 0x80)
	{
		realMSB = 0x00;	// Bank MSB is ignored for drum kits
		isUserIns = (ins == (0x80|0x40) || ins == (0x80|0x41));	// user drum (patch 64/65)
	}
	else
	{
		realMSB = msb;
		isUserIns = (msb == 0x40 || msb == 0x41);	// user instrument (bank 64/65)
	}
	
	if (isUserIns && lsb != 0x01)	// SC-55 doesn't have user instruments
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
		UINT8 insMask;
		
		// "default" instrument map - search on all instrument maps to guess the right one
		insModule = GetInsModuleID(insBankGS, ins, realMSB, 0xFF);
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
		insMask = GetGSInsModuleMask(insBankGS, ins, realMSB);
		if (insMask)
			modChk->gsimNot |= ~insMask;	// take note of the modules that can NOT use it
	}
	else
	{
		// fixed instrument map
		
		// test for the defined map
		insModule = GetInsModuleID(insBankGS, ins, realMSB, lsb);
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
		insModule = GetInsModuleID(insBankGS, ins, realMSB, 0xFF);
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
	DoInsCheck_XG(modChk, ins, msb, lsb);
	DoInsCheck_GS(modChk, ins, msb, lsb);
	
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

void MidiBankScan(MidiFile* cMidi, bool ignoreEmptyChns, BANKSCAN_RESULT* result)
{
	UINT16 curTrk;
	MidiTrack* mTrk;
	midevt_const_it evtIt;
	UINT8 evtChn;
	UINT8 insBankBuf[16][2];
	UINT8 insBank[16][3];
	std::set<UINT8> portIDs;
	UINT8 lastPortID;
	UINT8 curPortID;
	
	MODULE_CHECK modChk;
	UINT16 drumChnMask;
	UINT8 syxReset;	// keeps track of the most recent Reset message type
	
	UINT8 GS_Min;		// GS module ID: minimum compatibility
	UINT8 GS_Opt;		// GS module ID: optimal playback
	UINT8 XG_Opt;		// XG module ID: optimal playback
	UINT8 xgDrum;
	
	syxReset = SYX_RESET_UNDEF;
	
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
	
	drumChnMask = (1 << 9);
	memset(insBank, 0x00, 16 * 3);
	portIDs.clear();
	
	for (curTrk = 0; curTrk < cMidi->GetTrackCount(); curTrk ++)
	{
		mTrk = cMidi->GetTrack(curTrk);
		
		lastPortID = 0xFF;
		curPortID = 0x00;
		memset(insBankBuf, 0x00, 16 * 2);
		insBankBuf[9][0] = 0xFF;	// drums: ignore MSB unless set explicitly
		
		for (evtIt = mTrk->GetEventBegin(); evtIt != mTrk->GetEventEnd(); ++evtIt)
		{
			evtChn = evtIt->evtType & 0x0F;
			switch(evtIt->evtType & 0xF0)
			{
			case 0x90:	// Note On (if evtValB > 0)
				if (! evtIt->evtValB)
					break;	// Note Off
				
				if (curPortID != lastPortID)
				{
					lastPortID = curPortID;
					portIDs.insert(curPortID);
				}
				if (insBank[evtChn][2] & 0x80)
				{
					insBank[evtChn][2] &= ~0x80;	// remove "instrument check" flag
					if (drumChnMask & (1 << evtChn))
						DoInstrumentCheck(&modChk, 0x80 | insBank[evtChn][2], insBank[evtChn][0], insBank[evtChn][1]);
					else
						DoInstrumentCheck(&modChk, insBank[evtChn][2], insBank[evtChn][0], insBank[evtChn][1]);
				}
				break;
			case 0xB0:	// Control Change
				switch(evtIt->evtValA)
				{
				case 0x00:	// Bank Select MSB
					insBankBuf[evtChn][0] = evtIt->evtValB;
					break;
				case 0x20:	// Bank Select LSB
					insBankBuf[evtChn][1] = evtIt->evtValB;
					break;
				}
				break;
			case 0xC0:	// Instrument Change
				insBank[evtChn][0] = insBankBuf[evtChn][0];
				insBank[evtChn][1] = insBankBuf[evtChn][1];
				insBank[evtChn][2] = evtIt->evtValA;
				if (ignoreEmptyChns)
				{
					insBank[evtChn][2] |= 0x80;	// next note will execute instrument check
				}
				else
				{
					if (drumChnMask & (1 << evtChn))
						DoInstrumentCheck(&modChk, 0x80 | insBank[evtChn][2], insBank[evtChn][0], insBank[evtChn][1]);
					else
						DoInstrumentCheck(&modChk, insBank[evtChn][2], insBank[evtChn][0], insBank[evtChn][1]);
				}
				break;
			case 0xF0:
				switch(evtIt->evtType)
				{
				case 0xF0:	// SysEx message
					if (evtIt->evtData.size() < 0x03)
						break;
					// XG reset enabled drums via Bank MSB == 0x7F
					// special GS message can enable drum mode
					switch(evtIt->evtData[0x00])
					{
					case 0x41:	// Roland ID
						if (evtIt->evtData.size() < 0x08)
							break;
						// Data[0x01] == 0x1n - Device Number n
						// Data[0x02] == Model ID (MT-32 = 0x16, GS = 0x42)
						// Data[0x03] == Command ID (reqest data RQ1 = 0x11, data set DT1 = 0x12)
						if (evtIt->evtData[0x03] != 0x12)
							break;
						if (evtIt->evtData[0x02] == 0x16)
						{
							if (evtIt->evtData[0x04] == 0x20)
								modChk.fmOther |= (1 << FMBALL_TEXT_DISP);	// MT-32: Display ASCII characters
							else if (evtIt->evtData[0x04] == 0x7F && evtIt->evtData[0x05] == 0x00 && evtIt->evtData[0x06] == 0x00)
							{
								syxReset = MODULE_MT32;
								modChk.fmOther |= (1 << FMBOTH_MT_RESET);	// MT-32: All Parameters Reset
							}
							break;
						}
						
						if (evtIt->evtData[0x02] == 0x42)	// GS
						{
							// Data[0x04]	Address High
							// Data[0x05]	Address Mid
							// Data[0x06]	Address Low
							if (evtIt->evtData[0x04] == 0x40 && evtIt->evtData[0x05] == 0x00 && evtIt->evtData[0x06] == 0x7F)
							{
								// GS Reset: F0 41 10 42 12 40 00 7F 00 41 F7
								syxReset = MODULE_SC55;
								modChk.fmGS |= (1 << FMBGS_GS_RESET);
							}
							else if (evtIt->evtData[0x04] == 0x00 && evtIt->evtData[0x05] == 0x00 && evtIt->evtData[0x06] == 0x7F)
							{
								// SC-88 System Mode Set
								syxReset = MODULE_SC55;
								modChk.fmGS |= (1 << FMBGS_SC_RESET);
							}
							else if (evtIt->evtData[0x04] == 0x40 && (evtIt->evtData[0x05] & 0x70) == 0x10)
							{
								// Part Order
								// 10 1 2 3 4 5 6 7 8 9 11 12 13 14 15 16
								evtChn = PART_ORDER[evtIt->evtData[0x05] & 0x0F];
								switch(evtIt->evtData[0x06])
								{
								case 0x00:	// Tone Number (Bank MSB + instrument ID)
									insBankBuf[evtChn][0] = evtIt->evtData[0x07];
									insBank[evtChn][0] = insBankBuf[evtChn][0];
									insBank[evtChn][1] = insBankBuf[evtChn][1];
									insBank[evtChn][2] = evtIt->evtData[0x08];
									// TODO: do more
									insBank[evtChn][2] |= 0x80;
									break;
								case 0x15:	// Drum Channel
									if (evtIt->evtData[0x07])
										drumChnMask |= (1 << evtChn);
									else
										drumChnMask &= ~(1 << evtChn);
									break;
								}
							}
							else if (evtIt->evtData[0x04] == 0x40 && (evtIt->evtData[0x05] & 0x70) == 0x40)
							{
								UINT8 tempByt;
								
								evtChn = PART_ORDER[evtIt->evtData[0x05] & 0x0F];
								switch(evtIt->evtData[0x06])
								{
								case 0x00:	// Tone Map Number (== Bank LSB)
									insBankBuf[evtChn][1] = evtIt->evtData[0x07];
									break;
								case 0x01:	// Tone Map 0 Number (== map for Bank LSB 00)
									if (evtIt->evtData[0x07] <= 0x01)
										tempByt = MTGS_SC88;
									else
										tempByt = evtIt->evtData[0x07] - 0x01 + MTGS_SC55;
									// not really the correct to do it (not waiting for the actual
									// instrument change), but this will do for now
									modChk.fmGS |= 1 << (FMBALL_INSSET + tempByt);
									break;
								}
							}
						}
						else if (evtIt->evtData[0x02] == 0x45)	// Sound Canvas
						{
							if (evtIt->evtData[0x04] == 0x10)
							{
								if (evtIt->evtData[0x05] == 0x00)
									modChk.fmGS |= (1 << FMBALL_TEXT_DISP);
								else if (evtIt->evtData[0x05] < 0x10)
									modChk.fmGS |= (1 << FMBALL_PIXEL_ART);
							}
						}
						break;
					case 0x43:	// YAMAHA ID
						if (evtIt->evtData.size() < 0x06)
							break;
						if (evtIt->evtData[0x02] == 0x4C)	 // XG
						{
							if (evtIt->evtData[0x03] == 0x00 && evtIt->evtData[0x04] == 0x00)
							{
								if (evtIt->evtData[0x05] == 0x7E)
								{
									// XG Reset: F0 43 10 4C 00 00 7E 00 F7
									syxReset = MODULE_MU50;
									modChk.fmXG |= (1 << FMBXG_XG_RESET);
								}
								else if (evtIt->evtData[0x05] == 0x7F)
								{
									// All Parameters Reset
									syxReset = MODULE_MU50;
									modChk.fmXG |= (1 << FMBXG_ALL_RESET);
								}
							}
							else if (evtIt->evtData[0x03] == 0x06)
								modChk.fmXG |= (1 << FMBALL_TEXT_DISP);
							else if (evtIt->evtData[0x03] == 0x07)
								modChk.fmXG |= (1 << FMBALL_PIXEL_ART);
							else if (evtIt->evtData[0x03] == 0x08 || evtIt->evtData[0x03] == 0x0A)
							{
								evtChn = evtIt->evtData[0x04] & 0x0F;
								switch((evtIt->evtData[0x03] << 16) | (evtIt->evtData[0x05] << 0))
								{
								case 0x080001:	// Bank MSB
									insBankBuf[evtChn][0] = evtIt->evtData[0x06];
									break;
								case 0x080002:	// Bank LSB
									insBankBuf[evtChn][1] = evtIt->evtData[0x06];
									break;
								case 0x080003:	// Program Number
									insBank[evtChn][0] = insBankBuf[evtChn][0];
									insBank[evtChn][1] = insBankBuf[evtChn][1];
									insBank[evtChn][2] = evtIt->evtData[0x06];
									// TODO: do more
									insBank[evtChn][2] |= 0x80;
									break;
								}
							}
						}
						else if (evtIt->evtData[0x02] == 0x49)	// MU native
						{
							if (evtIt->evtData[0x03] == 0x00 && evtIt->evtData[0x04] == 0x00 &&
								evtIt->evtData[0x05] == 0x12)
							{
								// Select Voice Map (MU100+ only)
								// 00 = MU basic, 01 - MU100 native
								modChk.xgMapSel = evtIt->evtData[0x06];
							}
						}
						break;
					case 0x7E:
						if (evtIt->evtData.size() < 0x04)
							break;
						if (evtIt->evtData[0x01] == 0x7F && evtIt->evtData[0x02] == 0x09)
						{
							// GM Level 1 On: F0 7E 7F 09 01 F7
							// GM Level 2 On: F0 7E 7F 09 03 F7
							if (evtIt->evtData[0x03] == 0x01)
							{
								syxReset = MODULE_GM_1;
								modChk.fmGM |= (1 << FMBGM_L1_RESET);
							}
							else if (evtIt->evtData[0x03] == 0x03)
							{
								syxReset = MODULE_GM_2;
								modChk.fmGM |= (1 << FMBGM_L2_RESET);
							}
						}
						break;
					}
					break;
				case 0xFF:	// Meta Event
					switch(evtIt->evtValA)
					{
					case 0x21:	// MIDI Port
						if (evtIt->evtData[0x00] != curPortID)
						{
							// do a basic reset (TODO: improve port handling)
							drumChnMask = (1 << 9);
							memset(insBank, 0x00, 16 * 3);
							curPortID = evtIt->evtData[0x00];
						}
						break;
					}
					break;
				}
				break;
			}
		}	// end for (evtIt)
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
		if (modChk.gsMaxLSB == 0x01)
			defLSB = 1;	// the SC-55 map is only explicitly used on the SC-88
		else if (modChk.gsMaxLSB >= 0x04)
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
	
	xgDrum = (modChk.MaxDrumMSB == 0x7F);
	result->hasReset = syxReset;
	result->GS_Min = GS_Min;
	result->GS_Opt = GS_Opt;
	result->XG_Opt = XG_Opt;
	result->numPorts = portIDs.size();
	if (result->numPorts == 0)
		result->numPorts = 1;
	result->details = modChk;
	
	if (GS_Opt > MT_UNKNOWN)
		GS_Opt = MT_UNKNOWN;
	if (XG_Opt > MT_UNKNOWN)
		XG_Opt = MT_UNKNOWN;
	
	if (syxReset != SYX_RESET_UNDEF)
	{
		if (syxReset == MODULE_SC55)
			result->modType = MODULE_TYPE_GS | GS_Opt;
		else if (syxReset == MODULE_MU50)
			result->modType = MODULE_TYPE_XG | XG_Opt;
		else
			result->modType = syxReset;
	}
	else
	{
		if (GS_Opt != MT_UNKNOWN && ! xgDrum)
			result->modType = MODULE_TYPE_GS | GS_Opt;
		else if (XG_Opt != MT_UNKNOWN || xgDrum)
			result->modType = MODULE_TYPE_XG | XG_Opt;
		else if (modChk.fmGM & (1 << (FMBALL_INSSET + MTGM_LVL2)))
			result->modType = MODULE_GM_2;
		else if (modChk.fmGM & (1 << (FMBALL_INSSET + MTGM_LVL1)))
			result->modType = MODULE_GM_1;
		else
			result->modType = 0xFF;
	}
	
	return;
}
