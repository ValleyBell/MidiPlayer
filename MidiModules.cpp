#include <string>
#include <vector>
#include <map>
#include <stdlib.h>

#include <stdtype.h>

#include "MidiModules.hpp"
#include "MidiInsReader.h"	// for module constants

struct ModuleNames
{
	std::map<std::string, UINT8> shortNameID;	// short name -> ID
	std::vector<std::string> shortIDName;		// ID -> short name
	std::map<std::string, UINT8> longNameID;	// short name -> ID
	std::vector<std::string> longIDName;		// ID -> short name
	ModuleNames();
};


static ModuleNames modNames;

ModuleNames::ModuleNames()
{
	size_t curID;
	
	shortIDName.resize(0x100);
	shortIDName[MODULE_GM_1     ] = "GM";
	shortIDName[MODULE_GM_2     ] = "GM_L2";
	shortIDName[MODULE_SC55     ] = "SC-55";
	shortIDName[MODULE_SC88     ] = "SC-88";
	shortIDName[MODULE_SC88PRO  ] = "SC-88Pro";
	shortIDName[MODULE_SC8850   ] = "SC-8850";
	shortIDName[MODULE_TG300B   ] = "TG300B";
	shortIDName[MODULE_MU50     ] = "MU50";
	shortIDName[MODULE_MU80     ] = "MU80";
	shortIDName[MODULE_MU90     ] = "MU90";
	shortIDName[MODULE_MU100    ] = "MU100";
	shortIDName[MODULE_MU128    ] = "MU128";
	shortIDName[MODULE_MU1000   ] = "MU1000";
	shortIDName[MODULE_MT32     ] = "MT-32";
	
	longIDName = shortIDName;
	longIDName[ MODULE_GM_2     ] = "GM Level 2";
	longIDName[ MODULE_SC8850   ] = "SC-8820/8850";
	longIDName[ MODULE_MU1000   ] = "MU1000/MU2000";
	longIDName[ MODULE_TYPE_GS | MT_UNKNOWN] = "GS/unknown";
	longIDName[ MODULE_TYPE_XG | MT_UNKNOWN] = "XG/unknown";
	
	for (curID = 0x00; curID < shortIDName.size(); curID ++)
		shortNameID[shortIDName[curID]] = curID;
	for (curID = 0x00; curID < longIDName.size(); curID ++)
		longNameID[longIDName[curID]] = curID;
	
	return;
}

UINT8 GetIDFromNameOrNumber(const std::string& valStr, const std::map<std::string, UINT8>& nameLUT, UINT8& retValue)
{
	std::map<std::string, UINT8>::const_iterator nameIt;
	char* endStr;
	
	nameIt = nameLUT.find(valStr);
	if (nameIt != nameLUT.end())
	{
		retValue = nameIt->second;
		return 1;
	}
	
	retValue = (UINT8)strtoul(valStr.c_str(), &endStr, 0);
	if (endStr == valStr.c_str())
		return 0;	// not read
	else if (endStr == valStr.c_str() + valStr.length())
		return 1;	// fully read
	else
		return 2;	// partly read
}

void MidiModule::SetPortList(const std::vector<std::string>& portStrList)
{
	std::vector<std::string>::const_iterator portIt;
	
	this->ports.clear();
	for (portIt = portStrList.begin(); portIt != portStrList.end(); ++portIt)
	{
		UINT8 port;
		char* endStr;
		
		port = (UINT8)strtoul(portIt->c_str(), &endStr, 0);
		if (endStr != portIt->c_str())
			this->ports.push_back(port);
	}
	
	return;
}

void MidiModule::SetPlayTypes(const std::vector<std::string>& playTypeStrs, const std::map<std::string, UINT8>& playTypeLUT)
{
	std::vector<std::string>::const_iterator typeIt;
	char* endStr;
	
	this->playType.clear();
	for (typeIt = playTypeStrs.begin(); typeIt != playTypeStrs.end(); ++typeIt)
	{
		UINT8 pType;
		UINT8 retVal;
		UINT8 curMod;
		
		retVal = GetIDFromNameOrNumber(*typeIt, playTypeLUT, pType);
		if (retVal == 0)
		{
			// unable to parse - check for special strings
			if (*typeIt == "SC-xx")
			{
				for (curMod = 0x00; curMod < MT_UNKNOWN; curMod ++)
					this->playType.push_back(MODULE_TYPE_GS | curMod);
			}
			else if (*typeIt == "MUxx")
			{
				for (curMod = 0x00; curMod < MT_UNKNOWN; curMod ++)
					this->playType.push_back(MODULE_TYPE_XG | curMod);
			}
		}
		else if (retVal == 1)
		{
			// fully parsed - just push the value
			this->playType.push_back(pType);
		}
		else
		{
			// number was partly parseable - try resolving 0x1#/0x2#
			pType = (UINT8)strtoul(typeIt->c_str(), &endStr, 0);
			if (*endStr == '#')
			{
				pType <<= 4;
				for (curMod = 0x00; curMod < MT_UNKNOWN; curMod ++)
					this->playType.push_back(pType | curMod);
			}
		}
	}
	
	return;
}


MidiOutPortList::MidiOutPortList() :
	state(0x00)
{
}


MidiModuleCollection::MidiModuleCollection() :
	_keepPortsOpen(false)
{
	return;
}

