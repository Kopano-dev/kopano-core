/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <kopano/platform.h>

#include <mapix.h>
#include <kopano/ECGuid.h>
#include "ECMemStream.h"
#include <kopano/Trace.h>
#include <kopano/ECDebug.h>


/*
 * ECMemBlock implementation 
 *
 */

#define EC_MEMBLOCK_SIZE 8192

ECMemBlock::ECMemBlock(char *buffer, ULONG ulDataLen, ULONG ulFlags) : ECUnknown("ECMemBlock")
{
	this->cbTotal = 0;
	this->cbCurrent = 0;
	this->lpCurrent = NULL;
	this->cbOriginal = 0;
	this->lpOriginal = NULL;
	this->ulFlags = ulFlags;

	if(ulDataLen > 0) {
		cbTotal = ulDataLen;
		cbCurrent = ulDataLen;
		lpCurrent = (char *)malloc(ulDataLen);
		memcpy(lpCurrent, buffer, ulDataLen);

		if(ulFlags & STGM_TRANSACTED) {
			cbOriginal = ulDataLen;
			lpOriginal = (char *)malloc(ulDataLen);
			memcpy(lpOriginal, buffer, ulDataLen);
		}
	}
}

ECMemBlock::~ECMemBlock()
{
	free(lpCurrent);
	if (ulFlags & STGM_TRANSACTED)
		free(lpOriginal);
}

HRESULT	ECMemBlock::Create(char *buffer, ULONG ulDataLen, ULONG ulFlags, ECMemBlock **lppStream)
{
	ECMemBlock *lpMemBlock = NULL;

	try {
		lpMemBlock = new ECMemBlock(buffer, ulDataLen, ulFlags);
	} catch (std::exception &) {
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}

	return lpMemBlock->QueryInterface(IID_ECMemBlock, (void **)lppStream);
}

HRESULT ECMemBlock::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECMemBlock, this);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

// Reads at most ulLen chars, may be shorter due to shorter data len
HRESULT	ECMemBlock::ReadAt(ULONG ulPos, ULONG ulLen, char *buffer, ULONG *ulBytesRead)
{
	HRESULT hr = hrSuccess;
	ULONG ulToRead = cbCurrent - ulPos;

	ulToRead = ulLen < ulToRead ? ulLen : ulToRead;

	memcpy(buffer, lpCurrent+ulPos, ulToRead);

	if(ulBytesRead)
		*ulBytesRead = ulToRead;

	return hr;
}

