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
class ECMemBlock _kc_final : public ECUnknown {
private:
	ECMemBlock(char *buffer, ULONG ulDataLen, ULONG ulFlags);
	~ECMemBlock();

public:
	static HRESULT	Create(char *buffer, ULONG ulDataLen, ULONG ulFlags, ECMemBlock **lppStream);
	virtual HRESULT QueryInterface(REFIID refiid, void **iface) _kc_override;
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
class ECMemStream _kc_final : public ECUnknown {
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
	virtual ULONG Release(void) _kc_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **iface) _kc_override;
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

	class xStream _kc_final : public IStream {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/ISequentialStream.hpp>
		#include <kopano/xclsfrag/IStream.hpp>
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
