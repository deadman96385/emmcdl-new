/*****************************************************************************
 * sahara.cpp
 *
 * This class implements sahara protocol support
 *
 * Copyright (c) 2007-2011
 * Qualcomm Technologies Incorporated.
 * All Rights Reserved.
 * Qualcomm Confidential and Proprietary
 *
 *****************************************************************************/
/*=============================================================================
                        Edit History

$Header: //deploy/qcom/qct/platform/wpci/prod/woa/emmcdl/main/latest/src/sahara.cpp#19 $
$DateTime: 2018/09/03 09:38:50 $ $Author: wmcisvc $

when       who     what, where, why
-------------------------------------------------------------------------------
30/10/12   pgw     Initial version.
=============================================================================*/

#include "sahara.h"
extern int conn_timeout;

Sahara::Sahara(SerialPort *port, HANDLE hLogFile)
{
  sport = port;
  hLog = hLogFile;
}

void Sahara::Log(char *str,...)
{
  // For now map the log to dump output to console
  va_list ap;
  va_start(ap,str);
  vprintf(str,ap);
  va_end(ap);
}
void Sahara::Log(TCHAR *str,...)
{
  // For now map the log to dump output to console
  va_list ap;
  va_start(ap,str);
  vwprintf(str,ap);
  va_end(ap);
}

int Sahara::DeviceReset()
{
  execute_cmd_t exe_cmd;
  exe_cmd.cmd = SAHARA_RESET_REQ;
  exe_cmd.len = 0x8;
  return sport->Write((BYTE *)&exe_cmd, sizeof(exe_cmd));
}

int Sahara::MemoryDump(UINT64 addr, UINT32 len)
{
  int status;
  DWORD bytesRead;

  memory_read_64_t read_cmd;
  read_cmd.cmd = SAHARA_64BIT_MEMORY_DEBUG;
  read_cmd.len = sizeof(read_cmd);
  read_cmd.addr = addr;
  read_cmd.data_len = len;

  if( len > 256 ) {
    wprintf(L"Can only dump max 256 bytes\n");
    return -ERROR_INVALID_PARAMETER;
  }
  wprintf(L"Dumping at addr: 0x%x\n",read_cmd.addr);
  wprintf(L"Data Length: 0x%x\n",read_cmd.data_len);

  status = sport->Write((BYTE *)&read_cmd, sizeof(read_cmd));
  if (status != ERROR_SUCCESS) {
    return -ERROR_INVALID_HANDLE;
  }

  BYTE raw_data[256];
  bytesRead = len;
  status = sport->Read(raw_data, &bytesRead);
  for(UINT32 i=0; i < bytesRead; i++) {
    wprintf(L"0x%x ",raw_data[i]);
    // Insert a newline every 16 characters
    if( ((i+1) & 0xF) == 0 ) wprintf(L"\n");
  }
  wprintf(L"\n");
  return ERROR_SUCCESS;
}

int Sahara::ReadData(int cmd, BYTE *buf, int len)
{
  int status;
  DWORD bytesRead;

  execute_cmd_t exe_cmd;
  exe_cmd.cmd = SAHARA_EXECUTE_REQ;
  exe_cmd.len = sizeof(exe_cmd);
  exe_cmd.client_cmd = cmd;

  status = sport->Write((BYTE *)&exe_cmd, sizeof(exe_cmd));
  if (status != ERROR_SUCCESS) {
    return -ERROR_INVALID_HANDLE;
  }

  execute_rsp_t exe_rsp;
  bytesRead = sizeof(exe_rsp);
  status = sport->Read((BYTE*)&exe_rsp, &bytesRead);
  if (exe_rsp.data_len > 0 && bytesRead == sizeof(exe_rsp)) {
    if (exe_rsp.data_len > (DWORD)(len)) {
      wprintf(L"Data Buffer not sufficient; Expected buffer (%d) ,Actual Buffer (%d)\n", exe_rsp.data_len, len);
      return -ERROR_INVALID_DATA;
    }
    exe_cmd.cmd = SAHARA_EXECUTE_DATA;
    exe_cmd.len = sizeof(exe_cmd);
    exe_cmd.client_cmd = cmd;
    status = sport->Write((BYTE *)&exe_cmd, sizeof(exe_cmd));
    if (status != ERROR_SUCCESS) {
      return -ERROR_INVALID_HANDLE;
    }
    bytesRead = len;
    status = sport->Read((BYTE *)buf, &bytesRead);
    return bytesRead;
  }

  return -ERROR_INVALID_DATA;
}

