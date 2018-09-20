#include <set>
#include <string.h>	// for memset()

#include <stdtype.h>
#include "MidiLib.hpp"
#include "MidiInsReader.h"
#include "MidiBankScan.hpp"


typedef struct
{
	INT16 GS_Min;		// GS module ID: minimum compatibility (using non-GM fallback instruments)
	INT16 GS_Opt;		// GS module ID: optimal playback
	INT16 XG_Opt;		// XG module ID: optimal playback (simpler, because there aren't multiple overlapping maps)
	UINT8 GS_OptLSB;	// GS module: maximum Bank LSB (instrument map)
	UINT8 GS_DefLSB;	// GS module: uses "default" map in some cases (will increase "optimal" module by 1)
	UINT8 XG_Unknown;	// XG module: encountered unknown instrument
	UINT8 GS_User;		// GS module: encountered user instruments
	UINT8 MaxDrumKit;
	UINT8 MaxDrumMSB;
} MODULE_CHECK;
// possible bonus: detect GM MIDIs with XG drums (i.e. not Bank MSB, except for MSB=127 on drum channel)
//	-> XG (compatible with GM)

// Function Prototypes
static UINT8 GetInsModuleID(const INS_BANK* insBank, UINT8 ins, UINT8 msb, UINT8 lsb);
static void DoInstrumentCheck(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb);


static const char* MODNAMES_GS[] =
{	"GM",
	"SC-55", 
	"SC-88",
	"SC-88Pro",
	"SC-8820/SC-8850",
};
static const char* MODNAMES_XG[] =
{	"GM",
	"MU50", 
	"MU80",
	"MU90",
	"MU100",
	"MU128",
	"MU1000/MU2000",
};

static const UINT8 PART_ORDER[0x10] =
{	0x9, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};


#define SYX_RESET_UNDEF	0xFF
#define SYX_RESET_GM	MODULE_GM_1
#define SYX_RESET_GM2	MODULE_GM_2
#define SYX_RESET_GS	MODULE_SC55
#define SYX_RESET_XG	MODULE_MU50
#define SYX_RESET_MT32	MODULE_MT32


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

