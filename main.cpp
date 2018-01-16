#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <conio.h>
#include <Windows.h>
#else
#include <unistd.h>
#define Sleep(x)	usleep(x * 1000)
#endif

#include "inih/cpp/INIReader.h"	// https://github.com/benhoyt/inih

#include <stdtype.h>
#include "MidiLib.hpp"
#include "MidiOut.h"
#include "MidiPlay.hpp"
#include "MidiInsReader.h"
#include "MidiBankScan.hpp"
#include "vis.hpp"


struct InstrumentSetCfg
{
	UINT8 setType;
	std::string pathName;
};
struct MidiModule
{
	std::string name;
	UINT8 modType;	// module type
	std::vector<UINT8> ports;
	std::vector<UINT8> playType;	// supported types for playing
};


static const char* MODNAMES_GM[] =
{
	"GM",
	"GM Level 2"
};
static const char* MODNAMES_GS[] =
{
	"SC-55",
	"SC-88",
	"SC-88Pro",
	"SC-8850",
};
static const char* MODNAMES_XG[] =
{
	"MU50",
	"MU80",
	"MU90",
	"MU100",
	"MU128",
	"MU1000/MU2000",
};


static const char* INS_SET_PATH = "_MidiInsSets/";
static std::string midFileName;
static MidiFile CMidi;
static MidiPlayer midPlay;

static bool keepPortsOpen;
static UINT8 playerCfgFlags;	// see PlayerOpts::flags
static UINT8 forceSrcType;
static UINT8 forceModID;
static std::vector<InstrumentSetCfg> insSetFiles;
static std::vector<MidiModule> midiModules;
static std::vector<INS_BANK> insBanks;
static std::string syxFile;
static std::vector<UINT8> syxData;


static bool is_no_space(char c);
static void CfgString2Vector(const std::string& valueStr, std::vector<std::string>& valueVector);
static UINT8 GetIDFromNameOrNumber(const std::string& valStr, const std::map<std::string, UINT8>& nameLUT, UINT8& retValue);
static void ReadPlayTypeList(const std::vector<std::string>& ptList, const std::map<std::string, UINT8>& nameLUT, std::vector<UINT8>& playTypes);
static UINT8 LoadConfig(const std::string& cfgFile);
static size_t GetOptimalModule(const BANKSCAN_RESULT* scanRes);
static const char* GetModuleTypeName(UINT8 modType);
void PlayMidi(void);
static void SendSyxDataToPorts(const std::vector<MIDIOUT_PORT*>& outPorts, size_t dataLen, const UINT8* data);
static void SendSyxData(const std::vector<MIDIOUT_PORT*>& outPorts, const std::vector<UINT8>& syxData);
static void MidiEventCallback(void* userData, const MidiEvent* midiEvt, UINT16 chnID);


