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
#define DBL_TO_U64(x)	(UINT64)(INT64)(x)
#else
#define U64_TO_DBL(x)	(double)(x)
#define DBL_TO_U64(x)	(UINT64)(x)
#endif
#ifdef _MSC_VER
#define snprintf	_snprintf
#endif

#define TICK_FP_SHIFT	8
#define TICK_FP_MUL		(1 << TICK_FP_SHIFT)

#define FULL_CHN_ID(portID, midChn)	(((portID) << 4) | ((midChn) << 0))

// filtered volume bits
#define FILTVOL_CCVOL	0	// Main Volume controller
#define FILTVOL_CCEXPR	1	// Expression controller
#define FILTVOL_GMSYX	2	// GM Master Volume SysEx

// fade volume mode constants
#define FDVMODE_CCVOL	FILTVOL_CCVOL
#define FDVMODE_CCEXPR	FILTVOL_CCEXPR
#define FDVMODE_GMSYX	FILTVOL_GMSYX

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
static const UINT8 RESET_XG_PARAM[] = {0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7F, 0x00, 0xF7};
static const UINT8 XG_VOICE_MAP[] = {0xF0, 0x43, 0x10, 0x49, 0x00, 0x00, 0x12, 0xFF, 0xF7};
static const UINT8 GM_MST_VOL[] = {0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x00, 0xF7};

extern UINT8 optShowInsChange;

static inline UINT32 ReadBE24(const UINT8* data)
{
	return (data[0x00] << 16) | (data[0x01] << 8) | (data[0x02] << 0);
}

static inline UINT32 ReadBE14(const UINT8* data)	// 2 bytes, 7 bits per byte
{
	return ((data[0x00] & 0x7F) << 7) | ((data[0x01] & 0x7F) << 0);
}

static inline UINT32 ReadBE21(const UINT8* data)	// 3 bytes, 7 bits per byte
{
	return ((data[0x00] & 0x7F) << 14) | ((data[0x01] & 0x7F) << 7) | ((data[0x02] & 0x7F) << 0);
}

MidiPlayer::MidiPlayer() :
	_cMidi(NULL), _songLength(0),
	_insBankGM1(NULL), _insBankGM2(NULL), _insBankGS(NULL), _insBankXG(NULL), _insBankYGS(NULL), _insBankMT32(NULL)
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
	if (_noteVis.GetChnGroupCount() != _outPorts.size())
		_noteVis.Initialize(_outPorts.size());
	return;
}

void MidiPlayer::SetOutputPorts(const std::vector<MIDIOUT_PORT*>& outPorts)
{
	_outPorts = outPorts;
	_chnStates.resize(_outPorts.size() * 0x10);
	if (_noteVis.GetChnGroupCount() != _outPorts.size())
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
	if (_playing)
		RefreshSrcDevSettings();
	
	return;
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
	if (_playing)
	{
		RefreshSrcDevSettings();
		if (insRefresh)
			AllInsRefresh();
	}
	
	return;
}

void MidiPlayer::SetDstModuleType(UINT8 modType, bool chnRefresh)
{
	_options.dstType = modType;
	if (_playing && chnRefresh)
		AllChannelRefresh();
	
	return;
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
		// TG300B instrument map is very similar to the SC-88 one
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
	UINT64 initDelay;
	
	_loopPt.used = false;
	_curLoop = 0;
	_rcpMidTextMode = 0;
	_karaokeMode = 0;
	
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
	_tmrMinStart = OSTimer_GetTime(_osTimer) << TICK_FP_SHIFT;
	initDelay = 0;	// additional time (in ms) to wait due to device reset commands
	_tmrFadeStart = 0;
	_tmrFadeLen = 0;
	_fadeVol = 0x100;
	_fadeVolMode = 0xFF;
	_filteredVol = 0x00;
	
	_defSrcInsMap = 0xFF;
	_defDstInsMap = 0xFF;
	RefreshSrcDevSettings();
	InitializeChannels();
	if (_options.flags & PLROPTS_RESET)
	{
		size_t curPort;
		if (_options.dstType == MODULE_MT32)
		{
			// MT-32 mode - nothing to do right now
			//vis_printf("Sending Device Reset (%s) ...", "MT-32");
		}
		else if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_GM)
		{
			// send GM reset
			if (MMASK_MOD(_options.dstType) == MTGM_LVL2)
			{
				vis_printf("Sending Device Reset (%s) ...", "GM Level 2");
				for (curPort = 0; curPort < _outPorts.size(); curPort ++)
					MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_GM2), RESET_GM2);
			}
			else //if (MMASK_MOD(_options.dstType) == MTGM_LVL1)
			{
				vis_printf("Sending Device Reset (%s) ...", "GM");
				for (curPort = 0; curPort < _outPorts.size(); curPort ++)
					MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_GM1), RESET_GM1);
			}
			initDelay += 200;
		}
		else if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
		{
			// send GS reset
			if (MMASK_MOD(_options.dstType) >= MTGS_SC88 && MMASK_MOD(_options.dstType) != MTGS_TG300B)
			{
				vis_printf("Sending Device Reset (%s) ...", "SC");
				for (curPort = 0; curPort < _outPorts.size(); curPort ++)
					MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_SC), RESET_SC);
			}
			else
			{
				vis_printf("Sending Device Reset (%s) ...", "GS");
				for (curPort = 0; curPort < _outPorts.size(); curPort ++)
					MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_GS), RESET_GS);
			}
			initDelay += 200;
		}
		else if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_XG)
		{
			// send XG reset
			vis_printf("Sending Device Reset (%s) ...", "XG");
			for (curPort = 0; curPort < _outPorts.size(); curPort ++)
			{
				MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_GM1), RESET_GM1);
				MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_XG), RESET_XG);
				MidiOutPort_SendLongMsg(_outPorts[curPort], sizeof(RESET_XG_PARAM), RESET_XG_PARAM);
			}
			initDelay += 400;	// XG modules take a bit to fully reset
		}
	}
	InitializeChannels_Post();
	
	_tmrMinStart += initDelay * _tmrFreq / 1000;
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

