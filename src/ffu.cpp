/*****************************************************************************
* ffu.cpp
*
* This class implements FFU image support
*
* Copyright (c) 2007-2011
* Qualcomm Technologies Incorporated.
* All Rights Reserved.
* Qualcomm Confidential and Proprietary
*
*****************************************************************************/
/*=============================================================================
Edit History

$Header: //deploy/qcom/qct/platform/wpci/prod/woa/emmcdl/main/latest/src/ffu.cpp#29 $
$DateTime: 2019/03/11 15:00:38 $ $Author: wmcisvc $

when       who     what, where, why
-------------------------------------------------------------------------------
11/04/12   pgw     Initial version.
=============================================================================*/

#include "stdio.h"
#include "stdlib.h"
#include "tchar.h"
#include "ffu.h"
#include "windows.h"
#include "string.h"

// Constructor
FFUImage::FFUImage()
{
  hFFU = INVALID_HANDLE_VALUE;
  BlockDataEntries = NULL;
  FFUStoreHeaders = NULL;
  PayloadDataStart = NULL;
  GptEntries = NULL;
  szFFUFile = NULL;
  szRawProgram = NULL;
  pProto = NULL;

  // Default sector size can be overridden
  DISK_SECTOR_SIZE = 512;
}

FFUImage::~FFUImage()
{
  if(GptEntries) free(GptEntries);
  if (BlockDataEntries != NULL) {
    // Free each data block we allocated
    for (UINT i = 0; i < FFUStoreHeaders[0].v2.NumOfStores; i++) {
      if (BlockDataEntries[i] != NULL) free(BlockDataEntries[i]);
    }
    free(BlockDataEntries);
    BlockDataEntries = NULL;
  }
  if (FFUStoreHeaders) free(FFUStoreHeaders);
  if (PayloadDataStart) free(PayloadDataStart);
  FFUStoreHeaders = NULL;
  GptEntries = NULL;
}

// Set the disk sector size normally 512 for eMMC or 4096 for UFS
void FFUImage::SetDiskSectorSize(int size)
{
  DISK_SECTOR_SIZE = size;
}

// Given a 64 bit memory address, set the Offset and OffsetHigh of an OVERLAPPED variable 
void FFUImage::SetOffset(OVERLAPPED* ovlVariable, UINT64 offset)
{
  ResetEvent(ovlVariable->hEvent);
  ovlVariable->Offset = offset & 0xffffffff;
  ovlVariable->OffsetHigh = (offset & 0xffffffff00000000) >> 32;
}

// Compute the size of the padding at the end of each section of the FFU, and return
// the offset into the file of the next starting area.
UINT64 FFUImage::GetNextStartingArea(UINT64 chunkSizeInBytes, UINT64 sizeOfArea)
{
  UINT64 extraSpacePastBoundary = sizeOfArea % chunkSizeInBytes;
  // If the area does not end on a chunk boundary (i.e. there is a padding), compute next boundary.
  if (extraSpacePastBoundary != 0) {
    return sizeOfArea + (chunkSizeInBytes - extraSpacePastBoundary);
  } else {
    return  sizeOfArea;
  }

}

gpt_entry_t *FFUImage::GetPartition(UINT64 uiStartSector)
{
  // Now loop through all the partition entries to find out entry where this falls within
  for (int i = 0; i < MAX_GPT_ENTRIES; i++) {
    // Check if the start sector falls within this partition
    if (uiStartSector >= GptEntries[i].first_lba &&
      uiStartSector < GptEntries[i].last_lba) {
      return &GptEntries[i];
    }
  }

  // If we didn't find the partition return NULL
  return NULL;
}

