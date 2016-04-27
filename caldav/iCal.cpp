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
#include "iCal.h"
#include "CalDavUtil.h"

#include <vector>

#include <kopano/CommonUtil.h>
#include <kopano/restrictionutil.h>
#include "icaluid.h"

#include <libxml/tree.h>
#include <libxml/parser.h>
#include "PublishFreeBusy.h"

#include <kopano/mapi_ptr.h>

using namespace std;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static const char THIS_FILE[] = __FILE__;
#endif

/**
 * Default constructor
 */
iCal::iCal(Http *lpRequest, IMAPISession *lpSession, ECLogger *lpLogger, std::string strSrvTz, std::string strCharset) : ProtocolBase(lpRequest, lpSession, lpLogger, strSrvTz, strCharset)
{
}

/**
 * Default destructor
 */
iCal::~iCal()
{
}

HRESULT iCal::HrHandleCommand(const std::string &strMethod)
{
	HRESULT hr = hrSuccess;

	if (!strMethod.compare("GET") || !strMethod.compare("HEAD"))
		hr = HrHandleIcalGet(strMethod);
	else if (!strMethod.compare("PUT"))
		hr = HrHandleIcalPost();
	else if (!strMethod.compare("DELETE"))
		hr = HrDelFolder();
	else
		hr = MAPI_E_CALL_FAILED;

	return hr;	
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
	HRESULT hr = hrSuccess;
	std::string strIcal;
	std::string strMsg;
	std::string strModtime;
	LPSPropValue lpProp = NULL;
	LPMAPITABLE lpContents = NULL;
	bool blIsWholeCal = true;
	bool blCensorFlag = 0;

	if ((m_ulFolderFlag & SHARED_FOLDER) && !HasDelegatePerm(m_lpDefStore, m_lpActiveStore))
		blCensorFlag = true;
	
	// retrieve table and restrict as per request
	hr = HrGetContents(&lpContents);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to retrieve contents of folder, error code: 0x%08X", hr);
		goto exit;
	}

	// convert table to ical data
	hr = HrGetIcal(lpContents, blCensorFlag, &strIcal);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to retrieve ical data, error code: 0x%08X", hr);
		goto exit;
	}
	
	hr = HrGetOneProp(m_lpUsrFld, PR_LOCAL_COMMIT_TIME_MAX, &lpProp);
	if (hr == hrSuccess)
		strModtime = SPropValToString(lpProp);

	
