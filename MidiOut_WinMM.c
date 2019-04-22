// WinMM MIDI Output
// -----------------
#include <stdlib.h>

#include <Windows.h>
#ifndef DWORD_PTR
#define DWORD_PTR	DWORD
#endif

#include <stdtype.h>
#include "MidiOut.h"

#ifdef _MSC_VER
#define strdup	_strdup
#endif

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

static UINT8 MidiOutPort_DoLongMsg(MIDIOUT_PORT* mop, size_t dataLen, const void* data)
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

UINT8 MidiOutPort_SendLongMsg(MIDIOUT_PORT* mop, size_t dataLen, const void* data)
{
	if (! IsBadWritePtr((LPVOID)data, dataLen))
	{
		return MidiOutPort_DoLongMsg(mop, dataLen, data);
	}
	else
	{
		UINT8 retVal;
		void* dupData;
		
		// Windows is stupid and requires R/W access for the data, so it doesn't work with data
		// from read-only memory pages. We make a temporary copy of the data to work around that.
		dupData = malloc(dataLen);
		memcpy(dupData, data, dataLen);
		
		retVal = MidiOutPort_DoLongMsg(mop, dataLen, dupData);
		
		free(dupData);
		return retVal;
	}
}

UINT8 MidiOut_GetPortList(MIDI_PORT_LIST* mpl)
{
	UINT32 curPort;
	UINT devID;
	MMRESULT retValMM;
	MIDIOUTCAPSA moCaps;
	MIDI_PORT_DESC* pDesc;
	
	mpl->count = midiOutGetNumDevs();
	mpl->ports = (MIDI_PORT_DESC*)calloc(mpl->count, sizeof(MIDI_PORT_DESC));
	if (mpl->ports == NULL)
		return 0xFF;
	
	for (curPort = 0; curPort < mpl->count; curPort ++)
	{
		pDesc = &mpl->ports[curPort];
		//devID = (curPort == 0) ? MIDI_MAPPER : (UINT)(curPort - 1);
		devID = (UINT)curPort;
		
		retValMM = midiOutGetDevCapsA(devID, &moCaps, sizeof(MIDIOUTCAPSA));
		if (retValMM != MMSYSERR_NOERROR)
		{
			pDesc->id = MIDI_PORT_ID_INVALID;
			pDesc->name = NULL;
		}
		else
		{
			pDesc->id = devID;
			pDesc->name = strdup(moCaps.szPname);
		}
	}
	
	return 0x00;
}

void MidiOut_FreePortList(MIDI_PORT_LIST* mpl)
{
	UINT32 curPort;
	
	for (curPort = 0; curPort < mpl->count; curPort ++)
		free(mpl->ports[curPort].name);
	
	mpl->count = 0;
	free(mpl->ports);	mpl->ports = NULL;
	
	return;
}
