#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <vector>
#include <string>
#include <iconv.h>	// for iconv_t

#ifdef _WIN32
#undef GetFileTitle
#undef GetFileExtension
#endif

const char* GetFileTitle(const char* filePath);
const char* GetFileExtension(const char* filePath);
void StandardizeDirSeparators(std::string& filePath);
std::string CombinePaths(const std::string& basePath, const std::string& addPath);
std::string FindFile_List(const std::vector<std::string>& fileList, const std::vector<std::string>& pathList);
std::string FindFile_Single(const std::string& fileName, const std::vector<std::string>& pathList);
char StrCharsetConv(iconv_t hIConv, std::string& outStr, const std::string& inStr);

#endif	// __UTILS_HPP__