int main(int argc, char* argv[])
{
	int argbase;
	UINT8 retVal;
	size_t curInsBnk;
	
	setlocale(LC_ALL, "");	// enable UTF-8 support on Linux
	
	printf("MIDI Player\n");
	printf("-----------\n");
	if (argc < 2)
	{
		printf("Usage: MidiPlayer.exe [options] file.mid\n");
		printf("Options:\n");
		//printf("    -e - ignore empty channels\n");
		printf("    -o n - set option bitmask (default: 0x07)\n");
		printf("           Bit 0 (0x01) - send GM/GS/XG reset, if missing\n");
		printf("           Bit 1 (0x02) - strict mode (enforce GS instrument map)\n");
		printf("           Bit 2 (0x04) - enable Capital Tone Fallback (SC-55 style)\n");
		printf("    -s n - enforce source type\n");
		printf("           0x00 = GM, 0x10..0x13 = SC-55/88/88Pro/8850,\n");
		printf("           0x20..24 = MU50/80/90/100/128/1000, 0x70 = MT-32\n");
		printf("    -m n - enforce playback on module with ID n\n");
		printf("    -x   - send .syx file to all ports before playing the MIDI\n");
		return 0;
	}
	
	playerCfgFlags = PLROPTS_RESET | PLROPTS_STRICT | PLROPTS_ENABLE_CTF;
	forceSrcType = 0xFF;
	forceModID = 0xFF;
	syxFile = "";
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		char optChr = tolower(argv[argbase][1]);
		
		if (optChr == 'o')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			playerCfgFlags = (UINT8)strtoul(argv[argbase], NULL, 0);
		}
		else if (optChr == 's')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			forceSrcType = (UINT8)strtoul(argv[argbase], NULL, 0);
		}
		else if (optChr == 'm')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			forceModID = (UINT8)strtoul(argv[argbase], NULL, 0);
		}
		else if (optChr == 'x')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			syxFile = argv[argbase];
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
	
	retVal = LoadConfig("config.ini");
	if (retVal)
	{
		printf("Error: No modules defined!\n");
		return 0;
	}
	
	insBanks.resize(insSetFiles.size());
	for (curInsBnk = 0; curInsBnk < insSetFiles.size(); curInsBnk ++)
	{
		const InstrumentSetCfg* tmpInsSet = &insSetFiles[curInsBnk];
		INS_BANK* insBank = &insBanks[curInsBnk];
		
		retVal = LoadInstrumentList(tmpInsSet->pathName.c_str(), insBank);
		if (retVal)
			printf("InsSet %s Load: 0x%02X\n", tmpInsSet->pathName.c_str(), retVal);
		
		SetBankScanInstruments(tmpInsSet->setType, insBank);
		midPlay.SetInstrumentBank(tmpInsSet->setType, insBank);
	}
	
	midFileName = argv[argbase + 0];
	printf("Opening %s ...\n", midFileName.c_str());
	retVal = CMidi.LoadFile(midFileName.c_str());
	if (retVal)
	{
		printf("Error opening %s\n", midFileName.c_str());
		printf("Errorcode: %02X\n", retVal);
		return retVal;
	}
	printf("File loaded.\n");
	
	PlayMidi();
	
	for (curInsBnk = 0; curInsBnk < insBanks.size(); curInsBnk ++)
		FreeInstrumentBank(&insBanks[curInsBnk]);
	insBanks.clear();
	
	return 0;
}

static bool is_no_space(char c)
{
	return ! ::isspace((unsigned char)c);
}

static void CfgString2Vector(const std::string& valueStr, std::vector<std::string>& valueVector)
{
	valueVector.clear();
	std::stringstream ss(valueStr);
	std::string item;
	
	while(ss.good())
	{
		std::getline(ss, item, ',');
		
		// ltrim
		item.erase(item.begin(), std::find_if(item.begin(), item.end(), &is_no_space));
		// rtrim
		item.erase(std::find_if(item.rbegin(), item.rend(), &is_no_space).base(), item.end());
		
		valueVector.push_back(item);
	}
	if (valueVector.size() == 1 && valueVector[0].empty())
		valueVector.clear();
	
	return;
}

static UINT8 GetIDFromNameOrNumber(const std::string& valStr, const std::map<std::string, UINT8>& nameLUT, UINT8& retValue)
{
	std::map<std::string, UINT8>::const_iterator nameIt;
	char* endStr;
	
	nameIt = nameLUT.find(valStr);
	if (nameIt != nameLUT.end())
	{
		retValue = nameIt->second;
		return 1;
	}
	
	retValue = (UINT8)strtoul(valStr.c_str(), &endStr, 0);
	if (endStr == valStr.c_str())
		return 0;	// not read
	else if (endStr == valStr.c_str() + valStr.length())
		return 1;	// fully read
	else
		return 2;	// partly read
}

