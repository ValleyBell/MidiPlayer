#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>	// for isspace()

#include <stdtype.h>
#include "MidiInsReader.h"

#ifdef _MSC_VER
#define strdup	_strdup
#endif


/*
# MIDI Sequencer "Cherry" Instrument List Dump

# New INS Version
# Ver 000 000 000 000
V 001 000 000 000 000
## old versions lack the GRA field

# LSB MSB PC# KEY GRA NAME
L 000 000 000 000 000 XG
L 000 000 000 000 003 MU100

P 000 000 000 000 000 GM

# LSB MSB PC# 000 Instrument
M 001 000 001 000 000 Piano 1
M 001 000 002 000 000 Piano 2

# LSB MSB PC# 000 Drum map name
D 001 000 001 000 000 55 STANDARD
D 002 000 025 000 001 88 ELECTRONIC

# LSB MSB PC# Note Drum name (MSB=1 PC=0)
N 001 000 001 035 000 Kick 2
N 001 000 017 036 000 MONDO K
*/

#define INSCOL_LSB	0	// Bank LSB
#define INSCOL_MSB	1	// Bank MSB
#define INSCOL_PC	2	// Program Change/Instrument ID
#define INSCOL_KEY	3	// key/note
#define INSCOL_GRA	4	// layout ID

static void RemoveNewLines(char* str)
{
	char* strPtr;
	
	strPtr = str + strlen(str) - 1;
	while(strPtr >= str && (UINT8)*strPtr < 0x20)
	{
		*strPtr = '\0';
		strPtr --;
	}
	
	return;
}

static size_t GetColList(const char* line, size_t maxCols, const char** colPtrs)
{
	size_t curCol;
	const char* lPtr;
	
	lPtr = line;
	for (curCol = 0; curCol < maxCols; curCol ++)
	{
		if (*lPtr == '\0')
			break;
		
		colPtrs[curCol] = lPtr;
		// skip actual data (until a whitespace is reached)
		while(*lPtr != '\0' && ! isspace(*lPtr))
			lPtr ++;
		// skip whitespace in order to reach next data
		while(*lPtr != '\0' && isspace(*lPtr))
			lPtr ++;
	}
	
	return curCol;
}

static UINT8 ParseInsLine(const char* line, UINT8 version, char* lType, UINT8 fields[5], const char** descStr)
{
	const char* lPtr;
	const char* colList[8];
	size_t curCol;
	size_t maxCols;
	
	lPtr = line;
	while(*lPtr != '\0' && isspace(*lPtr))
		lPtr ++;
	if (*lPtr == '\0' || *lPtr == '#')
		return 0xFF;	// empty/comment line
	
	if (! (*lPtr >= 'A' && *lPtr <= 'Z'))
		return 0x80;	// invalid line type
	maxCols = GetColList(lPtr, 8, colList);
	
	*lType = colList[0][0];	// line type
	// known line types: D, L, M, N, P, V
	
	switch(version)
	{
	case 0:
		if (maxCols < 6)
			return 0x81;	// not enough columns
		for (curCol = 0; curCol < 4; curCol ++)
		{
			fields[curCol] = (UINT8)strtoul(colList[1 + curCol], (char**)&lPtr, 10);
			if (! isspace(*lPtr) && *lPtr != '\0')
				return 0x82;	// enountered invalid data
		}
		fields[4] = 0;
		*descStr = colList[5];	// line description
		break;
	case 1:
		if (maxCols < 7)
			return 0x81;	// not enough columns
		for (curCol = 0; curCol < 5; curCol ++)
		{
			fields[curCol] = (UINT8)strtoul(colList[1 + curCol], (char**)&lPtr, 10);
			if (! isspace(*lPtr) && *lPtr != '\0')
				return 0x82;	// enountered invalid data
		}
		*descStr = colList[6];	// line description
		break;
	default:
		return 0xFE;	// unknown version
	}
	
	return 0x00;
}

static void AddInstrument(INS_BANK* insBank, const INS_DATA* newIns, UINT8 drumMode)
{
	INS_PRG_LST* tempPrg;
	UINT8 prgID;
	
	prgID = (drumMode << 7) | newIns->program;
	tempPrg = &insBank->prg[prgID];
	if (tempPrg->count >= tempPrg->alloc)
	{
		tempPrg->alloc += 0x20;
		tempPrg->instruments = (INS_DATA*)realloc(tempPrg->instruments, tempPrg->alloc * sizeof(INS_DATA));
	}
	tempPrg->instruments[tempPrg->count] = *newIns;
	tempPrg->count ++;
	
	if (newIns->bankMSB > insBank->maxBankMSB)
		insBank->maxBankMSB = newIns->bankMSB;
	if (newIns->bankLSB > insBank->maxBankLSB)
		insBank->maxBankLSB = newIns->bankLSB;
	if (drumMode && newIns->program > insBank->maxDrumKit)
		insBank->maxDrumKit = newIns->program;
	
	return;
}

