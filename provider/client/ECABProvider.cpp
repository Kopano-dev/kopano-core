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
#include <new>
#include <kopano/platform.h>
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
using namespace KCHL;

ECABProvider::ECABProvider(ULONG ulFlags, const char *szClassName) :
    ECUnknown(szClassName)
{
	m_ulFlags = ulFlags;
}

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
	HRESULT			hr = hrSuccess;
	object_ptr<ECABLogon> lpABLogon;
	sGlobalProfileProps	sProfileProps;
	LPMAPIUID	lpGuid = NULL;
	object_ptr<WSTransport> lpTransport;

	if (lpMAPISup == nullptr || lppABLogon == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// Get the username and password from the profile settings
	hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
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
	hr = ECABLogon::Create(lpMAPISup, lpTransport, sProfileProps.ulProfileFlags, (GUID *)lpGuid, &~lpABLogon);
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
