#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <istream>
#include <Windows.h>
#include <iconv.h>

#include <stdtype.h>
#include "utils.hpp"
#include "m3uargparse.hpp"


#ifdef _MSC_VER
#define	stricmp	_stricmp
#else
#define	stricmp	strcasecmp
#endif

static const char* M3UV2_HEAD = "#EXTM3U";
static const char* M3UV2_META = "#EXTINF:";
static const UINT8 UTF8_SIG[] = {0xEF, 0xBB, 0xBF};


//UINT8 ParseSongFiles(const std::vector<std::string> args, std::vector<SongFileList>& songList, std::vector<std::string>& playlistList);
static bool ReadM3UPlaylist(const char* fileName, std::vector<SongFileList>& songList, bool isM3Uu8);
static std::string WinStr2UTF8(const std::string& str);


UINT8 ParseSongFiles(const std::vector<std::string> args, std::vector<SongFileList>& songList, std::vector<std::string>& playlistList)
{
	int curArg;
	const char* fileName;
	const char* fileExt;
	UINT8 resVal;
	bool retValB;
	
	songList.clear();
	playlistList.clear();
	resVal = 0x00;
	for (curArg = 0; curArg < args.size(); curArg ++)
	{
		fileName = args[curArg].c_str();
		fileExt = GetFileExtension(fileName);
		if (fileExt == NULL)
			fileExt = "";
		if (! stricmp(fileExt, "m3u") || ! stricmp(fileExt, "m3u8"))
		{
			size_t plSong = songList.size();
			
			retValB = ReadM3UPlaylist(fileName, songList, ! stricmp(fileExt, "m3u8"));
			if (! retValB)
			{
				resVal |= 0x01;
				continue;
			}
			
			for (; plSong < songList.size(); plSong ++)
				songList[plSong].playlistID = playlistList.size();
			playlistList.push_back(fileName);
		}
		else
		{
			SongFileList sfl;
			sfl.fileName = fileName;
			sfl.playlistID = -1;
			songList.push_back(sfl);
		}
	}
	
	return 0x00;
}

static bool ReadM3UPlaylist(const char* fileName, std::vector<SongFileList>& songList, bool isM3Uu8)
{
	std::ifstream hFile;
	std::string baseDir;
	const char* strPtr;
	char fileSig[0x03];
	bool isUTF8;
	bool isV2Fmt;
	size_t METASTR_LEN;
	UINT32 lineNo;
	std::string tempStr;
	
#ifdef WIN32
	std::wstring fileNameW;
	fileNameW.resize(MultiByteToWideChar(CP_UTF8, 0, fileName, -1, NULL, 0) - 1);
	MultiByteToWideChar(CP_UTF8, 0, fileName, -1, &fileNameW[0], fileNameW.size() + 1);
	hFile.open(fileNameW);
#else
	hFile.open(fileName);
#endif
	if (! hFile.is_open())
		return false;
	
	strPtr = GetFileTitle(fileName);
	baseDir = std::string(fileName, strPtr - fileName);
	
	memset(fileSig, 0x00, 3);
	hFile.read(fileSig, 3);
	isUTF8 = ! memcmp(fileSig, UTF8_SIG, 3);
	if (! isUTF8)
		hFile.seekg(0, std::ios_base::beg);
	isUTF8 |= isM3Uu8;
	
	isV2Fmt = false;
	METASTR_LEN = strlen(M3UV2_META);
	lineNo = 0;
	while(hFile.good() && ! hFile.eof())
	{
		std::getline(hFile, tempStr);
		lineNo ++;
		
		while(! tempStr.empty() && iscntrl((unsigned char)tempStr.back()))
			tempStr.pop_back();	// remove NewLine-Characters
		if (tempStr.empty())
			continue;
		
		if (lineNo == 1 && tempStr == M3UV2_HEAD)
		{
			isV2Fmt = true;
			continue;
		}
		if (isV2Fmt && ! tempStr.compare(0, METASTR_LEN, M3UV2_META))
		{
			// Ignore metadata of m3u v2
			lineNo ++;
			continue;
		}
		
		if (! isUTF8)
			tempStr = WinStr2UTF8(tempStr);
		// at this point, we should have UTF-8 file names
		
		SongFileList sfl;
		sfl.fileName = CombinePaths(baseDir, tempStr);
		StandardizeDirSeparators(sfl.fileName);
		songList.push_back(sfl);
	}
	
	hFile.close();
	
	return true;
}

static std::string WinStr2UTF8(const std::string& str)
{
#ifdef _WIN32
	std::wstring wtemp;
	std::string out;
	
	wtemp.resize(MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0) - 1);
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wtemp[0], wtemp.size() + 1);
	
	out.resize(WideCharToMultiByte(CP_UTF8, 0, wtemp.c_str(), -1, NULL, 0, NULL, NULL) - 1);
	WideCharToMultiByte(CP_UTF8, 0, wtemp.c_str(), -1, &out[0], out.size() + 1, NULL, NULL);
	
	return out;
#else
	// TODO: handle Windows codepages
	return str;
#endif
}
