#include <list>
#include <vector>
#include <algorithm>

#include <stdtype.h>
#include "NoteVis.hpp"


static const UINT8 DRUM_GROUP[0x80] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	// 0x00-0x0F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 0,	// 0x10-0x1F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,	// 0x20-0x2F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	// 0x30-0x3F
	0, 0, 0, 0, 0, 0, 0, 2, 2, 3, 3, 0, 0, 0, 4, 4,	// 0x40-0x4F
	5, 5, 0, 0, 0, 0, 6, 6, 0, 0, 0, 0, 0, 0, 0, 0,	// 0x50-0x5F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	// 0x60-0x6F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	// 0x70-0x7F
};

static const UINT32 DRUM_AGE[0x80] =
{
	150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150,	// 0x00-0x0F
	150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150,	// 0x10-0x1F
	150, 150, 150, 150, 150, 150, 150, 150, 150, 150,  80, 150,  80, 150, 300, 150,	// 0x20-0x2F
	150, 600, 150, 300, 150, 300, 150, 600, 150, 600, 150, 150, 150, 150, 150, 150,	// 0x30-0x3F
	150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150,	// 0x40-0x4F
	150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150,	// 0x50-0x5F
	150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150,	// 0x60-0x6F
	150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150,	// 0x70-0x7F
};

NoteVisualization::NoteVisualization()
{
}

NoteVisualization::~NoteVisualization()
{
}

void NoteVisualization::Initialize(UINT8 chnGroups)
{
	size_t curChn;
	
	_modAttrs.volume = 0x7F;
	_modAttrs.expression = 0x7F;
	_modAttrs.pan = 0x00;
	_modAttrs.detune[0] = 0;
	_modAttrs.detune[1] = 0;
	
	_chnList.clear();
	_chnList.resize(chnGroups * 0x10);
	for (curChn = 0; curChn < _chnList.size(); curChn  ++)
	{
		auto& chn = _chnList[curChn];
		chn.Initialize();
		if ((curChn & 0x0F) == 0x09)
			chn._chnMode = 0x01;	// drum channel
	}
	
	return;
}

void NoteVisualization::Reset(void)
{
	Initialize(_chnList.size() / 0x10);
	return;
}

NoteVisualization::ChnInfo* NoteVisualization::GetChannel(UINT16 chn)
{
	return &_chnList[chn];
}

const NoteVisualization::MidiModifiers& NoteVisualization::GetAttributes(void) const
{
	return _modAttrs;
}

NoteVisualization::MidiModifiers& NoteVisualization::GetAttributes(void)
{
	return _modAttrs;
}

void NoteVisualization::AdvanceAge(UINT32 time)
{
	size_t curChn;
	
	for (curChn = 0; curChn < _chnList.size(); curChn  ++)
		_chnList[curChn].AdvanceAge(time);
	
	return;
}


void NoteVisualization::ChnInfo::Initialize(void)
{
	_chnMode = 0x00;
	_attr.volume = 100;
	_attr.expression = 0x7F;
	_attr.pan = 0x00;
	_attr.detune[0] = 0;
	_attr.detune[1] = 0;
	
	_pbRange = 2;
	_transpose = 0;
	_detune = 0;
	
	_notes.clear();
	
	return;
}

NoteVisualization::NoteInfo* NoteVisualization::ChnInfo::AddNote(UINT8 note, UINT8 vel)
{
	NoteInfo nInf;
	note &= 0x7F;
	nInf.height = note;
	nInf.velocity = vel;
	nInf.curAge = 0;
	nInf.maxAge = 0;
	if (_chnMode & 0x01)
	{
		DrumNotePrepare(note);
		nInf.maxAge = DRUM_AGE[note];	// set age for drum notes
	}
	_notes.push_back(nInf);
	
	// make sure the list doesn't grow endlessly with buggy MIDIs
	if (_notes.size() >= 0x80)
	{
		std::list<NoteInfo>::iterator ntIt;
		ntIt = _notes.begin();
		std::advance(ntIt, 0x80 - 0x20);
		_notes.erase(_notes.begin(), ntIt);
	}
	
	return &_notes.back();
}

static bool NoteAgeMarked(NoteVisualization::NoteInfo& note)
{
	return note.curAge == (UINT32)-1;
}

