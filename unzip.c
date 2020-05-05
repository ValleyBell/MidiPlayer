// ZIP decompression module
// Written by Valley Bell, 2020-04

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include <stdtype.h>
#include "unzip.h"

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE	static __inline	// __forceinline
#elif defined(__GNUC__)
#define INLINE	static __inline__
#else
#define INLINE	static inline
#endif
#endif	// INLINE


typedef struct _zip_extra_data_pointers
{
	UINT16 extraLen;
	const UINT8* extra;
	UINT64* fileSize;
	UINT64* comprSize;
	UINT64* fileHdrOfs;
	UINT32* diskNo;
} ZIP_XDATA_PTRS;

typedef struct _zip_decompressor ZIP_DECOMPR;

typedef size_t (*ZIP_DECOMPRESS_FUNC)(ZIP_DECOMPR* zdec, size_t bufSize, void* buffer);
typedef UINT8 (*ZIP_DECOMPRESS_DEINIT)(ZIP_DECOMPR* zdec);

typedef struct _zip_decoder_deflate
{
	z_stream zStr;
	UINT8* buffer;
	size_t bufSize;
} ZDEC_DEFLATE;

struct _zip_decompressor
{
	ZIP_DECOMPRESS_FUNC decode;
	ZIP_DECOMPRESS_DEINIT deinit;
	UINT8 mode;
	
	union
	{
		ZDEC_DEFLATE deflate;
	} decData;
	FILE* hFileSrc;
	UINT64 remSrcLen;
};


static size_t SearchSigRev(FILE* hFile, const void* signature, size_t highPos, size_t lowPos);
static size_t SearchEOCD64(FILE* hFile, size_t eocd32Pos);
static UINT8 ParseExtraData(ZIP_XDATA_PTRS* zxd);
static UINT8 ReadLocalFileHeader(FILE* hFile, ZIP_LOC_FHDR* zlfh);
static UINT8 ReadCentralDir(FILE* hFile, const ZIP_EOCD* eocd, ZIP_DIR_ENTRY** retZdeList);
static UINT8 ReadCentralDirEntry(FILE* hFile, ZIP_DIR_ENTRY* zde);
static UINT8 ReadEndOfCentralDir(FILE* hFile, ZIP_EOCD* eocd);
static UINT8 ReadEndOfCentralDir64(FILE* hFile, ZIP_EOCD* eocd, size_t eocd64Pos);

static UINT8 DecomprInit(ZIP_DECOMPR* zdec, UINT8 mode, FILE* hFileZip, UINT64 srcLen);
static UINT8 DecomprStored_Init(ZIP_DECOMPR* zdec);
static size_t DecomprStored_Decode(ZIP_DECOMPR* zdec, size_t bufSize, void* buffer);
static UINT8 DecomprDeflate_Init(ZIP_DECOMPR* zdec);
static UINT8 DecomprDeflate_Deinit(ZIP_DECOMPR* zdec);
static size_t DecomprDeflate_Decode(ZIP_DECOMPR* zdec, size_t bufSize, void* buffer);

//UINT8 ZIP_LoadFromFile(FILE* hFile, ZIP_FILE* zf);
//void ZIP_FreeEntry(ZIP_DIR_ENTRY* zde);
//UINT8 ZIP_Unload(ZIP_FILE* zf);
//const ZIP_DIR_ENTRY* ZIP_GetEntryFromName(const ZIP_FILE* zf, const char* fileName);
//UINT8 ZIP_ExtractToFile(FILE* hFileZip, const ZIP_DIR_ENTRY* zde, FILE* hFileOut);
//UINT8 ZIP_ExtractToBuffer(FILE* hFileZip, const ZIP_DIR_ENTRY* zde, size_t bufLen, void* buffer, size_t* writtenBytes);


static const char CDIR_SIG[4] = {'P', 'K', 1, 2};
static const char LOCFHDR_SIG[4] = {'P', 'K', 3, 4};
static const char EOCD_SIG[4] = {'P', 'K', 5, 6};
static const char EOCD64_SIG[4] = {'P', 'K', 6, 6};
static const char EOCD64LOC_SIG[4] = {'P', 'K', 6, 7};
static const char DATA_DESC_SIG[4] = {'P', 'K', 7, 8};

#define EXTID_ZIP64		0x0001

