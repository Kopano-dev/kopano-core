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
#include <kopano/memory.hpp>
#include "ECArchiveAwareMsgStore.h"
#include "ECArchiveAwareMessage.h"
#include <kopano/ECGuid.h>
#include <kopano/mapi_ptr.h>

using namespace KCHL;

ECArchiveAwareMsgStore::ECArchiveAwareMsgStore(char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore)
: ECMsgStore(lpszProfname, lpSupport, lpTransport, fModify, ulProfileFlags, fIsSpooler, fIsDefaultStore, bOfflineStore)
{ }

HRESULT ECArchiveAwareMsgStore::Create(char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore, ECMsgStore **lppECMsgStore)
{
	HRESULT hr = hrSuccess;
	auto lpStore = new ECArchiveAwareMsgStore(lpszProfname, lpSupport,
	               lpTransport, fModify, ulProfileFlags, fIsSpooler,
	               fIsDefaultStore, bOfflineStore);
	hr = lpStore->QueryInterface(IID_ECMsgStore, (void **)lppECMsgStore);

	if(hr != hrSuccess)
		delete lpStore;

	return hr;
}

HRESULT ECArchiveAwareMsgStore::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
{
	// By default we'll try to open an archive aware message when a message is opened. The exception
	// is when the client is not licensed to do so or when it's explicitly disabled by passing 
	// IID_IECMessageRaw as the lpInterface parameter. This is for instance needed for the archiver
	// itself becaus it needs to operate on the non-stubbed (or raw) message.
	// In this override, we only check for the presence of IID_IECMessageRaw. If that's found, we'll
	// pass an ECMessageFactory instance to our parents OpenEntry.
	// Otherwise we'll pass an ECArchiveAwareMessageFactory instance, which will check the license
	// create the appropriate message type. If the object turns out to be a message that is.

	const bool bRawMessage = (lpInterface && memcmp(lpInterface, &IID_IECMessageRaw, sizeof(IID)) == 0);
	HRESULT hr = hrSuccess;

	if (bRawMessage)
		hr = ECMsgStore::OpenEntry(cbEntryID, lpEntryID, &IID_IMessage, ulFlags, ECMessageFactory(), lpulObjType, lppUnk);
	else
		hr = ECMsgStore::OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, ECArchiveAwareMessageFactory(), lpulObjType, lppUnk);

	return hr;
}

HRESULT ECArchiveAwareMsgStore::OpenItemFromArchive(LPSPropValue lpPropStoreEIDs, LPSPropValue lpPropItemEIDs, ECMessage **lppMessage)
{
	HRESULT hr;
	BinaryList			lstStoreEIDs;
	BinaryList			lstItemEIDs;
	BinaryListIterator	iterStoreEID;
	BinaryListIterator	iterIterEID;
	object_ptr<ECMessage, IID_ECMessage> ptrArchiveMessage;

	if (lpPropStoreEIDs == NULL || 
		lpPropItemEIDs == NULL || 
		lppMessage == NULL || 
		PROP_TYPE(lpPropStoreEIDs->ulPropTag) != PT_MV_BINARY ||
		PROP_TYPE(lpPropItemEIDs->ulPropTag) != PT_MV_BINARY ||
		lpPropStoreEIDs->Value.MVbin.cValues != lpPropItemEIDs->Value.MVbin.cValues)
		return MAPI_E_INVALID_PARAMETER;

	// First get a list of items that could be retrieved from cached archive stores.
	hr = CreateCacheBasedReorderedList(lpPropStoreEIDs->Value.MVbin, lpPropItemEIDs->Value.MVbin, &lstStoreEIDs, &lstItemEIDs);
	if (hr != hrSuccess)
		return hr;

	iterStoreEID = lstStoreEIDs.begin();
	iterIterEID = lstItemEIDs.begin();
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
	BinaryList lstStoreEIDs;
	BinaryList lstItemEIDs;

	BinaryList lstUncachedStoreEIDs;
	BinaryList lstUncachedItemEIDs;

	for (ULONG i = 0; i < sbaStoreEIDs.cValues; ++i) {
		const std::vector<BYTE> eid(sbaStoreEIDs.lpbin[i].lpb, sbaStoreEIDs.lpbin[i].lpb + sbaStoreEIDs.lpbin[i].cb);
		if (m_mapStores.find(eid) != m_mapStores.end()) {
			lstStoreEIDs.push_back(sbaStoreEIDs.lpbin + i);
			lstItemEIDs.push_back(sbaItemEIDs.lpbin + i);
		} else {
			lstUncachedStoreEIDs.push_back(sbaStoreEIDs.lpbin + i);
			lstUncachedItemEIDs.push_back(sbaItemEIDs.lpbin + i);
		}
	}

	lstStoreEIDs.splice(lstStoreEIDs.end(), lstUncachedStoreEIDs);
	lstItemEIDs.splice(lstItemEIDs.end(), lstUncachedItemEIDs);

	lplstStoreEIDs->swap(lstStoreEIDs);
	lplstItemEIDs->swap(lstItemEIDs);

	return hrSuccess;
}

HRESULT ECArchiveAwareMsgStore::GetArchiveStore(LPSBinary lpStoreEID, ECMsgStore **lppArchiveStore)
{
	HRESULT hr;

	const std::vector<BYTE> eid(lpStoreEID->lpb, lpStoreEID->lpb + lpStoreEID->cb);
	MsgStoreMap::const_iterator iterStore = m_mapStores.find(eid);
	if (iterStore != m_mapStores.cend())
		return iterStore->second->QueryInterface(IID_ECMsgStore, (LPVOID*)lppArchiveStore);
	
	// @todo: Consolidate this with ECMSProvider::LogonByEntryID
	UnknownPtr ptrUnknown;
	ECMsgStorePtr ptrOnlineStore;
	ULONG cbEntryID = 0;
	EntryIdPtr ptrEntryID;
	std::string ServerURL;
	bool bIsPseudoUrl = false;
	std::string strServer;
	bool bIsPeer = false;
	object_ptr<WSTransport> ptrTransport;
	ECMsgStorePtr ptrArchiveStore;
	object_ptr<IECPropStorage, IID_IECPropStorage> ptrPropStorage;

	hr = QueryInterface(IID_ECMsgStoreOnline, &~ptrUnknown);
	if (hr != hrSuccess)
		return hr;
	hr = ptrUnknown->QueryInterface(IID_ECMsgStore, &~ptrOnlineStore);
	if (hr != hrSuccess)
		return hr;
	hr = UnWrapStoreEntryID(lpStoreEID->cb, (LPENTRYID)lpStoreEID->lpb, &cbEntryID, &~ptrEntryID);
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

	hr = ECMsgStore::Create(const_cast<char *>(GetProfileName()), this->lpSupport, ptrTransport, FALSE, 0, FALSE, FALSE, FALSE, &~ptrArchiveStore);
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
	m_mapStores.insert(MsgStoreMap::value_type(eid, ptrArchiveStore));
	return hrSuccess;
}