exit:
	if (hr == hrSuccess)
	{
		if (!strModtime.empty())
			m_lpRequest->HrResponseHeader("Etag", strModtime);
		m_lpRequest->HrResponseHeader("Content-Type", "text/calendar; charset=\"utf-8\""); 

		if (strIcal.empty()) {
			m_lpRequest->HrResponseHeader(204, "No Content");
		} else {
			m_lpRequest->HrResponseHeader(200, "OK");
			if(!blIsWholeCal)
				strMsg = "attachment; filename=\"" + W2U(m_wstrFldName) + "\"";
			else
				strMsg = "attachment; filename=\"" + (m_wstrFldName.empty() ? "Calendar" : W2U(m_wstrFldName.substr(0,10))) + ".ics\"";
			m_lpRequest->HrResponseHeader("Content-Disposition", strMsg);
		}
		if (strMethod.compare("GET") == 0)
			m_lpRequest->HrResponseBody(strIcal);
		// @todo, send Content-Length header? but HrFinalize in HTTP class will also send with size:0
	}
	else if (hr == MAPI_E_NOT_FOUND)
	{
		m_lpRequest->HrResponseHeader(404, "Not Found");
	}
	else
		m_lpRequest->HrResponseHeader(500, "Internal Server Error");
	
	MAPIFreeBuffer(lpProp);
	if (lpContents)
		lpContents->Release();

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
	LPSPropTagArray lpPropTagArr = NULL;
	LPMAPITABLE lpContTable = NULL;
	LPSRowSet lpRows = NULL;
	SBinary sbEid = {0,0};
	SBinary sbUid = {0,0};
	ULONG ulItemCount = 0;
	ULONG ulProptag = 0;
	ULONG cValues = 0;
	ICalToMapi *lpICalToMapi = NULL;
	time_t tLastMod = 0;
	bool blCensorPrivate = false;

	std::string strUidString;
	std::string strIcal;
	
	eIcalType etype = VEVENT;
	FILETIME ftModTime;
	time_t tUnixModTime;
	map<std::string, int> mpIcalEntries;
	map<std::string, FILETIME> mpSrvTimes;
	map<std::string,SBinary> mpSrvEntries;
	
	map<std::string, int>::const_iterator mpIterI;
	map<std::string,SBinary>::const_iterator mpIterJ;


	ulProptag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_GOID], PT_BINARY);
	cValues = 3;
	if ((hr = MAPIAllocateBuffer(CbNewSPropTagArray(cValues), (void **)&lpPropTagArr)) != hrSuccess)
		goto exit;

	//Include PR_ENTRYID,PR_LAST_MODIFICATION_TIME & Named Prop GlobalObjUid.
	lpPropTagArr->cValues = cValues;
	
	lpPropTagArr->aulPropTag[0] = PR_ENTRYID;
	lpPropTagArr->aulPropTag[1] = PR_LAST_MODIFICATION_TIME;
	lpPropTagArr->aulPropTag[2] = ulProptag;
	
	//retrive entries from ical data.
	CreateICalToMapi(m_lpActiveStore, m_lpAddrBook, false, &lpICalToMapi);

	m_lpRequest->HrGetBody(&strIcal);
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
		
		strUidString = bin2hex((ULONG)sbEid.cb,(LPBYTE)sbEid.lpb);
		mpIcalEntries[strUidString] = i;
	}

	if ((m_ulFolderFlag & SHARED_FOLDER) && !HasDelegatePerm(m_lpDefStore, m_lpActiveStore))
		blCensorPrivate = true;
	
	hr = m_lpUsrFld->GetContentsTable( 0, &lpContTable);
	if(hr != hrSuccess)
		goto exit;

	
	hr = lpContTable->SetColumns((LPSPropTagArray)lpPropTagArr, 0);
	if (hr != hrSuccess)
		goto exit;

	//Map of server Entries.
	//Generate map of UID : Modification time & UID : ENTRYID.
	while (TRUE)
	{
		hr = lpContTable->QueryRows(50, 0, &lpRows);
		if (hr != hrSuccess)
			goto exit;

		if (lpRows->cRows == 0)
			break;

		for (ULONG i = 0; i < lpRows->cRows; ++i) {
			if (lpRows->aRow[i].lpProps[0].ulPropTag == PR_ENTRYID)
			{
				if(lpRows->aRow[i].lpProps[2].ulPropTag == ulProptag)
					sbUid = lpRows->aRow[i].lpProps[2].Value.bin;
				else
					continue;// skip new entries

				sbEid.cb = lpRows->aRow[i].lpProps[0].Value.bin.cb;
				if ((hr = MAPIAllocateBuffer(sbEid.cb,(void**)&sbEid.lpb)) != hrSuccess)
					goto exit;
				memcpy(sbEid.lpb, lpRows->aRow[i].lpProps[0].Value.bin.lpb, sbEid.cb);

				strUidString =  bin2hex((ULONG)sbUid.cb,(LPBYTE)sbUid.lpb);
			
				mpSrvEntries[strUidString] = sbEid;
				
				if(lpRows->aRow[i].lpProps[1].ulPropTag == PR_LAST_MODIFICATION_TIME)
					mpSrvTimes[strUidString] = lpRows->aRow[i].lpProps[1].Value.ft;				
			}
		}

		if(lpRows)
		{
			FreeProws(lpRows);
			lpRows = NULL;
		}
	}

	mpIterI = mpIcalEntries.begin();
	mpIterJ = mpSrvEntries.begin();
	//Iterate through entries and perform ADD, DELETE, Modify.
	while(1)
	{
		if(mpIterJ == mpSrvEntries.end() && mpIterI == mpIcalEntries.end())
			break;
		
		if(mpIcalEntries.end() == mpIterI && mpSrvEntries.end() != mpIterJ )
		{
			hr = HrDelMessage(mpIterJ->second, blCensorPrivate);
			if(hr != hrSuccess)
			{
				m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to Delete Message : 0x%08X", hr);
				goto exit;
			}
			++mpIterJ;
		}
		else if(mpIcalEntries.end() != mpIterI && mpSrvEntries.end() == mpIterJ)
		{
			hr = HrAddMessage(lpICalToMapi, mpIterI->second);
			if(hr != hrSuccess)
			{
				m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to Add New Message : 0x%08X", hr);
				goto exit;
			}
			++mpIterI;
		}
		else if(mpSrvEntries.end() != mpIterJ && mpIcalEntries.end() != mpIterI )
		{
			if(!mpIterI->first.compare(mpIterJ->first))
			{

				lpICalToMapi->GetItemInfo(mpIterI->second, &etype, &tLastMod, &sbEid);
				ftModTime =  mpSrvTimes[mpIterJ->first];
				FileTimeToUnixTime(ftModTime, &tUnixModTime);
				if(tUnixModTime != tLastMod && etype == VEVENT)
				{
					hr = HrModify(lpICalToMapi, mpIterJ->second, mpIterI->second, blCensorPrivate);
					if(hr != hrSuccess)
					{
						m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to Modify Message : 0x%08X", hr);
						goto exit;
					}
				}
				++mpIterI;
				++mpIterJ;
			}
			else if( mpIterI->first.compare(mpIterJ->first) < 0 )
			{
				hr = HrAddMessage(lpICalToMapi, mpIterI->second);
				if(hr != hrSuccess)
				{
					m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to Add New Message : 0x%08X", hr);
					goto exit;
				}
				++mpIterI;
			}
			else if(mpIterI->first.compare(mpIterJ->first) > 0  )
			{
				hr = HrDelMessage(mpIterJ->second, blCensorPrivate);
				if(hr != hrSuccess)
				{
					m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to Delete Message : 0x%08X", hr);
					goto exit;
				}
				++mpIterJ;
			}
		}//else if 
	}//while

	if(m_ulFolderFlag & DEFAULT_FOLDER)
		hr = HrPublishDefaultCalendar(m_lpSession, m_lpDefStore, time(NULL), FB_PUBLISH_DURATION, m_lpLogger);

	if(hr != hrSuccess) {
		hr = hrSuccess;
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Error publishing freebusy for user %ls", m_wstrUser.c_str());
	}

