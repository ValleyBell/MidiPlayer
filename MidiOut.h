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

#ifdef __cplusplus
}
#endif

#endif	// __MIDIOUT_H__
