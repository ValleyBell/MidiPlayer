#include <stdtype.h>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <stdio.h>

#include "MidiOut.h"
#include "MidiPortAliases.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf	_snprintf
#endif


MidiPortAliases::MidiPortAliases()
{
}

MidiPortAliases::~MidiPortAliases()
{
}

void MidiPortAliases::ClearAliases(void)
{
	_aliases.clear();
	return;
}

void MidiPortAliases::LoadPortList(const MIDI_PORT_LIST& portList)
{
	UINT32 curPort;
	
	_portIDs.clear();	_portIDs.reserve(portList.count);
	_portNames.clear();	_portNames.reserve(portList.count);
	for (curPort = 0; curPort < portList.count; curPort ++)
	{
		_portIDs.push_back(portList.ports[curPort].id);
		_portNames.push_back(portList.ports[curPort].name);
	}
	
	return;
}

void MidiPortAliases::AddAlias(const std::string& aliasName, const std::string& portPattern)
{
	size_t curPort;
	size_t hashPos;
	
	hashPos = aliasName.find('#');
	if (hashPos == std::string::npos)
	{
		for (curPort = 0; curPort < _portNames.size(); curPort ++)
		{
			if (PatternMatch(portPattern, _portNames[curPort]))
			{
				_aliases[aliasName] = curPort;
				break;
			}
		}
	}
	else
	{
		std::string aPrefix;
		std::string aPostfix;
		std::vector<char> numBuf;	// buffer for storing the alias number
		std::string aNameNew;
		size_t digits;
		unsigned int aliasID;
		
		aPrefix = aliasName.substr(0, hashPos);
		for (; hashPos < aliasName.length() && aliasName[hashPos] == '#'; hashPos ++)
			;
		aPostfix = aliasName.substr(hashPos);
		digits = aliasName.length() - aPostfix.length() - aPrefix.length();
		
		numBuf.resize(0x10);
		aliasID = 0;	// begin with number 0
		for (curPort = 0; curPort < _portNames.size(); curPort ++)
		{
			if (PatternMatch(portPattern, _portNames[curPort]))
			{
				do
				{
					snprintf(&numBuf[0], numBuf.size(), "%*u", aliasID);
					aNameNew = aPrefix + &numBuf[0] + aPostfix;
					aliasID ++;
				} while(_aliases.find(aNameNew) != _aliases.end());	// go to first unused ID
				_aliases[aNameNew] = curPort;
			}
		}
	}
	
	return;
}

/*static*/ bool MidiPortAliases::PatternMatch(const std::string& pattern, const std::string& match)
{
	if (pattern == match)
		return true;
	// TODO: handle wildcards * and ?
	return false;
}

const std::map<std::string, UINT32>& MidiPortAliases::GetAliases() const
{
	return _aliases;
}