int FFUImage::AddEntryToRawProgram(UINT64 ui64FileOffset, __int64 i64StartSector, UINT64 ui64NumSectors, DWORD physPart)
{
  HANDLE hRawPrg;
  char szXMLString[MAX_STRING_LEN * 2];
  char szFile[MAX_STRING_LEN];
  char szPartition[MAX_PART_NAME] = "GPT";
  DWORD dwBytesWrite;
  size_t sBytesConvert;
  int status = ERROR_SUCCESS;
  hRawPrg = CreateFile(szRawProgram,
    GENERIC_WRITE | GENERIC_READ,
    0,    // We want exclusive access to this disk
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL);

  if (hRawPrg == INVALID_HANDLE_VALUE) {
    status = GetLastError();
  }
  else {
    gpt_entry_t *gpt = GetPartition(i64StartSector);
    if (gpt != NULL) {
       wcstombs_s(&sBytesConvert, szPartition, sizeof(szPartition), gpt->part_name, MAX_PART_NAME);
    }
    wcstombs_s(&sBytesConvert, szFile, sizeof(szFile), szFFUFile, sizeof(szFile));
    SetFilePointer(hRawPrg, 0, 0, FILE_END);
    if (i64StartSector < 0) {
       sprintf_s(szXMLString, sizeof(szXMLString), "<program SECTOR_SIZE_IN_BYTES=\"%i\" file_sector_offset=\"%I64d\" filename=\"%s\" label=\"Backup_GPT\" num_partition_sectors=\"%I64d\" physical_partition_number=\"%i\" size_in_KB=\"%I64d\" sparse=\"false\" start_byte_hex=\"0x%I64x\" start_sector=\"NUM_DISK_SECTORS%I64d\"/>\n",
         DISK_SECTOR_SIZE, ui64FileOffset/DISK_SECTOR_SIZE, szFile, ui64NumSectors, physPart, ui64NumSectors*DISK_SECTOR_SIZE/1024, i64StartSector * DISK_SECTOR_SIZE, i64StartSector);
    }
    else {
       sprintf_s(szXMLString, sizeof(szXMLString), "<program SECTOR_SIZE_IN_BYTES=\"%i\" file_sector_offset=\"%I64d\" filename=\"%s\" label=\"%s\" num_partition_sectors=\"%I64d\" physical_partition_number=\"%i\" size_in_KB=\"%I64d\" sparse=\"false\" start_byte_hex=\"0x%I64x\" start_sector=\"%I64d\"/>\n",
         DISK_SECTOR_SIZE, ui64FileOffset/DISK_SECTOR_SIZE, szFile, szPartition, ui64NumSectors, physPart, ui64NumSectors *DISK_SECTOR_SIZE / 1024, i64StartSector * DISK_SECTOR_SIZE, i64StartSector);
     }
     WriteFile(hRawPrg, szXMLString, (DWORD)strlen(szXMLString), &dwBytesWrite, NULL);
     CloseHandle(hRawPrg);
   }

   return status;
}

int FFUImage::TerminateRawProgram(void)
{
  HANDLE hRawPrg;
  DWORD dwBytesOut;
  DWORD dwLUN=0;
  char szXMLString[] = "</data>\n";

  szFFUFile = _T("");

  // Dump partition table to end of rawprogram in case anyone wants to override a partition
  for (UINT p = 0; p < FFUStoreHeaders[0].v2.NumOfStores; p++) {
    ReadGPT(p);
    if ((FFUStoreHeaders[0].MajorVersion >= 2) && (FFUStoreHeaders[p].v2.pwszDevicePath != NULL)) {
      dwLUN = MapLunFromGuid(FFUStoreHeaders[p].v2.pwszDevicePath);
    }
    for (int i = 0; i < MAX_GPT_ENTRIES; i++) {
      // If this is valid entry dump it out 
      if (GptEntries[i].first_lba > 0) {
        AddEntryToRawProgram(0, GptEntries[i].first_lba, GptEntries[i].last_lba - GptEntries[i].first_lba+1, dwLUN);
      }
    }
  }

  hRawPrg = CreateFile( szRawProgram,
    GENERIC_WRITE | GENERIC_READ,
    0,    // We want exclusive access to this disk
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL);
  
  if( hRawPrg == INVALID_HANDLE_VALUE ) {
    return GetLastError();
  }

  SetFilePointer(hRawPrg,0,0,FILE_END);

  // write out all the entries for gpt_main0.bin
  WriteFile(hRawPrg,szXMLString,sizeof(szXMLString)-1,&dwBytesOut,NULL);

  CloseHandle(hRawPrg);
  return ERROR_SUCCESS;
}

