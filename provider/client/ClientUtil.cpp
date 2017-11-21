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
#include <memory>
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
#include <kopano/memory.hpp>
#include "Mem.h"
#include <kopano/stringutil.h>

#include <kopano/charset/convstring.h>
#include "EntryPoint.h"

using namespace KCHL;

// profile properties
static constexpr const SizedSPropTagArray(22, sptaKopanoProfile) =
	{22, {PR_EC_PATH, PR_PROFILE_NAME_A, PR_EC_USERNAME_A,
	PR_EC_USERNAME_W, PR_EC_USERPASSWORD_A, PR_EC_USERPASSWORD_W,
	PR_EC_IMPERSONATEUSER_A, PR_EC_IMPERSONATEUSER_W, PR_EC_FLAGS,
	PR_EC_SSLKEY_FILE, PR_EC_SSLKEY_PASS, PR_EC_PROXY_HOST,
	PR_EC_PROXY_PORT, PR_EC_PROXY_USERNAME, PR_EC_PROXY_PASSWORD,
	PR_EC_PROXY_FLAGS, PR_EC_CONNECTION_TIMEOUT, PR_EC_OFFLINE_PATH_A,
	PR_EC_OFFLINE_PATH_W, PR_SERVICE_NAME,
	PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION,
	PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC}};

