#include <stdtype.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <list>
#include <string>
#include <algorithm>

#include "MidiLib.hpp"
#include "MidiOut.h"
#include "OSTimer.h"
#include "MidiPlay.hpp"
#include "MidiBankScan.hpp"
#include "MidiInsReader.h"	// for MODTYPE_ defines
#include "vis.hpp"
#include "utils.hpp"


#if defined(_MSC_VER) && _MSC_VER < 1300
// VC6 has an implementation for [signed] __int64 -> double
// but support for unsigned __int64 is missing
#define U64_TO_DBL(x)	(double)(INT64)(x)
#else
#define U64_TO_DBL(x)	(double)(x)
#endif
#ifdef _MSC_VER
#define snprintf	_snprintf
#endif

#define TICK_FP_SHIFT	8
#define TICK_FP_MUL		(1 << TICK_FP_SHIFT)

#define FULL_CHN_ID(portID, midChn)	(((portID) << 4) | ((midChn) << 0))

#define BNKBIT_MSB		0
#define BNKBIT_LSB		1
#define BNKBIT_INS		2
#define BNKMSK_NONE		0x00
#define BNKMSK_MSB		(1 << BNKBIT_MSB)	// 0x01
#define BNKMSK_LSB		(1 << BNKBIT_LSB)	// 0x02
#define BNKMSK_ALLBNK	(BNKMSK_MSB | BNKMSK_LSB)
#define BNKMSK_INS		(1 << BNKBIT_INS)	// 0x04

static const UINT8 PART_ORDER[0x10] =
{	0x9, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};

static const UINT8 RESET_GM1[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
static const UINT8 RESET_GM2[] = {0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7};
static const UINT8 RESET_GS[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};
static const UINT8 RESET_SC[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x00, 0x00, 0x7F, 0x00, 0x01, 0xF7};
static const UINT8 RESET_XG[] = {0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7};

extern UINT8 optShowInsChange;

MidiPlayer::MidiPlayer() :
	_cMidi(NULL), _songLength(0),
	_insBankGM1(NULL), _insBankGM2(NULL), _insBankGS(NULL), _insBankXG(NULL), _insBankYGS(NULL), _insBankMT32(NULL),
	_evtCbFunc(NULL), _evtCbData(NULL)
{
	_osTimer = OSTimer_Init();
	_tmrFreq = OSTimer_GetFrequency(_osTimer) << TICK_FP_SHIFT;
}

MidiPlayer::~MidiPlayer()
{
	OSTimer_Deinit(_osTimer);
}

void MidiPlayer::SetMidiFile(MidiFile* midiFile)
{
	_songLength = 0;
	_tempoList.clear();
	_cMidi = midiFile;
	
	PrepareMidi();
	
	return;
}

void MidiPlayer::SetOutputPort(MIDIOUT_PORT* outPort)
{
	_outPorts.clear();
	_outPorts.push_back(outPort);
	_chnStates.resize(_outPorts.size() * 0x10);
	_noteVis.Initialize(_outPorts.size());
	return;
}

void MidiPlayer::SetOutputPorts(const std::vector<MIDIOUT_PORT*>& outPorts)
{
	_outPorts = outPorts;
	_chnStates.resize(_outPorts.size() * 0x10);
	_noteVis.Initialize(_outPorts.size());
	return;
}

void MidiPlayer::SetOutPortMapping(size_t numPorts, const size_t* outPorts)
{
	_portMap = std::vector<size_t>(outPorts, outPorts + numPorts);
}

void MidiPlayer::SetOptions(const PlayerOpts& plrOpts)
{
	_options = plrOpts;
}

const PlayerOpts& MidiPlayer::GetOptions(void) const
{
	return _options;
}

UINT8 MidiPlayer::GetModuleType(void) const
{
	return _options.dstType;
}

void MidiPlayer::SetSrcModuleType(UINT8 modType, bool insRefresh)
{
	_options.srcType = modType;
	if (insRefresh)
		AllInsRefresh();
	
	return;
}

void MidiPlayer::SetEventCallback(MIDI_EVT_CB cbFunc, void* cbData)
{
	_evtCbFunc = cbFunc;
	_evtCbData = cbData;
}

void MidiPlayer::SetInstrumentBank(UINT8 moduleType, const INS_BANK* insBank)
{
	switch(moduleType)
	{
	case MODULE_GM_1:
		_insBankGM1 = insBank;
		return;
	case MODULE_GM_2:
		_insBankGM2 = insBank;
		return;
	case MODULE_TG300B:
		_insBankYGS = insBank;
		return;
	case MODULE_MT32:
		_insBankMT32 = insBank;
		return;
	default:
		switch(MMASK_TYPE(moduleType))
		{
		case MODULE_TYPE_GS:
			_insBankGS = insBank;
			return;
		case MODULE_TYPE_XG:
			_insBankXG = insBank;
			return;
		}
	}
	
	return;
}

const INS_BANK* MidiPlayer::SelectInsMap(UINT8 moduleType, UINT8* insMapModule)
{
	if (insMapModule != NULL)
		*insMapModule = MMASK_MOD(moduleType);
	switch(moduleType)
	{
	case MODULE_GM_1:
		if (_insBankGM1 != NULL)
			return _insBankGM1;
		// do various fallbaks
		if (_insBankGM2 != NULL)
			return _insBankGM2;
		return _insBankGS;
	case MODULE_GM_2:
		if (_insBankGM2 != NULL)
			return _insBankGM2;
		return SelectInsMap(MODULE_GM_1, insMapModule);
	case MODULE_TG300B:
		if (_insBankYGS != NULL)
		{
			if (insMapModule != NULL)
				*insMapModule = 0x00;
			return _insBankYGS;
		}
		// TG300B instrument is very similar to the SC-88 one
		return SelectInsMap(MODULE_SC88, insMapModule);
	case MODULE_MT32:
		if (insMapModule != NULL)
			*insMapModule = 0x00;
		return _insBankMT32;
	}
	if (MMASK_TYPE(moduleType) == MODULE_TYPE_GS)
		return _insBankGS;
	else if (MMASK_TYPE(moduleType) == MODULE_TYPE_XG)
		return _insBankXG;
	
	return NULL;
}

UINT8 MidiPlayer::Start(void)
{
	if (! _outPorts.size())
		return 0xF1;
	if (! _cMidi->GetTrackCount())
		return 0xF0;
	if (_chnStates.empty())
		return 0xF2;
	
	size_t curTrk;
	
	_loopPt.used = false;
	_curLoop = 0;
	
	_trkStates.clear();
	for (curTrk = 0; curTrk < _cMidi->GetTrackCount(); curTrk ++)
	{
		MidiTrack* mTrk = _cMidi->GetTrack(curTrk);
		TrackState mTS;
		
		mTS.trkID = curTrk;
		mTS.portID = 0;
		mTS.endPos = mTrk->GetEventEnd();
		mTS.evtPos = mTrk->GetEventBegin();
		_trkStates.push_back(mTS);
	}
	
	_midiTempo = 500000;	// default tempo
	RefreshTickTime();
	
	_nextEvtTick = 0;
	_tmrStep = 0;
	
	InitializeChannels();
	if (_options.flags & PLROPTS_RESET)
	{
		size_t curPort;
		vis_addstr("Sending Device Reset");
		if (_options.dstType == MODULE_MT32)
		{
			// MT-32 mode - nothing to do right now
		}
		else if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_GM)
		{
			// send GM reset
			if (MMASK_MOD(_options.dstType) == MTGM_LVL2)
			{
				for (curPort = 0; curPort < _outPorts.size(); curPort ++)
					MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_GM2), RESET_GM2);
			}
			else //if (MMASK_MOD(_options.dstType) == MTGM_LVL1)
			{
				for (curPort = 0; curPort < _outPorts.size(); curPort ++)
					MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_GM1), RESET_GM1);
			}
		}
		else if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
		{
			// send GS reset
			if (MMASK_MOD(_options.dstType) >= MTGS_SC88 && MMASK_MOD(_options.dstType) != MTGS_TG300B)
			{
				for (curPort = 0; curPort < _outPorts.size(); curPort ++)
					MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_SC), RESET_SC);
			}
			else
			{
				for (curPort = 0; curPort < _outPorts.size(); curPort ++)
					MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_GS), RESET_GS);
			}
		}
		else if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_XG)
		{
			// send XG reset
			for (curPort = 0; curPort < _outPorts.size(); curPort ++)
			{
				MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_GM1), RESET_GM1);
				MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_XG), RESET_XG);
			}
		}
	}
	InitializeChannels_Post();
	_playing = true;
	_paused = false;
	
	return 0x00;
}

UINT8 MidiPlayer::Stop(void)
{
	AllNotesStop();
	
	_playing = false;
	_paused = false;
	return 0x00;
}

UINT8 MidiPlayer::Pause(void)
{
	if (! _playing)
		return 0xFF;
	if (_paused)
		return 0x00;
	
	AllNotesStop();
	
	_paused = true;
	return 0x00;
}

UINT8 MidiPlayer::Resume(void)
{
	if (! _playing)
		return 0xFF;
	if (! _paused)
		return 0x00;
	
	AllNotesRestart();
	
	_tmrStep = 0;
	_paused = false;
	return 0x00;
}

UINT8 MidiPlayer::GetState(void) const
{
	return (_playing << 0) | (_paused << 1);
}

double MidiPlayer::GetSongLength(void) const
{
	return U64_TO_DBL(_songLength) / U64_TO_DBL(_tmrFreq);
}

