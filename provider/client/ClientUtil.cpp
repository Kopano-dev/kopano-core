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
#include <algorithm>
#include <cctype>
#include "ClientUtil.h"

#include <kopano/ECGetText.h>

#include <mapi.h>
#include <mapidefs.h>
#include <mapiutil.h>

#include <kopano/CommonUtil.h>
#include "WSTransport.h"
#include <kopano/ECConfig.h>

#include "kcore.hpp"
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include <kopano/mapiguidext.h>
#include <kopano/mapiext.h>

#include "Mem.h"
#include <kopano/stringutil.h>

#include <kopano/charset/convstring.h>
#include "EntryPoint.h"

using namespace std;

HRESULT ClientUtil::HrInitializeStatusRow (const char * lpszProviderDisplay, ULONG ulResourceType, LPMAPISUP lpMAPISup, LPSPropValue lpspvIdentity, ULONG ulFlags)
{
	HRESULT			hResult = hrSuccess;
	LPSPropValue	lpspvStatusRow = NULL;
	ULONG			cCurVal = 0;
	unsigned int	size = 0;
	std::wstring	wstrSearchKey;

	hResult = MAPIAllocateBuffer(sizeof(SPropValue) * 13, (void**)&lpspvStatusRow);
	if(hResult != hrSuccess)
		goto exit;

	memset(lpspvStatusRow, 0, sizeof(SPropValue) * 13);

	if(lpszProviderDisplay)
	{
		size = strlen(lpszProviderDisplay)+1;

		// Set the PR_PROVIDER_DISPLAY property:
		lpspvStatusRow[cCurVal].ulPropTag = PR_PROVIDER_DISPLAY_A;
		hResult = MAPIAllocateMore(size, lpspvStatusRow, (void**)&lpspvStatusRow[cCurVal].Value.lpszA);
		if(hResult != hrSuccess)
			goto exit;
		memcpy(lpspvStatusRow[cCurVal].Value.lpszA, lpszProviderDisplay, size);
		++cCurVal;

	// Set the PR_DISPLAY_NAME property
		lpspvStatusRow[cCurVal].ulPropTag = PR_DISPLAY_NAME_A;
		hResult = MAPIAllocateMore(size, lpspvStatusRow, (void**)&lpspvStatusRow[cCurVal].Value.lpszA);
		if(hResult != hrSuccess)
			goto exit;
		memcpy(lpspvStatusRow[cCurVal].Value.lpszA, lpszProviderDisplay, size);
		++cCurVal;
	}

	// PR_PROVIDER_DLL_NAME
	lpspvStatusRow[cCurVal].ulPropTag = PR_PROVIDER_DLL_NAME_A;
	lpspvStatusRow[cCurVal++].Value.lpszA = (LPSTR)WCLIENT_DLL_NAME;

	// Set the PR_STATUS_CODE property:
	lpspvStatusRow[cCurVal].ulPropTag = PR_STATUS_CODE;
	lpspvStatusRow[cCurVal++].Value.l = 1;

	// Set the PR_STATUS_STRING property
	lpspvStatusRow[cCurVal].ulPropTag = PR_STATUS_STRING_W;
	lpspvStatusRow[cCurVal++].Value.lpszW = _W("Available");

	// Set the PR_IDENTITY_ENTRYID property
	lpspvStatusRow[cCurVal].ulPropTag = PR_IDENTITY_ENTRYID;
	lpspvStatusRow[cCurVal++].Value.bin = lpspvIdentity[XPID_EID].Value.bin;

	// Set the PR_IDENTITY_DISPLAY property
	lpspvStatusRow[cCurVal].ulPropTag = PROP_TAG(PROP_TYPE(lpspvIdentity[XPID_NAME].ulPropTag), PROP_ID(PR_IDENTITY_DISPLAY));
	lpspvStatusRow[cCurVal++].Value.LPSZ = lpspvIdentity[XPID_NAME].Value.LPSZ;

	// Set the PR_IDENTITY_SEARCH_KEY property
	lpspvStatusRow[cCurVal].ulPropTag = PR_IDENTITY_SEARCH_KEY;
	lpspvStatusRow[cCurVal++].Value.bin = lpspvIdentity[XPID_SEARCH_KEY].Value.bin;

	// Set the PR_OWN_STORE_ENTRYID property
	lpspvStatusRow[cCurVal].ulPropTag = PR_OWN_STORE_ENTRYID;
	lpspvStatusRow[cCurVal++].Value.bin = lpspvIdentity[XPID_STORE_EID].Value.bin;

	lpspvStatusRow[cCurVal].ulPropTag = PR_RESOURCE_METHODS;
	lpspvStatusRow[cCurVal++].Value.l = STATUS_VALIDATE_STATE;

	lpspvStatusRow[cCurVal].ulPropTag = PR_RESOURCE_TYPE;
	lpspvStatusRow[cCurVal++].Value.l = ulResourceType; //like MAPI_STORE_PROVIDER or MAPI_TRANSPORT_PROVIDER

	hResult = lpMAPISup->ModifyStatusRow(cCurVal, lpspvStatusRow, ulFlags);

exit:
	MAPIFreeBuffer(lpspvStatusRow);
	return hResult;
}

