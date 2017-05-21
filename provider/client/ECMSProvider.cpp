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
#include <kopano/platform.h>
#include <mapi.h>
#include <mapiutil.h>
#include <mapispi.h>

#include <kopano/ECGetText.h>
#include <kopano/memory.hpp>
#include "Mem.h"

#include <kopano/ECGuid.h>
#include <kopano/ECInterfaceDefs.h>
#include "ECMSProvider.h"
#include "ECMsgStore.h"
#include "ECABProvider.h"

#include <kopano/ECDebug.h>

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

using namespace std;
using namespace KCHL;
typedef KCHL::memory_ptr<ECUSER> ECUserPtr;

ECMSProvider::ECMSProvider(ULONG ulFlags, const char *szClassName) :
	ECUnknown(szClassName), m_ulFlags(ulFlags)
{
}

ECMSProvider::~ECMSProvider()
{
}

HRESULT ECMSProvider::Create(ULONG ulFlags, ECMSProvider **lppECMSProvider) {
	auto lpECMSProvider = new(std::nothrow) ECMSProvider(ulFlags, "IMSProvider");
	if (lpECMSProvider == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto ret = lpECMSProvider->QueryInterface(IID_ECMSProvider,
	           reinterpret_cast<void **>(lppECMSProvider));
	if (ret != hrSuccess)
		delete lpECMSProvider;
	return ret;
}

HRESULT ECMSProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMSProvider, this);
	REGISTER_INTERFACE2(IMSProvider, &this->m_xMSProvider);
	REGISTER_INTERFACE3(ISelectUnicode, IUnknown, &this->m_xUnknown);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMSProvider::Shutdown(ULONG * lpulFlags) 
{
	return hrSuccess;
}

