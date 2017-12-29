#ifndef __MIDIBANKSCAP_HPP__
#define __MIDIBANKSCAP_HPP__

#include <stdtype.h>
#include "MidiInsReader.h"
#include "MidiLib.hpp"


typedef struct
{
	UINT8 modType;		// module type (GM/GS/XG)
	UINT8 hasReset;
	UINT8 GS_Min;		// GS module ID: minimum compatibility (using non-GM fallback instruments)
	UINT8 GS_Opt;		// GS module ID: optimal playback
	UINT8 XG_Opt;		// XG module ID: optimal playback
	UINT8 XG_Flags;		// XG module flags: Bit 0 (01): XG drums, Bit 7 (80): unknown instrument
	UINT8 GS_Flags;		// GS module flags: Bit 0/1 (03): has user instruments
	UINT8 numPorts;		// number of MIDI ports required for proper playback
} BANKSCAN_RESULT;

void SetBankScanInstruments(UINT8 moduleID, const INS_BANK* insBank);
void MidiBankScan(MidiFile* cMidi, bool ignoreEmptyChns, BANKSCAN_RESULT* result);


#endif	// __MIDIBANKSCAP_HPP__
