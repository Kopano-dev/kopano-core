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
#include <kopano/ECInterfaceDefs.h>
#include <kopano/memory.hpp>
#include <mapi.h>
#include <mapiutil.h>
#include <mapispi.h>
#include "ECABProvider.h"

#include <kopano/ECGetText.h>

#include <kopano/ECGuid.h>
#include <edkguid.h>

#include <kopano/Trace.h>
#include <kopano/ECDebug.h>

#include "EntryPoint.h"
#include <kopano/mapiext.h>

#include "ECABProviderSwitch.h"
#include "ProviderUtil.h"

#include <kopano/charset/convstring.h>

using namespace KCHL;

ECABProviderSwitch::ECABProviderSwitch(void) : ECUnknown("ECABProviderSwitch")
{
}

HRESULT ECABProviderSwitch::Create(ECABProviderSwitch **lppECABProvider)
{
	HRESULT hr = hrSuccess;

	ECABProviderSwitch *lpECABProvider = new ECABProviderSwitch();

	hr = lpECABProvider->QueryInterface(IID_ECABProvider, (void **)lppECABProvider);

	if(hr != hrSuccess)
		delete lpECABProvider;

	return hr;
}

HRESULT ECABProviderSwitch::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECABProvider, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABProvider, &this->m_xABProvider);
	REGISTER_INTERFACE2(IUnknown, &this->m_xABProvider);
	REGISTER_INTERFACE3(ISelectUnicode, IUnknown, &this->m_xUnknown);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABProviderSwitch::Shutdown(ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECABProviderSwitch::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG ulFlags, ULONG * lpulcbSecurity, LPBYTE * lppbSecurity, LPMAPIERROR * lppMAPIError, LPABLOGON * lppABLogon)
{
	HRESULT hr = hrSuccess;
	PROVIDER_INFO sProviderInfo;
	ULONG ulConnectType = CT_UNSPECIFIED;
	object_ptr<IABLogon> lpABLogon;
	object_ptr<IABProvider> lpOnline;

	convstring tstrProfileName(lpszProfileName, ulFlags);
	hr = GetProviders(&g_mapProviders, lpMAPISup, convstring(lpszProfileName, ulFlags).c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		goto exit;
	hr = sProviderInfo.lpABProviderOnline->QueryInterface(IID_IABProvider, &~lpOnline);
	if (hr != hrSuccess)
		goto exit;

	// Online
	hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags, nullptr, nullptr, nullptr, &~lpABLogon);
	ulConnectType = CT_ONLINE;

	// Set the provider in the right connection type
	if (SetProviderMode(lpMAPISup, &g_mapProviders, convstring(lpszProfileName, ulFlags).c_str(), ulConnectType) != hrSuccess) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if(hr != hrSuccess) {
		if(ulFlags & MDB_NO_DIALOG) {
			hr = MAPI_E_FAILONEPROVIDER;
			goto exit;
		} else if(hr == MAPI_E_NETWORK_ERROR) {
			hr = MAPI_E_FAILONEPROVIDER; //for disable public folders, so you can work offline
			goto exit;
		} else if (hr == MAPI_E_LOGON_FAILED) {
			hr = MAPI_E_UNCONFIGURED; // Linux error ??//
			//hr = MAPI_E_LOGON_FAILED;
			goto exit;
		}else{
			hr = MAPI_E_LOGON_FAILED;
			goto exit;
		}
	}

	hr = lpMAPISup->SetProviderUID((LPMAPIUID)&MUIDECSAB, 0);
	if(hr != hrSuccess)
		goto exit;
	
	hr = lpABLogon->QueryInterface(IID_IABLogon, (void **)lppABLogon);
	if(hr != hrSuccess)
		goto exit;

	if(lpulcbSecurity)
		*lpulcbSecurity = 0;

	if(lppbSecurity)
		*lppbSecurity = NULL;

	if (lppMAPIError)
		*lppMAPIError = NULL;

exit:
	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECABProviderSwitch, ABProvider, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECABProviderSwitch, ABProvider, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECABProviderSwitch, ABProvider, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECABProviderSwitch, ABProvider, Shutdown, (ULONG *, lpulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECABProviderSwitch, ABProvider, Logon, (LPMAPISUP, lpMAPISup), (ULONG, ulUIParam), (LPTSTR, lpszProfileName), (ULONG, ulFlags), (ULONG *, lpulcbSecurity), (LPBYTE *, lppbSecurity), (LPMAPIERROR *, lppMAPIError), (LPABLOGON *, lppABLogon))
