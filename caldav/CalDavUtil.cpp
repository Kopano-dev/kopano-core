/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <string>
#include <utility>
#include <kopano/ECRestriction.h>
#include "CalDavUtil.h"
#include <kopano/EMSAbTag.h>
#include <kopano/MAPIErrors.h>
#include <kopano/charset/convert.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <kopano/timeutil.hpp>

using namespace KC;

/**
 * Open MAPI session
 *
 * @param[in]	strUser		User login name
 * @param[in]	strPass		User password
 * @param[in]	strPath		Kopano server path
 * @param[out]	lppSession	IMAPISession object if login is successful
 * @return		HRESULT
 * @retval		MAPI_E_LOGON_FAILED		Unable to login with the specified user-name and password
 */
HRESULT HrAuthenticate(const std::string &appVersion,
    const std::string &appMisc, const std::wstring &wstrUser,
    const std::wstring &wstrPass, const std::string &strPath,
    IMAPISession **lppSession)
{
	// @todo: if login with utf8 username is not possible, lookup user from addressbook? but how?
	return HrOpenECSession(lppSession, appVersion.c_str(), appMisc.c_str(),
	       wstrUser.c_str(), wstrPass.c_str(), strPath.c_str(),
	       0, NULL, NULL, NULL);
}

/**
 * Add Property to the folder or message
 *
 * @param[in]		lpMapiProp		IMAPIProp object pointer to which property is to be added
 * @param[in]		ulPropTag		Property Tag of the property to be added
 * @param[in]		bFldId			Boolean to state if the property to be added is FolderID or not
 * @param[in,out]	wstrProperty		String value of the property, if empty then GUID is created and added
 *
 * @return			HRESULT
 */
// @todo rewrite usage of this function, and remove it
HRESULT HrAddProperty(IMAPIProp *lpMapiProp, ULONG ulPropTag, bool bFldId, std::wstring *wstrProperty)
{
	SPropValue sPropVal;
	memory_ptr<SPropValue> lpMsgProp;
	GUID sGuid;

	if(wstrProperty->empty())
	{
		CoCreateGuid(&sGuid);
		wstrProperty->assign(convert_to<std::wstring>(bin2hex(sizeof(GUID), &sGuid)));
	}

	assert(PROP_TYPE(ulPropTag) == PT_UNICODE);
	sPropVal.ulPropTag = ulPropTag;
	sPropVal.Value.lpszW = (LPWSTR)wstrProperty->c_str();
	auto hr = HrGetOneProp(lpMapiProp, sPropVal.ulPropTag, &~lpMsgProp);
	if (hr == MAPI_E_NOT_FOUND) {
		hr = HrSetOneProp(lpMapiProp, &sPropVal);
		if (hr == E_ACCESSDENIED && bFldId)
		{
			hr = HrGetOneProp(lpMapiProp, PR_ENTRYID, &~lpMsgProp);
			if(hr != hrSuccess)
				return hr;
			wstrProperty->assign(convert_to<std::wstring>(bin2hex(lpMsgProp->Value.bin)));
		}
	} else if (hr == hrSuccess)
		wstrProperty->assign(lpMsgProp->Value.lpszW);
	return hr;
}

/**
 * Add Property to the folder or message
 *
 * @param[in]		lpMsgStore		Pointer to store of the user
 * @param[in]		sbEid			EntryID of the folder to which property has to be added
 * @param[in]		ulPropertyId	Property Tag of the property to be added
 * @param[in]		bIsFldID		Boolean to state if the property to be added is FolderID or not
 * @param[in,out]	lpwstrProperty	String value of the property, if empty then GUID is created and added
 *
 * @return			HRESULT
 */