static void ReadPlayTypeList(const std::vector<std::string>& ptList, const std::map<std::string, UINT8>& nameLUT, std::vector<UINT8>& playTypes)
{
	std::vector<std::string>::const_iterator typeIt;
	char* endStr;
	
	playTypes.clear();
	for (typeIt = ptList.begin(); typeIt != ptList.end(); ++typeIt)
	{
		UINT8 pType;
		UINT8 retVal;
		UINT8 curMod;
		
		retVal = GetIDFromNameOrNumber(*typeIt, nameLUT, pType);
		if (retVal == 0)
		{
			if (*typeIt == "SC-xx")
			{
				for (curMod = 0x00; curMod < MT_UNKNOWN; curMod ++)
					playTypes.push_back(MODULE_TYPE_GS | curMod);
			}
			else if (*typeIt == "MUxx")
			{
				for (curMod = 0x00; curMod < MT_UNKNOWN; curMod ++)
					playTypes.push_back(MODULE_TYPE_XG | curMod);
			}
		}
		else if (retVal == 1)
		{
			playTypes.push_back(pType);
		}
		else
		{
			pType = (UINT8)strtoul(typeIt->c_str(), &endStr, 0);
			if (*endStr == '#')
			{
				pType <<= 4;
				for (curMod = 0x00; curMod < MT_UNKNOWN; curMod ++)
					playTypes.push_back(pType | curMod);
			}
		}
	}
	
	return;
}

static UINT8 LoadConfig(const std::string& cfgFile)
{
	std::map<std::string, UINT8> INSSET_NAME_MAP;
	std::map<std::string, UINT8> MODTYPE_NAME_MAP;
	std::string insSetPath;
	std::map<std::string, UINT8>::const_iterator nmIt;
	std::vector<std::string> modList;
	std::vector<std::string>::const_iterator mlIt;
	
	INSSET_NAME_MAP["GM"] = MODULE_GM_1;
	INSSET_NAME_MAP["GM_L2"] = MODULE_GM_2;
	INSSET_NAME_MAP["GS"] = MODULE_TYPE_GS;
	INSSET_NAME_MAP["XG"] = MODULE_TYPE_XG;
	INSSET_NAME_MAP["MT-32"] = MODULE_MT32;
	MODTYPE_NAME_MAP["GM"] = MODULE_GM_1;
	MODTYPE_NAME_MAP["GM_L2"] = MODULE_GM_2;
	MODTYPE_NAME_MAP["SC-55"] = MODULE_SC55;
	MODTYPE_NAME_MAP["SC-88"] = MODULE_SC88;
	MODTYPE_NAME_MAP["SC-88Pro"] = MODULE_SC88PRO;
	MODTYPE_NAME_MAP["SC-8850"] = MODULE_SC8850;
	MODTYPE_NAME_MAP["TG300B"] = MODULE_TG300B;
	MODTYPE_NAME_MAP["MU50"] = MODULE_MU50;
	MODTYPE_NAME_MAP["MU80"] = MODULE_MU80;
	MODTYPE_NAME_MAP["MU90"] = MODULE_MU90;
	MODTYPE_NAME_MAP["MU100"] = MODULE_MU100;
	MODTYPE_NAME_MAP["MU128"] = MODULE_MU128;
	MODTYPE_NAME_MAP["MU1000"] = MODULE_MU1000;
	MODTYPE_NAME_MAP["MT-32"] = MODULE_MT32;
	
	INIReader iniFile(cfgFile);
	if (iniFile.ParseError())
	{
		printf("Error reading %s!\n", cfgFile.c_str());
		return 0xFF;
	}
	
	keepPortsOpen = iniFile.GetBoolean("General", "KeepPortsOpen", true);
	
	insSetFiles.clear();
	insSetPath = iniFile.Get("InstrumentSets", "DataPath", INS_SET_PATH);
	for (nmIt = INSSET_NAME_MAP.begin(); nmIt != INSSET_NAME_MAP.end(); ++nmIt)
	{
		std::string fileName = iniFile.Get("InstrumentSets", nmIt->first, "");
		if (! fileName.empty())
		{
			InstrumentSetCfg isc;
			isc.setType = nmIt->second;
			isc.pathName = insSetPath + fileName;
			insSetFiles.push_back(isc);
		}
	}
	
	midiModules.clear();
	CfgString2Vector(iniFile.Get("General", "Modules", ""), modList);
	for (mlIt = modList.begin(); mlIt != modList.end(); ++mlIt)
	{
		MidiModule mMod;
		std::vector<std::string> list;
		size_t curItem;
		
		mMod.name = *mlIt;
		
		if (! GetIDFromNameOrNumber(iniFile.Get(mMod.name, "ModType", ""), MODTYPE_NAME_MAP, mMod.modType))
			continue;
		
		CfgString2Vector(iniFile.Get(mMod.name, "Ports", ""), list);
		for (curItem = 0; curItem < list.size(); curItem ++)
		{
			UINT8 port;
			char* endStr;
			
			port = (UINT8)strtoul(list[curItem].c_str(), &endStr, 0);
			if (endStr != list[curItem].c_str())
				mMod.ports.push_back(port);
		}
		
		CfgString2Vector(iniFile.Get(mMod.name, "PlayTypes", ""), list);
		ReadPlayTypeList(list, MODTYPE_NAME_MAP, mMod.playType);
		
		midiModules.push_back(mMod);
	}
	
	keepPortsOpen = true;
	
	return 0x00;
}

