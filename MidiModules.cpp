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

MidiModOpts GetDefaultMidiModOpts(void)
{
	MidiModOpts mmo;
	memset(&mmo, 0x00, sizeof(MidiModOpts));
	mmo.resetType = 0xFF;
	return mmo;
}

UINT8 GetMidiModResetType(UINT8 modType)
{
	switch(MMASK_TYPE(modType))
	{
	case MODULE_TYPE_GM:
		if (MMASK_MOD(modType) <= MTGM_LVL2)
			return modType;
		else
			return MODULE_GM_1;
	case MODULE_TYPE_GS:
		if (modType == MODULE_SC55 || modType == MODULE_TG300B)
			return MODULE_SC55;
		else
			return MODULE_SC88;
	case MODULE_TYPE_XG:
		return MMO_RESET_XG_ALL;
	case MODULE_TYPE_K5:
		if (modType == MODULE_NS5R)
			return MMO_RESET_GMGSXG;
		else
			return MODULE_GM_1;
	case MODULE_TYPE_LA:
		return MMO_RESET_LA_SOFT;
	default:
		return MMO_RESET_NONE;
	}
}

UINT8 GetMidiModMasterVolType(UINT8 modType)
{
	switch(MMASK_TYPE(modType))
	{
	case MODULE_TYPE_GM:
	case MODULE_TYPE_XG:
		return MODULE_TYPE_GM;
	case MODULE_TYPE_GS:
		if (modType == MODULE_SC55)
			return MODULE_SC55;	// early SC-55 models don't support GM Master Volume SysEx
		else
			return MODULE_TYPE_GM;	// prefer GM SysEx due to being shorter than the GS variant
	case MODULE_TYPE_K5:
		if (modType == MODULE_NS5R)
			return MODULE_TYPE_GM;
		else
			return MMO_MSTVOL_CC_VOL;
	case MODULE_TYPE_LA:
		return MODULE_TYPE_LA;
	default:
		return MMO_MSTVOL_CC_VOL;
	}
}

UINT8 GetMidiModDefInsMap(UINT8 modType)
{
	switch(MMASK_TYPE(modType))
	{
	case MODULE_TYPE_GS:
		if (MMASK_MOD(modType) == MTGS_SC8850)
			return MTGS_SC88PRO;	// default to 88Pro map, because the 8850 has a very weak Standard Kit 1
		if (MMASK_MOD(modType) < MT_UNKNOWN)
			return MMASK_MOD(modType);
		else
			return MTGS_SC55;	// for TG300B mode
	case MODULE_TYPE_XG:
		// MU50/80/90 have only a single instrument map, which is called "MU basic".
		// MU100 and later add an additional map.
		//if (MMASK_MOD(modType) >= MTXG_MU100)
		//	return MTXG_MU100;
		//else
		//	return MTXG_MU50;
		return MTXG_MU50;	// MU basic sounds better with General MIDI stuff, IMO.
	default:
		return 0x00;
	}
}

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
	shortIDName[MODULE_05RW     ] = "05R/W";
	shortIDName[MODULE_X5DR     ] = "X5DR";
	shortIDName[MODULE_NS5R     ] = "NS5R";
	shortIDName[MODULE_KGMB     ] = "K-GMb";
	shortIDName[MODULE_MT32     ] = "MT-32";
//	shortIDName[MODULE_CM32P    ] = "CM-32P";
	shortIDName[MODULE_CM64     ] = "CM-64";
	
	longIDName = shortIDName;
	longIDName[ MODULE_GM_2     ] = "GM Level 2";
	longIDName[ MODULE_SC8850   ] = "SC-8820/8850";
	longIDName[ MODULE_MU1000   ] = "MU1000/2000";
	longIDName[ MODULE_TYPE_GS | MT_UNKNOWN] = "GS/unknown";
	longIDName[ MODULE_TYPE_XG | MT_UNKNOWN] = "XG/unknown";
	longIDName[ MODULE_MT32     ] = "MT-32/CM-32L";
	
	for (curID = 0x00; curID < shortIDName.size(); curID ++)
		shortNameID[shortIDName[curID]] = curID;
	for (curID = 0x00; curID < longIDName.size(); curID ++)
		longNameID[longIDName[curID]] = curID;
	
	return;
}

/*static*/ UINT8 MidiModule::GetIDFromNameOrNumber(const std::string& valStr, const std::map<std::string, UINT8>& nameLUT, UINT8& retValue)
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

MidiModuleCollection::PortState::PortState() :
	_portID((size_t)-1),
	_hMOP(NULL)
{
}

MidiModuleCollection::PortState::~PortState()
{
}

UINT8 MidiModuleCollection::PortState::OpenPort(UINT32 portID)
{
	if (_hMOP != NULL)
		return 0x00;	// already open
	
	UINT8 retVal;
	
	_hMOP = MidiOutPort_Init();
	if (_hMOP == NULL)
		return 0xFF;
	_portID = portID;
	
	retVal = MidiOutPort_OpenDevice(_hMOP, _portID);
	if (retVal)
	{
		MidiOutPort_Deinit(_hMOP);	_hMOP = NULL;
		return retVal;
	}
	return 0x00;
}

