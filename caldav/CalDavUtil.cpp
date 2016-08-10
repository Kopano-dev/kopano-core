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
#include "CalDavUtil.h"
#include <kopano/EMSAbTag.h>
#include <kopano/charset/convert.h>
#include <kopano/mapi_ptr.h>

using namespace std;


/**
 * Open MAPI session
 *
 * @param[in]	strUser		User's login name
 * @param[in]	strPass		User's password
 * @param[in]	strPath		Kopano server's path
 * @param[out]	lppSession	IMAPISession object if login is successful
 * @return		HRESULT
 * @retval		MAPI_E_LOGON_FAILED		Unable to login with the specified user-name and password
 */
HRESULT HrAuthenticate(ECLogger *const lpLogger, const std::string & appVersion, const std::string & appMisc, const std::wstring &wstrUser, const std::wstring &wstrPass, std::string strPath, IMAPISession **lppSession)
{
	// @todo: if login with utf8 username is not possible, lookup user from addressbook? but how?
	return HrOpenECSession(lpLogger, lppSession, appVersion.c_str(), appMisc.c_str(), wstrUser.c_str(), wstrPass.c_str(), strPath.c_str(), 0, NULL, NULL, NULL);
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
	HRESULT hr = hrSuccess;
	SPropValue sPropVal;
	LPSPropValue lpMsgProp = NULL;
	GUID sGuid;

	if(wstrProperty->empty())
	{
		CoCreateGuid(&sGuid);
		wstrProperty->assign(convert_to<wstring>(bin2hex(sizeof(GUID), (LPBYTE)&sGuid)));
	}

	ASSERT(PROP_TYPE(ulPropTag) == PT_UNICODE);
	sPropVal.ulPropTag = ulPropTag;
	sPropVal.Value.lpszW = (LPWSTR)wstrProperty->c_str();

	hr = HrGetOneProp(lpMapiProp, sPropVal.ulPropTag, &lpMsgProp);
	if (hr == MAPI_E_NOT_FOUND) {
		hr = HrSetOneProp(lpMapiProp, &sPropVal);
		if (hr == E_ACCESSDENIED && bFldId)
		{
			hr = HrGetOneProp(lpMapiProp, PR_ENTRYID, &lpMsgProp);
			if(hr != hrSuccess)
				goto exit;

			wstrProperty->assign(convert_to<wstring>(bin2hex(lpMsgProp->Value.bin.cb, lpMsgProp->Value.bin.lpb)));
		}
	} else if (hr != hrSuccess)
		goto exit;
	else
		wstrProperty->assign(lpMsgProp->Value.lpszW);

exit:
	MAPIFreeBuffer(lpMsgProp);
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
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpUsrFld = NULL;
	ULONG ulObjType = 0;
	
	hr = lpMsgStore->OpenEntry(sbEid.cb, (LPENTRYID)sbEid.lpb, NULL, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN *) &lpUsrFld);
	if(hr != hrSuccess)
		goto exit;

	hr = HrAddProperty(lpUsrFld, ulPropertyId, bIsFldID, lpwstrProperty);
	if(hr != hrSuccess)
		goto exit;

	hr = lpUsrFld->SaveChanges(KEEP_OPEN_READWRITE);

exit:
	if(lpUsrFld)
		lpUsrFld->Release();

	return hr;
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
 * @param[in]	lpLogger		Pointer to ECLogger	object 
 * @param[in]	wstrFldId		Folder-id of the folder to be searched
 * @param[out]	lppUsrFld		Return pointer for the folder found
 *
 * @return		mapi error codes
 * @retval		MAPI_E_NOT_FOUND	Folder refrenced by folder-id not found
 *
 * @todo	add some check to remove the dirty >50 length check
 */
