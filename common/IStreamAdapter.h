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

#ifndef ISTREAMADAPTER_H
#define ISTREAMADAPTER_H

#include <kopano/zcdefs.h>

class IStreamAdapter _zcp_final : public IStream {
public:
    IStreamAdapter(std::string& str);
	virtual ~IStreamAdapter(void) {}
    
    virtual HRESULT QueryInterface(REFIID iid, void **pv);
	virtual ULONG AddRef(void) { return 1; }
	virtual ULONG Release(void) { return 1; }
    virtual HRESULT Read(void *pv, ULONG cb, ULONG *pcbRead);
    virtual HRESULT Write(const void *pv, ULONG cb, ULONG *pcbWritten);
    virtual HRESULT Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition);
    virtual HRESULT SetSize(ULARGE_INTEGER libNewSize);
    virtual HRESULT CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten);
	virtual HRESULT Commit(DWORD flags) { return hrSuccess; }
	virtual HRESULT Revert(void) { return hrSuccess; }
	virtual HRESULT LockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD lock) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT UnlockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD lock) { return MAPI_E_NO_SUPPORT; }
    virtual HRESULT Stat(STATSTG *pstatstg, DWORD grfStatFlag);
	virtual HRESULT Clone(IStream **) { return MAPI_E_NO_SUPPORT; }
	IStream *get(void) { return this; }
private:
    size_t	m_pos;
    std::string& m_str;
};

#endif

