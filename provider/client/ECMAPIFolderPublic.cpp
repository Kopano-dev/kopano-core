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
#include <kopano/memory.hpp>
#include "ECMAPIFolderPublic.h"

#include "Mem.h"
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include "ClientUtil.h"
#include "pcutil.hpp"
#include <edkmdb.h>
#include <kopano/mapiext.h>

#include <kopano/stringutil.h>
#include "ECMsgStorePublic.h"

#include <kopano/charset/convstring.h>

#include <kopano/ECGetText.h>
#include <mapiutil.h>

using namespace KC;

ECMAPIFolderPublic::ECMAPIFolderPublic(ECMsgStore *lpMsgStore, BOOL modify,
    WSMAPIFolderOps *ops, enumPublicEntryID ePublicEntryID) :
	ECMAPIFolder(lpMsgStore, modify, ops, "IMAPIFolderPublic"),
	m_ePublicEntryID(ePublicEntryID)
{
	HrAddPropHandlers(PR_ACCESS, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_ACCESS_LEVEL, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_RIGHTS, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_ENTRYID, GetPropHandler, DefaultSetPropComputed, this);
	// FIXME: special for publicfolders, save in global profile
	HrAddPropHandlers(PR_DISPLAY_NAME, GetPropHandler, SetPropHandler, this);
	HrAddPropHandlers(PR_COMMENT, GetPropHandler, SetPropHandler, this);
	HrAddPropHandlers(PR_RECORD_KEY, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_PARENT_ENTRYID,GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_FOLDER_TYPE, GetPropHandler, DefaultSetPropSetReal, this);
	HrAddPropHandlers(PR_FOLDER_CHILD_COUNT, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_SUBFOLDERS, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_ORIGINAL_ENTRYID, GetPropHandler, DefaultSetPropComputed, this, false, true);
}

HRESULT	ECMAPIFolderPublic::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMAPIFolderPublic, this);
	return ECMAPIFolder::QueryInterface(refiid, lppInterface);
}

HRESULT ECMAPIFolderPublic::Create(ECMsgStore *lpMsgStore, BOOL fModify, WSMAPIFolderOps *lpFolderOps, enumPublicEntryID ePublicEntryID, ECMAPIFolder **lppECMAPIFolder)
{
	return alloc_wrap<ECMAPIFolderPublic>(lpMsgStore, fModify,
	       lpFolderOps, ePublicEntryID)
	       .as(IID_ECMAPIFolder, reinterpret_cast<void **>(lppECMAPIFolder));
}