int Sahara::DumpDeviceInfo(pbl_info_t *pbl_info)
{
  DWORD dataBuf[64];
  int status = ERROR_SUCCESS;

  // Connect to the device in command mode
  status = ConnectToDevice(true, 3);
  if( status != ERROR_SUCCESS) {
    return status;
  }

  // Make sure we get command ready back
  execute_rsp_t cmd_rdy;
  DWORD bytesRead = sizeof(cmd_rdy);

  status = sport->Read((BYTE *)&cmd_rdy, &bytesRead);
  if (status != ERROR_SUCCESS || bytesRead == 0 ) {
    wprintf(L"No response from device after switch mode command\n");
    return ERROR_INVALID_PARAMETER;
  } else if (cmd_rdy.cmd != SAHARA_CMD_READY) {
    wprintf(L"Unexpected response for command mode %i\n", cmd_rdy.cmd);
    return ERROR_INVALID_PARAMETER;
  }

  // Dump out all the device information
  status = ReadData(SAHARA_EXEC_CMD_SERIAL_NUM_READ, (BYTE*)dataBuf, sizeof(dataBuf));
  if ( status > 0 && status < sizeof(dataBuf)) {
    pbl_info->serial = dataBuf[0];
  }
  status = ReadData(SAHARA_EXEC_CMD_MSM_HW_ID_READ, (BYTE*)dataBuf, sizeof(dataBuf));
  if (status > 0 && status < sizeof(dataBuf)) {
    pbl_info->msm_id = dataBuf[1];
  }
  status = ReadData(SAHARA_EXEC_CMD_OEM_PK_HASH_READ, (BYTE*)dataBuf, sizeof(dataBuf));
  if (status > 0 && status < sizeof(dataBuf)) {
    memcpy_s(pbl_info->pk_hash, 32, dataBuf, sizeof(pbl_info->pk_hash));
  }
  status = ReadData(SAHARA_EXEC_CMD_GET_SOFTWARE_VERSION_SBL, (BYTE*)dataBuf, sizeof(dataBuf));
  if (status > 0 && status < sizeof(dataBuf)) {
    pbl_info->pbl_sw = dataBuf[0];
  }

  ModeSwitch(0);
  return ERROR_SUCCESS;
}

int Sahara::DumpDebugInfo(TCHAR *szFileName)
{
    DWORD dataBuf[256];
    HANDLE hDbgFile = NULL;
    DWORD dwBytesOut;
    int status = ERROR_SUCCESS;

    // Connect to the device in command mode
    status = ConnectToDevice(true, 3);
    if (status != ERROR_SUCCESS) {
        return status;
    }

   // Make sure we get command ready back
    execute_rsp_t cmd_rdy;
    DWORD bytesRead = sizeof(cmd_rdy);
    status = sport->Read((BYTE *)&cmd_rdy, &bytesRead);
    if (status != ERROR_SUCCESS || bytesRead == 0) {
        wprintf(L"No response from device after switch mode command\n");
        return ERROR_INVALID_PARAMETER;
    }
    else if (cmd_rdy.cmd != SAHARA_CMD_READY) {
        wprintf(L"Unexpected response for command mode %i\n", cmd_rdy.cmd);
        return ERROR_INVALID_PARAMETER;
    }
    memset(dataBuf, 0x0, sizeof(dataBuf));
    status = ReadData(SAHARA_EXEC_CMD_READ_DEBUG_DATA, (BYTE*)dataBuf, sizeof(dataBuf));
    if (status > 0 && status < sizeof(dataBuf)) {
        hDbgFile = CreateFile(szFileName,
                              GENERIC_WRITE | GENERIC_READ,
                              0,    // We want exclusive access to this disk
                              NULL,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);
        if (hDbgFile == INVALID_HANDLE_VALUE) {
            return GetLastError();
        }
        wprintf(L"Debug Info is %d bytes\n", status);
        WriteFile(hDbgFile, (BYTE*)dataBuf, status, &dwBytesOut, NULL);
        CloseHandle(hDbgFile);
    }
    else {
        wprintf(L"Failure in reading Debug Info(=%d)\n", status);
    }

    ModeSwitch(0);
    return ERROR_SUCCESS;
}

int Sahara::ModeSwitch(int mode)
{
  // Finally at the end send switch mode to put us back in original EDL state
  int status = ERROR_SUCCESS;
  execute_cmd_t cmd_switch_mode;
  BYTE read_data_req[256];
  DWORD bytesRead = sizeof(read_data_req);

  cmd_switch_mode.cmd = SAHARA_SWITCH_MODE;
  cmd_switch_mode.len = sizeof(cmd_switch_mode);
  cmd_switch_mode.client_cmd = mode;
  status = sport->Write((BYTE*)&cmd_switch_mode, sizeof(cmd_switch_mode));

  // Read the response to switch mode if the device responds
  status = sport->Read((BYTE *)&read_data_req, &bytesRead);
  return status;
}