HRESULT ClientUtil::HrSetIdentity(WSTransport *lpTransport, LPMAPISUP lpMAPISup, LPSPropValue* lppIdentityProps)
{
	HRESULT			hr = hrSuccess;
	ULONG			cbEntryStore = 0;
	LPENTRYID		lpEntryStore = NULL;
	LPENTRYID		lpEID = NULL;
	ULONG			cbEID = 0;
	ULONG			cValues = 0;
	ULONG			ulSize = 0;
	ECUSER *lpUser = NULL;
	tstring			strProfileSenderSearchKey;
	LPSPropValue	lpIdentityProps = NULL;

	// Get the username and email adress
	hr = lpTransport->HrGetUser(0, NULL, fMapiUnicode, &lpUser);
	if(hr != hrSuccess)
		goto exit;

	cValues = NUM_IDENTITY_PROPS;
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * cValues, (void**)&lpIdentityProps);
	if (hr != hrSuccess)
		goto exit;
	memset(lpIdentityProps, 0, sizeof(SPropValue) * cValues);

	strProfileSenderSearchKey.reserve(_tcslen(TRANSPORT_ADDRESS_TYPE_ZARAFA) + 1 + _tcslen(lpUser->lpszMailAddress));
	strProfileSenderSearchKey = TRANSPORT_ADDRESS_TYPE_ZARAFA;
	strProfileSenderSearchKey += ':';
	strProfileSenderSearchKey += lpUser->lpszMailAddress;
	std::transform(strProfileSenderSearchKey.begin(), strProfileSenderSearchKey.end(), strProfileSenderSearchKey.begin(), ::toupper);

	lpIdentityProps[XPID_EID].ulPropTag = PR_SENDER_ENTRYID;
	lpIdentityProps[XPID_EID].Value.bin.cb = lpUser->sUserId.cb;
	hr = MAPIAllocateMore(lpUser->sUserId.cb, (LPVOID)lpIdentityProps, (void**)&lpIdentityProps[XPID_EID].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;
	memcpy(lpIdentityProps[XPID_EID].Value.bin.lpb, lpUser->sUserId.lpb, lpUser->sUserId.cb);

	// Create the PR_SENDER_NAME property value.
	lpIdentityProps[XPID_NAME].ulPropTag = PR_SENDER_NAME;
	ulSize = sizeof(TCHAR) * (_tcslen(lpUser->lpszFullName) + 1);
	hr = MAPIAllocateMore(ulSize, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_NAME].Value.LPSZ);
	if (hr != hrSuccess)
		goto exit;
	memcpy(lpIdentityProps[XPID_NAME].Value.LPSZ, lpUser->lpszFullName, ulSize);

	// Create the PR_SENDER_SEARCH_KEY value. 
	lpIdentityProps[XPID_SEARCH_KEY].ulPropTag = PR_SENDER_SEARCH_KEY;
	lpIdentityProps[XPID_SEARCH_KEY].Value.bin.cb = strProfileSenderSearchKey.size()+1;
	hr = MAPIAllocateMore(lpIdentityProps[XPID_SEARCH_KEY].Value.bin.cb, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_SEARCH_KEY].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;
	memcpy(lpIdentityProps[XPID_SEARCH_KEY].Value.bin.lpb, strProfileSenderSearchKey.c_str(), lpIdentityProps[XPID_SEARCH_KEY].Value.bin.cb);

	// PR_SENDER_EMAIL_ADDRESS
	lpIdentityProps[XPID_ADDRESS].ulPropTag = PR_SENDER_EMAIL_ADDRESS;
	ulSize = sizeof(TCHAR) * (_tcslen(lpUser->lpszMailAddress) + 1);
	hr = MAPIAllocateMore(ulSize, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_ADDRESS].Value.LPSZ);
	if (hr != hrSuccess)
		goto exit;
	memcpy(lpIdentityProps[XPID_ADDRESS].Value.LPSZ, lpUser->lpszMailAddress, ulSize);

	// PR_SENDER_ADDRTYPE
	lpIdentityProps[XPID_ADDRTYPE].ulPropTag = PR_SENDER_ADDRTYPE;
	ulSize = sizeof(TCHAR) * (_tcslen(TRANSPORT_ADDRESS_TYPE_ZARAFA) + 1);
	hr = MAPIAllocateMore(ulSize, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_ADDRTYPE].Value.LPSZ);
	if (hr != hrSuccess)
		goto exit;
	memcpy(lpIdentityProps[XPID_ADDRTYPE].Value.LPSZ, TRANSPORT_ADDRESS_TYPE_ZARAFA, ulSize);

	//PR_OWN_STORE_ENTRYID
	// Get the default store for this user, not an issue if it fails when not on home server
	if(lpTransport->HrGetStore(0, NULL, &cbEntryStore, &lpEntryStore, 0, NULL) == hrSuccess) 
	{
		hr = lpMAPISup->WrapStoreEntryID(cbEntryStore, lpEntryStore, &cbEID, (LPENTRYID*)&lpEID);
		if(hr != hrSuccess) 
			goto exit;

		lpIdentityProps[XPID_STORE_EID].ulPropTag = PR_OWN_STORE_ENTRYID;
		lpIdentityProps[XPID_STORE_EID].Value.bin.cb = cbEID;
		hr = MAPIAllocateMore(cbEID, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_STORE_EID].Value.bin.lpb);
		if (hr != hrSuccess)
			goto exit;
		memcpy(lpIdentityProps[XPID_STORE_EID].Value.bin.lpb, (LPBYTE) lpEID, cbEID);
	}
	
	// Set the identity in the global provider identity
	*lppIdentityProps = lpIdentityProps;

exit:
	if(hr != hrSuccess && lpIdentityProps != NULL) {
		MAPIFreeBuffer(lpIdentityProps);
		*lppIdentityProps = NULL;	// just to be sure...
	}
	MAPIFreeBuffer(lpEntryStore);
	MAPIFreeBuffer(lpEID);
	MAPIFreeBuffer(lpUser);
	return hr;
}

/** 
 * ReadReceipt replace function of MAPI for windows. MAPI for windows
 * support only UNICODE properties. We still can't use the support
 * version, since mapi4linux doesn't implement it. We could move this
 * code there, but then outlook and webaccess will sent different read
 * receipt messages.
 * 
 * @param[in] ulFlags 0 or MAPI_NON_READ
 * @param[in] lpReadMessage Original message to send read receipt for
 * @param[in,out] lppEmptyMessage Message to edit
 * 
 * @return MAPI Error code
 */
