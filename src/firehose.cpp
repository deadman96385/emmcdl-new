/*****************************************************************************
 * firehose.cpp
 *
 * This class can perform emergency flashing given the transport layer
 *
 * Copyright (c) 2007-2013
 * Qualcomm Technologies Incorporated.
 * All Rights Reserved.
 * Qualcomm Confidential and Proprietary
 *
 *****************************************************************************/
/*=============================================================================
                        Edit History

$Header: //deploy/qcom/qct/platform/wpci/prod/woa/emmcdl/main/latest/src/firehose.cpp#42 $
$DateTime: 2019/10/04 07:47:23 $ $Author: wmcisvc $

when       who     what, where, why
-------------------------------------------------------------------------------
02/06/13   pgw     Initial version.
=============================================================================*/

#include "stdio.h"
#include "tchar.h"
#include "firehose.h"
#include "xmlparser.h"
#include "partition.h"
#include <exception>
#include "cinttypes"

Firehose::~Firehose()
{
  if(m_payload != NULL ) {
    //free(m_payload);
	_aligned_free(m_payload);
    m_payload = NULL;
  }
  if (program_pkt != NULL) {
	  free(program_pkt);
	  program_pkt = NULL;
  }
}

Firehose::Firehose(SerialPort *port,HANDLE hLogFile)
{
  // Initialize the serial port
  Firehose();
  hLog = hLogFile;
  sport = port;
}

Firehose::Firehose()
{
  bSectorAddress = true;
  dwMaxPacketSize = 1024*1024;
  diskSectors = 0;
  m_payload = NULL;
  program_pkt = NULL;
  m_buffer_len = 0;
  m_buffer_ptr = NULL;
}
void Firehose::FirehoseInit(SerialPort *port,HANDLE hLogFile)
{
  // Initialize the serial port
  hLog = hLogFile;
  sport = port;

}

int Firehose::ReadData(BYTE *pOutBuf, DWORD dwBufSize, bool bXML)
{
  DWORD dwBytesRead = 0;
  bool bFoundXML = false;
  int status = ERROR_SUCCESS;

  if (bXML) {
    memset(pOutBuf, 0, dwBufSize);
    // First check if there is data available in our buffer
    for (int i=0; i < 3;i++){
      for (; m_buffer_len > 0;m_buffer_len--) {
        *pOutBuf++ = *m_buffer_ptr++;
        dwBytesRead++;

        // Check for end of XML
        if (strncmp(((char *)pOutBuf) - 7, "</data>", 7) == 0) {
          m_buffer_len--;
          bFoundXML = true;
          break;
        }
      }

      // If buffer length hits 0 means we didn't find end of XML so read more data
      if (!bFoundXML) {
        // Zero out the buffer to remove any other data and reset pointer
        memset(m_buffer, 0, dwMaxPacketSize);
        m_buffer_len = dwMaxPacketSize;
        m_buffer_ptr = m_buffer;
        if (sport->Read(m_buffer, &m_buffer_len) != ERROR_SUCCESS) {
          m_buffer_len = 0;
        }
      } else {
        break;
      }
    }
  } else {
    // First copy over any extra data that may be present from previous read
    dwBytesRead = dwBufSize;
    if (m_buffer_len > 0) {
      wprintf(L"Using %i bytes from mbuffer", m_buffer_len);
      // If there is more bytes available then we have space for then only copy out the bytes we need and return
      if (m_buffer_len >= dwBufSize) {
        m_buffer_len -= dwBufSize;
        memcpy_s(pOutBuf, dwBufSize, m_buffer_ptr, dwBufSize);
        m_buffer_ptr += dwBufSize;
        return dwBufSize;
      }

      // Need all the data so copy it all and read any more data that may be present
      memcpy_s(pOutBuf, dwBufSize, m_buffer_ptr, m_buffer_len);
      pOutBuf += m_buffer_len;
      dwBytesRead = dwBufSize - m_buffer_len;
    }

    status = sport->Read(pOutBuf, &dwBytesRead);
    dwBytesRead += m_buffer_len;
    m_buffer_len = 0;
  }

  if (status != ERROR_SUCCESS)
  {
    return status;
  }

  return dwBytesRead;
}