double MidiPlayer::GetPlaybackPos(void) const
{
	UINT64 curTime;
	UINT64 tmrTick;
	std::list<TempoChg>::const_iterator tempoIt;
	
	curTime = OSTimer_GetTime(_osTimer) << TICK_FP_SHIFT;
	tmrTick = 0;
	for (tempoIt = _tempoList.begin(); tempoIt != _tempoList.end(); ++tempoIt)
	{
		if (tempoIt->tick > _nextEvtTick)
			break;	// stop when going too far
	}
	--tempoIt;
	// I just assume that _curTickTime is correct.
	tmrTick = tempoIt->tmrTick + (_nextEvtTick - tempoIt->tick) * _curTickTime;
	if (curTime < _tmrStep)
		tmrTick -= (_tmrStep - curTime);
	return U64_TO_DBL(tmrTick) / U64_TO_DBL(_tmrFreq);
}

const std::vector<MidiPlayer::ChannelState>& MidiPlayer::GetChannelStates(void) const
{
	return _chnStates;
}

NoteVisualization* MidiPlayer::GetNoteVis(void)
{
	return &_noteVis;
}


void MidiPlayer::RefreshTickTime(void)
{
	UINT64 tmrMul;
	UINT64 tmrDiv;
	
	tmrMul = _tmrFreq * _midiTempo;
	tmrDiv = (UINT64)1000000 * _cMidi->GetMidiResolution();
	_curTickTime = (tmrMul + tmrDiv / 2) / tmrDiv;
	return;
}

void MidiPlayer::DoEvent(TrackState* trkState, const MidiEvent* midiEvt)
{
	MIDIOUT_PORT* outPort = _outPorts[trkState->portID];
	
	if (midiEvt->evtType < 0xF0)
	{
		UINT8 evtType = midiEvt->evtType & 0xF0;
		UINT8 evtChn = midiEvt->evtType & 0x0F;
		UINT16 chnID = FULL_CHN_ID(trkState->portID, evtChn);
		ChannelState* chnSt = &_chnStates[chnID];
		bool didEvt = false;
		
		switch(evtType)
		{
		case 0x80:
		case 0x90:
			didEvt = HandleNoteEvent(chnSt, trkState, midiEvt);
			break;
		case 0xB0:
			didEvt = HandleControlEvent(chnSt, trkState, midiEvt);
			break;
		case 0xC0:
			didEvt = HandleInstrumentEvent(chnSt, midiEvt);
			break;
		case 0xE0:
			{
				NoteVisualization::ChnInfo* nvChn = _noteVis.GetChannel(chnID);
				INT32 pbVal = (midiEvt->evtValB << 7) | (midiEvt->evtValA << 0);
				pbVal = (pbVal - 0x2000) * nvChn->_pbRange;	// 0x2000 per semitone
				nvChn->_attr.detune[0] = (INT16)(pbVal / 0x20);	// make 8.8 fixed point
			}
			break;
		}
		if (! didEvt)
			MidiOutPort_SendShortMsg(outPort, midiEvt->evtType, midiEvt->evtValA, midiEvt->evtValB);
		if (_evtCbFunc != NULL)
			_evtCbFunc(_evtCbData, midiEvt, chnID);
		return;
	}
	
	switch(midiEvt->evtType)
	{
	case 0xF0:	// SysEx
		if (midiEvt->evtData.size() < 0x03)
			break;	// ignore invalid/empty SysEx messages
		{
			if (HandleSysExMessage(trkState, midiEvt))
				break;
			
			std::vector<UINT8> msgData(0x01 + midiEvt->evtData.size());
			msgData[0x00] = midiEvt->evtType;
			memcpy(&msgData[0x01], &midiEvt->evtData[0x00], midiEvt->evtData.size());
			MidiOutPort_SendLongMsg(outPort, msgData.size(), &msgData[0x00]);
		}
		if (_evtCbFunc != NULL)
			_evtCbFunc(_evtCbData, midiEvt, FULL_CHN_ID(trkState->portID, 0x00));
		if (_initChnPost)
			InitializeChannels_Post();
		break;
	case 0xF7:	// SysEx continuation
		{
			std::vector<UINT8> msgData(0x01 + midiEvt->evtData.size());
			msgData[0x00] = midiEvt->evtType;
			memcpy(&msgData[0x01], &midiEvt->evtData[0x00], midiEvt->evtData.size());
			MidiOutPort_SendLongMsg(outPort, msgData.size(), &msgData[0x00]);
		}
		break;
	case 0xFF:	// Meta Event
		switch(midiEvt->evtValA)
		{
		case 0x03:	// Text/Sequence Name
			if (trkState->trkID == 0 || _cMidi->GetMidiFormat() == 2)
			{
				std::string text = Vector2String(midiEvt->evtData);
				//printf("Text: %s\n", text.c_str());
			}
			break;
		case 0x06:	// Marker
			{
				std::string text = Vector2String(midiEvt->evtData);
				//printf("Marker: %s\n", text.c_str());
				if (text == "loopStart")
				{
					vis_addstr("loopStart found.");
					SaveLoopState(_loopPt, trkState);
				}
				else if (text == "loopEnd")
				{
					if (_loopPt.used && _loopPt.tick < _nextEvtTick)
					{
						_curLoop ++;
						if (! _numLoops || _curLoop < _numLoops)
						{
							vis_printf("Loop %u / %u\n", 1 + _curLoop, _numLoops);
							_breakMidiProc = true;
							RestoreLoopState(_loopPt);
						}
					}
				}
			}
			break;
		case 0x21:	// MIDI Port
			if (midiEvt->evtData.size() >= 1)
			{
				trkState->portID = midiEvt->evtData[0];
				// if a port map is defined, apply MIDI port -> output port mapping
				// else use the raw ID from the MIDI file
				if (_portMap.size() > 0)
					trkState->portID = (trkState->portID < _portMap.size()) ? _portMap[trkState->portID] : 0;
				// for invalid port IDs, default to the first one
				if (trkState->portID >= _outPorts.size())
					trkState->portID = 0;
			}
			break;
		case 0x2F:	// Track End
			trkState->evtPos = trkState->endPos;
			break;
		case 0x51:	// Trk End
			_midiTempo = (midiEvt->evtData[0x00] << 16) |
					(midiEvt->evtData[0x01] <<  8) |
					(midiEvt->evtData[0x02] <<  0);
			RefreshTickTime();
			break;
		}
		if (_evtCbFunc != NULL)
			_evtCbFunc(_evtCbData, midiEvt, trkState->trkID);
		break;
	}
	
	return;
}

void MidiPlayer::DoPlaybackStep(void)
{
	if (_paused)
		return;
	
	UINT64 curTime;
	
	curTime = OSTimer_GetTime(_osTimer) << TICK_FP_SHIFT;
	while(_playing)
	{
		UINT32 minTStamp = (UINT32)-1;
		
		size_t curTrk;
		for (curTrk = 0; curTrk < _trkStates.size(); curTrk ++)
		{
			TrackState* mTS = &_trkStates[curTrk];
			if (mTS->evtPos == mTS->endPos)
				continue;
			
			if (minTStamp > mTS->evtPos->tick)
				minTStamp = mTS->evtPos->tick;
		}
		if (minTStamp == (UINT32)-1)
		{
			if (_loopPt.used && _loopPt.tick < _nextEvtTick)
			{
				_curLoop ++;
				if (! _numLoops || _curLoop < _numLoops)
				{
					vis_printf("Loop %u / %u\n", 1 + _curLoop, _numLoops);
					RestoreLoopState(_loopPt);
					continue;
				}
			}
			_playing = false;
			break;
		}
		
		if (minTStamp > _nextEvtTick)
		{
			_tmrStep += (minTStamp - _nextEvtTick) * _curTickTime;
			_nextEvtTick = minTStamp;
		}
		
		if (_tmrStep > curTime)
			break;
		if (_tmrStep + _tmrFreq < curTime)
			_tmrStep = curTime;	// reset time when lagging behind >= 1 second
		
		_breakMidiProc = false;
		for (curTrk = 0; curTrk < _trkStates.size(); curTrk ++)
		{
			TrackState* mTS = &_trkStates[curTrk];
			while(mTS->evtPos != mTS->endPos && mTS->evtPos->tick <= _nextEvtTick)
			{
				DoEvent(mTS, &*mTS->evtPos);
				if (_breakMidiProc || mTS->evtPos == mTS->endPos)
					break;
				++mTS->evtPos;
			}
			if (_breakMidiProc)
				break;
		}
	}
	
	return;
}

bool MidiPlayer::HandleNoteEvent(ChannelState* chnSt, const TrackState* trkSt, const MidiEvent* midiEvt)
{
	UINT8 evtType = midiEvt->evtType & 0xF0;
	UINT8 evtChn = midiEvt->evtType & 0x0F;
	NoteVisualization::ChnInfo* nvChn = _noteVis.GetChannel(FULL_CHN_ID(chnSt->portID, chnSt->midChn));
	
	if ((evtType & 0xE0) != 0x80)
		return false;	// must be Note On or Note Off
	
	if ((evtType & 0x10) && midiEvt->evtValB > 0x00)
	{
		// Note On (90 xx 01..7F)
		NoteInfo nData;
		nData.chn = evtChn;
		nData.note = midiEvt->evtValA;
		nData.vel = midiEvt->evtValB;
		nData.srcTrk = trkSt->trkID;
		chnSt->notes.push_back(nData);
		
		// make sure the list doesn't grow endlessly with buggy MIDIs
		if (chnSt->notes.size() >= 0x80)
		{
			std::list<NoteInfo>::iterator ntIt;
			ntIt = chnSt->notes.begin();
			std::advance(ntIt, 0x80 - 0x20);
			chnSt->notes.erase(chnSt->notes.begin(), ntIt);
		}
		nvChn->AddNote(midiEvt->evtValA, midiEvt->evtValB);
	}
	else
	{
		// Note Off (80 xx xx / 90 xx 00)
		std::list<NoteInfo>::iterator ntIt;
		for (ntIt = chnSt->notes.begin(); ntIt != chnSt->notes.end(); ++ntIt)
		{
			if (ntIt->chn == evtChn && ntIt->note == midiEvt->evtValA)
			{
				chnSt->notes.erase(ntIt);
				break;
			}
		}
		nvChn->RemoveNote(midiEvt->evtValA);
	}
	
	return false;
}

