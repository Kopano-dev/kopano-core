/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <string>
#include <unordered_set>
#include "rules.h"
#include <mapi.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <edkmdb.h>
#include <edkguid.h>
#include <kopano/ECGetText.h>
#include <kopano/stringutil.h>
#include <kopano/Util.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/mapi_ptr.h>
#include <kopano/mapiguidext.h>
#include <kopano/charset/convert.h>
#include <kopano/IECInterfaces.hpp>
#include "PyMapiPlugin.h"

using namespace KC;
using namespace std::string_literals;
using std::string;
using std::wstring;
extern std::shared_ptr<ECConfig> g_lpConfig;

enum actstatus {
	ROP_FAILURE,
	ROP_NOOP,
	ROP_ERROR,
	ROP_SUCCESS,
	ROP_CANCEL,
	ROP_MOVED,
	ROP_FORWARDED,
};

struct actresult {
	enum actstatus status;
	HRESULT code;
};

class kc_icase_hash {
	public:
	size_t operator()(const std::string &i) const
	{
		return std::hash<std::string>()(strToLower(i));
	}
};

class kc_icase_equal {
	public:
	bool operator()(const std::string &a, const std::string &b) const
	{
		return strcasecmp(a.c_str(), b.c_str()) == 0;
	}
};

/**
 * Contains all the exact-match header names that will inhibit autoreplies.
 */
static const std::unordered_set<std::string, kc_icase_hash, kc_icase_equal> kc_stopreply_hdr = {
	/* Kopano - Vacation header already present, do not send vacation reply. */
	"X-Kopano-Vacation",
	/* RFC 3834 - Precedence: list/bulk/junk, do not reply to these mails. */
	"Auto-Submitted",
	"Precedence",
	/* RFC 2919 */
	"List-Id",
	/* RFC 2369 */
	"List-Help",
	"List-Subscribe",
	"List-Unsubscribe",
	"List-Post",
	"List-Owner",
	"List-Archive",
};

/* A list of prefix searches for entire header-value lines */
static const std::unordered_set<std::string, kc_icase_hash, kc_icase_equal> kc_stopreply_hdr2 = {
	/* From the package "vacation" */
	"X-Spam-Flag: YES",
	/* From openSUSE's vacation package */
	"X-Is-Junk: YES",
	"X-AMAZON",
	"X-LinkedIn",
};

/**
 * Determines from a set of lines from internet headers (can be wrapped or
 * not) whether to inhibit autoreplies.
 */
bool dagent_avoid_autoreply(const std::vector<std::string> &hl)
{
	for (const auto &line : hl) {
		if (isspace(line[0]))
			continue;
		size_t pos = line.find_first_of(':');
		if (pos == std::string::npos || pos == 0)
			continue;
		if (kc_stopreply_hdr.find(line.substr(0, pos)) != kc_stopreply_hdr.cend())
			return true;
		for (const auto &elem : kc_stopreply_hdr2)
			if (kc_stopreply_hdr2.find(line.substr(0, elem.size())) != kc_stopreply_hdr2.cend())
				return true;
	}
	return false;
}

static HRESULT GetRecipStrings(LPMESSAGE lpMessage, std::wstring &wstrTo,
    std::wstring &wstrCc, std::wstring &wstrBcc)
{
	SRowSetPtr ptrRows;
	MAPITablePtr ptrRecips;
	static constexpr const SizedSPropTagArray(2, sptaDisplay) =
		{2, {PR_DISPLAY_NAME_W, PR_RECIPIENT_TYPE}};

	wstrTo.clear();
	wstrCc.clear();
	wstrBcc.clear();

	HRESULT hr = lpMessage->GetRecipientTable(MAPI_UNICODE, &~ptrRecips);
	if(hr != hrSuccess)
		return hr;
	hr = ptrRecips->SetColumns(sptaDisplay, TBL_BATCH);
	if(hr != hrSuccess)
		return hr;

	while(1) {
		hr = ptrRecips->QueryRows(1, 0, &~ptrRows);
		if(hr != hrSuccess)
			return hr;
		if(ptrRows.size() == 0)
			break;
		if(ptrRows[0].lpProps[0].ulPropTag != PR_DISPLAY_NAME_W || ptrRows[0].lpProps[1].ulPropTag != PR_RECIPIENT_TYPE)
			continue;

		switch(ptrRows[0].lpProps[1].Value.ul) {
		case MAPI_TO:
			if (!wstrTo.empty()) wstrTo += L"; ";
			wstrTo += ptrRows[0].lpProps[0].Value.lpszW;
			break;
		case MAPI_CC:
			if (!wstrCc.empty()) wstrCc += L"; ";
			wstrCc += ptrRows[0].lpProps[0].Value.lpszW;
			break;
		case MAPI_BCC:
			if (!wstrBcc.empty()) wstrBcc += L"; ";
			wstrBcc += ptrRows[0].lpProps[0].Value.lpszW;
			break;
		}
	}
	return hrSuccess;
}

