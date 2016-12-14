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
 */
#include <kopano/platform.h>
#include <kopano/ECInterfaceDefs.h>
#include <kopano/memory.hpp>
#include <mapi.h>
#include <kopano/mapiext.h>
#include <mapispi.h>
#include <mapiutil.h>
#include "kcore.hpp"
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include "ECABProvider.h"
#include "ECABLogon.h"

#include <kopano/ECDebug.h>

#include <kopano/Util.h>

#include "WSTransport.h"
#include "ClientUtil.h"
#include "EntryPoint.h"
#include "pcutil.hpp"

typedef KCHL::memory_ptr<ECUSER> ECUserPtr;

#include <kopano/ECGetText.h>

using namespace std;

ECABProvider::ECABProvider(ULONG ulFlags, const char *szClassName) :
    ECUnknown(szClassName)
{
	m_ulFlags = ulFlags;
}

HRESULT ECABProvider::Create(ECABProvider **lppECABProvider)
{
	HRESULT hr = hrSuccess;

	ECABProvider *lpECABProvider = new ECABProvider(0, "ECABProvider");

	hr = lpECABProvider->QueryInterface(IID_ECABProvider, (void **)lppECABProvider);

	if(hr != hrSuccess)
		delete lpECABProvider;

	return hr;
}

HRESULT ECABProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECABProvider, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABProvider, &this->m_xABProvider);
	REGISTER_INTERFACE2(IUnknown, &this->m_xABProvider);
	REGISTER_INTERFACE3(ISelectUnicode, IUnknown, &this->m_xUnknown);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABProvider::Shutdown(ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECABProvider::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG ulFlags, ULONG * lpulcbSecurity, LPBYTE * lppbSecurity, LPMAPIERROR * lppMAPIError, LPABLOGON * lppABLogon)
{
	HRESULT			hr = hrSuccess;
	ECABLogon*		lpABLogon = NULL;
	sGlobalProfileProps	sProfileProps;
	LPMAPIUID	lpGuid = NULL;

	WSTransport*	lpTransport = NULL;

	if (!lpMAPISup || !lppABLogon) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Get the username and password from the profile settings
	hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if(hr != hrSuccess)
		goto exit;

	// Create a transport for this provider
	hr = WSTransport::Create(ulFlags, &lpTransport);
	if(hr != hrSuccess)
		goto exit;
	// Log on the transport to the server
	hr = lpTransport->HrLogon(sProfileProps);
	if(hr != hrSuccess)
		goto exit;

	hr = ECABLogon::Create(lpMAPISup, lpTransport, sProfileProps.ulProfileFlags, (GUID *)lpGuid, &lpABLogon);
	if(hr != hrSuccess)
		goto exit;

	AddChild(lpABLogon);

	hr = lpABLogon->QueryInterface(IID_IABLogon, (void **)lppABLogon);
	if(hr != hrSuccess)
		goto exit;

	if (lpulcbSecurity)
		*lpulcbSecurity = 0;

	if (lppbSecurity)
		*lppbSecurity = NULL;

	if (lppMAPIError)
		*lppMAPIError = NULL;

exit:
	if(lpABLogon)
		lpABLogon->Release();

	if(lpTransport)
		lpTransport->Release();

	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECABProvider, ABProvider, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECABProvider, ABProvider, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECABProvider, ABProvider, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECABProvider, ABProvider, Shutdown, (ULONG *, lpulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECABProvider, ABProvider, Logon, (LPMAPISUP, lpMAPISup), (ULONG, ulUIParam), (LPTSTR, lpszProfileName), (ULONG, ulFlags), (ULONG *, lpulcbSecurity), (LPBYTE *, lppbSecurity), (LPMAPIERROR *, lppMAPIError), (LPABLOGON *, lppABLogon))
