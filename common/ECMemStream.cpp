/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <new>
#include <mapix.h>
#include <kopano/ECGuid.h>
#include <kopano/memory.hpp>
#include "ECMemStream.h"
#define EC_MEMBLOCK_SIZE 8192

namespace KC {

ECMemBlock::ECMemBlock(const char *buffer, ULONG ulDataLen, ULONG fl) :
	ECUnknown("ECMemBlock"), ulFlags(fl)
{
	if (ulDataLen == 0)
		return;
	cbTotal = ulDataLen;
	cbCurrent = ulDataLen;
	lpCurrent = (char *)malloc(ulDataLen);
	if (lpCurrent == nullptr)
		throw std::bad_alloc();
	memcpy(lpCurrent, buffer, ulDataLen);
	if (!(ulFlags & STGM_TRANSACTED))
		return;
	cbOriginal = ulDataLen;
	lpOriginal = (char *)malloc(ulDataLen);
	if (lpOriginal == nullptr)
		throw std::bad_alloc();
	memcpy(lpOriginal, buffer, ulDataLen);
}

ECMemBlock::~ECMemBlock()
{
	free(lpCurrent);
	if (ulFlags & STGM_TRANSACTED)
		free(lpOriginal);
}

HRESULT	ECMemBlock::Create(const char *buffer, ULONG ulDataLen, ULONG ulFlags,
    ECMemBlock **lppStream)
{
	return alloc_wrap<ECMemBlock>(buffer, ulDataLen, ulFlags).put(lppStream);
}

HRESULT ECMemBlock::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMemBlock, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

// Reads at most ulLen chars, may be shorter due to shorter data len
HRESULT	ECMemBlock::ReadAt(ULONG ulPos, ULONG ulLen, char *buffer, ULONG *ulBytesRead)
{
	ULONG ulToRead = cbCurrent - ulPos;

	ulToRead = ulLen < ulToRead ? ulLen : ulToRead;
	memcpy(buffer, lpCurrent+ulPos, ulToRead);
	if(ulBytesRead)
		*ulBytesRead = ulToRead;
	return hrSuccess;
}

HRESULT ECMemBlock::WriteAt(ULONG ulPos, ULONG ulLen, const char *buffer,
    ULONG *ulBytesWritten)
{
	ULONG dsize = ulPos + ulLen;

	if(cbTotal < dsize) {
		ULONG newsize = cbTotal + ((dsize / EC_MEMBLOCK_SIZE) + 1) * EC_MEMBLOCK_SIZE; // + at least 8k
		auto lpNew = static_cast<char *>(realloc(lpCurrent, newsize));
		if (lpNew == NULL)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		lpCurrent = lpNew;
		memset(lpCurrent+cbTotal, 0, newsize-cbTotal);	// clear new alloced mem
		cbTotal = newsize;		// set new size
	}

	if (dsize > cbCurrent)			// if write part is bigger than actual data
		cbCurrent = ulPos + ulLen;	// set _real_ buffer size
	memcpy(lpCurrent+ulPos, buffer, ulLen);
	if(ulBytesWritten)
		*ulBytesWritten = ulLen;
	return hrSuccess;
}

HRESULT ECMemBlock::Commit()
{
	if (!(ulFlags & STGM_TRANSACTED))
		return hrSuccess;
	free(lpOriginal);
	lpOriginal = (char *)malloc(cbCurrent);
	if (lpOriginal == NULL)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	cbOriginal = cbCurrent;
	memcpy(lpOriginal, lpCurrent, cbCurrent);
	return hrSuccess;
}

HRESULT ECMemBlock::Revert()
{
	if (!(ulFlags & STGM_TRANSACTED))
		return hrSuccess;
	free(lpCurrent);
	lpCurrent = (char *)malloc(cbOriginal);
	if (lpCurrent == NULL)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	cbCurrent = cbTotal = cbOriginal;
	memcpy(lpCurrent, lpOriginal, cbOriginal);
	return hrSuccess;
}

HRESULT ECMemBlock::SetSize(ULONG ulSize)
{
	auto lpNew = static_cast<char *>(realloc(lpCurrent, ulSize));
	if (lpNew == NULL && ulSize != 0)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	if(ulSize > cbCurrent)
		memset(lpNew+cbCurrent, 0, ulSize-cbCurrent);
	lpCurrent = lpNew;
	cbCurrent = ulSize;
	cbTotal = ulSize;
	return hrSuccess;
}

HRESULT ECMemBlock::GetSize(ULONG *ulSize) const
{
	*ulSize = cbCurrent;
	return hrSuccess;
}

/*
 * ECMemStream, IStream compatible in-memory stream object
 */
ECMemStream::ECMemStream(const char *buffer, ULONG ulDataLen, ULONG f,
    CommitFunc cf, DeleteFunc df, void *p) :
	ECUnknown("IStream"), lpCommitFunc(cf), lpDeleteFunc(df), lpParam(p),
	ulFlags(f)
{
	ECMemBlock::Create(buffer, ulDataLen, ulFlags, &lpMemBlock);
}

ECMemStream::ECMemStream(ECMemBlock *mb, ULONG f, CommitFunc cf,
    DeleteFunc df, void *p) :
	ECUnknown("IStream"), lpMemBlock(mb), lpCommitFunc(cf),
	lpDeleteFunc(df), lpParam(p), ulFlags(f)
{
	lpMemBlock->AddRef();
}

ECMemStream::~ECMemStream()
{
	ULONG refs = 0;
	if (lpMemBlock != nullptr)
		refs = lpMemBlock->Release();
	if (refs == 0 && lpDeleteFunc != nullptr)
		lpDeleteFunc(lpParam);
}

HRESULT ECMemStream::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(IStream, this);
	REGISTER_INTERFACE2(ISequentialStream, this);
	REGISTER_INTERFACE2(IUnknown, this);
	REGISTER_INTERFACE2(ECMemStream, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

ULONG ECMemStream::Release()
{
	// Releasing last reference
	// If you read the docs on STGM_SHARE_EXCLUSIVE it doesn't say you need
	// to Commit() at the end, so if the client hasn't called Commit() yet,
	// we need to do it for them before throwing away the data.
	if (m_cRef == 1 && ulFlags & STGM_SHARE_EXCLUSIVE && fDirty)
		Commit(0);
	return ECUnknown::Release();
}

HRESULT	ECMemStream::Create(char *buffer, ULONG ulDataLen, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc,
							void *lpParam, ECMemStream **lppStream)
{
	return alloc_wrap<ECMemStream>(buffer, ulDataLen, ulFlags,
	       lpCommitFunc, lpDeleteFunc, lpParam).put(lppStream);
}

HRESULT	ECMemStream::Create(ECMemBlock *lpMemBlock, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc,
							void *lpParam, ECMemStream **lppStream)
{
	return alloc_wrap<ECMemStream>(lpMemBlock, ulFlags, lpCommitFunc,
	       lpDeleteFunc, lpParam).put(lppStream);
}

HRESULT ECMemStream::Read(void *pv, ULONG cb, ULONG *pcbRead)
{
	ULONG ulRead = 0;

	// FIXME we currently accept any block size for reading, should this be capped at say 64k ?
	// cb = std::min(cb, 65536);
	// Outlookspy tries to read the whole thing into a small textbox in one go which takes rather long
	// so I suspect PST files and Exchange have some kind of limit here (it should never be a problem
	// if the client is correctly coded, but hey ...)
	auto hr = lpMemBlock->ReadAt(static_cast<ULONG>(liPos.QuadPart),
	          cb, static_cast<char *>(pv), &ulRead);
	liPos.QuadPart += ulRead;
	if(pcbRead)
		*pcbRead = ulRead;
	return hr;
}

HRESULT ECMemStream::Write(const void *pv, ULONG cb, ULONG *pcbWritten)
{
	ULONG ulWritten = 0;

	if(!(ulFlags&STGM_WRITE))
		return MAPI_E_NO_ACCESS;
	auto hr = lpMemBlock->WriteAt(static_cast<ULONG>(liPos.QuadPart),
	          cb, static_cast<const char *>(pv), &ulWritten);
	if(hr != hrSuccess)
		return hr;
	liPos.QuadPart += ulWritten;
	if(pcbWritten)
		*pcbWritten = ulWritten;
	fDirty = TRUE;

	// If we're not in transacted mode, don't auto-commit; we should wait for the user
	// to commit() the flags. In exclusive mode, nobody else can see this stream, so there's
	// no point in committing already. We simply defer the commit until the stream is Released
	if(!(ulFlags & STGM_TRANSACTED) && !(ulFlags & STGM_SHARE_EXCLUSIVE))
		Commit(0);
	return hrSuccess;
}

HRESULT ECMemStream::Seek(LARGE_INTEGER dlibmove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition)
{
	ULONG ulSize = 0;
	auto hr = lpMemBlock->GetSize(&ulSize);
	if(hr != hrSuccess)
		return hr;

	switch(dwOrigin) {
	case SEEK_SET:
		liPos.QuadPart = dlibmove.QuadPart;
		break;
	case SEEK_CUR:
		liPos.QuadPart += dlibmove.QuadPart;
		break;
	case SEEK_END:
		liPos.QuadPart = ulSize + dlibmove.QuadPart;
		break;
	}
	if(liPos.QuadPart > ulSize)
		liPos.QuadPart = ulSize;
	if(plibNewPosition)
		plibNewPosition->QuadPart = liPos.QuadPart;
	return hrSuccess;
}

HRESULT ECMemStream::SetSize(ULARGE_INTEGER libNewSize)
{
	if(!(ulFlags&STGM_WRITE))
		return MAPI_E_NO_ACCESS;
	auto hr = lpMemBlock->SetSize(static_cast<ULONG>(libNewSize.QuadPart));
	fDirty = true;
	return hr;
}

HRESULT ECMemStream::CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten)
{
	ULONG ulOffset = 0, ulWritten = 0, ulSize = 0;
	auto hr = lpMemBlock->GetSize(&ulSize);
	if(hr != hrSuccess)
		return hr;

	assert(liPos.u.HighPart == 0);
	ulOffset = liPos.u.LowPart;

	while(cb.QuadPart && ulSize > ulOffset) {
		pstm->Write(lpMemBlock->GetBuffer() + ulOffset, std::min(ulSize - ulOffset, cb.u.LowPart), &ulWritten);
		ulOffset += ulWritten;
		cb.QuadPart -= ulWritten;
	}

	if(pcbRead)
		pcbRead->QuadPart = ulOffset - liPos.u.LowPart;
	if(pcbWritten)
		pcbWritten->QuadPart = ulOffset - liPos.u.LowPart;
	liPos.QuadPart = ulOffset;
	return hrSuccess;
}

HRESULT ECMemStream::Commit(DWORD grfCommitFlags)
{
	object_ptr<IStream> lpClonedStream;
	auto hr = lpMemBlock->Commit();
	if(hr != hrSuccess)
		return hr;

	// If there is no commit func, just ignore the commit
	if (lpCommitFunc != nullptr) {
		hr = Clone(&~lpClonedStream);
		if(hr != hrSuccess)
			return hr;
		hr = lpCommitFunc(lpClonedStream, lpParam);
	}
	fDirty = false;
	return hr;
}

HRESULT ECMemStream::Revert()
{
	auto hr = lpMemBlock->Revert();
	liPos.QuadPart = 0;
	return hr;
}

/* we don't support locking ! */
HRESULT ECMemStream::LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
	return STG_E_INVALIDFUNCTION;
}