static HRESULT MungeForwardBody(LPMESSAGE lpMessage, LPMESSAGE lpOrigMessage)
{
	SPropArrayPtr ptrBodies, ptrInfo;
	static constexpr const SizedSPropTagArray(4, sBody) =
		{4, {PR_BODY_W, PR_HTML, PR_RTF_IN_SYNC, PR_INTERNET_CPID}};
	static constexpr const SizedSPropTagArray(4, sInfo) =
		{4, {PR_SENT_REPRESENTING_NAME_W,
		PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, PR_MESSAGE_DELIVERY_TIME,
		PR_SUBJECT_W}};
	unsigned int cValues, ulCharset = 20127; /* US-ASCII */
	bool bPlain = false;
	SPropValue sNewBody;
	StreamPtr ptrStream;
	std::string strHTML, strHTMLForwardText;
	std::wstring wstrBody, strForwardText, wstrTo, wstrCc, wstrBcc;

	HRESULT hr = lpOrigMessage->GetProps(sBody, 0, &cValues, &~ptrBodies);
	if (FAILED(hr))
		return hr;
	if (PROP_TYPE(ptrBodies[3].ulPropTag) != PT_ERROR)
		ulCharset = ptrBodies[3].Value.ul;
	if (PROP_TYPE(ptrBodies[0].ulPropTag) == PT_ERROR && PROP_TYPE(ptrBodies[1].ulPropTag) == PT_ERROR)
		// plain and html not found, check sync flag
		bPlain = (ptrBodies[2].Value.b == FALSE);
	else
		bPlain = PROP_TYPE(ptrBodies[1].ulPropTag) == PT_ERROR && ptrBodies[1].Value.err == MAPI_E_NOT_FOUND;
	sNewBody.ulPropTag = bPlain ? PR_BODY_W : PR_HTML;

	// From: <fullname>
	// Sent: <date>
	// To: <original To:>
	// Cc: <original Cc:>
	// Subject: <>
	// Auto forwarded by a rule
	hr = GetRecipStrings(lpOrigMessage, wstrTo, wstrCc, wstrBcc);
	if (FAILED(hr))
		return hr;
	hr = lpOrigMessage->GetProps(sInfo, 0, &cValues, &~ptrInfo);
	if (FAILED(hr))
		return hr;

	if (bPlain) {
		// Plain text body
		strForwardText = L"From: ";
		if (PROP_TYPE(ptrInfo[0].ulPropTag) != PT_ERROR)
			strForwardText += ptrInfo[0].Value.lpszW;
		else if (PROP_TYPE(ptrInfo[1].ulPropTag) != PT_ERROR)
			strForwardText += ptrInfo[1].Value.lpszW;
		if (PROP_TYPE(ptrInfo[1].ulPropTag) != PT_ERROR) {
			strForwardText += L" <";
			strForwardText += ptrInfo[1].Value.lpszW;
			strForwardText += L">";
		}

		strForwardText += L"\nSent: ";
		if (PROP_TYPE(ptrInfo[2].ulPropTag) != PT_ERROR) {
			WCHAR buffer[64];
			struct tm date;
			auto t = FileTimeToUnixTime(ptrInfo[2].Value.ft);
			localtime_r(&t, &date);
			wcsftime(buffer, ARRAY_SIZE(buffer), L"%c", &date);
			strForwardText += buffer;
		}

		strForwardText += L"\nTo: ";
		strForwardText += wstrTo;
		strForwardText += L"\nCc: ";
		strForwardText += wstrCc;
		strForwardText += L"\nSubject: ";
		if (PROP_TYPE(ptrInfo[3].ulPropTag) != PT_ERROR)
			strForwardText += ptrInfo[3].Value.lpszW;
		strForwardText += L"\nAuto forwarded by a rule\n\n";

		if (ptrBodies[0].ulPropTag == PT_ERROR) {
			hr = lpOrigMessage->OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, &~ptrStream);
			if (hr == hrSuccess)
				hr = Util::HrStreamToString(ptrStream, wstrBody);
			// stream
			strForwardText.append(wstrBody);
		} else {
			strForwardText += ptrBodies[0].Value.lpszW;
		}
		sNewBody.Value.lpszW = (WCHAR*)strForwardText.c_str();
	}
	else {
		// HTML body (or rtf, but nuts to editing that!)
		hr = lpOrigMessage->OpenProperty(PR_HTML, &IID_IStream, 0, 0, &~ptrStream);
		if (hr == hrSuccess)
			hr = Util::HrStreamToString(ptrStream, strHTML);
		// icase <body> tag
		auto pos = str_ifind(strHTML.c_str(), "<body");
		pos = pos ? pos + strlen("<body") : strHTML.c_str();
		// if body tag was not found, this will make it be placed after the first tag, probably <html>
		if ((pos == strHTML.c_str() && *pos == '<') || pos != strHTML.c_str()) {
			// not all html bodies start actually using tags, so only seek if we find a <, or if we found a body tag starting point.
			while (*pos && *pos != '>')
				++pos;
			if (*pos == '>')
				++pos;
		}

		strHTMLForwardText = "<b>From:</b> ";
		if (PROP_TYPE(ptrInfo[0].ulPropTag) != PT_ERROR)
			Util::HrTextToHtml(ptrInfo[0].Value.lpszW, strHTMLForwardText, ulCharset);
		else if (PROP_TYPE(ptrInfo[1].ulPropTag) != PT_ERROR)
			Util::HrTextToHtml(ptrInfo[1].Value.lpszW, strHTMLForwardText, ulCharset);

		if (PROP_TYPE(ptrInfo[1].ulPropTag) != PT_ERROR) {
			strHTMLForwardText += " &lt;<a href=\"mailto:";
			Util::HrTextToHtml(ptrInfo[1].Value.lpszW, strHTMLForwardText, ulCharset);
			strHTMLForwardText += "\">";
			Util::HrTextToHtml(ptrInfo[1].Value.lpszW, strHTMLForwardText, ulCharset);
			strHTMLForwardText += "</a>&gt;";
		}

		strHTMLForwardText += "<br><b>Sent:</b> ";
		if (PROP_TYPE(ptrInfo[2].ulPropTag) != PT_ERROR) {
			char buffer[32];
			struct tm date;
			auto t = FileTimeToUnixTime(ptrInfo[2].Value.ft);
			localtime_r(&t, &date);
			strftime(buffer, 32, "%c", &date);
			strHTMLForwardText += buffer;
		}
		strHTMLForwardText += "<br><b>To:</b> ";
		Util::HrTextToHtml(wstrTo.c_str(), strHTMLForwardText, ulCharset);
		strHTMLForwardText += "<br><b>Cc:</b> ";
		Util::HrTextToHtml(wstrCc.c_str(), strHTMLForwardText, ulCharset);
		strHTMLForwardText += "<br><b>Subject:</b> ";
		if (PROP_TYPE(ptrInfo[3].ulPropTag) != PT_ERROR)
			Util::HrTextToHtml(ptrInfo[3].Value.lpszW, strHTMLForwardText, ulCharset);

		strHTMLForwardText += "<br><b>Auto forwarded by a rule</b><br><hr><br>";
		strHTML.insert((pos - strHTML.c_str()), strHTMLForwardText);
		sNewBody.Value.bin.cb = strHTML.size();
		sNewBody.Value.bin.lpb = (BYTE*)strHTML.c_str();
	}

	// set new body with forward markers
	return lpMessage->SetProps(1, &sNewBody, NULL);
}