HRESULT ECMemBlock::WriteAt(ULONG ulPos, ULONG ulLen, char *buffer, ULONG *ulBytesWritten)
{
	ULONG dsize = ulPos + ulLen;
	
	if(cbTotal < dsize) {
		ULONG newsize = cbTotal + ((dsize/EC_MEMBLOCK_SIZE)+1)*EC_MEMBLOCK_SIZE;	// + atleast 8k
		char *lpNew = (char *)realloc(lpCurrent, newsize);
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
	if(ulFlags & STGM_TRANSACTED) {
		free(lpOriginal);
		lpOriginal = NULL;

		lpOriginal = (char *)malloc(cbCurrent);
		if (lpOriginal == NULL)
			return MAPI_E_NOT_ENOUGH_MEMORY;

		cbOriginal = cbCurrent;
		memcpy(lpOriginal, lpCurrent, cbCurrent);
	}

	return hrSuccess;
}

HRESULT ECMemBlock::Revert()
{
	if(ulFlags & STGM_TRANSACTED) {
		free(lpCurrent);
		lpCurrent = NULL;

		lpCurrent = (char *)malloc(cbOriginal);
		if (lpCurrent == NULL)
			return MAPI_E_NOT_ENOUGH_MEMORY;

		cbCurrent = cbTotal = cbOriginal;
		memcpy(lpCurrent, lpOriginal, cbOriginal);
	}

	return hrSuccess;
}

HRESULT ECMemBlock::SetSize(ULONG ulSize)
{
	char *lpNew = (char *)malloc(ulSize);
	if (lpNew == NULL)
		return MAPI_E_NOT_ENOUGH_MEMORY;

	memcpy(lpNew, lpCurrent, ulSize < cbCurrent ? ulSize : cbCurrent);

	if(ulSize > cbCurrent)
		memset(lpNew+cbCurrent, 0, ulSize-cbCurrent);
	free(lpCurrent);
	lpCurrent = lpNew;
	cbCurrent = ulSize;
	cbTotal = ulSize;

	return hrSuccess;
}

HRESULT ECMemBlock::GetSize(ULONG *ulSize)
{
	*ulSize = cbCurrent;

	return hrSuccess;
}

/*
 * ECMemStream, IStream compatible in-memory stream object
 */

ECMemStream::ECMemStream(char *buffer, ULONG ulDataLen, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc,
						 void *lpParam) : ECUnknown("IStream")
{
	this->liPos.QuadPart = 0;
	ECMemBlock::Create(buffer, ulDataLen, ulFlags, &this->lpMemBlock);
	this->lpCommitFunc = lpCommitFunc;
	this->lpDeleteFunc = lpDeleteFunc;
	this->lpParam = lpParam;
	this->fDirty = FALSE;
	this->ulFlags = ulFlags;
}

ECMemStream::ECMemStream(ECMemBlock *lpMemBlock, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc,
						 void *lpParam) : ECUnknown("IStream")
{
	this->liPos.QuadPart = 0;
	this->lpMemBlock = lpMemBlock;
	lpMemBlock->AddRef();
	this->lpCommitFunc = lpCommitFunc;
	this->lpDeleteFunc = lpDeleteFunc;
	this->lpParam = lpParam;
	this->fDirty = FALSE;
	this->ulFlags = ulFlags;
}

ECMemStream::~ECMemStream()
{
	ULONG refs = 0;

	if(this->lpMemBlock)
		refs = this->lpMemBlock->Release();

	if (refs == 0 && this->lpDeleteFunc)
		lpDeleteFunc(lpParam);
}

HRESULT ECMemStream::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_IStream, &this->m_xStream);
	REGISTER_INTERFACE(IID_ISequentialStream, &this->m_xStream);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xStream);

	REGISTER_INTERFACE(IID_ECMemStream, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

ULONG ECMemStream::Release()
{
	if(this->m_cRef == 1) {
		// Releasing last reference

		// If you read the docs on STGM_SHARE_EXCLUSIVE it doesn't say you need
		// to Commit() at the end, so if the client hasn't called Commit() yet, 
		// we need to do it for them before throwing away the data.
		if(this->ulFlags & STGM_SHARE_EXCLUSIVE && this->fDirty) {
			this->Commit(0);
		}
	}
	return ECUnknown::Release();
}

HRESULT	ECMemStream::Create(char *buffer, ULONG ulDataLen, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc,
							void *lpParam, ECMemStream **lppStream)
{
	ECMemStream *lpStream = NULL;

	try {
		lpStream = new ECMemStream(buffer, ulDataLen, ulFlags, lpCommitFunc, lpDeleteFunc, lpParam);
	} catch (std::exception &) {
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}
	return lpStream->QueryInterface(IID_ECMemStream, (void **)lppStream);
}

HRESULT	ECMemStream::Create(ECMemBlock *lpMemBlock, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc,
							void *lpParam, ECMemStream **lppStream)
{
	ECMemStream *lpStream = NULL;

	try {
		lpStream = new ECMemStream(lpMemBlock, ulFlags, lpCommitFunc, lpDeleteFunc, lpParam);
	} catch (std::exception &) {
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}
	return lpStream->QueryInterface(IID_ECMemStream, (void **)lppStream);
}

HRESULT ECMemStream::Read(void *pv, ULONG cb, ULONG *pcbRead)
{
	HRESULT hr = hrSuccess;
	ULONG ulRead = 0;

	// FIXME we currently accept any block size for reading, should this be capped at say 64k ?
	// cb = cb > 65536 ? 65536 : cb;
	// Outlookspy tries to read the whole thing into a small textbox in one go which takes rather long
	// so I suspect PST files and Exchange have some kind of limit here (it should never be a problem
	// if the client is correctly coded, but hey ...)

	hr = this->lpMemBlock->ReadAt((ULONG)this->liPos.QuadPart, cb, (char *)pv, &ulRead);

	liPos.QuadPart += ulRead;

	if(pcbRead)
		*pcbRead = ulRead;

	return hr;
}

HRESULT ECMemStream::Write(const void *pv, ULONG cb, ULONG *pcbWritten)
{
	HRESULT hr;
	ULONG ulWritten = 0;

	if(!(ulFlags&STGM_WRITE))
		return MAPI_E_NO_ACCESS;

	hr = this->lpMemBlock->WriteAt((ULONG)this->liPos.QuadPart, cb, (char *)pv, &ulWritten);

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
	HRESULT hr;
	ULONG ulSize = 0;

	hr = this->lpMemBlock->GetSize(&ulSize);

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

	if(liPos.QuadPart < 0)
		liPos.QuadPart = 0;
	if(liPos.QuadPart > ulSize)
		liPos.QuadPart = ulSize;

	if(plibNewPosition)
		plibNewPosition->QuadPart = liPos.QuadPart;
	return hrSuccess;
}

HRESULT ECMemStream::SetSize(ULARGE_INTEGER libNewSize)
{
	HRESULT hr;

	if(!(ulFlags&STGM_WRITE))
		return MAPI_E_NO_ACCESS;

	hr = lpMemBlock->SetSize((ULONG)libNewSize.QuadPart);

	this->fDirty = TRUE;
	return hr;
}

HRESULT ECMemStream::CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten)
{
	HRESULT hr;
	ULONG ulOffset = 0;
	ULONG ulWritten = 0;
	ULONG ulSize = 0;
	
	hr = lpMemBlock->GetSize(&ulSize);
	if(hr != hrSuccess)
		return hr;

	ASSERT(liPos.u.HighPart == 0);
	ulOffset = liPos.u.LowPart;

	while(cb.QuadPart && ulSize > ulOffset) {
		pstm->Write(this->lpMemBlock->GetBuffer() + ulOffset, std::min(ulSize - ulOffset, cb.u.LowPart), &ulWritten);

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
	HRESULT hr = hrSuccess;
	IStream *lpClonedStream = NULL;

	hr = this->lpMemBlock->Commit();

	if(hr != hrSuccess)
		goto exit;

	// If there is no commit func, just ignore the commit
	if(this->lpCommitFunc) {

		hr = this->Clone(&lpClonedStream);

		if(hr != hrSuccess)
			goto exit;

		hr = this->lpCommitFunc(lpClonedStream, lpParam);
	}

	this->fDirty = FALSE;

exit:
	if(lpClonedStream)
		lpClonedStream->Release();

	return hr;
}

HRESULT ECMemStream::Revert()
{
	HRESULT hr = hrSuccess;

	hr = this->lpMemBlock->Revert();
	this->liPos.QuadPart = 0;

	return hr;
}

/* we don't support locking ! */
HRESULT ECMemStream::LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
	HRESULT hr = STG_E_INVALIDFUNCTION;

	hr=hrSuccess; //hack for loadsim

	return hr;
}

