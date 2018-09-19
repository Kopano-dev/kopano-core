/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
#include "soapKCmdProxy.h"

using namespace KC;

// profile properties
static constexpr const SizedSPropTagArray(20, sptaKopanoProfile) =
	{20, {PR_EC_PATH, PR_PROFILE_NAME_A, PR_EC_USERNAME_A,
	PR_EC_USERNAME_W, PR_EC_USERPASSWORD_A, PR_EC_USERPASSWORD_W,
	PR_EC_IMPERSONATEUSER_A, PR_EC_IMPERSONATEUSER_W, PR_EC_FLAGS,
	PR_EC_SSLKEY_FILE, PR_EC_SSLKEY_PASS, PR_EC_PROXY_HOST,
	PR_EC_PROXY_PORT, PR_EC_PROXY_USERNAME, PR_EC_PROXY_PASSWORD,
	PR_EC_PROXY_FLAGS, PR_EC_CONNECTION_TIMEOUT, PR_SERVICE_NAME,
	PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION,
	PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC}};

HRESULT ClientUtil::HrInitializeStatusRow (const char * lpszProviderDisplay, ULONG ulResourceType, LPMAPISUP lpMAPISup, LPSPropValue lpspvIdentity, ULONG ulFlags)
{
	memory_ptr<SPropValue> row;
	size_t n = 0;
	auto hResult = MAPIAllocateBuffer(sizeof(SPropValue) * 13, &~row);
	if(hResult != hrSuccess)
		return hResult;

	memset(row, 0, sizeof(SPropValue) * 13);

	if(lpszProviderDisplay)
	{
		unsigned int size = strlen(lpszProviderDisplay) + 1;
		row[n].ulPropTag = PR_PROVIDER_DISPLAY_A;
		hResult = KAllocCopy(lpszProviderDisplay, size, reinterpret_cast<void **>(&row[n].Value.lpszA), row);
		if(hResult != hrSuccess)
			return hResult;
		++n;
		row[n].ulPropTag = PR_DISPLAY_NAME_A;
		hResult = KAllocCopy(lpszProviderDisplay, size, reinterpret_cast<void **>(&row[n].Value.lpszA), row);
		if(hResult != hrSuccess)
			return hResult;
		++n;
	}

	row[n].ulPropTag     = PR_PROVIDER_DLL_NAME_A;
	row[n++].Value.lpszA = const_cast<char *>(WCLIENT_DLL_NAME);
	row[n].ulPropTag     = PR_STATUS_CODE;
	row[n++].Value.l     = 1;
	row[n].ulPropTag     = PR_STATUS_STRING_W;
	row[n++].Value.lpszW = KC_W("Available");
	row[n].ulPropTag     = PR_IDENTITY_ENTRYID;
	row[n++].Value.bin   = lpspvIdentity[XPID_EID].Value.bin;
	row[n].ulPropTag     = CHANGE_PROP_TYPE(PR_IDENTITY_DISPLAY, PROP_TYPE(lpspvIdentity[XPID_NAME].ulPropTag));
	row[n++].Value.LPSZ  = lpspvIdentity[XPID_NAME].Value.LPSZ;
	row[n].ulPropTag     = PR_IDENTITY_SEARCH_KEY;
	row[n++].Value.bin   = lpspvIdentity[XPID_SEARCH_KEY].Value.bin;
	row[n].ulPropTag     = PR_OWN_STORE_ENTRYID;
	row[n++].Value.bin   = lpspvIdentity[XPID_STORE_EID].Value.bin;
	row[n].ulPropTag     = PR_RESOURCE_METHODS;
	row[n++].Value.l     = STATUS_VALIDATE_STATE;
	row[n].ulPropTag     = PR_RESOURCE_TYPE;
	row[n++].Value.l     = ulResourceType; //like MAPI_STORE_PROVIDER or MAPI_TRANSPORT_PROVIDER
	return lpMAPISup->ModifyStatusRow(n, row, ulFlags);
}

