// RCP MIDI reader by Valley Bell
#include <string>
#include <vector>
#include <string.h>
#include <stdio.h>

#include <stdtype.h>
#include "MidiLib.hpp"
#include "RCPLoader.hpp"

#include "vis.hpp"
#define printf	vis_printf

struct UserSysExData
{
	std::string name;
	std::vector<UINT8> data;
};

struct RCP_INFO
{
	UINT8 fileVer;
	UINT16 trkCnt;
	UINT16 tickRes;
	UINT16 tempoBPM;
	UINT8 beatNum;
	UINT8 beatDen;
	UINT8 keySig;
	INT8 gblTransp;
	std::string cm6File;
	std::string gsdFile1;
	std::string gsdFile2;
	UserSysExData usrSyx[8];
};

struct PlayingNote
{
	UINT8 note;
	UINT32 len;	 // remaining length
};

static std::string RcpStr2StdStr(const char* rcpStr);
static void RTrimChar(std::string& text, char trimChar = ' ', bool leaveLast = false);
static std::vector<std::string> Str2Lines(const std::string& textStr, size_t lineLen);
static UINT8 val2shift(UINT32 value);
static void RcpKeySig2Mid(UINT8 rcpKeySig, UINT8 buffer[2]);
//UINT8 LoadRCPAsMidi(const char* fileName, MidiFile& midFile, std::vector<std::string>& initFiles);
//UINT8 LoadRCPAsMidi(FILE* infile, MidiFile& midFile, std::vector<std::string>& initFiles);
static UINT8 ReadRCPTrackAsMid(FILE* infile, const RCP_INFO* rcpInf, MidiTrack* trk);
static void WriteRolandSyxData(std::vector<UINT8>& buffer, const UINT8* syxHdr, UINT32 address, UINT32 len, const UINT8* data);
static void WriteRolandSyxBulk(std::vector<UINT8>& buffer, const UINT8* syxHdr, UINT32 address, UINT32 len, const UINT8* data, UINT32 bulkSize);
//UINT8 Cm62Syx(const char* fileName, std::vector<UINT8>& syxData);
//UINT8 Cm62Syx(FILE* infile, std::vector<UINT8>& syxData);
static void Bytes2NibblesHL(UINT32 bytes, UINT8* nibData, const UINT8* byteData);
static void GsdPartParam2BulkDump(UINT8* bulkData, const UINT8* partData);
UINT8 Gsd2Syx(const char* fileName, std::vector<UINT8>& syxData);
UINT8 Gsd2Syx(FILE* infile, std::vector<UINT8>& syxData);
static UINT16 ReadLE16(FILE* infile);
static UINT32 ReadLE32(FILE* infile);

static const UINT8 MT32_PATCH_CHG[0x10] = {0x41, 0x10, 0x16, 0x12, 0x03, 0x00, 0x00, 0xFF, 0xFF, 0x18, 0x32, 0x0C, 0x00, 0x01, 0xCC, 0xF7};

static UINT16 NUM_LOOPS = 2;

static std::string RcpStr2StdStr(const char* rcpStr)
{
	const char* curChr;
	const char* lastChr;
	
	lastChr = rcpStr - 1;
	for (curChr = rcpStr; *curChr != '\0'; curChr ++)
	{
		if (*curChr != ' ')
			lastChr = curChr;
	}
	lastChr ++;
	return std::string(rcpStr, lastChr);
}

static void RTrimChar(std::string& text, char trimChar, bool leaveLast)
{
	size_t curChr;
	
	for (curChr = text.length(); curChr > 0; curChr --)
	{
		if (text[curChr - 1] != trimChar)
			break;
	}
	if (leaveLast && curChr < text.length())
		curChr ++;
	
	text.resize(curChr);
	return;
}

static std::vector<std::string> Str2Lines(const std::string& textStr, size_t lineLen)
{
	size_t curPos;
	std::string lineStr;
	std::vector<std::string> result;
	
	for (curPos = 0; curPos < textStr.length(); curPos += lineLen)
	{
		lineStr = textStr.substr(curPos, lineLen);
		RTrimChar(lineStr, ' ', false);
		result.push_back(lineStr);
	}
	
	return result;
}

static UINT8 val2shift(UINT32 value)
{
	UINT8 shift = 0;
	value >>= 1;
	
	while(value)
	{
		shift ++;
		value >>= 1;
	}
	return shift;
}

static void RcpKeySig2Mid(UINT8 rcpKeySig, UINT8 buffer[2])
{
	INT8 key;
	
	if (rcpKeySig & 0x08)
		key = -(rcpKeySig & 0x07);
	else
		key = rcpKeySig & 0x07;
	
	buffer[0] = (UINT8)key;					// main key (number of sharps/flats)
	buffer[1] = (rcpKeySig & 0x10) >> 4;	// major (0) / minor (1)
	
	return;
}

static std::vector<UINT8> ProcessRcpSysEx(const std::vector<UINT8>& syxData, UINT8 param1, UINT8 param2, UINT8 midChn)
{
	std::vector<UINT8> syxBuf;
	size_t curPos;
	UINT8 chkSum;
	
	chkSum = 0x00;
	for (curPos = 0; curPos < syxData.size(); curPos ++)
	{
		UINT8 data = syxData[curPos];
		
		if (data & 0x80)
		{
			switch(data)
			{
			case 0x80:	// put data value (cmdP1)
				data = param1;
				break;
			case 0x81:	// put data value (cmdP2)
				data = param2;
				break;
			case 0x82:	// put data value (midChn)
				data = midChn;
				break;
			case 0x83:	// initialize Roland Checksum
				chkSum = 0x00;
				break;
			case 0x84:	// put Roland Checksum
				data = (-chkSum) & 0x7F;
				break;
			case 0xF7:	// SysEx end
				syxBuf.push_back(data);
				return syxBuf;
			default:
				printf("Unknown SysEx command 0x%02X found in SysEx data!\n", data);
				break;
			}
		}
		
		if (! (data & 0x80))
		{
			syxBuf.push_back(data);
			chkSum += data;
		}
	}
	
	syxBuf.push_back(0xF7);
	return syxBuf;
}

UINT8 LoadRCPAsMidi(const char* fileName, MidiFile& midFile, std::vector<std::string>& initFiles)
{
	FILE* infile;
	UINT8 retVal;
	
	infile = fopen(fileName, "rb");
	if (infile == NULL)
		return 0xFF;
	
	retVal = LoadRCPAsMidi(infile, midFile, initFiles);
	fclose(infile);
	
	return retVal;
}