static HRESULT CreateOutboxMessage(LPMDB lpOrigStore, LPMESSAGE *lppMessage)
{
	object_ptr<IMAPIFolder> lpOutbox;
	memory_ptr<SPropValue> lpOutboxEntryID;
	ULONG ulObjType = 0;

	auto hr = HrGetOneProp(lpOrigStore, PR_IPM_OUTBOX_ENTRYID, &~lpOutboxEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = lpOrigStore->OpenEntry(lpOutboxEntryID->Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(lpOutboxEntryID->Value.bin.lpb),
	     &iid_of(lpOutbox), MAPI_MODIFY, &ulObjType, &~lpOutbox);
	if (hr != hrSuccess)
		return hr;
	return lpOutbox->CreateMessage(nullptr, 0, lppMessage);
}

static HRESULT CreateReplyCopy(LPMAPISESSION lpSession, LPMDB lpOrigStore,
    IMAPIProp *lpOrigMessage, LPMESSAGE lpTemplate, LPMESSAGE *lppMessage)
{
	object_ptr<IMessage> lpReplyMessage;
	memory_ptr<SPropValue> lpProp, lpFrom, lpReplyRecipient;
	memory_ptr<SPropValue> lpSentMailEntryID;
	std::wstring strwSubject;
	ULONG cValues = 0, ulCmp = 0;
	SizedADRLIST(1, sRecip) = {0, {}};
	static constexpr const SizedSPropTagArray(5, sFrom) =
		{5, {PR_RECEIVED_BY_ENTRYID, PR_RECEIVED_BY_NAME,
		PR_RECEIVED_BY_ADDRTYPE, PR_RECEIVED_BY_EMAIL_ADDRESS,
		PR_RECEIVED_BY_SEARCH_KEY}};
	static constexpr const SizedSPropTagArray(6, sReplyRecipient) =
		{6, {PR_SENDER_ENTRYID, PR_SENDER_NAME, PR_SENDER_ADDRTYPE,
		PR_SENDER_EMAIL_ADDRESS, PR_SENDER_SEARCH_KEY, PR_NULL}};

	auto hr = CreateOutboxMessage(lpOrigStore, &~lpReplyMessage);
	if (hr != hrSuccess)
		return hr;
	hr = lpTemplate->CopyTo(0, NULL, NULL, 0, NULL, &IID_IMessage, lpReplyMessage, 0, NULL);
	if (hr != hrSuccess)
		return hr;
	// set "sent mail" folder entryid for spooler
	hr = HrGetOneProp(lpOrigStore, PR_IPM_SENTMAIL_ENTRYID, &~lpSentMailEntryID);
	if (hr != hrSuccess)
		return hr;
	lpSentMailEntryID->ulPropTag = PR_SENTMAIL_ENTRYID;
	hr = HrSetOneProp(lpReplyMessage, lpSentMailEntryID);
	if (hr != hrSuccess)
		return hr;

	// set a sensible subject
	hr = HrGetOneProp(lpReplyMessage, PR_SUBJECT_W, &~lpProp);
	if (hr == hrSuccess && lpProp->Value.lpszW[0] == L'\0') {
		// Exchange: uses "BT: orig subject" if empty, or only subject from template.
		hr = HrGetOneProp(lpOrigMessage, PR_SUBJECT_W, &~lpProp);
		if (hr == hrSuccess) {
			strwSubject = wstring(L"BT: ") + lpProp->Value.lpszW;
			lpProp->Value.lpszW = (WCHAR*)strwSubject.c_str();
			hr = HrSetOneProp(lpReplyMessage, lpProp);
			if (hr != hrSuccess)
				return hr;
		}
	}
	hr = HrGetOneProp(lpOrigMessage, PR_INTERNET_MESSAGE_ID, &~lpProp);
	if (hr == hrSuccess) {
		lpProp->ulPropTag = PR_IN_REPLY_TO_ID;
		hr = HrSetOneProp(lpReplyMessage, lpProp);
		if (hr != hrSuccess)
			return hr;
	}
	// set From to self
	hr = lpOrigMessage->GetProps(sFrom, 0, &cValues, &~lpFrom);
	if (FAILED(hr))
		return hr;

	lpFrom[0].ulPropTag = CHANGE_PROP_TYPE(PR_SENT_REPRESENTING_ENTRYID, PROP_TYPE(lpFrom[0].ulPropTag));
	lpFrom[1].ulPropTag = CHANGE_PROP_TYPE(PR_SENT_REPRESENTING_NAME, PROP_TYPE(lpFrom[1].ulPropTag));
	lpFrom[2].ulPropTag = CHANGE_PROP_TYPE(PR_SENT_REPRESENTING_ADDRTYPE, PROP_TYPE(lpFrom[2].ulPropTag));
	lpFrom[3].ulPropTag = CHANGE_PROP_TYPE(PR_SENT_REPRESENTING_EMAIL_ADDRESS, PROP_TYPE(lpFrom[3].ulPropTag));
	lpFrom[4].ulPropTag = CHANGE_PROP_TYPE(PR_SENT_REPRESENTING_SEARCH_KEY, PROP_TYPE(lpFrom[4].ulPropTag));

	hr = lpReplyMessage->SetProps(5, lpFrom, NULL);
	if (FAILED(hr))
		return hr;

	if (parseBool(g_lpConfig->GetSetting("set_rule_headers", NULL, "yes"))) {
		SPropValue sPropVal;
		PROPMAP_START(1)
		PROPMAP_NAMED_ID(KopanoRuleAction, PT_UNICODE, PS_INTERNET_HEADERS, "x-kopano-rule-action")
		PROPMAP_INIT(lpReplyMessage);

		sPropVal.ulPropTag = PROP_KopanoRuleAction;
		sPropVal.Value.lpszW = const_cast<wchar_t *>(L"reply");
		hr = HrSetOneProp(lpReplyMessage, &sPropVal);
		if (hr != hrSuccess)
			return hr;
	}

	// append To with original sender
	// @todo get Reply-To ?
	hr = lpOrigMessage->GetProps(sReplyRecipient, 0, &cValues, &~lpReplyRecipient);
	if (FAILED(hr))
		return hr;
	// obvious loop is being obvious
	if (PROP_TYPE(lpReplyRecipient[0].ulPropTag) != PT_ERROR && PROP_TYPE(lpFrom[0].ulPropTag ) != PT_ERROR) {
		hr = lpSession->CompareEntryIDs(lpReplyRecipient[0].Value.bin.cb, (LPENTRYID)lpReplyRecipient[0].Value.bin.lpb,
										lpFrom[0].Value.bin.cb, (LPENTRYID)lpFrom[0].Value.bin.lpb, 0, &ulCmp);
		if (hr == hrSuccess && ulCmp == TRUE)
			return MAPI_E_UNABLE_TO_COMPLETE;
	}

	lpReplyRecipient[0].ulPropTag = CHANGE_PROP_TYPE(PR_ENTRYID, PROP_TYPE(lpReplyRecipient[0].ulPropTag));
	lpReplyRecipient[1].ulPropTag = CHANGE_PROP_TYPE(PR_DISPLAY_NAME, PROP_TYPE(lpReplyRecipient[1].ulPropTag));
	lpReplyRecipient[2].ulPropTag = CHANGE_PROP_TYPE(PR_ADDRTYPE, PROP_TYPE(lpReplyRecipient[2].ulPropTag));
	lpReplyRecipient[3].ulPropTag = CHANGE_PROP_TYPE(PR_EMAIL_ADDRESS, PROP_TYPE(lpReplyRecipient[3].ulPropTag));
	lpReplyRecipient[4].ulPropTag = CHANGE_PROP_TYPE(PR_SEARCH_KEY, PROP_TYPE(lpReplyRecipient[4].ulPropTag));
	lpReplyRecipient[5].ulPropTag = PR_RECIPIENT_TYPE;
	lpReplyRecipient[5].Value.ul = MAPI_TO;
	sRecip.cEntries = 1;
	sRecip.aEntries[0].cValues = cValues;
	sRecip.aEntries[0].rgPropVals = lpReplyRecipient;

	hr = lpReplyMessage->ModifyRecipients(MODRECIP_ADD, sRecip);
	if (FAILED(hr))
		return hr;
	// return message
	return lpReplyMessage->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppMessage));
}

/**
 * @pat:	pattern
 * @subj:	subject string (domain part of an e-mail address)
 *
 * This _is_ different from kc_wildcard_cmp in that cmp2 allows
 * the asterisk to match dots.
 */
static bool kc_wildcard_cmp2(const char *pat, const char *subj)
{
	while (*pat != '\0' && *subj != '\0') {
		if (*pat == '*') {
			++pat;
			for (; *subj != '\0'; ++subj)
				if (kc_wildcard_cmp2(pat, subj))
					return true;
			continue;
		}
		if (tolower(*pat) != tolower(*subj))
			return false;
		++pat;
		++subj;
	}
	return *pat == '\0' && *subj == '\0';
}

static bool proc_fwd_allowed(const std::vector<std::string> &wdomlist,
    const char *addr)
{
	const char *p = strchr(addr, '@');
	if (p == nullptr) {
		ec_log_err("K-1900: Address \"%s\" had no '@', aborting forward because we could not possibly match it to a domain whitelist.", addr);
		return false;
	}
	addr = p + 1;
	for (const auto &wdomstr : wdomlist) {
		const char *wdom = wdomstr.c_str();
		if (strcmp(wdom, "*") == 0 /* fastpath */ ||
		    kc_wildcard_cmp2(wdom, addr)) {
			ec_log_info("K-1901: proc_fwd_allowed: \"%s\" whitelist match on \"%s\"", addr, wdom);
			return true;
		}
	}
	ec_log_info("K-1902: proc_fwd_allowed: \"%s\" matched nothing on the whitelist", addr);
	return false;
}

/**
 * @inbox:	folder to dump warning mail into
 * @addr:	target address which delivery was rejected to
 *
 * Drop an additional message into @inbox informing about the
 * unwillingness to process a forwarding rule.
 */
