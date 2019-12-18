/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include "iCal.h"
#include "CalDavUtil.h"
#include <map>
#include <memory>
#include <vector>
#include <kopano/CommonUtil.h>
#include <kopano/MAPIErrors.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include "icaluid.h"
#include <libxml/tree.h>
#include <libxml/parser.h>
#include "PublishFreeBusy.h"
#include <kopano/mapi_ptr.h>

using namespace KC;

iCal::iCal(Http &lpRequest, IMAPISession *lpSession,
    const std::string &strSrvTz, const std::string &strCharset) :
	ProtocolBase(lpRequest, lpSession, strSrvTz, strCharset)
{
}

HRESULT iCal::HrHandleCommand(const std::string &strMethod)
{
	if (!strMethod.compare("GET") || !strMethod.compare("HEAD"))
		return HrHandleIcalGet(strMethod);
	else if (!strMethod.compare("PUT"))
		return HrHandleIcalPost();
	else if (!strMethod.compare("DELETE"))
		return HrDelFolder();
	return MAPI_E_CALL_FAILED;
}

/**
 * Handles http GET and HEAD request
 *
 * The GET request could be to retrieve one paticular msg or all calendar entries
 * HEAD is just a GET without the body returned.
 *
 * @return	HRESULT
 * @retval	MAPI_E_NOT_FOUND	Folder or message not found
 */
HRESULT iCal::HrHandleIcalGet(const std::string &strMethod)
{
	std::string strIcal, strMsg, strModtime;
	memory_ptr<SPropValue> lpProp;
	object_ptr<IMAPITable> lpContents;
	bool blCensorFlag = m_ulFolderFlag & SHARED_FOLDER && !HasDelegatePerm(m_lpDefStore, m_lpActiveStore);

	// retrieve table and restrict as per request
	auto hr = HrGetContents(&~lpContents);
	if (hr != hrSuccess) {
		kc_perror("Unable to retrieve contents of folder", hr);
		goto exit;
	}
	// convert table to ical data
	hr = HrGetIcal(lpContents, blCensorFlag, &strIcal);
	if (hr != hrSuccess) {
		kc_pwarn("Unable to retrieve ical data", hr);
		goto exit;
	}
	hr = HrGetOneProp(m_lpUsrFld, PR_LOCAL_COMMIT_TIME_MAX, &~lpProp);
	if (hr == hrSuccess)
		strModtime = SPropValToString(lpProp);

exit:
	if (hr == hrSuccess)
	{
		if (!strModtime.empty())
			m_lpRequest.HrResponseHeader("Etag", strModtime);
		m_lpRequest.HrResponseHeader("Content-Type", "text/calendar; charset=\"utf-8\"");
		if (strIcal.empty()) {
			m_lpRequest.HrResponseHeader(204, "No Content");
		} else {
			m_lpRequest.HrResponseHeader(200, "OK");
			strMsg = "attachment; filename=\"" + (m_wstrFldName.empty() ? "Calendar" : W2U(m_wstrFldName.substr(0,10))) + ".ics\"";
			m_lpRequest.HrResponseHeader("Content-Disposition", strMsg);
		}
		if (strMethod.compare("GET") == 0)
			m_lpRequest.HrResponseBody(strIcal);
		// @todo, send Content-Length header? but HrFinalize in HTTP class will also send with size:0
	}
	else if (hr == MAPI_E_NOT_FOUND)
	{
		m_lpRequest.HrResponseHeader(404, "Not Found");
	}
	else
		m_lpRequest.HrResponseHeader(500, "Internal Server Error");
	return hr;
}

/**
 * Handles ical requests to add, modify & delete calendar entries
 *
 * @param	strServerTZ				server timezone string set in ical.cfg
 * @return	HRESULT
 * @retval	MAPI_E_NO_ACCESS		insufficient permissions on folder or message
 * @retval	MAPI_E_INVALID_OBJECT	no mapi message in ical data
 */
