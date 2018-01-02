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


#define TICK_FP_SHIFT	8
#define TICK_FP_MUL		(1 << TICK_FP_SHIFT)

static const UINT8 PART_ORDER[0x10] =
{	0x9, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};

static const UINT8 RESET_GM1[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
static const UINT8 RESET_GM2[] = {0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7};
static const UINT8 RESET_GS[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};
static const UINT8 RESET_XG[] = {0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7};

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
	return;
}

void MidiPlayer::SetOutputPorts(const std::vector<MIDIOUT_PORT*>& outPorts)
{
	_outPorts = outPorts;
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
	
	size_t curTrk;
	
	_chnStates.resize(_outPorts.size() * 0x10);
	InitializeChannels();
	_loopPt.used = false;
	
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
	
	if (_options.flags & PLROPTS_RESET)
	{
		size_t curPort;
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
			for (curPort = 0; curPort < _outPorts.size(); curPort ++)
				MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_GS), RESET_GS);
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
	return (double)_songLength / (double)_tmrFreq;
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
	return (double)tmrTick / (double)_tmrFreq;
}

const std::vector<MidiPlayer::ChannelState>& MidiPlayer::GetChannelStates(void) const
{
	return _chnStates;
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
		UINT16 chnID = (trkState->portID << 4) | evtChn;
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
			didEvt = HandleInstrumentEvent(chnSt, trkState, midiEvt);
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
	case 0xF0:
	case 0xF7:
		{
			if (HandleSysExMessage(trkState, midiEvt))
				break;
			
			std::vector<UINT8> msgData(0x01 + midiEvt->evtData.size());
			msgData[0x00] = midiEvt->evtType;
			memcpy(&msgData[0x01], &midiEvt->evtData[0x00], midiEvt->evtData.size());
			MidiOutPort_SendLongMsg(outPort, msgData.size(), &msgData[0x00]);
		}
		if (_evtCbFunc != NULL)
			_evtCbFunc(_evtCbData, midiEvt, (trkState->portID << 4) | 0x00);
		break;
	case 0xFF:
		switch(midiEvt->evtValA)
		{
		case 0x03:	// Text/Sequence Name
			if (trkState->trkID == 0 || _cMidi->GetMidiFormat() == 2)
			{
				std::string text(midiEvt->evtData.begin(), midiEvt->evtData.end());
				//printf("Text: %s\n", text.c_str());
			}
			break;
		case 0x06:	// Marker
			{
				std::string text(midiEvt->evtData.begin(), midiEvt->evtData.end());
				//printf("Marker: %s\n", text.c_str());
				if (text == "loopStart")
				{
					vis_addstr("loopStart found.");
					SaveLoopState(_loopPt, trkState);
				}
				else if (text == "loopEnd")
				{
					_breakMidiProc = true;
					RestoreLoopState(_loopPt);
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
				RestoreLoopState(_loopPt);
				continue;
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
	}
	
	return false;
}

bool MidiPlayer::HandleControlEvent(ChannelState* chnSt, const TrackState* trkSt, const MidiEvent* midiEvt)
{
	chnSt->ctrls[midiEvt->evtValA] = midiEvt->evtValB;
	switch(midiEvt->evtValA)
	{
	case 0x00:	// Bank MSB
		chnSt->insBank[0] = chnSt->ctrls[0x00];
		break;
	case 0x20:	// Bank LSB
		chnSt->insBank[1] = chnSt->ctrls[0x20];
		if ((_options.flags & PLROPTS_STRICT) && (_options.dstType == MODULE_SC55 || _options.dstType == MODULE_TG300B))
		{
			// enforce Bank LSB == 0 for GS/SC-55
			chnSt->insBank[1] = 0x00;
			MidiOutPort_SendShortMsg(chnSt->outPort, midiEvt->evtType, midiEvt->evtValA, chnSt->insBank[1]);
			return true;
		}
		break;
	case 0x07:	// Main Volume
		// if (Fading) then calculate new volume + send event + return true
		break;
	case 0x0A:	// Pan
		break;
	case 0x0B:	// Expression
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
				break;
			case 0x01:	// Fine Tuning
				chnSt->tuneFine &= ~0xFF00;
				chnSt->tuneFine |= ((INT16)midiEvt->evtValB - 0x40) << 8;
				break;
			case 0x02:	// Coarse Tuning
				chnSt->tuneCoarse = (INT8)midiEvt->evtValB - 0x40;
				if (chnSt->tuneCoarse < -24)
					chnSt->tuneCoarse = -24;
				else if (chnSt->tuneCoarse > +24)
					chnSt->tuneCoarse = +24;
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
		break;
	case 0x64:	// RPN LSB
		chnSt->rpnCtrl[1] = 0x00 | midiEvt->evtValB;
		break;
	case 0x65:	// RPN MSB
		chnSt->rpnCtrl[0] = 0x00 | midiEvt->evtValB;
		break;
	case 0x6F:	// RPG Maker loop controller
		if (midiEvt->evtValB == 0 || midiEvt->evtValB == 111)
		{
			vis_addstr("Loop Point found.");
			SaveLoopState(_loopPt, trkSt);
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
		break;
	case 0x7B:	// All Notes Off
		chnSt->notes.clear();
		if (_evtCbFunc != NULL)
		{
			MidiEvent noteEvt;
			noteEvt.evtType = 0x01;
			noteEvt.evtValA = 0x7B;
			noteEvt.evtValB = 0x00;
			_evtCbFunc(_evtCbData, &noteEvt, chnSt - &_chnStates[0]);
		}
		break;
	}
	
	return false;
}

static const INS_DATA* GetInsMapData(const INS_PRG_LST* insPrg, UINT8 ins, UINT8 msb, UINT8 lsb, UINT8 maxModuleID)
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

static const INS_DATA* GetExactInstrument(const INS_BANK* insBank, const MidiPlayer::ChannelState* chnSt, UINT8 maxModuleID, UINT8 bankIgnore)
{
	const INS_PRG_LST* insPrg;
	const INS_DATA* insData;
	UINT8 msb;
	UINT8 lsb;
	
	if (insBank == NULL)
		return NULL;
	insPrg = &insBank->prg[chnSt->curIns];
	
	// try exact match first
	if (bankIgnore)
	{
		insData = GetInsMapData(insPrg, chnSt->curIns, chnSt->insBank[0], chnSt->insBank[1], maxModuleID);
		if (insData != NULL)
			return insData;
	}
	msb = (bankIgnore & 0x01) ? 0xFF : chnSt->insBank[0];
	lsb = (bankIgnore & 0x02) ? 0xFF : chnSt->insBank[1];
	insData = GetInsMapData(insPrg, chnSt->curIns, msb, lsb, maxModuleID);
	return insData;
}

static const INS_DATA* GetClosestInstrument(const INS_BANK* insBank, const MidiPlayer::ChannelState* chnSt, UINT8 maxModuleID, UINT8 bankIgnore)
{
	const INS_PRG_LST* insPrg;
	const INS_DATA* insData;
	UINT8 msb;
	UINT8 lsb;
	
	if (insBank == NULL)
		return NULL;
	insPrg = &insBank->prg[chnSt->curIns];
	
	// try exact match first
	if (bankIgnore)
	{
		insData = GetInsMapData(insPrg, chnSt->curIns, chnSt->insBank[0], chnSt->insBank[1], maxModuleID);
		if (insData != NULL)
			return insData;
	}
	msb = (bankIgnore & 0x01) ? 0xFF : chnSt->insBank[0];
	lsb = (bankIgnore & 0x02) ? 0xFF : chnSt->insBank[1];
	insData = GetInsMapData(insPrg, chnSt->curIns, msb, lsb, maxModuleID);
	if (insData != NULL)
		return insData;
	insData = GetInsMapData(insPrg, chnSt->curIns, msb, 0xFF, maxModuleID);
	if (insData != NULL)
		return insData;
	insData = GetInsMapData(insPrg, chnSt->curIns, 0xFF, 0xFF, maxModuleID);
	return insData;
}

bool MidiPlayer::HandleInstrumentEvent(ChannelState* chnSt, const TrackState* trkSt, const MidiEvent* midiEvt)
{
	UINT8 oldMSB = chnSt->insBank[0];
	UINT8 oldLSB = chnSt->insBank[1];
	UINT8 oldIns = chnSt->curIns;
	bool isUserIns;
	bool didPatch;
	const INS_BANK* insBank;
	UINT8 mapModType;
	UINT8 bankIgnore;
	
	// start with the "actual" definition, then add patches
	chnSt->insBank[0] = chnSt->ctrls[0x00];
	chnSt->insBank[1] = chnSt->ctrls[0x20];
	chnSt->curIns = midiEvt->evtValA;
	chnSt->curIns = (chnSt->curIns & 0x7F) | (chnSt->flags & 0x80);
	chnSt->userInsID = 0xFFFF;
	chnSt->insMapOPtr = chnSt->insMapPPtr = NULL;
	isUserIns = false;
	didPatch = false;
	bankIgnore = 0x00;
	
	if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
	{
		if (MMASK_MOD(_options.srcType) >= MTGS_SC88 && MMASK_MOD(_options.srcType) != MTGS_TG300B)
		{
			if (chnSt->curIns == (0x80|0x40) || chnSt->curIns == (0x80|0x41))
				chnSt->userInsID = 0x8000 | (chnSt->curIns & 0x01);
			else if (chnSt->insBank[0] == 0x40 || chnSt->insBank[0] == 0x41)
				chnSt->userInsID = ((chnSt->insBank[0] & 0x01) << 7) | (chnSt->curIns & 0x7F);
		}
	}
	else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_XG)
	{
		if (chnSt->insBank[0] == 0x3F)
			chnSt->userInsID = (chnSt->curIns & 0x7F);
		if (chnSt->insBank[0] >= 0x7E)	// MSB 7E/7F = drum kits
			chnSt->flags |= 0x80;
		else
			chnSt->flags &= ~0x80;
		chnSt->curIns = (chnSt->curIns & 0x7F) | (chnSt->flags & 0x80);
	}
	else if (_options.srcType == MODULE_GM_1)
	{
		bankIgnore = 0x03;	// GM Level 1 doesn't support Bank MSB/LSB
	}
	else if (_options.srcType == MODULE_MT32)
	{
		isUserIns = true;	// disable CTF patch
		bankIgnore = 0x03;	// MT-32 doesn't support Bank MSB/LSB
	}
	if (chnSt->userInsID != 0xFFFF)
		isUserIns = true;
	
	// ignore Bank MSB/LSB on drum channels, depending on device type
	if (chnSt->flags & 0x80)
	{
		if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
			bankIgnore |= 0x01;	// ignore MSB
		else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_XG)
			bankIgnore |= 0x02;	// ignore LSB
		else
			bankIgnore |= 0x03;
	}
	
	
	// apply PLROPTS_STRICT filter now (regardless of the option),
	// so that instrument lookup works
	if (_options.srcType == MODULE_GM_1 || _options.srcType == MODULE_MT32)
	{
		chnSt->insBank[0] = 0x00;
		chnSt->insBank[1] = 0x00;
	}
	else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
	{
		if (_options.srcType == MODULE_SC55)
		{
			// SC-55 ignores Bank LSB, so we enforce the SC-55 instrument map here
			chnSt->insBank[1] = 0x01;
		}
		// SC-88+: force GS instrument bank LSB to 01+
		else if (chnSt->insBank[1] == 0x00)	// Bank LSB = default bank
		{
			// GS song: use bank that is optimal for the song
			// GM song: use "native" bank for device
			if (MMASK_MOD(_options.srcType) >= MT_UNKNOWN)
				bankIgnore |= 0x02;
			else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
				chnSt->insBank[1] = 0x01 + MMASK_MOD(_options.srcType);
			else
				chnSt->insBank[1] = 0x01 + MMASK_MOD(_options.dstType);
		}
	}
	insBank = SelectInsMap(_options.srcType, &mapModType);
	chnSt->insMapOPtr = GetExactInstrument(insBank, chnSt, mapModType, bankIgnore);
	
	if (_options.flags & PLROPTS_STRICT)
	{
		if (MMASK_TYPE(_options.srcType) != MODULE_TYPE_XG &&
			MMASK_TYPE(_options.dstType) == MODULE_TYPE_XG)
		{
			chnSt->insBank[0] = (chnSt->flags & 0x80) ? 0x7F : 0x00;
		}
	}
	if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
	{
		if (MMASK_TYPE(_options.srcType) != MODULE_TYPE_GS || chnSt->insBank[1] == 0x00)
		{
			// GS song: use bank that is optimal for the song
			// GM song: use "native" bank for device
			if (MMASK_MOD(_options.srcType) >= MT_UNKNOWN)
				bankIgnore |= 0x02;
			else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
				chnSt->insBank[1] = 0x01 + MMASK_MOD(_options.srcType);
			else
				chnSt->insBank[1] = 0x01 + MMASK_MOD(_options.dstType);
		}
	}
	
	// try to choose a fitting instrument for the destination device
	// (the XG patch above had to be done first)
	insBank = SelectInsMap(_options.dstType, &mapModType);
	chnSt->insMapPPtr = GetExactInstrument(insBank, chnSt, mapModType, bankIgnore);
	
	if (! (_options.flags & PLROPTS_STRICT))
	{
		// restore actual Bank MSB/LSB settings, for non-strict mode
		chnSt->insBank[0] = chnSt->ctrls[0x00];
		chnSt->insBank[1] = chnSt->ctrls[0x20];
		if (chnSt->insMapPPtr != NULL && chnSt->insMapOPtr != NULL)
			chnSt->insMapPPtr = chnSt->insMapOPtr;
	}
	
	if (chnSt->insMapPPtr == NULL && ! isUserIns && (~bankIgnore & 0x03) && (_options.flags & PLROPTS_ENABLE_CTF))
	{
		const INS_DATA* insData;
		
		insData = GetClosestInstrument(insBank, chnSt, mapModType, bankIgnore);
		if (insData == NULL)
		{
			// try General MIDI fallback
			chnSt->insBank[0] = 0x00;
			chnSt->insBank[1] = 0x00;
			// on GS devices, keep a fitting Bank LSB type
			if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
				chnSt->insBank[1] = 0x01 + MMASK_MOD(_options.srcType);
			
			if (chnSt->flags & 0x80)
			{
				// handle drum mode fallback
				chnSt->curIns &= 0x7F;
				if (_options.srcType == MODULE_SC55)
				{
					// SC-55 v1 style drum CTF
					// see https://www.vogons.org/viewtopic.php?p=501038#p501038
					if ((chnSt->curIns & 0x78) == 0x18)
						chnSt->curIns = 0x19;	// fall back to TR-808 kit
					else if (chnSt->curIns < 0x40)
						chnSt->curIns &= ~0x07;
				}
				else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
				{
					// SC-88+ simply ignores the instrument change
					chnSt->curIns = oldIns;
				}
				else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_XG)
				{
					// XG devices ignore the instrument change, but we should keep Bank MSB
					chnSt->insBank[0] = chnSt->ctrls[0x00];
					chnSt->curIns = oldIns;
				}
				else
				{
					// fall back to Standard Drum Kit
					chnSt->curIns = 0x00;
				}
				chnSt->curIns |= 0x80;
			}
			didPatch = true;
		}
		else
		{
			chnSt->insBank[0] = insData->bankMSB;
			chnSt->insBank[1] = insData->bankLSB;
		}
		chnSt->insMapPPtr = insData;
	}
	if (chnSt->insMapPPtr == NULL && MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
	{
		if (! chnSt->insBank[1] || _options.dstType == MODULE_SC55 || _options.dstType == MODULE_TG300B)
			chnSt->insMapPPtr = GetExactInstrument(insBank, chnSt, mapModType, bankIgnore | 0x02);
	}
	
	if (_options.flags & PLROPTS_STRICT)
	{
		if (_options.dstType == MODULE_SC55 || _options.dstType == MODULE_TG300B)
		{
			// We had to use LSB 01 for instrument lookup, but we actually send LSB 00.
			// (LSB is ignored on the SC-55 itself, but LSB 00 is required on the Yamaha MU80.)
			chnSt->insBank[1] = 0x00;
		}
	}
	
	// resend Bank MSB/LSB
	if (oldMSB != chnSt->insBank[0] || oldLSB != chnSt->insBank[1] && (~bankIgnore & 0x03))
	{
		UINT8 evtType = 0xB0 | (midiEvt->evtType & 0x0F);
		if (oldMSB != chnSt->insBank[0])
			MidiOutPort_SendShortMsg(chnSt->outPort, evtType, 0x00, chnSt->insBank[0]);
		if (oldLSB != chnSt->insBank[1])
			MidiOutPort_SendShortMsg(chnSt->outPort, evtType, 0x20, chnSt->insBank[1]);
	}
	if (chnSt->insBank[0] != chnSt->ctrls[0x00] || chnSt->insBank[1] != chnSt->ctrls[0x20])
		didPatch = true;
	
	if (true)
	{
		const char* oldName;
		const char* newName;
		UINT8 ctrlEvt = 0xB0 | (midiEvt->evtType & 0x0F);
		UINT8 insEvt = midiEvt->evtType;
		char msgStr[0x80];
		
		oldName = (chnSt->insMapOPtr == NULL) ? "" : chnSt->insMapOPtr->insName;
		newName = (chnSt->insMapPPtr == NULL) ? "" : chnSt->insMapPPtr->insName;
		if (didPatch)
		{
			sprintf(msgStr, "%s Patch: %02X 00 %02X  %02X 20 %02X  %02X %02X %s\n",
				(chnSt->flags & 0x80) ? "Drm" : "Ins",
				ctrlEvt, chnSt->ctrls[0x00], ctrlEvt, chnSt->ctrls[0x20], insEvt, midiEvt->evtValA, oldName);
			vis_addstr(msgStr);
			sprintf(msgStr, "       ->  %02X 00 %02X  %02X 20 %02X  %02X %02X %s\n",
				ctrlEvt, chnSt->insBank[0], ctrlEvt, chnSt->insBank[1], insEvt, chnSt->curIns & 0x7F, newName);
			vis_addstr(msgStr);
		}
		else
		{
			sprintf(msgStr, "%s Set:   %02X 00 %02X  %02X 20 %02X  %02X %02X %s\n",
				(chnSt->flags & 0x80) ? "Drm" : "Ins",
				ctrlEvt, chnSt->insBank[0], ctrlEvt, chnSt->insBank[1], insEvt, chnSt->curIns & 0x7F, newName);
			vis_addstr(msgStr);
		}
	}
	
	MidiOutPort_SendShortMsg(chnSt->outPort, midiEvt->evtType, chnSt->curIns & 0x7F, 0x00);
	return true;
}

bool MidiPlayer::HandleSysExMessage(const TrackState* trkSt, const MidiEvent* midiEvt)
{
	switch(midiEvt->evtData[0x00])
	{
	case 0x41:	// Roland ID
		// Data[0x01] == 0x1n - Device Number n
		// Data[0x02] == Model ID (MT-32 = 0x16, GS = 0x42)
		// Data[0x03] == Command ID (reqest data RQ1 = 0x11, data set DT1 = 0x12)
		if (midiEvt->evtData[0x03] == 0x12)
		{
			if (midiEvt->evtData.size() < 0x08)
				break;	// We need enough bytes for a full address.
			if (midiEvt->evtData[0x02] == 0x16)
				return HandleSysEx_MT32(trkSt, midiEvt);
			else if (midiEvt->evtData[0x02] == 0x42)
				return HandleSysEx_GS(trkSt, midiEvt);
		}
		break;
	case 0x43:	// YAMAHA ID
		// Data[0x01] == 0x1n - Device Number n
		if (midiEvt->evtData.size() < 0x07)
			break;	// We need enough bytes for a full address.
		if (midiEvt->evtData[0x02] == 0x4C)
			return HandleSysEx_XG(trkSt, midiEvt);
		break;
	case 0x7E:
		// GM Lvl 1 On:  F0 7E 7F 09 01 F7
		// GM Lvl 1 Off: F0 7E 7F 09 00 F7
		// GM Lvl 2 On:  F0 7E 7F 09 03 F7
		if (midiEvt->evtData[0x01] == 0x7F && midiEvt->evtData[0x02] == 0x09)
		{
			UINT8 gmMode = midiEvt->evtData[0x03];
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
	}
	
	return false;
}

bool MidiPlayer::HandleSysEx_MT32(const TrackState* trkSt, const MidiEvent* midiEvt)
{
	UINT32 addr;
	const UINT8* dataPtr;
	
	// Data[0x04]	Address High
	// Data[0x05]	Address Mid
	// Data[0x06]	Address Low
	addr =	(midiEvt->evtData[0x04] << 16) |
			(midiEvt->evtData[0x05] <<  8) |
			(midiEvt->evtData[0x06] <<  0);
	switch(addr & 0xFF0000)	// Address High
	{
	case 0x030000:	// Patch Temporary Area
		if ((addr & 0x0F) > 0x00)
			break;	// Right now we can only handle bulk writes.
		dataPtr = &midiEvt->evtData[0x07];
		if (addr < 0x030110)
		{
			UINT8 evtChn;
			ChannelState* chnSt = NULL;
			UINT8 newIns;
			
			evtChn = 1 + ((addr & 0x0000F0) >> 4);
			chnSt = &_chnStates[(trkSt->portID << 4) | evtChn];
			newIns = ((dataPtr[0x00] & 0x03) << 6) | ((dataPtr[0x01] & 0x3F) << 0);
			if (newIns < 0x80)
			{
				const INS_BANK* insBank;
				UINT8 mapModType;
				
				chnSt->curIns = newIns;
				chnSt->userInsID = 0xFFFF;
				insBank = SelectInsMap(_options.dstType, &mapModType);
				chnSt->insMapPPtr = GetExactInstrument(insBank, chnSt, mapModType, 0x03);
			}
			else
			{
				chnSt->curIns = 0xFF;
				chnSt->userInsID = newIns & 0x7F;
				chnSt->insMapPPtr = NULL;
			}
			chnSt->pbRange = dataPtr[0x04];
			if (true)
			{
				char msgStr[0x80];
				
				sprintf(msgStr, "MT-32 SysEx: Set Ch %u instrument = %u", evtChn, newIns);
				vis_addstr(msgStr);
			}
			if (_evtCbFunc != NULL)
			{
				MidiEvent insEvt;
				
				insEvt.evtType = 0xC0 | evtChn;
				insEvt.evtValA = chnSt->curIns;
				_evtCbFunc(_evtCbData, &insEvt, (trkSt->portID << 4) | evtChn);
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
			dataPtr = &midiEvt->evtData[0x07];
		}
		break;
	case 0x100000:	// System area
		break;
	case 0x200000:	// Display
		if (addr < 0x200100)
		{
			std::string dispMsg(&midiEvt->evtData[0x07], &midiEvt->evtData[midiEvt->evtData.size() - 2]);
			char msgStr[0x80];
			
			sprintf(msgStr, "MT-32 SysEx: Display = \"%s\"", dispMsg.c_str());
			vis_addstr(msgStr);
		}
		else if (addr == 0x200100)
		{
			vis_addstr("MT-32 SysEx: Display Reset");
		}
		break;
	case 0x7F0000:	//All parameters reset
		InitializeChannels();
		vis_addstr("SysEx: MT-32 Reset\n");
		break;
	}
	
	return false;
}

bool MidiPlayer::HandleSysEx_GS(const TrackState* trkSt, const MidiEvent* midiEvt)
{
	UINT32 addr;
	UINT8 evtChn;
	ChannelState* chnSt = NULL;
	
	// Data[0x04]	Address High
	// Data[0x05]	Address Mid
	// Data[0x06]	Address Low
	addr =	(midiEvt->evtData[0x04] << 16) |
			(midiEvt->evtData[0x05] <<  8) |
			(midiEvt->evtData[0x06] <<  0);
	switch(addr & 0xFF0000)	// Address High
	{
	case 0x000000:	// System
		switch(addr)
		{
		case 0x00007F:	// SC-88 System Mode Set
			InitializeChannels();	// it completely resets the device
			vis_addstr("SysEx: SC-88 System Mode Set\n");
			if (! (_options.dstType >= MODULE_SC88 && _options.dstType < MODULE_TG300B))
			{
				// for devices that don't understand the message, send GS reset instead
				MidiOutPort_SendLongMsg(_outPorts[trkSt->portID], sizeof(RESET_GS), RESET_GS);
				return true;
			}
			break;
		}
		break;
	case 0x400000:	// Patch (port A)
	case 0x500000:	// Patch (port B)
		if ((addr & 0x00F000) >= 0x001000)
		{
			addr &= ~0x000F00;	// remove channel ID
			evtChn = PART_ORDER[midiEvt->evtData[0x05] & 0x0F];
			chnSt = &_chnStates[(trkSt->portID << 4) | evtChn];
		}
		switch(addr)
		{
		case 0x40007F:	// GS reset
			// F0 41 10 42 12 40 00 7F 00 41 F7
			InitializeChannels();
			vis_addstr("SysEx: GS Reset\n");
			break;
		case 0x401015:	// use Rhythm Part (-> drum channel)
			// Part Order: 10 1 2 3 4 5 6 7 8 9 11 12 13 14 15 16
			if (midiEvt->evtData[0x07])
				chnSt->flags |= 0x80;	// drum mode on
			else
				chnSt->flags &= ~0x80;	// drum mode off
			break;
		case 0x401016:	// Pitch Key Shift
			break;
		}
		break;
	case 0x410000:	// Drum Setup (port A)
	case 0x510000:	// Drum Setup (port B)
		break;
	}
	
	return false;
}

bool MidiPlayer::HandleSysEx_XG(const TrackState* trkSt, const MidiEvent* midiEvt)
{
	UINT32 addr;
	
	addr =	(midiEvt->evtData[0x03] << 16) |
			(midiEvt->evtData[0x04] <<  8) |
			(midiEvt->evtData[0x05] <<  0);
	switch(addr)
	{
	case 0x00007E:	// XG System On
		// XG Reset: F0 43 10 4C 00 00 7E 00 F7
		InitializeChannels();
		vis_addstr("SysEx: XG Reset\n");
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
		ChannelState* chnSt = &_chnStates[curChn];
		UINT8 evtChn = curChn & 0x0F;
		
		for (ntIt = chnSt->notes.begin(); ntIt != chnSt->notes.end(); ++ntIt)
			MidiOutPort_SendShortMsg(chnSt->outPort, 0x90 | ntIt->chn, ntIt->note, 0x00);
		
		if (chnSt->ctrls[0x40] & 0x40)	// turn Sustain off
			MidiOutPort_SendShortMsg(chnSt->outPort, 0xB0 | evtChn, 0x40, 0x00);
		if (chnSt->ctrls[0x42] & 0x40)	// turn Sostenuto off
			MidiOutPort_SendShortMsg(chnSt->outPort, 0xB0 | evtChn, 0x42, 0x00);
	}
	
	return;
}

void MidiPlayer::AllNotesRestart(void)
{
	size_t curChn;
	std::list<NoteInfo>::iterator ntIt;
	
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState* chnSt = &_chnStates[curChn];
		UINT8 evtChn = curChn & 0x0F;
		
		if (chnSt->ctrls[0x40] & 0x40)	// turn Sustain on
			MidiOutPort_SendShortMsg(chnSt->outPort, 0xB0 | evtChn, 0x40, chnSt->ctrls[0x40]);
		if (chnSt->ctrls[0x42] & 0x40)	// turn Sostenuto on
			MidiOutPort_SendShortMsg(chnSt->outPort, 0xB0 | evtChn, 0x42, chnSt->ctrls[0x42]);
		
		if (chnSt->flags & 0x80)
			continue;	// skip restarting notes on drum channels
		
		for (ntIt = chnSt->notes.begin(); ntIt != chnSt->notes.end(); ++ntIt)
			MidiOutPort_SendShortMsg(chnSt->outPort, 0x90 | ntIt->chn, ntIt->note, ntIt->vel);
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
	
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		chnSt.flags = 0x00;
		chnSt.curIns = 0x00;
		chnSt.insBank[0] = chnSt.insBank[1] = 0x00;
		chnSt.userInsID = 0xFFFF;
		chnSt.insMapOPtr = chnSt.insMapPPtr = NULL;
		memset(&chnSt.ctrls[0], 0x00, 0x80);
		
		chnSt.rpnCtrl[0] = chnSt.rpnCtrl[1] = 0xFF;
		chnSt.pbRange = 2;
		chnSt.tuneCoarse = 0;
		chnSt.tuneFine = 0;
		
		chnSt.notes.clear();
		chnSt.outPort = _outPorts[curChn / 0x10];
	}
	for (curChn = 0x00; curChn < _chnStates.size(); curChn += 0x10)
	{
		ChannelState& drumChn = _chnStates[curChn | 0x09];
		drumChn.flags |= 0x80;	// set drum channel mode
		drumChn.curIns |= 0x80;
		if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_XG)
		{
			drumChn.ctrls[0x00] = 0x7F;
			drumChn.insBank[0] = 0x7F;
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
	
	_nextEvtTick = lp.tick;
	for (curTrk = 0; curTrk < _loopPt.trkEvtPos.size(); curTrk ++)
		_trkStates[curTrk].evtPos = _loopPt.trkEvtPos[curTrk];
	
	return;
}
