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
#include <kopano/ECRestriction.h>
#include "favoritesutil.h"

#include <edkmdb.h>

#include <kopano/mapiext.h>
#include <kopano/restrictionutil.h>
#include <kopano/CommonUtil.h>

#include <kopano/tstring.h>
#include <kopano/charset/convstring.h>

#include <string>
using namespace std;

static SizedSPropTagArray(SHORTCUT_NUM, sPropsShortcuts) = {SHORTCUT_NUM, {
	PR_INSTANCE_KEY, PR_FAV_PUBLIC_SOURCE_KEY, PR_FAV_PARENT_SOURCE_KEY,
	PR_FAV_DISPLAY_NAME, PR_FAV_DISPLAY_ALIAS, PR_FAV_LEVEL_MASK,
	PR_FAV_CONTAINER_CLASS}};

LPSPropTagArray GetShortCutTagArray() {
	return sPropsShortcuts;
}

/** 
 * Get the shortcut folder from the users default store. 
 * If the folder not exist it will create them
 *
 * @param lpSession Pointer to the current mapi session
 * @param lpszFolderName Pointer to a string containing the name for the shorcut folder. If NULL is passed, the folder has the default name 'Shortcut'.
 * @param lpszFolderComment Pointer to a string containing a comment associated with the shorcut folder. If NULL is passed, the folder has the default comment 'Shortcut folder'.
 * @param ulFlags MAPI_UNICODE unicode properties
 *                MAPI_CREATE create shorcut folder if not exist
 * @param lppShortcutFolder Pointer to a pointer to the opened shortcut folder
 */

HRESULT GetShortcutFolder(LPMAPISESSION lpSession, LPTSTR lpszFolderName, LPTSTR lpszFolderComment, ULONG ulFlags, LPMAPIFOLDER* lppShortcutFolder)
{
	HRESULT hr = hrSuccess;
	LPSPropValue lpPropValue = NULL;
	IMsgStore *lpMsgStore = NULL;
	IMAPIFolder *lpFolder = NULL;
	ULONG ulObjType = 0;

	hr = HrOpenDefaultStore(lpSession, &lpMsgStore);
	if(hr != hrSuccess)
		goto exit;

	// Get shortcut entryid
	hr = HrGetOneProp(lpMsgStore, PR_IPM_FAVORITES_ENTRYID, &lpPropValue);
	if(hr != hrSuccess) {
		if (hr == MAPI_E_NOT_FOUND && ulFlags & MAPI_CREATE)
			// Propety not found, re-create the shortcut folder
			hr = CreateShortcutFolder(lpMsgStore, lpszFolderName, lpszFolderComment, ulFlags & MAPI_UNICODE, lppShortcutFolder);
		goto exit;
	}

	// Open Shortcut folder
	hr = lpMsgStore->OpenEntry(lpPropValue->Value.bin.cb, (LPENTRYID)lpPropValue->Value.bin.lpb, &IID_IMAPIFolder, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN *)&lpFolder);
	if (hr != hrSuccess) {
		if (hr == MAPI_E_NOT_FOUND && ulFlags & MAPI_CREATE)
			// Folder not found, re-create the shortcut folder
			hr = CreateShortcutFolder(lpMsgStore, lpszFolderName, lpszFolderComment, ulFlags & MAPI_UNICODE, lppShortcutFolder);
		goto exit;
	}

	hr = lpFolder->QueryInterface(IID_IMAPIFolder, (void**)lppShortcutFolder);
	if (hr != hrSuccess)
		goto exit;

exit:
	MAPIFreeBuffer(lpPropValue);
	if(lpFolder)
		lpFolder->Release();

	if(lpMsgStore)
		lpMsgStore->Release();

	return hr;
}

/**
 * Helper function to create a new shorcut folder and update the property 
 * PR_IPM_FAVORITES_ENTRYID on the store object.
 * If the folder already exist, it will used and update only the property
 *
 * @param lpMsgStore Pointer to the private store
 * @param lpszFolderName Pointer to a string containing the name for the shorcut folder. If NULL is passed, the folder has the default name 'Shortcut'.
 * @param lpszFolderComment Pointer to a string containing a comment associated with the shorcut folder. If NULL is passed, the folder has the default comment 'Shortcut folder'.
 * @param ulFlags MAPI_UNICODE if the folder strings are in unicode format, otherwise 0.
 * @param lppShortcutFolder Pointer to a pointer to the opened shortcut folder
 */