HRESULT ClientUtil::HrInitializeStatusRow (const char * lpszProviderDisplay, ULONG ulResourceType, LPMAPISUP lpMAPISup, LPSPropValue lpspvIdentity, ULONG ulFlags)
{
	HRESULT			hResult = hrSuccess;
	memory_ptr<SPropValue> lpspvStatusRow;
	ULONG			cCurVal = 0;
	unsigned int	size = 0;
	std::wstring	wstrSearchKey;

	hResult = MAPIAllocateBuffer(sizeof(SPropValue) * 13, &~lpspvStatusRow);
	if(hResult != hrSuccess)
		return hResult;

	memset(lpspvStatusRow, 0, sizeof(SPropValue) * 13);

	if(lpszProviderDisplay)
	{
		size = strlen(lpszProviderDisplay)+1;

		// Set the PR_PROVIDER_DISPLAY property:
		lpspvStatusRow[cCurVal].ulPropTag = PR_PROVIDER_DISPLAY_A;
		hResult = MAPIAllocateMore(size, lpspvStatusRow, (void**)&lpspvStatusRow[cCurVal].Value.lpszA);
		if(hResult != hrSuccess)
			return hResult;
		memcpy(lpspvStatusRow[cCurVal].Value.lpszA, lpszProviderDisplay, size);
		++cCurVal;

	// Set the PR_DISPLAY_NAME property
		lpspvStatusRow[cCurVal].ulPropTag = PR_DISPLAY_NAME_A;
		hResult = MAPIAllocateMore(size, lpspvStatusRow, (void**)&lpspvStatusRow[cCurVal].Value.lpszA);
		if(hResult != hrSuccess)
			return hResult;
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
	lpspvStatusRow[cCurVal++].Value.lpszW = KC_W("Available");

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

	return lpMAPISup->ModifyStatusRow(cCurVal, lpspvStatusRow, ulFlags);
}

HRESULT ClientUtil::HrSetIdentity(WSTransport *lpTransport, LPMAPISUP lpMAPISup, LPSPropValue* lppIdentityProps)
{
	HRESULT			hr = hrSuccess;
	ULONG			cbEntryStore = 0;
	memory_ptr<ENTRYID> lpEntryStore, lpEID;
	ULONG			cbEID = 0;
	ULONG			cValues = 0;
	ULONG			ulSize = 0;
	memory_ptr<ECUSER> lpUser;
	tstring			strProfileSenderSearchKey;
	memory_ptr<SPropValue> lpIdentityProps;

	// Get the username and email adress
	hr = lpTransport->HrGetUser(0, NULL, fMapiUnicode, &~lpUser);
	if(hr != hrSuccess)
		return hr;
	cValues = NUM_IDENTITY_PROPS;
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * cValues, &~lpIdentityProps);
	if (hr != hrSuccess)
		return hr;
	memset(lpIdentityProps, 0, sizeof(SPropValue) * cValues);

	strProfileSenderSearchKey = strToUpper(tstring(TRANSPORT_ADDRESS_TYPE_ZARAFA) + KC_T(":") + lpUser->lpszMailAddress);
	lpIdentityProps[XPID_EID].ulPropTag = PR_SENDER_ENTRYID;
	lpIdentityProps[XPID_EID].Value.bin.cb = lpUser->sUserId.cb;
	hr = MAPIAllocateMore(lpUser->sUserId.cb, (LPVOID)lpIdentityProps, (void**)&lpIdentityProps[XPID_EID].Value.bin.lpb);
	if (hr != hrSuccess)
		return hr;
	memcpy(lpIdentityProps[XPID_EID].Value.bin.lpb, lpUser->sUserId.lpb, lpUser->sUserId.cb);

	// Create the PR_SENDER_NAME property value.
	lpIdentityProps[XPID_NAME].ulPropTag = PR_SENDER_NAME;
	ulSize = sizeof(TCHAR) * (_tcslen(lpUser->lpszFullName) + 1);
	hr = MAPIAllocateMore(ulSize, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_NAME].Value.LPSZ);
	if (hr != hrSuccess)
		return hr;
	memcpy(lpIdentityProps[XPID_NAME].Value.LPSZ, lpUser->lpszFullName, ulSize);

	// Create the PR_SENDER_SEARCH_KEY value. 
	lpIdentityProps[XPID_SEARCH_KEY].ulPropTag = PR_SENDER_SEARCH_KEY;
	lpIdentityProps[XPID_SEARCH_KEY].Value.bin.cb = strProfileSenderSearchKey.size()+1;
	hr = MAPIAllocateMore(lpIdentityProps[XPID_SEARCH_KEY].Value.bin.cb, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_SEARCH_KEY].Value.bin.lpb);
	if (hr != hrSuccess)
		return hr;
	memcpy(lpIdentityProps[XPID_SEARCH_KEY].Value.bin.lpb, strProfileSenderSearchKey.c_str(), lpIdentityProps[XPID_SEARCH_KEY].Value.bin.cb);

	// PR_SENDER_EMAIL_ADDRESS
	lpIdentityProps[XPID_ADDRESS].ulPropTag = PR_SENDER_EMAIL_ADDRESS;
	ulSize = sizeof(TCHAR) * (_tcslen(lpUser->lpszMailAddress) + 1);
	hr = MAPIAllocateMore(ulSize, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_ADDRESS].Value.LPSZ);
	if (hr != hrSuccess)
		return hr;
	memcpy(lpIdentityProps[XPID_ADDRESS].Value.LPSZ, lpUser->lpszMailAddress, ulSize);

	// PR_SENDER_ADDRTYPE
	lpIdentityProps[XPID_ADDRTYPE].ulPropTag = PR_SENDER_ADDRTYPE;
	ulSize = sizeof(TCHAR) * (_tcslen(TRANSPORT_ADDRESS_TYPE_ZARAFA) + 1);
	hr = MAPIAllocateMore(ulSize, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_ADDRTYPE].Value.LPSZ);
	if (hr != hrSuccess)
		return hr;
	memcpy(lpIdentityProps[XPID_ADDRTYPE].Value.LPSZ, TRANSPORT_ADDRESS_TYPE_ZARAFA, ulSize);

	//PR_OWN_STORE_ENTRYID
	// Get the default store for this user, not an issue if it fails when not on home server
	if (lpTransport->HrGetStore(0, nullptr, &cbEntryStore, &~lpEntryStore, 0, nullptr) == hrSuccess) {
		hr = lpMAPISup->WrapStoreEntryID(cbEntryStore, lpEntryStore, &cbEID, (&~lpEID).as<ENTRYID>());
		if(hr != hrSuccess) 
			return hr;
		lpIdentityProps[XPID_STORE_EID].ulPropTag = PR_OWN_STORE_ENTRYID;
		lpIdentityProps[XPID_STORE_EID].Value.bin.cb = cbEID;
		hr = MAPIAllocateMore(cbEID, (LPVOID)lpIdentityProps , (void**)&lpIdentityProps[XPID_STORE_EID].Value.bin.lpb);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpIdentityProps[XPID_STORE_EID].Value.bin.lpb, lpEID.get(), cbEID);
	}
	
	// Set the identity in the global provider identity
	*lppIdentityProps = lpIdentityProps.release();
	return hrSuccess;
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
	memory_ptr<SPropValue> spv, dpv;
	ULONG			ulMaxDestValues = 0;
	ULONG			dval = 0;
	ULONG			cSrcValues = 0;
	ULONG			cbTmp = 0;
	memory_ptr<BYTE> lpByteTmp;
	const TCHAR *lpMsgClass = NULL;
	LPTSTR			lpReportText = NULL;
	LPTSTR			lpReadText = NULL;
	FILETIME		ft;	
	adrlist_ptr lpMods;
	std::wstring	strName;
	std::wstring	strType;
	std::wstring	strAddress;
	tstring			strBodyText;
	time_t			zero = 0;
	time_t			tt;
	struct tm*		tm;
	char			szTime[255];
	object_ptr<IStream> lpBodyStream;
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

	static constexpr const SizedSPropTagArray(39, sPropReadReceipt) =
		{39, { PR_REPORT_TAG, PR_CONVERSATION_TOPIC,
		PR_CONVERSATION_INDEX, PR_SEARCH_KEY, PR_MESSAGE_CLASS,
		PR_SENDER_SEARCH_KEY, PR_SUBJECT, PR_SUBJECT_PREFIX,
		PR_NORMALIZED_SUBJECT, PR_SENDER_NAME, PR_SENDER_ENTRYID,
		PR_SENDER_ADDRTYPE, PR_SENDER_EMAIL_ADDRESS, PR_REPORT_NAME,
		PR_REPORT_ENTRYID, PR_READ_RECEIPT_ENTRYID, PR_RECEIVED_BY_NAME,
		PR_RECEIVED_BY_ENTRYID, PR_RECEIVED_BY_ADDRTYPE,
		PR_RECEIVED_BY_EMAIL_ADDRESS, PR_PRIORITY, PR_IMPORTANCE,
		PR_SENT_REPRESENTING_NAME, PR_SENT_REPRESENTING_ENTRYID,
		PR_SENT_REPRESENTING_SEARCH_KEY, PR_RCVD_REPRESENTING_NAME,
		PR_RCVD_REPRESENTING_ENTRYID, PR_MESSAGE_DELIVERY_TIME,
		PR_CLIENT_SUBMIT_TIME, PR_DISPLAY_TO, PR_DISPLAY_CC,
		PR_DISPLAY_BCC, PR_SENSITIVITY, PR_INTERNET_MESSAGE_ID,
		PR_DELIVER_TIME, PR_SENT_REPRESENTING_ADDRTYPE,
		PR_SENT_REPRESENTING_EMAIL_ADDRESS, PR_MDN_DISPOSITION_TYPE,
		PR_MDN_DISPOSITION_SENDINGMODE}};

	// Check incoming parameters
	if (lpReadMessage == nullptr || lppEmptyMessage == nullptr ||
	    *lppEmptyMessage == nullptr)
		return MAPI_E_INVALID_OBJECT;
	if ((ulFlags &~ MAPI_NON_READ) != 0)
		return MAPI_E_INVALID_PARAMETER;

	GetSystemTimeAsFileTime(&ft);

	if (ulFlags & MAPI_NON_READ) {
		lpMsgClass = KC_T("REPORT.IPM.Note.IPNNRN");
		lpReadText = _("Not read:");
		lpReportText = _("was not read because it expired before reading at time");
	}else{
		lpMsgClass = KC_T("REPORT.IPM.Note.IPNRN");
		lpReadText = _("Read:");
		lpReportText = _("was read on");
	}

	hr = lpReadMessage->GetProps(sPropReadReceipt, fMapiUnicode, &cSrcValues, &~spv);
	if(FAILED(hr) != hrSuccess)
		return hr;

