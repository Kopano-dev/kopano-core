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
#include <memory>
#include <new>
#include <utility>
#include <mapix.h>
#include <mapiutil.h>
#include "StoreHelper.h"
#include <kopano/mapi_ptr.h>

#include <kopano/mapiguidext.h>
#include <kopano/archiver-common.h>

#include <kopano/ECRestriction.h>

namespace KC { namespace helpers {

/**
 * The version of the current search folder restrictions. The version is the same
 * for all three folders, so if one restriction changes, they all get a new version.
 *
 * version 1: The search used as in V1 of the archiver.
 * version 2: Check if the original source key and current sourcekey match when stubbed equals true.
 */
#define ARCHIVE_SEARCH_VERSION 2

const StoreHelper::search_folder_info_t StoreHelper::s_infoSearchFolders[] = {
	{KC_T("Archive"), KC_T("This folder contains messages that are eligible for archiving"), &StoreHelper::SetupSearchArchiveFolder},
	{KC_T("Delete"), KC_T("This folder contains messages that are eligible for deletion"), &StoreHelper::SetupSearchDeleteFolder},
	{KC_T("Stub"), KC_T("This folder contains messages that are eligible for stubbing"), &StoreHelper::SetupSearchStubFolder}
};

/**
 * Create a new StoreHelper object.
 *
 * @param[in]	ptrMsgStore
 *					A MsgStorePtr that points to the message store for which to
 *					create a StoreHelper.
 * @param[out]	lpptrStoreHelper
 *					Pointer to a StoreHelperPtr that will be assigned the address
 *					of the new StoreHelper object.
 */
HRESULT StoreHelper::Create(MsgStorePtr &ptrMsgStore, StoreHelperPtr *lpptrStoreHelper)
{
	HRESULT hr;
	StoreHelperPtr ptrStoreHelper(new(std::nothrow) StoreHelper(ptrMsgStore));
	if (ptrStoreHelper == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	hr = ptrStoreHelper->Init();
	if (hr != hrSuccess)
		return hr;
	
	*lpptrStoreHelper = std::move(ptrStoreHelper);
	return hrSuccess;
}

StoreHelper::StoreHelper(MsgStorePtr &ptrMsgStore)
: MAPIPropHelper(ptrMsgStore.as<MAPIPropPtr>())
, m_ptrMsgStore(ptrMsgStore), m_propmap(8)
{ }

HRESULT StoreHelper::Init()
{
	HRESULT	hr;
	
	hr = MAPIPropHelper::Init();
	if (hr != hrSuccess)
		return hr;
	
	PROPMAP_INIT_NAMED_ID(ARCHIVE_STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidStoreEntryIds)
	PROPMAP_INIT_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidItemEntryIds)
	PROPMAP_INIT_NAMED_ID(ORIGINAL_SOURCEKEY, PT_BINARY, PSETID_Archive, dispidOrigSourceKey)
	PROPMAP_INIT_NAMED_ID(SEARCH_FOLDER_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidSearchFolderEntryIds)
	PROPMAP_INIT_NAMED_ID(STUBBED, PT_BOOLEAN, PSETID_Archive, dispidStubbed)
	PROPMAP_INIT_NAMED_ID(DIRTY, PT_BOOLEAN, PSETID_Archive, dispidDirty)
	PROPMAP_INIT_NAMED_ID(FLAGS, PT_LONG, PSETID_Archive, dispidFlags)
	PROPMAP_INIT_NAMED_ID(VERSION, PT_LONG, PSETID_Archive, dispidVersion)
	PROPMAP_INIT(m_ptrMsgStore)
	return hr;
}

/**
 * Open or optionaly create a folder by name from the IPM subtree.
 *
 * @param[in]	strFolder
 *					The name of the folder to open or create.
 * @param[in]	bCreate
 *					Set to true to create the folder if it doesn't exist.
 * @param[out]	lppFolder
 *					Pointer to a IMapiFolder pointer that will be assigned the address
 *					of the returned folderd.
 */
HRESULT StoreHelper::GetFolder(const tstring &strFolder, bool bCreate, LPMAPIFOLDER *lppFolder)
{
	HRESULT hr;
	MAPIFolderPtr ptrIpmSubtree;
	MAPIFolderPtr ptrFolder;
	
	hr = GetIpmSubtree(&~ptrIpmSubtree);
	if (hr != hrSuccess)
		return hr;
	hr = GetSubFolder(ptrIpmSubtree, strFolder, bCreate, &~ptrFolder);
	if (hr != hrSuccess)
		return hr;
		
	return ptrFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppFolder));
}

/**
 * Create or reinitialize the searchfolders used by the archiver. The search folders
 * need to be updated whenever the set of attached archives for a store change.
 */