HRESULT ClientUtil::ReadReceipt(ULONG ulFlags, LPMESSAGE lpReadMessage, LPMESSAGE* lppEmptyMessage)
{
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpSrcPropValue = NULL;
	LPSPropValue	lpDestPropValue = NULL;
	ULONG			ulMaxDestValues = 0;
	ULONG			ulCurDestValues = 0;
	ULONG			cSrcValues = 0;
	ULONG			cbTmp = 0;
	LPBYTE			lpByteTmp = NULL;
	const TCHAR *lpMsgClass = NULL;
	LPTSTR			lpReportText = NULL;
	LPTSTR			lpReadText = NULL;
	FILETIME		ft;	
	LPADRLIST		lpMods = NULL;
	std::wstring	strName;
	std::wstring	strType;
	std::wstring	strAddress;
	tstring			strBodyText;
	time_t			zero = 0;
	time_t			tt;
	struct tm*		tm;
	char			szTime[255];
	IStream*		lpBodyStream = NULL;
	tstring			tSubject;

	// The same properties as under windows
	enum ePropReadReceipt{	RR_REPORT_TAG, RR_CONVERSATION_TOPIC, RR_CONVERSATION_INDEX, 
							RR_SEARCH_KEY, RR_MESSAGE_CLASS, RR_SENDER_SEARCH_KEY,
							RR_SUBJECT, RR_SUBJECT_PREFIX, RR_NORMALIZED_SUBJECT, 
							RR_SENDER_NAME, RR_SENDER_ENTRYID, RR_SENDER_ADDRTYPE,												
							RR_SENDER_EMAIL_ADDRESS, RR_REPORT_NAME, RR_REPORT_ENTRYID, 
							RR_READ_RECEIPT_ENTRYID, RR_RECEIVED_BY_NAME, RR_RECEIVED_BY_ENTRYID, RR_RECEIVED_BY_ADDRTYPE, RR_RECEIVED_BY_EMAIL_ADDRESS,
							RR_PRIORITY, RR_IMPORTANCE, RR_SENT_REPRESENTING_NAME, 
							RR_SENT_REPRESENTING_ENTRYID, RR_SENT_REPRESENTING_SEARCH_KEY, RR_RCVD_REPRESENTING_NAME, RR_RCVD_REPRESENTING_ENTRYID, 
							RR_MESSAGE_DELIVERY_TIME, RR_CLIENT_SUBMIT_TIME, RR_DISPLAY_TO,
							RR_DISPLAY_CC, RR_DISPLAY_BCC, RR_SENSITIVITY, 
							RR_INTERNET_MESSAGE_ID, RR_DELIVER_TIME, RR_SENT_REPRESENTING_ADDRTYPE, RR_SENT_REPRESENTING_EMAIL_ADDRESS,
							RR_MDN_DISPOSITION_TYPE, RR_MDN_DISPOSITION_SENDINGMODE};

	SizedSPropTagArray(39, sPropReadReceipt) = {39, { PR_REPORT_TAG, PR_CONVERSATION_TOPIC, PR_CONVERSATION_INDEX, 
												PR_SEARCH_KEY, PR_MESSAGE_CLASS, PR_SENDER_SEARCH_KEY,
												PR_SUBJECT, PR_SUBJECT_PREFIX, PR_NORMALIZED_SUBJECT, 
												PR_SENDER_NAME, PR_SENDER_ENTRYID, PR_SENDER_ADDRTYPE,												
												PR_SENDER_EMAIL_ADDRESS, PR_REPORT_NAME, PR_REPORT_ENTRYID, 
												PR_READ_RECEIPT_ENTRYID, PR_RECEIVED_BY_NAME, PR_RECEIVED_BY_ENTRYID, PR_RECEIVED_BY_ADDRTYPE, PR_RECEIVED_BY_EMAIL_ADDRESS,
												PR_PRIORITY, PR_IMPORTANCE, PR_SENT_REPRESENTING_NAME, 
												PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_SEARCH_KEY, PR_RCVD_REPRESENTING_NAME, PR_RCVD_REPRESENTING_ENTRYID, 
												PR_MESSAGE_DELIVERY_TIME, PR_CLIENT_SUBMIT_TIME, PR_DISPLAY_TO,
												PR_DISPLAY_CC, PR_DISPLAY_BCC, PR_SENSITIVITY, 
												PR_INTERNET_MESSAGE_ID, PR_DELIVER_TIME, PR_SENT_REPRESENTING_ADDRTYPE, PR_SENT_REPRESENTING_EMAIL_ADDRESS,
												PR_MDN_DISPOSITION_TYPE, PR_MDN_DISPOSITION_SENDINGMODE } };

	// Check incoming parameters
	if(lpReadMessage == NULL || lppEmptyMessage == NULL || *lppEmptyMessage == NULL) {
		hr = MAPI_E_INVALID_OBJECT;
		goto exit;
	}

	if((ulFlags &~ MAPI_NON_READ) != 0) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	GetSystemTimeAsFileTime(&ft);

	if((ulFlags&MAPI_NON_READ) == MAPI_NON_READ) {
		lpMsgClass = _T("REPORT.IPM.Note.IPNNRN");
		lpReadText = _("Not read:");
		lpReportText = _("was not read because it expired before reading at time");
	}else{
		lpMsgClass = _T("REPORT.IPM.Note.IPNRN");
		lpReadText = _("Read:");
		lpReportText = _("was read on");
	}

	hr = lpReadMessage->GetProps((LPSPropTagArray)&sPropReadReceipt, fMapiUnicode, &cSrcValues, &lpSrcPropValue);
	if(FAILED(hr) != hrSuccess)
		goto exit;

	// important properties
	if(lpSrcPropValue[RR_REPORT_ENTRYID].ulPropTag != PR_REPORT_ENTRYID)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	strBodyText = _("Your message");
	strBodyText+= _T("\r\n\r\n");

	if(lpSrcPropValue[RR_DISPLAY_TO].ulPropTag == PR_DISPLAY_TO) {
		strBodyText+= _T("\t");
		strBodyText+= _("To:");
		strBodyText+= _T(" ");
		strBodyText+= lpSrcPropValue[RR_DISPLAY_TO].Value.LPSZ;
		strBodyText+= _T("\r\n");
	}
	if(lpSrcPropValue[RR_DISPLAY_CC].ulPropTag == PR_DISPLAY_CC) {
		strBodyText+= _T("\t");
		strBodyText+= _("Cc:");
		strBodyText+= _T(" ");
		strBodyText+= lpSrcPropValue[RR_DISPLAY_CC].Value.LPSZ;
		strBodyText+= _T("\r\n");
	}
	
	if(lpSrcPropValue[RR_SUBJECT].ulPropTag == PR_SUBJECT) {
		strBodyText+= _T("\t");
		strBodyText+= _("Subject:");
		strBodyText+= _T(" ");
		strBodyText+= lpSrcPropValue[RR_SUBJECT].Value.LPSZ;
		strBodyText+= _T("\r\n");
	}

	if(lpSrcPropValue[RR_CLIENT_SUBMIT_TIME].ulPropTag == PR_CLIENT_SUBMIT_TIME) {
		strBodyText+= _T("\t");
		strBodyText+= _("Sent on:");
		strBodyText+= _T(" ");

		FileTimeToUnixTime(lpSrcPropValue[RR_CLIENT_SUBMIT_TIME].Value.ft, &tt);
		tm = localtime(&tt);
		if(tm == NULL) {
			tm = localtime(&zero);
		}
		strftime(szTime, 255, "%c", tm);

		strBodyText+= convert_to<tstring>(szTime, strlen(szTime), CHARSET_CHAR);
		strBodyText+= _T("\r\n");
	}

	strBodyText+= _T("\r\n");
	strBodyText+= lpReportText;
	strBodyText+= _T(" ");
	
	FileTimeToUnixTime(ft, &tt);
	tm = localtime(&tt);
	if(tm == NULL) {
		tm = localtime(&zero);
	}
	strftime(szTime, 255, "%c", tm);

	strBodyText+= convert_to<tstring>(szTime, strlen(szTime), CHARSET_CHAR);
	strBodyText+= _T("\r\n");

	ulMaxDestValues = cSrcValues + 4;//+ default properties
	hr = MAPIAllocateBuffer(sizeof(SPropValue)*ulMaxDestValues, (void**)&lpDestPropValue);
	if(hr != hrSuccess)
		goto exit;

	memset(lpDestPropValue, 0, sizeof(SPropValue)*ulMaxDestValues);

	// Default properties
	lpDestPropValue[ulCurDestValues].ulPropTag = PR_DELETE_AFTER_SUBMIT;
	lpDestPropValue[ulCurDestValues++].Value.b = true;

	lpDestPropValue[ulCurDestValues].ulPropTag = PR_READ_RECEIPT_REQUESTED;
	lpDestPropValue[ulCurDestValues++].Value.b = false;

	lpDestPropValue[ulCurDestValues].ulPropTag = PR_MESSAGE_FLAGS;
	lpDestPropValue[ulCurDestValues++].Value.ul = 0;

	lpDestPropValue[ulCurDestValues].ulPropTag = PR_MESSAGE_CLASS;
	lpDestPropValue[ulCurDestValues++].Value.LPSZ = const_cast<TCHAR *>(lpMsgClass);
	
	lpDestPropValue[ulCurDestValues].ulPropTag = PR_REPORT_TEXT;
	lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpReportText;

    lpDestPropValue[ulCurDestValues].ulPropTag = PR_REPORT_TIME;
	lpDestPropValue[ulCurDestValues++].Value.ft = ft;
	
	lpDestPropValue[ulCurDestValues].ulPropTag = PR_SUBJECT_PREFIX;
	lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpReadText;

	if(lpSrcPropValue[RR_SUBJECT].ulPropTag == PR_SUBJECT)
	{
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SUBJECT;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_SUBJECT].Value.LPSZ;

		tSubject = tstring(lpReadText) + _T(" ") + lpSrcPropValue[RR_SUBJECT].Value.LPSZ;

		lpDestPropValue[ulCurDestValues].ulPropTag = PR_SUBJECT;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = (LPTSTR)tSubject.c_str();
	}else {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_SUBJECT;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpReadText;
	}

	if(lpSrcPropValue[RR_REPORT_TAG].ulPropTag == PR_REPORT_TAG) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_REPORT_TAG;
		lpDestPropValue[ulCurDestValues++].Value.bin = lpSrcPropValue[RR_REPORT_TAG].Value.bin;
	}

	if(lpSrcPropValue[RR_DISPLAY_TO].ulPropTag == PR_DISPLAY_TO) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_DISPLAY_TO;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_DISPLAY_TO].Value.LPSZ;
	}

	if(lpSrcPropValue[RR_DISPLAY_CC].ulPropTag == PR_DISPLAY_CC) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_DISPLAY_CC;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_DISPLAY_CC].Value.LPSZ;
	}

	if(lpSrcPropValue[RR_DISPLAY_BCC].ulPropTag == PR_DISPLAY_BCC) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_DISPLAY_BCC;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_DISPLAY_BCC].Value.LPSZ;
	}

	if(lpSrcPropValue[RR_CLIENT_SUBMIT_TIME].ulPropTag == PR_CLIENT_SUBMIT_TIME) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SUBMIT_TIME;
		lpDestPropValue[ulCurDestValues++].Value.ft = lpSrcPropValue[RR_CLIENT_SUBMIT_TIME].Value.ft;
	}
	
	if(lpSrcPropValue[RR_DELIVER_TIME].ulPropTag == PR_DELIVER_TIME) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_DELIVERY_TIME;
		lpDestPropValue[ulCurDestValues++].Value.ft = lpSrcPropValue[RR_DELIVER_TIME].Value.ft;
	}	

	if(lpSrcPropValue[RR_CONVERSATION_TOPIC].ulPropTag == PR_CONVERSATION_TOPIC) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_CONVERSATION_TOPIC;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_CONVERSATION_TOPIC].Value.LPSZ;
	}

	if(lpSrcPropValue[RR_CONVERSATION_INDEX].ulPropTag == PR_CONVERSATION_INDEX &&
		ScCreateConversationIndex(lpSrcPropValue[RR_CONVERSATION_INDEX].Value.bin.cb, lpSrcPropValue[RR_CONVERSATION_INDEX].Value.bin.lpb, &cbTmp, &lpByteTmp) == hrSuccess )
	{
		hr = MAPIAllocateMore(cbTmp, lpDestPropValue, (void**)&lpDestPropValue[ulCurDestValues].Value.bin.lpb);
		if(hr != hrSuccess)
			goto exit;
		
		lpDestPropValue[ulCurDestValues].Value.bin.cb = cbTmp;
		memcpy(lpDestPropValue[ulCurDestValues].Value.bin.lpb, lpByteTmp, cbTmp);

		lpDestPropValue[ulCurDestValues++].ulPropTag = PR_CONVERSATION_INDEX;

		MAPIFreeBuffer(lpByteTmp);
		lpByteTmp = NULL;
	}

	if(lpSrcPropValue[RR_IMPORTANCE].ulPropTag == PR_IMPORTANCE)
	{
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_IMPORTANCE;
		lpDestPropValue[ulCurDestValues++].Value.ul = lpSrcPropValue[RR_IMPORTANCE].Value.ul;
	}

	if(lpSrcPropValue[RR_PRIORITY].ulPropTag == PR_PRIORITY) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_PRIORITY;
		lpDestPropValue[ulCurDestValues++].Value.ul = lpSrcPropValue[RR_PRIORITY].Value.ul;
	}

	if(lpSrcPropValue[RR_SENDER_NAME].ulPropTag == PR_SENDER_NAME) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENDER_NAME;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_SENDER_NAME].Value.LPSZ;
	}
	
	if(lpSrcPropValue[RR_SENDER_ADDRTYPE].ulPropTag == PR_SENDER_ADDRTYPE) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENDER_ADDRTYPE;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_SENDER_ADDRTYPE].Value.LPSZ;
	}

	if(lpSrcPropValue[RR_SENDER_ENTRYID].ulPropTag == PR_SENDER_ENTRYID) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENDER_ENTRYID;
		lpDestPropValue[ulCurDestValues++].Value.bin = lpSrcPropValue[RR_SENDER_ENTRYID].Value.bin;
	}

	if(lpSrcPropValue[RR_SENDER_SEARCH_KEY].ulPropTag == PR_SENDER_SEARCH_KEY) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENDER_SEARCH_KEY;
		lpDestPropValue[ulCurDestValues++].Value.bin = lpSrcPropValue[RR_SENDER_SEARCH_KEY].Value.bin;
	}

	if(lpSrcPropValue[RR_SENDER_EMAIL_ADDRESS].ulPropTag == PR_SENDER_EMAIL_ADDRESS) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENDER_EMAIL_ADDRESS;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_SENDER_EMAIL_ADDRESS].Value.LPSZ;
	}
	
	if(lpSrcPropValue[RR_SENT_REPRESENTING_NAME].ulPropTag == PR_SENT_REPRESENTING_NAME) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_NAME;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_SENT_REPRESENTING_NAME].Value.LPSZ;
	}
	
	if(lpSrcPropValue[RR_SENT_REPRESENTING_ADDRTYPE].ulPropTag == PR_SENT_REPRESENTING_ADDRTYPE) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_ADDRTYPE;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_SENT_REPRESENTING_ADDRTYPE].Value.LPSZ;
	}

	if(lpSrcPropValue[RR_SENT_REPRESENTING_ENTRYID].ulPropTag == PR_SENT_REPRESENTING_ENTRYID) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_ENTRYID;
		lpDestPropValue[ulCurDestValues++].Value.bin = lpSrcPropValue[RR_SENT_REPRESENTING_ENTRYID].Value.bin;
	}

	if(lpSrcPropValue[RR_SENT_REPRESENTING_SEARCH_KEY].ulPropTag == PR_SENT_REPRESENTING_SEARCH_KEY) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_SEARCH_KEY;
		lpDestPropValue[ulCurDestValues++].Value.bin = lpSrcPropValue[RR_SENT_REPRESENTING_SEARCH_KEY].Value.bin;
	}

	if(lpSrcPropValue[RR_SENT_REPRESENTING_EMAIL_ADDRESS].ulPropTag == PR_SENT_REPRESENTING_EMAIL_ADDRESS) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_EMAIL_ADDRESS;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_SENT_REPRESENTING_EMAIL_ADDRESS].Value.LPSZ;
	}

	if(lpSrcPropValue[RR_MDN_DISPOSITION_SENDINGMODE].ulPropTag == PR_MDN_DISPOSITION_SENDINGMODE) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_MDN_DISPOSITION_SENDINGMODE;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_MDN_DISPOSITION_SENDINGMODE].Value.LPSZ;
	}

	if(lpSrcPropValue[RR_MDN_DISPOSITION_TYPE].ulPropTag == PR_MDN_DISPOSITION_TYPE) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_MDN_DISPOSITION_TYPE;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_MDN_DISPOSITION_TYPE].Value.LPSZ;
	}

	// We are representing the person who received the email if we're sending the read receipt for someone else.
	if(lpSrcPropValue[RR_RECEIVED_BY_ENTRYID].ulPropTag == PR_RECEIVED_BY_ENTRYID) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_SENT_REPRESENTING_ENTRYID;
		lpDestPropValue[ulCurDestValues++].Value = lpSrcPropValue[RR_RECEIVED_BY_ENTRYID].Value;
	}
	
	if(lpSrcPropValue[RR_RECEIVED_BY_NAME].ulPropTag == PR_RECEIVED_BY_NAME) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_SENT_REPRESENTING_NAME;
		lpDestPropValue[ulCurDestValues++].Value = lpSrcPropValue[RR_RECEIVED_BY_NAME].Value;
	}

	if(lpSrcPropValue[RR_RECEIVED_BY_EMAIL_ADDRESS].ulPropTag == PR_RECEIVED_BY_EMAIL_ADDRESS) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS;
		lpDestPropValue[ulCurDestValues++].Value = lpSrcPropValue[RR_RECEIVED_BY_EMAIL_ADDRESS].Value;
	}
	
	if(lpSrcPropValue[RR_RECEIVED_BY_ADDRTYPE].ulPropTag == PR_RECEIVED_BY_ADDRTYPE) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE;
		lpDestPropValue[ulCurDestValues++].Value = lpSrcPropValue[RR_RECEIVED_BY_ADDRTYPE].Value;
	}

