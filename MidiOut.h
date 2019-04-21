#ifndef __MIDIOUT_H__
#define __MIDIOUT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdtype.h>

typedef struct _midiout_port MIDIOUT_PORT;

MIDIOUT_PORT* MidiOutPort_Init(void);
void MidiOutPort_Deinit(MIDIOUT_PORT* mop);
UINT8 MidiOutPort_OpenDevice(MIDIOUT_PORT* mop, UINT32 id);
UINT8 MidiOutPort_CloseDevice(MIDIOUT_PORT* mop);
void MidiOutPort_SendShortMsg(MIDIOUT_PORT* mop, UINT8 event, UINT8 data1, UINT8 data2);
UINT8 MidiOutPort_SendLongMsg(MIDIOUT_PORT* mop, size_t dataLen, const void* data);


typedef struct _midi_port_description
{
	UINT32 id;	// internal ID
	char* name;	// name of the MIDI port
} MIDI_PORT_DESC;
typedef struct _midi_port_list
{
	UINT32 count;
	MIDI_PORT_DESC* ports;
} MIDI_PORT_LIST;
#define MIDI_PORT_ID_INVALID	((UINT32)-2)	// -1 is used for "default MIDI device"

UINT8 MidiOut_GetPortList(MIDI_PORT_LIST* portList);
void MidiOut_FreePortList(MIDI_PORT_LIST* portList);

#ifdef __cplusplus
}
#endif

#endif	// __MIDIOUT_H__
