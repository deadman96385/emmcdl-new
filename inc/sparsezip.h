#pragma once
#include "protocol.h"
#include <windows.h>

class SparseZipImage {
public:
	int PreLoadSparseZipImage(TCHAR *szSparseFile);
	int ProgramSparseZipImage(Protocol *pProtocol, __int64 sectorRead, HANDLE hWrite, __int64 sectorWrite, uint64 sectors, UINT8 partNum);
	int SparseZipImage::CheckForZipImage(TCHAR *szSparseFile);

	SparseZipImage();
	~SparseZipImage();

private:
	HANDLE hSparseZipImage;

	BYTE *zbuffer1;
	BYTE *zbufAlloc1;
	BYTE *zm_payload;
	BYTE *m_payload;

	BYTE *chk;
	BYTE *chkbuf;

};