HRESULT ECMAPIFolderPublic::GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT hr = hrSuccess;
	LPCTSTR lpszName = NULL;
	auto lpFolder = static_cast<ECMAPIFolderPublic *>(lpParam);

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_FOLDER_TYPE):
		if (lpFolder->m_ePublicEntryID == ePE_PublicFolders || lpFolder->m_ePublicEntryID == ePE_IPMSubtree || lpFolder->m_ePublicEntryID == ePE_Favorites) {
			lpsPropValue->ulPropTag = PR_FOLDER_TYPE;
			lpsPropValue->Value.l = FOLDER_GENERIC;
		} else {
			hr = lpFolder->HrGetRealProp(PR_FOLDER_TYPE, ulFlags, lpBase, lpsPropValue);
		}
		break;
	case PROP_ID(PR_ACCESS):
		// FIXME: use MAPI_ACCESS_MODIFY for favorites, public, favo sub  folders to change the displayname and comment
		if (lpFolder->m_ePublicEntryID == ePE_IPMSubtree) {
			lpsPropValue->ulPropTag = PR_ACCESS;
			lpsPropValue->Value.l = MAPI_ACCESS_READ;
			break;
		}else if (lpFolder->m_ePublicEntryID == ePE_Favorites) {
			lpsPropValue->ulPropTag = PR_ACCESS;
			lpsPropValue->Value.l = MAPI_ACCESS_READ;
			break;
		}
		hr = lpFolder->HrGetRealProp(PR_ACCESS, ulFlags, lpBase, lpsPropValue);
		if (hr == hrSuccess && lpFolder->m_ePublicEntryID == ePE_FavoriteSubFolder)
			lpsPropValue->Value.l |= MAPI_ACCESS_READ | MAPI_ACCESS_DELETE;
		else if (hr == hrSuccess && lpFolder->m_ePublicEntryID == ePE_PublicFolders)
			lpsPropValue->Value.l &= ~(MAPI_ACCESS_CREATE_CONTENTS | MAPI_ACCESS_CREATE_ASSOCIATED);
		break;
	case PROP_ID(PR_ACCESS_LEVEL):
		if (lpFolder->m_ePublicEntryID == ePE_IPMSubtree || lpFolder->m_ePublicEntryID == ePE_FavoriteSubFolder) {
			lpsPropValue->ulPropTag = PR_ACCESS_LEVEL;
			lpsPropValue->Value.l = MAPI_MODIFY;
		} else if(lpFolder->m_ePublicEntryID == ePE_Favorites) {
			lpsPropValue->ulPropTag = PR_ACCESS_LEVEL;
			lpsPropValue->Value.l = 0;
		} else {
			hr = lpFolder->HrGetRealProp(PR_ACCESS_LEVEL, ulFlags, lpBase, lpsPropValue);
		}
		break;
	case PROP_ID(PR_RIGHTS):
		if (lpFolder->m_ePublicEntryID == ePE_IPMSubtree) {
			lpsPropValue->ulPropTag = PR_RIGHTS;
			lpsPropValue->Value.l = ecRightsFolderVisible | ecRightsReadAny;
		} else if (lpFolder->m_ePublicEntryID == ePE_Favorites) {
			lpsPropValue->ulPropTag = PR_RIGHTS;
			lpsPropValue->Value.l = ecRightsAll;
		} else if (lpFolder->m_ePublicEntryID == ePE_PublicFolders) {
			lpsPropValue->ulPropTag = PR_RIGHTS;
			lpsPropValue->Value.l = ecRightsAll &~ecRightsCreate;
		} else {
			hr = lpFolder->HrGetRealProp(PR_RIGHTS, ulFlags, lpBase, lpsPropValue);
		}
		break;
	case PROP_ID(PR_ENTRYID):
		if (lpFolder->m_ePublicEntryID == ePE_PublicFolders) {
			lpsPropValue->ulPropTag = PR_ENTRYID;
			hr = ::GetPublicEntryId(ePE_PublicFolders, ((ECMsgStorePublic*)lpFolder->GetMsgStore())->GetStoreGuid(), lpBase, &lpsPropValue->Value.bin.cb, (LPENTRYID*)&lpsPropValue->Value.bin.lpb);
		} else {
			hr = ECGenericProp::DefaultGetProp(PR_ENTRYID, lpProvider, ulFlags, lpsPropValue, lpParam, lpBase);
			if(hr == hrSuccess && lpFolder->m_ePublicEntryID == ePE_FavoriteSubFolder)
				((LPENTRYID)lpsPropValue->Value.bin.lpb)->abFlags[3] = KOPANO_FAVORITE;
		}
		break;
	case PROP_ID(PR_DISPLAY_NAME): {
		// FIXME: Should be from the global profile and/or gettext (PR_FAVORITES_DEFAULT_NAME)
		if (lpFolder->m_ePublicEntryID == ePE_PublicFolders)
			lpszName = _("Public Folders");
		else if (lpFolder->m_ePublicEntryID == ePE_Favorites)
			lpszName = _("Favorites");
		else if (lpFolder->m_ePublicEntryID == ePE_IPMSubtree)
			lpszName = KC_T("IPM_SUBTREE");

		if (lpszName == nullptr) {
			hr = lpFolder->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
			break;
		}
		if (PROP_TYPE(ulPropTag) == PT_UNICODE) {
			const std::wstring strTmp = convert_to<std::wstring>(lpszName);
			hr = MAPIAllocateMore((strTmp.size() + 1) * sizeof(WCHAR), lpBase, (void**)&lpsPropValue->Value.lpszW);
			if (hr != hrSuccess)
				return hr;
			wcscpy(lpsPropValue->Value.lpszW, strTmp.c_str());
			lpsPropValue->ulPropTag = PR_DISPLAY_NAME_W;
			break;
		}
		const std::string strTmp = convert_to<std::string>(lpszName);
		hr = MAPIAllocateMore(strTmp.size() + 1, lpBase, (void**)&lpsPropValue->Value.lpszA);
		if (hr != hrSuccess)
			return hr;
		strcpy(lpsPropValue->Value.lpszA, strTmp.c_str());
		lpsPropValue->ulPropTag = PR_DISPLAY_NAME_A;
		break;
	}
	case PROP_ID(PR_COMMENT):
		// FIXME: load the message class from shortcut (favorite) folder, see setprops
		hr = lpFolder->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_RECORD_KEY):
		// Use entryid as record key because it should be global unique in outlook.
		hr = ECMAPIFolderPublic::GetPropHandler(PR_ENTRYID, lpProvider, ulFlags, lpsPropValue, lpParam, lpBase);
		if (hr != hrSuccess)
			break;
		if (lpFolder->m_ePublicEntryID == ePE_FavoriteSubFolder)
			((LPENTRYID)lpsPropValue->Value.bin.lpb)->abFlags[3] = KOPANO_FAVORITE;
		lpsPropValue->ulPropTag = PR_RECORD_KEY;
		break;
	case PROP_ID(PR_PARENT_ENTRYID):
		if (lpFolder->m_ePublicEntryID == ePE_IPMSubtree || lpFolder->m_ePublicEntryID == ePE_PublicFolders || lpFolder->m_ePublicEntryID == ePE_Favorites) {
			lpsPropValue->ulPropTag = PR_PARENT_ENTRYID;
			hr = ::GetPublicEntryId(ePE_IPMSubtree, ((ECMsgStorePublic*)lpFolder->GetMsgStore())->GetStoreGuid(), lpBase, &lpsPropValue->Value.bin.cb, (LPENTRYID*)&lpsPropValue->Value.bin.lpb);
		} else {
			hr = ECMAPIFolder::DefaultMAPIGetProp(PR_PARENT_ENTRYID, lpProvider, ulFlags, lpsPropValue, lpParam, lpBase);
		}
		break;
	case PROP_ID(PR_FOLDER_CHILD_COUNT):
		if (lpFolder->m_ePublicEntryID == ePE_IPMSubtree) {
			lpsPropValue->ulPropTag = PR_FOLDER_CHILD_COUNT;
			lpsPropValue->Value.ul = 2;
		} else {
			hr = ECMAPIFolder::GetPropHandler(PR_FOLDER_CHILD_COUNT, lpProvider, ulFlags, lpsPropValue, lpParam, lpBase);
		}
		break;
	case PROP_ID(PR_SUBFOLDERS):
		if (lpFolder->m_ePublicEntryID == ePE_IPMSubtree) {
			lpsPropValue->ulPropTag = PR_SUBFOLDERS;
			lpsPropValue->Value.b = TRUE;
		} else {
			hr = ECMAPIFolder::GetPropHandler(PR_SUBFOLDERS, lpProvider, ulFlags, lpsPropValue, lpParam, lpBase);
		}
		break;
	case PROP_ID(PR_DISPLAY_TYPE):
		if (lpFolder->m_ePublicEntryID == ePE_FavoriteSubFolder) {
			lpsPropValue->ulPropTag = PR_DISPLAY_TYPE;
			lpsPropValue->Value.ul = DT_FOLDER_LINK;
		}else {
			hr = lpFolder->HrGetRealProp(PR_DISPLAY_TYPE, ulFlags, lpBase, lpsPropValue);
		}
		break;
	case PROP_ID(PR_ORIGINAL_ENTRYID):
		// entryid on the server (only used for "Public Folders" folder)
		if (lpFolder->m_lpEntryId == nullptr)
			return MAPI_E_NOT_FOUND;
		lpsPropValue->Value.bin.cb = lpFolder->m_cbEntryId;
		hr = KAllocCopy(lpFolder->m_lpEntryId, lpFolder->m_cbEntryId, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb), lpBase);
		if (hr != hrSuccess)
			return hr;
		hr = hrSuccess;
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}
	return hr;
}

