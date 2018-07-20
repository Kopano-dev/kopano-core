/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/* AutoMAPI.h
 * Declaration of class AutoMAPI
 */
#ifndef AUTOMAPI_H_INCLUDED
#define AUTOMAPI_H_INCLUDED

#include <kopano/zcdefs.h>
#include <mapix.h>

namespace KC {

class AutoMAPI _kc_final {
public:
	~AutoMAPI() {
		if (m_bInitialized)
			MAPIUninitialize();
	}
	
	HRESULT Initialize(MAPIINIT_0 *lpMapiInit = nullptr)
	{
		HRESULT hr = hrSuccess;

		if (!m_bInitialized) {
			hr = MAPIInitialize(lpMapiInit);		
			if (hr == hrSuccess)
				m_bInitialized = true;
		}
			
		return hr;
	}

	bool IsInitialized() const {
		return m_bInitialized;
	}

private:
	bool m_bInitialized = false;
};

} /* namespace */

#endif // !defined AUTOMAPI_H_INCLUDED