UINT8 LoadRCPAsMidi(FILE* infile, MidiFile& midFile, std::vector<std::string>& initFiles)
{
	UINT8 fileVer;
	char tempBuf[0x200];
	UINT8* tempBufU = (UINT8*)tempBuf;
	std::string tempStr;
	RCP_INFO rcpInf;
	UINT16 curTrk;
	UINT32 tempLng;
	UINT8 retVal;
	MidiTrack* newTrk;
	
	fileVer = 0xFF;
	fread(tempBuf, 0x01, 0x20, infile);
	if (! strncmp(tempBuf, "RCM-PC98V2.0(C)COME ON MUSIC\r\n", 0x20))
		fileVer = 2;
	else if (! strncmp(tempBuf, "COME ON MUSIC RECOMPOSER RCP3.0", 0x20))
		fileVer = 3;
	if (fileVer == 0xFF)
		return 0x10;
	printf("Loading RCP v%u file ...\n", fileVer);
	
	rcpInf.fileVer = fileVer;
	midFile.ClearAll();
	
	newTrk = new MidiTrack;	// header track
	if (fileVer == 2)
	{
		// song title
		fread(tempBuf, 0x01, 0x40, infile);		tempBuf[0x40] = '\0';
		tempStr = RcpStr2StdStr(tempBuf);
		if (! tempStr.empty())
			newTrk->AppendMetaEvent(0, 0x03, tempStr.length(), tempStr.c_str());
		
		// comments
		fread(tempBuf, 0x01, 0x150, infile);	tempBuf[0x150] = '\0';
		tempStr = RcpStr2StdStr(tempBuf);
		if (! tempStr.empty())
		{
			// The comments section consists of 12 lines with 28 characters each.
			// Lines are padded with spaces, so we have to split them manually.
			std::vector<std::string> lines = Str2Lines(tempStr, 28);
			for (tempLng = 0; tempLng < lines.size(); tempLng ++)
				newTrk->AppendMetaEvent(0, 0x01, lines[tempLng].length(), lines[tempLng].c_str());
		}
		fseek(infile, 0x10, SEEK_CUR);
		
		rcpInf.tickRes = fgetc(infile);
		rcpInf.tempoBPM = fgetc(infile);
		rcpInf.beatNum = fgetc(infile);
		rcpInf.beatDen = fgetc(infile);
		rcpInf.keySig = fgetc(infile);
		rcpInf.gblTransp = fgetc(infile);
		
		// names of additional files
		fread(tempBuf, 0x01, 0x10, infile);	tempBuf[0x10] = '\0';
		rcpInf.cm6File = RcpStr2StdStr(tempBuf);
		fread(tempBuf, 0x01, 0x10, infile);	tempBuf[0x10] = '\0';
		rcpInf.gsdFile1 = RcpStr2StdStr(tempBuf);
		rcpInf.gsdFile2 = "";
		
		rcpInf.trkCnt = fgetc(infile);
		rcpInf.tickRes |= (fgetc(infile) << 8);
		
		fseek(infile, 0x1E, SEEK_CUR);	// skip TONENAME.TB file path
		fseek(infile, 0x20 * 0x10, SEEK_CUR);	// skip rhythm definitions
	}
	else
	{
		// song title
		fread(tempBuf, 0x01, 0x80, infile);		tempBuf[0x80] = '\0';
		tempStr = RcpStr2StdStr(tempBuf);
		if (! tempStr.empty())
			newTrk->AppendMetaEvent(0, 0x03, tempStr.length(), tempStr.c_str());
		
		// comments
		fread(tempBuf, 0x01, 0x168, infile);	tempBuf[0x168] = '\0';
		tempStr = RcpStr2StdStr(tempBuf);
		if (! tempStr.empty())
		{
			// The comments section consists of 12 lines with 30 characters each.
			// Lines are padded with spaces, so we have to split them manually.
			std::vector<std::string> lines = Str2Lines(tempStr, 30);
			for (tempLng = 0; tempLng < lines.size(); tempLng ++)
				newTrk->AppendMetaEvent(0, 0x01, lines[tempLng].length(), lines[tempLng].c_str());
		}
		
		rcpInf.trkCnt = ReadLE16(infile);
		rcpInf.tickRes = ReadLE16(infile);
		rcpInf.tempoBPM = ReadLE16(infile);
		rcpInf.beatNum = fgetc(infile);
		rcpInf.beatDen = fgetc(infile);
		rcpInf.keySig = fgetc(infile);
		rcpInf.gblTransp = fgetc(infile);
		fseek(infile, 0x06, SEEK_CUR);	// skip dummy?
		fseek(infile, 0x10, SEEK_CUR);	// skip ??
		fseek(infile, 0x70, SEEK_CUR);	// skip ??
		
		// names of additional files
		fread(tempBuf, 0x01, 0x10, infile);	tempBuf[0x10] = '\0';
		rcpInf.gsdFile1 = RcpStr2StdStr(tempBuf);
		fread(tempBuf, 0x01, 0x10, infile);	tempBuf[0x10] = '\0';
		rcpInf.gsdFile2 = RcpStr2StdStr(tempBuf);
		fread(tempBuf, 0x01, 0x10, infile);	tempBuf[0x10] = '\0';
		rcpInf.cm6File = RcpStr2StdStr(tempBuf);
		
		fseek(infile, 0x50, SEEK_CUR);	// skip ??
		fseek(infile, 0x80 * 0x10, SEEK_CUR);	// skip rhythm definitions
	}
	
	// In MDPlayer/RCP.cs, allowed values for trkCnt are 18 and 36.
	// For RCP files, trkCnt == 0 is also allowed and makes it assume 36 tracks.
	// Invalid values result in undefined behaviour due to not setting the rcpVer variable.
	// For G36 files, invalid values fall back to 18 tracks.
	if (rcpInf.trkCnt == 0)
		rcpInf.trkCnt = 36;
	
	if (! rcpInf.cm6File.empty())
		initFiles.push_back(rcpInf.cm6File);
	if (! rcpInf.gsdFile1.empty())
		initFiles.push_back(rcpInf.gsdFile1);
	// TODO: tell main MIDI player somehow that this would go to Port B
	//if (! rcpInf.gsdFile2.empty())
	//	initFiles.push_back(rcpInf.gsdFile2);
	
	midFile.SetMidiFormat(1);
	midFile.SetMidiResolution(rcpInf.tickRes);
	
	tempLng = 60000000 / rcpInf.tempoBPM;
	tempBufU[0] = (tempLng >> 16) & 0xFF;
	tempBufU[1] = (tempLng >>  8) & 0xFF;
	tempBufU[2] = (tempLng >>  0) & 0xFF;
	newTrk->AppendMetaEvent(0, 0x51, 0x03, tempBufU);
	
	if (rcpInf.beatNum > 0 && rcpInf.beatDen > 0)
	{
		tempBufU[0] = rcpInf.beatNum;
		tempBufU[1] = val2shift(rcpInf.beatDen);
		tempBufU[2] = 6 << tempBufU[1];
		tempBufU[3] = 8;
		newTrk->AppendMetaEvent(0, 0x58, 0x04, tempBufU);
	}
	
	RcpKeySig2Mid(rcpInf.keySig, tempBufU);
	newTrk->AppendMetaEvent(0, 0x59, 0x02, tempBufU);
	
	newTrk->AppendEvent(0, 0xFF, 0x2F, 0x00);
	midFile.Track_Append(newTrk);
	
	// user SysEx data
	for (curTrk = 0; curTrk < 8; curTrk ++)
	{
		size_t syxLen;
		UserSysExData& tempUSyx = rcpInf.usrSyx[curTrk];
		
		fread(tempBuf, 0x01, 0x18, infile);	tempBuf[0x18] = '\0';
		tempUSyx.name = RcpStr2StdStr(tempBuf);
		
		fread(tempBufU, 0x01, 0x18, infile);
		for (syxLen = 0x00; syxLen < 0x18; syxLen ++)
		{
			if (tempBufU[syxLen] == 0xF7)
			{
				syxLen ++;
				break;
			}
		}
		tempUSyx.data = std::vector<UINT8>(tempBufU, tempBufU + syxLen);
	}
	
	retVal = 0x00;
	//midFile._tracks.reserve(rcpInf.trkCnt);
	for (curTrk = 0; curTrk < rcpInf.trkCnt; curTrk ++)
	{
		MidiTrack* newTrk = new MidiTrack;
		retVal = ReadRCPTrackAsMid(infile, &rcpInf, newTrk);
		if (retVal)
		{
			if (retVal == 0x01)
			{
				printf("Early EOF when trying to read track %u!\n", 1 + curTrk);
				retVal = 0x00;	// assume that early EOF is not an error (trkCnt may be wrong)
			}
			break;
		}
		
		newTrk->AppendEvent(0, 0xFF, 0x2F, 0x00);
		midFile.Track_Append(newTrk);
	}
	
	return retVal;
}