exit:
	if(hr == hrSuccess || hr == MAPI_E_INVALID_OBJECT)
		m_lpRequest->HrResponseHeader(200,"OK");
	else if (hr == MAPI_E_NO_ACCESS)
		m_lpRequest->HrResponseHeader(403,"Forbidden");
	else
		m_lpRequest->HrResponseHeader(500,"Internal Server Error");

	for (mpIterJ = mpSrvEntries.begin(); mpIterJ != mpSrvEntries.end(); ++mpIterJ)
		MAPIFreeBuffer(mpIterJ->second.lpb);
	
	if(lpContTable)
		lpContTable->Release();
	
	if(lpRows)
		FreeProws(lpRows);

	if(lpICalToMapi)
		delete lpICalToMapi;
	
	MAPIFreeBuffer(lpPropTagArr);
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
	HRESULT hr = hrSuccess;
	LPMESSAGE lpMessage = NULL;
	ULONG ulObjType=0;
	ULONG ulTagPrivate = 0;

	ulTagPrivate = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);

	hr = m_lpUsrFld->OpenEntry(sbSrvEid.cb, (LPENTRYID) sbSrvEid.lpb, NULL, MAPI_BEST_ACCESS,
			&ulObjType, (LPUNKNOWN *) &lpMessage);
	if(hr != hrSuccess)
		goto exit;

	if (blCensor && IsPrivate(lpMessage, ulTagPrivate))
	{
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	hr = lpIcal2Mapi->GetItem(ulPos, 0, lpMessage);
	if(hr != hrSuccess)
		goto exit;
	
	hr = lpMessage->SaveChanges(0);

exit:
	if(lpMessage)
		lpMessage->Release();
	
	return hr;
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
	HRESULT hr = hrSuccess;
	LPMESSAGE lpMessage = NULL;

	hr = m_lpUsrFld->CreateMessage(NULL, 0, &lpMessage);
	if (hr != hrSuccess)
		goto exit;

	hr = lpIcal2Mapi->GetItem(ulPos, 0, lpMessage);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR,"Error creating a new calendar entry, error code : 0x%08X",hr);
		goto exit;
	}

	hr = lpMessage->SaveChanges(0);
	if (hr != hrSuccess)
		m_lpLogger->Log(EC_LOGLEVEL_ERROR,"Error saving a new calendar entry, error code : 0x%08X",hr);

exit:
	if (lpMessage)
		lpMessage->Release();

	return hr;
}

