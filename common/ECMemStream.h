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

#ifndef ECMEMSTREAM_H
#define ECMEMSTREAM_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>

/* The ECMemBlock class is basically a random-access block of data that can be
 * read from and written to, expanded and contracted, and has a Commit and Revert
 * function to save and reload data.
 *
 * The commit and revert functions use memory sparingly, as only changed blocks
 * are held in memory.
 */


class ECMemBlock _zcp_final : public ECUnknown {
private:
	ECMemBlock(char *buffer, ULONG ulDataLen, ULONG ulFlags);
	~ECMemBlock();

public:
	static HRESULT	Create(char *buffer, ULONG ulDataLen, ULONG ulFlags, ECMemBlock **lppStream);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;

	virtual HRESULT	ReadAt(ULONG ulPos, ULONG ulLen, char *buffer, ULONG *ulBytesRead);
	virtual HRESULT WriteAt(ULONG ulPos, ULONG ulLen, char *buffer, ULONG *ulBytesWritten);
	virtual HRESULT Commit();
	virtual HRESULT Revert();
	virtual HRESULT SetSize(ULONG ulSize);
	virtual HRESULT GetSize(ULONG *ulSize);

	virtual char *GetBuffer(void) { return lpCurrent; }

private:
	char *	lpCurrent;
	ULONG	cbCurrent, cbTotal;
	char *	lpOriginal;
	ULONG	cbOriginal;
	ULONG	ulFlags;
};

/* 
 * This is an IStream-compatible wrapper for ECMemBlock
 */

class ECMemStream _zcp_final : public ECUnknown {
public:
	typedef HRESULT (*CommitFunc)(IStream *lpStream, void *lpParam);
	typedef HRESULT (*DeleteFunc)(void *lpParam); /* Caller's function to remove lpParam data from memory */

private:
	ECMemStream(char *buffer, ULONG ulDAtaLen, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc, void *lpParam);
	ECMemStream(ECMemBlock *lpMemBlock, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc, void *lpParam);
	~ECMemStream();

public:
	static  HRESULT	Create(char *buffer, ULONG ulDataLen, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc,
						   void *lpParam, ECMemStream **lppStream);
	static  HRESULT	Create(ECMemBlock *lpMemBlock, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc,
						   void *lpParam, ECMemStream **lppStream);

	virtual ULONG Release(void) _zcp_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;

	virtual HRESULT Read(void *pv, ULONG cb, ULONG *pcbRead);
	virtual HRESULT Write(const void *pv, ULONG cb, ULONG *pcbWritten);
	virtual HRESULT Seek(LARGE_INTEGER dlibmove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition);
	virtual HRESULT SetSize(ULARGE_INTEGER libNewSize);
	virtual HRESULT CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten);
	virtual HRESULT Commit(DWORD grfCommitFlags);
	virtual HRESULT Revert();
	virtual HRESULT LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
	virtual HRESULT UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
	virtual HRESULT Stat(STATSTG *pstatstg, DWORD grfStatFlag);
	virtual HRESULT Clone(IStream **ppstm);

	virtual ULONG GetSize();
	virtual char* GetBuffer();

	class xStream _zcp_final : public IStream {
		// AddRef and Release from ECUnknown
		virtual ULONG   __stdcall AddRef(void) _zcp_override;
		virtual ULONG   __stdcall Release(void) _zcp_override;
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, LPVOID *lppInterface) _zcp_override;

		virtual HRESULT __stdcall Read(void *pv, ULONG cb, ULONG *pcbRead) _zcp_override;
		virtual HRESULT __stdcall Write(const void *pv, ULONG cb, ULONG *pcbWritten) _zcp_override;
		virtual HRESULT __stdcall Seek(LARGE_INTEGER dlibmove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) _zcp_override;
		virtual HRESULT __stdcall SetSize(ULARGE_INTEGER libNewSize) _zcp_override;
		virtual HRESULT __stdcall CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) _zcp_override;
		virtual HRESULT __stdcall Commit(DWORD grfCommitFlags) _zcp_override;
		virtual HRESULT __stdcall Revert() _zcp_override;
		virtual HRESULT __stdcall LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) _zcp_override;
		virtual HRESULT __stdcall UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) _zcp_override;
		virtual HRESULT __stdcall Stat(STATSTG *pstatstg, DWORD grfStatFlag) _zcp_override;
		virtual HRESULT __stdcall Clone(IStream **ppstm) _zcp_override;
	} m_xStream;

private:
	ULARGE_INTEGER	liPos;
	ECMemBlock		*lpMemBlock;
	CommitFunc		lpCommitFunc;
	DeleteFunc		lpDeleteFunc;
	void *			lpParam;
	BOOL			fDirty;
	ULONG			ulFlags;
};

#endif // ECMEMSTREAM_H
