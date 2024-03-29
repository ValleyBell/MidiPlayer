#ifndef __MIDIPLAY_HPP__
#define __MIDIPLAY_HPP__

#include <stdtype.h>
#include <string>
#include <vector>
#include <list>
#include <queue>

#include "MidiLib.hpp"
#include "NoteVis.hpp"
#include "MidiOut.h"
#include "OSTimer.h"
#include "MidiInsReader.h"
#include "MidiModules.hpp"	// for MidiModOpts and MidiModule


#define PLROPTS_RESET		0x01	// needs GM/GS/XG reset
#define PLROPTS_STRICT		0x02	// strict mode (GS Mode: enforces instrument map if Bank LSB == 0)
#define PLROPTS_ENABLE_CTF	0x04	// enable Capital Tone Fallback
#define PLROPTS_GDF_NONE	0x00	// no fallback
#define PLROPTS_GDF_ALL		0x01	// fallback for all to GM drums (patch 1)
#define PLROPTS_GDF_GS		0x02	// fallback for all but GS/XG drums
struct PlayerOpts
{
	UINT8 srcType;
	UINT8 dstType;
	UINT8 flags;	// see PLROPTS_ defines
	std::string loopStartText;
	std::string loopEndText;
	UINT32 numLoops;
	double fadeTime;
	double endPauseTime;
	bool nrpnLoops;
	bool noNoteOverlap;
	UINT8 gmDrumFallback;
	bool fixSysExChksum;
};

struct MidiQueueEvt
{
	UINT64 time;
	UINT8 flag;
	std::vector<UINT8> data;
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
	struct InstrumentInfo
	{
		UINT8 bank[2];	// 0 = Bank MSB, 1 = Bank LSB (current/patched state)
		UINT8 ins;		// IDs 00..7F - melody, 80..FF - drum kits
		UINT8 bnkIgn;	// bank ignore mask
		const INS_DATA* bankPtr;	// original instrument
	};
	struct ChannelState
	{
		UINT16 fullChnID;
		UINT8 midChn;
		UINT8 portID;
		UINT8 flags;		// Bit 7 (80) - is drum channel
		UINT8 defInsMap;	// default GS/XG instrument map (0xFF = use global default)
		InstrumentInfo insOrg;	// original instrument set by the song
		InstrumentInfo insSend;	// patched instrument as sent to the device
		UINT8 insState[3];	// 0 = Bank MSB, 1 = Bank LSB, 2 = instrument (last sent state)
		UINT8 curIns;		// instrument set by the MIDI file
		UINT16 userInsID;	// 0xFFFF - not a user instrument
		UINT8 ctrls[0x80];	// Bit 7 (80) - is still the default value
		UINT8 idCC[2];	// for SC-8820 CC1/CC2 remapping
		
		UINT8 devPartID;	// 00..0F = A01..A16, 10..1F = B01..B16, FF = off
		UINT8 devPartMode;	// 00 = normal, 80 = drum 1, 81 = drum 2, ..., FF = XG drum auto
		UINT8 gsPortID;
		UINT8 gsPartID;
		UINT8 keyLow;		// minimum key (lowest note) on this channel
		UINT8 keyHigh;		// maximum key (highest note) on this channel
		UINT8 rpnCtrl[2];	// [0] = MSB, [1] = LSB, 00..7F = RPN, 80..FF = NRPN
		bool hadDrumNRPN;
		INT8 pbRange;
		INT8 pbRangeUnscl;	// unscaled PB range set via RPN (used for MIDIs that expect non-standard PB ranges)
		INT8 tuneCoarse;
		INT16 tuneFine;		// stored as 8.8 fixed point (-0x4000 = -1 semitone .. 0 = centre .. +0x3FFF = +1 semitone)
		UINT16 pBendRaw;
		INT32 pBendScl;		// "full" pitch bend, scaled to 8192 ticks per semitone
		
		const char* userInsName;
		const InstrumentInfo* userInsRef;
		std::string insNameBuf;
		std::list<NoteInfo> notes;	// currently running notes
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
	struct TimeSigChg
	{
		UINT32 tick;
		UINT8 timeSig[4];
		UINT32 measPos[3];	// bar, beat, tick
	};
	struct KeySigChg
	{
		UINT32 tick;
		INT8 keySig[2];
	};
	struct LoopPoint
	{
		bool used;
		UINT32 tick;
		std::list<TempoChg>::const_iterator tempoPos;
		std::list<TimeSigChg>::const_iterator timeSigPos;
		std::list<KeySigChg>::const_iterator keySigPos;
		std::vector<midevt_const_it> trkEvtPos;	// evtPos of each track
	};
	
public:
	MidiPlayer();
	~MidiPlayer();
	