bool MidiPlayer::HandleControlEvent(ChannelState* chnSt, const TrackState* trkSt, const MidiEvent* midiEvt)
{
	MIDIOUT_PORT* outPort = _outPorts[chnSt->portID];
	NoteVisualization::ChnInfo* nvChn = _noteVis.GetChannel(FULL_CHN_ID(chnSt->portID, chnSt->midChn));
	UINT8 ctrlID = midiEvt->evtValA;
	
	if (ctrlID == chnSt->idCC[0])
		ctrlID = 0x10;
	else if (ctrlID == chnSt->idCC[1])
		ctrlID = 0x11;
	
	chnSt->ctrls[ctrlID] = midiEvt->evtValB;
	switch(ctrlID)
	{
	case 0x00:	// Bank MSB
		chnSt->insState[0] = chnSt->ctrls[0x00];
		break;
	case 0x20:	// Bank LSB
		chnSt->insState[1] = chnSt->ctrls[0x20];
		break;
	case 0x07:	// Main Volume
		// if (Fading) then calculate new volume + send event + return true
		nvChn->_attr.volume = midiEvt->evtValB;
		break;
	case 0x0A:	// Pan
		{
			UINT8 panVal = midiEvt->evtValB;
			if (_options.srcType == MODULE_MT32)
				panVal ^= 0x7F;	// MT-32 uses 0x7F (left) .. 0x3F (center) .. 0x00 (right)
			nvChn->_attr.pan = (INT8)panVal - 0x40;
			if ((_options.flags & PLROPTS_STRICT) && MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
			{
				// MT-32 on GS: send GM-compatible Pan value
				MidiOutPort_SendShortMsg(outPort, midiEvt->evtType, ctrlID, panVal);
				return true;
			}
		}
		break;
	case 0x0B:	// Expression
		nvChn->_attr.expression = midiEvt->evtValB;
		break;
	case 0x06:	// Data Entry MSB
		if (chnSt->rpnCtrl[0] == 0x00)
		{
			switch(chnSt->rpnCtrl[1])
			{
			case 0x00:	// Pitch Bend Range
				chnSt->pbRange = midiEvt->evtValB;
				if (chnSt->pbRange > 24)
					chnSt->pbRange = 24;
				nvChn->_pbRange = chnSt->pbRange;
				break;
			case 0x01:	// Fine Tuning
				chnSt->tuneFine &= ~0xFF00;
				chnSt->tuneFine |= ((INT16)midiEvt->evtValB - 0x40) << 8;
				nvChn->_transpose = (INT8)(chnSt->tuneFine >> 8);
				nvChn->_attr.detune[1] = (nvChn->_transpose << 8) | (nvChn->_transpose << 0);
				break;
			case 0x02:	// Coarse Tuning
				chnSt->tuneCoarse = (INT8)midiEvt->evtValB - 0x40;
				if (chnSt->tuneCoarse < -24)
					chnSt->tuneCoarse = -24;
				else if (chnSt->tuneCoarse > +24)
					chnSt->tuneCoarse = +24;
				nvChn->_transpose = chnSt->tuneCoarse;
				nvChn->_attr.detune[1] = (nvChn->_transpose << 8) | (nvChn->_transpose << 0);
				break;
			}
		}
		break;
	case 0x26:	// Data Entry MSB
		if (chnSt->rpnCtrl[0] == 0x00)
		{
			switch(chnSt->rpnCtrl[1])
			{
			case 0x01:	// Fine Tuning
				chnSt->tuneFine &= ~0x00FF;
				chnSt->tuneFine |= midiEvt->evtValB << 1;
				break;
			}
		}
		break;
	case 0x62:	// NRPN LSB
		chnSt->rpnCtrl[1] = 0x80 | midiEvt->evtValB;
		break;
	case 0x63:	// NRPN MSB
		chnSt->rpnCtrl[0] = 0x80 | midiEvt->evtValB;
		if (true)
			break;
		if (midiEvt->evtValB == 20)
		{
			vis_addstr("NRPN Loop Start found.");
			SaveLoopState(_loopPt, trkSt);
		}
		else if (midiEvt->evtValB == 30)
		{
			if (_loopPt.used && _loopPt.tick < _nextEvtTick)
			{
				_curLoop ++;
				if (! _numLoops || _curLoop < _numLoops)
				{
					vis_printf("Loop %u / %u\n", 1 + _curLoop, _numLoops);
					_breakMidiProc = true;
					RestoreLoopState(_loopPt);
				}
			}
		}
		break;
	case 0x64:	// RPN LSB
		chnSt->rpnCtrl[1] = 0x00 | midiEvt->evtValB;
		break;
	case 0x65:	// RPN MSB
		chnSt->rpnCtrl[0] = 0x00 | midiEvt->evtValB;
		break;
	case 0x6F:	// RPG Maker loop controller
		if (midiEvt->evtValB == 0 || midiEvt->evtValB == 111 || midiEvt->evtValB == 127)
		{
			if (! _loopPt.used)
			{
				vis_addstr("Loop Point found.");
				SaveLoopState(_loopPt, trkSt);
			}
		}
		else
		{
			vis_printf("Ctrl 111, value %u.", midiEvt->evtValB);
		}
		break;
	case 0x79:	// Reset All Controllers
		chnSt->ctrls[0x01] = 0x00;	// Modulation
		chnSt->ctrls[0x07] = 100;	// Volume
		chnSt->ctrls[0x0A] = 0x40;	// Pan
		chnSt->ctrls[0x0B] = 0x7F;	// Expression
		chnSt->ctrls[0x40] = 0x00;	// Sustain/Hold1
		chnSt->ctrls[0x41] = 0x00;	// Portamento
		chnSt->ctrls[0x42] = 0x00;	// Sostenuto
		chnSt->ctrls[0x43] = 0x00;	// Soft Pedal
		chnSt->rpnCtrl[0] = 0xFF;	// reset RPN state
		chnSt->rpnCtrl[1] = 0xFF;
		chnSt->pbRange = 2;
		if (_options.srcType == MODULE_MT32)
			chnSt->pbRange = 12;
		nvChn->_attr.volume = chnSt->ctrls[0x07];
		nvChn->_attr.pan = (INT8)chnSt->ctrls[0x0A] - 0x40;
		nvChn->_attr.expression = chnSt->ctrls[0x0B];
		nvChn->_pbRange = chnSt->pbRange;
		break;
	case 0x7B:	// All Notes Off
		chnSt->notes.clear();
		nvChn->ClearNotes();
		if (_evtCbFunc != NULL)
		{
			MidiEvent noteEvt = MidiTrack::CreateEvent_Std(0x01, 0x01, 0x00);	// redraw channel
			_evtCbFunc(_evtCbData, &noteEvt, FULL_CHN_ID(chnSt->portID, chnSt->midChn));
		}
		break;
	}
	
	if (ctrlID != midiEvt->evtValA)
		MidiOutPort_SendShortMsg(outPort, midiEvt->evtType, ctrlID, midiEvt->evtValB);
	
	return false;
}

static const INS_DATA* GetInsMapData(const INS_PRG_LST* insPrg, UINT8 msb, UINT8 lsb, UINT8 maxModuleID)
{
	const INS_DATA* insData;
	UINT32 curIns;
	
	for (curIns = 0; curIns < insPrg->count; curIns ++)
	{
		insData = &insPrg->instruments[curIns];
		if (msb == 0xFF || insData->bankMSB == msb)
		{
			if (lsb == 0xFF || insData->bankLSB == lsb)
			{
				if (insData->moduleID == maxModuleID)
					return insData;
			}
		}
	}
	for (curIns = 0; curIns < insPrg->count; curIns ++)
	{
		insData = &insPrg->instruments[curIns];
		if (msb == 0xFF || insData->bankMSB == msb)
		{
			if (lsb == 0xFF || insData->bankLSB == lsb)
			{
				if (insData->moduleID <= maxModuleID)
					return insData;
			}
		}
	}
	
	return NULL;
}

static const INS_DATA* GetExactInstrument(const INS_BANK* insBank, const MidiPlayer::InstrumentInfo* insInf, UINT8 maxModuleID, UINT8 bankIgnore)
{
	const INS_DATA* insData;
	UINT8 msb;
	UINT8 lsb;
	UINT8 ins;
	
	if (insBank == NULL)
		return NULL;
	
	// try exact match first
	insData = GetInsMapData(&insBank->prg[insInf->ins], insInf->bank[0], insInf->bank[1], maxModuleID);
	if (insData != NULL || ! bankIgnore)
		return insData;
	
	msb = (bankIgnore & BNKMSK_MSB) ? 0xFF : insInf->bank[0];
	lsb = (bankIgnore & BNKMSK_LSB) ? 0xFF : insInf->bank[1];
	ins = (bankIgnore & BNKMSK_INS) ? (insInf->ins & 0x80) : insInf->ins;
	return GetInsMapData(&insBank->prg[ins], msb, lsb, maxModuleID);
}

void MidiPlayer::HandleIns_CommonPatches(const ChannelState* chnSt, InstrumentInfo* insInf, UINT8 devType, UINT8& bankIgnore, const INS_BANK* insBank)
{
	if (devType == MODULE_GM_1)
	{
		bankIgnore = BNKMSK_ALLBNK;
		if (chnSt->flags & 0x80)
			bankIgnore |= BNKMSK_INS;	// there is only 1 drum kit
	}
	else if (MMASK_TYPE(devType) == MODULE_TYPE_GS)
	{
		if (chnSt->flags & 0x80)
			bankIgnore |= BNKMSK_MSB;	// ignore MSB on drum channels
	}
	else if (MMASK_TYPE(devType) == MODULE_TYPE_XG)
	{
		if ((chnSt->flags & 0x80) || insInf->bank[0] == 0x40)
			bankIgnore |= BNKMSK_LSB;	// ignore LSB on drum channels and SFX banks
	}
	else if (devType == MODULE_MT32)
	{
		if (insBank != NULL && insBank->maxBankMSB >= 0x01)
		{
			// when supported by the instrument bank, do CM-32L/P instrument set selection
			bankIgnore = BNKMSK_LSB;
			if (chnSt->midChn <= 0x09)
				insInf->bank[0] = 0x00;	// MT-32/CM-32L set
			else
				insInf->bank[0] = 0x01;	// CM-32P set
		}
		else
		{
			bankIgnore = BNKMSK_ALLBNK;
		}
		if (chnSt->flags & 0x80)
			bankIgnore |= BNKMSK_INS;	// there is only 1 drum kit
	}
	else
	{
		// generic handler
		if (insBank == NULL || insBank->maxBankMSB == 0x00)
			bankIgnore |= BNKMSK_MSB;
		if (insBank == NULL || insBank->maxBankLSB == 0x00)
			bankIgnore |= BNKMSK_LSB;
		if ((chnSt->flags & 0x80) && (insBank == NULL || insBank->maxDrumKit == 0x00))
			bankIgnore |= BNKMSK_INS;
	}
	
	return;
}

void MidiPlayer::HandleIns_DoFallback(const ChannelState* chnSt, InstrumentInfo* insInf, UINT8 devType, const INS_BANK* insBank, UINT8& bankIgnore)
{
	if (chnSt->userInsID != 0xFFFF)
		return;
	
	if (MMASK_TYPE(devType) == MODULE_TYPE_GS)
	{
		if (devType == MODULE_SC55)
		{
			// This implements the Capital Tone Fallback mode from SC-55 v1.
			if (! (chnSt->flags & 0x80))
			{
				// melody mode
				if (insBank != NULL)
				{
					const INS_PRG_LST* insPrg = &insBank->prg[insInf->ins];
				
					// 1. sub-CTF according to https://www.vogons.org/viewtopic.php?p=501280#p501280
					insInf->bank[0] &= ~0x07;
					if (GetInsMapData(insPrg, insInf->bank[0], insInf->bank[1], 0xFF) != NULL)
						return;
				}
				
				// 2. fall back to GM sound
				insInf->bank[0] = 0x00;
			}
			else
			{
				// drum CTF according to https://www.vogons.org/viewtopic.php?p=501038#p501038
				insInf->ins &= ~0x07;
			}
		}
		else if (devType == MODULE_TG300B)
		{
			// Yamaha's CTF is simple and very similar to the XG fallback.
			if (! (chnSt->flags & 0x80))
			{
				insInf->bank[0] = 0x00;
			}
		}
		else
		{
			// SC-88 and later simply ignores the instrument change
		}
	}
	else if (MMASK_TYPE(devType) == MODULE_TYPE_XG)
	{
		// XG has capital tone fallback by setting Bank LSB to 00
		// for Bank MSB 0, this results in GM sounds.
		// for Bank MSB 64/126/127, LSB isn't used anyway
		// for all unused/invalid Bank MSB, it results in silence
		insInf->bank[1] = 0x00;
	}
	else
	{
		// generic fallback code
		bankIgnore |= BNKMSK_ALLBNK;
	}
	
	return;
}

void MidiPlayer::HandleIns_GetOriginal(const ChannelState* chnSt, InstrumentInfo* insInf)
{
	const INS_BANK* insBank;
	const UINT8 devType = _options.srcType;
	UINT8 mapModType;
	UINT8 bankIgnore;
	
	insInf->bank[0] = chnSt->ctrls[0x00];
	insInf->bank[1] = chnSt->ctrls[0x20];
	insInf->ins = (chnSt->flags & 0x80) | (chnSt->curIns & 0x7F);
	bankIgnore = BNKMSK_NONE;
	insBank = SelectInsMap(devType, &mapModType);
	
	HandleIns_CommonPatches(chnSt, insInf, devType, bankIgnore, insBank);
	if (MMASK_TYPE(devType) == MODULE_TYPE_GS)
	{
		if (devType == MODULE_SC55 || devType == MODULE_TG300B)
		{
			// SC-55 ignores Bank LSB
			if (insBank != NULL && insBank->maxBankLSB > 0x00)
				insInf->bank[1] = 0x01;	// instrument bank has SC-55/88/... maps - default to the SC-55 one
			else
				bankIgnore |= BNKMSK_LSB;
		}
		else
		{
			// SC-88+: force GS instrument bank LSB to 01+
			if (insInf->bank[1] == 0x00)	// Bank LSB = default bank
			{
				// enforce the device's "native" bank
				if (MMASK_MOD(devType) < MT_UNKNOWN)
					insInf->bank[1] = 0x01 + MMASK_MOD(devType);
				else
					bankIgnore |= BNKMSK_LSB;	// unknown device - find anything
			}
		}
	}
	else if (MMASK_TYPE(devType) == MODULE_TYPE_XG)
	{
		// we're not as strict as in GetRemapped() here
		if (chnSt->flags & 0x80)
		{
			// enforce drum mode
			if (insInf->bank[0] < 0x7E)
				insInf->bank[0] = 0x7F;
		}
	}
	
	insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType, bankIgnore);
	if (insInf->bankPtr == NULL && insBank != NULL)
	{
		if (chnSt->userInsID != 0xFFFF)
		{
			if (MMASK_TYPE(devType) == MODULE_TYPE_GS && ! (chnSt->flags & 0x80))
			{
				InstrumentInfo tmpII;
				tmpII.bank[0] = 0x00;
				tmpII.bank[1] = (insInf->bank[0] == 0x41) ? 0x01 : insInf->bank[1];
				tmpII.ins = insInf->ins;
				insInf->bankPtr = GetExactInstrument(insBank, &tmpII, mapModType, bankIgnore);
			}
		}
		else if (_options.flags & PLROPTS_ENABLE_CTF)
		{
			// handle device-specific fallback modes
			HandleIns_DoFallback(chnSt, insInf, devType, insBank, bankIgnore);
			insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType, bankIgnore);
		}
	}
	
	return;
}