int FFUImage::CreateRawProgram(void)
{
  HANDLE hRawPrg;
  DWORD dwBytesOut;
  char szPath[256] = { 0 };
  char szOut[256] = { 0 };
  char szXMLString[256] = { 0 };

  // If no rawprogram defined we do nothing
  if(szRawProgram == NULL) return ERROR_SUCCESS;

  sprintf_s(szXMLString, sizeof(szXMLString), "<?xml version=\"1.0\" ?>\n<data>\n");
  hRawPrg = CreateFile(szRawProgram,
    GENERIC_WRITE | GENERIC_READ,
    0,    // We want exclusive access to this disk
    NULL,
    CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL,
    NULL);
  if (hRawPrg == INVALID_HANDLE_VALUE) {
    return GetLastError();
  }

  // write out all the entries for gpt_main0.bin
  WriteFile(hRawPrg, szXMLString, (DWORD)strlen(szXMLString), &dwBytesOut, NULL);

  wcstombs_s((size_t *)&dwBytesOut, szPath, sizeof(szPath), szFFUFile, wcslen(szFFUFile));
  wcstombs_s((size_t *)&dwBytesOut, szOut, sizeof(szOut), szFFUFile, wcslen(szFFUFile));
  sprintf_s(szXMLString, sizeof(szXMLString), "<!-- emmcdl.exe -splitffu %s -o %s -->\n", szPath, szOut);
  WriteFile(hRawPrg, szXMLString, (DWORD)strlen(szXMLString), &dwBytesOut, NULL);

  CloseHandle(hRawPrg);

  return ERROR_SUCCESS;
}

int FFUImage::ReadGPT(int index)
{
  // Read in the data chunk for GPT
  if (hFFU == INVALID_HANDLE_VALUE ) return ERROR_INVALID_HANDLE;
  DWORD bytesRead = 0;
  UINT64 readOffset = PayloadDataStart[index] + (UINT64)(FFUStoreHeaders[index].dwFinalTableIndex) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes);

  GptEntries = (gpt_entry_t*)malloc(sizeof(gpt_entry_t)*MAX_GPT_ENTRIES);
  SetOffset(&OvlRead, readOffset);

  // Read contents directly from FFU into the partition table
  if (!ReadFile(hFFU, &GptProtectiveMBR, sizeof(GptProtectiveMBR), &bytesRead, &OvlRead)){
    return GetLastError();
  }
  readOffset += DISK_SECTOR_SIZE;
  SetOffset(&OvlRead, readOffset);
  if (!ReadFile(hFFU, &GptHeader, sizeof(GptHeader), &bytesRead, &OvlRead)){
    return GetLastError();
  }
  // If GPT header is not valid this may have 4096 sector size
  if (strncmp(GptHeader.signature, "EFI PART", 8) != 0) {
    readOffset += UFS_SECTOR_SIZE - DISK_SECTOR_SIZE;
    SetOffset(&OvlRead, readOffset);
    if (!ReadFile(hFFU, &GptHeader, sizeof(GptHeader), &bytesRead, &OvlRead)) {
      return GetLastError();
    }
    if (strncmp(GptHeader.signature, "EFI PART", 8) != 0) {
      return ERROR_INVALID_PARAMETER;
    }
    DISK_SECTOR_SIZE = UFS_SECTOR_SIZE;
  }


  readOffset += DISK_SECTOR_SIZE;
  SetOffset(&OvlRead, readOffset);
  if (!ReadFile(hFFU, GptEntries, sizeof(gpt_entry_t)* MAX_GPT_ENTRIES, &bytesRead, &OvlRead)){
    return GetLastError();
  }

  return ERROR_SUCCESS;
}

UINT64 FFUImage::ReadBlockData(int storeIndex, UINT64 ui64FileOffset)
{
  DWORD bytesRead = 0;

  // Skip over validation entries
  if (FFUStoreHeaders[storeIndex].dwValidateDescriptorLength > 0) {
    ui64FileOffset += FFUStoreHeaders[storeIndex].dwValidateDescriptorLength;
  }

  // Allocate space and read in block data entries
  if (FFUStoreHeaders[storeIndex].dwWriteDescriptorLength > 0) {
    BlockDataEntries[storeIndex] = malloc(FFUStoreHeaders[storeIndex].dwWriteDescriptorLength);
    if (BlockDataEntries[storeIndex] == NULL) return ERROR_OUTOFMEMORY;
    SetOffset(&OvlRead, ui64FileOffset);
    if (!ReadFile(hFFU, BlockDataEntries[storeIndex], FFUStoreHeaders[storeIndex].dwWriteDescriptorLength, &bytesRead, &OvlRead)) {
      wprintf(L"Failed to read block data at offset: %I64d", ui64FileOffset);
    }
    ui64FileOffset += FFUStoreHeaders[storeIndex].dwWriteDescriptorLength;
  }

  return ui64FileOffset;
}