HRESULT HrAddProperty(IMsgStore *lpMsgStore, SBinary sbEid, ULONG ulPropertyId, bool bIsFldID, std::wstring *lpwstrProperty )
{
	object_ptr<IMAPIFolder> lpUsrFld;
	ULONG ulObjType = 0;
	HRESULT hr = lpMsgStore->OpenEntry(sbEid.cb, reinterpret_cast<ENTRYID *>(sbEid.lpb),
	             &iid_of(lpUsrFld), MAPI_BEST_ACCESS, &ulObjType, &~lpUsrFld);
	if(hr != hrSuccess)
		return hr;
	hr = HrAddProperty(lpUsrFld, ulPropertyId, bIsFldID, lpwstrProperty);
	if(hr != hrSuccess)
		return hr;
	return lpUsrFld->SaveChanges(KEEP_OPEN_READWRITE);
}

/**
 * Finds the folder in the store by using the its Folder ID (if
 * prefixed with FOLDER_PREFIX), or the folder name.
 *
 * Folder-id is could be either named property (dispidFldID) or PR_ENTRYID
 * and I'm not sure why, but I'll leave this for now.
 *
 * @param[in]	lpMsgStore		Pointer to users store (used for inbox/outbox)
 * @param[in]	lpRootFolder	Pointer to users root container (starting point in hierarchy, IPMSubtree most likely)
 * @param[in]	lpNamedProps	Named property tag array
 * @param[in]	wstrFldId		Folder-id of the folder to be searched
 * @param[out]	lppUsrFld		Return pointer for the folder found
 *
 * @return		mapi error codes
 * @retval		MAPI_E_NOT_FOUND	Folder refrenced by folder-id not found
 *
 * @todo	add some check to remove the dirty >50 length check
 */