int Firehose::ConnectToFlashProg(fh_configure_t *cfg)
{
  int status = ERROR_SUCCESS;
  DWORD dwBytesRead = dwMaxPacketSize;
  DWORD retry = 0;
  if (cfg->MaxPayloadSizeToTargetInBytes > 0)
  {
    dwMaxPacketSize = cfg->MaxPayloadSizeToTargetInBytes;
    dwBytesRead = dwMaxPacketSize;
  }

  // Allocation our global buffers only once when connecting to flash programmer
  if (m_payload == NULL || program_pkt == NULL || m_buffer == NULL) {
    //m_payload = (BYTE *)malloc(dwMaxPacketSize);
	m_payload = (BYTE *)_aligned_malloc(dwMaxPacketSize + 0x200, 512);
    program_pkt = (char *)malloc(MAX_XML_LEN);
    m_buffer = (BYTE *)malloc(dwMaxPacketSize);
    if (m_payload == NULL || program_pkt == NULL || m_buffer == NULL) {
      return ERROR_HV_INSUFFICIENT_MEMORY;
    }
  }

  // Read any pending data from the flash programmer
  memset(m_payload, 0, dwMaxPacketSize);
  dwBytesRead = ReadData((BYTE *)m_payload, dwMaxPacketSize, false);
  Log((char*)m_payload);

  // If this is UFS and didn't specify the disk_sector_size then update default to 4096
  if ((_stricmp(cfg->MemoryName, "ufs") == 0) && (DISK_SECTOR_SIZE == 512))
  {
	  wprintf(L"Programming UFS device using SECTOR_SIZE=4096\n");
    DISK_SECTOR_SIZE = 4096;
  }
  else if((_stricmp(cfg->MemoryName, "spinor") == 0) && (DISK_SECTOR_SIZE == 512))
  {
	  wprintf(L"Programming spinor device using SECTOR_SIZE=4096\n");
	  DISK_SECTOR_SIZE = 4096;
  } 
  else if ((_stricmp(cfg->MemoryName, "nand") == 0) && (DISK_SECTOR_SIZE == 512))
  {
	  wprintf(L"Programming nand device using SECTOR_SIZE=4096\n");
	  DISK_SECTOR_SIZE = 4096;
  }
  else {
	  wprintf(L"Programming device using SECTOR_SIZE=%i\n", DISK_SECTOR_SIZE);
  }



  sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version = \"1.0\" ?><data><configure MemoryName=\"%s\" ZLPAwareHost=\"%i\" SkipStorageInit=\"%i\" SkipWrite=\"%i\" MaxPayloadSizeToTargetInBytes=\"%i\" AlwaysValidate=\"%i\"/></data>",
    cfg->MemoryName, cfg->ZLPAwareHost, cfg->SkipStorageInit, cfg->SkipWrite, dwMaxPacketSize,cfg->AlwaysValidate);
  Log(program_pkt);

  status = sport->Write((BYTE *)program_pkt, (DWORD)strnlen_s(program_pkt,MAX_XML_LEN));
  if (status == ERROR_SUCCESS) {
    // Wait until we get the ACK or NAK back
    for (; retry < MAX_RETRY; retry++)
    {
      status = ReadStatus();
      if (status == ERROR_SUCCESS)
      {
        // Received an ACK to configure request we are good to go
        break;
      }
      else if (status == ERROR_INVALID_DATA)
      {
        // Received NAK to configure request check reason if it is MaxPayloadSizeToTarget then reduce and retry
        XMLParser xmlparse;
        UINT64 u64MaxSize = 0;
        // Make sure we got a configure response and set our max packet size
        xmlparse.ParseXMLInteger((char *)m_payload, "MaxPayloadSizeToTargetInBytes", &u64MaxSize);

        // If device can't handle a packet this large then change the the largest size they can use and reconfigure
        if ((u64MaxSize > 0) && (u64MaxSize  <  dwMaxPacketSize)) {
          dwMaxPacketSize = (UINT32)u64MaxSize;
          cfg->MaxPayloadSizeToTargetInBytes = dwMaxPacketSize;
          wprintf(L"We are decreasing our max packet size %i\n", dwMaxPacketSize);
          return ConnectToFlashProg(cfg);
        }
      }
    }

    if (retry == MAX_RETRY)
    {
		wprintf(_T("ERROR: No response to configure packet, Connection to flash programmer failed\n"));
      return ERROR_NOT_READY;
    }
  }
  else
  {
	  wprintf(_T("ERROR: Unable to write to port, Connection to flash programmer failed\n"));
  }

  // read out any pending data
  dwBytesRead = ReadData((BYTE *)m_payload, dwMaxPacketSize, false);
  Log((char *)m_payload);

  if (status != ERROR_SUCCESS)
  {
	  wprintf(_T("ERROR: Connection to flash programmer failed\n"));
  }
  return status;
}



