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
		unsigned int digits;
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
					snprintf(&numBuf[0], numBuf.size(), "%*u", digits, aliasID);
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
	size_t patIdx = 0;
	size_t matIdx = 0;
	while(patIdx < pattern.length())
	{
		if (pattern[patIdx] == '*')	// '*' == multi-character wildcard
		{
			patIdx ++;
			for (; patIdx < pattern.length(); patIdx ++)
			{
				if (pattern[patIdx] == '?')
					matIdx ++;
				else if (pattern[patIdx] != '*')
					break;
			}
			if (matIdx > match.length())
				return false;	// more ?s found than characters available
			
			size_t patMIdx = patIdx;	// search next series of matching bytes
			for (; patMIdx < pattern.length(); patMIdx ++)
			{
				if (pattern[patMIdx] == '*' || pattern[patMIdx] == '?')
					break;
			}
			if (patMIdx == patIdx)
				return true;	// wildcard * at the end - the rest will match
			
			std::string subStr = pattern.substr(patIdx, patMIdx - patIdx);
			matIdx = match.find(subStr, matIdx);
			if (matIdx == std::string::npos)
				return false;
			// skip the subStr in pattern and match (we already fully checked it)
			patIdx += subStr.length();
			matIdx += subStr.length();
		}
		else
		{
			if (matIdx >= match.length())
				return false;	// match end, but characters left pattern
			if (pattern[patIdx] != '?')	// '?' == single-character wildcard
			{
				if (pattern[patIdx] != match[matIdx])
					return false;
			}
			patIdx ++;
			matIdx ++;
		}
	}
	if (matIdx >= match.length())
		return true;	// full match
	else
		return false;	// characters left in match
}

const std::map<std::string, UINT32>& MidiPortAliases::GetAliases() const
{
	return _aliases;
}