HRESULT HrFindFolder(IMsgStore *lpMsgStore, IMAPIFolder *lpRootFolder,
    SPropTagArray *lpNamedProps, const std::wstring &wstrFldIdOrig,
    IMAPIFolder **lppUsrFld)
{
	std::string strBinEid;
	object_ptr<IMAPITable> lpHichyTable;
	SPropValue sPropFolderID, sPropFolderName;
	ULONG ulPropTagFldId = 0;
	rowset_ptr lpRows;
	SBinary sbEid = {0,0};
	IMAPIFolder *lpUsrFld = NULL;
	unsigned int ulObjType = 0, cbEntryID = 0;
	convert_context converter;
	memory_ptr<SPropValue> folder;
	static constexpr const SizedSPropTagArray(1, sPropTagArr) = {1, {PR_ENTRYID}};
	auto wstrFldId = wstrFldIdOrig;
	memory_ptr<ENTRYID> lpEntryID;

	// wstrFldId can be:
	//   FOLDER_PREFIX + hexed named folder id
	//   FOLDER_PREFIX + hexed entry id
	//   folder name
	if (wstrFldId.find(FOLDER_PREFIX) == 0)
		wstrFldId.erase(0, wcslen(FOLDER_PREFIX));

	// Hack Alert #47 -- get Inbox and Outbox as special folders
	if (wstrFldId.compare(L"Inbox") == 0) {
		auto hr = lpMsgStore->GetReceiveFolder(KC_T("IPM"), fMapiUnicode, &cbEntryID, &~lpEntryID, nullptr);
		if (hr != hrSuccess)
			return kc_perror("Cannot open Inbox folder, no receive folder entryID", hr);
		hr = MAPIAllocateBuffer(sizeof(SPropValue), &~folder);
		if (hr != hrSuccess)
			return hr;
		folder->Value.bin.cb = cbEntryID;
		folder->Value.bin.lpb = reinterpret_cast<BYTE *>(lpEntryID.get());
	} else if (wstrFldId.compare(L"Outbox") == 0) {
		memory_ptr<SPropValue> lpOutbox;
		auto hr = HrGetOneProp(lpMsgStore, PR_IPM_OUTBOX_ENTRYID, &~lpOutbox);
		if (hr != hrSuccess)
			return kc_perror("Cannot open Outbox folder, no PR_IPM_OUTBOX_ENTRYID", hr);
		folder = std::move(lpOutbox);
	}
	if (folder) {
		auto hr = lpMsgStore->OpenEntry(folder->Value.bin.cb, reinterpret_cast<ENTRYID *>(folder->Value.bin.lpb), &IID_IMAPIFolder, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN*)lppUsrFld);
		if (hr != hrSuccess)
			ec_log_err("Cannot open folder \"%ls\": %s (%x)",
				wstrFldId.c_str(), GetMAPIErrorMessage(hr), hr);
		// we're done either way
		return hr;
	}

	auto hr = lpRootFolder->GetHierarchyTable(CONVENIENT_DEPTH, &~lpHichyTable);
	if(hr != hrSuccess)
		return hr;
	//When ENTRY_ID is use For Read Only Calendars assuming the folder id string
	//not larger than lenght 50
	//FIXME: include some Entry-id identifier
	if(wstrFldId.size()> 50)
	{
		ulPropTagFldId = PR_ENTRYID;
		strBinEid = hex2bin(wstrFldId);
		sPropFolderID.Value.bin.cb = strBinEid.size();
		sPropFolderID.Value.bin.lpb = (LPBYTE)strBinEid.c_str();
	}
	else
	{
		// note: this is a custom kopano named property, defined in libicalmapi/names.*
		ulPropTagFldId = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_FLDID], PT_UNICODE);
		sPropFolderID.Value.lpszW = (LPWSTR)wstrFldId.c_str();
	}
	sPropFolderID.ulPropTag = ulPropTagFldId;
	sPropFolderName.ulPropTag = PR_DISPLAY_NAME_W;
	sPropFolderName.Value.lpszW = (WCHAR*)wstrFldId.c_str();

	// @note, this will find the first folder using this name (1 level, eg 'Calendar', no subfolders in caldav)
	// so if you have Calendar and subfolder/Calendar, the latter will not be able to open using names, but must use IDs.
	hr = ECOrRestriction(
		ECPropertyRestriction(RELOP_EQ, ulPropTagFldId, &sPropFolderID, ECRestriction::Cheap) +
		ECContentRestriction(FL_IGNORECASE, PR_DISPLAY_NAME_W, &sPropFolderName, ECRestriction::Cheap)
	).RestrictTable(lpHichyTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpHichyTable->SetColumns(sPropTagArr, 0);
	if(hr != hrSuccess)
		return hr;
	hr = lpHichyTable->QueryRows(1, 0, &~lpRows);
	if (hr != hrSuccess)
		return hr;
	if (lpRows->cRows != 1)
		return MAPI_E_NOT_FOUND;
	sbEid = lpRows[0].lpProps[0].Value.bin;
	hr = lpMsgStore->OpenEntry(sbEid.cb, reinterpret_cast<ENTRYID *>(sbEid.lpb),
	     &iid_of(lpUsrFld), MAPI_BEST_ACCESS, &ulObjType, reinterpret_cast<IUnknown **>(&lpUsrFld));
	if(hr != hrSuccess)
		return hr;
	*lppUsrFld = lpUsrFld;
	return hr;
}

/**
 * Set supported-report-set properties, used by mac ical.app client
 *
 * @param[in,out]	lpsProperty		Pointer to WEBDAVPROPERTY structure to store the supported-report-set properties
 *
 * @return			HRESULT			Always set to hrSuccess
 */
HRESULT HrBuildReportSet(WEBDAVPROPERTY *lpsProperty)
{
	int ulDepth = 0 ;
	WEBDAVITEM sDavItem;

	sDavItem.sDavValue.sPropName.strPropname = "supported-report";
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "report";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "acl-principal-prop-set";
	sDavItem.ulDepth = ulDepth + 2 ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "supported-report";
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "report";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "principal-match";
	sDavItem.ulDepth = ulDepth + 2 ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "supported-report";
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "report";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "principal-property-search";
	sDavItem.ulDepth = ulDepth + 2 ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "supported-report";
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "report";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "expand-property";
	sDavItem.ulDepth = ulDepth + 2 ;
	lpsProperty->lstItems.emplace_back(sDavItem);
	return hrSuccess;
}