HRESULT CreateShortcutFolder(IMsgStore *lpMsgStore, LPTSTR lpszFolderName, LPTSTR lpszFolderComment, ULONG ulFlags, LPMAPIFOLDER* lppShortcutFolder)
{
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpFolder = NULL;
	IMAPIFolder *lpNewFolder = NULL;
	ULONG ulObjType = 0;
	LPSPropValue lpProp = NULL;

	if (lpMsgStore == NULL || lppShortcutFolder == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (lpszFolderName == NULL) {
		if (ulFlags & MAPI_UNICODE)
			lpszFolderName = (LPTSTR)L"Shortcut";
		else
			lpszFolderName = (LPTSTR)"Shortcut";
	}

	if (lpszFolderComment == NULL) {
		if (ulFlags & MAPI_UNICODE)
			lpszFolderComment = (LPTSTR)L"Shortcut folder";
		else
			lpszFolderComment = (LPTSTR)"Shortcut folder";
	}

	// Open root folder
	hr = lpMsgStore->OpenEntry(0, NULL, &IID_IMAPIFolder, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN *)&lpFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = lpFolder->CreateFolder(FOLDER_GENERIC, lpszFolderName, lpszFolderComment, &IID_IMAPIFolder, ulFlags | OPEN_IF_EXISTS, &lpNewFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(lpNewFolder, PR_ENTRYID, &lpProp);
	if (hr != hrSuccess)
		goto exit;

	lpProp->ulPropTag = PR_IPM_FAVORITES_ENTRYID;

	hr = HrSetOneProp(lpMsgStore, lpProp);
	if (hr != hrSuccess)
		goto exit;

	hr = lpNewFolder->QueryInterface(IID_IMAPIFolder, (void**)lppShortcutFolder);
	if (hr != hrSuccess)
		goto exit;

exit:
	MAPIFreeBuffer(lpProp);
	if (lpFolder)
		lpFolder->Release();

	if (lpNewFolder)
		lpNewFolder->Release();

	return hr;
}

/**
 * Remove a favorite in the private store. also the sub favorites will be deleted
 *
 * @param lpShortcutFolder The shortcut folder in the private store.
 * @param lpPropSourceKey Pointer to the sourcekey of a favorite folder
 */
HRESULT DelFavoriteFolder(IMAPIFolder *lpShortcutFolder, LPSPropValue lpPropSourceKey)
{
	HRESULT hr = hrSuccess;
	LPMAPITABLE lpTable = NULL;
	LPSRestriction lpRestriction = NULL;
	SRowSet *lpRows = NULL;
	LPENTRYLIST lpsMsgList = NULL;
	SizedSPropTagArray(2, sPropDelFavo) = {2, { PR_ENTRYID, PR_FAV_PUBLIC_SOURCE_KEY }};
	std::list<string>	listSourceKey;
	string strSourceKey;
	SPropValue sPropSourceKey;
	ULONG ulMaxRows = 0;

	if (lpShortcutFolder == NULL || lpPropSourceKey == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpShortcutFolder->GetContentsTable(0, &lpTable);
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->GetRowCount(0, &ulMaxRows);
	if (hr != hrSuccess)
		goto exit;
	hr = lpTable->SetColumns(sPropDelFavo, 0);
	if (hr != hrSuccess)
		goto exit;

	// build restriction
	hr = ECPropertyRestriction(RELOP_EQ, PR_FAV_PUBLIC_SOURCE_KEY, lpPropSourceKey).CreateMAPIRestriction(&lpRestriction);
	if (hr != hrSuccess)
		goto exit;
	if (lpTable->FindRow(lpRestriction, BOOKMARK_BEGINNING , 0) != hrSuccess)
		goto exit; // Folder already removed

	hr = lpTable->QueryRows (1, 0, &lpRows);
	if (hr != hrSuccess)
		goto exit;

	if (lpRows->cRows == 0)
		goto exit; // Folder already removed

	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), (void**)&lpsMsgList);
	if (hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateMore(sizeof(SBinary)*ulMaxRows, lpsMsgList, (void**)&lpsMsgList->lpbin);
	if (hr != hrSuccess)
		goto exit;

//FIXME: check the properties in the row!!!!

	lpsMsgList->cValues = 0;

	// add entryid
	lpsMsgList->lpbin[lpsMsgList->cValues].cb = lpRows->aRow[0].lpProps[0].Value.bin.cb;

	if ((hr = MAPIAllocateMore(lpsMsgList->lpbin[lpsMsgList->cValues].cb, lpsMsgList, (void **) &lpsMsgList->lpbin[lpsMsgList->cValues].lpb)) != hrSuccess)
		goto exit;

	memcpy(lpsMsgList->lpbin[lpsMsgList->cValues].lpb, lpRows->aRow[0].lpProps[0].Value.bin.lpb, lpsMsgList->lpbin[lpsMsgList->cValues].cb);
	++lpsMsgList->cValues;

	strSourceKey.assign((char*)lpRows->aRow[0].lpProps[1].Value.bin.lpb, lpRows->aRow[0].lpProps[1].Value.bin.cb);
	listSourceKey.push_back(strSourceKey);
	FreeProws(lpRows);
	lpRows = NULL;
	FREE_RESTRICTION(lpRestriction);

	for (const auto &sk : listSourceKey) {
		sPropSourceKey.ulPropTag = PR_FAV_PUBLIC_SOURCE_KEY;
		sPropSourceKey.Value.bin.cb = sk.size();
		sPropSourceKey.Value.bin.lpb = const_cast<BYTE *>(reinterpret_cast<const BYTE *>(sk.c_str()));

		hr = ECPropertyRestriction(RELOP_EQ, PR_FAV_PARENT_SOURCE_KEY, &sPropSourceKey).CreateMAPIRestriction(&lpRestriction);
		if (hr != hrSuccess)
			goto exit;
		hr = lpTable->Restrict(lpRestriction, TBL_BATCH );
		if (hr != hrSuccess)
			goto exit;

		hr = lpTable->SeekRow(BOOKMARK_BEGINNING, 0, NULL);
		if (hr != hrSuccess)
			goto exit;

		while(true)
		{
			hr = lpTable->QueryRows (1, 0, &lpRows);
			if (hr != hrSuccess)
				goto exit;

			if (lpRows->cRows == 0)
				break; // no rows

//FIXME: check the properties in the row!!!!

			// add entryid
			lpsMsgList->lpbin[lpsMsgList->cValues].cb = lpRows->aRow[0].lpProps[0].Value.bin.cb;

			if ((hr = MAPIAllocateMore(lpsMsgList->lpbin[lpsMsgList->cValues].cb, lpsMsgList, (void **) &lpsMsgList->lpbin[lpsMsgList->cValues].lpb)) != hrSuccess)
				goto exit;
			memcpy(lpsMsgList->lpbin[lpsMsgList->cValues].lpb, lpRows->aRow[0].lpProps[0].Value.bin.lpb, lpsMsgList->lpbin[lpsMsgList->cValues].cb);
			++lpsMsgList->cValues;

			// Add sourcekey into the list
			strSourceKey.assign((char*)lpRows->aRow[0].lpProps[1].Value.bin.lpb, lpRows->aRow[0].lpProps[1].Value.bin.cb);
			listSourceKey.push_back(strSourceKey);
		} //while(true)

		FREE_RESTRICTION(lpRestriction);
		FreeProws(lpRows);
		lpRows = NULL;
	}

	hr = lpShortcutFolder->DeleteMessages(lpsMsgList,  0, NULL, 0);
	if (hr != hrSuccess)
		goto exit;

exit:
	FREE_RESTRICTION(lpRestriction);

	if (lpTable)
		lpTable->Release();

	if (lpRows)
		FreeProws(lpRows);
	MAPIFreeBuffer(lpsMsgList);
	return hr;
}

/**
 * Add a new favorite in the private store.
 *
 * @param[in] lpShortcutFolder The shortcut folder in the private store.
 * @param[in] ulLevel The depth of the folder, start from one.
 * @param[in] lpAliasName Alias name of the folder. Pass NULL to specify the standard foldername
 * @param[in] ulFlags possible MAPI_UNICODE for lpszAliasName
 * @param[in] cValues Count of property values pointed to by the lpPropArray parameter. The cValues parameter must not be zero.
 * @param[in] lpPropArray Pointer to an array of SPropValue structures holding property values to create the favorite. 
 */
HRESULT AddToFavorite(IMAPIFolder *lpShortcutFolder, ULONG ulLevel, LPCTSTR lpszAliasName, ULONG ulFlags, ULONG cValues, LPSPropValue lpPropArray)
{
	HRESULT hr = hrSuccess;
	IMessage *lpMessage = NULL;
	LPSPropValue lpPropSourceKey = NULL;
	LPSPropValue lpPropParentSourceKey = NULL;
	LPSPropValue lpPropDisplayName = NULL;
	LPSPropValue lpPropMessageClass = NULL;

	LPMAPITABLE lpTable = NULL;
	LPSPropValue lpNewPropArray = NULL;
	ULONG cPropArray = 0;

	LPSRestriction lpRestriction = NULL;

	if (lpShortcutFolder == NULL || lpPropArray == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	
	lpPropSourceKey = PpropFindProp(lpPropArray, cValues, PR_SOURCE_KEY);
	lpPropParentSourceKey = PpropFindProp(lpPropArray, cValues, PR_PARENT_SOURCE_KEY);
	lpPropDisplayName = PpropFindProp(lpPropArray, cValues, PR_DISPLAY_NAME);
	lpPropMessageClass = PpropFindProp(lpPropArray, cValues, PR_CONTAINER_CLASS);
	
	if (lpPropSourceKey == NULL || lpPropParentSourceKey == NULL || lpPropDisplayName == NULL)
	{
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	// Check for duplicates
	hr = lpShortcutFolder->GetContentsTable(0, &lpTable);
	if (hr != hrSuccess)
		goto exit;

	// build restriction
	hr = ECPropertyRestriction(RELOP_EQ, PR_FAV_PUBLIC_SOURCE_KEY, lpPropSourceKey).CreateMAPIRestriction(&lpRestriction);
	if (hr != hrSuccess)
		goto exit;
	if (lpTable->FindRow(lpRestriction, BOOKMARK_BEGINNING , 0) == hrSuccess)
		goto exit; // Folder already include

	// No duplicate, Start to add the favorite
	hr = lpShortcutFolder->CreateMessage(NULL, 0, &lpMessage);
	if (hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(sizeof(SPropValue) * 6, (void**)&lpNewPropArray);
	if (hr != hrSuccess)
		goto exit;

	lpNewPropArray[cPropArray].ulPropTag = PR_FAV_LEVEL_MASK;
	lpNewPropArray[cPropArray++].Value.ul = ulLevel;

	lpNewPropArray[cPropArray].ulPropTag = PR_FAV_PUBLIC_SOURCE_KEY;
	lpNewPropArray[cPropArray++].Value = lpPropSourceKey->Value;

	lpNewPropArray[cPropArray].ulPropTag = PR_FAV_DISPLAY_NAME;
	lpNewPropArray[cPropArray++].Value = lpPropDisplayName->Value;

	if (lpPropMessageClass) {
		lpNewPropArray[cPropArray].ulPropTag = PR_FAV_CONTAINER_CLASS;
		lpNewPropArray[cPropArray++].Value = lpPropMessageClass->Value;
	}	

	if (ulLevel > 1) {
		lpNewPropArray[cPropArray].ulPropTag = PR_FAV_PARENT_SOURCE_KEY;
		lpNewPropArray[cPropArray++].Value = lpPropParentSourceKey->Value;
	}

	if (lpszAliasName && lpszAliasName[0] != '\0') {
		tstring tDisplay(lpPropDisplayName->Value.LPSZ);
		convstring csAlias(lpszAliasName, ulFlags);
		if ((std::wstring)csAlias != tDisplay)
		{
			lpNewPropArray[cPropArray].ulPropTag = (ulFlags & MAPI_UNICODE) ? PR_FAV_DISPLAY_ALIAS_W : PR_FAV_DISPLAY_ALIAS_A;
			lpNewPropArray[cPropArray++].Value.lpszA = (LPSTR)lpszAliasName;
		}
	}

	hr = lpMessage->SetProps(cPropArray, lpNewPropArray, NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMessage->SaveChanges(0);
	if (hr != hrSuccess)
		goto exit;

exit:
	FREE_RESTRICTION(lpRestriction);
	MAPIFreeBuffer(lpNewPropArray);
	if (lpMessage)
		lpMessage->Release();

	if (lpTable)
		lpTable->Release();

	return hr;
}

/**
 * Add new folders to the favorites folder
 *
 * @param lpSession Pointer to the current mapi session
 * @param lpFolder Pointer to a folder in the public store, except a folder from the favorites folder
 * @param lpAliasName Pointer to a string containing another name for the folder
 * @param ulFlags Bitmask of flags that controls how the folder is added. The following flags can be set:
 * FAVO_FOLDER_LEVEL_BASE
 *		Add only the folder itself
 * FAVO_FOLDER_LEVEL_ONE
 *		Add the folder and the immediate subfolders only
 * FAVO_FOLDER_LEVEL_SUB
 *		Add the folder and all subfolders
 * MAPI_UNICODE
 *		lpAliasName parameter is in wide or multibyte format
 */
HRESULT AddFavoriteFolder(LPMAPIFOLDER lpShortcutFolder, LPMAPIFOLDER lpFolder, LPCTSTR lpAliasName, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	
	LPMAPITABLE lpTable = NULL;
	LPSPropValue lpsPropArray = NULL;
	LPSPropValue lpPropDepth = NULL; // No free needed

	SRowSet *lpRows = NULL;

	ULONG ulFolderFlags = 0;
	ULONG cValues = 0;

	SizedSPropTagArray(5, sPropsFolderInfo) = {5, { PR_DEPTH, PR_SOURCE_KEY, PR_PARENT_SOURCE_KEY, PR_DISPLAY_NAME, PR_CONTAINER_CLASS}};

// FIXME: check vaiables

	// Add folders to the shorcut folder
	hr = lpFolder->GetProps(sPropsFolderInfo, 0, &cValues, &lpsPropArray);
	if (FAILED(hr) != hrSuccess) //Gives always a warning
		goto exit;

	hr = AddToFavorite(lpShortcutFolder, 1, lpAliasName, ulFlags, cValues, lpsPropArray);
	if (hr != hrSuccess)
		goto exit;

	MAPIFreeBuffer(lpsPropArray);
	lpsPropArray = NULL;

	if (ulFlags == FAVO_FOLDER_LEVEL_SUB) {
		ulFolderFlags = CONVENIENT_DEPTH;
	} else if(ulFlags == FAVO_FOLDER_LEVEL_ONE) {
		ulFolderFlags = 0;
	}else {
		hr = hrSuccess; // Done
		goto exit;
	}

	// Get subfolders
	hr = lpFolder->GetHierarchyTable(ulFolderFlags, &lpTable);
	if (hr != hrSuccess)
		goto exit;
	hr = lpTable->SetColumns(sPropsFolderInfo, 0);
	if (hr != hrSuccess)
		goto exit;

	// Add the favorite recusive depended what the flags are
	while(true)
	{
		hr = lpTable->QueryRows (1, 0, &lpRows);
		if (hr != hrSuccess)
			goto exit;

		if (lpRows->cRows == 0)
			break;

		lpPropDepth = PpropFindProp(lpRows->aRow[0].lpProps,lpRows->aRow[0].cValues, PR_DEPTH);
		if (lpPropDepth == NULL) {
			hr = MAPI_E_CORRUPT_DATA;// Break the action
			goto exit;
		}

		hr = AddToFavorite(lpShortcutFolder, lpPropDepth->Value.ul + 1, NULL, 0, lpRows->aRow[0].cValues, lpRows->aRow[0].lpProps);
		if (hr != hrSuccess)
			// Break the action
			goto exit;
		FreeProws(lpRows);
		lpRows = NULL;

	} //while(true)

exit:
	if (lpTable)
		lpTable->Release();

	if (lpRows)
		FreeProws(lpRows);
	MAPIFreeBuffer(lpsPropArray);
	return hr;
}
