/*****************************************************************************
* protocol.h
*
* This file implements the streaming download protocol
*
* Copyright (c) 2007-2015
* Qualcomm Technologies Incorporated.
* All Rights Reserved.
* Qualcomm Confidential and Proprietary
*
*****************************************************************************/
/*=============================================================================
Edit History

$Header: //deploy/qcom/qct/platform/wpci/prod/woa/emmcdl/main/latest/inc/protocol.h#9 $
$DateTime: 2019/03/11 15:00:38 $ $Author: wmcisvc $

when       who     what, where, why
-------------------------------------------------------------------------------
12/09/15   pgw     Initial version.
=============================================================================*/
#pragma once

#include "partition.h"
#ifdef USE_ZLIB
#include "zlib.h"
#endif // USE_ZLIB
#include <windows.h>

#define MAX_LUNS            6
#define MAX_XML_LEN         2048
#define MAX_TRANSFER_SIZE   0x100000

class Protocol {
public:
  //int ConnectToFlashProg(fh_configure_t *cfg);
  //int CreateGPP(DWORD dwGPP1, DWORD dwGPP2, DWORD dwGPP3, DWORD dwGPP4);
  //int SetActivePartition(int prtn_num);

  Protocol();
  ~Protocol();

  int DumpDiskContents(uint64 start_sector, uint64 num_sectors, TCHAR *szOutFile, UINT8 partNum, TCHAR *szPartName, BOOL zDump=false);
  int WipeDiskContents(uint64 start_sector, uint64 num_sectors, TCHAR *szPartName, UINT8 partNum);

  int ReadGPT(bool debug, UINT8 partNum);
  int WriteGPT(TCHAR *szPartName, TCHAR *szBinFile, uint64 start);
  void EnableVerbose(void);
  int GetDiskSectorSize(void);
  void SetDiskSectorSize(int size);
  uint64 GetNumDiskSectors(void);
  HANDLE GetDiskHandle(void);

  virtual int DeviceReset(void) = ERROR_SUCCESS;
  virtual int WriteData(BYTE *writeBuffer, __int64 writeOffset, DWORD writeBytes, DWORD *bytesWritten, UINT8 partNum) = ERROR_SUCCESS;
  virtual int ReadData(BYTE *readBuffer, __int64 readOffset, DWORD readBytes, DWORD *bytesRead, UINT8 partNum) = ERROR_SUCCESS;
  virtual int FastCopy(HANDLE hRead, __int64 sectorRead, HANDLE hWrite, __int64 sectorWrite, uint64 sectors, UINT8 partNum, BOOL zDump = false) = ERROR_SUCCESS;
  virtual int ProgramRawCommand(TCHAR *key) = ERROR_SUCCESS;
  virtual int ProgramPatchEntry(PartitionEntry pe, TCHAR *key) = ERROR_SUCCESS;

protected:

  int LoadPartitionInfo(TCHAR *szPartName, PartitionEntry *pEntry);
  void Log(char *str, ...);
  void Log(TCHAR *str, ...);

  gpt_header_t gpt_hdr;
  gpt_entry_t *gpt_entries;
  uint64 disk_size;
  HANDLE hDisk;
  BYTE *buffer1;
  BYTE *buffer2;
  BYTE *bufAlloc1;
  BYTE *bufAlloc2;
  #ifdef USE_ZLIB
  BYTE *zbuffer1;
  BYTE *zbuffer2;
  BYTE *zbufAlloc1;
  BYTE *zbufAlloc2;
  #endif  //USE_ZLIB
  BOOL zlibDump;
  
  int DISK_SECTOR_SIZE;

private:

  bool bVerbose;


};