void MidiPlayer::HandleIns_GetRemapped(const ChannelState* chnSt, InstrumentInfo* insInf)
{
	InstrumentInfo insIOld;
	const INS_BANK* insBank;
	const UINT8 devType = _options.dstType;
	UINT8 mapModType;
	UINT8 bankIgnore;
	UINT8 strictPatch;
	
	if (_options.flags & PLROPTS_STRICT)
	{
		*insInf = chnSt->insOrg;
	}
	else
	{
		insInf->bank[0] = chnSt->ctrls[0x00];
		insInf->bank[1] = chnSt->ctrls[0x20];
		insInf->ins = (chnSt->flags & 0x80) | (chnSt->curIns & 0x7F);
	}
	insIOld = *insInf;
	bankIgnore = BNKMSK_NONE;
	strictPatch = BNKMSK_NONE;
	insBank = SelectInsMap(devType, &mapModType);
	
	HandleIns_CommonPatches(chnSt, insInf, devType, bankIgnore, insBank);
	if (MMASK_TYPE(devType) == MODULE_TYPE_GS)
	{
		if (_options.srcType == MODULE_MT32)
		{
			// [conversion] use MT-32 instruments on GS device
			insInf->bank[1] = 0x01 + MTGS_SC55;
			if (chnSt->flags & 0x80)
			{
				insInf->bank[0] = 0x00;
				insInf->ins = 0x7F | 0x80;
			}
			else
			{
				// channels 1-10: MT-32/CM-32L, channels 11-16: CM-32P
				insInf->bank[0] = (chnSt->midChn <= 0x09) ? 0x7F : 0x7E;
			}
		}
		else
		{
			if (insInf->bank[1] == 0x00 || MMASK_TYPE(_options.srcType) != MODULE_TYPE_GS)
			{
				UINT8 defaultDev;
				// GS song: use bank that is optimal for the song
				// GM song: use "native" bank for device
				if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS && MMASK_MOD(_options.srcType) < MT_UNKNOWN)
				{
					defaultDev = MMASK_MOD(_options.srcType);
				}
				else
				{
					defaultDev = MMASK_MOD(devType);
					if (defaultDev == MTGS_SC8850)
						defaultDev = MTGS_SC88PRO;	// TODO: make configurable
				}
				insInf->bank[1] = 0x01 + defaultDev;
				strictPatch |= BNKMSK_LSB;	// mark for undo when not strict
			}
			if (_options.srcType == MODULE_GM_1 && (chnSt->flags & 0x80))
			{
				if (false)	// TODO: make this an option
					insInf->ins = 0x00 | 0x80;	// for GM, enforce to Standard Kit 1
			}
		}
		if (chnSt->flags & 0x80)
		{
			// set (ignored) MSB to 0 on drum channels
			if (_options.flags & PLROPTS_STRICT)
				insInf->bank[0] = 0x00;
		}
		if (insBank != NULL && insBank->maxBankLSB == 0x00)
			bankIgnore |= BNKMSK_LSB;
	}
	else if (MMASK_TYPE(devType) == MODULE_TYPE_XG)
	{
		if (_options.flags & PLROPTS_STRICT)
		{
			if (MMASK_TYPE(_options.srcType) != MODULE_TYPE_XG)
			{
				insInf->bank[0] = (chnSt->flags & 0x80) ? 0x7F : 0x00;
				insInf->bank[1] = 0x00;
			}
			else
			{
				if (chnSt->flags & 0x80)
				{
					// enforce drum mode
					if (insInf->bank[0] < 0x7E)
						insInf->bank[0] = 0x7F;
				}
				else
				{
					// enforce capital tone
					if (insInf->bank[0] >= 0x7E)
						insInf->bank[0] = 0x00;
				}
			}
		}
		if ((chnSt->flags & 0x80) || insInf->bank[0] == 0x40)
		{
			// set (ignored) LSB to 0 on drum channels and SFX banks
			if (_options.flags & PLROPTS_STRICT)
				insInf->bank[1] = 0x00;
		}
	}
	else if (devType == MODULE_MT32)
	{
		strictPatch = ~bankIgnore & BNKMSK_ALLBNK;	// mark for undo when not strict
	}
	
	insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType, bankIgnore);
	if (insInf->bankPtr == NULL && insBank != NULL)
	{
		if (chnSt->userInsID != 0xFFFF)
		{
			if (MMASK_TYPE(devType) == MODULE_TYPE_GS && ! (chnSt->flags & 0x80))
			{
				InstrumentInfo tmpII;
				tmpII.bank[0] = 0x00;
				tmpII.bank[1] = (insInf->bank[0] == 0x41) ? 0x01 : insInf->bank[1];
				tmpII.ins = insInf->ins;
				insInf->bankPtr = GetExactInstrument(insBank, &tmpII, mapModType, bankIgnore);
			}
		}
		else if (_options.flags & PLROPTS_ENABLE_CTF)
		{
			// handle device-specific fallback modes
			UINT8 fbDevType = devType;
			if (true && MMASK_TYPE(fbDevType) == MODULE_TYPE_GS)
				fbDevType = MODULE_SC55;	// use SC-55 fallback method for all GS devices
			HandleIns_DoFallback(chnSt, insInf, fbDevType, insBank, bankIgnore);
			if (MMASK_TYPE(devType) == MODULE_TYPE_XG)
			{
				if (insInf->bank[0] > 0x00 && insInf->bank[0] < 0x40)
					insInf->bank[0] = 0x00;	// additional Bank MSB fallback to prevent sounds from going silent
			}
			insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType, bankIgnore);
		}
	}
	
	if (! (_options.flags & PLROPTS_STRICT))
	{
		if (strictPatch & BNKMSK_MSB)
			insInf->bank[0] = insIOld.bank[0];
		if (strictPatch & BNKMSK_LSB)
			insInf->bank[1] = insIOld.bank[1];
		if (strictPatch & BNKMSK_MSB)
			insInf->ins = insIOld.ins;
	}
	else //if (_options.flags & PLROPTS_STRICT)
	{
		if (devType == MODULE_SC55 || devType == MODULE_TG300B)
		{
			// We had to use LSB 01 for instrument lookup, but we actually send LSB 00.
			// (LSB is ignored on the SC-55 itself, but LSB 00 is required on the Yamaha MU80.)
			insInf->bank[1] = 0x00;
		}
	}
	
	return;
}

