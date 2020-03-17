/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <mapix.h>

namespace KC {

class AutoMAPI KC_FINAL {
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
