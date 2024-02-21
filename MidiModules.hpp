#ifndef __MIDIMODULES_HPP__
#define __MIDIMODULES_HPP__

#include <stdtype.h>
#include <string>
#include <vector>
#include <set>
#include <map>

#include "MidiOut.h"

struct MidiModOpts
{
	bool simpleVol;		// simple volume control
	bool aotIns;		// early/premature instrument changes
	bool instantSyx;	// SyxEx transmissions are instant and need no delay (for software drivers)
};

MidiModOpts GetDefaultMidiModOpts(void);

struct MidiModule
{
	std::string name;
	UINT8 modType;	// module type
	MidiModOpts options;
	std::vector<UINT32> ports;
	std::vector<UINT32> delayTime;	// delay all events by N milliseconds on the respective port
	std::vector<UINT16> chnMask;	// channels to be received by the respective port
	std::vector<UINT8> playType;	// supported types for playing
	
	static UINT8 GetIDFromNameOrNumber(const std::string& valStr, const std::map<std::string, UINT8>& nameLUT, UINT8& retValue);
	void SetPlayTypes(const std::vector<std::string>& playTypeStrs, const std::map<std::string, UINT8>& playTypeLUT);
};

// I still have no proper concept of how to handle the connection between MIDI modules and ports.
// The current structure feels hackish and doesn't allow using multiple instances of the same module or port.
struct MidiOutPortList
{
	UINT8 state;	// 0 - closed, 1 - open
	std::vector<MIDIOUT_PORT*> mOuts;
	MidiOutPortList();
};

class MidiModuleCollection
{
private:
	class PortState
	{
	public:
		size_t _portID;
		MIDIOUT_PORT* _hMOP;
		std::set<size_t> _moduleIDs;	// list of modules that are using this port
		
		PortState();
		~PortState();
		UINT8 OpenPort(UINT32 portID);
		UINT8 ClosePort(void);
	};
public:
	MidiModuleCollection();
	~MidiModuleCollection();
	
	static const std::map<std::string, UINT8>& GetShortModNameLUT(void);
	static const std::string& GetShortModName(UINT8 modType);
	static const std::map<std::string, UINT8>& GetLongModNameLUT(void);
	static const std::string& GetLongModName(UINT8 modType);
	
	void ClearModules(void);
	size_t GetModuleCount(void) const;
	MidiModule* GetModule(size_t moduleID);
	MidiModule& AddModule(const std::string& name, UINT8 modType);
	MidiModule& AddModule(const MidiModule& mMod);
	void RemoveModule(size_t moduleID);
	size_t GetOptimalModuleID(UINT8 playType) const;	// returns module for optimal playback
	
	UINT8 OpenModulePorts(size_t moduleID, size_t requiredPorts, MidiOutPortList** retPortList);
	UINT8 ClosePorts(MidiOutPortList* portList, bool force = false);
private:
	std::vector<MidiModule> _modules;
	std::vector<MidiOutPortList> _openPorts;	// open MIDI output ports (one list per module)
	std::vector<PortState> _ports;
	
	std::map<std::string, UINT8> _MODTYPE_NAMES;	// module type name look-up table
public:
	bool _keepPortsOpen;
};

#endif	// __MIDIMODULES_HPP__