bool MidiPlayer::HandleInstrumentEvent(ChannelState* chnSt, const MidiEvent* midiEvt, UINT8 noact)
{
	MIDIOUT_PORT* outPort = _outPorts[chnSt->portID];
	NoteVisualization::ChnInfo* nvChn = _noteVis.GetChannel(FULL_CHN_ID(chnSt->portID, chnSt->midChn));
	UINT8 oldMSB = chnSt->insState[0];
	UINT8 oldLSB = chnSt->insState[1];
	UINT8 oldIns = chnSt->insState[2];
	UINT8 bankMSB = chnSt->ctrls[0x00];
	
	chnSt->curIns = midiEvt->evtValA;
	chnSt->userInsID = 0xFFFF;
	
	// handle user instruments and channel mode changes
	if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
	{
		if (MMASK_MOD(_options.srcType) >= MTGS_SC88 && MMASK_MOD(_options.srcType) != MTGS_TG300B)
		{
			if ((chnSt->flags & 0x80) && (chnSt->curIns == 0x40 || chnSt->curIns == 0x41))	// user drum kit
				chnSt->userInsID = 0x8000 | (chnSt->curIns & 0x01);
			else if (bankMSB == 0x40 || bankMSB == 0x41)	// user instrument
				chnSt->userInsID = ((bankMSB & 0x01) << 7) | chnSt->curIns;
		}
	}
	else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_XG)
	{
		if (bankMSB == 0x3F)
			chnSt->userInsID = chnSt->curIns;	// QS300 user voices (00..1F only)
		if (bankMSB >= 0x7E)	// MSB 7E/7F = drum kits
			chnSt->flags |= 0x80;
		else
			chnSt->flags &= ~0x80;
		if (_options.flags & PLROPTS_STRICT)
		{
			if (chnSt->midChn == 0x09 && ! (chnSt->flags & 0x80))
			{
				// for now enforce drum mode on channel 9
				// TODO: XG allows ch 9 to be melody - what are the exact conditions??
				chnSt->flags |= 0x80;
				vis_addstr("Keeping drum mode on ch 9!");
			}
		}
		nvChn->_chnMode &= ~0x01;
		nvChn->_chnMode |= (chnSt->flags & 0x80) >> 7;
	}
	else if (_options.srcType == MODULE_GM_2)
	{
		if (bankMSB == 0x78)	// drum kits
			chnSt->flags |= 0x80;
		else if (bankMSB == 0x79)	// melody instruments
			chnSt->flags &= ~0x80;
		nvChn->_chnMode &= ~0x01;
		nvChn->_chnMode |= (chnSt->flags & 0x80) >> 7;
	}
	
	HandleIns_GetOriginal(chnSt, &chnSt->insOrg);
	HandleIns_GetRemapped(chnSt, &chnSt->insSend);
	
	if (optShowInsChange && ! (noact & 0x10))
	{
		const char* oldName;
		const char* newName;
		UINT8 ctrlEvt = 0xB0 | chnSt->midChn;
		UINT8 insEvt = midiEvt->evtType;
		UINT8 didPatch = BNKMSK_NONE;
		bool showOrgIns = false;
		
		didPatch |= (chnSt->insSend.bank[0] != chnSt->ctrls[0x00]) << BNKBIT_MSB;
		didPatch |= (chnSt->insSend.bank[1] != chnSt->ctrls[0x20]) << BNKBIT_LSB;
		didPatch |= ((chnSt->insSend.bank[2] & 0x7F) != chnSt->curIns) << BNKBIT_INS;
		if ((_options.flags & PLROPTS_STRICT) && MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
		{
			// only Bank LSB was patched and it was patched from 0 (default instrument map) to the "native" map
			if (didPatch == BNKMSK_LSB && chnSt->ctrls[0x20] == 0x00)
			{
				didPatch = BNKMSK_NONE;	// hide patching default instrument map in strict mode
				showOrgIns = true;
			}
		}
		if (! didPatch && chnSt->insSend.bankPtr == NULL)
			showOrgIns = true;
		
		oldName = (chnSt->insOrg.bankPtr == NULL) ? "" : chnSt->insOrg.bankPtr->insName;
		newName = (chnSt->insSend.bankPtr == NULL) ? "" : chnSt->insSend.bankPtr->insName;
		if (didPatch)
		{
			vis_printf("%s Patch: %02X 00 %02X  %02X 20 %02X  %02X %02X  %s\n",
				(chnSt->flags & 0x80) ? "Drm" : "Ins",
				ctrlEvt, chnSt->ctrls[0x00], ctrlEvt, chnSt->ctrls[0x20], insEvt, chnSt->curIns, oldName);
			vis_printf("       ->  %02X 00 %02X  %02X 20 %02X  %02X %02X  %s\n",
				ctrlEvt, chnSt->insSend.bank[0], ctrlEvt, chnSt->insSend.bank[1], insEvt, chnSt->insSend.ins & 0x7F, newName);
		}
		else if (! showOrgIns)
		{
			vis_printf("%s Set:   %02X 00 %02X  %02X 20 %02X  %02X %02X  %s\n",
				(chnSt->flags & 0x80) ? "Drm" : "Ins",
				ctrlEvt, chnSt->insSend.bank[0], ctrlEvt, chnSt->insSend.bank[1], insEvt, chnSt->insSend.ins & 0x7F, newName);
		}
		else
		{
			vis_printf("%s Set:   %02X 00 %02X  %02X 20 %02X  %02X %02X  %s\n",
				(chnSt->flags & 0x80) ? "Drm" : "Ins",
				ctrlEvt, chnSt->ctrls[0x00], ctrlEvt, chnSt->ctrls[0x20], insEvt, chnSt->curIns, oldName);
		}
	}
	
	// apply new state (insSend is a reference of the instrument, insState keeps the last data sent to the device)
	chnSt->insState[0] = chnSt->insSend.bank[0];
	chnSt->insState[1] = chnSt->insSend.bank[1];
	chnSt->insState[2] = chnSt->insSend.ins & 0x7F;
	
	if (noact & 0x01)
		return false;
	
	// resend Bank MSB/LSB
	if (oldMSB != chnSt->insState[0] || oldLSB != chnSt->insState[1])
	{
		UINT8 evtType = 0xB0 | chnSt->midChn;
		if (oldMSB != chnSt->insState[0])
			MidiOutPort_SendShortMsg(outPort, evtType, 0x00, chnSt->insState[0]);
		if (oldLSB != chnSt->insState[1])
			MidiOutPort_SendShortMsg(outPort, evtType, 0x20, chnSt->insState[1]);
	}
	MidiOutPort_SendShortMsg(outPort, midiEvt->evtType, chnSt->insState[2], 0x00);
	return true;
}

