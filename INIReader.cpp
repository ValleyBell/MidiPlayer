#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ini.h>
#include "INIReader.hpp"

INIReader::INIReader()
{
}

int INIReader::ReadFile(const std::string& filename)
{
	_sections.clear();
	_values.clear();
	return ini_parse(filename.c_str(), &ValueHandler, this);
}

int INIReader::ValueHandler(void* user, const char* section, const char* name, const char* value)
{
	INIReader* reader = (INIReader*)user;
	
	if (! reader->HasSection(section))
		reader->_sections.push_back(section);
	
	std::string& savedVal = reader->_values[section][name];
	//if (! savedVal.empty())
	//	savedVal += "\n";
	//savedVal += value;
	savedVal = value;
	
	return 1;
}

const std::vector<std::string>& INIReader::GetSections(void) const
{
	return _sections;
}

std::vector<std::string> INIReader::GetKeys(const std::string& section) const
{
	std::vector<std::string> result;
	SectKeyMap::const_iterator sectIt = _values.find(section);
	if (sectIt == _values.end())
		return result;
	
	result.reserve(sectIt->second.size());
	KeyMap::const_iterator kmIt;
	for (kmIt = sectIt->second.begin(); kmIt != sectIt->second.end(); ++kmIt)
		result.push_back(kmIt->first);
	return result;
}

bool INIReader::HasSection(const std::string& section) const
{
	return (_values.find(section) != _values.end());
}

bool INIReader::HasKey(const std::string& section, const std::string& name) const
{
	SectKeyMap::const_iterator sectIt = _values.find(section);
	if (sectIt == _values.end())
		return false;
	return (sectIt->second.find(name) != sectIt->second.end());
}

const std::string& INIReader::GetRaw(const std::string& section, const std::string& name, const std::string& defaultVal) const
{
	SectKeyMap::const_iterator sectIt = _values.find(section);
	if (sectIt == _values.end())
		return defaultVal;
	KeyMap::const_iterator keyIt = sectIt->second.find(name);
	if (keyIt == sectIt->second.end())
		return defaultVal;
	return keyIt->second;
}

std::string INIReader::GetString(const std::string& section, const std::string& name, const std::string& defaultVal) const
{
	std::string valStr = GetRaw(section, name, defaultVal);
	size_t vsLen = valStr.length();
	if (vsLen >= 2 && valStr[0] == '\"' && valStr[vsLen - 1] == '\"')
		valStr = valStr.substr(1, vsLen - 2);
	return valStr;
}

long INIReader::GetInteger(const std::string& section, const std::string& name, long defaultVal) const
{
	std::string valStr = GetRaw(section, name, "");
	char* end;
	long value = strtol(valStr.c_str(), &end, 0);	// strtol instead of atoi so that hex works as well
	return (end > valStr.c_str()) ? value : defaultVal;
}

double INIReader::GetFloat(const std::string& section, const std::string& name, double defaultVal) const
{
	std::string valStr = GetRaw(section, name, "");
	char* end;
	double value = strtod(valStr.c_str(), &end);
	return (end > valStr.c_str()) ? value : defaultVal;
}

bool INIReader::GetBoolean(const std::string& section, const std::string& name, bool defaultVal) const
{
	std::string valStr = GetRaw(section, name, "");
	std::transform(valStr.begin(), valStr.end(), valStr.begin(), ::tolower);	// for case-insensitive comparison
	if (valStr == "true" || valStr == "yes" || valStr == "on" || valStr == "1")
		return true;
	else if (valStr == "false" || valStr == "no" || valStr == "off" || valStr == "0")
		return false;
	else
		return defaultVal;
}
