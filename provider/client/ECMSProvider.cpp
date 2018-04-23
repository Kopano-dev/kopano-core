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
#include <new>
#include <string>
#include <kopano/platform.h>
#include <mapi.h>
#include <mapiutil.h>
#include <mapispi.h>

#include <kopano/ECGetText.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include "Mem.h"

#include <kopano/ECGuid.h>
#include "ECMSProvider.h"
#include "ECMsgStore.h"
#include "ECABProvider.h"
#include "ClientUtil.h"
#include "EntryPoint.h"

#include "WSUtil.h"
#include "pcutil.hpp"
#include "ProviderUtil.h"
#include <kopano/stringutil.h>

#include <edkguid.h>

#include <cwchar>
#include <kopano/charset/convert.h>
#include <kopano/charset/utf8string.h>
#include <edkmdb.h>
#include <kopano/mapiext.h>
#include <csignal>
#include <kopano/charset/convstring.h>

using namespace KC;

ECMSProvider::ECMSProvider(ULONG ulFlags, const char *cls_name) :
	ECUnknown(cls_name), m_ulFlags(ulFlags)
{
}

HRESULT ECMSProvider::Create(ULONG ulFlags, ECMSProvider **lppECMSProvider) {
	return alloc_wrap<ECMSProvider>(ulFlags, "IMSProvider").put(lppECMSProvider);
}

HRESULT ECMSProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMSProvider, this);
	REGISTER_INTERFACE2(IMSProvider, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMSProvider::Shutdown(ULONG * lpulFlags) 
{
	return hrSuccess;
}

HRESULT ECMSProvider::Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam,
    const TCHAR *lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID,
    ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity,
    LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError,
    LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	object_ptr<WSTransport> lpTransport;
	object_ptr<ECMsgStore> lpECMsgStore;
	object_ptr<ECMSLogon> lpECMSLogon;
	object_ptr<IProfSect> lpProfSect;
	unsigned int cValues = 0, ulStoreType = 0;
	memory_ptr<SPropValue> lpsPropArray;
	BOOL			fIsDefaultStore = FALSE;
	MAPIUID			guidMDBProvider;
	sGlobalProfileProps	sProfileProps;

	// If the EntryID is not configured, return MAPI_E_UNCONFIGURED, this will
	// cause MAPI to call our configuration entry point (MSGServiceEntry)
	if (lpEntryID == nullptr)
		return MAPI_E_UNCONFIGURED;
	if(lpcbSpoolSecurity)
		*lpcbSpoolSecurity = 0;
	if(lppbSpoolSecurity)
		*lppbSpoolSecurity = NULL;

	// Get the username and password from the profile settings
	auto hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if(hr != hrSuccess)
		return hr;

	// Open profile settings
	hr = lpMAPISup->OpenProfileSection(nullptr, MAPI_MODIFY, &~lpProfSect);
	if(hr != hrSuccess)
		return hr;

	static constexpr const SizedSPropTagArray(2, proptags) =
		{2, {PR_MDB_PROVIDER, PR_RESOURCE_FLAGS}};
	hr = lpProfSect->GetProps(proptags, 0, &cValues, &~lpsPropArray);
	if (FAILED(hr))
		return hr;

	if (lpsPropArray[1].ulPropTag == PR_RESOURCE_FLAGS &&
	    lpsPropArray[1].Value.ul & STATUS_DEFAULT_STORE)
		fIsDefaultStore = TRUE;
	// Create a transport for this message store
	hr = WSTransport::Create(ulFlags, &~lpTransport);
	if(hr != hrSuccess)
		return hr;
	hr = LogonByEntryID(lpTransport, &sProfileProps, cbEntryID, lpEntryID);
	if (lpsPropArray[0].ulPropTag == PR_MDB_PROVIDER) {
		memcpy(&guidMDBProvider, lpsPropArray[0].Value.bin.lpb, sizeof(MAPIUID));
	} else if (fIsDefaultStore == FALSE){
		// also fallback to private store when logon failed (hr, do not change)
		if (hr != hrSuccess || lpTransport->HrGetStoreType(cbEntryID, lpEntryID, &ulStoreType) != hrSuccess)
			// Maintain backward-compat: if connecting to a server that does not support the storetype
			// call, assume private store, which is what happened before this call was introduced
			ulStoreType = ECSTORE_TYPE_PRIVATE;

		if (ulStoreType == ECSTORE_TYPE_PRIVATE)
			memcpy(&guidMDBProvider, &KOPANO_STORE_DELEGATE_GUID, sizeof(MAPIUID));
		else if (ulStoreType == ECSTORE_TYPE_PUBLIC)
			memcpy(&guidMDBProvider, &KOPANO_STORE_PUBLIC_GUID, sizeof(MAPIUID));
		else if (ulStoreType == ECSTORE_TYPE_ARCHIVE)
			memcpy(&guidMDBProvider, &KOPANO_STORE_ARCHIVE_GUID, sizeof(MAPIUID));
		else {
			assert(false);
			return MAPI_E_NO_SUPPORT;
		}
	} else {
		memcpy(&guidMDBProvider, &KOPANO_SERVICE_GUID, sizeof(MAPIUID));
	}
	if(hr != hrSuccess)
		return hr;

	// Get a message store object
	hr = CreateMsgStoreObject(reinterpret_cast<const char *>(sProfileProps.strProfileName.c_str()),
	     lpMAPISup, cbEntryID, lpEntryID, ulFlags,
	     sProfileProps.ulProfileFlags, lpTransport, &guidMDBProvider,
	     false, fIsDefaultStore, false, &~lpECMsgStore);
	if(hr != hrSuccess)
		return hr;

	// Register ourselves with mapisupport
	//hr = lpMAPISup->SetProviderUID((MAPIUID *)&lpMsgStore->GetStoreGuid(), 0); 
	//if(hr != hrSuccess)
	//	return hr;
	
	// Return the variables
	if(lppMDB) { 
		hr = lpECMsgStore->QueryInterface(IID_IMsgStore, (void **)lppMDB);

		if(hr != hrSuccess)
			return hr;
	}
	if (lppMSLogon == nullptr)
		return hrSuccess;
	// We don't count lpMSLogon as a child, because its lifetime is coupled to lpMsgStore
	hr = ECMSLogon::Create(lpECMsgStore, &~lpECMSLogon);
	if(hr != hrSuccess)
		return hr;
	return lpECMSLogon->QueryInterface(IID_IMSLogon, reinterpret_cast<void **>(lppMSLogon));
}