#define HAVE(tag) (spv[RR_ ## tag].ulPropTag == (PR_ ## tag))
	// important properties
	if (!HAVE(REPORT_ENTRYID))
		return MAPI_E_INVALID_PARAMETER;

	strBodyText = _("Your message");
	strBodyText += KC_T("\r\n\r\n");

	if (HAVE(DISPLAY_TO)) {
		strBodyText += KC_T("\t");
		strBodyText+= _("To:");
		strBodyText += KC_T(" ");
		strBodyText += spv[RR_DISPLAY_TO].Value.LPSZ;
		strBodyText += KC_T("\r\n");
	}
	if (HAVE(DISPLAY_CC)) {
		strBodyText += KC_T("\t");
		strBodyText+= _("Cc:");
		strBodyText += KC_T(" ");
		strBodyText += spv[RR_DISPLAY_CC].Value.LPSZ;
		strBodyText += KC_T("\r\n");
	}
	if (HAVE(SUBJECT)) {
		strBodyText += KC_T("\t");
		strBodyText+= _("Subject:");
		strBodyText += KC_T(" ");
		strBodyText += spv[RR_SUBJECT].Value.LPSZ;
		strBodyText += KC_T("\r\n");
	}
	if (HAVE(CLIENT_SUBMIT_TIME)) {
		strBodyText += KC_T("\t");
		strBodyText+= _("Sent on:");
		strBodyText += KC_T(" ");
		FileTimeToUnixTime(spv[RR_CLIENT_SUBMIT_TIME].Value.ft, &tt);
		tm = localtime(&tt);
		if (tm == NULL)
			tm = localtime(&zero);
		strftime(szTime, 255, "%c", tm);

		strBodyText+= convert_to<tstring>(szTime, strlen(szTime), CHARSET_CHAR);
		strBodyText += KC_T("\r\n");
	}

	strBodyText += KC_T("\r\n");
	strBodyText+= lpReportText;
	strBodyText += KC_T(" ");
	
	FileTimeToUnixTime(ft, &tt);
	tm = localtime(&tt);
	if (tm == NULL)
		tm = localtime(&zero);
	strftime(szTime, 255, "%c", tm);

	strBodyText+= convert_to<tstring>(szTime, strlen(szTime), CHARSET_CHAR);
	strBodyText += KC_T("\r\n");
	ulMaxDestValues = cSrcValues + 4;//+ default properties
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * ulMaxDestValues, &~dpv);
	if(hr != hrSuccess)
		return hr;

	memset(dpv, 0, sizeof(SPropValue) * ulMaxDestValues);

	// Default properties
	dpv[dval].ulPropTag = PR_DELETE_AFTER_SUBMIT;
	dpv[dval++].Value.b = true;
	dpv[dval].ulPropTag = PR_READ_RECEIPT_REQUESTED;
	dpv[dval++].Value.b = false;
	dpv[dval].ulPropTag = PR_MESSAGE_FLAGS;
	dpv[dval++].Value.ul = 0;
	dpv[dval].ulPropTag = PR_MESSAGE_CLASS;
	dpv[dval++].Value.LPSZ = const_cast<TCHAR *>(lpMsgClass);
	dpv[dval].ulPropTag = PR_REPORT_TEXT;
	dpv[dval++].Value.LPSZ = lpReportText;
	dpv[dval].ulPropTag = PR_REPORT_TIME;
	dpv[dval++].Value.ft = ft;
	dpv[dval].ulPropTag = PR_SUBJECT_PREFIX;
	dpv[dval++].Value.LPSZ = lpReadText;

	if (HAVE(SUBJECT)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SUBJECT;
		dpv[dval++].Value.LPSZ = spv[RR_SUBJECT].Value.LPSZ;
		tSubject = tstring(lpReadText) + KC_T(" ") + spv[RR_SUBJECT].Value.LPSZ;
		dpv[dval].ulPropTag = PR_SUBJECT;
		dpv[dval++].Value.LPSZ = const_cast<TCHAR *>(tSubject.c_str());
	}else {
		dpv[dval].ulPropTag = PR_SUBJECT;
		dpv[dval++].Value.LPSZ = lpReadText;
	}

	if (HAVE(REPORT_TAG)) {
		dpv[dval].ulPropTag = PR_REPORT_TAG;
		dpv[dval++].Value.bin = spv[RR_REPORT_TAG].Value.bin;
	}
	if (HAVE(DISPLAY_TO)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_DISPLAY_TO;
		dpv[dval++].Value.LPSZ = spv[RR_DISPLAY_TO].Value.LPSZ;
	}
	if (HAVE(DISPLAY_CC)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_DISPLAY_CC;
		dpv[dval++].Value.LPSZ = spv[RR_DISPLAY_CC].Value.LPSZ;
	}
	if (HAVE(DISPLAY_BCC)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_DISPLAY_BCC;
		dpv[dval++].Value.LPSZ = spv[RR_DISPLAY_BCC].Value.LPSZ;
	}
	if (HAVE(CLIENT_SUBMIT_TIME)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SUBMIT_TIME;
		dpv[dval++].Value.ft = spv[RR_CLIENT_SUBMIT_TIME].Value.ft;
	}
	if (HAVE(DELIVER_TIME)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_DELIVERY_TIME;
		dpv[dval++].Value.ft = spv[RR_DELIVER_TIME].Value.ft;
	}	
	if (HAVE(CONVERSATION_TOPIC)) {
		dpv[dval].ulPropTag = PR_CONVERSATION_TOPIC;
		dpv[dval++].Value.LPSZ = spv[RR_CONVERSATION_TOPIC].Value.LPSZ;
	}
	if (HAVE(CONVERSATION_INDEX) &&
	    ScCreateConversationIndex(spv[RR_CONVERSATION_INDEX].Value.bin.cb, spv[RR_CONVERSATION_INDEX].Value.bin.lpb, &cbTmp, &~lpByteTmp) == hrSuccess)
	{
		hr = MAPIAllocateMore(cbTmp, dpv, reinterpret_cast<void **>(&dpv[dval].Value.bin.lpb));
		if(hr != hrSuccess)
			return hr;
		dpv[dval].Value.bin.cb = cbTmp;
		memcpy(dpv[dval].Value.bin.lpb, lpByteTmp, cbTmp);
		dpv[dval++].ulPropTag = PR_CONVERSATION_INDEX;
	}
	if (HAVE(IMPORTANCE)) {
		dpv[dval].ulPropTag = PR_IMPORTANCE;
		dpv[dval++].Value.ul = spv[RR_IMPORTANCE].Value.ul;
	}
	if (HAVE(PRIORITY)) {
		dpv[dval].ulPropTag = PR_PRIORITY;
		dpv[dval++].Value.ul = spv[RR_PRIORITY].Value.ul;
	}
	if (HAVE(SENDER_NAME)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENDER_NAME;
		dpv[dval++].Value.LPSZ = spv[RR_SENDER_NAME].Value.LPSZ;
	}
	if (HAVE(SENDER_ADDRTYPE)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENDER_ADDRTYPE;
		dpv[dval++].Value.LPSZ = spv[RR_SENDER_ADDRTYPE].Value.LPSZ;
	}
	if (HAVE(SENDER_ENTRYID)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENDER_ENTRYID;
		dpv[dval++].Value.bin = spv[RR_SENDER_ENTRYID].Value.bin;
	}
	if (HAVE(SENDER_SEARCH_KEY)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENDER_SEARCH_KEY;
		dpv[dval++].Value.bin = spv[RR_SENDER_SEARCH_KEY].Value.bin;
	}
	if (HAVE(SENDER_EMAIL_ADDRESS)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENDER_EMAIL_ADDRESS;
		dpv[dval++].Value.LPSZ = spv[RR_SENDER_EMAIL_ADDRESS].Value.LPSZ;
	}
	if (HAVE(SENT_REPRESENTING_NAME)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_NAME;
		dpv[dval++].Value.LPSZ = spv[RR_SENT_REPRESENTING_NAME].Value.LPSZ;
	}
	if (HAVE(SENT_REPRESENTING_ADDRTYPE)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_ADDRTYPE;
		dpv[dval++].Value.LPSZ = spv[RR_SENT_REPRESENTING_ADDRTYPE].Value.LPSZ;
	}
	if (HAVE(SENT_REPRESENTING_ENTRYID)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_ENTRYID;
		dpv[dval++].Value.bin = spv[RR_SENT_REPRESENTING_ENTRYID].Value.bin;
	}
	if (HAVE(SENT_REPRESENTING_SEARCH_KEY)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_SEARCH_KEY;
		dpv[dval++].Value.bin = spv[RR_SENT_REPRESENTING_SEARCH_KEY].Value.bin;
	}
	if (HAVE(SENT_REPRESENTING_EMAIL_ADDRESS)) {
		dpv[dval].ulPropTag = PR_ORIGINAL_SENT_REPRESENTING_EMAIL_ADDRESS;
		dpv[dval++].Value.LPSZ = spv[RR_SENT_REPRESENTING_EMAIL_ADDRESS].Value.LPSZ;
	}
	if (HAVE(MDN_DISPOSITION_SENDINGMODE)) {
		dpv[dval].ulPropTag = PR_MDN_DISPOSITION_SENDINGMODE;
		dpv[dval++].Value.LPSZ = spv[RR_MDN_DISPOSITION_SENDINGMODE].Value.LPSZ;
	}
	if (HAVE(MDN_DISPOSITION_TYPE)) {
		dpv[dval].ulPropTag = PR_MDN_DISPOSITION_TYPE;
		dpv[dval++].Value.LPSZ = spv[RR_MDN_DISPOSITION_TYPE].Value.LPSZ;
	}

	// We are representing the person who received the email if we're sending the read receipt for someone else.
	if (HAVE(RECEIVED_BY_ENTRYID)) {
		dpv[dval].ulPropTag = PR_SENT_REPRESENTING_ENTRYID;
		dpv[dval++].Value = spv[RR_RECEIVED_BY_ENTRYID].Value;
	}
	if (HAVE(RECEIVED_BY_NAME)) {
		dpv[dval].ulPropTag = PR_SENT_REPRESENTING_NAME;
		dpv[dval++].Value = spv[RR_RECEIVED_BY_NAME].Value;
	}
	if (HAVE(RECEIVED_BY_EMAIL_ADDRESS)) {
		dpv[dval].ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS;
		dpv[dval++].Value = spv[RR_RECEIVED_BY_EMAIL_ADDRESS].Value;
	}
	if (HAVE(RECEIVED_BY_ADDRTYPE)) {
		dpv[dval].ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE;
		dpv[dval++].Value = spv[RR_RECEIVED_BY_ADDRTYPE].Value;
	}