UINT8 MidiModuleCollection::PortState::ClosePort(void)
{
	if (_hMOP == NULL)
		return 0x00;
	
	UINT8 retVal;
	
	retVal = MidiOutPort_CloseDevice(_hMOP);
	MidiOutPort_Deinit(_hMOP);	_hMOP = NULL;
	_moduleIDs.clear();
	
	return retVal;
}


MidiModuleCollection::MidiModuleCollection() :
	_keepPortsOpen(false)
{
	return;
}

MidiModuleCollection::~MidiModuleCollection()
{
	size_t curPort;
	
	for (curPort = 0; curPort < _ports.size(); curPort ++)
		_ports[curPort].ClosePort();
	_ports.clear();
	
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

MidiModule* MidiModuleCollection::GetModule(size_t moduleID)
{
	return (moduleID < _modules.size()) ? &_modules[moduleID] : NULL;
}

MidiModule& MidiModuleCollection::AddModule(const std::string& name, UINT8 modType)
{
	MidiModule midiMod;
	
	midiMod.name = name;
	midiMod.modType = modType;
	_modules.push_back(midiMod);
	return _modules.back();
}

MidiModule& MidiModuleCollection::AddModule(const MidiModule& mMod)
{
	_modules.push_back(mMod);
	return _modules.back();
}

void MidiModuleCollection::RemoveModule(size_t moduleID)
{
	if (moduleID < _modules.size())
		return;
	_modules.erase(_modules.begin() + moduleID);
	return;
}

size_t MidiModuleCollection::GetOptimalModuleID(UINT8 playType) const
{
	size_t curMod;
	size_t curPT;
	
	// try for an exact match first
	for (curMod = 0; curMod < _modules.size(); curMod ++)
	{
		const MidiModule& mMod = _modules[curMod];
		if (mMod.ports.empty())
			continue;
		
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
		if (mMod.ports.empty())
			continue;
		
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
	
	resVal = 0x00;
	if (requiredPorts > mMod.ports.size())
	{
		//vis_printf("Warning: The module doesn't have enough ports defined for proper playback!\n");
		requiredPorts = mMod.ports.size();
		resVal = 0x10;	// not enough ports available
	}
	for (curPort = 0; curPort < mMod.ports.size() && portList.mOuts.size() < requiredPorts; curPort ++)
	{
		UINT8 retVal;
		UINT32 portID;
		
		portID = mMod.ports[curPort];
		if (portID == (UINT32)-1)
			continue;
		if (portID >= _ports.size())
			_ports.resize(portID + 1);
		PortState& pState = _ports[portID];
		
		if (pState._hMOP != NULL)	// port already open?
		{
			if (pState._moduleIDs.find(moduleID) != pState._moduleIDs.end())
			{
				// can't open a port more than once per module
				// so just try the next one
				continue;
			}
			else
			{
				// reuse port opened by other module
				pState._moduleIDs.insert(moduleID);
				portList.mOuts.push_back(pState._hMOP);
				continue;
			}
		}
		else
		{
			//printf("Opening MIDI port %u ...", portID);
			retVal = pState.OpenPort(portID);
			if (retVal)
			{
				//printf("  Error %02X\n", retVal);
				continue;
			}
			//printf("  OK, type: %s\n", GetModuleTypeName(mMod.modType));
			pState._moduleIDs.insert(moduleID);
			portList.mOuts.push_back(pState._hMOP);
		}
	}
	if (! portList.mOuts.empty())
		portList.state = 1;
	else if (portList.mOuts.size() < requiredPorts)
		resVal |= 0x01;	// one or more ports failed to open
	
	*retPortList = &portList;
	return resVal;
}

UINT8 MidiModuleCollection::ClosePorts(MidiOutPortList* portList, bool force)
{
	// the "force" parameter enforces closing regardless of state or _keepPortsOpen option
	size_t moduleID;
	size_t curOut;
	size_t curPort;
	
	if (portList->state != 1)
		return 0xC1;	// already closed
	
	for (moduleID = 0; moduleID < _openPorts.size(); moduleID ++)
	{
		if (&_openPorts[moduleID] == portList)
			break;
	}
	if (moduleID >= _openPorts.size())
		return 0xC2;	// unable to find module for port list
	
	for (curOut = 0; curOut < portList->mOuts.size(); curOut ++)
	{
		for (curPort = 0; curPort < _ports.size(); curPort ++)
		{
			if (_ports[curPort]._hMOP == portList->mOuts[curOut])
				break;
		}
		if (curPort >= _ports.size())
			continue;
		
		PortState& pState = _ports[curPort];
		pState._moduleIDs.erase(moduleID);
		if (pState._moduleIDs.empty() && ! _keepPortsOpen)
			pState.ClosePort();
	}
	portList->mOuts.clear();
	portList->state = 0;
	
	return 0x00;
}