HRESULT ECMSProvider::SpoolerLogon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam,
    const TCHAR *lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID,
    ULONG ulFlags, LPCIID lpInterface, ULONG cbSpoolSecurity,
    LPBYTE lpbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon,
    LPMDB *lppMDB)
{
	object_ptr<WSTransport> lpTransport;
	object_ptr<ECMsgStore> lpMsgStore;
	object_ptr<ECMSLogon> lpLogon;
	MAPIUID	guidMDBProvider;
	object_ptr<IProfSect> lpProfSect;
	ULONG cValues = 0;
	LPSPropValue lpsPropArray = NULL;
	sGlobalProfileProps	sProfileProps;

	if (lpEntryID == nullptr)
		return MAPI_E_UNCONFIGURED;
	if (cbSpoolSecurity == 0 || lpbSpoolSecurity == nullptr)
		return MAPI_E_NO_ACCESS;

	// Get Global profile settings
	auto hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if(hr != hrSuccess)
		return hr;

	// Open profile settings
	hr = lpMAPISup->OpenProfileSection(nullptr, MAPI_MODIFY, &~lpProfSect);
	if(hr != hrSuccess)
		return hr;

	static constexpr const SizedSPropTagArray(2, proptags) =
		{2, {PR_MDB_PROVIDER, PR_RESOURCE_FLAGS}};

	// Get the MDBProvider from the profile settings
	hr = lpProfSect->GetProps(proptags, 0, &cValues, &lpsPropArray);
	if(hr == hrSuccess || hr == MAPI_W_ERRORS_RETURNED)
	{
		if (lpsPropArray[0].ulPropTag == PR_MDB_PROVIDER)
				memcpy(&guidMDBProvider, lpsPropArray[0].Value.bin.lpb, sizeof(MAPIUID));
		if (lpsPropArray[1].ulPropTag == PR_RESOURCE_FLAGS &&
		    !(lpsPropArray[1].Value.ul & STATUS_DEFAULT_STORE))
			/* Deny spooler logon to any store that is not the default store */
			return MAPI_E_NOT_FOUND;
	}

	if (cbSpoolSecurity % sizeof(wchar_t) != 0)
		return MAPI_E_INVALID_PARAMETER;
	auto strSep = wmemchr(reinterpret_cast<wchar_t *>(lpbSpoolSecurity), 0, cbSpoolSecurity / sizeof(wchar_t));
	if (strSep == NULL)
		return MAPI_E_NO_ACCESS;
	++strSep;
	sProfileProps.strUserName = (wchar_t*)lpbSpoolSecurity;
	sProfileProps.strPassword = strSep;

	// Create a transport for this message store
	hr = WSTransport::Create(ulFlags, &~lpTransport);
	if(hr != hrSuccess)
		return hr;
	hr = LogonByEntryID(lpTransport, &sProfileProps, cbEntryID, lpEntryID);
	if (hr != hrSuccess)
		return MAPI_E_UNCONFIGURED;

	// Get a message store object
	hr = CreateMsgStoreObject((LPSTR)sProfileProps.strProfileName.c_str(), lpMAPISup, cbEntryID, lpEntryID, ulFlags, sProfileProps.ulProfileFlags, lpTransport,
	     &guidMDBProvider, true, true, false, &~lpMsgStore);
	if(hr != hrSuccess)
		return hr;

	// Register ourselves with mapisupport
	//guidStore = lpMsgStore->GetStoreGuid();
	//hr = lpMAPISup->SetProviderUID((MAPIUID *)&guidStore, 0); 
	//if(hr != hrSuccess)
	//	goto exit;

	// Return the variables
	if(lppMDB) {
		hr = lpMsgStore->QueryInterface(IID_IMsgStore, (void **)lppMDB);

		if(hr != hrSuccess)
			return hr;
	}
	if (lppMSLogon == nullptr)
		return hrSuccess;
	hr = ECMSLogon::Create(lpMsgStore, &~lpLogon);
	if(hr != hrSuccess)
		return hr;
	return lpLogon->QueryInterface(IID_IMSLogon, reinterpret_cast<void **>(lppMSLogon));
}
	