//	PR_RCVD_REPRESENTING_NAME, PR_RCVD_REPRESENTING_ENTRYID	

	if (HAVE(INTERNET_MESSAGE_ID)) {
		dpv[dval].ulPropTag = PR_INTERNET_MESSAGE_ID;
		dpv[dval++].Value.LPSZ = spv[RR_INTERNET_MESSAGE_ID].Value.LPSZ;
	}
#undef HAVE

	hr = (*lppEmptyMessage)->OpenProperty(PR_BODY, &IID_IStream, 0, MAPI_CREATE | MAPI_MODIFY, &~lpBodyStream);
	if (hr != hrSuccess)
		return hr;
	hr = lpBodyStream->Write(strBodyText.c_str(), strBodyText.size() * sizeof(TCHAR), NULL);
	if (hr != hrSuccess)
		return hr;
	hr = lpBodyStream->Commit( 0 );//0 = STGC_DEFAULT
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(CbNewADRLIST(1), &~lpMods);
	if (hr != hrSuccess)
		return hr;
	lpMods->cEntries = 1;

	hr = MAPIAllocateBuffer(sizeof(SPropValue) * 8, (void**)&lpMods->aEntries->rgPropVals);
	if (hr != hrSuccess)
		return hr;
	hr = ECParseOneOff(reinterpret_cast<ENTRYID *>(spv[RR_REPORT_ENTRYID].Value.bin.lpb), spv[RR_REPORT_ENTRYID].Value.bin.cb, strName, strType, strAddress);
	if (hr != hrSuccess)
		return hr;

	auto &pv = lpMods->aEntries->rgPropVals;
	pv[0].ulPropTag = PR_ENTRYID;
	pv[0].Value.bin = spv[RR_REPORT_ENTRYID].Value.bin;
	pv[1].ulPropTag = PR_ADDRTYPE_W;
	pv[1].Value.lpszW = const_cast<wchar_t *>(strType.c_str());
	pv[2].ulPropTag = PR_DISPLAY_NAME_W;
	pv[2].Value.lpszW = const_cast<wchar_t *>(strName.c_str());
	pv[3].ulPropTag = PR_TRANSMITABLE_DISPLAY_NAME_W;
	pv[3].Value.lpszW = const_cast<wchar_t *>(strName.c_str());
	pv[4].ulPropTag = PR_SMTP_ADDRESS_W;
	pv[4].Value.lpszW = const_cast<wchar_t *>(strAddress.c_str());
	pv[5].ulPropTag = PR_EMAIL_ADDRESS_W;
	pv[5].Value.lpszW = const_cast<wchar_t *>(strAddress.c_str());
	hr = HrCreateEmailSearchKey((LPSTR)strType.c_str(), (LPSTR)strAddress.c_str(), &cbTmp, &~lpByteTmp);
	if (hr != hrSuccess)
		return hr;

	pv[6].ulPropTag = PR_SEARCH_KEY;
	pv[6].Value.bin.cb = cbTmp;
	pv[6].Value.bin.lpb = lpByteTmp;
	pv[7].ulPropTag = PR_RECIPIENT_TYPE;
	pv[7].Value.ul = MAPI_TO;
	lpMods->aEntries->cValues = 8;

	hr = (*lppEmptyMessage)->ModifyRecipients(MODRECIP_ADD, lpMods);
	if (hr != hrSuccess)
		return hr;
	return (*lppEmptyMessage)->SetProps(dval, dpv, nullptr);
}

