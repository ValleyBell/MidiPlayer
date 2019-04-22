#ifndef __MIDIPORTALIASES_HPP__
#define __MIDIPORTALIASES_HPP__

#include <stdtype.h>
#include <string>
#include <vector>
#include <map>
#include <tuple>

typedef struct _midi_port_list MIDI_PORT_LIST;	// from MidiOut.h

class MidiPortAliases
{
public:
	MidiPortAliases();
	~MidiPortAliases();
	
	void ClearAliases(void);
	void LoadPortList(const MIDI_PORT_LIST& portList);
	void AddAlias(const std::string& aliasName, const std::string& portPattern);
	const std::map<std::string, UINT32>& GetAliases() const;
private:
	static bool PatternMatch(const std::string& pattern, const std::string& match);
	
	std::vector<UINT32> _portIDs;
	std::vector<std::string> _portNames;
	std::map<std::string, UINT32> _aliases;
};

#endif	// __MIDIPORTALIASES_HPP__
