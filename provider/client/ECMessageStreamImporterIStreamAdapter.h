/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECMessageStreamImporterIStreamAdapter_INCLUDED
#define ECMessageStreamImporterIStreamAdapter_INCLUDED

#include <kopano/memory.hpp>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/zcdefs.h>
#include "WSMessageStreamImporter.h"

/**
 * This class wraps a WSMessageStreamImporter object and exposes it as an IStream.
 * The actual import callto the server will be initiated by the first write to the
 * IStream.
 * On commit, the call thread will block until the asynchronous call has completed, and
 * the return value will be returned.
 */
class ECMessageStreamImporterIStreamAdapter KC_FINAL_OPG :
    public KC::ECUnknown, public IStream {
public:
	static HRESULT Create(WSMessageStreamImporter *lpStreamImporter, IStream **lppStream);

	// IUnknown
	virtual HRESULT QueryInterface(const IID &, void **) override;

	// ISequentialStream
	virtual HRESULT Read(void *pv, unsigned int cb, unsigned int *has_read) override;
	virtual HRESULT Write(const void *pv, unsigned int cb, unsigned int *has_written) override;

	// IStream
	virtual HRESULT Seek(LARGE_INTEGER move, unsigned int origin, ULARGE_INTEGER *newpos) override;
	virtual HRESULT SetSize(ULARGE_INTEGER newsize) override;
	virtual HRESULT CopyTo(IStream *, ULARGE_INTEGER size, ULARGE_INTEGER *has_read, ULARGE_INTEGER *has_written) override;
	virtual HRESULT Commit(unsigned int flags) override;
	virtual HRESULT Revert() override;
	virtual HRESULT LockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER size, unsigned int lock_type) override;
	virtual HRESULT UnlockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER size, unsigned int lock_type) override;
	virtual HRESULT Stat(STATSTG *, unsigned int flag) override;
	virtual HRESULT Clone(IStream **) override;

	protected:
	~ECMessageStreamImporterIStreamAdapter();

private:
	ECMessageStreamImporterIStreamAdapter(WSMessageStreamImporter *lpStreamImporter);

	WSMessageStreamImporterPtr	m_ptrStreamImporter;
	KC::object_ptr<WSMessageStreamSink> m_ptrSink;
	ALLOC_WRAP_FRIEND;
};

#endif // ndef ECMessageStreamImporterIStreamAdapter_INCLUDED