static void SanitizeSysExText(std::string& text)
{
	size_t curChr;
	
	for (curChr = 0; curChr < text.size(); curChr ++)
	{
		if (text[curChr] == '\0')
			text[curChr] = ' ';
	}
	
	return;
}

static void PrintHexDump(size_t bufSize, char* buffer, size_t dataLen, const UINT8* data, size_t maxVals)
{
	size_t curPos;
	size_t remBufSize;
	int printChrs;
	char* bufPtr;
	
	if (maxVals > dataLen)
		maxVals = dataLen;
	if (maxVals > bufSize / 3)
		maxVals = bufSize / 3;
	remBufSize = bufSize;
	bufPtr = buffer;
	for (curPos = 0x00; curPos < maxVals; curPos ++)
	{
		printChrs = snprintf(bufPtr, remBufSize, "%02X ", data[curPos]);
		if (printChrs <= 0 || (size_t)printChrs >= remBufSize)
			break;
		bufPtr += printChrs;
		remBufSize -= printChrs;
	}
	if (bufPtr > buffer && bufPtr[-1] == ' ')
		bufPtr[-1] = '\0';
	
	return;
}

bool MidiPlayer::HandleSysExMessage(const TrackState* trkSt, const MidiEvent* midiEvt)
{
	size_t syxSize = midiEvt->evtData.size();
	const UINT8* syxData = midiEvt->evtData.data();
	
	if (syxSize >= 1 && syxData[0x00] == 0xF0)
	{
		char syxStr[0x10];
		PrintHexDump(0x10, syxStr, midiEvt->evtData.size(), midiEvt->evtData.data(), 0x03);
		vis_printf("Warning: Repeated SysEx start command byte (%02X %s ...)\n", midiEvt->evtType, syxStr);
		
		// skip repeated F0 byte
		while(syxSize >= 1 && syxData[0x00] == 0xF0)
		{
			syxSize --;
			syxData ++;
		}
	}
	if (syxSize >= 1 && (syxData[0x00] & 0x80))
	{
		char syxStr[0x10];
		PrintHexDump(0x10, syxStr, midiEvt->evtData.size(), midiEvt->evtData.data(), 0x03);
		vis_printf("Warning: Can't parse bad SysEx message! (begins with %02X %s ...)\n", midiEvt->evtType, syxStr);
		return false;
	}
	if (syxSize < 0x03)
		return false;	// ignore incomplete SysEx messages
	
	switch(syxData[0x00])
	{
	case 0x41:	// Roland ID
		// Data[0x01] == 0x1n - Device Number n
		// Data[0x02] == Model ID (MT-32 = 0x16, GS = 0x42)
		// Data[0x03] == Command ID (reqest data RQ1 = 0x11, data set DT1 = 0x12)
		if (syxData[0x03] == 0x12)
		{
			if (syxSize < 0x08)
				break;	// We need enough bytes for a full address.
			if (syxData[0x02] == 0x16)
				return HandleSysEx_MT32(trkSt->portID, syxSize, syxData);
			else if (syxData[0x02] == 0x42)
				return HandleSysEx_GS(trkSt->portID, syxSize, syxData);
			else if (syxData[0x02] == 0x45)
			{
				UINT32 addr;
				addr =	(syxData[0x04] << 16) |
						(syxData[0x05] <<  8) |
						(syxData[0x06] <<  0);
				switch(addr & 0xFFFF00)
				{
				case 0x100000:	// ASCII Display
				{
					std::string dispMsg = Vector2String(syxData, 0x07, syxSize - 2);
					
					SanitizeSysExText(dispMsg);
					vis_printf("SC SysEx: Display = \"%s\"", dispMsg.c_str());
				}
					break;
				case 0x100100:	// Dot Display (page 1-10)
				case 0x100200:
				case 0x100300:
				case 0x100400:
				case 0x100500:
				{
					UINT8 pageID;
					
					pageID = (((addr & 0x00FF00) >> 7) | ((addr & 0x000040) >> 6)) - 1;
					if (pageID == 1)
						vis_printf("SC SysEx: Dot Display (Load/Show Page %u)", pageID);
					else
						vis_printf("SC SysEx: Dot Display: Load Page %u", pageID);
				}
					break;
				case 0x102000:
					if (addr == 0x102000)	// Dot Display: show page
					{
						UINT8 pageID;
						
						pageID = syxData[0x07];	// 00 = bar display, 01..0A = page 1..10
						vis_printf("SC SysEx: Dot Display: Show Page %u", pageID);
					}
					else if (addr == 0x102001)	// Dot Display: set display time
					{
						float dispTime;
						
						dispTime = syxData[0x07] * 0.48f;
						vis_printf("SC SysEx: Dot Display: set display time = %.2f sec", dispTime);
					}
					break;
				}
			}
		}
		break;
	case 0x43:	// YAMAHA ID
		// Data[0x01] == 0x1n - Device Number n
		if (syxSize < 0x07)
			break;	// We need enough bytes for a full address.
		if (syxData[0x02] == 0x4C)
			return HandleSysEx_XG(trkSt->portID, syxSize, syxData);
		break;
	case 0x7E:	// Universal Non-Realtime Message
		// GM Lvl 1 On:  F0 7E 7F 09 01 F7
		// GM Lvl 1 Off: F0 7E 7F 09 00 F7
		// GM Lvl 2 On:  F0 7E 7F 09 03 F7
		if (syxData[0x01] == 0x7F && syxData[0x02] == 0x09)
		{
			UINT8 gmMode = syxData[0x03];
			if (gmMode == 0x01)	// GM Level 1 On
			{
				InitializeChannels();
				vis_addstr("SysEx: GM Reset\n");
			}
			else if (gmMode == 0x03)	// GM Level 2 On
			{
				InitializeChannels();
				vis_addstr("SysEx: GM2 Reset\n");
			}
			
			if ((_options.flags & PLROPTS_RESET) && MMASK_TYPE(_options.dstType) != MODULE_TYPE_GM)
				return true;	// prevent GM reset on GS/XG devices
		}
		break;
	case 0x7F:	// Universal Realtime Message
		if (syxData[0x01] == 0x7F && syxData[0x02] == 0x04)
		{
			switch(syxData[0x03])
			{
			case 0x01:	// Master Volume
				// F0 7F 7F 04 01 ll mm F7
				break;
			}
		}
		break;
	}
	
	return false;
}

bool MidiPlayer::HandleSysEx_MT32(UINT8 portID, size_t syxSize, const UINT8* syxData)
{
	UINT32 addr;
	const UINT8* dataPtr;
	
	// Data[0x04]	Address High
	// Data[0x05]	Address Mid
	// Data[0x06]	Address Low
	addr =	(syxData[0x04] << 16) |
			(syxData[0x05] <<  8) |
			(syxData[0x06] <<  0);
	switch(addr & 0xFF0000)	// Address High
	{
	case 0x030000:	// Patch Temporary Area
		if ((addr & 0x0F) > 0x00)
			break;	// Right now we can only handle bulk writes.
		dataPtr = &syxData[0x07];
		if (addr < 0x030110)
		{
			UINT8 evtChn;
			ChannelState* chnSt = NULL;
			UINT8 newIns;
			
			evtChn = 1 + ((addr & 0x0000F0) >> 4);
			chnSt = &_chnStates[FULL_CHN_ID(portID, evtChn)];
			newIns = ((dataPtr[0x00] & 0x03) << 6) | ((dataPtr[0x01] & 0x3F) << 0);
			if (newIns < 0x80)
			{
				const INS_BANK* insBank;
				UINT8 mapModType;
				
				chnSt->curIns = newIns;
				chnSt->userInsID = 0xFFFF;
				//HandleIns_GetOriginal(chnSt, &chnSt->insOrg);
				//HandleIns_GetRemapped(chnSt, &chnSt->insSend);
				insBank = SelectInsMap(_options.dstType, &mapModType);
				chnSt->insSend.bankPtr = GetExactInstrument(insBank, &chnSt->insSend, mapModType, BNKMSK_ALLBNK);
			}
			else
			{
				chnSt->curIns = 0xFF;
				chnSt->userInsID = newIns & 0x7F;
				chnSt->insOrg.bankPtr = NULL;
				chnSt->insSend.bankPtr = NULL;
			}
			chnSt->pbRange = dataPtr[0x04];
			if (true)
			{
				vis_printf("MT-32 SysEx: Set Ch %u instrument = %u", evtChn, newIns);
			}
			if (_evtCbFunc != NULL)
			{
				MidiEvent insEvt = MidiTrack::CreateEvent_Std(0xC0 | evtChn, chnSt->curIns, 0x00);
				_evtCbFunc(_evtCbData, &insEvt, FULL_CHN_ID(portID, evtChn));
			}
			break;
		}
		break;
	case 0x040000:	// Timbre Temporary Area
		break;
	case 0x050000:	// Patch Memory
		break;
	case 0x080000:	// Timbre Memory
		{
			UINT8 timID = (addr & 0x007E00) >> 9;
			dataPtr = &syxData[0x07];
		}
		break;
	case 0x100000:	// System area
		break;
	case 0x200000:	// Display
		if (addr < 0x200100)
		{
			std::string dispMsg = Vector2String(syxData, 0x07, syxSize - 2);
			
			SanitizeSysExText(dispMsg);
			vis_printf("MT-32 SysEx: Display = \"%s\"", dispMsg.c_str());
		}
		else if (addr == 0x200100)
		{
			vis_addstr("MT-32 SysEx: Display Reset");
		}
		break;
	case 0x7F0000:	// All parameters reset
		InitializeChannels();
		vis_addstr("SysEx: MT-32 Reset\n");
		break;
	}
	
	return false;
}

