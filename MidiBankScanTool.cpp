#include <iostream>
#include <fstream>
#include <string>
#include <ctype.h>	// for tolower()
#include <string.h>	// for stricmp
#include <vector>
#include <map>

#include <stdtype.h>
#include "MidiLib.hpp"

#include "MidiInsReader.h"
#include "MidiBankScan.hpp"

#ifdef _MSC_VER
#define stricmp		_stricmp
#else
#define stricmp		strcasecmp
#endif

struct TempoChg
{
	UINT32 tick;
	UINT32 tempo;
	UINT64 tmrTick;
};

static void PrintFeatureMask(UINT8 fmType, UINT32 fm);
static void InitModuleNames(void);
static void CalcSongLength(void);


static std::map<std::string, UINT8> shortNameID;	// short name -> ID
static std::vector<std::string> shortIDName;		// ID -> short name

//static INS_BANK insBankGM1;
static INS_BANK insBankGM2;
static INS_BANK insBankGS;
static INS_BANK insBankXG;
//static INS_BANK insBankYGS;	// Yamaha GS (TG300B mode)
//static INS_BANK insBankKorg;
//static INS_BANK insBankMT32;
static bool ignoreEmptyChns;

static MidiFile* _cMidi;
static UINT64 _songLength;
static UINT32 _songTickLen;
static std::list<TempoChg> _tempoList;
static UINT64 _tmrFreq = 1000000000;

static std::string Bin2Str(UINT32 value, size_t len)
{
	std::string str(len, '\0');
	for (size_t bit = 0; bit < len; bit ++, value >>= 1)
		str[len - 1 - bit] = '0' + (value & 1);
	return str;
}