static UINT8 ReadRCPTrackAsMid(FILE* infile, const RCP_INFO* rcpInf, MidiTrack* trk)
{
	UINT32 trkBasePos;
	UINT32 trkEndPos;
	UINT32 trkLen;
	UINT32 parentPos;
	UINT8 trkID;
	UINT8 rhythmMode;
	UINT8 midiDev;
	INT8 transp;
	INT32 startTick;
	UINT8 trkMute;
	char tempBuf[0x40];
	UINT8* tempBufU = (UINT8*)tempBuf;
	std::string trkName;
	std::vector<UINT32> measurePos;
	std::vector<PlayingNote> playNotes;
	UINT16 curBar;
	UINT8 trkEnd;
	UINT8 midChn;
	UINT8 cmdType;
	UINT8 cmdP1;
	UINT8 cmdP2;
	UINT16 cmdP0Delay;
	UINT16 cmdDurat;
	UINT32 curDly;
	UINT32 curTick;
	UINT8 loopIdx;
	UINT32 loopPPos[8];
	UINT32 loopPos[8];
	UINT16 loopCnt[8];
	UINT32 loopTick[8];
	UINT8 gsParams[6];	// 0 device ID, 1 model ID, 2 address high, 3 address low
	UINT8 xgParams[6];	// 0 device ID, 1 model ID, 2 address high, 3 address low
	UINT8 chkSum;
	UINT32 tempoVal;
	UINT8 lastCmd;
	
	trkBasePos = ftell(infile);
	if (rcpInf->fileVer == 2)
	{
		trkLen = ReadLE16(infile);
		trkLen = (trkLen & ~0x03) | ((trkLen & 0x03) << 16);
	}
	else if (rcpInf->fileVer == 3)
	{
		trkLen = ReadLE32(infile);
	}
	if (feof(infile))
		return 0x01;
	trkEndPos = trkBasePos + trkLen;
	
	trkID = fgetc(infile);				// track ID
	rhythmMode = fgetc(infile);			// rhythm mode
	midChn = fgetc(infile);				// MIDI channel
	if (midChn == 0xFF)
	{
		midiDev = 0xFF;
		midChn = 0x00;
	}
	else
	{
		midiDev = midChn >> 4;
		midChn &= 0x0F;
	}
	transp = (INT8)fgetc(infile);		// transposition
	startTick = (INT8)fgetc(infile);	// start tick
	trkMute = fgetc(infile);			// mute
	curDly = fread(tempBuf, 0x01, 0x24, infile);	tempBuf[0x24] = '\0';
	if (curDly < 0x24)
		return 0x01;
	trkName = RcpStr2StdStr(tempBuf);
	
	if (! trkName.empty())
		trk->AppendMetaEvent(0, 0x03, trkName.length(), trkName.c_str());
	if (midiDev != 0xFF)
	{
		trk->AppendMetaEvent(0, 0x21, 1, &midiDev);	// Meta Event: MIDI Port Prefix
		trk->AppendMetaEvent(0, 0x20, 1, &midChn);	// Meta Event: MIDI Channel Prefix
	}
	if (rhythmMode != 0)
		printf("Warning: RCP Track %u: Rhythm Mode %u!\n", trkID, rhythmMode);
	if (transp & 0x80)
	{
		// bit 7 set = rhythm channel -> ignore transposition setting
		transp = 0;
	}
	else
	{
		transp = (transp & 0x40) ? (-0x80 + transp) : transp;	// 7-bit -> 8-bit sign extension
		transp += rcpInf->gblTransp;	// add global transposition
	}
	
	memset(gsParams, 0x00, 6);
	memset(xgParams, 0x00, 6);
	trkEnd = (trkMute == 0x01);	// EUPHO01.RCP has trkMute == 0x20 and the tracks need to play
	parentPos = 0x00;
	playNotes.clear();
	curTick = (UINT32)startTick;
	curDly = 0;
	// add "startTick" offset to initial delay
	if (startTick >= 0)
	{
		curDly += startTick;
		startTick = 0;
	}
	loopIdx = 0x00;
	measurePos.push_back(ftell(infile));
	curBar = 0;
	lastCmd = 0x00;
	while((UINT32)ftell(infile) < trkEndPos && ! feof(infile) && ! trkEnd)
	{
		UINT32 prevPos = ftell(infile);
		size_t curPN;
		UINT32 minDurat;
		
		if (rcpInf->fileVer == 2)
		{
			cmdType = fgetc(infile);
			cmdP0Delay = fgetc(infile);
			cmdP1 = fgetc(infile);
			cmdDurat = cmdP1;
			cmdP2 = fgetc(infile);
		}
		else if (rcpInf->fileVer == 3)
		{
			cmdType = fgetc(infile);
			cmdP2 = fgetc(infile);
			cmdP0Delay = ReadLE16(infile);
			cmdDurat = ReadLE16(infile);
			cmdP1 = (UINT8)cmdDurat;
		}
		if (cmdType < 0x80)
		{
			if (cmdType == 0)
				cmdDurat = 0;	// required by DQ5_64.RCP
			cmdType = (cmdType + transp) & 0x7F;
			// duration == 0 -> no note
			for (curPN = 0; curPN < playNotes.size(); curPN ++)
			{
				if (playNotes[curPN].note == cmdType)
				{
					// note already playing - set new length
					playNotes[curPN].len = cmdDurat;
					cmdDurat = 0;	// prevent adding note below
					break;
				}
			}
			if (cmdDurat > 0 && cmdP2 > 0 && midiDev != 0xFF)
			{
				trk->AppendEvent(curDly, 0x90 | midChn, cmdType, cmdP2);
				curDly = 0;
				
				PlayingNote pn;
				pn.note = cmdType;
				pn.len = cmdDurat;
				playNotes.push_back(pn);
			}
		}
		else
		{
			switch(cmdType)
			{
			case 0x90: case 0x91: case 0x92: case 0x93:	// send User SysEx (defined via header)
			case 0x94: case 0x95: case 0x96: case 0x97:
				if (midiDev == 0xFF)
					break;
				{
					std::vector<UINT8> syxBuf;
					UINT8 syxID;
					
					syxID = cmdType & 0x07;
					syxBuf = ProcessRcpSysEx(rcpInf->usrSyx[syxID].data, cmdP1, cmdP2, midChn);
					if (! syxBuf.empty())
						trk->AppendSysEx(curDly, syxBuf.size(), &syxBuf[0]);
					curDly = 0;
				}
				break;
			case 0x98:	// send SysEx
				{
					std::vector<UINT8> text;
					std::vector<UINT8> syxBuf;
					size_t curParam;
					
					cmdType = fgetc(infile);
					while(cmdType == 0xF7)
					{
						if (rcpInf->fileVer == 2)
						{
							fgetc(infile);	// skip "delay" byte
							for (curParam = 0; curParam < 2; curParam ++)
								text.push_back(fgetc(infile));
						}
						else if (rcpInf->fileVer == 3)
						{
							for (curParam = 0; curParam < 5; curParam ++)
								text.push_back(fgetc(infile));
						}
						cmdType = fgetc(infile);
					}
					fseek(infile, -1, SEEK_CUR);
					if (midiDev == 0xFF)
						break;
					
					syxBuf = ProcessRcpSysEx(text, cmdP1, cmdP2, midChn);
					if (! syxBuf.empty())
						trk->AppendSysEx(curDly, syxBuf.size(), &syxBuf[0]);
					curDly = 0;
				}
				break;
			//case 0x99:	// "OutsideProcessExec"?? (according to MDPlayer)
			//case 0xC0:	// DX7 Function
			//case 0xC1:	// DX Parameter
			//case 0xC2:	// DX RERF
			//case 0xC3:	// TX Function
			//case 0xC5:	// FB-01 P Parameter
			//case 0xC6:	// FB-01 S System
			//case 0xC7:	// TX81Z V VCED
			//case 0xC8:	// TX81Z A ACED
			//case 0xC9:	// TX81Z P PCED
			//case 0xCA:	// TX81Z S System
			//case 0xCB:	// TX81Z E EFFECT
			//case 0xCC:	// DX7-2 R Remote SW
			//case 0xCD:	// DX7-2 A ACED
			//case 0xCE:	// DX7-2 P PCED
			//case 0xCF:	// TX802 P PCED
			case 0xD0:	// YAMAHA Base Address
				xgParams[2] = cmdP1;
				xgParams[3] = cmdP2;
				break;
			case 0xD1:	// YAMAHA Device Data
				xgParams[0] = cmdP1;
				xgParams[1] = cmdP2;
				break;
			case 0xD2:	// YAMAHA Address / Parameter
				xgParams[4] = cmdP1;
				xgParams[5] = cmdP2;
				if (midiDev == 0xFF)
					break;
				
				tempBufU[0] = 0x43;	// YAMAHA ID
				memcpy(&tempBufU[1], &xgParams[0], 6);
				tempBufU[7] = 0xF7;
				trk->AppendSysEx(curDly, 8, tempBufU);
				curDly = 0;
				break;
			case 0xD3:	// YAMAHA XG Address / Parameter
				xgParams[4] = cmdP1;
				xgParams[5] = cmdP2;
				if (midiDev == 0xFF)
					break;
				
				tempBufU[0] = 0x43;	// YAMAHA ID
				tempBufU[1] = 0x10;	// Parameter Change
				tempBufU[2] = 0x4C;	// XG
				memcpy(&tempBufU[3], &xgParams[2], 4);
				tempBufU[7] = 0xF7;
				trk->AppendSysEx(curDly, 8, tempBufU);
				curDly = 0;
				break;
			//case 0xDC:	// MKS-7
			case 0xDD:	// Roland Base Address
				gsParams[2] = cmdP1;
				gsParams[3] = cmdP2;
				break;
			case 0xDE:	// Roland Parameter
				gsParams[4] = cmdP1;
				gsParams[5] = cmdP2;
				if (midiDev == 0xFF)
					break;
				
				tempBufU[0] = 0x41;	// Roland ID
				tempBufU[1] = gsParams[0];
				tempBufU[2] = gsParams[1];
				tempBufU[3] = 0x12;
				chkSum = 0x00;	// initialize checksum
				for (cmdP1 = 0; cmdP1 < 4; cmdP1 ++)
				{
					tempBufU[4 + cmdP1] = gsParams[2 + cmdP1];
					chkSum += gsParams[2 + cmdP1];	// add to checksum
				}
				tempBufU[8] = (-chkSum) & 0x7F;
				tempBufU[9] = 0xF7;
				trk->AppendSysEx(curDly, 10, tempBufU);
				curDly = 0;
				break;
			case 0xDF:	// Roland Device
				gsParams[0] = cmdP1;
				gsParams[1] = cmdP2;
				break;
			case 0xE1:	// set XG instrument
				if (midiDev == 0xFF)
					break;
				trk->AppendEvent(curDly, 0xB0 | midChn, 0x20, cmdP2);
				trk->AppendEvent(0, 0xC0 | midChn, cmdP1, 0x00);
				curDly = 0;
				break;
			case 0xE2:	// set GS instrument
				if (midiDev == 0xFF)
					break;
				trk->AppendEvent(curDly, 0xB0 | midChn, 0x00, cmdP2);
				trk->AppendEvent(0, 0xC0 | midChn, cmdP1, 0x00);
				curDly = 0;
				break;
			case 0xE5:	// "Key Scan"
				printf("Key Scan command found! Offset %04X\n", prevPos);
				break;
			case 0xE6:	// MIDI channel
				cmdP1 --;
				if (cmdP1 == 0xFF)
				{
					midiDev = 0xFF;
					midChn = 0x00;
				}
				else
				{
					midiDev = cmdP1 >> 4;	// port ID
					midChn = cmdP1 & 0x0F;	// channel ID
					trk->AppendMetaEvent(curDly, 0x21, 1, &midiDev);	// Meta Event: MIDI Port Prefix
					trk->AppendMetaEvent(0, 0x20, 1, &midChn);			// Meta Event: MIDI Channel Prefix
					curDly = 0;
				}
				break;
			case 0xE7:	// Tempo Modifier
				if (cmdP2)
					printf("Warning: Interpolated Tempo Change at 0x%04X!\n", prevPos);
				tempoVal = (UINT32)(60000000.0 / (rcpInf->tempoBPM * cmdP1 / 64.0) + 0.5);
				tempBufU[0] = (tempoVal >> 16) & 0xFF;
				tempBufU[1] = (tempoVal >>  8) & 0xFF;
				tempBufU[2] = (tempoVal >>  0) & 0xFF;
				trk->AppendMetaEvent(curDly, 0x51, 0x03, tempBufU);
				curDly = 0;
				break;
			case 0xEA:	// Channel Aftertouch
				if (midiDev == 0xFF)
					break;
				trk->AppendEvent(curDly, 0xD0 | midChn, cmdP1, 0x00);
				curDly = 0;
				break;
			case 0xEB:	// Control Change
				if (midiDev == 0xFF)
					break;
				trk->AppendEvent(curDly, 0xB0 | midChn, cmdP1, cmdP2);
				curDly = 0;
				break;
			case 0xEC:	// Instrument
				if (midiDev == 0xFF)
					break;
				if (cmdP1 < 0x80)
				{
					trk->AppendEvent(curDly, 0xC0 | midChn, cmdP1, 0x00);
					curDly = 0;
				}
				else if (cmdP1 < 0xC0 && (midChn >= 1 && midChn < 9))
				{
					// set MT-32 instrument
					memcpy(tempBufU, MT32_PATCH_CHG, 0x10);
					tempBufU[0x06] = (midChn - 1) << 4;
					tempBufU[0x07] = (cmdP1 >> 6) & 0x03;
					tempBufU[0x08] = (cmdP1 >> 0) & 0x3F;
					chkSum = 0x00;	// initialize checksum
					for (cmdP1 = 0x04; cmdP1 < 0x0E; cmdP1 ++)
						chkSum += tempBufU[cmdP1];	// add to checksum
					tempBufU[0x0E] = (-chkSum) & 0x7F;
					trk->AppendSysEx(curDly, 0x10, tempBufU);
					curDly = 0;
				}
				break;
			case 0xED:	// Note Aftertouch
				if (midiDev == 0xFF)
					break;
				trk->AppendEvent(curDly, 0xA0 | midChn, cmdP1, cmdP2);
				curDly = 0;
				break;
			case 0xEE:	// Pitch Bend
				if (midiDev == 0xFF)
					break;
				trk->AppendEvent(curDly, 0xE0 | midChn, cmdP1, cmdP2);
				curDly = 0;
				break;
			case 0xF5:	// Key Signature Change
				printf("Warning: Key Signature Change at 0x%04X!\n", prevPos);
				RcpKeySig2Mid((UINT8)cmdP0Delay, tempBufU);
				trk->AppendMetaEvent(curDly, 0x59, 0x02, tempBufU);
				curDly = 0;
				cmdP0Delay = 0;
				break;
			case 0xF6:	// comment
				{
					std::string text;
					UINT8 curParam;
					
					if (rcpInf->fileVer == 2)
					{
						text.push_back((char)cmdP1);
						text.push_back((char)cmdP2);
					}
					else if (rcpInf->fileVer == 3)
					{
						text.push_back((char)cmdP2);
						text.push_back((cmdP0Delay >> 0) & 0xFF);
						text.push_back((cmdP0Delay >> 8) & 0xFF);
						text.push_back((cmdDurat >> 0) & 0xFF);
						text.push_back((cmdDurat >> 8) & 0xFF);
					}
					
					cmdType = fgetc(infile);
					while(cmdType == 0xF7)
					{
						if (rcpInf->fileVer == 2)
						{
							fgetc(infile);	// skip "delay" byte
							for (curParam = 0; curParam < 2; curParam ++)
								text.push_back(fgetc(infile));
						}
						else if (rcpInf->fileVer == 3)
						{
							for (curParam = 0; curParam < 5; curParam ++)
								text.push_back(fgetc(infile));
						}
						cmdType = fgetc(infile);
					}
					fseek(infile, -1, SEEK_CUR);
					
					RTrimChar(text, ' ', false);
					trk->AppendMetaEvent(curDly, 0x01, text.length(), text.c_str());
					curDly = 0;
					cmdP0Delay = 0;
					break;
				}
			case 0xF7:	// continuation of previous command
				printf("Error: Unexpected continuation command at 0x%04X!\n", prevPos);
				break;
			case 0xF8:	// Loop End
				if (loopIdx == 0)
				{
					printf("Warning: Loop End without Loop Start at 0x%04X!\n", prevPos);
				}
				else
				{
					bool takeLoop;
					UINT32 loopTicks;
					UINT32 loopBeats;
					UINT32 lbLimit = (rcpInf->beatNum ? rcpInf->beatNum : 4) * 500;
					
					takeLoop = false;
					loopIdx --;
					loopCnt[loopIdx] ++;
					loopTicks = (curTick - loopTick[loopIdx]) / loopCnt[loopIdx];
					loopBeats = loopTicks / rcpInf->tickRes;
					if (cmdP0Delay == 0 || (loopBeats * cmdP0Delay) >= lbLimit)
					{
						// infinite loop
						//trk->AppendEvent(curDly, 0xB0 | midChn, 0x6F, (UINT8)loopCnt);
						//curDly = 0;
						
						if (loopCnt[loopIdx] < NUM_LOOPS)
							takeLoop = true;
					}
					else
					{
						if (loopCnt[loopIdx] < cmdP0Delay)
							takeLoop = true;
					}
					if (takeLoop)
					{
						parentPos = loopPPos[loopIdx];
						fseek(infile, loopPos[loopIdx], SEEK_SET);
						loopIdx ++;
					}
				}
				cmdP0Delay = 0;
				break;
			case 0xF9:	// Loop Start
				//trk->AppendEvent(curDly, 0xB0 | midChn, 0x6F, 0);
				//curDly = 0;
				if (loopIdx >= 8)
				{
					printf("Error: Trying to do more than 8 nested loops at 0x%04X!\n", prevPos);
				}
				else
				{
					loopPPos[loopIdx] = parentPos;	// required by YS-2･018.RCP
					loopPos[loopIdx] = ftell(infile);
					loopCnt[loopIdx] = 0;
					loopTick[loopIdx] = curTick;
					if (loopIdx > 0 && loopPos[loopIdx] == loopPos[loopIdx - 1])
						loopIdx --;	// ignore loop command (required by YS-2･018.RCP)
					loopIdx ++;
				}
				cmdP0Delay = 0;
				break;
			case 0xFC:	// repeat previous measure
				if (lastCmd != 0xFC && parentPos)
				{
					printf("Warning Track %u: Leaving recursive Repeat Measure at 0x%04X!\n", trkID, prevPos);
					fseek(infile, parentPos, SEEK_SET);
					parentPos = 0x00;
					cmdP0Delay = 0;
				}
				else
				{
					UINT16 measureID;
					UINT32 repeatPos;
					UINT32 cachedPos;
					
					if (rcpInf->fileVer == 2)
					{
						measureID = cmdP0Delay;
						repeatPos = (cmdP2 << 8) | ((cmdP1 & ~0x03) << 0);
					}
					else if (rcpInf->fileVer == 3)
					{
						measureID = cmdP0Delay;
						// cmdDurat == command ID, I have no idea why the first command has ID 0x30
						repeatPos = 0x002E + (cmdDurat - 0x0030) * 0x06;
					}
					cmdP0Delay = 0;
					
					//sprintf(tempBuf, "Repeat Bar %u", measureID);
					//trk->AppendMetaEvent(curDly, 0x06, strlen(tempBuf), tempBuf);
					//curDly = 0;
					
					if (measureID >= measurePos.size())
					{
						printf("Warning: Trying to repeat invalid bar %u (have %u bars) at 0x%04X!\n",
							measureID, curBar + 1, prevPos);
						break;
					}
					if (parentPos == ftell(infile))
						break;
					cachedPos = measurePos[measureID] - trkBasePos;
					//if (cachedPos != repeatPos)
					//	printf("Warning: Repeat Measure %u: offset mismatch (0x%04X != 0x%04X) at 0x%04X!\n",
					//		measureID, repeatPos, cachedPos, prevPos);
					
					if (! parentPos)	// this check was verified to be necessary for some files
						parentPos = ftell(infile);
					fseek(infile, trkBasePos + repeatPos, SEEK_SET);	// YS3-25.RCP relies on this
				}
				break;
			case 0xFD:	// measure end
				if (curBar >= 0x8000)	// prevent infinite loops
				{
					trkEnd = 1;
					break;
				}
				//sprintf(tempBuf, "Bar %u", 1 + curBar);
				//trk->AppendMetaEvent(curDly, 0x06, strlen(tempBuf), tempBuf);
				//curDly = 0;
				
				if (parentPos)
				{
					fseek(infile, parentPos, SEEK_SET);
					parentPos = 0x00;
				}
				measurePos.push_back(ftell(infile));
				curBar ++;
				cmdP0Delay = 0;
				break;
			case 0xFE:	// track end
				trkEnd = 1;
				cmdP0Delay = 0;
				break;
			default:
				printf("Unhandled RCP command 0x%02X at position 0x%04X!\n", cmdType, prevPos);
				break;
			}	// end switch(cmdType)
		}	// end if (cmdType >= 0x80)
		
		lastCmd = cmdType;
		do
		{
			if (playNotes.empty())
				break;	// minor speed-optimization
			
			minDurat = cmdP0Delay;
			for (curPN = 0; curPN < playNotes.size(); curPN ++)
			{
				if (playNotes[curPN].len < minDurat)
					minDurat = playNotes[curPN].len;
			}
			
			for (curPN = 0; curPN < playNotes.size(); curPN ++)
				playNotes[curPN].len -= minDurat;
			cmdP0Delay -= minDurat;
			curDly += minDurat;
			curTick += minDurat;
			
			for (curPN = 0; curPN < playNotes.size(); curPN ++)
			{
				if (playNotes[curPN].len == 0)
				{
					trk->AppendEvent(curDly, 0x90 | midChn, playNotes[curPN].note, 0x00);
					curDly = 0;
					playNotes.erase(playNotes.begin() + curPN);
					curPN --;
				}
			}
		} while(minDurat > 0 && cmdP0Delay > 0);
		curDly += cmdP0Delay;
		curTick += cmdP0Delay;
		
		// remove ticks from curDly from all events until startTicks reaches 0
		if (startTick < 0 && curDly > 0)
		{
			startTick += curDly;
			if (startTick >= 0)
			{
				curDly = startTick;
				startTick = 0;
			}
			else
			{
				curDly = 0;
			}
		}
	}
	
	while(! playNotes.empty())
	{
		size_t curPN;
		UINT32 minDurat = playNotes[0].len;
		for (curPN = 1; curPN < playNotes.size(); curPN ++)
		{
			if (playNotes[curPN].len < minDurat)
				minDurat = playNotes[curPN].len;
		}
		
		for (curPN = 0; curPN < playNotes.size(); curPN ++)
			playNotes[curPN].len -= minDurat;
		curDly += minDurat;
		
		for (curPN = 0; curPN < playNotes.size(); curPN ++)
		{
			if (playNotes[curPN].len == 0)
			{
				trk->AppendEvent(curDly, 0x90 | midChn, playNotes[curPN].note, 0x00);
				curDly = 0;
				playNotes.erase(playNotes.begin() + curPN);
				curPN --;
			}
		}
	}
	
	fseek(infile, trkEndPos, SEEK_SET);
	return 0x00;
}