/**
 * Set Acl properties, used by mac ical.app client
 *
 * @param[in,out]	lpsProperty		Pointer to WEBDAVPROPERTY structure to store the acl properties
 *
 * @return			HRESULT			Always set to hrSuccess
 */
HRESULT HrBuildACL(WEBDAVPROPERTY *lpsProperty)
{
	int ulDepth = 0 ;
	WEBDAVITEM sDavItem;

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "all";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "read-current-user-privilege-set";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "read";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "write";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "write-content";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "write-properties";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "unlock";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "read-acl";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "bind";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);
	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.emplace_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "unbind";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.emplace_back(sDavItem);
	return hrSuccess;
}

/**
 * Return the GUID value from the input string
 *
 * Input string is of format '/caldav/Calendar name/asbxjk3-3980434-xn49cn4930.ics',
 * function returns 'asbxjk3-3980434-xn49cn4930'
 *
 * @param[in]	strInput	Input string contaning guid
 * @return		string		string countaing guid
 */
std::string StripGuid(const std::string &strInput)
{
	auto ulFoundSlash = strInput.rfind('/');
	if (ulFoundSlash == std::string::npos)
		ulFoundSlash = 0;
	else
		++ulFoundSlash;
	auto ulFound = strInput.find(".ics", ulFoundSlash);
	if (ulFound != std::string::npos)
		return {strInput.cbegin() + ulFoundSlash, strInput.cbegin() + ulFound};
	return "";
}

/**
 * Get owner of given store
 *
 * @param[in]	lpSession			IMAPISession object pointer of the user
 * @param[in]	lpDefStore			Default store to get ownership info from
 * @param[out]	lppImailUser		IMailUser Object pointer of the owner
 *
 * @return		HRESULT
 */
HRESULT HrGetOwner(IMAPISession *lpSession, IMsgStore *lpDefStore, IMailUser **lppImailUser)
{
	SPropValuePtr ptrSProp;
	object_ptr<IMailUser> lpMailUser;
	ULONG ulObjType = 0;

	HRESULT hr = HrGetOneProp(lpDefStore, PR_MAILBOX_OWNER_ENTRYID, &~ptrSProp);
	if(hr != hrSuccess)
		return hr;
	hr = lpSession->OpenEntry(ptrSProp->Value.bin.cb, reinterpret_cast<ENTRYID *>(ptrSProp->Value.bin.lpb),
	     &iid_of(lpMailUser), MAPI_BEST_ACCESS, &ulObjType, &~lpMailUser);
	if(hr != hrSuccess)
		return hr;
	*lppImailUser = lpMailUser.release();
	return hrSuccess;
}

/**
 * Get all calendar folder of a specified folder, also includes all sub folders
 *
 * @param[in]	lpSession		IMAPISession object of the user
 * @param[in]	lpFolder		IMAPIFolder object for which all calendar are to returned(Optional, can be set to NULL if lpsbEid != NULL)
 * @param[in]	lpsbEid			EntryID of the Folder for which all calendar are to be returned(can be NULL if lpFolderIn != NULL)
 * @param[out]	lppTable		IMAPITable of the sub calendar of the folder
 *
 * @return		HRESULT
 */
HRESULT HrGetSubCalendars(IMAPISession *lpSession, IMAPIFolder *lpFolder,
    SBinary *lpsbEid, IMAPITable **lppTable)
{
	object_ptr<IMAPIFolder> local_fld;
	ULONG ulObjType = 0;
	IMAPITable *lpTable = NULL;
	SPropValue sPropVal;
	ECOrRestriction rst;

	if (lpFolder == nullptr) {
		auto hr = lpSession->OpenEntry(lpsbEid->cb, reinterpret_cast<ENTRYID *>(lpsbEid->lpb),
		          &iid_of(local_fld), MAPI_BEST_ACCESS, &ulObjType, &~local_fld);
		if(hr != hrSuccess)
			return hr;
		lpFolder = local_fld.get();
	}

	auto hr = lpFolder->GetHierarchyTable(CONVENIENT_DEPTH, &lpTable);
	if(hr != hrSuccess)
		return hr;

	sPropVal.ulPropTag = PR_CONTAINER_CLASS_A;
	sPropVal.Value.lpszA = const_cast<char *>("IPF.Appointment");
	rst += ECContentRestriction(FL_IGNORECASE, sPropVal.ulPropTag, &sPropVal, ECRestriction::Shallow);
	sPropVal.Value.lpszA = const_cast<char *>("IPF.Task");
	rst += ECContentRestriction(FL_IGNORECASE, sPropVal.ulPropTag, &sPropVal, ECRestriction::Shallow);
	hr = rst.RestrictTable(lpTable);
	if (hr != hrSuccess)
		return hr;
	*lppTable = lpTable;
	return hrSuccess;
}