UINT8 LoadInstrumentList(const char* fileName, INS_BANK* insBank)
{
	FILE* hFile;
	UINT32 insAlloc;
	char lineBuf[0x100];
	char* tempPtr;
	
	UINT8 fileVer;
	char lineType;
	UINT8 lineFields[5];
	const char* lineDesc;
	UINT8 retVal;
	INS_DATA newIns;
	
	memset(insBank, 0x00, sizeof(INS_BANK));
	
	hFile = fopen(fileName, "rt");
	if (hFile == NULL)
		return 0xFF;
	
	insAlloc = 0x100;
	fileVer = 0;	// assume version 0 at first
	insBank->moduleType = 0x00;
	insBank->maxBankMSB = 0x00;
	insBank->maxBankLSB = 0x00;
	insBank->maxDrumKit = 0x00;
	while(! feof(hFile))
	{
		tempPtr = fgets(lineBuf, 0x100, hFile);
		if (tempPtr == NULL)
			break;
		RemoveNewLines(lineBuf);
		
		retVal = ParseInsLine(lineBuf, fileVer, &lineType, lineFields, &lineDesc);
		if (retVal)
			continue;
		
		// all fields (but 'V') use lineFields[INSCOL_GRA], lineDesc
		switch(lineType)
		{
		case 'V':	// file version
			fileVer = lineFields[0];
			break;
		case 'L':	// layout name
			// lineFields[INSCOL_GRA], lineDesc only
			break;
		case 'P':	// categories??
			// lineFields[INSCOL_LSB], lineFields[INSCOL_MSB]
			break;
		case 'M':	// melody instrument
			// lineFields[INSCOL_LSB], lineFields[INSCOL_MSB], lineFields[INSCOL_PC]
			newIns.bankMSB = lineFields[INSCOL_MSB];
			newIns.bankLSB = lineFields[INSCOL_LSB];
			newIns.program = lineFields[INSCOL_PC] - 1;
			newIns.moduleID = lineFields[INSCOL_GRA];
			newIns.insName = strdup(lineDesc);
			AddInstrument(insBank, &newIns, 0);
			break;
		case 'D':	// drum instrument
			newIns.bankMSB = lineFields[INSCOL_MSB];
			newIns.bankLSB = lineFields[INSCOL_LSB];
			newIns.program = lineFields[INSCOL_PC] - 1;
			newIns.moduleID = lineFields[INSCOL_GRA];
			newIns.insName = strdup(lineDesc);
			AddInstrument(insBank, &newIns, 1);
			break;
		case 'N':	// drum note name
			/*newDrum.bankMSB = lineFields[INSCOL_MSB];
			newDrum.bankLSB = lineFields[INSCOL_LSB];
			newDrum.program = lineFields[INSCOL_PC] - 1;
			newDrum.key = lineFields[INSCOL_KEY];
			newDrum.moduleID = lineFields[INSCOL_GRA];
			newDrum.insName = strdup(lineDesc);*/
			break;
		}
	}
	
	fclose(hFile);
	
	return 0x00;
}

static void FreeInstrumentList(UINT32 insCount, INS_DATA* insList)
{
	UINT32 curIns;
	
	for (curIns = 0; curIns < insCount; curIns ++)
	{
		free(insList[curIns].insName);
	}
	free(insList);
	
	return;
}

void FreeInstrumentBank(INS_BANK* insBank)
{
	UINT16 curPrg;
	INS_PRG_LST* tempPrg;
	
	for (curPrg = 0x00; curPrg < 0x100; curPrg ++)
	{
		tempPrg = &insBank->prg[curPrg];
		FreeInstrumentList(tempPrg->count, tempPrg->instruments);
		tempPrg->instruments = NULL;
		tempPrg->alloc = tempPrg->count = 0;
	}
	
	return;
}


void PatchInstrumentBank(INS_BANK* insBank, UINT8 flags, UINT8 msb, UINT8 lsb)
{
	UINT16 curPrg;
	UINT32 curIns;
	INS_PRG_LST* tempPrg;
	INS_DATA* insData;
	
	for (curPrg = 0x00; curPrg < 0x100; curPrg ++)
	{
		tempPrg = &insBank->prg[curPrg];
		for (curIns = 0; curIns < tempPrg->count; curIns ++)
		{
			insData = &tempPrg->instruments[curIns];
			if (flags & 0x01)
				insData->bankMSB = msb;
			if (flags & 0x02)
				insData->bankLSB = lsb;
		}
	}
	
	return;
}