int main(int argc, char* argv[])
{
	int argbase;
	UINT8 retVal;
	MidiFile cMidi;
	
	//std::cout << "MIDI Bank Scan\n";
	//std::cout << "--------------n";
	if (argc < 2)
	{
		std::cout << "Usage: MidiBankScan.exe [options] input.mid\n";
		std::cout << "Options:\n";
		std::cout << "    -e - ignore empty channels\n";
#ifdef _DEBUG
		getchar();
#endif
		return 0;
	}
	
	ignoreEmptyChns = false;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		char optChr = tolower(argv[argbase][1]);
		
		if (optChr == 's' || optChr == 'd')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			//UINT8 algoID = atoi(argv[argbase]);
		}
		else if (optChr == 'e')
		{
			ignoreEmptyChns = true;
		}
		else
		{
			break;
		}
		argbase ++;
	}
	if (argc < argbase + 1)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	retVal = LoadInstrumentList("_MidiInsSets/gs.ins", &insBankGS);
	if (retVal)
		printf("GS Load: 0x%02X\n", retVal);
	retVal = LoadInstrumentList("_MidiInsSets/xg.ins", &insBankXG);
	if (retVal)
		printf("XG Load: 0x%02X\n", retVal);
	{
		INS_BANK tmpBank;
		retVal = LoadInstrumentList("_MidiInsSets/y-plg.ins", &tmpBank);
		if (retVal)
			printf("XG-PLG Load: 0x%02X\n", retVal);
		MergeInstrumentBanks(&insBankXG, &tmpBank);
		FreeInstrumentBank(&tmpBank);
	}
	//retVal = LoadInstrumentList("_MidiInsSets/ygs.ins", &insBankYGS);
	//if (retVal)
	//	printf("YGS Load: 0x%02X\n", retVal);
	retVal = LoadInstrumentList("_MidiInsSets/gml2.ins", &insBankGM2);
	if (retVal)
		printf("GM2 Load: 0x%02X\n", retVal);
	
	//std::cout << "Opening ...\n";
	retVal = cMidi.LoadFile(argv[argbase + 0]);
	if (retVal)
	{
		printf("Error opening %s\n", argv[argbase + 0]);
		printf("Errorcode: %02X\n", retVal);
		return retVal;
	}
	
	SetBankScanInstruments(MODULE_TYPE_GS, &insBankGS);
	SetBankScanInstruments(MODULE_TYPE_XG, &insBankXG);
	//SetBankScanInstruments(MODULE_TG300B, &insBankYGS);
	//SetBankScanInstruments(MODULE_GM_1, &insBankGM1);
	SetBankScanInstruments(MODULE_GM_2, &insBankGM2);
	//SetBankScanInstruments(MODULE_TYPE_K5, &insBankK5);
	//SetBankScanInstruments(MODULE_MT32, &insBankMT32);
	InitModuleNames();
	
	_cMidi = &cMidi;
	
	BANKSCAN_RESULT scanRes;
	CalcSongLength();
	MidiBankScan(&cMidi, ignoreEmptyChns, &scanRes);
	printf("Song Len:\t%.3f s\n", _songLength / (double)_tmrFreq);
	printf("modType:\t0x%02X (%s)\n", scanRes.modType, shortIDName[scanRes.modType].c_str());
	printf("numPorts:\t%u\n", scanRes.numPorts);
	printf("hasReset:\t0x%02X\n", scanRes.hasReset);
	printf("SpcFeatures:\t0x%02X\n", scanRes.spcFeature);
	if (scanRes.spcFeature)
	{
		if (scanRes.spcFeature & SPCFEAT_KARAOKE)
			printf("    Bit %d - %s: %s\n", 0, "Soft Karaoke (.kar) Lyrics", "YES");
	}
	printf("ChannelUsage:\t0b%s\n", Bin2Str(scanRes.details.chnUseMask, 16).c_str());
	printf("minimalGS:\t0x%02X (%s)\n", scanRes.GS_Min, shortIDName[scanRes.GS_Min].c_str());
	printf("optimalGS:\t0x%02X (%s)\n", scanRes.GS_Opt, shortIDName[scanRes.GS_Opt].c_str());
	printf("optimalXG:\t0x%02X (%s)\n", scanRes.XG_Opt, shortIDName[scanRes.XG_Opt].c_str());
	printf("FeatureMaskGM:\t0x%08X\n", scanRes.details.fmGM);
	PrintFeatureMask(MODULE_TYPE_GM, scanRes.details.fmGM);
	printf("FeatureMaskGS:\t0x%08X\n", scanRes.details.fmGS);
	PrintFeatureMask(MODULE_TYPE_GS, scanRes.details.fmGS);
	printf("FeatureMaskXG:\t0x%08X\n", scanRes.details.fmXG);
	PrintFeatureMask(MODULE_TYPE_XG, scanRes.details.fmXG);
	printf("FeatureMaskOther:\t0x%08X\n", scanRes.details.fmOther);
	PrintFeatureMask(MODULE_TYPE_OT, scanRes.details.fmOther);
	printf("MaxDrumKit:\t%u\n", (UINT8)(1 + scanRes.details.MaxDrumKit));
	printf("MaxDrumChnMSB:\t%u\n", scanRes.details.MaxDrumMSB);
	printf("GSInsMap_Default:\t0b%s\n", Bin2Str(scanRes.details.gsimAllMap, 4).c_str());
	printf("GSInsMap_NoSupport:\t0b%s\n", Bin2Str(scanRes.details.gsimNot, 4).c_str());
	printf("GS_MaxLSB:\t%u\n", scanRes.details.gsMaxLSB);
	printf("XG_InsMap:\t%d\n", (INT8)scanRes.details.xgMapSel);
	
	//CMidi.ClearAll();
#ifdef _DEBUG
	//getchar();
#endif
	
	FreeInstrumentBank(&insBankGS);
	FreeInstrumentBank(&insBankXG);
	//FreeInstrumentBank(&insBankYGS);
	FreeInstrumentBank(&insBankGM2);
	
	return 0;
}

