/*****************************************************************************
* sparse.cpp
*
* This class implements Sparse image support
*
* Copyright (c) 2007-2015
* Qualcomm Technologies Incorporated.
* All Rights Reserved.
* Qualcomm Confidential and Proprietary
*
*****************************************************************************/
/*=============================================================================
Edit History

$Header: //deploy/qcom/qct/platform/wpci/prod/woa/emmcdl/main/latest/src/sparse.cpp#3 $
$DateTime: 2015/07/22 17:54:37 $ $Author: wmcisvc $

when       who     what, where, why
-------------------------------------------------------------------------------
02/05/15   pgw     Initial version.
=============================================================================*/

#include "stdio.h"
#include "stdlib.h"
#include "tchar.h"
#include "sparse.h"
#include "windows.h"
#include "string.h"

// Constructor
SparseImage::SparseImage()
{
  bSparseImage = false;
}

// Destructor
SparseImage::~SparseImage()
{
  if (bSparseImage)
  {
    CloseHandle(hSparseImage);
  }
}

// This will load a sparse image into memory and read headers if it is a sparse image
// RETURN: 
// ERROR_SUCCESS - Image and headers we loaded successfully
// 
int SparseImage::PreLoadImage(TCHAR *szSparseFile)
{
  DWORD dwBytesRead;
  hSparseImage = CreateFile(szSparseFile,
    GENERIC_READ,
    FILE_SHARE_READ, // We only read from here so let others open the file as well
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL);
  if (hSparseImage == INVALID_HANDLE_VALUE) return GetLastError();

  // Load the sparse file header and verify it is valid
  if (ReadFile(hSparseImage, &SparseHeader, sizeof(SparseHeader), &dwBytesRead, NULL)) {
    // Check the magic number in the sparse header to see if this is a vaild sparse file
    if (SparseHeader.dwMagic != SPARSE_MAGIC) {
      CloseHandle(hSparseImage);
      return ERROR_BAD_FORMAT;
    }
  }
  else {
    // Couldn't read the file likely someone else has it open for exclusive access
    CloseHandle(hSparseImage);
    return ERROR_READ_FAULT;
  }

  bSparseImage = true;
  return ERROR_SUCCESS;
}

int SparseImage::ProgramImage(Protocol *pProtocol, __int64 dwOffset)
{
  CHUNK_HEADER ChunkHeader;
  BYTE *bpDataBuf = NULL;
  DWORD dwBytesRead;
  DWORD dwBytesOut;
  int status = ERROR_SUCCESS;

  // Make sure we have first successfully found a sparse file and headers are loaded okay
  if (!bSparseImage)
  {
    return ERROR_FILE_NOT_FOUND;
  }

  // Allocated a buffer 

  // Main loop through all block entries in the sparse image
  for (UINT32 i=0; i < SparseHeader.dwTotalChunks; i++) {

    // Read chunk header 
    if (ReadFile(hSparseImage, &ChunkHeader, sizeof(ChunkHeader), &dwBytesRead, NULL)){
      UINT32 dwChunkSize = ChunkHeader.dwChunkSize*SparseHeader.dwBlockSize;
      // Allocate a buffer the size of the chunk and read in the data
      bpDataBuf = (BYTE *)malloc(dwChunkSize);
      if (bpDataBuf == NULL){
        return ERROR_OUTOFMEMORY;
      }
      if (ChunkHeader.wChunkType == SPARSE_RAW_CHUNK){
        if (ReadFile(hSparseImage, bpDataBuf, dwChunkSize, &dwBytesRead, NULL)) {
          // Now we have the data so use whatever protocol we need to write this out
          pProtocol->WriteData(bpDataBuf, dwOffset, dwChunkSize, &dwBytesOut,0);
          dwOffset += dwChunkSize;
        }
        else {
          status = GetLastError();
          break;
        }
      }
      else if (ChunkHeader.wChunkType == SPARSE_FILL_CHUNK) {
        // Fill chunk with data byte given for now just skip over this area
        dwOffset += ChunkHeader.dwChunkSize*SparseHeader.dwBlockSize;
      }
      else if (ChunkHeader.wChunkType == SPARSE_DONT_CARE) {
        // Skip the specified number of bytes in the output file
        // Fill the buffer with 0
        memset(bpDataBuf, 0, dwChunkSize);
        pProtocol->WriteData(bpDataBuf, dwOffset, dwChunkSize, &dwBytesOut, 0);
        dwOffset += dwChunkSize;
      }
      else {
        // We have no idea what type of chunk this is return a failure and close file
        status = ERROR_INVALID_DATA;
        break;
      }
      free(bpDataBuf);
      bpDataBuf = NULL;
    }
    else {
      // Failed to read data something is wrong with the file
      status = GetLastError();
      break;
    }
  }

  // If we broke here from an error condition free the bpDataBuf
  if( bpDataBuf != NULL) {
    free(bpDataBuf);
    bpDataBuf = NULL;
  }

  // If we failed to load the file close the handle and set sparse image back to false
  if (status != ERROR_SUCCESS) {
    bSparseImage = false;
    CloseHandle(hSparseImage);
  }

  return status;
}

