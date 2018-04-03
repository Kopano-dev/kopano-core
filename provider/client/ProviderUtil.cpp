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
#include <utility>
#include <kopano/ECGetText.h>
#include <kopano/memory.hpp>
#include <mapi.h>
#include <mapiutil.h>
#include <mapispi.h>

#include "ClientUtil.h"
#include "Mem.h"
#include <kopano/stringutil.h>

#include <kopano/ECGuid.h>

#include "ECABProvider.h"
#include "ECMSProvider.h"
#include "ECMsgStore.h"
#include "ECArchiveAwareMsgStore.h"
#include "ECMsgStorePublic.h"
#include <kopano/charset/convstring.h>
#include "EntryPoint.h"
#include "ProviderUtil.h"

#include <kopano/charset/convert.h>

using namespace KC;

HRESULT CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	HRESULT hr = hrSuccess;
	BOOL fTheSame = FALSE;
	PEID peid1 = (PEID)lpEntryID1;
	PEID peid2 = (PEID)lpEntryID2;

	if(lpEntryID1 == NULL || lpEntryID2 == NULL || lpulResult == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (cbEntryID1 < (sizeof(GUID) + 4 + 4) || cbEntryID2 < (sizeof(GUID) + 4 + 4)) {
		hr = MAPI_E_INVALID_ENTRYID;
		goto exit;
	}

	if(memcmp(&peid1->guid, &peid2->guid, sizeof(GUID)) != 0)
		goto exit;

	if(peid1->ulVersion != peid2->ulVersion)
		goto exit;

	if(peid1->usType != peid2->usType)
		goto exit;

	if(peid1->ulVersion == 0) {

		if(cbEntryID1 < sizeof(EID_V0))
			goto exit;

		if( ((EID_V0*)lpEntryID1)->ulId != ((EID_V0*)lpEntryID2)->ulId )
			goto exit;

	}else {
		if(cbEntryID1 < CbNewEID(""))
			goto exit;

		if(peid1->uniqueId != peid2->uniqueId) //comp. with the old ulId
			goto exit;
	}

	fTheSame = TRUE;

exit:
	if(lpulResult)
		*lpulResult = fTheSame;

	return hr;
}

HRESULT SetProviderMode(IMAPISupport *lpMAPISup, ECMapProvider* lpmapProvider, LPCSTR lpszProfileName, ULONG ulConnectType)
{
	return hrSuccess;
}

