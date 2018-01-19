#ifndef __UTILS_HPP__
#define __UTILS_HPP__

const char* GetFileTitle(const char* filePath);
const char* GetFileExtention(const char* filePath);
void StandardizeDirSeparators(std::string& filePath);
std::string CombinePaths(const std::string& basePath, const std::string& addPath);
std::string FindFile_List(const std::vector<std::string>& fileList, const std::vector<std::string>& pathList);
std::string FindFile_Single(const std::string& fileName, const std::vector<std::string>& pathList);

#endif	// __UTILS_HPP__