int Firehose::ReadStatus(void)
{
  // Make sure we read an ACK back
  for (int i = 0; i < ACK_TIMEOUT_COUNT; i++) {
    while (ReadData(m_payload, MAX_XML_LEN, true) > 0)
    {
      // Check for ACK packet
      Log((char *)m_payload);
      if (strstr((char *)m_payload, "ACK") != NULL) {
        return ERROR_SUCCESS;
      }
      else if (strstr((char *)m_payload, "NAK") != NULL) {
        wprintf(L"\n---Target returned NAK---\n");
        return ERROR_INVALID_DATA;
      }
    }
  }
  return ERROR_NOT_READY;
}

int Firehose::ProgramPatchEntry(PartitionEntry pe, TCHAR *key)
{
  UNREFERENCED_PARAMETER(pe);
  TCHAR tmp_key[MAX_STRING_LEN];
  size_t bytesOut;
  int status = ERROR_SUCCESS;

  // Make sure we get a valid parameter passed in
  if (key == NULL) return ERROR_INVALID_PARAMETER;
  wcscpy_s(tmp_key,key);

  memset(program_pkt,0,MAX_XML_LEN);
  sprintf_s(program_pkt,MAX_XML_LEN,"<?xml version=\"1.0\" ?><data>");
  StringReplace(tmp_key,L".",L"");
  StringReplace(tmp_key,L".",L"");
  wcstombs_s(&bytesOut,&program_pkt[strlen(program_pkt)],2000,tmp_key,MAX_XML_LEN);
  strcat_s(program_pkt,MAX_XML_LEN,"></data>\n");
  status= sport->Write((BYTE*)program_pkt,(DWORD)strlen(program_pkt));
  if( status != ERROR_SUCCESS ) return status;

  Log(program_pkt);
  // There is no response to patch requests

  return ReadStatus();
}

int Firehose::WriteData(BYTE *writeBuffer, __int64 writeOffset, DWORD writeBytes, DWORD *bytesWritten, UINT8 partNum)
{
  DWORD dwBytesRead;
  int status = ERROR_SUCCESS;

  // If we are provided with a buffer read the data directly into there otherwise read into our internal buffer
  if (writeBuffer == NULL || bytesWritten == NULL) {
    return ERROR_INVALID_PARAMETER;
  }

  *bytesWritten = 0;
  memset(program_pkt, 0, MAX_XML_LEN);
  if (writeOffset >= 0) {
    sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>\n"
      "<program SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"%i\"/>"
      "\n</data>", DISK_SECTOR_SIZE, (int)writeBytes/DISK_SECTOR_SIZE, partNum, (int)(writeOffset/DISK_SECTOR_SIZE));
  }
  else { // If start sector is negative write to back of disk
    sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>\n"
      "<program SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"NUM_DISK_SECTORS%i\"/>"
      "\n</data>", DISK_SECTOR_SIZE, (int)writeBytes / DISK_SECTOR_SIZE, partNum, (int)(writeOffset / DISK_SECTOR_SIZE));
  }
  status = sport->Write((BYTE*)program_pkt, (DWORD)strlen(program_pkt));
  Log((char *)program_pkt);

  // This should have log information from device
  // Get the response after read is done

  if (ReadStatus() != ERROR_SUCCESS) goto WriteSectorsExit;

  UINT64 ticks = GetTickCount64();
  //UINT64 dwBufSize = writeBytes;

  // loop through and write the data
  dwBytesRead = dwMaxPacketSize;
  for (DWORD i = 0; i < writeBytes; ) {
    if ((writeBytes - i)  < dwMaxPacketSize) {
      dwBytesRead = (int)(writeBytes - i);
    }
    status = sport->Write(&writeBuffer[i], dwBytesRead);
    if (status != ERROR_SUCCESS) {
      goto WriteSectorsExit;
    }
    *bytesWritten += dwBytesRead;
	i += dwMaxPacketSize;
    wprintf(L"Sectors remaining %i\r", (int)(writeBytes - i));
  }

  Log("\nDownloaded raw image size %i at speed %i KB/s\n", (int)writeBytes, (int)(((writeBytes / 1024) * 1000) / (GetTickCount64() - ticks + 1)));

  // Get the response after read is done
  status = ReadStatus();

  // Read and display any other log packets we may have