HRESULT ECMAPIFolderPublic::SetPropHandler(ULONG ulPropTag, void *lpProvider,
    const SPropValue *lpsPropValue, void *lpParam)
{
	HRESULT hr = hrSuccess;
	auto lpFolder = static_cast<ECMAPIFolderPublic *>(lpParam);

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_DISPLAY_NAME):
		if (lpFolder->m_ePublicEntryID == ePE_PublicFolders)
			hr = MAPI_E_COMPUTED;
			// FIXME: save in profile, #define PR_PROFILE_ALLPUB_DISPLAY_NAME    PROP_TAG(PT_STRING8, pidProfileMin+0x16)
		else if (lpFolder->m_ePublicEntryID == ePE_Favorites)
			hr = MAPI_E_COMPUTED;
			// FIXME: save in profile, #define PR_PROFILE_FAVFLD_DISPLAY_NAME    PROP_TAG(PT_STRING8, pidProfileMin+0x0F)
		else if (lpFolder->m_ePublicEntryID == ePE_FavoriteSubFolder)
			hr = MAPI_E_COMPUTED;
			// FIXME: Save the property to private shortcut folder message
		else
			hr = lpFolder->HrSetRealProp(lpsPropValue);
		break;
	case PROP_ID(PR_COMMENT):
		if (lpFolder->m_ePublicEntryID == ePE_PublicFolders)
			hr = MAPI_E_COMPUTED;
			// FIXME: save in profile, #define PR_PROFILE_FAVFLD_COMMENT        PROP_TAG(PT_STRING8, pidProfileMin+0x15)
		else if (lpFolder->m_ePublicEntryID == ePE_Favorites)
			hr = MAPI_E_COMPUTED;
			// FIXME: save in profile, #define PR_PROFILE_FAVFLD_COMMENT        PROP_TAG(PT_STRING8, pidProfileMin+0x15)
		else
			hr = lpFolder->HrSetRealProp(lpsPropValue);
		break;
	
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}

	return hr;
}