#define BUFSIZE_DECODE	0x1000
#define BUFSIZE_SEARCH	64


// --- platform-in dependent read methods ---
INLINE UINT16 ReadLE16(const UINT8* data)
{
#ifdef _LITTLE_ENDIAN
	return *(UINT16*)data;
#else
	return (data[0] << 0) | (data[1] << 8);
#endif
}

INLINE UINT32 ReadLE32(const UINT8* data)
{
#ifdef _LITTLE_ENDIAN
	return *(UINT32*)data;
#else
	return	((UINT32)data[0] <<  0) | ((UINT32)data[1] <<  8) |
			((UINT32)data[2] << 16) | ((UINT32)data[3] << 24);
#endif
}

INLINE UINT64 ReadLE64(const UINT8* data)
{
#ifdef _LITTLE_ENDIAN
	return *(UINT64*)data;
#else
	return	((UINT64)data[0] <<  0) | ((UINT64)data[1] <<  8) |
			((UINT64)data[2] << 16) | ((UINT64)data[3] << 24) |
			((UINT64)data[4] << 32) | ((UINT64)data[5] << 40) |
			((UINT64)data[6] << 48) | ((UINT64)data[7] << 56);
#endif
}

INLINE UINT16 freadLE16(FILE* hFile)
{
	UINT8 buffer[2];
	fread(buffer, 2, 1, hFile);
	return ReadLE16(buffer);
}

INLINE UINT32 freadLE32(FILE* hFile)
{
	UINT8 buffer[4];
	fread(buffer, 4, 1, hFile);
	return ReadLE32(buffer);
}

INLINE UINT64 freadLE64(FILE* hFile)
{
	UINT8 buffer[8];
	fread(buffer, 8, 1, hFile);
	return ReadLE64(buffer);
}

INLINE void* freadVarDataNULL(FILE* hFile, size_t len)
{
	void* result;
	
	if (len == 0)
		return NULL;
	
	result = malloc(len);
	fread(result, 1, len, hFile);
	return result;
}

INLINE char* freadStr(FILE* hFile, size_t len, UINT8 zeroNull)
{
	char* result;
	
	if (zeroNull && len == 0)
		return NULL;
	
	result = (char*)malloc(len + 1);
	fread(result, 1, len, hFile);
	result[len] = '\0';
	return result;
}


static size_t SearchSigRev(FILE* hFile, const void* signature, size_t highPos, size_t lowPos)
{
	// search for signature from end to beginning
	size_t filePos;
	char buffer[BUFSIZE_SEARCH+3];	// +3 for overlap scan
	size_t bufRead;
	size_t bufPos;
	
	memset(buffer, 0x00, sizeof(buffer));
	filePos = highPos;
	while(1)
	{
		memcpy(&buffer[BUFSIZE_SEARCH], &buffer[0], 3);	// for overlap scan
		if (filePos >= BUFSIZE_SEARCH)
			bufRead = BUFSIZE_SEARCH;
		else
			bufRead = filePos;
		filePos = filePos - bufRead;
		
		fseek(hFile, filePos, SEEK_SET);
		bufRead = fread(buffer, 1, bufRead, hFile);
		
		bufPos = BUFSIZE_SEARCH;
		while(bufPos > 0)
		{
			bufPos --;
			if (! memcmp(&buffer[bufPos], signature, 4))
				return filePos + bufPos;
		}
	}
	
	return (size_t)-1;
}

static size_t SearchEOCD64(FILE* hFile, size_t eocd32Pos)
{
	char sig[4];
	size_t eocdLocPos;
	UINT64 eocd64Pos;
	UINT32 eocd64Disk;
	UINT32 diskTotal;
	
	if (eocd32Pos < 0x14)
		return (size_t)-1;
	eocdLocPos = SearchSigRev(hFile, EOCD64LOC_SIG, eocd32Pos - 0x14 + 4, 0);
	if (eocdLocPos == (size_t)-1)
		return (size_t)-1;
	
	fseek(hFile, eocdLocPos, SEEK_SET);
	fread(sig, 4, 1, hFile);
	if (memcmp(sig, EOCD64LOC_SIG, 4))
		return (size_t)-1;
	eocd64Disk = freadLE32(hFile);	// number of disk with ZIP64 EOCD
	eocd64Pos = freadLE64(hFile);
	diskTotal = freadLE32(hFile);	// total number of disks
	if (eocd64Disk != diskTotal - 1)
		return (size_t)-1;	// We can't handle the EOCD being on a different disk.
	
	return (size_t)eocd64Pos;
}

