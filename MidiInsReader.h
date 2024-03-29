#ifndef __MIDIINSREADER_H__
#define __MIDIINSREADER_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdtype.h>


typedef struct
{
	UINT8 bankMSB;
	UINT8 bankLSB;
	UINT8 program;
	UINT8 moduleID;
	char* insName;
} INS_DATA;

typedef struct
{
	UINT32 alloc;
	UINT32 count;
	INS_DATA* instruments;
} INS_PRG_LST;
typedef struct
{
	UINT8 moduleType;
	UINT8 maxBankMSB;
	UINT8 maxBankLSB;
	UINT8 maxDrumKit;
	INS_PRG_LST prg[0x100];	// program IDs: 00..7F - melody instruments, 80-FF - drum instruments
} INS_BANK;


#define MMASK_TYPE(x)	(x & 0xF0)
#define MMASK_MOD(x)	(x & 0x0F)

#define MODULE_TYPE_GM	0x00
#define MODULE_TYPE_GS	0x10
#define MODULE_TYPE_XG	0x20
#define MODULE_TYPE_K5	0x30	// Korg 5s-Series
#define MODULE_TYPE_LA	0x60	// MT-32 (LA synth) / CM-64
#define MODULE_TYPE_OT	0x70	// other non-GM modules

#define MT_UNKNOWN		0x08

#define MTGM_LVL1		0x00
#define MTGM_LVL2		0x01

#define MTGS_SC55		0x00
#define MTGS_SC88		0x01
#define MTGS_SC88PRO	0x02
#define MTGS_SC8850		0x03
#define MTGS_TG300B		0x0F	// Yamaha's GS emulation (TB300B mode)

#define MTXG_MU50		0x00
#define MTXG_MU80		0x01
#define MTXG_MU90		0x02
#define MTXG_MU100		0x03
#define MTXG_MU128		0x04
#define MTXG_MU1000		0x05

#define MTK5_05RW		0x01
#define MTK5_X5DR		0x02
#define MTK5_NS5R		0x03
#define MTK5_GMB		0x00	// Korg GM-b map

#define MTLA_MT32		0x00
#define MTLA_CM32P		0x01
#define MTLA_CM64		0x02


#define MODULE_GM_1		(MODULE_TYPE_GM | MTGM_LVL1)
#define MODULE_GM_2		(MODULE_TYPE_GM | MTGM_LVL2)

#define MODULE_SC55		(MODULE_TYPE_GS | MTGS_SC55)
#define MODULE_SC88		(MODULE_TYPE_GS | MTGS_SC88)
#define MODULE_SC88PRO	(MODULE_TYPE_GS | MTGS_SC88PRO)
#define MODULE_SC8850	(MODULE_TYPE_GS | MTGS_SC8850)
#define MODULE_TG300B	(MODULE_TYPE_GS | MTGS_TG300B)

#define MODULE_MU50		(MODULE_TYPE_XG | MTXG_MU50)
#define MODULE_MU80		(MODULE_TYPE_XG | MTXG_MU80)
#define MODULE_MU90		(MODULE_TYPE_XG | MTXG_MU90)
#define MODULE_MU100	(MODULE_TYPE_XG | MTXG_MU100)
#define MODULE_MU128	(MODULE_TYPE_XG | MTXG_MU128)
#define MODULE_MU1000	(MODULE_TYPE_XG | MTXG_MU1000)

#define MODULE_05RW		(MODULE_TYPE_K5 | MTK5_05RW)
#define MODULE_X5DR		(MODULE_TYPE_K5 | MTK5_X5DR)
#define MODULE_NS5R		(MODULE_TYPE_K5 | MTK5_NS5R)
#define MODULE_KGMB		(MODULE_TYPE_K5 | MTK5_GMB)

#define MODULE_MT32		(MODULE_TYPE_LA | MTLA_MT32)
#define MODULE_CM32P	(MODULE_TYPE_LA | MTLA_CM32P)
#define MODULE_CM64		(MODULE_TYPE_LA | MTLA_CM64)


UINT8 LoadInstrumentList(const char* fileName, INS_BANK* insBank);
void FreeInstrumentBank(INS_BANK* insBank);

// flags:
//	Bit 0 (01) - patch Bank MSB
//	Bit 1 (02) - patch Bank LSB
void PatchInstrumentBank(INS_BANK* insBank, UINT8 flags, UINT8 msb, UINT8 lsb);
// moduleID: copy only instruments of a specific module (0xFF - copy everything)
void CopyInstrumentBank(INS_BANK* dest, const INS_BANK* source, UINT8 moduleID);
void MergeInstrumentBanks(INS_BANK* dest, const INS_BANK* source);


#ifdef __cplusplus
}
#endif

#endif	// __MIDIINSREADER_H__