void NoteVisualization::ChnInfo::DrumNotePrepare(UINT8 note)
{
	std::list<NoteInfo>::iterator nIt;
	UINT8 drmGroup;
	
	note &= 0x7F;
	drmGroup = DRUM_GROUP[note];
	if (drmGroup == 0)
	{
		for (nIt = _notes.begin(); nIt != _notes.end(); ++nIt)
		{
			if (nIt->height == note)
				nIt->curAge = (UINT32)-1;
		}
	}
	else
	{
		for (nIt = _notes.begin(); nIt != _notes.end(); ++nIt)
		{
			if (DRUM_GROUP[nIt->height] == drmGroup)
				nIt->curAge = (UINT32)-1;
		}
	}
	_notes.erase(std::remove_if(_notes.begin(), _notes.end(), &NoteAgeMarked), _notes.end());
	
	return;
}

void NoteVisualization::ChnInfo::RemoveNote(UINT8 note)
{
	std::list<NoteInfo>::iterator nIt;
	
	for (nIt = _notes.begin(); nIt != _notes.end(); ++nIt)
	{
		if (nIt->height == note && ! nIt->maxAge)
		{
			_notes.erase(nIt);	// remove first match
			return;
		}
	}
	
	return;
}

void NoteVisualization::ChnInfo::ClearNotes(void)
{
	_notes.clear();
	return;
}

#define NVM_NONE	0	// always output maximum volume
#define NVM_VEL		1	// take only note velocity into account
#define NVM_VELVOL	2	// use velocity, channel volume
#define NVM_CHN		3	// use velocity, channel volume, channel expression
#define NVM_ALL		4	// use velocity, channel volume, expression, master volume
#define NOTEVOL_MODE	NVM_VEL

std::list<NoteVisualization::NoteInfo> NoteVisualization::ChnInfo::GetProcessedNoteList(const NoteVisualization::MidiModifiers& moduleAttr) const
{
	std::list<NoteInfo> result;
	std::list<NoteInfo>::const_iterator nIt;
	INT32 notePitch;
	UINT32 noteVol;
	
	for (nIt = _notes.begin(); nIt != _notes.end(); ++nIt)
	{
		if (_chnMode & 0x01)
		{
			// no pitch correction on drum channels
			notePitch = nIt->height;
		}
		else
		{
			notePitch = nIt->height << 8;
			notePitch += _attr.detune[0] + _attr.detune[1];
			notePitch += moduleAttr.detune[0] + moduleAttr.detune[1];
			notePitch = (notePitch + 0x80) >> 8;
			if (notePitch < 0x00)
				notePitch = 0x00;
			else if (notePitch > 0x7F)
				notePitch = 0x7F;
		}
		
		switch(NOTEVOL_MODE)
		{
		case NVM_NONE:
		default:
			noteVol = 0x7F;
			break;
		case NVM_VEL:
			noteVol = nIt->velocity;
			break;
		case NVM_VELVOL:
			noteVol = nIt->velocity * _attr.volume;
			noteVol = (noteVol + 0x3F) / 0x7F;
			break;
		case NVM_CHN:
			noteVol = nIt->velocity * _attr.volume * _attr.expression;
			noteVol = (noteVol + 0x1F81) / 0x3F01;
			break;
		case NVM_ALL:
			noteVol = nIt->velocity * _attr.volume * _attr.expression;
			noteVol = (noteVol * moduleAttr.expression / 0x7F) * moduleAttr.volume;
			noteVol = (noteVol + 0x0FA0BF) / 0x1F417F;
			break;
		}
		
		NoteInfo note;
		note.height = (UINT8)notePitch;
		note.velocity = (UINT8)noteVol;
		result.push_back(note);
	}
	
	return result;
}

const std::list<NoteVisualization::NoteInfo>& NoteVisualization::ChnInfo::GetNoteList(void) const
{
	return _notes;
}

static bool NoteAgeExpired(NoteVisualization::NoteInfo& note)
{
	return note.maxAge && note.curAge >= note.maxAge;
}

void NoteVisualization::ChnInfo::AdvanceAge(UINT32 time)
{
	std::list<NoteInfo>::iterator nIt;
	
	for (nIt = _notes.begin(); nIt != _notes.end(); ++nIt)
		nIt->curAge += time;
	_notes.erase(std::remove_if(_notes.begin(), _notes.end(), &NoteAgeExpired), _notes.end());
	
	return;
}
