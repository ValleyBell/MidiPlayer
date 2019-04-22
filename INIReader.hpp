// loosely based on the INIReader from https://github.com/benhoyt/inih

#ifndef __INIREADER_HPP__
#define __INIREADER_HPP__

#include <map>
#include <vector>
#include <string>

class INIReader
{
private:
	typedef std::map<std::string, std::string> KeyMap;
	typedef std::map<std::string, KeyMap> SectKeyMap;
public:
	INIReader();
	int ReadFile(const std::string& filename);
	
	const std::vector<std::string>& GetSections(void) const;
	std::vector<std::string> GetKeys(const std::string& section) const;
	bool HasSection(const std::string& section) const;
	bool HasKey(const std::string& section, const std::string& name) const;
	
	std::string GetString(const std::string& section, const std::string& name, const std::string& defaultVal) const;
	long GetInteger(const std::string& section, const std::string& name, long defaultVal) const;
	double GetFloat(const std::string& section, const std::string& name, double defaultVal) const;
	bool GetBoolean(const std::string& section, const std::string& name, bool defaultVal) const;
	
private:
	SectKeyMap _values;	// map<"section", map<"key", "value">>
	std::vector<std::string> _sections;
	
	static int ValueHandler(void* user, const char* section, const char* name, const char* value);
	const std::string& GetRaw(const std::string& section, const std::string& name, const std::string& defaultVal) const;
};

#endif	// __INIREADER_HPP__
