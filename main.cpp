#include <stdio.h>
#include <stdlib.h>
#include <string.h>	// for memcmp()
#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <algorithm>

#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif

#ifdef _WIN32
#include <conio.h>
#include <Windows.h>

#define USE_WMAIN
#else
#include <unistd.h>
#define Sleep(x)	usleep(x * 1000)
#include <limits.h>	// for PATH_MAX
#include <signal.h>	// for kill()
#endif
#include <iconv.h>

#if ENABLE_ZIP_SUPPORT
#include "unzip.h"
#endif

#include "INIReader.hpp"

#include <stdtype.h>
#include "MidiLib.hpp"
#include "MidiPortAliases.hpp"
#include "MidiModules.hpp"
#include "MidiOut.h"
#include "MidiPlay.hpp"
#include "MidiInsReader.h"
#include "MidiBankScan.hpp"
#include "vis.hpp"
#include "utils.hpp"
#include "m3uargparse.hpp"
#include "RCPLoader.hpp"


struct InstrumentSetCfg
{
	UINT8 setType;
	std::vector<std::string> pathNames;
};


//int main(int argc, char* argv[]);
static char* GetAppFilePath(void);
#if ENABLE_ZIP_SUPPORT
static std::string DecompressFromZIP(const std::string& path);
#endif
static bool is_no_space(char c);
static void CfgString2Vector(const std::string& valueStr, std::vector<std::string>& valueVector);
static size_t GetMidiPortList(const std::vector<std::string>& portStrList, std::vector<UINT32>& portList);
static void Vector_Str2UInt32(const std::vector<std::string>& strList, std::vector<UINT32>& intList);
static UINT16 ParseChnMask(const std::string& maskStr);
static void ParseChnMaskList(const std::vector<std::string>& maskStrList, std::vector<UINT16>& maskList);
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

static bool dummyOutput;
static UINT32 numLoops;
static UINT32 defNumLoops;
static double fadeTime;
static UINT8 playerCfgFlags;	// see PlayerOpts::flags
static std::string plrLoopText[2];
static UINT8 forceSrcType;
static UINT8 forceModID;
static std::vector<InstrumentSetCfg> insSetFiles;
static MidiPortAliases midiPortAliases;
static MidiModuleCollection midiModColl;
static std::vector<INS_BANK> insBanks;
static std::string syxFile;
static std::vector<UINT8> syxData;
static bool didSendSyx;
static UINT8 tempSrcType;

static std::vector<SongFileList> songList;
static std::vector<std::string> plList;
static int controlVal;
static size_t curSong;
static UINT8 songInsMap;	// detected instrument map
static size_t songOptDev;	// optimal device

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

