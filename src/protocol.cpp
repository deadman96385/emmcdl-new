/*****************************************************************************
* protocol.cpp
*
* This class can perform emergency flashing given the transport layer
*
* Copyright (c) 2007-2015
* Qualcomm Technologies Incorporated.
* All Rights Reserved.
* Qualcomm Confidential and Proprietary
*
*****************************************************************************/
/*=============================================================================
Edit History

$Header: //deploy/qcom/qct/platform/wpci/prod/woa/emmcdl/main/latest/src/protocol.cpp#15 $
$DateTime: 2019/03/11 15:00:38 $ $Author: wmcisvc $

when       who     what, where, why
-------------------------------------------------------------------------------
12/09/14   pgw     Initial version.
=============================================================================*/

#include "protocol.h"
#include "tchar.h"
#include "firehose.h"

extern fh_configure_t m_cfg;
Protocol::Protocol(void)
{
  hDisk = INVALID_HANDLE_VALUE;
  buffer1 = buffer2 = NULL;
  gpt_entries = NULL;
  bVerbose = false;

  // Set default sector size
  DISK_SECTOR_SIZE = 512;

  zlibDump = FALSE;

#ifndef ARM64
  bufAlloc1 = (BYTE *)malloc(MAX_TRANSFER_SIZE + 0x200);
  if (bufAlloc1) buffer1 = (BYTE *)(((DWORD)bufAlloc1 + 0x200) & ~0x1ff);
  bufAlloc2 = (BYTE *)malloc(MAX_TRANSFER_SIZE + 0x200);
  if (bufAlloc2) buffer2 = (BYTE *)(((DWORD)bufAlloc2 + 0x200) & ~0x1ff);

  //zbufAlloc1 = (BYTE *)malloc(MAX_TRANSFER_SIZE + 0x400);
  //if (zbufAlloc1) zbuffer1 = (BYTE *)(((DWORD)zbufAlloc1 + 0x200) & ~0x1ff);
  //zbufAlloc2 = (BYTE *)malloc(MAX_TRANSFER_SIZE + 0x400);
  //if (zbufAlloc2) zbuffer2 = (BYTE *)(((DWORD)zbufAlloc2 + 0x200) & ~0x1ff);
  
#else
  bufAlloc1 = (BYTE *)_aligned_malloc(MAX_TRANSFER_SIZE + 0x200,512);
  if (bufAlloc1) buffer1 = (BYTE *)bufAlloc1;
  bufAlloc2 = (BYTE *)_aligned_malloc(MAX_TRANSFER_SIZE + 0x200, 512);
  if (bufAlloc2) buffer2 = (BYTE *)bufAlloc2;
#endif
}

Protocol::~Protocol(void)
{
  // Free any remaining buffers we may have
#ifndef ARM64
  if (bufAlloc1) free(bufAlloc1);
  if (bufAlloc2) free(bufAlloc2);
#else
  if (bufAlloc1) _aligned_free(bufAlloc1);
  if (bufAlloc2) _aligned_free(bufAlloc2);
#endif // ARM64
  if (gpt_entries) free(gpt_entries);
}

void Protocol::Log(char *str, ...)
{
  // For now map the log to dump output to console
  if (bVerbose) {
    va_list ap;
    va_start(ap, str);
    vprintf(str, ap);
    va_end(ap);
    printf("\n");
  }
}

void Protocol::Log(TCHAR *str, ...)
{
  // For now map the log to dump output to console
  if (bVerbose) {
    va_list ap;
    va_start(ap, str);
    vwprintf(str, ap);
    va_end(ap);
    wprintf(L"\n");
  }
}

void Protocol::EnableVerbose()
{
  bVerbose = true;
}

int Protocol::LoadPartitionInfo(TCHAR *szPartName, PartitionEntry *pEntry)
{
  int status = ERROR_SUCCESS;

  if (szPartName == NULL)
  {
	  return ERROR_NOT_FOUND;
  }
  // First of all read in the GPT information and see if it was successful
  for (UINT8 lun = 0; lun < MAX_LUNS; lun++) {
    status = ReadGPT(false, lun);
    if (status == ERROR_SUCCESS) {
      // Check to make sure partition name is found
      status = ERROR_NOT_FOUND;
      memset(pEntry, 0, sizeof(PartitionEntry));
      for (int i = 0; i < 128; i++) {
        if (_wcsicmp(szPartName, gpt_entries[i].part_name) == 0) {
          pEntry->start_sector = gpt_entries[i].first_lba;
          pEntry->num_sectors = gpt_entries[i].last_lba - gpt_entries[i].first_lba + 1;
          pEntry->physical_partition_number = lun;
          return ERROR_SUCCESS;
        }
      }
    }
  }
  return status;
}