HRESULT ECMSProvider::CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	return ::CompareStoreIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
}

/**
 * Log a WSTransport object on to a server based on the URL found in the provided store entryid.
 *
 * @param[in,out]	lppTransport		Double pointer to the WSTransport object that is to be logged on. This
 *										is a double pointer because a new WSTransport object will be returned if
 *										the entryid contains a pseudo URL that is resolved to another server than
 *										the server specified in the profile.
 * @param[in,out]	lpsProfileProps		The profile properties used to connect to logon to the server. If the
 *										entryid doesn't contain a pseudo URL, the serverpath in the profile properties
 *										will be updated to contain the path extracted from the entryid.
 * @param[in]		cbEntryID			The length of the passed entryid in bytes.
 * @param[in]		lpEntryID			Pointer to the store entryid from which the server path will be extracted.
 *
 * @retval	MAPI_E_FAILONEPROVIDER		Returned when the extraction of the URL failed.
 */
HRESULT ECMSProvider::LogonByEntryID(object_ptr<WSTransport> &lpTransport,
    sGlobalProfileProps *lpsProfileProps, ULONG cbEntryID, ENTRYID *lpEntryID)
{
	std::string extractedServerPath; // The extracted server path
	bool		bIsPseudoUrl = false;

	assert(lpTransport != nullptr);
	auto hr = HrGetServerURLFromStoreEntryId(cbEntryID, lpEntryID, extractedServerPath, &bIsPseudoUrl);
	if (hr != hrSuccess)
		return MAPI_E_FAILONEPROVIDER;

	// Log on the transport to the server
	if (!bIsPseudoUrl) {
		sGlobalProfileProps sOtherProps = *lpsProfileProps;
		
		sOtherProps.strServerPath = extractedServerPath;
		hr = lpTransport->HrLogon(sOtherProps);
		if (hr != hrSuccess)
			// If we failed to open a non-pseudo-URL, fallback to using the server from the global
			// profile section. We need this because some older versions wrote a non-pseudo URL, which
			// we should still support - even when the hostname of the server changes for example.
			hr = lpTransport->HrLogon(*lpsProfileProps);
		return hr;
	}

	std::string strServerPath; // The resolved server path
	bool bIsPeer;

	hr = lpTransport->HrLogon(*lpsProfileProps);
	if (hr != hrSuccess)
		return hr;
	hr = HrResolvePseudoUrl(lpTransport, extractedServerPath.c_str(), strServerPath, &bIsPeer);
	if (hr != hrSuccess)
		return hr;
	if (bIsPeer)
		return hrSuccess;
	object_ptr<WSTransport> lpAltTransport;
	hr = lpTransport->CreateAndLogonAlternate(strServerPath.c_str(), &~lpAltTransport);
	if (hr != hrSuccess)
		return hr;
	lpTransport->HrLogOff();
	lpTransport = std::move(lpAltTransport);
	return hrSuccess;
}