int Sahara::LoadFlashProg(TCHAR *szFlashPrg)
{
  read_data_t read_data_req = {0};
  read_data_64_t read_data64_req = {0};
  cmd_hdr_t read_cmd_hdr = { 0 };
  image_end_t read_img_end = { 0 };
  DWORD status = ERROR_SUCCESS;
  DWORD bytesRead = sizeof(read_data64_req);
  DWORD totalBytes = 0, read_data_offset = 0, read_data_len = 0;
  done_t done_pkt = { 0 };
  HANDLE hFlashPrg = INVALID_HANDLE_VALUE;
  BYTE *dataBuf = (BYTE *)malloc(128*1024);

  if (dataBuf == NULL) {
    status = ERROR_OUTOFMEMORY;
    goto LoadFlashProgExit;
  }

  hFlashPrg = CreateFile( szFlashPrg,
                     GENERIC_READ,
                     FILE_SHARE_READ,
                     NULL,
                     OPEN_EXISTING,
                     0,
                     NULL);
  if( hFlashPrg == INVALID_HANDLE_VALUE ) {
    status = ERROR_OPEN_FAILED;
    goto LoadFlashProgExit;
  }

  wprintf(L"Successfully open flash programmer to write: %s\n",szFlashPrg);

  for(;;) {

    // Try to receive data up to 5 times if device is slow
    for(int i=0; i < 5; i++) {
      memset(&read_cmd_hdr,0,sizeof(read_cmd_hdr));
      bytesRead = sizeof(read_cmd_hdr);
      status = sport->Read((BYTE *)&read_cmd_hdr,&bytesRead);
      if (bytesRead > 0) break;
      Sleep(100);
    }

    // Check if it is a 32-bit or 64-bit read
    if (read_cmd_hdr.cmd == SAHARA_64BIT_MEMORY_READ_DATA)
    {
      memset(&read_data64_req, 0, sizeof(read_data64_req));
      bytesRead = sizeof(read_data64_req);
      status = sport->Read((BYTE *)&read_data64_req, &bytesRead);
      read_data_offset = (UINT32)read_data64_req.data_offset;
      read_data_len = (UINT32)read_data64_req.data_len;
      if (read_data_len > (128 * 1024)) {
        wprintf(L"Trying to read too large a packet\n");
        status = ERROR_INSUFFICIENT_BUFFER;
        goto LoadFlashProgExit;
      }
    }
    else if (read_cmd_hdr.cmd == SAHARA_READ_DATA)
    {
      memset(&read_data_req, 0, sizeof(read_data_req));
      bytesRead = sizeof(read_data_req);
      status = sport->Read((BYTE *)&read_data_req, &bytesRead);
      read_data_offset = read_data_req.data_offset;
      read_data_len = read_data_req.data_len;
    }
    else {
      // Didn't find any read data command so break
      break;
    }

    // Set the offset in the file requested
    if (SetFilePointer(hFlashPrg, read_data_offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
      wprintf(L"Failed to update FlashProgrammer pointer offset\n");
      status = GetLastError();
      goto LoadFlashProgExit;
    }

    if (!ReadFile(hFlashPrg, dataBuf, read_data_len, &bytesRead, NULL)) {
      wprintf(L"Failed to read data from flash programmer file\n");
      status = GetLastError();
      goto LoadFlashProgExit;
    }

    //wprintf(L"FileOffset: %i requested: %i bytesWrite: %i\n", read_data_offset, read_data_len, bytesRead);

    if (sport->Write(dataBuf,bytesRead) != ERROR_SUCCESS ) {
      wprintf(L"Failed to write data to device in IMEM\n");
      status = GetLastError();
      goto LoadFlashProgExit;
    }
    totalBytes += bytesRead;
  }

  // Done loading flash programmer so close handle
  CloseHandle(hFlashPrg);
  hFlashPrg = INVALID_HANDLE_VALUE;

  if (bytesRead == 0) {
    wprintf(L"Device failed to send any commands after last command\n");
    status = ERROR_TIMEOUT;
    goto LoadFlashProgExit;
  }

  if (read_cmd_hdr.cmd != SAHARA_END_TRANSFER) {
	  wprintf(L"Expecting SAHARA_END_TRANSFER but found: %i\n", read_cmd_hdr.cmd);
    status = ERROR_WRITE_FAULT;
    goto LoadFlashProgExit;
  }

  // Read the remaining part of the end transfer packet
  bytesRead = sizeof(read_img_end);
  status = sport->Read((BYTE *)&read_img_end, &bytesRead);
  if (read_img_end.status != SAHARA_ERROR_SUCCESS) {
	  wprintf(L"Image load failed with status: %i\n", read_img_end.status);
	  status = read_img_end.status;
    goto LoadFlashProgExit;
  }

  done_pkt.cmd = SAHARA_DONE_REQ;
  done_pkt.len = 8;
  status = sport->Write((BYTE *)&done_pkt,8);
  if( status != ERROR_SUCCESS) {
    goto LoadFlashProgExit;
  }

  bytesRead = sizeof(done_pkt);
  status = sport->Read((BYTE*)&done_pkt,&bytesRead);
  if( done_pkt.cmd != SAHARA_DONE_RSP) {
	wprintf(L"Expecting SAHARA_DONE_RSP but found: %i", done_pkt.cmd);
    status = ERROR_WRITE_FAULT;
    goto LoadFlashProgExit;
  }

  // After Sahara is done set timeout back to default for firehose
  sport->SetTimeout(conn_timeout);
  Sleep(500);

LoadFlashProgExit:
  free(dataBuf);
  if(hFlashPrg != INVALID_HANDLE_VALUE) CloseHandle(hFlashPrg);
  return status;
}

bool Sahara::CheckDevice(void)
{
  hello_req_t hello_req = {0};
  int status = ERROR_SUCCESS;
  DWORD bytesRead = sizeof(hello_req);
  sport->SetTimeout(1);
  status = sport->Read((BYTE *)&hello_req,&bytesRead);
  sport->SetTimeout(1000);
  return ( hello_req.cmd == SAHARA_HELLO_REQ );
}

int Sahara::ConnectToDevice(bool bReadHello, int mode)
{
  hello_req_t hello_req = {0};
  int status = ERROR_SUCCESS;
  DWORD bytesRead = sizeof(hello_req);

  if (bReadHello) {
    sport->SetTimeout(10);
    status = sport->Read((BYTE *)&hello_req, &bytesRead);
    if (hello_req.cmd != SAHARA_HELLO_REQ) {
      // Read out any pending data to get to a good state
      while(bytesRead > 0) sport->Read((BYTE *)&hello_req, &bytesRead);
      // If no hello packet is waiting then try PBL hack to bring device to good state
      if (PblHack() != ERROR_SUCCESS) {
        // If we fail to connect try to do a mode switch command to get hello packet and try again
        wprintf(L"Did not receive Sahara hello packet from device\n");
        return ERROR_INVALID_HANDLE;
      }
    }
    sport->SetTimeout(2000);
  } else {
    // Read any pending data
    sport->Flush();
  }

  hello_req.cmd = SAHARA_HELLO_RSP;
  hello_req.len = 0x30;
  hello_req.version = 2;
  hello_req.version_min=1;
  hello_req.max_cmd_len = 0;
  hello_req.mode = mode;

  status = sport->Write((BYTE *)&hello_req, sizeof(hello_req));

  if (status != ERROR_SUCCESS) {
    wprintf(L"Failed to write hello response back to device\n");
    return ERROR_INVALID_HANDLE;
 }

  return ERROR_SUCCESS;
}

// This function is to fix issue where PBL does not propery handle PIPE reset need to make sure 1 TX and 1 RX is working we may be out of sync...
int Sahara::PblHack(void)
{
  int status = ERROR_SUCCESS;

  // Assume that we already got the hello req so send hello response
  status = ConnectToDevice(false, 3);
  if (status != ERROR_SUCCESS) {
    return status;
  }

  // Make sure we get command ready back
  execute_rsp_t cmd_rdy;
  DWORD bytesRead = sizeof(cmd_rdy);

  sport->SetTimeout(10);
  status = sport->Read((BYTE *)&cmd_rdy, &bytesRead);
  sport->SetTimeout(conn_timeout);
  if (status != ERROR_SUCCESS || bytesRead == 0) {
    // Assume there was a data toggle issue and send the mode switch command
    return ModeSwitch(0);
  } else if (cmd_rdy.cmd != SAHARA_CMD_READY) {
    wprintf(L"PblHack: did not work - %i\n", cmd_rdy.cmd);
    return ERROR_INVALID_DATA;
  }

  // Successfully got the CMD_READY so now switch back to normal mode
  return ModeSwitch(0);
}