static void PrintFeatureMask(UINT8 fmType, UINT32 fm)
{
	static const std::map<int, std::string> fmtxtAll{
		{FMBALL_BAD_INS, "has invalid instruments"},
		{FMBALL_USER_INS, "uses User Instruments"},
		{FMBALL_USER_DRM, "uses User Drum Kits"},
		{FMBALL_TEXT_DISP, "shows ASCII text"},
		{FMBALL_PIXEL_ART, "shows pixel art"},
	};
	static const std::map<int, std::string> fmtxtGM{
		{FMBALL_INSSET + MTGM_LVL1, "uses GM Level 1 instruments"},
		{FMBALL_INSSET + MTGM_LVL2, "uses GM Level 2 instruments"},
		{FMBGM_L1_RESET, "found GM Level 1"},
		{FMBGM_L1_RESET, "found GM Level 2"},
	};
	static const std::map<int, std::string> fmtxtGS{
		{FMBALL_INSSET + MTGS_SC55, "uses SC-55 instruments"},
		{FMBALL_INSSET + MTGS_SC88, "uses SC-88 instruments"},
		{FMBALL_INSSET + MTGS_SC88PRO, "uses SC-88Pro instruments"},
		{FMBALL_INSSET + MTGS_SC8850, "uses SC-8850 instruments"},
		{FMBGS_GS_RESET, "found GS Reset"},
		{FMBGS_SC_RESET, "found SC-88 System Mode Set"},
		{FMBGS_DEF_MAP, "uses default instrument map (Bank LSB 0)"},
		{FMBGS_SC_MAP, "uses SC-specific instrument map (Bank LSB 1+)"},
	};
	static const std::map<int, std::string> fmtxtXG{
		{FMBALL_INSSET + MTXG_MU50, "uses MU50 instruments"},
		{FMBALL_INSSET + MTXG_MU80, "uses MU80 instruments"},
		{FMBALL_INSSET + MTXG_MU90, "uses MU90 instruments"},
		{FMBALL_INSSET + MTXG_MU100, "uses MU100 instruments"},
		{FMBALL_INSSET + MTXG_MU128, "uses MU128 instruments"},
		{FMBALL_INSSET + MTXG_MU1000, "uses MU1000 instruments"},
		{FMBXG_XG_RESET, "found XG Reset"},
		{FMBXG_ALL_RESET, "found XG All Parameters Reset"},
		{FMBXG_GM_MAP, "uses GM instrument map (Bank LSB 0)"},
		{FMBXG_MU100_MAP, "uses MU100 native instrument map (Bank LSB 126)"},
		{FMBXG_BASIC_MAP, "uses MU Basic instrument map (Bank LSB 127)"},
		{FMBXG_NEEDS_CTF, "found an instrument that relies on Capital Tone Fallback"},
		{FMBXG_PANEL, "uses Yamaha keyboard panel voices"},
		{FMBXG_PLG_VL, "uses PLG100-VL voices"},
		{FMBXG_PLG_DX, "uses PLG100-DX voices"},
	};
	static const std::map<int, std::string> fmtxtOther{
		{FMBOTH_MT_RESET, "found MT-32 Reset"},
		{FMBALL_TEXT_DISP, "shows MT-32 ASCII text"},
	};
	
	std::map<int, std::string>::const_iterator mapIt;
	for (int bit = 0; bit < 32; bit ++)
	{
		if (fmType == MODULE_TYPE_GM)
		{
			mapIt = fmtxtGM.find(bit);
			if (mapIt == fmtxtGM.end())
				continue;
		}
		else if (fmType == MODULE_TYPE_GS)
		{
			mapIt = fmtxtGS.find(bit);
			if (mapIt == fmtxtGS.end())
			{
				mapIt = fmtxtAll.find(bit);
				if (mapIt == fmtxtAll.end())
					continue;
			}
		}
		else if (fmType == MODULE_TYPE_XG)
		{
			mapIt = fmtxtXG.find(bit);
			if (mapIt == fmtxtXG.end())
			{
				mapIt = fmtxtAll.find(bit);
				if (mapIt == fmtxtAll.end())
					continue;
			}
		}
		else if (fmType == MODULE_TYPE_OT)
		{
			mapIt = fmtxtOther.find(bit);
			if (mapIt == fmtxtOther.end())
				continue;
		}
		else
		{
			continue;
		}
		UINT8 state = (UINT8)((fm >> bit) & 0x01);
		printf("    Bit %d - %s: %s\n", mapIt->first, mapIt->second.c_str(), state ? "YES" : "NO");
	}
	return;
}

static void InitModuleNames(void)
{
	size_t curID;
	
	shortIDName.resize(0x100);
	shortIDName[MODULE_GM_1     ] = "GM";
	shortIDName[MODULE_GM_2     ] = "GM_L2";
	shortIDName[MODULE_SC55     ] = "SC-55";
	shortIDName[MODULE_SC88     ] = "SC-88";
	shortIDName[MODULE_SC88PRO  ] = "SC-88Pro";
	shortIDName[MODULE_SC8850   ] = "SC-8850";
	shortIDName[MODULE_TG300B   ] = "TG300B";
	shortIDName[MODULE_MU50     ] = "MU50";
	shortIDName[MODULE_MU80     ] = "MU80";
	shortIDName[MODULE_MU90     ] = "MU90";
	shortIDName[MODULE_MU100    ] = "MU100";
	shortIDName[MODULE_MU128    ] = "MU128";
	shortIDName[MODULE_MU1000   ] = "MU1000";
	shortIDName[MODULE_05RW     ] = "05R/W";
	shortIDName[MODULE_X5DR     ] = "X5DR";
	shortIDName[MODULE_NS5R     ] = "NS5R";
	shortIDName[MODULE_KGMB     ] = "K-GMb";
	shortIDName[MODULE_MT32     ] = "MT-32";
	shortIDName[MODULE_CM32P    ] = "CM-32P";
	shortIDName[MODULE_CM64     ] = "CM-64";
	
	for (curID = 0x00; curID < shortIDName.size(); curID ++)
		shortNameID[shortIDName[curID]] = curID;
	
	return;
}