HRESULT iCal::HrHandleIcalPost()
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPITable> lpContTable;
	SBinary sbEid = {0, 0}, sbUid = {0, 0};
	ULONG ulItemCount = 0;
	time_t tLastMod = 0;
	bool blCensorPrivate = false;
	std::string strUidString, strIcal;
	eIcalType etype = VEVENT;
	std::map<std::string, int> mpIcalEntries;
	std::map<std::string, FILETIME> mpSrvTimes;
	std::map<std::string, SBinary> mpSrvEntries;
	decltype(mpIcalEntries)::const_iterator mpIterI;
	decltype(mpSrvEntries)::const_iterator mpIterJ;
	unsigned int ulProptag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
	SizedSPropTagArray(3, proptags) = {3, {PR_ENTRYID, PR_LAST_MODIFICATION_TIME, ulProptag}};
	//Include PR_ENTRYID,PR_LAST_MODIFICATION_TIME & Named Prop GlobalObjUid.

	//retrive entries from ical data.
	std::unique_ptr<ICalToMapi> lpICalToMapi;
	hr = CreateICalToMapi(m_lpUsrFld, m_lpAddrBook, false, &unique_tie(lpICalToMapi));
	if (hr != hrSuccess) {
		kc_perrorf("CreateICalToMapi", hr);
		goto exit;
	}
	m_lpRequest.HrGetBody(&strIcal);
	if(!strIcal.empty())
	{
		hr = lpICalToMapi->ParseICal(strIcal, m_strCharset, m_strSrvTz, m_lpLoginUser, 0);
		if (hr!=hrSuccess)
			goto exit;
	}

	ulItemCount = lpICalToMapi->GetItemCount();
	//map of Ical entries.
	//generate map for each entry's UID and Position.
	for (ULONG i = 0; i < ulItemCount; ++i) {
		hr = lpICalToMapi->GetItemInfo(i, &etype, &tLastMod, &sbEid);
		if (hr != hrSuccess || etype != VEVENT)
			continue;
		strUidString = bin2hex(sbEid);
		mpIcalEntries[strUidString] = i;
	}

	if ((m_ulFolderFlag & SHARED_FOLDER) && !HasDelegatePerm(m_lpDefStore, m_lpActiveStore))
		blCensorPrivate = true;
	hr = m_lpUsrFld->GetContentsTable(0, &~lpContTable);
	if(hr != hrSuccess)
		goto exit;
	hr = lpContTable->SetColumns(proptags, 0);
	if (hr != hrSuccess)
		goto exit;

	//Map of server Entries.
	//Generate map of UID : Modification time & UID : ENTRYID.
	while (TRUE)
	{
		rowset_ptr lpRows;
		hr = lpContTable->QueryRows(50, 0, &~lpRows);
		if (hr != hrSuccess)
			goto exit;
		if (lpRows->cRows == 0)
			break;

		for (ULONG i = 0; i < lpRows->cRows; ++i) {
			if (lpRows[i].lpProps[0].ulPropTag != PR_ENTRYID)
				continue;
			if (lpRows[i].lpProps[2].ulPropTag == ulProptag)
				sbUid = lpRows[i].lpProps[2].Value.bin;
			else
				continue; // skip new entries
			sbEid.cb = lpRows[i].lpProps[0].Value.bin.cb;
			hr = KAllocCopy(lpRows[i].lpProps[0].Value.bin.lpb, sbEid.cb, reinterpret_cast<void **>(&sbEid.lpb));
			if (hr != hrSuccess)
				goto exit;
			strUidString = bin2hex(sbUid);
			mpSrvEntries[strUidString] = sbEid;
			if (lpRows[i].lpProps[1].ulPropTag == PR_LAST_MODIFICATION_TIME)
				mpSrvTimes[strUidString] = lpRows[i].lpProps[1].Value.ft;
		}
	}

	mpIterI = mpIcalEntries.cbegin();
	mpIterJ = mpSrvEntries.cbegin();
	//Iterate through entries and perform ADD, DELETE, Modify.
	while(1)
	{
		if (mpIterJ == mpSrvEntries.cend() && mpIterI == mpIcalEntries.cend())
			break;

		if (mpIcalEntries.cend() == mpIterI && mpSrvEntries.cend() != mpIterJ) {
			hr = HrDelMessage(mpIterJ->second, blCensorPrivate);
			if(hr != hrSuccess)
			{
				kc_perror("Unable to delete message", hr);
				goto exit;
			}
			++mpIterJ;
		} else if (mpIcalEntries.cend() != mpIterI && mpSrvEntries.cend() == mpIterJ) {
			hr = HrAddMessage(lpICalToMapi.get(), mpIterI->second);
			if(hr != hrSuccess)
			{
				kc_perror("Unable to add new message", hr);
				goto exit;
			}
			++mpIterI;
		} else if (mpSrvEntries.cend() != mpIterJ && mpIcalEntries.cend() != mpIterI) {
			if(!mpIterI->first.compare(mpIterJ->first))
			{
				lpICalToMapi->GetItemInfo(mpIterI->second, &etype, &tLastMod, &sbEid);
				if (etype == VEVENT && FileTimeToUnixTime(mpSrvTimes[mpIterJ->first]) != tLastMod) {
					hr = HrModify(lpICalToMapi.get(), mpIterJ->second, mpIterI->second, blCensorPrivate);
					if(hr != hrSuccess)
					{
						kc_perror("Unable to modify message", hr);
						goto exit;
					}
				}
				++mpIterI;
				++mpIterJ;
			}
			else if( mpIterI->first.compare(mpIterJ->first) < 0 )
			{
				hr = HrAddMessage(lpICalToMapi.get(), mpIterI->second);
				if(hr != hrSuccess)
				{
					kc_perror("Unable to add new message", hr);
					goto exit;
				}
				++mpIterI;
			}
			else if(mpIterI->first.compare(mpIterJ->first) > 0  )
			{
				hr = HrDelMessage(mpIterJ->second, blCensorPrivate);
				if(hr != hrSuccess)
				{
					kc_perror("Unable to delete message", hr);
					goto exit;
				}
				++mpIterJ;
			}
		}//else if
	}//while

	if(m_ulFolderFlag & DEFAULT_FOLDER)
		hr = HrPublishDefaultCalendar(m_lpSession, m_lpDefStore, time(NULL), FB_PUBLISH_DURATION);
	if(hr != hrSuccess) {
		hr = hrSuccess;
		ec_log_err("Error publishing freebusy for user %ls", m_wstrUser.c_str());
	}