	void SetMidiFile(MidiFile* midiFile);
	void SetOutputPort(MIDIOUT_PORT* outPort);
	void SetOutputPorts(const std::vector<MIDIOUT_PORT*>& outPorts, const MidiModule* midiMod);
	void SetOutPortMapping(size_t numPorts, const size_t* outPorts);
	void SetOptions(const PlayerOpts& plrOpts);
	const PlayerOpts& GetOptions(void) const;
	UINT8 GetModuleType(void) const;	// current MIDI module type used for playback
	MidiModOpts GetPortOptions(void) const;
	void SetSrcModuleType(UINT8 modType, bool insRefresh = false);
	void SetDstModuleType(UINT8 modType, bool chnRefresh = false);
	void SetInstrumentBank(UINT8 moduleType, const INS_BANK* insBank);
	UINT8 Start(void);
	UINT8 Stop(void);
	UINT8 Pause(void);
	UINT8 Resume(void);
	UINT8 FlushEvents(void);
	UINT8 StopAllNotes(void);
	UINT8 FadeOutT(double fadeTime);	// fade out over x seconds
	UINT8 GetState(void) const;
	double GetSongLength(void) const;	// returns length in seconds
	void GetSongLengthM(UINT32* bar, UINT32* beat, UINT32* tick) const;	// return length in bar:beat:tick
	void GetSongStatsM(UINT32* maxBar, UINT16* maxBeatNum, UINT16* maxBeatDen, UINT32* maxTickCnt) const;
	double GetPlaybackPos(bool allowOverflow = false) const;
	void GetPlaybackPosM(UINT32* bar, UINT32* beat, UINT32* tick) const;
	UINT32 GetCurTimeSig(void) const;	// low word: numerator, high word: denominator
	double GetCurTempo(void) const;
	INT8 GetCurKeySig(void) const;
	const std::vector<ChannelState>& GetChannelStates(void) const;
	NoteVisualization* GetNoteVis(void);
	void HandleRawEvent(size_t dataLen, const UINT8* data);
	
	void AdvanceManualTiming(UINT64 time, INT8 mode);	// mode: 0 - set, 1 - accumulate, -1 - set mode
	void DoPlaybackStep(void);
private:
	UINT64 Timer_GetTime(void) const;
	void SendMidiEventS(size_t portID, UINT8 event, UINT8 data1, UINT8 data2);	// short MIDI event
	void SendMidiEventL(size_t portID, size_t dataLen, const void* data);	// long MIDI event
	
	const INS_BANK* SelectInsMap(UINT8 moduleType, UINT8* insMapModule) const;
	static bool tempo_compare(const TempoChg& first, const TempoChg& second);
	static bool timesig_compare(const MidiPlayer::TimeSigChg& first, const MidiPlayer::TimeSigChg& second);
	static bool keysig_compare(const MidiPlayer::KeySigChg& first, const MidiPlayer::KeySigChg& second);
	void PrepareMidi(void);
	void RefreshSrcDevSettings(void);
	void InitChannelAssignment(void);
	void InitializeChannels(void);
	void InitializeChannels_Post(void);
	void RefreshTickTime(void);
	static void CalcMeasureTime(const TimeSigChg& tsc, UINT32 ticksWhole, UINT32 tickPos,
								UINT32* mtBar, UINT32* mtBeat, UINT32* mtTick);
	void DoEvent(TrackState* trkState, const MidiEvent* midiEvt);
	void ProcessEventQueue(bool flush = false);
	void EvtQueue_OptimizePortEvts(std::queue<MidiQueueEvt>& meq, INT64 dtMove);
	void EvtQueue_OptimizeChnEvts(std::vector<MidiQueueEvt>& meList, INT64 dtMove, UINT64 limitMinTime);
	void UpdateSongCtrlEvts(void);
	void ForceNoteOff(ChannelState* chnSt, UINT8 note);
	bool HandleNoteEvent(ChannelState* chnSt, const TrackState* trkSt, const MidiEvent* midiEvt);
	bool HandleControlEvent(ChannelState* chnSt, const TrackState* trkSt, const MidiEvent* midiEvt);
	void HandleIns_CommonPatches(const ChannelState* chnSt, InstrumentInfo* insInf, UINT8 devType, const INS_BANK* insBank);
	void HandleIns_DoFallback(const ChannelState* chnSt, InstrumentInfo* insInf, UINT8 devType, UINT8 maxModuleID, const INS_BANK* insBank);
	void HandleIns_GetOriginal(const ChannelState* chnSt, InstrumentInfo* insInf);
	void HandleIns_GetRemapped(const ChannelState* chnSt, InstrumentInfo* insInf);
	bool HandleInstrumentEvent(ChannelState* chnSt, const MidiEvent* midiEvt, UINT8 noact = 0x00);
	void DoChangedPartMode(ChannelState* chnSt, UINT8 moduleType);
	void DoChangedPartMode_Post(void);
	bool NeedMasterVolRemap(UINT8 syxType);
	bool HandleSysExMessage(const TrackState* trkSt, const MidiEvent* midiEvt);
	bool HandleSysEx_MT32(UINT8 portID, size_t syxSize, const UINT8* syxData);
	bool HandleSysEx_GS(UINT8 portID, size_t syxSize, const UINT8* syxData);
	bool HandleSysEx_XG(UINT8 portID, size_t syxSize, const UINT8* syxData);
	void AllNotesStop(void);
	void AllNotesRestart(void);
	void AllInsRefresh(void);
	UINT8 CalcSimpleChnMainVol(const ChannelState* chnSt) const;
	void FadeVolRefresh(size_t portID = (size_t)-1);
	void AllChannelRefresh(void);
	void SaveLoopState(LoopPoint& lp, const TrackState* loopMarkTrk = NULL);
	void RestoreLoopState(const LoopPoint& lp);
	