/**
 * Check for delegate permissions on the store
 * @param[in]	lpDefStore		The users default store
 * @param[in]	lpSharedStore	The store of the other user
 *
 * @return		bool			True if permissions are set or false
 */
bool HasDelegatePerm(IMsgStore *lpDefStore, IMsgStore *lpSharedStore)
{
	object_ptr<IMessage> lpFbMessage;
	memory_ptr<SPropValue> lpProp, lpMailBoxEid;
	object_ptr<IMAPIContainer> lpRootCont;
	unsigned int ulType = 0, ulPos = 0;
	SBinary sbEid = {0,0};
	bool blFound = false;

	HRESULT hr = HrGetOneProp(lpDefStore, PR_MAILBOX_OWNER_ENTRYID, &~lpMailBoxEid);
	if (hr != hrSuccess)
		return false;
	hr = lpSharedStore->OpenEntry(0, nullptr, &iid_of(lpRootCont), 0, &ulType, &~lpRootCont);
	if (hr != hrSuccess)
		return false;
	hr = HrGetOneProp(lpRootCont, PR_FREEBUSY_ENTRYIDS, &~lpProp);
	if (hr != hrSuccess)
		return false;

	if (lpProp->Value.MVbin.cValues > 1 && lpProp->Value.MVbin.lpbin[1].cb != 0)
		sbEid = lpProp->Value.MVbin.lpbin[1];
	else
		return false;
	hr = lpSharedStore->OpenEntry(sbEid.cb, reinterpret_cast<ENTRYID *>(sbEid.lpb),
	     &iid_of(lpFbMessage), MAPI_BEST_ACCESS, &ulType, &~lpFbMessage);
	if (hr != hrSuccess)
		return false;
	hr = HrGetOneProp(lpFbMessage, PR_SCHDINFO_DELEGATE_ENTRYIDS, &~lpProp);
	if (hr != hrSuccess)
		return false;

	for (ULONG i = 0; i < lpProp->Value.MVbin.cValues; ++i) {
		if (lpProp->Value.MVbin.lpbin[i].cb == lpMailBoxEid->Value.bin.cb &&
		    memcmp(lpProp->Value.MVbin.lpbin[i].lpb, lpMailBoxEid->Value.bin.lpb, lpMailBoxEid->Value.bin.cb) == 0)
		{
			blFound = true;
			ulPos = i;
			break;
		}
	}

	if (!blFound)
		return false;
	hr = HrGetOneProp(lpFbMessage, PR_DELEGATE_FLAGS, &~lpProp);
	if (hr != hrSuccess)
		return false;
	return lpProp->Value.MVl.cValues >= ulPos && lpProp->Value.MVl.lpl[ulPos];
}
/**
 * Check if the message is a private item
 *
 * @param[in]	lpMessage			message to be checked
 * @param[in]	ulPropIDPrivate		named property tag
 * @return		bool
 */
bool IsPrivate(LPMESSAGE lpMessage, ULONG ulPropIDPrivate)
{
	memory_ptr<SPropValue> lpPropPrivate;
	return HrGetOneProp(lpMessage, ulPropIDPrivate, &~lpPropPrivate) == hrSuccess &&
	       lpPropPrivate->Value.b == TRUE;
}