/**
 * Deletes the mapi message
 * 
 * The message is moved to wastebasket(deleted items folder)
 * 
 * @param[in]	sbEid		EntryID of the message to be deleted
 * @param[in]	blCensor	boolean to block delete of private messages
 *
 * @return		HRESULT 
 */
HRESULT iCal::HrDelMessage(SBinary sbEid, bool blCensor)
{
	HRESULT hr = hrSuccess;
	LPENTRYLIST lpEntryList = NULL;
	LPMESSAGE lpMessage = NULL;
	ULONG ulObjType = 0;
	ULONG ulTagPrivate = 0;
	
	ulTagPrivate = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);

	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), (void**)&lpEntryList);
	if (hr != hrSuccess)
	{
		m_lpLogger->Log(EC_LOGLEVEL_ERROR,"Error allocating memory, error code : 0x%08X",hr);
		goto exit;
	}

	lpEntryList->cValues = 1;

	hr = MAPIAllocateMore(sizeof(SBinary), lpEntryList, (void**)&lpEntryList->lpbin);
	if(hr != hrSuccess)
	{
		m_lpLogger->Log(EC_LOGLEVEL_ERROR,"Error allocating memory, error code : 0x%08X",hr);
		goto exit;
	}

	hr = m_lpUsrFld->OpenEntry(sbEid.cb, (LPENTRYID) sbEid.lpb, NULL, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN *)&lpMessage);
	if(hr != hrSuccess)
		goto exit;
	
	if(blCensor && IsPrivate(lpMessage, ulTagPrivate))
	{
		hr = hrSuccess; // ignoring private items.
		goto exit;
	}

	lpEntryList->lpbin[0].cb = sbEid.cb;
	if ((hr = MAPIAllocateMore(sbEid.cb, lpEntryList, (void**)&lpEntryList->lpbin[0].lpb)) != hrSuccess)
		goto exit;

	memcpy(lpEntryList->lpbin[0].lpb, sbEid.lpb, sbEid.cb);
				
	hr = m_lpUsrFld->DeleteMessages(lpEntryList, 0, NULL, MESSAGE_DIALOG);
	if(hr != hrSuccess)
	{
		m_lpLogger->Log(EC_LOGLEVEL_ERROR,"Error while deleting a calendar entry, error code : 0x%08X",hr);
		goto exit;
	}

exit:
	MAPIFreeBuffer(lpEntryList);
	if(lpMessage)
		lpMessage->Release();

	return hr;
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
	HRESULT hr = hrSuccess;
	std::string strUid;
	std::string strUrl;
	LPSRestriction lpsRestriction = NULL;
	MAPITablePtr ptrContents;	
	SizedSPropTagArray(1, sPropEntryIdcol) = {1, {PR_ENTRYID}};
	ULONG ulRows = 0;

	if (!m_lpUsrFld) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	hr = m_lpUsrFld->GetContentsTable(0, &ptrContents);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR,"Error retrieving calendar entries, error code : 0x%08X",hr);
		goto exit;
	}

	hr = ptrContents->SetColumns((LPSPropTagArray)&sPropEntryIdcol, 0);
	if (hr != hrSuccess)
		goto exit;

	m_lpRequest->HrGetUrl(&strUrl);
	strUid = StripGuid(strUrl);
	if (!strUid.empty()) {
		// single item requested
		hr = HrMakeRestriction(strUid, m_lpNamedProps, &lpsRestriction);
		if (hr != hrSuccess)
			goto exit;

		hr = ptrContents->Restrict(lpsRestriction, TBL_BATCH);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR,"Error restricting calendar entries, error code : 0x%08X",hr);
			goto exit;
		}

		// single item not present, return 404
		hr = ptrContents->GetRowCount(0, &ulRows);
		if (hr != hrSuccess || ulRows != 1) {
			hr = MAPI_E_NOT_FOUND;
			goto exit;
		}
	}
	
	hr = ptrContents->QueryInterface(IID_IMAPITable, (LPVOID*)lppTable);

