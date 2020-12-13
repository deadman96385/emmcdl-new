/*****************************************************************************
 * ffu.h
 *
 * This file has structures and functions for FFU format
 *
 * Copyright (c) 2007-2012
 * Qualcomm Technologies Incorporated.
 * All Rights Reserved.
 * Qualcomm Confidential and Proprietary
 *
 *****************************************************************************/
/*=============================================================================
                        Edit History

$Header: //deploy/qcom/qct/platform/wpci/prod/woa/emmcdl/main/latest/inc/ffu.h#11 $
$DateTime: 2016/04/14 19:40:02 $ $Author: wmcisvc $

when       who     what, where, why
-------------------------------------------------------------------------------
11/04/12   pgw     Initial version.
=============================================================================*/
#pragma once
#include "partition.h"
#include "serialport.h"
#include "firehose.h"
#include <windows.h>

#define MAX_GPT_ENTRIES     128
#define MAX_PART_NAME       36
#define DEVICE_PATH_ID_LEN  512
#define UFS_SECTOR_SIZE     4096

typedef unsigned __int64 uint64;

// GUID to LUN mapping
#define EFI_UFS_LUN_0_GUID L"VenHw(860845C1-BE09-4355-8BC1-30D64FF8E63A)"
#define EFI_UFS_LUN_1_GUID L"VenHw(8D90D477-39A3-4A38-AB9E-586FF69ED051)"
#define EFI_UFS_LUN_2_GUID L"VenHw(EDF85868-87EC-4F77-9CDA-5F10DF2FE601)"
#define EFI_UFS_LUN_3_GUID L"VenHw(1AE69024-8AEB-4DF8-BC98-0032DBDF5024)"
#define EFI_UFS_LUN_4_GUID L"VenHw(D33F1985-F107-4A85-BE38-68DC7AD32CEA)"
#define EFI_UFS_LUN_5_GUID L"VenHw(4BA1D05F-088E-483F-A97E-B19B9CCF59B0)"
#define EFI_UFS_LUN_6_GUID L"VenHw(4ACF98F6-26FA-44D2-8132-282F2D19A4C5)"
#define EFI_UFS_LUN_7_GUID L"VenHw(8598155F-34DE-415C-8B55-843E3322D36F)"
#define EFI_UFS_RPMB_LUN_GUID L"VenHw(5397474E-F75D-44B3-8E57-D9324FCF6FE1)"

enum DISK_ACCESS_METHOD
{
  DISK_BEGIN = 0,
  DISK_SEQ = 1,
  DISK_END = 2
};

// Note all these structures need to be PACKED
#pragma pack(push,1)
// Security Header struct. The first data read in from the FFU.
typedef struct _SECURITY_HEADER
{
    UINT32 cbSize;            // size of struct, overall
    BYTE   signature[12];     // "SignedImage "
    UINT32 dwChunkSizeInKb;   // size of a hashed chunk within the image
    UINT32 dwAlgId;           // algorithm used to hash
    UINT32 dwCatalogSize;     // size of catalog to validate
    UINT32 dwHashTableSize;   // size of hash table
} SECURITY_HEADER;

// Image Header struct found within Image Header region of FFU
typedef struct _IMAGE_HEADER
{
    DWORD  cbSize;           // sizeof(ImageHeader)
    BYTE   Signature[12];    // "ImageFlash  "
    DWORD  ManifestLength;   // in bytes
    DWORD  dwChunkSize;      // Used only during image generation.
} IMAGE_HEADER;

// Note in V2 these entries are appended to the end of STORE_HEADER
typedef struct _STORE_HEADER_V2
{
  UINT16 NumOfStores;        // Total number of stores
  UINT16 StoreIndex;         // Current store index
  UINT64 qwStorePayloadSize; // Payload data only, excludes padding
  UINT16 DevicePathLength;   // Length of the device path, without nul character
  wchar_t *pwszDevicePath;   // Device path
} STORE_HEADER_V2;

