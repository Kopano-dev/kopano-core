/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <new>
#include <utility>
#include "ArchiveHelper.h"
#include "ArchiverSession.h"
#include <kopano/ECLogger.h>
#include "StoreHelper.h"
#include "ECACL.h"
#include <kopano/ECABEntryID.h>
#include <kopano/ECGetText.h> // defines the wonderful macro "_"
#include <kopano/Util.h>

namespace KC { namespace helpers {

/**
 * Create an ArchiveHelper object based on a message store and a folder name.
 *
 * @param[in]	ptrArchiveStore
 *					A MsgStorePtr that points to the message store that's used as an archive.
 * @param[in]	strFolder
 *					The folder name that's used as the root of the archive. If left empty, the IPM subtree
 *					of the message store is used as the archive folder.
 * @param[in]	lpszServerPath
 * 					When set and not empty, it specifies the serverpath to the server containing the archive. So it
 * 					implies that the archive is remote (other server/cluster).
 * @param[out]	lpptrArchiveHelper
 *					Pointer to a ArchiveHelperPtr that assigned the address of the returned ArchiveHelper.
 */
HRESULT ArchiveHelper::Create(LPMDB lpArchiveStore, const tstring &strFolder, const char *lpszServerPath, ArchiveHelperPtr *lpptrArchiveHelper)
{
	HRESULT hr;
	ArchiveHelperPtr ptrArchiveHelper(
		new(std::nothrow) ArchiveHelper(lpArchiveStore, strFolder,
		lpszServerPath ? lpszServerPath : std::string()));
	if (ptrArchiveHelper == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	hr = ptrArchiveHelper->Init();
	if (hr != hrSuccess)
		return hr;
	*lpptrArchiveHelper = std::move(ptrArchiveHelper);
	return hrSuccess;
}

/**
 * Create an ArchiveHelper object based on a message store and a folder.
 *
 * @param[in]	ptrArchiveStore
 *					A MsgStorePtr that points to the message store that's used as an archive.
 * @param[in]	ptrArchiveFolder
 *					A MAPIFolderPtr that points to the folder that will be used as the root of the archive.
 * @param[in]	lpszServerPath
 * 					When set and not empty, it specifies the serverpath to the server containing the archive. So it
 * 					implies that the archive is remote (other server/cluster).
 * @param[out]	lpptrArchiveHelper
 *					Pointer to a ArchiveHelperPtr that assigned the address of the returned ArchiveHelper.
 */
HRESULT ArchiveHelper::Create(LPMDB lpArchiveStore, LPMAPIFOLDER lpArchiveFolder, const char *lpszServerPath, ArchiveHelperPtr *lpptrArchiveHelper)
{
	HRESULT hr;
	ArchiveHelperPtr ptrArchiveHelper(
		new(std::nothrow) ArchiveHelper(lpArchiveStore, lpArchiveFolder,
		lpszServerPath ? lpszServerPath : std::string()));
	if (ptrArchiveHelper == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	hr = ptrArchiveHelper->Init();
	if (hr != hrSuccess)
		return hr;
	*lpptrArchiveHelper = std::move(ptrArchiveHelper);
	return hrSuccess;
}

HRESULT ArchiveHelper::Create(ArchiverSessionPtr ptrSession, const SObjectEntry &archiveEntry, ECLogger *lpLogger, ArchiveHelperPtr *lpptrArchiveHelper)
{
	HRESULT hr;
	MsgStorePtr ptrArchiveStore;
	ULONG ulType;
	MAPIFolderPtr ptrArchiveRootFolder;
	ArchiveHelperPtr ptrArchiveHelper;

	if (lpptrArchiveHelper == NULL)
		return MAPI_E_INVALID_PARAMETER;

	hr = ptrSession->OpenStore(archiveEntry.sStoreEntryId, &~ptrArchiveStore);
	if (hr != hrSuccess) {
		if (lpLogger)
			lpLogger->perr("Failed to open archive store", hr);
		return hr;
	}
	hr = ptrArchiveStore->OpenEntry(archiveEntry.sItemEntryId.size(), archiveEntry.sItemEntryId, &iid_of(ptrArchiveRootFolder), MAPI_BEST_ACCESS | fMapiDeferredErrors, &ulType, &~ptrArchiveRootFolder);
	if (hr != hrSuccess) {
		if (lpLogger)
			lpLogger->perr("Failed to open archive root folder", hr);
		return hr;
	}
	
	// We pass a NULL to the lpszServerPath argument, indicating that it's local. However, it was already
	// remotely opened by ptrSession->OpenStore(). Effectively this causes ptrArchiveHelper->GetArchiveEntry()
	// to malfunction (it won't wrap the entryid with the serverpath). This is not an issue as we don't use
	// that here anyway.
	hr = ArchiveHelper::Create(ptrArchiveStore, ptrArchiveRootFolder, NULL, &ptrArchiveHelper);
	if (hr != hrSuccess) {
		if (lpLogger)
			lpLogger->perr("Failed to create archive helper", hr);
		return hr;
	}
	*lpptrArchiveHelper = std::move(ptrArchiveHelper);
	return hrSuccess;
}

ArchiveHelper::ArchiveHelper(LPMDB lpArchiveStore, const tstring &strFolder, const std::string &strServerPath)
: m_ptrArchiveStore(lpArchiveStore, true)
, m_strFolder(strFolder)
, m_strServerPath(strServerPath), m_propmap(4)
{ }

ArchiveHelper::ArchiveHelper(LPMDB lpArchiveStore, LPMAPIFOLDER lpArchiveFolder, const std::string &strServerPath)
: m_ptrArchiveStore(lpArchiveStore, true)
, m_ptrArchiveFolder(lpArchiveFolder, true)
, m_strServerPath(strServerPath)
{ }

/**
 * Initialize an ArchiveHelper object.
 */
HRESULT ArchiveHelper::Init()
{
	HRESULT	hr = hrSuccess;

	PROPMAP_INIT_NAMED_ID(ATTACHED_USER_ENTRYID, PT_BINARY, PSETID_Archive, dispidAttachedUser)
	PROPMAP_INIT_NAMED_ID(ARCHIVE_TYPE, PT_LONG, PSETID_Archive, dispidType)
	PROPMAP_INIT_NAMED_ID(ATTACH_TYPE, PT_LONG, PSETID_Archive, dispidAttachType)
	PROPMAP_INIT_NAMED_ID(SPECIAL_FOLDER_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidSpecialFolderEntryIds);
	PROPMAP_INIT(m_ptrArchiveStore)
	return hr;
}

/**
 * Get the user that's attached to this archive.
 *
 * @param[out]	lpsUserEntryId
 *					Pointer to a entryid_t that will be populated with the entryid of the user
 *					whose store is attached to this archive.
 */
HRESULT ArchiveHelper::GetAttachedUser(abentryid_t *lpsUserEntryId)
{
	HRESULT hr;
	MAPIFolderPtr ptrFolder;
	SPropValuePtr ptrPropValue;
	
	hr = GetArchiveFolder(false, &~ptrFolder);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrFolder, PROP_ATTACHED_USER_ENTRYID, &~ptrPropValue);
	if (hr != hrSuccess)
		return hr;
	*lpsUserEntryId = ptrPropValue->Value.bin;
	return hrSuccess;
}

/**
 * Set the user that's attached to this archive.
 *
 * @param[in]	sUserEntryId
 *					The entryid of the user whose store is attached to this
 *					archive.
 */
HRESULT ArchiveHelper::SetAttachedUser(const abentryid_t &sUserEntryId)
{
	HRESULT hr;
	MAPIFolderPtr ptrFolder;
	SPropValue sPropValue = {0};

	hr = GetArchiveFolder(true, &~ptrFolder);
	if (hr != hrSuccess)
		return hr;
		
	sPropValue.ulPropTag = PROP_ATTACHED_USER_ENTRYID;
	sPropValue.Value.bin.cb = sUserEntryId.size();
	sPropValue.Value.bin.lpb = sUserEntryId;
	return HrSetOneProp(ptrFolder, &sPropValue);
}

/**
 * Get an SObjectEntry that uniquely identifies this archive.
 *
 * @param[in]	bCreate
 * 					Create the folder if it doesn't exist.
 * @param[out]	lpSObjectEntry
 *					Pointer to a SObjectEntry structure that will be populated with the unique
 *					reference to this archive.
 */
HRESULT ArchiveHelper::GetArchiveEntry(bool bCreate, SObjectEntry *lpsObjectEntry)
{
	HRESULT hr;
	SPropValuePtr ptrStoreEntryId;
	MAPIFolderPtr ptrFolder;
	SPropValuePtr ptrFolderEntryId;
	
	hr = HrGetOneProp(m_ptrArchiveStore, PR_ENTRYID, &~ptrStoreEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = GetArchiveFolder(bCreate, &~ptrFolder);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrFolder, PR_ENTRYID, &~ptrFolderEntryId);
	if (hr != hrSuccess)
		return hr;
	lpsObjectEntry->sStoreEntryId = ptrStoreEntryId->Value.bin;
	if (!m_strServerPath.empty())
		lpsObjectEntry->sStoreEntryId.wrap(m_strServerPath);
	lpsObjectEntry->sItemEntryId = ptrFolderEntryId->Value.bin;
	return hrSuccess;
}

/**
 * Get the archive type of this archive.
 *
 * @param[out]	lparchType
 *					Pointer to a ArchiveType enum that will be set to the type
 *					of this archive.
 * @param[out]	lpattachType
 *					Pointer to a AttachType enum that will be set to the way this
 * 					archive was attached (explicit / implicit).
 */
HRESULT ArchiveHelper::GetArchiveType(ArchiveType *lparchType, AttachType *lpattachType)
{
	HRESULT hr;
	SPropValuePtr ptrPropVal;
	MAPIFolderPtr ptrArchiveFolder;

	ArchiveType archType;
	AttachType attachType = UnknownAttach;

	hr = HrGetOneProp(m_ptrArchiveStore, PROP_ARCHIVE_TYPE, &~ptrPropVal);
	if (hr == MAPI_E_NOT_FOUND) {
		archType = UndefArchive;
		hr = hrSuccess;
	} else if (hr == hrSuccess) {
		switch (ptrPropVal->Value.l) {
		case SingleArchive:
		case MultiArchive:
			archType = (ArchiveType)ptrPropVal->Value.l;
			break;
		default:
			hr = MAPI_E_CORRUPT_DATA;
			break;
		}
	}
	if (hr != hrSuccess)
		return hr;

	if (lpattachType) {
		hr = GetArchiveFolder(true, &~ptrArchiveFolder);
		if (hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(ptrArchiveFolder, PROP_ATTACH_TYPE, &~ptrPropVal);
		if (hr == MAPI_E_NOT_FOUND) {
			attachType = ExplicitAttach;
			hr = hrSuccess;
		} else if (hr == hrSuccess) {
			switch (ptrPropVal->Value.l) {
			case ExplicitAttach:
			case ImplicitAttach:
				attachType = (AttachType)ptrPropVal->Value.l;
				break;
			default:
				hr = MAPI_E_CORRUPT_DATA;
				break;
			}
		}
		if (hr != hrSuccess)
			return hr;
	}

	if (lparchType)
		*lparchType = archType;

	if (lpattachType)
		*lpattachType = attachType;

	return hrSuccess;
}

/**
 * Set the archive type for this archive.
 *
 * @param[in]	aType
 *					The ArchiveType type of this archive.
 */
HRESULT ArchiveHelper::SetArchiveType(ArchiveType archType, AttachType attachType)
{
	HRESULT hr;
	MAPIFolderPtr ptrArchiveFolder;
	SPropValue sPropVal = {0};

	sPropVal.ulPropTag = PROP_ARCHIVE_TYPE;
	sPropVal.Value.l = archType;

	hr = HrSetOneProp(m_ptrArchiveStore, &sPropVal);
	if (hr != hrSuccess)
		return hr;
	hr = GetArchiveFolder(true, &~ptrArchiveFolder);
	if (hr != hrSuccess)
		return hr;

	sPropVal.ulPropTag = PROP_ATTACH_TYPE;
	sPropVal.Value.l = attachType;
	return HrSetOneProp(ptrArchiveFolder, &sPropVal);
}

/**
 * Set permissions on the archive for the user specified by sUserEntryId. This makes sure that the archive is
 * at least readable for the user. If the archive is in a subfolder, only the subfolder that is the archive will
 * be visible. The other folders will be hidden.
 *
 * @param[in]	sUserEntryId
 *					The entryid of the user for who permissions are being set.
 * @param[in]	bWriteable.
 *					If set to true, the archive will be writable by the user.
 */
HRESULT ArchiveHelper::SetPermissions(const abentryid_t &sUserEntryId, bool bWritable)
{
	HRESULT hr;
	MAPIFolderPtr ptrFolder;
	object_ptr<IExchangeModifyTable> ptrEMT;
	MAPITablePtr ptrTable;
	SPropValue sUserProps[2];
	SPropValue sOtherProps[2];
	memory_ptr<ROWLIST> ptrRowList;
	StoreHelperPtr ptrStoreHelper;
	
	hr = MAPIAllocateBuffer(CbNewROWLIST(2), &~ptrRowList);
	if (hr != hrSuccess)
		return hr;
	ptrRowList->cEntries = 0;

	// First set permissions on the IPM Subtree since we'll simply overwrite
	// them if the archive folder IS the IPM Subtree.
	
	// Grant folder visible permissions on the IPM Subtree for this user.
	hr = StoreHelper::Create(m_ptrArchiveStore, &ptrStoreHelper);
	if (hr != hrSuccess)
		return hr;
	hr = ptrStoreHelper->GetIpmSubtree(&~ptrFolder);
	if (hr != hrSuccess)
		return hr;
	hr = ptrFolder->OpenProperty(PR_ACL_TABLE, &iid_of(ptrEMT), 0, fMapiDeferredErrors, &~ptrEMT);
	if (hr != hrSuccess)
		return hr;
	
	sUserProps[0].ulPropTag = PR_MEMBER_ENTRYID;
	sUserProps[0].Value.bin.cb = sUserEntryId.size();
	sUserProps[0].Value.bin.lpb = sUserEntryId;
	sUserProps[1].ulPropTag = PR_MEMBER_RIGHTS;
	sUserProps[1].Value.l = RIGHTS_FOLDER_VISIBLE;
	
	ptrRowList->cEntries = 1;
	ptrRowList->aEntries[0].ulRowFlags = ROW_MODIFY;
	ptrRowList->aEntries[0].cValues = 2;
	ptrRowList->aEntries[0].rgPropVals = sUserProps;
	
	hr = ptrEMT->ModifyTable(0, ptrRowList);
	if (hr != hrSuccess && hr != MAPI_E_INVALID_PARAMETER)	// We can't set rights for non-active users.
		return hr;
	
	// Grant read only permissions on the archive folder for this user (unless bWritable is requested).
	// Grant no access for all other users (everyone)
	hr = GetArchiveFolder(true, &~ptrFolder);
	if (hr != hrSuccess)
		return hr;
	hr = ptrFolder->OpenProperty(PR_ACL_TABLE, &iid_of(ptrEMT), 0, fMapiDeferredErrors, &~ptrEMT);
	if (hr != hrSuccess)
		return hr;
	
	sUserProps[0].ulPropTag = PR_MEMBER_ENTRYID;
	sUserProps[0].Value.bin.cb = sUserEntryId.size();
	sUserProps[0].Value.bin.lpb = sUserEntryId;
	sUserProps[1].ulPropTag = PR_MEMBER_RIGHTS;
	sUserProps[1].Value.l = (bWritable ? ROLE_OWNER : ROLE_REVIEWER);
	
	sOtherProps[0].ulPropTag = PR_MEMBER_ENTRYID;
	sOtherProps[0].Value.bin.cb = g_cbEveryoneEid;
	sOtherProps[0].Value.bin.lpb = g_lpEveryoneEid;
	sOtherProps[1].ulPropTag = PR_MEMBER_RIGHTS;
	sOtherProps[1].Value.l = RIGHTS_NONE;
	
	ptrRowList->cEntries = 2;
	ptrRowList->aEntries[0].ulRowFlags = ROW_MODIFY;
	ptrRowList->aEntries[0].cValues = 2;
	ptrRowList->aEntries[0].rgPropVals = sUserProps;
	ptrRowList->aEntries[1].ulRowFlags = ROW_MODIFY;
	ptrRowList->aEntries[1].cValues = 2;
	ptrRowList->aEntries[1].rgPropVals = sOtherProps;
	
	hr = ptrEMT->ModifyTable(0, ptrRowList);
	if (hr == MAPI_W_PARTIAL_COMPLETION)		// We can't set rights for non-active users.
		hr = hrSuccess;	
	
	return hr;
}

/**
 * Open or create the folder in the archive that's linked to the non-archive folder referenced by ptrSourceFolder.
 *
 * @param[in]	ptrSourceFolder
 *					The MAPIFolderPtr that points to the folder for which the attached archive folder
 *					is being requested.
 * @param[in]	ptrSession
 *					The Session pointer that's used to open folders with.
 * @param[out]	lppDestinationFolder
 *					Pointer to a MAPIFolder pointer that's assigned the address of the returned folder.
 */
HRESULT ArchiveHelper::GetArchiveFolderFor(MAPIFolderPtr &ptrSourceFolder, ArchiverSessionPtr ptrSession, LPMAPIFOLDER *lppDestinationFolder)
{
	HRESULT hr;
	SPropValuePtr ptrStoreEntryId;
	SPropValuePtr ptrFolderType;
	SPropValuePtr ptrFolderEntryId;
	MAPIPropHelperPtr ptrSourceFolderHelper;
	MAPIPropHelperPtr ptrArchiveFolderHelper;
	ObjectEntryList lstFolderArchives;
	ObjectEntryList::const_iterator iArchiveFolder;
	MAPIFolderPtr ptrArchiveFolder;
	MAPIFolderPtr ptrParentFolder;
	MAPIFolderPtr ptrArchiveParentFolder;
	ULONG ulType = 0;
	ULONG cValues = 0;
	SPropArrayPtr ptrPropArray;
	SObjectEntry objectEntry;
	static constexpr const SizedSPropTagArray(3, sptaFolderPropsForCreate) =
		{3, {PR_CONTAINER_CLASS, PR_DISPLAY_NAME, PR_COMMENT}};
	static constexpr const SizedSPropTagArray(2, sptaFolderPropsForReference) =
		{2, {PR_ENTRYID, PR_STORE_ENTRYID}};
	
	hr = HrGetOneProp(m_ptrArchiveStore, PR_ENTRYID, &~ptrStoreEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIPropHelper::Create(ptrSourceFolder.as<MAPIPropPtr>(), &ptrSourceFolderHelper);
	if (hr != hrSuccess)
		return hr;
	
	hr = ptrSourceFolderHelper->GetArchiveList(&lstFolderArchives);
	if (hr == MAPI_E_CORRUPT_DATA) {
		// If the list is corrupt, the folder will become unusable. We'll just create a new folder, which will most
		// likely become the same folder (if the name hasn't changed).
		hr = hrSuccess;
	} else if (hr != hrSuccess)
		return hr;
	
	iArchiveFolder = find_if(lstFolderArchives.cbegin(), lstFolderArchives.cend(), StoreCompare(ptrStoreEntryId->Value.bin));
	if (iArchiveFolder != lstFolderArchives.cend()) {
		hr = m_ptrArchiveStore->OpenEntry(iArchiveFolder->sItemEntryId.size(), iArchiveFolder->sItemEntryId, &iid_of(ptrArchiveFolder), MAPI_BEST_ACCESS | fMapiDeferredErrors, &ulType, &~ptrArchiveFolder);
		if (hr != hrSuccess)
			return hr;
		return ptrArchiveFolder->QueryInterface(IID_IMAPIFolder,
			reinterpret_cast<LPVOID *>(lppDestinationFolder));
	}

	hr = ptrSourceFolderHelper->GetParentFolder(ptrSession, &~ptrParentFolder);
	if (hr != hrSuccess)
		return hr;
			
	// If the parent is the root folder, we're currently working on the IPM-SUBTREE. This means
	// we can just return the root archive folder in that case.
	hr = HrGetOneProp(ptrParentFolder, PR_FOLDER_TYPE, &~ptrFolderType);
	if (hr != hrSuccess)
		return hr;
	if (ptrFolderType->Value.l == FOLDER_ROOT)
		hr = GetArchiveFolder(true, &~ptrArchiveFolder);
	else {		
		bool bIsArchiveRoot = false;

		hr = GetArchiveFolderFor(ptrParentFolder, ptrSession, &~ptrArchiveParentFolder);
		if (hr != hrSuccess)
			return hr;
		hr = IsArchiveFolder(ptrArchiveParentFolder, &bIsArchiveRoot);
		if (hr != hrSuccess)
			return hr;
		
		// We now have the parent of the folder we're looking for. Se we can just create the folder we need.
		hr = ptrSourceFolder->GetProps(sptaFolderPropsForCreate, 0, &cValues, &~ptrPropArray);
		if (FAILED(hr))
			return hr;
		
		hr = ptrArchiveParentFolder->CreateFolder(FOLDER_GENERIC, 
		     const_cast<TCHAR *>(PROP_TYPE(ptrPropArray[1].ulPropTag) == PT_ERROR ? KC_T("") : ptrPropArray[1].Value.LPSZ),
		     const_cast<TCHAR *>(PROP_TYPE(ptrPropArray[2].ulPropTag) == PT_ERROR ? KC_T("") : ptrPropArray[2].Value.LPSZ),
		     &iid_of(ptrArchiveFolder), OPEN_IF_EXISTS | fMapiUnicode,
		     &~ptrArchiveFolder);
		if (hr != hrSuccess)
			return hr;

		if (bIsArchiveRoot) {
			bool bIsSpecialFolder = false;

			hr = IsSpecialFolder(sfBase, ptrArchiveFolder, &bIsSpecialFolder);
			if (hr != hrSuccess)
				return hr;

			if (bIsSpecialFolder) {
				ULONG ulCollisionCount = 0;
				do {
					tstring strFolderName(PROP_TYPE(ptrPropArray[1].ulPropTag) == PT_ERROR ? KC_T("") : ptrPropArray[1].Value.LPSZ);
					if (ulCollisionCount > 0) {
						strFolderName.append(KC_T(" ("));
						strFolderName.append(tstringify(ulCollisionCount));
						strFolderName.append(1, ')');
					}
					hr = ptrArchiveParentFolder->CreateFolder(FOLDER_GENERIC,
					     const_cast<TCHAR *>(strFolderName.c_str()),
					     const_cast<TCHAR *>(PROP_TYPE(ptrPropArray[2].ulPropTag) == PT_ERROR ? KC_T("") : ptrPropArray[2].Value.LPSZ),
					     &iid_of(ptrArchiveFolder), fMapiUnicode, &~ptrArchiveFolder);
					if (hr != hrSuccess && hr != MAPI_E_COLLISION)
						return hr;

					++ulCollisionCount;
				} while (hr == MAPI_E_COLLISION && ulCollisionCount < 0xffff);	// We need to stop counting at some point.
				if (hr != hrSuccess)
					return hr;
			}
		}

		if (PROP_TYPE(ptrPropArray[0].ulPropTag) != PT_ERROR)
			hr = HrSetOneProp(ptrArchiveFolder, &ptrPropArray[0]);
	}
	if (hr != hrSuccess)
		return hr;

	// Update the archive list for the source folder
	hr = HrGetOneProp(ptrArchiveFolder, PR_ENTRYID, &~ptrFolderEntryId);
	if (hr != hrSuccess)
		return hr;
	objectEntry.sStoreEntryId = ptrStoreEntryId->Value.bin;
	objectEntry.sItemEntryId = ptrFolderEntryId->Value.bin;
	lstFolderArchives.emplace_back(objectEntry);
	lstFolderArchives.sort();
	lstFolderArchives.unique();
	hr = ptrSourceFolderHelper->SetArchiveList(lstFolderArchives);
	if (hr != hrSuccess)
		return hr;

	// Reference the source folder in the archive folder
	hr = ptrSourceFolder->GetProps(sptaFolderPropsForReference, 0, &cValues, &~ptrPropArray);
	if (hr != hrSuccess)
		return hr;
	objectEntry.sStoreEntryId = ptrPropArray[1].Value.bin;
	objectEntry.sItemEntryId = ptrPropArray[0].Value.bin;
	hr = MAPIPropHelper::Create(ptrArchiveFolder.as<MAPIPropPtr>(), &ptrArchiveFolderHelper);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveFolderHelper->SetReference(objectEntry);
	if (hr != hrSuccess)
		return hr;
	return ptrArchiveFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppDestinationFolder));
}

/**
 * Get the History folder. If the folder doesn't exist it will be created.
 */
HRESULT ArchiveHelper::GetHistoryFolder(LPMAPIFOLDER *lppHistoryFolder)
{
	return GetSpecialFolder(sfHistory, true, lppHistoryFolder);
}

/**
 * Get the Outgoing foler. If the folder doesn't exist it will be created.
 */
HRESULT ArchiveHelper::GetOutgoingFolder(LPMAPIFOLDER *lppOutgoingFolder)
{
	return GetSpecialFolder(sfOutgoing, true, lppOutgoingFolder);
}

/**
 * Get the DeletedItems folder. If the folder doesn't exist it will be created.
 * @note: This is not the trash folder, but a special archive folder that contains
 *        messages that were deleted in the primary store.
 */
HRESULT ArchiveHelper::GetDeletedItemsFolder(LPMAPIFOLDER *lppOutgoingFolder)
{
	return GetSpecialFolder(sfDeleted, true, lppOutgoingFolder);
}

/**
 * Get the root folder of the special folders. This folder contains the history,
 * outgoing and deleted items folders. If the folder doesn't exist it won't be
 * created.
 */
HRESULT ArchiveHelper::GetSpecialsRootFolder(LPMAPIFOLDER *lppSpecialsRootFolder)
{
	return GetSpecialFolder(sfBase, false, lppSpecialsRootFolder);
}

/**
 * Get the archive folder. This opens the actual folder if only a name was specified.
 *
 * @param[in]	bCreate
 * 					Create the folder if it doesn't exist.
 * @param[out]	lppArchiveFolder
 *					Pointer to the MAPIFolder pointer that will be assigned the address of the
 *					returned folder.
 */
HRESULT ArchiveHelper::GetArchiveFolder(bool bCreate, LPMAPIFOLDER *lppArchiveFolder)
{
	HRESULT hr;
	StoreHelperPtr ptrStoreHelper;

	if (m_ptrArchiveFolder != nullptr)
		return m_ptrArchiveFolder->QueryInterface(IID_IMAPIFolder,
			reinterpret_cast<LPVOID *>(lppArchiveFolder));
	hr = StoreHelper::Create(m_ptrArchiveStore, &ptrStoreHelper);
	if (hr != hrSuccess)
		return hr;
	if (m_strFolder.empty())
		hr = ptrStoreHelper->GetIpmSubtree(&~m_ptrArchiveFolder);
	else
		hr = ptrStoreHelper->GetFolder(m_strFolder, bCreate, &~m_ptrArchiveFolder);
	if (hr != hrSuccess)
		return hr;
	return m_ptrArchiveFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppArchiveFolder));
}

/**
 * Check if the passed folder is the same folder that would be returned with GetArchiveFolder.
 *
 * @param[in]	lpFolder
 *					Pointer to the MAPIFolder to check.
 * @param[out]	lpbResult
 *					True if the folder is the same as would be returned by GetArchiveFolder.
 */
HRESULT ArchiveHelper::IsArchiveFolder(LPMAPIFOLDER lpFolder, bool *lpbResult)
{
	HRESULT hr;
	SPropValuePtr ptrFolderEntryID;
	MAPIFolderPtr ptrArchiveFolder;
	SPropValuePtr ptrArchiveEntryID;
	ULONG ulResult = 0;

	hr = HrGetOneProp(lpFolder, PR_ENTRYID, &~ptrFolderEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = GetArchiveFolder(false, &~ptrArchiveFolder);
	if (hr == MAPI_E_NOT_FOUND) {
		*lpbResult = false;
		return hrSuccess;
	}
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrArchiveFolder, PR_ENTRYID, &~ptrArchiveEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = m_ptrArchiveStore->CompareEntryIDs(ptrFolderEntryID->Value.bin.cb, (LPENTRYID)ptrFolderEntryID->Value.bin.lpb,
						ptrArchiveEntryID->Value.bin.cb, (LPENTRYID)ptrArchiveEntryID->Value.bin.lpb,
						0, &ulResult);
	if (hr != hrSuccess)
		return hr;

	*lpbResult = (ulResult != FALSE);
	return hrSuccess;
}

HRESULT ArchiveHelper::GetSpecialFolderEntryID(eSpecFolder sfWhich, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	HRESULT hr;
	MAPIFolderPtr ptrArchiveRoot;
	SPropValuePtr ptrSFEntryIDs;

	hr = GetArchiveFolder(false, &~ptrArchiveRoot);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrArchiveRoot, PROP_SPECIAL_FOLDER_ENTRYIDS, &~ptrSFEntryIDs);
	if (hr != hrSuccess)
		return hr;
	if (ptrSFEntryIDs->Value.MVbin.cValues <= ULONG(sfWhich) || ptrSFEntryIDs->Value.MVbin.lpbin[sfWhich].cb == 0)
		return MAPI_E_NOT_FOUND;

	return Util::HrCopyEntryId(ptrSFEntryIDs->Value.MVbin.lpbin[sfWhich].cb,
		reinterpret_cast<LPENTRYID>(ptrSFEntryIDs->Value.MVbin.lpbin[sfWhich].lpb),
		lpcbEntryID, lppEntryID);
}

HRESULT ArchiveHelper::SetSpecialFolderEntryID(eSpecFolder sfWhich, ULONG cbEntryID, LPENTRYID lpEntryID)
{
	HRESULT hr;
	MAPIFolderPtr ptrArchiveRoot;
	SPropValuePtr ptrSFEntryIDs;

	hr = GetArchiveFolder(false, &~ptrArchiveRoot);
	if (hr != hrSuccess)
		return hr;

	hr = HrGetOneProp(ptrArchiveRoot, PROP_SPECIAL_FOLDER_ENTRYIDS, &~ptrSFEntryIDs);
	if (hr == MAPI_E_NOT_FOUND) {
		hr = MAPIAllocateBuffer(sizeof(SPropValue), &~ptrSFEntryIDs);
		if (hr != hrSuccess)
			return hr;

		ptrSFEntryIDs->ulPropTag = PROP_SPECIAL_FOLDER_ENTRYIDS;
		ptrSFEntryIDs->Value.MVbin.cValues = 0;
		ptrSFEntryIDs->Value.MVbin.lpbin = NULL;
	}
	if (hr != hrSuccess)
		return hr;

	if (ptrSFEntryIDs->Value.MVbin.cValues <= ULONG(sfWhich)) {
		LPSBinary lpbinPrev = ptrSFEntryIDs->Value.MVbin.lpbin;

		hr = MAPIAllocateMore((sfWhich + 1) * sizeof(SBinary), ptrSFEntryIDs, (LPVOID*)&ptrSFEntryIDs->Value.MVbin.lpbin);
		if (hr != hrSuccess)
			return hr;

		// Copy old entries
		for (ULONG i = 0; i < ptrSFEntryIDs->Value.MVbin.cValues; ++i)
			ptrSFEntryIDs->Value.MVbin.lpbin[i] = lpbinPrev[i];		// Shallow copy

		// Pad entries
		for (ULONG i = ptrSFEntryIDs->Value.MVbin.cValues; i < ULONG(sfWhich); ++i) {
			ptrSFEntryIDs->Value.MVbin.lpbin[i].cb = 0;
			ptrSFEntryIDs->Value.MVbin.lpbin[i].lpb = NULL;
		}

		ptrSFEntryIDs->Value.MVbin.cValues = sfWhich + 1;
	}

	ptrSFEntryIDs->Value.MVbin.lpbin[sfWhich].cb = cbEntryID;
	ptrSFEntryIDs->Value.MVbin.lpbin[sfWhich].lpb = (LPBYTE)lpEntryID;	// Shallow copy

	hr = HrSetOneProp(ptrArchiveRoot, ptrSFEntryIDs);
	if (hr != hrSuccess)
		return hr;

	return ptrArchiveRoot->SaveChanges(KEEP_OPEN_READWRITE);
}

/**
 * Open one of the special folders.
 * 
 * @param[in]	sfWhich				The folder to open. Valid values are sfBase, sfHistory, sfOutgoing and sfDeleted.
 * @param[in]	bCreate				Specify if the folder should be created if absent.
 * @param[out]	lppSpecialFolder	Will point to the special folder on success.
 */
HRESULT ArchiveHelper::GetSpecialFolder(eSpecFolder sfWhich, bool bCreate, LPMAPIFOLDER *lppSpecialFolder)
{
	HRESULT hr;
	ULONG ulSpecialFolderID;
	EntryIdPtr ptrSpecialFolderID;
	MAPIFolderPtr ptrSpecialFolder;

	hr = GetSpecialFolderEntryID(sfWhich, &ulSpecialFolderID, &~ptrSpecialFolderID);
	if (hr == hrSuccess) {
		ULONG ulType;
		hr = m_ptrArchiveStore->OpenEntry(ulSpecialFolderID, ptrSpecialFolderID, &iid_of(ptrSpecialFolder), MAPI_MODIFY, &ulType, &~ptrSpecialFolder);
	} else if (hr == MAPI_E_NOT_FOUND && bCreate) {
		hr = CreateSpecialFolder(sfWhich, &~ptrSpecialFolder);
	}
	if (hr != hrSuccess)
		return hr;
	
	return ptrSpecialFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppSpecialFolder));
}