static void WriteRolandSyxData(std::vector<UINT8>& buffer, const UINT8* syxHdr, UINT32 address, UINT32 len, const UINT8* data)
{
	UINT32 pos = buffer.size();
	
	buffer.resize(pos + 0x0A + len);
	buffer[pos + 0x00] = 0xF0;
	buffer[pos + 0x01] = syxHdr[0];
	buffer[pos + 0x02] = syxHdr[1];
	buffer[pos + 0x03] = syxHdr[2];
	buffer[pos + 0x04] = syxHdr[3];
	buffer[pos + 0x05] = (address >> 16) & 0x7F;
	buffer[pos + 0x06] = (address >>  8) & 0x7F;
	buffer[pos + 0x07] = (address >>  0) & 0x7F;
	memcpy(&buffer[pos + 0x08], data, len);
	
	UINT8 chkSum = 0x00;
	UINT32 curPos;
	for (curPos = 0x05; curPos < 0x08 + len; curPos ++)
		chkSum += buffer[pos + curPos];
	
	buffer[pos + len + 0x08] = (-chkSum) & 0x7F;
	buffer[pos + len + 0x09] = 0xF7;
	
	return;
}

static void WriteRolandSyxBulk(std::vector<UINT8>& buffer, const UINT8* syxHdr, UINT32 address, UINT32 len, const UINT8* data, UINT32 bulkSize)
{
	UINT32 curPos;
	UINT32 wrtBytes;
	UINT32 curAddr;
	UINT32 syxAddr;
	
	curAddr = ((address & 0x00007F) >> 0) |
				((address & 0x007F00) >> 1) |
				((address & 0x7F0000) >> 2);
	for (curPos = 0x00; curPos < len; )
	{
		wrtBytes = len - curPos;
		if (wrtBytes > bulkSize)
			wrtBytes = bulkSize;
		syxAddr = ((curAddr & 0x00007F) << 0) |
					((curAddr & 0x003F80) << 1) |
					((curAddr & 0x1FC000) << 2);
		WriteRolandSyxData(buffer, syxHdr, syxAddr, wrtBytes, &data[curPos]);
		curPos += wrtBytes;
		curAddr += wrtBytes;
	}
	
	return;
}