HRESULT StoreHelper::UpdateSearchFolders()
{
	HRESULT hr;
	MAPIFolderPtr ptrSearchArchiveFolder;
	MAPIFolderPtr ptrSearchDeleteFolder;
	MAPIFolderPtr ptrSearchStubFolder;
	SPropValuePtr ptrSearchArchiveEntryId;
	SPropValuePtr ptrSearchDeleteEntryId;
	SPropValuePtr ptrSearchStubEntryId;
	SPropValuePtr ptrPropValue;

	hr = CreateSearchFolders(&~ptrSearchArchiveFolder, &~ptrSearchDeleteFolder, &~ptrSearchStubFolder);
	if (hr != hrSuccess)
		return hr;
	
	assert(ptrSearchArchiveFolder);
	assert(ptrSearchDeleteFolder);
	assert(ptrSearchStubFolder);
	hr = HrGetOneProp(ptrSearchArchiveFolder, PR_ENTRYID, &~ptrSearchArchiveEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrSearchDeleteFolder, PR_ENTRYID, &~ptrSearchDeleteEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrSearchStubFolder, PR_ENTRYID, &~ptrSearchStubEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(SPropValue), &~ptrPropValue);
	if (hr != hrSuccess)
		return hr;
		
	ptrPropValue->ulPropTag = PROP_SEARCH_FOLDER_ENTRYIDS;
	ptrPropValue->Value.MVbin.cValues = 3;
	
	hr = MAPIAllocateMore(3 * sizeof(*ptrPropValue->Value.MVbin.lpbin), ptrPropValue, (LPVOID*)&ptrPropValue->Value.MVbin.lpbin);
	if (hr != hrSuccess)
		return hr;
	
	ptrPropValue->Value.MVbin.lpbin[0].cb = ptrSearchArchiveEntryId->Value.bin.cb;
	ptrPropValue->Value.MVbin.lpbin[0].lpb = ptrSearchArchiveEntryId->Value.bin.lpb;
	ptrPropValue->Value.MVbin.lpbin[1].cb = ptrSearchDeleteEntryId->Value.bin.cb;
	ptrPropValue->Value.MVbin.lpbin[1].lpb = ptrSearchDeleteEntryId->Value.bin.lpb;
	ptrPropValue->Value.MVbin.lpbin[2].cb = ptrSearchStubEntryId->Value.bin.cb;
	ptrPropValue->Value.MVbin.lpbin[2].lpb = ptrSearchStubEntryId->Value.bin.lpb;
	return HrSetOneProp(m_ptrMsgStore, ptrPropValue);
}

/**
 * Get the IPM subtree.
 *
 * @param[out]	lppFolder
 *					Pointer to an IMAPIFolder pointer that will be assigned the
 *					address of the returned folder.
 */
HRESULT StoreHelper::GetIpmSubtree(LPMAPIFOLDER *lppFolder)
{
	HRESULT	hr;
	SPropValuePtr ptrPropValue;
	ULONG ulType = 0;

	if (!m_ptrIpmSubtree) {
		hr = HrGetOneProp(m_ptrMsgStore, PR_IPM_SUBTREE_ENTRYID, &~ptrPropValue);
		if (hr != hrSuccess)
			return hr;
		hr = m_ptrMsgStore->OpenEntry(ptrPropValue->Value.bin.cb, reinterpret_cast<ENTRYID *>(ptrPropValue->Value.bin.lpb), &iid_of(m_ptrIpmSubtree), MAPI_BEST_ACCESS | fMapiDeferredErrors, &ulType, &~m_ptrIpmSubtree);
		if (hr != hrSuccess)
			return hr;
	}
	
	return m_ptrIpmSubtree->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppFolder));
}

/**
 * Get the three search folders that are created with StoreHelper::UpdateSearchFolders.
 *
 * @param[out]	lppSearchArchiveFolder
 *					Pointer to an IMAPIFolder pointer that will be assigned the
 *					address of the searcholder containing the messages that are eligible
 * 					for archiving. These are basically unarchived messages that are not
 * 					flagged to be never archived.
 * @param[out]	lppSearchDeleteFolder
 *					Pointer to an IMAPIFolder pointer that will be assigned the
 *					address of the searcholder containing the messages that are eligible
 * 					for deletion. These are basically archived messages that are not flagged
 * 					to be never deleted.
 * @param[out]	lppSearchStubFolder
 *					Pointer to an IMAPIFolder pointer that will be assigned the
 *					address of the searcholder containing the messages that are eligible
 * 					for stubbing. These are basically all non-stubbed, archived messages that
 * 					are not flagged to be never stubbed.
 */
