#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <conio.h>
#include <Windows.h>
#define unlink	_unlink

#define USE_WMAIN
#else
#include <unistd.h>
#define Sleep(x)	usleep(x * 1000)
#include <limits.h>	// for PATH_MAX
#include <signal.h>	// for kill()
#endif
#include <iconv.h>

#include <INIReader.h>

#include <stdtype.h>
#include "MidiLib.hpp"
#include "MidiModules.hpp"
#include "MidiOut.h"
#include "MidiPlay.hpp"
#include "MidiInsReader.h"
#include "MidiBankScan.hpp"
#include "vis.hpp"
#include "utils.hpp"
#include "m3uargparse.hpp"


struct InstrumentSetCfg
{
	UINT8 setType;
	std::string pathName;
};


//int main(int argc, char* argv[]);
static char* GetAppFilePath(void);
static bool is_no_space(char c);
static void CfgString2Vector(const std::string& valueStr, std::vector<std::string>& valueVector);
static UINT8 LoadConfig(const std::string& cfgFile);
static const char* GetStr1or2(const char* str1, const char* str2);
static const char* GetModuleTypeNameS(UINT8 modType);
static const char* GetModuleTypeNameL(UINT8 modType);
void PlayMidi(void);
static void SendSyxDataToPorts(const std::vector<MIDIOUT_PORT*>& outPorts, size_t dataLen, const UINT8* data);
static void SendSyxData(const std::vector<MIDIOUT_PORT*>& outPorts, const std::vector<UINT8>& syxData);
static void MidiEventCallback(void* userData, const MidiEvent* midiEvt, UINT16 chnID);
static std::string GetMidiSongTitle(MidiFile* cMidi);


static const char* INS_SET_PATH = "_MidiInsSets/";
static std::string midFileName;
static MidiFile CMidi;
static MidiPlayer midPlay;

static UINT32 numLoops;
static UINT32 defNumLoops;
static UINT8 playerCfgFlags;	// see PlayerOpts::flags
static UINT8 forceSrcType;
static UINT8 forceModID;
static std::vector<InstrumentSetCfg> insSetFiles;
static MidiModuleCollection midiModColl;
static std::vector<INS_BANK> insBanks;
static std::string syxFile;
static std::vector<UINT8> syxData;

static std::vector<SongFileList> songList;
static std::vector<std::string> plList;
static int controlVal;
static size_t curSong;

extern std::vector<UINT8> optShowMeta;
extern UINT8 optShowInsChange;
static std::string defCodepages[2];

static std::vector<std::string> appSearchPaths;
static std::string cfgBasePath;
static std::string strmSrv_metaFile;
static std::string strmSrv_pidFile;
static int strmSrv_PIDopt;
static int strmSrv_curPID;

static iconv_t hCurIConv[2];

static BANKSCAN_RESULT scanRes;
static size_t modIDOpen;
static MidiOutPortList* mopList = NULL;