static HRESULT kc_send_fwdabort_notice(IMsgStore *store, const wchar_t *addr, const wchar_t *subject)
{
	memory_ptr<ENTRYID> eid;
	ULONG eid_size;
	auto ret = store->GetReceiveFolder((LPTSTR)"IPM", 0, &eid_size, &~eid, nullptr);
	if (ret != hrSuccess)
		return kc_perror("K-2382", ret);
	object_ptr<IMAPIFolder> inbox;
	unsigned int objtype = 0;
	ret = store->OpenEntry(eid_size, eid, &iid_of(inbox), MAPI_MODIFY, &objtype, &~inbox);
	if (ret != hrSuccess)
		return kc_perror("K-2383", ret);
	object_ptr<IMessage> msg;
	ret = inbox->CreateMessage(nullptr, 0, &~msg);
	if (ret != hrSuccess)
		return kc_perror("K-2384", ret);

	SPropValue prop[7];
	size_t nprop = 0;
	prop[nprop].ulPropTag = PR_SENDER_NAME_W;
	prop[nprop++].Value.lpszW = const_cast<wchar_t *>(L"Mail Delivery System");

	auto newsubject = convert_to<std::wstring>(g_lpConfig->GetSetting("forward_whitelist_domain_subject"));
	auto pos = newsubject.find(L"%subject");
	if (pos != std::string::npos)
		newsubject = newsubject.replace(pos, 8, subject);
	prop[nprop].ulPropTag = PR_SUBJECT_W;
	prop[nprop++].Value.lpszW = const_cast<wchar_t *>(newsubject.c_str());

	prop[nprop].ulPropTag = PR_MESSAGE_FLAGS;
	prop[nprop++].Value.ul = 0;
	prop[nprop].ulPropTag = PR_MESSAGE_CLASS_W;
	prop[nprop++].Value.lpszW = const_cast<wchar_t *>(L"REPORT.IPM.Note.NDR");
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	prop[nprop].ulPropTag = PR_CLIENT_SUBMIT_TIME;
	prop[nprop++].Value.ft = ft;
	prop[nprop].ulPropTag = PR_MESSAGE_DELIVERY_TIME;
	prop[nprop++].Value.ft = ft;

	auto newbody = convert_to<std::wstring>(g_lpConfig->GetSetting("forward_whitelist_domain_message"));
	pos = newbody.find(L"%subject");
	if (pos != std::string::npos)
		newbody = newbody.replace(pos, 8, subject);
	pos = newbody.find(L"%sender");
	if (pos != std::string::npos)
		newbody = newbody.replace(pos, 7, addr);

	prop[nprop].ulPropTag = PR_BODY_W;
	prop[nprop++].Value.lpszW = const_cast<wchar_t *>(newbody.c_str());
	auto hr = msg->SetProps(nprop, prop, nullptr);
	if (hr != hrSuccess)
		return kc_perror("K-3283: SetProps", hr);
	hr = msg->SaveChanges(KEEP_OPEN_READONLY);
	if (hr != hrSuccess)
		return kc_perror("K-3284: commit", hr);
	hr = HrNewMailNotification(store, msg);
	if (hr != hrSuccess)
		return kc_pwarn("K-3285: NewMailNotification", hr);
	return hrSuccess;
}

/**
 * Checks the rule recipient list for a possible loop, and filters
 * that recipient. Returns an error when no recipients are left after
 * the filter.
 *
 * @param[in] lpMessage The original delivered message performing the rule action
 * @param[in] lpRuleRecipients The recipient list from the rule
 * @param[in] bOpDelegate	If the action a delegate or forward action
 * @param[out] lppNewRecipients The actual recipient list to perform the action on
 *
 * @return MAPI error code
 */
static HRESULT CheckRecipients(IAddrBook *lpAdrBook, IMsgStore *orig_store,
    IMAPIProp *lpMessage, const ADRLIST *lpRuleRecipients, bool bOpDelegate,
    bool bIncludeAsP1, ADRLIST **lppNewRecipients)
{
	adrlist_ptr lpRecipients;
	memory_ptr<SPropValue> lpMsgClass;
	std::wstring strFromName, strFromType, strFromAddress;

	auto hr = HrGetAddress(lpAdrBook, dynamic_cast<IMessage *>(lpMessage),
	          PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_NAME_W,
	          PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W,
	          strFromName, strFromType, strFromAddress);
	if (hr != hrSuccess)
		return kc_perror("Unable to get from address", hr);
	hr = MAPIAllocateBuffer(CbNewADRLIST(lpRuleRecipients->cEntries), &~lpRecipients);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffer failed", hr);

	lpRecipients->cEntries = 0;
	std::vector<std::string> fwd_whitelist =
		tokenize(g_lpConfig->GetSetting("forward_whitelist_domains"), " ");
	if (HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMsgClass) != hrSuccess)
		/* ignore error - will check for pointer instead */;

	for (ULONG i = 0; i < lpRuleRecipients->cEntries; ++i) {
		std::wstring strRuleName, strRuleType, strRuleAddress;

		hr = HrGetAddress(lpAdrBook, lpRuleRecipients->aEntries[i].rgPropVals, lpRuleRecipients->aEntries[i].cValues, PR_ENTRYID,
		     CHANGE_PROP_TYPE(PR_DISPLAY_NAME, PT_UNSPECIFIED), CHANGE_PROP_TYPE(PR_ADDRTYPE, PT_UNSPECIFIED), CHANGE_PROP_TYPE(PR_EMAIL_ADDRESS, PT_UNSPECIFIED),
		     strRuleName, strRuleType, strRuleAddress);
		if (hr != hrSuccess)
			return kc_perror("Unable to get rule address", hr);

		auto rule_addr_std = convert_to<std::string>(strRuleAddress);
		memory_ptr<SPropValue> subject;
		std::wstring subject_wstd;
		hr = HrGetOneProp(lpMessage, PR_SUBJECT_W, &~subject);
		if (hr == hrSuccess)
			subject_wstd = convert_to<std::wstring>(subject->Value.lpszW);
		else if (hr != MAPI_E_NOT_FOUND)
			return hr;

		if (!proc_fwd_allowed(fwd_whitelist, rule_addr_std.c_str())) {
			kc_send_fwdabort_notice(orig_store, strRuleAddress.c_str(), subject_wstd.c_str());
			return MAPI_E_NO_ACCESS;
		}
		if (strFromAddress == strRuleAddress &&
		    // Hack for Meeting requests
		    (!bOpDelegate || lpMsgClass == nullptr ||
		    strstr(lpMsgClass->Value.lpszA, "IPM.Schedule.Meeting.") == nullptr)) {
			ec_log_info("Same user found in From and rule, blocking for loop protection");
			continue;
		}

		// copy recipient
		hr = Util::HrCopyPropertyArray(lpRuleRecipients->aEntries[i].rgPropVals, lpRuleRecipients->aEntries[i].cValues, &lpRecipients->aEntries[lpRecipients->cEntries].rgPropVals, &lpRecipients->aEntries[lpRecipients->cEntries].cValues, true);
		if (hr != hrSuccess)
			return kc_perrorf("Util::HrCopyPropertyArray failed", hr);

		++lpRecipients->cEntries;
        if(bIncludeAsP1) {
			auto lpRecipType = PpropFindProp(lpRecipients->aEntries[lpRecipients->cEntries-1].rgPropVals, lpRecipients->aEntries[lpRecipients->cEntries-1].cValues, PR_RECIPIENT_TYPE);
            if(!lpRecipType) {
                ec_log_crit("Attempt to add recipient with no PR_RECIPIENT_TYPE");
				return MAPI_E_INVALID_PARAMETER;
            }
            lpRecipType->Value.ul = MAPI_P1;
        }
	}

	if (lpRecipients->cEntries == 0) {
		ec_log_warn("Loop protection blocked all recipients, skipping rule");
		return MAPI_E_UNABLE_TO_COMPLETE;
	}
	if (lpRecipients->cEntries != lpRuleRecipients->cEntries)
		ec_log_info("Loop protection blocked some recipients");
	*lppNewRecipients = lpRecipients.release();
	return hrSuccess;
}