HRESULT HrFindFolder(IMsgStore *lpMsgStore, IMAPIFolder *lpRootFolder, LPSPropTagArray lpNamedProps, ECLogger *lpLogger, std::wstring wstrFldId, IMAPIFolder **lppUsrFld)
{
	HRESULT hr = hrSuccess;
	std::string strBinEid;
	SRestriction *lpRestrict = NULL;
	IMAPITable *lpHichyTable = NULL;
	SPropValue sPropFolderID;
	SPropValue sPropFolderName;
	ULONG ulPropTagFldId = 0;
	SRowSet *lpRows = NULL;
	SBinary sbEid = {0,0};
	IMAPIFolder *lpUsrFld = NULL;
	ULONG ulObjType = 0;
	convert_context converter;
	ULONG cbEntryID = 0;
	LPENTRYID lpEntryID = NULL;
	LPSPropValue lpOutbox = NULL;
	SizedSPropTagArray(1, sPropTagArr) = {1, {PR_ENTRYID}};

	// wstrFldId can be:
	//   FOLDER_PREFIX + hexed named folder id
	//   FOLDER_PREFIX + hexed entry id
	//   folder name

	if (wstrFldId.find(FOLDER_PREFIX) == 0)
		wstrFldId.erase(0, wcslen(FOLDER_PREFIX));

	// Hack Alert #47 -- get Inbox and Outbox as special folders
	if (wstrFldId.compare(L"Inbox") == 0) {
		hr = lpMsgStore->GetReceiveFolder(const_cast<TCHAR *>(_T("IPM")), fMapiUnicode, &cbEntryID, &lpEntryID, NULL);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Cannot open Inbox Folder, no Receive Folder EntryID: 0x%08X", hr);
			goto exit;
		}
	} else if (wstrFldId.compare(L"Outbox") == 0) {
		hr = HrGetOneProp(lpMsgStore, PR_IPM_OUTBOX_ENTRYID, &lpOutbox);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Cannot open Outbox Folder, no PR_IPM_OUTBOX_ENTRYID: 0x%08X", hr);
			goto exit;
		}
		cbEntryID = lpOutbox->Value.bin.cb;
		lpEntryID = (LPENTRYID)lpOutbox->Value.bin.lpb;
	}
	if (cbEntryID && lpEntryID) {
		hr = lpMsgStore->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN*)lppUsrFld);
		if (hr != hrSuccess)
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Cannot open %ls Folder: 0x%08X", wstrFldId.c_str(), hr);
		// we're done either way
		goto exit;
	}


	hr = lpRootFolder->GetHierarchyTable(CONVENIENT_DEPTH, &lpHichyTable);
	if(hr != hrSuccess)
		goto exit;
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

	CREATE_RESTRICTION(lpRestrict);
	CREATE_RES_OR(lpRestrict, lpRestrict, 2);
	DATA_RES_PROPERTY(lpRestrict, lpRestrict->res.resOr.lpRes[0], RELOP_EQ, ulPropTagFldId, &sPropFolderID);
	// @note, this will find the first folder using this name (1 level, eg 'Calendar', no subfolders in caldav)
	// so if you have Calendar and subfolder/Calender, the latter won't be able to open using names, but must use id's.
	DATA_RES_CONTENT(lpRestrict, lpRestrict->res.resOr.lpRes[1], FL_IGNORECASE, PR_DISPLAY_NAME_W, &sPropFolderName);

	hr = lpHichyTable->Restrict(lpRestrict,TBL_BATCH);
	if(hr != hrSuccess)
		goto exit;

	hr = lpHichyTable->SetColumns((LPSPropTagArray)&sPropTagArr, 0);
	if(hr != hrSuccess)
		goto exit;
	
	hr = lpHichyTable->QueryRows(1,0,&lpRows);
	if (hr != hrSuccess)
		goto exit;

	if (lpRows->cRows != 1) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	
	sbEid = lpRows->aRow[0].lpProps[0].Value.bin;
	
	hr = lpMsgStore->OpenEntry(sbEid.cb, (LPENTRYID)sbEid.lpb,NULL, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN *) &lpUsrFld);
	if(hr != hrSuccess)
		goto exit;

	*lppUsrFld = lpUsrFld;

