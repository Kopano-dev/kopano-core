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

using namespace std;

ECMsgStorePublic::ECMsgStorePublic(char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL bOfflineStore) :
	ECMsgStore(lpszProfname, lpSupport, lpTransport, fModify, ulProfileFlags, fIsSpooler, false, bOfflineStore)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMsgStorePublic::ECMsgStorePublic","");

	HrAddPropHandlers(PR_IPM_SUBTREE_ENTRYID,			GetPropHandler,	DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	HrAddPropHandlers(PR_IPM_PUBLIC_FOLDERS_ENTRYID,	GetPropHandler,	DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	HrAddPropHandlers(PR_IPM_FAVORITES_ENTRYID,			GetPropHandler,	DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	HrAddPropHandlers(PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID,	GetPropHandler, SetPropHandler,			(void*) this, FALSE, TRUE);

	m_lpIPMSubTreeID = NULL;
	m_lpIPMFavoritesID = NULL;
	m_lpIPMPublicFoldersID = NULL;

	m_cIPMSubTreeID = 0;
	m_cIPMFavoritesID = 0;
	m_cIPMPublicFoldersID = 0;

	m_lpIPMSubTree = NULL;
	m_lpDefaultMsgStore = NULL;

	TRACE_MAPI(TRACE_RETURN, "ECMsgStorePublic::ECMsgStorePublic","");
}

ECMsgStorePublic::~ECMsgStorePublic(void)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMsgStorePublic::~ECMsgStorePublic","");

	if (m_lpDefaultMsgStore)
		m_lpDefaultMsgStore->Release();

	if (m_lpIPMSubTree)
		m_lpIPMSubTree->Release();
	MAPIFreeBuffer(m_lpIPMSubTreeID);
	MAPIFreeBuffer(m_lpIPMFavoritesID);
	MAPIFreeBuffer(m_lpIPMPublicFoldersID);
	TRACE_MAPI(TRACE_RETURN, "~ECMsgStorePublic::ECMsgStorePublic","");

}

HRESULT	ECMsgStorePublic::Create(char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL bOfflineStore, ECMsgStore **lppECMsgStore) {
	HRESULT hr = hrSuccess;

	ECMsgStorePublic *lpStore = new ECMsgStorePublic(lpszProfname, lpSupport, lpTransport, fModify, ulProfileFlags, fIsSpooler, bOfflineStore);

	hr = lpStore->QueryInterface(IID_ECMsgStore, (void **)lppECMsgStore);

	if(hr != hrSuccess)
		delete lpStore;

	return hr;
}

HRESULT ECMsgStorePublic::QueryInterface(REFIID refiid, void **lppInterface)
{
	return ECMsgStore::QueryInterface(refiid, lppInterface);
}

HRESULT ECMsgStorePublic::GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT hr = hrSuccess;

	ECMsgStorePublic *lpStore = (ECMsgStorePublic *)lpParam;

	switch(ulPropTag) {
	case PR_IPM_SUBTREE_ENTRYID:
		hr = ::GetPublicEntryId(ePE_IPMSubtree, lpStore->GetStoreGuid(), lpBase, &lpsPropValue->Value.bin.cb, (LPENTRYID*)&lpsPropValue->Value.bin.lpb);
		break;
	case PR_IPM_PUBLIC_FOLDERS_ENTRYID:
		hr = ::GetPublicEntryId(ePE_PublicFolders, lpStore->GetStoreGuid(), lpBase, &lpsPropValue->Value.bin.cb, (LPENTRYID*)&lpsPropValue->Value.bin.lpb);
		break;
	case PR_IPM_FAVORITES_ENTRYID:
		hr = ::GetPublicEntryId(ePE_Favorites, lpStore->GetStoreGuid(), lpBase, &lpsPropValue->Value.bin.cb, (LPENTRYID*)&lpsPropValue->Value.bin.lpb);
		break;
	case PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID:
		hr = lpStore->HrGetRealProp(PR_IPM_SUBTREE_ENTRYID, ulFlags, lpBase, lpsPropValue);
		if (hr == hrSuccess) {
			lpsPropValue->ulPropTag = PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID;
		}
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}

	return hr;
}