HRESULT ClientUtil::HrSetIdentity(WSTransport *lpTransport, LPMAPISUP lpMAPISup, LPSPropValue* lppIdentityProps)
{
	ULONG cbEntryStore = 0, cbEID = 0;
	memory_ptr<ENTRYID> lpEntryStore, lpEID;
	memory_ptr<ECUSER> lpUser;
	memory_ptr<SPropValue> idp;

	// Get the username and email adress
	auto hr = lpTransport->HrGetUser(0, NULL, fMapiUnicode, &~lpUser);
	if(hr != hrSuccess)
		return hr;
	unsigned int cValues = NUM_IDENTITY_PROPS;
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * cValues, &~idp);
	if (hr != hrSuccess)
		return hr;
	memset(idp, 0, sizeof(SPropValue) * cValues);

	auto strProfileSenderSearchKey = strToUpper(tstring(TRANSPORT_ADDRESS_TYPE_ZARAFA) + KC_T(":") + lpUser->lpszMailAddress);
	idp[XPID_EID].ulPropTag = PR_SENDER_ENTRYID;
	idp[XPID_EID].Value.bin.cb = lpUser->sUserId.cb;
	hr = KAllocCopy(lpUser->sUserId.lpb, lpUser->sUserId.cb, reinterpret_cast<void **>(&idp[XPID_EID].Value.bin.lpb), idp);
	if (hr != hrSuccess)
		return hr;

	idp[XPID_NAME].ulPropTag = PR_SENDER_NAME;
	unsigned int ulSize = sizeof(TCHAR) * (_tcslen(lpUser->lpszFullName) + 1);
	hr = KAllocCopy(lpUser->lpszFullName, ulSize, reinterpret_cast<void **>(&idp[XPID_NAME].Value.LPSZ), idp);
	if (hr != hrSuccess)
		return hr;

	idp[XPID_SEARCH_KEY].ulPropTag = PR_SENDER_SEARCH_KEY;
	idp[XPID_SEARCH_KEY].Value.bin.cb = strProfileSenderSearchKey.size()+1;
	hr = KAllocCopy(strProfileSenderSearchKey.c_str(), idp[XPID_SEARCH_KEY].Value.bin.cb, reinterpret_cast<void **>(&idp[XPID_SEARCH_KEY].Value.bin.lpb), idp);
	if (hr != hrSuccess)
		return hr;

	idp[XPID_ADDRESS].ulPropTag = PR_SENDER_EMAIL_ADDRESS;
	ulSize = sizeof(TCHAR) * (_tcslen(lpUser->lpszMailAddress) + 1);
	hr = KAllocCopy(lpUser->lpszMailAddress, ulSize, reinterpret_cast<void **>(&idp[XPID_ADDRESS].Value.LPSZ), idp);
	if (hr != hrSuccess)
		return hr;

	idp[XPID_ADDRTYPE].ulPropTag = PR_SENDER_ADDRTYPE;
	ulSize = sizeof(TCHAR) * (_tcslen(TRANSPORT_ADDRESS_TYPE_ZARAFA) + 1);
	hr = KAllocCopy(TRANSPORT_ADDRESS_TYPE_ZARAFA, ulSize, reinterpret_cast<void **>(&idp[XPID_ADDRTYPE].Value.LPSZ), idp);
	if (hr != hrSuccess)
		return hr;

	// Get the default store for this user, not an issue if it fails when not on home server
	if (lpTransport->HrGetStore(0, nullptr, &cbEntryStore, &~lpEntryStore, 0, nullptr) == hrSuccess) {
		hr = lpMAPISup->WrapStoreEntryID(cbEntryStore, lpEntryStore, &cbEID, (&~lpEID).as<ENTRYID>());
		if (hr != hrSuccess)
			return hr;
		idp[XPID_STORE_EID].ulPropTag = PR_OWN_STORE_ENTRYID;
		idp[XPID_STORE_EID].Value.bin.cb = cbEID;
		hr = KAllocCopy(lpEID.get(), cbEID, reinterpret_cast<void **>(&idp[XPID_STORE_EID].Value.bin.lpb), idp);
		if (hr != hrSuccess)
			return hr;
	}

	// Set the identity in the global provider identity
	*lppIdentityProps = idp.release();
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
	if (lpReadMessage == nullptr || lppEmptyMessage == nullptr ||
	    *lppEmptyMessage == nullptr)
		return MAPI_E_INVALID_OBJECT;
	if ((ulFlags & ~MAPI_NON_READ) != 0)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<SPropValue> spv, dpv;
	unsigned int dval = 0, cSrcValues = 0, cbTmp = 0;
	memory_ptr<BYTE> lpByteTmp;
	const TCHAR *lpMsgClass = NULL;
	LPTSTR lpReportText = nullptr, lpReadText = nullptr;
	FILETIME	ft;
	adrlist_ptr lpMods;
	std::wstring strName, strType, strAddress;
	time_t			zero = 0;
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

	GetSystemTimeAsFileTime(&ft);

	if (ulFlags & MAPI_NON_READ) {
		lpMsgClass = KC_T("REPORT.IPM.Note.IPNNRN");
		lpReadText = KC_TX("Not read:");
		lpReportText = KC_TX("was not read because it expired before reading at time");
	}else{
		lpMsgClass = KC_T("REPORT.IPM.Note.IPNRN");
		lpReadText = KC_TX("Read:");
		lpReportText = KC_TX("was read on");
	}

	auto hr = lpReadMessage->GetProps(sPropReadReceipt, fMapiUnicode, &cSrcValues, &~spv);
	if(FAILED(hr) != hrSuccess)
		return hr;