#ifdef USE_WMAIN
int wmain(int argc, wchar_t* wargv[])
{
	char** argv;
#else
int main(int argc, char* argv[])
{
#endif
	int argbase;
	UINT8 retVal;
	int resVal;
	size_t curInsBnk;
	UINT8 curCP;
	size_t initSongID;
	
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
		printf("    -l n - play looping songs n times (default: 2) \n");
		printf("    -S n - begin playing at the n-th file\n");
#ifndef _WIN32
		printf("    -I   - Ices2 PID (for Metadata refresh)\n");
#endif
		return 0;
	}
	
#ifdef USE_WMAIN
	argv = (char**)malloc(argc * sizeof(char*));
	for (argbase = 0; argbase < argc; argbase ++)
	{
		int bufSize = WideCharToMultiByte(CP_UTF8, 0, wargv[argbase], -1, NULL, 0, NULL, NULL);
		argv[argbase] = (char*)malloc(bufSize);
		WideCharToMultiByte(CP_UTF8, 0, wargv[argbase], -1, argv[argbase], bufSize, NULL, NULL);
	}
#endif
	
	playerCfgFlags = PLROPTS_RESET /*| PLROPTS_STRICT | PLROPTS_ENABLE_CTF*/;
	numLoops = 0;
	forceSrcType = 0xFF;
	forceModID = 0xFF;
	syxFile = "";
	optShowMeta.resize(9, 1);
	optShowInsChange = 1;
	defCodepages[0] = "";
	defCodepages[1] = "";
	strmSrv_PIDopt = 0;
	initSongID = 0;
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		char optChr = argv[argbase][1];
		
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
		else if (optChr == 'l')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			numLoops = (UINT32)strtoul(argv[argbase], NULL, 0);
		}
		else if (optChr == 'x')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			syxFile = argv[argbase];
		}
		else if (optChr == 'I')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			strmSrv_PIDopt = (int)strtol(argv[argbase], NULL, 0);
		}
		else if (optChr == 'S')
		{
			argbase ++;
			if (argbase >= argc)
				break;
			
			initSongID = (size_t)strtol(argv[argbase], NULL, 0) - 1;
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
	
	appSearchPaths.clear();
	{
		char* appPath;
		const char* appTitle;
		
		// 1. actual application path (potentially resolved symlink)
		appPath = GetAppFilePath();
		appTitle = GetFileTitle(appPath);
		if (appTitle != appPath)
			appSearchPaths.push_back(std::string(appPath, appTitle - appPath));
		free(appPath);
		
		// 2. called path
		appPath = argv[0];
		appTitle = GetFileTitle(argv[0]);
		if (appTitle != appPath)
			appSearchPaths.push_back(std::string(argv[0], appTitle - argv[0]));
	}
	// 3. current directory
	appSearchPaths.push_back("./");
	
	std::string cfgFilePath = FindFile_Single("config.ini", appSearchPaths);
	if (cfgFilePath.empty())
	{
		printf("config.ini not found!\n");
		return 1;
	}
	
	{
		const char* startPtr = cfgFilePath.c_str();
		const char* endPtr = GetFileTitle(startPtr);
		cfgBasePath = std::string(startPtr, endPtr - startPtr);
	}
	printf("Config Path: %s\n", cfgFilePath.c_str());
	printf("Config Base: %s\n", cfgBasePath.c_str());
	
	retVal = LoadConfig(cfgFilePath);
	if (retVal)
	{
		printf("Error: No modules defined!\n");
		return 0;
	}
	
	retVal = ParseSongFiles(std::vector<const char*>(argv + argbase, argv + argc), songList, plList);
	if (retVal)
		printf("One or more playlists couldn't be read!\n");
	if (songList.empty())
	{
		printf("No songs to play.\n");
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
	
	if (defCodepages[0].empty())
		defCodepages[0] = "CP1252";
	for (curCP = 0; curCP < 2; curCP ++)
	{
		// Printing is always done in UTF-8.
		if (defCodepages[curCP].empty())
			hCurIConv[curCP] = NULL;
		else
			hCurIConv[curCP] = iconv_open("UTF-8", defCodepages[curCP].c_str());
	}
	vis_init();
	vis_set_locales(2, hCurIConv);
	vis_set_midi_modules(&midiModColl);
	
	resVal = 0;
	controlVal = +1;	// default: next song
	for (curSong = initSongID; curSong < songList.size(); )
	{
		FILE* hFile;
		
		midFileName = songList[curSong].fileName;
		//printf("Opening %s ...\n", midFileName.c_str());
#ifdef WIN32
		std::wstring fileNameW;
		fileNameW.resize(MultiByteToWideChar(CP_UTF8, 0, midFileName.c_str(), -1, NULL, 0) - 1);
		MultiByteToWideChar(CP_UTF8, 0, midFileName.c_str(), -1, &fileNameW[0], fileNameW.size() + 1);
		hFile = _wfopen(fileNameW.c_str(), L"rb");
#else
		hFile = fopen(midFileName.c_str(), "rb");
#endif
		if (hFile == NULL)
		{
			retVal = 0xFF;
		}
		else
		{
			retVal = CMidi.LoadFile(hFile);
			fclose(hFile);
		}
		if (retVal)
		{
			vis_printf("Error opening %s\n", midFileName.c_str());
			vis_printf("Errorcode: %02X\n", retVal);
			vis_update();
			resVal = 1;
			if (controlVal == -1 && curSong == 0)
				controlVal = +1;
			curSong += controlVal;
			vis_getch_wait();
			continue;
		}
		//printf("File loaded.\n");
		
		PlayMidi();
		
		// done in PlayMidi
		//CMidi.ClearAll();
		if (controlVal == +1)
		{
			curSong ++;
		}
		else if (controlVal == -1)
		{
			if (curSong > 0)
				curSong --;
		}
		else if (controlVal == +9)
		{
			break;
		}
	}
	//if (resVal)
	//	vis_getch_wait();
	vis_deinit();
	for (curCP = 0; curCP < 2; curCP ++)
	{
		if (hCurIConv[curCP] != NULL)
			iconv_close(hCurIConv[curCP]);
	}
	if (! strmSrv_metaFile.empty())
		unlink(strmSrv_metaFile.c_str());
	
	for (curInsBnk = 0; curInsBnk < insBanks.size(); curInsBnk ++)
		FreeInstrumentBank(&insBanks[curInsBnk]);
	insBanks.clear();
	
	return 0;
}

static char* GetAppFilePath(void)
{
	char* appPath;
	int retVal;
	
#ifdef _WIN32
#ifdef USE_WMAIN
	wchar_t* appPathW;
	
	appPathW = (wchar_t*)malloc(MAX_PATH * sizeof(wchar_t));
	retVal = GetModuleFileNameW(NULL, appPathW, MAX_PATH);
	if (! retVal)
		appPathW[0] = L'\0';
	
	retVal = WideCharToMultiByte(CP_UTF8, 0, appPathW, -1, NULL, 0, NULL, NULL);
	if (retVal < 0)
		retVal = 1;
	appPath = (char*)malloc(retVal);
	retVal = WideCharToMultiByte(CP_UTF8, 0, appPathW, -1, appPath, retVal, NULL, NULL);
	if (retVal < 0)
		appPath[0] = '\0';
	free(appPathW);
#else
	appPath = (char*)malloc(MAX_PATH * sizeof(char));
	retVal = GetModuleFileNameA(NULL, appPath, MAX_PATH);
	if (! retVal)
		appPath[0] = '\0';
#endif
#else
	appPath = (char*)malloc(PATH_MAX * sizeof(char));
	retVal = readlink("/proc/self/exe", appPath, PATH_MAX);
	if (retVal == -1)
		appPath[0] = '\0';
#endif
	
	return appPath;
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

static UINT8 LoadConfig(const std::string& cfgFile)
{
	std::map<std::string, UINT8> INSSET_NAME_MAP;
	std::string insSetPath;
	std::map<std::string, UINT8>::const_iterator nmIt;
	std::vector<std::string> modList;
	std::vector<std::string>::const_iterator mlIt;
	
	INSSET_NAME_MAP["GM"] = MODULE_GM_1;
	INSSET_NAME_MAP["GM_L2"] = MODULE_GM_2;
	INSSET_NAME_MAP["GS"] = MODULE_TYPE_GS;
	INSSET_NAME_MAP["YGS"] = MODULE_TG300B;
	INSSET_NAME_MAP["XG"] = MODULE_TYPE_XG;
	INSSET_NAME_MAP["MT-32"] = MODULE_MT32;
	
	INIReader iniFile(cfgFile);
	if (iniFile.ParseError())
	{
		printf("Error reading %s!\n", cfgFile.c_str());
		return 0xFF;
	}
	
	midiModColl._keepPortsOpen = iniFile.GetBoolean("General", "KeepPortsOpen", false);
	defNumLoops = iniFile.GetInteger("General", "LoopCount", 2);
	
	strmSrv_pidFile = iniFile.Get("StreamServer", "PIDFile", "");
	strmSrv_metaFile = iniFile.Get("StreamServer", "MetadataFile", "");
	
	optShowInsChange = iniFile.GetBoolean("Display", "ShowInsChange", true);
	optShowMeta[1] = iniFile.GetBoolean("Display", "ShowMetaText", true);
	optShowMeta[6] = iniFile.GetBoolean("Display", "ShowMetaMarker", true);
	optShowMeta[0] = iniFile.GetBoolean("Display", "ShowMetaOther", true);
	
	defCodepages[0] = iniFile.Get("Display", "DefaultCodepage", "");
	defCodepages[1] = iniFile.Get("Display", "FallbackCodepage", "");
	
	insSetFiles.clear();
	insSetPath = iniFile.Get("InstrumentSets", "DataPath", INS_SET_PATH);
	insSetPath = CombinePaths(cfgBasePath, insSetPath);
	for (nmIt = INSSET_NAME_MAP.begin(); nmIt != INSSET_NAME_MAP.end(); ++nmIt)
	{
		std::string fileName = iniFile.Get("InstrumentSets", nmIt->first, "");
		if (! fileName.empty())
		{
			InstrumentSetCfg isc;
			isc.setType = nmIt->second;
			isc.pathName = CombinePaths(insSetPath, fileName);
			insSetFiles.push_back(isc);
		}
	}
	
	midiModColl.ClearModules();
	CfgString2Vector(iniFile.Get("General", "Modules", ""), modList);
	for (mlIt = modList.begin(); mlIt != modList.end(); ++mlIt)
	{
		UINT8 modType;
		std::vector<std::string> list;
		
		if (! GetIDFromNameOrNumber(iniFile.Get(*mlIt, "ModType", ""), midiModColl.GetShortModNameLUT(), modType))
			continue;
		
		MidiModule& mMod = midiModColl.AddModule(*mlIt, modType);
		
		CfgString2Vector(iniFile.Get(mMod.name, "Ports", ""), list);
		mMod.SetPortList(list);
		CfgString2Vector(iniFile.Get(mMod.name, "PlayTypes", ""), list);
		mMod.SetPlayTypes(list, midiModColl.GetShortModNameLUT());
	}
	
	return 0x00;
}

static const char* GetStr1or2(const char* str1, const char* str2)
{
	return (str1 == NULL || str1[0] == '\0') ? str2 : str1;
}

static const char* GetModuleTypeNameS(UINT8 modType)
{
	const char* unkStr = "unknown";
	if (MMASK_TYPE(modType) == MODULE_TYPE_GS)
		unkStr = "GS/unk";
	else if (MMASK_TYPE(modType) == MODULE_TYPE_XG)
		unkStr = "XG/unk";
	return GetStr1or2(midiModColl.GetShortModName(modType).c_str(), unkStr);
}

static const char* GetModuleTypeNameL(UINT8 modType)
{
	const char* unkStr = "unknown";
	if (MMASK_TYPE(modType) == MODULE_TYPE_GS)
		unkStr = "GS/unknown";
	else if (MMASK_TYPE(modType) == MODULE_TYPE_XG)
		unkStr = "XG/unknown";
	return GetStr1or2(midiModColl.GetLongModName(modType).c_str(), unkStr);
}

size_t main_GetOpenedModule(void)
{
	return modIDOpen;
}

UINT8 main_CloseModule(void)
{
	UINT8 retVal;
	
	retVal = midiModColl.ClosePorts(mopList);
	mopList = NULL;
	
	return retVal;
}

UINT8 main_OpenModule(size_t modID)
{
	if (mopList != NULL)
		return 0xC0;	// already open
	
	UINT8 retVal;
	
	retVal = midiModColl.OpenModulePorts(modID, scanRes.numPorts, &mopList);
	if (retVal && mopList->mOuts.empty())
	{
		vis_addstr("Error opening MIDI ports!");
		return retVal;
	}
	modIDOpen = modID;
	midPlay.SetOutputPorts(mopList->mOuts);
	
	if ((retVal & 0xF0) == 0x10)
		vis_addstr("Warning: The module doesn't have enough ports defined for proper playback!");
	if ((retVal & 0x0F) == 0x01)
		vis_addstr("Warning: One or more ports could not be opened!");
	
	return retVal;
}


void PlayMidi(void)
{
	PlayerOpts plrOpts;
	UINT8 retVal;
	size_t chosenModule;
	MidiModule* mMod;
	
	// try to detect the instrument set used by the MIDI
	MidiBankScan(&CMidi, true, &scanRes);
	if (forceSrcType != 0xFF)
		scanRes.modType = forceSrcType;
	
	if (scanRes.modType == 0xFF)
	{
		scanRes.modType = MODULE_GM_1;
		vis_printf("Falling back to %s mode ...\n", GetModuleTypeNameL(scanRes.modType));
	}
	
	if (forceModID == 0xFF)
		chosenModule = midiModColl.GetOptimalModuleID(scanRes.modType);
	else
		chosenModule = forceModID;
	if (chosenModule == (size_t)-1 || chosenModule >= midiModColl.GetModuleCount())
	{
		vis_printf("Unable to find an appropriate MIDI module!\n");
		vis_getch_wait();
		return;
	}
	mMod = midiModColl.GetModule(chosenModule);
	vis_printf("Using module %s.\n", mMod->name.c_str());
	
	vis_addstr("Opening MIDI ports ...");
	vis_update();
	retVal = main_OpenModule(chosenModule);
	if (retVal && mopList->mOuts.empty())
	{
		vis_getch_wait();
		return;
	}
	vis_update();
	
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
	{
		bool resetOff = true;
		if (MMASK_TYPE(scanRes.hasReset) == MODULE_TYPE_GM && MMASK_TYPE(mMod->modType) != MODULE_TYPE_GM)
			resetOff = false;	// disable GM reset
		else if (MMASK_TYPE(scanRes.hasReset) != MMASK_TYPE(mMod->modType))
			resetOff = false;	// enforce manual reset when MIDI and device types differ
		else if (MMASK_TYPE(mMod->modType) == MODULE_TYPE_GS && mMod->modType >= MODULE_SC88)
		{
			if (! (scanRes.details.fmGS & (1 << FMBGS_SC_RESET)))
				resetOff = false;	// enforce SC-88 reset when missing
		}
		if (resetOff)
			plrOpts.flags &= ~PLROPTS_RESET;
	}
	midPlay.SetOptions(plrOpts);
	midPlay._numLoops = numLoops ? numLoops : defNumLoops;
	
	midPlay.SetMidiFile(&CMidi);
	if (songList.size() > 1)
	{
		vis_set_track_number(1 + curSong);
		vis_set_track_count(songList.size());
	}
	vis_set_midi_file(midFileName.c_str(), &CMidi);
	vis_set_midi_player(&midPlay);
	vis_printf("Song length: %.3f s\n", midPlay.GetSongLength());
	
	if (! strmSrv_metaFile.empty())
	{
		FILE* hFile;
		
		if (strmSrv_PIDopt)
		{
			strmSrv_curPID = strmSrv_PIDopt;
		}
		else
		{
			strmSrv_curPID = 0;
			hFile = fopen(strmSrv_pidFile.c_str(), "rt");
			if (hFile != NULL)
			{
				fscanf(hFile, "%d", &strmSrv_curPID);
				fclose(hFile);
			}
		}
		
		hFile = fopen(strmSrv_metaFile.c_str(), "wt");
		if (hFile != NULL)
		{
			const char* fileTitle;
			std::string songTitle;
			
			fileTitle = GetFileTitle(midFileName.c_str());
			songTitle = GetMidiSongTitle(&CMidi);
			fprintf(hFile, "TITLE=%s", fileTitle);
			if (1)
			{
				if (scanRes.numPorts <= 1)
					fprintf(hFile, " [%s]", GetModuleTypeNameS(scanRes.modType));
				else
					fprintf(hFile, " [%s x%u]", GetModuleTypeNameS(scanRes.modType), scanRes.numPorts);
			}
			if (! songTitle.empty())
				fprintf(hFile, ": %s", songTitle.c_str());
			fputc('\n', hFile);
			fclose(hFile);
			
#ifndef _WIN32
			if (strmSrv_curPID)
			{
				int retValI = kill(strmSrv_curPID, SIGUSR1);
				if (retValI)
					vis_printf("Unable to send signal to stream server!! (PID %d)\n", strmSrv_curPID);
			}
#endif
		}
	}
	
	midPlay.SetEventCallback(&MidiEventCallback, &midPlay);
	vis_new_song();
	
	midPlay.Start();
	if (! syxData.empty())
	{
		vis_addstr("Sending SYX data ...");
		vis_update();
		midPlay.Pause();	// do pause/resume to fix initial timing
		SendSyxData(mopList->mOuts, syxData);
		midPlay.Resume();
	}
	vis_update();
	
	controlVal = vis_main();
	midPlay.Stop();
	
	vis_addstr("Cleaning ...");
	vis_update();
	CMidi.ClearAll();
	vis_addstr("Done.");
	vis_update();
	
	main_CloseModule();
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

static std::string GetMidiSongTitle(MidiFile* cMidi)
{
	if (cMidi->GetTrackCount() == 0)
		return "";
	
	MidiTrack* mTrk = cMidi->GetTrack(0);
	midevt_iterator evtIt;
	
	for (evtIt = mTrk->GetEventBegin(); evtIt != mTrk->GetEventEnd(); ++evtIt)
	{
		if (evtIt->tick > cMidi->GetMidiResolution())
			break;
		
		if (evtIt->evtType == 0xFF && evtIt->evtValA == 0x03)	// FF 03 - Sequence Name
		{
			std::string evtText = Vector2String(evtIt->evtData);
			std::string convText;
			char retVal;
			UINT8 curCP;
			
			for (curCP = 0; curCP < 2; curCP ++)
			{
				retVal = StrCharsetConv(hCurIConv[curCP], convText, evtText);
				if (! (retVal & 0x80))
					return convText;
			}
			// unable to convert - just return original text for now
			return evtText;
		}
	}
	
	return "";
}
