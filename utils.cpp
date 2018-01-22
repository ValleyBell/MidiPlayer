#include <stddef.h>
#include <string.h>
#include <string>
#include <stdio.h>
#include <vector>

#ifdef _WIN32
// TODO
#else
#include <iconv.h>
#endif

#include "utils.hpp"


static const char* GetLastDirSeparator(const char* filePath);
//const char* GetFileTitle(const char* filePath);
//const char* GetFileExtention(const char* filePath);
//void StandardizeDirSeparators(std::string& filePath);
static bool IsAbsolutePath(const char* filePath);
//std::string CombinePaths(const std::string& basePath, const std::string& addPath);


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

const char* GetFileTitle(const char* filePath)
{
	const char* dirSepPos;
	
	dirSepPos = GetLastDirSeparator(filePath);
	return (dirSepPos != NULL) ? &dirSepPos[1] : filePath;
}

const char* GetFileExtention(const char* filePath)
{
	const char* dirSepPos;
	const char* extDotPos;
	
	dirSepPos = GetLastDirSeparator(filePath);
	if (dirSepPos == NULL)
		dirSepPos = filePath;
	extDotPos = strrchr(dirSepPos, '.');
	return (extDotPos == NULL) ? NULL : (extDotPos + 1);
}

void StandardizeDirSeparators(std::string& filePath)
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

std::string CombinePaths(const std::string& basePath, const std::string& addPath)
{
	if (basePath.empty() || IsAbsolutePath(addPath.c_str()))
		return addPath;
	if (basePath.back() == '/' || basePath.back() == '\\')
		return basePath + addPath;
	else
		return basePath + '/' + addPath;
}

std::string FindFile_List(const std::vector<std::string>& fileList, const std::vector<std::string>& pathList)
{
	size_t curFile;
	std::vector<std::string>::const_reverse_iterator pathIt;
	std::vector<std::string>::const_iterator fileIt;
	std::string fullName;
	FILE* hFile;
	
	for (pathIt = pathList.rbegin(); pathIt != pathList.rend(); ++pathIt)
	{
		for (fileIt = fileList.begin(); fileIt != fileList.end(); ++fileIt)
		{
			fullName = CombinePaths(*pathIt, *fileIt);
			
			hFile = fopen(fullName.c_str(), "r");
			if (hFile != NULL)
			{
				fclose(hFile);
				return fullName;
			}
		}
	}
	
	return "";
}

std::string FindFile_Single(const std::string& fileName, const std::vector<std::string>& pathList)
{
	std::vector<std::string> fileList;
	
	fileList.push_back(fileName);
	return FindFile_List(fileList, pathList);
}

char StrCharsetConv(iconv_t hIConv, std::string& outStr, const std::string& inStr)
{
	if (inStr.empty())
	{
		outStr = inStr;
		return 0x00;
	}
	
#ifdef _WIN32
	outStr = inStr;
	return 0x01;
#else
	size_t remBytesIn;
	size_t remBytesOut;
	char* inPtr;
	char* outPtr;
	size_t wrtBytes;
	
	iconv(hIConv, NULL, NULL, NULL, NULL);	// reset conversion state
	outStr.resize(inStr.length() * 3 / 2);
	
	remBytesIn = inStr.length();	inPtr = (char*)&inStr[0];
	remBytesOut = outStr.length();	outPtr = &outStr[0];
	wrtBytes = iconv(hIConv, &inPtr, &remBytesIn, &outPtr, &remBytesOut);
	while(wrtBytes == (size_t)-1)
	{
		if (errno == EILSEQ || errno == EINVAL)
		{
			// invalid encoding - return original string
			outStr = inStr;
			return 0x01;
		}
		// errno == E2BIG
		wrtBytes = outPtr - &outStr[0];
		outStr.resize(outStr.length() + remBytesIn * 2);
		
		remBytesOut = outStr.length() - wrtBytes;
		outPtr = &outStr[wrtBytes];
		wrtBytes = iconv(hIConv, &inPtr, &remBytesIn, &outPtr, &remBytesOut);
	}
	
	wrtBytes = outPtr - &outStr[0];
	outStr.resize(wrtBytes);
	return 0x00;
#endif
}
