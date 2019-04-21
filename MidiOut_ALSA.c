// ALSA MIDI Output
// ----------------
#include <stdlib.h>

#include <alsa/asoundlib.h>

#include <stdtype.h>
#include "MidiOut.h"

//typedef struct _midiout_port MIDIOUT_PORT;
struct _midiout_port
{
	snd_seq_t* hSeq;
	snd_seq_addr_t srcPort;
	snd_seq_addr_t dstPort;
};


static UINT8 FindPortAddress(MIDIOUT_PORT* mop, UINT32 portID, snd_seq_addr_t* portAddr);
static void SetupSourcePort(MIDIOUT_PORT* mop);


static const UINT8 snd_cmd_type[] =
{
	SND_SEQ_EVENT_NOTEOFF,		// 0x80
	SND_SEQ_EVENT_NOTEON,		// 0x90
	SND_SEQ_EVENT_KEYPRESS,		// 0xA0
	SND_SEQ_EVENT_CONTROLLER,	// 0xB0
	SND_SEQ_EVENT_PGMCHANGE,	// 0xC0
	SND_SEQ_EVENT_CHANPRESS,	// 0xD0
	SND_SEQ_EVENT_PITCHBEND,	// 0xE0
	SND_SEQ_EVENT_SYSEX			// 0xF0
};


MIDIOUT_PORT* MidiOutPort_Init(void)
{
	MIDIOUT_PORT* mop;
	
	mop = (MIDIOUT_PORT*)calloc(1, sizeof(MIDIOUT_PORT));
	if (mop == NULL)
		return NULL;
	
	mop->hSeq = NULL;
	
	return mop;
}

void MidiOutPort_Deinit(MIDIOUT_PORT* mop)
{
	if (mop->hSeq != NULL)
		MidiOutPort_CloseDevice(mop);
	
	free(mop);
	
	return;
}

UINT8 MidiOutPort_OpenDevice(MIDIOUT_PORT* mop, UINT32 id)
{
	int retVal;
	
	if (mop->hSeq != NULL)
		return 0xFE;
	
	retVal = snd_seq_open(&mop->hSeq, "default", SND_SEQ_OPEN_DUPLEX, 0);
	if (retVal < 0)
		return 0xFF;
	
	if (FindPortAddress(mop, id, &mop->dstPort))
	{
		printf("Port not found\n");
		snd_seq_close(mop->hSeq);
		mop->hSeq = NULL;
		return 0xF0;
	}
	
	mop->srcPort.client = snd_seq_client_id(mop->hSeq);
	mop->srcPort.port = 0;
	SetupSourcePort(mop);
	//printf("Port found: %d:%d\n", mop->dstPort.client, mop->dstPort.port);
	//printf("My port: %d:%d\n", mop->srcPort.client, mop->srcPort.port);
	
	retVal = snd_seq_connect_to(mop->hSeq, mop->srcPort.port, mop->dstPort.client, mop->dstPort.port);
	if (retVal < 0)
	{
		printf("snd_seq_connect error: %d\n", retVal);
		snd_seq_close(mop->hSeq);
		mop->hSeq = NULL;
		return 0x80;
	}
	
	return 0x00;
}

static UINT8 FindPortAddress(MIDIOUT_PORT* mop, UINT32 portID, snd_seq_addr_t* portAddr)
{
	snd_seq_client_info_t* cinfo;
	snd_seq_port_info_t* pinfo;
	UINT32 curPort;
	int client;
	int retVal;
	
	snd_seq_client_info_alloca(&cinfo);
	snd_seq_port_info_alloca(&pinfo);
	
	curPort = 0;
	snd_seq_client_info_set_client(cinfo, -1);
	while(snd_seq_query_next_client(mop->hSeq, cinfo) >= 0)
	{
		client = snd_seq_client_info_get_client(cinfo);
		
		snd_seq_port_info_set_client(pinfo, client);
		snd_seq_port_info_set_port(pinfo, -1);
		while(snd_seq_query_next_port(mop->hSeq, pinfo) >= 0)
		{
			// port must support MIDI messages
			if (! (snd_seq_port_info_get_type(pinfo) & SND_SEQ_PORT_TYPE_MIDI_GENERIC))
				continue;
			// port must be writable and write subscription is required
			if ((~snd_seq_port_info_get_capability(pinfo) & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)))
				continue;
			
			//if (snd_seq_port_info_get_client(pinfo) == 14)
			//	continue;	// skip Midi Through
			
			//printf("Valid port %d:%d ...\n", client, snd_seq_port_info_get_port(pinfo));
			if (curPort == portID)
			{
				portAddr->client = snd_seq_port_info_get_client(pinfo);
				portAddr->port = snd_seq_port_info_get_port(pinfo);
				return 0x00;
			}
			curPort ++;
		}
	}
	
	return 0x01;	// not found
}

