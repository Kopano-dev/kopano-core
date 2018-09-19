/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <new>
#include "ECMessageStreamImporterIStreamAdapter.h"

HRESULT ECMessageStreamImporterIStreamAdapter::Create(WSMessageStreamImporter *lpStreamImporter, IStream **lppStream)
{
	if (lpStreamImporter == NULL || lppStream == NULL)
		return MAPI_E_INVALID_PARAMETER;
	return KC::alloc_wrap<ECMessageStreamImporterIStreamAdapter>(lpStreamImporter)
	       .as(IID_IStream, reinterpret_cast<void **>(lppStream));
}

HRESULT ECMessageStreamImporterIStreamAdapter::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(ISequentialStream, this);
	REGISTER_INTERFACE2(IStream, this);
	return ECUnknown::QueryInterface(refiid, lppInterface);
}

// ISequentialStream
HRESULT ECMessageStreamImporterIStreamAdapter::Read(void* /*pv*/, ULONG /*cb*/, ULONG* /*pcbRead*/)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMessageStreamImporterIStreamAdapter::Write(const void *pv, ULONG cb, ULONG *pcbWritten)
{
	if (!m_ptrSink) {
		auto hr = m_ptrStreamImporter->StartTransfer(&~m_ptrSink);
		if (hr != hrSuccess)
			return hr;
	}
	auto hr = m_ptrSink->Write(pv, cb);
	if (hr != hrSuccess)
		return hr;
	if (pcbWritten)
		*pcbWritten = cb;
	return hrSuccess;
}

// IStream
HRESULT ECMessageStreamImporterIStreamAdapter::Seek(LARGE_INTEGER /*dlibMove*/, DWORD /*dwOrigin*/, ULARGE_INTEGER* /*plibNewPosition*/)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMessageStreamImporterIStreamAdapter::SetSize(ULARGE_INTEGER /*libNewSize*/)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMessageStreamImporterIStreamAdapter::CopyTo(IStream* /*pstm*/, ULARGE_INTEGER /*cb*/, ULARGE_INTEGER* /*pcbRead*/, ULARGE_INTEGER* /*pcbWritten*/)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMessageStreamImporterIStreamAdapter::Commit(DWORD /*grfCommitFlags*/)
{
	HRESULT hrAsync = hrSuccess;

	if (m_ptrSink == NULL)
		return MAPI_E_UNCONFIGURED;
	m_ptrSink.reset();
	auto hr = m_ptrStreamImporter->GetAsyncResult(&hrAsync);
	if (hr == hrSuccess)
		hr = hrAsync;
	return hr;
}

HRESULT ECMessageStreamImporterIStreamAdapter::Revert(void)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMessageStreamImporterIStreamAdapter::LockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/, DWORD /*dwLockType*/)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMessageStreamImporterIStreamAdapter::UnlockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/, DWORD /*dwLockType*/)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMessageStreamImporterIStreamAdapter::Stat(STATSTG* /*pstatstg*/, DWORD /*grfStatFlag*/)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMessageStreamImporterIStreamAdapter::Clone(IStream** /*ppstm*/)
{
	return MAPI_E_NO_SUPPORT;
}

ECMessageStreamImporterIStreamAdapter::ECMessageStreamImporterIStreamAdapter(WSMessageStreamImporter *lpStreamImporter)
: m_ptrStreamImporter(lpStreamImporter, true)
{ }

ECMessageStreamImporterIStreamAdapter::~ECMessageStreamImporterIStreamAdapter()
{
	Commit(0);	// This causes us to wait for the async thread.
}
