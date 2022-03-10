// ZIP decompression module
// Written by Valley Bell, 2020-04

#ifndef __UNZIP_H__
#define __UNZIP_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdtype.h>

// ZIP compression method
#define ZCM_STORED		0
#define ZCM_DEFLATED	8

typedef struct _zip_local_file_header
{
	char signature[4];		// "PK",3,4
	UINT16 verExtract;
	UINT16 flags;
	UINT16 comprMethod;
	UINT16 modTime;
	UINT16 modDate;
	UINT32 crc32;
	UINT64 comprSize;
	UINT64 fileSize;	// decompressed size
	UINT16 filenameLen;
	UINT16 extraLen;
	char* filename;		// file name, note: directories always end with '/'
	UINT8* extra;
} ZIP_LOC_FHDR;

typedef struct _zip_directory_entry
{
	char signature[4];		// "PK",1,2
	UINT16 verCreate;
	UINT16 verExtract;
	UINT16 flags;
	UINT16 comprMethod;
	UINT16 modTime;
	UINT16 modDate;
	UINT32 crc32;
	UINT64 comprSize;
	UINT64 fileSize;	// decompressed size
	UINT16 filenameLen;
	UINT16 extraLen;
	UINT16 commentLen;
	UINT32 diskNo;
	UINT16 attrInt;		// internal attributes
	UINT32 attrExt;		// external attributes
	UINT64 fileHdrOfs;	// local file header offset
	char* filename;		// file name, note: directories always end with '/'
	UINT8* extra;
	char* comment;
} ZIP_DIR_ENTRY;

typedef struct _zip_end_of_central_directory
{
	char signature[4];		// "PK",5,6
	UINT32 diskEOCD;		// number of this disk (with EOCD)
	UINT32 diskCD;			// number of disk where CD begins
	UINT64 diskEntries;		// entries on this disk
	UINT64 totalEntries;	// total entries in CD
	UINT64 cdSize;			// total size of CD
	UINT64 cdOffset;		// file offset on diskCD
	UINT16 commentLen;
	char* comment;			// ZIP file comment
} ZIP_EOCD;

typedef struct _zip_file_data
{
	ZIP_EOCD eocd;
	ZIP_DIR_ENTRY* entries;
} ZIP_FILE;


UINT8 ZIP_LoadFromFile(FILE* hFile, ZIP_FILE* zf);
UINT8 ZIP_Unload(ZIP_FILE* zf);
void ZIP_FreeDirEntry(ZIP_DIR_ENTRY* zde);
const ZIP_DIR_ENTRY* ZIP_GetEntryFromName(const ZIP_FILE* zf, const char* fileName);
UINT8 ZIP_ExtractToFile(FILE* hFileZip, const ZIP_DIR_ENTRY* zde, FILE* hFileOut);
UINT8 ZIP_ExtractToBuffer(FILE* hFileZip, const ZIP_DIR_ENTRY* zde, size_t bufLen, void* buffer, size_t* writtenBytes);


#define ZERR_OK			0x00
#define ZERR_BAD_CRC	0x10	// CRC32 checksum mismatch
#define ZERR_NO_LFH		0x40	// "Local File Header" signature not found
#define ZERR_NO_CDIR	0x41	// "Central Directory" signature not found
#define ZERR_NO_EOCD	0x42	// "End Of Central Directory" signature not found
#define ZERR_NO_EOCD64	0x43	// "End Of ZIP64 Central Directory" signature not found
#define ZERR_BAD_COMPR	0x80	// unsupported compression method
#define ZERR_NO_MEMORY	0xF0	// memory allocation error
#define ZERR_API_ERR	0xC0	// API call error (for e.g. zlib calls)
#define ZERR_NO_ZIP		ZERR_NO_EOCD	// no valid ZIP file (EOCD not found)
#define ZERR_FAIL		0xFF	// general error code

#ifdef __cplusplus
}
#endif

#endif	// __UNZIP_H__