HRESULT StoreHelper::GetSearchFolders(LPMAPIFOLDER *lppSearchArchiveFolder, LPMAPIFOLDER *lppSearchDeleteFolder, LPMAPIFOLDER *lppSearchStubFolder)
{
	HRESULT hr;
	SPropValuePtr ptrPropValue;
	MAPIFolderPtr ptrSearchArchiveFolder;
	MAPIFolderPtr ptrSearchDeleteFolder;
	MAPIFolderPtr ptrSearchStubFolder;	// subset of SearchDeleteFolder.
	ULONG ulType = 0;
	bool bUpdateRef = false;
	
	hr = HrGetOneProp(m_ptrMsgStore, PROP_SEARCH_FOLDER_ENTRYIDS, &~ptrPropValue);
	if (hr != hrSuccess && hr != MAPI_E_NOT_FOUND)
		return hr;
		
	if (hr == hrSuccess && ptrPropValue->Value.MVbin.cValues == 3) {
		hr = m_ptrMsgStore->OpenEntry(ptrPropValue->Value.MVbin.lpbin[0].cb,
		     reinterpret_cast<ENTRYID *>(ptrPropValue->Value.MVbin.lpbin[0].lpb),
		     &iid_of(ptrSearchArchiveFolder), fMapiDeferredErrors | MAPI_BEST_ACCESS,
		     &ulType, &~ptrSearchArchiveFolder);
		if (hr == hrSuccess)
			hr = CheckAndUpdateSearchFolder(ptrSearchArchiveFolder, esfArchive);
		else if (hr == MAPI_E_NOT_FOUND) {
			bUpdateRef = true;
			hr = CreateSearchFolder(esfArchive, &~ptrSearchArchiveFolder);
		}
		if (hr != hrSuccess)
			return hr;
		hr = m_ptrMsgStore->OpenEntry(ptrPropValue->Value.MVbin.lpbin[1].cb,
		     reinterpret_cast<ENTRYID *>(ptrPropValue->Value.MVbin.lpbin[1].lpb),
		     &iid_of(ptrSearchDeleteFolder), fMapiDeferredErrors | MAPI_BEST_ACCESS,
		     &ulType, &~ptrSearchDeleteFolder);
		if (hr == hrSuccess)
			hr = CheckAndUpdateSearchFolder(ptrSearchDeleteFolder, esfDelete);
		else if (hr == MAPI_E_NOT_FOUND) {
			bUpdateRef = true;
			hr = CreateSearchFolder(esfDelete, &~ptrSearchDeleteFolder);
		}
		if (hr != hrSuccess)
			return hr;
		hr = m_ptrMsgStore->OpenEntry(ptrPropValue->Value.MVbin.lpbin[2].cb,
		     reinterpret_cast<ENTRYID *>(ptrPropValue->Value.MVbin.lpbin[2].lpb),
		     &iid_of(ptrSearchStubFolder), fMapiDeferredErrors | MAPI_BEST_ACCESS,
		     &ulType, &~ptrSearchStubFolder);
		if (hr == hrSuccess)
			hr = CheckAndUpdateSearchFolder(ptrSearchStubFolder, esfStub);
		else if (hr == MAPI_E_NOT_FOUND) {
			bUpdateRef = true;
			hr = CreateSearchFolder(esfStub, &~ptrSearchStubFolder);
		}
		if (hr != hrSuccess)
			return hr;
	} else {
		bUpdateRef = true;
		hr = CreateSearchFolders(&~ptrSearchArchiveFolder, &~ptrSearchDeleteFolder, &~ptrSearchStubFolder);
		if (hr != hrSuccess)
			return hr;
	}

	if (bUpdateRef) {
		SPropValuePtr ptrSearchArchiveEntryId;
		SPropValuePtr ptrSearchDeleteEntryId;
		SPropValuePtr ptrSearchStubEntryId;

		assert(ptrSearchArchiveFolder);
		assert(ptrSearchDeleteFolder);
		assert(ptrSearchStubFolder);
		hr = HrGetOneProp(ptrSearchArchiveFolder, PR_ENTRYID, &~ptrSearchArchiveEntryId);
		if (hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(ptrSearchDeleteFolder, PR_ENTRYID, &~ptrSearchDeleteEntryId);
		if (hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(ptrSearchStubFolder, PR_ENTRYID, &~ptrSearchStubEntryId);
		if (hr != hrSuccess)
			return hr;
		hr = MAPIAllocateBuffer(sizeof(SPropValue), &~ptrPropValue);
		if (hr != hrSuccess)
			return hr;
			
		ptrPropValue->ulPropTag = PROP_SEARCH_FOLDER_ENTRYIDS;
		ptrPropValue->Value.MVbin.cValues = 3;
		
		hr = MAPIAllocateMore(3 * sizeof(*ptrPropValue->Value.MVbin.lpbin), ptrPropValue, (LPVOID*)&ptrPropValue->Value.MVbin.lpbin);
		if (hr != hrSuccess)
			return hr;
		
		ptrPropValue->Value.MVbin.lpbin[0].cb = ptrSearchArchiveEntryId->Value.bin.cb;
		ptrPropValue->Value.MVbin.lpbin[0].lpb = ptrSearchArchiveEntryId->Value.bin.lpb;
		ptrPropValue->Value.MVbin.lpbin[1].cb = ptrSearchDeleteEntryId->Value.bin.cb;
		ptrPropValue->Value.MVbin.lpbin[1].lpb = ptrSearchDeleteEntryId->Value.bin.lpb;
		ptrPropValue->Value.MVbin.lpbin[2].cb = ptrSearchStubEntryId->Value.bin.cb;
		ptrPropValue->Value.MVbin.lpbin[2].lpb = ptrSearchStubEntryId->Value.bin.lpb;
		
		hr = HrSetOneProp(m_ptrMsgStore, ptrPropValue);
		if (hr != hrSuccess)
			return hr;
	}
	
	hr = ptrSearchArchiveFolder->QueryInterface(IID_IMAPIFolder, (LPVOID*)lppSearchArchiveFolder);
	if (hr != hrSuccess)
		return hr;
	hr = ptrSearchDeleteFolder->QueryInterface(IID_IMAPIFolder, (LPVOID*)lppSearchDeleteFolder);
	if (hr != hrSuccess)
		return hr;
	
	return ptrSearchStubFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppSearchStubFolder));
}