HRESULT ArchiveHelper::CreateSpecialFolder(eSpecFolder sfWhich, LPMAPIFOLDER *lppSpecialFolder)
{
	HRESULT hr;
	MAPIFolderPtr ptrParent;
	MAPIFolderPtr ptrSpecialFolder;
	LPTSTR lpszName = NULL;
	LPTSTR lpszDesc = NULL;
	ULONG ulCreateFlags = OPEN_IF_EXISTS;
	ULONG ulCollisionCount = 0;
	SPropValuePtr ptrEntryID;

	if (sfWhich == sfBase)
		// We need to get the archive root to create the special root folder in
		hr = GetArchiveFolder(true, &~ptrParent);
	else
		// We need to get the special root to create the other special folders in
		hr = GetSpecialFolder(sfBase, true, &~ptrParent);
	if (hr != hrSuccess)
		return hr;

	switch (sfWhich) {
	case sfBase:
		lpszName = _("Kopano Archive");
		lpszDesc = _("This folder contains the special archive folders.");
		ulCreateFlags = 0;
		break;
	case sfHistory:
		lpszName = _("History");
		lpszDesc = _("This folder contains archives that have been replaced by a newer version.");
		break;
	case sfOutgoing:
		lpszName = _("Outgoing");
		lpszDesc = _("This folder contains archives of all outgoing messages.");
		break;
	case sfDeleted:
		lpszName = _("Deleted");
		lpszDesc = _("This folder contains archives of messages that have been deleted.");
		break;
	default:
		assert(false);
		return MAPI_E_INVALID_PARAMETER;
	}

	do {
		tstring strFolderName(lpszName);
		if (ulCollisionCount > 0) {
			strFolderName.append(KC_T(" ("));
			strFolderName.append(tstringify(ulCollisionCount));
			strFolderName.append(1, ')');
		}
		hr = ptrParent->CreateFolder(FOLDER_GENERIC, (LPTSTR)strFolderName.c_str(), lpszDesc, &iid_of(ptrSpecialFolder), fMapiUnicode | ulCreateFlags, &~ptrSpecialFolder);
		if (hr != hrSuccess && hr != MAPI_E_COLLISION)
			return hr;

		++ulCollisionCount;
	} while (hr == MAPI_E_COLLISION && ulCollisionCount < 0xffff);	// We need to stop counting at some point.
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrSpecialFolder, PR_ENTRYID, &~ptrEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = SetSpecialFolderEntryID(sfWhich, ptrEntryID->Value.bin.cb, (LPENTRYID)ptrEntryID->Value.bin.lpb);
	if (hr != hrSuccess)
		return hr;

	return ptrSpecialFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppSpecialFolder));
}