//	PR_RCVD_REPRESENTING_NAME, PR_RCVD_REPRESENTING_ENTRYID	

	if(lpSrcPropValue[RR_INTERNET_MESSAGE_ID].ulPropTag == PR_INTERNET_MESSAGE_ID) {
		lpDestPropValue[ulCurDestValues].ulPropTag = PR_INTERNET_MESSAGE_ID;
		lpDestPropValue[ulCurDestValues++].Value.LPSZ = lpSrcPropValue[RR_INTERNET_MESSAGE_ID].Value.LPSZ;
	}

	hr = (*lppEmptyMessage)->OpenProperty(PR_BODY, &IID_IStream, 0, MAPI_CREATE | MAPI_MODIFY, (IUnknown**)&lpBodyStream);
	if (hr != hrSuccess)
		goto exit;

	hr = lpBodyStream->Write(strBodyText.c_str(), strBodyText.size() * sizeof(TCHAR), NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = lpBodyStream->Commit( 0 );//0 = STGC_DEFAULT
	if (hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(CbNewADRLIST(1), (void**)&lpMods);
	if (hr != hrSuccess)
		goto exit;

	lpMods->cEntries = 1;

	hr = MAPIAllocateBuffer(sizeof(SPropValue) * 8, (void**)&lpMods->aEntries->rgPropVals);
	if (hr != hrSuccess)
		goto exit;

	hr = ECParseOneOff((LPENTRYID)lpSrcPropValue[RR_REPORT_ENTRYID].Value.bin.lpb, lpSrcPropValue[RR_REPORT_ENTRYID].Value.bin.cb, strName, strType, strAddress);
	if (hr != hrSuccess)
		goto exit;

	lpMods->aEntries->rgPropVals[0].ulPropTag = PR_ENTRYID;
	lpMods->aEntries->rgPropVals[0].Value.bin = lpSrcPropValue[RR_REPORT_ENTRYID].Value.bin;
	lpMods->aEntries->rgPropVals[1].ulPropTag = PR_ADDRTYPE_W;
	lpMods->aEntries->rgPropVals[1].Value.lpszW = (WCHAR*)strType.c_str();
	lpMods->aEntries->rgPropVals[2].ulPropTag = PR_DISPLAY_NAME_W;
	lpMods->aEntries->rgPropVals[2].Value.lpszW = (WCHAR*)strName.c_str();
	lpMods->aEntries->rgPropVals[3].ulPropTag = PR_TRANSMITABLE_DISPLAY_NAME_W;
	lpMods->aEntries->rgPropVals[3].Value.lpszW = (WCHAR*)strName.c_str();
	lpMods->aEntries->rgPropVals[4].ulPropTag = PR_SMTP_ADDRESS_W;
	lpMods->aEntries->rgPropVals[4].Value.lpszW = (WCHAR*)strAddress.c_str();
	lpMods->aEntries->rgPropVals[5].ulPropTag = PR_EMAIL_ADDRESS_W;
	lpMods->aEntries->rgPropVals[5].Value.lpszW = (WCHAR*)strAddress.c_str();
	
	hr = HrCreateEmailSearchKey((LPSTR)strType.c_str(), (LPSTR)strAddress.c_str(), &cbTmp, &lpByteTmp);
	if (hr != hrSuccess)
		goto exit;	

	lpMods->aEntries->rgPropVals[6].ulPropTag = PR_SEARCH_KEY;
	lpMods->aEntries->rgPropVals[6].Value.bin.cb = cbTmp;
	lpMods->aEntries->rgPropVals[6].Value.bin.lpb = lpByteTmp;

	lpMods->aEntries->rgPropVals[7].ulPropTag = PR_RECIPIENT_TYPE;
	lpMods->aEntries->rgPropVals[7].Value.ul = MAPI_TO;
	
	lpMods->aEntries->cValues = 8;

	hr = (*lppEmptyMessage)->ModifyRecipients(MODRECIP_ADD, lpMods);
	if (hr != hrSuccess)
		goto exit;

	hr = (*lppEmptyMessage)->SetProps(ulCurDestValues, lpDestPropValue, NULL);
	if (hr != hrSuccess)
		goto exit;

exit:
	if(lpBodyStream)
		lpBodyStream->Release();
	MAPIFreeBuffer(lpDestPropValue);
	MAPIFreeBuffer(lpSrcPropValue);
	MAPIFreeBuffer(lpByteTmp);
	if(lpMods)
		FreePadrlist(lpMods);	

    return hr;
}

HRESULT ClientUtil::GetGlobalProfileProperties(LPMAPISUP lpMAPISup, struct sGlobalProfileProps* lpsProfileProps)
{
	HRESULT			hr = hrSuccess;
	LPPROFSECT		lpGlobalProfSect = NULL;

	hr = lpMAPISup->OpenProfileSection((LPMAPIUID)pbGlobalProfileSectionGuid, MAPI_MODIFY, &lpGlobalProfSect);
	if(hr != hrSuccess)
		goto exit;

	hr = ClientUtil::GetGlobalProfileProperties(lpGlobalProfSect, lpsProfileProps);
	if(hr != hrSuccess)
		goto exit;

exit:
	if(lpGlobalProfSect)
		lpGlobalProfSect->Release();

	return hr;
}

HRESULT ClientUtil::GetGlobalProfileProperties(LPPROFSECT lpGlobalProfSect, struct sGlobalProfileProps* lpsProfileProps)
{
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpsPropArray = NULL;
	ULONG			cValues = 0;
	LPSPropValue	lpsEMSPropArray = NULL;
	LPSPropValue	lpPropEMS = NULL;
	ULONG			cEMSValues = 0;
	LPSPropValue	lpProp = NULL;
	bool			bIsEMS = false;

	if(lpGlobalProfSect == NULL || lpsProfileProps == NULL)
	{
		hr = MAPI_E_INVALID_OBJECT;
		goto exit;
	}

	if(HrGetOneProp(lpGlobalProfSect, PR_PROFILE_UNRESOLVED_NAME, &lpPropEMS) == hrSuccess || g_ulLoadsim) {
		bIsEMS = true;
	}

	if(bIsEMS) {
		SizedSPropTagArray(4, sptaEMSProfile) = {4,{PR_PROFILE_NAME_A, PR_PROFILE_UNRESOLVED_SERVER, PR_PROFILE_UNRESOLVED_NAME, PR_PROFILE_USER}};

		// This is an emulated MSEMS store. Get the properties we need and convert them to ZARAFA-style properties
		hr = lpGlobalProfSect->GetProps((LPSPropTagArray)&sptaEMSProfile, 0, &cEMSValues, &lpsEMSPropArray);
		if(FAILED(hr))
			goto exit;

		hr = ConvertMSEMSProps(cEMSValues, lpsEMSPropArray, &cValues, &lpsPropArray);
		if(FAILED(hr))
			goto exit;
	} else {
		// Get the properties we need directly from the global profile section
		hr = lpGlobalProfSect->GetProps((LPSPropTagArray)&sptaKopanoProfile, 0, &cValues, &lpsPropArray);
		if(FAILED(hr))
			goto exit;
	}

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_PATH)) != NULL)
		lpsProfileProps->strServerPath = lpProp->Value.lpszA;

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_PROFILE_NAME_A)) != NULL)
		lpsProfileProps->strProfileName = lpProp->Value.lpszA;

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_USERNAME_W)) != NULL)
		lpsProfileProps->strUserName = convstring::from_SPropValue(lpProp);
	else if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_USERNAME_A)) != NULL)
		lpsProfileProps->strUserName = convstring::from_SPropValue(lpProp);

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_USERPASSWORD_W)) != NULL)
		lpsProfileProps->strPassword = convstring::from_SPropValue(lpProp);
	else if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_USERPASSWORD_A)) != NULL)
		lpsProfileProps->strPassword = convstring::from_SPropValue(lpProp);

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_IMPERSONATEUSER_W)) != NULL)
		lpsProfileProps->strImpersonateUser = convstring::from_SPropValue(lpProp);
	else if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_IMPERSONATEUSER_A)) != NULL)
		lpsProfileProps->strImpersonateUser = convstring::from_SPropValue(lpProp);

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_FLAGS)) != NULL)
		lpsProfileProps->ulProfileFlags = lpProp->Value.ul;
	else
		lpsProfileProps->ulProfileFlags = 0;

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_SSLKEY_FILE)) != NULL)
		lpsProfileProps->strSSLKeyFile = lpProp->Value.lpszA;

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_SSLKEY_PASS)) != NULL)
		lpsProfileProps->strSSLKeyPass = lpProp->Value.lpszA;

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_HOST)) != NULL)
		lpsProfileProps->strProxyHost = lpProp->Value.lpszA;
	
	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_PORT)) != NULL)
		lpsProfileProps->ulProxyPort = lpProp->Value.ul;
	else
		lpsProfileProps->ulProxyPort = 0;

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_FLAGS)) != NULL)
		lpsProfileProps->ulProxyFlags = lpProp->Value.ul;
	else
		lpsProfileProps->ulProxyFlags = 0;

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_USERNAME)) != NULL)
		lpsProfileProps->strProxyUserName = lpProp->Value.lpszA;
	
	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_PASSWORD)) != NULL)
		lpsProfileProps->strProxyPassword = lpProp->Value.lpszA;

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_CONNECTION_TIMEOUT)) != NULL)
		lpsProfileProps->ulConnectionTimeOut = lpProp->Value.ul;
	else
		lpsProfileProps->ulConnectionTimeOut = 10;

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_OFFLINE_PATH_W)) != NULL)
		lpsProfileProps->strOfflinePath = convstring::from_SPropValue(lpProp);
	else if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_OFFLINE_PATH_A)) != NULL)
		lpsProfileProps->strOfflinePath = convstring::from_SPropValue(lpProp);

	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION)) != NULL)
		lpsProfileProps->strClientAppVersion = lpProp->Value.lpszA;
	if((lpProp = PpropFindProp(lpsPropArray, cValues, PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC)) != NULL)
		lpsProfileProps->strClientAppMisc = lpProp->Value.lpszA;

	lpsProfileProps->bIsEMS = bIsEMS;

	hr = hrSuccess;