#ifdef swprintf
#	undef swprintf
#endif

ECMSProviderSwitch::ECMSProviderSwitch(ULONG ulFlags) : ECUnknown("ECMSProviderSwitch")
{
	m_ulFlags = ulFlags;
}

HRESULT ECMSProviderSwitch::Create(ULONG ulFlags, ECMSProviderSwitch **lppMSProvider)
{
	return alloc_wrap<ECMSProviderSwitch>(ulFlags).put(lppMSProvider);
}

HRESULT ECMSProviderSwitch::QueryInterface(REFIID refiid, void **lppInterface)
{
	/*refiid == IID_ECMSProviderSwitch */
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMSProvider, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMSProviderSwitch::Shutdown(ULONG * lpulFlags)
{
	//FIXME
	return hrSuccess;
}

HRESULT ECMSProviderSwitch::Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam,
    const TCHAR *lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID,
    ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity,
    LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError,
    LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	object_ptr<ECMsgStore> lpecMDB;
	sGlobalProfileProps	sProfileProps;
	object_ptr<IProfSect> lpProfSect;
	memory_ptr<SPropValue> lpsPropArray, lpProp, lpIdentityProps;
	ULONG			cValues = 0;

	char*			lpDisplayName = NULL;
	bool			bIsDefaultStore = false;
	object_ptr<IMsgStore> lpMDB;
	object_ptr<IMSLogon> lpMSLogon;
	PROVIDER_INFO sProviderInfo;
	ULONG ulConnectType = CT_UNSPECIFIED;
	object_ptr<IMSProvider> lpOnline;
	convert_context converter;
	memory_ptr<ENTRYID> lpStoreID;
	ULONG			cbStoreID = 0;

	convstring			tstrProfileName(lpszProfileName, ulFlags);
	auto laters = make_scope_success([&]() {
		if (lppMAPIError != nullptr)
			*lppMAPIError = nullptr;
	});

	// Get the username and password from the profile settings
	auto hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if (hr != hrSuccess)
		return hr;

	// Open profile settings
	hr = lpMAPISup->OpenProfileSection(nullptr, MAPI_MODIFY, &~lpProfSect);
	if (hr != hrSuccess)
		return hr;

	if (lpEntryID == NULL) {

		// Try to initialize the provider
		if (InitializeProvider(NULL, lpProfSect, sProfileProps, &cbStoreID, &~lpStoreID) != hrSuccess)
			return MAPI_E_UNCONFIGURED;

		lpEntryID = lpStoreID;
		cbEntryID = cbStoreID;
	}

	static constexpr const SizedSPropTagArray(1, proptag) = {1, {PR_MDB_PROVIDER}};
	hr = lpProfSect->GetProps(proptag, 0, &cValues, &~lpsPropArray);
	if (hr == hrSuccess && lpsPropArray[0].ulPropTag == PR_MDB_PROVIDER &&
	    (CompareMDBProvider(lpsPropArray[0].Value.bin.lpb, &KOPANO_SERVICE_GUID) ||
	     CompareMDBProvider(lpsPropArray[0].Value.bin.lpb, &MSEMS_SERVICE_GUID)))
			bIsDefaultStore = true;
	hr = hrSuccess;

	hr = GetProviders(&g_mapProviders, lpMAPISup, tstrProfileName.c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		return hr;
	hr = sProviderInfo.lpMSProviderOnline->QueryInterface(IID_IMSProvider, &~lpOnline);
	if (hr != hrSuccess)
		return hr;

	// Default error
	hr = MAPI_E_LOGON_FAILED; //or MAPI_E_FAILONEPROVIDER

	// Connect online if any of the following is true:
	// - Online store was specifically requested (MDB_ONLINE)
	// - Profile is not offline capable
	// - Store being opened is not the user's default store

	if ((ulFlags & MDB_ONLINE) ||
	    !(sProviderInfo.ulProfileFlags & EC_PROFILE_FLAGS_OFFLINE) ||
	    bIsDefaultStore == false) {
		bool fDone = false;

		while(!fDone) {
			hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags, lpInterface, nullptr, nullptr, nullptr, &~lpMSLogon, &~lpMDB);
			ulConnectType = CT_ONLINE;
			fDone = true;
		}
	}

	// Set the provider in the right connection type
	if (bIsDefaultStore &&
	    SetProviderMode(lpMAPISup, &g_mapProviders, tstrProfileName.c_str(), ulConnectType) != hrSuccess)
		return MAPI_E_INVALID_PARAMETER;

	if(hr != hrSuccess) {
		if (hr == MAPI_E_NETWORK_ERROR)
			return MAPI_E_FAILONEPROVIDER; //for disable public folders, so you can work offline
		else if (hr == MAPI_E_LOGON_FAILED)
			return MAPI_E_UNCONFIGURED; // Linux error ??//
		else
			return MAPI_E_LOGON_FAILED;
	}

	hr = lpMDB->QueryInterface(IID_ECMsgStore, &~lpecMDB);
	if (hr != hrSuccess)
		return hr;

	// Register ourselves with mapisupport
	hr = lpMAPISup->SetProviderUID((MAPIUID *)&lpecMDB->GetStoreGuid(), 0);
	if (hr != hrSuccess)
		return hr;

	// Set profile identity
	hr = ClientUtil::HrSetIdentity(lpecMDB->lpTransport, lpMAPISup, &~lpIdentityProps);
	if (hr != hrSuccess)
		return hr;

	// Get store name
	// The server will return MAPI_E_UNCONFIGURED when an attempt is made to open a store
	// that does not exist on that server. However the store is only opened the first time
	// when it's actually needed.
	// Since this is the first call that actually needs information from the store, we need
	// to be prepared to handle the MAPI_E_UNCONFIGURED error as we want to propagate this
	// up to the caller so this 'error' can be resolved by reconfiguring the profile.
	hr = HrGetOneProp(lpMDB, PR_DISPLAY_NAME_A, &~lpProp);
	if (hr == MAPI_E_UNCONFIGURED)
		return MAPI_E_UNCONFIGURED;
	if (hr != hrSuccess || lpProp->ulPropTag != PR_DISPLAY_NAME_A) {
		lpDisplayName = KC_A("Unknown");
		hr = hrSuccess;
	} else {
		lpDisplayName = lpProp->Value.lpszA;
	}

	if (CompareMDBProvider(&lpecMDB->m_guidMDB_Provider, &KOPANO_SERVICE_GUID) ||
		CompareMDBProvider(&lpecMDB->m_guidMDB_Provider, &KOPANO_STORE_DELEGATE_GUID))
	{
		hr = ClientUtil::HrInitializeStatusRow(lpDisplayName, MAPI_STORE_PROVIDER, lpMAPISup, lpIdentityProps, 0);
		if (hr != hrSuccess)
			return hr;
	}

	if (lppMSLogon) {
		hr = lpMSLogon->QueryInterface(IID_IMSLogon, (void **)lppMSLogon);
		if (hr != hrSuccess)
			return hr;
	}
	
	if (lppMDB) {
		hr = lpMDB->QueryInterface(IID_IMsgStore, (void **)lppMDB);
		if (hr != hrSuccess)
			return hr;
	}

	// Store username and password so SpoolerLogon can log on to the profile
	if(lppbSpoolSecurity)
	{
		ULONG cbSpoolSecurity = sizeof(wchar_t) * (sProfileProps.strUserName.length() + sProfileProps.strPassword.length() + 1 + 1);

		hr = MAPIAllocateBuffer(cbSpoolSecurity, (void **)lppbSpoolSecurity);
		if(hr != hrSuccess)
			return hr;

		swprintf((wchar_t*)*lppbSpoolSecurity, cbSpoolSecurity, L"%s%c%s", sProfileProps.strUserName.c_str(), 0, sProfileProps.strPassword.c_str());
		*lpcbSpoolSecurity = cbSpoolSecurity;
	}

	return hr;
}