WriteSectorsExit:
  return status;
}

int Firehose::ReadData(BYTE *readBuffer, __int64 readOffset, DWORD readBytes, DWORD *bytesRead, UINT8 partNum)
{
  DWORD dwBytesRead;
  int status = ERROR_SUCCESS;

  // If we are provided with a buffer read the data directly into there otherwise read into our internal buffer
  if (readBuffer == NULL || bytesRead == NULL) {
    return ERROR_INVALID_PARAMETER;
  }

  memset(program_pkt, 0, MAX_XML_LEN);
  if (readOffset >= 0) {
    sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>\n"
      "<read SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"%i\"/>"
      "\n</data>", DISK_SECTOR_SIZE, (int)readBytes/DISK_SECTOR_SIZE, partNum, (int)(readOffset/DISK_SECTOR_SIZE));
  }
  else { // If start sector is negative read from back of disk
    sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>\n"
      "<read SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"NUM_DISK_SECTORS%i\"/>"
      "\n</data>", DISK_SECTOR_SIZE, (int)readBytes / DISK_SECTOR_SIZE, partNum, (int)(readOffset / DISK_SECTOR_SIZE));
  }
  status = sport->Write((BYTE*)program_pkt, (DWORD)strlen(program_pkt));
  Log((char *)program_pkt);

  // Wait until device returns with ACK or NAK
  while ((status = ReadStatus()) == ERROR_NOT_READY);
  if (status == ERROR_INVALID_DATA) goto ReadSectorsExit;

  UINT64 ticks = GetTickCount64();
  DWORD bytesToRead = dwMaxPacketSize;

  for (UINT32 tmp_sectors = (UINT32)readBytes/DISK_SECTOR_SIZE; tmp_sectors > 0; tmp_sectors -= (bytesToRead / DISK_SECTOR_SIZE)) {
    if (tmp_sectors < dwMaxPacketSize / DISK_SECTOR_SIZE) {
      bytesToRead = tmp_sectors*DISK_SECTOR_SIZE;
    }
    else {
      bytesToRead = dwMaxPacketSize;
    }

    DWORD offset = 0;
    while (offset < bytesToRead) {
      dwBytesRead = ReadData(&readBuffer[offset], bytesToRead - offset, false);
      offset += dwBytesRead;
    }

    // Now either write the data to the buffer or handle given
    readBuffer += bytesToRead;
    *bytesRead += bytesToRead;
    wprintf(L"Sectors remaining %i\r", (int)tmp_sectors);
  }

  Log("\nDownloaded raw image at speed %i KB/s\n", (int)(readBytes / (GetTickCount64() - ticks + 1)));

  // Get the response after read is done first response should be finished command
  status = ReadStatus();

ReadSectorsExit:
  return status;
}

