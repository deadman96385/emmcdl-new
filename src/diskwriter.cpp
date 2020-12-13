/*****************************************************************************
 * diskwritter.cpp
 *
 * This file implements disk information and dumping files to disk
 *
 * Copyright (c) 2007-2011
 * Qualcomm Technologies Incorporated.
 * All Rights Reserved.
 * Qualcomm Confidential and Proprietary
 *
 *****************************************************************************/
/*=============================================================================
                        Edit History

$Header: //deploy/qcom/qct/platform/wpci/prod/woa/emmcdl/main/latest/src/diskwriter.cpp#25 $
$DateTime: 2019/08/12 05:20:06 $ $Author: wmcisvc $

when       who     what, where, why
-------------------------------------------------------------------------------
10/10/11   pgw     Keep volume open otherwise windows will remount it on us
06/28/11   pgw     Aligned all buffers to 128 bytes for ARM SDCC suport
05/18/11   pgw     Changed to uint64 use local handle rather than passing it in
                   updated performance tests and API's.
04/13/11   pgw     Fixed bug for empty card readers.
03/21/11   pgw     Initial version.
=============================================================================*/

#include "diskwriter.h"
#include "winerror.h"
#include <winioctl.h>
#include <string.h>
#include <stdio.h>
#include "tchar.h"
#include "windows.h"
#include "sahara.h"

// Only List COM port information for desktop version
#ifndef ARM
#include "setupapi.h"
#define INITGUID 1    // This is needed to properly define the GUID's in devpkey.h
#include "devpkey.h"
#endif //ARM

DiskWriter::DiskWriter()
{
  ovl.hEvent = CreateEvent(NULL, FALSE, FALSE,NULL);
  hVolume = INVALID_HANDLE_VALUE;
  bPatchDisk = false;

  // Create some nice 128 byte aligned buffers required by ARM
  disks = NULL;
  volumes = NULL;
  gpt_entries = (gpt_entry_t*)malloc(sizeof(gpt_entry_t)*256);
  disks = (disk_entry_t*)malloc(sizeof(disk_entry_t)*MAX_DISKS);
  volumes = (vol_entry_t*)malloc(sizeof(vol_entry_t)*MAX_VOLUMES);
  
}

DiskWriter::~DiskWriter()
{
  if(disks) free(disks);
  if(volumes) free(volumes);
  CloseHandle(ovl.hEvent);
}

int DiskWriter::ProgramRawCommand(TCHAR *key)
{
  UNREFERENCED_PARAMETER(key);
  return ERROR_INVALID_FUNCTION;
}

int DiskWriter::DeviceReset(void)
{
  return ERROR_INVALID_FUNCTION;
}