#define HAVE(tag) (spv[RR_ ## tag].ulPropTag == (PR_ ## tag))
	// important properties
	if (!HAVE(REPORT_ENTRYID))
		return MAPI_E_INVALID_PARAMETER;

	tstring strBodyText = KC_TX("Your message");
	strBodyText += KC_T("\r\n\r\n");

	if (HAVE(DISPLAY_TO)) {
		strBodyText += KC_T("\t");
		strBodyText += KC_TX("To:");
		strBodyText += KC_T(" ");
		strBodyText += spv[RR_DISPLAY_TO].Value.LPSZ;
		strBodyText += KC_T("\r\n");
	}
	if (HAVE(DISPLAY_CC)) {
		strBodyText += KC_T("\t");
		strBodyText += KC_TX("Cc:");
		strBodyText += KC_T(" ");
		strBodyText += spv[RR_DISPLAY_CC].Value.LPSZ;
		strBodyText += KC_T("\r\n");
	}
	if (HAVE(SUBJECT)) {
		strBodyText += KC_T("\t");
		strBodyText += KC_TX("Subject:");
		strBodyText += KC_T(" ");
		strBodyText += spv[RR_SUBJECT].Value.LPSZ;
		strBodyText += KC_T("\r\n");
	}
	if (HAVE(CLIENT_SUBMIT_TIME)) {
		strBodyText += KC_T("\t");
		strBodyText += KC_TX("Sent on:");
		strBodyText += KC_T(" ");
		auto tt = FileTimeToUnixTime(spv[RR_CLIENT_SUBMIT_TIME].Value.ft);
		auto tm = localtime(&tt);
		if (tm == NULL)
			tm = localtime(&zero);
		strftime(szTime, 255, "%c", tm);

		strBodyText+= convert_to<tstring>(szTime, strlen(szTime), CHARSET_CHAR);
		strBodyText += KC_T("\r\n");
	}

	strBodyText += KC_T("\r\n");
	strBodyText+= lpReportText;
	strBodyText += KC_T(" ");
	auto tt = FileTimeToUnixTime(ft);
	auto tm = localtime(&tt);
	if (tm == NULL)
		tm = localtime(&zero);
	strftime(szTime, 255, "%c", tm);

	strBodyText+= convert_to<tstring>(szTime, strlen(szTime), CHARSET_CHAR);
	strBodyText += KC_T("\r\n");
	auto ulMaxDestValues = cSrcValues + 4;//+ default properties
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
		dpv[dval].ulPropTag = PR_CONVERSATION_INDEX;
		dpv[dval].Value.bin.cb = cbTmp;
		hr = KAllocCopy(lpByteTmp, cbTmp, reinterpret_cast<void **>(&dpv[dval].Value.bin.lpb), dpv);
		if(hr != hrSuccess)
			return hr;
		++dval;
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
	lpMods->cEntries = 0;
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * 8, (void**)&lpMods->aEntries->rgPropVals);
	if (hr != hrSuccess)
		return hr;
	++lpMods->cEntries;
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
	object_ptr<IProfSect> lpGlobalProfSect;
	auto hr = lpMAPISup->OpenProfileSection(reinterpret_cast<const MAPIUID *>(&pbGlobalProfileSectionGuid), MAPI_MODIFY, &~lpGlobalProfSect);
	if(hr != hrSuccess)
		return hr;
	return ClientUtil::GetGlobalProfileProperties(lpGlobalProfSect, lpsProfileProps);
}

