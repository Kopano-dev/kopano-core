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

#include <string>
#include <vector>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "ProtocolBase.h"
#include <kopano/stringutil.h>
#include <kopano/CommonUtil.h>
#include <kopano/MAPIErrors.h>
#include "CalDavUtil.h"
#include <kopano/mapi_ptr.h>

using namespace KC;

ProtocolBase::ProtocolBase(Http *lpRequest, IMAPISession *lpSession,
    const std::string &strSrvTz, const std::string &strCharset) :
	m_lpRequest(lpRequest), m_lpSession(lpSession), m_strSrvTz(strSrvTz),
	m_strCharset(strCharset)
{
}

/**
 * Opens the store and folders required for the Request. Also checks
 * if DELETE or RENAME actions are allowed on the folder.
 *
 * @return	MAPI error code
 * @retval	MAPI_E_NO_ACCESS	Not enough permissions to open the folder or store
 * @retval	MAPI_E_NOT_FOUND	Folder requested by client not found
 */
HRESULT ProtocolBase::HrInitializeClass()
{
	HRESULT hr = hrSuccess;
	std::string strUrl;
	std::string strMethod, strFldOwner, strFldName;
	memory_ptr<SPropValue> lpDefaultProp, lpFldProp;
	SPropValuePtr lpEntryID;
	ULONG ulRes = 0;
	bool bIsPublic = false;
	ULONG ulType = 0;
	MAPIFolderPtr lpRoot;

	/* URLs
	 * 
	 * /caldav/							| depth: 0 results in root props, depth: 1 results in 0 + calendar FOLDER list current user. eg: the same happens /caldav/self/
	 * /caldav/self/					| depth: 0 results in root props, depth: 1 results in 0 + calendar FOLDER list current user. see ^^
	 * /caldav/self/Calendar/			| depth: 0 results in root props, depth: 1 results in 0 + _GIVEN_ calendar CONTENTS list current user.
	 * /caldav/self/_NAMED_FOLDER_ID_/ (evo has date? still??) (we also do hexed entryids?)
	 * /caldav/other/[...]
	 * /caldav/public/[...]
	 *
	 * /caldav/user/folder/folder/		| subfolders are not allowed!
	 * /caldav/user/folder/id.ics		| a message (note: not ending in /)
	 */

	m_lpRequest->HrGetUrl(&strUrl);
	m_lpRequest->HrGetUser(&m_wstrUser);
	m_lpRequest->HrGetMethod(&strMethod);	

	HrParseURL(strUrl, &m_ulUrlFlag, &strFldOwner, &strFldName);
	m_wstrFldOwner = U2W(strFldOwner);
	m_wstrFldName = U2W(strFldName);
	bIsPublic = m_ulUrlFlag & REQ_PUBLIC;
	if (m_wstrFldOwner.empty())
		m_wstrFldOwner = m_wstrUser;

	hr = m_lpSession->OpenAddressBook(0, NULL, 0, &~m_lpAddrBook);
	if(hr != hrSuccess)
		return kc_perror("Error opening addressbook", hr);
	// default store required for various actions (delete, freebusy, ...)
	hr = HrOpenDefaultStore(m_lpSession, &~m_lpDefStore);
	if(hr != hrSuccess)
	{
		ec_log_err("Error opening default store of user \"%ls\": %s (%x)",
			m_wstrUser.c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	hr = HrGetOwner(m_lpSession, m_lpDefStore, &~m_lpLoginUser);
	if(hr != hrSuccess)
		return hr;

	/*
	 * Set m_lpActiveStore
	 */
	if (bIsPublic)
	{
		// open public
		hr = HrOpenECPublicStore(m_lpSession, &~m_lpActiveStore);
		if (hr != hrSuccess) {
			ec_log_err("Unable to open public store with user \"%ls\": %s (%x)",
				m_wstrUser.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}
	} else if (wcscasecmp(m_wstrUser.c_str(), m_wstrFldOwner.c_str())) {
		// open shared store
		hr = HrOpenUserMsgStore(m_lpSession, (WCHAR*)m_wstrFldOwner.c_str(), &~m_lpActiveStore);
		if (hr != hrSuccess) {
			ec_log_err("Unable to open store of user \"%ls\" with user \"%ls\": %s (%x)",
				m_wstrFldOwner.c_str(), m_wstrUser.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}
		m_ulFolderFlag |= SHARED_FOLDER;
	} else {
		// @todo, make auto pointers
		hr = m_lpDefStore->QueryInterface(IID_IMsgStore, &~m_lpActiveStore);
		if (hr != hrSuccess)
			return hr;
	}

	// Retrieve named properties
	hr = HrLookupNames(m_lpActiveStore, &~m_lpNamedProps);
	if (hr != hrSuccess)
		return hr;

	// get active user info
	if (bIsPublic)
		hr = m_lpLoginUser->QueryInterface(IID_IMailUser, &~m_lpActiveUser);
	else
		hr = HrGetOwner(m_lpSession, m_lpActiveStore, &~m_lpActiveUser);
	if(hr != hrSuccess)
		return hr;

	/*
	 * Set m_lpIPMSubtree, used for CopyFolder, CreateFolder, DeleteFolder
	 */
	hr = OpenSubFolder(m_lpActiveStore, NULL, '/', bIsPublic, false, &~m_lpIPMSubtree);
	if(hr != hrSuccess)
	{
		ec_log_err("Error opening IPM SUBTREE using user \"%ls\": %s (%x)",
			m_wstrUser.c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	// Get active store default calendar to prevent delete action on this folder
	hr = m_lpActiveStore->OpenEntry(0, nullptr, &iid_of(lpRoot), 0, &ulType, &~lpRoot);
	if(hr != hrSuccess)
	{
		ec_log_err("Error opening root container using user \"%ls\": %s (%x)",
			m_wstrUser.c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	if (!bIsPublic) {
		// get default calendar entryid for non-public stores
		hr = HrGetOneProp(lpRoot, PR_IPM_APPOINTMENT_ENTRYID, &~lpDefaultProp);
		if(hr != hrSuccess)
		{
			ec_log_err("Error retrieving entryid of default calendar for user \"%ls\": %s (%x)",
				m_wstrUser.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}
	}

	/*
	 * Set m_lpUsrFld
	 */
	if (strMethod.compare("MKCALENDAR") == 0 && (m_ulUrlFlag & SERVICE_CALDAV))
	{
		// created in the IPM_SUBTREE
		hr = OpenSubFolder(m_lpActiveStore, NULL, '/', bIsPublic,
		     false, &~m_lpUsrFld);
		if(hr != hrSuccess)
		{
			ec_log_err("Error opening IPM_SUBTREE folder of user \"%ls\": %s (%x)",
				m_wstrUser.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}
	}
	else if(!m_wstrFldName.empty())
	{
		// @note, caldav allows creation of calendars for non-existing urls, but since this can also use IDs, I am not sure we want to.
		hr = HrFindFolder(m_lpActiveStore, m_lpIPMSubtree, m_lpNamedProps, m_wstrFldName, &~m_lpUsrFld);
		if(hr != hrSuccess)
		{
			ec_log_err("Error opening named folder of user \"%ls\", folder \"%ls\": %s (%x)",
				m_wstrUser.c_str(), m_wstrFldName.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}
		m_ulFolderFlag |= SINGLE_FOLDER;

		// check if this is the default calendar folder to enable freebusy publishing
		if (lpDefaultProp &&
		    HrGetOneProp(m_lpUsrFld, PR_ENTRYID, &~lpEntryID) == hrSuccess &&
		    m_lpActiveStore->CompareEntryIDs(lpEntryID->Value.bin.cb, (LPENTRYID)lpEntryID->Value.bin.lpb,
		    lpDefaultProp->Value.bin.cb, (LPENTRYID)lpDefaultProp->Value.bin.lpb, 0, &ulRes) == hrSuccess &&
		    ulRes == TRUE)
		{
			// disable delete default folder, and enable fb publish
			m_blFolderAccess = false;
			m_ulFolderFlag |= DEFAULT_FOLDER;
		}
	}
	// default calendar
	else if (bIsPublic) {
		hr = m_lpIPMSubtree->QueryInterface(IID_IMAPIFolder, &~m_lpUsrFld);
		if (hr != hrSuccess)
			return hr;
	} else {
		// open default calendar
		hr = m_lpActiveStore->OpenEntry(lpDefaultProp->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpDefaultProp->Value.bin.lpb),
		     &iid_of(m_lpUsrFld), MAPI_BEST_ACCESS, &ulType, &~m_lpUsrFld);
		if (hr != hrSuccess)
		{
			ec_log_err("Unable to open default calendar for user \"%ls\": %s (%x)",
				m_wstrUser.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}
		// we already know we don't want to delete this folder
		m_blFolderAccess = false;
		m_ulFolderFlag |= DEFAULT_FOLDER;
	}

	/*
	 * Workaround for old users with sunbird / lightning on old url base.
	 */
	{
		std::vector<std::string> parts;
		parts = tokenize(strUrl, '/', true);

		m_lpRequest->HrGetHeaderValue("User-Agent", &strAgent);

		// /caldav/
		// /caldav/username/ (which we return in XML data! (and shouldn't)), since this isn't a calendar, but /caldav/username/Calendar/ is.
		if ((strAgent.find("Sunbird/1") != std::string::npos || strAgent.find("Lightning/1") != std::string::npos) && parts.size() <= 2) {
			// Mozilla Sunbird / Lightning doesn't handle listing of calendars, only contents.
			// We therefore redirect them to the default calendar url.
			SPropValuePtr ptrDisplayName;
			auto strLocation = "/caldav/" + urlEncode(m_wstrFldOwner, "utf-8");

			if (HrGetOneProp(m_lpUsrFld, PR_DISPLAY_NAME_W, &~ptrDisplayName) == hrSuccess) {
				std::string part = urlEncode(ptrDisplayName->Value.lpszW, "UTF-8"); 
				strLocation += "/" + part + "/";
			} else {
				// return 404 ?
				strLocation += "/Calendar/";
			}

			m_lpRequest->HrResponseHeader(301, "Moved Permanently");
			m_lpRequest->HrResponseHeader("Location", m_converter.convert_to<std::string>(strLocation));
			return MAPI_E_NOT_ME;
		}
	}

	/*
	 * Check delete / rename access on folder, if not already blocked.
	 */
	if (m_blFolderAccess &&
	    // lpDefaultProp already should contain PR_IPM_APPOINTMENT_ENTRYID
	    lpDefaultProp != nullptr) {
		ULONG ulCmp;

		hr = HrGetOneProp(m_lpUsrFld, PR_ENTRYID, &~lpFldProp);
		if (hr != hrSuccess)
			return hr;
		hr = m_lpSession->CompareEntryIDs(lpDefaultProp->Value.bin.cb, (LPENTRYID)lpDefaultProp->Value.bin.lpb,
		     lpFldProp->Value.bin.cb, (LPENTRYID)lpFldProp->Value.bin.lpb, 0, &ulCmp);
		if (hr != hrSuccess || ulCmp == TRUE)
			m_blFolderAccess = false;
	}
	if (m_blFolderAccess) {
		hr = HrGetOneProp(m_lpUsrFld, PR_SUBFOLDERS, &~lpFldProp);
		if(hr != hrSuccess)
			return hr;
		if(lpFldProp->Value.b == (unsigned short)true && !strMethod.compare("DELETE"))
			m_blFolderAccess = false;
	}
	return hr;
}

/**
 * converts widechar to utf-8 string
 * @param[in]	strWinChar	source string(windows-1252) to be converted
 * @return		string		converted string (utf-8)
 */
std::string ProtocolBase::W2U(const std::wstring &strWideChar)
{
	return m_converter.convert_to<std::string>(m_strCharset.c_str(), strWideChar, rawsize(strWideChar), CHARSET_WCHAR);
}

/**
 * converts widechar to utf-8 string
 * @param[in]	strWinChar	source string(windows-1252) to be converted
 * @return		string		converted string (utf-8)
 */
std::string ProtocolBase::W2U(const WCHAR* lpwWideChar)
{
	return m_converter.convert_to<std::string>(m_strCharset.c_str(), lpwWideChar, rawsize(lpwWideChar), CHARSET_WCHAR);
}

/**
 * converts utf-8 string to widechar
 * @param[in]	strUtfChar	source string(utf-8) to be converted
 * @return		wstring		converted wide string
 */
std::wstring ProtocolBase::U2W(const std::string &strUtfChar)
{
	return m_converter.convert_to<std::wstring>(strUtfChar, rawsize(strUtfChar), m_strCharset.c_str());	
}

/**
 * Convert SPropValue to string
 * 
 * @param[in]	lpSprop		SPropValue to be converted
 * @return		string
 */
std::string ProtocolBase::SPropValToString(const SPropValue *lpSprop)
{
	std::string strRetVal;
	
	if (lpSprop == NULL)
		return strRetVal;
	if (PROP_TYPE(lpSprop->ulPropTag) == PT_SYSTIME)
		strRetVal = stringify_int64(FileTimeToUnixTime(lpSprop->Value.ft), false);
	else if (PROP_TYPE(lpSprop->ulPropTag) == PT_STRING8)
		strRetVal = lpSprop->Value.lpszA;
	else if (PROP_TYPE(lpSprop->ulPropTag) == PT_UNICODE)
		strRetVal = W2U(lpSprop->Value.lpszW);
	//Global UID
	else if (PROP_TYPE(lpSprop->ulPropTag) == PT_BINARY)
		HrGetICalUidFromBinUid(lpSprop->Value.bin, &strRetVal);	
	else if (PROP_TYPE(lpSprop->ulPropTag) == PT_LONG)
		strRetVal = stringify(lpSprop->Value.ul, false);
	return strRetVal;
}