int Firehose::CreateGPP(DWORD dwGPP1, DWORD dwGPP2, DWORD dwGPP3, DWORD dwGPP4)
{
  int status = ERROR_SUCCESS;

  sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>\n"
                        "<createstoragedrives DRIVE4_SIZE_IN_KB=\"%i\" DRIVE5_SIZE_IN_KB=\"%i\" DRIVE6_SIZE_IN_KB=\"%i\" DRIVE7_SIZE_IN_KB=\"%i\" />"
                        "</data>",dwGPP1,dwGPP2,dwGPP3,dwGPP4);

  status= sport->Write((BYTE*)program_pkt, (DWORD)strlen(program_pkt));
  Log((char *)program_pkt);

  // Read response
  status = ReadStatus();

  // Read and display any other log packets we may have
  if (ReadData(m_payload, dwMaxPacketSize, false) > 0) Log((char*)m_payload);

  return status;
}

int Firehose::DeviceReset()
{
	int status = ERROR_SUCCESS;

	memset(program_pkt, 0, MAX_XML_LEN);
	//char reset_pkt[] = "<?xml version=\"1.0\" ?><data><power DelayInSeconds=\"2\" value=\"reset\"/></data>";

	sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data><power DelayInSeconds=\"2\" value=\"reset\"/></data>");
	status = sport->Write((BYTE*)program_pkt, (DWORD)strlen(program_pkt));
	Log((char*)program_pkt);

	// Read response
	status = ReadStatus();

	// Read and display any other log packets we may have
	if (ReadData(m_payload, dwMaxPacketSize, false) > 0) printf((char*)m_payload);

	return status;
}

int Firehose::SetActivePartition(int prtn_num)
{
  int status = ERROR_SUCCESS;

  sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>\n"
                        "<setbootablestoragedrive value=\"%i\" />"
                        "</data>\n",prtn_num);

  status= sport->Write((BYTE*)program_pkt, (DWORD)strlen(program_pkt));
  Log((char *)program_pkt);

  // Read response
  status = ReadStatus();

  // Read and display any other log packets we may have
  if (ReadData(m_payload, dwMaxPacketSize, false) > 0) Log((char*)m_payload);

  return status;
}

int Firehose::ProgramRawCommand(TCHAR *key)
{
  size_t bytesOut;
  int status = ERROR_SUCCESS;
  memset(program_pkt, 0, MAX_XML_LEN);
  sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>");
  wcstombs_s(&bytesOut, &program_pkt[strlen(program_pkt)], MAX_STRING_LEN, key, MAX_XML_LEN);
  strcat_s(program_pkt, MAX_XML_LEN, "></data>\n");
  status = sport->Write((BYTE*)program_pkt, (DWORD)strlen(program_pkt));
  printf("Programming RAW command: %s\n",(char *)program_pkt);

  // Some RAW commands can take longer like erase so keep polling status for
  // some time (5 seconds)
  for(int i=0; i < RAW_STATUS_TIMEOUT; i++) {
    if( status != ERROR_NOT_READY ) break;
  }
  if (status == ERROR_SUCCESS) {
	  status = ReadStatus();
  }
  return status;
}