HRESULT ECMemStream::UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
	return hrSuccess; //hack for loadsim
	//return STG_E_INVALIDFUNCTION;
}

HRESULT ECMemStream::Stat(STATSTG *pstatstg, DWORD grfStatFlag)
{
	HRESULT hr;
	ULONG ulSize = 0;

	if (pstatstg == NULL)
		return MAPI_E_INVALID_PARAMETER;

	hr = this->lpMemBlock->GetSize(&ulSize);

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
	HRESULT hr = hrSuccess;
	ECMemStream *lpStream = NULL;

	ECMemStream::Create(this->lpMemBlock, ulFlags, this->lpCommitFunc, this->lpDeleteFunc, lpParam, &lpStream);

	hr = lpStream->QueryInterface(IID_IStream, (void **)ppstm);

	lpStream->Release();

	return hr;
}

ULONG ECMemStream::GetSize()
{
	ULONG ulSize = 0;
	this->lpMemBlock->GetSize(&ulSize);
	return ulSize;
}

char* ECMemStream::GetBuffer()
{
	return this->lpMemBlock->GetBuffer();
}

ULONG   ECMemStream::xStream::AddRef()
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::AddRef", "");
	METHOD_PROLOGUE_(ECMemStream, Stream);
	ULONG ulRef =  pThis->AddRef();
	TRACE_STREAM(TRACE_RETURN, "IStream::AddRef", "%d",  ulRef);
	return ulRef;
}