static UINT32 CountFilteredIns(UINT32 srcCount, const INS_DATA* srcData, UINT8 moduleID)
{
	UINT32 curIns;
	UINT32 insCount;
	
	if (moduleID == 0xFF)
		return srcCount;
	
	insCount = 0;
	for (curIns = 0; curIns < srcCount; curIns ++)
	{
		if (srcData[curIns].moduleID == moduleID)
			insCount ++;
	}
	return insCount;
}

static UINT32 CopyFilteredInsList(UINT32 dstAlloc, INS_DATA* dstData, UINT32 srcCount, const INS_DATA* srcData, UINT8 moduleID)
{
	UINT32 curIns;
	UINT32 insCount;
	
	if (moduleID == 0xFF)
	{
		if (dstAlloc < srcCount)
			srcCount = dstAlloc;
		for (curIns = 0; curIns < srcCount; curIns ++)
		{
			dstData[curIns] = srcData[curIns];
			dstData[curIns].insName = strdup(srcData[curIns].insName);
		}
		return curIns;
	}
	else
	{
		insCount = 0;
		for (curIns = 0; curIns < srcCount; curIns ++)
		{
			if (insCount >= dstAlloc)
				break;
			if (srcData[curIns].moduleID == moduleID)
			{
				dstData[insCount] = srcData[curIns];
				dstData[insCount].insName = strdup(srcData[curIns].insName);
				insCount ++;
			}
		}
		return insCount;
	}
}

void CopyInstrumentBank(INS_BANK* dest, const INS_BANK* source, UINT8 moduleID)
{
	UINT16 curPrg;
	
	dest->moduleType = source->moduleType;
	dest->maxBankMSB = source->maxBankMSB;
	dest->maxBankLSB = source->maxBankLSB;
	dest->maxDrumKit = source->maxDrumKit;
	for (curPrg = 0x00; curPrg < 0x100; curPrg ++)
	{
		const INS_PRG_LST* srcPrg = &source->prg[curPrg];
		INS_PRG_LST* dstPrg = &dest->prg[curPrg];
		
		dstPrg->alloc = CountFilteredIns(srcPrg->count, srcPrg->instruments, moduleID);
		dstPrg->instruments = (INS_DATA*)malloc(dstPrg->alloc * sizeof(INS_DATA));
		dstPrg->count = CopyFilteredInsList(dstPrg->alloc, dstPrg->instruments, srcPrg->count, srcPrg->instruments, moduleID);
	}
	
	return;
}

static UINT32 MergeInsList(UINT32 dstAlloc, UINT32 dstCount, INS_DATA* dstData, UINT32 srcCount, const INS_DATA* srcData)
{
	UINT32 srcIns;
	UINT32 dstIns;
	const INS_DATA* siData;
	INS_DATA* diData;
	
	for (srcIns = 0; srcIns < srcCount; srcIns ++)
	{
		if (dstCount >= dstAlloc)
			break;
		
		siData = &srcData[srcIns];
		for (dstIns = 0; dstIns < dstCount; dstIns ++)
		{
			diData = &dstData[dstIns];
			if (siData->bankMSB == diData->bankMSB && siData->bankLSB == diData->bankLSB &&
				siData->program == diData->program && siData->moduleID == diData->moduleID)
				break;	// found duplicate instrument - skip
		}
		if (dstIns >= dstCount)
		{
			dstData[dstCount] = *siData;
			dstData[dstCount].insName = strdup(siData->insName);
			dstCount ++;
		}
	}
	return dstCount;
}

void MergeInstrumentBanks(INS_BANK* dest, const INS_BANK* source)
{
	UINT16 curPrg;
	
	if (dest->maxBankMSB < source->maxBankMSB)
		dest->maxBankMSB = source->maxBankMSB;
	if (dest->maxBankLSB < source->maxBankLSB)
		dest->maxBankLSB = source->maxBankLSB;
	if (dest->maxDrumKit < source->maxDrumKit)
		dest->maxDrumKit = source->maxDrumKit;
	for (curPrg = 0x00; curPrg < 0x100; curPrg ++)
	{
		const INS_PRG_LST* srcPrg = &source->prg[curPrg];
		INS_PRG_LST* dstPrg = &dest->prg[curPrg];
		
		dstPrg->alloc = dstPrg->count + srcPrg->count;
		dstPrg->instruments = (INS_DATA*)realloc(dstPrg->instruments, dstPrg->alloc * sizeof(INS_DATA));
		dstPrg->count = MergeInsList(dstPrg->alloc, dstPrg->count, dstPrg->instruments, srcPrg->count, srcPrg->instruments);
	}
	
	return;
}