MidiModuleCollection::~MidiModuleCollection()
{
	size_t curMod;
	
	for (curMod = 0; curMod < _openPorts.size(); curMod ++)
		ClosePorts(&_openPorts[curMod], true);
	
	return;
}


/*static*/ const std::map<std::string, UINT8>& MidiModuleCollection::GetShortModNameLUT(void)
{
	return modNames.shortNameID;
}

/*static*/ const std::string& MidiModuleCollection::GetShortModName(UINT8 modType)
{
	return modNames.shortIDName[modType];
}

/*static*/ const std::map<std::string, UINT8>& MidiModuleCollection::GetLongModNameLUT(void)
{
	return modNames.longNameID;
}

/*static*/ const std::string& MidiModuleCollection::GetLongModName(UINT8 modType)
{
	return modNames.longIDName[modType];
}

void MidiModuleCollection::ClearModules(void)
{
	_modules.clear();
	
	return;
}

size_t MidiModuleCollection::GetModuleCount(void) const
{
	return _modules.size();
}

MidiModule& MidiModuleCollection::GetModule(size_t moduleID)
{
	return _modules[moduleID];
}

MidiModule& MidiModuleCollection::AddModule(const std::string& name, UINT8 modType)
{
	MidiModule midiMod;
	
	midiMod.name = name;
	midiMod.modType = modType;
	_modules.push_back(midiMod);
	return _modules.back();
}

size_t MidiModuleCollection::GetOptimalModuleID(UINT8 playType) const
{
	size_t curMod;
	size_t curPT;
	
	// try for an exact match first
	for (curMod = 0; curMod < _modules.size(); curMod ++)
	{
		const MidiModule& mMod = _modules[curMod];
		
		for (curPT = 0; curPT < mMod.playType.size(); curPT ++)
		{
			if (mMod.playType[curPT] == playType)
				return curMod;
		}
	}
	// then search for approximate matches (GS on "any GS device", GM on GS/XG, etc.)
	for (curMod = 0; curMod < _modules.size(); curMod ++)
	{
		const MidiModule& mMod = _modules[curMod];
		
		for (curPT = 0; curPT < mMod.playType.size(); curPT ++)
		{
			if (MMASK_TYPE(mMod.playType[curPT]) == MMASK_TYPE(playType))
				return curMod;
			if (MMASK_TYPE(playType) == MODULE_TYPE_GM)
			{
				if (MMASK_TYPE(mMod.playType[curPT]) == MODULE_TYPE_GS ||
					MMASK_TYPE(mMod.playType[curPT]) == MODULE_TYPE_XG)
					return curMod;
			}
		}
	}
	
	return (size_t)-1;
}

UINT8 MidiModuleCollection::OpenModulePorts(size_t moduleID, size_t requiredPorts, MidiOutPortList** retPortList)
{
	if (moduleID >= _modules.size())
		return 0xFF;	// invalid module ID
	
	size_t curPort;
	UINT8 resVal;
	
	if (_openPorts.size() <= moduleID)
		_openPorts.resize(moduleID + 1);
	MidiModule& mMod = _modules[moduleID];
	MidiOutPortList& portList = _openPorts[moduleID];
	
	if (portList.state == 1)
		return 0xC0;	// module already opened
	if (portList.state == 2)
	{
		// module is closed, but ports were kept open
		portList.state = 0;
		*retPortList = &portList;
		return 0x00;
	}
	
	resVal = 0x00;
	if (requiredPorts > mMod.ports.size())
	{
		//vis_printf("Warning: The module doesn't have enough ports defined for proper playback!\n");
		requiredPorts = mMod.ports.size();
		resVal = 0x10;	// not enough ports available
	}
	for (curPort = 0; curPort < requiredPorts; curPort ++)
	{
		MIDIOUT_PORT* newPort;
		UINT8 retVal;
		
		newPort = MidiOutPort_Init();
		if (newPort == NULL)
			continue;
		//printf("Opening MIDI port %u ...", mMod.ports[curPort]);
		retVal = MidiOutPort_OpenDevice(newPort, mMod.ports[curPort]);
		if (retVal)
		{
			MidiOutPort_Deinit(newPort);
			//printf("  Error %02X\n", retVal);
			resVal |= 0x01;	// one or more ports failed to open
			continue;
		}
		//printf("  OK, type: %s\n", GetModuleTypeName(mMod.modType));
		portList.mOuts.push_back(newPort);
	}
	if (! portList.mOuts.empty())
		portList.state = 1;
	
	*retPortList = &portList;
	return resVal;
}

UINT8 MidiModuleCollection::ClosePorts(MidiOutPortList* portList, bool force)
{
	size_t curPort;
	UINT8 resVal;
	
	// the "force" parameter enforces closing regardless of state or _keepPortsOpen option
	if (! force)
	{
		if (portList->state != 1)
			return 0xC1;	// already closed
		
		if (_keepPortsOpen)
		{
			portList->state = 2;
			return 0x00;
		}
	}
	
	for (curPort = 0; curPort < portList->mOuts.size(); curPort ++)
	{
		resVal = MidiOutPort_CloseDevice(portList->mOuts[curPort]);
		MidiOutPort_Deinit(portList->mOuts[curPort]);
		portList->mOuts[curPort] = NULL;
	}
	portList->mOuts.clear();
	portList->state = 0;
	
	return 0x00;
}
