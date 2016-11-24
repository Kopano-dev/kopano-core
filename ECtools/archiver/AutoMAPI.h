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

/* AutoMAPI.h
 * Declaration of class AutoMAPI
 */
#ifndef AUTOMAPI_H_INCLUDED
#define AUTOMAPI_H_INCLUDED

#include <kopano/zcdefs.h>

namespace KC {

class AutoMAPI _kc_final {
public:
	AutoMAPI() : m_bInitialized(false) {}
	~AutoMAPI() {
		if (m_bInitialized)
			MAPIUninitialize();
	}
	
	HRESULT Initialize(MAPIINIT_0 *lpMapiInit) {
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
	bool m_bInitialized;
};

} /* namespace */

#endif // !defined AUTOMAPI_H_INCLUDED