/**
 * Open or create a subfolder by name from another folder.
 *
 * @param[in]	ptrFolder
 *					MAPIFolderPtr that points to the folder from which a subfolder
 *					will be opened.
 * @param[in]	strFolder
 *					The name of the subfolder to open or create.
 * @param[in]	bCreate
 *					Set to true to create the subfolder if it doesn't exist.
 * @param[out]	lppFolder
 *					Pointer to an IMAPIFolder pointer that will be assigned the
 *					address of the returned folder.
 */
HRESULT StoreHelper::GetSubFolder(MAPIFolderPtr &ptrFolder, const tstring &strFolder, bool bCreate, LPMAPIFOLDER *lppFolder)
{
	HRESULT hr;
	MAPITablePtr ptrTable;
	SRowSetPtr ptrRowSet;
	MAPIFolderPtr ptrSubFolder;
	ULONG ulType = 0;
	static constexpr const SizedSPropTagArray(1, sptaFolderProps) = {1, {PR_ENTRYID}};
	SPropValue				sResPropValue = {0};
	
	sResPropValue.ulPropTag = PR_DISPLAY_NAME;
	sResPropValue.Value.LPSZ = (LPTSTR)strFolder.c_str();
	
	hr = ptrFolder->GetHierarchyTable(fMapiDeferredErrors, &~ptrTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SetColumns(sptaFolderProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ECPropertyRestriction(RELOP_EQ, PR_DISPLAY_NAME, &sResPropValue, ECRestriction::Cheap)
	     .FindRowIn(ptrTable, BOOKMARK_BEGINNING, 0);
	if (hr == hrSuccess) {
		hr = ptrTable->QueryRows(1, TBL_NOADVANCE, &~ptrRowSet);
		if (hr != hrSuccess)
			return hr;
		if (ptrRowSet.size() != 1)
			return MAPI_E_NOT_FOUND;
		hr = m_ptrMsgStore->OpenEntry(ptrRowSet[0].lpProps[0].Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(ptrRowSet[0].lpProps[0].Value.bin.lpb),
		     &iid_of(ptrSubFolder), MAPI_BEST_ACCESS, &ulType, &~ptrSubFolder);
	} else if (hr == MAPI_E_NOT_FOUND && bCreate) {
		hr = ptrFolder->CreateFolder(FOLDER_GENERIC, (LPTSTR)strFolder.c_str(), nullptr, &iid_of(ptrSubFolder), fMapiUnicode, &~ptrSubFolder);
		if (hr != hrSuccess)
			return hr;
		// Maybe set some class thingy!
	}
	if (hr != hrSuccess)
		return hr;
		
	return ptrSubFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppFolder));
}