HRESULT ECMsgStorePublic::SetPropHandler(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam)
{
	HRESULT hr = hrSuccess;
	SPropValue sPropValue;

	ECMsgStorePublic *lpStore = (ECMsgStorePublic *)lpParam;

	switch(ulPropTag) {
	case PR_EC_PUBLIC_IPM_SUBTREE_ENTRYID:
		sPropValue.ulPropTag = PR_IPM_SUBTREE_ENTRYID;
		sPropValue.Value = lpsPropValue->Value;	// Cheap copy
		hr = lpStore->HrSetRealProp(&sPropValue);
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}

	return hr;
}

HRESULT ECMsgStorePublic::SetEntryId(ULONG cbEntryId, LPENTRYID lpEntryId)
{
	HRESULT hr;
	
	hr = ECMsgStore::SetEntryId(cbEntryId, lpEntryId);
	if(hr != hrSuccess)
		return hr;

	return BuildIPMSubTree();
}

HRESULT ECMsgStorePublic::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
{
	HRESULT				hr = hrSuccess;
	unsigned int		ulObjType = 0;

	ECMAPIFolder*		lpMAPIFolder = NULL;

	BOOL				fModifyObject = FALSE;
	enumPublicEntryID	ePublicEntryID = ePE_None;
	ULONG				ulResult = 0;

	IECPropStorage*		lpPropStorage = NULL;
	WSMAPIFolderOps*	lpFolderOps = NULL;
	LPSPropValue		lpsPropValue = NULL;
	LPENTRYID			lpEntryIDIntern = NULL;
	LPSPropValue		lpParentProp = NULL;
	ULONG				ulResults;

	// Check input/output variables
	if(lpulObjType == NULL || lppUnk == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if(ulFlags & MAPI_MODIFY) {
		if(!fModify) {
			hr = MAPI_E_NO_ACCESS;
			goto exit;
		} else
			fModifyObject = TRUE;
	}

	if(ulFlags & MAPI_BEST_ACCESS)
		fModifyObject = fModify;

	// Open always online the root folder
	if(cbEntryID == 0 || lpEntryID == NULL) {
		hr = ECMsgStore::OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
		goto exit;
	}

	hr = HrCompareEntryIdWithStoreGuid(cbEntryID, lpEntryID, &GetStoreGuid());
	if(hr != hrSuccess)
		goto exit;

	if(ComparePublicEntryId(ePE_IPMSubtree, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE)
		ePublicEntryID = ePE_IPMSubtree;
	else if(ComparePublicEntryId(ePE_Favorites, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE)
		ePublicEntryID = ePE_Favorites;
	else if(ComparePublicEntryId(ePE_PublicFolders, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE)
		ePublicEntryID = ePE_PublicFolders;
	else if (lpEntryID && (lpEntryID->abFlags[3] & KOPANO_FAVORITE)) {
		ePublicEntryID = ePE_FavoriteSubFolder;

		// Replace the original entryid because this one is only readable
		if ((hr = MAPIAllocateBuffer(cbEntryID, (void**)&lpEntryIDIntern)) != hrSuccess)
			goto exit;
		memcpy(lpEntryIDIntern, lpEntryID, cbEntryID);

		// Remove Flags intern
		lpEntryIDIntern->abFlags[3] &= ~KOPANO_FAVORITE;

		lpEntryID = lpEntryIDIntern;

	}
	
	hr = HrGetObjTypeFromEntryId(cbEntryID, (LPBYTE)lpEntryID, &ulObjType);
	if(hr != hrSuccess)
		goto exit;

	if (ulObjType != MAPI_FOLDER && ePublicEntryID != ePE_FavoriteSubFolder)
	{
		// Open online Messages
		hr = ECMsgStore::OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
		if (hr != hrSuccess)
			goto exit;

		// Message is open, now we can exit
		goto exit;
	}

	switch( ulObjType ) {
	case MAPI_FOLDER:

		if (ePublicEntryID == ePE_PublicFolders) {

			hr = MAPIAllocateBuffer(sizeof(SPropValue), (void**)&lpsPropValue);
			if(hr != hrSuccess)
				goto exit;

			// Get the online Subtree entryid
			hr = HrGetRealProp(PR_IPM_SUBTREE_ENTRYID, 0, lpsPropValue, lpsPropValue);
			if(hr != hrSuccess)
				goto exit;

			cbEntryID = lpsPropValue->Value.bin.cb;
			lpEntryID = (LPENTRYID)lpsPropValue->Value.bin.lpb;
		}

		if (ePublicEntryID != ePE_IPMSubtree && ePublicEntryID != ePE_Favorites) {
			hr = lpTransport->HrOpenFolderOps(cbEntryID, lpEntryID, &lpFolderOps);
			if(hr != hrSuccess)
				goto exit;
		} else {
			lpFolderOps = NULL;
		}

		hr = ECMAPIFolderPublic::Create(this, fModifyObject, lpFolderOps, ePublicEntryID, &lpMAPIFolder);
		if(hr != hrSuccess)
			goto exit;

		if (ePublicEntryID != ePE_IPMSubtree && ePublicEntryID != ePE_Favorites) {
			//FIXME: Wrong parent entryid
			hr = lpTransport->HrOpenPropStorage(m_cbEntryId, m_lpEntryId, cbEntryID, lpEntryID, ulFlags&(SHOW_SOFT_DELETES), &lpPropStorage);
			if(hr != hrSuccess)
				goto exit;

			hr = lpMAPIFolder->HrSetPropStorage(lpPropStorage, TRUE);
			if(hr != hrSuccess)
				goto exit;

			//if(ePublicEntryID == ePE_FavoriteSubFolder)
				//lpEntryID->abFlags[3] = KOPANO_FAVORITE;
		} else {
			lpMAPIFolder->HrLoadEmptyProps();
		}

		hr = lpMAPIFolder->SetEntryId(cbEntryID, lpEntryID);
		if(hr != hrSuccess)
			goto exit;

		MAPIFreeBuffer(lpsPropValue);
		lpsPropValue = NULL;

		// Get the parent entryid of a folder an check if this is the online subtree entryid. When it is, 
		// change the parent to the static parent entryid
		hr = MAPIAllocateBuffer(sizeof(SPropValue), (void**)&lpsPropValue);
		if(hr != hrSuccess)
			goto exit;

		if (HrGetOneProp((LPMAPIPROP)(&lpMAPIFolder->m_xMAPIFolder), PR_PARENT_ENTRYID, &lpParentProp) == hrSuccess &&
			HrGetRealProp(PR_IPM_SUBTREE_ENTRYID, 0, lpsPropValue, lpsPropValue) == hrSuccess &&
			CompareEntryIDs(lpsPropValue->Value.bin.cb, (LPENTRYID)lpsPropValue->Value.bin.lpb, lpParentProp->Value.bin.cb, (LPENTRYID)lpParentProp->Value.bin.lpb, 0, &ulResults) == hrSuccess &&
			ulResults == TRUE)
		{
			if (lpMAPIFolder)
				lpMAPIFolder->SetParentID(this->m_cIPMPublicFoldersID, this->m_lpIPMPublicFoldersID);
		}

		MAPIFreeBuffer(lpParentProp);
		lpParentProp = NULL;
		MAPIFreeBuffer(lpsPropValue);
		lpsPropValue = NULL;
		AddChild(lpMAPIFolder);

		if(lpInterface)
			hr = lpMAPIFolder->QueryInterface(*lpInterface,(void **)lppUnk);
		else
			hr = lpMAPIFolder->QueryInterface(IID_IMAPIFolder, (void **)lppUnk);

		if(lpulObjType)
			*lpulObjType = MAPI_FOLDER;

		break;
	case MAPI_MESSAGE:
		//FIXME: change for offline support
		hr = ECMsgStore::OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
		if (hr != hrSuccess)
			goto exit;
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		goto exit;

	}

exit:
	if (lpFolderOps)
		lpFolderOps->Release();

	if (lpMAPIFolder)
		lpMAPIFolder->Release();

	if (lpPropStorage)
		lpPropStorage->Release();
	MAPIFreeBuffer(lpsPropValue);
	MAPIFreeBuffer(lpEntryIDIntern);
	MAPIFreeBuffer(lpParentProp);
	return hr;
}

HRESULT ECMsgStorePublic::InitEntryIDs()
{
	HRESULT hr;

	if (m_lpIPMSubTreeID == NULL) {
		hr = ::GetPublicEntryId(ePE_IPMSubtree, GetStoreGuid(), NULL, &m_cIPMSubTreeID, &m_lpIPMSubTreeID);
		if(hr != hrSuccess)
			return hr;
	}

	if (m_lpIPMPublicFoldersID == NULL) {
		hr = ::GetPublicEntryId(ePE_PublicFolders, GetStoreGuid(), NULL, &m_cIPMPublicFoldersID, &m_lpIPMPublicFoldersID);
		if(hr != hrSuccess)
			return hr;
	}

	if (m_lpIPMFavoritesID == NULL) {
		hr = ::GetPublicEntryId(ePE_Favorites, GetStoreGuid(), NULL, &m_cIPMFavoritesID, &m_lpIPMFavoritesID);
		if(hr != hrSuccess)
			return hr;
	}

	return hrSuccess;
}

HRESULT ECMsgStorePublic::GetPublicEntryId(enumPublicEntryID ePublicEntryID, void *lpBase, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	HRESULT hr = hrSuccess;
	ULONG cbPublicID = 0;
	LPENTRYID lpPublicID = NULL;
	LPENTRYID lpEntryID = NULL;

	hr = InitEntryIDs();
	if(hr != hrSuccess)
		goto exit;

	if (lpcbEntryID == NULL || lppEntryID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

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
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
	}

	if (lpBase)
		hr = MAPIAllocateMore(cbPublicID, lpBase, (void**)&lpEntryID);
	else
		hr = MAPIAllocateBuffer(cbPublicID, (void**)&lpEntryID);
	if (hr != hrSuccess)
		goto exit;

	memcpy(lpEntryID, lpPublicID, cbPublicID);

	*lpcbEntryID = cbPublicID;
	*lppEntryID = lpEntryID;

exit:
	return hr;
}

HRESULT ECMsgStorePublic::ComparePublicEntryId(enumPublicEntryID ePublicEntryID, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG *lpulResult)
{
	HRESULT hr;
	ULONG ulResult = 0;
	ULONG cbPublicID = 0;
	LPENTRYID lpPublicID = NULL;

	hr = InitEntryIDs();
	if(hr != hrSuccess)
		return hr;

	if (lpEntryID == NULL || lpulResult == NULL)
		return MAPI_E_INVALID_PARAMETER;

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
	HRESULT hr = hrSuccess;
	ECMemTable *lpIPMSubTree = NULL;
	LPSPropValue lpProps = NULL;
	ULONG cProps = 0;
	ULONG cMaxProps = 0;
	ULONG ulRowId = 0;
	SPropValue sKeyProp;

	SizedSPropTagArray(13, sPropsHierarchyColumns) = {13, {
			PR_ENTRYID, PR_DISPLAY_NAME_W,
			PR_CONTENT_COUNT, PR_CONTENT_UNREAD,
			PR_STORE_ENTRYID, PR_STORE_RECORD_KEY,
			PR_STORE_SUPPORT_MASK, PR_INSTANCE_KEY,
			PR_RECORD_KEY, PR_ACCESS, PR_ACCESS_LEVEL,
			PR_OBJECT_TYPE, PR_FOLDER_TYPE} };

	if (m_lpIPMSubTree != NULL){
		ASSERT(FALSE);
		goto exit;
	}

	hr = ECMemTable::Create((LPSPropTagArray)&sPropsHierarchyColumns, PR_ROWID, &lpIPMSubTree);
	if(hr != hrSuccess)
		goto exit;

	//  Favorites
	ulRowId = 1;
	cMaxProps = 22;
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * cMaxProps, (void**)&lpProps);
	if(hr != hrSuccess)
		goto exit;
	
	lpProps[cProps].ulPropTag = PR_ENTRYID;
	hr = GetPublicEntryId(ePE_Favorites, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		goto exit;
	++cProps;

	lpProps[cProps].ulPropTag = PR_LONGTERM_ENTRYID_FROM_TABLE;
	hr = GetPublicEntryId(ePE_Favorites, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		goto exit;
	++cProps;

	lpProps[cProps].ulPropTag = PR_DISPLAY_TYPE;
	lpProps[cProps++].Value.ul = DT_FOLDER;

	lpProps[cProps].ulPropTag = PR_DEPTH;
	lpProps[cProps++].Value.ul = 1;

	lpProps[cProps].ulPropTag = PR_PARENT_ENTRYID;
	hr = GetPublicEntryId(ePE_IPMSubtree, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;
	++cProps;

	lpProps[cProps].ulPropTag = PR_DISPLAY_NAME_W;
	lpProps[cProps++].Value.lpszW = _W("Favorites"); // FIXME: Use dynamic name, read from global profile (like exchange)

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
	hr = MAPIAllocateMore(lpProps[cProps].Value.bin.cb, lpProps, (void**)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		goto exit;

	memset(lpProps[cProps].Value.bin.lpb, 0, lpProps[cProps].Value.bin.cb );
	memcpy(lpProps[cProps].Value.bin.lpb, &ulRowId, sizeof(ULONG));
	++cProps;

	lpProps[cProps].ulPropTag = PR_RECORD_KEY;
	hr = GetPublicEntryId(ePE_Favorites, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		goto exit;
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
		goto exit;

	ASSERT(cProps <= cMaxProps);

	MAPIFreeBuffer(lpProps);
	lpProps = NULL;

	// the folder "Public Folders"
	++ulRowId;
	cProps = 0;
	cMaxProps = 20;

	hr = MAPIAllocateBuffer(sizeof(SPropValue) * cMaxProps, (void**)&lpProps);
	if(hr != hrSuccess)
		goto exit;
	
	lpProps[cProps].ulPropTag = PR_ENTRYID;
	hr = ((ECMsgStorePublic*)GetMsgStore())->GetPublicEntryId(ePE_PublicFolders, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		goto exit;
	++cProps;
	
	lpProps[cProps].ulPropTag = PR_LONGTERM_ENTRYID_FROM_TABLE;
	hr = GetPublicEntryId(ePE_PublicFolders, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		goto exit;
	++cProps;

	lpProps[cProps].ulPropTag = PR_DISPLAY_TYPE;
	lpProps[cProps++].Value.ul = DT_FOLDER;

	lpProps[cProps].ulPropTag = PR_DEPTH;
	lpProps[cProps++].Value.ul = 1;

	lpProps[cProps].ulPropTag = PR_PARENT_ENTRYID;
	hr = GetPublicEntryId(ePE_IPMSubtree, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;
	++cProps;

	lpProps[cProps].ulPropTag = PR_DISPLAY_NAME_W;
	lpProps[cProps++].Value.lpszW = _W("Public Folders"); // FIXME: Use dynamic name, read from global profile (like exchange)

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
	hr = MAPIAllocateMore(lpProps[cProps].Value.bin.cb, lpProps, (void**)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		goto exit;

	memset(lpProps[cProps].Value.bin.lpb, 0, lpProps[cProps].Value.bin.cb );
	memcpy(lpProps[cProps].Value.bin.lpb, &ulRowId, sizeof(ULONG));
	++cProps;

	lpProps[cProps].ulPropTag = PR_RECORD_KEY;
	hr = GetPublicEntryId(ePE_PublicFolders, lpProps, &lpProps[cProps].Value.bin.cb, (LPENTRYID*)&lpProps[cProps].Value.bin.lpb);
	if(hr != hrSuccess)
		goto exit;
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
		goto exit;

	ASSERT(cProps <= cMaxProps);

	MAPIFreeBuffer(lpProps);
	lpProps = NULL;

	m_lpIPMSubTree = lpIPMSubTree;

exit:
	MAPIFreeBuffer(lpProps);
	return hr;
}

ECMemTable *ECMsgStorePublic::GetIPMSubTree()
{
	ASSERT(m_lpIPMSubTree != NULL);

	return m_lpIPMSubTree;
}

HRESULT ECMsgStorePublic::GetDefaultShortcutFolder(IMAPIFolder** lppFolder)
{
	HRESULT hr = hrSuccess;
	ULONG ulObjType;
	IMAPIFolder *lpFolder = NULL;
	IMsgStore *lpMsgStore = NULL;
	LPSPropValue lpPropValue = NULL;
	ULONG cbEntryId;
	LPENTRYID lpEntryId = NULL;
	LPENTRYID lpStoreEntryID = NULL;
	ULONG cbStoreEntryID;
	string strRedirServer;
	WSTransport *lpTmpTransport = NULL;

	if (m_lpDefaultMsgStore == NULL)
	{
		// Get the default store for this user
		hr = lpTransport->HrGetStore(0, NULL, &cbStoreEntryID, &lpStoreEntryID, NULL, NULL, &strRedirServer);
		if (hr == MAPI_E_UNABLE_TO_COMPLETE) {
			// reopen store of user which is on another server
			hr = lpTransport->CreateAndLogonAlternate(strRedirServer.c_str(), &lpTmpTransport);
			if (hr != hrSuccess)
				goto exit;

			hr = lpTmpTransport->HrGetStore(0, NULL, &cbStoreEntryID, &lpStoreEntryID, NULL, NULL);
		}
 		if(hr != hrSuccess)
			goto exit;

		hr = WrapStoreEntryID(0, (LPTSTR)WCLIENT_DLL_NAME, cbStoreEntryID, lpStoreEntryID, &cbEntryId, &lpEntryId);
		if(hr != hrSuccess)
			goto exit;

		// Open default store
		hr = lpSupport->OpenEntry(cbEntryId, lpEntryId, &IID_IMsgStore, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN *)&lpMsgStore);
 		if(hr != hrSuccess) 
			goto exit;

		hr = lpMsgStore->QueryInterface(IID_IMsgStore, (void**)&m_lpDefaultMsgStore);
		if (hr != hrSuccess)
			goto exit;
	}

	// Get shortcut entryid
	hr = HrGetOneProp(m_lpDefaultMsgStore, PR_IPM_FAVORITES_ENTRYID, &lpPropValue);
	if(hr != hrSuccess)
		goto exit;

	// Open Shortcut folder
	hr = m_lpDefaultMsgStore->OpenEntry(lpPropValue->Value.bin.cb, (LPENTRYID)lpPropValue->Value.bin.lpb, &IID_IMAPIFolder, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN *)&lpFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = lpFolder->QueryInterface(IID_IMAPIFolder, (void**)lppFolder);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (lpTmpTransport) {
		lpTmpTransport->HrLogOff();
		lpTmpTransport->Release();
	}
	MAPIFreeBuffer(lpStoreEntryID);
	MAPIFreeBuffer(lpEntryId);
	MAPIFreeBuffer(lpPropValue);
	if (lpFolder)
		lpFolder->Release();

	if (lpMsgStore)
		lpMsgStore->Release();

	return hr;
}

HRESULT ECMsgStorePublic::Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection)
{
	HRESULT hr = hrSuccess;
	ULONG ulResult = 0;
	LPENTRYID lpEntryIDIntern = NULL;

	if(ComparePublicEntryId(ePE_IPMSubtree, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE) {
		hr = MAPI_E_NO_SUPPORT; // FIXME
		goto exit;
	} else if(ComparePublicEntryId(ePE_Favorites, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE) {
		hr = MAPI_E_NO_SUPPORT; // FIXME
		goto exit;
	} else if(ComparePublicEntryId(ePE_PublicFolders, cbEntryID, lpEntryID, &ulResult) == hrSuccess && ulResult == TRUE) {
		hr = MAPI_E_NO_SUPPORT; // FIXME
		goto exit;
	} else if (lpEntryID && (lpEntryID->abFlags[3] & KOPANO_FAVORITE)) {
		// Replace the original entryid because this one is only readable
		if ((hr = MAPIAllocateBuffer(cbEntryID, (void**)&lpEntryIDIntern)) != hrSuccess)
			goto exit;
		memcpy(lpEntryIDIntern, lpEntryID, cbEntryID);

		// Remove Flags intern
		lpEntryIDIntern->abFlags[3] &= ~KOPANO_FAVORITE;

		lpEntryID = lpEntryIDIntern;
	}

	hr = ECMsgStore::Advise(cbEntryID, lpEntryID, ulEventMask, lpAdviseSink, lpulConnection);

exit:
	MAPIFreeBuffer(lpEntryIDIntern);
	return hr;
}