static void DoInstrumentCheck(MODULE_CHECK* modChk, UINT8 ins, UINT8 msb, UINT8 lsb)
{
	INT16 insModule;
	UINT8 xgIns;
	UINT8 gsMSB;
	
	// check for XG map
	if (msb >= 0x80)
	{
		xgIns = ins;	// use "default" map for unset MSB (drums on channel 10, melody on 1-9, 11-16)
		gsMSB = (ins & 0x80) ? 0x7F : 0x00;
	}
	else
	{
		xgIns = ins & 0x7F;	// use master map (melody/drum selection via directly MSB)
		gsMSB = msb;
	}
	insModule = GetInsModuleID(insBankXG, xgIns, gsMSB, lsb);
	if (insModule >= 0x80)
	{
		insModule = GetInsModuleID(insBankXG, xgIns, gsMSB, 0x00);	// do the usual XG fallback
		if (insModule < 0x80)
			modChk->XG_Unknown = 1;
	}
	if (insModule > modChk->XG_Opt)
		modChk->XG_Opt = insModule;
	if (gsMSB == 0x7F)
	{
		if ((ins & 0x7F) > modChk->MaxDrumKit)
			modChk->MaxDrumKit = ins & 0x7F;
	}
	if (ins & 0x80)
	{
		if (modChk->MaxDrumMSB < msb)
			modChk->MaxDrumMSB = msb;
	}
	
	// check for GS map
	gsMSB = (ins & 0x80) ? 0x00 : msb;
	if (ins == (0x80|0x40) || ins == (0x80|0x41) || gsMSB == 0x40 || gsMSB == 0x41)
	{
		// user patches/drums are supported by SC-88 and later
		// user patch: bank MSB == 0x40/0x41
		// user drum: instrument == 0x40/0x41
		insModule = MTGS_SC88;
		if (ins & 0x80)
			modChk->GS_User |= 0x02;	// user drum
		else
			modChk->GS_User |= 0x01;	// user patch
		if (insModule > modChk->GS_Opt)
			modChk->GS_Opt = insModule;
		if (insModule > modChk->GS_Min)
			modChk->GS_Min = insModule;
	}
	else if (lsb == 0x00)
	{
		modChk->GS_DefLSB = 1;
		// "default" instrument map - search on all instrument maps to guess the right one
		insModule = GetInsModuleID(insBankGS, ins, gsMSB, 0xFF);
		if (insModule > modChk->GS_Opt)
			modChk->GS_Opt = insModule;
		if (insModule > modChk->GS_Min)
			modChk->GS_Min = insModule;
	}
	else
	{
		// fixed instrument map
		if (modChk->GS_OptLSB < lsb)
			modChk->GS_OptLSB = lsb;
		// test for the defined map
		insModule = GetInsModuleID(insBankGS, ins, gsMSB, lsb);
		if (insModule > modChk->GS_Opt)
			modChk->GS_Opt = insModule;
		// test for the minimal map (e.g. Bank MSB 0 is present on all maps)
		insModule = GetInsModuleID(insBankGS, ins, gsMSB, 0xFF);
		if (insModule > modChk->GS_Min)
			modChk->GS_Min = insModule;
	}
	if (ins & 0x80)
	{
		if ((ins & 0x7F) > modChk->MaxDrumKit)
			modChk->MaxDrumKit = ins & 0x7F;
	}
	
	return;
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
	UINT8 curPortID;
	
	MODULE_CHECK modChk;
	
	UINT8 highBankMSB;
	UINT8 highBankLSB;
	UINT8 xgDrum;
	UINT16 drumChnMask;
	UINT8 syxReset;
	
	highBankMSB = 0x00;
	highBankLSB = 0x00;
	xgDrum = 0;
	syxReset = SYX_RESET_UNDEF;
	
	modChk.GS_Min = -1;
	modChk.GS_Opt = -1;
	modChk.XG_Opt = -1;
	modChk.GS_OptLSB = 0x00;
	modChk.GS_DefLSB = 0;
	modChk.XG_Unknown = 0;
	modChk.GS_User = 0x00;
	modChk.MaxDrumKit = 0x00;
	modChk.MaxDrumMSB = 0x00;
	
	curPortID = 0x00;
	drumChnMask = (1 << 9);
	memset(insBank, 0x00, 16 * 3);
	portIDs.clear();
	
	for (curTrk = 0; curTrk < cMidi->GetTrackCount(); curTrk ++)
	{
		mTrk = cMidi->GetTrack(curTrk);
		
		memset(insBankBuf, 0x00, 16 * 2);
		insBankBuf[9][0] = 0xFF;	// drums: ignore MSB unless set explicitly
		
		for (evtIt = mTrk->GetEventBegin(); evtIt != mTrk->GetEventEnd(); ++evtIt)
		{
			evtChn = evtIt->evtType & 0x0F;
			switch(evtIt->evtType & 0xF0)
			{
			case 0x90:	// Note On (if evtValB > 0)
				if (evtIt->evtValB && (insBank[evtChn][2] & 0x80))
				{
					insBank[evtChn][2] &= ~0x80;	// remove "instrument check" flag
					if (drumChnMask & (1 << evtChn))
					{
						if (insBank[evtChn][0] == 0x7F)
							xgDrum = 1;
						DoInstrumentCheck(&modChk, 0x80 | insBank[evtChn][2], insBank[evtChn][0], insBank[evtChn][1]);
					}
					else
					{
						DoInstrumentCheck(&modChk, insBank[evtChn][2], insBank[evtChn][0], insBank[evtChn][1]);
						if (highBankMSB < insBank[evtChn][0])
							highBankMSB = insBank[evtChn][0];
					}
					if (highBankLSB < insBank[evtChn][1])
						highBankLSB = insBank[evtChn][1];
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
					{
						if (insBank[evtChn][0] == 0x7F)
							xgDrum = 1;
						DoInstrumentCheck(&modChk, 0x80 | insBank[evtChn][2], insBank[evtChn][0], insBank[evtChn][1]);
					}
					else
					{
						DoInstrumentCheck(&modChk, insBank[evtChn][2], insBank[evtChn][0], insBank[evtChn][1]);
						if (highBankMSB < insBank[evtChn][0])
							highBankMSB = insBank[evtChn][0];
					}
					if (highBankLSB < insBank[evtChn][1])
						highBankLSB = insBank[evtChn][1];
				}
				break;
			case 0xF0:
				if (evtIt->evtData.size() < 0x03)
					break;
				switch(evtIt->evtType)
				{
				case 0xF0:	// SysEx message
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
							if (syxReset == SYX_RESET_UNDEF)
								syxReset = SYX_RESET_MT32;
							break;
						}
						
						if (evtIt->evtData[0x02] == 0x42)
						{
							// Data[0x04]	Address High
							// Data[0x05]	Address Mid
							// Data[0x06]	Address Low
							if (evtIt->evtData[0x04] == 0x40 && evtIt->evtData[0x05] == 0x00 && evtIt->evtData[0x06] == 0x7F)
							{
								// GS Reset: F0 41 10 42 12 40 00 7F 00 41 F7
								syxReset = SYX_RESET_GS;
							}
							else if (evtIt->evtData[0x04] == 0x00 && evtIt->evtData[0x05] == 0x00 && evtIt->evtData[0x06] == 0x7F)
							{
								// SC-88 System Mode Set
								syxReset = SYX_RESET_GS;
								if (modChk.GS_Opt < 0x01)
									modChk.GS_Opt = 0x01;
							}
							else if (evtIt->evtData[0x04] == 0x40 && (evtIt->evtData[0x05] & 0x70) == 0x10)
							{
								// Part Order
								// 10 1 2 3 4 5 6 7 8 9 11 12 13 14 15 16
								evtChn = PART_ORDER[evtIt->evtData[0x05] & 0x0F];
								switch(evtIt->evtData[0x06])
								{
								case 0x15:	// Drum Channel
									if (evtIt->evtData[0x07])
										drumChnMask |= (1 << evtChn);
									else
										drumChnMask &= ~(1 << evtChn);
									break;
								}
							}
						}
						break;
					case 0x43:	// YAMAHA ID
						if (evtIt->evtData.size() < 0x06)
							break;
						if (evtIt->evtData[0x02] == 0x4C)
						{
							if (evtIt->evtData[0x03] == 0x00 && evtIt->evtData[0x04] == 0x00 &&
								evtIt->evtData[0x05] == 0x7E)
							{
								// XG Reset: F0 43 10 4C 00 00 7E 00 F7
								syxReset = SYX_RESET_XG;
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
								syxReset = SYX_RESET_GM;
							else if (evtIt->evtData[0x03] == 0x03)
								syxReset = SYX_RESET_GM2;
						}
						break;
					}
					break;
				case 0xFF:	// Meta Event
					switch(evtIt->evtValA)
					{
					case 0x21:	// MIDI Port
						portIDs.insert(evtIt->evtData[0x00]);
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
	
	//if (syxReset = SYX_RESET_MT32 && modChk.GS_OptLSB > 0x00)
	//	syxReset = SYX_RESET_UNDEF;
	if (modChk.GS_OptLSB > 0x00)
	{
		UINT8 minGS;
		
		if (modChk.GS_OptLSB == 0x01)
			modChk.GS_DefLSB = 1;	// the SC-55 map is only explicitly used on the SC-88
		else if (modChk.GS_OptLSB >= 0x04)
			modChk.GS_DefLSB = 0;	// disable "prefer next higher model" for SC-8850
		else if (modChk.GS_OptLSB == 0x03)
			modChk.GS_DefLSB = 0;	// for now, don't go from SC-88Pro to SC-8850 by default
		// If there is a "default map" instrument set used, assume that the MIDI
		// was made with the "next higher" module in mind.
		minGS = (modChk.GS_OptLSB - 0x01) + modChk.GS_DefLSB;
		if (minGS > MTGS_SC8850)
			minGS = MTGS_SC8850;
		if (minGS > modChk.GS_Opt)
			modChk.GS_Opt = minGS;
	}
	
	{
		const char* gsMinStr;
		const char* gsOptStr;
		const char* xgOptStr;
		UINT8 didPrint;
		bool gmDerive;
		
		gsMinStr = (modChk.GS_Min < 4) ? MODNAMES_GS[1+modChk.GS_Min] : "unknown";
		gsOptStr = (modChk.GS_Opt < 4) ? MODNAMES_GS[1+modChk.GS_Opt] : "unknown";
		xgOptStr = (modChk.XG_Opt < 6) ? MODNAMES_XG[1+modChk.XG_Opt] : "unknown";
		
		gmDerive = ((syxReset == SYX_RESET_UNDEF) || (syxReset == SYX_RESET_GM));
		
		didPrint = 0;
		if (gmDerive && modChk.MaxDrumMSB == 0x00 && highBankMSB == 0 && highBankLSB == 0)
		{
			// enforce GM detection for MIDIs with Bank MSB == 0 + DrumKit > 0 and no XG reset
			if (modChk.GS_Opt != 0)	// make SC-55 drum kits disable GM detection
				modChk.MaxDrumKit = 0x00;
			modChk.XG_Opt = 0;
			if (modChk.GS_Opt == 0xFF)
				modChk.GS_Opt = 0;	// required for GM detection
		}
		if (highBankMSB == 0 && highBankLSB == 0 && modChk.GS_Opt == 0 && modChk.XG_Opt == 0 && modChk.MaxDrumKit == 0x00)
		{
			//if (syxReset == SYX_RESET_GS || syxReset == SYX_RESET_XG)
			//	syxReset = SYX_RESET_UNDEF;
			if (syxReset != SYX_RESET_GS)
			{
				modChk.GS_Opt = 0xFF;
				modChk.GS_Min = 0xFF;
			}
			if (syxReset != SYX_RESET_XG && ! xgDrum)
			{
				modChk.XG_Opt = 0xFF;
			}
			printf("GM");
			gmDerive = false;
			didPrint = 1;
		}
		if ((gmDerive || syxReset == SYX_RESET_GS) && modChk.GS_Min != 0xFF)
		{
			if (didPrint)
				printf(", ");
			if (gsMinStr != gsOptStr)
				printf("GS/%s (compatible with %s)", gsOptStr, gsMinStr);
			else
				printf("GS/%s", gsOptStr);
			// If there are unkown XG instruments, but GS instruments were all valid,
			// then it's probably not XG.
			if (modChk.GS_Opt != 0xFF && modChk.XG_Opt != 0xFF && modChk.XG_Unknown)
				modChk.XG_Opt = 0xFF;
			didPrint = 1;
		}
		if ((gmDerive || syxReset == SYX_RESET_XG) && modChk.XG_Opt != 0xFF)
		{
			if (didPrint)
				printf(", ");
			printf("XG/%s", xgOptStr);
			if (modChk.XG_Unknown)
				printf(" (bad instruments)");
			if (modChk.XG_Opt < 0 && syxReset == SYX_RESET_XG)
				modChk.XG_Opt = 0;	// TODO: do this in a better way [required for MIDIs that use SysEx for setting instruments]
			didPrint = 1;
		}
		if (! didPrint)
			printf("unknown");
		if (xgDrum)
			printf(" [XG Drums]");
		putchar('\n');
	}
	
	result->hasReset = syxReset;
	result->GS_Min = (UINT8)modChk.GS_Min;
	result->GS_Opt = (UINT8)modChk.GS_Opt;
	result->XG_Opt = (UINT8)modChk.XG_Opt;
	result->GS_Flags = modChk.GS_User;
	result->XG_Flags = (xgDrum << 0) | (modChk.XG_Unknown << 7);
	result->numPorts = portIDs.size();
	if (result->numPorts == 0)
		result->numPorts = 1;
	
	if (modChk.GS_Opt > MT_UNKNOWN)
		modChk.GS_Opt = MT_UNKNOWN;
	if (modChk.XG_Opt > MT_UNKNOWN)
		modChk.XG_Opt = MT_UNKNOWN;
	
	if (syxReset == SYX_RESET_GM)
		result->modType = MODULE_GM_1;
	else if (syxReset == SYX_RESET_GM2)
		result->modType = MODULE_GM_2;
	else if (syxReset == SYX_RESET_GS)
		result->modType = MODULE_TYPE_GS | modChk.GS_Opt;
	else if (syxReset == SYX_RESET_MT32)
		result->modType = MODULE_MT32;
	else if (syxReset == SYX_RESET_XG)
		result->modType = MODULE_TYPE_XG | modChk.XG_Opt;
	else
	{
		if (modChk.GS_Opt != MT_UNKNOWN && ! xgDrum)
			result->modType = MODULE_TYPE_GS | modChk.GS_Opt;
		else if (modChk.XG_Opt != MT_UNKNOWN || xgDrum)
			result->modType = MODULE_TYPE_XG | modChk.XG_Opt;
		else if (highBankMSB == 0 && highBankLSB == 0)
			result->modType = MODULE_GM_1;
		else if (highBankMSB == 120 || highBankMSB == 121)
			result->modType = MODULE_GM_2;
		else
			result->modType = 0xFF;
	}
	
	return;
}