HRESULT StoreHelper::CheckAndUpdateSearchFolder(LPMAPIFOLDER lpSearchFolder, eSearchFolder esfWhich)
{
	HRESULT hr;
	SPropValuePtr ptrVersion;

	if (lpSearchFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;
	hr = HrGetOneProp(lpSearchFolder, PROP_VERSION, &~ptrVersion);
	if (hr != hrSuccess && hr != MAPI_E_NOT_FOUND)
		return hr;
	if (hr == hrSuccess && ptrVersion->Value.ul == ARCHIVE_SEARCH_VERSION)
		return hr;

	return (this->*s_infoSearchFolders[esfWhich].fnSetup)(lpSearchFolder, NULL, NULL);
}

HRESULT StoreHelper::CreateSearchFolder(eSearchFolder esfWhich, LPMAPIFOLDER *lppSearchFolder)
{
	HRESULT hr;
	ULONG ulType = 0;
	MAPIFolderPtr ptrRootContainer;
	MAPIFolderPtr ptrArchiveSearchRoot;

	if (lppSearchFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;
	hr = m_ptrMsgStore->OpenEntry(0, nullptr, &iid_of(ptrRootContainer),
	     MAPI_BEST_ACCESS | fMapiDeferredErrors, &ulType, &~ptrRootContainer);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRootContainer->CreateFolder(FOLDER_GENERIC, (LPTSTR)"Zarafa-Archive-Search-Root", nullptr, &iid_of(ptrArchiveSearchRoot), fMapiDeferredErrors | OPEN_IF_EXISTS, &~ptrArchiveSearchRoot);
	if (hr != hrSuccess)
		return hr;

	return DoCreateSearchFolder(ptrArchiveSearchRoot, esfWhich, lppSearchFolder);
}

HRESULT StoreHelper::CreateSearchFolders(LPMAPIFOLDER *lppSearchArchiveFolder, LPMAPIFOLDER *lppSearchDeleteFolder, LPMAPIFOLDER *lppSearchStubFolder)
{
	HRESULT hr;
	ULONG ulType = 0;
	MAPIFolderPtr ptrRootContainer;
	MAPIFolderPtr ptrArchiveSearchRoot;
	bool bWithErrors = false;

	if (lppSearchArchiveFolder == NULL || lppSearchDeleteFolder == NULL ||
	    lppSearchStubFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;
	hr = m_ptrMsgStore->OpenEntry(0, nullptr, &iid_of(ptrRootContainer),
	     MAPI_BEST_ACCESS | fMapiDeferredErrors, &ulType, &~ptrRootContainer);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRootContainer->CreateFolder(FOLDER_GENERIC, (LPTSTR)"Zarafa-Archive-Search-Root", nullptr, &iid_of(ptrArchiveSearchRoot), fMapiDeferredErrors | OPEN_IF_EXISTS, &~ptrArchiveSearchRoot);
	if (hr != hrSuccess)
		return hr;

	// Create the common restrictions here and pass them through DoCreateSearchFolder
	hr = DoCreateSearchFolder(ptrArchiveSearchRoot, esfArchive, lppSearchArchiveFolder);
	if (hr != hrSuccess)
		bWithErrors = true;

	hr = DoCreateSearchFolder(ptrArchiveSearchRoot, esfDelete, lppSearchDeleteFolder);
	if (hr != hrSuccess)
		bWithErrors = true;

	hr = DoCreateSearchFolder(ptrArchiveSearchRoot, esfStub, lppSearchStubFolder);
	if (hr != hrSuccess)
		bWithErrors = true;

	return bWithErrors ? MAPI_W_ERRORS_RETURNED : hrSuccess;
}

HRESULT StoreHelper::DoCreateSearchFolder(LPMAPIFOLDER lpParent, eSearchFolder esfWhich, LPMAPIFOLDER *lppSearchFolder)
{
	HRESULT hr;
	MAPIFolderPtr ptrSearchFolder;

	if (lpParent == NULL || lppSearchFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;
	hr = lpParent->CreateFolder(FOLDER_SEARCH, (LPTSTR)s_infoSearchFolders[esfWhich].lpszName, (LPTSTR)s_infoSearchFolders[esfWhich].lpszDescription, &iid_of(ptrSearchFolder), OPEN_IF_EXISTS | fMapiUnicode, &~ptrSearchFolder);
	if (hr != hrSuccess)
		return hr;
	hr = (this->*s_infoSearchFolders[esfWhich].fnSetup)(ptrSearchFolder, NULL, NULL);
	if (hr != hrSuccess)
		return hr;

	return ptrSearchFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppSearchFolder));
}

HRESULT StoreHelper::SetupSearchArchiveFolder(LPMAPIFOLDER lpSearchFolder, const ECRestriction *lpresClassCheck, const ECRestriction *lpresArchiveCheck)
{
	HRESULT hr;
	ECOrRestriction resClassCheck;
	ECAndRestriction resArchiveCheck;
	MAPIFolderPtr ptrIpmSubtree;
	SPropValuePtr ptrPropEntryId;
	SPropValue sPropStubbed;
	EntryListPtr ptrEntryList;
	ECAndRestriction resArchiveFolder;
	SRestrictionPtr ptrRestriction;
	SPropValue sPropVersion;

	if (lpSearchFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (!lpresClassCheck) {
		hr = GetClassCheckRestriction(&resClassCheck);
		if (hr != hrSuccess)
			return hr;
		lpresClassCheck = &resClassCheck;
	}

	if (!lpresArchiveCheck) {
		hr = GetArchiveCheckRestriction(&resArchiveCheck);
		if (hr != hrSuccess)
			return hr;
		lpresArchiveCheck = &resArchiveCheck;
	}
	hr = GetIpmSubtree(&~ptrIpmSubtree);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrIpmSubtree, PR_ENTRYID, &~ptrPropEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~ptrEntryList);
	if (hr != hrSuccess)
		return hr;
	ptrEntryList->cValues = 1;
	
	hr = MAPIAllocateMore(sizeof(SBinary), ptrEntryList, (LPVOID*)&ptrEntryList->lpbin);
	if (hr != hrSuccess)
		return hr;
	ptrEntryList->lpbin[0].cb = ptrPropEntryId->Value.bin.cb;
	ptrEntryList->lpbin[0].lpb = ptrPropEntryId->Value.bin.lpb;

	sPropStubbed.ulPropTag = PROP_STUBBED; sPropStubbed.Value.b = 1;

	// Create/Update the search folder that tracks unarchived message that are not flagged to be never archived
	resArchiveFolder +=
		*lpresClassCheck +
		ECNotRestriction(
			ECAndRestriction(
				ECExistRestriction(PROP_STUBBED) +
				ECPropertyRestriction(RELOP_EQ, PROP_STUBBED, &sPropStubbed, ECRestriction::Cheap) +

				// The prop stub is only valid if the stub was not copied.
				ECExistRestriction(PROP_ORIGINAL_SOURCEKEY) +
				ECComparePropsRestriction(RELOP_EQ, PROP_ORIGINAL_SOURCEKEY, PR_SOURCE_KEY)
			)
		) +
		ECNotRestriction(
			ECAndRestriction(
				ECExistRestriction(PROP_FLAGS) +
				ECBitMaskRestriction(BMR_NEZ, PROP_FLAGS, ARCH_NEVER_ARCHIVE)
			)
		) +
		ECNotRestriction(*lpresArchiveCheck);
	hr = resArchiveFolder.CreateMAPIRestriction(&~ptrRestriction, ECRestriction::Cheap);
	if (hr != hrSuccess)
		return hr;
	// Set the search criteria
	hr = lpSearchFolder->SetSearchCriteria(ptrRestriction, ptrEntryList, BACKGROUND_SEARCH|RECURSIVE_SEARCH);
	if (hr != hrSuccess)
		return hr;

	sPropVersion.ulPropTag = PROP_VERSION;
	sPropVersion.Value.ul = ARCHIVE_SEARCH_VERSION;
	return HrSetOneProp(lpSearchFolder, &sPropVersion);
}

HRESULT StoreHelper::SetupSearchDeleteFolder(LPMAPIFOLDER lpSearchFolder, const ECRestriction *lpresClassCheck, const ECRestriction *lpresArchiveCheck)
{
	HRESULT hr;
	ECOrRestriction resClassCheck;
	ECAndRestriction resArchiveCheck;
	MAPIFolderPtr ptrIpmSubtree;
	SPropValuePtr ptrPropEntryId;
	EntryListPtr ptrEntryList;
	ECAndRestriction resDeleteFolder;
	SRestrictionPtr ptrRestriction;
	SPropValue sPropVersion;

	if (lpSearchFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (!lpresClassCheck) {
		hr = GetClassCheckRestriction(&resClassCheck);
		if (hr != hrSuccess)
			return hr;
		lpresClassCheck = &resClassCheck;
	}

	if (!lpresArchiveCheck) {
		hr = GetArchiveCheckRestriction(&resArchiveCheck);
		if (hr != hrSuccess)
			return hr;
		lpresArchiveCheck = &resArchiveCheck;
	}
	hr = GetIpmSubtree(&~ptrIpmSubtree);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrIpmSubtree, PR_ENTRYID, &~ptrPropEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~ptrEntryList);
	if (hr != hrSuccess)
		return hr;
	ptrEntryList->cValues = 1;
	
	hr = MAPIAllocateMore(sizeof(SBinary), ptrEntryList, (LPVOID*)&ptrEntryList->lpbin);
	if (hr != hrSuccess)
		return hr;
	ptrEntryList->lpbin[0].cb = ptrPropEntryId->Value.bin.cb;
	ptrEntryList->lpbin[0].lpb = ptrPropEntryId->Value.bin.lpb;

	// Create/Update the search folder that tracks all archived message that are not flagged to be never deleted or never stubbed
	resDeleteFolder +=
		*lpresClassCheck +
		ECNotRestriction(
			ECAndRestriction(
				ECExistRestriction(PROP_FLAGS) +
				ECBitMaskRestriction(BMR_NEZ, PROP_FLAGS, ARCH_NEVER_DELETE)
			)
		) +
		*lpresArchiveCheck;
	hr = resDeleteFolder.CreateMAPIRestriction(&~ptrRestriction, ECRestriction::Cheap);
	if (hr != hrSuccess)
		return hr;
	// Set the search criteria
	hr = lpSearchFolder->SetSearchCriteria(ptrRestriction, ptrEntryList, BACKGROUND_SEARCH|RECURSIVE_SEARCH);
	if (hr != hrSuccess)
		return hr;

	sPropVersion.ulPropTag = PROP_VERSION;
	sPropVersion.Value.ul = ARCHIVE_SEARCH_VERSION;
	return HrSetOneProp(lpSearchFolder, &sPropVersion);
}

HRESULT StoreHelper::SetupSearchStubFolder(LPMAPIFOLDER lpSearchFolder, const ECRestriction* /*lpresClassCheck*/, const ECRestriction *lpresArchiveCheck)
{
	HRESULT hr;
	ECAndRestriction resArchiveCheck;
	MAPIFolderPtr ptrIpmSubtree;
	SPropValuePtr ptrPropEntryId;
	SPropValue sPropStubbed;
	SPropValue sPropMsgClass[2];
	EntryListPtr ptrEntryList;
	ECAndRestriction resStubFolder;
	SRestrictionPtr ptrRestriction;
	SPropValue sPropVersion;

	if (lpSearchFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (!lpresArchiveCheck) {
		hr = GetArchiveCheckRestriction(&resArchiveCheck);
		if (hr != hrSuccess)
			return hr;
		lpresArchiveCheck = &resArchiveCheck;
	}
	hr = GetIpmSubtree(&~ptrIpmSubtree);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrIpmSubtree, PR_ENTRYID, &~ptrPropEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~ptrEntryList);
	if (hr != hrSuccess)
		return hr;
	ptrEntryList->cValues = 1;
	
	hr = MAPIAllocateMore(sizeof(SBinary), ptrEntryList, (LPVOID*)&ptrEntryList->lpbin);
	if (hr != hrSuccess)
		return hr;
	ptrEntryList->lpbin[0].cb = ptrPropEntryId->Value.bin.cb;
	ptrEntryList->lpbin[0].lpb = ptrPropEntryId->Value.bin.lpb;

	sPropStubbed.ulPropTag = PROP_STUBBED; 
	sPropStubbed.Value.b = 1;

	sPropMsgClass[0].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[0].Value.LPSZ = const_cast<TCHAR *>(KC_T("IPM.Note"));
	sPropMsgClass[1].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[1].Value.LPSZ = const_cast<TCHAR *>(KC_T("IPM.Note.")); // RES_CONTENT w FL_PREFIX

	// Create/Update the search folder that tracks non-stubbed archived message that are not flagged to be never stubbed.
	resStubFolder +=
		ECOrRestriction(
			ECPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, &sPropMsgClass[0], ECRestriction::Cheap) +
			ECContentRestriction(FL_PREFIX, PR_MESSAGE_CLASS, &sPropMsgClass[1], ECRestriction::Cheap)
		) +
		ECNotRestriction(
			ECAndRestriction(
				ECExistRestriction(PROP_STUBBED) +
				ECPropertyRestriction(RELOP_EQ, PROP_STUBBED, &sPropStubbed, ECRestriction::Cheap)
			)
		) +
		ECNotRestriction(
			ECAndRestriction(
				ECExistRestriction(PROP_FLAGS) +
				ECBitMaskRestriction(BMR_NEZ, PROP_FLAGS, ARCH_NEVER_STUB)
			)
		) +
		*lpresArchiveCheck;
	hr = resStubFolder.CreateMAPIRestriction(&~ptrRestriction, ECRestriction::Cheap);
	if (hr != hrSuccess)
		return hr;
	// Set the search criteria
	hr = lpSearchFolder->SetSearchCriteria(ptrRestriction, ptrEntryList, BACKGROUND_SEARCH|RECURSIVE_SEARCH);
	if (hr != hrSuccess)
		return hr;

	sPropVersion.ulPropTag = PROP_VERSION;
	sPropVersion.Value.ul = ARCHIVE_SEARCH_VERSION;
	return HrSetOneProp(lpSearchFolder, &sPropVersion);
}

HRESULT StoreHelper::GetClassCheckRestriction(ECOrRestriction *lpresClassCheck)
{
	SPropValue sPropMsgClass[9];
	ECOrRestriction resClassCheck;

	sPropMsgClass[0].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[0].Value.LPSZ = const_cast<TCHAR *>(KC_T("IPM.Note"));
	sPropMsgClass[1].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[1].Value.LPSZ = const_cast<TCHAR *>(KC_T("IPM.Note.")); // RES_CONTENT w FL_PREFIX
	sPropMsgClass[2].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[2].Value.LPSZ = const_cast<TCHAR *>(KC_T("IPM.Schedule.Meeting.Request"));
	sPropMsgClass[3].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[3].Value.LPSZ = const_cast<TCHAR *>(KC_T("IPM.Schedule.Meeting.Resp.Pos"));
	sPropMsgClass[4].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[4].Value.LPSZ = const_cast<TCHAR *>(KC_T("IPM.Schedule.Meeting.Resp.Neg"));
	sPropMsgClass[5].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[5].Value.LPSZ = const_cast<TCHAR *>(KC_T("IPM.Schedule.Meeting.Resp.Tent"));
	sPropMsgClass[6].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[6].Value.LPSZ = const_cast<TCHAR *>(KC_T("IPM.Schedule.Meeting.Canceled"));
	sPropMsgClass[7].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[7].Value.LPSZ = const_cast<TCHAR *>(KC_T("Report.IPM.Note.NDR"));
	sPropMsgClass[8].ulPropTag = PR_MESSAGE_CLASS;
	sPropMsgClass[8].Value.LPSZ = const_cast<TCHAR *>(KC_T("Report.IPM.Note.IPNRN"));

	// Build the message class restriction.
	resClassCheck +=
		ECPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, &sPropMsgClass[0], ECRestriction::Shallow) +
		ECContentRestriction(FL_PREFIX, PR_MESSAGE_CLASS, &sPropMsgClass[1], ECRestriction::Shallow) +
		ECPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, &sPropMsgClass[2], ECRestriction::Shallow) +
		ECPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, &sPropMsgClass[3], ECRestriction::Shallow) +
		ECPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, &sPropMsgClass[4], ECRestriction::Shallow) +
		ECPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, &sPropMsgClass[5], ECRestriction::Shallow) +
		ECPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, &sPropMsgClass[6], ECRestriction::Shallow) +
		ECPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, &sPropMsgClass[7], ECRestriction::Shallow) +
		ECPropertyRestriction(RELOP_EQ, PR_MESSAGE_CLASS, &sPropMsgClass[8], ECRestriction::Shallow);
	*lpresClassCheck = std::move(resClassCheck);
	return hrSuccess;
}