HRESULT ClientUtil::GetGlobalProfileProperties(LPMAPISUP lpMAPISup, struct sGlobalProfileProps* lpsProfileProps)
{
	HRESULT			hr = hrSuccess;
	object_ptr<IProfSect> lpGlobalProfSect;

	hr = lpMAPISup->OpenProfileSection(reinterpret_cast<const MAPIUID *>(&pbGlobalProfileSectionGuid), MAPI_MODIFY, &~lpGlobalProfSect);
	if(hr != hrSuccess)
		return hr;
	return ClientUtil::GetGlobalProfileProperties(lpGlobalProfSect, lpsProfileProps);
}

HRESULT ClientUtil::GetGlobalProfileProperties(LPPROFSECT lpGlobalProfSect, struct sGlobalProfileProps* lpsProfileProps)
{
	HRESULT			hr = hrSuccess;
	memory_ptr<SPropValue> lpsPropArray;
	ULONG			cValues = 0;
	const SPropValue *lpProp = NULL;

	if (lpGlobalProfSect == nullptr || lpsProfileProps == nullptr)
		return MAPI_E_INVALID_OBJECT;

	// Get the properties we need directly from the global profile section
	hr = lpGlobalProfSect->GetProps(sptaKopanoProfile, 0, &cValues, &~lpsPropArray);
	if(FAILED(hr))
		return hr;

	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_PATH)) != NULL)
		lpsProfileProps->strServerPath = lpProp->Value.lpszA;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_PROFILE_NAME_A)) != NULL)
		lpsProfileProps->strProfileName = lpProp->Value.lpszA;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_USERNAME_W)) != NULL)
		lpsProfileProps->strUserName = convstring::from_SPropValue(lpProp);
	else if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_USERNAME_A)) != NULL)
		lpsProfileProps->strUserName = convstring::from_SPropValue(lpProp);
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_USERPASSWORD_W)) != NULL)
		lpsProfileProps->strPassword = convstring::from_SPropValue(lpProp);
	else if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_USERPASSWORD_A)) != NULL)
		lpsProfileProps->strPassword = convstring::from_SPropValue(lpProp);
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_IMPERSONATEUSER_W)) != NULL)
		lpsProfileProps->strImpersonateUser = convstring::from_SPropValue(lpProp);
	else if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_IMPERSONATEUSER_A)) != NULL)
		lpsProfileProps->strImpersonateUser = convstring::from_SPropValue(lpProp);
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_FLAGS)) != NULL)
		lpsProfileProps->ulProfileFlags = lpProp->Value.ul;
	else
		lpsProfileProps->ulProfileFlags = 0;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_SSLKEY_FILE)) != NULL)
		lpsProfileProps->strSSLKeyFile = lpProp->Value.lpszA;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_SSLKEY_PASS)) != NULL)
		lpsProfileProps->strSSLKeyPass = lpProp->Value.lpszA;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_HOST)) != NULL)
		lpsProfileProps->strProxyHost = lpProp->Value.lpszA;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_PORT)) != NULL)
		lpsProfileProps->ulProxyPort = lpProp->Value.ul;
	else
		lpsProfileProps->ulProxyPort = 0;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_FLAGS)) != NULL)
		lpsProfileProps->ulProxyFlags = lpProp->Value.ul;
	else
		lpsProfileProps->ulProxyFlags = 0;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_USERNAME)) != NULL)
		lpsProfileProps->strProxyUserName = lpProp->Value.lpszA;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_PROXY_PASSWORD)) != NULL)
		lpsProfileProps->strProxyPassword = lpProp->Value.lpszA;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_CONNECTION_TIMEOUT)) != NULL)
		lpsProfileProps->ulConnectionTimeOut = lpProp->Value.ul;
	else
		lpsProfileProps->ulConnectionTimeOut = 10;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_OFFLINE_PATH_W)) != NULL)
		lpsProfileProps->strOfflinePath = convstring::from_SPropValue(lpProp);
	else if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_OFFLINE_PATH_A)) != NULL)
		lpsProfileProps->strOfflinePath = convstring::from_SPropValue(lpProp);
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION)) != NULL)
		lpsProfileProps->strClientAppVersion = lpProp->Value.lpszA;
	if ((lpProp = PCpropFindProp(lpsPropArray, cValues, PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC)) != NULL)
		lpsProfileProps->strClientAppMisc = lpProp->Value.lpszA;

	return hrSuccess;
}