int DiskWriter::GetVolumeInfo(vol_entry_t *vol)
{
  TCHAR mount[MAX_PATH+1];
  DWORD mountsize;

  //try {
    // Clear out our mount path
    memset(mount,0,sizeof(mount));

    if( GetVolumePathNamesForVolumeName(vol->rootpath,mount,MAX_PATH,&mountsize) ) {
      DWORD maxcomplen;
      DWORD fsflags;
      wcscpy_s(vol->mount, mount);
      // Chop of the last trailing char
      vol->mount[wcslen(vol->mount)-1] = 0; 
      vol->drivetype = GetDriveType(vol->mount);
      // We only fill out information for DRIVE_FIXED and DRIVE_REMOVABLE
      if( vol->drivetype == DRIVE_REMOVABLE || vol->drivetype == DRIVE_FIXED ) {
        GetVolumeInformation(vol->mount,vol->volume,MAX_PATH+1,(LPDWORD)&vol->serialnum,&maxcomplen,&fsflags,vol->fstype,MAX_PATH);
        // Create handle to volume and get disk number
        TCHAR volPath[MAX_PATH+1] = _T("\\\\.\\");
        wcscat_s(volPath,vol->mount);

        // Create the file using the volume name
        hDisk = CreateFile( volPath,
                          GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
        if( hDisk != INVALID_HANDLE_VALUE ) {
          // Now get physical disk number
          _STORAGE_DEVICE_NUMBER devInfo;
          DWORD bytesRead;
          DeviceIoControl( hDisk,IOCTL_STORAGE_GET_DEVICE_NUMBER,NULL,0,&devInfo,sizeof(devInfo),&bytesRead,NULL);
          vol->disknum = devInfo.DeviceNumber;
          CloseHandle(hDisk);
        }

      } 
    }
  //} catch(exception &e) {
  //  wprintf(L"Exception in GetVolumeInfo: %s\n", e.what());
  //}

  return ERROR_SUCCESS;
}

int DiskWriter::GetDiskInfo(disk_entry_t *de)
{
  TCHAR tPath[MAX_PATH+1];
  
  int status = ERROR_SUCCESS;

  // Create path to physical drive
  swprintf_s(tPath, _T("\\\\.\\PhysicalDrive%i"), de->disknum);
  // Create the file using the physical drive
  hDisk = CreateFile(tPath,
    GENERIC_READ,
    (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE),
    NULL,
    OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED,
    NULL);
  if (hDisk != INVALID_HANDLE_VALUE) {
    DISK_GEOMETRY_EX info;
    DWORD bytesRead;
    if (DeviceIoControl(hDisk,
      IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,    // dwIoControlCode
      NULL,                          // lpInBuffer
      0,                             // nInBufferSize
      &info,                         // output buffer
      sizeof(info),                  // size of output buffer
      &bytesRead,                    // number of bytes returned
      &ovl                           // OVERLAPPED structure
      ))
    {
      wcscpy_s(de->diskname, MAX_PATH, tPath);
      de->disksize = *(uint64*)(&info.DiskSize);
      de->blocksize = info.Geometry.BytesPerSector;
    }
    else {
      status = ERROR_NO_VOLUME_LABEL;
    }
    CloseHandle(hDisk);
  }
  else {
    status = GetLastError();
  }


  if (status != ERROR_SUCCESS) {
    de->disknum = -1;
    de->disksize = (uint64)-1;
    de->diskname[0] = 0;
    de->volnum[0] = -1;
  }

  return ERROR_SUCCESS;
}

int DiskWriter::InitDiskList(bool verbose)
{
  HANDLE vHandle;
  TCHAR VolumeName[MAX_PATH+1];
  BOOL bValid = true;
  int i=0;
#ifndef ARM
  HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_COMPORT,NULL,NULL,DIGCF_DEVICEINTERFACE|DIGCF_PRESENT);
  DEVPROPTYPE ulPropertyType = DEVPROP_TYPE_STRING;
  DWORD dwSize;
#else //ARM
  UNREFERENCED_PARAMETER(verbose);
#endif
  if( disks == NULL || volumes == NULL ) {
    return ERROR_INVALID_PARAMETER;
  }

  wprintf(L"Finding all devices in emergency download mode...\n");

#ifndef ARM
  if( hDevInfo != INVALID_HANDLE_VALUE ) {
    // Successfully got a list of ports
    for(int i=0; ;i++) {
      WCHAR szBuffer[512];
      SP_DEVINFO_DATA DeviceInfoData;
      DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
      if( !SetupDiEnumDeviceInfo(hDevInfo,i,&DeviceInfoData) && (GetLastError() == ERROR_NO_MORE_ITEMS) ) {
        // No more ports
        break;
      }
      // successfully found entry print out the info
      if( SetupDiGetDeviceProperty(hDevInfo,&DeviceInfoData,&DEVPKEY_Device_FriendlyName,&ulPropertyType,(BYTE*)szBuffer, sizeof(szBuffer), &dwSize, 0)) {
        if( (GetLastError() == ERROR_SUCCESS) && (wcsstr(szBuffer,L"QDLoader 9008") || wcsstr(szBuffer, L"EDL 90E2"))!= NULL ) {
          wprintf(szBuffer);
          // Get the serial number and display it if verbose is enabled
          if (verbose)
          {
            WCHAR *port = wcsstr(szBuffer, L"COM");
            if (port != NULL) {
              SerialPort spTemp;
              pbl_info_t pbl_info;
              spTemp.Open(_wtoi((port + 3)));
              Sahara sh(&spTemp);
              int status = sh.DumpDeviceInfo(&pbl_info);
              if (status == ERROR_SUCCESS) {
                wprintf(L": SERIAL=0x%x : HW_ID=0x%x", pbl_info.serial, pbl_info.msm_id);
              }
            }
          }
          wprintf(_T("\n"));
        }
      }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
  }  
#endif //ARM

  memset(volumes,0,sizeof(vol_entry_t)*MAX_VOLUMES);
  
  // Set all disks to invalid
  for(i=0; i < MAX_VOLUMES; i++) {
    volumes[i].disknum = -1;
  }

  wprintf(L"\nFinding all disks on computer ...\n");
  // First loop through all the volumes

  vHandle = FindFirstVolume(VolumeName,MAX_PATH);
  for(i=0; bValid && (i < MAX_VOLUMES); i++) {
    wcscpy_s(volumes[i].rootpath,MAX_PATH,VolumeName);
    GetVolumeInfo(&volumes[i]);
    bValid = FindNextVolume(vHandle,VolumeName,MAX_PATH);
  }

  // Now loop through all physical disks to find all the useful information
  for(i=0; i< MAX_DISKS; i++) {
    disks[i].disknum = i;
    // If we can't successfully get the information then just continue
    if( GetDiskInfo(&disks[i]) != ERROR_SUCCESS) {
      continue;
    }
    // Find volume for this disk if it exists
    int v = 0;
    for(int j=0; j < MAX_VOLUMES; j++) {
      if( volumes[j].disknum == i ) {
        disks[i].volnum[v++] = j; 
      }
    }
    disks[i].volnum[v] = 0;

    // If disk info is valid print it out
    if( disks[i].disknum != -1 ) {
      wprintf(L"%i. %s  Size: %I64dMB, (%I64d sectors), size: %i Mount:%s, Name:[%s]\n",i,disks[i].diskname, disks[i].disksize/(1024*1024),(disks[i].disksize/(disks[i].blocksize)), disks[i].blocksize, volumes[disks[i].volnum[0]].mount, volumes[disks[i].volnum[0]].volume);
    }
  }

  
  return ERROR_SUCCESS;
}

// Patch a file on computer and put it in %temp% directory
int DiskWriter::ProgramPatchEntry(PartitionEntry pe, TCHAR *key)
{
  UNREFERENCED_PARAMETER(key);
  DWORD bytesIn, bytesOut;
  BOOL bPatchFile = FALSE;
  int status = ERROR_SUCCESS;

  // If filename is disk then patch after else patch the file
  if( (wcscmp(pe.filename,L"DISK") == 0) && bPatchDisk) {
    wprintf(L"Patch file on disk\n");
  } else if( (wcscmp(pe.filename,L"DISK") != 0) && !bPatchDisk ) {
    // Copy file to local temp directory in case it is on share and patch there
    wprintf(L"Patch file locally\n");
    hDisk = CreateFile( pe.filename,
                          (GENERIC_READ | GENERIC_WRITE),
                          0,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    if( hDisk == INVALID_HANDLE_VALUE ) {
      wprintf(L"Failed to open file %s\n",pe.filename);
      return GetLastError();
    }
    bPatchFile = TRUE;
  } else {
    wprintf(L"No file specified skipping command\n");
    return ERROR_SUCCESS;
  }
    
  // If there is a CRC then calculate this value before we write it out
  if( pe.crc_size > 0 ) {
    if (ReadData(buffer1, pe.crc_start*DISK_SECTOR_SIZE, ((int)pe.crc_size + DISK_SECTOR_SIZE), &bytesIn, pe.physical_partition_number) == ERROR_SUCCESS) {
      Partition p(1);
      pe.patch_value += p.CalcCRC32(buffer1,(int)pe.crc_size);
      printf("Patch value 0x%x\n", (UINT32)pe.patch_value );
    }
  }
  
  // read input
  if( (status = ReadData(buffer1,pe.start_sector*DISK_SECTOR_SIZE, DISK_SECTOR_SIZE, &bytesIn, 0)) == ERROR_SUCCESS) {
    // Apply the patch
    memcpy_s(&(buffer1[pe.patch_offset]), (int)(MAX_TRANSFER_SIZE-pe.patch_offset), &(pe.patch_value), (int)pe.patch_size);

    // Write it back out
    status = WriteData(buffer1,pe.start_sector*DISK_SECTOR_SIZE,DISK_SECTOR_SIZE, &bytesOut, 0);
  }

  // If hInFile is not disk file then close after patching it
  if( bPatchFile ) {
    CloseHandle(hDisk);
  } 
  return status;
}

#define MAX_TEST_SIZE (4*1024*1024)
#define LOOP_COUNT 1000

int DiskWriter::CorruptionTest(uint64 offset)
{
  int status = ERROR_SUCCESS;
  BOOL bWriteDone = FALSE;
  BOOL bReadDone = FALSE;
  DWORD bytesOut = 0;
  UINT64 ticks;
  
  char *bufAlloc = NULL;
  char *temp1 = NULL;

  bufAlloc = new char[MAX_TEST_SIZE + 1024*1024];

  // Round off to 1MB boundary
  temp1 = (char *)(((DWORD)bufAlloc + 1024*1024) & ~0xFFFFF);
  ticks = GetTickCount64();
  wprintf(L"OffsetLow is at 0x%x\n", (UINT32)offset);
  wprintf(L"OffsetHigh is at 0x%x\n", (UINT32)(offset >> 32));
  wprintf(L"temp1 = 0x%x\n",(UINT32)temp1);

  for(int i=0; i < MAX_TEST_SIZE; i++) {
    temp1[i] = (char)i;
  }

  for(int i=0; i < LOOP_COUNT; i++) {
    ovl.OffsetHigh = (offset >> 32);
    ovl.Offset = offset & 0xFFFFFFFF;
  
    bWriteDone = WriteFile(hDisk,&temp1[i],MAX_TEST_SIZE,NULL,&ovl);
    if( !bWriteDone ) status = GetLastError();
    if( !bWriteDone && (status != ERROR_SUCCESS) ) {
      wprintf(L"status %i bWriteDone %i bytesWrite %i\n", status, (int)bWriteDone, (int)bytesOut);
      break;
    }

    ovl.Offset = offset & 0xFFFFFFFF;
    ovl.OffsetHigh = (offset >> 32);

    bReadDone = ReadFile(hDisk,&temp1[i],MAX_TEST_SIZE,NULL,&ovl);
    if( !bReadDone ) status = GetLastError();
    if( !bReadDone && (status != ERROR_SUCCESS) ) {
      wprintf(L"status %i bReadDone %i bytesRead %i\n", status, (int)bReadDone, (int)bytesOut);
      break;
    }

    for(int i=0; i < MAX_TEST_SIZE; i++) {
      if( temp1[i] != (char)i ) {
        // If it is different dump out buffer
        wprintf(L"Failed: Offset:  at %i expected %i found %i\n",i,(i&0xff),(int)temp1[i]);
      }
    }
    wprintf(L".");
    offset += 0x40;
  }

  delete[] bufAlloc;
  return ERROR_SUCCESS;
}

int DiskWriter::DiskTest(uint64 offset)
{
  int status = ERROR_SUCCESS;
  BOOL bWriteDone = FALSE;
  BOOL bReadDone = FALSE;
  DWORD bytesOut = 0;
  UINT64 ticks;
  DWORD iops;

  char *bufAlloc = NULL;
  char *temp1 = NULL;
  wprintf(L"Testing this stuff out\n");

  bufAlloc = new char[MAX_TEST_SIZE + 1024*1024];

  // Round off to 1MB boundary
  temp1 = (char *)(((DWORD)bufAlloc + 1024*1024) & ~0xFFFFF);
  
  wprintf(L"Sequential write test 4MB buffer %i\n", (int)temp1);
  ovl.Offset = (offset & 0xffffffff);
  ovl.OffsetHigh = (offset >> 32);
  
  ticks = GetTickCount64();
  for(int i=0; i < 50; i++) {
    bWriteDone = WriteFile(hDisk,temp1,MAX_TEST_SIZE,&bytesOut,&ovl);
    if( !bWriteDone ) status = GetLastError();
    if( !bWriteDone && (status != ERROR_SUCCESS) ) {
      wprintf(L"status %i bWriteDone %i bytesOut %i\n", status, (int)bWriteDone, (int)bytesOut);
      break;
    }
  }
  ticks = GetTickCount64() - ticks;
  wprintf(L"Sequential Write transfer rate: %i KB/s\n", (int)(50*MAX_TEST_SIZE/ticks));

  ovl.Offset = (offset & 0xffffffff);
  ovl.OffsetHigh = (offset >> 32);
  
  wprintf(L"Random write test 4KB buffer\n");
  ticks = GetTickCount64();
  for(iops=0; (GetTickCount64() - ticks) < 2000; iops++) {
    bReadDone = WriteFile(hDisk,temp1,4*1024,NULL,&ovl);
    if( !bWriteDone ) status = GetLastError();
    if( !bWriteDone && (status != ERROR_SUCCESS) ) {
      wprintf(L"status %i Offset: %i bWriteDone %i bytesWrite %i\n", status, (UINT32)ovl.Offset, (int)bWriteDone, (int)bytesOut);
      break;
    }
    ovl.Offset += 0x1000;
  }
  ticks = GetTickCount64() - ticks;
  wprintf(L"Random 4K write IOPS = %i\n", (int)(iops/2));
  
  wprintf(L"Flush Buffer!\n");
  ovl.Offset = (offset & 0xffffffff);
  ovl.OffsetHigh = (offset >> 32);
  ticks = GetTickCount64();
  for(int i=0; i < 100; i++) {
    bReadDone = ReadFile(hDisk,temp1,MAX_TEST_SIZE,NULL,&ovl);
    if( !bReadDone ) status = GetLastError();
    if( !bReadDone && (status != ERROR_SUCCESS) ) {
      wprintf(L"status %i bReadDone %i bytesRead %i\n", status, (int)bReadDone, (int)bytesOut);
      break;
    }
  }
  
  
  wprintf(L"Sequential read test 4MB buffer %i\n", (int)temp1);
  ovl.Offset = (offset & 0xffffffff);
  ovl.OffsetHigh = (offset >> 32);
  ticks = GetTickCount64();
  for(int i=0; i < 50; i++) {
    bReadDone = ReadFile(hDisk,temp1,MAX_TEST_SIZE,NULL,&ovl);
    if( !bReadDone ) status = GetLastError();
    if( !bReadDone && (status != ERROR_SUCCESS) ) {
      wprintf(L"status %i bReadDone %i bytesRead %i\n", status, (int)bReadDone, (int)bytesOut);
      break;
    }
  }

  ticks = GetTickCount64() - ticks;
  wprintf(L"Sequential Read transfer rate: %i KB/s in %i ms\n", (int)(50*MAX_TEST_SIZE/ticks), (int)ticks);
  
  wprintf(L"Random read  test 4KB buffer\n");
  ticks = GetTickCount64();
  for(iops=0; (GetTickCount64() - ticks) < 2000; iops++) {
    bReadDone = ReadFile(hDisk,temp1,4*1024,NULL,&ovl);
    if( !bReadDone ) status = GetLastError();
    if( !bReadDone && (status != ERROR_SUCCESS) ) {
      wprintf(L"status %i bReadDone %i bytesRead %i\n", status, (int)bReadDone, (int)bytesOut);
      break;
    }
    ovl.Offset += 0x100000;
  }
  wprintf(L"Random 4K read IOPS = %i\n", (int)(iops/2));
  
  delete[] bufAlloc;
  return status;
}

int DiskWriter::WriteData(BYTE *writeBuffer, __int64 writeOffset, DWORD writeBytes, DWORD *bytesWritten, UINT8 partNum)
{
  UNREFERENCED_PARAMETER(partNum);
  OVERLAPPED ovlWrite;
  BOOL bWriteDone;
  int status = ERROR_SUCCESS;

  // Check inputs
  if (bytesWritten == NULL) {
    return ERROR_INVALID_PARAMETER;
  }

  // If disk handle not opened then return error
  if ((hDisk == NULL) || (hDisk == INVALID_HANDLE_VALUE)) {
    return ERROR_INVALID_HANDLE;
  }

  ovlWrite.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (ovlWrite.hEvent == NULL) {
    return ERROR_INVALID_HANDLE;
  }

  ResetEvent(ovlWrite.hEvent);
  ovlWrite.Offset = (DWORD)(writeOffset);
  ovlWrite.OffsetHigh = ((writeOffset) >> 32);

  // Write the data to the disk/file at the given offset
  bWriteDone = WriteFile(hDisk, writeBuffer, writeBytes, bytesWritten, &ovlWrite);
  if (!bWriteDone) status = GetLastError();
  
  // If not done and IO is pending wait for it to complete
  if (!bWriteDone && (status == ERROR_IO_PENDING)) {
    status = ERROR_SUCCESS;
    bWriteDone = GetOverlappedResult(hDisk, &ovlWrite, bytesWritten, TRUE);
    if (!bWriteDone) status = GetLastError();
  }

  CloseHandle(ovlWrite.hEvent);
  return status;
}

int DiskWriter::ReadData(BYTE *readBuffer, __int64 readOffset, DWORD readBytes, DWORD *bytesRead, UINT8 partNum)
{
  UNREFERENCED_PARAMETER(partNum);
  OVERLAPPED ovlRead;
  BOOL bReadDone = TRUE;
  int status = ERROR_SUCCESS;

  // Check input parameters
  if (bytesRead == NULL) {
    return ERROR_INVALID_PARAMETER;
  }

  // Make sure we have a valid handle to our disk/file
  if ((hDisk == NULL) || (hDisk == INVALID_HANDLE_VALUE)) {
    return ERROR_INVALID_HANDLE;
  }

  ovlRead.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (ovlRead.hEvent == NULL) {
    return ERROR_INVALID_HANDLE;
  }

  ResetEvent(ovlRead.hEvent);
  ovlRead.Offset = (DWORD)readOffset;
  ovlRead.OffsetHigh = (readOffset >> 32);

  bReadDone = ReadFile(hDisk, readBuffer, readBytes, bytesRead, &ovlRead);
  if (!bReadDone) status = GetLastError();
  
  // If not done and IO is pending wait for it to complete
  if (!bReadDone && (status == ERROR_IO_PENDING)) {
    status = ERROR_SUCCESS;
    bReadDone = GetOverlappedResult(hDisk, &ovlRead, bytesRead, TRUE);
    if (!bReadDone) status = GetLastError();
  }

  CloseHandle(ovlRead.hEvent);
  return status;
}


int DiskWriter::UnmountVolume(vol_entry_t vol)
{
  DWORD bytesRead = 0;

  //try {
    TCHAR volPath[MAX_PATH+1] = _T("\\\\.\\");
    wcscat_s(volPath,vol.mount);
    // Create the file using the volume name
    hVolume = CreateFile( volPath,
                        (GENERIC_READ | GENERIC_WRITE),
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);

    if( hVolume != INVALID_HANDLE_VALUE ) {
      DeviceIoControl( hVolume,FSCTL_DISMOUNT_VOLUME,NULL,0,NULL,0,&bytesRead,NULL);
      wprintf(L"Unmount drive success.\n");
    } else {
      wprintf(L"Warning: Failed to unmount drive continuing...\n");
    }
  //} catch (exception &e) {
  //  wprintf(L"Exception in LockDevice: %s\n", e.what());
  //}
  return ERROR_SUCCESS;
}

int DiskWriter::OpenDiskFile(TCHAR *oFile, uint64 sectors)
{
  int status = ERROR_SUCCESS;
  if( oFile == NULL ) {
    return ERROR_INVALID_HANDLE;
  }

  // First try to create new if it doesn't exist
  hDisk = CreateFile( oFile,
                      GENERIC_WRITE | GENERIC_READ,
                      0,    // We want exclusive access to this disk
                      NULL,
                      CREATE_NEW,
                      FILE_FLAG_OVERLAPPED,
                      NULL);
  // If file  
  if( hDisk == INVALID_HANDLE_VALUE ) {
    status = GetLastError();
    // apply on top of existing file.
    if( status == ERROR_FILE_EXISTS ) {
      status = ERROR_SUCCESS;
      hDisk = CreateFile( oFile,
                        GENERIC_WRITE | GENERIC_READ,
                        0,    // We want exclusive access to this disk
                        NULL,
                        OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED,
                        NULL);
    }
    if( hDisk == INVALID_HANDLE_VALUE ) {
      status = GetLastError();
    }
  }
  disk_size = sectors*DISK_SECTOR_SIZE;
  return status;
}

int DiskWriter::OpenDevice(int dn)
{
  // Make sure our parameters are okay
  
  disk_entry_t de = disks[dn];
  // Lock all volumes associated with this physical disk
  if (de.volnum[0] != 0) {
    if (UnmountVolume(volumes[de.volnum[0]]) != ERROR_SUCCESS) {
      wprintf(L"Warning: Failed to unmount volume %s\n", volumes[de.volnum[0]].mount);
      //return ERROR_INVALID_HANDLE;
    }
  }

  // All associated volumes have been unlocked so now open handle to physical disk
  TCHAR tPath[MAX_PATH];
  //reset DISK_SECTOR_SIZE as it is hard coded to 512 in the constructor.
  DISK_SECTOR_SIZE = de.blocksize;  
  swprintf_s(tPath, _T("\\\\.\\PhysicalDrive%i"), dn);
  wprintf(tPath);
  // Create the file using the volume name
  hDisk = CreateFile(tPath,
    GENERIC_WRITE | GENERIC_READ,
    (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE),    // open it this way so we can write to any disk...
    NULL,
    OPEN_EXISTING,
    FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OVERLAPPED,
    NULL);

  disk_num = dn;
  disk_size = disks[dn].disksize;
  return ERROR_SUCCESS;
}

void DiskWriter::CloseDevice()
{
  disk_num = -1;
  if(hDisk != INVALID_HANDLE_VALUE ) CloseHandle(hDisk);
  if(hVolume != INVALID_HANDLE_VALUE ) CloseHandle(hVolume);
}

bool DiskWriter::IsDeviceWriteable()
{
  DWORD bytesRead;
    if( DeviceIoControl( hDisk,IOCTL_DISK_IS_WRITABLE,NULL,0,NULL,0,&bytesRead,NULL) ) {
      return true;
    }
  return false;
}

int DiskWriter::WipeLayout()
{
  DWORD bytesRead;
    if( DeviceIoControl( hDisk,IOCTL_DISK_DELETE_DRIVE_LAYOUT,NULL,0,NULL,0,&bytesRead,&ovl) ) {

      if( DeviceIoControl( hDisk,IOCTL_DISK_UPDATE_PROPERTIES,NULL,0,NULL,0,&bytesRead,&ovl) ) {
        return ERROR_SUCCESS;
      }
    }
  return ERROR_GEN_FAILURE;
}

int DiskWriter::RawReadTest(uint64 offset)
{
  // Set up the overlapped structure
  OVERLAPPED ovlp;
  int status = ERROR_SUCCESS;
  LARGE_INTEGER large_val;
  large_val.QuadPart = offset;
  ovlp.hEvent = CreateEvent(NULL, FALSE, FALSE,_T("DiskWriter"));
  if (!ovlp.hEvent) return GetLastError();
  ovlp.Offset = large_val.LowPart;
  ovlp.OffsetHigh = large_val.HighPart;

  DWORD bytesIn = 0; 
  DWORD dwResult;
  BOOL bResult = ReadFile(hDisk, buffer1, DISK_SECTOR_SIZE, NULL, &ovlp);
  dwResult = GetLastError();
  if( !bResult ) {
    // If Io is pending wait for it to finish
    if( dwResult == ERROR_IO_PENDING ) {
      // Wait for actual data to be read if pending
      wprintf(L"Wait for it to finish %I64d\n", offset);
      bResult = GetOverlappedResult(hDisk,&ovlp,&bytesIn,TRUE);
      if( !bResult ) {
        dwResult = GetLastError();
        if( dwResult != ERROR_IO_INCOMPLETE) {
          status = ERROR_READ_FAULT;
        }
      } else {
        // Finished read successfully
        status = ERROR_SUCCESS;
      }
    } else if( dwResult != ERROR_SUCCESS ) {
      wprintf(L"Status is: %i\n",(int)dwResult);
      status = ERROR_READ_FAULT;
    }
  }

  CloseHandle(ovlp.hEvent);
  return status;
}

extern int sd;
static unsigned char chkbuf[MAX_TRANSFER_SIZE];
int DiskWriter::FastCopy(HANDLE hRead, __int64 sectorRead, HANDLE hWrite, __int64 sectorWrite, uint64 sectors, UINT8 partNum, BOOL zDump)
{  // Set up the overlapped structure
  UNREFERENCED_PARAMETER(partNum);
  UNREFERENCED_PARAMETER(zDump);
  OVERLAPPED ovlWrite, ovlRead, ovlsparse;
  int stride;
  uint64 sec;
  DWORD bytesOut = 0;
  DWORD bytesRead = 0;
  int readStatus = ERROR_SUCCESS;
  int status = ERROR_SUCCESS;
  BOOL bWriteDone = TRUE;
  BOOL bBuffer1 = TRUE;

#ifdef USE_ZLIB
  LONGLONG destsparseoffset = 0;
  LONGLONG destsparsesize = 0;
  LONGLONG destdatasize = 0;

  //Compression variables.
  int ret;
  int flush;
  unsigned zBytesWrite=0;
  
  z_stream strm;
  /* allocate deflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
  if (ret != Z_OK)
	  return ret;
#endif // USE_ZLIB

  UINT64 ticks = GetTickCount64();
  UINT64 curtick = 0;


  if (sectorWrite < 0)
  {
    sectorWrite = GetNumDiskSectors() + sectorWrite;
  }

  // Set initial stride size to size of buffer
  stride = MAX_TRANSFER_SIZE / DISK_SECTOR_SIZE;

  // Setup offsets for read and write
  ovlWrite.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (ovlWrite.hEvent == NULL) return ERROR_OUTOFMEMORY;
  ovlWrite.Offset = (DWORD)(sectorWrite*DISK_SECTOR_SIZE);
  ovlWrite.OffsetHigh = ((sectorWrite*DISK_SECTOR_SIZE) >> 32);

  ovlsparse.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (ovlsparse.hEvent == NULL) return ERROR_OUTOFMEMORY;
  ovlsparse.Offset = (DWORD)(sectorWrite*DISK_SECTOR_SIZE);
  ovlsparse.OffsetHigh = ((sectorWrite*DISK_SECTOR_SIZE) >> 32);

  ovlRead.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (ovlRead.hEvent == NULL) return ERROR_OUTOFMEMORY;
  ovlRead.Offset = (DWORD)(sectorRead*DISK_SECTOR_SIZE);
  ovlRead.OffsetHigh = ((sectorRead*DISK_SECTOR_SIZE) >> 32);

  if (hRead == INVALID_HANDLE_VALUE) {
    wprintf(L"hRead = INVALID_HANDLE_VALUE, zeroing input buffer\n");
    memset(buffer1, 0, stride*DISK_SECTOR_SIZE);
    memset(buffer2, 0, stride*DISK_SECTOR_SIZE);
    bytesRead = stride*DISK_SECTOR_SIZE;
  }

  BOOL sparseret = false;
  FILE_ZERO_DATA_INFORMATION sparsezerodata = { 0,0 };
  if (sd)
  {
	  sparseret = DeviceIoControl((HANDLE)hWrite,                     // handle to a file
		  FSCTL_SET_SPARSE,                     // dwIoControlCode
		  NULL, // input buffer
		  NULL,                // size of input buffer
		  NULL,                                 // lpOutBuffer
		  0,                                    // nOutBufferSize
		  &bytesRead,            // number of bytes returned
		  &ovlsparse);        // OVERLAPPED structure
	  if (!sparseret)
	  {
		  status = GetLastError();
		  return status;
	  }

  }

  sec = 0;
  while (sec < sectors) {

    // Check if we have to read smaller number of sectors
    if ((sec + stride > sectors) && (sectors != 0)) {
      stride = (DWORD)(sectors - sec);
    }

    // If read handle is valid then read file file and wait for response
    if (hRead != INVALID_HANDLE_VALUE) {
      bytesRead = 0;
      if(bBuffer1 ) readStatus = ReadFile(hRead, buffer1, stride*DISK_SECTOR_SIZE, &bytesRead, &ovlRead);
      else readStatus = ReadFile(hRead, buffer2, stride*DISK_SECTOR_SIZE, &bytesRead, &ovlRead);
      if (!readStatus) status = GetLastError();
      if (status == ERROR_IO_PENDING) readStatus = GetOverlappedResult(hRead, &ovlRead, &bytesRead, TRUE);
      if (!readStatus) {
        status = GetLastError();
        break;
      }
      status = ERROR_SUCCESS;

      // Need to round up to nearest sector size if read partial sector from input file
      bytesRead = (bytesRead + DISK_SECTOR_SIZE - 1) & ~(DISK_SECTOR_SIZE - 1);
    }

    if (ovlRead.Offset + bytesRead < ovlRead.Offset) ovlRead.OffsetHigh++;
    ovlRead.Offset += bytesRead;

    if (!bWriteDone) {
      // Wait for the previous write to complete before we start a new one
      bWriteDone = GetOverlappedResult(hWrite, &ovlWrite, &bytesOut, TRUE);
      if (!bWriteDone) {
        status = GetLastError();
        break;
      }
      // Increment the offset for the next write now that this one has completed
      if (ovlWrite.Offset + bytesOut < ovlWrite.Offset) ovlWrite.OffsetHigh++;
      ovlWrite.Offset += bytesOut;

      status = ERROR_SUCCESS;
    }
    
	sec += stride;
	sectorRead += stride;
#ifdef USE_ZLIB
	if (zlibDump)
	{
		// ---------------------------- Compression ----------------------------------------------------------------
		strm.avail_in = bytesRead;
		if (sec >= sectors)	flush = Z_FINISH;
		else flush = Z_NO_FLUSH;

		if (bBuffer1) strm.next_in = (Bytef *)buffer1;
		else strm.next_in = (Bytef *)buffer2;

		/* run deflate() on input until output buffer not full, finish
		compression if all of source has been read in */
		do {
			strm.avail_out = MAX_TRANSFER_SIZE;

			if (bBuffer1) strm.next_out = (Bytef *)zbuffer1;
			else strm.next_out = (Bytef *)zbuffer2;

			ret = deflate(&strm, flush);    /* no bad return value */
			if (ret == Z_STREAM_ERROR)  /* state not clobbered */
			{
				wprintf(L"\nCompress stream failed");
				return Z_STREAM_ERROR;
			}
			else
			{
				zBytesWrite = MAX_TRANSFER_SIZE - strm.avail_out;
				bytesRead = 0;
				//wprintf(L"Ticks : %i : zBytesWrite : %i  ", (GetTickCount64() - ticks + 1), zBytesWrite);
				
				if (zBytesWrite > 0)
				{

					//Write to zbin file
					if (bBuffer1) bWriteDone = WriteFile(hWrite, zbuffer1, zBytesWrite, &bytesRead, &ovlWrite);
					else bWriteDone = WriteFile(hWrite, zbuffer2, zBytesWrite, &bytesRead, &ovlWrite);

					// Check if write already completed update the offset otherwise we update it when we finish reading
					if (bWriteDone)
					{
						if (ovlWrite.Offset + bytesRead < ovlWrite.Offset) ovlWrite.OffsetHigh++;
						ovlWrite.Offset += bytesRead;
						//wprintf(L"\n bWriteDone is more : %i : %i : %i\n", zBytesWrite, bWriteDone, bytesRead);
					}
				}
			}

		} while (strm.avail_out == 0);
		// ---------------------------- end Compression ----------------------------------------------------------------
	}
	else if (sd)
	{
		DWORD dwTemp;
		BOOL bStatus = false;
		if (bBuffer1)
		{
			if (memcmp(chkbuf, buffer1, MAX_TRANSFER_SIZE))
			{
				bWriteDone = WriteFile(hWrite, buffer1, bytesRead, NULL, &ovlWrite);
				//wprintf(L"\nDatapart: %lu : %lu \n", destsparsesize, destsparseoffset);
				if (destsparsesize > 0)
				{
					
					sparsezerodata.BeyondFinalZero.QuadPart = destsparseoffset;
					bStatus = DeviceIoControl(hWrite,
						FSCTL_SET_ZERO_DATA,
						&sparsezerodata,
						sizeof(sparsezerodata),
						NULL,
						0,
						&dwTemp,
						&ovlsparse);
					if (!bStatus)
					{
						status = GetLastError();
						return status;
					}
					//wprintf(L"\nDeviceIoControl: %lu : %lu : %lu\n", destsparsesize, sparsezerodata.FileOffset.QuadPart, sparsezerodata.BeyondFinalZero.QuadPart);
					destsparsesize = 0;
				}
				if (destdatasize == 0)
				{
					wprintf(L"\n< DestData Start: %I64u >\n", (sectorRead - stride));
				}
				destdatasize++;
			}
			else
			{
				if (destsparsesize == 0)
				{
					//wprintf(L"\n dataspare offse : %lu", destsparseoffset);
					sparsezerodata.FileOffset.QuadPart = destsparseoffset;
				}
				destsparsesize += 1;
				if (destdatasize > 0)
				{
					wprintf(L"\n< DestData Size: %I64u >\n", destdatasize*stride);
					destdatasize = 0;
				}

			}
		}
		else
		{
			if (memcmp(chkbuf, buffer2, MAX_TRANSFER_SIZE))
			{
				bWriteDone = WriteFile(hWrite, buffer2, bytesRead, NULL, &ovlWrite);
				//wprintf(L"\nDatapart: %lu : %lu \n", destsparsesize, destsparseoffset);
				if (destsparsesize > 0)
				{
					sparsezerodata.BeyondFinalZero.QuadPart = destsparseoffset;
					
					bStatus = DeviceIoControl(hWrite,
						FSCTL_SET_ZERO_DATA,
						&sparsezerodata,
						sizeof(sparsezerodata),
						NULL,
						0,
						&dwTemp,
						&ovlsparse);
					if (!bStatus)
					{
						status = GetLastError();
						return status;
					}
					//wprintf(L"\nDeviceIoControl: %lu : %lu : %lu\n", destsparsesize, sparsezerodata.FileOffset.QuadPart, sparsezerodata.BeyondFinalZero.QuadPart);
					destsparsesize = 0;
					if (destdatasize == 0)
					{
						wprintf(L"\n< DestData Start: %I64u >\n", (sectorRead - stride));
					}
					destdatasize++;
				}
			}
			else
			{
				if (destsparsesize == 0)
				{
					//wprintf(L"\n dataspare offse : %lu\n", destsparseoffset);
					sparsezerodata.FileOffset.QuadPart = destsparseoffset;
				}
				destsparsesize += 1;
				if (destdatasize > 0)
				{
					wprintf(L"\n< DestData Size: %I64u >\n", destdatasize*stride);
					destdatasize = 0;
				}
			}
		}
		// Check if write already completed update the offset otherwise we update it when we finish reading
		if (bWriteDone)
		{
			if (ovlWrite.Offset + bytesRead < ovlWrite.Offset) ovlWrite.OffsetHigh++;
			ovlWrite.Offset += bytesRead;

			if (ovlsparse.Offset + bytesRead < ovlsparse.Offset) ovlsparse.OffsetHigh++;
			ovlsparse.Offset += bytesRead;

			//destsparseoffset += bytesRead;
			destsparseoffset += bytesRead;
		}
		if (sec >= sectors)
		{
			wprintf(L"\n< DestData Size: %I64u >\n", destdatasize*stride);
		}

	}
	else
#endif   // USE_ZLIB
	{
		// Now start a write for the corresponding buffer we just read
		if (bBuffer1) bWriteDone = WriteFile(hWrite, buffer1, bytesRead, NULL, &ovlWrite);
		else bWriteDone = WriteFile(hWrite, buffer2, bytesRead, NULL, &ovlWrite);
		
		// Check if write already completed update the offset otherwise we update it when we finish reading
		if (bWriteDone)
		{
			if (ovlWrite.Offset + bytesRead < ovlWrite.Offset) ovlWrite.OffsetHigh++;
			ovlWrite.Offset += bytesRead;
		}
	}

    bBuffer1 = !bBuffer1;

    wprintf(L"Sectors remaining: %i \r", (int)(sectors - sec));
  }

#ifdef USE_ZLIB
  if (zlibDump)
  {
	  wprintf(L"\nClosing compression stream");
	  (void)deflateEnd(&strm);
  }
#endif // USE_ZLIB

  wprintf(L"\nStatus = %i\n", status);
  // If we hit end of file and we read some data then round up to nearest block and write it out and wait
  // for it to complete else the next operation might fail.
  if ((sec < sectors) && (bytesRead > 0) && (status == ERROR_SUCCESS)) {
    bytesRead = (bytesRead + DISK_SECTOR_SIZE - 1) & ~(DISK_SECTOR_SIZE - 1);
    if (bBuffer1) bWriteDone = WriteFile(hWrite, buffer1, bytesRead, NULL, &ovlWrite);
    else  bWriteDone = WriteFile(hWrite, buffer2, bytesRead, NULL, &ovlWrite);
    wprintf(L"Wrote last bytes: %i\n", (int)bytesRead);
  }

  // Wait for pending write transfers to complete
  if (!bWriteDone && (GetLastError() == ERROR_IO_PENDING)) {
    bWriteDone = GetOverlappedResult(hWrite, &ovlWrite, &bytesOut, TRUE);
    if (!bWriteDone) status = GetLastError();
  }


  curtick = GetTickCount64() - (ticks + 1);
  UINT64 mb = 0;
  if (DISK_SECTOR_SIZE == 512)
  {
	  mb = (sectors/2)/1024;
  }
  else 
  {
	  mb = (sectors*4)/1024;
  }
  wprintf(L"\nTime elapsed %f secs, Datatransfer at %f MB/s\n", (float)curtick / 1000, (float)((mb*1000)/curtick));
  

  CloseHandle(ovlRead.hEvent);
  CloseHandle(ovlWrite.hEvent);
  CloseHandle(ovlsparse.hEvent);
  return status;
}