exit:
	MAPIFreeBuffer(lpPropEMS);
	MAPIFreeBuffer(lpsPropArray);
	MAPIFreeBuffer(lpsEMSPropArray);
	return hr;
}

HRESULT ClientUtil::GetGlobalProfileDelegateStoresProp(LPPROFSECT lpGlobalProfSect, ULONG *lpcDelegates, LPBYTE *lppDelegateStores)
{
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpsPropValue = NULL;
	ULONG			cValues = 0;
	SPropTagArray	sPropTagArray;
	LPBYTE			lpDelegateStores = NULL;

	if(lpGlobalProfSect == NULL || lpcDelegates == NULL || lppDelegateStores == NULL)
	{
		hr = MAPI_E_INVALID_OBJECT;
		goto exit;
	}
	
	sPropTagArray.cValues = 1;
	sPropTagArray.aulPropTag[0] =  PR_STORE_PROVIDERS;

	hr = lpGlobalProfSect->GetProps(&sPropTagArray, 0, &cValues, &lpsPropValue);
	if(hr != hrSuccess)
		goto exit;

	if(lpsPropValue[0].Value.bin.cb > 0){
		hr = MAPIAllocateBuffer(lpsPropValue[0].Value.bin.cb, (void**)&lpDelegateStores);
		if(hr != hrSuccess)
			goto exit;

		memcpy(lpDelegateStores, lpsPropValue[0].Value.bin.lpb, lpsPropValue[0].Value.bin.cb);
	}

	*lpcDelegates = lpsPropValue[0].Value.bin.cb;
	*lppDelegateStores = lpDelegateStores;

	hr = hrSuccess;

exit:
	MAPIFreeBuffer(lpsPropValue);
	return hr;
}