/**
 * Creates restriction to find calendar entries refrenced by strGuid.
 *
 * @param[in]	strGuid			Guid string of calendar entry requested by caldav client, in url-base64 mode
 * @param[in]	lpNamedProps	Named property tag array
 * @param[out]	lpsRectrict		Pointer to the restriction created
 *
 * @return		HRESULT
 * @retval		MAPI_E_INVALID_PARAMETER	null parameter is passed in lpsRectrict
 */
HRESULT HrMakeRestriction(const std::string &strGuid, LPSPropTagArray lpNamedProps, LPSRestriction *lpsRectrict)
{
	LPSRestriction lpsRoot = NULL;
	std::string strBinGuid;
	SPropValue sSpropVal = {0};
	ECOrRestriction rst;

	if (lpsRectrict == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// convert guid to outlook format
	if (IsOutlookUid(strGuid))
		strBinGuid = hex2bin(strGuid);
	else
		HrMakeBinUidFromICalUid(strGuid, &strBinGuid);

	sSpropVal.Value.bin.cb = (ULONG)strBinGuid.size();
	sSpropVal.Value.bin.lpb = (LPBYTE)strBinGuid.c_str();
	sSpropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
	rst += ECPropertyRestriction(RELOP_EQ, sSpropVal.ulPropTag, &sSpropVal, ECRestriction::Shallow);

	// converting guid to hex
	auto strBinOtherUID = hex2bin(strGuid);
	sSpropVal.ulPropTag = PR_ENTRYID;
	sSpropVal.Value.bin.cb = (ULONG)strBinOtherUID.size();
	sSpropVal.Value.bin.lpb = (LPBYTE)strBinOtherUID.c_str();

	// When CreateAndGetGuid() fails PR_ENTRYID is used as guid.
	rst += ECPropertyRestriction(RELOP_EQ, PR_ENTRYID, &sSpropVal, ECRestriction::Shallow);

	// z-push iphone UIDs are not in Outlook format
	sSpropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
	rst += ECPropertyRestriction(RELOP_EQ, sSpropVal.ulPropTag, &sSpropVal, ECRestriction::Shallow);

	// PUT url [guid].ics part, (e.g. Evolution UIDs)
	sSpropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTTSREF], PT_STRING8);
	sSpropVal.Value.lpszA = (char*)strGuid.c_str();
	rst += ECPropertyRestriction(RELOP_EQ, sSpropVal.ulPropTag, &sSpropVal, ECRestriction::Shallow);
	auto hr = rst.CreateMAPIRestriction(&lpsRoot, ECRestriction::Full);
	if (lpsRoot && lpsRectrict)
		*lpsRectrict = std::move(lpsRoot);
	return hr;
}

/**
 * Finds mapi message in the folder using the UID string.
 * UID string can be PR_ENTRYID/GlobalOjectId of the message.
 *
 * @param[in]	strGuid			Guid value of the message to be searched
 * @param[in]	lpUsrFld		Mapi folder in which the message has to be searched
 * @param[in]	lpNamedProps	Named property tag array
 * @param[out]	lppMessage		if found the mapi message is returned
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	No message found containing the guid value.
 */
