/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <string>
#include <utility>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "ECMsgStorePublic.h"
#include "ECMAPIFolder.h"
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include "ClientUtil.h"
#include "pcutil.hpp"
#include <kopano/ECGetText.h>
#include <kopano/mapiext.h>
#include <mapiutil.h>
#include "ECMAPIFolderPublic.h"
#include <kopano/ECGuid.h>

using namespace KC;

ECMsgStorePublic::ECMsgStorePublic(const char *lpszProfname,
    IMAPISupport *sup, WSTransport *tp, BOOL modify,
    ULONG ulProfileFlags, BOOL fIsSpooler, BOOL bOfflineStore) :
	ECMsgStore(lpszProfname, sup, tp, modify,
	    ulProfileFlags, fIsSpooler, false, bOfflineStore)
{
	HrAddPropHandlers(PR_IPM_SUBTREE_ENTRYID, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_IPM_PUBLIC_FOLDERS_ENTRYID, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_IPM_FAVORITES_ENTRYID, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID, GetPropHandler, SetPropHandler, this, false, true);
}

HRESULT ECMsgStorePublic::Create(const char *lpszProfname,
    IMAPISupport *lpSupport, WSTransport *lpTransport, BOOL fModify,
    ULONG ulProfileFlags, BOOL fIsSpooler, BOOL bOfflineStore,
    ECMsgStore **lppECMsgStore)
{
	return alloc_wrap<ECMsgStorePublic>(lpszProfname, lpSupport,
	       lpTransport, fModify, ulProfileFlags,
	       fIsSpooler, bOfflineStore)
	       .as(IID_ECMsgStore, reinterpret_cast<void **>(lppECMsgStore));
}

HRESULT ECMsgStorePublic::QueryInterface(REFIID refiid, void **lppInterface)
{
	return ECMsgStore::QueryInterface(refiid, lppInterface);
}

HRESULT ECMsgStorePublic::GetPropHandler(unsigned int ulPropTag,
     void *lpProvider, unsigned int ulFlags, SPropValue *lpsPropValue,
     ECGenericProp *lpParam, void *lpBase)
{
	auto lpStore = static_cast<ECMsgStorePublic *>(lpParam);

	switch(ulPropTag) {
	case PR_IPM_SUBTREE_ENTRYID:
		return ::GetPublicEntryId(ePE_IPMSubtree, lpStore->GetStoreGuid(), lpBase, &lpsPropValue->Value.bin.cb, (LPENTRYID*)&lpsPropValue->Value.bin.lpb);
	case PR_IPM_PUBLIC_FOLDERS_ENTRYID:
		return ::GetPublicEntryId(ePE_PublicFolders, lpStore->GetStoreGuid(), lpBase, &lpsPropValue->Value.bin.cb, (LPENTRYID*)&lpsPropValue->Value.bin.lpb);
	case PR_IPM_FAVORITES_ENTRYID:
		return ::GetPublicEntryId(ePE_Favorites, lpStore->GetStoreGuid(), lpBase, &lpsPropValue->Value.bin.cb, (LPENTRYID*)&lpsPropValue->Value.bin.lpb);
	case PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID: {
		auto hr = lpStore->HrGetRealProp(PR_IPM_SUBTREE_ENTRYID, ulFlags, lpBase, lpsPropValue);
		if (hr == hrSuccess)
			lpsPropValue->ulPropTag = PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID;
		return hr;
	}
	default:
		return MAPI_E_NOT_FOUND;
	}
}

HRESULT ECMsgStorePublic::SetPropHandler(unsigned int ulPropTag,
    void *lpProvider, const SPropValue *lpsPropValue, ECGenericProp *lpParam)
{
	SPropValue sPropValue;
	auto lpStore = static_cast<ECMsgStorePublic *>(lpParam);

	switch(ulPropTag) {
	case PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID:
		sPropValue.ulPropTag = PR_IPM_SUBTREE_ENTRYID;
		sPropValue.Value = lpsPropValue->Value;	// Cheap copy
		return lpStore->HrSetRealProp(&sPropValue);
	default:
		return MAPI_E_NOT_FOUND;
	}
}

