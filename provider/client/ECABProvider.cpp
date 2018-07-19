/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include <mapi.h>
#include <kopano/charset/convstring.h>
#include <kopano/mapiext.h>
#include <mapispi.h>
#include <mapiutil.h>
#include "kcore.hpp"
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include "ECABProvider.h"
#include "ECABLogon.h"
#include <kopano/Util.h>

#include "WSTransport.h"
#include "ClientUtil.h"
#include "EntryPoint.h"
#include "ProviderUtil.h"
#include "pcutil.hpp"
#include <kopano/ECGetText.h>

using namespace KC;

ECABProvider::ECABProvider(ULONG ulFlags, const char *cls_name) :
	ECUnknown(cls_name), m_ulFlags(ulFlags)
{}

HRESULT ECABProvider::Create(ECABProvider **lppECABProvider)
{
	return alloc_wrap<ECABProvider>(0, "ECABProvider").put(lppECABProvider);
}

HRESULT ECABProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABProvider, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABProvider::Shutdown(ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECABProvider::Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam,
    const TCHAR *lpszProfileName, ULONG ulFlags, ULONG *lpulcbSecurity,
    LPBYTE *lppbSecurity, LPMAPIERROR *lppMAPIError, LPABLOGON *lppABLogon)
{
	if (lpMAPISup == nullptr || lppABLogon == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<ECABLogon> lpABLogon;
	sGlobalProfileProps	sProfileProps;
	object_ptr<WSTransport> lpTransport;
	// Get the username and password from the profile settings
	auto hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if(hr != hrSuccess)
		return hr;

	// Create a transport for this provider
	hr = WSTransport::Create(ulFlags, &~lpTransport);
	if(hr != hrSuccess)
		return hr;
	// Log on the transport to the server
	hr = lpTransport->HrLogon(sProfileProps);
	if(hr != hrSuccess)
		return hr;
	hr = ECABLogon::Create(lpMAPISup, lpTransport, sProfileProps.ulProfileFlags, nullptr, &~lpABLogon);
	if(hr != hrSuccess)
		return hr;
	AddChild(lpABLogon);

	hr = lpABLogon->QueryInterface(IID_IABLogon, (void **)lppABLogon);
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

ECABProviderSwitch::ECABProviderSwitch(void) : ECUnknown("ECABProviderSwitch")
{
}

HRESULT ECABProviderSwitch::Create(ECABProviderSwitch **lppECABProvider)
{
	return alloc_wrap<ECABProviderSwitch>().put(lppECABProvider);
}

HRESULT ECABProviderSwitch::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABProvider, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABProviderSwitch::Shutdown(ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECABProviderSwitch::Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam,
    const TCHAR *lpszProfileName, ULONG ulFlags, ULONG *lpulcbSecurity,
    LPBYTE *lppbSecurity, LPMAPIERROR *lppMAPIError, LPABLOGON *lppABLogon)
{
	PROVIDER_INFO sProviderInfo;
	object_ptr<IABLogon> lpABLogon;
	object_ptr<IABProvider> lpOnline;

	convstring tstrProfileName(lpszProfileName, ulFlags);
	auto hr = GetProviders(&g_mapProviders, lpMAPISup, convstring(lpszProfileName, ulFlags).c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		return hr;
	hr = sProviderInfo.lpABProviderOnline->QueryInterface(IID_IABProvider, &~lpOnline);
	if (hr != hrSuccess)
		return hr;

	// Online
	hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags, nullptr, nullptr, nullptr, &~lpABLogon);
	// Set the provider in the right connection type
	if (SetProviderMode(lpMAPISup, &g_mapProviders,
	    convstring(lpszProfileName, ulFlags).c_str(), CT_ONLINE) != hrSuccess)
		return MAPI_E_INVALID_PARAMETER;

	if(hr != hrSuccess) {
		if (hr == MAPI_E_NETWORK_ERROR)
			/* for disable public folders, so you can work offline */
			return MAPI_E_FAILONEPROVIDER;
		else if (hr == MAPI_E_LOGON_FAILED)
			return MAPI_E_UNCONFIGURED; /* Linux error ?? */
			//hr = MAPI_E_LOGON_FAILED;
		else
			return MAPI_E_LOGON_FAILED;
	}

	hr = lpMAPISup->SetProviderUID((LPMAPIUID)&MUIDECSAB, 0);
	if(hr != hrSuccess)
		return hr;
	hr = lpABLogon->QueryInterface(IID_IABLogon, (void **)lppABLogon);
	if(hr != hrSuccess)
		return hr;
	if(lpulcbSecurity)
		*lpulcbSecurity = 0;

	if(lppbSecurity)
		*lppbSecurity = NULL;

	if (lppMAPIError)
		*lppMAPIError = NULL;
	return hrSuccess;
}