HRESULT HrFindAndGetMessage(const std::string &strGuid, IMAPIFolder *lpUsrFld,
    SPropTagArray *lpNamedProps, IMessage **lppMessage)
{
	memory_ptr<SRestriction> lpsRoot;
	rowset_ptr lpValRows;
	object_ptr<IMAPITable> lpTable;
	object_ptr<IMessage> lpMessage;
	ULONG ulObjType = 0;
	static constexpr const SizedSPropTagArray(1, sPropTagArr) = {1, {PR_ENTRYID}};

	HRESULT hr = HrMakeRestriction(strGuid, lpNamedProps, &~lpsRoot);
	if (hr != hrSuccess)
		return hr;
	hr = lpUsrFld->GetContentsTable(0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->SetColumns(sPropTagArr, 0);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->Restrict(lpsRoot, TBL_BATCH);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->QueryRows(1, 0, &~lpValRows);
	if (hr != hrSuccess)
		return hr;
	if (lpValRows->cRows != 1)
		return MAPI_E_NOT_FOUND;
	if (PROP_TYPE(lpValRows[0].lpProps[0].ulPropTag) != PT_BINARY)
		return MAPI_E_NOT_FOUND;
	SBinary sbEid = lpValRows[0].lpProps[0].Value.bin;
	hr = lpUsrFld->OpenEntry(sbEid.cb, reinterpret_cast<ENTRYID *>(sbEid.lpb),
	     &iid_of(lpMessage), MAPI_MODIFY, &ulObjType, &~lpMessage);
	if (hr != hrSuccess)
		return hr;
	*lppMessage = lpMessage.release();
	return hrSuccess;
}

/**
 * Retrieves freebusy information of attendees and converts it to ical data
 *
 * @param[in]	lpMapiToIcal	Mapi to ical conversion object
 * @param[in]	lpFBSupport		IFreebusySupport object used to retrive freebusy information of attendee
 * @param[in]	lpAddrBook		Addressbook used for user lookups
 * @param[in]	lplstUsers		List of attendees whose freebusy is requested
 * @param[out]	lpFbInfo		Structure which stores the retrieved ical data for the attendees
 *
 * @return MAPI error code
 * @retval hrSuccess valid users have freebusy blocks, invalid users have empty string in ical data. (see xml converter for request-status code)
 */
HRESULT HrGetFreebusy(MapiToICal *lpMapiToIcal, IFreeBusySupport* lpFBSupport, IAddrBook *lpAddrBook, std::list<std::string> *lplstUsers, WEBDAVFBINFO *lpFbInfo)
{
	memory_ptr<FBUser> lpUsers;
	IFreeBusyData **lppFBData = NULL;
	memory_ptr<FBBlock_1> lpsFBblks;
	std::string strMethod, strIcal;
	FILETIME ftStart = {0, 0}, ftEnd = {0, 0};
	LONG lMaxblks = 100, lblkFetched = 0;
	WEBDAVFBUSERINFO sWebFbUserInfo;
	std::list<std::string>::const_iterator itUsers;
	adrlist_ptr lpAdrList;
	memory_ptr<FlagList> ptrFlagList;
	EntryIdPtr ptrEntryId;
	unsigned int cFBData = 0, cbEntryId = 0, ulObj = 0;
	ABContainerPtr ptrABDir;

	HRESULT hr = lpAddrBook->GetDefaultDir(&cbEntryId, &~ptrEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = lpAddrBook->OpenEntry(cbEntryId, ptrEntryId, &iid_of(ptrABDir), 0, &ulObj, &~ptrABDir);
	if (hr != hrSuccess)
		return hr;
	unsigned int cUsers = lplstUsers->size();
	hr = MAPIAllocateBuffer(CbNewADRLIST(cUsers), &~lpAdrList);
	if(hr != hrSuccess)
		return hr;
	lpAdrList->cEntries = 0;
	hr = MAPIAllocateBuffer(CbNewFlagList(cUsers), &~ptrFlagList);
	if (hr != hrSuccess)
		return hr;

	ptrFlagList->cFlags = cUsers;
	cUsers = 0;
	for (const auto &user : *lplstUsers) {
		lpAdrList->aEntries[cUsers].cValues = 1;
		hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpAdrList->aEntries[cUsers].rgPropVals);
		if(hr != hrSuccess)
			return hr;
		++lpAdrList->cEntries;
		lpAdrList->aEntries[cUsers].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_A;
		lpAdrList->aEntries[cUsers].rgPropVals[0].Value.lpszA = const_cast<char *>(user.c_str());
		ptrFlagList->ulFlag[cUsers] = MAPI_UNRESOLVED;
		++cUsers;
	}

	// NULL or sptaAddrListProps containing just PR_ENTRYID?
	hr = ptrABDir->ResolveNames(NULL, EMS_AB_ADDRESS_LOOKUP, lpAdrList, ptrFlagList);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(FBUser)*cUsers, &~lpUsers);
	if (hr != hrSuccess)
		return hr;

	// Get the user entryids
	for (cUsers = 0; cUsers < lpAdrList->cEntries; ++cUsers) {
		const SPropValue *lpEntryID = nullptr;
		if (ptrFlagList->ulFlag[cUsers] == MAPI_RESOLVED)
			lpEntryID = lpAdrList->aEntries[cUsers].cfind(PR_ENTRYID);
		if (lpEntryID == nullptr) {
			lpUsers[cUsers].m_cbEid = 0;
			lpUsers[cUsers].m_lpEid = NULL;
			continue;
		}
		lpUsers[cUsers].m_cbEid = lpEntryID->Value.bin.cb;
		hr = KAllocCopy(lpEntryID->Value.bin.lpb, lpEntryID->Value.bin.cb, reinterpret_cast<void **>(&lpUsers[cUsers].m_lpEid), lpUsers);
		if (hr != hrSuccess)
			return hr;
	}

	hr = MAPIAllocateBuffer(sizeof(IFreeBusyData*)*cUsers, (void **)&lppFBData);
	if (hr != hrSuccess)
		return hr;
	memset(lppFBData, '\0', sizeof(IFreeBusyData *) * cUsers);

	// retrieve freebusy for the attendees
	hr = lpFBSupport->LoadFreeBusyData(cUsers, lpUsers, lppFBData, NULL, &cFBData);
	if (hr != hrSuccess)
		goto exit;
	ftStart = UnixTimeToFileTime(lpFbInfo->tStart);
	ftEnd   = UnixTimeToFileTime(lpFbInfo->tEnd);
	itUsers = lplstUsers->cbegin();
	// iterate through all users
	for (ULONG i = 0; i < cUsers; ++i) {
		object_ptr<IEnumFBBlock> lpEnumBlock;
		strIcal.clear();

		if (!lppFBData[i])
			goto next;
		hr = lppFBData[i]->EnumBlocks(&~lpEnumBlock, ftStart, ftEnd);
		if (hr != hrSuccess)
			goto next;
		hr = MAPIAllocateBuffer(sizeof(FBBlock_1)*lMaxblks, &~lpsFBblks);
		if (hr != hrSuccess)
			goto next;
		// restrict the freebusy blocks
		hr = lpEnumBlock->Restrict(ftStart, ftEnd);
		if (hr != hrSuccess)
			goto next;

		// ignore error
		lpEnumBlock->Next(lMaxblks, lpsFBblks, &lblkFetched);
		// add freebusy blocks to ical data
		if (lblkFetched == 0)
			hr = lpMapiToIcal->AddBlocks(NULL, lblkFetched, lpFbInfo->tStart, lpFbInfo->tEnd, lpFbInfo->strOrganiser, *itUsers, lpFbInfo->strUID);
		else
			hr = lpMapiToIcal->AddBlocks(lpsFBblks, lblkFetched, lpFbInfo->tStart, lpFbInfo->tEnd, lpFbInfo->strOrganiser, *itUsers, lpFbInfo->strUID);
		if (hr != hrSuccess)
			goto next;

		// retrieve VFREEBUSY ical data
		lpMapiToIcal->Finalize(M2IC_NO_VTIMEZONE, &strMethod, &strIcal);
next:
		sWebFbUserInfo.strUser = *itUsers;
		sWebFbUserInfo.strIcal = strIcal;
		lpFbInfo->lstFbUserInfo.emplace_back(sWebFbUserInfo);
		++itUsers;
		lpMapiToIcal->ResetObject();
		lblkFetched = 0;
	}
	// ignoring ical data for unknown users.
	hr = hrSuccess;

exit:
	if (lppFBData) {
		for (ULONG i = 0; i < cUsers; ++i)
			if (lppFBData[i])
				lppFBData[i]->Release();
		MAPIFreeBuffer(lppFBData);
	}
	return hr;
}