UINT8 Cm62Syx(const char* fileName, std::vector<UINT8>& syxData)
{
	FILE* infile;
	UINT8 retVal;
	
	infile = fopen(fileName, "rb");
	if (infile == NULL)
		return 0xFF;
	
	retVal = Cm62Syx(infile, syxData);
	fclose(infile);
	
	return retVal;
}

UINT8 Cm62Syx(FILE* infile, std::vector<UINT8>& syxData)
{
	static const UINT8 MT32_SYX_HDR[4] = {0x41, 0x10, 0x16, 0x12};
	std::vector<UINT8> cm6Data;
	char tempBuf[0x80];
	
	fread(tempBuf, 0x01, 0x20, infile);
	if (strcmp(&tempBuf[0x00], "COME ON MUSIC"))
		return 0x10;
	if (memcmp(&tempBuf[0x0E], "\0\0R ", 0x04))
		return 0x10;
	
	fseek(infile, 0, SEEK_END);
	cm6Data.resize(ftell(infile));
	fseek(infile, 0, SEEK_SET);
	if (cm6Data.size() < 0x5849)
		return 0xF8;	// file too small
	fread(&cm6Data[0], 0x01, cm6Data.size(), infile);
	
	syxData.clear();
	UINT8 deviceType = cm6Data[0x001A];
	printf("Loading CM6 Control File, %s mode\n", deviceType ? "CM-64" : "MT-32");
	// comment
	memcpy(tempBuf, &cm6Data[0x0040], 0x40);	tempBuf[0x40] = '\0';
	
	//ReadRcpStr(&cm6Inf.comment, 0x40, &cm6Data[0x0040]);
	WriteRolandSyxData(syxData, MT32_SYX_HDR, 0x7F0000, 0x00, NULL);	// MT-32/CM-32P Reset
	WriteRolandSyxData(syxData, MT32_SYX_HDR, 0x100000, 0x17, &cm6Data[0x0080]);	// MT-32 System
	WriteRolandSyxBulk(syxData, MT32_SYX_HDR, 0x050000, 0x400, &cm6Data[0x0A34], 0x100);	// MT-32 Patch Memory
	WriteRolandSyxBulk(syxData, MT32_SYX_HDR, 0x080000, 0x4000, &cm6Data[0x0E34], 0x100);	// MT-32 Timbre Memory
	//WriteRolandSyxBulk(syxData, MT32_SYX_HDR, 0x040000, 0x7B0, &cm6Data[0x0130], 0x100);	// MT-32 Timbre Temporary
	//WriteRolandSyxData(syxData, MT32_SYX_HDR, 0x030000, 0x90, &cm6Data[0x00A0]);	// MT-32 Patch Temporary
	WriteRolandSyxBulk(syxData, MT32_SYX_HDR, 0x030110, 0x154, &cm6Data[0x0130], 0x100);	// MT-32 Rhythm Setup
	
	if (deviceType > 0)
	{
		WriteRolandSyxData(syxData, MT32_SYX_HDR, 0x520000, 0x11, &cm6Data[0x5832]);	// CM-32P System
		WriteRolandSyxBulk(syxData, MT32_SYX_HDR, 0x510000, 0x980, &cm6Data[0x4EB2], 0x100);	// CM-32P Patch Memory
		//WriteRolandSyxData(syxData, MT32_SYX_HDR, 0x500000, 0x7E, &cm6Data[0x4E34]);	// CM-32P Patch Temporary
	}
	
	return 0x00;
}

