/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
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
	virtual HRESULT Commit(unsigned int flags) override;

	protected:
	~ECMessageStreamImporterIStreamAdapter();

private:
	ECMessageStreamImporterIStreamAdapter(WSMessageStreamImporter *lpStreamImporter);

	KC::object_ptr<WSMessageStreamImporter> m_ptrStreamImporter;
	KC::object_ptr<WSMessageStreamSink> m_ptrSink;
	ALLOC_WRAP_FRIEND;
};