HRESULT ECMAPIFolderPublic::GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	object_ptr<ECMemTable> lpMemTable;
	object_ptr<ECMemTableView> lpView;
	memory_ptr<SPropTagArray> lpPropTagArray;
	SizedSPropTagArray(11, sPropsContentColumns) =
		{11, {PR_ENTRYID, PR_DISPLAY_NAME, PR_MESSAGE_FLAGS, PR_SUBJECT,
		PR_STORE_ENTRYID, PR_STORE_RECORD_KEY, PR_STORE_SUPPORT_MASK,
		PR_INSTANCE_KEY, PR_RECORD_KEY, PR_ACCESS, PR_ACCESS_LEVEL}};

	if (m_ePublicEntryID != ePE_IPMSubtree && m_ePublicEntryID != ePE_Favorites)
		return ECMAPIFolder::GetContentsTable(ulFlags, lppTable);
	if (ulFlags & SHOW_SOFT_DELETES)
		return MAPI_E_NO_SUPPORT;
	Util::proptag_change_unicode(ulFlags, sPropsContentColumns);
	auto hr = ECMemTable::Create(sPropsContentColumns, PR_ROWID, &~lpMemTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpMemTable->HrGetView(createLocaleFromName(""), ulFlags & MAPI_UNICODE, &~lpView);
	if (hr != hrSuccess)
		return hr;
	return lpView->QueryInterface(IID_IMAPITable, reinterpret_cast<void **>(lppTable));
}

HRESULT ECMAPIFolderPublic::GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	object_ptr<ECMemTableView> lpView;

	if( m_ePublicEntryID == ePE_IPMSubtree)
	{
		// FIXME: if exchange support CONVENIENT_DEPTH than we must implement this
		if (ulFlags & (SHOW_SOFT_DELETES | CONVENIENT_DEPTH))
			return MAPI_E_NO_SUPPORT;
		auto hr = static_cast<ECMsgStorePublic *>(GetMsgStore())->GetIPMSubTree()->HrGetView(createLocaleFromName(""), ulFlags, &~lpView);
		if(hr != hrSuccess)
			return hr;
		return lpView->QueryInterface(IID_IMAPITable, (void **)lppTable);
	} else if( m_ePublicEntryID == ePE_Favorites || m_ePublicEntryID == ePE_FavoriteSubFolder) {
		return MAPI_E_NO_SUPPORT;
	} else {
		return ECMAPIFolder::GetHierarchyTable(ulFlags, lppTable);
	}
}

HRESULT ECMAPIFolderPublic::SaveChanges(ULONG ulFlags)
{
	HRESULT hr = hrSuccess;

	// Nothing to do

	return hr;
}