exit:
	if (lpsRestriction)
		FREE_RESTRICTION(lpsRestriction);

	return hr;
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
	HRESULT hr = hrSuccess;
	LPSRowSet lpRows = NULL;	
	LPMESSAGE lpMessage = NULL;
	SBinary sbEid = {0,0};
	ULONG ulObjType = 0;
	ULONG ulTagPrivate = 0;
	ULONG ulFlag = 0;
	bool blCensor = false;	
	MapiToICal *lpMtIcal = NULL;
	std::string strical;
	
	ulTagPrivate = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PRIVATE], PT_BOOLEAN);
	
	CreateMapiToICal(m_lpAddrBook, "utf-8", &lpMtIcal);
	if (lpMtIcal == NULL) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR,"Error Creating MapiToIcal object, error code : 0x%08X",hr);
		hr = E_FAIL;
		goto exit;
	}

	while (TRUE)
	{
		hr = lpTable->QueryRows(50, 0, &lpRows);
		if (hr != hrSuccess)
		{
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Error retrieving table rows, error code : 0x%08X", hr);
			goto exit;
		}

		if (lpRows->cRows == 0)
			break;

		for (ULONG i = 0; i < lpRows->cRows; ++i) {
			blCensor = blCensorPrivate; // reset censor flag for next message
			ulFlag = 0;

			if (lpRows->aRow[i].lpProps[0].ulPropTag != PR_ENTRYID)
				continue;

			sbEid = lpRows->aRow[i].lpProps[0].Value.bin;

			hr = m_lpUsrFld->OpenEntry(sbEid.cb, (LPENTRYID)sbEid.lpb,
									NULL, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN*)&lpMessage);
			if (hr != hrSuccess)
			{
				m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Error opening message for ical conversion, error code : 0x%08X", hr);
				m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "%d \n %s", sbEid.cb, bin2hex(sbEid.cb,sbEid.lpb).c_str());
				// Ignore error, just skip the message
				hr = hrSuccess;
				continue;
			}
			
			if(blCensor && IsPrivate(lpMessage, ulTagPrivate))
				ulFlag = M2IC_CENSOR_PRIVATE;
			else
				ulFlag = 0;

			hr = lpMtIcal->AddMessage(lpMessage, m_strSrvTz, ulFlag);
			if (hr != hrSuccess)
			{
				m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Error converting mapi message to ical, error code : 0x%08X", hr);
				// Ignore broken message
				hr = hrSuccess;
			}

			lpMessage->Release();
			lpMessage = NULL;
		}
		
		if(lpRows)
		{
			FreeProws(lpRows);
			lpRows = NULL;
		}

	}
	
	hr = lpMtIcal->Finalize(0, NULL, lpstrIcal);
	if (hr != hrSuccess)
		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Unable to create ical output of calendar, error code 0x%08X", hr);

exit:
	if (lpRows)
		FreeProws(lpRows);

	if (lpMessage)
		lpMessage->Release();

	delete lpMtIcal;
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
	LPSPropValue lpWstBoxEid = NULL;
	LPSPropValue lpFldEid = NULL;
	IMAPIFolder *lpWasteBoxFld = NULL;
	ULONG ulObjType = 0;

	if (m_blFolderAccess == false) {
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	// Folder is not protected, so now we can move it to the wastebasket folder
	hr = HrGetOneProp(m_lpActiveStore, PR_IPM_WASTEBASKET_ENTRYID, &lpWstBoxEid);
	if (hr != hrSuccess)
		goto exit;
	
	hr = m_lpActiveStore->OpenEntry(lpWstBoxEid->Value.bin.cb, (LPENTRYID)lpWstBoxEid->Value.bin.lpb, NULL, MAPI_MODIFY, &ulObjType, (LPUNKNOWN*)&lpWasteBoxFld);
	if (hr != hrSuccess)
	{
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Error opening \"Deleted items\" folder, error code : 0x%08X", hr);
		goto exit;
	}

	hr = HrGetOneProp(m_lpUsrFld, PR_ENTRYID, &lpFldEid);
	if (hr != hrSuccess)
		goto exit;

	hr = m_lpIPMSubtree->CopyFolder(lpFldEid->Value.bin.cb, (LPENTRYID)lpFldEid->Value.bin.lpb, NULL, lpWasteBoxFld, NULL, 0, NULL, MAPI_MOVE);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (hr == hrSuccess)
		m_lpRequest->HrResponseHeader(200,"OK");
	else if (hr == MAPI_E_NO_ACCESS)
		m_lpRequest->HrResponseHeader(403,"Forbidden");
	else
		m_lpRequest->HrResponseHeader(500,"Internal Server Error");

	MAPIFreeBuffer(lpWstBoxEid);
	MAPIFreeBuffer(lpFldEid);
	if (lpWasteBoxFld)
		lpWasteBoxFld->Release();

	return hr;
}