exit:
	// Free either one. lpEntryID may point to lpOutbox memory.
	if (lpOutbox)
		MAPIFreeBuffer(lpOutbox);
	else if (lpEntryID)
		MAPIFreeBuffer(lpEntryID);

	if(lpRows)
		FreeProws(lpRows);

	if(lpHichyTable)
		lpHichyTable->Release();

	if(lpRestrict)
		FREE_RESTRICTION(lpRestrict);

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
	HRESULT hr = hrSuccess;
	int ulDepth = 0 ;
	WEBDAVITEM sDavItem;

	sDavItem.sDavValue.sPropName.strPropname = "supported-report";
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "report";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "acl-principal-prop-set";
	sDavItem.ulDepth = ulDepth + 2 ;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "supported-report";
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "report";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "principal-match";
	sDavItem.ulDepth = ulDepth + 2 ;
	lpsProperty->lstItems.push_back(sDavItem);
	
	sDavItem.sDavValue.sPropName.strPropname = "supported-report";	
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "report";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "principal-property-search";
	sDavItem.ulDepth = ulDepth + 2 ;
	lpsProperty->lstItems.push_back(sDavItem);
	
	sDavItem.sDavValue.sPropName.strPropname = "supported-report";	
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "report";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "expand-property";
	sDavItem.ulDepth = ulDepth + 2 ;
	lpsProperty->lstItems.push_back(sDavItem);

	return hr;
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
	HRESULT hr = hrSuccess;
	int ulDepth = 0 ;
	WEBDAVITEM sDavItem;

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.sDavValue.sPropName.strNS = WEBDAVNS;
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "all";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "read-current-user-privilege-set";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "read";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth ;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "write";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "write-content";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "write-properties";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "unlock";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "read-acl";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "bind";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);
	sDavItem.sDavValue.sPropName.strPropname = "privilege";
	sDavItem.ulDepth = ulDepth;
	lpsProperty->lstItems.push_back(sDavItem);

	sDavItem.sDavValue.sPropName.strPropname = "unbind";
	sDavItem.ulDepth = ulDepth + 1;
	lpsProperty->lstItems.push_back(sDavItem);

	return hr;
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
	size_t ulFound = -1;
	size_t ulFoundSlash = -1;
	std::string strRetVal;

	ulFoundSlash = strInput.rfind('/');
	if(ulFoundSlash == string::npos)
		ulFoundSlash = 0;
	else
		++ulFoundSlash;

	ulFound = strInput.rfind(".ics");
	if(ulFound != wstring::npos)
		strRetVal.assign(strInput.begin() + ulFoundSlash, strInput.begin() + ulFound);

	return strRetVal;
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
	HRESULT hr = hrSuccess;
	SPropValuePtr ptrSProp;
	IMailUser *lpMailUser = NULL;
	ULONG ulObjType = 0;

	hr = HrGetOneProp(lpDefStore, PR_MAILBOX_OWNER_ENTRYID, &ptrSProp);
	if(hr != hrSuccess)
		goto exit;
	
	hr = lpSession->OpenEntry(ptrSProp->Value.bin.cb, (LPENTRYID)ptrSProp->Value.bin.lpb, NULL, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN*)&lpMailUser);
	if(hr != hrSuccess)
		goto exit;

	*lppImailUser = lpMailUser;
	lpMailUser = NULL;

exit:
	if(lpMailUser)
		lpMailUser->Release();

	return hr;
}

/**
 * Get all calendar folder of a specified folder, also includes all sub folders
 * 
 * @param[in]	lpSession		IMAPISession object of the user
 * @param[in]	lpFolderIn		IMAPIFolder object for which all calendar are to returned(Optional, can be set to NULL if lpsbEid != NULL)
 * @param[in]	lpsbEid			EntryID of the Folder for which all calendar are to be returned(can be NULL if lpFolderIn != NULL)
 * @param[out]	lppTable		IMAPITable of the sub calendar of the folder
 * 
 * @return		HRESULT 
 */