HRESULT ECMemStream::UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
	return STG_E_INVALIDFUNCTION;
}

HRESULT ECMemStream::Stat(STATSTG *pstatstg, DWORD grfStatFlag)
{
	ULONG ulSize = 0;

	if (pstatstg == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpMemBlock->GetSize(&ulSize);
	if(hr != hrSuccess)
		return hr;

	memset(pstatstg, 0, sizeof(STATSTG));
	pstatstg->cbSize.QuadPart = ulSize;
	pstatstg->type = STGTY_STREAM;
	pstatstg->grfMode = ulFlags;
	return hrSuccess;
}

HRESULT ECMemStream::Clone(IStream **ppstm)
{
	object_ptr<ECMemStream> lpStream;
	auto hr = ECMemStream::Create(lpMemBlock, ulFlags, lpCommitFunc, lpDeleteFunc, lpParam, &~lpStream);
	if (hr != hrSuccess)
		return hr;
	return lpStream->QueryInterface(IID_IStream, reinterpret_cast<void **>(ppstm));
}

ULONG ECMemStream::GetSize()
{
	ULONG ulSize = 0;
	lpMemBlock->GetSize(&ulSize);
	return ulSize;
}

char* ECMemStream::GetBuffer()
{
	return lpMemBlock->GetBuffer();
}

} /* namespace */