// returns module type for optimal playback
static size_t GetOptimalModule(const BANKSCAN_RESULT* scanRes)
{
	size_t curMod;
	size_t curPT;
	
	// try for an exact match first
	for (curMod = 0; curMod < midiModules.size(); curMod ++)
	{
		const MidiModule& mMod = midiModules[curMod];
		
		for (curPT = 0; curPT < mMod.playType.size(); curPT ++)
		{
			if (mMod.playType[curPT] == scanRes->modType)
				return curMod;
		}
	}
	// then search for approximate matches (GS on "any GS device", GM on GS/XG, etc.)
	for (curMod = 0; curMod < midiModules.size(); curMod ++)
	{
		const MidiModule& mMod = midiModules[curMod];
		
		for (curPT = 0; curPT < mMod.playType.size(); curPT ++)
		{
			if (MMASK_TYPE(mMod.playType[curPT]) == MMASK_TYPE(scanRes->modType))
				return curMod;
			if (MMASK_TYPE(scanRes->modType) == MODULE_TYPE_GM)
			{
				if (MMASK_TYPE(mMod.playType[curPT]) == MODULE_TYPE_GS ||
					MMASK_TYPE(mMod.playType[curPT]) == MODULE_TYPE_XG)
					return curMod;
			}
		}
	}
	
	return (size_t)-1;
}

static const char* GetModuleTypeName(UINT8 modType)
{
	if (modType == MODULE_MT32)
		return "MT-32";
	else if (modType == MODULE_TG300B)
		return "TG300B";
	else if (modType == (MODULE_TYPE_GS | MT_UNKNOWN))
		return "GS/unknown";
	else if (modType == (MODULE_TYPE_XG | MT_UNKNOWN))
		return "XG/unknown";
	else if (MMASK_TYPE(modType) == MODULE_TYPE_GM)
		return MODNAMES_GM[MMASK_MOD(modType)];
	else if (MMASK_TYPE(modType) == MODULE_TYPE_GS)
		return MODNAMES_GS[MMASK_MOD(modType)];
	else if (MMASK_TYPE(modType) == MODULE_TYPE_XG)
		return MODNAMES_XG[MMASK_MOD(modType)];
	else
		return "unknown";
}