extern UINT8 wth;
int Firehose::FastCopy(HANDLE hRead, __int64 sectorRead, HANDLE hWrite, __int64 sectorWrite, uint64 sectors, UINT8 partNum, BOOL zDump)
{
  DWORD dwBytesRead = 0;
  BOOL bReadStatus = TRUE;
  __int64 dwWriteOffset = sectorWrite*DISK_SECTOR_SIZE;
  __int64 dwReadOffset = sectorRead*DISK_SECTOR_SIZE;
  int status = ERROR_SUCCESS;


  int hundredmb = 0;
  UINT64 ticks = 0; // GetTickCount64();
  UINT64 curtick = 0;
  UINT64 kbpacket = dwMaxPacketSize / 1024;
  UNREFERENCED_PARAMETER(zDump);

#ifdef USE_ZLIB
  unsigned zBytesWrite = 0;
  int nextbuflen = 0;

  //Decompression variables.
  int ret;
  z_stream strm;
  /* allocate inflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;
  ret = inflateInit(&strm);
  if (ret != Z_OK)
	  return ret;
#endif // USE_ZLIB


  // If we are provided with a buffer read the data directly into there otherwise read into our internal buffer
  if (hWrite == NULL ) {
    return ERROR_INVALID_PARAMETER;
  }

  // Determine if we are writing to firehose or reading from firehose
  memset(program_pkt, 0, MAX_XML_LEN);
  if (hWrite == hDisk){
    if (sectorWrite < 0){
      sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>\n"
        "<program SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"NUM_DISK_SECTORS%i\"/>"
        "\n</data>", DISK_SECTOR_SIZE, (int)sectors, partNum, (int)sectorWrite);
    }
    else
    {
      sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>\n"
        "<program SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"%i\"/>"
        "\n</data>", DISK_SECTOR_SIZE, (int)sectors, partNum, (int)sectorWrite);
    }
  }
  else
  {
    sprintf_s(program_pkt, MAX_XML_LEN, "<?xml version=\"1.0\" ?><data>\n"
      "<read SECTOR_SIZE_IN_BYTES=\"%i\" num_partition_sectors=\"%i\" physical_partition_number=\"%i\" start_sector=\"%i\"/>"
      "\n</data>", DISK_SECTOR_SIZE, (int)sectors, partNum, (int)sectorRead);
  }

  // Write out the command and wait for ACK/NAK coming back
  status = sport->Write((BYTE*)program_pkt, (DWORD)strlen(program_pkt));
  Log((char *)program_pkt);

  // Wait until device returns with ACK or NAK
  while ((status = ReadStatus()) == ERROR_NOT_READY);

  // The ReadStatus will use our zero buffer so set this data down here
  if (hRead == INVALID_HANDLE_VALUE) {
    wprintf(L"hRead = INVALID_HANDLE_VALUE, zeroing input buffer\n");
    memset(m_payload, 0, dwMaxPacketSize);
    dwBytesRead = dwMaxPacketSize;
  }
  else {
    // Move file pointer to the give location if value specified is > 0
    if (sectorRead > 0) {
      LONG sectorReadHigh = dwReadOffset >> 32;
      status = SetFilePointer(hRead, (LONG)dwReadOffset, &sectorReadHigh, FILE_BEGIN);
      if (status == INVALID_SET_FILE_POINTER) {
        status = GetLastError();
        wprintf(L"Failed to set offset 0x%I64x status: %i\n", sectorRead, status);
        return status;
      }
      status = ERROR_SUCCESS;
    }
  }
  ticks = GetTickCount64();
  if (status == ERROR_SUCCESS)
  {
    DWORD bytesToRead;
    for (UINT32 tmp_sectors = (UINT32)sectors; tmp_sectors > 0; ) {
      bytesToRead = dwMaxPacketSize;
      if (tmp_sectors < dwMaxPacketSize / DISK_SECTOR_SIZE) {
        bytesToRead = tmp_sectors*DISK_SECTOR_SIZE;
      }
	  
      if (hWrite == hDisk)
      {
        // Writing to disk and reading from file...
        dwBytesRead = 0;
		// Additional Check for NULL to fix prefast warnings
		if ((hRead != INVALID_HANDLE_VALUE) && (hRead != NULL)) {
			bReadStatus = ReadFile(hRead, m_payload, bytesToRead, &dwBytesRead, NULL);
		}
#ifdef USE_ZLIB



		//---------------------------------- decompress zbin files here -------------------------------------------------------
		if (zDump)
		{
			if (!dwBytesRead)
			{
				wprintf(L"Breakig as bytes read is zero\n");
				break;
			}
			strm.avail_in = dwBytesRead;
			strm.next_in = (Bytef *)m_payload;

			/* run inflate() on input until output buffer not full */
			do {
				strm.avail_out = MAX_TRANSFER_SIZE - nextbuflen;
				strm.next_out = (Bytef *)(zbuffer1 + nextbuflen);
				ret = inflate(&strm, Z_NO_FLUSH);
				
				switch (ret) {
				case Z_NEED_DICT:
					ret = Z_DATA_ERROR;     /* and fall through */
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
					(void)inflateEnd(&strm);
					return ret;
				}
				
				if (nextbuflen == 0)
				{
					zBytesWrite = MAX_TRANSFER_SIZE - strm.avail_out;
				}
				else
				{
					zBytesWrite = (MAX_TRANSFER_SIZE - nextbuflen) - strm.avail_out;
				}
				
				if (bReadStatus) {
					//Write to flashprogrammer only if buffer reached MAX_TRANSFER_SIZE
					if ((zBytesWrite + nextbuflen) == MAX_TRANSFER_SIZE)
					{
						status = sport->Write(zbuffer1, MAX_TRANSFER_SIZE);
						if (status != ERROR_SUCCESS) {
							break;
						}
						dwWriteOffset += zBytesWrite;
						nextbuflen = 0;
						tmp_sectors -= (MAX_TRANSFER_SIZE / DISK_SECTOR_SIZE);
						wprintf(L"Sectors remaining %u\r", tmp_sectors);
					}
					else
					{
						// Bufferup to align with MAX_TRANSFER_SIZE
						nextbuflen += zBytesWrite;
					}
				}
				else {
					// If there is partial data read out then write out next chunk to finish this up
					status = GetLastError();
					break;
				}
				if (tmp_sectors <= 0)
					break;
			} while (strm.avail_out == 0);
			if (tmp_sectors <= 0)
				break;
			//---------------------------------- end decompress zbin  -------------------------------------------------------
		}
		else