int FFUImage::MapLunFromGuid(TCHAR *szGuid)
{
  if (wcscmp(szGuid, EFI_UFS_LUN_0_GUID) == 0) return 0;
  else if (wcscmp(szGuid, EFI_UFS_LUN_1_GUID) == 0) return 1;
  else if (wcscmp(szGuid, EFI_UFS_LUN_2_GUID) == 0) return 2;
  else if (wcscmp(szGuid, EFI_UFS_LUN_3_GUID) == 0) return 3;
  else if (wcscmp(szGuid, EFI_UFS_LUN_4_GUID) == 0) return 4;
  else if (wcscmp(szGuid, EFI_UFS_LUN_5_GUID) == 0) return 5;
  else if (wcscmp(szGuid, EFI_UFS_LUN_6_GUID) == 0) return 6;
  else if (wcscmp(szGuid, EFI_UFS_LUN_7_GUID) == 0) return 7;
  else if (wcscmp(szGuid, EFI_UFS_RPMB_LUN_GUID) == 0) return 8;
  
  return -1;
}

int FFUImage::ParseStoreHeaders(UINT64 uiFileOffset)
{
  STORE_HEADER firstHeader;
  DWORD bytesRead = 0;

  // Read FFUStoreHeader from the FFU
  SetOffset(&OvlRead, uiFileOffset);
  if (!ReadFile(hFFU, &firstHeader, sizeof(firstHeader), &bytesRead, &OvlRead)) return GetLastError();

  // Support for FFU V2 check the header version
  if (firstHeader.MajorVersion >= 2) {
    // Make sure the index of the store we are trying to read is valid
    if (firstHeader.v2.NumOfStores < 1) {
      wprintf(L"dwNumStores: %i invalid\n", firstHeader.v2.NumOfStores);
      return ERROR_INVALID_PARAMETER;
    }
    uiFileOffset += sizeof(STORE_HEADER)-4;
    // Read in LUN name
    firstHeader.v2.pwszDevicePath = (wchar_t*)malloc(sizeof(wchar_t)*(firstHeader.v2.DevicePathLength+1));
    if (firstHeader.v2.pwszDevicePath == NULL) return ERROR_OUTOFMEMORY;
    firstHeader.v2.pwszDevicePath[firstHeader.v2.DevicePathLength] = 0; // Null terminate string
    SetOffset(&OvlRead, uiFileOffset);
    if (!ReadFile(hFFU, firstHeader.v2.pwszDevicePath, firstHeader.v2.DevicePathLength*sizeof(wchar_t), &bytesRead, &OvlRead)) return GetLastError();
    uiFileOffset += firstHeader.v2.DevicePathLength*sizeof(wchar_t);

    //uiFileOffset = GetNextStartingArea(sizeof(BLOCK_DATA_ENTRY), uiFileOffset);
  }
  else {
    firstHeader.v2.NumOfStores = 1;
    firstHeader.v2.StoreIndex = 0;
    uiFileOffset += sizeof(STORE_HEADER) - sizeof(STORE_HEADER_V2);
    uiFileOffset = GetNextStartingArea(sizeof(BLOCK_DATA_ENTRY), uiFileOffset);
  }

  // Allocate space for all the store headers
  FFUStoreHeaders = (STORE_HEADER *)malloc(firstHeader.v2.NumOfStores*sizeof(STORE_HEADER));
  if (FFUStoreHeaders == NULL) return ERROR_OUTOFMEMORY;
  // Copy over the first header and loop through to get the rest of them
  memcpy(FFUStoreHeaders, &firstHeader, sizeof(STORE_HEADER));

  // Allocate space for storing all the data block pointers
  BlockDataEntries = (void **)malloc(firstHeader.v2.NumOfStores*sizeof(void *));
  if (BlockDataEntries == NULL) return ERROR_OUTOFMEMORY;

  // Read in first chunk of data entries this is always valid
  uiFileOffset = ReadBlockData(0, uiFileOffset);

  // Read though all the validation and block entries till we get to correct store header
  for (UINT i = 1; i < FFUStoreHeaders[0].v2.NumOfStores; i++) {
    // Stores are aligned on chunk size boundary
    uiFileOffset = GetNextStartingArea(FFUSecurityHeader.dwChunkSizeInKb * 1024, uiFileOffset);
    // Read FFUStoreHeader from the FFU
    SetOffset(&OvlRead, uiFileOffset);
    if (!ReadFile(hFFU, &FFUStoreHeaders[i], sizeof(STORE_HEADER), &bytesRead, &OvlRead)) {
      return GetLastError();
    }

    uiFileOffset += sizeof(STORE_HEADER)-4;
    
    // Read in LUN name
	//Warning C6385		Reading invalid data from 'FFUStoreHeaders' : the readable size is '_Old_12`266' bytes, but '532' bytes may be read.
	//Warning C6386		Buffer overrun while writing to 'FFUStoreHeaders' : the writable size is 'firstHeader.v2.NumOfStores*sizeof(STORE_HEADER)' bytes, but '532' bytes might be written.
#pragma prefast(suppress: 6385 6386, "The size of FFUStoreHeader = NumOfStores*sizeof(STORE_HEADER), where as the size of STORED_HEADER is 266 bytes. So the size of FFUStoreHeader can be 266 or 532 or 798 depending upon NumOfStores. So suppressing above commented warnings caused in next line.")
    FFUStoreHeaders[i].v2.pwszDevicePath = (wchar_t*)malloc(sizeof(wchar_t)*(FFUStoreHeaders[i].v2.DevicePathLength + 1));
    if (FFUStoreHeaders[i].v2.pwszDevicePath == NULL) return ERROR_OUTOFMEMORY;
    FFUStoreHeaders[i].v2.pwszDevicePath[FFUStoreHeaders[i].v2.DevicePathLength] = 0; // Null terminate string
    SetOffset(&OvlRead, uiFileOffset);
    if (!ReadFile(hFFU, FFUStoreHeaders[i].v2.pwszDevicePath, sizeof(wchar_t)*FFUStoreHeaders[i].v2.DevicePathLength, &bytesRead, &OvlRead)) return GetLastError();
    uiFileOffset += FFUStoreHeaders[i].v2.DevicePathLength*sizeof(wchar_t);

    //uiFileOffset = GetNextStartingArea(sizeof(BLOCK_DATA_ENTRY), uiFileOffset);

    // Read in the corresponding data blocks for this store header
    uiFileOffset = ReadBlockData(i, uiFileOffset);
  }

  // Update the payload data offset for each store header
  PayloadDataStart = (UINT64 *)malloc(sizeof(UINT64)*FFUStoreHeaders[0].v2.NumOfStores);
  if (PayloadDataStart == NULL) return ERROR_OUTOFMEMORY;

  // Only the first data block is aligned the reset are back to back
  uiFileOffset = GetNextStartingArea(FFUSecurityHeader.dwChunkSizeInKb * 1024, uiFileOffset);
  for (UINT i = 0; i < FFUStoreHeaders[0].v2.NumOfStores; i++) {
    PayloadDataStart[i] = uiFileOffset;
    uiFileOffset += FFUStoreHeaders[i].v2.qwStorePayloadSize;
  }

  return ERROR_SUCCESS;
}