int Protocol::WriteGPT(TCHAR *szPartName, TCHAR *szBinFile, uint64 start = 0)
{
  int status = ERROR_SUCCESS;
  PartitionEntry partEntry;

  TCHAR *cmd_pkt = (TCHAR *)malloc(MAX_XML_LEN*sizeof(TCHAR));
  if (cmd_pkt == NULL) {
    return ERROR_HV_INSUFFICIENT_MEMORY;
  }
  Partition partition;
  
  if (LoadPartitionInfo(szPartName, &partEntry) == ERROR_SUCCESS) {
    wcscpy_s(partEntry.filename, szBinFile);
    partEntry.eCmd = CMD_PROGRAM;
    swprintf_s(cmd_pkt, MAX_XML_LEN, L"<program SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"%i\"/", 
                DISK_SECTOR_SIZE, (int)partEntry.num_sectors, partEntry.physical_partition_number, (int)partEntry.start_sector);
	wprintf(L"\nProgramming: %s\n", cmd_pkt);
	status = partition.ProgramPartitionEntry(this, partEntry, cmd_pkt);
  }
  else if (start >= 0)
  {
	  memset(&partEntry, 0, sizeof(PartitionEntry));
	  wcscpy_s(partEntry.filename, szBinFile);
	  partEntry.eCmd = CMD_PROGRAM;
	  partEntry.start_sector = start;
	  partEntry.num_sectors = 0;
	  partEntry.physical_partition_number = m_cfg.Lun;
	  swprintf_s(cmd_pkt, MAX_XML_LEN, L"<program SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"%i\"/",
		  DISK_SECTOR_SIZE, (int)partEntry.num_sectors, partEntry.physical_partition_number, (int)partEntry.start_sector);
	  wprintf(L"\nProgramming: %s\n", cmd_pkt);
	  status = partition.ProgramPartitionEntry(this, partEntry, cmd_pkt);
  }
  else
  {
         wprintf(L"\nUnable to find partition : %s \n", szPartName);
         return ERROR_FILE_NOT_FOUND;
  }

  // Free our memory for command packet again
  free(cmd_pkt);
  return status;
}