HRESULT ECMAPIFolderPublic::SetProps(ULONG cValues,
    const SPropValue *lpPropArray, SPropProblemArray **lppProblems)
{
	HRESULT hr;

	hr = ECMAPIContainer::SetProps(cValues, lpPropArray, lppProblems);
	if (hr != hrSuccess)
		return hr;

	if (lpStorage)
	{
		hr = ECMAPIContainer::SaveChanges(KEEP_OPEN_READWRITE);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT ECMAPIFolderPublic::DeleteProps(const SPropTagArray *lpPropTagArray,
    SPropProblemArray **lppProblems)
{
	HRESULT hr;

	hr = ECMAPIContainer::DeleteProps(lpPropTagArray, lppProblems);
	if (hr != hrSuccess)
		return hr;
	if (lpStorage == nullptr)
		return hrSuccess;
	return ECMAPIContainer::SaveChanges(KEEP_OPEN_READWRITE);
}

HRESULT ECMAPIFolderPublic::OpenEntry(ULONG cbEntryID, const ENTRYID *eid,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	unsigned int objtype = 0;
	memory_ptr<ENTRYID> lpEntryID;
	auto hr = KAllocCopy(eid, cbEntryID, &~lpEntryID);
	if (hr != hrSuccess)
		return hr;
	if (cbEntryID > 0) {
		hr = HrGetObjTypeFromEntryId(cbEntryID, reinterpret_cast<BYTE *>(lpEntryID.get()), &objtype);
		if(hr != hrSuccess)
			return hr;
		if (objtype == MAPI_FOLDER && m_ePublicEntryID == ePE_FavoriteSubFolder)
			lpEntryID->abFlags[3] = KOPANO_FAVORITE;
	}
	return ECMAPIFolder::OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
}

HRESULT ECMAPIFolderPublic::SetEntryId(ULONG cbEntryId, const ENTRYID *lpEntryId)
{
	if (m_ePublicEntryID == ePE_Favorites || m_ePublicEntryID == ePE_IPMSubtree)
		return ECGenericProp::SetEntryId(cbEntryId, lpEntryId);
	// With notification handler
	return ECMAPIFolder::SetEntryId(cbEntryId, lpEntryId);
}

// @note if you change this function please look also at ECMAPIFolder::CopyFolder
HRESULT ECMAPIFolderPublic::CopyFolder(ULONG cbEntryID,
    const ENTRYID *lpEntryID, const IID *lpInterface, void *lpDestFolder,
    const TCHAR *lpszNewFolderName, ULONG_PTR ulUIParam,
    IMAPIProgress *lpProgress, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	ULONG ulResult = 0;
	object_ptr<IMAPIFolder> lpMapiFolder;
	ecmem_ptr<SPropValue> lpPropArray;
	GUID guidDest;
	GUID guidFrom;

	//Get the interface of destinationfolder
	if(lpInterface == NULL || *lpInterface == IID_IMAPIFolder)
		lpMapiFolder.reset(static_cast<IMAPIFolder *>(lpDestFolder));
	else if(*lpInterface == IID_IMAPIContainer)
		hr = ((IMAPIContainer*)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else if(*lpInterface == IID_IUnknown)
		hr = ((IUnknown*)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else if(*lpInterface == IID_IMAPIProp)
		hr = ((IMAPIProp*)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else
		hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
	
	if(hr != hrSuccess)
		return hr;

	// Get the destination entry ID
	hr = HrGetOneProp(lpMapiFolder, PR_ENTRYID, &~lpPropArray);
	if(hr != hrSuccess)
		return hr;

	// Check if it's  the same store of kopano so we can copy/move fast
	if (!IsKopanoEntryId(cbEntryID, (LPBYTE)lpEntryID) ||
	    !IsKopanoEntryId(lpPropArray[0].Value.bin.cb, lpPropArray[0].Value.bin.lpb) ||
	    HrGetStoreGuidFromEntryId(cbEntryID, reinterpret_cast<const BYTE *>(lpEntryID), &guidFrom) != hrSuccess ||
	    HrGetStoreGuidFromEntryId(lpPropArray[0].Value.bin.cb, lpPropArray[0].Value.bin.lpb, &guidDest) != hrSuccess ||
	    memcmp(&guidFrom, &guidDest, sizeof(GUID)) != 0 ||
	    lpFolderOps == nullptr)
		// Support object handled de copy/move
		return GetMsgStore()->lpSupport->CopyFolder(&IID_IMAPIFolder,
		       static_cast<IMAPIFolder *>(this), cbEntryID, lpEntryID,
		       lpInterface, lpDestFolder, lpszNewFolderName, ulUIParam,
		       lpProgress, ulFlags);

	// if the entryid a a publicfolders entryid just change the entryid to a server entryid
	if (((ECMsgStorePublic *)GetMsgStore())->ComparePublicEntryId(ePE_PublicFolders, lpPropArray[0].Value.bin.cb, (LPENTRYID)lpPropArray[0].Value.bin.lpb, &ulResult) == hrSuccess && ulResult == TRUE)
	{
		hr = HrGetOneProp(lpMapiFolder, PR_ORIGINAL_ENTRYID, &~lpPropArray);
		if(hr != hrSuccess)
			return hr;
	}
	//FIXME: Progressbar
	return lpFolderOps->HrCopyFolder(cbEntryID, lpEntryID,
	       lpPropArray[0].Value.bin.cb, reinterpret_cast<ENTRYID *>(lpPropArray[0].Value.bin.lpb),
	       convstring(lpszNewFolderName, ulFlags), ulFlags, 0);
}

HRESULT ECMAPIFolderPublic::DeleteFolder(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulUIParam, IMAPIProgress *lpProgress,
    ULONG ulFlags)
{
	memory_ptr<SPropValue> lpProp;
	if (!ValidateZEntryId(cbEntryID, reinterpret_cast<const BYTE *>(lpEntryID), MAPI_FOLDER))
		return MAPI_E_INVALID_ENTRYID;
	if (cbEntryID <= 4 || !(lpEntryID->abFlags[3] & KOPANO_FAVORITE))
		return ECMAPIFolder::DeleteFolder(cbEntryID, lpEntryID,
		       ulUIParam, lpProgress, ulFlags);

	// favorite folder not supported
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMAPIFolderPublic::CopyMessages(LPENTRYLIST lpMsgList, LPCIID lpInterface, LPVOID lpDestFolder, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	if (lpMsgList == nullptr || lpMsgList->cValues == 0)
		return hrSuccess;
	if (lpMsgList->lpbin == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = hrSuccess;
	ULONG ulResult = 0;
	object_ptr<IMAPIFolder> lpMapiFolder;
	memory_ptr<SPropValue> lpPropArray;

	//Get the interface of destinationfolder
	if(lpInterface == NULL || *lpInterface == IID_IMAPIFolder)
		lpMapiFolder.reset(static_cast<IMAPIFolder *>(lpDestFolder));
	else if(*lpInterface == IID_IMAPIContainer)
		hr = ((IMAPIContainer*)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else if(*lpInterface == IID_IUnknown)
		hr = ((IUnknown*)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else if(*lpInterface == IID_IMAPIProp)
		hr = ((IMAPIProp*)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else
		hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
	
	if(hr != hrSuccess)
		return hr;

	// Get the destination entry ID
	hr = HrGetOneProp(lpMapiFolder, PR_ENTRYID, &~lpPropArray);
	if(hr != hrSuccess)
		return hr;

	// if the destination is the publicfolders entryid, just block
	if(((ECMsgStorePublic*)GetMsgStore())->ComparePublicEntryId(ePE_PublicFolders, lpPropArray[0].Value.bin.cb, (LPENTRYID)lpPropArray[0].Value.bin.lpb, &ulResult) == hrSuccess && ulResult == TRUE)
		return MAPI_E_NO_ACCESS;

	return ECMAPIFolder::CopyMessages(lpMsgList, lpInterface, lpDestFolder, ulUIParam, lpProgress, ulFlags);
}

HRESULT ECMAPIFolderPublic::CreateMessage(LPCIID lpInterface, ULONG ulFlags, LPMESSAGE *lppMessage)
{
	if (m_ePublicEntryID == ePE_PublicFolders)
		return MAPI_E_NO_ACCESS;

	return ECMAPIFolder::CreateMessage(lpInterface, ulFlags, lppMessage);
}