int FFUImage::ParseHeaders(void)
{
  int status = ERROR_SUCCESS;
  UINT64 uiFileOffset = 0;
  DWORD bytesRead = 0;

  if( hFFU == INVALID_HANDLE_VALUE ) return ERROR_INVALID_HANDLE;

  /* Read the Security Header */
  SetOffset(&OvlRead, 0);
  if( !ReadFile(hFFU, &FFUSecurityHeader, sizeof(FFUSecurityHeader), &bytesRead, &OvlRead) ) {
    return ERROR_READ_FAULT;
  }

  /* Read the Image Header */

  // Get the location in the file of the ImageHeader
  uiFileOffset = sizeof(FFUSecurityHeader) + FFUSecurityHeader.dwCatalogSize + FFUSecurityHeader.dwHashTableSize;
  uiFileOffset = GetNextStartingArea(FFUSecurityHeader.dwChunkSizeInKb*1024, uiFileOffset);

  // Read FFUImageHeader from the FFU
  SetOffset(&OvlRead, uiFileOffset);
  if (!ReadFile(hFFU, &FFUImageHeader, sizeof(FFUImageHeader), &bytesRead, &OvlRead)) 
    return GetLastError();

  /* Read the StoreHeader */

  // Get the location in the file of the StoreHeader
  uiFileOffset +=  sizeof(FFUImageHeader) + FFUImageHeader.ManifestLength;
  uiFileOffset = GetNextStartingArea(FFUSecurityHeader.dwChunkSizeInKb*1024, uiFileOffset);
  
  // Read in the StoreHeader at index 0
  status = ParseStoreHeaders(uiFileOffset);
  
  return status;
}

