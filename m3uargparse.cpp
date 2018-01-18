#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <istream>

#include <stdtype.h>
#include "m3uargparse.hpp"


#ifdef _MSC_VER
#define	stricmp	_stricmp
#else
#define	stricmp	strcasecmp
#endif

static const char* M3UV2_HEAD = "#EXTM3U";
static const char* M3UV2_META = "#EXTINF:";
static const UINT8 UTF8_SIG[] = {0xEF, 0xBB, 0xBF};


//UINT8 ParseSongFiles(int argc, char* argv[], std::vector<SongFileList>& songList, std::vector<std::string>& playlistList);
static bool ReadM3UPlaylist(const char* fileName, std::vector<SongFileList>& songList);
static const char* GetLastDirSeparator(const char* filePath);
static const char* GetFileExtention(const char* filePath);
static void StandardizeDirSeparators(std::string& filePath);
static bool IsAbsolutePath(const char* filePath);
static std::string CombinePaths(const std::string& basePath, const std::string& addPath);


UINT8 ParseSongFiles(int argc, char* argv[], std::vector<SongFileList>& songList, std::vector<std::string>& playlistList)
{
	int curArg;
	const char* fileName;
	const char* fileExt;
	UINT8 resVal;
	bool retValB;
	
	songList.clear();
	playlistList.clear();
	resVal = 0x00;
	for (curArg = 0; curArg < argc; curArg ++)
	{
		fileName = argv[curArg];
		fileExt = GetFileExtention(fileName);
		if (fileExt == NULL)
			fileExt = "";
		if (! stricmp(fileExt, "m3u") || ! stricmp(fileExt, "m3u8"))
		{
			size_t plSong = songList.size();
			
			retValB = ReadM3UPlaylist(fileName, songList);
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

static bool ReadM3UPlaylist(const char* fileName, std::vector<SongFileList>& songList)
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
	
	hFile.open(fileName);
	if (! hFile.is_open())
		return false;
	
	strPtr = GetLastDirSeparator(fileName);
	if (strPtr == NULL)
		baseDir = "";
	else
		baseDir = std::string(fileName, strPtr + 1 - fileName);
	
	memset(fileSig, 0x00, 3);
	hFile.read(fileSig, 3);
	isUTF8 = ! memcmp(fileSig, UTF8_SIG, 3);
	hFile.seekg(0, std::ios_base::beg);
	
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
		
		SongFileList sfl;
		sfl.fileName = CombinePaths(baseDir, tempStr);
		StandardizeDirSeparators(sfl.fileName);
		songList.push_back(sfl);
	}
	
	hFile.close();
	
	return true;
}

static const char* GetLastDirSeparator(const char* filePath)
{
	const char* sepPos1;
	const char* sepPos2;
	
	if (strncmp(filePath, "\\\\", 2))
		filePath += 2;	// skip Windows network prefix
	sepPos1 = strrchr(filePath, '/');
	sepPos2 = strrchr(filePath, '\\');
	return (sepPos1 < sepPos2) ? sepPos2 : sepPos1;
}

static const char* GetFileExtention(const char* filePath)
{
	const char* dirSepPos;
	const char* extDotPos;
	
	dirSepPos = GetLastDirSeparator(filePath);
	if (dirSepPos == NULL)
		dirSepPos = filePath;
	extDotPos = strrchr(dirSepPos, '.');
	return (extDotPos == NULL) ? NULL : (extDotPos + 1);
}

static void StandardizeDirSeparators(std::string& filePath)
{
	size_t curChr;
	
	curChr = 0;
	if (! filePath.compare(curChr, 2, "\\\\"))
		curChr += 2;	// skip Windows network prefix
	for (; curChr < filePath.length(); curChr ++)
	{
		if (filePath[curChr] == '\\')
			filePath[curChr] = '/';
	}
	
	return;
}

static bool IsAbsolutePath(const char* filePath)
{
	if (filePath[0] == '\0')
		return false;	// empty string
	if (filePath[0] == '/' || filePath[0] == '\\')
		return true;	// absolute UNIX path
	if (filePath[1] == ':')
	{
		if ((filePath[0] >= 'A' && filePath[0] <= 'Z') ||
			(filePath[0] >= 'a' && filePath[0] <= 'z'))
		return true;	// Device Path: C:\path
	}
	if (! strncmp(filePath, "\\\\", 2))
		return true;	// Network Path: \\computername\path
	return false;
}

static std::string CombinePaths(const std::string& basePath, const std::string& addPath)
{
	if (basePath.empty() || IsAbsolutePath(addPath.c_str()))
		return addPath;
	if (basePath.back() == '/' || basePath.back() == '\\')
		return basePath + addPath;
	else
		return basePath + '/' + addPath;
}