exit:
	if(hr == hrSuccess || hr == MAPI_E_INVALID_OBJECT)
		m_lpRequest.HrResponseHeader(200,"OK");
	else if (hr == MAPI_E_NO_ACCESS)
		m_lpRequest.HrResponseHeader(403,"Forbidden");
	else
		m_lpRequest.HrResponseHeader(500,"Internal Server Error");
	for (mpIterJ = mpSrvEntries.cbegin(); mpIterJ != mpSrvEntries.cend(); ++mpIterJ)
		MAPIFreeBuffer(mpIterJ->second.lpb);
	mpSrvEntries.clear();
	mpIcalEntries.clear();
	mpSrvTimes.clear();
	return hr;
}

/**
 * Edits the existing message in the folder
 *
 * @param[in]	lpIcal2Mapi		ical to mapi conversion object
 * @param[in]	sbSrvEid		EntryID of the message to be edited
 * @param[in]	ulPos			Position in the list of messages in conversion object
 * @param[in]	blCensor		boolean to block edit of private items
 * @return		HRESULT
 * @retval		MAPI_E_NO_ACCESS	unable to edit private item
 */
HRESULT iCal::HrModify( ICalToMapi *lpIcal2Mapi, SBinary sbSrvEid, ULONG ulPos, bool blCensor)
{
	object_ptr<IMessage> lpMessage;
	ULONG ulObjType=0;
	unsigned int ulTagPrivate = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);
	HRESULT hr = m_lpUsrFld->OpenEntry(sbSrvEid.cb, reinterpret_cast<ENTRYID *>(sbSrvEid.lpb),
	             &iid_of(lpMessage), MAPI_BEST_ACCESS, &ulObjType, &~lpMessage);
	if(hr != hrSuccess)
		return hr;
	if (blCensor && IsPrivate(lpMessage, ulTagPrivate))
		return MAPI_E_NO_ACCESS;
	hr = lpIcal2Mapi->GetItem(ulPos, 0, lpMessage);
	if(hr != hrSuccess)
		return hr;
	return lpMessage->SaveChanges(0);
}

/**
 * Creates a new message in the folder and sets its properties
 *
 * @param[in]	lpIcal2Mapi		ical to mapi conversion object
 * @param[in]	ulPos			the possition of the messasge in list
 * @return		HRESULT
 */
HRESULT iCal::HrAddMessage(ICalToMapi *lpIcal2Mapi, ULONG ulPos)
{
	object_ptr<IMessage> lpMessage;
	HRESULT hr = m_lpUsrFld->CreateMessage(nullptr, 0, &~lpMessage);
	if (hr != hrSuccess)
		return hr;
	hr = lpIcal2Mapi->GetItem(ulPos, 0, lpMessage);
	if (hr != hrSuccess)
		return kc_perror("Error creating a new calendar entry", hr);
	hr = lpMessage->SaveChanges(0);
	if (hr != hrSuccess)
		return kc_perror("Error saving a new calendar entry", hr);
	return hrSuccess;
}