static void get_guid(TCHAR* dst, char* src) {
	guid_t *guid = (guid_t *)src;
	swprintf_s(dst, GUID_LEN,L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2,
		guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
		guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

int Protocol::ReadGPT(bool debug, UINT8 lun)
{
  int status = ERROR_SUCCESS;
  DWORD bytesRead;
  gpt_header_t gpt_hdr;
  gpt_entries = (gpt_entry_t*)malloc(sizeof(gpt_entry_t)* 128);
  BYTE *databuffer = (BYTE *)malloc(DISK_SECTOR_SIZE);
  TCHAR gpt[GUID_LEN];

  if (gpt_entries == NULL || databuffer == NULL) {
    return ERROR_OUTOFMEMORY;
  }

  if (debug) wprintf(L"\n\nDumping GPT for Physical Parittion %i\n\n", lun);

  // Need to buffer data into databuffer since sector may be larger than gpt_hdr
  status = ReadData(databuffer, DISK_SECTOR_SIZE, DISK_SECTOR_SIZE, &bytesRead, lun);

#pragma prefast(suppress: 6385, "The default DISK_SECTOR_SIZE is 512, so suppressing warning [Reading invalid data from 'databuffer' : the readable size is 'DISK_SECTOR_SIZE' bytes, but '512' bytes may be read.]")
  memcpy_s((BYTE *)&gpt_hdr, sizeof(gpt_header_t), databuffer, sizeof(gpt_header_t));

  if ((status == ERROR_SUCCESS) && (memcmp("EFI PART", gpt_hdr.signature, 8) == 0)) {
    if (debug) wprintf(L"\nSuccessfully found GPT partition\n");
    status = ReadData((BYTE *)gpt_entries, 2 * DISK_SECTOR_SIZE, sizeof(gpt_entry_t) * 128, &bytesRead, lun);
    if ((status == ERROR_SUCCESS) && debug) {
      for (int i = 0; (i < gpt_hdr.num_entries) && (i < 128); i++) {
		  if (gpt_entries[i].first_lba > 0)
		  {
			  get_guid(gpt, gpt_entries[i].type_guid);
			  wprintf(L"\nLun - %i - %i. Partition Name: %s Start LBA: %I64d Size in LBA: %I64d - guid: %s", lun, i + 1, gpt_entries[i].part_name, gpt_entries[i].first_lba, gpt_entries[i].last_lba - gpt_entries[i].first_lba + 1,gpt);
		  }
      } 
    }
  }
  else {
    if (debug) wprintf(L"\nNo valid GPT found");
    free(gpt_entries);
    gpt_entries = NULL;
    status = ERROR_NOT_FOUND;
  }

  free(databuffer);
  return status;
}


uint64 Protocol::GetNumDiskSectors()
{
  return disk_size / DISK_SECTOR_SIZE;

}

void Protocol::SetDiskSectorSize(int size)
{
  DISK_SECTOR_SIZE = size;
}

int Protocol::GetDiskSectorSize(void)
{
  return DISK_SECTOR_SIZE;
}

HANDLE Protocol::GetDiskHandle(void)
{
  return hDisk;
}

int Protocol::DumpDiskContents(uint64 start_sector, uint64 num_sectors, TCHAR *szOutFile, UINT8 partNum, TCHAR *szPartName, BOOL zDump)
{
  int status = ERROR_SUCCESS;
  // If there is a partition name provided load the info for the partition name
  zlibDump = zDump;
  if (szPartName != NULL) {
    PartitionEntry pe;
    if (LoadPartitionInfo(szPartName, &pe) == ERROR_SUCCESS) {
      start_sector = pe.start_sector;
      num_sectors = pe.num_sectors;
      partNum = pe.physical_partition_number;
    }
    else {
      return ERROR_FILE_NOT_FOUND;
    }

  }

  // Create the output file and dump contents at offset for num blocks
  HANDLE hOutFile = CreateFile(szOutFile,
    GENERIC_WRITE | GENERIC_READ,
    0,    // We want exclusive access to this disk
    NULL,
    CREATE_ALWAYS,
    0, 
    NULL);

  // Make sure we were able to successfully create file handle
  if (hOutFile == INVALID_HANDLE_VALUE) {
    return GetLastError();
  }

  status = FastCopy(hDisk, start_sector, hOutFile, 0, num_sectors, partNum, zDump);
  CloseHandle(hOutFile);

  return status;
}

int Protocol::WipeDiskContents(uint64 start_sector, uint64 num_sectors, TCHAR *szPartName, UINT8 partNum)
{
  PartitionEntry pe;
  TCHAR *cmd_pkt;
  int status = ERROR_SUCCESS;

  // If there is a partition name provided load the info for the partition name
  if (szPartName != NULL) {
    PartitionEntry pe;
    if (LoadPartitionInfo(szPartName, &pe) == ERROR_SUCCESS) {
      start_sector = pe.start_sector;
      num_sectors = pe.num_sectors;
      partNum = pe.physical_partition_number;
    }
    else {
      return ERROR_FILE_NOT_FOUND;
    }

  }

  cmd_pkt = (TCHAR *)malloc(MAX_XML_LEN*sizeof(TCHAR));
  if (cmd_pkt == NULL) {
    return ERROR_HV_INSUFFICIENT_MEMORY;
  }

  memset(&pe, 0, sizeof(pe));
  wcscpy_s(pe.filename, L"ZERO");
  pe.start_sector = start_sector;
  pe.num_sectors = num_sectors;
  pe.eCmd = CMD_ERASE;
  pe.physical_partition_number = partNum;
  swprintf_s(cmd_pkt, MAX_XML_LEN, L"<program SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"%i\"/", DISK_SECTOR_SIZE, (int)num_sectors, partNum, (int)start_sector);
  Partition partition;
  status = partition.ProgramPartitionEntry(this,pe, cmd_pkt);

  free(cmd_pkt); // Free our buffer
  return status;
}

