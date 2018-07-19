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
#include <mapidefs.h>
#include <mapispi.h>
#include <kopano/memory.hpp>
#include "ZCABProvider.h"
#include "EntryPoint.h"

using namespace KC;

HRESULT MSGServiceEntry(HINSTANCE hInst, LPMALLOC lpMalloc,
    LPMAPISUP psup, ULONG ulUIParam, ULONG ulFlags, ULONG ulContext,
    ULONG cvals, const SPropValue *pvals, IProviderAdmin *lpAdminProviders,
    MAPIERROR **lppMapiError)
{
	HRESULT hr = hrSuccess;

	switch(ulContext) {
	case MSG_SERVICE_INSTALL:
		hr = hrSuccess;
		break;
	case MSG_SERVICE_UNINSTALL:
		hr = hrSuccess;
		break;
	case MSG_SERVICE_DELETE:
		hr = hrSuccess;
		break;
	case MSG_SERVICE_PROVIDER_CREATE:
		// we never get here in linux (see M4LProviderAdmin::CreateProvider)
		assert(false);
		hr = hrSuccess;
		break;
	case MSG_SERVICE_PROVIDER_DELETE:
		hr = hrSuccess;
		break;
	case MSG_SERVICE_CONFIGURE:
	case MSG_SERVICE_CREATE:
		hr = hrSuccess;
		break;
	};

	if (lppMapiError)
		*lppMapiError = NULL;
	return hr;
}

HRESULT ABProviderInit(HINSTANCE hInstance, LPMALLOC lpMalloc,
    LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore,
    LPFREEBUFFER lpFreeBuffer, ULONG ulFlags, ULONG ulMAPIVer,
    ULONG *lpulProviderVer, LPABPROVIDER *lppABProvider)
{
	object_ptr<ZCABProvider> lpABProvider;

	if (ulMAPIVer < CURRENT_SPI_VERSION)
		return MAPI_E_VERSION;

	// create provider and query interface.
	auto hr = ZCABProvider::Create(&~lpABProvider);
	if (hr != hrSuccess)
		return hr;
	hr = lpABProvider->QueryInterface(IID_IABProvider, (void **)lppABProvider);
	if (hr != hrSuccess)
		return hr;
	*lpulProviderVer = CURRENT_SPI_VERSION;
	return hr;
}
