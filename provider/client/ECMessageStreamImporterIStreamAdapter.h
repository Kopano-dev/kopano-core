/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECMessageStreamImporterIStreamAdapter_INCLUDED
#define ECMessageStreamImporterIStreamAdapter_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include "WSMessageStreamImporter.h"

/**
 * This class wraps a WSMessageStreamImporter object and exposes it as an IStream.
 * The actual import callto the server will be initiated by the first write to the
 * IStream.
 * On commit, the call thread will block until the asynchronous call has completed, and
 * the return value will be returned.
 */
class ECMessageStreamImporterIStreamAdapter final :
    public KC::ECUnknown, public IStream {
public:
	static HRESULT Create(WSMessageStreamImporter *lpStreamImporter, IStream **lppStream);

	// IUnknown
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

	// ISequentialStream
	virtual HRESULT Read(void *pv, ULONG cb, ULONG *pcbRead);
	virtual HRESULT Write(const void *pv, ULONG cb, ULONG *pcbWritten);

	// IStream
	virtual HRESULT Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition);
	virtual HRESULT SetSize(ULARGE_INTEGER libNewSize);
	virtual HRESULT CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten);
	virtual HRESULT Commit(DWORD grfCommitFlags);
	virtual HRESULT Revert(void);
	virtual HRESULT LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
	virtual HRESULT UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
	virtual HRESULT Stat(STATSTG *pstatstg, DWORD grfStatFlag);
	virtual HRESULT Clone(IStream **ppstm);

private:
	ECMessageStreamImporterIStreamAdapter(WSMessageStreamImporter *lpStreamImporter);
	~ECMessageStreamImporterIStreamAdapter();

	WSMessageStreamImporterPtr	m_ptrStreamImporter;
	KC::object_ptr<WSMessageStreamSink> m_ptrSink;
	ALLOC_WRAP_FRIEND;
};

#endif // ndef ECMessageStreamImporterIStreamAdapter_INCLUDED