HRESULT ECMsgStorePublic::SetEntryId(ULONG cbEntryId, const ENTRYID *lpEntryId)
{
	HRESULT hr;

	hr = ECMsgStore::SetEntryId(cbEntryId, lpEntryId);
	if(hr != hrSuccess)
		return hr;

	return BuildIPMSubTree();
}

HRESULT ECMsgStorePublic::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	if (lpulObjType == nullptr || lppUnk == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	unsigned int objtype = 0;
	object_ptr<ECMAPIFolder> lpMAPIFolder;
	BOOL				fModifyObject = FALSE;
	enumPublicEntryID	ePublicEntryID = ePE_None;
	ULONG ulResult = 0, ulResults;
	object_ptr<IECPropStorage> lpPropStorage;
	object_ptr<WSMAPIFolderOps> lpFolderOps;
	memory_ptr<SPropValue> lpsPropValue, lpParentProp;
	memory_ptr<ENTRYID> lpEntryIDIntern;

	if(ulFlags & MAPI_MODIFY) {
		if (!fModify)
			return MAPI_E_NO_ACCESS;
		fModifyObject = TRUE;
	}

	if(ulFlags & MAPI_BEST_ACCESS)
		fModifyObject = fModify;

	// Open always online the root folder
	if (cbEntryID == 0 || lpEntryID == nullptr)
		return ECMsgStore::OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
	auto hr = HrCompareEntryIdWithStoreGuid(cbEntryID, lpEntryID, &GetStoreGuid());
	if(hr != hrSuccess)
		return hr;

	if(ComparePublicEntryId(ePE_IPMSubtree, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE)
		ePublicEntryID = ePE_IPMSubtree;
	else if(ComparePublicEntryId(ePE_Favorites, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE)
		ePublicEntryID = ePE_Favorites;
	else if(ComparePublicEntryId(ePE_PublicFolders, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE)
		ePublicEntryID = ePE_PublicFolders;
	else if (lpEntryID && (lpEntryID->abFlags[3] & KOPANO_FAVORITE)) {
		ePublicEntryID = ePE_FavoriteSubFolder;
		// Replace the original entryid because this one is only readable
		hr = KAllocCopy(lpEntryIDIntern, cbEntryID, &~lpEntryIDIntern);
		if (hr != hrSuccess)
			return hr;
		// Remove Flags intern
		lpEntryIDIntern->abFlags[3] &= ~KOPANO_FAVORITE;
		lpEntryID = lpEntryIDIntern;
	}

	hr = HrGetObjTypeFromEntryId(cbEntryID, lpEntryID, &objtype);
	if(hr != hrSuccess)
		return hr;
	if (objtype == MAPI_MESSAGE ||
	    (objtype != MAPI_FOLDER && ePublicEntryID != ePE_FavoriteSubFolder))
		// Open online Messages.
		// On success, message is open, now we can exit
		return ECMsgStore::OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
	if (objtype != MAPI_FOLDER)
		return MAPI_E_NOT_FOUND;

	if (ePublicEntryID == ePE_PublicFolders) {
		hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpsPropValue);
		if (hr != hrSuccess)
			return hr;
		// Get the online Subtree entryid
		hr = HrGetRealProp(PR_IPM_SUBTREE_ENTRYID, 0, lpsPropValue, lpsPropValue);
		if (hr != hrSuccess)
			return hr;
		cbEntryID = lpsPropValue->Value.bin.cb;
		lpEntryID = (LPENTRYID)lpsPropValue->Value.bin.lpb;
	}

	if (ePublicEntryID != ePE_IPMSubtree && ePublicEntryID != ePE_Favorites) {
		hr = lpTransport->HrOpenFolderOps(cbEntryID, lpEntryID, &~lpFolderOps);
		if (hr != hrSuccess)
			return hr;
	} else {
		lpFolderOps.reset();
	}

	hr = ECMAPIFolderPublic::Create(this, fModifyObject, lpFolderOps, ePublicEntryID, &~lpMAPIFolder);
	if (hr != hrSuccess)
		return hr;

	if (ePublicEntryID != ePE_IPMSubtree && ePublicEntryID != ePE_Favorites) {
		//FIXME: Wrong parent entryid
		hr = lpTransport->HrOpenPropStorage(m_cbEntryId, m_lpEntryId, cbEntryID, lpEntryID, ulFlags & SHOW_SOFT_DELETES, &~lpPropStorage);
		if (hr != hrSuccess)
			return hr;
		hr = lpMAPIFolder->HrSetPropStorage(lpPropStorage, TRUE);
		if (hr != hrSuccess)
			return hr;
		//if (ePublicEntryID == ePE_FavoriteSubFolder)
		//	lpEntryID->abFlags[3] = KOPANO_FAVORITE;
	} else {
		lpMAPIFolder->HrLoadEmptyProps();
	}

	hr = lpMAPIFolder->SetEntryId(cbEntryID, lpEntryID);
	if (hr != hrSuccess)
		return hr;
	// Get the parent entryid of a folder a check if this is the online subtree entryid. When it is,
	// change the parent to the static parent entryid
	hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpsPropValue);
	if (hr != hrSuccess)
		return hr;
	if (HrGetOneProp(lpMAPIFolder, PR_PARENT_ENTRYID, &~lpParentProp) == hrSuccess &&
	    HrGetRealProp(PR_IPM_SUBTREE_ENTRYID, 0, lpsPropValue, lpsPropValue) == hrSuccess &&
	    CompareEntryIDs(lpsPropValue->Value.bin.cb, (LPENTRYID)lpsPropValue->Value.bin.lpb, lpParentProp->Value.bin.cb, (LPENTRYID)lpParentProp->Value.bin.lpb, 0, &ulResults) == hrSuccess &&
	    ulResults == TRUE)
		lpMAPIFolder->SetParentID(m_cIPMPublicFoldersID, m_lpIPMPublicFoldersID);

	AddChild(lpMAPIFolder);
	if (lpulObjType)
		*lpulObjType = MAPI_FOLDER;
	if (lpInterface != nullptr)
		return lpMAPIFolder->QueryInterface(*lpInterface, reinterpret_cast<void **>(lppUnk));
	return lpMAPIFolder->QueryInterface(IID_IMAPIFolder, reinterpret_cast<void **>(lppUnk));
}

HRESULT ECMsgStorePublic::InitEntryIDs()
{
	HRESULT hr;

	if (m_lpIPMSubTreeID == NULL) {
		hr = ::GetPublicEntryId(ePE_IPMSubtree, GetStoreGuid(), nullptr, &m_cIPMSubTreeID, &~m_lpIPMSubTreeID);
		if(hr != hrSuccess)
			return hr;
	}

	if (m_lpIPMPublicFoldersID == NULL) {
		hr = ::GetPublicEntryId(ePE_PublicFolders, GetStoreGuid(), nullptr, &m_cIPMPublicFoldersID, &~m_lpIPMPublicFoldersID);
		if(hr != hrSuccess)
			return hr;
	}

	if (m_lpIPMFavoritesID == NULL) {
		hr = ::GetPublicEntryId(ePE_Favorites, GetStoreGuid(), nullptr, &m_cIPMFavoritesID, &~m_lpIPMFavoritesID);
		if(hr != hrSuccess)
			return hr;
	}

	return hrSuccess;
}

HRESULT ECMsgStorePublic::GetPublicEntryId(enumPublicEntryID ePublicEntryID, void *lpBase, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	if (lpcbEntryID == NULL || lppEntryID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ULONG cbPublicID = 0;
	LPENTRYID lpPublicID = NULL;
	LPENTRYID lpEntryID = NULL;

	HRESULT hr = InitEntryIDs();
	if(hr != hrSuccess)
		return hr;
	switch(ePublicEntryID)
	{
		case ePE_IPMSubtree:
			cbPublicID = m_cIPMSubTreeID;
			lpPublicID = m_lpIPMSubTreeID;
			break;
		case ePE_PublicFolders:
			cbPublicID = m_cIPMPublicFoldersID;
			lpPublicID = m_lpIPMPublicFoldersID;
			break;
		case ePE_Favorites:
			cbPublicID = m_cIPMFavoritesID;
			lpPublicID = m_lpIPMFavoritesID;
			break;
		default:
			return MAPI_E_INVALID_PARAMETER;
	}

	hr = KAllocCopy(lpPublicID, cbPublicID, reinterpret_cast<void **>(&lpEntryID), lpBase);
	if (hr != hrSuccess)
		return hr;
	*lpcbEntryID = cbPublicID;
	*lppEntryID = lpEntryID;
	return hrSuccess;
}

HRESULT ECMsgStorePublic::ComparePublicEntryId(enumPublicEntryID ePublicEntryID,
    ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG *lpulResult)
{
	if (lpEntryID == NULL || lpulResult == NULL)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr;
	unsigned int ulResult = 0, cbPublicID = 0;
	LPENTRYID lpPublicID = NULL;

	hr = InitEntryIDs();
	if(hr != hrSuccess)
		return hr;
	switch(ePublicEntryID)
	{
		case ePE_IPMSubtree:
			cbPublicID = m_cIPMSubTreeID;
			lpPublicID = m_lpIPMSubTreeID;
			break;
		case ePE_PublicFolders:
			cbPublicID = m_cIPMPublicFoldersID;
			lpPublicID = m_lpIPMPublicFoldersID;
			break;
		case ePE_Favorites:
			cbPublicID = m_cIPMFavoritesID;
			lpPublicID = m_lpIPMFavoritesID;
			break;
		default:
			return MAPI_E_INVALID_PARAMETER;
	}

	hr = GetMsgStore()->CompareEntryIDs(cbEntryID, lpEntryID, cbPublicID, lpPublicID, 0, &ulResult);
	if(hr != hrSuccess)
		return hr;

	*lpulResult = ulResult;
	return hrSuccess;
}

HRESULT ECMsgStorePublic::BuildIPMSubTree()
{
	object_ptr<ECMemTable> lpIPMSubTree;
	memory_ptr<SPropValue> lpProps;
	ULONG cProps = 0, cMaxProps = 0, ulRowId = 0;
	SPropValue sKeyProp;
	static constexpr const SizedSPropTagArray(13, sPropsHierarchyColumns) = {13, {
			PR_ENTRYID, PR_DISPLAY_NAME_W,
			PR_CONTENT_COUNT, PR_CONTENT_UNREAD,
			PR_STORE_ENTRYID, PR_STORE_RECORD_KEY,
			PR_STORE_SUPPORT_MASK, PR_INSTANCE_KEY,
			PR_RECORD_KEY, PR_ACCESS, PR_ACCESS_LEVEL,
			PR_OBJECT_TYPE, PR_FOLDER_TYPE} };

	if (m_lpIPMSubTree != NULL){
		assert(false);
		return hrSuccess;
	}
	auto hr = ECMemTable::Create(sPropsHierarchyColumns, PR_ROWID, &~lpIPMSubTree);
	if(hr != hrSuccess)
		return hr;

	//  Favorites
	ulRowId = 1;
	cMaxProps = 22;
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * cMaxProps, &~lpProps);
	if(hr != hrSuccess)
		return hr;
	lpProps[cProps].ulPropTag = PR_ENTRYID;
	hr = GetPublicEntryId(ePE_Favorites, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		return hr;
	++cProps;

	lpProps[cProps].ulPropTag = PR_LONGTERM_ENTRYID_FROM_TABLE;
	hr = GetPublicEntryId(ePE_Favorites, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		return hr;
	++cProps;

	lpProps[cProps].ulPropTag = PR_DISPLAY_TYPE;
	lpProps[cProps++].Value.ul = DT_FOLDER;

	lpProps[cProps].ulPropTag = PR_DEPTH;
	lpProps[cProps++].Value.ul = 1;

	lpProps[cProps].ulPropTag = PR_PARENT_ENTRYID;
	hr = GetPublicEntryId(ePE_IPMSubtree, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if (hr != hrSuccess)
		return hr;
	++cProps;

	lpProps[cProps].ulPropTag = PR_DISPLAY_NAME_W;
	lpProps[cProps++].Value.lpszW = KC_W("Favorites"); // FIXME: Use dynamic name, read from global profile (like exchange)
	lpProps[cProps].ulPropTag = PR_CONTENT_COUNT;
	lpProps[cProps++].Value.ul = 0;
	lpProps[cProps].ulPropTag = PR_CONTENT_UNREAD;
	lpProps[cProps++].Value.ul = 0;

	if (ECMAPIProp::DefaultMAPIGetProp(PR_STORE_ENTRYID, this, 0, &lpProps[cProps], this, lpProps) == hrSuccess)
		++cProps;
	if (ECMAPIProp::DefaultMAPIGetProp(PR_STORE_RECORD_KEY, this, 0, &lpProps[cProps], this, lpProps) == hrSuccess)
		++cProps;
	if (ECMAPIProp::DefaultMAPIGetProp(PR_STORE_SUPPORT_MASK, this, 0, &lpProps[cProps], this, lpProps) == hrSuccess)
		++cProps;

	lpProps[cProps].ulPropTag = PR_INSTANCE_KEY;
	lpProps[cProps].Value.bin.cb = sizeof(ULONG)*2;
	hr = MAPIAllocateMore(lpProps[cProps].Value.bin.cb, lpProps,
	     reinterpret_cast<void **>(&lpProps[cProps].Value.bin.lpb));
	if(hr != hrSuccess)
		return hr;

	memset(lpProps[cProps].Value.bin.lpb, 0, lpProps[cProps].Value.bin.cb );
	memcpy(lpProps[cProps].Value.bin.lpb, &ulRowId, sizeof(ULONG));
	++cProps;

	lpProps[cProps].ulPropTag = PR_RECORD_KEY;
	hr = GetPublicEntryId(ePE_Favorites, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		return hr;
	++cProps;

	lpProps[cProps].ulPropTag = PR_ACCESS;
	lpProps[cProps++].Value.ul = MAPI_ACCESS_READ;

	lpProps[cProps].ulPropTag = PR_ACCESS_LEVEL;
	lpProps[cProps++].Value.ul = 0;

	lpProps[cProps].ulPropTag = PR_RIGHTS;
	lpProps[cProps++].Value.ul = ecRightsAll;

	lpProps[cProps].ulPropTag = PR_SUBFOLDERS;
	lpProps[cProps++].Value.b = true;

	lpProps[cProps].ulPropTag = PR_OBJECT_TYPE;
	lpProps[cProps++].Value.ul = MAPI_FOLDER;

	lpProps[cProps].ulPropTag = PR_FOLDER_TYPE;
	lpProps[cProps++].Value.ul = FOLDER_GENERIC;

	lpProps[cProps].ulPropTag = PR_ROWID;
	lpProps[cProps++].Value.ul = ulRowId;

	sKeyProp.ulPropTag = PR_ROWID;
	sKeyProp.Value.ul = ulRowId;

	hr = lpIPMSubTree->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &sKeyProp, lpProps, cProps);
	if (hr != hrSuccess)
		return hr;
	assert(cProps <= cMaxProps);

	// the folder "Public Folders"
	++ulRowId;
	cProps = 0;
	cMaxProps = 20;

	hr = MAPIAllocateBuffer(sizeof(SPropValue) * cMaxProps, &~lpProps);
	if(hr != hrSuccess)
		return hr;
	lpProps[cProps].ulPropTag = PR_ENTRYID;
	hr = ((ECMsgStorePublic*)GetMsgStore())->GetPublicEntryId(ePE_PublicFolders, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		return hr;
	++cProps;

	lpProps[cProps].ulPropTag = PR_LONGTERM_ENTRYID_FROM_TABLE;
	hr = GetPublicEntryId(ePE_PublicFolders, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		return hr;
	++cProps;

	lpProps[cProps].ulPropTag = PR_DISPLAY_TYPE;
	lpProps[cProps++].Value.ul = DT_FOLDER;

	lpProps[cProps].ulPropTag = PR_DEPTH;
	lpProps[cProps++].Value.ul = 1;

	lpProps[cProps].ulPropTag = PR_PARENT_ENTRYID;
	hr = GetPublicEntryId(ePE_IPMSubtree, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if (hr != hrSuccess)
		return hr;
	++cProps;

	lpProps[cProps].ulPropTag = PR_DISPLAY_NAME_W;
	lpProps[cProps++].Value.lpszW = KC_W("Public Folders"); // FIXME: Use dynamic name, read from global profile (like exchange)
	lpProps[cProps].ulPropTag = PR_CONTENT_COUNT;
	lpProps[cProps++].Value.ul = 0;
	lpProps[cProps].ulPropTag = PR_CONTENT_UNREAD;
	lpProps[cProps++].Value.ul = 0;

	if (ECMAPIProp::DefaultMAPIGetProp(PR_STORE_ENTRYID, this, 0, &lpProps[cProps], this, lpProps) == hrSuccess)
		++cProps;
	if (ECMAPIProp::DefaultMAPIGetProp(PR_STORE_RECORD_KEY, this, 0, &lpProps[cProps], this, lpProps) == hrSuccess)
		++cProps;
	if (ECMAPIProp::DefaultMAPIGetProp(PR_STORE_SUPPORT_MASK, this, 0, &lpProps[cProps], this, lpProps) == hrSuccess)
		++cProps;

	lpProps[cProps].ulPropTag = PR_INSTANCE_KEY;
	lpProps[cProps].Value.bin.cb = sizeof(ULONG)*2;
	hr = MAPIAllocateMore(lpProps[cProps].Value.bin.cb, lpProps,
	     reinterpret_cast<void **>(&lpProps[cProps].Value.bin.lpb));
	if(hr != hrSuccess)
		return hr;
	memset(lpProps[cProps].Value.bin.lpb, 0, lpProps[cProps].Value.bin.cb );
	memcpy(lpProps[cProps].Value.bin.lpb, &ulRowId, sizeof(ULONG));
	++cProps;

	lpProps[cProps].ulPropTag = PR_RECORD_KEY;
	hr = GetPublicEntryId(ePE_PublicFolders, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		return hr;
	++cProps;

	lpProps[cProps].ulPropTag = PR_ACCESS;
	lpProps[cProps++].Value.ul = 2; //FIXME: use variable

	lpProps[cProps].ulPropTag = PR_ACCESS_LEVEL;
	lpProps[cProps++].Value.ul = 1;

	//lpProps[cProps].ulPropTag = PR_RIGHTS;
	//lpProps[cProps++].Value.ul = 1;

	lpProps[cProps].ulPropTag = PR_SUBFOLDERS;
	lpProps[cProps++].Value.b = true;

	lpProps[cProps].ulPropTag = PR_OBJECT_TYPE;
	lpProps[cProps++].Value.ul = MAPI_FOLDER;

	lpProps[cProps].ulPropTag = PR_FOLDER_TYPE;
	lpProps[cProps++].Value.ul = FOLDER_GENERIC;

	lpProps[cProps].ulPropTag = PR_ROWID;
	lpProps[cProps++].Value.ul = ulRowId;

	sKeyProp.ulPropTag = PR_ROWID;
	sKeyProp.Value.ul = ulRowId;

	hr = lpIPMSubTree->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &sKeyProp, lpProps, cProps);
	if (hr != hrSuccess)
		return hr;

	assert(cProps <= cMaxProps);
	m_lpIPMSubTree = std::move(lpIPMSubTree);
	return hrSuccess;
}

ECMemTable *ECMsgStorePublic::GetIPMSubTree()
{
	assert(m_lpIPMSubTree != NULL);
	return m_lpIPMSubTree;
}

HRESULT ECMsgStorePublic::Advise(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulEventMask, IMAPIAdviseSink *lpAdviseSink, ULONG *lpulConnection)
{
	ULONG ulResult = 0;
	memory_ptr<ENTRYID> lpEntryIDIntern;

	if(ComparePublicEntryId(ePE_IPMSubtree, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE) {
		return MAPI_E_NO_SUPPORT; // FIXME
	} else if(ComparePublicEntryId(ePE_Favorites, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE) {
		return MAPI_E_NO_SUPPORT; // FIXME
	} else if(ComparePublicEntryId(ePE_PublicFolders, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE) {
		return MAPI_E_NO_SUPPORT; // FIXME
	} else if (lpEntryID && (lpEntryID->abFlags[3] & KOPANO_FAVORITE)) {
		// Replace the original entryid because this one is only readable
		auto hr = KAllocCopy(lpEntryID, cbEntryID, &~lpEntryIDIntern);
		if (hr != hrSuccess)
			return hr;
		// Remove Flags intern
		lpEntryIDIntern->abFlags[3] &= ~KOPANO_FAVORITE;

		lpEntryID = lpEntryIDIntern;
	}
	return ECMsgStore::Advise(cbEntryID, lpEntryID, ulEventMask, lpAdviseSink, lpulConnection);
}