UINT8 MidiPlayer::FadeOutT(double fadeTime)
{
	if (! _playing)
		return 0xFF;
	
	_tmrFadeStart = (UINT64)-1;
	_tmrFadeLen = DBL_TO_U64(fadeTime *_tmrFreq);
	vis_printf("Fading Out ... (%.2f s)", fadeTime);
	
	UINT8 devType = MMASK_TYPE(_options.dstType);
	UINT8 devMod = MMASK_MOD(_options.dstType);
	
	if ((devType == MODULE_TYPE_GM && devMod >= MTGM_LVL2) ||
		devType == MODULE_TYPE_GS || devType == MODULE_TYPE_XG)
	{
		// prefer using GM Master Volume SysEx for GM2, GS and XG
		// ("GM1" might be used for all sorts of generic stuff that doesn't know SysEx.)
		_fadeVolMode = FDVMODE_GMSYX;
		_filteredVol |= (1 << FILTVOL_GMSYX);
	}
	else
	{
		// for all others, send Main Volume controller
		_fadeVolMode = FDVMODE_CCVOL;
		_filteredVol |= (1 << FILTVOL_CCVOL);
	}
	
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
	{
		if (tmrTick <= _tmrStep - curTime)
			return 0;	// prevent underflow
		tmrTick -= (_tmrStep - curTime);
	}
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
	if (tmrDiv == 0)
		tmrDiv = 1000000;
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
		UINT16 portChnID = FULL_CHN_ID(trkState->portID, evtChn);
		ChannelState* chnSt = &_chnStates[portChnID];
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
				NoteVisualization::ChnInfo* nvChn = _noteVis.GetChannel(portChnID);
				INT32 pbVal = (midiEvt->evtValB << 7) | (midiEvt->evtValA << 0);
				pbVal = (pbVal - 0x2000) * nvChn->_pbRange;	// 0x2000 per semitone
				nvChn->_attr.detune[0] = (INT16)(pbVal / 0x20);	// make 8.8 fixed point
			}
			break;
		}
		if (! didEvt)
			MidiOutPort_SendShortMsg(outPort, midiEvt->evtType, midiEvt->evtValA, midiEvt->evtValB);
		
		if (evtType == 0xB0)
			vis_do_ctrl_change(portChnID, midiEvt->evtValA);
		else if (evtType == 0xC0)
			vis_do_ins_change(portChnID);
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
		//case 0x00:	// Sequence Number
		case 0x01:	// Text
			{
				std::string text = Vector2String(midiEvt->evtData);
				if (text.empty())
					break;
				if (text == "@KMIDI KARAOKE FILE")
				{
					_karaokeMode = 1;	// Soft Karaoke .kar files
					_softKarTrack = trkState->trkID;
					vis_addstr("Soft Karaoke Mode enabled.");
				}
				if (_karaokeMode == 1)
				{
					if (text.length() >= 2 && text[0] == '@')
					{
						// tag handling according to https://www.mixagesoftware.com/en/midikit/help/HTML/karaoke_formats.html
						// TODO: Should I really lock karaoke handling to a certain track?
						// I've seen MIDIs that have the @K/@V in the 2nd track and @T + all lyrics in the 3rd track.
						_softKarTrack = trkState->trkID;
						switch(text[1])
						{
						case 'K':	// .kar mode enable
							break;
						case 'V':	// version information
							break;
						case 'L':	// language
							break;
						case 'T':	// song title, artist, sequencer
							break;
						case 'I':	// additional information
							break;
						}
						break;	// allow printing song information
					}
					if (_softKarTrack == trkState->trkID)
						return;	// but don't show karaoke text
				}
			}
			break;
		//case 0x02:	// Copyright
		case 0x03:	// Track/Sequence Name
			if (trkState->trkID == 0 || _cMidi->GetMidiFormat() == 2)
			{
				std::string text = Vector2String(midiEvt->evtData);
				//printf("Text: %s\n", text.c_str());
				if (trkState->trkID == 0 && _rcpMidTextMode < 2)
				{
					// fix poor title/comment conversion of RCP2MID programs
					if (_rcpMidTextMode == 0)
					{
						// The event is the song title, which is always 64 characters long. (padded with spaces)
						if (text.length() == 0x40)
							_rcpMidTextMode = 1;
						// We don't remove the padding, so we can just let it be printed as is.
					}
					else if (_rcpMidTextMode == 1)
					{
						// The "song title" is followed by another event that contains the comment section.
						// The comment section consists of 12 lines with 28 (RCP v2) or 30 (RCP v3) characters each.
						// There are no "newline" characters in RCP songs. Instead, each line is padded with spaces.
						if (text.length() == 12 * 28 || text.length() == 12 * 30)
						{
							size_t lineSize = text.length() / 12;
							size_t curPos;
							
							// print each line separately (and use "Text" event instead of "Title")
							for (curPos = 0; curPos < text.length(); curPos += lineSize)
								vis_print_meta(trkState->trkID, 0x01, lineSize, (const char*)&midiEvt->evtData[curPos]);
							_rcpMidTextMode = 2;
							return;
						}
					}
				}
			}
			break;
		//case 0x04:	// Instrument Name
		case 0x05:	// Lyric
			return;	// don't print for now
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
		//case 0x07:	// Cue Point
		//case 0x08:	// Program Name
		//case 0x09:	// Port Name
		//case 0x20:	// Channel Prefix
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
		case 0x51:	// Tempo
			_midiTempo = ReadBE24(&midiEvt->evtData[0x00]);
			RefreshTickTime();
			break;
		//case 0x54:	// SMPTE offset
		//case 0x58:	// Time Signature
		//case 0x59:	// Key Signature
		//case 0x7F:	// Sequence Specific
		}
		if (midiEvt->evtData.empty())
			vis_print_meta(trkState->trkID, midiEvt->evtValA, 0, NULL);
		else
			vis_print_meta(trkState->trkID, midiEvt->evtValA, midiEvt->evtData.size(), (const char*)&midiEvt->evtData[0x00]);
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
	if (! _tmrStep && curTime < _tmrMinStart)
		_tmrStep = _tmrMinStart;	// handle "initial delay" after starting the song
	if (_tmrFadeLen && _tmrFadeStart == (UINT64)-1)
	{
		_tmrFadeStart = curTime;	// start fading
		_tmrFadeNext = _tmrFadeStart;
	}
	
	if (_tmrFadeLen && curTime >= _tmrFadeNext)
	{
		UINT64 fadeStep;
		
		_tmrFadeNext += _tmrFreq * 100 / 1000;	// update once every 0.1 s
		
		fadeStep = (curTime - _tmrFadeStart) * 0x100 / _tmrFadeLen;
		_fadeVol = (fadeStep < 0x100) ? (UINT16)(0x100 - fadeStep) : 0x00;
		
		FadeVolRefresh();
		
		if (_fadeVol == 0x00)
			Stop();
	}
	if (curTime < _tmrStep)
		return;
	
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
		if (_tmrStep + _tmrFreq * 1 < curTime)
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
	if (chnSt->ctrls[ctrlID] > 0x7F && (_options.flags & PLROPTS_STRICT))
	{
		vis_printf("Warning: Bad controller data (%02X %02X %02X)\n", midiEvt->evtType, midiEvt->evtValA, midiEvt->evtValB);
		if (ctrlID == 0x07 || ctrlID == 0x0B)
			chnSt->ctrls[ctrlID] = 0x7F;	// clip to maximum volume
		else
			chnSt->ctrls[ctrlID] &= 0x7F;	// just remove the invalid bit
	}
	
	switch(ctrlID)
	{
	case 0x00:	// Bank MSB
		chnSt->insState[0] = chnSt->ctrls[ctrlID];
		break;
	case 0x20:	// Bank LSB
		chnSt->insState[1] = chnSt->ctrls[ctrlID];
		break;
	case 0x07:	// Main Volume
		if (_filteredVol & (1 << FILTVOL_CCVOL))
		{
			UINT8 val = (UINT8)((chnSt->ctrls[ctrlID] *  _fadeVol + 0x80) / 0x100);
			nvChn->_attr.volume = val;
			MidiOutPort_SendShortMsg(outPort, midiEvt->evtType, ctrlID, val);
			return true;
		}
		nvChn->_attr.volume = chnSt->ctrls[ctrlID];
		break;
	case 0x0A:	// Pan
		{
			UINT8 panVal = chnSt->ctrls[ctrlID];
			bool didPatch = false;
			
			if (_options.srcType == MODULE_MT32)
			{
				panVal ^= 0x7F;	// MT-32 uses 0x7F (left) .. 0x3F (center) .. 0x00 (right)
				didPatch = true;
			}
			if (panVal == 0x00)
				panVal = 0x01;	// pan level 0 and 1 are the same in GM/GS/XG
			nvChn->_attr.pan = (INT8)panVal - 0x40;
			if (didPatch && MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
			{
				// MT-32 on GS: send GM-compatible Pan value
				MidiOutPort_SendShortMsg(outPort, midiEvt->evtType, ctrlID, panVal);
				return true;
			}
		}
		break;
	case 0x0B:	// Expression
		if (_filteredVol & (1 << FILTVOL_CCEXPR))
		{
			UINT8 val = (UINT8)((chnSt->ctrls[ctrlID] *  _fadeVol + 0x80) / 0x100);
			nvChn->_attr.expression = val;
			MidiOutPort_SendShortMsg(outPort, midiEvt->evtType, ctrlID, val);
			return true;
		}
		nvChn->_attr.expression = chnSt->ctrls[ctrlID];
		break;
	case 0x06:	// Data Entry MSB
		if (chnSt->rpnCtrl[0] == 0x00)
		{
			switch(chnSt->rpnCtrl[1])
			{
			case 0x00:	// Pitch Bend Range
				chnSt->pbRange = chnSt->ctrls[ctrlID];
				if (chnSt->pbRange > 24)
					chnSt->pbRange = 24;
				nvChn->_pbRange = chnSt->pbRange;
				break;
			case 0x01:	// Fine Tuning
				chnSt->tuneFine &= ~0xFF00;
				chnSt->tuneFine |= ((INT16)chnSt->ctrls[ctrlID] - 0x40) << 8;
				nvChn->_detune = (INT8)(chnSt->tuneFine >> 8);
				nvChn->_attr.detune[1] = (nvChn->_transpose << 8) + (nvChn->_detune << 0);
				break;
			case 0x02:	// Coarse Tuning
				chnSt->tuneCoarse = (INT8)chnSt->ctrls[ctrlID] - 0x40;
				if (chnSt->tuneCoarse < -24)
					chnSt->tuneCoarse = -24;
				else if (chnSt->tuneCoarse > +24)
					chnSt->tuneCoarse = +24;
				nvChn->_transpose = chnSt->tuneCoarse;
				nvChn->_attr.detune[1] = (nvChn->_transpose << 8) + (nvChn->_detune << 0);
				break;
			}
		}
		else if (chnSt->rpnCtrl[0] >= (0x80|0x14) && chnSt->rpnCtrl[0] <= (0x80|0x35))
		{
			chnSt->hadDrumNRPN = true;
		}
		break;
	case 0x26:	// Data Entry LSB
		if (chnSt->rpnCtrl[0] == 0x00)
		{
			switch(chnSt->rpnCtrl[1])
			{
			case 0x01:	// Fine Tuning
				chnSt->tuneFine &= ~0x00FF;
				chnSt->tuneFine |= chnSt->ctrls[ctrlID] << 1;
				nvChn->_detune = (INT8)(chnSt->tuneFine >> 8);
				nvChn->_attr.detune[1] = (nvChn->_transpose << 8) + (nvChn->_detune << 0);
				break;
			}
		}
		break;
	case 0x62:	// NRPN LSB
		chnSt->rpnCtrl[1] = 0x80 | chnSt->ctrls[ctrlID];
		break;
	case 0x63:	// NRPN MSB
		chnSt->rpnCtrl[0] = 0x80 | chnSt->ctrls[ctrlID];
		if (true)
			break;
		if (chnSt->ctrls[ctrlID] == 20)
		{
			vis_addstr("NRPN Loop Start found.");
			SaveLoopState(_loopPt, trkSt);
		}
		else if (chnSt->ctrls[ctrlID] == 30)
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
		chnSt->rpnCtrl[1] = 0x00 | chnSt->ctrls[ctrlID];
		break;
	case 0x65:	// RPN MSB
		chnSt->rpnCtrl[0] = 0x00 | chnSt->ctrls[ctrlID];
		break;
	case 0x6F:	// RPG Maker loop controller
		if (chnSt->ctrls[ctrlID] == 0 || chnSt->ctrls[ctrlID] == 111 || chnSt->ctrls[ctrlID] == 127)
		{
			if (! _loopPt.used)
			{
				vis_addstr("RPG Maker Loop Point found.");
				SaveLoopState(_loopPt, trkSt);
			}
		}
		else
		{
			vis_printf("Ctrl 111, value %u.", chnSt->ctrls[ctrlID]);
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
		chnSt->rpnCtrl[0] = 0x7F;	// reset RPN state
		chnSt->rpnCtrl[1] = 0x7F;
		chnSt->pbRange = _defPbRange;
		nvChn->_attr.volume = chnSt->ctrls[0x07];
		nvChn->_attr.pan = (INT8)chnSt->ctrls[0x0A] - 0x40;
		nvChn->_attr.expression = chnSt->ctrls[0x0B];
		nvChn->_pbRange = chnSt->pbRange;
		break;
	case 0x7B:	// All Notes Off
		chnSt->notes.clear();
		nvChn->ClearNotes();
		vis_do_channel_event(FULL_CHN_ID(chnSt->portID, chnSt->midChn), 0x01, 0x00);
		break;
	}
	
	if (ctrlID != midiEvt->evtValA || chnSt->ctrls[ctrlID] != midiEvt->evtValB)
	{
		MidiOutPort_SendShortMsg(outPort, midiEvt->evtType, ctrlID, chnSt->ctrls[ctrlID]);
		return true;
	}
	
	return false;
}

static const INS_DATA* GetInsMapData(const INS_PRG_LST* insPrg, UINT8 msb, UINT8 lsb, UINT8 maxModuleID)
{
	const INS_DATA* insData;
	const INS_DATA* idLowerMod;
	UINT32 curIns;
	
	idLowerMod = NULL;
	for (curIns = 0; curIns < insPrg->count; curIns ++)
	{
		insData = &insPrg->instruments[curIns];
		if (msb == 0xFF || insData->bankMSB == msb)
		{
			if (lsb == 0xFF || insData->bankLSB == lsb)
			{
				if (insData->moduleID == maxModuleID)
					return insData;
				else if (insData->moduleID < maxModuleID && idLowerMod == NULL)
					idLowerMod = insData;
			}
		}
	}
	return idLowerMod;
}

static const INS_DATA* GetExactInstrument(const INS_BANK* insBank, const MidiPlayer::InstrumentInfo* insInf, UINT8 maxModuleID)
{
	const INS_DATA* insData;
	UINT8 msb;
	UINT8 lsb;
	UINT8 ins;
	
	if (insBank == NULL)
		return NULL;
	
	// try exact match first
	insData = GetInsMapData(&insBank->prg[insInf->ins], insInf->bank[0], insInf->bank[1], maxModuleID);
	if (insData != NULL || ! insInf->bnkIgn)
		return insData;
	
	msb = (insInf->bnkIgn & BNKMSK_MSB) ? 0xFF : insInf->bank[0];
	lsb = (insInf->bnkIgn & BNKMSK_LSB) ? 0xFF : insInf->bank[1];
	ins = (insInf->bnkIgn & BNKMSK_INS) ? (insInf->ins & 0x80) : insInf->ins;
	return GetInsMapData(&insBank->prg[ins], msb, lsb, maxModuleID);
}

void MidiPlayer::HandleIns_CommonPatches(const ChannelState* chnSt, InstrumentInfo* insInf, UINT8 devType, const INS_BANK* insBank)
{
	if (devType == MODULE_GM_1)
	{
		insInf->bnkIgn = BNKMSK_ALLBNK;
		if (chnSt->flags & 0x80)
			insInf->bnkIgn |= BNKMSK_INS;	// there is only 1 drum kit
	}
	else if (MMASK_TYPE(devType) == MODULE_TYPE_GS)
	{
		if (chnSt->flags & 0x80)
			insInf->bnkIgn |= BNKMSK_MSB;	// ignore MSB on drum channels
	}
	else if (MMASK_TYPE(devType) == MODULE_TYPE_XG)
	{
		if ((chnSt->flags & 0x80) || insInf->bank[0] == 0x40)
			insInf->bnkIgn |= BNKMSK_LSB;	// ignore LSB on drum channels and SFX banks
	}
	else if (devType == MODULE_MT32)
	{
		if (insBank != NULL && insBank->maxBankMSB >= 0x01)
		{
			// when supported by the instrument bank, do CM-32L/P instrument set selection
			insInf->bnkIgn = BNKMSK_LSB;
			if (chnSt->midChn <= 0x09)
				insInf->bank[0] = 0x00;	// MT-32/CM-32L set
			else
				insInf->bank[0] = 0x01;	// CM-32P set
		}
		else
		{
			insInf->bnkIgn = BNKMSK_ALLBNK;
		}
		if (chnSt->flags & 0x80)
			insInf->bnkIgn |= BNKMSK_INS;	// there is only 1 drum kit
	}
	else
	{
		// generic handler
		if (insBank == NULL || insBank->maxBankMSB == 0x00)
			insInf->bnkIgn |= BNKMSK_MSB;
		if (insBank == NULL || insBank->maxBankLSB == 0x00)
			insInf->bnkIgn |= BNKMSK_LSB;
		if ((chnSt->flags & 0x80) && (insBank == NULL || insBank->maxDrumKit == 0x00))
			insInf->bnkIgn |= BNKMSK_INS;
	}
	
	return;
}

void MidiPlayer::HandleIns_DoFallback(const ChannelState* chnSt, InstrumentInfo* insInf, UINT8 devType, const INS_BANK* insBank)
{
	if (chnSt->userInsID != 0xFFFF)
		return;
	
	if (MMASK_TYPE(devType) == MODULE_TYPE_GS)
	{
		if (devType == MODULE_SC55)
		{
			UINT8 bankLSB = (insInf->bnkIgn & BNKMSK_LSB) ? 0xFF : insInf->bank[1];
			
			// This implements the Capital Tone Fallback mode from SC-55 v1.
			if (! (chnSt->flags & 0x80))
			{
				// melody mode
				if (insBank != NULL)
				{
					const INS_PRG_LST* insPrg = &insBank->prg[insInf->ins];
					
					// 1. sub-CTF according to https://www.vogons.org/viewtopic.php?p=501280#p501280
					insInf->bank[0] &= ~0x07;
					if (GetInsMapData(insPrg, insInf->bank[0], bankLSB, 0xFF) != NULL)
						return;
				}
				
				// 2. fall back to GM sound
				insInf->bank[0] = 0x00;
			}
			else
			{
				// drum CTF according to https://www.vogons.org/viewtopic.php?p=501038#p501038
				UINT8 newIns;
				if ((insInf->ins & 0x7F) < 0x40)
					newIns = insInf->ins & ~0x07;
				else
					newIns = insInf->ins;
				if (GetInsMapData(&insBank->prg[newIns], insInf->bank[0], bankLSB, 0xFF) != NULL)
				{
					insInf->ins = newIns;
					return;
				}
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
		UINT8 msbH = insInf->bank[0] & 0xF0;
		UINT8 msbL = insInf->bank[0] & 0x0F;
		
		if (insInf->bank[0] == 0x3F)
			return;	// user instrument
		if (msbH >= 0x20 && msbH <= 0x60 && msbL >= 0x01 && msbL <= 0x03)
			return;	// special voices for PLG100 boards
		
		// XG has capital tone fallback by setting Bank LSB to 00
		// for Bank MSB 0, this results in GM sounds.
		// for Bank MSB 64/126/127, LSB isn't used anyway
		// for all unused/invalid Bank MSB, it results in silence
		insInf->bank[1] = 0x00;
	}
	else
	{
		// generic fallback code
		insInf->bnkIgn |= BNKMSK_ALLBNK;
	}
	
	return;
}

void MidiPlayer::HandleIns_GetOriginal(const ChannelState* chnSt, InstrumentInfo* insInf)
{
	const INS_BANK* insBank;
	const UINT8 devType = _options.srcType;
	UINT8 mapModType;
	
	insInf->bank[0] = chnSt->ctrls[0x00];
	insInf->bank[1] = chnSt->ctrls[0x20];
	insInf->ins = (chnSt->flags & 0x80) | (chnSt->curIns & 0x7F);
	insInf->bnkIgn = BNKMSK_NONE;
	insBank = SelectInsMap(devType, &mapModType);
	
	HandleIns_CommonPatches(chnSt, insInf, devType, insBank);
	if (MMASK_TYPE(devType) == MODULE_TYPE_GS)
	{
		if (devType == MODULE_SC55 || devType == MODULE_TG300B)
		{
			// SC-55 ignores Bank LSB
			insInf->bnkIgn |= BNKMSK_LSB;
		}
		else
		{
			// SC-88+: force GS instrument bank LSB to 01+
			if (insInf->bank[1] == 0x00)	// Bank LSB = default bank
			{
				// enforce the device's "native" bank
				UINT8 insMap = (chnSt->defInsMap != 0xFF) ? chnSt->defInsMap : (_defSrcInsMap & 0x7F);
				if (insMap < 0x7F)
					insInf->bank[1] = 0x01 + insMap;
				else
					insInf->bnkIgn |= BNKMSK_LSB;	// unknown device - find anything
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
		if (MMASK_MOD(devType) >= MTXG_MU100)
		{
			UINT8 insMap = (chnSt->defInsMap != 0xFF) ? chnSt->defInsMap : (_defSrcInsMap & 0x7F);
			UINT8 insBank = insMap ? 0x7E : 0x7F;	// 7F = MU Basic, 7E = MU100 Native
			if (insInf->bank[0] == 0x00 && insInf->bank[1] == 0x00)
				insInf->bank[1] = insBank;
			else if (insInf->bank[0] == 0x7F && insInf->ins == (0x80|0x00))
				insInf->ins = 0x80 | insBank;
		}
	}
	
	insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType);
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
				// get instrument name from the instrument the user instrument is based on
				insInf->bankPtr = GetExactInstrument(insBank, &tmpII, mapModType);
			}
		}
		else
		{
			// handle device-specific fallback modes
			HandleIns_DoFallback(chnSt, insInf, devType, insBank);
			insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType);
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
	insInf->bnkIgn = BNKMSK_NONE;
	strictPatch = BNKMSK_NONE;
	insBank = SelectInsMap(devType, &mapModType);
	
	HandleIns_CommonPatches(chnSt, insInf, devType, insBank);
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
			if (chnSt->insOrg.bnkIgn & BNKMSK_LSB)	// for SC-55 / TG300B
				insInf->bank[1] = 0x00;
			if (MMASK_TYPE(_options.srcType) != MODULE_TYPE_GS)
			{
				insInf->bank[0] = 0x00;
				insInf->bank[1] = 0x00;
				// do NOT mark as "strict", as these patches are required for proper playback
			}
			if (insInf->bank[1] == 0x00)
			{
				// when set to "default" map, select correct map based on source song/destination device
				UINT8 defaultMap;
				
				if (_options.flags & PLROPTS_STRICT)
				{
					// GS song: use bank that is optimal for the song
					// GM song: use "native" bank for device
					if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
						defaultMap = _defSrcInsMap & 0x7F;
					else
						defaultMap = _defDstInsMap;
				}
				else
				{
					// non-strict mode: just look up current device
					defaultMap = _defDstInsMap;
				}
				
				insInf->bank[1] = 0x01 + defaultMap;
				strictPatch |= BNKMSK_LSB;	// mark for undo when not strict
			}
			if ((chnSt->insOrg.bnkIgn & BNKMSK_INS) && (chnSt->flags & 0x80))
			{
				// drum kit fallback for GM songs
				// TODO: make this an option
				if ((insInf->ins & 0x47) > 0x00 && insInf->ins != (0x80|0x19))
				{
					insInf->ins = 0x80 | 0x00;	// for GM, enforce Standard Kit 1 for non-GS drum kits
					strictPatch |= BNKMSK_INS;
				}
			}
			if (insInf->bank[1] > 0x01 + MMASK_MOD(devType))
			{
				// When playing an SC-88Pro MIDI on SC-88, we have to fix
				// the Bank LSB setting to make the instruments work at all.
				if (insInf->ins == (0x80|0x7F) || insInf->bank[0] >= 0x7E)
					insInf->bank[1] = 0x01 + MTGS_SC55;
				else
					insInf->bank[1] = 0x01 + MMASK_MOD(devType);
			}
		}
		if (chnSt->flags & 0x80)
		{
			// set (ignored) MSB to 0 on drum channels
			insInf->bank[0] = 0x00;
			strictPatch |= BNKMSK_MSB;
		}
		if (insBank != NULL && insBank->maxBankLSB == 0x00)
			insInf->bnkIgn |= BNKMSK_LSB;
	}
	else if (MMASK_TYPE(devType) == MODULE_TYPE_XG)
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
		if (_options.flags & PLROPTS_STRICT)
		{
			if (MMASK_MOD(devType) >= MTXG_MU100)
			{
				UINT8 gblInsMap;
				UINT8 insMap;
				UINT8 insBank;
				
				if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_XG)
					gblInsMap = _defSrcInsMap & 0x7F;
				else
					gblInsMap = _defDstInsMap;
				insMap = (chnSt->defInsMap != 0xFF) ? chnSt->defInsMap : gblInsMap;
				
				insBank = insMap ? 0x7E : 0x7F;	// 7F = MU Basic, 7E = MU100 Native
				if (insInf->bank[0] == 0x00 && insInf->bank[1] == 0x00)
					insInf->bank[1] = insBank;
				else if (insInf->bank[0] == 0x7F && insInf->ins == (0x80|0x00))
					insInf->ins = 0x80 | insBank;
			}
			else
			{
				// explicit instrument map (7E/7F) -> GM map (00)
				if (insInf->bank[0] == 0x00 && insInf->bank[1] >= 0x7E)
					insInf->bank[1] = 0x00;
				else if (insInf->bank[0] == 0x7F && insInf->ins >= (0x80|0x7E))
					insInf->ins = 0x80 | 0x00;
			}
		}
		if ((chnSt->flags & 0x80) || insInf->bank[0] == 0x40)
		{
			// set (ignored) LSB to 0 on drum channels and SFX banks
			insInf->bank[1] = 0x00;
			strictPatch |= BNKMSK_LSB;
		}
	}
	else if (devType == MODULE_MT32)
	{
		strictPatch = ~insInf->bnkIgn & BNKMSK_ALLBNK;	// mark for undo when not strict
	}
	
	insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType);
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
				insInf->bankPtr = GetExactInstrument(insBank, &tmpII, mapModType);
			}
		}
		else if (_options.flags & PLROPTS_ENABLE_CTF)
		{
			// handle device-specific fallback modes
			if (MMASK_TYPE(devType) == MODULE_TYPE_GS)
			{
				// 1. ignore the instrument map (Bank LSB)
				insInf->bnkIgn |= BNKMSK_LSB;
				insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType);
				if (insInf->bankPtr != NULL)
					insInf->bank[1] = insInf->bankPtr->bankLSB;
				
				// 2. ignore the variation setting (Bank MSB) via SC-55 fallback
				if (insInf->bankPtr == NULL)
				{
					HandleIns_DoFallback(chnSt, insInf, MODULE_SC55, insBank);
					insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType);
				}
				
				// 3. fall back to GM
			}
			else if (MMASK_TYPE(devType) == MODULE_TYPE_XG)
			{
				if (insInf->bank[0] > 0x00 && insInf->bank[0] < 0x40)
					insInf->bank[0] = 0x00;	// additional Bank MSB fallback to prevent sounds from going silent
				HandleIns_DoFallback(chnSt, insInf, devType, insBank);
				insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType);
				
				if (insInf->bankPtr == NULL)
				{
					// GM fallback while keeping melody/drum mode intact
					if (chnSt->flags & 0x80)
					{
						insInf->bank[0] = 0x7F;
						insInf->bnkIgn |= BNKMSK_LSB | BNKMSK_INS;
					}
					else
					{
						insInf->bank[0] = 0x00;
						insInf->bnkIgn |= BNKMSK_LSB;
					}
					insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType);
				}
			}
			else
			{
				HandleIns_DoFallback(chnSt, insInf, devType, insBank);
				insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType);
			}
			if (insInf->bankPtr == NULL)
			{
				// try GM fallback
				insInf->bnkIgn |= BNKMSK_ALLBNK;
				if (chnSt->flags & 0x80)
					insInf->bnkIgn |= BNKMSK_INS;
				insInf->bankPtr = GetExactInstrument(insBank, insInf, mapModType);
			}
			if (insInf->bankPtr != NULL)
			{
				insInf->bank[0] = insInf->bankPtr->bankMSB;
				insInf->bank[1] = insInf->bankPtr->bankLSB;
				insInf->ins = insInf->bankPtr->program;
			}
			strictPatch = BNKMSK_NONE;
		}
	}
	
	if (! (_options.flags & PLROPTS_STRICT))
	{
		if (strictPatch & BNKMSK_MSB)
			insInf->bank[0] = insIOld.bank[0];
		if (strictPatch & BNKMSK_LSB)
			insInf->bank[1] = insIOld.bank[1];
		if (strictPatch & BNKMSK_INS)
			insInf->ins = insIOld.ins;
	}
	else //if (_options.flags & PLROPTS_STRICT)
	{
		if (devType == MODULE_SC55 || devType == MODULE_TG300B)
		{
			// We had to use LSB 01 for instrument lookup, but we actually send LSB 00.
			// All SC-55 models ignore LSB, but early XG devices in TG300B mode do not.
			// (MU80 is known to require LSB 0, DB50XG/S-YXG50 and MU128 don't care.)
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
	chnSt->userInsName = NULL;
	
	// handle user instruments and channel mode changes
	if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
	{
		if (MMASK_MOD(_options.srcType) >= MTGS_SC88 && MMASK_MOD(_options.srcType) != MTGS_TG300B)
		{
			if ((chnSt->flags & 0x80) && (chnSt->curIns == 0x40 || chnSt->curIns == 0x41))	// user drum kit
			{
				chnSt->userInsID = 0x8000 | (chnSt->curIns & 0x01);
				if (_sc88UsrDrmNames[chnSt->curIns & 0x01][0] != '\0')	// use only when set by the MIDI
					chnSt->userInsName = _sc88UsrDrmNames[chnSt->curIns & 0x01].c_str();
			}
			else if (bankMSB == 0x40 || bankMSB == 0x41)	// user instrument
			{
				chnSt->userInsID = ((bankMSB & 0x01) << 7) | chnSt->curIns;
			}
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
#if 1
				// for now enforce drum mode on channel 9
				// TODO: XG allows ch 9 to be melody - what are the exact conditions??
				chnSt->flags |= 0x80;
				vis_addstr("Keeping drum mode on ch 9!");
#endif
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
	if (_options.dstType == MODULE_MT32)
	{
		// On the MT-32, you can remap the patch set to other timbres.
		InstrumentInfo insInf;
		const INS_BANK* insBank;
		UINT8 mapModType;
		
		insInf.bank[0] = (chnSt->midChn <= 0x09) ? 0x00 : 0x01;	// MT-32/CM-32L (0x00) vs. CM-32P (0x01)
		insInf.bank[1] = 0x00;
		if (insInf.bank[0] == 0x00)
			insInf.ins = (_mt32PatchTGrp[chnSt->curIns] << 6) | (_mt32PatchTNum[chnSt->curIns] << 0);
		else
			insInf.ins = (_cm32pPatchTMedia[chnSt->curIns] << 7) | (_cm32pPatchTNum[chnSt->curIns] << 0);
		if (insInf.ins < 0x80)
		{
			// MT-32/CM-32L: timbre group A/B
			// CM-32P: internal group
			if (insInf.ins != chnSt->curIns)
			{
				insBank = SelectInsMap(_options.dstType, &mapModType);
				chnSt->insSend.bankPtr = GetExactInstrument(insBank, &insInf, mapModType);
				chnSt->userInsName = chnSt->insSend.bankPtr->insName;
			}
		}
		else
		{
			if (insInf.bank[0] == 0x00)
			{
				if (insInf.ins < 0xC0)
				{
					// MT-32/CM-32L: timbre group I
					chnSt->userInsName = _mt32TimbreNames[insInf.ins & 0x3F].c_str();
				}
				else
				{
					// MT-32/CM-32L: timbre group R
					// TODO: I need drum names for this one.
				}
			}
			else
			{
				// CM-32P: card group
			}
		}
	}
	
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
		didPatch |= ((chnSt->insSend.ins & 0x7F) != chnSt->curIns) << BNKBIT_INS;
		if (_options.flags & PLROPTS_STRICT)
		{
			if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
			{
				// only Bank LSB was patched and it was patched from 0 (default instrument map) to the "native" map
				if (chnSt->ctrls[0x20] == 0x00 && didPatch == BNKMSK_LSB)
				{
					didPatch = BNKMSK_NONE;	// hide patching default instrument map in strict mode
					showOrgIns = true;
				}
			}
			else if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_XG)
			{
				// MU instrument map for GM sounds (MSB 0, LSB 0 or drum kit 0)
				if ((chnSt->ctrls[0x00] == 0x00 && chnSt->ctrls[0x20] == 0x00 && didPatch == BNKMSK_LSB) ||
					(chnSt->ctrls[0x00] == 0x7F && chnSt->curIns == 0x00 && didPatch == BNKMSK_INS))
				{
					didPatch = BNKMSK_NONE;	// hide patching default instrument map in strict mode
					showOrgIns = true;
				}
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
	chnSt->hadDrumNRPN = false;	// setting the drum kit resets any drum NRPN modifications
	return true;
}

static void SanitizeSysExText(std::string& text)
{
	// I've seen a few MIDIs using byte 0x00 for spaces. (SC-55 text)
	// MIDIs from Settlers II use byte 0x14 for spaces (MU-80 text)
	size_t curChr;
	
	for (curChr = 0; curChr < text.size(); curChr ++)
	{
		if (text[curChr] >= 0x00 && text[curChr] <= 0x1F)
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

static void PrintPortChn(char* buffer, UINT8 port, UINT8 chn)
{
	buffer[0] = (port < 0x1A) ? ('A' + port) : '-';
	if (chn < 0x10)
		sprintf(&buffer[1], "%02u", 1 + chn);
	else
		strcpy(&buffer[1], "--");
	
	return;
}

static bool CheckRolandChecksum(size_t dataLen, const UINT8* data)
{
	if (dataLen <= 0x03)
		return true;	// SysEx that ends right after the address - my SC-88VL doesn't complain about it
	
	size_t curPos;
	UINT8 sum = 0x00;
	
	for (curPos = 0x00; curPos < dataLen; curPos ++)
		sum += data[curPos];
	sum &= 0x7F;
	
	return ! sum;
}

bool MidiPlayer::HandleSysExMessage(const TrackState* trkSt, const MidiEvent* midiEvt)
{
	size_t syxSize = midiEvt->evtData.size();
	if (! syxSize)
		return false;
	const UINT8* syxData = &midiEvt->evtData[0x00];
	
	if (syxSize >= 1 && syxData[0x00] == 0xF0)
	{
		char syxStr[0x10];
		PrintHexDump(0x10, syxStr, midiEvt->evtData.size(), &midiEvt->evtData[0x00], 0x03);
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
		PrintHexDump(0x10, syxStr, midiEvt->evtData.size(), &midiEvt->evtData[0x00], 0x03);
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
		// Data[0x03] == Command ID
		if (syxData[0x03] == 0x11)	// ReQuest 1 (RQ1)
		{
			if (syxSize < 0x0B)
				break;	// We need enough bytes for a full address + checksum.
			
			UINT32 addr = ReadBE24(&syxData[0x04]);
			UINT32 size = ReadBE21(&syxData[0x07]);
			vis_printf("SysEx: GS Request: Address %06X, size %06X\n", addr, size);
		}
		else if (syxData[0x03] == 0x12)	// Data Transmit 1 (DT1)
		{
			if (syxSize < 0x08)
				break;	// We need enough bytes for a full address + checksum.
			if (! CheckRolandChecksum(syxSize - 0x05, &syxData[0x04]))	// check data from address until (not including) 0xF7 byte
				vis_addstr("Warning: SysEx Roland checksum invalid!\n");
			
			if (syxData[0x02] == 0x16)
				return HandleSysEx_MT32(trkSt->portID, syxSize, syxData);
			else if (syxData[0x02] == 0x42)
				return HandleSysEx_GS(trkSt->portID, syxSize, syxData);
			else if (syxData[0x02] == 0x45)
			{
				UINT32 addr = ReadBE24(&syxData[0x04]);
				switch(addr & 0xFFFF00)
				{
				case 0x100000:	// ASCII Display
				{
					std::string dispMsg = Vector2String(syxData, 0x07, syxSize - 2);
					
					SanitizeSysExText(dispMsg);
					vis_printf("SysEx SC: Display = \"%s\"", dispMsg.c_str());
					vis_do_syx_text(FULL_CHN_ID(trkSt->portID, 0x00), 0x45, dispMsg.length(), dispMsg.data());
				}
					break;
				case 0x100100:	// Dot Display (page 1-10)
				case 0x100200:
				case 0x100300:
				case 0x100400:
				case 0x100500:
				{
					UINT8 pageID;
					UINT8 startOfs;
					UINT8 dataLen;
					
					pageID = (((addr & 0x00FF00) >> 7) | ((addr & 0x000040) >> 6)) - 1;
					if (pageID == 1)
						vis_printf("SysEx SC: Dot Display (Load/Show Page %u)", pageID);
					else
						vis_printf("SysEx SC: Dot Display: Load Page %u", pageID);
					
					// Partial display write DO work and I've seen MIDIs doing it.
					startOfs = addr & 0x3F;
					dataLen = syxSize - 0x09;	// 0x07 (start ofs) + 1 (checksum) + 1 (F7 terminator)
					if (startOfs + dataLen > 0x40)
						dataLen = 0x40 - startOfs;
					memcpy(&_pixelPageMem[pageID - 1][startOfs], &syxData[0x07], dataLen);
					
					if (pageID == 1)	// only page 1 is shown instantly
						vis_do_syx_bitmap(FULL_CHN_ID(trkSt->portID, pageID), 0x45, 0x40, _pixelPageMem[pageID - 1]);
				}
					break;
				case 0x102000:
					if (addr == 0x102000)	// Dot Display: show page
					{
						UINT8 pageID;
						
						pageID = syxData[0x07];	// 00 = bar display, 01..0A = page 1..10
						vis_printf("SysEx SC: Dot Display: Show Page %u", pageID);
						if (pageID >= 1 && pageID <= 10)
							vis_do_syx_bitmap(FULL_CHN_ID(trkSt->portID, pageID), 0x45, 0x40, _pixelPageMem[pageID - 1]);
						else
							vis_do_syx_bitmap(FULL_CHN_ID(trkSt->portID, pageID), 0x45, 0x00, NULL);
					}
					else if (addr == 0x102001)	// Dot Display: set display time
					{
						float dispTime;
						
						dispTime = syxData[0x07] * 0.48f;
						vis_printf("SysEx SC: Dot Display: set display time = %.2f sec", dispTime);
					}
					break;
				}
			}
		}
		break;
	case 0x43:	// YAMAHA ID
		// Data[0x01] == 0xcn - Command ID c, Device Number n
		switch(syxData[0x01] & 0xF0)
		{
		case 0x00:	// Bulk Dump
			if (syxSize < 0x08)
				break;	// We need enough bytes for byte count + address.
			{
				UINT16 size = ReadBE14(&syxData[0x03]);
				UINT32 addr = ReadBE24(&syxData[0x05]);
				vis_printf("SysEx: XG Bulk Dump to address %06X, size %04X\n", addr, size);
			}
			break;
		case 0x10:	// Parameter Change
			if (syxSize < 0x07)
				break;	// We need enough bytes for a full address + at least 1 byte of data.
			if (syxData[0x02] == 0x4C)	// XG
				return HandleSysEx_XG(trkSt->portID, syxSize, syxData);
			else if (syxData[0x02] == 0x49)	// MU native
			{
				UINT32 addr = ReadBE24(&syxData[0x03]);
				switch(addr)
				{
				case 0x000012:	// Select Voice Map (MU100+ only)
					// 00 = MU Basic, 01 = MU100 Native
					vis_printf("SysEx MU: Set Voice Map to %u (%s)", syxData[0x06],
							syxData[0x06] ? "MU100 Native" : "MU Basic");
					// Note: This can break drum channels, if sent in the following order:
					//	- Bank MSB 127
					//	- SysEx Voice Map
					//	- Program Change
					_defSrcInsMap = syxData[0x06];
					break;
				}
			}
			break;
		case 0x20:	// Dump Request
		case 0x30:	// Parameter Request
			if (syxSize < 0x06)
				break;
			
			UINT32 addr = ReadBE24(&syxData[0x03]);
			if (syxData[0x01] & 0x10)
				vis_printf("SysEx: XG Request Parameter from address %06X\n", addr);
			else
				vis_printf("SysEx: XG Request Dump from address %06X\n", addr);
			break;
		}
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
				_mstVol = syxData[0x05];
				vis_printf("SysEx GM: Master Volume = %u", _mstVol);
				if (_filteredVol & (1 << FILTVOL_GMSYX))
					return true;	// don't send when fading
				_noteVis.GetAttributes().volume = _mstVol;
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
	addr = ReadBE24(&syxData[0x04]);
	switch(addr & 0xFF0000)	// Address High
	{
	case 0x030000:	// Patch Temporary Area
		if ((addr & 0x0F) > 0x00)
			break;	// Right now we can only handle bulk writes.
		dataPtr = &syxData[0x07];
		if (addr < 0x030110)
		{
			UINT8 evtChn;
			UINT16 portChnID;
			ChannelState* chnSt = NULL;
			NoteVisualization::ChnInfo* nvChn = NULL;
			UINT8 newIns;
			
			evtChn = 1 + ((addr & 0x0000F0) >> 4);
			portChnID = FULL_CHN_ID(portID, evtChn);
			if (portChnID >= _chnStates.size())
				return false;
			chnSt = &_chnStates[portChnID];
			nvChn = _noteVis.GetChannel(portChnID);
			newIns = ((dataPtr[0x00] & 0x03) << 6) | ((dataPtr[0x01] & 0x3F) << 0);
			if (newIns < 0x80)
			{
				InstrumentInfo insInf;
				const INS_BANK* insBank;
				UINT8 mapModType;
				
				chnSt->curIns = newIns;
				chnSt->userInsID = 0xFFFF;
				insInf.bank[0] = 0x00;
				insInf.bank[1] = 0x00;
				insInf.ins = newIns;
				insBank = SelectInsMap(_options.dstType, &mapModType);
				chnSt->insSend.bankPtr = GetExactInstrument(insBank, &insInf, mapModType);
				chnSt->userInsName = chnSt->insSend.bankPtr->insName;
			}
			else
			{
				chnSt->curIns = 0xFF;
				chnSt->userInsID = newIns & 0x7F;
				chnSt->insOrg.bankPtr = NULL;
				chnSt->insSend.bankPtr = NULL;
				if (chnSt->userInsID < 0x40)
					chnSt->userInsName = _mt32TimbreNames[chnSt->userInsID].c_str();
				//else
				//	// timbre group R
			}
			chnSt->pbRange = dataPtr[0x04];
			nvChn->_pbRange = chnSt->pbRange;
			if (true)
			{
				vis_printf("SysEx MT-32: Set Ch %u instrument = %u", evtChn, newIns);
			}
			vis_do_ins_change(portChnID);
			break;
		}
		break;
	case 0x040000:	// Timbre Temporary Area
		break;
	case 0x050000:	// Patch Memory
		{
			UINT32 dataLen = syxSize - 0x09;
			const UINT8* data = &syxData[0x07];
			for (; dataLen > 0; addr ++, data ++, dataLen --)
			{
				UINT8 patchID = ((addr & 0x000700) >> 4) | ((addr & 0x000078) >> 3);
				UINT8 patchAddr = addr & 0x00007;
				switch(patchAddr)
				{
				case 0x00:
					_mt32PatchTGrp[patchID] = *data;
					break;
				case 0x01:
					_mt32PatchTNum[patchID] = *data;
					break;
				}
			}
		}
		break;
	case 0x080000:	// Timbre Memory
		{
			UINT8 timID = (addr & 0x007E00) >> 9;
			UINT16 timAddr = addr & 0x0001FF;
			
			dataPtr = &syxData[0x07];
			if (timAddr >= 0x00 && timAddr <= 0x0A)
			{
				size_t copyLen;
				std::string timName = Vector2String(syxData, 0x07, syxSize - 2);
				SanitizeSysExText(timName);
				
				copyLen = _mt32TimbreNames[timID].length() - timAddr;
				copyLen = (timName.length() <= copyLen) ? timName.length() : copyLen;
				memcpy(&_mt32TimbreNames[timID][timAddr], timName.c_str(), copyLen);
			}
		}
		break;
	case 0x100000:	// System Area
		break;
	case 0x200000:	// Display
		if (addr < 0x200100)
		{
			std::string dispMsg = Vector2String(syxData, 0x07, syxSize - 2);
			
			SanitizeSysExText(dispMsg);
			vis_printf("SysEx MT-32: Display = \"%s\"", dispMsg.c_str());
			vis_do_syx_text(FULL_CHN_ID(portID, 0x00), 0x16, dispMsg.length(), dispMsg.data());
		}
		else if (addr == 0x200100)
		{
			vis_addstr("SysEx MT-32: Display Reset");
		}
		break;
	case 0x500000:	// CM-32P Patch Temporary Area
	case 0x510000:	// CM-32P Patch Memory
		{
			UINT32 dataLen = syxSize - 0x09;
			const UINT8* data = &syxData[0x07];
			for (; dataLen > 0; addr ++, data ++, dataLen --)
			{
				UINT16 ofs = ((addr & 0x007F00) >> 7) | ((addr & 0x00007F) >> 0);
				UINT8 patchID = ofs / 0x13;
				UINT8 patchAddr = ofs % 0x13;
				switch(patchAddr)
				{
				case 0x00:
					_cm32pPatchTMedia[patchID] = *data;
					break;
				case 0x01:
					_cm32pPatchTNum[patchID] = *data;
					break;
				}
			}
		}
		break;
	case 0x520000:	// CM-32P System Area
		break;
	case 0x7F0000:	// All Parameters Reset (applies to MT-32/CM-32L *and* CM-32P)
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
	char portChnStr[4];
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
	addr = ReadBE24(&syxData[0x04]);
	switch(addr & 0xFF0000)	// Address High
	{
	case 0x000000:	// System
		if ((addr & 0x00FF00) == 0x000100)
			addr &= ~0x0000FF;	// remove block ID
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
		case 0x000100:	// Channel Message Receive Port
			{
				evtChn = PART_ORDER[syxData[0x06] & 0x0F];
				evtPort = portID;
				if (syxData[0x06] & 0x10)
					evtPort ^= 0x01;	// TODO: verify this
				PrintPortChn(portChnStr, evtPort, evtChn);
				vis_printf("SysEx GS Chn %s: Receive from Port %c", portChnStr, 'A' + syxData[0x07]);
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
			size_t copyLen;
			
			std::string drmName = Vector2String(syxData, 0x07, syxSize - 2);
			SanitizeSysExText(drmName);
			if (syxData[0x06] == 0x00 && drmName.length() > 1)
				vis_printf("SysEx SC-88: Set User Drum Set %u Name = \"%s\"\n", evtChn, drmName.c_str());
			else
				vis_printf("SysEx SC-88: Set User Drum Set %u Name [%X] = \"%s\"\n", evtChn, syxData[0x06], drmName.c_str());
			if (evtChn >= 2)
				break;
			
			copyLen = _sc88UsrDrmNames[evtChn].length() - syxData[0x06];
			copyLen = (drmName.length() <= copyLen) ? drmName.length() : copyLen;
			memcpy(&_sc88UsrDrmNames[evtChn][syxData[0x06]], drmName.c_str(), copyLen);
		}
			break;
		}
		break;
	case 0x400000:	// Patch (port A)
	case 0x500000:	// Patch (port B)
	case 0x600000:	// Patch (port C) (SC-8850 only)
	case 0x700000:	// Patch (port D) (SC-8850 only)
		evtPort = portID;
		if (addr & 0x100000)
			evtPort ^= 0x01;	// TODO: what does the 8850 do here?
		addr &= ~0x300000;	// remove port bits
		if ((addr & 0x00F000) >= 0x001000)
		{
			addr &= ~0x000F00;	// remove channel ID
			evtChn = PART_ORDER[syxData[0x05] & 0x0F];
			portChnID = FULL_CHN_ID(evtPort, evtChn);
			if (portChnID >= _chnStates.size())
				return false;
			PrintPortChn(portChnStr, evtPort, evtChn);
			chnSt = &_chnStates[portChnID];
			nvChn = _noteVis.GetChannel(portChnID);
		}
		switch(addr)
		{
		case 0x400000:	// Master Tune
			{
				INT16 tune;
				// one nibble per byte, range is 0x0018 [-1 semitone] .. 0x0400 [center] .. 0x07E8 [+1 semitone]
				tune =	((syxData[0x07] & 0x0F) << 12) |
						((syxData[0x08] & 0x0F) <<  8) |
						((syxData[0x09] & 0x0F) <<  4) |
						((syxData[0x0A] & 0x0F) <<  0);
				tune -= 0x400;
				if (tune < -0x3E8)
					tune = -0x3E8;
				else if (tune > +0x3E8)
					tune = +0x3E8;
				_noteVis.GetAttributes().detune[0] = tune >> 2;
			}
			break;
		case 0x400004:	// Master Volume
			_mstVol = syxData[0x07];
			vis_printf("SysEx GS: Master Volume = %u", _mstVol);
			if (_filteredVol & (1 << FILTVOL_GMSYX))
				return true;	// don't send when fading
			_noteVis.GetAttributes().volume = _mstVol;
			break;
		case 0x400005:	// Master Key-Shift
			{
				INT8 transp = (INT8)syxData[0x07] - 0x40;
				if (transp < -24)
					transp = -24;
				else if (transp > +24)
					transp = +24;
				_noteVis.GetAttributes().detune[1] = transp << 8;
			}
			break;
		case 0x400006:	// Master Pan
			{
				UINT8 panVal = syxData[0x07];
				if (panVal == 0x00)
					panVal = 0x01;
				_noteVis.GetAttributes().pan = panVal - 0x40;
			}
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
			vis_printf("SysEx SC: ALL Display = \"%s\"", dispMsg.c_str());
			vis_do_syx_text(FULL_CHN_ID(portID, 0x00), 0x42, dispMsg.length(), dispMsg.data());
		}
			break;
		case 0x401000:	// Tone Number
			chnSt->ctrls[0x00] = syxData[0x07];
			chnSt->curIns = syxData[0x08];
			vis_printf("SysEx GS Chn %s: Bank MSB = %02X, Ins = %02X", portChnStr, chnSt->ctrls[0x00], chnSt->curIns);
			{
				MidiEvent insEvt = MidiTrack::CreateEvent_Std(0xC0 | evtChn, chnSt->curIns, 0x00);
				HandleInstrumentEvent(chnSt, &insEvt, 0x11);
				vis_do_ins_change(portChnID);
			}
			break;
		case 0x401002:	// Receive Channel
			if (syxData[0x07] >= 0x10)
				vis_printf("SysEx GS Chn %s: Receive from MIDI channel %s", portChnStr, "--");
			else
				vis_printf("SysEx GS Chn %s: Receive from MIDI channel %02u", portChnStr, 1 + syxData[0x07]);
			break;
		case 0x401015:	// use Rhythm Part (-> drum channel)
			// Part Order: 10 1 2 3 4 5 6 7 8 9 11 12 13 14 15 16
			if (syxData[0x07])
				chnSt->flags |= 0x80;	// drum mode on
			else
				chnSt->flags &= ~0x80;	// drum mode off
			nvChn->_chnMode &= ~0x01;
			nvChn->_chnMode |= (chnSt->flags & 0x80) >> 7;
			if (! syxData[0x07])
				vis_printf("SysEx GS Chn %s: Part Mode: %s", portChnStr, "Normal");
			else
				vis_printf("SysEx GS Chn %s: Part Mode: %s %u", portChnStr, "Drum", syxData[0x07]);
			
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
				vis_do_ins_change(portChnID);
			}
			break;
		case 0x401016:	// Pitch Key Shift
			{
				chnSt->tuneCoarse = (INT8)syxData[0x07] - 0x40;
				if (chnSt->tuneCoarse < -24)
					chnSt->tuneCoarse = -24;
				else if (chnSt->tuneCoarse > +24)
					chnSt->tuneCoarse = +24;
				nvChn->_transpose = chnSt->tuneCoarse;
				nvChn->_attr.detune[1] = (nvChn->_transpose << 8) + (nvChn->_detune << 0);
			}
			break;
		case 0x401017:	// Pitch Offset Fine
			{
				UINT8 offset = ((syxData[0x07] & 0x0F) << 4) | ((syxData[0x08] & 0x0F) << 0);
				chnSt->tuneFine = (offset - 0x80) << 7;
				if (chnSt->tuneFine < -0x3C00)
					chnSt->tuneFine = -0x3C00;
				else if (chnSt->tuneFine > +0x3C00)
					chnSt->tuneFine = +0x3C00;
				nvChn->_detune = (INT8)(chnSt->tuneFine >> 8);
				nvChn->_attr.detune[1] = (nvChn->_transpose << 8) + (nvChn->_detune << 0);
			}
			break;
		case 0x401019:	// Part Level
			chnSt->ctrls[0x07] = syxData[0x07];
			if (_filteredVol & (1 << FILTVOL_CCVOL))
				return true;
			nvChn->_attr.volume = chnSt->ctrls[0x07];
			vis_do_ctrl_change(portChnID, 0x07);
			break;
		case 0x40101C:	// Part Pan
			// 00 [random], 01 [L63] .. 40 [C] .. 7F [R63]
			chnSt->ctrls[0x0A] = syxData[0x07];
			nvChn->_attr.pan = (INT8)chnSt->ctrls[0x0A] - 0x40;
			vis_do_ctrl_change(portChnID, 0x0A);
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
					vis_printf("Warning: SysEx GS Chn %s: CC%u reprogramming to CC#%u might not work!",
								portChnStr, 1 + ccNo, syxData[0x07]);
					break;	// ignore stuff like Modulation
				}
				chnSt->idCC[ccNo] = syxData[0x07];
				if (chnSt->idCC[ccNo] == 0x10 + ccNo)
				{
					chnSt->idCC[ccNo] = 0xFF;
					return true;	// for the defaults, silently drop the message
				}
				
				vis_printf("Warning: SysEx GS Chn %s: Enabling CC reprogramming bug fix! (CC%u assigned to CC#%u)",
							portChnStr, 1 + ccNo, syxData[0x07]);
				return true;
			}
			break;
		case 0x401021:	// Part Reverb Level
			chnSt->ctrls[0x5B] = syxData[0x07];
			break;
		case 0x401022:	// Part Chorus Level
			chnSt->ctrls[0x5D] = syxData[0x07];
			break;
		case 0x40102C:	// Part Delay Level
			chnSt->ctrls[0x5E] = syxData[0x07];
			break;
		case 0x402010:	// Bend Pitch Control
			chnSt->pbRange = (INT8)syxData[0x07] - 0x40;
			if ((INT8)chnSt->pbRange < 0)
				chnSt->pbRange = 0;
			else if (chnSt->pbRange > 24)
				chnSt->pbRange = 24;
			nvChn->_pbRange = chnSt->pbRange;
			break;
		case 0x404000:	// Tone Map Number (== Bank LSB)
			chnSt->ctrls[0x20] = syxData[0x07];
			vis_printf("SysEx GS Chn %s: Bank LSB = %02X", portChnStr, chnSt->ctrls[0x20]);
			break;
		case 0x404001:	// Tone Map 0 Number (setting when Bank LSB == 0)
			vis_printf("SysEx GS Chn %s: Set Default Tone Map to %u", portChnStr, syxData[0x07]);
			chnSt->defInsMap = syxData[0x07] - 0x01;	// 00,01..04 -> FF,00..03
			break;
		}
		break;
	case 0x410000:	// Drum Setup (port A)
	case 0x510000:	// Drum Setup (port B)
		evtPort = portID;
		if (addr & 0x100000)
			evtPort ^= 0x01;
		addr &= ~0x10F0FF;	// remove port bit (bit 20), map ID (bits 12-15) and note number (bits 0-7)
		switch(addr)
		{
		case 0x410000:	// Drum Map Name
		{
			std::string drmName = Vector2String(syxData, 0x07, syxSize - 2);
			if (syxData[0x06] == 0x00 && drmName.length() > 1)
				vis_printf("SysEx SC-88: Set Drum Map Name = \"%s\"\n", drmName.c_str());
			else
				vis_printf("SysEx SC-88: Set Drum Map Name [%X] = \"%s\"\n", syxData[0x06], drmName.c_str());
		}
			break;
		}
		break;
	}
	
	return false;
}

bool MidiPlayer::HandleSysEx_XG(UINT8 portID, size_t syxSize, const UINT8* syxData)
{
	UINT32 addr;
	UINT8 evtPort;
	UINT8 evtChn;
	UINT16 portChnID;
	char portChnStr[4];
	ChannelState* chnSt = NULL;
	NoteVisualization::ChnInfo* nvChn = NULL;
	
	addr = ReadBE24(&syxData[0x03]);
	switch(addr & 0xFF0000)	// Address High
	{
	case 0x000000:	// System
		switch(addr)
		{
		case 0x000000:	// Master Tune
			{
				INT16 tune;
				// one nibble per byte, range is 0x0018 [-1 semitone] .. 0x0400 [center] .. 0x07E8 [+1 semitone]
				tune =	((syxData[0x06] & 0x0F) << 12) |
						((syxData[0x07] & 0x0F) <<  8) |
						((syxData[0x08] & 0x0F) <<  4) |
						((syxData[0x09] & 0x0F) <<  0);
				tune -= 0x400;
				if (tune < -0x400)
					tune = -0x400;
				else if (tune > +0x3FF)
					tune = +0x3FF;
				_noteVis.GetAttributes().detune[0] = tune >> 2;
			}
			break;
		case 0x000004:	// Master Volume
			_mstVol = syxData[0x06];
			vis_printf("SysEx XG: Master Volume = %u", _mstVol);
			if (_filteredVol & (1 << FILTVOL_GMSYX))
				return true;	// don't send when fading
			_noteVis.GetAttributes().volume = _mstVol;
			break;
		case 0x000005:	// Master Attenuator
			vis_printf("SysEx XG: Master Attenuator = %u", syxData[0x06]);
			_noteVis.GetAttributes().expression = 0x7F - syxData[0x06];
			break;
		case 0x000006:	// Master Transpose
			{
				INT8 transp = (INT8)syxData[0x06] - 0x40;
				if (transp < -24)
					transp = -24;
				else if (transp > +24)
					transp = +24;
				_noteVis.GetAttributes().detune[1] = transp << 8;
			}
			break;
		case 0x00007D:	// Drum Setup Reset
			vis_printf("SysEx XG: Drum %u Reset", syxData[0x06]);
			break;
		case 0x00007E:	// XG System On
			// XG Reset: F0 43 10 4C 00 00 7E 00 F7
			InitializeChannels();
			vis_addstr("SysEx: XG Reset");
			if ((_options.flags & PLROPTS_RESET) && MMASK_TYPE(_options.dstType) != MODULE_TYPE_XG)
				return true;	// prevent XG reset on other devices
			break;
		case 0x00007F:	// All Parameters Reset
			vis_addstr("SysEx XG: All Parameters Reset");
			_defSrcInsMap = 0xFF;	// Yes, this one is reset with this SysEx message.
			RefreshSrcDevSettings();
			InitializeChannels();
			if (_options.flags & PLROPTS_STRICT)
				return true;	// ignore for now, TODO: send, then ensure proper Voice Map
			break;
		}
		break;
	case 0x020000:	// Effect 1
		break;
	case 0x030000:	// Effect 2
		break;
	case 0x060000:	// ASCII Display
	{
		std::string dispMsg = Vector2String(syxData, 0x06, syxSize - 1);
		
		SanitizeSysExText(dispMsg);
		vis_printf("SysEx MU: Display = \"%s\"", dispMsg.c_str());
		vis_do_syx_text(FULL_CHN_ID(portID, 0x00), 0x43, dispMsg.length(), dispMsg.data());
	}
		break;
	case 0x070000:	// Display Bitmap
		vis_addstr("SysEx MU: Display Bitmap");
		if ((addr & 0x00FFFF) >= 0x0030)
			break;	// anything beyond offset 0030 was confirmed to be completely ignored (i.e. image is not drawn)
	{
		UINT8 startOfs;
		UINT8 dataLen;
		
		// Partial writes to anywhere in the display memory sets those bits and redraws the image.
		startOfs = addr & 0xFF;
		dataLen = syxSize - 0x07;	// 0x06 (start ofs) + 1 (F7 terminator)
		if (startOfs + dataLen > 0x30)
			dataLen = 0x30 - startOfs;
		memcpy(&_pixelPageMem[0][startOfs], &syxData[0x06], dataLen);
		
		vis_do_syx_bitmap(FULL_CHN_ID(portID, 0x00), 0x43, 0x30, _pixelPageMem[0]);
	}
		break;
	case 0x080000:	// Multi Part
	case 0x0A0000:	// Multi Part (additional)
		addr &= ~0x00FF00;	// remove part ID
		evtChn = syxData[0x04] & 0x0F;
		evtPort = (syxData[0x04] & 0x70) >> 4;
		// TODO: check what the actual hardware does when receiving the message on Port B
		portChnID = FULL_CHN_ID(evtPort, evtChn);
		if (portChnID >= _chnStates.size())
			return false;
		PrintPortChn(portChnStr, evtPort, evtChn);
		chnSt = &_chnStates[portChnID];
		nvChn = _noteVis.GetChannel(portChnID);
		switch(addr)
		{
		case 0x080001:	// Bank MSB
			chnSt->ctrls[0x00] = syxData[0x06];
			vis_printf("SysEx XG Chn %s: Bank MSB = %02X", portChnStr, chnSt->ctrls[0x00]);
			break;
		case 0x080002:	// Bank LSB
			chnSt->ctrls[0x20] = syxData[0x06];
			vis_printf("SysEx XG Chn %s: Bank LSB = %02X", portChnStr, chnSt->ctrls[0x20]);
			break;
		case 0x080003:	// Program Number
			chnSt->curIns = syxData[0x06];
			vis_printf("SysEx XG Chn %s: Ins = %02X", portChnStr, chnSt->curIns);
			{
				MidiEvent insEvt = MidiTrack::CreateEvent_Std(0xC0 | evtChn, chnSt->curIns, 0x00);
				HandleInstrumentEvent(chnSt, &insEvt, 0x11);
				vis_do_ins_change(portChnID);
			}
			break;
		case 0x080004:	// Receive Channel
			{
				char recvPCStr[4];
				if (syxData[0x06] == 0x7F)
					PrintPortChn(recvPCStr, 0xFF, 0xFF);
				else
					PrintPortChn(recvPCStr, syxData[0x06] >> 4, syxData[0x06] & 0x0F);
				vis_printf("SysEx XG Chn %s: Receive from MIDI channel %s", portChnStr, recvPCStr);
			}
			break;
		case 0x080007:	// Part Mode
			// Part Order: 10 1 2 3 4 5 6 7 8 9 11 12 13 14 15 16
			if (syxData[0x06])
				chnSt->flags |= 0x80;	// drum mode on
			else
				chnSt->flags &= ~0x80;	// drum mode off
			nvChn->_chnMode &= ~0x01;
			nvChn->_chnMode |= (chnSt->flags & 0x80) >> 7;
			if (syxData[0x06] == 0x00)
				vis_printf("SysEx XG Chn %s: Part Mode: %s", portChnStr, "Normal");
			else if (syxData[0x06] == 0x01)
				vis_printf("SysEx XG Chn %s: Part Mode: %s (%s)", portChnStr, "Drum", "Auto");
			else
				vis_printf("SysEx XG Chn %s: Part Mode: %s %u", portChnStr, "Drum", syxData[0x06] - 0x01);
			break;
		case 0x080008:	// Note Shift
			{
				chnSt->tuneCoarse = (INT8)syxData[0x06] - 0x40;
				if (chnSt->tuneCoarse < -24)
					chnSt->tuneCoarse = -24;
				else if (chnSt->tuneCoarse > +24)
					chnSt->tuneCoarse = +24;
				nvChn->_transpose = chnSt->tuneCoarse;
				nvChn->_attr.detune[1] = (nvChn->_transpose << 8) + (nvChn->_detune << 0);
			}
			break;
		case 0x080017:	// Detune
			{
				UINT8 offset = ((syxData[0x06] & 0x0F) << 4) | ((syxData[0x07] & 0x0F) << 0);
				chnSt->tuneFine = (offset - 0x80) << 7;
				nvChn->_detune = (INT8)(chnSt->tuneFine >> 8);
				nvChn->_attr.detune[1] = (nvChn->_transpose << 8) + (nvChn->_detune << 0);
			}
			break;
		case 0x08000B:	// Volume
			chnSt->ctrls[0x07] = syxData[0x06];
			if (_filteredVol & (1 << FILTVOL_CCVOL))
				return true;
			nvChn->_attr.volume = chnSt->ctrls[0x07];
			vis_do_ctrl_change(portChnID, 0x07);
			break;
		case 0x08000E:	// Pan
			// 00 [random], 01 [L63] .. 40 [C] .. 7F [R63]
			chnSt->ctrls[0x0A] = syxData[0x06];
			nvChn->_attr.pan = (INT8)chnSt->ctrls[0x0A] - 0x40;
			vis_do_ctrl_change(portChnID, 0x0A);
			break;
		case 0x080011:	// Dry Level
			break;
		case 0x080012:	// Chorus Send
			chnSt->ctrls[0x5D] = syxData[0x06];
			break;
		case 0x080013:	// Reverb Send
			chnSt->ctrls[0x5B] = syxData[0x06];
			break;
		case 0x080014:	// Variation Send
			chnSt->ctrls[0x5E] = syxData[0x06];
			break;
		case 0x080023:	// Pitch Bend Control
			chnSt->pbRange = (INT8)syxData[0x06] - 0x40;
			if ((INT8)chnSt->pbRange < 0)
				chnSt->pbRange = 0;
			else if (chnSt->pbRange > 24)
				chnSt->pbRange = 24;
			nvChn->_pbRange = chnSt->pbRange;
			break;
		case 0x080035:	// Receive Note Message
			vis_printf("SysEx XG Chn %s: Receive Notes: %s", portChnStr,
				syxData[0x06] ? "Yes" : "No (muted)");
			break;
		case 0x080067:	// Portamento Switch
			chnSt->ctrls[0x41] = syxData[0x06] ? 0x00 : 0x40;
			break;
		case 0x080068:	// Portamento Time
			chnSt->ctrls[0x05] = syxData[0x06];
			break;
		}
		break;
	case 0x100000:	// A/D Part
		break;
	case 0x110000:	// A/D System
		break;
	case 0x300000:	// Drum Setup 1
	case 0x310000:	// Drum Setup 2
	case 0x320000:	// Drum Setup 3
	case 0x330000:	// Drum Setup 4
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
	
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		
		if (chnSt.curIns == 0xFF)
			continue;
		
		if ((chnSt.flags & 0x80) && chnSt.hadDrumNRPN)
		{
			char portChnStr[4];
			PrintPortChn(portChnStr, curChn >> 4, curChn & 0x0F);
			vis_printf("Warning: Channel %s: Drum NRPNs reset due to instrument refresh!", portChnStr);
		}
		MidiEvent insEvt = MidiTrack::CreateEvent_Std(0xC0 | chnSt.midChn, chnSt.curIns, 0x00);
		HandleInstrumentEvent(&chnSt, &insEvt, 0x10);
		vis_do_ins_change(curChn);
	}
	
	return;
}

void MidiPlayer::FadeVolRefresh(void)
{
	if (_fadeVolMode == FDVMODE_GMSYX)
	{
		UINT8 newVol = (_mstVol * _fadeVol + 0x80) / 0x100;
		if (newVol == _mstVolFade)
			return;
		_mstVolFade = newVol;
		
		std::vector<UINT8> syxData(sizeof(GM_MST_VOL));
		size_t curPort;
		
		memcpy(&syxData[0], GM_MST_VOL, syxData.size());
		syxData[0x05] = 0x00;			// master volume LSB
		syxData[0x06] = _mstVolFade;	// master volume MSB
		for (curPort = 0; curPort < _outPorts.size(); curPort ++)
			MidiOutPort_SendLongMsg(_outPorts[curPort], syxData.size(), &syxData[0]);
		
		_noteVis.GetAttributes().volume = _mstVolFade;
		//vis_printf("Master Volume (fade): %u", _mstVolFade);
	}
	else
	{
		size_t curChn;
		UINT8 ctrlID;
		UINT8 val;
		
		// Expression or Main Volume controller
		ctrlID = (_fadeVolMode == FDVMODE_CCEXPR) ? 0x0B : 0x07;
		
		// send volume controllers to all channels
		for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
		{
			ChannelState& chnSt = _chnStates[curChn];
			MIDIOUT_PORT* outPort = _outPorts[chnSt.portID];
			NoteVisualization::ChnInfo* nvChn = _noteVis.GetChannel(curChn);
			
			val = (UINT8)((chnSt.ctrls[ctrlID] * _fadeVol + 0x80) / 0x100);
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, ctrlID, val);
			
			if (_fadeVolMode == FDVMODE_CCVOL)
				nvChn->_attr.volume = val;
			else if (_fadeVolMode == FDVMODE_CCEXPR)
				nvChn->_attr.expression = val;
			vis_do_ctrl_change(curChn, ctrlID);
		}
	}
	
	return;
}

void MidiPlayer::AllChannelRefresh(void)
{
	size_t curChn;
	UINT8 curCtrl;
	std::list<NoteInfo>::iterator ntIt;
	UINT8 defDstPbRange;
	
	if (_options.dstType == MODULE_MT32)
		defDstPbRange = 12;
	else
		defDstPbRange = 2;
	InitializeChannels_Post();
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		MIDIOUT_PORT* outPort = _outPorts[chnSt.portID];
		MidiEvent tempEvt;
		
		if (chnSt.curIns != 0xFF)
		{
			chnSt.insState[0] = chnSt.insState[1] = chnSt.insState[2] = 0xFF;	// enforce resending
			tempEvt = MidiTrack::CreateEvent_Std(0xC0 | chnSt.midChn, chnSt.curIns, 0x00);
			HandleInstrumentEvent(&chnSt, &tempEvt, 0x10);
			vis_do_ins_change(curChn);
		}
		// Main Volume (7) and Pan (10) may be patched
		tempEvt = MidiTrack::CreateEvent_Std(0xB0 | chnSt.midChn, 0x07, chnSt.ctrls[0x07]);
		if (! HandleControlEvent(&chnSt, NULL, &tempEvt))
			MidiOutPort_SendShortMsg(outPort, tempEvt.evtType, tempEvt.evtValA, tempEvt.evtValB);
		tempEvt = MidiTrack::CreateEvent_Std(0xB0 | chnSt.midChn, 0x0A, chnSt.ctrls[0x0A]);
		if (! HandleControlEvent(&chnSt, NULL, &tempEvt))
			MidiOutPort_SendShortMsg(outPort, tempEvt.evtType, tempEvt.evtValA, tempEvt.evtValB);
		tempEvt = MidiTrack::CreateEvent_Std(0xB0 | chnSt.midChn, 0x0B, chnSt.ctrls[0x0B]);
		if (! HandleControlEvent(&chnSt, NULL, &tempEvt))
			MidiOutPort_SendShortMsg(outPort, tempEvt.evtType, tempEvt.evtValA, tempEvt.evtValB);
		
		// We're sending MSB + LSB here. (A few controllers that are handled separately are skipped.)
		for (curCtrl = 0x01; curCtrl < 0x20; curCtrl ++)
		{
			if (curCtrl == 0x06 || curCtrl == 0x07 || curCtrl == 0x0A || curCtrl == 0x0B)
				continue;
			// TODO: use 0xFF as default value - 0x00 is unsafe
			if (chnSt.ctrls[0x00 | curCtrl] != 0x00)
				MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x00 | curCtrl, chnSt.ctrls[0x00 | curCtrl]);
			if (chnSt.ctrls[0x20 | curCtrl] != 0x00)
				MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x20 | curCtrl, chnSt.ctrls[0x20 | curCtrl]);
		}
		for (curCtrl = 0x40; curCtrl < 0x60; curCtrl ++)
		{
			if (chnSt.ctrls[curCtrl] != 0x00)
				MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, curCtrl, chnSt.ctrls[curCtrl]);
		}
		// Channel Mode messages are left out
		
		// restore RPNs
		if (chnSt.pbRange != defDstPbRange)
		{
			// set Pitch Bend Range
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x65, 0x00);
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x64, 0x00);
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x06, chnSt.pbRange);
		}
		if (chnSt.tuneCoarse != 0)
		{
			// set Coarse Tuning
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x65, 0x00);
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x64, 0x02);
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x06, 0x40 + chnSt.tuneCoarse);
		}
		if (chnSt.tuneFine != 0)
		{
			// set Fine Tuning
			UINT8 valM, valL;
			
			valM = 0x40 + (chnSt.tuneFine >> 8);
			valL = (chnSt.tuneFine >> 1) & 0x7F;
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x65, 0x00);
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x64, 0x01);
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x06, valM);
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x26, valL);
		}
		// restore RPN state
		if (chnSt.rpnCtrl[0x00] & 0x80)
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x63, chnSt.rpnCtrl[0x00] & 0x7F);
		else
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x65, chnSt.rpnCtrl[0x00]);
		if (chnSt.rpnCtrl[0x01] & 0x80)
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x64, chnSt.rpnCtrl[0x01] & 0x7F);
		else
			MidiOutPort_SendShortMsg(outPort, 0xB0 | chnSt.midChn, 0x62, chnSt.rpnCtrl[0x01]);
		
		// TODO: restore Pitch Bend and NRPNs
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
				tc.tempo = ReadBE24(&evtIt->evtData[0x00]);
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

