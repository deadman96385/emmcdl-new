/*****************************************************************************
 * firehose.h
 *
 * This file implements the streaming download protocol
 *
 * Copyright (c) 2007-2013
 * Qualcomm Technologies Incorporated.
 * All Rights Reserved.
 * Qualcomm Confidential and Proprietary
 *
 *****************************************************************************/
/*=============================================================================
                        Edit History

$Header: //deploy/qcom/qct/platform/wpci/prod/woa/emmcdl/main/latest/inc/firehose.h#16 $
$DateTime: 2019/06/13 16:37:39 $ $Author: wmcisvc $

when       who     what, where, why
-------------------------------------------------------------------------------
02/06/13   pgw     Initial version.
=============================================================================*/
#pragma once

#include "serialport.h"
#include "protocol.h"
#include "partition.h"
#include <stdio.h>
#include <Windows.h>

#define RAW_STATUS_TIMEOUT 10  // 10*500ms = 5 seconds
#define MAX_RETRY   50
#define ACK_TIMEOUT_COUNT 10

typedef struct {
  BYTE Version;
  char MemoryName[8];
  bool SkipWrite;
  bool SkipStorageInit;
  bool ZLPAwareHost;
  int ActivePartition;
  int MaxPayloadSizeToTargetInBytes;
  UINT8 Lun;
  int AlwaysValidate;
} fh_configure_t;

class Firehose : public Protocol {
public:
  Firehose(SerialPort *port, HANDLE hLogFile = NULL);
  ~Firehose();
  Firehose();

  int WriteData(BYTE *writeBuffer, __int64 writeOffset, DWORD writeBytes, DWORD *bytesWritten, UINT8 partNum);
  int ReadData(BYTE *readBuffer, __int64 readOffset, DWORD readBytes, DWORD *bytesRead, UINT8 partNum);

  int DeviceReset(void);
  int FastCopy(HANDLE hRead, __int64 sectorRead, HANDLE hWrite, __int64 sectorWrite, uint64 sectors, UINT8 partNum, BOOL zDump);
  int ProgramPatchEntry(PartitionEntry pe, TCHAR *key);
  int ProgramRawCommand(TCHAR *key);

  // Firehose specific operations
  int CreateGPP(DWORD dwGPP1, DWORD dwGPP2, DWORD dwGPP3, DWORD dwGPP4);
  int SetActivePartition(int prtn_num);
  int ConnectToFlashProg(fh_configure_t *cfg);
  void FirehoseInit(SerialPort *port, HANDLE hLogFile = NULL);

protected:

private:
  int ReadData(BYTE *pOutBuf, DWORD uiBufSize, bool bXML);
  int ReadStatus(void);

  SerialPort *sport;
  uint64 diskSectors;
  bool bSectorAddress;
  BYTE *m_payload;
  BYTE *m_buffer;
  BYTE *m_buffer_ptr;
  DWORD m_buffer_len;
  UINT32 dwMaxPacketSize;
  HANDLE hLog;
  char *program_pkt;
};