static HRESULT CreateForwardCopy(IAddrBook *lpAdrBook, IMsgStore *lpOrigStore,
    IMessage *lpOrigMessage, const ADRLIST *lpRecipients,
    bool bOpDelegate, bool bDoPreserveSender, bool bDoNotMunge,
    bool bForwardAsAttachment, IMessage **lppMessage)
{
	object_ptr<IMessage> lpFwdMsg;
	memory_ptr<SPropValue> lpSentMailEntryID, lpOrigSubject;
	memory_ptr<SPropTagArray> lpExclude;
	adrlist_ptr filtered_recips;
	ULONG ulANr = 0;
	static constexpr const SizedSPropTagArray(10, sExcludeFromCopyForward) = {10, {
		PR_TRANSPORT_MESSAGE_HEADERS,
		PR_SENT_REPRESENTING_ENTRYID,
		PR_SENT_REPRESENTING_NAME,
		PR_SENT_REPRESENTING_ADDRTYPE,
		PR_SENT_REPRESENTING_EMAIL_ADDRESS,
		PR_SENT_REPRESENTING_SEARCH_KEY,
		PR_READ_RECEIPT_REQUESTED,
		PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED,
		PR_MESSAGE_FLAGS,
		PR_MESSAGE_RECIPIENTS, // This must be the last entry, see bDoNotMunge
	} };
	static constexpr const SizedSPropTagArray (3, sExcludeFromCopyRedirect) = {3, {
		PR_TRANSPORT_MESSAGE_HEADERS,
		PR_MESSAGE_FLAGS,
		PR_MESSAGE_RECIPIENTS, // This must be the last entry, see bDoNotMunge
	} };
	static constexpr const SizedSPropTagArray(1, sExcludeFromAttachedForward) =
		{1, {PR_TRANSPORT_MESSAGE_HEADERS}};
	SPropValue sForwardProps[5];
	wstring strSubject;

	if (lpRecipients == NULL || lpRecipients->cEntries == 0) {
		ec_log_crit("No rule recipient");
		return MAPI_E_INVALID_PARAMETER;
	}

	auto hr = CheckRecipients(lpAdrBook, lpOrigStore, lpOrigMessage, lpRecipients,
	          bOpDelegate, bDoNotMunge, &~filtered_recips);
	if (hr == MAPI_E_NO_ACCESS) {
		ec_log_info("K-1904: Forwarding not permitted. Ending rule processing.");
		return hr;
	}
	if (hr == MAPI_E_UNABLE_TO_COMPLETE)
		return hr;
	if (hr == hrSuccess)
		lpRecipients = filtered_recips.get();
	hr = HrGetOneProp(lpOrigStore, PR_IPM_SENTMAIL_ENTRYID, &~lpSentMailEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = CreateOutboxMessage(lpOrigStore, &~lpFwdMsg);
	if (hr != hrSuccess)
		return hr;
	// If we're doing a redirect, copy over the original PR_SENT_REPRESENTING_*, otherwise don't
	hr = Util::HrCopyPropTagArray(bDoPreserveSender ? sExcludeFromCopyRedirect : sExcludeFromCopyForward, &~lpExclude);
	if (hr != hrSuccess)
		return hr;

    if(bDoNotMunge) {
        // The idea here is to enable 'resend' mode and to include the original recipient list. What will
        // happen is that the original recipient list will be used to generate the headers of the message, but
        // only the MAPI_P1 recipients will be used to send the message to. This is exactly what we want. So
        // with bDoNotMunge, we copy the original recipient from the original message, and set MSGFLAG_RESEND.
        // Later on, we set the actual recipient to MAPI_P1
        SPropValue sPropResend;
        sPropResend.ulPropTag = PR_MESSAGE_FLAGS;
        sPropResend.Value.ul = MSGFLAG_UNSENT | MSGFLAG_RESEND | MSGFLAG_READ;
		--lpExclude->cValues; // strip PR_MESSAGE_RECIPIENTS, since original recipients should be used
        hr = HrSetOneProp(lpFwdMsg, &sPropResend);
        if(hr != hrSuccess)
		return hr;
    }

	if (bForwardAsAttachment) {
		object_ptr<IAttach> lpAttach;
		object_ptr<IMessage> lpAttachMsg;

		hr = lpFwdMsg->CreateAttach(nullptr, 0, &ulANr, &~lpAttach);
		if (hr != hrSuccess)
			return hr;

		SPropValue sAttachMethod;
		sAttachMethod.ulPropTag = PR_ATTACH_METHOD;
		sAttachMethod.Value.ul = ATTACH_EMBEDDED_MSG;

		hr = lpAttach->SetProps(1, &sAttachMethod, NULL);
		if (hr != hrSuccess)
			return hr;
		hr = lpAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY, &~lpAttachMsg);
		if (hr != hrSuccess)
			return hr;
		hr = lpOrigMessage->CopyTo(0, NULL, sExcludeFromAttachedForward,
		     0, NULL, &IID_IMessage, lpAttachMsg, 0, NULL);
		if (hr != hrSuccess)
			return hr;
		hr = lpAttachMsg->SaveChanges(0);
		if (hr != hrSuccess)
			return hr;
		hr = lpAttach->SaveChanges(0);
		if (hr != hrSuccess)
			return hr;
	}
	else {
		hr = lpOrigMessage->CopyTo(0, NULL, lpExclude, 0, NULL, &IID_IMessage, lpFwdMsg, 0, NULL);
		if (hr != hrSuccess)
			return hr;
	}

	hr = lpFwdMsg->ModifyRecipients(MODRECIP_ADD, lpRecipients);
	if (hr != hrSuccess)
		return hr;
	// set from email ??
	hr = HrGetOneProp(lpOrigMessage, PR_SUBJECT, &~lpOrigSubject);
	if (hr == hrSuccess)
		strSubject = lpOrigSubject->Value.lpszW;
	if(!bDoNotMunge || bForwardAsAttachment)
		strSubject.insert(0, L"FW: ");

	ULONG cfp = 0;
	sForwardProps[cfp].ulPropTag = PR_AUTO_FORWARDED;
	sForwardProps[cfp++].Value.b = TRUE;
	sForwardProps[cfp].ulPropTag = PR_SUBJECT;
	sForwardProps[cfp++].Value.lpszW = (WCHAR*)strSubject.c_str();
	sForwardProps[cfp].ulPropTag = PR_SENTMAIL_ENTRYID;
	sForwardProps[cfp++].Value.bin = lpSentMailEntryID->Value.bin;
	if (bForwardAsAttachment) {
		sForwardProps[cfp].ulPropTag = PR_MESSAGE_CLASS;
		sForwardProps[cfp++].Value.lpszW = const_cast<wchar_t *>(L"IPM.Note");
	}

	if (parseBool(g_lpConfig->GetSetting("set_rule_headers", NULL, "yes"))) {
		PROPMAP_START(1)
		PROPMAP_NAMED_ID(KopanoRuleAction, PT_UNICODE, PS_INTERNET_HEADERS, "x-kopano-rule-action")
		PROPMAP_INIT(lpFwdMsg);

		sForwardProps[cfp].ulPropTag = PROP_KopanoRuleAction;
		sForwardProps[cfp++].Value.lpszW = LPWSTR(bDoPreserveSender ? L"redirect" : L"forward");
	}

	hr = lpFwdMsg->SetProps(cfp, sForwardProps, NULL);
	if (hr != hrSuccess)
		return hr;

	if (!bDoNotMunge && !bForwardAsAttachment) {
		// because we're forwarding this as a new message, clear the old received message id
		static constexpr const SizedSPropTagArray(1, sptaDeleteProps) =
			{1, {PR_INTERNET_MESSAGE_ID}};
		hr = lpFwdMsg->DeleteProps(sptaDeleteProps, NULL);
		if(hr != hrSuccess)
			return hr;
		MungeForwardBody(lpFwdMsg, lpOrigMessage);
	}
	*lppMessage = lpFwdMsg.release();
	return hr;
}