void MidiPlayer::RefreshSrcDevSettings(void)
{
	if (_defSrcInsMap >= 0x80)
	{
		if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS)
		{
			if (MMASK_MOD(_options.srcType) < MT_UNKNOWN)
				_defSrcInsMap = MMASK_MOD(_options.srcType);	// instrument map, depending on model
			else
				_defSrcInsMap = 0x00;
		}
		else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_XG)
		{
			// XG has only 2 instrument maps: "MU Basic" and "MU100 Native"
			_defSrcInsMap = (MMASK_MOD(_options.srcType) >= MTXG_MU100) ? 0x01 : 0x00;
		}
		else
		{
			_defSrcInsMap = 0x00;
		}
		_defSrcInsMap |= 0x80;
	}
	if (_options.srcType == MODULE_MT32)
		_defPbRange = 12;
	else
		_defPbRange = 2;
	
	return;
}

void MidiPlayer::InitializeChannels(void)
{
	size_t curChn;
	size_t curIns;
	
	_mstVol = 0x7F;
	_mstVolFade = (_mstVol * _fadeVol + 0x80) / 0x100;
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		chnSt.midChn = curChn & 0x0F;
		chnSt.portID = curChn >> 4;
		chnSt.flags = 0x00;
		chnSt.defInsMap = 0xFF;
		chnSt.insOrg.bank[0] = chnSt.insOrg.bank[1] = chnSt.insOrg.ins = 0x00;
		chnSt.insOrg.bankPtr = NULL;
		chnSt.insSend.bank[0] = chnSt.insSend.bank[1] = chnSt.insSend.ins = 0xFF;	// initialize to "not set"
		chnSt.insSend.bankPtr = NULL;
		chnSt.insState[0] = chnSt.insState[1] = chnSt.insState[2] = 0xFF;
		chnSt.curIns = 0xFF;
		chnSt.userInsID = 0xFFFF;
		chnSt.userInsName = NULL;
		memset(&chnSt.ctrls[0], 0x00, 0x80);
		chnSt.ctrls[0x07] = 100;
		chnSt.ctrls[0x0A] = 0x40;
		chnSt.ctrls[0x0B] = 127;
		chnSt.ctrls[0x5B] = 40;
		chnSt.idCC[0] = chnSt.idCC[1] = 0xFF;
		
		chnSt.rpnCtrl[0] = chnSt.rpnCtrl[1] = 0x7F;
		chnSt.hadDrumNRPN = false;
		chnSt.pbRange = _defPbRange;
		chnSt.tuneCoarse = 0;
		chnSt.tuneFine = 0;
		
		chnSt.notes.clear();
		
		vis_do_channel_event(curChn, 0x00, 0x00);
	}
	for (curChn = 0x00; curChn < _chnStates.size(); curChn += 0x10)
	{
		ChannelState& drumChn = _chnStates[curChn | 0x09];
		drumChn.flags |= 0x80;	// set drum channel mode
		if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_XG)
			drumChn.ctrls[0x00] = 0x7F;
	}
	
	memset(_pixelPageMem, 0x00, 0x40 * 10);
	for (curIns = 0; curIns < 2; curIns ++)
	{
		_sc88UsrDrmNames[curIns] = std::string(0x0C, ' ');
		_sc88UsrDrmNames[curIns][0] = '\0';
	}
	for (curIns = 0x00; curIns < 0x80; curIns ++)
	{
		_mt32PatchTGrp[curIns] = (curIns >> 6) & 0x03;
		_mt32PatchTNum[curIns] = (curIns >> 0) & 0x3F;
		_cm32pPatchTMedia[curIns] = 0x00;
		_cm32pPatchTNum[curIns] = curIns;
	}
	for (curIns = 0x00; curIns < 0x40; curIns ++)
	{
		_mt32TimbreNames[curIns] = std::string(0x0A, ' ');
		_mt32TimbreNames[curIns][0] = '\0';
	}
	
	_noteVis.Reset();
	_noteVis.GetAttributes().volume = _mstVolFade;
	for (curChn = 0x00; curChn < _chnStates.size(); curChn ++)
	{
		ChannelState& chnSt = _chnStates[curChn];
		NoteVisualization::ChnInfo* nvChn = _noteVis.GetChannel(curChn);
		nvChn->_chnMode |= (chnSt.flags & 0x80) >> 7;
		nvChn->_attr.volume = chnSt.ctrls[0x07];
		nvChn->_attr.pan = (INT8)chnSt.ctrls[0x0A] - 0x40;
		nvChn->_attr.expression = chnSt.ctrls[0x0B];
		nvChn->_pbRange = chnSt.pbRange;
	}
	_initChnPost = true;
	
	return;
}