/**
 * Deletes the mapi message
 *
 * The message is moved to wastebasket(deleted items folder)
 *
 * @param[in]	sbEid		EntryID of the message to be deleted
 * @param[in]	blCensor	boolean to block deletion of private messages
 *
 * @return		HRESULT
 */
HRESULT iCal::HrDelMessage(SBinary sbEid, bool blCensor)
{
	memory_ptr<ENTRYLIST> lpEntryList;
	object_ptr<IMessage> lpMessage;
	ULONG ulObjType = 0;
	unsigned int ulTagPrivate = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);
	HRESULT hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~lpEntryList);
	if (hr != hrSuccess)
		return kc_perror("Error allocating memory", hr);
	lpEntryList->cValues = 1;

	hr = MAPIAllocateMore(sizeof(SBinary), lpEntryList, (void**)&lpEntryList->lpbin);
	if(hr != hrSuccess)
		return kc_perror("Error allocating memory", hr);
	hr = m_lpUsrFld->OpenEntry(sbEid.cb, reinterpret_cast<ENTRYID *>(sbEid.lpb),
	     &iid_of(lpMessage), MAPI_BEST_ACCESS, &ulObjType, &~lpMessage);
	if(hr != hrSuccess)
		return hr;
	if(blCensor && IsPrivate(lpMessage, ulTagPrivate))
		return hrSuccess; /* ignoring private items */

	lpEntryList->lpbin[0].cb = sbEid.cb;
	hr = KAllocCopy(sbEid.lpb, sbEid.cb, reinterpret_cast<void **>(&lpEntryList->lpbin[0].lpb), lpEntryList);
	if (hr != hrSuccess)
		return hr;
	hr = m_lpUsrFld->DeleteMessages(lpEntryList, 0, NULL, MESSAGE_DIALOG);
	if(hr != hrSuccess)
		return kc_perror("Error while deleting a calendar entry", hr);
	return hrSuccess;
}

/**
 * Returns Table according to the entries requested.
 *
 * Restction is applied on table only when single calendar entry is requested
 * using http GET request, else all the calendar entries are sent.
 *
 * @param[out]	lppTable	Table containing rows of calendar entries as requested in URL.
 *
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	user's folder is not set in m_lpUsrFld
 */
HRESULT iCal::HrGetContents(LPMAPITABLE *lppTable)
{
	std::string strUrl;
	memory_ptr<SRestriction> lpsRestriction;
	MAPITablePtr ptrContents;
	static constexpr const SizedSPropTagArray(1, sPropEntryIdcol) = {1, {PR_ENTRYID}};
	ULONG ulRows = 0;

	if (m_lpUsrFld == nullptr)
		return MAPI_E_NOT_FOUND;
	auto hr = m_lpUsrFld->GetContentsTable(0, &~ptrContents);
	if (hr != hrSuccess)
		return kc_perror("Error retrieving calendar entries", hr);
	hr = ptrContents->SetColumns(sPropEntryIdcol, 0);
	if (hr != hrSuccess)
		return hr;
	m_lpRequest.HrGetUrl(&strUrl);
	auto strUid = StripGuid(strUrl);
	if (!strUid.empty()) {
		// single item requested
		hr = HrMakeRestriction(strUid, m_lpNamedProps, &~lpsRestriction);
		if (hr != hrSuccess)
			return hr;
		hr = ptrContents->Restrict(lpsRestriction, TBL_BATCH);
		if (hr != hrSuccess)
			return kc_perror("Error restricting calendar entries", hr);
		// single item not present, return 404
		hr = ptrContents->GetRowCount(0, &ulRows);
		if (hr != hrSuccess || ulRows != 1)
			return MAPI_E_NOT_FOUND;
	}
	return ptrContents->QueryInterface(IID_IMAPITable, (LPVOID*)lppTable);
}

/**
 * Converts mapi messages of the table into ical data
 *
 * @param[in]	lpTable				Table containing mapi messages
 * @param[in]	blCensorPrivate		Flag to censor private items while accessing shared folders
 * @param[out]	lpstrIcal			Pointer to string containing ical data of the mapi messages
 * @return		HRESULT
 * @retval		E_FAIL				Error creating MapiToICal object
 */