ULONG   ECMemStream::xStream::Release()
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::Release", "");
	METHOD_PROLOGUE_(ECMemStream, Stream);
	ULONG ulRef = pThis->Release();
	TRACE_STREAM(TRACE_RETURN, "IStream::Release", "%d",  ulRef);
	return ulRef;
}

HRESULT ECMemStream::xStream::QueryInterface(REFIID refiid, LPVOID *lppInterface)
{
	char szGuidId[1024+1];
	_snprintf(szGuidId, 1024, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", refiid.Data1, refiid.Data2, refiid.Data3, refiid.Data4[0], refiid.Data4[1], refiid.Data4[2], refiid.Data4[3], refiid.Data4[4], refiid.Data4[5], refiid.Data4[6], refiid.Data4[7]);
	
	TRACE_STREAM(TRACE_ENTRY, "IStream::QueryInterface", "%s", szGuidId);
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_STREAM(TRACE_RETURN, "IStream::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}


HRESULT ECMemStream::xStream::Read(void *pv, ULONG cb, ULONG *pcbRead)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::Read", "size=%d", cb);
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->Read(pv, cb, pcbRead);
	TRACE_STREAM(TRACE_RETURN, "IStream::Read", "read=%d, Result=%s", (pcbRead)?(*pcbRead):0, GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemStream::xStream::Write(const void *pv, ULONG cb, ULONG *pcbWritten)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::Write", "size=%d", cb);
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr =  pThis->Write(pv, cb, pcbWritten);
	TRACE_STREAM(TRACE_RETURN, "IStream::Write", "written=%d, Result=%s", (pcbWritten)?*pcbWritten:0, GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemStream::xStream::Seek(LARGE_INTEGER dlibmove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::Seek", "dlibmove=%d, dwOrigin=%d", (int)dlibmove.QuadPart, dwOrigin);
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->Seek(dlibmove, dwOrigin, plibNewPosition);
	TRACE_STREAM(TRACE_RETURN, "IStream::Seek", "newPos=%d, Result=%s", (plibNewPosition)?(int)plibNewPosition->QuadPart:0, GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemStream::xStream::SetSize(ULARGE_INTEGER libNewSize)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::SetSize", "%d",  (int)libNewSize.QuadPart);
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->SetSize(libNewSize);
	TRACE_STREAM(TRACE_RETURN, "IStream::SetSize", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemStream::xStream::CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::CopyTo", "cb=%d", cb.QuadPart);
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->CopyTo(pstm, cb, pcbRead, pcbWritten);
	TRACE_STREAM(TRACE_RETURN, "IStream::CopyTo", "%s cbRead=%d, cbWritten=%d", GetMAPIErrorDescription(hr).c_str(), (pcbRead)?pcbRead->QuadPart: 0, (pcbWritten)?pcbWritten->QuadPart: 0 );
	return hr;
}

HRESULT ECMemStream::xStream::Commit(DWORD grfCommitFlags)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::Commit", "grfCommitFlags=0x%08X", grfCommitFlags);
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->Commit(grfCommitFlags);
	TRACE_STREAM(TRACE_RETURN, "IStream::Commit", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemStream::xStream::Revert()
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::Revert", "");
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->Revert();
	TRACE_STREAM(TRACE_RETURN, "IStream::Revert", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemStream::xStream::LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::LockRegion", "");
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->LockRegion(libOffset, cb, dwLockType);
	TRACE_STREAM(TRACE_RETURN, "IStream::LockRegion", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemStream::xStream::UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::UnLockRegion", "");
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->UnlockRegion(libOffset, cb, dwLockType);
	TRACE_STREAM(TRACE_RETURN, "IStream::UnLockRegion", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemStream::xStream::Stat(STATSTG *pstatstg, DWORD grfStatFlag)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::Stat", "grfStatFlag=0x%08X", grfStatFlag);
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->Stat(pstatstg, grfStatFlag);
	TRACE_STREAM(TRACE_RETURN, "IStream::Stat", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemStream::xStream::Clone(IStream **ppstm)
{
	TRACE_STREAM(TRACE_ENTRY, "IStream::Clone", "");
	METHOD_PROLOGUE_(ECMemStream, Stream);
	HRESULT hr = pThis->Clone(ppstm);
	TRACE_STREAM(TRACE_RETURN, "IStream::Clone", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}