void MidiPlayer::InitializeChannels_Post(void)
{
	size_t curChn;
	UINT8 defDstPbRange;
	
	_initChnPost = false;
	
	if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
	{
		if (MMASK_MOD(_options.dstType) < MT_UNKNOWN)
			_defDstInsMap = MMASK_MOD(_options.dstType);
		else
			_defDstInsMap = 0x00;
		if (_defDstInsMap == MTGS_SC8850)
			_defDstInsMap = MTGS_SC88PRO;	// TODO: make configurable
	}
	else if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_XG)
	{
		_defDstInsMap = (MMASK_MOD(_options.dstType) >= MTXG_MU100) ? 0x01 : 0x00;
	}
	else
	{
		_defDstInsMap = 0x00;
	}
	if (_options.dstType == MODULE_MT32)
		defDstPbRange = 12;
	else
		defDstPbRange = 2;
	
	for (curChn = 0x00; curChn < _chnStates.size(); curChn += 0x10)
	{
		ChannelState& drumChn = _chnStates[curChn | 0x09];
		MIDIOUT_PORT* outPort = _outPorts[drumChn.portID];
		
		if (_options.flags & PLROPTS_STRICT)
		{
			if (MMASK_TYPE(_options.dstType) == MODULE_TYPE_GS)
			{
				drumChn.insState[0] = 0x00;	// Bank MSB 0
				if (_options.srcType == MODULE_TG300B)
				{
					drumChn.insState[1] = 0x01 + MTGS_SC55;
				}
				else if (MMASK_TYPE(_options.srcType) == MODULE_TYPE_GS && MMASK_MOD(_options.srcType) < MT_UNKNOWN)
				{
					if (MMASK_MOD(_options.srcType) <= MMASK_MOD(_options.dstType))
						drumChn.insState[1] = 0x01 + MMASK_MOD(_options.srcType);
					else
						drumChn.insState[1] = 0x01 + MMASK_MOD(_options.dstType);
				}
				else
				{
					drumChn.insState[1] = 0x00;
				}
				drumChn.insState[2] = 0x00;	// Instrument: Standard Kit 1
				
				if (_options.srcType == MODULE_MT32)
				{
					drumChn.insState[1] = 0x01;	// SC-55 map
					drumChn.insState[2] = 0x7F;	// select MT-32 drum kit
				}
				
				if (_options.dstType == MODULE_SC55 || _options.dstType == MODULE_TG300B)
					drumChn.insState[1] = 0x00;
				else if (drumChn.insState[1] == 0x00)
					drumChn.insState[1] = 0x01 + _defDstInsMap;
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
			if (chnSt.pbRange != defDstPbRange)
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
