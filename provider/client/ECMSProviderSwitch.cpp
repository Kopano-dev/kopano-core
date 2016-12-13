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

#include <kopano/ECGetText.h>
#include <kopano/ECInterfaceDefs.h>
#include <kopano/memory.hpp>
#include <mapi.h>
#include <mapiutil.h>
#include <mapispi.h>

#include "ECMSProviderSwitch.h"
#include "ECMSProvider.h"
#include <kopano/ECGuid.h>

#include <kopano/Trace.h>
#include <kopano/ECDebug.h>

#include <edkguid.h>
#include "EntryPoint.h"
#include "DLLGlobal.h"
#include <edkmdb.h>
#include <kopano/mapiext.h>

#include "ClientUtil.h"
#include "ECMsgStore.h"
#include <kopano/stringutil.h>
#include <csignal>

#include "ProviderUtil.h"

#include <kopano/charset/convstring.h>

#ifdef swprintf
	#undef swprintf
#endif

using namespace KCHL;

ECMSProviderSwitch::ECMSProviderSwitch(ULONG ulFlags) : ECUnknown("ECMSProviderSwitch")
{
	m_ulFlags = ulFlags;
}

HRESULT ECMSProviderSwitch::Create(ULONG ulFlags, ECMSProviderSwitch **lppMSProvider)
{
	ECMSProviderSwitch *lpMSProvider = new ECMSProviderSwitch(ulFlags);

	return lpMSProvider->QueryInterface(IID_ECUnknown/*IID_ECMSProviderSwitch*/, (void **)lppMSProvider);
}

HRESULT ECMSProviderSwitch::QueryInterface(REFIID refiid, void **lppInterface)
{
	/*refiid == IID_ECMSProviderSwitch */
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMSProvider, &this->m_xMSProvider);
	REGISTER_INTERFACE2(IUnknown, &this->m_xMSProvider);
	REGISTER_INTERFACE3(ISelectUnicode, IUnknown, &this->m_xUnknown);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

ULONG ECMSProviderSwitch::Release()
{
	return ECUnknown::Release();
}

HRESULT ECMSProviderSwitch::Shutdown(ULONG * lpulFlags) 
{
	HRESULT hr = hrSuccess;

	//FIXME
	return hr;
}

