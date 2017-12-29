// WinMM MIDI Output
// -----------------
#include <stdlib.h>

#include <Windows.h>

#include <stdtype.h>
#include "MidiOut.h"

//typedef struct _midiout_port MIDIOUT_PORT;
struct _midiout_port
{
	HMIDIOUT hMidiOut;
};

MIDIOUT_PORT* MidiOutPort_Init(void)
{
	MIDIOUT_PORT* mop;
	
	mop = (MIDIOUT_PORT*)calloc(1, sizeof(MIDIOUT_PORT));
	if (mop == NULL)
		return NULL;
	
	mop->hMidiOut = NULL;
	
	return mop;
}

void MidiOutPort_Deinit(MIDIOUT_PORT* mop)
{
	if (mop->hMidiOut != NULL)
		MidiOutPort_CloseDevice(mop);
	
	free(mop);
	
	return;
}

UINT8 MidiOutPort_OpenDevice(MIDIOUT_PORT* mop, UINT32 id)
{
	MMRESULT retMM;
	
	if (mop->hMidiOut != NULL)
		return 0xFE;
	
	retMM = midiOutOpen(&mop->hMidiOut, id, (DWORD_PTR)NULL, (DWORD_PTR)NULL, CALLBACK_NULL);
	if (retMM != MMSYSERR_NOERROR)
		return 0xFF;	// open error
	return 0x00;
}

UINT8 MidiOutPort_CloseDevice(MIDIOUT_PORT* mop)
{
	MMRESULT retMM;
	
	if (mop->hMidiOut == NULL)
		return 0xFE;
	
	retMM = midiOutClose(mop->hMidiOut);
	if (retMM != MMSYSERR_NOERROR)
		return 0xFF;	// close error
	
	mop->hMidiOut = NULL;
	return 0x00;
}

void MidiOutPort_SendShortMsg(MIDIOUT_PORT* mop, UINT8 event, UINT8 data1, UINT8 data2)
{
	DWORD midiMsg;
	
	if (mop->hMidiOut == NULL)
		return;
	
	midiMsg = (event << 0) | (data1 << 8) | (data2 << 16);
	midiOutShortMsg(mop->hMidiOut, midiMsg);
	
	return;
}

UINT8 MidiOutPort_SendLongMsg(MIDIOUT_PORT* mop, size_t dataLen, const void* data)
{
	MIDIHDR mHdr;
	MMRESULT retMM;
	
	if (mop->hMidiOut == NULL)
		return 0xFE;
	
	memset(&mHdr, 0x00, sizeof(MIDIHDR));
	mHdr.lpData = (LPSTR)data;
	mHdr.dwBufferLength = dataLen;
	mHdr.dwFlags = 0x00;
	
	retMM = midiOutPrepareHeader(mop->hMidiOut, &mHdr, sizeof(MIDIHDR));
	if (retMM != MMSYSERR_NOERROR)
		return 0x10;
	
	retMM = midiOutLongMsg(mop->hMidiOut, &mHdr, sizeof(MIDIHDR));
	if (retMM != MMSYSERR_NOERROR)
	{
		midiOutUnprepareHeader(mop->hMidiOut, &mHdr, sizeof(MIDIHDR));
		return 0x11;
	}
	
	retMM = midiOutUnprepareHeader(mop->hMidiOut, &mHdr, sizeof(MIDIHDR));
	if (retMM != MMSYSERR_NOERROR)
		return 0x12;
	
	return 0x00;
}
