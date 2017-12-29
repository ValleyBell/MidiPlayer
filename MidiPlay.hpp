#ifndef __MIDIPLAY_HPP__
#define __MIDIPLAY_HPP__

#include <stdtype.h>
#include <vector>
#include <list>

//#include "MidiLib.hpp"
class MidiFile;
#include "MidiOut.h"
#include "OSTimer.h"
#include "MidiInsReader.h"

#define PLROPTS_RESET		0x01	// needs GM/GS/XG reset
#define PLROPTS_STRICT		0x02	// strict mode (GS Mode: enforces instrument map if Bank LSB == 0)
#define PLROPTS_ENABLE_CTF	0x04	// enable Capital Tone Fallback
struct PlayerOpts
{
	UINT8 srcType;
	UINT8 dstType;
	UINT8 flags;	// see PLROPTS_ defines
};

class MidiPlayer
{
public:
	struct NoteInfo
	{
		UINT8 chn;
		UINT8 note;
		UINT8 vel;
		UINT16 srcTrk;	// track that started the note
	};
	struct ChannelState
	{
		UINT8 flags;	// Bit 7 (80) - is drum channel
		UINT8 curIns;
		UINT8 insBank[2];	// 0 = Bank MSB; 1 = Bank LSB (current/patched state)
		const INS_DATA* insMapOPtr;	// original instrument
		const INS_DATA* insMapPPtr;	// patched instrument
		UINT8 ctrls[0x80];
		
		UINT8 rpnCtrl[2];	// [0] = MSB, [1] = LSB, 00..7F = RPN, 80..FF = NRPN
		UINT8 pbRange;
		INT8 tuneCoarse;
		INT16 tuneFine;		// stored as 8.8 fixed point
		
		std::list<NoteInfo> notes;	// currently running notes
		MIDIOUT_PORT* outPort;
	};
private:
	struct TrackState
	{
		UINT16 trkID;
		UINT8 portID;
		midevt_const_it endPos;
		midevt_const_it evtPos;
	};
	struct TempoChg
	{
		UINT32 tick;
		UINT32 tempo;
		UINT64 tmrTick;
	};
	
public:
	MidiPlayer();
	~MidiPlayer();
	
	void SetMidiFile(MidiFile* midiFile);
	void SetOutputPort(MIDIOUT_PORT* outPort);
	void SetOutputPorts(const std::vector<MIDIOUT_PORT*>& outPorts);
	void SetOutPortMapping(size_t numPorts, const size_t* outPorts);
	void SetOptions(const PlayerOpts& plrOpts);
	void SetInstrumentBank(UINT8 moduleType, const INS_BANK* insBank);
	UINT8 Start(void);
	UINT8 Stop(void);
	UINT8 Pause(void);
	UINT8 Resume(void);
	UINT8 GetState(void) const;
	double GetSongLength(void) const;	// returns length in seconds
	
	void DoPlaybackStep(void);
private:
	const INS_BANK* SelectInsMap(UINT8 moduleType, UINT8* insMapModule);
	static bool tempo_compare(const TempoChg& first, const TempoChg& second);
	void PrepareMidi(void);
	void InitializeChannels(void);
	void RefreshTickTime(void);
	void DoEvent(TrackState* trkState, const MidiEvent* midiEvt);
	bool HandleNoteEvent(ChannelState* chnSt, const TrackState* trkSt, const MidiEvent* midiEvt);
	bool HandleControlEvent(ChannelState* chnSt, const TrackState* trkSt, const MidiEvent* midiEvt);
	bool HandleInstrumentEvent(ChannelState* chnSt, const TrackState* trkSt, const MidiEvent* midiEvt);
	bool HandleSysEx(const TrackState* trkSt, const MidiEvent* midiEvt);
	void AllNotesStop(void);
	void AllNotesRestart(void);
	
	bool _playing;
	bool _paused;
	
	MidiFile* _cMidi;
	UINT64 _songLength;
	std::list<TempoChg> _tempoList;
	const INS_BANK* _insBankGM1;
	const INS_BANK* _insBankGM2;
	const INS_BANK* _insBankGS;
	const INS_BANK* _insBankXG;
	const INS_BANK* _insBankYGS;	// Yamaha GS (TG300B mode)
	const INS_BANK* _insBankMT32;
	
	PlayerOpts _options;
	std::vector<size_t> _portMap;	// MIDI track port -> ID of MIDIOUT_PORT object
	std::vector<MIDIOUT_PORT*> _outPorts;
	OS_TIMER* _osTimer;
	UINT64 _tmrFreq;	// number of virtual timer ticks for 1 second
	UINT64 _tmrStep;
	
	std::vector<TrackState> _trkStates;
	std::vector<ChannelState> _chnStates;
	UINT32 _midiTempo;
	UINT32 _lastEvtTick;
	UINT64 _curTickTime;	// time for 1 MIDI tick at current tempo
};

#endif	// __MIDIPLAY_HPP__