// HRESULT HrDelegateMessage(LPMAPISESSION lpSession, LPEXCHANGEMANAGESTORE lpIEMS, IMAPIProp *lpMessage, LPADRENTRY lpAddress)
static HRESULT HrDelegateMessage(IMAPIProp *lpMessage)
{
	SPropValue sNewProps[6] = {{0}};
	memory_ptr<SPropValue> lpProps;
	ULONG cValues = 0;
	static constexpr const SizedSPropTagArray(5, sptaRecipProps) =
		{5, {PR_RECEIVED_BY_ENTRYID, PR_RECEIVED_BY_ADDRTYPE,
		PR_RECEIVED_BY_EMAIL_ADDRESS, PR_RECEIVED_BY_NAME,
		PR_RECEIVED_BY_SEARCH_KEY}};
	static constexpr SizedSPropTagArray(1, sptaSentMail) =
		{1, {PR_SENTMAIL_ENTRYID}};

	// set PR_RCVD_REPRESENTING on original receiver
	auto hr = lpMessage->GetProps(sptaRecipProps, 0, &cValues, &~lpProps);
	if (hr != hrSuccess)
		return hr;

	lpProps[0].ulPropTag = PR_RCVD_REPRESENTING_ENTRYID;
	lpProps[1].ulPropTag = PR_RCVD_REPRESENTING_ADDRTYPE;
	lpProps[2].ulPropTag = PR_RCVD_REPRESENTING_EMAIL_ADDRESS;
	lpProps[3].ulPropTag = PR_RCVD_REPRESENTING_NAME;
	lpProps[4].ulPropTag = PR_RCVD_REPRESENTING_SEARCH_KEY;

	hr = lpMessage->SetProps(cValues, lpProps, NULL);
	if (hr != hrSuccess)
		return hr;

	// TODO: delete PR_RECEIVED_BY_ values?
	sNewProps[0].ulPropTag = PR_DELEGATED_BY_RULE;
	sNewProps[0].Value.b = TRUE;
	sNewProps[1].ulPropTag = PR_DELETE_AFTER_SUBMIT;
	sNewProps[1].Value.b = TRUE;
	hr = lpMessage->SetProps(2, sNewProps, NULL);
	if (hr != hrSuccess)
		return hr;

	// Don't want to move to sent mail
	hr = lpMessage->DeleteProps(sptaSentMail, NULL);
	if (hr != hrSuccess)
		return hr;
	return lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
}

