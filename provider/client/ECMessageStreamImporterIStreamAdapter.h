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

#ifndef ECMessageStreamImporterIStreamAdapter_INCLUDED
#define ECMessageStreamImporterIStreamAdapter_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include "WSMessageStreamImporter.h"
#include <kopano/mapi_ptr.h>

/**
 * This class wraps a WSMessageStreamImporter object and exposes it as an IStream.
 * The actual import callto the server will be initiated by the first write to the
 * IStream.
 * On commit, the call thread will block until the asynchronous call has completed, and
 * the return value will be returned.
 */
class ECMessageStreamImporterIStreamAdapter : public ECUnknown
{
public:
	static HRESULT Create(WSMessageStreamImporter *lpStreamImporter, IStream **lppStream);

	// IUnknown
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

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

private:
	class xSequentialStream _zcp_final : public ISequentialStream {
		// IUnknown
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
		
		// ISequentialStream
		virtual HRESULT __stdcall Read(void *pv, ULONG cb, ULONG *pcbRead) _zcp_override;
		virtual HRESULT __stdcall Write(const void *pv, ULONG cb, ULONG *pcbWritten) _zcp_override;
	} m_xSequentialStream;

	class xStream _zcp_final : public IStream {
		// IUnknown
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
		
		// ISequentialStream
		virtual HRESULT __stdcall Read(void *pv, ULONG cb, ULONG *pcbRead);
		virtual HRESULT __stdcall Write(const void *pv, ULONG cb, ULONG *pcbWritten);
	
		// IStream
	    virtual HRESULT __stdcall Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition);
	    virtual HRESULT __stdcall SetSize(ULARGE_INTEGER libNewSize);
	    virtual HRESULT __stdcall CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten);
	    virtual HRESULT __stdcall Commit(DWORD grfCommitFlags);
	    virtual HRESULT __stdcall Revert(void);
	    virtual HRESULT __stdcall LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
	    virtual HRESULT __stdcall UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
	    virtual HRESULT __stdcall Stat(STATSTG *pstatstg, DWORD grfStatFlag);
	    virtual HRESULT __stdcall Clone(IStream **ppstm);
	} m_xStream;

private:
	WSMessageStreamImporterPtr	m_ptrStreamImporter;
	WSMessageStreamSinkPtr		m_ptrSink;
};

typedef mapi_object_ptr<ECMessageStreamImporterIStreamAdapter> ECMessageStreamImporterIStreamAdapterPtr;

#endif // ndef ECMessageStreamImporterIStreamAdapter_INCLUDED
