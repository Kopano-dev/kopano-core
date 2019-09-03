/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <new>
#include "ZCABProvider.h"
#include "ZCABLogon.h"
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiguid.h>
#include <kopano/ECGuid.h>
#include <kopano/memory.hpp>

using namespace KC;

ZCABProvider::ZCABProvider(ULONG ulFlags, const char *cls_name) :
	ECUnknown(cls_name)
{
}

HRESULT ZCABProvider::Create(ZCABProvider **lppZCABProvider)
{
	return alloc_wrap<ZCABProvider>(0, "ZCABProvider").put(lppZCABProvider);
}

HRESULT ZCABProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ZCABProvider, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABProvider, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ZCABProvider::Shutdown(ULONG * lpulFlags)
{
	*lpulFlags = 0;
	return hrSuccess;
}

HRESULT ZCABProvider::Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam,
    const TCHAR *lpszProfileName, ULONG ulFlags, ULONG *lpulcbSecurity,
    LPBYTE *lppbSecurity, LPMAPIERROR *lppMAPIError, LPABLOGON *lppABLogon)
{
	HRESULT hr = hrSuccess;
	object_ptr<ZCABLogon> lpABLogon;

	if (lpMAPISup == nullptr || lppABLogon == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// todo: remove flags & guid .. probably add other stuff from profile?
	hr = ZCABLogon::Create(lpMAPISup, 0, nullptr, &~lpABLogon);
	if(hr != hrSuccess)
		return hr;
	AddChild(lpABLogon);
	hr = lpABLogon->QueryInterface(IID_IABLogon, reinterpret_cast<void **>(lppABLogon));
	if(hr != hrSuccess)
		return hr;
	if (lpulcbSecurity)
		*lpulcbSecurity = 0;

	if (lppbSecurity)
		*lppbSecurity = NULL;

	if (lppMAPIError)
		*lppMAPIError = NULL;
	return hrSuccess;
}