HRESULT ArchiveHelper::IsSpecialFolder(eSpecFolder sfWhich, LPMAPIFOLDER lpFolder, bool *lpbResult)
{
	HRESULT hr;
	ULONG cbSpecialEntryID;
	EntryIdPtr ptrSpecialEntryID;
	SPropValuePtr ptrFolderEntryID;
	ULONG ulResult = 0;

	hr = GetSpecialFolderEntryID(sfWhich, &cbSpecialEntryID, &~ptrSpecialEntryID);
	if (hr == MAPI_E_NOT_FOUND) {
		*lpbResult = false;
		return hrSuccess;
	}
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(lpFolder, PR_ENTRYID, &~ptrFolderEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = m_ptrArchiveStore->CompareEntryIDs(ptrFolderEntryID->Value.bin.cb, (LPENTRYID)ptrFolderEntryID->Value.bin.lpb,
						cbSpecialEntryID, ptrSpecialEntryID, 0, &ulResult);
	if (hr != hrSuccess)
		return hr;

	*lpbResult = (ulResult != FALSE);
	return hrSuccess;
}

HRESULT ArchiveHelper::PrepareForFirstUse(ECLogger *lpLogger)
{
	HRESULT hr;
	StoreHelperPtr ptrStoreHelper;
	MAPIFolderPtr ptrIpmSubtree;
	SPropValue sEntryId;

	hr = StoreHelper::Create(m_ptrArchiveStore, &ptrStoreHelper);
	if (hr != hrSuccess) {
		if (lpLogger)
			lpLogger->perr("Failed to create store helper", hr);
		return hr;
	}
	hr = ptrStoreHelper->GetIpmSubtree(&~ptrIpmSubtree);
	if (hr != hrSuccess) {
		if (lpLogger)
			lpLogger->perr("Failed to get archive IPM subtree", hr);
		return hr;
	}

	hr = ptrIpmSubtree->EmptyFolder(0, NULL, DEL_ASSOCIATED|DELETE_HARD_DELETE);
	if (FAILED(hr)) {
		if (lpLogger)
			lpLogger->perr("Failed to empty archive IPM subtree", hr);
		return hr;
	} else if (hr != hrSuccess) {
		if (lpLogger)
			lpLogger->Log(EC_LOGLEVEL_WARNING, "archive IPM subtree was only partially emptied.");
	}

	// Now set the Wastebasket entryid to 0,NULL, so outlook won't create it
	sEntryId.ulPropTag = PR_IPM_WASTEBASKET_ENTRYID;
	sEntryId.Value.bin.cb = 0;
	sEntryId.Value.bin.lpb = NULL;
	hr = HrSetOneProp(m_ptrArchiveStore, &sEntryId);
	if (hr != hrSuccess && lpLogger != nullptr)
		lpLogger->perr("Failed to clear wasebasket entryid", hr);
	return hr;
}

}} /* namespace */