HRESULT ECMSProviderSwitch::SpoolerLogon(LPMAPISUP lpMAPISup,
    ULONG_PTR ulUIParam, const TCHAR *lpszProfileName, ULONG cbEntryID,
    LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface,
    ULONG cbSpoolSecurity, LPBYTE lpbSpoolSecurity, LPMAPIERROR *lppMAPIError,
    LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	IMSProvider *lpProvider = NULL; // Do not release
	PROVIDER_INFO sProviderInfo;
	object_ptr<IMsgStore> lpMDB;
	object_ptr<IMSLogon> lpMSLogon;
	object_ptr<ECMsgStore> lpecMDB;
	auto laters = make_scope_success([&]() {
		if (lppMAPIError != nullptr)
			*lppMAPIError = nullptr;
	});

	if (lpEntryID == NULL)
		return MAPI_E_UNCONFIGURED;

	if (cbSpoolSecurity == 0 || lpbSpoolSecurity == NULL)
		return MAPI_E_NO_ACCESS;
	auto hr = GetProviders(&g_mapProviders, lpMAPISup, convstring(lpszProfileName, ulFlags).c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		return hr;

	lpProvider = sProviderInfo.lpMSProviderOnline;
	hr = lpProvider->SpoolerLogon(lpMAPISup, ulUIParam, lpszProfileName,
	     cbEntryID, lpEntryID, ulFlags, lpInterface, cbSpoolSecurity,
	     lpbSpoolSecurity, nullptr, &~lpMSLogon, &~lpMDB);
	if (hr != hrSuccess)
		return hr;
	hr = lpMDB->QueryInterface(IID_ECMsgStore, &~lpecMDB);
	if (hr != hrSuccess)
		return hr;

	// Register ourselves with mapisupport
	hr = lpMAPISup->SetProviderUID((MAPIUID *)&lpecMDB->GetStoreGuid(), 0);
	if (hr != hrSuccess)
		return hr;

	if (lppMSLogon) {
		hr = lpMSLogon->QueryInterface(IID_IMSLogon, (void **)lppMSLogon);
		if (hr != hrSuccess)
			return hr;
	}
	
	if (lppMDB) {
		hr = lpMDB->QueryInterface(IID_IMsgStore, (void **)lppMDB);
		if (hr != hrSuccess)
			return hr;
	}

	return hr;
}

HRESULT ECMSProviderSwitch::CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	return ::CompareStoreIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
}
