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
#include <mapispi.h>
#include <mapiutil.h>
#include <kopano/ECGuid.h>
#include <kopano/ECInterfaceDefs.h>
#include "kcore.hpp"
#include "ECXPProvider.h"
#include "ECXPLogon.h"

#include "WSTransport.h"
#include "Mem.h"

#include <kopano/Util.h>

#include <kopano/ECDebug.h>

#include "ClientUtil.h"
#include "EntryPoint.h"

#include <kopano/charset/convstring.h>
#include <kopano/ECGetText.h>

using namespace KCHL;

ECXPProvider::ECXPProvider() : ECUnknown("IXPProvider")
{
}

ECXPProvider::~ECXPProvider()
{
	if(m_lpIdentityProps)
		ECFreeBuffer(m_lpIdentityProps);
}

HRESULT ECXPProvider::Create(ECXPProvider **lppECXPProvider) {
	auto lpECXPProvider = new(std::nothrow) ECXPProvider;
	if (lpECXPProvider == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto ret = lpECXPProvider->QueryInterface(IID_ECXPProvider,
	           reinterpret_cast<void **>(lppECXPProvider));
	if (ret != hrSuccess)
		delete lpECXPProvider;
	return ret;
}

HRESULT ECXPProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECXPProvider, this);
	REGISTER_INTERFACE2(IXPProvider, &this->m_xXPProvider);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECXPProvider::Shutdown(ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECXPProvider::TransportLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG * lpulFlags, LPMAPIERROR * lppMAPIError, LPXPLOGON * lppXPLogon)
{
	HRESULT			hr = hrSuccess;
	object_ptr<ECXPLogon> lpXPLogon;
	object_ptr<WSTransport> lpTransport;
	ECMapProvider::const_iterator iterMap;
	std::string		strServerURL;
	std::string		strUniqueId;
	BOOL			bOffline = FALSE;
	convstring			tstrProfileName(lpszProfileName, *lpulFlags);
	std::string		strDisplayName;

	// Get transport by looking at how we have logged on. We assume here that a message store or addressbook has been
	// logged on before calling TransportLogon and therefore the connection type is never CT_UNSPECIFIED.
	iterMap = g_mapProviders.find(tstrProfileName);

	// Online if: no entry in map, OR map specifies online mode
	if (iterMap == g_mapProviders.cend() ||
	    iterMap->second.ulConnectType == CT_ONLINE) {
		// Online
		hr = WSTransport::HrOpenTransport(lpMAPISup, &~lpTransport, FALSE);
		bOffline = FALSE;
	} else {
		// Offline
		hr = WSTransport::HrOpenTransport(lpMAPISup, &~lpTransport, TRUE);
		bOffline = TRUE;
	}
	if (hr != hrSuccess)
		return MAPI_E_FAILONEPROVIDER;
	hr = ECXPLogon::Create(tstrProfileName, bOffline, this, lpMAPISup, &~lpXPLogon);
	if(hr != hrSuccess)
		return hr;
	hr = lpXPLogon->QueryInterface(IID_IXPLogon, (void **)lppXPLogon);
	if(hr != hrSuccess)
		return hr;
	AddChild(lpXPLogon);

	// Set profile identity
	hr = ClientUtil::HrSetIdentity(lpTransport, lpMAPISup, &m_lpIdentityProps);
	if(hr != hrSuccess)
		return hr;

	// Initialize statusrow
	strDisplayName = convert_to<std::string>(g_strManufacturer.c_str()) + _A(" Transport");

	hr = ClientUtil::HrInitializeStatusRow(strDisplayName.c_str(), MAPI_TRANSPORT_PROVIDER, lpMAPISup, m_lpIdentityProps, 0);
	if(hr != hrSuccess)
		return hr;
	*lpulFlags = 0;
	*lppMAPIError = NULL;
	return hrSuccess;
}

DEF_HRMETHOD1(TRACE_MAPI, ECXPProvider, XPProvider, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECXPProvider, XPProvider, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECXPProvider, XPProvider, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECXPProvider, XPProvider, Shutdown, (ULONG *, lpulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECXPProvider, XPProvider, TransportLogon, (LPMAPISUP, lpMAPISup), (ULONG, ulUIParam), (LPTSTR, lpszProfileName), (ULONG *, lpulFlags), (LPMAPIERROR *, lppMAPIError), (LPXPLOGON *, lppXPLogon))