HRESULT ECMSProvider::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity, LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	HRESULT			hr = hrSuccess;
	object_ptr<WSTransport> lpTransport;
	object_ptr<ECMsgStore> lpECMsgStore;
	object_ptr<ECMSLogon> lpECMSLogon;
	object_ptr<IProfSect> lpProfSect;
	ULONG			cValues = 0;
	memory_ptr<SPropValue> lpsPropArray;
	BOOL			fIsDefaultStore = FALSE;
	ULONG			ulStoreType = 0;
	MAPIUID			guidMDBProvider;
	BOOL			bOfflineStore = FALSE;
	sGlobalProfileProps	sProfileProps;

	// Always suppress UI when running in a service
	if(m_ulFlags & MAPI_NT_SERVICE)
		ulFlags |= MDB_NO_DIALOG;

	// If the EntryID is not configured, return MAPI_E_UNCONFIGURED, this will
	// cause MAPI to call our configuration entry point (MSGServiceEntry)
	if (lpEntryID == nullptr)
		return MAPI_E_UNCONFIGURED;
	if(lpcbSpoolSecurity)
		*lpcbSpoolSecurity = 0;
	if(lppbSpoolSecurity)
		*lppbSpoolSecurity = NULL;

	// Get the username and password from the profile settings
	hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
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
	hr = LogonByEntryID(&+lpTransport, &sProfileProps, cbEntryID, lpEntryID);
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
	hr = CreateMsgStoreObject((LPSTR)sProfileProps.strProfileName.c_str(), lpMAPISup, cbEntryID, lpEntryID, ulFlags, sProfileProps.ulProfileFlags, lpTransport,
	     &guidMDBProvider, false, fIsDefaultStore, bOfflineStore, &~lpECMsgStore);
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

	// We don't count lpMSLogon as a child, because its lifetime is coupled to lpMsgStore
	if(lppMSLogon) {
		hr = ECMSLogon::Create(lpECMsgStore, &~lpECMSLogon);
		if(hr != hrSuccess)
			return hr;
		hr = lpECMSLogon->QueryInterface(IID_IMSLogon, (void **)lppMSLogon);
		
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

//FIXME: What todo with offline??
//TODO: online/offline state???
HRESULT ECMSProvider::SpoolerLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG cbSpoolSecurity, LPBYTE lpbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	HRESULT hr = hrSuccess;
	object_ptr<WSTransport> lpTransport;
	object_ptr<ECMsgStore> lpMsgStore;
	object_ptr<ECMSLogon> lpLogon;
	MAPIUID	guidMDBProvider;
	object_ptr<IProfSect> lpProfSect;
	ULONG cValues = 0;
	LPSPropValue lpsPropArray = NULL;
	bool bOfflineStore = false;
	sGlobalProfileProps	sProfileProps;
	wchar_t *strSep = NULL;

	if (lpEntryID == nullptr)
		return MAPI_E_UNCONFIGURED;
	if (cbSpoolSecurity == 0 || lpbSpoolSecurity == nullptr)
		return MAPI_E_NO_ACCESS;

	// Get Global profile settings
	hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
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
	strSep = (wchar_t*)wmemchr((wchar_t*)lpbSpoolSecurity, 0, cbSpoolSecurity / sizeof(wchar_t));
	if (strSep == NULL)
		return MAPI_E_NO_ACCESS;
	++strSep;
	sProfileProps.strUserName = (wchar_t*)lpbSpoolSecurity;
	sProfileProps.strPassword = strSep;

	// Create a transport for this message store
	hr = WSTransport::Create(ulFlags, &~lpTransport);
	if(hr != hrSuccess)
		return hr;
	hr = LogonByEntryID(&+lpTransport, &sProfileProps, cbEntryID, lpEntryID);
	if(hr != hrSuccess) {
		if (ulFlags & MDB_NO_DIALOG)
			return MAPI_E_FAILONEPROVIDER;
		return MAPI_E_UNCONFIGURED;
	}

	// Get a message store object
	hr = CreateMsgStoreObject((LPSTR)sProfileProps.strProfileName.c_str(), lpMAPISup, cbEntryID, lpEntryID, ulFlags, sProfileProps.ulProfileFlags, lpTransport,
	     &guidMDBProvider, true, true, bOfflineStore, &~lpMsgStore);
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

	if(lppMSLogon) {
		hr = ECMSLogon::Create(lpMsgStore, &~lpLogon);
		if(hr != hrSuccess)
			return hr;
		hr = lpLogon->QueryInterface(IID_IMSLogon, (void **)lppMSLogon);
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
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
HRESULT ECMSProvider::LogonByEntryID(WSTransport **lppTransport, sGlobalProfileProps *lpsProfileProps, ULONG cbEntryID, LPENTRYID lpEntryID)
{
	HRESULT hr;
	string		extractedServerPath;		// The extracted server path
	bool		bIsPseudoUrl = false;
	WSTransport	*lpTransport = NULL;

	assert(lppTransport != NULL && *lppTransport != NULL);
	lpTransport = *lppTransport;

	hr = HrGetServerURLFromStoreEntryId(cbEntryID, lpEntryID, extractedServerPath, &bIsPseudoUrl);
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
	} else {
		string strServerPath;				// The resolved server path
		bool bIsPeer;
		WSTransport *lpAltTransport = NULL;

		hr = lpTransport->HrLogon(*lpsProfileProps);
		if (hr != hrSuccess)
			return hr;

		hr = HrResolvePseudoUrl(lpTransport, extractedServerPath.c_str(), strServerPath, &bIsPeer);
		if (hr != hrSuccess)
			return hr;

		if (!bIsPeer) {
			hr = lpTransport->CreateAndLogonAlternate(strServerPath.c_str(), &lpAltTransport);
			if (hr != hrSuccess)
				return hr;

			lpTransport->HrLogOff();
			lpTransport->Release();
			*lppTransport = lpAltTransport;
		}
	}
	return hrSuccess;
}

DEF_ULONGMETHOD1(TRACE_MAPI, ECMSProvider, MSProvider, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMSProvider, MSProvider, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMSProvider, MSProvider, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD1(TRACE_MAPI, ECMSProvider, MSProvider, Shutdown, (ULONG *, lpulFlags))

/* has 12 args, no macro deals with it atm */
HRESULT ECMSProvider::xMSProvider::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity, LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	METHOD_PROLOGUE_(ECMSProvider, MSProvider);
	HRESULT hr = pThis->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID,ulFlags, lpInterface, lpcbSpoolSecurity, lppbSpoolSecurity, lppMAPIError, lppMSLogon, lppMDB);
	return hr;
}

HRESULT ECMSProvider::xMSProvider::SpoolerLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG lpcbSpoolSecurity, LPBYTE lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	METHOD_PROLOGUE_(ECMSProvider, MSProvider);
	HRESULT hr = pThis->SpoolerLogon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID,ulFlags, lpInterface, lpcbSpoolSecurity, lppbSpoolSecurity, lppMAPIError, lppMSLogon, lppMDB);
	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECMSProvider, MSProvider, CompareStoreIDs, (ULONG, cbEntryID1), (LPENTRYID, lpEntryID1), (ULONG, cbEntryID2), (LPENTRYID, lpEntryID2), (ULONG, ulFlags), (ULONG *, lpulResult))