HRESULT ECMSProviderSwitch::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity, LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	HRESULT			hr = hrSuccess;
	ECMsgStore*		lpecMDB = NULL;

	sGlobalProfileProps	sProfileProps;
	LPPROFSECT		lpProfSect = NULL;
	memory_ptr<SPropValue> lpsPropArray, lpProp, lpIdentityProps;
	ULONG			cValues = 0;

	char*			lpDisplayName = NULL;
	bool			bIsDefaultStore = false;
	LPMDB			lpMDB = NULL;
	LPMSLOGON		lpMSLogon = NULL;

	PROVIDER_INFO sProviderInfo;
	ULONG ulConnectType = CT_UNSPECIFIED;
	IMSProvider *lpOnline = NULL;
	convert_context converter;
	memory_ptr<ENTRYID> lpStoreID;
	ULONG			cbStoreID = 0;

	convstring			tstrProfileName(lpszProfileName, ulFlags);

	// Get the username and password from the profile settings
	hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if (hr != hrSuccess)
		goto exit;

	// Open profile settings
	hr = lpMAPISup->OpenProfileSection(NULL, MAPI_MODIFY, &lpProfSect);
	if (hr != hrSuccess)
		goto exit;

	if (lpEntryID == NULL) {

		// Try to initialize the provider
		if (InitializeProvider(NULL, lpProfSect, sProfileProps, &cbStoreID, &~lpStoreID) != hrSuccess) {
			hr = MAPI_E_UNCONFIGURED;
			goto exit;
		}

		lpEntryID = lpStoreID;
		cbEntryID = cbStoreID;
	}

	static constexpr SizedSPropTagArray(1, proptag) = {1, {PR_MDB_PROVIDER}};
	hr = lpProfSect->GetProps(proptag, 0, &cValues, &~lpsPropArray);
	if (hr == hrSuccess && lpsPropArray[0].ulPropTag == PR_MDB_PROVIDER &&
	    (CompareMDBProvider(lpsPropArray[0].Value.bin.lpb, &KOPANO_SERVICE_GUID) ||
	     CompareMDBProvider(lpsPropArray[0].Value.bin.lpb, &MSEMS_SERVICE_GUID)))
			bIsDefaultStore = true;
	hr = hrSuccess;

	hr = GetProviders(&g_mapProviders, lpMAPISup, tstrProfileName.c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		goto exit;

	hr = sProviderInfo.lpMSProviderOnline->QueryInterface(IID_IMSProvider, (void **)&lpOnline);
	if (hr != hrSuccess)
		goto exit;

	// Default error
	hr = MAPI_E_LOGON_FAILED; //or MAPI_E_FAILONEPROVIDER

	// Connect online if any of the following is true:
	// - Online store was specifically requested (MDB_ONLINE)
	// - Profile is not offline capable
	// - Store being opened is not the user's default store

	if ((ulFlags & MDB_ONLINE) == MDB_ONLINE || (sProviderInfo.ulProfileFlags&EC_PROFILE_FLAGS_OFFLINE) != EC_PROFILE_FLAGS_OFFLINE || bIsDefaultStore == false)
	{
		bool fDone = false;

		while(!fDone) {
			hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags, lpInterface, NULL, NULL, NULL, &lpMSLogon, &lpMDB);
			ulConnectType = CT_ONLINE;
			fDone = true;
		}
	}

	// Set the provider in the right connection type
	if (bIsDefaultStore) {
		if (SetProviderMode(lpMAPISup, &g_mapProviders, tstrProfileName.c_str(), ulConnectType) != hrSuccess) {
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
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
	

	hr = lpMDB->QueryInterface(IID_ECMsgStore, (void **)&lpecMDB);
	if (hr != hrSuccess)
		goto exit;

	// Register ourselves with mapisupport
	hr = lpMAPISup->SetProviderUID((MAPIUID *)&lpecMDB->GetStoreGuid(), 0); 
	if (hr != hrSuccess)
		goto exit;

	// Set profile identity
	hr = ClientUtil::HrSetIdentity(lpecMDB->lpTransport, lpMAPISup, &~lpIdentityProps);
	if (hr != hrSuccess)
		goto exit;

	// Get store name
	// The server will return MAPI_E_UNCONFIGURED when an attempt is made to open a store
	// that does not exist on that server. However the store is only opened the first time
	// when it's actually needed.
	// Since this is the first call that actually needs information from the store, we need
	// to be prepared to handle the MAPI_E_UNCONFIGURED error as we want to propagate this
	// up to the caller so this 'error' can be resolved by reconfiguring the profile.
	hr = HrGetOneProp(lpMDB, PR_DISPLAY_NAME_A, &~lpProp);
	if (hr == MAPI_E_UNCONFIGURED)
		goto exit;
	if (hr != hrSuccess || lpProp->ulPropTag != PR_DISPLAY_NAME_A) {
		lpDisplayName = _A("Unknown");
		hr = hrSuccess;
	} else {
		lpDisplayName = lpProp->Value.lpszA;
	}

	if (CompareMDBProvider(&lpecMDB->m_guidMDB_Provider, &KOPANO_SERVICE_GUID) ||
		CompareMDBProvider(&lpecMDB->m_guidMDB_Provider, &KOPANO_STORE_DELEGATE_GUID))
	{
		hr = ClientUtil::HrInitializeStatusRow(lpDisplayName, MAPI_STORE_PROVIDER, lpMAPISup, lpIdentityProps, 0);
		if (hr != hrSuccess)
			goto exit;
	}

	if (lppMSLogon) {
		hr = lpMSLogon->QueryInterface(IID_IMSLogon, (void **)lppMSLogon);
		if (hr != hrSuccess)
			goto exit;
	}
	
	if (lppMDB) {
		hr = lpMDB->QueryInterface(IID_IMsgStore, (void **)lppMDB);
		if (hr != hrSuccess)
			goto exit;
	}

	// Store username and password so SpoolerLogon can log on to the profile
	if(lppbSpoolSecurity)
	{
		ULONG cbSpoolSecurity = sizeof(wchar_t) * (sProfileProps.strUserName.length() + sProfileProps.strPassword.length() + 1 + 1);

		hr = MAPIAllocateBuffer(cbSpoolSecurity, (void **)lppbSpoolSecurity);
		if(hr != hrSuccess)
			goto exit;

		swprintf((wchar_t*)*lppbSpoolSecurity, cbSpoolSecurity, L"%s%c%s", sProfileProps.strUserName.c_str(), 0, sProfileProps.strPassword.c_str());
		*lpcbSpoolSecurity = cbSpoolSecurity;
	}

exit:
	if (lppMAPIError)
		*lppMAPIError = NULL;
	if (lpProfSect)
		lpProfSect->Release();
	if (lpMSLogon)
		lpMSLogon->Release();
	
	if (lpMDB)
		lpMDB->Release();

	if (lpecMDB)
		lpecMDB->Release();
    
	if (lpOnline)
		lpOnline->Release();
	return hr;
}

HRESULT ECMSProviderSwitch::SpoolerLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG cbSpoolSecurity, LPBYTE lpbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	HRESULT hr = hrSuccess;
	IMSProvider *lpProvider = NULL; // Do not release
	PROVIDER_INFO sProviderInfo;
	LPMDB lpMDB = NULL;
	LPMSLOGON lpMSLogon = NULL;
	ECMsgStore *lpecMDB = NULL;

	if (lpEntryID == NULL) {
		hr = MAPI_E_UNCONFIGURED;
		goto exit;
	}
	
	if (cbSpoolSecurity == 0 || lpbSpoolSecurity == NULL) {
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	hr = GetProviders(&g_mapProviders, lpMAPISup, convstring(lpszProfileName, ulFlags).c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		goto exit;

	lpProvider = sProviderInfo.lpMSProviderOnline;
	hr = lpProvider->SpoolerLogon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags, lpInterface, cbSpoolSecurity, lpbSpoolSecurity, NULL, &lpMSLogon, &lpMDB);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMDB->QueryInterface(IID_ECMsgStore, (void **)&lpecMDB);
	if (hr != hrSuccess)
		goto exit;

	// Register ourselves with mapisupport
	hr = lpMAPISup->SetProviderUID((MAPIUID *)&lpecMDB->GetStoreGuid(), 0); 
	if (hr != hrSuccess)
		goto exit;

	if (lppMSLogon) {
		hr = lpMSLogon->QueryInterface(IID_IMSLogon, (void **)lppMSLogon);
		if (hr != hrSuccess)
			goto exit;
	}
	
	if (lppMDB) {
		hr = lpMDB->QueryInterface(IID_IMsgStore, (void **)lppMDB);
		if (hr != hrSuccess)
			goto exit;
	}

exit:
	if (lppMAPIError)
		*lppMAPIError = NULL;

	if (lpecMDB)
		lpecMDB->Release();

	if (lpMSLogon)
		lpMSLogon->Release();
	
	if (lpMDB)
		lpMDB->Release();

	return hr;
}
	
HRESULT ECMSProviderSwitch::CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	return ::CompareStoreIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
}

