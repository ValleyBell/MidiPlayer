#ifndef __MIDIMODULES_HPP__
#define __MIDIMODULES_HPP__

#include <stdtype.h>
#include <string>
#include <vector>
#include <map>

#include "MidiOut.h"

UINT8 GetIDFromNameOrNumber(const std::string& valStr, const std::map<std::string, UINT8>& nameLUT, UINT8& retValue);

struct MidiModule
{
	std::string name;
	UINT8 modType;	// module type
	std::vector<UINT8> ports;
	std::vector<UINT8> playType;	// supported types for playing
	
	void SetPortList(const std::vector<std::string>& portStrList);
	void SetPlayTypes(const std::vector<std::string>& playTypeStrs, const std::map<std::string, UINT8>& playTypeLUT);
};

struct MidiOutPortList
{
	UINT8 state;
	std::vector<MIDIOUT_PORT*> mOuts;
};

class MidiModuleCollection
{
public:
	MidiModuleCollection();
	~MidiModuleCollection();
	
	static const std::map<std::string, UINT8>& GetShortModNameLUT(void);
	static const std::string& GetShortModName(UINT8 modType);
	static const std::map<std::string, UINT8>& GetLongModNameLUT(void);
	static const std::string& GetLongModName(UINT8 modType);
	
	void ClearModules(void);
	size_t GetModuleCount(void) const;
	MidiModule& GetModule(size_t moduleID);
	MidiModule& AddModule(const std::string& name, UINT8 modType);
	size_t GetOptimalModuleID(UINT8 playType) const;	// returns module for optimal playback
	
	UINT8 OpenModulePorts(size_t moduleID, size_t requiredPorts, MidiOutPortList** retMOuts);
	UINT8 ClosePorts(const std::vector<MIDIOUT_PORT*>& mOuts);
private:
	std::vector<MidiModule> _modules;
	std::vector<MidiOutPortList> _openPorts;	// open MIDI output ports (one list per module)
	std::vector<size_t> _portOpenCount;
	
	std::map<std::string, UINT8> _MODTYPE_NAMES;	// module type name look-up table
};

#endif	// __MIDIMODULES_HPP__
