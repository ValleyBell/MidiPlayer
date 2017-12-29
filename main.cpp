// TODO:
//	- "sound upgrade" option
//	- send initial SYX file (for MT-32 stuff)
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <vector>
#include <string>

#ifdef _WIN32
#include <conio.h>
#include <Windows.h>
#else
#include <unistd.h>
#define Sleep(x)	usleep(x * 1000)
//#define _getch		getchar
//#define _kbhit()	false
#include "getch_linux.h"
#endif

#include <stdtype.h>
#include "MidiLib.hpp"
#include "MidiOut.h"
#include "MidiPlay.hpp"
#include "MidiInsReader.h"
#include "MidiBankScan.hpp"


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
static MidiFile CMidi;
static MidiPlayer midPlay;

static bool keepPortsOpen;
static UINT8 playerCfgFlags;	// see PlayerOpts::flags
static UINT8 forceSrcType;
static size_t forceModID;
static std::vector<InstrumentSetCfg> insSetFiles;
static std::vector<MidiModule> midiModules;
static std::vector<INS_BANK> insBanks;
static std::string syxFile;
static std::vector<UINT8> syxData;


static void LoadConfig(void);
static size_t GetOptimalModule(const BANKSCAN_RESULT* scanRes);
static const char* GetModuleTypeName(UINT8 modType);
void PlayMidi(void);
static void SendSyxDataToPorts(const std::vector<MIDIOUT_PORT*>& outPorts, size_t dataLen, const UINT8* data);
static void SendSyxData(const std::vector<MIDIOUT_PORT*>& outPorts, const std::vector<UINT8>& syxData);


int main(int argc, char* argv[])
{
	int argbase;
	UINT8 retVal;
	size_t curInsBnk;
	
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
		return 0;
	}
	
#ifndef WIN32
	tcgetattr(STDIN_FILENO, &oldterm);
	termmode = false;
#endif
	
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
			
			forceModID = strtoul(argv[argbase], NULL, 0);
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
	
	LoadConfig();
	
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
	
	printf("Opening %s ...\n", argv[argbase + 0]);
	retVal = CMidi.LoadFile(argv[argbase + 0]);
	if (retVal)
	{
		printf("Error opening %s\n", argv[argbase + 0]);
		printf("Errorcode: %02X\n", retVal);
		return retVal;
	}
	printf("File loaded.\n");
	
#ifndef WIN32
	changemode(true);
#endif
	PlayMidi();
#ifndef WIN32
	changemode(false);
#endif
	
	for (curInsBnk = 0; curInsBnk < insBanks.size(); curInsBnk ++)
		FreeInstrumentBank(&insBanks[curInsBnk]);
	insBanks.clear();
	
	return 0;
}

static void LoadConfig(void)
{
	insSetFiles.clear();
	{
		InstrumentSetCfg isc;
		isc.setType = MODULE_TYPE_GS;
		isc.pathName = std::string(INS_SET_PATH) + "gs.ins";
		insSetFiles.push_back(isc);
	}
	{
		InstrumentSetCfg isc;
		isc.setType = MODULE_TYPE_XG;
		isc.pathName = std::string(INS_SET_PATH) + "xg.ins";
		insSetFiles.push_back(isc);
	}
	{
		InstrumentSetCfg isc;
		isc.setType = MODULE_GM_2;
		isc.pathName = std::string(INS_SET_PATH) + "gml2.ins";
		insSetFiles.push_back(isc);
	}
	{
		InstrumentSetCfg isc;
		isc.setType = MODULE_MT32;
		isc.pathName = std::string(INS_SET_PATH) + "mt32.ins";
		insSetFiles.push_back(isc);
	}
	
	midiModules.clear();
	{
		MidiModule mMod;
		const UINT8 ports[] = {0, 1};	// S-YXG50 A/B
		const UINT8 playTypes[] = {MODULE_GM_1, MODULE_MU50, MODULE_MU80, MODULE_MU90, MODULE_MU100, MODULE_MU128, MODULE_MU1000};
		mMod.name = "S-YXG";
		mMod.modType = MODULE_MU50;
		mMod.ports = std::vector<UINT8>(ports, ports + sizeof(ports));
		mMod.playType = std::vector<UINT8>(playTypes, playTypes + sizeof(playTypes));
		midiModules.push_back(mMod);
	}
	{
		MidiModule mMod;
		const UINT8 ports[] = {2, 3};	// MS GS, BASSMIDI A
		const UINT8 playTypes[] = {MODULE_GM_1, MODULE_SC55};
		mMod.name = "SC-55";
		mMod.modType = MODULE_SC55;
		mMod.ports = std::vector<UINT8>(ports, ports + sizeof(ports));
		mMod.playType = std::vector<UINT8>(playTypes, playTypes + sizeof(playTypes));
		midiModules.push_back(mMod);
	}
	{
		MidiModule mMod;
		const UINT8 ports[] = {0, 3};	// S-YXG50 A/BASSMIDI A
		const UINT8 playTypes[] = {MODULE_SC55, MODULE_SC88, MODULE_SC88PRO, MODULE_SC8850};
		mMod.name = "GenericGS";
		//mMod.modType = MODULE_SC88;
		mMod.modType = MODULE_TG300B;
		mMod.ports = std::vector<UINT8>(ports, ports + sizeof(ports));
		mMod.playType = std::vector<UINT8>(playTypes, playTypes + sizeof(playTypes));
		midiModules.push_back(mMod);
	}
	{
		MidiModule mMod;
		const UINT8 ports[] = {5};	// Munt
		const UINT8 playTypes[] = {MODULE_MT32};
		mMod.name = "MT-32";
		mMod.modType = MODULE_MT32;
		mMod.ports = std::vector<UINT8>(ports, ports + sizeof(ports));
		mMod.playType = std::vector<UINT8>(playTypes, playTypes + sizeof(playTypes));
		midiModules.push_back(mMod);
	}
	
	keepPortsOpen = true;
	
	return;
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
	
	midPlay.SetOutputPorts(mOuts);
	Sleep(100);
	
	if (! syxData.empty())
	{
		printf("Sending SYX data ...\n");
		SendSyxData(mOuts, syxData);
	}
	
	midPlay.SetMidiFile(&CMidi);
	printf("Song length: %.3f s\n", midPlay.GetSongLength());
	
	midPlay.Start();
	while(midPlay.GetState() & 0x01)
	{
		midPlay.DoPlaybackStep();
		
		if (_kbhit())
		{
			int inkey = _getch();
			if (isalpha(inkey))
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