#endif // USE_ZLIB
		{
			//Flash regular files here.
			if (bReadStatus) {
				status = sport->Write(m_payload, bytesToRead);
				if (status != ERROR_SUCCESS) {
					break;
				}
				dwWriteOffset += dwBytesRead;
			}
			else {
				// If there is partial data read out then write out next chunk to finish this up
				status = GetLastError();
				break;
			}
			tmp_sectors -= (bytesToRead / DISK_SECTOR_SIZE);
		}

      }// Else this is a read command so read data from device in dwMaxPacketSize chunks
      else {
        DWORD offset = 0;
        while (offset < bytesToRead) {
          dwBytesRead = ReadData(&m_payload[offset], bytesToRead - offset, false);
          offset += dwBytesRead;
        }
        // Now either write the data to the buffer or handle given
        if (!WriteFile(hWrite, m_payload, bytesToRead, &dwBytesRead, NULL))  {
          // Failed to write out the data
          status = GetLastError();
          break;
        }
		tmp_sectors -= (bytesToRead / DISK_SECTOR_SIZE);
      }
	  hundredmb++;
 
	  if (hundredmb == 100)
	  {
		  hundredmb = 0;
		  curtick = GetTickCount64() - (ticks + 1);
		  if (kbpacket <= 32)
		  {
			  wprintf(L"\nCompleted: %i%%, Transferred %fMb in %f secs, Transfer speed: %d Kb/s\n", (100 - (int)((100 * tmp_sectors) / sectors)), (float)kbpacket * 100 / 1024, (float)curtick / 1000, (int)(kbpacket * 100 * 1000 / curtick));
		  }
		  else
		  {
			  wprintf(L"\nCompleted: %i%%, Transferred 100Mb in %f secs, Transfer speed: %i Mb/s\n", (100 - (int)((100 * tmp_sectors) / sectors)), (float)curtick / 1000, (int)(kbpacket * 100 * 1000 / curtick) / 1024);
		  }
		  ticks = GetTickCount64();
	  }
	  if (wth == 0)
	  {
		  wprintf(L"Sectors remaining %i\r", (int)tmp_sectors);
	  }
	  else
	  {
		  if(hundredmb % wth == 0)
			  wprintf(L"Sectors remaining %i\r", (int)tmp_sectors);
	  }
    }
  }
#ifdef USE_ZLIB
  if (zDump)
  {
	  (void)inflateEnd(&strm);
  }
#endif // USE_ZLIB

  // Get the response after raw transfer is completed
  if (status == ERROR_SUCCESS) {
    status = ReadStatus();
  }

  return status;
}