void PlayMidi(void)
{
	BANKSCAN_RESULT scanRes;
	PlayerOpts plrOpts;
	size_t chosenModule;
	MidiModule* mMod;
	UINT8 curPort;
	UINT8 retVal;
	std::vector<MIDIOUT_PORT*> mOuts;
	
	// try to detect the instrument set used by the MIDI
	MidiBankScan(&CMidi, true, &scanRes);
	if (forceSrcType != 0xFF)
		scanRes.modType = forceSrcType;
	
	{
		printf("MIDI Scan Result: %s\n", GetModuleTypeName(scanRes.modType));
		if (MMASK_TYPE(scanRes.modType) == MODULE_TYPE_GS && scanRes.GS_Min < scanRes.GS_Opt)
			printf("    - GS backwards compatible with %s\n", MODNAMES_GS[MMASK_MOD(scanRes.GS_Min)]);
		if (MMASK_TYPE(scanRes.modType) != MODULE_TYPE_XG && (scanRes.XG_Flags & 0x01))
			printf("    - used XG drums\n");
		if (MMASK_TYPE(scanRes.modType) == MODULE_TYPE_XG && (scanRes.XG_Flags & 0x80))
			printf("    - unknown XG instruments found\n");
		if (scanRes.modType == 0xFF)
		{
			scanRes.modType = MODULE_GM_1;
			printf("Falling back to %s mode ...\n", GetModuleTypeName(scanRes.modType));
		}
	}
	
	if (forceModID == 0xFF)
		chosenModule = GetOptimalModule(&scanRes);
	else
		chosenModule = forceModID;
	if (chosenModule == (size_t)-1 || chosenModule >= midiModules.size())
	{
		printf("Unable to find an appropriate MIDI module!\n");
		return;
	}
	mMod = &midiModules[chosenModule];
	printf("Using module %s.\n", mMod->name.c_str());
	
	if (! syxFile.empty())
	{
		FILE* hFile;
		
		hFile = fopen(syxFile.c_str(), "rb");
		if (hFile != NULL)
		{
			size_t readBytes;
			
			fseek(hFile, 0, SEEK_END);
			syxData.resize(ftell(hFile));
			rewind(hFile);
			
			readBytes = 0;
			if (syxData.size() > 0)
				readBytes = fread(&syxData[0], 0x01, syxData.size(), hFile);
			syxData.resize(readBytes);
			fclose(hFile);
		}
	}
	
	plrOpts.srcType = scanRes.modType;
	plrOpts.dstType = mMod->modType;
	plrOpts.flags = playerCfgFlags;
	if (scanRes.hasReset != 0xFF)
		plrOpts.flags &= ~PLROPTS_RESET;
	midPlay.SetOptions(plrOpts);
	
	if (scanRes.numPorts > mMod->ports.size())
	{
		printf("Warning: The module doesn't have enought ports defined for proper playback!\n");
		scanRes.numPorts = (UINT8)mMod->ports.size();
	}
	for (curPort = 0; curPort < scanRes.numPorts; curPort ++)
	{
		MIDIOUT_PORT* newPort;
		
		newPort = MidiOutPort_Init();
		if (newPort == NULL)
			continue;
		printf("Opening MIDI port %u ...", mMod->ports[curPort]);
		retVal = MidiOutPort_OpenDevice(newPort, mMod->ports[curPort]);
		if (retVal)
		{
			MidiOutPort_Deinit(newPort);
			printf("  Error %02X\n", retVal);
			continue;
		}
		printf("  OK, type: %s\n", GetModuleTypeName(mMod->modType));
		mOuts.push_back(newPort);
	}
	if (mOuts.empty())
	{
		printf("Error opening MIDI ports!\n");
		getchar();
		return;
	}
	
	midPlay.SetOutputPorts(mOuts);
	midPlay.SetMidiFile(&CMidi);
	vis_set_type_str(0, GetModuleTypeName(mMod->modType));
	vis_set_midi_file(midFileName.c_str(), &CMidi);
	vis_set_midi_player(&midPlay);
	vis_set_type_str(1, GetModuleTypeName(scanRes.modType));
	printf("Song length: %.3f s\n", midPlay.GetSongLength());
	
	vis_init();
	
	if (! syxData.empty())
	{
		vis_addstr("Sending SYX data ...");
		SendSyxData(mOuts, syxData);
	}
	
	midPlay.SetEventCallback(&MidiEventCallback, &midPlay);
	midPlay.Start();
	vis_new_song();
	Sleep(100);
	
	while(midPlay.GetState() & 0x01)
	{
		int inkey;
		
		midPlay.DoPlaybackStep();
		vis_update();
		
		inkey = vis_getch();
		if (inkey)
		{
			if (inkey < 0x100 && isalpha(inkey))
				inkey = toupper(inkey);
			
			if (inkey == 0x1B || inkey == 'Q')
				break;
			else if (inkey == ' ')
			{
				if (midPlay.GetState() & 0x02)
					midPlay.Resume();
				else
					midPlay.Pause();
			}
		}
		Sleep(1);
	}
	midPlay.Stop();
	vis_deinit();
	
	//Sleep(500);
	printf("Cleaning ...\n");
	CMidi.ClearAll();
	printf("Done.\n");
	
	for (curPort = 0; curPort < mOuts.size(); curPort ++)
	{
		MidiOutPort_CloseDevice(mOuts[curPort]);
		MidiOutPort_Deinit(mOuts[curPort]);	mOuts[curPort] = NULL;
	}
	mOuts.clear();
	Sleep(100);
	
	return;
}