static UINT8 ParseExtraData(ZIP_XDATA_PTRS* zxd)
{
	size_t curPos;
	UINT16 fieldID;
	UINT16 fieldLen;
	UINT8 z64Fields;
	const UINT8* dataPtr;
	size_t dataPos;
	
	z64Fields = 0x00;
	z64Fields |= (zxd->fileSize != NULL && *zxd->fileSize == 0xFFFFFFFF) << 0;
	z64Fields |= (zxd->comprSize != NULL && *zxd->comprSize == 0xFFFFFFFF) << 1;
	z64Fields |= (zxd->fileHdrOfs != NULL && *zxd->fileHdrOfs == 0xFFFFFFFF) << 2;
	z64Fields |= (zxd->diskNo != NULL && *zxd->diskNo == 0xFFFF) << 3;
	
	for (curPos = 0x00; curPos < zxd->extraLen; curPos ++)
	{
		fieldID = ReadLE16(&zxd->extra[curPos + 0x00]);
		fieldLen = ReadLE16(&zxd->extra[curPos + 0x02]);
		dataPtr = &zxd->extra[curPos + 0x04];
		curPos += 0x04 + fieldLen;
		
		switch(fieldID)
		{
		case EXTID_ZIP64:
			dataPos = 0x00;
			if (z64Fields & 0x01)
			{
				*zxd->fileSize = ReadLE64(&dataPtr[dataPos]);
				dataPos += 0x08;
			}
			if (z64Fields & 0x02)
			{
				*zxd->comprSize = ReadLE64(&dataPtr[dataPos]);
				dataPos += 0x08;
			}
			if (z64Fields & 0x04)
			{
				*zxd->fileHdrOfs = ReadLE64(&dataPtr[dataPos]);
				dataPos += 0x08;
			}
			if (z64Fields & 0x08)
			{
				*zxd->diskNo = ReadLE32(&dataPtr[dataPos]);
				dataPos += 0x04;
			}
			break;
		}
	}
	
	return ZERR_OK;
}

static UINT8 ReadLocalFileHeader(FILE* hFile, ZIP_LOC_FHDR* zlfh)
{
	fread(zlfh->signature, 4, 1, hFile);
	if (memcmp(zlfh->signature, LOCFHDR_SIG, 4))
		return ZERR_NO_LFH;
	zlfh->verExtract = freadLE16(hFile);
	zlfh->flags = freadLE16(hFile);
	zlfh->comprMethod = freadLE16(hFile);
	zlfh->modTime = freadLE16(hFile);
	zlfh->modDate = freadLE16(hFile);
	zlfh->crc32 = freadLE32(hFile);
	zlfh->comprSize = freadLE32(hFile);
	zlfh->fileSize = freadLE32(hFile);
	zlfh->filenameLen = freadLE16(hFile);
	zlfh->extraLen = freadLE16(hFile);
	//zlfh->filename = freadStr(hFile, zlfh->filenameLen, 0);
	fseek(hFile, zlfh->filenameLen, SEEK_CUR);
	zlfh->extra = (UINT8*)freadVarDataNULL(hFile, zlfh->extraLen);
	
	if (zlfh->extraLen > 0)
	{
		ZIP_XDATA_PTRS zxd =
		{
			zlfh->extraLen, zlfh->extra,
			&zlfh->fileSize,
			&zlfh->comprSize,
			NULL, NULL,
		};
		ParseExtraData(&zxd);
	}
	free(zlfh->extra);	zlfh->extra = NULL;
	
	return ZERR_OK;
}

static UINT8 ReadCentralDir(FILE* hFile, const ZIP_EOCD* eocd, ZIP_DIR_ENTRY** retZdeList)
{
	ZIP_DIR_ENTRY* zdeList;
	UINT64 curEntry;
	UINT8 retVal;
	
	zdeList = (ZIP_DIR_ENTRY*)calloc((size_t)eocd->totalEntries, sizeof(ZIP_DIR_ENTRY));
	*retZdeList = zdeList;
	if (zdeList == NULL)
		return ZERR_NO_MEMORY;
	fseek(hFile, (size_t)eocd->cdOffset, SEEK_SET);
	for (curEntry = 0; curEntry < eocd->totalEntries; curEntry ++)
	{
		retVal = ReadCentralDirEntry(hFile, &zdeList[curEntry]);
		if (retVal)
			return retVal;
	}
	
	return ZERR_OK;
}

