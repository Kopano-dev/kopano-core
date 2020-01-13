/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
	if (lppMapiError)
		*lppMapiError = NULL;
	return hrSuccess;
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
	hr = lpABProvider->QueryInterface(IID_IABProvider, reinterpret_cast<void **>(lppABProvider));
	if (hr != hrSuccess)
		return hr;
	*lpulProviderVer = CURRENT_SPI_VERSION;
	return hr;
}