HRESULT iCal::HrGetIcal(IMAPITable *lpTable, bool blCensorPrivate, std::string *lpstrIcal)
{
	ULONG ulObjType = 0;
	std::unique_ptr<MapiToICal> lpMtIcal;
	std::string strical;
	unsigned int ulTagPrivate = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);
	auto hr = CreateMapiToICal(m_lpAddrBook, "utf-8", &unique_tie(lpMtIcal));
	if (hr != hrSuccess) {
		kc_perror("Error Creating MapiToIcal object", hr);
		return hr;
	}

	while (TRUE)
	{
		rowset_ptr lpRows;
		hr = lpTable->QueryRows(50, 0, &~lpRows);
		if (hr != hrSuccess)
			return kc_perror("Error retrieving table rows", hr);
		if (lpRows->cRows == 0)
			break;

		for (ULONG i = 0; i < lpRows->cRows; ++i) {
			bool blCensor = blCensorPrivate; // reset censor flag for next message
			if (lpRows[i].lpProps[0].ulPropTag != PR_ENTRYID)
				continue;
			SBinary sbEid = lpRows[i].lpProps[0].Value.bin;
			object_ptr<IMessage> lpMessage;
			hr = m_lpUsrFld->OpenEntry(sbEid.cb, (LPENTRYID)sbEid.lpb,
			     &iid_of(lpMessage), MAPI_BEST_ACCESS, &ulObjType, &~lpMessage);
			if (hr != hrSuccess)
			{
				ec_log_debug("Error opening message for ical conversion: %s (%x)",
					GetMAPIErrorMessage(hr), hr);
				ec_log_debug("%d \n %s", sbEid.cb, bin2hex(sbEid).c_str());
				// Ignore error, just skip the message
				hr = hrSuccess;
				continue;
			}
			hr = lpMtIcal->AddMessage(lpMessage, m_strSrvTz,
			     blCensor && IsPrivate(lpMessage, ulTagPrivate) ? M2IC_CENSOR_PRIVATE : 0);
			if (hr != hrSuccess)
			{
				ec_log_debug("Error converting mapi message to ical: %s (%x)",
					GetMAPIErrorMessage(hr), hr);
				// Ignore broken message
				hr = hrSuccess;
			}
		}
	}

	hr = lpMtIcal->Finalize(0, NULL, lpstrIcal);
	if (hr != hrSuccess)
		ec_log_debug("Unable to create ical output of calendar: %s (%x)", GetMAPIErrorMessage(hr), hr);
	return hr;
}

/**
 * Deletes the current selected folder (m_lpUsrFld)
 *
 * Used to delete folders created during publish operation in iCal.app,
 * the folder is moved to wastebasket(Deleted items folder)
 * Only user created folders can be deleted. Special folders which are
 * required by the model may not be deleted.
 *
 * @return	HRESULT
 * @retval	MAPI_E_NO_ACCESS	insufficient permissions to delete folder
 */
HRESULT iCal::HrDelFolder()
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpWstBoxEid, lpFldEid;
	object_ptr<IMAPIFolder> lpWasteBoxFld;
	ULONG ulObjType = 0;

	if (!m_blFolderAccess) {
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	// Folder is not protected, so now we can move it to the wastebasket folder
	hr = HrGetOneProp(m_lpActiveStore, PR_IPM_WASTEBASKET_ENTRYID, &~lpWstBoxEid);
	if (hr != hrSuccess)
		goto exit;
	hr = m_lpActiveStore->OpenEntry(lpWstBoxEid->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpWstBoxEid->Value.bin.lpb),
	     &iid_of(lpWasteBoxFld), MAPI_MODIFY, &ulObjType, &~lpWasteBoxFld);
	if (hr != hrSuccess)
	{
		kc_perror("Error opening \"Deleted items\" folder", hr);
		goto exit;
	}
	hr = HrGetOneProp(m_lpUsrFld, PR_ENTRYID, &~lpFldEid);
	if (hr != hrSuccess)
		goto exit;
	hr = m_lpIPMSubtree->CopyFolder(lpFldEid->Value.bin.cb, (LPENTRYID)lpFldEid->Value.bin.lpb, NULL, lpWasteBoxFld, NULL, 0, NULL, MAPI_MOVE);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (hr == hrSuccess)
		m_lpRequest.HrResponseHeader(200,"OK");
	else if (hr == MAPI_E_NO_ACCESS)
		m_lpRequest.HrResponseHeader(403,"Forbidden");
	else
		m_lpRequest.HrResponseHeader(500,"Internal Server Error");
	return hr;
}
