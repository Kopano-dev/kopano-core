/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include "mailer.h"
#include "archive.h"
#include <mapitags.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include <mapiutil.h>
#include <mapidefs.h>
#include <mapix.h>
#include <mapi.h>
#include <kopano/ECLogger.h>
#include <kopano/ECRestriction.h>
#include <kopano/ECConfig.h>
#include <kopano/ecversion.h>
#include <kopano/MAPIErrors.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include <kopano/stringutil.h>
#include "TimeUtil.h"
#include "mapicontact.h"
#include <kopano/mapiguidext.h>
#include <kopano/EMSAbTag.h>
#include <kopano/ECABEntryID.h>
#include <kopano/ECGetText.h>
#include <kopano/charset/convert.h>
#include <kopano/charset/convstring.h>
#include "PyMapiPlugin.h"
#include <list>
#include <algorithm>
#include "fileutil.h"

using namespace KC;
using std::list;
using std::string;
using std::wstring;
extern std::shared_ptr<ECConfig> g_lpConfig;

/**
 * Expand all rows in the lpTable to normal user recipient
 * entries. When a group is expanded from a group, this function will
 * be called recursively.
 *
 * @param[in]	lpAddrBook	The Global Addressbook of the user sending lpMessage
 * @param[in]	lpMessage	The message to expand groups for
 * @param[in]	lpTable		The restricted recipient table of lpMessage
 * @param[in]	lpEntryRestriction The restriction used on lpTable
 * @param[in]	ulRecipType	The recipient type (To/Cc/Bcc), default is MAPI_TO
 * @param[in]	lpExpandedGroups	List of EntryIDs of groups already expanded. Double groups will just be removed.
 * @param[in]	recurrence	true if this function should recurse further.
 */
static HRESULT ExpandRecipientsRecursive(LPADRBOOK lpAddrBook,
    IMessage *lpMessage, IMAPITable *lpTable,
    LPSRestriction lpEntryRestriction, ULONG ulRecipType,
    list<SBinary> *lpExpandedGroups, bool recurrence = true)
{
	ULONG			ulObj = 0;
	bool			bExpandSub = recurrence;
	static constexpr const SizedSPropTagArray(7, sptaColumns) =
		{7, {PR_ROWID, PR_DISPLAY_NAME_W, PR_SMTP_ADDRESS_W,
		PR_RECIPIENT_TYPE, PR_OBJECT_TYPE, PR_DISPLAY_TYPE, PR_ENTRYID}};

	auto hr = lpTable->SetColumns(sptaColumns, 0);
	if (hr != hrSuccess)
		return kc_perrorf("SetColumns failed", hr);

	while (true) {
		memory_ptr<SPropValue> lpSMTPAddress;
		rowset_ptr lpsRowSet;
		/* Request group from table */
		hr = lpTable->QueryRows(1, 0, &~lpsRowSet);
		if (hr != hrSuccess)
			return kc_perrorf("QueryRows failed", hr);
		if (lpsRowSet->cRows != 1)
			break;

		/* From this point on we use 'continue' when something fails,
		 * since all errors are related to the current entry and we should
		 * make sure we resolve as many recipients as possible. */
		auto lpRowId = lpsRowSet[0].cfind(PR_ROWID);
		auto lpEntryId = lpsRowSet[0].cfind(PR_ENTRYID);
		auto lpDisplayType = lpsRowSet[0].cfind(PR_DISPLAY_TYPE);
		auto lpObjectType = lpsRowSet[0].cfind(PR_OBJECT_TYPE);
		auto lpRecipType = lpsRowSet[0].cfind(PR_RECIPIENT_TYPE);
		auto lpDisplayName = lpsRowSet[0].cfind(PR_DISPLAY_NAME_W);
		auto lpEmailAddress = lpsRowSet[0].cfind(PR_SMTP_ADDRESS_W);

		/* lpRowId, lpRecipType, and lpDisplayType are optional.
		 * lpEmailAddress is only mandatory for MAPI_MAILUSER */
		if (!lpEntryId || !lpObjectType || !lpDisplayName)
			continue;
		/* By default we inherit the recipient type from parent */
		if (lpRecipType)
			ulRecipType = lpRecipType->Value.ul;

		if (lpObjectType->Value.ul == MAPI_MAILUSER) {
			if (!lpEmailAddress)
				continue;

			SizedADRLIST(1, sRowSMTProwSet);
			SPropValue p[4];

			sRowSMTProwSet.cEntries = 1;
			sRowSMTProwSet.aEntries[0].cValues = 4;
			sRowSMTProwSet.aEntries[0].rgPropVals = p;
			p[0].ulPropTag = PR_EMAIL_ADDRESS_W;
			p[0].Value.lpszW = lpEmailAddress->Value.lpszW;
			p[1].ulPropTag = PR_SMTP_ADDRESS_W;
			p[1].Value.lpszW = lpEmailAddress->Value.lpszW;
			p[2].ulPropTag = PR_RECIPIENT_TYPE;
			p[2].Value.ul = ulRecipType; /* Inherit from parent group */
			p[3].ulPropTag = PR_DISPLAY_NAME_W;
			p[3].Value.lpszW = lpDisplayName->Value.lpszW;
			hr = lpMessage->ModifyRecipients(MODRECIP_ADD, sRowSMTProwSet);
			if (hr != hrSuccess)
				ec_log_err("Unable to add e-mail address of %ls from group: %s (%x)",
					lpEmailAddress->Value.lpszW, GetMAPIErrorMessage(hr), hr);
			continue;
		}

		SBinary sEntryId;
		object_ptr<IMAPITable> lpContentsTable;
		object_ptr<IDistList> lpDistlist;

		/* If we should recur further, just remove the group from the recipients list */
		if (!recurrence)
			goto remove_group;

		/* Only continue when this group has not yet been expanded previously */
		if (find(lpExpandedGroups->begin(), lpExpandedGroups->end(), lpEntryId->Value.bin) != lpExpandedGroups->end())
			goto remove_group;
		hr = lpAddrBook->OpenEntry(lpEntryId->Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(lpEntryId->Value.bin.lpb),
		     &iid_of(lpDistlist), 0, &ulObj, &~lpDistlist);
		if (hr != hrSuccess)
			continue;
		if (ulObj != MAPI_DISTLIST)
			continue;

		/* Never expand groups with an email address. The whole point of the email address is that it can be used
		 * as a single entity */
		if (HrGetOneProp(lpDistlist, PR_SMTP_ADDRESS_W, &~lpSMTPAddress) == hrSuccess &&
		    wcslen(lpSMTPAddress->Value.lpszW) > 0)
			continue;
		hr = lpDistlist->GetContentsTable(MAPI_UNICODE, &~lpContentsTable);
		if (hr != hrSuccess)
			continue;
		hr = lpContentsTable->Restrict(lpEntryRestriction, 0);
		if (hr != hrSuccess)
			continue;

		/* Group has been expanded (because we successfully have the contents table) time
		 * to add it to our expanded group list. This has to be done or at least before the
		 * recursive call to ExpandRecipientsRecursive().*/
		hr = Util::HrCopyEntryId(lpEntryId->Value.bin.cb, (LPENTRYID)lpEntryId->Value.bin.lpb,
		     &sEntryId.cb, (LPENTRYID *)&sEntryId.lpb);
		lpExpandedGroups->emplace_back(sEntryId);

		/* Don't expand group Everyone or companies since both already contain all users
		 * which should be put in the recipient list. */
		bExpandSub = !(((lpDisplayType) ? lpDisplayType->Value.ul == DT_ORGANIZATION : false) ||
					   wcscasecmp(lpDisplayName->Value.lpszW, L"Everyone") == 0);
		// @todo find everyone using it's static entryid?

		/* Start/Continue recursion */
		hr = ExpandRecipientsRecursive(lpAddrBook, lpMessage, lpContentsTable,
		     lpEntryRestriction, ulRecipType, lpExpandedGroups, bExpandSub);
		/* Ignore errors */
remove_group:
		/* Only delete row when the rowid is present */
		if (!lpRowId)
			continue;

		hr = lpMessage->ModifyRecipients(MODRECIP_REMOVE,
		     reinterpret_cast<ADRLIST *>(lpsRowSet.get()));
		if (hr != hrSuccess) {
			ec_log_err("Unable to remove group %ls from recipient list: %s (%x).",
				lpDisplayName->Value.lpszW, GetMAPIErrorMessage(hr), hr);
			continue;
		}
	}
	return hrSuccess;
}

/**
 * Expands groups in normal recipients.
 *
 * This function builds the restriction, and calls the recursion
 * function, since we can have group-in-groups.
 *
 * @todo use restriction macros for readability.
 *
 * @param[in]	lpAddrBook	The Global Addressbook of the user sending lpMessage
 * @param[in]	lpMessage	The message to expand groups for.
 * @return		HRESULT
 */
static HRESULT ExpandRecipients(LPADRBOOK lpAddrBook, IMessage *lpMessage)
{
	object_ptr<IMAPITable> lpTable;
	memory_ptr<SRestriction> lpRestriction, lpEntryRestriction;
	/*
	 * Setup group restriction:
	 * PR_OBJECT_TYPE == MAPI_DISTLIST && PR_ADDR_TYPE == "ZARAFA"
	 */
	auto hr = MAPIAllocateBuffer(sizeof(SRestriction), &~lpRestriction);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffer failed", hr);
	hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRestriction, (LPVOID*)&lpRestriction->res.resAnd.lpRes);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateMore failed(1)", hr);
	lpRestriction->rt = RES_AND;
	lpRestriction->res.resAnd.cRes = 2;

	hr = MAPIAllocateMore(sizeof(SPropValue), lpRestriction, (LPVOID*)&lpRestriction->res.resAnd.lpRes[0].res.resProperty.lpProp);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateMore failed(2)", hr);

	lpRestriction->res.resAnd.lpRes[0].rt = RES_PROPERTY;
	lpRestriction->res.resAnd.lpRes[0].res.resProperty.relop = RELOP_EQ;
	lpRestriction->res.resAnd.lpRes[0].res.resProperty.ulPropTag = PR_OBJECT_TYPE;
	lpRestriction->res.resAnd.lpRes[0].res.resProperty.lpProp->ulPropTag = PR_OBJECT_TYPE;
	lpRestriction->res.resAnd.lpRes[0].res.resProperty.lpProp->Value.ul = MAPI_DISTLIST;

	hr = MAPIAllocateMore(sizeof(SPropValue), lpRestriction, (LPVOID*)&lpRestriction->res.resAnd.lpRes[1].res.resProperty.lpProp);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateMore failed(3)", hr);

	lpRestriction->res.resAnd.lpRes[1].rt = RES_PROPERTY;
	lpRestriction->res.resAnd.lpRes[1].res.resProperty.relop = RELOP_EQ;
	lpRestriction->res.resAnd.lpRes[1].res.resProperty.ulPropTag = PR_ADDRTYPE_W;
	lpRestriction->res.resAnd.lpRes[1].res.resProperty.lpProp->ulPropTag = PR_ADDRTYPE_W;
	lpRestriction->res.resAnd.lpRes[1].res.resProperty.lpProp->Value.lpszW = const_cast<wchar_t *>(L"ZARAFA");

	/*
	 * Setup entry restriction:
	 * PR_ADDR_TYPE == "ZARAFA"
	 */
	hr = MAPIAllocateBuffer(sizeof(SRestriction), &~lpEntryRestriction);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffer failed", hr);
	hr = MAPIAllocateMore(sizeof(SPropValue), lpEntryRestriction, (LPVOID*)&lpEntryRestriction->res.resProperty.lpProp);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateMore failed(4)", hr);

	lpEntryRestriction->rt = RES_PROPERTY;
	lpEntryRestriction->res.resProperty.relop = RELOP_EQ;
	lpEntryRestriction->res.resProperty.ulPropTag = PR_ADDRTYPE_W;
	lpEntryRestriction->res.resProperty.lpProp->ulPropTag = PR_ADDRTYPE_W;
	lpEntryRestriction->res.resProperty.lpProp->Value.lpszW = const_cast<wchar_t *>(L"ZARAFA");
	hr = lpMessage->GetRecipientTable(MAPI_UNICODE, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("GetRecipientTable failed", hr);

	/* The first table we send with ExpandRecipientsRecursive() is the RecipientTable itself,
	 * we need to put a restriction on this table since the first time only the groups
	 * should be added to the recipients list. Subsequent calls to ExpandRecipientsRecursive()
	 * will send the group member table and will correct add the members to the recipients
	 * table. */
	hr = lpTable->Restrict(lpRestriction, 0);
	if (hr != hrSuccess)
		return kc_perrorf("Restrict failed", hr);

	/* ExpandRecipientsRecursive() will run recursively expanding each group
	 * it finds including all subgroups. It will use the lExpandedGroups list
	 * to protect itself for circular subgroup membership */
	std::list<SBinary> lExpandedGroups;
	hr = ExpandRecipientsRecursive(lpAddrBook, lpMessage, lpTable, lpEntryRestriction, MAPI_TO, &lExpandedGroups);
	if (hr != hrSuccess)
		kc_perrorf("ExpandRecipientsRecursive failed", hr);
	for (const auto &g : lExpandedGroups)
		MAPIFreeBuffer(g.lpb);
	return hr;
}