int FFUImage::WriteDataEntry(UINT64 dwFileOffset, __int64 dwDiskOffset, UINT64 dwNumSectors, DWORD physPart) {
  int status = ERROR_SUCCESS;

  // If no raw program specified then write to disk
  if (szRawProgram == NULL) {
    // Malloc a buffer and read in the data and write to disk
    BYTE *dataBlock = (BYTE*)malloc(FFUStoreHeaders[0].dwBlockSizeInBytes * 64); // Create storage for 64*128 KB block
    if( dataBlock == NULL ) return ERROR_OUTOFMEMORY;
    while (dwNumSectors > 0) {
      DWORD dwReadSize = FFUStoreHeaders[0].dwBlockSizeInBytes * 64;
      DWORD dwBytesIn = 0;
      if(dwReadSize > dwNumSectors*DISK_SECTOR_SIZE) dwReadSize = (DWORD)(dwNumSectors*DISK_SECTOR_SIZE);
      SetOffset(&OvlRead, dwFileOffset);
      if (ReadFile(hFFU, dataBlock, dwReadSize, &dwBytesIn, &OvlRead)) {
        status = pProto->WriteData(dataBlock, dwDiskOffset*DISK_SECTOR_SIZE, dwBytesIn, &dwBytesIn, (UINT8)physPart);
      }
      else {
        status = GetLastError();
        break;
      }
      dwFileOffset += dwReadSize;
      dwNumSectors -= (dwReadSize / DISK_SECTOR_SIZE);
      dwDiskOffset += (dwReadSize / DISK_SECTOR_SIZE);
    }
    free(dataBlock);
  }
  else {
    // Write to rawprogram file
    AddEntryToRawProgram(dwFileOffset, dwDiskOffset, dwNumSectors,physPart);
  }
  return status;
}

// This function will either dump FFU information to rawprogram or to disk
// szRawProgram = NULL then dump to disk
// szRawProgram = Name of rawprogram them dump to file
int FFUImage::DumpRawProgram(int index)
{
  BOOL  bPendingData = FALSE;
  DWORD dwNextBlockIndex = 0;
  DWORD dwBlockOffset = 0;
  DWORD dwStartBlock = 0;
  DWORD dwLUN = 0;
  BLOCK_DATA_ENTRY* BlockDataEntriesPtr = (BLOCK_DATA_ENTRY*)BlockDataEntries[index];
  gpt_entry_t *gpt_hdr = NULL;
  int status = ERROR_SUCCESS;
  __int64 diskOffset = 0;

  // Map Guid to LUN 
  if (FFUStoreHeaders[index].MajorVersion >= 2) {
    dwLUN = MapLunFromGuid(FFUStoreHeaders[index].v2.pwszDevicePath);
    if (dwLUN < 0) return ERROR_INVALID_PARAMETER;
  }

  for (UINT32 i = 0; i < FFUStoreHeaders[index].dwWriteDescriptorCount; i++) {
    for (UINT32 j = 0; j < BlockDataEntriesPtr->dwLocationCount; j++){
      if (BlockDataEntriesPtr->rgDiskLocations[j].dwDiskAccessMethod == DISK_BEGIN) {
        UINT64 tmpOffset = (UINT64)BlockDataEntriesPtr->rgDiskLocations[j].dwBlockIndex*(UINT64)FFUStoreHeaders[index].dwBlockSizeInBytes;
        // If this block does not follow the previous block create a new chunk or data is not pending
        if ((BlockDataEntriesPtr->rgDiskLocations[j].dwBlockIndex != dwNextBlockIndex) || !bPendingData ||
            (gpt_hdr != NULL && tmpOffset/DISK_SECTOR_SIZE > gpt_hdr->last_lba) ) {
          // Add this entry to the rawprogram0.xml
          if (bPendingData) {
            WriteDataEntry( PayloadDataStart[index] + (UINT64)(dwStartBlock) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes),
                            (diskOffset / DISK_SECTOR_SIZE),
                            (UINT64)(dwBlockOffset - dwStartBlock) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes) / DISK_SECTOR_SIZE, dwLUN);
          }
          diskOffset = tmpOffset;
          dwStartBlock = dwBlockOffset;
          gpt_hdr = GetPartition(diskOffset/DISK_SECTOR_SIZE);
          bPendingData = TRUE;
        }

        dwNextBlockIndex = BlockDataEntriesPtr->rgDiskLocations[j].dwBlockIndex + 1;
      }
      else if (BlockDataEntriesPtr->rgDiskLocations[j].dwDiskAccessMethod == DISK_END) {
        // Add this entry to the rawprogram0.xml
        if (bPendingData) {
			if (i == 0) {
				WriteDataEntry(PayloadDataStart[index] + (UINT64)(dwStartBlock) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes),
					(diskOffset / DISK_SECTOR_SIZE),
					(UINT64)((i+1) - dwStartBlock) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes) / DISK_SECTOR_SIZE, dwLUN);
			}
			else {
				WriteDataEntry(PayloadDataStart[index] + (UINT64)(dwStartBlock) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes),
					(diskOffset / DISK_SECTOR_SIZE),
					(UINT64)(i - dwStartBlock) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes) / DISK_SECTOR_SIZE, dwLUN);
			}
        }

        diskOffset = (__int64)-1 * (__int64)FFUStoreHeaders[index].dwBlockSizeInBytes;  // End of disk is -1

        // Add this entry to the rawprogram0.xml special case this should be last chunk in the disk
        WriteDataEntry(PayloadDataStart[index] + (UINT64)(dwStartBlock) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes),
          (diskOffset / DISK_SECTOR_SIZE),
          FFUStoreHeaders[index].dwBlockSizeInBytes / DISK_SECTOR_SIZE, dwLUN);
        bPendingData = FALSE;
      }

    }

    // Increment our offset by number of blocks
    if (BlockDataEntriesPtr->dwLocationCount > 0) {
      BlockDataEntriesPtr = (BLOCK_DATA_ENTRY *)((DWORD)BlockDataEntriesPtr + (sizeof(BLOCK_DATA_ENTRY)+(BlockDataEntriesPtr->dwLocationCount - 1)*sizeof(DISK_LOCATION)));
    }
    else {
      BlockDataEntriesPtr = (BLOCK_DATA_ENTRY *)((DWORD)BlockDataEntriesPtr + sizeof(BLOCK_DATA_ENTRY));
    }
    dwBlockOffset++;
  }

  if (bPendingData) {
    // Add this entry to the rawprogram0.xml
    WriteDataEntry(PayloadDataStart[index] + (UINT64)(dwStartBlock) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes),
      (diskOffset / DISK_SECTOR_SIZE),
      (UINT64)(dwBlockOffset - dwStartBlock) * (UINT64)(FFUStoreHeaders[index].dwBlockSizeInBytes) / DISK_SECTOR_SIZE, dwLUN);
  }

  return status;
}