int DiskWriter::GetRawDiskSize( uint64 *ds)
{
  int status = ERROR_SUCCESS;
  // Start at 512 MB for good measure to get us to size quickly
  uint64 diff = DISK_SECTOR_SIZE * 1024;

  // Read data from various sectors till we figure out how big disk is
  if( ds == NULL || hDisk == INVALID_HANDLE_VALUE) {
    return ERROR_INVALID_PARAMETER;
  }

  // Keep doubling size till we can't read anymore
  *ds = 0;
  for(;;) {
    status = RawReadTest(*ds + diff);
    if (diff <= DISK_SECTOR_SIZE) {
      if( status == ERROR_SUCCESS) {
        *ds = *ds + diff;
      }
      break;
    }
    if( status == ERROR_SUCCESS) {
      *ds = *ds + diff;
      diff = diff * 2;
    } else {
      diff = diff / 2;
    }
  }
  wprintf(L"Value of ds is %I64d\n",*ds);

  return ERROR_SUCCESS;
}

int DiskWriter::LockDevice()
{
  DWORD bytesRead = 0;
	  if( DeviceIoControl( hDisk,FSCTL_DISMOUNT_VOLUME,NULL,0,NULL,0,&bytesRead,NULL) ) {
      if( DeviceIoControl( hDisk,FSCTL_LOCK_VOLUME,NULL,0,NULL,0,&bytesRead,NULL) ) {
        wprintf(L"Locked volume and dismounted\n");
        return ERROR_SUCCESS;
	    }
    }
  return ERROR_GEN_FAILURE;
}

int DiskWriter::UnlockDevice()
{
  DWORD bytesRead = 0;
    if( DeviceIoControl( hDisk,FSCTL_UNLOCK_VOLUME,NULL,0,NULL,0,&bytesRead,NULL) ) {
      return ERROR_SUCCESS;
    }
  return ERROR_GEN_FAILURE;
}