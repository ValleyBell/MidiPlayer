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
	UINT8 playBias;
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
//UINT8 LoadRCPAsMidi(const char* fileName, MidiFile& midFile);
//UINT8 LoadRCPAsMidi(FILE* infile, MidiFile& midFile);
static UINT8 ReadRCPTrackAsMid(FILE* infile, const RCP_INFO* rcpInf, MidiTrack* trk);
static UINT16 ReadLE16(FILE* infile);
static UINT32 ReadLE32(FILE* infile);

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
				data = (0x100 - chkSum) & 0x7F;
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
	
	return syxBuf;
}

UINT8 LoadRCPAsMidi(const char* fileName, MidiFile& midFile)
{
	FILE* infile;
	UINT8 retVal;
	
	infile = fopen(fileName, "rb");
	if (infile == NULL)
		return 0xFF;
	
	retVal = LoadRCPAsMidi(infile, midFile);
	fclose(infile);
	
	return retVal;
}

UINT8 LoadRCPAsMidi(FILE* infile, MidiFile& midFile)
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
		rcpInf.playBias = fgetc(infile);
		
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
		rcpInf.playBias = fgetc(infile);
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
	
	midFile.SetMidiFormat(1);
	midFile.SetMidiResolution(rcpInf.tickRes);
	
	tempLng = 60000000 / rcpInf.tempoBPM;
	tempBufU[0] = (tempLng >> 16) & 0xFF;
	tempBufU[1] = (tempLng >>  8) & 0xFF;
	tempBufU[2] = (tempLng >>  0) & 0xFF;
	newTrk->AppendMetaEvent(0, 0x51, 0x03, tempBufU);
	
	tempBufU[0] = rcpInf.beatNum;
	tempBufU[1] = val2shift(rcpInf.beatDen);
	tempBufU[2] = 6 << tempBufU[1];
	tempBufU[3] = 8;
	newTrk->AppendMetaEvent(0, 0x58, 0x04, tempBufU);
	
	RcpKeySig2Mid(rcpInf.keySig, tempBufU);
	newTrk->AppendMetaEvent(0, 0x59, 0x02, tempBufU);
	
	if (rcpInf.playBias)
		printf("Warning: PlayBIAS == %u!\n", rcpInf.playBias);
	
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
	UINT8 transp;
	INT8 startTick;
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
	UINT8 loopIdx;
	UINT32 loopPos[8];
	UINT16 loopCnt[8];
	UINT8 gsParams[6];	// 0 device ID, 1 model ID, 2 address high, 3 address low
	UINT8 xgParams[6];	// 0 device ID, 1 model ID, 2 address high, 3 address low
	UINT8 chkSum;
	UINT32 tempoVal;
	
	trkBasePos = ftell(infile);
	if (rcpInf->fileVer == 2)
		trkLen = ReadLE16(infile);
	else if (rcpInf->fileVer == 3)
		trkLen = ReadLE32(infile);
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
	transp = fgetc(infile);				// transposition
	startTick = (INT8)fgetc(infile);	// start tick
	trkMute = fgetc(infile);			// mute
	fread(tempBuf, 0x01, 0x24, infile);	tempBuf[0x24] = '\0';
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
	if (transp > 0x80)
	{
		// known values are: 0x00..0x3F (+0 .. +63), 0x40..0x7F (-64 .. -1), 0x80 (drums)
		printf("Warning: RCP Track %u: Key 0x%02X!\n", trkID, transp);
		transp = 0x00;
	}
	if (startTick != 0)
		printf("Warning: RCP Track %u: Start Tick %+d!\n", trkID, startTick);
	
	memset(gsParams, 0x00, 6);
	memset(xgParams, 0x00, 6);
	trkEnd = 0;
	parentPos = 0x00;
	playNotes.clear();
	curDly = 0;
	loopIdx = 0x00;
	measurePos.push_back(ftell(infile));
	curBar = 0;
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
			if (cmdDurat > 0)
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
				{
					std::vector<UINT8> syxBuf;
					UINT8 syxID;
					
					syxID = cmdType & 0x07;
					syxBuf = ProcessRcpSysEx(rcpInf->usrSyx[syxID].data, cmdP1, cmdP2, midChn);
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
					
					syxBuf = ProcessRcpSysEx(text, cmdP1, cmdP2, midChn);
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
				
				tempBufU[0] = 0x43;	// YAMAHA ID
				memcpy(&tempBufU[1], &xgParams[0], 6);
				tempBufU[7] = 0xF7;
				trk->AppendSysEx(curDly, 8, tempBufU);
				curDly = 0;
				break;
			case 0xD3:	// YAMAHA XG Address / Parameter
				xgParams[4] = cmdP1;
				xgParams[5] = cmdP2;
				
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
				tempBufU[8] = (0x100 - chkSum) & 0x7F;
				tempBufU[9] = 0xF7;
				trk->AppendSysEx(curDly, 10, tempBufU);
				curDly = 0;
				break;
			case 0xDF:	// Roland Device
				gsParams[0] = cmdP1;
				gsParams[1] = cmdP2;
				break;
			case 0xE2:	// set GS instrument
				trk->AppendEvent(curDly, 0xB0 | midChn, 0x00, cmdP2);
				trk->AppendEvent(0, 0xC0 | midChn, cmdP1, 0x00);
				curDly = 0;
				break;
			case 0xE5:	// "Key Scan"
				printf("Key Scan command found! Offset %04X\n", prevPos);
				break;
			case 0xE6:	// MIDI channel
				cmdP1 --;
				if (cmdP1 != 0xFF)
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
				trk->AppendEvent(curDly, 0xD0 | midChn, cmdP1, 0x00);
				curDly = 0;
				break;
			case 0xEB:	// Control Change
				trk->AppendEvent(curDly, 0xB0 | midChn, cmdP1, cmdP2);
				curDly = 0;
				break;
			case 0xEC:	// Instrument
				trk->AppendEvent(curDly, 0xC0 | midChn, cmdP1, 0x00);
				curDly = 0;
				break;
			case 0xED:	// Note Aftertouch
				trk->AppendEvent(curDly, 0xA0 | midChn, cmdP1, cmdP2);
				curDly = 0;
				break;
			case 0xEE:	// Pitch Bend
				trk->AppendEvent(curDly, 0xE0 | midChn, cmdP1, cmdP2);
				curDly = 0;
				break;
			case 0xF5:	// Key Signature Change
				// TODO: find a file that uses this
				printf("Warning: Key Signature Change at 0x%04X!\n", prevPos);
				RcpKeySig2Mid((UINT8)cmdP0Delay, tempBufU);
				trk->AppendMetaEvent(0, 0x59, 0x02, tempBufU);
				curDly = 0;
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
					
					takeLoop = false;
					loopIdx --;
					loopCnt[loopIdx] ++;
					if (cmdP0Delay == 0)
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
					loopPos[loopIdx] = ftell(infile);
					loopCnt[loopIdx] = 0;
					loopIdx ++;
				}
				cmdP0Delay = 0;
				break;
			case 0xFC:	// repeat previous measure
				{
					UINT16 measureID;
					UINT32 repeatPos;
					UINT32 cachedPos;
					
					if (rcpInf->fileVer == 2)
					{
						measureID = cmdP0Delay;
						repeatPos = (cmdP2 << 8) | (cmdP1 << 0);
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
					cachedPos = measurePos[measureID] - trkBasePos;
					if (cachedPos != repeatPos)
						printf("Warning: Repeat Measure %u: offset mismatch (0x%04X != 0x%04X) at 0x%04X!\n",
							measureID, repeatPos, cachedPos, prevPos);
					
					if (! parentPos)	// this check was verified to be necessary for some files
						parentPos = ftell(infile);
					// using cachedPos here, just in case the offsets are incorrect
					fseek(infile, trkBasePos + cachedPos, SEEK_SET);
				}
				break;
			case 0xFD:	// measure end
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
	}
	
	fseek(infile, trkEndPos, SEEK_SET);
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