#if ENABLE_ZIP_SUPPORT
static std::string lastUnzFN;
static ZIP_FILE lastZipFile;
#endif


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
	setlocale(LC_NUMERIC, "C");	// enforce decimal dot
	
	printf("MIDI Player\n");
	printf("-----------\n");
	if (argc < 2)
	{
		printf("Usage: MidiPlayer.exe [options] file.mid\n");
		printf("Options:\n");
		printf("    -L   - list all MIDI devices and quit\n");
		printf("    -D   - dummy MIDI output\n");
		printf("    -o n - set option bitmask (default: 0x01)\n");
		printf("           Bit 0 (0x01) - send GM/GS/XG reset, if missing\n");
		printf("           Bit 1 (0x02) - strict mode (enforce GS instrument map)\n");
		printf("           Bit 2 (0x04) - enable Capital Tone Fallback (SC-55 style)\n");
		printf("    -s n - enforce source type\n");
		printf("           0x%02X = GM, 0x%02X..0x%02X = SC-55/88/88Pro/8850,\n",
			MODULE_GM_1, MODULE_SC55, MODULE_SC8850);
		printf("           0x%02X..%02X = MU50/80/90/100/128/1000, 0x%02X = MT-32\n",
			MODULE_MU50, MODULE_MU1000, MODULE_MT32);
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
	plrLoopText[0] = "loopStart";
	plrLoopText[1] = "loopEnd";
	dummyOutput = false;
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
		else if (optChr == 'L')
		{
			MIDI_PORT_LIST mpList;
			MIDI_PORT_DESC* mpDesc;
			UINT32 curPort;
			
			retVal = MidiOut_GetPortList(&mpList);
			if (retVal)
				return 1;
			for (curPort = 0; curPort < mpList.count; curPort ++)
			{
				mpDesc = &mpList.ports[curPort];
				printf("Port %u - ID: %d, Name: \"%s\"\n", curPort, mpDesc->id, mpDesc->name);
			}
			MidiOut_FreePortList(&mpList);
			return 0;
		}
		else if (optChr == 'D')
		{
			dummyOutput = true;
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
		size_t curFile;
		
		retVal = LoadInstrumentList(tmpInsSet->pathNames[0].c_str(), insBank);
		if (retVal)
		{
			printf("InsSet %s Load: 0x%02X\n", tmpInsSet->pathNames[0].c_str(), retVal);
			continue;
		}
		
		for (curFile = 1; curFile < tmpInsSet->pathNames.size(); curFile ++)
		{
			INS_BANK tmpBank;
			
			retVal = LoadInstrumentList(tmpInsSet->pathNames[curFile].c_str(), &tmpBank);
			if (retVal)
			{
				printf("InsSet %s Load: 0x%02X\n", tmpInsSet->pathNames[curFile].c_str(), retVal);
				continue;
			}
			MergeInstrumentBanks(insBank, &tmpBank);
			FreeInstrumentBank(&tmpBank);
		}
		
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
	didSendSyx = false;
	
	if (! syxFile.empty())
	{
		FILE* hFile = fopen(syxFile.c_str(), "rb");
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
	
	resVal = 0;
	controlVal = +1;	// default: next song
	for (curSong = initSongID; curSong < songList.size(); )
	{
		FILE* hFile;
#if ENABLE_ZIP_SUPPORT
		std::string zippedFName;
#endif
		std::vector<std::string> initFiles;
		
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
#if ENABLE_ZIP_SUPPORT
		if (hFile == NULL)
		{
			zippedFName = DecompressFromZIP(midFileName);
			if (! zippedFName.empty())
				hFile = fopen(zippedFName.c_str(), "rb");
		}
#endif
		if (hFile == NULL)
		{
			retVal = 0xFF;
		}
		else
		{
			retVal = CMidi.LoadFile(hFile);
			if (retVal >= 0x10)
			{
				char fileSig[4];
				rewind(hFile);
				fread(fileSig, 1, 4, hFile);
				if (! memcmp(fileSig, "RIFF", 4))	// .rmi file?
				{
					fseek(hFile, 0x14, SEEK_SET);	// attempt to seek over to actual MIDI data
					retVal = CMidi.LoadFile(hFile);	// and try reading again from there
				}
			}
			if (retVal >= 0x10)
			{
				rewind(hFile);
				retVal = LoadRCPAsMidi(hFile, CMidi, initFiles);
				vis_update();
				//vis_getch_wait();
			}
			fclose(hFile);
		}
#if ENABLE_ZIP_SUPPORT
		if (! zippedFName.empty())
			remove(zippedFName.c_str());
#endif
		if (retVal)
		{
			vis_printf("Error 0x%02X opening %s\n", retVal, midFileName.c_str());
			vis_update();
			resVal = 1;
			if (controlVal == -1 && curSong == 0)
				controlVal = +1;
			curSong += controlVal;
			vis_getch_wait();
			continue;
		}
		//printf("File loaded.\n");
		tempSrcType = 0xFF;
		
		if (! initFiles.empty())
		{
			UINT8 iRetVal;
			
			const char* basePtr = midFileName.c_str();
			const char* endPtr = GetFileTitle(basePtr);
			std::string initFPath = std::string(basePtr, endPtr) + initFiles[0];
			iRetVal = Cm62Syx(initFPath.c_str(), syxData);
			if (! iRetVal)
			{
				didSendSyx = false;
				tempSrcType = MODULE_MT32;
			}
			else
			{
				vis_printf("Error 0x%02X opening %s\n", retVal, initFPath.c_str());
				vis_update();
				vis_getch_wait();
			}
		}
		
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
#if ENABLE_ZIP_SUPPORT
	if (! lastUnzFN.empty())
		ZIP_Unload(&lastZipFile);
#endif
	if (! strmSrv_metaFile.empty())
		remove(strmSrv_metaFile.c_str());
	
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

#if ENABLE_ZIP_SUPPORT
static std::string DecompressFromZIP(const std::string& path)
{
	std::string normPath;	// path with normalized dir separators
	size_t pathSepPos;
	std::string zipPath;
	FILE* hFileZip;
	UINT8 retVal;
	
	normPath = path;
#ifdef _WIN32
	size_t curChr;
	for (curChr = 0; curChr < normPath.size(); curChr ++)
	{
		if (normPath[curChr] == '\\')
			normPath[curChr] = '/';
	}
#endif
	
	pathSepPos = normPath.rfind('/');
	hFileZip = NULL;
	while(pathSepPos != std::string::npos)
	{
		zipPath = path.substr(0, pathSepPos);
		hFileZip = fopen(zipPath.c_str(), "rb");
		if (hFileZip != NULL)
			break;
		if (pathSepPos > 0)
			pathSepPos = normPath.rfind('/', pathSepPos - 1);
		else
			pathSepPos = std::string::npos;
	}
	if (hFileZip == NULL)
	{
		vis_printf("No ZIP found!\n");
		return std::string();
	}
	if (lastUnzFN != zipPath)
	{
		if (! lastUnzFN.empty())
			ZIP_Unload(&lastZipFile);
		retVal = ZIP_LoadFromFile(hFileZip, &lastZipFile);
		if (retVal)
		{
			fclose(hFileZip);
			lastUnzFN.clear();
			vis_printf("Error reading ZIP file: %s\n", zipPath.c_str());
			return std::string();
		}
		lastUnzFN = zipPath;
	}
	std::string tempFilePath;
#ifdef _WIN32
	tempFilePath = getenv("TEMP");
#else
	tempFilePath = "/tmp";
#endif
	tempFilePath = tempFilePath + '/' + "_ztemp.mid";
	vis_printf("Extracting ZIPed file to %s\n", tempFilePath.c_str());
	vis_update();
	
	std::string packedFName = path.substr(pathSepPos + 1);
	UINT64 curEnt;
	const ZIP_DIR_ENTRY* zde = NULL;
	for (curEnt = 0; curEnt < lastZipFile.eocd.totalEntries; curEnt ++)
	{
		zde = &lastZipFile.entries[curEnt];
		if (! stricmp(zde->filename, packedFName.c_str()))
			break;
	}
	if (zde == NULL)
	{
		fclose(hFileZip);
		vis_printf("Error getting file from ZIP: %s\n", packedFName.c_str());
		return std::string();
	}
	
	FILE* hFileOut = fopen(tempFilePath.c_str(), "wb");
	if (hFileOut == NULL)
	{
		fclose(hFileZip);
		vis_printf("Unable to write to temp file: %s\n", tempFilePath.c_str());
		return std::string();
	}
	
	retVal = ZIP_ExtractToFile(hFileZip, zde, hFileOut);
	
	fclose(hFileZip);
	fclose(hFileOut);
	if (retVal >= 0x80)
	{
		vis_printf("ZIP decompression error %02X while extracting %s\n", retVal, tempFilePath.c_str());
		remove(tempFilePath.c_str());
		return std::string();
	}
	
	return tempFilePath;
}
#endif	// ENABLE_ZIP_SUPPORT

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

static size_t GetMidiPortList(const std::vector<std::string>& portStrList, std::vector<UINT32>& portList)
{
	std::vector<std::string>::const_iterator portIt;
	std::map<std::string, UINT32>::const_iterator paIt;
	UINT32 port;
	char* endStr;
	const std::map<std::string, UINT32>& portAliases = midiPortAliases.GetAliases();
	size_t validPorts;
	
	portList.clear();
	validPorts = 0;
	for (portIt = portStrList.begin(); portIt != portStrList.end(); ++portIt)
	{
		port = (UINT32)-1;
		
		if (port == (UINT32)-1)
		{
			paIt = portAliases.find(*portIt);
			if (paIt != portAliases.end())
				port = paIt->second;
		}
		// TODO: maybe accept "internal hardware ID" in some way?
		if (port == (UINT32)-1)
		{
			unsigned long pVal = strtoul(portIt->c_str(), &endStr, 0);
			if (endStr != portIt->c_str())
				port = (UINT32)pVal;
		}
		
		if (port != (UINT32)-1)
			validPorts ++;
		else
			printf("Unknown MIDI port: %s\n", portIt->c_str());
		portList.push_back(port);
	}
	
	return validPorts;
}

static void Vector_Str2UInt32(const std::vector<std::string>& strList, std::vector<UINT32>& intList)
{
	std::vector<std::string>::const_iterator slIt;
	char* endStr;
	
	intList.clear();
	for (slIt = strList.begin(); slIt != strList.end(); ++slIt)
	{
		unsigned long val = strtoul(slIt->c_str(), &endStr, 0);
		intList.push_back((UINT32)val);
	}
	
	return;
}

static UINT16 ParseChnMask(const std::string& maskStr)
{
	if (maskStr.empty())
		return 0xFFFF;	// empty - default to "all channels"
	std::string prefix = maskStr.substr(0, 2);
	if (prefix.length() >= 2)
		prefix[1] = (char)tolower((UINT8)prefix[1]);
	if (prefix == "0x")
	{
		unsigned long val = strtoul(maskStr.c_str(), NULL, 0);
		return (UINT16)val;
	}
	else if (isdigit((unsigned char)maskStr[0]))
	{
		// 1-base channel range
		char* endStr;
		unsigned long chn1 = strtoul(maskStr.c_str(), &endStr, 0);
		UINT16 mask = 0x0000;
		if (*endStr != '-')
		{
			// single channel ID
			mask = 1 << (chn1 - 1);
		}
		else
		{
			// range: 1-16
			unsigned long chn2 = strtoul(&endStr[1], NULL, 0);
			if (chn1 <= chn2)
			{
				mask = (1 << (chn2 - chn1 + 1)) - 1;
				mask <<= (chn1 - 1);
			}
			else
			{
				// range overflow: 15-3, results in 15,16,1,2,3
				UINT16 mask1 = ~((1 << (chn1 - 1)) - 1);	// mask for chn1..15
				UINT16 mask2 = (1 << chn2) - 1;	// mask for 1..chn2
				mask = mask1 | mask2;
			}
		}
		return mask;
	}
	else
	{
		return 0x0000;	// invalid
	}
}

static void ParseChnMaskList(const std::vector<std::string>& maskStrList, std::vector<UINT16>& maskList)
{
	std::vector<std::string>::const_iterator cmlIt;
	
	maskList.clear();
	for (cmlIt = maskStrList.begin(); cmlIt != maskStrList.end(); ++cmlIt)
		maskList.push_back(ParseChnMask(*cmlIt));
	
	return;
}

static UINT8 LoadConfig(const std::string& cfgFile)
{
	std::map<std::string, UINT8> INSSET_NAME_MAP;
	std::string insSetPath;
	std::map<std::string, UINT8>::const_iterator nmIt;
	std::vector<std::string> devList;
	std::vector<std::string> modList;
	std::vector<std::string>::const_iterator listIt;
	MIDI_PORT_LIST mpList;
	UINT8 retVal;
	size_t insSetXG;
	size_t insSetPLG;
	
	INSSET_NAME_MAP["GM"] = MODULE_GM_1;
	INSSET_NAME_MAP["GM_L2"] = MODULE_GM_2;
	INSSET_NAME_MAP["GS"] = MODULE_TYPE_GS;
	INSSET_NAME_MAP["YGS"] = MODULE_TG300B;
	INSSET_NAME_MAP["XG"] = MODULE_TYPE_XG;
	INSSET_NAME_MAP["XG-PLG"] = MODULE_TYPE_XG | 0x08;
	INSSET_NAME_MAP["MT-32"] = MODULE_MT32;
	
	INIReader iniFile;
	if (iniFile.ReadFile(cfgFile))
	{
		printf("Error reading %s!\n", cfgFile.c_str());
		return 0xFF;
	}
	
	midiModColl._keepPortsOpen = iniFile.GetBoolean("General", "KeepPortsOpen", false);
	defNumLoops = iniFile.GetInteger("General", "LoopCount", 2);
	fadeTime = iniFile.GetFloat("General", "FadeTime", 5.0);
	plrLoopText[0] = iniFile.GetString("General", "Maker_LoopStart", plrLoopText[0]);
	plrLoopText[1] = iniFile.GetString("General", "Maker_LoopEnd", plrLoopText[1]);
	
	strmSrv_pidFile = iniFile.GetString("StreamServer", "PIDFile", "");
	strmSrv_metaFile = iniFile.GetString("StreamServer", "MetadataFile", "");
	
	optShowInsChange = iniFile.GetBoolean("Display", "ShowInsChange", true);
	optShowMeta[1] = iniFile.GetBoolean("Display", "ShowMetaText", true);
	optShowMeta[6] = iniFile.GetBoolean("Display", "ShowMetaMarker", true);
	optShowMeta[0] = iniFile.GetBoolean("Display", "ShowMetaOther", true);
	
	defCodepages[0] = iniFile.GetString("Display", "DefaultCodepage", "");
	defCodepages[1] = iniFile.GetString("Display", "FallbackCodepage", "");
	
	insSetFiles.clear();
	insSetXG = (size_t)-1;
	insSetPLG = (size_t)-1;
	insSetPath = iniFile.GetString("InstrumentSets", "DataPath", INS_SET_PATH);
	insSetPath = CombinePaths(cfgBasePath, insSetPath);
	for (nmIt = INSSET_NAME_MAP.begin(); nmIt != INSSET_NAME_MAP.end(); ++nmIt)
	{
		std::string fileName = iniFile.GetString("InstrumentSets", nmIt->first, "");
		if (! fileName.empty())
		{
			InstrumentSetCfg isc;
			isc.setType = nmIt->second;
			if (isc.setType == MODULE_TYPE_XG)
				insSetXG = insSetFiles.size();
			else if (isc.setType == (MODULE_TYPE_XG | 0x08))
				insSetPLG = insSetFiles.size();
			isc.pathNames.push_back(CombinePaths(insSetPath, fileName));
			insSetFiles.push_back(isc);
		}
	}
	if (insSetXG != (size_t)-1 && insSetPLG != (size_t)-1)
	{
		insSetFiles[insSetXG].pathNames.push_back(insSetFiles[insSetPLG].pathNames[0]);
		insSetFiles.erase(insSetFiles.begin() + insSetPLG);
	}
	
	midiPortAliases.ClearAliases();
	retVal = MidiOut_GetPortList(&mpList);
	if (! retVal)
	{
		midiPortAliases.LoadPortList(mpList);
		MidiOut_FreePortList(&mpList);
	}
	
	devList = iniFile.GetKeys("Devices");
	for (listIt = devList.begin(); listIt != devList.end(); ++listIt)
		midiPortAliases.AddAlias(*listIt, iniFile.GetString("Devices", *listIt, ""));
	
	midiModColl.ClearModules();
	CfgString2Vector(iniFile.GetString("General", "Modules", ""), modList);
	for (listIt = modList.begin(); listIt != modList.end(); ++listIt)
	{
		MidiModule mMod;
		std::vector<std::string> list;
		size_t validPorts;
		
		mMod.name = *listIt;
		if (! MidiModule::GetIDFromNameOrNumber(iniFile.GetString(*listIt, "ModType", ""), midiModColl.GetShortModNameLUT(), mMod.modType))
			continue;
		
		CfgString2Vector(iniFile.GetString(mMod.name, "Ports", ""), list);
		validPorts = GetMidiPortList(list, mMod.ports);
		CfgString2Vector(iniFile.GetString(mMod.name, "PortDelay", ""), list);
		Vector_Str2UInt32(list, mMod.delayTime);
		CfgString2Vector(iniFile.GetString(mMod.name, "ChnMask", ""), list);
		ParseChnMaskList(list, mMod.chnMask);
		CfgString2Vector(iniFile.GetString(mMod.name, "PlayTypes", ""), list);
		mMod.SetPlayTypes(list, midiModColl.GetShortModNameLUT());
		
		mMod.options = 0x00;
		if (iniFile.GetBoolean(mMod.name, "SimpleVolCtrl", false))
			mMod.options |= MMOD_OPT_SIMPLE_VOL;
		if (iniFile.GetBoolean(mMod.name, "AoTInsChange", false))
			mMod.options |= MMOD_OPT_AOT_INS;
		
		if (mMod.ports.empty())
		{
			printf("Module %s: No ports defined!\n", mMod.name.c_str());
			continue;
		}
		if (! validPorts && ! dummyOutput)
		{
			printf("Module %s: None of the ports is available!\n", mMod.name.c_str());
			continue;
		}
		if (mMod.playType.empty())
		{
			printf("Module %s: No Play Types defined!\n", mMod.name.c_str());
			continue;
		}
		midiModColl.AddModule(mMod);
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
	if (mopList == NULL)
		return 0x01;
	
	UINT8 retVal;
	
	retVal = midiModColl.ClosePorts(mopList);
	mopList = NULL;
	
	return retVal;
}

UINT8 main_OpenModule(size_t modID)
{
	if (mopList != NULL && mopList->state)
		return 0xC0;	// already open
	
	UINT8 retVal;
	size_t portCnt;
	MidiModule* mMod;
	
	portCnt = scanRes.numPorts;
	mMod = midiModColl.GetModule(modID);
	if (mMod != NULL && ! mMod->chnMask.empty())
		portCnt *= mMod->chnMask.size();
	if (dummyOutput)
	{
		mopList = NULL;
		retVal = 0x00;	// indicate success
	}
	else
	{
		retVal = midiModColl.OpenModulePorts(modID, portCnt, &mopList);
	}
	if (retVal && (mopList == NULL || mopList->mOuts.empty()))
	{
		vis_addstr("Error opening MIDI ports!");
		return retVal;
	}
	modIDOpen = modID;
	midPlay.SetDstModuleType(mMod->modType, false);
	if (mopList != NULL)
		midPlay.SetOutputPorts(mopList->mOuts, mMod);
	else
		midPlay.SetOutputPorts(std::vector<MIDIOUT_PORT*>(portCnt, NULL), mMod);
	
	if ((retVal & 0xF0) == 0x10)
		vis_addstr("Warning: The module doesn't have enough ports defined for proper playback!");
	if ((retVal & 0x0F) == 0x01)
		vis_addstr("Warning: One or more ports could not be opened!");
	
	return retVal;
}

double main_GetFadeTime(void)
{
	return fadeTime;
}

UINT8 main_GetSongInsMap(void)
{
	return songInsMap;
}

size_t main_GetSongOptDevice(void)
{
	return songOptDev;
}

UINT8* main_GetForcedInsMap(void)
{
	return &forceSrcType;
}

UINT8* main_GetForcedModule(void)
{
	return &forceModID;
}


void PlayMidi(void)
{
	PlayerOpts plrOpts;
	UINT8 retVal;
	size_t chosenModule;
	MidiModule* mMod;
	
	// try to detect the instrument set used by the MIDI
	MidiBankScan(&CMidi, true, &scanRes);
	if (tempSrcType != 0xFF)
		scanRes.modType = tempSrcType;
	if (forceSrcType != 0xFF)
		scanRes.modType = forceSrcType;
	songInsMap = scanRes.modType;
	
	if (scanRes.modType == 0xFF)
	{
		scanRes.modType = MODULE_GM_1;
		vis_printf("Falling back to %s mode ...\n", GetModuleTypeNameL(scanRes.modType));
	}
	
	songOptDev = midiModColl.GetOptimalModuleID(scanRes.modType);
	chosenModule = (forceModID != 0xFF) ? forceModID : songOptDev;
	if (chosenModule == (size_t)-1 || chosenModule >= midiModColl.GetModuleCount())
	{
		vis_printf("Unable to find an appropriate MIDI module for %s!\n", GetModuleTypeNameL(songInsMap));
		vis_update();
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
	
	plrOpts.srcType = scanRes.modType;
	plrOpts.dstType = mMod->modType;
	plrOpts.flags = playerCfgFlags;
	plrOpts.loopStartText = plrLoopText[0];
	plrOpts.loopEndText = plrLoopText[1];
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
			
			//fileTitle = GetFileTitle(midFileName.c_str());
			fileTitle = midFileName.c_str();
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
	
	vis_new_song();
	
	midPlay.Start();
	if (! syxData.empty() && (! didSendSyx || false))
	{
		vis_addstr("Sending SYX data ...");
		vis_update();
		midPlay.Pause();	// do pause/resume to fix initial timing
		if (mopList != NULL)
			SendSyxData(mopList->mOuts, syxData);
		didSendSyx = true;
		midPlay.Resume();
	}
	vis_update();
	
#ifdef _WIN32
#ifndef _DEBUG
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
#endif
	controlVal = vis_main();
#ifdef _WIN32
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#endif
	midPlay.Stop();
	
	vis_addstr("Done.");
	vis_update();
	
	CMidi.ClearAll();
	midPlay.FlushEvents();
	main_CloseModule();
	Sleep(100);
	
	return;
}

static void SendSyxDataToPorts(const std::vector<MIDIOUT_PORT*>& outPorts, size_t dataLen, const UINT8* data)
{
	std::vector<MIDIOUT_PORT*>::const_iterator portIt;
	
	midPlay.HandleRawEvent(dataLen, data);
	portIt = outPorts.begin();
	if (portIt != outPorts.end())	// HandleRawEvent already sends it to the first port
	{
		for (++portIt; portIt != outPorts.end(); ++portIt)
			MidiOutPort_SendLongMsg(*portIt, dataLen, data);
	}
	
	// wait for data to be transferred (3125 bytes per second)
	// (31250 bits per second transfer rate, 1 byte of payload = 10 bits: 1 start bit, 8 data bits, 1 stop bit)
	//Sleep(dataLen * 1000 / 3125);
	
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