/*
 * Read registry key to discover the installation directory for the exchange redirector
 *
 * @param[out] lpConfigPath String containing full config path
 */
HRESULT ClientUtil::GetConfigPath(std::string *lpConfigPath)
{
	return MAPI_E_NO_SUPPORT;
}

/**
 * Convert incoming MSEMS profile properties to ZARAFA properties
 *
 * Basically we take the username and servername from the exchange properties and set all other properties
 * by reading a configuration file.
 *
 * @param cValues[in] Number of props in pValues
 * @param pValues[in] Incoming exchange properties (must contain PR_PROFILE_UNRESOLVED_{USER,SERVER})
 * @param lpcValues[out] Number of properties in lppProps
 * @param lppProps[out] New ZARAFA properties
 */
HRESULT ClientUtil::ConvertMSEMSProps(ULONG cValues, LPSPropValue pValues, ULONG *lpcValues, LPSPropValue *lppProps)
{
	HRESULT hr = hrSuccess;
	LPSPropValue lpProps = NULL;
	char *szUsername;
	std::string strServerPath;
	std::wstring strUsername;
	ULONG cProps = 0;
	LPSPropValue lpServer = NULL;
	LPSPropValue lpUsername = NULL;
	LPSPropValue lpProfileName = NULL;
	static const configsetting_t settings[] = {
		{ "ssl_port", "237" },
		{ "ssl_key_file", "c:\\program files\\kopano\\exchange-redirector.pem" },
		{ "ssl_key_pass", "kopano" },
		{ "server_address", "" },
		{ "log_method","file" },
		{ "log_file","-" },
		{ "log_level", "3", CONFIGSETTING_RELOADABLE },
		{ "log_timestamp","1" },
		{ "log_buffer_size", "0" },
		{ NULL, NULL },
	};
	ECConfig *lpConfig = ECConfig::Create(settings);
	std::string strConfigPath;

	hr = GetConfigPath(&strConfigPath);
	if(hr != hrSuccess) {
		TRACE_RELEASE("Unable to find config file (registry key missing)", (char *)strConfigPath.c_str());
		goto exit;
	}

	// Remove trailing slash
	if(*(strConfigPath.end()-1) == '\\' )
		strConfigPath.resize(strConfigPath.size()-1);

	strConfigPath += "\\exchange-redirector.cfg";

	TRACE_RELEASE("Using config file '%s'", (char *)strConfigPath.c_str());

	if(!lpConfig->LoadSettings((char *)strConfigPath.c_str())) {
		hr = MAPI_E_NOT_FOUND;
		TRACE_RELEASE("Unable to load config file '%s'", (char *)strConfigPath.c_str());
		goto exit;
	}

	if(g_ulLoadsim) {
		lpUsername = PpropFindProp(pValues, cValues, PR_PROFILE_USER);
		if(!lpUsername) {
			TRACE_RELEASE("PR_PROFILE_USER not set");
			hr = MAPI_E_UNCONFIGURED;
			goto exit;
		}
	} else {
		lpUsername = PpropFindProp(pValues, cValues, PR_PROFILE_UNRESOLVED_NAME);
		lpServer = PpropFindProp(pValues, cValues, PR_PROFILE_UNRESOLVED_SERVER);

		if(!lpServer || !lpUsername) {
			TRACE_RELEASE("PR_PROFILE_UNRESOLVED_NAME or PR_PROFILE_UNRESOLVED_SERVER not set");
			hr = MAPI_E_UNCONFIGURED;
			goto exit;
		}
	}

	hr = MAPIAllocateBuffer(sizeof(SPropValue) * 7, (LPVOID *)&lpProps);
	if (hr != hrSuccess)
		goto exit;

	if (lpConfig->GetSetting("server_address")[0]) {
		strServerPath = (std::string)"https://" + lpConfig->GetSetting("server_address") + ":" + lpConfig->GetSetting("ssl_port") + "/";
	} else {
		if(!lpServer) {
			hr = MAPI_E_UNCONFIGURED;
			goto exit;
		}
		strServerPath = (std::string)"https://" + lpServer->Value.lpszA + ":" + lpConfig->GetSetting("ssl_port") + "/";
	}

	szUsername = lpUsername->Value.lpszA;

	if(strrchr(szUsername, '='))
		szUsername = strrchr(szUsername, '=')+1;

	lpProps[cProps].ulPropTag = PR_EC_PATH;
	if ((hr = MAPIAllocateMore(strServerPath.size() + 1, lpProps, (void**)&lpProps[cProps].Value.lpszA)) != hrSuccess)
		goto exit;
	strcpy(lpProps[cProps++].Value.lpszA, strServerPath.c_str());

	strUsername = convert_to<std::wstring>(szUsername);
	lpProps[cProps].ulPropTag = PR_EC_USERNAME;
	if ((hr = MAPIAllocateMore((strUsername.size() + 1) * sizeof(TCHAR), lpProps, (void**)&lpProps[cProps].Value.lpszW)) != hrSuccess)
		goto exit;
	wcscpy(lpProps[cProps++].Value.lpszW, strUsername.c_str());

	lpProps[cProps].ulPropTag = PR_EC_USERPASSWORD;
	if ((hr = MAPIAllocateMore(sizeof(TCHAR), lpProps, (void**)&lpProps[cProps].Value.LPSZ)) != hrSuccess)
		goto exit;
	_tcscpy(lpProps[cProps++].Value.LPSZ, L"");

	lpProps[cProps].ulPropTag = PR_EC_SSLKEY_FILE;
	if ((hr = MAPIAllocateMore(strlen(lpConfig->GetSetting("ssl_key_file")) + 1, lpProps, (void**)&lpProps[cProps].Value.lpszA)) != hrSuccess)
		goto exit;
	strcpy(lpProps[cProps++].Value.lpszA, lpConfig->GetSetting("ssl_key_file"));

	lpProps[cProps].ulPropTag = PR_EC_SSLKEY_PASS;
	if ((hr = MAPIAllocateMore(strlen(lpConfig->GetSetting("ssl_key_pass")) + 1, lpProps, (void**)&lpProps[cProps].Value.lpszA)) != hrSuccess)
		goto exit;
	strcpy(lpProps[cProps++].Value.lpszA, lpConfig->GetSetting("ssl_key_pass"));

	lpProps[cProps].ulPropTag = PR_EC_FLAGS; // Since we're emulating exchange, use 22-byte exchange-style sourcekeys
	lpProps[cProps++].Value.ul = EC_PROFILE_FLAGS_TRUNCATE_SOURCEKEY;

	lpProfileName = PpropFindProp(pValues, cValues, PR_PROFILE_NAME_A);
	if(lpProfileName) {
		lpProps[cProps].ulPropTag = PR_PROFILE_NAME_A;
		if ((hr = MAPIAllocateMore(strlen(lpProfileName->Value.lpszA) + 1, lpProps, (void**)&lpProps[cProps].Value.lpszA)) != hrSuccess)
			goto exit;
		strcpy(lpProps[cProps++].Value.lpszA, lpProfileName->Value.lpszA);
	}
	
	TRACE_RELEASE("Redirecting to %s", (char *)strServerPath.c_str());

	*lpcValues = cProps;
	*lppProps = lpProps;

exit:
	if (hr != hrSuccess)
		MAPIFreeBuffer(lpProps);
	delete lpConfig;
	return hr;
}