int FFUImage::FFUToRawProgram(TCHAR *szFFUName, TCHAR *szImageFile)
{
//  TCHAR *szFFUFile = wcsrchr(szFFUName, L'\\');
  int status = PreLoadImage(szFFUName);
  if (status != ERROR_SUCCESS) goto FFUToRawProgramExit;
  szRawProgram = szImageFile;
  status = CreateRawProgram();
  if (status != ERROR_SUCCESS) goto FFUToRawProgramExit;

  // For each store dump the data
  for (UINT i = 0; i < FFUStoreHeaders[0].v2.NumOfStores; i++) {
    status = ReadGPT(i);
    if (status != ERROR_SUCCESS) goto FFUToRawProgramExit;
    status = DumpRawProgram(i);

  }
  if (status != ERROR_SUCCESS) goto FFUToRawProgramExit;
  status = TerminateRawProgram();

FFUToRawProgramExit:
  CloseFFUFile();
  return status;
}

int FFUImage::ProgramImage(Protocol *proto, __int64 dwOffset)
{
  UNREFERENCED_PARAMETER(dwOffset); // This value is embedded in the FFU
  int status = ERROR_SUCCESS;

  // Make sure parameters passed in are valid
  if( proto == NULL ) {
    return ERROR_INVALID_PARAMETER;
  }

  // Make sure we write to disk
  szRawProgram = NULL;
  pProto = proto;

  // For each store dump the data
  for (UINT i = 0; i < FFUStoreHeaders[0].v2.NumOfStores; i++) {
    status = ReadGPT(i);
    if (status != ERROR_SUCCESS) break;
    status = DumpRawProgram(i);

  }
  return status;
}

int FFUImage::PreLoadImage(TCHAR *szFFUPath)
{
  int status = ERROR_SUCCESS;
  OvlRead.hEvent = CreateEvent(NULL, FALSE, NULL, NULL);
  OvlWrite.hEvent = CreateEvent(NULL, FALSE, NULL, NULL);
  if( (OvlRead.hEvent == NULL) || (OvlWrite.hEvent == NULL)) {
    return ERROR_OUTOFMEMORY;
  }
  ResetEvent(OvlRead.hEvent);
  ResetEvent(OvlWrite.hEvent);

  hFFU = CreateFile( szFFUPath,
    GENERIC_READ,
    FILE_SHARE_READ, // Open file for read and let others access it as well
    NULL,
    4,
    FILE_ATTRIBUTE_NORMAL,
    NULL);
  if (hFFU == INVALID_HANDLE_VALUE) return GetLastError();

  status = ParseHeaders();

  // Update the FFU file to remove directory now that we have a handle
  szFFUFile = wcsrchr(szFFUPath, L'\\');
  if (szFFUFile != NULL) szFFUFile++;
  else szFFUFile = szFFUPath;

  return status;
}