HRESULT HrGetSubCalendars(IMAPISession *lpSession, IMAPIFolder *lpFolderIn, SBinary *lpsbEid, IMAPITable **lppTable)
{
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpFolder = NULL;
	ULONG ulObjType = 0;
	IMAPITable *lpTable = NULL;
	SPropValue sPropVal;
	SRestriction *lpRestrict = NULL;
	bool FreeFolder = false;

	if(!lpFolderIn)
	{
		hr = lpSession->OpenEntry(lpsbEid->cb, (LPENTRYID)lpsbEid->lpb, NULL, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN *)&lpFolder);
		if(hr != hrSuccess)
			goto exit;
		FreeFolder = true;
	}
	else
		lpFolder = lpFolderIn;

	hr = lpFolder->GetHierarchyTable(CONVENIENT_DEPTH,&lpTable);
	if(hr != hrSuccess)
		goto exit;

	sPropVal.ulPropTag = PR_CONTAINER_CLASS_A;
	sPropVal.Value.lpszA = const_cast<char *>("IPF.Appointment");

	CREATE_RESTRICTION(lpRestrict);
	
	CREATE_RES_OR(lpRestrict, lpRestrict, 2);
	DATA_RES_CONTENT(lpRestrict, lpRestrict->res.resOr.lpRes[0], FL_IGNORECASE, sPropVal.ulPropTag, &sPropVal);

	sPropVal.Value.lpszA = const_cast<char *>("IPF.Task");
	DATA_RES_CONTENT(lpRestrict, lpRestrict->res.resOr.lpRes[1], FL_IGNORECASE, sPropVal.ulPropTag, &sPropVal);

	hr = lpTable->Restrict(lpRestrict,TBL_BATCH);
	if(hr != hrSuccess)
		goto exit;

	*lppTable = lpTable;

