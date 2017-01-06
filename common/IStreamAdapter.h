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

namespace KC {

class _kc_export IStreamAdapter _kc_final : public IStream {
public:
    IStreamAdapter(std::string& str);
	_kc_hidden virtual ~IStreamAdapter(void) _kc_impdtor;
	_kc_hidden virtual HRESULT QueryInterface(REFIID iid, void **pv) _kc_override;
	_kc_hidden virtual ULONG AddRef(void) _kc_override { return 1; }
	_kc_hidden virtual ULONG Release(void) _kc_override { return 1; }
	virtual HRESULT Read(void *pv, ULONG cb, ULONG *pcbRead) _kc_override;
	virtual HRESULT Write(const void *pv, ULONG cb, ULONG *pcbWritten) _kc_override;
	virtual HRESULT Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) _kc_override;
	virtual HRESULT SetSize(ULARGE_INTEGER libNewSize) _kc_override;
	virtual HRESULT CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) _kc_override;
	_kc_hidden virtual HRESULT Commit(DWORD flags) _kc_override { return hrSuccess; }
	_kc_hidden virtual HRESULT Revert(void) _kc_override { return hrSuccess; }
	_kc_hidden virtual HRESULT LockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD lock) _kc_override { return MAPI_E_NO_SUPPORT; }
	_kc_hidden virtual HRESULT UnlockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD lock) _kc_override { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT Stat(STATSTG *pstatstg, DWORD grfStatFlag) _kc_override;
	_kc_hidden virtual HRESULT Clone(IStream **) _kc_override { return MAPI_E_NO_SUPPORT; }
	_kc_hidden IStream *get(void) { return this; }
private:
    size_t	m_pos;
    std::string& m_str;
};

} /* namespace */

#endif

