
#include "sparsezip.h"
#include "partition.h"

// Constructor
SparseZipImage::SparseZipImage()
{
	zbufAlloc1 = (BYTE *)malloc(MAX_TRANSFER_SIZE + 0x400);
	if (zbufAlloc1) zbuffer1 = (BYTE *)(((DWORD)zbufAlloc1 + 0x200) & ~0x1ff);

	zm_payload = (BYTE *)malloc((MAX_TRANSFER_SIZE)+0x400);
	if (zm_payload) m_payload = (BYTE *)(((DWORD)zm_payload + 0x200) & ~0x1ff);

	chk = NULL;
}

// Destructor
SparseZipImage::~SparseZipImage()
{
	if (hSparseZipImage)
	{
		CloseHandle(hSparseZipImage);
	}
#pragma prefast(suppress: 6001, "Even if we initialize zbufAlloc1, same warning[Using uninitialized memory '*this'] shows. So suppressing this warning.")
	if (zbufAlloc1) free(zbufAlloc1);
	if (zm_payload) free(zm_payload);
	if (chk) free(chk);
}

int SparseZipImage::PreLoadSparseZipImage(TCHAR *szSparseFile)
{
	//HANDLE hSparseCheckForZipImage;
	DWORD dwBytesRead = 0;
	DWORD status = 0;
	BOOL bReadStatus = TRUE;

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

	hSparseZipImage = CreateFile(szSparseFile,
		GENERIC_READ,
		FILE_SHARE_READ, // We only read from here so let others open the file as well
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (hSparseZipImage == INVALID_HANDLE_VALUE) return GetLastError();


	dwBytesRead = 0;
	if (hSparseZipImage != INVALID_HANDLE_VALUE) {
		bReadStatus = ReadFile(hSparseZipImage, m_payload, MAX_TRANSFER_SIZE, &dwBytesRead, NULL);
	}
	else
	{
		return GetLastError();
	}

	if (!dwBytesRead)
	{
		wprintf(L"Returning as bytes read is zero\n");
		return Z_DATA_ERROR;
	}
	strm.avail_in = dwBytesRead;
	strm.next_in = (Bytef *)m_payload;

	strm.avail_out = MAX_TRANSFER_SIZE;
	strm.next_out = (Bytef *)zbuffer1;
	ret = inflate(&strm, Z_NO_FLUSH);


	switch (ret) {
	case Z_NEED_DICT:
		ret = Z_DATA_ERROR;     /* and fall through */
	case Z_DATA_ERROR:
	case Z_MEM_ERROR:
		(void)inflateEnd(&strm);
		return ret;
	}
	(void)inflateEnd(&strm);

	ret = SetFilePointer(hSparseZipImage, NULL, NULL, FILE_BEGIN);
	if (status == INVALID_SET_FILE_POINTER) {
		status = GetLastError();
		wprintf(L"Failed to set offset to FILE_BEGIN: %i\n", status);
		return status;
	}

	return ERROR_SUCCESS;


}

int SparseZipImage::CheckForZipImage(TCHAR *szSparseFile)
{

	hSparseZipImage = CreateFile(szSparseFile,
		GENERIC_READ,
		FILE_SHARE_READ, // We only read from here so let others open the file as well
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (hSparseZipImage == INVALID_HANDLE_VALUE) return GetLastError();

	return ERROR_SUCCESS;
}

//#define CHUNK 0x100000
//static unsigned char chkbuf[MAX_TRANSFER_SIZE_5M];
int SparseZipImage::ProgramSparseZipImage(Protocol *pProtocol, __int64 sectorRead, HANDLE hWrite, __int64 sectorWrite, uint64 sectors, UINT8 partNum)
{
	UNREFERENCED_PARAMETER(sectorRead);
	DWORD dwBytesRead = 0;
	BOOL bReadStatus = TRUE;
	int diskSecorSize = pProtocol->GetDiskSectorSize();
	__int64 dwWriteOffset = sectorWrite*diskSecorSize;
	//__int64 dwWriteOffsetSectors = sectorWrite;
	DWORD dwBytesOut = 0;
	UINT32 sectorscountdown = (MAX_TRANSFER_SIZE / diskSecorSize);
	int status = ERROR_SUCCESS;



	chk = (BYTE *)malloc((MAX_TRANSFER_SIZE)+0x400);
	if (chk) chkbuf = (BYTE *)(((DWORD)chk + 0x200) & ~0x1ff);
	else return ERROR_OUTOFMEMORY;

	memset(chkbuf, 0, MAX_TRANSFER_SIZE);


	//Decompression variables.
	int ret;
	z_stream strm;
	unsigned zBytesWrite = 0;
	int nextbuflen = 0;

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit(&strm);
	if (ret != Z_OK)
		return ret;
	
	if (hWrite == NULL) {
		return ERROR_INVALID_PARAMETER;
	}

	if (hWrite != NULL)
	{
		for (UINT32 tmp_sectors = (UINT32)sectors; tmp_sectors > 0; ) {
			// Writing to disk and reading from file...
			dwBytesRead = 0;
			if (hSparseZipImage != INVALID_HANDLE_VALUE) {
				bReadStatus = ReadFile(hSparseZipImage, m_payload, MAX_TRANSFER_SIZE, &dwBytesRead, NULL);
			}
			//---------------------------------- decompress zbin files here -------------------------------------------------------

			if (!dwBytesRead)
			{
				wprintf(L"Breaking as bytes read is zero\n");
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

						if (memcmp(chkbuf, zbuffer1, MAX_TRANSFER_SIZE))
						{
							pProtocol->WriteData(zbuffer1, dwWriteOffset, MAX_TRANSFER_SIZE, &dwBytesOut, partNum);
						}
						/*
						status = sport->Write(zbuffer1, MAX_TRANSFER_SIZE);
						if (status != ERROR_SUCCESS) {
							break;
						}
						*/
						dwWriteOffset += MAX_TRANSFER_SIZE;
						//dwWriteOffsetSectors += sectorscountdown;
							
						tmp_sectors -= sectorscountdown;							
						nextbuflen = 0;
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
			//wprintf(L"Sparse Sectors remaining %i\r", (int)tmp_sectors);
		}
		(void)inflateEnd(&strm);
	}
	else {
		return ERROR_INVALID_PARAMETER;
	}

	return status;

}