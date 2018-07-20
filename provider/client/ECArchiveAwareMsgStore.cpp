/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <utility>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "ECArchiveAwareMsgStore.h"
#include "ECArchiveAwareMessage.h"
#include <kopano/ECGuid.h>
#include <kopano/mapi_ptr.h>

using namespace KC;

ECArchiveAwareMsgStore::ECArchiveAwareMsgStore(const char *lpszProfname,
    IMAPISupport *sup, WSTransport *tp, BOOL modify, ULONG ulProfileFlags,
    BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore) :
	ECMsgStore(lpszProfname, sup, tp, modify, ulProfileFlags, fIsSpooler,
	    fIsDefaultStore, bOfflineStore)
{ }

HRESULT ECArchiveAwareMsgStore::Create(const char *lpszProfname,
    IMAPISupport *lpSupport, WSTransport *lpTransport, BOOL fModify,
    ULONG ulProfileFlags, BOOL fIsSpooler, BOOL fIsDefaultStore,
    BOOL bOfflineStore, ECMsgStore **lppECMsgStore)
{
	return alloc_wrap<ECArchiveAwareMsgStore>(lpszProfname, lpSupport,
	       lpTransport, fModify, ulProfileFlags, fIsSpooler, fIsDefaultStore,
	       bOfflineStore).as(IID_ECMsgStore, lppECMsgStore);
}

HRESULT ECArchiveAwareMsgStore::OpenEntry(ULONG cbEntryID,
    const ENTRYID *lpEntryID, const IID *lpInterface, ULONG ulFlags,
    ULONG *lpulObjType, IUnknown **lppUnk)
{
	// By default we'll try to open an archive aware message when a message is opened. The exception
	// is when the client is not licensed to do so or when it's explicitly disabled by passing 
	// IID_IECMessageRaw as the lpInterface parameter. This is for instance needed for the archiver
	// itself becaus it needs to operate on the non-stubbed (or raw) message.
	// In this override, we only check for the presence of IID_IECMessageRaw. If that's found, we'll
	// pass an ECMessageFactory instance to our parents OpenEntry.
	// Otherwise we'll pass an ECArchiveAwareMessageFactory instance, which will check the license
	// create the appropriate message type. If the object turns out to be a message that is.
	if (lpInterface != nullptr && memcmp(lpInterface, &IID_IECMessageRaw, sizeof(IID)) == 0)
		return ECMsgStore::OpenEntry(cbEntryID, lpEntryID, &IID_IMessage, ulFlags, ECMessageFactory(), lpulObjType, lppUnk);
	return ECMsgStore::OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, ECArchiveAwareMessageFactory(), lpulObjType, lppUnk);
}

HRESULT ECArchiveAwareMsgStore::OpenItemFromArchive(LPSPropValue lpPropStoreEIDs, LPSPropValue lpPropItemEIDs, ECMessage **lppMessage)
{
	if (lpPropStoreEIDs == nullptr || lpPropItemEIDs == nullptr ||
	    lppMessage == nullptr ||
	    PROP_TYPE(lpPropStoreEIDs->ulPropTag) != PT_MV_BINARY ||
	    PROP_TYPE(lpPropItemEIDs->ulPropTag) != PT_MV_BINARY ||
	    lpPropStoreEIDs->Value.MVbin.cValues != lpPropItemEIDs->Value.MVbin.cValues)
		return MAPI_E_INVALID_PARAMETER;

	BinaryList lstStoreEIDs, lstItemEIDs;
	object_ptr<ECMessage> ptrArchiveMessage;
	// First get a list of items that could be retrieved from cached archive stores.
	auto hr = CreateCacheBasedReorderedList(lpPropStoreEIDs->Value.MVbin, lpPropItemEIDs->Value.MVbin, &lstStoreEIDs, &lstItemEIDs);
	if (hr != hrSuccess)
		return hr;

	auto iterStoreEID = lstStoreEIDs.begin();
	auto iterIterEID = lstItemEIDs.begin();
	for (; iterStoreEID != lstStoreEIDs.end(); ++iterStoreEID, ++iterIterEID) {
		ECMsgStorePtr	ptrArchiveStore;
		ULONG			ulType = 0;

		hr = GetArchiveStore(*iterStoreEID, &~ptrArchiveStore);
		if (hr == MAPI_E_NO_SUPPORT)
			return hr;	// No need to try any other archives.
		if (hr != hrSuccess)
			continue;
		hr = ptrArchiveStore->OpenEntry((*iterIterEID)->cb, reinterpret_cast<ENTRYID *>((*iterIterEID)->lpb), &IID_ECMessage, 0, &ulType, &~ptrArchiveMessage);
		if (hr != hrSuccess)
			continue;
		break;
	}

	if (iterStoreEID == lstStoreEIDs.end())
		return MAPI_E_NOT_FOUND;
	if (ptrArchiveMessage)
		hr = ptrArchiveMessage->QueryInterface(IID_ECMessage, (LPVOID*)lppMessage);
	return hr;
}