HRESULT ClientUtil::GetGlobalProfileProperties(IProfSect *sect, struct sGlobalProfileProps *gp)
{
	if (sect == nullptr || gp == nullptr)
		return MAPI_E_INVALID_OBJECT;

	memory_ptr<SPropValue> s;
	ULONG			cValues = 0;
	// Get the properties we need directly from the global profile section
	auto hr = sect->GetProps(sptaKopanoProfile, 0, &cValues, &~s);
	if(FAILED(hr))
		return hr;

	if (s[0].ulPropTag == PR_EC_PATH)
		gp->strServerPath = s[0].Value.lpszA;
	if (s[1].ulPropTag == PR_PROFILE_NAME_A)
		gp->strProfileName = s[1].Value.lpszA;
	if (s[3].ulPropTag == PR_EC_USERNAME_W)
		gp->strUserName = s[3].Value.lpszW;
	else if (s[2].ulPropTag == PR_EC_USERNAME_A)
		gp->strUserName = convstring::from_SPropValue(&s[2]);
	if (s[5].ulPropTag == PR_EC_USERPASSWORD_W)
		gp->strPassword = s[5].Value.lpszW;
	else if (s[4].ulPropTag == PR_EC_USERPASSWORD_A)
		gp->strPassword = convstring::from_SPropValue(&s[4]);
	if (s[7].ulPropTag == PR_EC_IMPERSONATEUSER_W)
		gp->strImpersonateUser = s[7].Value.lpszW;
	else if (s[6].ulPropTag == PR_EC_IMPERSONATEUSER_A)
		gp->strImpersonateUser = convstring::from_SPropValue(&s[6]);
	if (s[8].ulPropTag == PR_EC_FLAGS)
		gp->ulProfileFlags = s[8].Value.ul;
	if (s[9].ulPropTag == PR_EC_SSLKEY_FILE)
		gp->strSSLKeyFile = s[9].Value.lpszA;
	if (s[10].ulPropTag == PR_EC_SSLKEY_PASS)
		gp->strSSLKeyPass = s[10].Value.lpszA;
	if (s[11].ulPropTag == PR_EC_PROXY_HOST)
		gp->strProxyHost = s[11].Value.lpszA;
	if (s[12].ulPropTag == PR_EC_PROXY_PORT)
		gp->ulProxyPort = s[12].Value.ul;
	if (s[13].ulPropTag == PR_EC_PROXY_USERNAME)
		gp->strProxyUserName = s[13].Value.lpszA;
	if (s[14].ulPropTag == PR_EC_PROXY_PASSWORD)
		gp->strProxyPassword = s[14].Value.lpszA;
	if (s[15].ulPropTag == PR_EC_PROXY_FLAGS)
		gp->ulProxyFlags = s[15].Value.ul;
	if (s[16].ulPropTag == PR_EC_CONNECTION_TIMEOUT)
		gp->ulConnectionTimeOut = s[16].Value.ul;
	if (s[18].ulPropTag == PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION)
		gp->strClientAppVersion = s[18].Value.lpszA;
	if (s[19].ulPropTag == PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC)
		gp->strClientAppMisc = s[19].Value.lpszA;
	return hrSuccess;
}