static void SendSyxDataToPorts(const std::vector<MIDIOUT_PORT*>& outPorts, size_t dataLen, const UINT8* data)
{
	std::vector<MIDIOUT_PORT*>::const_iterator portIt;
	
	for (portIt = outPorts.begin(); portIt != outPorts.end(); ++portIt)
		MidiOutPort_SendLongMsg(*portIt, dataLen, data);
	
	// wait for data to be transferred
	// (transfer speed = 31250 bits per second)
	Sleep(dataLen * 1000 * 8 / 31250);
	
	return;
}

static void SendSyxData(const std::vector<MIDIOUT_PORT*>& outPorts, const std::vector<UINT8>& syxData)
{
	if (syxData.empty())
		return;
	
	size_t syxStart;
	size_t curPos;
	
	syxStart = (size_t)-1;
	for (curPos = 0x00; curPos < syxData.size(); curPos ++)
	{
		if (syxData[curPos] == 0xF0)
		{
			if (syxStart != (size_t)-1)
				SendSyxDataToPorts(outPorts, curPos - syxStart, &syxData[syxStart]);
			syxStart = curPos;
		}
		else if (syxData[curPos] == 0xF7)
		{
			if (syxStart != (size_t)-1)
				SendSyxDataToPorts(outPorts, curPos + 1 - syxStart, &syxData[syxStart]);
			syxStart = (size_t)-1;
		}
	}
	if (syxStart != (size_t)-1)
		SendSyxDataToPorts(outPorts, curPos - syxStart, &syxData[syxStart]);
	
	return;
}

static void MidiEventCallback(void* userData, const MidiEvent* midiEvt, UINT16 chnID)
{
	switch(midiEvt->evtType & 0xF0)
	{
	case 0x00:	// general events
		if (midiEvt->evtType == 0x01)
			vis_do_channel_event(chnID, midiEvt->evtValA, midiEvt->evtValB);
		break;
	case 0x80:
		vis_do_note(chnID, midiEvt->evtValA, 0x00);
		break;
	case 0x90:
		vis_do_note(chnID, midiEvt->evtValA, midiEvt->evtValB);
		break;
	case 0xB0:
		vis_do_ctrl_change(chnID, midiEvt->evtValA, midiEvt->evtValB);
		break;
	case 0xC0:
		vis_do_ins_change(chnID);
		break;
	case 0xF0:
		switch(midiEvt->evtType)
		{
		case 0xFF:
			if (midiEvt->evtData.empty())
				vis_print_meta(chnID, midiEvt->evtValA, 0, NULL);
			else
				vis_print_meta(chnID, midiEvt->evtValA, midiEvt->evtData.size(), (const char*)&midiEvt->evtData[0x00]);
			break;
		}
		break;
	}
	return;
}
