/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <algorithm>
#include <mapicode.h>
#include <mapidefs.h>
#include <mapiguid.h>
#include "IStreamAdapter.h"

namespace KC {

IStreamAdapter::IStreamAdapter(std::string &str) : m_str(str)
{}

HRESULT IStreamAdapter::QueryInterface(REFIID iid, void **pv){
	if(iid == IID_IStream || iid == IID_ISequentialStream || iid == IID_IUnknown) {
		*pv = this;
		return hrSuccess;
	}
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT IStreamAdapter::Read(void *pv, ULONG cb, ULONG *pcbRead)
{
	size_t toread = std::min(cb, (ULONG)(m_str.size() - m_pos));
	
	memcpy(pv, m_str.data() + m_pos, toread);
	
	m_pos += toread;
	
	if(pcbRead)
		*pcbRead = toread;
	
	return hrSuccess;
}

HRESULT IStreamAdapter::Write(const void *pv, ULONG cb, ULONG *pcbWritten)
{
	if (m_pos + cb > m_str.size())
		m_str.resize(m_pos+cb);

	memcpy(const_cast<char *>(m_str.data() + m_pos), pv, cb);
	m_pos += cb;
	
	if(pcbWritten)
		*pcbWritten = cb;
	
	return hrSuccess;
}

HRESULT IStreamAdapter::Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition)
{
	if(dwOrigin == SEEK_SET) {
		if (dlibMove.QuadPart < 0)
			m_pos = 0;
		else
			m_pos = dlibMove.QuadPart;
	}
	else if(dwOrigin == SEEK_CUR) {
		if (dlibMove.QuadPart < 0 &&
		    m_pos < static_cast<ULONGLONG>(-dlibMove.QuadPart))
			m_pos = 0;
		else
			m_pos += dlibMove.QuadPart;
	}
	else if(dwOrigin == SEEK_END) {
		if (dlibMove.QuadPart < 0 &&
		    m_str.size() < static_cast<ULONGLONG>(-dlibMove.QuadPart))
			m_pos = 0;
		else
			m_pos = m_str.size() + dlibMove.QuadPart;
	}

	// Fix overflow		
	if (m_pos > m_str.size())
		m_pos = m_str.size();
	
	if (plibNewPosition)
		plibNewPosition->QuadPart = m_pos;
		
	return hrSuccess;
}

HRESULT IStreamAdapter::SetSize(ULARGE_INTEGER libNewSize)
{
	LARGE_INTEGER zero = { { 0 } };
	m_str.resize(libNewSize.QuadPart);
	return Seek(zero, 0, NULL);
}

HRESULT IStreamAdapter::CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten)
{
	char buf[4096];
	ULONG len = 0;
	
	while(1) {
		auto hr = Read(buf, sizeof(buf), &len);
		if(hr != hrSuccess)
			return hr;
			
		if(len == 0)
			break;
			
		hr = pstm->Write(buf, len, NULL);
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT IStreamAdapter::Stat(STATSTG *pstatstg, DWORD grfStatFlag)
{
	memset(pstatstg, 0, sizeof(STATSTG));
	pstatstg->cbSize.QuadPart = m_str.size();
	
	return hrSuccess;
}

} /* namespace */