DEF_ULONGMETHOD1(TRACE_MAPI, ECMSProviderSwitch, MSProvider, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMSProviderSwitch, MSProvider, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMSProviderSwitch, MSProvider, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD1(TRACE_MAPI, ECMSProviderSwitch, MSProvider, Shutdown, (ULONG *, lpulFlags))

HRESULT ECMSProviderSwitch::xMSProvider::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity, LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderSwitch::Logon", "flags=%x, cbEntryID=%d", ulFlags, cbEntryID);
	METHOD_PROLOGUE_(ECMSProviderSwitch, MSProvider);
	HRESULT hr = pThis->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID,ulFlags, lpInterface, lpcbSpoolSecurity, lppbSpoolSecurity, lppMAPIError, lppMSLogon, lppMDB);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderSwitch::Logon", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMSProviderSwitch::xMSProvider::SpoolerLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG lpcbSpoolSecurity, LPBYTE lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderSwitch::SpoolerLogon", "flags=%x", ulFlags);
	METHOD_PROLOGUE_(ECMSProviderSwitch, MSProvider);
	HRESULT hr = pThis->SpoolerLogon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID,ulFlags, lpInterface, lpcbSpoolSecurity, lppbSpoolSecurity, lppMAPIError, lppMSLogon, lppMDB);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderSwitch::SpoolerLogon", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECMSProviderSwitch, MSProvider, CompareStoreIDs, (ULONG, cbEntryID1), (LPENTRYID, lpEntryID1), (ULONG, cbEntryID2), (LPENTRYID, lpEntryID2), (ULONG, ulFlags), (ULONG *, lpulResult))