int FFUImage::SplitFFUBin( TCHAR *szPartName, TCHAR *szOutputFile)
{
  DWORD bytesRead = 0;
  DWORD start_sector = 0;
  DWORD end_sector = 0;
  int status = ERROR_SUCCESS;
  HANDLE hImage = INVALID_HANDLE_VALUE;
  DWORDLONG diskOffset = 0;
  UINT32 i=0;
  TCHAR szNewFile[256];

  for(i=0; i < MAX_GPT_ENTRIES; i++) {
    // If all is selected then dump all partitions
    if( wcscmp(szPartName,_T("all")) == 0  ) {
      // Ignore partitions without name
      if(wcscmp(GptEntries[i].part_name,_T("")) == 0)
      {
        continue;
      }
      SplitFFUBin(GptEntries[i].part_name,szOutputFile);
    } else if( wcscmp(GptEntries[i].part_name,szPartName) == 0 ) {
      start_sector = (DWORD)GptEntries[i].first_lba;
      end_sector = (DWORD)GptEntries[i].last_lba;
      break;
    }
  }

  // If no partition was found or crashdump partition return
  if( (i == MAX_GPT_ENTRIES) || (wcscmp(GptEntries[i].part_name,L"CrashDump") == 0) ) return ERROR_SUCCESS;

  swprintf_s(szNewFile,_T("%s\\%s.bin"),szOutputFile,GptEntries[i].part_name);

  for (i = 0; i < FFUStoreHeaders[0].dwWriteDescriptorCount; i++) {
    if(((BLOCK_DATA_ENTRY*)BlockDataEntries[0])[i].dwBlockCount == 0)
    {
      // No blocks attached to this entry
      continue;
    }

    diskOffset = ((BLOCK_DATA_ENTRY*)BlockDataEntries[0])[i].rgDiskLocations[0].dwBlockIndex;
    diskOffset *= FFUStoreHeaders[0].dwBlockSizeInBytes;

    if( ((diskOffset/ DISK_SECTOR_SIZE) >= start_sector) &&  ((diskOffset/ DISK_SECTOR_SIZE) <= end_sector)) {
      BYTE *dataBlock = (BYTE*)malloc(FFUStoreHeaders[0].dwBlockSizeInBytes); // Create storage for 128 KB block
      if(dataBlock == NULL ) return ERROR_OUTOFMEMORY;

      SetOffset(&OvlRead, PayloadDataStart[0]+(UINT64)(i) * (UINT64)(FFUStoreHeaders[0].dwBlockSizeInBytes) );
      if (!ReadFile(hFFU, dataBlock, FFUStoreHeaders[0].dwBlockSizeInBytes, &bytesRead, &OvlRead)) {
        status = GetLastError();
        break;
      }

      // Only create the file if there is data to write to it
      if( hImage == INVALID_HANDLE_VALUE ) {
        wprintf(_T("Exporting file: %s\n"),szNewFile);
        hImage = CreateFile( szNewFile,
          GENERIC_WRITE | GENERIC_READ,
          0,    // We want exclusive access to this disk
          NULL,
          CREATE_ALWAYS,
          FILE_ATTRIBUTE_NORMAL,
          NULL);
        if( hImage == INVALID_HANDLE_VALUE || hImage == NULL) return GetLastError();
      }

      DWORDLONG tmp = start_sector; // Needed to force 64bit calc
      tmp = tmp * DISK_SECTOR_SIZE;
      diskOffset -= tmp;

      SetOffset(&OvlWrite, diskOffset);
      WriteFile(hImage, dataBlock, FFUStoreHeaders[0].dwBlockSizeInBytes, &bytesRead, &OvlWrite);
      free(dataBlock);
    }
  }
  //}

  if(hImage != INVALID_HANDLE_VALUE || hImage != NULL) CloseHandle(hImage);
  return status;
}

int FFUImage::CloseFFUFile(void)
{
  CloseHandle(OvlRead.hEvent);
  CloseHandle(OvlWrite.hEvent);
  CloseHandle(hFFU);
  hFFU = INVALID_HANDLE_VALUE;
  return ERROR_SUCCESS;
}