static UINT8 ReadCentralDirEntry(FILE* hFile, ZIP_DIR_ENTRY* zde)
{
	fread(zde->signature, 4, 1, hFile);
	if (memcmp(zde->signature, CDIR_SIG, 4))
		return ZERR_NO_CDIR;
	zde->verCreate = freadLE16(hFile);
	zde->verExtract = freadLE16(hFile);
	zde->flags = freadLE16(hFile);
	zde->comprMethod = freadLE16(hFile);
	zde->modTime = freadLE16(hFile);
	zde->modDate = freadLE16(hFile);
	zde->crc32 = freadLE32(hFile);
	zde->comprSize = freadLE32(hFile);
	zde->fileSize = freadLE32(hFile);
	zde->filenameLen = freadLE16(hFile);
	zde->extraLen = freadLE16(hFile);
	zde->commentLen = freadLE16(hFile);
	zde->diskNo = freadLE16(hFile);
	zde->attrInt = freadLE16(hFile);
	zde->attrExt = freadLE32(hFile);
	zde->fileHdrOfs = freadLE32(hFile);
	zde->filename = freadStr(hFile, zde->filenameLen, 0);
	zde->extra = (UINT8*)freadVarDataNULL(hFile, zde->extraLen);
	zde->comment = freadStr(hFile, zde->commentLen, 1);
	
	if (zde->extraLen > 0)
	{
		ZIP_XDATA_PTRS zxd =
		{
			zde->extraLen, zde->extra,
			&zde->fileSize,
			&zde->comprSize,
			&zde->fileHdrOfs,
			&zde->diskNo,
		};
		ParseExtraData(&zxd);
	}
	
	return ZERR_OK;
}

static UINT8 ReadEndOfCentralDir(FILE* hFile, ZIP_EOCD* eocd)
{
	size_t fileLen;
	size_t eocdPos;
	size_t sigMinPos;
	
	fseek(hFile, 0, SEEK_END);
	fileLen = ftell(hFile);
	sigMinPos = 0x10016;	// search window: 0x16 bytes EOCD size + up to 65535 bytes comment
	if (fileLen <= sigMinPos)
		sigMinPos = 0;
	else
		sigMinPos = fileLen - sigMinPos;
	eocdPos = SearchSigRev(hFile, EOCD_SIG, fileLen, sigMinPos);
	if (eocdPos == (size_t)-1)
		return ZERR_NO_EOCD;
	
	fseek(hFile, eocdPos, SEEK_SET);
	fread(eocd->signature, 4, 1, hFile);
	eocd->diskEOCD = freadLE16(hFile);
	eocd->diskCD = freadLE16(hFile);
	eocd->diskEntries = freadLE16(hFile);
	eocd->totalEntries = freadLE16(hFile);
	eocd->cdSize = freadLE32(hFile);
	eocd->cdOffset = freadLE32(hFile);
	eocd->commentLen = freadLE16(hFile);
	eocd->comment = freadStr(hFile, eocd->commentLen, 1);
	
	if (eocd->totalEntries == 0xFFFF || eocd->cdSize == 0xFFFF || eocd->cdOffset == 0xFFFFFFFF)
	{
		size_t eocd64Pos = SearchEOCD64(hFile, eocdPos);
		if (eocd64Pos != (size_t)-1)
			return ReadEndOfCentralDir64(hFile, eocd, eocd64Pos);
	}
	
	return ZERR_OK;
}