static void SetupSourcePort(MIDIOUT_PORT* mop)
{
	snd_seq_port_info_t* pinfo;
	int retVal;
	
	snd_seq_port_info_alloca(&pinfo);
	
	snd_seq_port_info_set_port(pinfo, mop->srcPort.port);
	snd_seq_port_info_set_port_specified(pinfo, 1);
	
	//snd_seq_port_info_set_name(pinfo, "");
	
	snd_seq_port_info_set_capability(pinfo, 0);
	snd_seq_port_info_set_type(pinfo,
		SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
	
	retVal = snd_seq_create_port(mop->hSeq, pinfo);
	if (retVal < 0)
	{
		printf("create port error %d\n", retVal);
		return;
	}
	return;
}

UINT8 MidiOutPort_CloseDevice(MIDIOUT_PORT* mop)
{
	int retVal;
	
	if (mop->hSeq == NULL)
		return 0xFE;
	
	retVal = snd_seq_close(mop->hSeq);
	if (retVal < 0)
		return 0xFF;	// close error
	
	mop->hSeq = NULL;
	return 0x00;
}

void MidiOutPort_SendShortMsg(MIDIOUT_PORT* mop, UINT8 event, UINT8 data1, UINT8 data2)
{
	snd_seq_event_t seqEvt;
	int retVal;
	
	if (mop->hSeq == NULL)
		return;
	if (event < 0x80 || event >= 0xF0)
		return;	// ignore invalid message types
	
	snd_seq_ev_clear(&seqEvt);
	seqEvt.flags = 0x00;
	snd_seq_ev_set_source(&seqEvt, mop->srcPort.port);
	snd_seq_ev_set_direct(&seqEvt);
	seqEvt.dest = mop->dstPort;
	
	seqEvt.type = snd_cmd_type[(event >> 4) & 0x07];
	snd_seq_ev_set_fixed(&seqEvt);
	switch(seqEvt.type)
	{
	case SND_SEQ_EVENT_NOTEOFF:
	case SND_SEQ_EVENT_NOTEON:
	case SND_SEQ_EVENT_KEYPRESS:
		seqEvt.data.note.channel = event & 0x0F;
		seqEvt.data.note.note = data1;
		seqEvt.data.note.velocity = data2;
		break;
	case SND_SEQ_EVENT_CONTROLLER:
		seqEvt.data.control.channel = event & 0x0F;
		seqEvt.data.control.param = data1;
		seqEvt.data.control.value = data2;
		break;
	case SND_SEQ_EVENT_PGMCHANGE:
	case SND_SEQ_EVENT_CHANPRESS:
		seqEvt.data.control.channel = event & 0x0F;
		seqEvt.data.control.value = data1;
		break;
	case SND_SEQ_EVENT_PITCHBEND:
		seqEvt.data.control.channel = event & 0x0F;
		seqEvt.data.control.value = ((data2 << 7) | (data1 << 0)) - 0x2000;
		break;
	default:
		return;	// ignore
	}
	retVal = snd_seq_event_output(mop->hSeq, &seqEvt);
	if (retVal < 0)
		printf("snd_seq_event_output error %d\n", retVal);
	retVal = snd_seq_drain_output(mop->hSeq);
	if (retVal < 0)
		printf("snd_seq_drain_output error %d\n", retVal);
	
	return;
}

#define SYSEX_BLOCK_SIZE	0x400

UINT8 MidiOutPort_SendLongMsg(MIDIOUT_PORT* mop, size_t dataLen, const void* data)
{
	snd_seq_event_t seqEvt;
	size_t remLen;
	int retVal;
	
	if (mop->hSeq == NULL)
		return 0xFE;
	
	snd_seq_ev_clear(&seqEvt);
	seqEvt.flags = 0x00;
	snd_seq_ev_set_source(&seqEvt, mop->srcPort.port);
	snd_seq_ev_set_direct(&seqEvt);
	seqEvt.dest = mop->dstPort;
	
	seqEvt.type = SND_SEQ_EVENT_SYSEX;
	snd_seq_ev_set_variable(&seqEvt, dataLen, (void*)data);
	
	// Note: aplaymidi does something like this,
	//       but this does definitely NOT work with TiMidity.
	remLen = seqEvt.data.ext.len;
	while(remLen > 0)
	{
		seqEvt.data.ext.len = (remLen < SYSEX_BLOCK_SIZE) ? remLen : SYSEX_BLOCK_SIZE;
		
		retVal = snd_seq_event_output(mop->hSeq, &seqEvt);
		if (retVal < 0)
			printf("snd_seq_event_output error %d\n", retVal);
		retVal = snd_seq_drain_output(mop->hSeq);
		if (retVal < 0)
			printf("snd_seq_drain_output error %d\n", retVal);
		retVal = snd_seq_sync_output_queue(mop->hSeq);
		if (retVal < 0)
			printf("snd_seq_sync_output_queue error %d\n", retVal);
		
		seqEvt.data.ext.ptr = (char*)seqEvt.data.ext.ptr + seqEvt.data.ext.len;
		remLen -= seqEvt.data.ext.len;
	}
	
	return 0x00;
}

UINT8 MidiOut_GetPortList(MIDI_PORT_LIST* mpl)
{
	snd_seq_t* hSeq;
	snd_seq_client_info_t* cinfo;
	snd_seq_port_info_t* pinfo;
	UINT32 curPort;
	MIDI_PORT_DESC* pDesc;
	int retVal;
	
	retVal = snd_seq_open(&hSeq, "default", SND_SEQ_OPEN_DUPLEX, 0);
	if (retVal < 0)
		return 0xFF;
	
	snd_seq_client_info_alloca(&cinfo);
	snd_seq_port_info_alloca(&pinfo);
	
	// at first, count all ports
	curPort = 0;
	snd_seq_client_info_set_client(cinfo, -1);
	while(snd_seq_query_next_client(hSeq, cinfo) >= 0)
	{
		snd_seq_port_info_set_client(pinfo, snd_seq_client_info_get_client(cinfo));
		snd_seq_port_info_set_port(pinfo, -1);
		while(snd_seq_query_next_port(hSeq, pinfo) >= 0)
		{
			if (! (snd_seq_port_info_get_type(pinfo) & SND_SEQ_PORT_TYPE_MIDI_GENERIC))
				continue;
			if ((~snd_seq_port_info_get_capability(pinfo) & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)))
				continue;
			
			curPort ++;	// count a valid port
		}
	}
	
	mpl->count = curPort;
	mpl->ports = (MIDI_PORT_DESC*)calloc(mpl->count, sizeof(MIDI_PORT_DESC));
	if (mpl->ports == NULL)
	{
		snd_seq_close(hSeq);
		return 0xFF;
	}
	
	// now collect all information
	curPort = 0;
	snd_seq_client_info_set_client(cinfo, -1);
	while(snd_seq_query_next_client(hSeq, cinfo) >= 0)
	{
		snd_seq_port_info_set_client(pinfo, snd_seq_client_info_get_client(cinfo));
		snd_seq_port_info_set_port(pinfo, -1);
		while(snd_seq_query_next_port(hSeq, pinfo) >= 0)
		{
			// port must support MIDI messages
			if (! (snd_seq_port_info_get_type(pinfo) & SND_SEQ_PORT_TYPE_MIDI_GENERIC))
				continue;
			// port must be writable and write subscription is required
			if ((~snd_seq_port_info_get_capability(pinfo) & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)))
				continue;
			
			pDesc = &mpl->ports[curPort];
			// both IDs are 8 bits each
			pDesc->id = (snd_seq_port_info_get_client(pinfo) << 8) | (snd_seq_port_info_get_port(pinfo) << 0);
			pDesc->name = strdup(snd_seq_port_info_get_name(pinfo));
			curPort ++;
		}
	}
	
	snd_seq_close(hSeq);
	
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