HRESULT GetProviders(ECMapProvider* lpmapProvider, IMAPISupport *lpMAPISup, const char *lpszProfileName, ULONG ulFlags, PROVIDER_INFO* lpsProviderInfo)
{
	PROVIDER_INFO sProviderInfo;
	object_ptr<ECMSProvider> lpECMSProvider;
	object_ptr<ECABProvider> lpECABProvider;
	sGlobalProfileProps	sProfileProps;

	if (lpmapProvider == nullptr || lpMAPISup == nullptr ||
	    lpszProfileName == nullptr || lpsProviderInfo == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	auto iterProvider = lpmapProvider->find(lpszProfileName);
	if (iterProvider != lpmapProvider->cend()) {
		*lpsProviderInfo = iterProvider->second;
		return hrSuccess;
	}
		
	// Get the username and password from the profile settings
	auto hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if(hr != hrSuccess)
		return hr;

	// Init providers

	// Message store online
	hr = ECMSProvider::Create(ulFlags, &~lpECMSProvider);
	if(hr != hrSuccess)
		return hr;

	// Addressbook online
	hr = ECABProvider::Create(&~lpECABProvider);
	if(hr != hrSuccess)
		return hr;

	// Fill in the Provider info struct
	
	//Init only the firsttime the flags
	sProviderInfo.ulProfileFlags = sProfileProps.ulProfileFlags;
	sProviderInfo.ulConnectType = CT_ONLINE;
	hr = lpECMSProvider->QueryInterface(IID_IMSProvider, &~sProviderInfo.lpMSProviderOnline);
	if(hr != hrSuccess)
		return hr;
	hr = lpECABProvider->QueryInterface(IID_IABProvider, &~sProviderInfo.lpABProviderOnline);
	if(hr != hrSuccess)
		return hr;
	lpmapProvider->insert({lpszProfileName, sProviderInfo});
	*lpsProviderInfo = std::move(sProviderInfo);
	return hrSuccess;
}

// Create an anonymous message store, linked to transport and support object
//
// NOTE
//  Outlook will stay 'alive' when the user shuts down until the support
//  object is released, so we have to make sure that when the users has released
//  all the msgstore objects, we also release the support object.
//
HRESULT CreateMsgStoreObject(const char *lpszProfname, IMAPISupport *lpMAPISup,
    ULONG cbEntryID, ENTRYID *lpEntryID, ULONG ulMsgFlags, ULONG ulProfileFlags,
    WSTransport *lpTransport, const MAPIUID *lpguidMDBProvider, BOOL bSpooler,
    BOOL fIsDefaultStore, BOOL bOfflineStore, ECMsgStore **lppECMsgStore)
{
	HRESULT	hr = hrSuccess;
	object_ptr<ECMsgStore> lpMsgStore;
	object_ptr<IECPropStorage> lpStorage;
	BOOL fModify = ulMsgFlags & MDB_WRITE || ulMsgFlags & MAPI_BEST_ACCESS; // FIXME check access at server

	if (CompareMDBProvider(lpguidMDBProvider, &KOPANO_STORE_PUBLIC_GUID) == TRUE)
		hr = ECMsgStorePublic::Create(lpszProfname, lpMAPISup, lpTransport, fModify, ulProfileFlags, bSpooler, bOfflineStore, &~lpMsgStore);
	else if (CompareMDBProvider(lpguidMDBProvider, &KOPANO_STORE_ARCHIVE_GUID) == TRUE)
		hr = ECMsgStore::Create(lpszProfname, lpMAPISup, lpTransport, fModify, ulProfileFlags, bSpooler, FALSE, bOfflineStore, &~lpMsgStore);
	else
		hr = ECArchiveAwareMsgStore::Create(lpszProfname, lpMAPISup, lpTransport, fModify, ulProfileFlags, bSpooler, fIsDefaultStore, bOfflineStore, &~lpMsgStore);

	if (hr != hrSuccess)
		return hr;

	memcpy(&lpMsgStore->m_guidMDB_Provider, lpguidMDBProvider,sizeof(MAPIUID));

	// Get a propstorage for the message store
	hr = lpTransport->HrOpenPropStorage(0, nullptr, cbEntryID, lpEntryID, 0, &~lpStorage);
	if (hr != hrSuccess)
		return hr;

	// Set up the message store to use this storage
	hr = lpMsgStore->HrSetPropStorage(lpStorage, FALSE);
	if (hr != hrSuccess)
		return hr;

	// Setup callback for session change
	hr = lpTransport->AddSessionReloadCallback(lpMsgStore, ECMsgStore::Reload, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = lpMsgStore->SetEntryId(cbEntryID, lpEntryID);
	if (hr != hrSuccess)
		return hr;
	return lpMsgStore->QueryInterface(IID_ECMsgStore,
	       reinterpret_cast<void **>(lppECMsgStore));
}

HRESULT GetTransportToNamedServer(WSTransport *lpTransport, LPCTSTR lpszServerName, ULONG ulFlags, WSTransport **lppTransport)
{
	utf8string strPseudoUrl = utf8string::from_string("pseudo://");
	char *lpszServerPath = NULL;
	bool bIsPeer = false;
	WSTransport *lpNewTransport = NULL;

	if (lpszServerName == NULL || lpTransport == NULL || lppTransport == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if ((ulFlags & ~MAPI_UNICODE) != 0)
		return MAPI_E_UNKNOWN_FLAGS;

	utf8string strServerName = convstring(lpszServerName, ulFlags);
	strPseudoUrl.append(strServerName);
	auto hr = lpTransport->HrResolvePseudoUrl(strPseudoUrl.c_str(), &lpszServerPath, &bIsPeer);
	if (hr != hrSuccess)
		return hr;

	if (bIsPeer) {
		lpNewTransport = lpTransport;
		lpNewTransport->AddRef();
	} else {
		hr = lpTransport->CreateAndLogonAlternate(lpszServerPath, &lpNewTransport);
		if (hr != hrSuccess)
			return hr;
	}

	*lppTransport = lpNewTransport;
	return hrSuccess;
}