exit:
	if(lpRestrict)
		FREE_RESTRICTION(lpRestrict);
	
	if(FreeFolder && lpFolder)
		lpFolder->Release();

	return hr;
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
	HRESULT hr = hrSuccess;
	LPMESSAGE lpFbMessage = NULL;
	LPSPropValue lpProp = NULL;
	LPSPropValue lpMailBoxEid = NULL;
	IMAPIContainer *lpRootCont = NULL;
	ULONG ulType = 0;
	ULONG ulPos = 0;
	SBinary sbEid = {0,0};
	bool blFound = false;
	bool blDelegatePerm = false; // no permission

	hr = HrGetOneProp(lpDefStore, PR_MAILBOX_OWNER_ENTRYID, &lpMailBoxEid);
	if (hr != hrSuccess)
		goto exit;

	hr = lpSharedStore->OpenEntry(0, NULL, NULL, 0, &ulType, (LPUNKNOWN*)&lpRootCont);
	if (hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(lpRootCont, PR_FREEBUSY_ENTRYIDS, &lpProp);
	if (hr != hrSuccess)
		goto exit;

	if (lpProp->Value.MVbin.cValues > 1 && lpProp->Value.MVbin.lpbin[1].cb != 0)
		sbEid = lpProp->Value.MVbin.lpbin[1];
	else
		goto exit;

	hr = lpSharedStore->OpenEntry(sbEid.cb, (LPENTRYID)sbEid.lpb, NULL, MAPI_BEST_ACCESS, &ulType, (LPUNKNOWN*)&lpFbMessage);
	if (hr != hrSuccess)
		goto exit;

	MAPIFreeBuffer(lpProp);
	lpProp = NULL;
	hr = HrGetOneProp(lpFbMessage, PR_SCHDINFO_DELEGATE_ENTRYIDS, &lpProp);
	if (hr != hrSuccess)
		goto exit;

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
		goto exit;
	
	MAPIFreeBuffer(lpProp);
	lpProp = NULL;
	hr = HrGetOneProp(lpFbMessage, PR_DELEGATE_FLAGS, &lpProp);
	if (hr != hrSuccess)
		goto exit;

	if(lpProp->Value.MVl.cValues >= ulPos && lpProp->Value.MVl.lpl[ulPos])
		blDelegatePerm = true;
	else
		blDelegatePerm = false;

exit:
	MAPIFreeBuffer(lpMailBoxEid);
	MAPIFreeBuffer(lpProp);

	if (lpFbMessage)
		lpFbMessage->Release();

	if (lpRootCont)
		lpRootCont->Release();

	return blDelegatePerm;
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
	HRESULT hr = hrSuccess;
	LPSPropValue lpPropPrivate = NULL;
	bool bIsPrivate = false;

	hr = HrGetOneProp(lpMessage, ulPropIDPrivate, &lpPropPrivate);
	if (hr != hrSuccess)
		goto exit;
	
	if(lpPropPrivate->Value.b == TRUE)
		bIsPrivate = true;
	
exit:
	MAPIFreeBuffer(lpPropPrivate);
	return bIsPrivate;
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
	HRESULT hr = hrSuccess;
	LPSRestriction lpsRoot = NULL;
	std::string strBinGuid;
	std::string strBinOtherUID;
	SPropValue sSpropVal = {0};

	if (lpsRectrict == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	CREATE_RESTRICTION(lpsRoot);
	CREATE_RES_OR(lpsRoot, lpsRoot, 4);

	// convert guid to outlook format
	if (IsOutlookUid(strGuid))
		strBinGuid = hex2bin(strGuid);
	else
		HrMakeBinUidFromICalUid(strGuid, &strBinGuid);
	
	sSpropVal.Value.bin.cb = (ULONG)strBinGuid.size();
	sSpropVal.Value.bin.lpb = (LPBYTE)strBinGuid.c_str();
	sSpropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);		
	DATA_RES_PROPERTY(lpsRoot, lpsRoot->res.resOr.lpRes[0], RELOP_EQ, sSpropVal.ulPropTag, &sSpropVal);
	
	// converting guid to hex
	strBinOtherUID = hex2bin(strGuid);
	sSpropVal.ulPropTag = PR_ENTRYID;
	sSpropVal.Value.bin.cb = (ULONG)strBinOtherUID.size();
	sSpropVal.Value.bin.lpb = (LPBYTE)strBinOtherUID.c_str();
	
	// When CreateAndGetGuid() fails PR_ENTRYID is used as guid.
	DATA_RES_PROPERTY(lpsRoot, lpsRoot->res.resOr.lpRes[1], RELOP_EQ, PR_ENTRYID, &sSpropVal);

	// z-push iphone UIDs are not in Outlook format		
	sSpropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
	DATA_RES_PROPERTY(lpsRoot, lpsRoot->res.resOr.lpRes[2], RELOP_EQ, sSpropVal.ulPropTag, &sSpropVal);

	// PUT url [guid].ics part, (eg. Evolution UIDs)
	sSpropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTTSREF], PT_STRING8);
	sSpropVal.Value.lpszA = (char*)strGuid.c_str();
	DATA_RES_PROPERTY(lpsRoot, lpsRoot->res.resOr.lpRes[3], RELOP_EQ, sSpropVal.ulPropTag, &sSpropVal);
	