HRESULT ClientUtil::GetGlobalProfileDelegateStoresProp(LPPROFSECT lpGlobalProfSect, ULONG *lpcDelegates, LPBYTE *lppDelegateStores)
{
	if (lpGlobalProfSect == nullptr || lpcDelegates == nullptr ||
	    lppDelegateStores == nullptr)
		return MAPI_E_INVALID_OBJECT;

	memory_ptr<SPropValue> lpsPropValue;
	ULONG			cValues = 0;
	SizedSPropTagArray(1, sPropTagArray);
	memory_ptr<BYTE> lpDelegateStores;

	sPropTagArray.cValues = 1;
	sPropTagArray.aulPropTag[0] =  PR_STORE_PROVIDERS;
	auto hr = lpGlobalProfSect->GetProps(sPropTagArray, 0, &cValues, &~lpsPropValue);
	if(hr != hrSuccess)
		return hr;

	if(lpsPropValue[0].Value.bin.cb > 0){
		hr = KAllocCopy(lpsPropValue[0].Value.bin.lpb, lpsPropValue[0].Value.bin.cb, &~lpDelegateStores);
		if(hr != hrSuccess)
			return hr;
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
	if (lpcbEntryId == nullptr || lppEntryId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	EID			eid;
	LPENTRYID	lpEntryId = NULL;
	if (CoCreateGuid(&eid.uniqueId) != hrSuccess)
		return MAPI_E_CALL_FAILED;

	unsigned int cbEntryId = CbNewEID("");
	auto hr = ECAllocateBuffer(cbEntryId, reinterpret_cast<void **>(&lpEntryId));
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
	if (lpEntryId == nullptr || lpbIsPseudoUrl == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (cbEntryId < offsetof(EID, ulVersion) + sizeof(EID::ulVersion))
		return MAPI_E_INVALID_ENTRYID;

	bool	bIsPseudoUrl = false;
	auto eby = reinterpret_cast<const char *>(lpEntryId);
	decltype(EID::ulVersion) version;
	std::string path;

	memcpy(&version, eby + offsetof(EID, ulVersion), sizeof(version));
	auto z = (version == 0) ? offsetof(EID_V0, szServer) : offsetof(EID, szServer);
	path.assign(eby + z, cbEntryId - z);
	auto pos = path.find_first_of('\0');
	if (pos != std::string::npos)
		path.erase(pos);
	if (kc_starts_with(path, "pseudo://"))
		bIsPseudoUrl = true;
	else if (!kc_starts_with(path, "http://") &&
	    !kc_starts_with(path, "https://") &&
	    !kc_starts_with(path, "file://") &&
	    !kc_starts_with(path, "default:"))
		return MAPI_E_NOT_FOUND;
	rServerPath = std::move(path);
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
	if (lpTransport == nullptr || lpszUrl == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (strncmp(lpszUrl, "pseudo://", 9))
		return MAPI_E_NOT_FOUND;

	ecmem_ptr<char> lpszServerPath;
	bool		bIsPeer = false;
	auto hr = lpTransport->HrResolvePseudoUrl(lpszUrl, &~lpszServerPath, &bIsPeer);
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
	if (lpcbEntryID == nullptr || lppEntryID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	LPENTRYID lpEntryID = NULL;
	GUID guidEmpty = {0};
	EID eid(MAPI_FOLDER, guidStore, guidEmpty);

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

	unsigned int cbEntryID = CbEID(&eid);
	auto hr = KAllocCopy(&eid, cbEntryID, reinterpret_cast<void **>(&lpEntryID), lpBase);
	if (hr != hrSuccess)
		return hr;
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
	return memcmp(lpguid, lpguidKopano, sizeof(GUID)) == 0;
}

soap_lock_guard::soap_lock_guard(WSSoap &p) :
	m_parent(p), m_dg(p.m_hDataLock)
{}

void soap_lock_guard::unlock()
{
	if (m_done)
		return;
	m_done = true;
	/* Clean up data created with soap_malloc */
	if (m_parent.m_lpCmd != nullptr && m_parent.m_lpCmd->soap != nullptr) {
		soap_destroy(m_parent.m_lpCmd->soap);
		soap_end(m_parent.m_lpCmd->soap);
	}
	m_dg.unlock();
}

soap_lock_guard::~soap_lock_guard()
{
	unlock();
}