// nibbilize, high-low order
static void Bytes2NibblesHL(UINT32 bytes, UINT8* nibData, const UINT8* byteData)
{
	UINT32 curPos;
	
	for (curPos = 0x00; curPos < bytes; curPos ++)
	{
		nibData[curPos * 2 + 0] = (byteData[curPos] >> 4) & 0x0F;
		nibData[curPos * 2 + 1] = (byteData[curPos] >> 0) & 0x0F;
	}
	
	return;
}

static void GsdPartParam2BulkDump(UINT8* bulkData, const UINT8* partData)
{
	UINT8 partMem[0x70];
	UINT8 curPos;
	UINT8 curCtrl;
	
	partMem[0x00] = partData[0x00];	// Bank MSB
	partMem[0x01] = partData[0x01];	// tone number
	
	// Rx. Pitch Bend/Ch. Pressure/Program Change/Control Change/Poly Pressure/Note Message/RPN/NRPN
	partMem[0x02] = 0x00;
	for (curPos = 0; curPos < 8; curPos ++)
		partMem[0x02] |= (partData[0x03 + curPos] & 0x01) << (7 - curPos);
	// Rx. Modulation/Volume/Panpot/Expression/Hold 1 (Sustain)/Portamento/SostenutoSoft Pedal
	partMem[0x03] = 0x00;
	for (curPos = 0; curPos < 8; curPos ++)
		partMem[0x03] |= (partData[0x0B + curPos] & 0x01) << (7 - curPos);
	partMem[0x04] = partData[0x02];	// Rx. Channel
	
	partMem[0x05] = (partData[0x13] & 0x01) << 7;	// Mono/Poly Mode
	partMem[0x05] |= ((partData[0x15] & 0x03) << 5) | (partData[0x15] ? 0x10 : 0x00);	// Rhythm Part Mode
	partMem[0x05] |= (partData[0x14] & 0x03) << 0;	// Assign Mode
	
	partMem[0x06] = partData[0x16];	// Pitch Key Shift
	partMem[0x07] = ((partData[0x17] & 0x0F) << 4) | ((partData[0x18] & 0x0F) << 0);	// Pitch Offset Fine
	partMem[0x08] = partData[0x19];	// Part Level
	partMem[0x09] = partData[0x1C];	// Part Panpot
	partMem[0x0A] = partData[0x1B];	// Velocity Sense Offset
	partMem[0x0B] = partData[0x1A];	// Velocity Sense Depth
	partMem[0x0C] = partData[0x1D];	// Key Range Low
	partMem[0x0D] = partData[0x1E];	// Key Range High
	
	// Chorus Send Depth/Reverb Send Depth/Tone Modify 1-8
	for (curPos = 0x00; curPos < 0x0A; curPos ++)
		partMem[0x0E + curPos] = partData[0x21 + curPos];
	partMem[0x18] = 0x00;
	partMem[0x19] = 0x00;
	// Scale Tuning C to B
	for (curPos = 0x00; curPos < 0x0C; curPos ++)
		partMem[0x1A + curPos] = partData[0x2B + curPos];
	partMem[0x26] = partData[0x1F];	// CC1 Controller Number
	partMem[0x27] = partData[0x20];	// CC2 Controller Number
	
	// Destination Controllers
	for (curCtrl = 0; curCtrl < 6; curCtrl ++)
	{
		UINT8 srcPos = 0x37 + curCtrl * 0x0B;
		UINT8 dstPos = 0x28 + curCtrl * 0x0C;
		for (curPos = 0x00; curPos < 0x03; curPos ++)
			partMem[dstPos + 0x00 + curPos] = partData[srcPos + 0x00 + curPos];
		partMem[dstPos + 0x03] = (curCtrl == 2 || curCtrl == 3) ? 0x40 : 0x00;	// verified with Recomposer 3.0 PC-98
		for (curPos = 0x00; curPos < 0x08; curPos ++)
			partMem[dstPos + 0x04 + curPos] = partData[srcPos + 0x03 + curPos];
	}
	
	Bytes2NibblesHL(0x70, bulkData, partMem);
	return;
}

