#ifndef __NOTE_VIS_HPP__
#define __NOTE_VIS_HPP__

#include <stdtype.h>
#include <vector>
#include <list>

class NoteVisualization
{
public:
	struct MidiModifiers
	{
		UINT8 volume;
		UINT8 expression;
		INT8 pan;	// -0x40 .. 0x00 .. +0x3F
		INT16 detune[2];	// [0] - pitch bend, [1] - RPN tuning
	};
	struct NoteInfo
	{
		UINT8 height;
		UINT8 velocity;
		UINT32 curAge;	// current age (milliseconds)
		UINT32 maxAge;	// maximum age (enforces automatic note removal)
	};
	struct ChnInfo
	{
		// --- general settings ---
		UINT8 _chnMode;	// 00 - normal, 01 - drum
		
		// --- settings via MIDI controllers ---
		MidiModifiers _attr;	// attributes
		// RPN parameter cache (required for setting attributes properly)
		UINT8 _pbRange;
		INT8 _transpose;	// RPN coarse tuning (-0x40 .. 0x00 .. +0x3F)
		INT8 _detune;		// RPN fine tuning (-0x40 .. 0x00 .. +0x3F)
		
		void Initialize(void);
		NoteInfo* AddNote(UINT8 note, UINT8 vel);
		void RemoveNote(UINT8 note);
		void ClearNotes(void);
		void DrumNotePrepare(UINT8 note);
		const std::list<NoteInfo>& GetNoteList(void) const;
		std::list<NoteInfo> GetProcessedNoteList(const MidiModifiers& moduleAttr) const;
		void AdvanceAge(UINT32 time);
	private:
		std::list<NoteInfo> _notes;
	};
	
	NoteVisualization();
	~NoteVisualization();
	
	void Initialize(UINT8 chnGroups);	// 1 channel group = 16 channels
	void Reset(void);
	UINT8 GetChnGroupCount(void) const;
	const ChnInfo* GetChannel(UINT16 chn) const;
	ChnInfo* GetChannel(UINT16 chn);
	const MidiModifiers& GetAttributes(void) const;
	MidiModifiers& GetAttributes(void);
	void AdvanceAge(UINT32 time);
	
private:
	MidiModifiers _modAttrs;
	std::vector<ChnInfo> _chnList;
};

#endif	// __NOTE_VIS_HPP__