HRESULT ECArchiveAwareMsgStore::CreateCacheBasedReorderedList(SBinaryArray sbaStoreEIDs, SBinaryArray sbaItemEIDs, BinaryList *lplstStoreEIDs, BinaryList *lplstItemEIDs)
{
	BinaryList lstStoreEIDs, lstItemEIDs;
	BinaryList lstUncachedStoreEIDs, lstUncachedItemEIDs;

	for (ULONG i = 0; i < sbaStoreEIDs.cValues; ++i) {
		const std::vector<BYTE> eid(sbaStoreEIDs.lpbin[i].lpb, sbaStoreEIDs.lpbin[i].lpb + sbaStoreEIDs.lpbin[i].cb);
		if (m_mapStores.find(eid) != m_mapStores.end()) {
			lstStoreEIDs.emplace_back(sbaStoreEIDs.lpbin + i);
			lstItemEIDs.emplace_back(sbaItemEIDs.lpbin + i);
		} else {
			lstUncachedStoreEIDs.emplace_back(sbaStoreEIDs.lpbin + i);
			lstUncachedItemEIDs.emplace_back(sbaItemEIDs.lpbin + i);
		}
	}

	lstStoreEIDs.splice(lstStoreEIDs.end(), lstUncachedStoreEIDs);
	lstItemEIDs.splice(lstItemEIDs.end(), lstUncachedItemEIDs);
	*lplstStoreEIDs = std::move(lstStoreEIDs);
	*lplstItemEIDs = std::move(lstItemEIDs);
	return hrSuccess;
}

HRESULT ECArchiveAwareMsgStore::GetArchiveStore(LPSBinary lpStoreEID, ECMsgStore **lppArchiveStore)
{
	const std::vector<BYTE> eid(lpStoreEID->lpb, lpStoreEID->lpb + lpStoreEID->cb);
	MsgStoreMap::const_iterator iterStore = m_mapStores.find(eid);
	if (iterStore != m_mapStores.cend())
		return iterStore->second->QueryInterface(IID_ECMsgStore, (LPVOID*)lppArchiveStore);
	
	// @todo: Consolidate this with ECMSProvider::LogonByEntryID
	object_ptr<IMsgStore> ptrUnknown;
	ECMsgStorePtr ptrOnlineStore;
	ULONG cbEntryID = 0;
	EntryIdPtr ptrEntryID;
	std::string ServerURL, strServer;
	bool bIsPseudoUrl = false, bIsPeer = false;
	object_ptr<WSTransport> ptrTransport;
	ECMsgStorePtr ptrArchiveStore;
	object_ptr<IECPropStorage> ptrPropStorage;

	auto hr = QueryInterface(IID_ECMsgStoreOnline, &~ptrUnknown);
	if (hr != hrSuccess)
		return hr;
	hr = ptrUnknown->QueryInterface(IID_ECMsgStore, &~ptrOnlineStore);
	if (hr != hrSuccess)
		return hr;
	hr = UnWrapStoreEntryID(lpStoreEID->cb, reinterpret_cast<ENTRYID *>(lpStoreEID->lpb), &cbEntryID, &~ptrEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetServerURLFromStoreEntryId(cbEntryID, ptrEntryID, ServerURL, &bIsPseudoUrl);
	if (hr != hrSuccess)
		return hr;

	if (bIsPseudoUrl) {
		hr = HrResolvePseudoUrl(ptrOnlineStore->lpTransport, ServerURL.c_str(), strServer, &bIsPeer);
		if (hr != hrSuccess)
			return hr;
		if (!bIsPeer)
			ServerURL = strServer;
		else {
			// We can't just use the transport from ptrOnlineStore as that will be
			// logged off when ptrOnlineStore gets destroyed (at the end of this finction).
			hr = ptrOnlineStore->lpTransport->CloneAndRelogon(&~ptrTransport);
			if (hr != hrSuccess)
				return hr;
		}
	}

	if (!ptrTransport) {
		// We get here if lpszServer wasn't a pseudo URL or if it was and it resolved
		// to another server than the one we're connected with.
		hr = ptrOnlineStore->lpTransport->CreateAndLogonAlternate(ServerURL.c_str(), &~ptrTransport);
		if (hr != hrSuccess)
			return hr;
	}

	hr = ECMsgStore::Create(const_cast<char *>(GetProfileName()), lpSupport,
	     ptrTransport, false, 0, false, false, false, &~ptrArchiveStore);
	if (hr != hrSuccess)
		return hr;
	// Get a propstorage for the message store
	hr = ptrTransport->HrOpenPropStorage(0, nullptr, cbEntryID, ptrEntryID, 0, &~ptrPropStorage);
	if (hr != hrSuccess)
		return hr;
	// Set up the message store to use this storage
	hr = ptrArchiveStore->HrSetPropStorage(ptrPropStorage, FALSE);
	if (hr != hrSuccess)
		return hr;
	// Setup callback for session change
	hr = ptrTransport->AddSessionReloadCallback(ptrArchiveStore, ECMsgStore::Reload, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveStore->SetEntryId(cbEntryID, ptrEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveStore->QueryInterface(IID_ECMsgStore, (LPVOID*)lppArchiveStore);
	if (hr != hrSuccess)
		return hr;
	m_mapStores.emplace(eid, ptrArchiveStore);
	return hrSuccess;
}