static struct actresult proc_op_copy(IMAPISession *ses, const ACTION &action,
    const std::string &rule, StatsClient *sc, IMessage **msg)
{
	const auto &cmov = action.actMoveCopy;
	sc->inc(SCN_RULES_COPYMOVE);
	if (action.acttype == OP_COPY)
		ec_log_debug("Rule action: copying e-mail");
	else
		ec_log_debug("Rule action: moving e-mail");

	// First try to open the folder on the session as that will just work if we have the store open
	object_ptr<IMAPIFolder> dst_folder;
	unsigned int obj_type;
	auto hr = ses->OpenEntry(cmov.cbFldEntryId, cmov.lpFldEntryId,
	          &IID_IMAPIFolder, MAPI_MODIFY, &obj_type, &~dst_folder);
	if (hr != hrSuccess) {
		ec_log_info("Rule \"%s\": Unable to open folder through session, trying through store: %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		object_ptr<IMsgStore> dst_store;
		hr = ses->OpenMsgStore(0, cmov.cbStoreEntryId,
		     cmov.lpStoreEntryId, nullptr, MAPI_BEST_ACCESS, &~dst_store);
		if (hr != hrSuccess) {
			ec_log_err("Rule \"%s\": Unable to open destination store: %s (%x)",
				rule.c_str(), GetMAPIErrorMessage(hr), hr);
			return {ROP_ERROR, hr};
		}
		hr = dst_store->OpenEntry(cmov.cbFldEntryId, cmov.lpFldEntryId,
		     &IID_IMAPIFolder, MAPI_MODIFY, &obj_type, &~dst_folder);
		if (hr != hrSuccess || obj_type != MAPI_FOLDER) {
			ec_log_err("Rule \"%s\": Unable to open destination folder: %s (%x)",
				rule.c_str(), GetMAPIErrorMessage(hr), hr);
			return {ROP_ERROR, hr};
		}
	}

	object_ptr<IMessage> newmsg;
	hr = dst_folder->CreateMessage(nullptr, 0, &~newmsg);
	if (hr != hrSuccess) {
		ec_log_err("Unable to create e-mail for rule \"%s\": %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_FAILURE, hr};
	}
	hr = (*msg)->CopyTo(0, nullptr, nullptr, 0, nullptr, &IID_IMessage,
	     newmsg, 0, nullptr);
	if (hr != hrSuccess) {
		ec_log_err("Unable to copy e-mail for rule \"%s\": %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_FAILURE, hr};
	}
	hr = Util::HrCopyIMAPData(*msg, newmsg);
	// the function only returns errors on get/setprops, not when the data is just missing
	if (hr != hrSuccess) {
		ec_log_err("Unable to copy IMAP data e-mail for rule \"%s\", continuing: %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_FAILURE, hr};
	}
	/* Save the copy in its new location */
	hr = newmsg->SaveChanges(0);
	if (hr != hrSuccess) {
		ec_log_err("Rule \"%s\": Unable to copy/move message: %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_ERROR, hr};
	}
	return {ROP_SUCCESS};
}

static struct actresult proc_op_reply(IMAPISession *ses, IMsgStore *store,
    IMAPIFolder *inbox, const ACTION &action, const std::string &rule,
    StatsClient *sc, IMessage **msg)
{
	const auto &repl = action.actReply;
	sc->inc(SCN_RULES_REPLY_AND_OOF);

	memory_ptr<SPropValue> pv;
	if (HrGetOneProp(*msg, PR_TRANSPORT_MESSAGE_HEADERS_A, &~pv) == hrSuccess &&
	    dagent_avoid_autoreply(tokenize(pv->Value.lpszA, "\n"))) {
		ec_log_warn("Rule \""s + rule + "\": Not replying to an autoreply");
		return {ROP_NOOP};
	}
	if (action.acttype == OP_REPLY)
		ec_log_debug("Rule action: replying e-mail");
	else
		ec_log_debug("Rule action: OOF replying e-mail");

	IMessage *tmpl = nullptr;
	unsigned int objtype;
	auto hr = inbox->OpenEntry(repl.cbEntryId, repl.lpEntryId,
	          &IID_IMessage, 0, &objtype, reinterpret_cast<IUnknown **>(&tmpl));
	if (hr != hrSuccess) {
		ec_log_err("Rule \"%s\": Unable to open reply message: %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_ERROR, hr};
	}
	object_ptr<IMessage> replymsg;
	hr = CreateReplyCopy(ses, store, *msg, tmpl, &~replymsg);
	if (hr != hrSuccess) {
		ec_log_err("Rule \"%s\": Unable to create reply message: %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_ERROR, hr};
	}
	hr = replymsg->SubmitMessage(0);
	if (hr != hrSuccess) {
		ec_log_err("Rule \"%s\": Unable to send reply message: %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_ERROR, hr};
	}
	return {ROP_SUCCESS};
}

static struct actresult proc_op_fwd(IAddrBook *abook, IMsgStore *orig_store,
    const ACTION &act, const std::string &rule, StatsClient *sc,
    IMessage **lppMessage)
{
	object_ptr<IMessage> lpFwdMsg;

	sc->inc(SCN_RULES_FORWARD);
	// TODO: test act.lpAction[n].ulActionFlavor
	// FWD_PRESERVE_SENDER			1
	// FWD_DO_NOT_MUNGE_MSG			2
	// FWD_AS_ATTACHMENT			4
	// redirect == 3
	if (act.lpadrlist->cEntries == 0) {
		ec_log_debug("Forwarding rule doesn't have recipients");
		return {ROP_NOOP};
	}
	memory_ptr<SPropValue> pv;
	if (HrGetOneProp(*lppMessage, PR_TRANSPORT_MESSAGE_HEADERS_A, &~pv) == hrSuccess &&
	    dagent_avoid_autoreply(tokenize(pv->Value.lpszA, "\n"))) {
		ec_log_warn("Rule \""s + rule + "\": Not forwarding autoreplies");
		return {ROP_NOOP};
	}
	if (parseBool(g_lpConfig->GetSetting("no_double_forward"))) {
		/*
		 * Loop protection. When header is added to the message, it
		 * will stop to forward or redirect the message.
		 */
		PROPMAP_START(1)
		PROPMAP_NAMED_ID(KopanoRuleAction, PT_UNICODE, PS_INTERNET_HEADERS, "x-kopano-rule-action")
		auto hr = m_propmap.Resolve(*lppMessage);
		if (hr != hrSuccess)
			return {ROP_FAILURE, hr};
		if (HrGetOneProp(*lppMessage, PROP_KopanoRuleAction, &~pv) == hrSuccess) {
			ec_log_warn("Rule "s + rule + ": FORWARD loop protection. Message will not be forwarded or redirected because it includes header \"x-kopano-rule-action\"");
			return {ROP_NOOP};
		}
	}
	ec_log_debug("Rule action: %s e-mail", (act.ulActionFlavor & FWD_PRESERVE_SENDER) ? "redirecting" : "forwarding");
	auto hr = CreateForwardCopy(abook, orig_store, *lppMessage,
	     act.lpadrlist, false, act.ulActionFlavor & FWD_PRESERVE_SENDER,
	     act.ulActionFlavor & FWD_DO_NOT_MUNGE_MSG,
	     act.ulActionFlavor & FWD_AS_ATTACHMENT, &~lpFwdMsg);
	if (hr != hrSuccess) {
		auto msg = "Rule " + rule + ": FORWARD Unable to create forward message: %s (%x)";
		ec_log_err(msg.c_str(), GetMAPIErrorMessage(hr), hr);
		return {hr == MAPI_E_NO_ACCESS ? ROP_FAILURE : ROP_ERROR, hr};
	}
	hr = lpFwdMsg->SubmitMessage(0);
	if (hr != hrSuccess) {
		auto msg = "Rule " + rule + ": FORWARD Unable to send forward message: %s (%x)";
		ec_log_err(msg.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_ERROR, hr};
	}
	return {ROP_SUCCESS};
}

static struct actresult proc_op_delegate(IAddrBook *abk, IMsgStore *store,
    const ACTION &action, const std::string &rule, StatsClient *sc,
    IMessage **msg)
{
	sc->inc(SCN_RULES_DELEGATE);
	if (action.lpadrlist->cEntries == 0) {
		ec_log_debug("Delegating rule doesn't have recipients");
		return {ROP_NOOP};
	}
	ec_log_debug("Rule action: delegating e-mail");
	object_ptr<IMessage> fwdmsg;
	auto hr = CreateForwardCopy(abk, store, *msg, action.lpadrlist,
	          true, true, true, false, &~fwdmsg);
	if (hr != hrSuccess) {
		ec_log_err("Rule \"%s\": DELEGATE Unable to create delegate message: %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_ERROR, hr};
	}
	/* set delegate properties */
	hr = HrDelegateMessage(fwdmsg);
	if (hr != hrSuccess) {
		ec_log_err("Rule \"%s\": DELEGATE Unable to modify delegate message: %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_ERROR, hr};
	}
	hr = fwdmsg->SubmitMessage(0);
	if (hr != hrSuccess) {
		ec_log_err("Rule \"%s\": DELEGATE Unable to send delegate message: %s (%x)",
			rule.c_str(), GetMAPIErrorMessage(hr), hr);
		return {ROP_ERROR, hr};
	}
	/* don't set forwarded flag */
	return {ROP_SUCCESS};
}

static struct actresult proc_op_markread(IMessage *msg,
    StatsClient *sc)
{
	sc->inc(SCN_RULES_MARKREAD);
	auto ret = msg->SetReadFlag(SUPPRESS_RECEIPT);
	if (ret == hrSuccess)
		return {ROP_SUCCESS};
	return {ROP_ERROR, ret};
}

static struct actresult proc_op_act(IMAPISession *ses, IMsgStore *store,
    IMAPIFolder *inbox, IAddrBook *abk, const ACTION &action,
    const std::string &rule, StatsClient *sc, IMessage **msg)
{
	switch (action.acttype) {
	case OP_MOVE:
	case OP_COPY: {
		auto ret = proc_op_copy(ses, action, rule, sc, msg);
		if (ret.status == ROP_SUCCESS && action.acttype == OP_MOVE)
			return {ROP_MOVED};
		return ret;
	}
	case OP_REPLY:
	case OP_OOF_REPLY:
		return proc_op_reply(ses, store, inbox, action, rule, sc, msg);
	case OP_FORWARD: {
		auto ret = proc_op_fwd(abk, store, action, rule, sc, msg);
		if (ret.status == ROP_SUCCESS)
			/* Update original message, set as forwarded */
			return {ROP_FORWARDED};
		return ret;
	}
	case OP_BOUNCE:
		sc->inc(SCN_RULES_BOUNCE);
		/*
		 * scBounceCode?
		 * TODO:
		 * 1. make copy of lpMessage, needs CopyTo() function
		 * 2. copy From: to To:
		 * 3. SubmitMessage()
		 */
		ec_log_warn("Rule \"%s\": BOUNCE actions are currently unsupported", rule.c_str());
		break;
	case OP_DELEGATE:
		return proc_op_delegate(abk, store, action, rule, sc, msg);

	case OP_DEFER_ACTION:
		sc->inc(SCN_RULES_DEFER);
		ec_log_warn("Rule \"%s\": DEFER client actions are currently unsupported", rule.c_str());
		break;
	case OP_TAG:
		sc->inc(SCN_RULES_TAG);
		ec_log_warn("Rule \"%s\": TAG actions are currently unsupported", rule.c_str());
		break;
	case OP_DELETE:
		sc->inc(SCN_RULES_DELETE);
		/*
		 * Since *msg wasn't yet saved in the server, we can just
		 * return a special MAPI Error code here, this will trigger the
		 * out-of-office mail (according to microsoft), but not save
		 * the message and drop it. The error code will become
		 * hrSuccess automatically after returning from the post
		 * processing function.
		 */
		ec_log_debug("Rule action: deleting e-mail");
		return {ROP_CANCEL};
	case OP_MARK_AS_READ:
		ec_log_debug("Rule action: mark as read");
		return proc_op_markread(*msg, sc);
	}
	return {ROP_SUCCESS};
}

// lpMessage: gets EntryID, maybe pass this and close message in DAgent.cpp
HRESULT HrProcessRules(const std::string &recip, pym_plugin_intf *pyMapiPlugin,
    IMAPISession *lpSession, IAddrBook *lpAdrBook, IMsgStore *lpOrigStore,
    IMAPIFolder *lpOrigInbox, IMessage **lppMessage, StatsClient *const sc)
{
	object_ptr<IExchangeModifyTable> lpTable;
	object_ptr<IMAPITable> lpView;
	bool bAddFwdFlag = false, bMoved = false, bOOFactive = false;
	static constexpr const SizedSPropTagArray(11, sptaRules) =
		{11, {PR_RULE_ID, PR_RULE_IDS, PR_RULE_SEQUENCE, PR_RULE_STATE,
		PR_RULE_USER_FLAGS, PR_RULE_CONDITION, PR_RULE_ACTIONS,
		PR_RULE_PROVIDER, CHANGE_PROP_TYPE(PR_RULE_NAME, PT_STRING8),
		PR_RULE_LEVEL, PR_RULE_PROVIDER_DATA}};
	static constexpr const SizedSSortOrderSet(1, sosRules) =
		{1, 0, 0, {{PR_RULE_SEQUENCE, TABLE_SORT_ASCEND}}};
	std::string strRule;
	LPSRestriction lpCondition = NULL;
	ACTIONS* lpActions = NULL;
	memory_ptr<SPropValue> OOFProps;
	unsigned int cValues, ulResult = 0;
	SPropValue sForwardProps[4];
	object_ptr<IECExchangeModifyTable> lpECModifyTable;

	sc->inc(SCN_RULES_INVOKES);
	auto hr = lpOrigInbox->OpenProperty(PR_RULES_TABLE, &IID_IExchangeModifyTable, 0, 0, &~lpTable);
	if (hr != hrSuccess) {
		kc_perrorf("OpenProperty failed", hr);
		goto exit;
	}
	hr = lpTable->QueryInterface(IID_IECExchangeModifyTable, &~lpECModifyTable);
	if(hr != hrSuccess) {
		kc_perrorf("QueryInterface failed", hr);
		goto exit;
	}
	hr = lpECModifyTable->DisablePushToServer();
	if(hr != hrSuccess) {
		kc_perrorf("DisablePushToServer failed", hr);
		goto exit;
	}
	hr = pyMapiPlugin->RulesProcessing("PreRuleProcess", lpSession, lpAdrBook, lpOrigStore, lpTable, &ulResult);
	if(hr != hrSuccess) {
		kc_perrorf("RulesProcessing failed", hr);
		goto exit;
	}

	// get OOF-state for recipient-store
	static constexpr const SizedSPropTagArray(5, sptaStoreProps) = {3, {PR_EC_OUTOFOFFICE, PR_EC_OUTOFOFFICE_FROM, PR_EC_OUTOFOFFICE_UNTIL,}};
	hr = lpOrigStore->GetProps(sptaStoreProps, 0, &cValues, &~OOFProps);
	if (FAILED(hr)) {
		ec_log_err("lpOrigStore->GetProps failed: %s (%x) - OOF-state unavailable",
			GetMAPIErrorMessage(hr), hr);
	} else {
		bOOFactive = OOFProps[0].ulPropTag == PR_EC_OUTOFOFFICE && OOFProps[0].Value.b;

		if (bOOFactive) {
			time_t now = time(nullptr);
			if (OOFProps[1].ulPropTag == PR_EC_OUTOFOFFICE_FROM)
				bOOFactive &= FileTimeToUnixTime(OOFProps[1].Value.ft) <= now;
			if (OOFProps[2].ulPropTag == PR_EC_OUTOFOFFICE_UNTIL)
				bOOFactive &= now <= FileTimeToUnixTime(OOFProps[2].Value.ft);
		}
	}

	//TODO do something with ulResults
	hr = lpTable->GetTable(0, &~lpView);
	if(hr != hrSuccess) {
		kc_perrorf("GetTable failed", hr);
		goto exit;
	}
	hr = lpView->SetColumns(sptaRules, 0);
	if (hr != hrSuccess) {
		kc_perrorf("SetColumns failed", hr);
		goto exit;
	}
	hr = lpView->SortTable(sosRules, 0);
	if (hr != hrSuccess) {
		kc_perrorf("SortTable failed", hr);
		goto exit;
	}

	while (1) {
		rowset_ptr lpRowSet;
	        hr = lpView->QueryRows(1, 0, &~lpRowSet);
		if (hr != hrSuccess) {
			kc_perrorf("QueryRows failed", hr);
				goto exit;
		}
	        if (lpRowSet->cRows == 0)
			break;

		sc->inc(SCN_RULES_NRULES, static_cast<int64_t>(lpRowSet->cRows));
		auto lpRuleName = lpRowSet[0].cfind(CHANGE_PROP_TYPE(PR_RULE_NAME, PT_STRING8));
		if (lpRuleName)
			strRule = lpRuleName->Value.lpszA;
		else
			strRule = "(no name)";

		ec_log_debug("Processing rule \"%s\" for \"%s\"", strRule.c_str(), recip.c_str());
		auto lpRuleState = lpRowSet[0].cfind(PR_RULE_STATE);
		if (lpRuleState == nullptr) {
			ec_log_warn("Rule '%s' for '%s' skipped, having no PR_RULE_STATE property.", strRule.c_str(), recip.c_str());
			continue;
		}
		if (!(lpRuleState->Value.i & ST_ENABLED)) {
			ec_log_debug("Rule '%s' is disabled, skipping...", strRule.c_str());
			continue;
		}
		if ((lpRuleState->Value.i & ST_ONLY_WHEN_OOF) && !bOOFactive) {
			ec_log_debug("Rule '%s' active, but doesn't apply (OOF-state == false), skipping...", strRule.c_str());
			continue;
		}

		lpCondition = NULL;
		lpActions = NULL;
		auto lpProp = lpRowSet[0].cfind(PR_RULE_CONDITION);
		if (lpProp)
			// NOTE: object is placed in Value.lpszA, not Value.x
			lpCondition = (LPSRestriction)lpProp->Value.lpszA;
		if (!lpCondition) {
			ec_log_debug("Rule \"%s\" has no condition, skipping...", strRule.c_str());
			continue;
		}
		lpProp = lpRowSet[0].cfind(PR_RULE_ACTIONS);
		if (lpProp)
			// NOTE: object is placed in Value.lpszA, not Value.x
			lpActions = (ACTIONS*)lpProp->Value.lpszA;
		if (!lpActions) {
			ec_log_debug("Rule '%s' has no action, skipping...", strRule.c_str());
			continue;
		}

		// test if action should be done...
		// @todo: Create the correct locale for the current store.
		hr = TestRestriction(lpCondition, *lppMessage, createLocaleFromName(""));
		if (hr != hrSuccess) {
			ec_log_info("Rule \"%s\" does not match: %s (%x)", strRule.c_str(),
				GetMAPIErrorMessage(hr), hr);
			continue;
		}
		ec_log_info("Rule "s + strRule + " matches");
		sc->inc(SCN_RULES_NACTIONS, static_cast<int64_t>(lpActions->cActions));

		for (ULONG n = 0; n < lpActions->cActions; ++n) {
			const auto &action = lpActions->lpAction[n];
			auto ret = proc_op_act(lpSession, lpOrigStore, lpOrigInbox, lpAdrBook, action, strRule, sc, lppMessage);
			if (ret.status == ROP_FAILURE) {
				hr = ret.code;
				goto exit;
			} else if (ret.status == ROP_CANCEL) {
				hr = MAPI_E_CANCEL;
				goto exit;
			}
			if (ret.status == ROP_MOVED)
				bMoved = true;
			if (ret.status == ROP_FORWARDED)
				bAddFwdFlag = true;
		} // end action loop

		if (lpRuleState && (lpRuleState->Value.i & ST_EXIT_LEVEL))
			break;
	}

	if (bAddFwdFlag) {
		sForwardProps[0].ulPropTag = PR_ICON_INDEX;
		sForwardProps[0].Value.ul = ICON_MAIL_FORWARDED;
		sForwardProps[1].ulPropTag = PR_LAST_VERB_EXECUTED;
		sForwardProps[1].Value.ul = NOTEIVERB_FORWARD;
		sForwardProps[2].ulPropTag = PR_LAST_VERB_EXECUTION_TIME;
		GetSystemTimeAsFileTime(&sForwardProps[2].Value.ft);
		// set forward in msg flag
		hr = (*lppMessage)->SetProps(3, sForwardProps, NULL);
	}
 exit:
	if (hr != hrSuccess && hr != MAPI_E_CANCEL)
		kc_perror("Error while processing rules", hr);
	// The message was moved to another folder(s), do not save it in the inbox anymore, so cancel it.
	if (hr == hrSuccess && bMoved)
		hr = MAPI_E_CANCEL;
	if (hr != hrSuccess)
		sc->inc(SCN_RULES_INVOKES_FAIL);
	return hr;
}