/**
 * Rewrites a FAX:number "email address" to a sendable email address.
 *
 * @param[in]	lpMAPISession	The session of the user
 * @param[in]	lpMessage		The message to send
 * @return		HRESULT
 */
static HRESULT RewriteRecipients(LPMAPISESSION lpMAPISession,
    IMessage *lpMessage)
{
	object_ptr<IMAPITable> lpTable;
	memory_ptr<SPropTagArray> lpRecipColumns;
	const char	*const lpszFaxDomain = g_lpConfig->GetSetting("fax_domain");
	const char	*const lpszFaxInternational = g_lpConfig->GetSetting("fax_international");
	string		strFaxMail;
	unsigned int ulObjType, cValues;
	// contab email_offset: 0: business, 1: home, 2: primary (outlook uses string 'other')
	static constexpr const SizedSPropTagArray(3, sptaFaxNumbers) =
		{ 3, {PR_BUSINESS_FAX_NUMBER_A, PR_HOME_FAX_NUMBER_A,
		PR_PRIMARY_FAX_NUMBER_A}};

	if (!lpszFaxDomain || strcmp(lpszFaxDomain, "") == 0)
		return hrSuccess;
	auto hr = lpMessage->GetRecipientTable(MAPI_UNICODE, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("GetRecipientTable failed", hr);

	// we need all columns when rewriting FAX to SMTP
	hr = lpTable->QueryColumns(TBL_ALL_COLUMNS, &~lpRecipColumns);
	if (hr != hrSuccess)
		return kc_perrorf("QueryColumns failed", hr);
	hr = lpTable->SetColumns(lpRecipColumns, 0);
	if (hr != hrSuccess)
		return kc_perrorf("SetColumns failed", hr);

	while (TRUE) {
		rowset_ptr lpRowSet;
		hr = lpTable->QueryRows(1, 0, &~lpRowSet);
		if (hr != hrSuccess)
			return kc_perrorf("QueryRows failed", hr);
		if (lpRowSet->cRows == 0)
			break;
		auto lpEmailAddress = lpRowSet[0].find(PR_EMAIL_ADDRESS_W);
		auto lpEmailName = lpRowSet[0].cfind(PR_DISPLAY_NAME_W);
		auto lpAddrType = lpRowSet[0].find(PR_ADDRTYPE_W);
		auto lpEntryID = lpRowSet[0].find(PR_ENTRYID);
		if (!(lpEmailAddress && lpAddrType && lpEntryID && lpEmailName))
			continue;
		if (wcscmp(lpAddrType->Value.lpszW, L"FAX") != 0)
			continue;

		// rewrite FAX address to <number>@<faxdomain>
		wstring wstrName, wstrType, wstrEmailAddress;
		memory_ptr<ENTRYID> lpNewEntryID;
		memory_ptr<SPropValue> lpFaxNumbers;
		ULONG cbNewEntryID;

		if (ECParseOneOff((LPENTRYID)lpEntryID->Value.bin.lpb, lpEntryID->Value.bin.cb, wstrName, wstrType, wstrEmailAddress) == hrSuccess) {
			// user entered manual fax address
			strFaxMail = convert_to<string>(wstrEmailAddress);
		} else {
			// check if entry is in contacts folder
			LPCONTAB_ENTRYID lpContabEntryID = (LPCONTAB_ENTRYID)lpEntryID->Value.bin.lpb;
			auto guid = reinterpret_cast<GUID *>(&lpContabEntryID->muid);

			// check validity of lpContabEntryID
			if (sizeof(CONTAB_ENTRYID) > lpEntryID->Value.bin.cb ||
				*guid != PSETID_CONTACT_FOLDER_RECIPIENT ||
				lpContabEntryID->email_offset < 3 ||
				lpContabEntryID->email_offset > 5)
			{
				/*hr = MAPI_E_INVALID_PARAMETER;*/
				ec_log_err("Unable to convert FAX recipient, using %ls", lpEmailAddress->Value.lpszW);
				continue;
			}

			// 0..2 == reply to email offsets
			// 3..5 == fax email offsets
			lpContabEntryID->email_offset -= 3;

			object_ptr<IMAPIProp> lpFaxMailuser;
			hr = lpMAPISession->OpenEntry(lpContabEntryID->cbeid,
			     reinterpret_cast<ENTRYID *>(lpContabEntryID->abeid),
			     &iid_of(lpFaxMailuser), 0, &ulObjType, &~lpFaxMailuser);
			if (hr != hrSuccess) {
				ec_log_err("Unable to convert FAX recipient, using %ls: %s (%x)",
					lpEmailAddress->Value.lpszW, GetMAPIErrorMessage(hr), hr);
				continue;
			}
			hr = lpFaxMailuser->GetProps(sptaFaxNumbers, 0, &cValues, &~lpFaxNumbers);
			if (FAILED(hr)) {
				ec_log_err("Unable to convert FAX recipient, using %ls: %s (%x)",
					lpEmailAddress->Value.lpszW, GetMAPIErrorMessage(hr), hr);
				continue;
			}
			if (lpFaxNumbers[lpContabEntryID->email_offset].ulPropTag != sptaFaxNumbers.aulPropTag[lpContabEntryID->email_offset]) {
				ec_log_err("No suitable FAX number found, using %ls", lpEmailAddress->Value.lpszW);
				continue;
			}
			strFaxMail = lpFaxNumbers[lpContabEntryID->email_offset].Value.lpszA;
		}
		strFaxMail += string("@") + lpszFaxDomain;
		if (strFaxMail[0] == '+' && lpszFaxInternational != nullptr)
			strFaxMail = lpszFaxInternational + strFaxMail.substr(1, strFaxMail.length());

		auto wstrFaxMail = convert_to<std::wstring>(strFaxMail);
		std::wstring wstrOldFaxMail = lpEmailAddress->Value.lpszW; // keep old string for logging
		// hack values in lpRowSet
		lpEmailAddress->Value.lpszW = (WCHAR*)wstrFaxMail.c_str();
		lpAddrType->Value.lpszW = const_cast<wchar_t *>(L"SMTP");
		// old value is stuck to the row allocation, so we can override it, but we also must free the new!
		ECCreateOneOff((LPTSTR)lpEmailName->Value.lpszW, (LPTSTR)L"SMTP", (LPTSTR)wstrFaxMail.c_str(), MAPI_UNICODE, &cbNewEntryID, &~lpNewEntryID);
		lpEntryID->Value.bin.lpb = reinterpret_cast<BYTE *>(lpNewEntryID.get());
		lpEntryID->Value.bin.cb = cbNewEntryID;

		hr = lpMessage->ModifyRecipients(MODRECIP_MODIFY,
		     reinterpret_cast<ADRLIST *>(lpRowSet.get()));
		if (hr != hrSuccess) {
			ec_log_err("Unable to set new FAX mail address for \"%ls\" to \"%s\": %s (%x)",
				wstrOldFaxMail.c_str(), strFaxMail.c_str(), GetMAPIErrorMessage(hr), hr);
			continue;
		}
		ec_log_info("Using new FAX mail address %s", strFaxMail.c_str());
	}
	return hrSuccess;
}

/**
 * Make the recipient table in the message unique. Key is the PR_SMTP_ADDRESS and PR_RECIPIENT_TYPE (To/Cc/Bcc).
 *
 * @param[in]	lpMessage	The message to fix the recipient table for.
 * @return		HRESULT
 */
static HRESULT UniqueRecipients(IMessage *lpMessage)
{
	object_ptr<IMAPITable> lpTable;
	string			strEmail;
	ULONG			ulRecipType = 0;
	static constexpr const SizedSPropTagArray(3, sptaColumns) =
		{3, {PR_ROWID, PR_SMTP_ADDRESS_A, PR_RECIPIENT_TYPE}};
	static constexpr const SizedSSortOrderSet(2, sosOrder) = {
		2, 0, 0, {
			{ PR_SMTP_ADDRESS_A, TABLE_SORT_ASCEND },
			{ PR_RECIPIENT_TYPE, TABLE_SORT_ASCEND },
		}
	};

	auto hr = lpMessage->GetRecipientTable(0, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SetColumns(sptaColumns, 0);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SortTable(sosOrder, 0);
	if (hr != hrSuccess)
		return hr;

	while (TRUE) {
		rowset_ptr lpRowSet;
		hr = lpTable->QueryRows(1, 0, &~lpRowSet);
		if (hr != hrSuccess)
			return hr;
		if (lpRowSet->cRows == 0)
			break;
		auto lpEmailAddress = lpRowSet[0].cfind(PR_SMTP_ADDRESS_A);
		auto lpRecipType = lpRowSet[0].cfind(PR_RECIPIENT_TYPE);
		if (!lpEmailAddress || !lpRecipType)
			continue;

		/* Filter To, Cc, Bcc individually */
		if (strEmail != lpEmailAddress->Value.lpszA || ulRecipType != lpRecipType->Value.ul) {
			strEmail = string(lpEmailAddress->Value.lpszA);
			ulRecipType = lpRecipType->Value.ul;
			continue;
		}
		hr = lpMessage->ModifyRecipients(MODRECIP_REMOVE,
		     reinterpret_cast<ADRLIST *>(lpRowSet.get()));
		if (hr != hrSuccess)
			kc_perror("Failed to remove duplicate entry", hr);
	}
	return hrSuccess;
}

static HRESULT RewriteQuotedRecipients(IMessage *lpMessage)
{
	object_ptr<IMAPITable> lpTable;
	static constexpr const SizedSPropTagArray(3, sptaColumns) =
		{3, {PR_ROWID, PR_EMAIL_ADDRESS_W, PR_RECIPIENT_TYPE}};

	auto hr = lpMessage->GetRecipientTable(0, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("GetRecipientTable failed", hr);
	hr = lpTable->SetColumns(sptaColumns, 0);
	if (hr != hrSuccess)
		return kc_perrorf("SetColumns failed", hr);

	while (TRUE) {
		rowset_ptr lpRowSet;
		hr = lpTable->QueryRows(1, 0, &~lpRowSet);
		if (hr != hrSuccess)
			return kc_perrorf("QueryRows failed", hr);
		if (lpRowSet->cRows == 0)
			break;
		auto lpEmailAddress = lpRowSet[0].find(PR_EMAIL_ADDRESS_W);
		auto lpRecipType = lpRowSet[0].cfind(PR_RECIPIENT_TYPE);
		if (!lpEmailAddress || !lpRecipType)
			continue;

		std::wstring strEmail = lpEmailAddress->Value.lpszW;
		bool quoted = (strEmail[0] == '\'' && strEmail[strEmail.size()-1] == '\'') ||
		              (strEmail[0] == '"' && strEmail[strEmail.size()-1] == '"');
		if (!quoted)
			continue;

		ec_log_info("Rewrite quoted recipient: %ls", strEmail.c_str());
		strEmail = strEmail.substr(1, strEmail.size() - 2);
		lpEmailAddress->Value.lpszW = (WCHAR *)strEmail.c_str();
		hr = lpMessage->ModifyRecipients(MODRECIP_MODIFY,
		     reinterpret_cast<ADRLIST *>(lpRowSet.get()));
		if (hr != hrSuccess)
			return kc_perrorf("Failed to rewrite quoted recipient", hr);
	}
	return hrSuccess;
}
/**
 * Removes all MAPI_P1 marked recipients from a message.
 *
 * @param[in]	lpMessage	Message to remove MAPI_P1 recipients from
 * @return		HRESULT
 */
static HRESULT RemoveP1Recipients(IMessage *lpMessage)
{
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpRows;
	SPropValue sPropRestrict;

	sPropRestrict.ulPropTag = PR_RECIPIENT_TYPE;
	sPropRestrict.Value.ul = MAPI_P1;

	auto hr = lpMessage->GetRecipientTable(0, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("GetRecipientTable failed", hr);
	hr = ECPropertyRestriction(RELOP_EQ, PR_RECIPIENT_TYPE,
	     &sPropRestrict, ECRestriction::Cheap).RestrictTable(lpTable, 0);
	if (hr != hrSuccess)
		return kc_perrorf("Restrict failed", hr);
	hr = lpTable->QueryRows(-1, 0, &~lpRows);
	if (hr != hrSuccess)
		return kc_perrorf("QueryRows failed" ,hr);
	hr = lpMessage->ModifyRecipients(MODRECIP_REMOVE, reinterpret_cast<ADRLIST *>(lpRows.get()));
	if (hr != hrSuccess)
		return kc_perrorf("ModifyRecipients failed", hr);
	return hrSuccess;
}

enum eORPos {
	OR_DISPLAY_TO, OR_DISPLAY_CC, OR_DISPLAY_BCC, OR_SEARCH_KEY,
	OR_SENDER_ADDRTYPE, OR_SENDER_EMAIL_ADDRESS, OR_SENDER_ENTRYID,
	OR_SENDER_NAME, OR_SENDER_SEARCH_KEY, OR_SENT_REPRESENTING_ADDRTYPE,
	OR_SENT_REPRESENTING_EMAIL_ADDRESS, OR_SENT_REPRESENTING_ENTRYID,
	OR_SENT_REPRESENTING_NAME, OR_SENT_REPRESENTING_SEARCH_KEY, OR_SUBJECT,
	OR_CLIENT_SUBMIT_TIME,
};

static void mdnprop_populate(SPropValue *p, unsigned int &n,
    const SPropValue *a, const FILETIME &ft)
{
	p[n].ulPropTag     = PR_SUBJECT_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(L"Undelivered Mail Returned to Sender");
	p[n].ulPropTag     = PR_MESSAGE_FLAGS;
	p[n++].Value.ul    = 0;
	p[n].ulPropTag     = PR_MESSAGE_CLASS_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(L"REPORT.IPM.Note.NDR");
	p[n].ulPropTag     = PR_CLIENT_SUBMIT_TIME;
	p[n++].Value.ft    = ft;
	p[n].ulPropTag     = PR_MESSAGE_DELIVERY_TIME;
	p[n++].Value.ft    = ft;
	p[n].ulPropTag     = PR_SENDER_NAME_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(L"Mail Delivery System");
	/*
	 * Although lpszA is used, we just copy pointers. By not forcing _A or
	 * _W, this works in unicode and normal compile mode. Set the
	 * properties PR_RCVD_REPRESENTING_* and PR_RECEIVED_BY_* and
	 * PR_ORIGINAL_SENDER_* and PR_ORIGINAL_SENT_*.
	 */
	if (PROP_TYPE(a[OR_SENDER_NAME].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_RECEIVED_BY_NAME;
		p[n++].Value.lpszA = a[OR_SENDER_NAME].Value.lpszA;
		p[n].ulPropTag     = PR_ORIGINAL_SENDER_NAME;
		p[n++].Value.lpszA = a[OR_SENDER_NAME].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_SENDER_EMAIL_ADDRESS].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_RECEIVED_BY_EMAIL_ADDRESS;
		p[n++].Value.lpszA = a[OR_SENDER_EMAIL_ADDRESS].Value.lpszA;
		p[n].ulPropTag     = PR_ORIGINAL_SENDER_EMAIL_ADDRESS;
		p[n++].Value.lpszA = a[OR_SENDER_EMAIL_ADDRESS].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_SENDER_ADDRTYPE].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_RECEIVED_BY_ADDRTYPE;
		p[n++].Value.lpszA = a[OR_SENDER_ADDRTYPE].Value.lpszA;
		p[n].ulPropTag     = PR_ORIGINAL_SENDER_ADDRTYPE;
		p[n++].Value.lpszA = a[OR_SENDER_ADDRTYPE].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_SENDER_SEARCH_KEY].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag   = PR_RECEIVED_BY_SEARCH_KEY;
		p[n++].Value.bin = a[OR_SENDER_SEARCH_KEY].Value.bin;
		p[n].ulPropTag   = PR_ORIGINAL_SENDER_SEARCH_KEY;
		p[n++].Value.bin = a[OR_SENDER_SEARCH_KEY].Value.bin;
	}
	if (PROP_TYPE(a[OR_SENDER_ENTRYID].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag   = PR_RECEIVED_BY_ENTRYID;
		p[n++].Value.bin = a[OR_SENDER_ENTRYID].Value.bin;
		p[n].ulPropTag   = PR_ORIGINAL_SENDER_ENTRYID;
		p[n++].Value.bin = a[OR_SENDER_ENTRYID].Value.bin;
	}
	if (PROP_TYPE(a[OR_SENT_REPRESENTING_NAME].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_RCVD_REPRESENTING_NAME;
		p[n++].Value.lpszA = a[OR_SENT_REPRESENTING_NAME].Value.lpszA;
		p[n].ulPropTag     = PR_ORIGINAL_SENT_REPRESENTING_NAME;
		p[n++].Value.lpszA = a[OR_SENT_REPRESENTING_NAME].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_SENT_REPRESENTING_EMAIL_ADDRESS].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_RCVD_REPRESENTING_EMAIL_ADDRESS;
		p[n++].Value.lpszA = a[OR_SENT_REPRESENTING_EMAIL_ADDRESS].Value.lpszA;
		p[n].ulPropTag     = PR_ORIGINAL_SENT_REPRESENTING_EMAIL_ADDRESS;
		p[n++].Value.lpszA = a[OR_SENT_REPRESENTING_EMAIL_ADDRESS].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_SENT_REPRESENTING_ADDRTYPE].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_RCVD_REPRESENTING_ADDRTYPE;
		p[n++].Value.lpszA = a[OR_SENT_REPRESENTING_ADDRTYPE].Value.lpszA;
		p[n].ulPropTag     = PR_ORIGINAL_SENT_REPRESENTING_ADDRTYPE;
		p[n++].Value.lpszA = a[OR_SENT_REPRESENTING_ADDRTYPE].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_SENT_REPRESENTING_SEARCH_KEY].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag   = PR_RCVD_REPRESENTING_SEARCH_KEY;
		p[n++].Value.bin = a[OR_SENT_REPRESENTING_SEARCH_KEY].Value.bin;
		p[n].ulPropTag   = PR_ORIGINAL_SENT_REPRESENTING_SEARCH_KEY;
		p[n++].Value.bin = a[OR_SENT_REPRESENTING_SEARCH_KEY].Value.bin;
	}
	if (PROP_TYPE(a[OR_SENT_REPRESENTING_ENTRYID].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag   = PR_RCVD_REPRESENTING_ENTRYID;
		p[n++].Value.bin = a[OR_SENT_REPRESENTING_ENTRYID].Value.bin;
		p[n].ulPropTag   = PR_ORIGINAL_SENT_REPRESENTING_ENTRYID;
		p[n++].Value.bin = a[OR_SENT_REPRESENTING_ENTRYID].Value.bin;
	}
	if (PROP_TYPE(a[OR_DISPLAY_TO].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_ORIGINAL_DISPLAY_TO;
		p[n++].Value.lpszA = a[OR_DISPLAY_TO].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_DISPLAY_CC].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_ORIGINAL_DISPLAY_CC;
		p[n++].Value.lpszA = a[OR_DISPLAY_CC].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_DISPLAY_BCC].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_ORIGINAL_DISPLAY_BCC;
		p[n++].Value.lpszA = a[OR_DISPLAY_BCC].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_SUBJECT].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag     = PR_ORIGINAL_SUBJECT;
		p[n++].Value.lpszA = a[OR_SUBJECT].Value.lpszA;
	}
	if (PROP_TYPE(a[OR_CLIENT_SUBMIT_TIME].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag  = PR_ORIGINAL_SUBMIT_TIME;
		p[n++].Value.ft = a[OR_CLIENT_SUBMIT_TIME].Value.ft;
	}
	if (PROP_TYPE(a[OR_SEARCH_KEY].ulPropTag) != PT_ERROR) {
		p[n].ulPropTag   = PR_ORIGINAL_SEARCH_KEY;
		p[n++].Value.bin = a[OR_SEARCH_KEY].Value.bin;
	}
}

static HRESULT mdn_error_rcpt(IMessage *msg, const std::vector<sFailedRecip> &fr,
    const FILETIME &ft, convert_context &conv)
{
	unsigned int ent = 0;
	adrlist_ptr mods;
	auto ret = MAPIAllocateBuffer(CbNewADRLIST(fr.size()), &~mods);
	if (ret != hrSuccess)
		return ret;

	mods->cEntries = 0;
	for (size_t j = 0; j < fr.size(); ++j) {
		const sFailedRecip &cur = fr.at(j);
		ret = MAPIAllocateBuffer(sizeof(SPropValue) * 10, reinterpret_cast<void **>(&mods->aEntries[ent].rgPropVals));
		if (ret != hrSuccess)
			return ret;

		size_t pos = 0;
		mods->cEntries = ent;

		auto &pv = mods->aEntries[ent].rgPropVals;
		pv[pos].ulPropTag = PR_RECIPIENT_TYPE;
		pv[pos++].Value.ul = MAPI_TO;
		pv[pos].ulPropTag = PR_EMAIL_ADDRESS_A;
		pv[pos++].Value.lpszA = const_cast<char *>(cur.strRecipEmail.c_str());
		pv[pos].ulPropTag = PR_ADDRTYPE_W;
		pv[pos++].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
		pv[pos].ulPropTag = PR_DISPLAY_NAME_W;
		if (!cur.strRecipName.empty())
			pv[pos++].Value.lpszW = const_cast<wchar_t *>(cur.strRecipName.c_str());
		else
			pv[pos++].Value.lpszW = conv.convert_to<wchar_t *>(cur.strRecipEmail);

		pv[pos].ulPropTag = PR_REPORT_TEXT_A;
		pv[pos++].Value.lpszA = const_cast<char *>(cur.strSMTPResponse.c_str());
		pv[pos].ulPropTag = PR_REPORT_TIME;
		pv[pos++].Value.ft = ft;
		pv[pos].ulPropTag = PR_TRANSMITABLE_DISPLAY_NAME_A;
		pv[pos++].Value.lpszA = const_cast<char *>(cur.strRecipEmail.c_str());
		pv[pos].ulPropTag = 0x0C200003; // PR_NDR_STATUS_CODE;
		pv[pos++].Value.ul = cur.ulSMTPcode;
		pv[pos].ulPropTag = PR_NDR_DIAG_CODE;
		pv[pos++].Value.ul = MAPI_DIAG_MAIL_RECIPIENT_UNKNOWN;
		pv[pos].ulPropTag = PR_NDR_REASON_CODE;
		pv[pos++].Value.ul = MAPI_REASON_TRANSFER_FAILED;
		mods->aEntries[ent].cValues = pos;
		++ent;
	}
	return msg->ModifyRecipients(MODRECIP_ADD, mods);
}

/**
 * Creates an MDN message in the inbox of the given store for the passed message.
 *
 * This creates an MDN message in the inbox of the store passed, setting the correct properties and recipients. The most
 * important part of this function is to report errors of why sending failed. Sending can fail due to an overall problem
 * (when the entire message could not be sent) or when only some recipient didn't receive the message.
 *
 * In the case of partial failure (some recipients did not receive the email), the MDN message is populated with a recipient
 * table for all the recipients that failed. An error is attached to each of these recipients. The error information is
 * retrieved from the passed lpMailer object.
 *
 * @param lpMailer Mailer object used to send the lpMessage message containing the errors
 * @param lpMessage Failed message
 */
HRESULT SendUndeliverable(ECSender *lpMailer, IMsgStore *lpStore,
    IMessage *lpMessage)
{
	object_ptr<IMAPIFolder> lpInbox;
	object_ptr<IMessage> lpErrorMsg, lpOriginalMessage;
	memory_ptr<ENTRYID> lpEntryID;
	unsigned int cbEntryID, ulObjType, cValuesOriginal = 0, ulRows = 0;
	wstring			newbody;
	memory_ptr<SPropValue> lpPropValue, lpPropValueAttach, lpPropArrayOriginal;
	unsigned int	ulPropPos = 0;
	FILETIME		ft;
	object_ptr<IAttach> lpAttach;
	object_ptr<IMAPITable> lpTableMods;
	/* CopyTo() vars */
	unsigned int	ulPropAttachPos;
	ULONG			ulAttachNum;
	const std::vector<sFailedRecip> &temporaryFailedRecipients = lpMailer->getTemporaryFailedRecipients();
	const std::vector<sFailedRecip> &permanentFailedRecipients = lpMailer->getPermanentFailedRecipients();

	// These props are on purpose without _A and _W
	static constexpr const SizedSPropTagArray(16, sPropsOriginal) = {
		16,
		{ PR_DISPLAY_TO, PR_DISPLAY_CC,
		  PR_DISPLAY_BCC, PR_SEARCH_KEY,
		  PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS,
		  PR_SENDER_ENTRYID, PR_SENDER_NAME,
		  PR_SENDER_SEARCH_KEY, PR_SENT_REPRESENTING_ADDRTYPE,
		  PR_SENT_REPRESENTING_EMAIL_ADDRESS, PR_SENT_REPRESENTING_ENTRYID,
		  PR_SENT_REPRESENTING_NAME, PR_SENT_REPRESENTING_SEARCH_KEY,
		  PR_SUBJECT_W, PR_CLIENT_SUBMIT_TIME }
	};
	static constexpr const SizedSPropTagArray(7, sPropTagRecipient) = {
		7,
		{ PR_RECIPIENT_TYPE, PR_DISPLAY_NAME, PR_DISPLAY_TYPE,
		  PR_ADDRTYPE, PR_EMAIL_ADDRESS,
		  PR_ENTRYID, PR_SEARCH_KEY }
	};

	// open inbox
	auto hr = lpStore->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &cbEntryID, &~lpEntryID, nullptr);
	if (hr != hrSuccess) {
		ec_log_warn("Unable to resolve incoming folder: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	hr = lpStore->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpInbox);
	if (hr != hrSuccess || ulObjType != MAPI_FOLDER) {
		ec_log_warn("Unable to open inbox folder: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return MAPI_E_NOT_FOUND;
	}
	// make new message in inbox
	hr = lpInbox->CreateMessage(nullptr, 0, &~lpErrorMsg);
	if (hr != hrSuccess) {
		ec_log_warn("Unable to create undeliverable message: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	// Get properties from the original message
	hr = lpMessage->GetProps(sPropsOriginal, 0, &cValuesOriginal, &~lpPropArrayOriginal);
	if (FAILED(hr))
		return kc_perrorf("GetProps failed", hr);
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * 34, &~lpPropValue);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffers failed", hr);

	GetSystemTimeAsFileTime(&ft);
	mdnprop_populate(lpPropValue, ulPropPos, lpPropArrayOriginal, ft);

	// Add the original message into the errorMessage
	hr = lpErrorMsg->CreateAttach(nullptr, 0, &ulAttachNum, &~lpAttach);
	if (hr != hrSuccess) {
		ec_log_warn("Unable to create attachment: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	hr = lpAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY, &~lpOriginalMessage);
	if (hr != hrSuccess)
		return kc_perrorf("OpenProperty failed", hr);
	hr = lpMessage->CopyTo(0, nullptr, nullptr, 0, nullptr, &IID_IMessage, lpOriginalMessage, 0, nullptr);
	if (hr != hrSuccess)
		return kc_perrorf("CopyTo failed", hr);

	// Remove MAPI_P1 recipients. These are present when you resend a resent message. They shouldn't be there since
	// we should be resending the original message
	hr = RemoveP1Recipients(lpOriginalMessage);
	if (hr != hrSuccess)
		return kc_perrorf("RemoveP1Recipients failed", hr);
	hr = lpOriginalMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		return kc_perrorf("SaveChanges failed", hr);
	ulPropAttachPos = 0;
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * 4, &~lpPropValueAttach);
	if (hr != hrSuccess)
		return hr;

	lpPropValueAttach[ulPropAttachPos].ulPropTag = PR_ATTACH_METHOD;
	lpPropValueAttach[ulPropAttachPos++].Value.ul = ATTACH_EMBEDDED_MSG;
	lpPropValueAttach[ulPropAttachPos].ulPropTag = PR_ATTACH_MIME_TAG_W;
	lpPropValueAttach[ulPropAttachPos++].Value.lpszW = const_cast<wchar_t *>(L"message/rfc822");
	if(PROP_TYPE(lpPropArrayOriginal[OR_SUBJECT].ulPropTag) != PT_ERROR) {
		lpPropValueAttach[ulPropAttachPos].ulPropTag = CHANGE_PROP_TYPE(PR_DISPLAY_NAME, PROP_TYPE(lpPropArrayOriginal[OR_SUBJECT].ulPropTag));
		lpPropValueAttach[ulPropAttachPos++].Value.lpszA = lpPropArrayOriginal[OR_SUBJECT].Value.lpszA;
	}
	lpPropValueAttach[ulPropAttachPos].ulPropTag = PR_RENDERING_POSITION;
	lpPropValueAttach[ulPropAttachPos++].Value.ul = -1;

	hr = lpAttach->SetProps(ulPropAttachPos, lpPropValueAttach, NULL);
	if (hr != hrSuccess)
		return kc_perrorf("SetProps failed", hr);
	hr = lpAttach->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		return kc_perrorf("SaveChanges failed", hr);

	// add failed recipients to error report
	hr = lpMessage->GetRecipientTable(MAPI_UNICODE, &~lpTableMods);
	if (hr != hrSuccess)
		return kc_perrorf("GetRecipientTable failed", hr);
	hr = lpTableMods->SetColumns(sPropTagRecipient, TBL_BATCH);
	if (hr != hrSuccess)
		return kc_perrorf("SetColumns failed", hr);
	hr = lpTableMods->GetRowCount(0, &ulRows);
	if (hr != hrSuccess)
		return kc_perrorf("GetRowCount failed", hr);

	if (ulRows == 0 || (permanentFailedRecipients.empty() && temporaryFailedRecipients.empty())) {
		// No specific failed recipients, so the entire message failed
		// If there's a pr_body, outlook will display that, and not the 'default' outlook error report
		// Message error
		newbody = L"Unfortunately, kopano-spooler was unable to deliver your mail.\nThe error given was:\n\n";
		newbody.append(lpMailer->getErrorString());
		newbody.append(L"\n\nYou may need to contact your e-mail administrator to solve this problem.\n");

		lpPropValue[ulPropPos].ulPropTag = PR_BODY_W;
		lpPropValue[ulPropPos++].Value.lpszW = (WCHAR*)newbody.c_str();

		if (ulRows > 0) {
			// All recipients failed, therefore all recipient need to be in the MDN recipient table
			rowset_ptr lpRows;
			hr = lpTableMods->QueryRows(-1, 0, &~lpRows);
			if (hr != hrSuccess)
				return kc_perrorf("QueryRows failed", hr);
			hr = lpErrorMsg->ModifyRecipients(MODRECIP_ADD, reinterpret_cast<ADRLIST *>(lpRows.get()));
			if (hr != hrSuccess)
				return kc_perrorf("ModifyRecipients failed", hr);
		}
	}
	else if (ulRows > 0)
	{
		convert_context converter;
		newbody = L"Unfortunately, kopano-spooler was unable to deliver your mail to the/some of the recipient(s).\n";
		newbody.append(L"You may need to contact your e-mail administrator to solve this problem.\n");

		if (!temporaryFailedRecipients.empty()) {
			newbody.append(L"\nRecipients that will be retried:\n");

			for (size_t i = 0; i < temporaryFailedRecipients.size(); ++i) {
				const sFailedRecip &cur = temporaryFailedRecipients.at(i);

				newbody.append(L"\t");
				newbody.append(cur.strRecipName.c_str());
				newbody.append(L" <");
				newbody.append(converter.convert_to<wchar_t *>(cur.strRecipEmail));
				newbody.append(L">\n");
			}
		}

		if (!permanentFailedRecipients.empty()) {
			newbody.append(L"\nRecipients that failed permanently:\n");

			for (size_t i = 0; i < permanentFailedRecipients.size(); ++i) {
				const sFailedRecip &cur = permanentFailedRecipients.at(i);

				newbody.append(L"\t");
				newbody.append(cur.strRecipName.c_str());
				newbody.append(L" <");
				newbody.append(converter.convert_to<wchar_t *>(cur.strRecipEmail));
				newbody.append(L">\n");
			}
		}

		lpPropValue[ulPropPos].ulPropTag = PR_BODY_W;
		lpPropValue[ulPropPos++].Value.lpszW = const_cast<wchar_t *>(newbody.c_str());

		// Only some recipients failed, so add only failed recipients to the MDN message. This causes
		// resends only to go to those recipients. This means we should add all error recipients to the
		// recipient list of the MDN message.
		hr = mdn_error_rcpt(lpErrorMsg, temporaryFailedRecipients, ft, converter);
		if (hr != hrSuccess)
			return hr;
	}

	// Add properties
	hr = lpErrorMsg->SetProps(ulPropPos, lpPropValue, NULL);
	if (hr != hrSuccess)
		return kc_perrorf("SetProps failed", hr);
	// save message
	hr = lpErrorMsg->SaveChanges(KEEP_OPEN_READONLY);
	if (hr != hrSuccess)
		return kc_perror("Unable to commit message", hr);
	// New mail notification
	if (HrNewMailNotification(lpStore, lpErrorMsg) != hrSuccess)
		ec_log_warn("Unable to issue \"New Mail\" notification: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
	return hrSuccess;
}

/**
 * Converts a Contacts folder EntryID to a ZARAFA addressbook EntryID.
 *
 * A contacts folder EntryID contains an offset that is an index in three different possible EntryID named properties.
 *
 * @param[in]	lpUserStore	The store of the user where the contact is stored.
 * @param[in]	ct		The contact EntryID
 * @param[out]	kp		The EntryID where the contact points to
 * @return		HRESULT
 */
static HRESULT ContactToKopano(IMsgStore *lpUserStore,
    const SBinary &ct, SBinary *kp)
{
	auto lpContabEntryID = reinterpret_cast<const CONTAB_ENTRYID *>(ct.lpb);
	auto guid = reinterpret_cast<const GUID *>(&lpContabEntryID->muid);
	unsigned int ulObjType, cValues;
	object_ptr<IMAPIProp> lpContact;
	LPSPropValue lpEntryIds = NULL;
	memory_ptr<SPropTagArray> lpPropTags;
	memory_ptr<MAPINAMEID> lpNames;
	memory_ptr<MAPINAMEID *> lppNames;

	if (sizeof(CONTAB_ENTRYID) > ct.cb ||
	    *guid != PSETID_CONTACT_FOLDER_RECIPIENT ||
	    lpContabEntryID->email_offset > 2)
		return MAPI_E_NOT_FOUND;

	auto hr = lpUserStore->OpenEntry(lpContabEntryID->cbeid, reinterpret_cast<ENTRYID *>(const_cast<BYTE *>(lpContabEntryID->abeid)),
	          &iid_of(lpContact), 0, &ulObjType, &~lpContact);
	if (hr != hrSuccess)
		return kc_perror("Unable to open contact entryid", hr);
	hr = MAPIAllocateBuffer(sizeof(MAPINAMEID) * 3, &~lpNames);
	if (hr != hrSuccess)
		return kc_perror("No memory for named IDs from contact", hr);
	hr = MAPIAllocateBuffer(sizeof(LPMAPINAMEID) * 3, &~lppNames);
	if (hr != hrSuccess)
		return kc_perror("No memory for named IDs from contact", hr);

	// Email1EntryID
	lpNames[0].lpguid = (GUID*)&PSETID_Address;
	lpNames[0].ulKind = MNID_ID;
	lpNames[0].Kind.lID = 0x8085;
	lppNames[0] = &lpNames[0];
	// Email2EntryID
	lpNames[1].lpguid = (GUID*)&PSETID_Address;
	lpNames[1].ulKind = MNID_ID;
	lpNames[1].Kind.lID = 0x8095;
	lppNames[1] = &lpNames[1];
	// Email3EntryID
	lpNames[2].lpguid = (GUID*)&PSETID_Address;
	lpNames[2].ulKind = MNID_ID;
	lpNames[2].Kind.lID = 0x80A5;
	lppNames[2] = &lpNames[2];

	hr = lpContact->GetIDsFromNames(3, lppNames, 0, &~lpPropTags);
	if (hr != hrSuccess)
		return kc_perror("Error while retrieving named data from contact", hr);
	hr = lpContact->GetProps(lpPropTags, 0, &cValues, &lpEntryIds);
	if (FAILED(hr))
		return kc_perror("Unable to get named properties", hr);
	if (PROP_TYPE(lpEntryIds[lpContabEntryID->email_offset].ulPropTag) != PT_BINARY) {
		ec_log_err("Offset %d not found in contact", lpContabEntryID->email_offset);
		return MAPI_E_NOT_FOUND;
	}
	hr = KAllocCopy(lpEntryIds[lpContabEntryID->email_offset].Value.bin.lpb, lpEntryIds[lpContabEntryID->email_offset].Value.bin.cb, reinterpret_cast<void **>(kp->lpb));
	if (hr != hrSuccess)
		return kc_perror("No memory for contact EID", hr);
	kp->cb = lpEntryIds[lpContabEntryID->email_offset].Value.bin.cb;
	return hrSuccess;
}

/**
 * Converts an One-off EntryID to a ZARAFA addressbook EntryID.
 *
 * @param[in]	lpAddrBook	The Global Addressbook of the user sending the mail.
 * @param[in]	ulSMTPEID	The number of bytes in lpSMTPEID
 * @param[in]	smtp	The One off EntryID.
 * @param[out]	zeid	The ZARAFA entryid of the user defined in the One off.
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	User not a Kopano user, or lpSMTPEID is not an One-off EntryID
 */
static HRESULT SMTPToZarafa(IAddrBook *lpAddrBook, const SBinary &smtp,
    SBinary *zeid)
{
	wstring wstrName, wstrType, wstrEmailAddress;
	adrlist_ptr lpAList;

	// representing entryid can also be a one off id, so search the user, and then get the entryid again ..
	// we then always should have yourself as the sender, otherwise: denied
	if (ECParseOneOff(reinterpret_cast<const ENTRYID *>(smtp.lpb),
	    smtp.cb, wstrName, wstrType, wstrEmailAddress) != hrSuccess)
		return MAPI_E_NOT_FOUND;
	auto hr = MAPIAllocateBuffer(CbNewADRLIST(1), &~lpAList);
	if (hr != hrSuccess)
		return hrSuccess;
	lpAList->cEntries = 0;
	lpAList->aEntries[0].cValues = 1;
	if ((hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpAList->aEntries[0].cValues, (void**)&lpAList->aEntries[0].rgPropVals)) != hrSuccess)
		return hrSuccess;
	++lpAList->cEntries;
	lpAList->aEntries[0].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_W;
	lpAList->aEntries[0].rgPropVals[0].Value.lpszW = (WCHAR*)wstrEmailAddress.c_str();
	hr = lpAddrBook->ResolveName(0, EMS_AB_ADDRESS_LOOKUP, NULL, lpAList);
	if (hr != hrSuccess)
		return kc_perrorf("ResolveName failed", hr);
	auto lpSpoofEID = lpAList->aEntries[0].cfind(PR_ENTRYID);
	if (!lpSpoofEID) {
		kc_perror("PpropFindProp failed", MAPI_E_NOT_FOUND);
		return hrSuccess;
	}
	hr = KAllocCopy(lpSpoofEID->Value.bin.lpb, lpSpoofEID->Value.bin.cb, reinterpret_cast<void **>(&zeid->lpb));
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffer failed", hr);
	zeid->cb = lpSpoofEID->Value.bin.cb;
	return hrSuccess;
}

/**
 * Find a user in a group. Used when checking for send-as users.
 *
 * @param[in]	lpAdrBook		The Global Addressbook of the user sending the mail.
 * @param[in]	owner		The EntryID of the user to find in the group
 * @param[in]	dl		The EntryID of the group (distlist)
 * @param[out]	lpulCmp			The result of the comparison of CompareEntryID. FALSE if not found, TRUE if found.
 * @param[in]	level			Internal parameter to keep track of recursion. Max is 10 levels deep before it gives up.
 * @return		HRESULT
 */
static HRESULT HrFindUserInGroup(IAddrBook *lpAdrBook, const SBinary &owner,
    const SBinary &dl, unsigned int *lpulCmp, int level = 0)
{
	unsigned int ulCmp = 0, ulObjType = 0;
	object_ptr<IDistList> lpDistList;
	object_ptr<IMAPITable> lpMembersTable;
	static constexpr const SizedSPropTagArray(2, sptaIDProps) =
		{2, {PR_ENTRYID, PR_OBJECT_TYPE}};

	if (lpulCmp == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (level > 10) {
		HRESULT hr = MAPI_E_TOO_COMPLEX;
		ec_log_err("HrFindUserInGroup(): level too big %d: %s (%x)",
			level, GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	auto hr = lpAdrBook->OpenEntry(dl.cb, reinterpret_cast<const ENTRYID *>(dl.lpb),
	          &iid_of(lpDistList), 0, &ulObjType, &~lpDistList);
	if (hr != hrSuccess)
		return kc_perrorf("OpenEntry failed", hr);
	hr = lpDistList->GetContentsTable(0, &~lpMembersTable);
	if (hr != hrSuccess)
		return kc_perrorf("GetContentsTable failed", hr);
	hr = lpMembersTable->SetColumns(sptaIDProps, 0);
	if (hr != hrSuccess)
		return kc_perrorf("SetColumns failed", hr);

	// sort on PR_OBJECT_TYPE (MAILUSER < DISTLIST) ?
	while (TRUE) {
		rowset_ptr lpRowSet;
		hr = lpMembersTable->QueryRows(1, 0, &~lpRowSet);
		if (hr != hrSuccess)
			return kc_perrorf("QueryRows failed", hr);
		if (lpRowSet->cRows == 0)
			break;
		if (lpRowSet[0].lpProps[0].ulPropTag != PR_ENTRYID ||
		    lpRowSet[0].lpProps[1].ulPropTag != PR_OBJECT_TYPE)
			continue;
		if (lpRowSet[0].lpProps[1].Value.ul == MAPI_MAILUSER)
			hr = lpAdrBook->CompareEntryIDs(owner.cb, reinterpret_cast<const ENTRYID *>(owner.lpb),
			     lpRowSet[0].lpProps[0].Value.bin.cb, reinterpret_cast<ENTRYID *>(lpRowSet[0].lpProps[0].Value.bin.lpb),
			     0, &ulCmp);
		else if (lpRowSet[0].lpProps[1].Value.ul == MAPI_DISTLIST)
			hr = HrFindUserInGroup(lpAdrBook, owner,
			     lpRowSet[0].lpProps[0].Value.bin, &ulCmp, level + 1);
		if (hr == hrSuccess && ulCmp == TRUE)
			break;
	}
	*lpulCmp = ulCmp;
	return hrSuccess;
}

/**
 * Looks up a user in the addressbook, and opens the store of that user.
 *
 * @param[in]	lpAddrBook		The Global Addressbook of the user
 * @param[in]	lpUserStore		The store of the user, just to create the deletegate store entry id
 * @param[in]	lpAdminSession	We need full rights on the delegate store, so use the admin session to open it
 * @param[in]	ulRepresentCB	Number of bytes in lpRepresentEID
 * @param[in]	lpRepresentEID	EntryID of the delegate user
 * @param[out]	lppRepStore		The store of the delegate
 * @return		HRESULT
 */
static HRESULT HrOpenRepresentStore(IAddrBook *lpAddrBook,
    IMsgStore *lpUserStore, IMAPISession *lpAdminSession, const SBinary &repr,
    IMsgStore **lppRepStore)
{
	unsigned int ulObjType = 0, ulRepStoreCB = 0;
	object_ptr<IMAPIProp> lpRepresenting;
	memory_ptr<SPropValue> lpRepAccount;
	object_ptr<IExchangeManageStore> lpExchangeManageStore;
	memory_ptr<ENTRYID> lpRepStoreEID;
	object_ptr<IMsgStore> lpRepStore;

	auto hr = lpAddrBook->OpenEntry(repr.cb, reinterpret_cast<const ENTRYID *>(repr.lpb),
	          &iid_of(lpRepresenting), 0, &ulObjType, &~lpRepresenting);
	if (hr != hrSuccess) {
		ec_log_info("Unable to open representing user in addressbook: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return MAPI_E_NOT_FOUND;
	}
	hr = HrGetOneProp(lpRepresenting, PR_ACCOUNT, &~lpRepAccount);
	if (hr != hrSuccess) {
		ec_log_info("Unable to find account name for representing user: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return MAPI_E_NOT_FOUND;
	}

	hr = lpUserStore->QueryInterface(IID_IExchangeManageStore, &~lpExchangeManageStore);
	if (hr != hrSuccess) {
		ec_log_info("IExchangeManageStore interface not found: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	hr = lpExchangeManageStore->CreateStoreEntryID(nullptr, lpRepAccount->Value.LPSZ, fMapiUnicode, &ulRepStoreCB, &~lpRepStoreEID);
	if (hr != hrSuccess) {
		ec_log_err("Unable to create store entryid for representing user \"" TSTRING_PRINTF "\": %s (%x)",
			lpRepAccount->Value.LPSZ, GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	// Use the admin session to open the store, so we have full rights
	hr = lpAdminSession->OpenMsgStore(0, ulRepStoreCB, lpRepStoreEID, nullptr, MAPI_BEST_ACCESS, &~lpRepStore);
	if (hr != hrSuccess) {
		ec_log_err("Unable to open store of representing user \"" TSTRING_PRINTF "\": %s (%x)",
			lpRepAccount->Value.LPSZ, GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	return lpRepStore->QueryInterface(IID_IMsgStore,
	       reinterpret_cast<void **>(lppRepStore));
}

/**
 * Checks for the presence of an Addressbook EntryID in a given
 * array. If the array contains a group EntryID, it is opened, and
 * searched within the group for the presence of the given EntryID.
 *
 * @param[in] szFunc Context name how this function is used. Used in logging.
 * @param[in] lpszMailer The name of the user sending the email.
 * @param[in] lpAddrBook The Global Addressbook.
 * @param[in] owner EntryID of the "Owner" object, which is searched in the array
 * @param[in] mv Array of EntryIDs to search in
 * @param[out] lpulObjType lpOwnerEID was found in this type of object (user or group)
 * @param[out] lpbAllowed User is (not) found in array
 *
 * @return hrSuccess
 */
static HRESULT HrCheckAllowedEntryIDArray(const char *szFunc,
    const wchar_t *lpszMailer, IAddrBook *lpAddrBook, const SBinary &owner,
    const SBinaryArray &mv, unsigned int *lpulObjType, bool *lpbAllowed)
{
	HRESULT hr = hrSuccess;
	unsigned int ulObjType, ulCmpRes;

	for (unsigned int i = 0; i < mv.cValues; ++i) {
		// quick way to see what object the entryid points to .. otherwise we need to call OpenEntry, which is slow
		if (GetNonPortableObjectType(mv.lpbin[i].cb, reinterpret_cast<const ENTRYID *>(mv.lpbin[i].lpb), &ulObjType))
			continue;

		if (ulObjType == MAPI_DISTLIST) {
			hr = HrFindUserInGroup(lpAddrBook, owner, mv.lpbin[i], &ulCmpRes);
		} else if (ulObjType == MAPI_MAILUSER) {
			hr = lpAddrBook->CompareEntryIDs(owner.cb, reinterpret_cast<const ENTRYID *>(owner.lpb),
			     mv.lpbin[i].cb, reinterpret_cast<const ENTRYID *>(mv.lpbin[i].lpb), 0, &ulCmpRes);
		} else {
			ec_log_err("Invalid object %d in %s list of user \"%ls\": %s (%x)",
				ulObjType, szFunc, lpszMailer, GetMAPIErrorMessage(hr), hr);
			continue;
		}

		if (hr == hrSuccess && ulCmpRes == TRUE) {
			*lpulObjType = ulObjType;
			*lpbAllowed = true;
			// always return success, since lpbAllowed is always written
			return hrSuccess;
		}
	}

	*lpbAllowed = false;
	return hrSuccess;
}

/**
 * Checks if the current user is has send-as rights as specified user. Needs
 * admin rights to open the delegate store.
 *
 * @param[in]	lpAddrBook	The Global Addressbook of the user trying to send an email.
 * @param[in]	lpUserStore	The store of the user trying to send an email.
 * @param[in]	lpAdminSession MAPI session of the Kopano SYSTEM user.
 * @param[in]	lpMailer	ECSender object (inetmapi), used to set an error for an error mail if not allowed.
 * @param[in]	owner		EntryID of the user sending the mail.
 * @param[in]	repr		EntryID of the user set in the From address. Can be a One-off entryid.
 * @param[out]	lpbAllowed	Set to true if the lpOwnerEID is a delegate of lpRepresentEID
 * @param[out]	lppRepStore	The store of the delegate when allowed.
 * @return		HRESULT
 */
static HRESULT CheckSendAs(IAddrBook *lpAddrBook, IMsgStore *lpUserStore,
    IMAPISession *lpAdminSession, ECSender *lpMailer, const SBinary &owner,
    SBinary repr, bool *lpbAllowed, IMsgStore **lppRepStore)
{
	bool bAllowed = false, bHasStore = false;
	ULONG ulObjType;
	object_ptr<IMAPIProp> lpMailboxOwner, lpRepresenting;
	memory_ptr<SPropValue> lpOwnerProps, lpRepresentProps;
	SPropValue sSpoofEID = {0};
	unsigned int ulCmpRes = 0, cValues = 0;
	static constexpr const SizedSPropTagArray(3, sptaIDProps) =
		{3, {PR_DISPLAY_NAME_W, PR_EC_SENDAS_USER_ENTRYIDS,
		PR_DISPLAY_TYPE}};

	auto hr = SMTPToZarafa(lpAddrBook, repr, &sSpoofEID.Value.bin);
	if (hr != hrSuccess)
		hr = ContactToKopano(lpUserStore, repr, &sSpoofEID.Value.bin);
	if (hr == hrSuccess)
		repr = std::move(sSpoofEID.Value.bin);
	// you can always send as yourself
	if (lpAddrBook->CompareEntryIDs(owner.cb, reinterpret_cast<const ENTRYID *>(owner.lpb),
	    repr.cb, reinterpret_cast<const ENTRYID *>(repr.lpb), 0, &ulCmpRes) == hrSuccess &&
	    ulCmpRes == true) {
		bAllowed = true;
		goto exit;
	}

	// representing entryid is now always a Kopano Entry ID. Open the user so we can log the display name
	hr = lpAddrBook->OpenEntry(repr.cb, reinterpret_cast<const ENTRYID *>(repr.lpb),
	     &iid_of(lpRepresenting), 0, &ulObjType, &~lpRepresenting);
	if (hr != hrSuccess) {
		kc_perrorf("OpenEntry failed(1)", hr);
		goto exit;
	}
	hr = lpRepresenting->GetProps(sptaIDProps, 0, &cValues, &~lpRepresentProps);
	if (FAILED(hr)) {
		kc_perrorf("GetProps failed(1)", hr);
		goto exit;
	}
	hr = hrSuccess;

	// Open the owner to get the displayname for logging
	if (lpAddrBook->OpenEntry(owner.cb, reinterpret_cast<const ENTRYID *>(owner.lpb),
	    &iid_of(lpMailboxOwner), 0, &ulObjType, &~lpMailboxOwner) != hrSuccess) {
		kc_perrorf("OpenEntry failed(2)", hr);
		goto exit;
	}
	hr = lpMailboxOwner->GetProps(sptaIDProps, 0, &cValues, &~lpOwnerProps);
	if (FAILED(hr)) {
		kc_perrorf("GetProps failed(2)", hr);
		goto exit;
	}
	hr = hrSuccess;

	if (lpRepresentProps[2].ulPropTag != PR_DISPLAY_TYPE) {	// Required property for a mailuser object
		hr = MAPI_E_NOT_FOUND;
		ec_log_notice("CheckSendAs(): PR_DISPLAY_TYPE missing: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	bHasStore = (lpRepresentProps[2].Value.l == DT_MAILUSER);
	if (lpRepresentProps[1].ulPropTag != PR_EC_SENDAS_USER_ENTRYIDS)
		// No sendas, therefore no sendas permissions, but we don't fail
		goto exit;

	hr = HrCheckAllowedEntryIDArray("sendas",
	     lpRepresentProps[0].ulPropTag == PR_DISPLAY_NAME_W ? lpRepresentProps[0].Value.lpszW : L"<no name>",
	     lpAddrBook, owner, lpRepresentProps[1].Value.MVbin, &ulObjType, &bAllowed);
	if (bAllowed)
		ec_log_err("Mail for user \"%ls\" is sent as %s \"%ls\"",
			lpOwnerProps[0].ulPropTag == PR_DISPLAY_NAME_W ? lpOwnerProps[0].Value.lpszW : L"<no name>",
			(ulObjType != MAPI_DISTLIST)?"user":"group",
			lpRepresentProps[0].ulPropTag == PR_DISPLAY_NAME_W ? lpRepresentProps[0].Value.lpszW : L"<no name>");
exit:
	if (!bAllowed) {
		if (lpRepresentProps && PROP_TYPE(lpRepresentProps[0].ulPropTag) != PT_ERROR)
			lpMailer->setError(format(KC_A("You are not allowed to send as user or group \"%s\""), convert_to<std::string>(lpRepresentProps[0].Value.lpszW).c_str()));
		else
			lpMailer->setError(KC_TX("The user or group you try to send as could not be found."));

		ec_log_err("User \"%ls\" is not allowed to send as user or group \"%ls\". "
			"You may enable all outgoing addresses by enabling the always_send_delegates option.",
			(lpOwnerProps && PROP_TYPE(lpOwnerProps[0].ulPropTag) != PT_ERROR) ? lpOwnerProps[0].Value.lpszW : L"<unknown>",
			(lpRepresentProps && PROP_TYPE(lpRepresentProps[0].ulPropTag) != PT_ERROR) ? lpRepresentProps[0].Value.lpszW : L"<unknown>");
	}

	if (bAllowed && bHasStore)
		hr = HrOpenRepresentStore(lpAddrBook, lpUserStore, lpAdminSession, repr, lppRepStore);
	else
		*lppRepStore = NULL;
	*lpbAllowed = bAllowed;
	MAPIFreeBuffer(sSpoofEID.Value.bin.lpb);
	return hr;
}

/**
 * Checks if the current user is a delegate of a specified user. Needs
 * admin rights to open the delegate store.
 *
 * @param[in]	lpAddrBook	The Global Addressbook of the user trying to send an email.
 * @param[in]	lpUserStore	The store of the user trying to send an email.
 * @param[in]	lpAdminSession MAPI session of the Kopano SYSTEM user.
 * @param[in]	owner		EntryID of the user sending the mail.
 * @param[in]	repr		EntryID of the user set in the From address. Can be a One-off entryid.
 * @param[out]	lpbAllowed	Set to true if the lpOwnerEID is a delegate of lpRepresentEID
 * @param[out]	lppRepStore	The store of the delegate when allowed.
 * @return		HRESULT
 * @retval		hrSuccess, always returned, actual return value in lpbAllowed.
 */
static HRESULT CheckDelegate(IAddrBook *lpAddrBook, IMsgStore *lpUserStore,
    IMAPISession *lpAdminSession, const SBinary &owner, SBinary repr,
    bool *lpbAllowed, IMsgStore **lppRepStore)
{
	bool bAllowed = false;
	ULONG ulObjType;
	object_ptr<IMsgStore> lpRepStore;
	memory_ptr<SPropValue> lpUserOwnerName, lpRepOwnerName;
	object_ptr<IMAPIFolder> lpRepSubtree;
	memory_ptr<SPropValue> lpRepFBProp, lpDelegates;
	object_ptr<IMessage> lpRepFBMessage;
	SPropValue sSpoofEID = {0};

	auto hr = SMTPToZarafa(lpAddrBook, repr, &sSpoofEID.Value.bin);
	if (hr != hrSuccess)
		hr = ContactToKopano(lpUserStore, repr, &sSpoofEID.Value.bin);
	if (hr == hrSuccess)
		repr = std::move(sSpoofEID.Value.bin);
	hr = HrOpenRepresentStore(lpAddrBook, lpUserStore, lpAdminSession, repr, &~lpRepStore);
	if (hr == MAPI_E_NOT_FOUND) {
		hr = hrSuccess;	// No store: no delegate allowed!
		goto exit;
	}
	else if (hr != hrSuccess) {
		kc_perrorf("HrOpenRepresentStore failed", hr);
		goto exit;
	}
	hr = HrGetOneProp(lpUserStore, PR_MAILBOX_OWNER_NAME, &~lpUserOwnerName);
	if (hr != hrSuccess)
		ec_log_notice("CheckDelegate() PR_MAILBOX_OWNER_NAME(user) fetch failed: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
	hr = HrGetOneProp(lpRepStore, PR_MAILBOX_OWNER_NAME, &~lpRepOwnerName);
	if (hr != hrSuccess)
		ec_log_notice("CheckDelegate() PR_MAILBOX_OWNER_NAME(rep) fetch failed: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
	// ignore error, just a name for logging

	// open root container
	hr = lpRepStore->OpenEntry(0, nullptr, &iid_of(lpRepSubtree), 0, &ulObjType, &~lpRepSubtree);
	if (hr != hrSuccess) {
		ec_log_notice("CheckDelegate() OpenENtry(rep) failed: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}
	hr = HrGetOneProp(lpRepSubtree, PR_FREEBUSY_ENTRYIDS, &~lpRepFBProp);
	if (hr != hrSuccess) {
		ec_log_notice("CheckDelegate() HrGetOneProp(rep) failed: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	if (lpRepFBProp->Value.MVbin.cValues < 2) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	hr = lpRepSubtree->OpenEntry(lpRepFBProp->Value.MVbin.lpbin[1].cb,
	     reinterpret_cast<ENTRYID *>(lpRepFBProp->Value.MVbin.lpbin[1].lpb),
	     &iid_of(lpRepFBMessage), 0, &ulObjType, &~lpRepFBMessage);
	if (hr != hrSuccess) {
		ec_log_notice("CheckDelegate() OpenEntry(rep) failed: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}
	hr = HrGetOneProp(lpRepFBMessage, PR_SCHDINFO_DELEGATE_ENTRYIDS, &~lpDelegates);
	if (hr != hrSuccess) {
		ec_log_notice("CheckDelegate() HrGetOneProp failed: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}
	hr = HrCheckAllowedEntryIDArray("delegate", lpRepOwnerName ? lpRepOwnerName->Value.lpszW : L"<no name>",
	     lpAddrBook, owner, lpDelegates->Value.MVbin, &ulObjType, &bAllowed);
	if (hr != hrSuccess) {
		kc_perrorf("HrCheckAllowedEntryIDArray failed", hr);
		goto exit;
	}
	if (bAllowed)
		ec_log_info("Mail for user \"%ls\" is allowed on behalf of user \"%ls\"%s",
						lpUserOwnerName ? lpUserOwnerName->Value.lpszW : L"<no name>",
						lpRepOwnerName ? lpRepOwnerName->Value.lpszW : L"<no name>",
						(ulObjType != MAPI_DISTLIST)?"":" because of group");
exit:
	*lpbAllowed = bAllowed;
	// when any step failed, delegate is not setup correctly, so bAllowed == false
	hr = hrSuccess;
	if (bAllowed)
		*lppRepStore = lpRepStore.release();
	MAPIFreeBuffer(sSpoofEID.Value.bin.lpb);
	return hr;
}

/**
 * Copies the sent message to the delegate store. Returns the copy of lpMessage.
 *
 * @param[in]	lpMessage	The message to be copied to the delegate store in that "Sent Items" folder.
 * @param[in]	lpRepStore	The store of the delegate where the message will be copied.
 * @param[out]	lppRepMessage The new message in the delegate store.
 * @return		HRESULT
 */
static HRESULT CopyDelegateMessageToSentItems(LPMESSAGE lpMessage,
    LPMDB lpRepStore, LPMESSAGE *lppRepMessage)
{
	memory_ptr<SPropValue> lpSentItemsEntryID;
	object_ptr<IMAPIFolder> lpSentItems;
	ULONG ulObjType;
	object_ptr<IMessage> lpDestMsg;
	SPropValue sProp;

	auto hr = HrGetOneProp(lpRepStore, PR_IPM_SENTMAIL_ENTRYID, &~lpSentItemsEntryID);
	if (hr != hrSuccess) {
		ec_log_warn("Unable to find the representee's Sent Items folder: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	hr = lpRepStore->OpenEntry(lpSentItemsEntryID->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpSentItemsEntryID->Value.bin.lpb),
	     &IID_IMAPIFolder, MAPI_BEST_ACCESS, &ulObjType, &~lpSentItems);
	if (hr != hrSuccess) {
		ec_log_warn("Unable to open the representee's Sent Items folder: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	hr = lpSentItems->CreateMessage(nullptr, 0, &~lpDestMsg);
	if (hr != hrSuccess) {
		ec_log_warn("Unable to create the representee's message: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	hr = lpMessage->CopyTo(0, nullptr, nullptr, 0, nullptr, &IID_IMessage, lpDestMsg, 0, nullptr);
	if (FAILED(hr)) {
		ec_log_warn("Unable to copy the representee's message: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	sProp.ulPropTag = PR_MESSAGE_FLAGS;
	sProp.Value.ul = MSGFLAG_READ;
	hr = lpDestMsg->SetProps(1, &sProp, nullptr);
	if (hr != hrSuccess) {
		ec_log_warn("Unable to edit the representee's message: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	*lppRepMessage = lpDestMsg.release();
	ec_log_info("Copy placed in the representee's Sent Items folder");
	return hrSuccess;
}

/**
 * Delete the message from the outgoing queue. Should always be
 * called, unless the message should be retried later (SMTP server
 * temporarily not available or timed message).
 *
 * @param[in]	cbEntryId	Number of bytes in lpEntryId
 * @param[in]	lpEntryId	EntryID of the message to remove from outgoing queue.
 * @param[in]	lpMsgStore	Message store of the user containing the message of lpEntryId
 * @return		HRESULT
 */
static HRESULT PostSendProcessing(ULONG cbEntryId, const ENTRYID *lpEntryId,
    IMsgStore *lpMsgStore)
{
	object_ptr<IECSpooler> lpSpooler;
	auto hr = GetECObject(lpMsgStore, iid_of(lpSpooler), &~lpSpooler);
	if (hr != hrSuccess)
		return kc_perror("Unable to get PR_EC_OBJECT in post-send processing", hr);
	hr = lpSpooler->DeleteFromMasterOutgoingTable(cbEntryId, lpEntryId, EC_SUBMIT_MASTER);
	if (hr != hrSuccess)
		ec_log_warn("Could not remove invalid message from queue: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
	return hr;
}

static void lograw1(IMAPISession *ses, IAddrBook *ab, IMessage *msg,
    const struct sending_options &sopt)
{
	std::ostringstream os;
	auto ret = IMToINet(ses, ab, msg, os, sopt);
	if (ret != hrSuccess) {
		kc_perror("IMToINet", ret);
		return;
	}
	struct tm tm;
	char buf[64];
	gmtime_safe(time(nullptr), &tm);
	std::string fname = g_lpConfig->GetSetting("log_raw_message_path");
	if (CreatePath(fname.c_str()) < 0)
		ec_log_err("Could not mkdir \"%s\": %s\n", fname.c_str(), strerror(errno));
	strftime(buf, sizeof(buf), "/SMTP1_%Y%m%d%H%M%S_", &tm);
	fname += buf;
	snprintf(buf, sizeof(buf), "%08x.eml", rand_mt());
	fname += buf;
	std::unique_ptr<FILE, file_deleter> fp(fopen(fname.c_str(), "w"));
	if (fp == nullptr) {
		ec_log_warn("Cannot write to %s: %s", fname.c_str(), strerror(errno));
		return;
	}
	fputs(os.str().c_str(), fp.get());
}

/**
 * Using the given resources, sends the mail to the SMTP server.
 *
 * @param[in]	lpAdminSession	Kopano SYSTEM user MAPI session.
 * @param[in]	lpUserSession	MAPI Session of the user sending the mail.
 * @param[in]	lpServiceAdmin	IECServiceAdmin interface on the user's store.
 * @param[in]	lpSecurity		IECSecurity interface on the user's store.
 * @param[in]	lpUserStore		The IMsgStore interface of the user's store.
 * @param[in]	lpAddrBook		The Global Addressbook of the user.
 * @param[in]	lpMailer		ECSender object (inetmapi), used to send the mail.
 * @param[in]	cbMsgEntryId	Number of bytes in lpMsgEntryId
 * @param[in]	lpMsgEntryId	EntryID of the message to be sent.
 * @param[out]	lppMessage		The message that processed. Always returned if opened.
 *
 * @note The mail will be removed by the calling process when we return an error, except for the errors/warnings listed below.
 * @retval	hrSuccess	Mail was successful sent moved when when needed.
 * @retval	MAPI_E_WAIT	Mail has a specific timestamp when it should be sent.
 * @retval	MAPI_W_NO_SERVICE	The SMTP server is not responding correctly.
 */
static HRESULT ProcessMessage(IMAPISession *lpAdminSession,
    IMAPISession *lpUserSession, IECServiceAdmin *lpServiceAdmin,
    IECSecurity *lpSecurity, IMsgStore *lpUserStore, IAddrBook *lpAddrBook,
    ECSender *lpMailer, unsigned int cbMsgEntryId, const ENTRYID *lpMsgEntryId,
    IMessage **lppMessage, std::shared_ptr<ECLogger> logger, bool &doSentMail)
{
	object_ptr<IMessage> lpMessage;
	unsigned int ulObjType = 0, cbOwner = 0, cValuesMoveProps = 0;
	unsigned int ulCmpRes = 0, ulResult = 0;
	memory_ptr<ENTRYID> lpOwner;
	memory_ptr<ECUSER> lpUser;
	SPropValue		sPropSender[4];
	static constexpr const SizedSPropTagArray(5, sptaMoveReprProps) =
		{5, {PR_SENT_REPRESENTING_NAME_W,
		PR_SENT_REPRESENTING_ADDRTYPE_W,
		PR_SENT_REPRESENTING_EMAIL_ADDRESS_W,
		PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_SEARCH_KEY}};
	memory_ptr<SPropValue> lpMoveReprProps, lpPropOwner;
	bool bAllowSendAs = false, bAllowDelegate = false;
	object_ptr<IMsgStore> lpRepStore;
	object_ptr<IMessage> lpRepMessage;
	memory_ptr<SPropValue> lpRepEntryID, lpSubject, lpMsgSize;
	memory_ptr<SPropValue> lpAutoForward, lpMsgClass, lpDeferSendTime;
	memory_ptr<SPropValue> trash_eid, parent_entryid;
	PyMapiPluginFactory pyMapiPluginFactory;
	std::unique_ptr<pym_plugin_intf> ptrPyMapiPlugin;
	const char *cts = nullptr;
	ArchiveResult	archiveResult;
	sending_options sopt;

	imopt_default_sending_options(&sopt);

	// When sending messages, we want to minimize the use of tnef.
	// In case always_send_tnef is set to yes, we force tnef, otherwise we
	// minimize (set to no or minimal).
	if (!strcmp(g_lpConfig->GetSetting("always_send_tnef"), "minimal") ||
	    !parseBool(g_lpConfig->GetSetting("always_send_tnef")))
		sopt.use_tnef = -1;
	else
		sopt.use_tnef = 1;

	sopt.allow_send_to_everyone = parseBool(g_lpConfig->GetSetting("allow_send_to_everyone"));
	// Enable SMTP Delivery Status Notifications
	sopt.enable_dsn = parseBool(g_lpConfig->GetSetting("enable_dsn"));
	sopt.always_expand_distr_list = parseBool(g_lpConfig->GetSetting("expand_groups"));

	// Init plugin system
	auto hr = pyMapiPluginFactory.create_plugin(g_lpConfig.get(), "SpoolerPluginManager", &unique_tie(ptrPyMapiPlugin));
	if (hr != hrSuccess) {
		ec_log_crit("K-1733: Unable to initialize the spooler plugin system: %s (%x).",
			GetMAPIErrorMessage(hr), hr);
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	// Get the owner of the store
	hr = lpSecurity->GetOwner(&cbOwner, &~lpOwner);
	if (hr != hrSuccess) {
		kc_perror("Unable to get owner information", hr);
		goto exit;
	}
	// We now have the owner ID, get the owner information through the ServiceAdmin
	hr = lpServiceAdmin->GetUser(cbOwner, lpOwner, MAPI_UNICODE, &~lpUser);
	if (hr != hrSuccess) {
		kc_perror("Unable to get user information from store", hr);
		goto exit;
	}

	// open the message we need to send
	hr = lpUserStore->OpenEntry(cbMsgEntryId, reinterpret_cast<const ENTRYID *>(lpMsgEntryId),
	     &IID_IMessage, MAPI_BEST_ACCESS, &ulObjType, &~lpMessage);
	if (hr != hrSuccess) {
		ec_log_err("Could not open message in store from user %ls: %s (%x)",
			lpUser->lpszUsername, GetMAPIErrorMessage(hr), hr);
		goto exit;
	}
	hr = HrGetOneProp(lpUserStore, PR_IPM_WASTEBASKET_ENTRYID, &~trash_eid);
	if (hr != hrSuccess && hr != MAPI_E_NOT_FOUND) {
		kc_perror("Unable to get wastebasket entryid", hr);
		goto exit;
	}
	hr = HrGetOneProp(lpMessage, PR_PARENT_ENTRYID, &~parent_entryid);
	if (hr != hrSuccess && hr != MAPI_E_NOT_FOUND) {
		kc_perror("Unable to get parent entryid", hr);
		goto exit;
	}
	if (trash_eid != nullptr && parent_entryid != nullptr &&
	    trash_eid->Value.bin.cb == parent_entryid->Value.bin.cb &&
	    memcmp(trash_eid->Value.bin.lpb, parent_entryid->Value.bin.lpb, trash_eid->Value.bin.cb) == 0) {
		ec_log_err("Message is in Trash, will not send");
		doSentMail = false;
		goto exit;
	}

	/* Get subject for logging - ignore errors, we check for nullptr. */
	hr = HrGetOneProp(lpMessage, PR_SUBJECT_W, &~lpSubject);
	if (hr != hrSuccess && hr != MAPI_E_NOT_FOUND) {
		kc_perror("Unable to get subject", hr);
		goto exit;
	}
	hr = HrGetOneProp(lpMessage, PR_MESSAGE_SIZE, &~lpMsgSize);
	if (hr != hrSuccess && hr != MAPI_E_NOT_FOUND) {
		kc_perror("Unable to get message size", hr);
		goto exit;
	}

	// do we need to send the message already?
	hr = HrGetOneProp(lpMessage, PR_DEFERRED_SEND_TIME, &~lpDeferSendTime);
	if (hr == hrSuccess) {
		// check time
		auto sendat = FileTimeToUnixTime(lpDeferSendTime->Value.ft);
		if (time(nullptr) < sendat) {
			// should actually be logged just once .. but how?
			struct tm tmp;
			char timestring[256];

			localtime_r(&sendat, &tmp);
			strftime(timestring, 256, "%c", &tmp);
			ec_log_info("E-mail for user %ls, subject \"%ls\", should be sent later at \"%s\"",
				lpUser->lpszUsername, lpSubject ? lpSubject->Value.lpszW : L"<none>", timestring);
			hr = MAPI_E_WAIT;
			goto exit;
		}
	} else if (hr != MAPI_E_NOT_FOUND) {
		kc_perror("Unable to get PR_DEFERRED_SEND_TIME", hr);
		goto exit;
	}

	// fatal, all other log messages are otherwise somewhat meaningless
	if (ec_log_get()->Log(EC_LOGLEVEL_DEBUG))
		ec_log_debug("Sending e-mail for user %ls, subject: \"%ls\", size: %d",
			lpUser->lpszUsername, lpSubject ? lpSubject->Value.lpszW : L"<none>",
			lpMsgSize ? lpMsgSize->Value.ul : 0);
	else
		ec_log_info("Sending e-mail for user %ls, size: %d",
			lpUser->lpszUsername, lpMsgSize ? lpMsgSize->Value.ul : 0);

	/*
	   PR_SENDER_* maps to Sender:
	   PR_SENT_REPRESENTING_* maps to From:
	   Sender: field is optional, From: is mandatory
	   PR_SENDER_* is mandatory, and always set by us (will be overwritten if was set)
	   PR_SENT_REPRESENTING_* is optional, and set by outlook when the user modifies the From in outlook.
	*/
	// Set PR_SENT_REPRESENTING, as this is set on all "Sent" items and is the column
	// that is shown by default in Outlook's "Sent Items" folder
	if (HrGetOneProp(lpMessage, PR_SENT_REPRESENTING_ENTRYID, &~lpRepEntryID) != hrSuccess) {
		// set current user as sender (From header)
		sPropSender[0].ulPropTag = PR_SENT_REPRESENTING_NAME_W;
		sPropSender[0].Value.lpszW = (LPTSTR)lpUser->lpszFullName;
		sPropSender[1].ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE_W;
		sPropSender[1].Value.lpszW = (LPTSTR)L"ZARAFA";
		sPropSender[2].ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS_W;
		sPropSender[2].Value.lpszW = (LPTSTR)lpUser->lpszMailAddress;
		sPropSender[3].ulPropTag = PR_SENT_REPRESENTING_ENTRYID;
		sPropSender[3].Value.bin = lpUser->sUserId;

		HRESULT hr2 = lpMessage->SetProps(4, sPropSender, NULL);
		if (hr2 != hrSuccess) {
			kc_perror("Unable to set sender id for message", hr2);
			goto exit;
		}
	}
		// requested that mail is sent as somebody else
		// since we can have SMTP and ZARAFA entry IDs, we will open it, and get the

		// If this is a forwarded e-mail, then allow sending as the original sending e-mail address. Note that
		// this can be misused by MAPI client that just set PR_AUTO_FORWARDED. Since it would have been just as
		// easy for the client just to spoof their 'from' address via SMTP, we're allowing this for now. You can
		// completely turn it off via the 'allow_redirect_spoofing' setting.
	else if (strcmp(g_lpConfig->GetSetting("allow_redirect_spoofing"), "yes") == 0 &&
	    HrGetOneProp(lpMessage, PR_AUTO_FORWARDED, &~lpAutoForward) == hrSuccess &&
	    lpAutoForward->Value.b) {
			bAllowSendAs = true;
	} else {
		hr = HrGetOneProp(lpUserStore, PR_MAILBOX_OWNER_ENTRYID, &~lpPropOwner);
		if (hr != hrSuccess) {
			kc_perror("Unable to get Kopano mailbox owner id", hr);
			goto exit;
		}
		hr = lpAddrBook->CompareEntryIDs(lpPropOwner->Value.bin.cb, (LPENTRYID)lpPropOwner->Value.bin.lpb,
		     lpRepEntryID->Value.bin.cb, (LPENTRYID)lpRepEntryID->Value.bin.lpb, 0, &ulCmpRes);
		if (hr == hrSuccess && ulCmpRes == FALSE) {
			if (strcmp(g_lpConfig->GetSetting("always_send_delegates"), "yes") == 0) {
				// pre 6.20 behaviour
				bAllowDelegate = true;
				HrOpenRepresentStore(lpAddrBook, lpUserStore, lpAdminSession, lpRepEntryID->Value.bin, &~lpRepStore);
				// ignore error if unable to open, just the copy of the mail might possibily not be done.
			} else if(strcmp(g_lpConfig->GetSetting("allow_delegate_meeting_request"), "yes") == 0 &&
			    HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMsgClass) == hrSuccess &&
			    ((strcasecmp(lpMsgClass->Value.lpszA, "IPM.Schedule.Meeting.Request" ) == 0) ||
			    (strcasecmp(lpMsgClass->Value.lpszA, "IPM.Schedule.Meeting.Canceled" ) == 0))) {
				// Meeting request can always sent as 'on behalf of' (Zarafa and SMTP user).
				// This is needed if a user forward a meeting request. If you have permissions on a calendar,
				// you can always sent with 'on behalve of'. This behavior is like exchange.
				bAllowDelegate = true;
			} else {
				hr = CheckDelegate(lpAddrBook, lpUserStore, lpAdminSession, lpPropOwner->Value.bin,
				     lpRepEntryID->Value.bin, &bAllowDelegate, &~lpRepStore);
				if (hr != hrSuccess)
					goto exit;
			}
			if (!bAllowDelegate) {
				hr = CheckSendAs(lpAddrBook, lpUserStore, lpAdminSession, lpMailer, lpPropOwner->Value.bin,
				     lpRepEntryID->Value.bin, &bAllowSendAs, &~lpRepStore);
				if (hr != hrSuccess)
					goto exit;
				if (!bAllowSendAs) {
					ec_log_warn("E-mail for user %ls may not be sent, notifying user", lpUser->lpszUsername);
					HRESULT hr2 = SendUndeliverable(lpMailer, lpUserStore, lpMessage);
					if (hr2 != hrSuccess)
						ec_log_err("Unable to create undeliverable message for user %ls: %s (%x)",
							lpUser->lpszUsername, GetMAPIErrorMessage(hr2), hr2);
					// note: hr == hrSuccess, parent process will not send the undeliverable too
					goto exit;
				}
				// else {}: we are allowed to directly send
			}
			// else {}: allowed with 'on behalf of'
		}
		// else {}: owner and representing are the same, send as normal mail
	}

	// put storeowner info in PR_SENDER_ props, forces correct From data
	sPropSender[0].ulPropTag = PR_SENDER_NAME_W;
	sPropSender[0].Value.LPSZ = lpUser->lpszFullName;
	sPropSender[1].ulPropTag = PR_SENDER_ADDRTYPE_W;
	sPropSender[1].Value.LPSZ = const_cast<TCHAR *>(KC_T("ZARAFA"));
	sPropSender[2].ulPropTag = PR_SENDER_EMAIL_ADDRESS_W;
	sPropSender[2].Value.LPSZ = lpUser->lpszMailAddress;
	sPropSender[3].ulPropTag = PR_SENDER_ENTRYID;
	sPropSender[3].Value.bin = lpUser->sUserId;
	// @todo PR_SENDER_SEARCH_KEY

	hr = lpMessage->SetProps(4, sPropSender, NULL);
	if (hr != hrSuccess) {
		kc_perror("Unable to update message with sender", hr);
		goto exit;
	}
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess) {
		kc_perror("Unable to save message before sending", hr);
		goto exit;
	}

	cts = g_lpConfig->GetSetting("copy_delegate_mails");
	if (lpRepStore != nullptr && (strcmp(cts, "yes") == 0 ||
	    strcmp(cts, "move-to-rep") == 0))
		// copy the original message with the actual sender data
		// so you see the "on behalf of" in the sent-items version, even when send-as is used (see below)
		CopyDelegateMessageToSentItems(lpMessage, lpRepStore, &~lpRepMessage);
		// possible error is logged in function.
	if (lpRepStore != nullptr && strcmp(cts, "move-to-rep") == 0) {
		SizedSPropTagArray(1, dp) = {1, {PR_SENTMAIL_ENTRYID}};
		/* don't move to SentItems of delegate (leave in Outbox) */
		lpMessage->DeleteProps(dp, nullptr);
		/* Trigger deletion from Outbox */
		SPropValue pv;
		pv.ulPropTag = PR_DELETE_AFTER_SUBMIT; /* delete from Outbox */
		pv.Value.b = true;
		lpMessage->SetProps(1, &pv, nullptr);
	}

	if (bAllowSendAs) {
		// move PR_REPRESENTING to PR_SENDER_NAME
		hr = lpMessage->GetProps(sptaMoveReprProps, 0, &cValuesMoveProps, &~lpMoveReprProps);
		if (FAILED(hr)) {
			kc_perror("Unable to find sender information", hr);
			goto exit;
		}
		hr = lpMessage->DeleteProps(sptaMoveReprProps, NULL);
		if (FAILED(hr)) {
			kc_perror("Unable to remove sender information", hr);
			goto exit;
		}

		lpMoveReprProps[0].ulPropTag = CHANGE_PROP_TYPE(PR_SENDER_NAME_W,          PROP_TYPE(lpMoveReprProps[0].ulPropTag));
		lpMoveReprProps[1].ulPropTag = CHANGE_PROP_TYPE(PR_SENDER_ADDRTYPE_W,      PROP_TYPE(lpMoveReprProps[1].ulPropTag));
		lpMoveReprProps[2].ulPropTag = CHANGE_PROP_TYPE(PR_SENDER_EMAIL_ADDRESS_W, PROP_TYPE(lpMoveReprProps[2].ulPropTag));
		lpMoveReprProps[3].ulPropTag = CHANGE_PROP_TYPE(PR_SENDER_ENTRYID,         PROP_TYPE(lpMoveReprProps[3].ulPropTag));
		lpMoveReprProps[4].ulPropTag = CHANGE_PROP_TYPE(PR_SENDER_SEARCH_KEY,      PROP_TYPE(lpMoveReprProps[4].ulPropTag));

		hr = lpMessage->SetProps(5, lpMoveReprProps, NULL);
		if (FAILED(hr)) {
			kc_perror("Unable to update sender information", hr);
			goto exit;
		}

		/*
		 * Note: do not save these changes!
		 *
		 * If we're sending through Outlook, we're sending a copy of
		 * the message from the root container. Changes to this
		 * message make no sense, since it's deleted anyway.
		 *
		 * If we're sending through WebAccess, we're sending the real
		 * message, and bDoSentMail is true. This will move the
		 * message to the users sent-items folder (using the entryid
		 * from the message) and move it using its entryid. Since we
		 * didn't save these changes, the original unmodified version
		 * will be moved to the sent-items folder, and that will show
		 * the correct From/Sender data.
		 */
	}

	if (sopt.always_expand_distr_list) {
		// Expand recipients with ADDRTYPE=ZARAFA to multiple ADDRTYPE=SMTP recipients
		hr = ExpandRecipients(lpAddrBook, lpMessage);
		if(hr != hrSuccess)
			ec_log_warn("Unable to expand message recipient groups: %s (%x)",
				GetMAPIErrorMessage(hr), hr);
	}
	hr = RewriteRecipients(lpUserSession, lpMessage);
	if (hr != hrSuccess)
		ec_log_warn("Unable to rewrite recipients: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
	if (sopt.always_expand_distr_list) {
		// Only touch recips if we're expanding groups; the rationale is here that the user
		// has typed a recipient twice if we have duplicates and expand_groups = no, so that's
		// what the user wanted apparently. What's more, duplicate recips are filtered for RCPT TO
		// later.
		hr = UniqueRecipients(lpMessage);
		if (hr != hrSuccess)
			ec_log_warn("Unable to remove duplicate recipients: %s (%x)",
				GetMAPIErrorMessage(hr), hr);
	}
	RewriteQuotedRecipients(lpMessage);

	hr = ptrPyMapiPlugin->MessageProcessing("PreSending", lpUserSession, lpAddrBook, lpUserStore, NULL, lpMessage, &ulResult);
	if (hr != hrSuccess)
		goto exit;
	if (ulResult == MP_RETRY_LATER) {
		hr = MAPI_E_WAIT;
		goto exit;
	} else if (ulResult == MP_FAILED) {
		ec_log_crit("Plugin error, hook gives a failed error: %s (%x).",
			GetMAPIErrorMessage(ulResult), ulResult);
		hr = MAPI_E_CANCEL;
		goto exit;
	}

	// Archive the message
	if (parseBool(g_lpConfig->GetSetting("archive_on_send"))) {
		ArchivePtr ptrArchive;

		hr = Archive::Create(lpAdminSession, &ptrArchive);
		if (hr != hrSuccess) {
			kc_perror("Unable to instantiate archive object", hr);
			goto exit;
		}
		hr = ptrArchive->HrArchiveMessageForSending(lpMessage, &archiveResult, logger);
		if (hr != hrSuccess) {
			if (ptrArchive->HaveErrorMessage())
				lpMailer->setError(ptrArchive->GetErrorMessage());
			goto exit;
		}
	}

	if (parseBool(g_lpConfig->GetSetting("log_raw_message_stage1")))
		lograw1(lpUserSession, lpAddrBook, lpMessage, sopt);

	// Now hand message to library which will send it, inetmapi will handle addressbook
	hr = IMToINet(lpUserSession, lpAddrBook, lpMessage, lpMailer, sopt);
	// log using fatal, all other log messages are otherwise somewhat meaningless
	if (hr == MAPI_W_NO_SERVICE) {
		ec_log_warn("Unable to connect to SMTP server, retrying mail for user %ls later", lpUser->lpszUsername);
		goto exit;
	} else if (hr != hrSuccess) {
		ec_log_warn("E-mail for user %ls could not be sent, notifying user: %s (%x)",
			lpUser->lpszUsername, GetMAPIErrorMessage(hr), hr);
		hr = SendUndeliverable(lpMailer, lpUserStore, lpMessage);
		if (hr != hrSuccess)
			ec_log_err("Unable to create undeliverable message for user %ls: %s (%x)",
				lpUser->lpszUsername, GetMAPIErrorMessage(hr), hr);
		// we set hr to success, so the parent process does not create the undeliverable thing again
		hr = hrSuccess;
		goto exit;
	} else {
		ec_log_debug("E-mail for user %ls was accepted by SMTP server", lpUser->lpszUsername);
	}

	// If we have a repsenting message, save that now in the sent-items of that user
	if (lpRepMessage) {
		HRESULT hr2 = lpRepMessage->SaveChanges(0);
		if (hr2 != hrSuccess)
			kc_perror("The representee's mail copy could not be saved", hr2);
	}

exit:
	if (FAILED(hr))
		archiveResult.Undo(lpAdminSession);
	// We always return the processes message to the caller, whether it failed or not
	if (lpMessage)
		lpMessage->QueryInterface(IID_IMessage, (void**)lppMessage);
	return hr;
}

/**
 * Entry point, sends the mail for a user. Most of the time, it will
 * also move the sent mail to the "Sent Items" folder of the user.
 *
 * @param[in]	szUsername	The username to login as. This name is in unicode.
 * @param[in]	szSMTP		The SMTP server name or IP address to use.
 * @param[in]	szPath		The URI to the Kopano server.
 * @param[in]	cbMsgEntryId The number of bytes in lpMsgEntryId
 * @param[in]	lpMsgEntryId The EntryID of the message to send
 * @param[in]	bDoSentMail	true if the mail should be moved to the "Sent Items" folder of the user.
 * @return		HRESULT
 */
HRESULT ProcessMessageForked(const wchar_t *szUsername, const char *szSMTP,
    int ulPort, const char *szPath, unsigned int cbMsgEntryId,
    const ENTRYID *lpMsgEntryId, std::shared_ptr<ECLogger> logger,
    bool bDoSentMail)
{
	HRESULT			hr = hrSuccess;
	object_ptr<IMAPISession> lpAdminSession, lpUserSession;
	object_ptr<IAddrBook> lpAddrBook;
	std::unique_ptr<ECSender> lpMailer;
	object_ptr<IMsgStore> lpUserStore;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	object_ptr<IECSecurity> lpSecurity;
	memory_ptr<SPropValue> lpsProp;
	object_ptr<IMessage> lpMessage;

	lpMailer.reset(CreateSender(szSMTP, ulPort));
	if (!lpMailer) {
		hr = MAPI_E_NOT_ENOUGH_MEMORY;
		ec_log_notice("ProcessMessageForked(): CreateSender failed: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	// The Admin session is used for checking delegates and archiving
	hr = HrOpenECAdminSession(&~lpAdminSession, PROJECT_VERSION,
	     "mailer:admin", szPath, EC_PROFILE_FLAGS_NO_PUBLIC_STORE,
	     g_lpConfig->GetSetting("sslkey_file", "", NULL),
	     g_lpConfig->GetSetting("sslkey_pass", "", NULL));
	if (hr != hrSuccess) {
		kc_perror("Unable to open admin session", hr);
		goto exit;
	}

	/*
	 * For proper group expansion, we'll need to login as the
	 * user. When sending an email to group 'Everyone' it should not
	 * be possible to send the email to users that cannot be viewed
	 * (because they are in a different company).  By using a
	 * usersession for email sending we will let the server handle all
	 * permissions and can correctly resolve everything.
	 */
	hr = HrOpenECSession(&~lpUserSession, PROJECT_VERSION, "mailer",
	     szUsername, L"", szPath, EC_PROFILE_FLAGS_NO_PUBLIC_STORE,
	     g_lpConfig->GetSetting("sslkey_file", "", NULL),
	     g_lpConfig->GetSetting("sslkey_pass", "", NULL));
	if (hr != hrSuccess) {
		kc_perror("Unable to open user session", hr);
		goto exit;
	}
	hr = lpUserSession->OpenAddressBook(0, nullptr, AB_NO_DIALOG, &~lpAddrBook);
	if (hr != hrSuccess) {
		kc_perror("Unable to open addressbook", hr);
		goto exit;
	}
	hr = HrOpenDefaultStore(lpUserSession, &~lpUserStore);
	if (hr != hrSuccess) {
		kc_perror("Unable to open default store of user", hr);
		goto exit;
	}
	hr = GetECObject(lpUserStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (hr != hrSuccess) {
		kc_perror("ServiceAdmin interface not supported", hr);
		goto exit;
	}
	hr = GetECObject(lpUserStore, iid_of(lpSecurity), &~lpSecurity);
	if (hr != hrSuccess) {
		kc_perror("IID_IECSecurity not supported by store", hr);
		goto exit;
	}

	hr = ProcessMessage(lpAdminSession, lpUserSession, lpServiceAdmin,
	     lpSecurity, lpUserStore, lpAddrBook, lpMailer.get(), cbMsgEntryId,
	     lpMsgEntryId, &~lpMessage, logger, bDoSentMail);
	if (hr != hrSuccess && hr != MAPI_E_WAIT && hr != MAPI_W_NO_SERVICE && lpMessage) {
		// use lpMailer to set body in SendUndeliverable
		if (!lpMailer->haveError())
			lpMailer->setError(format(KC_A("Error found while trying to send your message: %s (%x)"), GetMAPIErrorMessage(hr), hr));
		hr = SendUndeliverable(lpMailer.get(), lpUserStore, lpMessage);
		if (hr != hrSuccess) {
			// dont make parent complain too
			hr = hrSuccess;
			goto exit;
		}
	}

exit:
	// The following code is after the exit tag because we *always* want to clean up the message from the outgoing queue, not
	// just when it was sent correctly. This also means we should do post-sending processing (DoSentMail()).
	// Ignore error, we want to give the possible failed hr back to the main process. Logging is already done.
	if (hr != MAPI_W_NO_SERVICE && hr != MAPI_E_WAIT) {
		if (lpMsgEntryId && lpUserStore)
			PostSendProcessing(cbMsgEntryId, lpMsgEntryId, lpUserStore);
		if (bDoSentMail && lpUserSession && lpMessage)
			DoSentMail(NULL, lpUserStore, 0, std::move(lpMessage));
	}
	return hr;
}