// Store Header struct found within Store Header region of FFU
typedef struct _STORE_HEADER
{
    UINT32 dwUpdateType; // indicates partial or full flash
    UINT16 MajorVersion, MinorVersion; // used to validate struct
    UINT16 FullFlashMajorVersion, FullFlashMinorVersion; // FFU version, i.e. the image format
    char szPlatformId[192]; // string which indicates what device this FFU is intended to be written to
    UINT32 dwBlockSizeInBytes; // size of an image block in bytes – the device’s actual sector size may differ
    UINT32 dwWriteDescriptorCount; // number of write descriptors to iterate through
    UINT32 dwWriteDescriptorLength; // total size of all the write descriptors, in bytes (included so they can be read out up front and interpreted later)
    UINT32 dwValidateDescriptorCount; // number of validation descriptors to check
    UINT32 dwValidateDescriptorLength; // total size of all the validation descriptors, in bytes
    UINT32 dwInitialTableIndex; // block index in the payload of the initial (invalid) GPT
    UINT32 dwInitialTableCount; // count of blocks for the initial GPT, i.e. the GPT spans blockArray[idx..(idx + count -1)]
    UINT32 dwFlashOnlyTableIndex; // first block index in the payload of the flash-only GPT (included so safe flashing can be accomplished)
    UINT32 dwFlashOnlyTableCount; // count of blocks in the flash-only GPT
    UINT32 dwFinalTableIndex; // index in the table of the real GPT
    UINT32 dwFinalTableCount; // number of blocks in the real GPT

    // V2 entries
    STORE_HEADER_V2 v2;     // Version v2 extra entries
} STORE_HEADER;


// Struct used in BLOCK_DATA_ENTRY structs to define where to put data on disk
typedef struct _DISK_LOCATION
{
    UINT32 dwDiskAccessMethod;
    UINT32 dwBlockIndex;
} DISK_LOCATION; 

typedef struct _BLOCK_DATA_ENTRY
{
    UINT32 dwLocationCount;
    UINT32 dwBlockCount;
    DISK_LOCATION rgDiskLocations[1];
} BLOCK_DATA_ENTRY;
#pragma pack(pop)

class FFUImage {
public:
  int PreLoadImage(TCHAR *szFFUPath);
  int ProgramImage(Protocol *proto, __int64 dwOffset);
  int CloseFFUFile(void);
  void SetDiskSectorSize(int size);
  int SplitFFUBin(TCHAR *szPartName, TCHAR *szOutputFile);
  int FFUToRawProgram(TCHAR *szFFUName, TCHAR *szImagePath);
  FFUImage();
  ~FFUImage();

private:
  void SetOffset(OVERLAPPED* ovlVariable, UINT64 offset);
  int CreateRawProgram(void);
  int TerminateRawProgram(void);
  int WriteDataEntry(UINT64 dwFileOffset, __int64 dwDiskOffset, UINT64 dwNumSectors, DWORD physPart);
  int DumpRawProgram(int physPart);
  int AddEntryToRawProgram(UINT64 ui64FileOffset, __int64 i64StartSector, UINT64 ui64NumSectors, DWORD physPart);
  int ReadGPT(int index);
  int ParseHeaders(void);
  int ParseStoreHeaders(UINT64 uiFileOffset);
  int MapLunFromGuid(TCHAR *szGuid);
  UINT64 ReadBlockData(int storeIndex, UINT64 ui64FileOffset);
  gpt_entry_t *GetPartition(UINT64 uiStartSector);
  UINT64 GetNextStartingArea(UINT64 chunkSizeInBytes, UINT64 sizeOfArea);

  // Headers found within FFU image
  SECURITY_HEADER FFUSecurityHeader;
  IMAGE_HEADER FFUImageHeader;
  STORE_HEADER* FFUStoreHeaders;
  void** BlockDataEntries;
  UINT64* PayloadDataStart;
  TCHAR *szFFUFile;
  TCHAR *szRawProgram;
  Protocol *pProto;

  HANDLE hFFU;
  OVERLAPPED OvlRead;
  OVERLAPPED OvlWrite;

  // GPT Stuff
  BYTE GptProtectiveMBR[512];
  gpt_header_t GptHeader;
  gpt_entry_t *GptEntries;
  int DISK_SECTOR_SIZE;

};