	bool _playing;
	bool _paused;
	bool _useManualTiming;
	
	MidiFile* _cMidi;
	UINT64 _songLength;
	UINT32 _songTickLen;
	UINT32 _songMeasLen[3];	// song length in [bars, beats, ticks]
	UINT8 _statsTimeSig[3];	// max. numerator, max. denominator, min. denominator (for max. ticks/beat)
	std::list<TempoChg> _tempoList;
	std::list<TimeSigChg> _timeSigList;
	std::list<KeySigChg> _keySigList;
	const INS_BANK* _insBankGM1;
	const INS_BANK* _insBankGM2;
	const INS_BANK* _insBankGS;
	const INS_BANK* _insBankXG;
	const INS_BANK* _insBankYGS;	// Yamaha GS (TG300B mode)
	const INS_BANK* _insBankKorg;
	const INS_BANK* _insBankMT32;
	
	PlayerOpts _options;
	
	std::vector<size_t> _portMap;	// MIDI track port -> ID of MIDIOUT_PORT object
	std::vector<MIDIOUT_PORT*> _outPorts;
	std::vector<UINT32> _outPortDelay;	// delay (in ms) for all event on this port (for sync'ing HW/SW)
	std::vector<UINT16> _portChnMask;	// delay (in ms) for all event on this port (for sync'ing HW/SW)
	MidiModOpts _portOpts;
	std::vector< std::queue<MidiQueueEvt> > _midiEvtQueue;
	
	OS_TIMER* _osTimer;
	UINT64 _tmrFreq;		// number of virtual timer ticks for 1 second
	UINT64 _tmrStep;		// timestamp: next update of sequence processor
	UINT64 _tmrMinStart;	// timestamp when the song should start playing (for initialization delay)
	UINT64 _tmrFadeStart;	// timestamp: beginning of fade out (-1 -> start with next update)
	UINT64 _tmrFadeLen;		// duration of fade out (in timer ticks)
	UINT64 _tmrFadeNext;	// timestamp: next fade out update
	UINT64 _manTimeTick;
	std::list<TempoChg>::const_iterator _tempoPos;
	std::list<TimeSigChg>::const_iterator _timeSigPos;
	std::list<KeySigChg>::const_iterator _keySigPos;
	
	UINT8 _defSrcInsMap;	// default instrument map of source device
							// 00..0F when set via MIDI
							// 80..8F when set based on detected device
							// FF when not set
	UINT8 _defDstInsMap;	// default instrument map of destination device (for GM -> GS/XG mapping)
	UINT8 _defPbRange;
	std::vector<TrackState> _trkStates;
	std::vector<ChannelState> _chnStates;
	UINT8 _mstVol;			// master volume, according to SysEx
	UINT8 _mstVolFade;		// master volume, after applying FadeOut value
	
	UINT8 _pixelPageMem[10][0x40];
	InstrumentInfo _sc88usrIns[0x100];
	std::string _sc88UsrDrmNames[2];
	std::string _mt32TimbreNames[0x40];
	UINT8 _mt32PatchTGrp[0x80];	// 0x00 - preset A, 0x01 - preset B, 0x02 - internal?, 0x03 - rhythm?
	UINT8 _mt32PatchTNum[0x80];	// 0x00 .. 0x3F
	UINT8 _cm32pPatchTMedia[0x80];	// 0x00 - internal, 0x01 - card
	UINT8 _cm32pPatchTNum[0x80];	// 0x00 .. 0x7F
	
	NoteVisualization _noteVis;
	LoopPoint _loopPt;
	UINT32 _curLoop;
	UINT8 _rcpMidTextMode;
	UINT8 _karaokeMode;
	UINT16 _softKarTrack;
	bool _breakMidiProc;
	bool _hardReset;		// enforce "hard" reset (resets custom instrument maps)
	bool _initChnPost;
	UINT16 _partModeChg_PortChnID;
	UINT8 _partModeChg_ModType;	// 0xFF = no action, 0x00..0x7F = MIDI type
	bool _meqDoSort;		// MIDI Event Queue: do resorting
	UINT32 _midiTempo;
	UINT8 _midiTimeSig[4];	// numerator, denominator (pow2), metronome pulse, 32nd notes per beat
	INT8 _midiKeySig[2];	// number of sharps/flats, scale (major/minor)
	UINT32 _curEvtTick;
	UINT32 _nextEvtTick;
	UINT64 _curTickTime;	// time for 1 MIDI tick at current tempo
	UINT16 _fadeVol;		// current fade out volume (8.8 fixed point)
};

#endif	// __MIDIPLAY_HPP__