HRESULT ClientUtil::GetGlobalProfileDelegateStoresProp(LPPROFSECT lpGlobalProfSect, ULONG *lpcDelegates, LPBYTE *lppDelegateStores)
{
	HRESULT			hr = hrSuccess;
	memory_ptr<SPropValue> lpsPropValue;
	ULONG			cValues = 0;
	SizedSPropTagArray(1, sPropTagArray);
	memory_ptr<BYTE> lpDelegateStores;

	if (lpGlobalProfSect == nullptr || lpcDelegates == nullptr ||
	    lppDelegateStores == nullptr)
		return MAPI_E_INVALID_OBJECT;
	
	sPropTagArray.cValues = 1;
	sPropTagArray.aulPropTag[0] =  PR_STORE_PROVIDERS;
	hr = lpGlobalProfSect->GetProps(sPropTagArray, 0, &cValues, &~lpsPropValue);
	if(hr != hrSuccess)
		return hr;

	if(lpsPropValue[0].Value.bin.cb > 0){
		hr = MAPIAllocateBuffer(lpsPropValue[0].Value.bin.cb, &~lpDelegateStores);
		if(hr != hrSuccess)
			return hr;
		memcpy(lpDelegateStores, lpsPropValue[0].Value.bin.lpb, lpsPropValue[0].Value.bin.cb);
	}

	*lpcDelegates = lpsPropValue[0].Value.bin.cb;
	*lppDelegateStores = lpDelegateStores.release();
	return hrSuccess;
}