/* 
entryid functions

*/

HRESULT HrCreateEntryId(GUID guidStore, unsigned int ulObjType, ULONG* lpcbEntryId, LPENTRYID* lppEntryId)
{
	HRESULT		hr;
	EID			eid;
	ULONG		cbEntryId = 0;
	LPENTRYID	lpEntryId = NULL;

	if (lpcbEntryId == NULL || lppEntryId == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (CoCreateGuid(&eid.uniqueId) != hrSuccess)
		return MAPI_E_CALL_FAILED;

	cbEntryId = CbNewEID("");

	hr = ECAllocateBuffer(cbEntryId, (void**)&lpEntryId); 
	if(hr != hrSuccess)
		return hr;

	eid.guid = guidStore;
	eid.usType = ulObjType;

	memcpy(lpEntryId, &eid, cbEntryId);

	*lpcbEntryId = cbEntryId;
	*lppEntryId = lpEntryId;
	return hrSuccess;
}

/**
 * Extract the server URL from a store entryid.
 * @param[in]	cbEntryId			The length of the entryid pointed to by
						lpEntryId
 * @param[in]	lpEntryId			Pointer to the store entryid.
 * @param[out]	rServerPath			Reference to a std::string that will be
 *						set to the server path extracted from
 *						the entry id.
 * @param[out]	lpbIsPseudoUrl			Pointer to a boolean that will be set to
 *						true if the extracted server path is a
 *						pseudo URL.
 * @retval	MAPI_E_INVALID_PARAMETER	lpEntryId or lpbIsPseudoUrl is NULL
 * @retval	MAPI_E_NOT_FOUND		The extracted server path does not start
 *						with http://, https://, file:// or pseudo://
 */
HRESULT HrGetServerURLFromStoreEntryId(ULONG cbEntryId, LPENTRYID lpEntryId, std::string& rServerPath, bool *lpbIsPseudoUrl)
{
	PEID	peid = (PEID)lpEntryId;
	EID_V0*	peid_V0 = NULL;

	ULONG	ulMaxSize = 0;
	ULONG	ulSize = 0;
	char*	lpTmpServerName = NULL;
	bool	bIsPseudoUrl = false;

	if (lpEntryId == NULL || lpbIsPseudoUrl == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (peid->ulVersion == 0) 
	{
		peid_V0 = (EID_V0*)lpEntryId;

		ulMaxSize = cbEntryId - offsetof(EID_V0, szServer);
		ulSize = strnlen((char*)peid_V0->szServer, ulMaxSize);
		lpTmpServerName = (char*)peid_V0->szServer;
	} else {
		ulMaxSize = cbEntryId - offsetof(EID, szServer);
		ulSize = strnlen((char*)peid->szServer, ulMaxSize);
		lpTmpServerName = (char*)peid->szServer;
	}

	if (ulSize >= ulMaxSize)
		return MAPI_E_NOT_FOUND;
	if (strncasecmp(lpTmpServerName, "pseudo://", 9) == 0)
		bIsPseudoUrl = true;
	else if (strncasecmp(lpTmpServerName, "http://", 7) && 
			 strncasecmp(lpTmpServerName, "https://", 8) && 
			 strncasecmp(lpTmpServerName, "file://", 7))
		return MAPI_E_NOT_FOUND;

	rServerPath = lpTmpServerName;
	*lpbIsPseudoUrl = bIsPseudoUrl;
	return hrSuccess;
}

/**
 * Resolve a pseudoURL
 * @param[in]	lpTransport			Pointer to a WebServices transport object
 * @param[in]	lpszUrl				C string containing pseudoURL.
 * @param[out]	serverPath			Reference to a std::string that will be
 *						set to the iresolved server path.
 * @param[out]	lpbIsPeer			Pointer to a boolean that will be set to
 *						true the server is a peer.
 * @retval	MAPI_E_INVALID_PARAMETER	lpTransport or lpszUrl are NULL
 * @retval	MAPI_E_NOT_FOUND		The extracted server path does not start
 *						with pseudo://
 */
HRESULT HrResolvePseudoUrl(WSTransport *lpTransport, const char *lpszUrl, std::string& serverPath, bool *lpbIsPeer)
{
	HRESULT		hr = hrSuccess;
	char		*lpszServerPath = NULL;
	bool		bIsPeer = false;

	if (lpTransport == NULL || lpszUrl == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (strncmp(lpszUrl, "pseudo://", 9))
	{
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	hr = lpTransport->HrResolvePseudoUrl(lpszUrl, &lpszServerPath, &bIsPeer);
	if (hr != hrSuccess)
		goto exit;

	serverPath = lpszServerPath;
	if (lpbIsPeer)
		*lpbIsPeer = bIsPeer;

exit:
	if (lpszServerPath)
		ECFreeBuffer(lpszServerPath);

	return hr;
}

HRESULT HrCompareEntryIdWithStoreGuid(ULONG cbEntryID, LPENTRYID lpEntryID, LPCGUID guidStore)
{
	if (lpEntryID == NULL || guidStore == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (cbEntryID < 20)
		return MAPI_E_INVALID_ENTRYID;
	if (memcmp(lpEntryID->ab, guidStore, sizeof(GUID)) != 0)
		return MAPI_E_INVALID_ENTRYID;
	return hrSuccess;
}

HRESULT GetPublicEntryId(enumPublicEntryID ePublicEntryID, GUID guidStore, void *lpBase, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	HRESULT hr = hrSuccess;
	ULONG cbEntryID = 0;
	LPENTRYID lpEntryID = NULL;

	GUID guidEmpty = {0};
	EID eid = EID(MAPI_FOLDER, guidStore, guidEmpty);

	switch (ePublicEntryID) {
		case ePE_IPMSubtree:
			eid.uniqueId.Data4[7] = 1;
			break;
		case ePE_Favorites:
			eid.uniqueId.Data4[7] = 2;
			break;
		case ePE_PublicFolders:
			eid.uniqueId.Data4[7] = 3;
			break;
		default:
			return MAPI_E_INVALID_PARAMETER;
	}

	if (lpcbEntryID == NULL || lppEntryID == NULL)
		return MAPI_E_INVALID_PARAMETER;

	cbEntryID = CbEID(&eid);

	if (lpBase)
		hr = MAPIAllocateMore(cbEntryID, lpBase, (void**)&lpEntryID);
	else
		hr = MAPIAllocateBuffer(cbEntryID, (void**)&lpEntryID);
	if (hr != hrSuccess)
		return hr;

	memcpy(lpEntryID, &eid, cbEntryID);

	*lpcbEntryID = cbEntryID;
	*lppEntryID = lpEntryID;
	return hrSuccess;
}

BOOL CompareMDBProvider(LPBYTE lpguid, const GUID *lpguidKopano) {
	return CompareMDBProvider((MAPIUID*)lpguid, lpguidKopano);
}

BOOL CompareMDBProvider(MAPIUID* lpguid, const GUID *lpguidKopano)
{
	if (memcmp(lpguid, lpguidKopano, sizeof(GUID)) == 0)
		return TRUE;
	return FALSE;
}