HRESULT StoreHelper::GetArchiveCheckRestriction(ECAndRestriction *lpresArchiveCheck)
{
	HRESULT hr;
	SPropValue sPropDirty = {0};
	ObjectEntryList lstArchives;
	ECAndRestriction resArchiveCheck;

	sPropDirty.ulPropTag = PROP_DIRTY; 
	sPropDirty.Value.b = 1;

	resArchiveCheck +=
		ECAndRestriction(
			ECExistRestriction(PROP_ORIGINAL_SOURCEKEY) +
			ECComparePropsRestriction(RELOP_EQ, PROP_ORIGINAL_SOURCEKEY, PR_SOURCE_KEY)
		) +
		ECNotRestriction(
			ECAndRestriction(
				ECExistRestriction(PROP_DIRTY) +
				ECPropertyRestriction(RELOP_EQ, PROP_DIRTY, &sPropDirty, ECRestriction::Shallow)
			)
		);

	hr = GetArchiveList(&lstArchives);
	if (hr != hrSuccess)
		return hr;

	// Build the restriction that checks that the message has been archived to all archives.
	resArchiveCheck += ECExistRestriction(PROP_ARCHIVE_STORE_ENTRYIDS);
	for (const auto &arc : lstArchives) {
		SPropValue sPropFolderEntryId;

		sPropFolderEntryId.ulPropTag = (PROP_ARCHIVE_STORE_ENTRYIDS & ~MV_FLAG);
		sPropFolderEntryId.Value.bin.cb  = arc.sStoreEntryId.size();
		sPropFolderEntryId.Value.bin.lpb = arc.sStoreEntryId;
		resArchiveCheck += ECPropertyRestriction(RELOP_EQ, PROP_ARCHIVE_STORE_ENTRYIDS, &sPropFolderEntryId, ECRestriction::Full);
	}
	*lpresArchiveCheck = std::move(resArchiveCheck);
	return hrSuccess;
}

}} /* namespace */

