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
#include "ECMessageStreamImporterIStreamAdapter.h"
#include <kopano/ECInterfaceDefs.h>

HRESULT ECMessageStreamImporterIStreamAdapter::Create(WSMessageStreamImporter *lpStreamImporter, IStream **lppStream)
{
	ECMessageStreamImporterIStreamAdapterPtr ptrAdapter;

	if (lpStreamImporter == NULL || lppStream == NULL)
		return MAPI_E_INVALID_PARAMETER;

	try {
		ptrAdapter.reset(new ECMessageStreamImporterIStreamAdapter(lpStreamImporter));
	} catch (const std::bad_alloc &) {
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}

	return ptrAdapter->QueryInterface(IID_IStream, reinterpret_cast<LPVOID *>(lppStream));
}

HRESULT ECMessageStreamImporterIStreamAdapter::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_ISequentialStream, &this->m_xSequentialStream);
	REGISTER_INTERFACE(IID_IStream, &this->m_xStream);

	return ECUnknown::QueryInterface(refiid, lppInterface);
}

// ISequentialStream
HRESULT ECMessageStreamImporterIStreamAdapter::Read(void* /*pv*/, ULONG /*cb*/, ULONG* /*pcbRead*/)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMessageStreamImporterIStreamAdapter::Write(const void *pv, ULONG cb, ULONG *pcbWritten)
{
	HRESULT hr;

	if (!m_ptrSink) {
		hr = m_ptrStreamImporter->StartTransfer(&m_ptrSink);
		if (hr != hrSuccess)
			return hr;
	}

	hr = m_ptrSink->Write((LPVOID)pv, cb);
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
	HRESULT hr;
	HRESULT hrAsync = hrSuccess;

	if (m_ptrSink == NULL)
		return MAPI_E_UNCONFIGURED;

	m_ptrSink.reset();

	hr = m_ptrStreamImporter->GetAsyncResult(&hrAsync);
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



// ISequentialStream proxies
ULONG ECMessageStreamImporterIStreamAdapter::xSequentialStream::AddRef()
{
	METHOD_PROLOGUE_(ECMessageStreamImporterIStreamAdapter, SequentialStream);
	TRACE_MAPI(TRACE_ENTRY, "ISequentialStream::AddRef", "");
	return pThis->AddRef();
}

ULONG ECMessageStreamImporterIStreamAdapter::xSequentialStream::Release()
{
	METHOD_PROLOGUE_(ECMessageStreamImporterIStreamAdapter, SequentialStream);
	TRACE_MAPI(TRACE_ENTRY, "ISequentialStream::Release", "");	
	return pThis->Release();
}

DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, SequentialStream, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, SequentialStream, Read, (void *, pv), (ULONG, cb), (ULONG *, pcbRead))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, SequentialStream, Write, (const void *, pv), (ULONG, cb), (ULONG *, pcbWritten))


// IStream proxies
ULONG ECMessageStreamImporterIStreamAdapter::xStream::AddRef()
{
	METHOD_PROLOGUE_(ECMessageStreamImporterIStreamAdapter, Stream);
	TRACE_MAPI(TRACE_ENTRY, "IStream::AddRef", "");
	return pThis->AddRef();
}

ULONG ECMessageStreamImporterIStreamAdapter::xStream::Release()
{
	METHOD_PROLOGUE_(ECMessageStreamImporterIStreamAdapter, Stream);
	TRACE_MAPI(TRACE_ENTRY, "IStream::Release", "");	
	return pThis->Release();
}

DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, Read, (void *, pv), (ULONG, cb), (ULONG *, pcbRead))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, Write, (const void *, pv), (ULONG, cb), (ULONG *, pcbWritten))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, Seek, (LARGE_INTEGER, dlibMove), (DWORD, dwOrigin), (ULARGE_INTEGER *, plibNewPosition))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, SetSize, (ULARGE_INTEGER, libNewSize))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, CopyTo, (IStream *, pstm), (ULARGE_INTEGER, cb), (ULARGE_INTEGER *, pcbRead), (ULARGE_INTEGER *, pcbWritten))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, Commit, (DWORD, grfCommitFlags))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, Revert, (void))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, LockRegion, (ULARGE_INTEGER, libOffset), (ULARGE_INTEGER, cb), (DWORD, dwLockType))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, UnlockRegion, (ULARGE_INTEGER, libOffset), (ULARGE_INTEGER, cb), (DWORD, dwLockType))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, Stat, (STATSTG *, pstatstg), (DWORD, grfStatFlag))
DEF_HRMETHOD(TRACE_MAPI, ECMessageStreamImporterIStreamAdapter, Stream, Clone, (IStream **, ppstm))