/* 
entryid functions

*/

HRESULT HrCreateEntryId(const GUID &guidStore, unsigned int ulObjType,
    ULONG *lpcbEntryId, ENTRYID **lppEntryId)
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
HRESULT HrGetServerURLFromStoreEntryId(ULONG cbEntryId,
    const ENTRYID *lpEntryId, std::string &rServerPath, bool *lpbIsPseudoUrl)
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
			 strncasecmp(lpTmpServerName, "file://", 7) &&
			 strncasecmp(lpTmpServerName, "default:", 8))
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
	ecmem_ptr<char> lpszServerPath;
	bool		bIsPeer = false;

	if (lpTransport == NULL || lpszUrl == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (strncmp(lpszUrl, "pseudo://", 9))
		return MAPI_E_NOT_FOUND;
	hr = lpTransport->HrResolvePseudoUrl(lpszUrl, &~lpszServerPath, &bIsPeer);
	if (hr != hrSuccess)
		return hr;
	serverPath = lpszServerPath.get();
	if (lpbIsPeer)
		*lpbIsPeer = bIsPeer;
	return hrSuccess;
}

HRESULT HrCompareEntryIdWithStoreGuid(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const GUID *guidStore)
{
	if (lpEntryID == NULL || guidStore == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (cbEntryID < 20)
		return MAPI_E_INVALID_ENTRYID;
	if (memcmp(lpEntryID->ab, guidStore, sizeof(GUID)) != 0)
		return MAPI_E_INVALID_ENTRYID;
	return hrSuccess;
}

HRESULT GetPublicEntryId(enumPublicEntryID ePublicEntryID,
    const GUID &guidStore, void *lpBase, ULONG *lpcbEntryID,
    ENTRYID **lppEntryID)
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

BOOL CompareMDBProvider(const BYTE *lpguid, const GUID *lpguidKopano)
{
	return CompareMDBProvider(reinterpret_cast<const MAPIUID *>(lpguid), lpguidKopano);
}

BOOL CompareMDBProvider(const MAPIUID *lpguid, const GUID *lpguidKopano)
{
	if (memcmp(lpguid, lpguidKopano, sizeof(GUID)) == 0)
		return TRUE;
	return FALSE;
}