bool MidiPlayer::HandleSysEx_GS(UINT8 portID, size_t syxSize, const UINT8* syxData)
{
	UINT32 addr;
	UINT8 evtPort;
	UINT8 evtChn;
	UINT16 portChnID;
	ChannelState* chnSt = NULL;
	NoteVisualization::ChnInfo* nvChn = NULL;
	
	if (MMASK_TYPE(_options.srcType) == MODULE_MT32)
	{
		// I've seen MT-32 MIDIs with stray GS messages. Ignore most of them.
		if (! ((syxData[0x04] & 0x3F) == 0x00 && syxData[0x05] == 0x00))
		{
			// fixes Steam-Heart's SH03_MMT.MID
			vis_addstr("Ignoring stray GS SysEx message!");
			return true;
		}
	}
	
	// Data[0x04]	Address High
	// Data[0x05]	Address Mid
	// Data[0x06]	Address Low
	addr =	(syxData[0x04] << 16) |
			(syxData[0x05] <<  8) |
			(syxData[0x06] <<  0);
	switch(addr & 0xFF0000)	// Address High
	{
	case 0x000000:	// System
		switch(addr)
		{
		case 0x00007F:	// SC-88 System Mode Set
			InitializeChannels();	// it completely resets the device
			vis_printf("SysEx: SC-88 System Mode %u\n", 1 + syxData[0x07]);
			if ((_options.flags & PLROPTS_RESET) && MMASK_TYPE(_options.dstType) != MODULE_TYPE_GS)
				return true;	// prevent GS reset on other devices
			if (! (_options.dstType >= MODULE_SC88 && _options.dstType < MODULE_TG300B))
			{
				// for devices that don't understand the message, send GS reset instead
				MidiOutPort_SendLongMsg(_outPorts[portID], sizeof(RESET_GS), RESET_GS);
				return true;
			}
			break;
		}
		break;
	case 0x210000:	// User Drum-Set
		evtChn = (addr & 0x001000) >> 12;
		addr &= ~0x00F0FF;	// remove drum set ID (bits 12-15) and note number (bits 0-7)
		switch(addr)
		{
		case 0x210000:	// Drum Set Name
		{
			std::string drmName = Vector2String(syxData, 0x07, syxSize - 2);
			if (syxData[0x06] == 0x00 && drmName.length() > 1)
				vis_printf("SC-88 SysEx: Set User Drum Set %u Name = \"%s\"\n", evtChn, drmName.c_str());
			else
				vis_printf("SC-88 SysEx: Set User Drum Set %u Name [%X] = \"%s\"\n", evtChn, syxData[0x06], drmName.c_str());
		}
			break;
		}
		break;
	case 0x400000:	// Patch (port A)
	case 0x500000:	// Patch (port B)
		evtPort = portID;
		if (addr & 0x100000)
			evtPort ^= 0x01;
		addr &= ~0x100000;	// remove port bit
		if ((addr & 0x00F000) >= 0x001000)
		{
			addr &= ~0x000F00;	// remove channel ID
			evtChn = PART_ORDER[syxData[0x05] & 0x0F];
			portChnID = FULL_CHN_ID(evtPort, evtChn);
			chnSt = &_chnStates[portChnID];
			nvChn = _noteVis.GetChannel(portChnID);
		}
		switch(addr)
		{
		case 0x400004:	// Master Volume
			break;
		case 0x40007F:	// GS reset
			// F0 41 10 42 12 40 00 7F 00 41 F7
			InitializeChannels();
			vis_addstr("SysEx: GS Reset\n");
			if ((_options.flags & PLROPTS_RESET) && MMASK_TYPE(_options.dstType) != MODULE_TYPE_GS)
				return true;	// prevent GS reset on other devices
			break;
		case 0x400100:	// Patch Name
		{
			std::string dispMsg = Vector2String(syxData, 0x07, syxSize - 2);
			
			SanitizeSysExText(dispMsg);
			vis_printf("SC SysEx: ALL Display = \"%s\"", dispMsg.c_str());
		}
			break;
		case 0x401015:	// use Rhythm Part (-> drum channel)
			// Part Order: 10 1 2 3 4 5 6 7 8 9 11 12 13 14 15 16
			if (syxData[0x07])
				chnSt->flags |= 0x80;	// drum mode on
			else
				chnSt->flags &= ~0x80;	// drum mode off
			nvChn->_chnMode &= ~0x01;
			nvChn->_chnMode |= (chnSt->flags & 0x80) >> 7;
			if (optShowInsChange)
			{
				if (! syxData[0x07])
					vis_printf("SysEx: Chn %c%02u Part Mode: %s", 'A' + evtPort, 1 + evtChn, "Normal");
				else
					vis_printf("SysEx: Chn %c%02u Part Mode: %s %u", 'A' + evtPort, 1 + evtChn, "Drum", syxData[0x07]);
			}
			
			if (chnSt->curIns == 0xFF)
				break;	// skip all the refreshing if the instrument wasn't set by the MIDI yet
			{
				UINT8 flags = 0x10;	// re-evaluate instrument, but don't print anything
				if (true)	// option: emulate HW instrument reset
				{
					// The message always resets Bank MSB and the instrument to 0.
					// Due to the way it works, the current Bank LSB value is also applied, even if it was changed
					// since the last instrument change.
					chnSt->ctrls[0x00] = 0x00;
					chnSt->curIns = 0x00;
					if (! (_options.flags & PLROPTS_STRICT))
						flags |= 0x01;	// we don't need to resend anything either
				}
				
				MidiEvent insEvt = MidiTrack::CreateEvent_Std(0xC0 | evtChn, chnSt->curIns, 0x00);
				HandleInstrumentEvent(chnSt, &insEvt, flags);
				if (_evtCbFunc != NULL)
					_evtCbFunc(_evtCbData, &insEvt, portChnID);
			}
			break;
		case 0x401016:	// Pitch Key Shift
			break;
		case 0x40101F:	// CC1 Controller Number
		case 0x401020:	// CC2 Controller Number
			if (_options.dstType == MODULE_SC8850)
			{
				UINT8 ccNo;
				
				// On the SC-8820, CC1/CC2 number reprogramming is broken.
				// It's best to ignore the message and manually remap the controllers to CC#16/CC#17.
				ccNo = addr - 0x40101F;
				if (syxData[0x07] < 0x0C)
				{
					vis_printf("Warning: Channel %u: CC%u reprogramming to CC#%u might not work!",
								1 + evtChn, 1 + ccNo, syxData[0x07]);
					break;	// ignore stuff like Modulation
				}
				chnSt->idCC[ccNo] = syxData[0x07];
				if (chnSt->idCC[ccNo] == 0x10 + ccNo)
				{
					chnSt->idCC[ccNo] = 0xFF;
					return true;	// for the defaults, silently drop the message
				}
				
				vis_printf("Warning: Channel %u: Ignoring CC%u reprogramming to CC#%u!",
							1 + evtChn, 1 + ccNo, syxData[0x07]);
				return true;
			}
			break;
		}
		break;
	case 0x410000:	// Drum Setup (port A)
	case 0x510000:	// Drum Setup (port B)
		break;
	}
	
	return false;
}

bool MidiPlayer::HandleSysEx_XG(UINT8 portID, size_t syxSize, const UINT8* syxData)
{
	UINT32 addr;
	
	addr =	(syxData[0x03] << 16) |
			(syxData[0x04] <<  8) |
			(syxData[0x05] <<  0);
	switch(addr)
	{
	case 0x00007E:	// XG System On
		// XG Reset: F0 43 10 4C 00 00 7E 00 F7
		InitializeChannels();
		vis_addstr("SysEx: XG Reset\n");
		if ((_options.flags & PLROPTS_RESET) && MMASK_TYPE(_options.dstType) != MODULE_TYPE_XG)
			return true;	// prevent XG reset on other devices
		break;
	}
	
	return false;
}

void MidiPlayer::AllNotesStop(void)
{
	size_t curChn;
	std::list<NoteInfo>::iterator ntIt;
	
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		MIDIOUT_PORT* outPort = _outPorts[chnSt.portID];
		
		for (ntIt = chnSt.notes.begin(); ntIt != chnSt.notes.end(); ++ntIt)
			MidiOutPort_SendShortMsg(outPort, 0x90 | ntIt->chn, ntIt->note, 0x00);
		
		if (chnSt.ctrls[0x40] & 0x40)	// turn Sustain off
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x40, 0x00);
		if (chnSt.ctrls[0x42] & 0x40)	// turn Sostenuto off
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x42, 0x00);
	}
	
	return;
}