static UINT64 _curTickTime;	// time for 1 MIDI tick at current tempo
static UINT32 _midiTempo;

static inline UINT32 ReadBE24(const UINT8* data)
{
	return (data[0x00] << 16) | (data[0x01] << 8) | (data[0x02] << 0);
}

static bool tempo_compare(const TempoChg& first, const TempoChg& second)
{
	return (first.tick < second.tick);
}

static void RefreshTickTime(void)
{
	UINT64 tmrMul;
	UINT64 tmrDiv;
	
	tmrMul = _tmrFreq * _midiTempo;
	tmrDiv = (UINT64)1000000 * _cMidi->GetMidiResolution();
	if (tmrDiv == 0)
		tmrDiv = 1000000;
	_curTickTime = (tmrMul + tmrDiv / 2) / tmrDiv;
	return;
}

static void CalcSongLength(void)
{
	UINT16 curTrk;
	UINT32 tickBase;
	UINT32 maxTicks;
	UINT32 ticksWhole;
	std::list<TempoChg>::iterator tempoIt;
	std::list<TempoChg>::iterator tPrevIt;
	
	_songLength = 0;
	_tempoList.clear();
	_tempoList.clear();
	
	tickBase = 0;
	maxTicks = 0;
	for (curTrk = 0; curTrk < _cMidi->GetTrackCount(); curTrk ++)
	{
		MidiTrack* mTrk = _cMidi->GetTrack(curTrk);
		midevt_iterator evtIt;
		
		for (evtIt = mTrk->GetEventBegin(); evtIt != mTrk->GetEventEnd(); ++evtIt)
		{
			evtIt->tick += tickBase;	// for Format 2 files, apply track offset
			
			if (evtIt->evtType == 0xFF)
			{
				switch(evtIt->evtValA)
				{
				case 0x51:	// FF 51 - tempo change
					{
						TempoChg tc;
						tc.tick = evtIt->tick;
						tc.tempo = ReadBE24(&evtIt->evtData[0x00]);
						tc.tmrTick = 0;
						_tempoList.push_back(tc);
					}
					break;
				}
			}
		}
		if (_cMidi->GetMidiFormat() == 2)
		{
			maxTicks = mTrk->GetTickCount();
			tickBase = maxTicks;
		}
		else
		{
			if (maxTicks < mTrk->GetTickCount())
				maxTicks = mTrk->GetTickCount();
		}
	}
	
	_tempoList.sort(tempo_compare);
	if (_tempoList.empty() || _tempoList.front().tick > 0)
	{
		// add initial tempo, if no tempo is set at tick 0
		TempoChg tc;
		tc.tick = 0;
		tc.tempo = 500000;	// 120 BPM
		tc.tmrTick = 0;
		_tempoList.push_front(tc);
	}
	
	// calculate time position of tempo events and song length
	tPrevIt = _tempoList.begin();
	tempoIt = tPrevIt;	++tempoIt;
	for (; tempoIt != _tempoList.end(); ++tempoIt)
	{
		UINT32 tickDiff = tempoIt->tick - tPrevIt->tick;
		_midiTempo = tPrevIt->tempo;
		RefreshTickTime();
		
		tempoIt->tmrTick = tPrevIt->tmrTick + tickDiff * _curTickTime;
		
		tPrevIt = tempoIt;
	}
	
	_midiTempo = tPrevIt->tempo;
	RefreshTickTime();
	_songTickLen = maxTicks;
	_songLength = tPrevIt->tmrTick + (maxTicks - tPrevIt->tick) * _curTickTime;
	_midiTempo = _tempoList.begin()->tempo;
	
	return;
}