UINT8 Gsd2Syx(const char* fileName, std::vector<UINT8>& syxData)
{
	FILE* infile;
	UINT8 retVal;
	
	infile = fopen(fileName, "rb");
	if (infile == NULL)
		return 0xFF;
	
	retVal = Gsd2Syx(infile, syxData);
	fclose(infile);
	
	return retVal;
}

UINT8 Gsd2Syx(FILE* infile, std::vector<UINT8>& syxData)
{
	static const UINT8 SC55_SYX_HDR[4] = {0x41, 0x10, 0x42, 0x12};
	static const UINT8 PART2CHN[0x10] =
		{0x09, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
	static const UINT8 CHN2PART[0x10] =
		{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
	std::vector<UINT8> gsdData;
	char tempBuf[0x20];
	UINT8 voiceRes[0x10];
	UINT8 bulkBuffer[0x100];
	UINT8 curChn;
	
	fread(tempBuf, 0x01, 0x20, infile);
	if (strcmp(&tempBuf[0x00], "COME ON MUSIC"))
		return 0x10;
	if (strcmp(&tempBuf[0x0E], "GS CONTROL 1.0"))
		return 0x10;
	
	fseek(infile, 0, SEEK_END);
	gsdData.resize(ftell(infile));
	fseek(infile, 0, SEEK_SET);
	if (gsdData.size() < 0x0A71)
		return 0xF8;	// file too small
	fread(&gsdData[0], 0x01, gsdData.size(), infile);
	
	syxData.clear();
	printf("Loading GSD Control File\n");
	
	curChn = 0x00;
	WriteRolandSyxData(syxData, SC55_SYX_HDR, 0x40007F, 0x01, &curChn);	// SC-55 Reset
	// Recomposer 3.0 sends Master Volume (40 00 04), Key-Shift (40 00 06) and Pan (via GM SysEx) separately,
	// but doing a bulk-dump works just fine on SC-55/88.
	// The SC-55 requires Master Tune (40 00 00) to be sent separately though or it will throw a "DT1 Data Error".
	WriteRolandSyxData(syxData, SC55_SYX_HDR, 0x400000, 0x04, &gsdData[0x0020]);	// Master Tune
	WriteRolandSyxData(syxData, SC55_SYX_HDR, 0x400004, 0x03, &gsdData[0x0024]);	// Common Settings
	for (curChn = 0x00; curChn < 0x10; curChn ++)
		voiceRes[curChn] = gsdData[0x0036 + PART2CHN[curChn] * 0x7A + 0x79];
	WriteRolandSyxData(syxData, SC55_SYX_HDR, 0x400110, 0x10, voiceRes);	// Voice Reserve
	WriteRolandSyxData(syxData, SC55_SYX_HDR, 0x400130, 0x07, &gsdData[0x0027]);	// Reverb Settings
	WriteRolandSyxData(syxData, SC55_SYX_HDR, 0x400138, 0x08, &gsdData[0x002E]);	// Chorus Settings
	
	// Part Settings
	for (curChn = 0x00; curChn < 0x10; curChn ++)
	{
		UINT32 addrOfs = 0x90 + CHN2PART[curChn] * 0xE0;
		UINT32 syxAddr = ((addrOfs & 0x007F) << 0) | ((addrOfs & 0x3F80) << 1);
		GsdPartParam2BulkDump(bulkBuffer, &gsdData[0x0036 + curChn * 0x7A]);
		WriteRolandSyxBulk(syxData, SC55_SYX_HDR, 0x480000 | syxAddr, 0xE0, bulkBuffer, 0x80);
	}
	
	// Drum Setup
	for (curChn = 0; curChn < 2; curChn ++)	// 2 drum maps
	{
		// drum level, pan, reverb, chorus
		static const UINT8 DRMPAR_ADDR[4] = {0x02, 0x06, 0x08, 0x0A};
		const UINT8* drmPtr = &gsdData[0x07D6 + curChn * 0x014C];
		UINT8 curNote;
		UINT8 curParam;
		UINT8 paramBuf[0x80];
		
		for (curParam = 0; curParam < 4; curParam ++)
		{
			UINT32 syxAddr = (curChn << 12) | (DRMPAR_ADDR[curParam] << 8);
			
			memset(paramBuf, 0x00, 0x80);
			for (curNote = 0x00; curNote < 82; curNote ++)
				paramBuf[27 + curNote] = drmPtr[curNote * 4 + curParam];
			Bytes2NibblesHL(0x80, bulkBuffer, paramBuf);
			WriteRolandSyxBulk(syxData, SC55_SYX_HDR, 0x490000 | syxAddr, 0x100, bulkBuffer, 0x80);
		}
	}
	
	return 0x00;
}


static UINT16 ReadLE16(FILE* infile)
{
	UINT8 data[0x02];
	
	fread(data, 0x02, 1, infile);
	return (data[0x00] << 0) | (data[0x01] << 8);
}

static UINT32 ReadLE32(FILE* infile)
{
	UINT8 data[0x04];
	
	fread(data, 0x04, 1, infile);
	return	(data[0x00] <<  0) | (data[0x01] <<  8) |
			(data[0x02] << 16) | (data[0x03] << 24);
}