static UINT8 ReadEndOfCentralDir64(FILE* hFile, ZIP_EOCD* eocd, size_t eocd64Pos)
{
	char sig[4];
	UINT64 blkSize;
	UINT16 verCreate;
	UINT16 verExtract;
	
	fseek(hFile, eocd64Pos, SEEK_SET);
	fread(sig, 4, 1, hFile);
	if (memcmp(sig, EOCD64_SIG, 4))
		return ZERR_NO_EOCD64;
	blkSize = freadLE64(hFile);		// EOCD64 size (ignored)
	verCreate = freadLE16(hFile);	// Creator version (ignored)
	verExtract = freadLE16(hFile);	// version required for extraction (ignored)
	eocd->diskEOCD = freadLE32(hFile);
	eocd->diskCD = freadLE32(hFile);
	eocd->diskEntries = freadLE64(hFile);
	eocd->totalEntries = freadLE64(hFile);
	eocd->cdSize = freadLE64(hFile);
	eocd->cdOffset = freadLE64(hFile);
	// ignore ZIP64 extensible data sector
	
	return ZERR_OK;
}


static UINT8 DecomprInit(ZIP_DECOMPR* zdec, UINT8 mode, FILE* hFileZip, UINT64 srcLen)
{
	UINT8 retVal;
	
	zdec->hFileSrc = hFileZip;
	zdec->remSrcLen = srcLen;
	
	if (mode == ZCM_STORED)
		retVal = DecomprStored_Init(zdec);
	else if (mode == ZCM_DEFLATED)
		retVal = DecomprDeflate_Init(zdec);
	else
		retVal = ZERR_BAD_COMPR;
	return retVal;
}

// ZIP decompression: stored
static UINT8 DecomprStored_Init(ZIP_DECOMPR* zdec)
{
	zdec->decode = DecomprStored_Decode;
	zdec->deinit = NULL;
	return ZERR_OK;
}

static size_t DecomprStored_Decode(ZIP_DECOMPR* zdec, size_t bufSize, void* buffer)
{
	size_t copyBytes;
	
	copyBytes = bufSize;
	if (copyBytes > zdec->remSrcLen)
		copyBytes = (size_t)zdec->remSrcLen;
	if (copyBytes > 0)
	{
		copyBytes = fread(buffer, 1, copyBytes, zdec->hFileSrc);
		zdec->remSrcLen -= copyBytes;
	}
	return copyBytes;
}

// ZIP decompression: deflate
static UINT8 DecomprDeflate_Init(ZIP_DECOMPR* zdec)
{
	int retVal;
	ZDEC_DEFLATE* zdd = &zdec->decData.deflate;
	
	zdec->decode = DecomprDeflate_Decode;
	zdec->deinit = DecomprDeflate_Deinit;
	
	zdd->bufSize = BUFSIZE_DECODE;
	if (zdd->bufSize > zdec->remSrcLen)
		zdd->bufSize = (size_t)zdec->remSrcLen;
	zdd->buffer = (UINT8*)malloc(zdd->bufSize);
	
	memset(&zdd->zStr, 0, sizeof(z_stream));
	retVal = inflateInit2(&zdd->zStr, -MAX_WBITS);
	if (retVal != Z_OK)
	{
		free(zdd->buffer);
		zdd->buffer = NULL;
		return ZERR_API_ERR;
	}
	
	return ZERR_OK;
}

static UINT8 DecomprDeflate_Deinit(ZIP_DECOMPR* zdec)
{
	ZDEC_DEFLATE* zdd = &zdec->decData.deflate;
	int retVal;
	
	free(zdd->buffer);
	zdd->buffer = NULL;
	
	retVal = inflateEnd(&zdd->zStr);
	return (retVal == Z_OK) ? ZERR_OK : ZERR_API_ERR;
}

static size_t DecomprDeflate_Decode(ZIP_DECOMPR* zdec, size_t bufSize, void* buffer)
{
	ZDEC_DEFLATE* zdd = &zdec->decData.deflate;
	int retVal;
	
	zdd->zStr.next_out = (Bytef*)buffer;
	zdd->zStr.avail_out = (uInt)bufSize;
	while(zdd->zStr.avail_out > 0)
	{
		if (zdd->zStr.avail_in == 0)
		{
			zdd->zStr.next_in = (Bytef*)zdd->buffer;
			zdd->zStr.avail_in = fread(zdd->buffer, 1, zdd->bufSize, zdec->hFileSrc);
		}
		retVal = inflate(&zdd->zStr, Z_NO_FLUSH);
		if (retVal != Z_OK)
			break;
	}
	return bufSize - zdd->zStr.avail_out;
}


