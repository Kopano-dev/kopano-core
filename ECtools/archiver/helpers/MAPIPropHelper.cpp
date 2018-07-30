/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <utility>
#include "MAPIPropHelper.h"
#include "ArchiverSession.h"
#include <kopano/archiver-common.h>
#include <mapiutil.h>
#include <kopano/Util.h>
#include <kopano/mapi_ptr.h>
#include <kopano/mapiguidext.h>

namespace KC { namespace helpers {

/**
 * Create a MAPIPropHelper object.
 *
 * @param[in]	ptrMapiProp
 *					The MAPIPropPtr that points to the IMAPIProp object for which to create
 *					a MAPIPropHelper.
 * @param[out]	lppptrMAPIPropHelper
 *					Pointer to a MAPIPropHelperPtr that will be assigned the returned
 *					MAPIPropHelper object.
 */
HRESULT MAPIPropHelper::Create(MAPIPropPtr ptrMapiProp, MAPIPropHelperPtr *lpptrMAPIPropHelper)
{
	HRESULT hr;
	MAPIPropHelperPtr ptrMAPIPropHelper(new(std::nothrow) MAPIPropHelper(ptrMapiProp));
	if (ptrMAPIPropHelper == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	hr = ptrMAPIPropHelper->Init();
	if (hr != hrSuccess)
		return hr;
	*lpptrMAPIPropHelper = std::move(ptrMAPIPropHelper);
	return hrSuccess;
}

MAPIPropHelper::MAPIPropHelper(MAPIPropPtr ptrMapiProp) :
    m_ptrMapiProp(ptrMapiProp), m_propmap(8)
{ }

/**
 * Initialize a MAPIPropHelper object.
 */
HRESULT MAPIPropHelper::Init()
{
	HRESULT	hr = hrSuccess;

	PROPMAP_INIT_NAMED_ID(ARCHIVE_STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidStoreEntryIds)
	PROPMAP_INIT_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidItemEntryIds)
	PROPMAP_INIT_NAMED_ID(ORIGINAL_SOURCEKEY, PT_BINARY, PSETID_Archive, dispidOrigSourceKey)
	PROPMAP_INIT_NAMED_ID(STUBBED, PT_BOOLEAN, PSETID_Archive, dispidStubbed)
	PROPMAP_INIT_NAMED_ID(DIRTY, PT_BOOLEAN, PSETID_Archive, dispidDirty)
	PROPMAP_INIT_NAMED_ID(REF_STORE_ENTRYID, PT_BINARY, PSETID_Archive, dispidRefStoreEntryId)
	PROPMAP_INIT_NAMED_ID(REF_ITEM_ENTRYID, PT_BINARY, PSETID_Archive, dispidRefItemEntryId)
	PROPMAP_INIT_NAMED_ID(REF_PREV_ENTRYID, PT_BINARY, PSETID_Archive, dispidRefPrevEntryId)
	PROPMAP_INIT(m_ptrMapiProp)
	return hr;
}

/**
 * Determine the state of the message. With this state one can determine if a
 * message is stubbed or dirty and copied or moved.
 *
 * @param[in]	ptrSession
 * 					The session needed to open the archive message(s) to determine
 * 					if a message was copied or moved.
 * @param[out]	lpState
 * 					The state that will be setup according to the message state.
 */
HRESULT MAPIPropHelper::GetMessageState(ArchiverSessionPtr ptrSession, MessageState *lpState)
{
	HRESULT hr;
	ULONG cMessageProps = 0;
	SPropArrayPtr ptrMessageProps;
	ULONG ulState = 0;
	int result = 0;

	SizedSPropTagArray(6, sptaMessageProps) = {6, {PR_ENTRYID, PROP_STUBBED, PROP_DIRTY, PR_SOURCE_KEY, PROP_ORIGINAL_SOURCEKEY, PR_EC_HIERARCHYID}};
	enum {IDX_ENTRYID, IDX_STUBBED, IDX_DIRTY, IDX_SOURCE_KEY, IDX_ORIGINAL_SOURCEKEY, IDX_HIERARCHYID};

	if (lpState == NULL)
		return MAPI_E_INVALID_PARAMETER;
	hr = m_ptrMapiProp->GetProps(sptaMessageProps, 0, &cMessageProps, &~ptrMessageProps);
	if (FAILED(hr))
		return hr;
	if (PROP_TYPE(ptrMessageProps[IDX_ENTRYID].ulPropTag) == PT_ERROR)
		return ptrMessageProps[IDX_ENTRYID].Value.err;
	if (PROP_TYPE(ptrMessageProps[IDX_SOURCE_KEY].ulPropTag) == PT_ERROR)
		return ptrMessageProps[IDX_SOURCE_KEY].Value.err;
	if (PROP_TYPE(ptrMessageProps[IDX_STUBBED].ulPropTag) == PT_ERROR &&
	    ptrMessageProps[IDX_STUBBED].Value.err != MAPI_E_NOT_FOUND)
		return ptrMessageProps[IDX_STUBBED].Value.err;
	if (PROP_TYPE(ptrMessageProps[IDX_DIRTY].ulPropTag) == PT_ERROR &&
	    ptrMessageProps[IDX_DIRTY].Value.err != MAPI_E_NOT_FOUND)
		return ptrMessageProps[IDX_DIRTY].Value.err;
	if (PROP_TYPE(ptrMessageProps[IDX_ORIGINAL_SOURCEKEY].ulPropTag) == PT_ERROR &&
	    ptrMessageProps[IDX_ORIGINAL_SOURCEKEY].Value.err != MAPI_E_NOT_FOUND)
		return ptrMessageProps[IDX_ORIGINAL_SOURCEKEY].Value.err;
	if (PROP_TYPE(ptrMessageProps[IDX_HIERARCHYID].ulPropTag) == PT_ERROR)
		return ptrMessageProps[IDX_HIERARCHYID].Value.err;
	hr = hrSuccess;

	// Determine stubbed / dirty state.
	if (PROP_TYPE(ptrMessageProps[IDX_STUBBED].ulPropTag) != PT_ERROR && ptrMessageProps[IDX_STUBBED].Value.b == TRUE)
		ulState |= MessageState::msStubbed;

	if (PROP_TYPE(ptrMessageProps[IDX_DIRTY].ulPropTag) != PT_ERROR &&
	    ptrMessageProps[IDX_DIRTY].Value.b == TRUE &&
	    !(ulState & MessageState::msStubbed))
		// If, for some reason, both dirty and stubbed are set, it is safest to mark the message
		// as stubbed. That might cause the archive to miss out some changes, but if we marked
		// it as dirty, we might be rearchiving a stub, loosing all interesting information.
		ulState |= MessageState::msDirty;

	// Determine copy / move state.
	if (PROP_TYPE(ptrMessageProps[IDX_ORIGINAL_SOURCEKEY].ulPropTag) == PT_ERROR) {
		assert(ptrMessageProps[IDX_ORIGINAL_SOURCEKEY].Value.err == MAPI_E_NOT_FOUND);
		// No way to determine of message was copied/moved, so assume it's not.
		return hr;
	}

	hr = Util::CompareProp(&ptrMessageProps[IDX_SOURCE_KEY], &ptrMessageProps[IDX_ORIGINAL_SOURCEKEY], createLocaleFromName(""), &result);
	if (hr != hrSuccess)
		return hr;

	if (result != 0) {
		// The message is copied. Now check if it was moved.
		ObjectEntryList lstArchives;
		ULONG ulType;
		MessagePtr ptrArchiveMsg;
		MAPIPropHelperPtr ptrArchiveHelper;
		SObjectEntry refEntry;
		MsgStorePtr ptrStore;
		MessagePtr ptrMessage;

		hr = GetArchiveList(&lstArchives, true);
		if (hr != hrSuccess)
			return hr;

		for (const auto &arc : lstArchives) {
			HRESULT hrTmp;
			MsgStorePtr ptrArchiveStore;

			hrTmp = ptrSession->OpenReadOnlyStore(arc.sStoreEntryId, &~ptrArchiveStore);
			if (hrTmp != hrSuccess)
				continue;
			hrTmp = ptrArchiveStore->OpenEntry(arc.sItemEntryId.size(), arc.sItemEntryId, &iid_of(ptrArchiveMsg), 0, &ulType, &~ptrArchiveMsg);
			if (hrTmp != hrSuccess)
				continue;
			break;
		}

		if (!ptrArchiveMsg) {
			if (ulState & MessageState::msStubbed)
				return MAPI_E_NOT_FOUND;
			else
				/*
				 * We were unable to open any archived message, but the message is
				 * not stubbed anyway. Just mark it as a copy.
				 */
				ulState |= MessageState::msCopy;
		} else {
			hr = MAPIPropHelper::Create(ptrArchiveMsg.as<MAPIPropPtr>(), &ptrArchiveHelper);
			if (hr != hrSuccess)
				return hr;
			hr = ptrArchiveHelper->GetReference(&refEntry);
			if (hr != hrSuccess)
				return hr;
			hr = ptrSession->OpenReadOnlyStore(refEntry.sStoreEntryId, &~ptrStore);
			if (hr != hrSuccess)
				return hr;

			hr = ptrStore->OpenEntry(refEntry.sItemEntryId.size(), refEntry.sItemEntryId, &iid_of(ptrArchiveMsg), 0, &ulType, &~ptrMessage);
			if (hr == hrSuccess) {
				/*
				 * One would expect that if the message was opened properly here, the message that's being
				 * processed was copied because we were able to open the original reference, which should
				 * have been removed either way.
				 * However, because of a currently (13-07-2011) unknown issue, the moved message can be
				 * opened with its old entryid. This is probably a cache issue.
				 * If this happens, the message just opened is the same message as the one that's being
				 * processed. That can be easily verified by comparing the record key.
				 */
				SPropValuePtr ptrRecordKey;

				hr = HrGetOneProp(ptrMessage, PR_EC_HIERARCHYID, &~ptrRecordKey);
				if (hr != hrSuccess)
					return hr;

				if (ptrMessageProps[IDX_HIERARCHYID].Value.ul == ptrRecordKey->Value.ul) {
					// We opened the same message through the reference, which shouldn't be possible. This
					// must have been a move operation.
					ulState |= MessageState::msMove;
				} else
					ulState |= MessageState::msCopy;
			} else if (hr == MAPI_E_NOT_FOUND) {
				hr = hrSuccess;
				ulState |= MessageState::msMove;
			} else
				return hr;
		}
	}

	lpState->m_ulState = ulState;
	return hr;
}

/**
 * Get the list of archives for the object.
 * This has a different meaning for different objects:
 * Message store: A list of folders that are the root folders of the attached archives.
 * Folders: A list of folders that are the corresponding folders in the attached archives.
 * Messages: A list of messages that are archived versions of the current message.
 *
 * @param[out]	lplstArchives
 *					Pointer to a list that will be populated with the archive references.
 *
 * @param[in]	bIgnoreSourceKey
 * 					Don't try to detect a copy/move and return an empty list in that case.
 */
HRESULT MAPIPropHelper::GetArchiveList(ObjectEntryList *lplstArchives, bool bIgnoreSourceKey)
{
	HRESULT hr;
	ULONG cbValues = 0;
	SPropArrayPtr ptrPropArray;
	ObjectEntryList lstArchives;
	int result = 0;
	SizedSPropTagArray (4, sptaArchiveProps) = {4, {PROP_ARCHIVE_STORE_ENTRYIDS, PROP_ARCHIVE_ITEM_ENTRYIDS, PROP_ORIGINAL_SOURCEKEY, PR_SOURCE_KEY}};

	enum {
		IDX_ARCHIVE_STORE_ENTRYIDS,
		IDX_ARCHIVE_ITEM_ENTRYIDS,
		IDX_ORIGINAL_SOURCEKEY,
		IDX_SOURCE_KEY
	};

	hr = m_ptrMapiProp->GetProps(sptaArchiveProps, 0, &cbValues, &~ptrPropArray);
	if (FAILED(hr))
		return hr;

	if (hr == MAPI_W_ERRORS_RETURNED) {
		/**
		 * We expect all three PROP_* properties to be present or all three to be absent, with
		 * one exception: If PR_SOURCE_KEY is missing PROP_ORIGINAL_SOURCEKEY is not needed.
		 **/
		if (PROP_TYPE(ptrPropArray[IDX_ARCHIVE_STORE_ENTRYIDS].ulPropTag) == PT_ERROR &&
			PROP_TYPE(ptrPropArray[IDX_ARCHIVE_ITEM_ENTRYIDS].ulPropTag) == PT_ERROR)
		{
			// No entry ids exist. So that's fine
			return hrSuccess;
		}
		else if (PROP_TYPE(ptrPropArray[IDX_ARCHIVE_STORE_ENTRYIDS].ulPropTag) != PT_ERROR &&
				 PROP_TYPE(ptrPropArray[IDX_ARCHIVE_ITEM_ENTRYIDS].ulPropTag) != PT_ERROR)
		{
			// Both exist. So if PR_SOURCEKEY_EXISTS and PROP_ORIGINAL_SOURCEKEY doesn't
			// the entry is corrupt
			if (PROP_TYPE(ptrPropArray[IDX_SOURCE_KEY].ulPropTag) != PT_ERROR) {
				if (PROP_TYPE(ptrPropArray[IDX_ORIGINAL_SOURCEKEY].ulPropTag) == PT_ERROR) {
					return MAPI_E_CORRUPT_DATA;
				} else if (!bIgnoreSourceKey) {
					// @todo: Create correct locale.
					hr = Util::CompareProp(&ptrPropArray[IDX_SOURCE_KEY], &ptrPropArray[IDX_ORIGINAL_SOURCEKEY], createLocaleFromName(""), &result);
					if (hr != hrSuccess)
						return hr;
					if (result != 0)
						// The archive list was apparently copied into this message. So it's not valid (not an error).
						return hr;
				}
			} else
				hr = hrSuccess;
		}
		else
		{
			// One exists, one doesn't.
			return MAPI_E_CORRUPT_DATA;
		}
	}

	if (ptrPropArray[IDX_ARCHIVE_STORE_ENTRYIDS].Value.MVbin.cValues !=
	    ptrPropArray[IDX_ARCHIVE_ITEM_ENTRYIDS].Value.MVbin.cValues)
		return MAPI_E_CORRUPT_DATA;

	for (ULONG i = 0; i < ptrPropArray[0].Value.MVbin.cValues; ++i) {
		SObjectEntry objectEntry;
		objectEntry.sStoreEntryId = ptrPropArray[IDX_ARCHIVE_STORE_ENTRYIDS].Value.MVbin.lpbin[i];
		objectEntry.sItemEntryId = ptrPropArray[IDX_ARCHIVE_ITEM_ENTRYIDS].Value.MVbin.lpbin[i];
		lstArchives.emplace_back(std::move(objectEntry));
	}
	swap(*lplstArchives, lstArchives);
	return hr;
}

/**
 * Set or replace the list of archives for the current object.
 *
 * @param[in]	lstArchives
 *					The list of archive references that should be stored in the object.
 * @param[in]	bExplicitCommit
 *					If set to true, the changes are committed before this function returns.
 */
HRESULT MAPIPropHelper::SetArchiveList(const ObjectEntryList &lstArchives, bool bExplicitCommit)
{
	HRESULT hr;
	ULONG cValues = lstArchives.size();
	SPropArrayPtr ptrPropArray;
	SPropValuePtr ptrSourceKey;
	ObjectEntryList::const_iterator iArchive;
	ULONG cbProps = 2;

	hr = MAPIAllocateBuffer(3 * sizeof(SPropValue), &~ptrPropArray);
	if (hr != hrSuccess)
		return hr;

	ptrPropArray[0].ulPropTag = PROP_ARCHIVE_STORE_ENTRYIDS;
	ptrPropArray[0].Value.MVbin.cValues = cValues;
	hr = MAPIAllocateMore(cValues * sizeof(SBinary), ptrPropArray, (LPVOID*)&ptrPropArray[0].Value.MVbin.lpbin);
	if (hr != hrSuccess)
		return hr;
	ptrPropArray[1].ulPropTag = PROP_ARCHIVE_ITEM_ENTRYIDS;
	ptrPropArray[1].Value.MVbin.cValues = cValues;
	hr = MAPIAllocateMore(cValues * sizeof(SBinary), ptrPropArray, (LPVOID*)&ptrPropArray[1].Value.MVbin.lpbin);
	if (hr != hrSuccess)
		return hr;

	iArchive = lstArchives.cbegin();
	for (ULONG i = 0; i < cValues; ++i, ++iArchive) {
		ptrPropArray[0].Value.MVbin.lpbin[i].cb = iArchive->sStoreEntryId.size();
		hr = KAllocCopy(iArchive->sStoreEntryId, iArchive->sStoreEntryId.size(), reinterpret_cast<void **>(&ptrPropArray[0].Value.MVbin.lpbin[i].lpb), ptrPropArray);
		if (hr != hrSuccess)
			return hr;
		ptrPropArray[1].Value.MVbin.lpbin[i].cb = iArchive->sItemEntryId.size();
		hr = KAllocCopy(iArchive->sItemEntryId, iArchive->sItemEntryId.size(), reinterpret_cast<void **>(&ptrPropArray[1].Value.MVbin.lpbin[i].lpb), ptrPropArray);
		if (hr != hrSuccess)
			return hr;
	}

	/**
	 * We store the sourcekey of the item for which the list of archives is valid. This way if the
	 * item gets moved everything is fine. But when it gets copied a new archive will be created
	 * for it.
	 **/
	hr = HrGetOneProp(m_ptrMapiProp, PR_SOURCE_KEY, &~ptrSourceKey);
	if (hr == hrSuccess) {
		ptrPropArray[2].ulPropTag = PROP_ORIGINAL_SOURCEKEY;
		ptrPropArray[2].Value.bin = ptrSourceKey->Value.bin;	// Cheap copy

		cbProps = 3;
	}

	hr = m_ptrMapiProp->SetProps(cbProps, ptrPropArray.get(), NULL);
	if (hr != hrSuccess)
		return hr;
	if (bExplicitCommit)
		hr = m_ptrMapiProp->SaveChanges(KEEP_OPEN_READWRITE);
	return hr;
}

/**
 * Set a reference to a primary object in an archived object. A reference is set on archive
 * folders and archive messages. They reference to the original folder or message for which the
 * archived version exists.
 *
 * @param[in]	sEntryId
 *					The id of the referenced object.
 * @param[in]	bExplicitCommit
 *					If set to true, the changes are committed before this function returns.
 */
HRESULT  MAPIPropHelper::SetReference(const SObjectEntry &sEntry, bool bExplicitCommit)
{
	HRESULT hr;
	SPropValue sPropArray[2] = {{0}};

	sPropArray[0].ulPropTag = PROP_REF_STORE_ENTRYID;
	sPropArray[0].Value.bin.cb = sEntry.sStoreEntryId.size();
	sPropArray[0].Value.bin.lpb = sEntry.sStoreEntryId;

	sPropArray[1].ulPropTag = PROP_REF_ITEM_ENTRYID;
	sPropArray[1].Value.bin.cb = sEntry.sItemEntryId.size();
	sPropArray[1].Value.bin.lpb = sEntry.sItemEntryId;

	hr = m_ptrMapiProp->SetProps(2, sPropArray, NULL);
	if (hr != hrSuccess)
		return hr;
	if (bExplicitCommit)
		hr = m_ptrMapiProp->SaveChanges(KEEP_OPEN_READWRITE);
	return hr;
}

HRESULT MAPIPropHelper::ClearReference(bool bExplicitCommit)
{
	HRESULT hr;
	SizedSPropTagArray(2, sptaReferenceProps) = {2, {PROP_REF_STORE_ENTRYID, PROP_REF_ITEM_ENTRYID}};

	hr = m_ptrMapiProp->DeleteProps(sptaReferenceProps, NULL);
	if (hr != hrSuccess)
		return hr;
	if (bExplicitCommit)
		hr = m_ptrMapiProp->SaveChanges(KEEP_OPEN_READWRITE);
	return hr;
}

HRESULT MAPIPropHelper::GetReference(SObjectEntry *lpEntry)
{
	HRESULT hr;
	ULONG cMessageProps = 0;
	SPropArrayPtr ptrMessageProps;
	SizedSPropTagArray(2, sptaMessageProps) = {2, {PROP_REF_STORE_ENTRYID, PROP_REF_ITEM_ENTRYID}};
	enum {IDX_REF_STORE_ENTRYID, IDX_REF_ITEM_ENTRYID};

	if (lpEntry == NULL)
		return MAPI_E_INVALID_PARAMETER;
	hr = m_ptrMapiProp->GetProps(sptaMessageProps, 0, &cMessageProps, &~ptrMessageProps);
	if (FAILED(hr))
		return hr;
	if (PROP_TYPE(ptrMessageProps[IDX_REF_STORE_ENTRYID].ulPropTag) == PT_ERROR)
		return ptrMessageProps[IDX_REF_STORE_ENTRYID].Value.err;
	if (PROP_TYPE(ptrMessageProps[IDX_REF_ITEM_ENTRYID].ulPropTag) == PT_ERROR)
		return ptrMessageProps[IDX_REF_ITEM_ENTRYID].Value.err;
	lpEntry->sStoreEntryId = ptrMessageProps[IDX_REF_STORE_ENTRYID].Value.bin;
	lpEntry->sItemEntryId = ptrMessageProps[IDX_REF_ITEM_ENTRYID].Value.bin;
	return hr;
}

HRESULT MAPIPropHelper::ReferencePrevious(const SObjectEntry &sEntry)
{
	SPropValue sPropValue = {0};

	sPropValue.ulPropTag = PROP_REF_PREV_ENTRYID;
	sPropValue.Value.bin.cb = sEntry.sItemEntryId.size();
	sPropValue.Value.bin.lpb = sEntry.sItemEntryId;
	return HrSetOneProp(m_ptrMapiProp, &sPropValue);
}

HRESULT MAPIPropHelper::OpenPrevious(ArchiverSessionPtr ptrSession, LPMESSAGE *lppMessage)
{
	HRESULT hr;
	SPropValuePtr ptrEntryID;
	ULONG ulType;
	MessagePtr ptrMessage;

	if (lppMessage == NULL)
		return MAPI_E_INVALID_PARAMETER;
	hr = HrGetOneProp(m_ptrMapiProp, PROP_REF_PREV_ENTRYID, &~ptrEntryID);
	if (hr != hrSuccess)
		return hr;

	hr = ptrSession->GetMAPISession()->OpenEntry(ptrEntryID->Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(ptrEntryID->Value.bin.lpb),
	     &iid_of(ptrMessage), MAPI_MODIFY, &ulType, &~ptrMessage);
	if (hr == MAPI_E_NOT_FOUND) {
		SPropValuePtr ptrStoreEntryID;
		MsgStorePtr ptrStore;

		hr = HrGetOneProp(m_ptrMapiProp, PR_STORE_ENTRYID, &~ptrStoreEntryID);
		if (hr != hrSuccess)
			return hr;
		hr = ptrSession->OpenStore(ptrStoreEntryID->Value.bin, &~ptrStore);
		if (hr != hrSuccess)
			return hr;
		hr = ptrStore->OpenEntry(ptrEntryID->Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(ptrEntryID->Value.bin.lpb),
		     &iid_of(ptrMessage), MAPI_MODIFY, &ulType, &~ptrMessage);
	}
	if (hr != hrSuccess)
		return hr;

	return ptrMessage->QueryInterface(IID_IMessage,
		reinterpret_cast<LPVOID *>(lppMessage));
}

/**
 * Remove the {72e98ebc-57d2-4ab5-b0aad50a7b531cb9}/stubbed property. Note that IsStubbed can still
 * return true if the message class is not updated properly. However, this is done in the caller
 * of this function, which has no notion of the set of named properies that are needed to remove this
 * property.
 */
HRESULT MAPIPropHelper::RemoveStub()
{
	SizedSPropTagArray(1, sptaArchiveProps) = {1, {PROP_STUBBED}};
	return m_ptrMapiProp->DeleteProps(sptaArchiveProps, NULL);
}

HRESULT MAPIPropHelper::SetClean()
{
	SizedSPropTagArray(1, sptaDirtyProps) = {1, {PROP_DIRTY}};
	return m_ptrMapiProp->DeleteProps(sptaDirtyProps, NULL);
}

/**
 * Detach an object from its archived version.
 * This does not cause the reference in the archived version to be removed.
 */
HRESULT MAPIPropHelper::DetachFromArchives()
{
	SizedSPropTagArray(5, sptaArchiveProps) = {5, {PROP_ARCHIVE_STORE_ENTRYIDS, PROP_ARCHIVE_ITEM_ENTRYIDS, PROP_STUBBED, PROP_DIRTY, PROP_ORIGINAL_SOURCEKEY}};
	return m_ptrMapiProp->DeleteProps(sptaArchiveProps, NULL);
}

/**
 * Get the parent folder of an object.
 *
 * @param[in]	lpSession
 *					Pointer to a session object that's used to open the folder with.
 * @param[in]	lppFolder
 *					Pointer to a IMAPIFolder pointer that will be assigned the address
 *					of the returned folder.
 */
HRESULT MAPIPropHelper::GetParentFolder(ArchiverSessionPtr ptrSession, LPMAPIFOLDER *lppFolder)
{
	HRESULT hr;
	SPropArrayPtr ptrPropArray;
	MsgStorePtr ptrMsgStore;
	MAPIFolderPtr ptrFolder;
	ULONG cValues = 0;
	ULONG ulType = 0;
	static constexpr const SizedSPropTagArray(2, sptaProps) =
		{2, {PR_PARENT_ENTRYID, PR_STORE_ENTRYID}};

	if (ptrSession == NULL)
		return MAPI_E_INVALID_PARAMETER;
	// We can't just open a folder on the session (at least not in Linux). So we open the store first
	hr = m_ptrMapiProp->GetProps(sptaProps, 0, &cValues, &~ptrPropArray);
	if (hr != hrSuccess)
		return hr;
	hr = ptrSession->OpenStore(ptrPropArray[1].Value.bin, &~ptrMsgStore);
	if (hr != hrSuccess)
		return hr;
	hr = ptrMsgStore->OpenEntry(ptrPropArray[0].Value.bin.cb, reinterpret_cast<ENTRYID *>(ptrPropArray[0].Value.bin.lpb), &iid_of(ptrFolder), MAPI_BEST_ACCESS | fMapiDeferredErrors, &ulType, &~ptrFolder);
	if (hr != hrSuccess)
		return hr;
	return ptrFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppFolder));
}

}} /* namespace */