exit:
	if (lpsRoot && lpsRectrict)
		*lpsRectrict = lpsRoot;

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
HRESULT HrFindAndGetMessage(std::string strGuid, IMAPIFolder *lpUsrFld, LPSPropTagArray lpNamedProps, IMessage **lppMessage)
{
	HRESULT hr = hrSuccess;
	SBinary sbEid = {0,0};
	SRestriction *lpsRoot = NULL;
	SRowSet *lpValRows = NULL;
	IMAPITable *lpTable = NULL;
	IMessage *lpMessage = NULL;	
	ULONG ulObjType = 0;
	SizedSPropTagArray(1, sPropTagArr) = {1, {PR_ENTRYID}};
	
	hr = HrMakeRestriction(strGuid, lpNamedProps, &lpsRoot);
	if (hr != hrSuccess)
		goto exit;
	
	hr = lpUsrFld->GetContentsTable(0, &lpTable);
	if(hr != hrSuccess)
		goto exit;

	hr = lpTable->SetColumns((LPSPropTagArray)&sPropTagArr, 0);
	if(hr != hrSuccess)
		goto exit;

	hr = lpTable->Restrict(lpsRoot, TBL_BATCH);
	if(hr != hrSuccess)
		goto exit;
	
	hr = lpTable->QueryRows(1, 0, &lpValRows);
	if (hr != hrSuccess)
		goto exit;

	if (lpValRows->cRows != 1)
	{
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	
	if (PROP_TYPE(lpValRows->aRow[0].lpProps[0].ulPropTag) != PT_BINARY)
	{
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	sbEid = lpValRows->aRow[0].lpProps[0].Value.bin;

	hr = lpUsrFld->OpenEntry(sbEid.cb, (LPENTRYID)sbEid.lpb, NULL, MAPI_MODIFY, &ulObjType, (LPUNKNOWN*)&lpMessage);
	if (hr != hrSuccess)
		goto exit;
	
	*lppMessage = lpMessage;
	lpMessage = NULL;

exit:
	if(lpMessage)
		lpMessage->Release();

	if(lpTable)
		lpTable->Release();
	
	if(lpsRoot)
		FREE_RESTRICTION(lpsRoot);

	if(lpValRows)
		FreeProws(lpValRows);
	
	return hr;
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
	HRESULT hr = hrSuccess;
	FBUser *lpUsers = NULL;
	IEnumFBBlock *lpEnumBlock = NULL;
	IFreeBusyData **lppFBData = NULL;
	FBBlock_1 *lpsFBblks = NULL;
	std::string strMethod;
	std::string strIcal;
	ULONG cUsers = 0;
	ULONG cFBData = 0;
	FILETIME ftStart = {0,0};
	FILETIME ftEnd = {0,0};
	LONG lMaxblks = 100;
	LONG lblkFetched = 0;
	WEBDAVFBUSERINFO sWebFbUserInfo;
	std::list<std::string>::const_iterator itUsers;
	LPADRLIST lpAdrList = NULL;
	FlagListPtr ptrFlagList;
	LPSPropValue lpEntryID = NULL;

	EntryIdPtr ptrEntryId;
	ULONG cbEntryId		= 0;
	ULONG ulObj			= 0;
	ABContainerPtr ptrABDir;

	hr = lpAddrBook->GetDefaultDir(&cbEntryId, &ptrEntryId);
	if (hr != hrSuccess)
		goto exit;

	hr = lpAddrBook->OpenEntry(cbEntryId, ptrEntryId, NULL, 0, &ulObj, &ptrABDir);
	if (hr != hrSuccess)
		goto exit;


	cUsers = lplstUsers->size();

	hr = MAPIAllocateBuffer(CbNewSRowSet(cUsers), (void **)&lpAdrList);
	if(hr != hrSuccess)
		goto exit;

	lpAdrList->cEntries = cUsers;

	hr = MAPIAllocateBuffer(CbNewFlagList(cUsers), (void **) &ptrFlagList);
	if (hr != hrSuccess)
		goto exit;

	ptrFlagList->cFlags = cUsers;

	
	for (itUsers = lplstUsers->begin(), cUsers = 0; itUsers != lplstUsers->end(); ++itUsers, ++cUsers) {
		lpAdrList->aEntries[cUsers].cValues = 1;

		hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpAdrList->aEntries[cUsers].rgPropVals);
		if(hr != hrSuccess)
			goto exit;

		lpAdrList->aEntries[cUsers].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_A;
		lpAdrList->aEntries[cUsers].rgPropVals[0].Value.lpszA = (char*)itUsers->c_str();

		ptrFlagList->ulFlag[cUsers] = MAPI_UNRESOLVED;
	}

	// NULL or sptaAddrListProps containing just PR_ENTRYID?
	hr = ptrABDir->ResolveNames(NULL, EMS_AB_ADDRESS_LOOKUP, lpAdrList, ptrFlagList);
	if (hr != hrSuccess)
		goto exit;
		

	hr = MAPIAllocateBuffer(sizeof(FBUser)*cUsers, (void**)&lpUsers);
	if (hr != hrSuccess)
		goto exit;

	// Get the user entryids
	for (cUsers = 0; cUsers < lpAdrList->cEntries; ++cUsers) {

		if (ptrFlagList->ulFlag[cUsers] == MAPI_RESOLVED) {
			lpEntryID = PpropFindProp(lpAdrList->aEntries[cUsers].rgPropVals, lpAdrList->aEntries[cUsers].cValues, PR_ENTRYID);
		} else {
			lpEntryID = NULL;
		}
		if (lpEntryID) {
			lpUsers[cUsers].m_cbEid = lpEntryID->Value.bin.cb;
			hr = MAPIAllocateMore(lpEntryID->Value.bin.cb, lpUsers, (void**)&lpUsers[cUsers].m_lpEid);
			if (hr != hrSuccess)
				goto exit;
			memcpy(lpUsers[cUsers].m_lpEid, lpEntryID->Value.bin.lpb, lpEntryID->Value.bin.cb);
		} else {
			lpUsers[cUsers].m_cbEid = 0;
			lpUsers[cUsers].m_lpEid = NULL;
		}
	}

	hr = MAPIAllocateBuffer(sizeof(IFreeBusyData*)*cUsers, (void**)&lppFBData);
	if (hr != hrSuccess)
		goto exit;
	
	// retrieve freebusy for the attendees
	hr = lpFBSupport->LoadFreeBusyData(cUsers, lpUsers, lppFBData, NULL, &cFBData);
	if (hr != hrSuccess)
		goto exit;

	UnixTimeToFileTime(lpFbInfo->tStart, &ftStart);
	UnixTimeToFileTime(lpFbInfo->tEnd, &ftEnd);

	itUsers = lplstUsers->begin();
	// iterate through all users
	for (ULONG i = 0; i < cUsers; ++i) {
		strIcal.clear();

		if (!lppFBData[i])
			goto next;
		
		hr = lppFBData[i]->EnumBlocks(&lpEnumBlock, ftStart, ftEnd);
		if (hr != hrSuccess)
			goto next;
		
		hr = MAPIAllocateBuffer(sizeof(FBBlock_1)*lMaxblks, (void**)&lpsFBblks);
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
		hr = lpMapiToIcal->Finalize(M2IC_NO_VTIMEZONE, &strMethod, &strIcal);
		
next:
		sWebFbUserInfo.strUser = *itUsers;
		sWebFbUserInfo.strIcal = strIcal;
		lpFbInfo->lstFbUserInfo.push_back(sWebFbUserInfo);

		++itUsers;

		lpMapiToIcal->ResetObject();
		
		if(lpEnumBlock)
			lpEnumBlock->Release();
		lpEnumBlock = NULL;

		MAPIFreeBuffer(lpsFBblks);
		lpsFBblks = NULL;

		lblkFetched = 0;
	}
	// ignoring ical data for unknown users.
	hr = hrSuccess;

exit:
	MAPIFreeBuffer(lpUsers);
	if (lpAdrList)
		FreePadrlist(lpAdrList);
	
	if (lppFBData) {
		for (ULONG i = 0; i < cUsers; ++i)
			if (lppFBData[i])
				lppFBData[i]->Release();
		MAPIFreeBuffer(lppFBData);
	}

	return hr;
}