void MidiPlayer::AllNotesRestart(void)
{
	size_t curChn;
	std::list<NoteInfo>::iterator ntIt;
	
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		MIDIOUT_PORT* outPort = _outPorts[chnSt.portID];
		
		if (chnSt.ctrls[0x40] & 0x40)	// turn Sustain on
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x40, chnSt.ctrls[0x40]);
		if (chnSt.ctrls[0x42] & 0x40)	// turn Sostenuto on
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x42, chnSt.ctrls[0x42]);
		
		if (chnSt.flags & 0x80)
			continue;	// skip restarting notes on drum channels
		
		for (ntIt = chnSt.notes.begin(); ntIt != chnSt.notes.end(); ++ntIt)
			MidiOutPort_SendShortMsg(outPort, 0x90 | ntIt->chn, ntIt->note, ntIt->vel);
	}
	
	return;
}

void MidiPlayer::AllInsRefresh(void)
{
	size_t curChn;
	std::list<NoteInfo>::iterator ntIt;
	
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		
		if (chnSt.curIns == 0xFF)
			continue;
		MidiEvent insEvt = MidiTrack::CreateEvent_Std(0xC0 | chnSt.midChn, chnSt.curIns, 0x00);
		HandleInstrumentEvent(&chnSt, &insEvt, 0x10);
		if (_evtCbFunc != NULL)
			_evtCbFunc(_evtCbData, &insEvt, curChn);
	}
	
	return;
}

/*static*/ bool MidiPlayer::tempo_compare(const MidiPlayer::TempoChg& first, const MidiPlayer::TempoChg& second)
{
	return (first.tick < second.tick);
}

void MidiPlayer::PrepareMidi(void)
{
	UINT16 curTrk;
	UINT32 tickBase;
	UINT32 maxTicks;
	std::list<TempoChg>::iterator tempoIt;
	std::list<TempoChg>::iterator tPrevIt;
	
	_tempoList.clear();
	tickBase = 0;
	maxTicks = 0;
	for (curTrk = 0; curTrk < _cMidi->GetTrackCount(); curTrk ++)
	{
		MidiTrack* mTrk = _cMidi->GetTrack(curTrk);
		midevt_iterator evtIt;
		
		for (evtIt = mTrk->GetEventBegin(); evtIt != mTrk->GetEventEnd(); ++evtIt)
		{
			evtIt->tick += tickBase;	// for Format 2 files, apply track offset
			
			if (evtIt->evtType == 0xFF && evtIt->evtValA == 0x51)	// FF 51 - tempo change
			{
				TempoChg tc;
				tc.tick = evtIt->tick;
				tc.tempo =	(evtIt->evtData[0x00] << 16) |
							(evtIt->evtData[0x01] <<  8) |
							(evtIt->evtData[0x02] <<  0);
				tc.tmrTick = 0;
				_tempoList.push_back(tc);
			}
		}
		if (_cMidi->GetMidiFormat() == 2)
		{
			maxTicks = mTrk->GetTickCount();
			tickBase = maxTicks;
		}
		else
		{
			if (maxTicks < mTrk->GetTickCount())
				maxTicks = mTrk->GetTickCount();
		}
	}
	_tempoList.sort(tempo_compare);
	if (_tempoList.empty() || _tempoList.front().tick > 0)
	{
		// add initial tempo, if no tempo is set at tick 0
		TempoChg tc;
		tc.tick = 0;
		tc.tempo = 500000;	// 120 BPM
		tc.tmrTick = 0;
		_tempoList.push_front(tc);
	}
	
	// calculate time position of tempo events and song length
	tPrevIt = _tempoList.begin();
	tempoIt = tPrevIt;	++tempoIt;
	for (; tempoIt != _tempoList.end(); ++tempoIt)
	{
		UINT32 tickDiff = tempoIt->tick - tPrevIt->tick;
		_midiTempo = tPrevIt->tempo;
		RefreshTickTime();
		
		tempoIt->tmrTick = tPrevIt->tmrTick + tickDiff * _curTickTime;
		
		tPrevIt = tempoIt;
	}
	
	_midiTempo = tPrevIt->tempo;
	RefreshTickTime();
	_songLength = tPrevIt->tmrTick + (maxTicks - tPrevIt->tick) * _curTickTime;
	
	return;
}

void MidiPlayer::InitializeChannels(void)
{
	size_t curChn;
	MidiEvent chnInitEvt = MidiTrack::CreateEvent_Std(0x01, 0x00, 0x00);
	
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		chnSt.midChn = curChn & 0x0F;
		chnSt.portID = curChn >> 4;
		chnSt.flags = 0x00;
		chnSt.insOrg.bank[0] = chnSt.insOrg.bank[1] = chnSt.insOrg.ins = 0x00;
		chnSt.insOrg.bankPtr = NULL;
		chnSt.insSend.bank[0] = chnSt.insSend.bank[1] = chnSt.insSend.ins = 0xFF;	// initialize to "not set"
		chnSt.insSend.bankPtr = NULL;
		chnSt.insState[0] = chnSt.insState[1] = chnSt.insState[2] = 0xFF;
		chnSt.curIns = 0xFF;
		chnSt.userInsID = 0xFFFF;
		memset(&chnSt.ctrls[0], 0x00, 0x80);
		chnSt.idCC[0] = chnSt.idCC[1] = 0xFF;
		
		chnSt.rpnCtrl[0] = chnSt.rpnCtrl[1] = 0xFF;
		chnSt.pbRange = 2;
		if (_options.srcType == MODULE_MT32)
			chnSt.pbRange = 12;
		chnSt.tuneCoarse = 0;
		chnSt.tuneFine = 0;
		
		chnSt.notes.clear();
		
		if (_evtCbFunc != NULL)
			_evtCbFunc(_evtCbData, &chnInitEvt, curChn);
	}
	for (curChn = 0x00; curChn < _chnStates.size(); curChn += 0x10)
	{
		ChannelState& drumChn = _chnStates[curChn | 0x09];
		drumChn.flags |= 0x80;	// set drum channel mode
		if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_XG)
			drumChn.ctrls[0x00] = 0x7F;
	}
	_noteVis.Reset();
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		NoteVisualization::ChnInfo* nvChn = _noteVis.GetChannel(curChn);
		nvChn->_chnMode |= (chnSt.flags & 0x80) >> 7;
		nvChn->_pbRange = chnSt.pbRange;
	}
	_initChnPost = true;
	
	return;
}

void MidiPlayer::InitializeChannels_Post(void)
{
	size_t curChn;
	
	_initChnPost = false;
	for (curChn = 0x00; curChn < _chnStates.size(); curChn += 0x10)
	{
		ChannelState& drumChn = _chnStates[curChn | 0x09];
		MIDIOUT_PORT* outPort = _outPorts[drumChn.portID];
		
		if (_options.flags & PLROPTS_STRICT)
		{
			if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
			{
				drumChn.insState[0] = 0x00;	// Bank MSB 0
				if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS && MMASK_MOD(_options.srcType) < MT_UNKNOWN)
					drumChn.insState[1] = 0x01 + MMASK_MOD(_options.srcType);
				else if (_options.dstType == MODULE_SC8850)
					drumChn.insState[1] = 0x01 + MTGS_SC88PRO;	// TODO: make configurable
				else
					drumChn.insState[1] = 0x01 + MMASK_MOD(_options.dstType);
				drumChn.insState[2] = 0x00;	// Instrument: Standard Kit 1
				
				if (MMASK_TYPE(_options.srcType) == MODULE_MT32)
				{
					drumChn.insState[1] = 0x01;	// SC-55 map
					drumChn.insState[2] = 0x7F;	// select MT-32 drum kit
				}
				
				MidiOutPort_SendShortMsg(outPort, 0xB0 | drumChn.midChn, 0x00, drumChn.insState[0]);
				MidiOutPort_SendShortMsg(outPort, 0xB0 | drumChn.midChn, 0x20, drumChn.insState[1]);
				MidiOutPort_SendShortMsg(outPort, 0xC0 | drumChn.midChn, drumChn.insState[2], 0x00);
			}
		}
	}
	
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		MIDIOUT_PORT* outPort = _outPorts[chnSt.portID];
		
		if (_options.flags & PLROPTS_STRICT)
		{
			// TODO: store default PB range somewhere.
			if (chnSt.pbRange != 2 && MMASK_TYPE(_options.dstType) < MODULE_TYPE_OT)
			{
				// set initial Pitch Bend Range
				MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x65, 0x00);
				MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x64, 0x00);
				MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x06, chnSt.pbRange);
				// reset RPN selection
				MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x65, 0x7F);
				MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x64, 0x7F);
			}
		}
	}
	
	return;
}

void MidiPlayer::SaveLoopState(LoopPoint& lp, const TrackState* loopMarkTrk)
{
	size_t curTrk;
	
	lp.tick = _nextEvtTick;
	lp.trkEvtPos.resize(_trkStates.size());
	for (curTrk = 0; curTrk < lp.trkEvtPos.size(); curTrk ++)
	{
		lp.trkEvtPos[curTrk] = _trkStates[curTrk].evtPos;
		if (&_trkStates[curTrk] == loopMarkTrk)
			lp.trkEvtPos[curTrk] ++;	// skip loop event
	}
	lp.used = true;
	
	return;
}

void MidiPlayer::RestoreLoopState(const LoopPoint& lp)
{
	if (! lp.used)
		return;
	
	size_t curTrk;
	
	AllNotesStop();	// prevent hanging notes
	
	_nextEvtTick = lp.tick;
	for (curTrk = 0; curTrk < _loopPt.trkEvtPos.size(); curTrk ++)
		_trkStates[curTrk].evtPos = _loopPt.trkEvtPos[curTrk];
	
	return;
}