UINT8 ZIP_LoadFromFile(FILE* hFile, ZIP_FILE* zf)
{
	UINT8 retVal;
	
	retVal = ReadEndOfCentralDir(hFile, &zf->eocd);
	if (retVal)
		return retVal;
	retVal = ReadCentralDir(hFile, &zf->eocd, &zf->entries);
	if (retVal)
		return retVal;
	
	return ZERR_OK;
}

UINT8 ZIP_Unload(ZIP_FILE* zf)
{
	UINT64 curEntry;
	
	for (curEntry = 0; curEntry < zf->eocd.totalEntries; curEntry ++)
		ZIP_FreeDirEntry(&zf->entries[curEntry]);
	free(zf->entries);		zf->entries = NULL;
	free(zf->eocd.comment);	zf->eocd.comment = NULL;
	memset(&zf->eocd, 0x00, sizeof(ZIP_EOCD));
	
	return ZERR_OK;
}

void ZIP_FreeDirEntry(ZIP_DIR_ENTRY* zde)
{
	free(zde->filename);	zde->filename = NULL;
	free(zde->extra);		zde->extra = NULL;
	free(zde->comment);		zde->comment = NULL;
	
	return;
}

const ZIP_DIR_ENTRY* ZIP_GetEntryFromName(const ZIP_FILE* zf, const char* fileName)
{
	UINT64 curEntry;
	
	for (curEntry = 0; curEntry < zf->eocd.totalEntries; curEntry ++)
	{
		if (! strcmp(zf->entries[curEntry].filename, fileName))
			return &zf->entries[curEntry];
	}
	
	return NULL;
}

UINT8 ZIP_ExtractToFile(FILE* hFileZip, const ZIP_DIR_ENTRY* zde, FILE* hFileOut)
{
	ZIP_LOC_FHDR zlhf;
	ZIP_DECOMPR zdec;
	UINT8 retVal;
	size_t readBytes;
	UINT64 remBytes;
	uLong crcSum;
	size_t bufLen;
	void* buffer;
	
	fseek(hFileZip, (size_t)zde->fileHdrOfs, SEEK_SET);
	retVal = ReadLocalFileHeader(hFileZip, &zlhf);
	if (retVal)
		return retVal;
	
	retVal = DecomprInit(&zdec, (UINT8)zde->comprMethod, hFileZip, zde->comprSize);
	if (retVal)
		return retVal;
	crcSum = crc32(0, Z_NULL, 0);	// initialize CRC
	
	bufLen = BUFSIZE_DECODE;
	if (bufLen > zlhf.fileSize)
		bufLen = (size_t)zlhf.fileSize;
	buffer = malloc(bufLen);
	
	remBytes = zlhf.fileSize;
	while(remBytes > 0)
	{
		readBytes = zdec.decode(&zdec, bufLen, buffer);
		if (readBytes <= 0)
			break;
		fwrite(buffer, 1, readBytes, hFileOut);
		crcSum = crc32(crcSum, (Bytef*)buffer, (uInt)readBytes);
		remBytes -= readBytes;
	}
	
	if (zdec.deinit != NULL)
		zdec.deinit(&zdec);
	free(buffer);
	
	if (crcSum != zlhf.crc32)
		return ZERR_BAD_CRC;
	return ZERR_OK;
}

UINT8 ZIP_ExtractToBuffer(FILE* hFileZip, const ZIP_DIR_ENTRY* zde, size_t bufLen, void* buffer, size_t* writtenBytes)
{
	ZIP_LOC_FHDR zlhf;
	ZIP_DECOMPR zdec;
	UINT8 retVal;
	uLong crcSum;
	
	fseek(hFileZip, (size_t)zde->fileHdrOfs, SEEK_SET);
	retVal = ReadLocalFileHeader(hFileZip, &zlhf);
	if (retVal)
		return retVal;
	
	retVal = DecomprInit(&zdec, (UINT8)zde->comprMethod, hFileZip, zde->comprSize);
	if (retVal)
		return retVal;
	crcSum = crc32(0, Z_NULL, 0);	// initialize CRC
	
	*writtenBytes = zdec.decode(&zdec, bufLen, buffer);
	crcSum = crc32(crcSum, (Bytef*)buffer, (uInt)*writtenBytes);
	
	if (zdec.deinit != NULL)
		zdec.deinit(&zdec);
	
	if (crcSum != zlhf.crc32)
		return ZERR_BAD_CRC;
	return ZERR_OK;
}
