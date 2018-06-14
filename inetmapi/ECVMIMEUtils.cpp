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
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <exception>
#include <memory>
#include <iostream>
#include <string>
#include "ECVMIMEUtils.h"
#include "MAPISMTPTransport.h"
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/ECRestriction.h>
#include <kopano/MAPIErrors.h>
#include <kopano/memory.hpp>
#include <kopano/charset/convert.h>

#include <kopano/stringutil.h>

#include <mapi.h>
#include <mapitags.h>
#include <mapidefs.h>
#include <mapiutil.h>
#include <mapix.h>
#include <kopano/mapiext.h>
#include <kopano/EMSAbTag.h>
#include <kopano/ECABEntryID.h>
#include <kopano/mapi_ptr.h>
#include <vmime/base.hpp>

using namespace KC::string_literals;

namespace KC {

class mapiTimeoutHandler : public vmime::net::timeoutHandler {
public:
	virtual ~mapiTimeoutHandler(void) = default;

	// @todo add logging
	virtual bool isTimeOut() _kc_override { return getTime() >= (m_last + 5*60); };
	virtual void resetTimeOut() _kc_override { m_last = getTime(); };
	virtual bool handleTimeOut() _kc_override { return false; };

	const unsigned int getTime() const {
		return vmime::platform::getHandler()->getUnixTime();
	}

private:
	unsigned int m_last = 0;
};

class mapiTimeoutHandlerFactory : public vmime::net::timeoutHandlerFactory {
public:
	vmime::shared_ptr<vmime::net::timeoutHandler> create(void) override
	{
		return vmime::make_shared<mapiTimeoutHandler>();
	};
};

ECVMIMESender::ECVMIMESender(const std::string &host, int port) :
    ECSender(host, port)
{
}

/**
 * Adds all the recipients from a table into the passed recipient list
 *
 * @param lpAdrBook Pointer to the addressbook for the user sending the message (important for multi-tenancy separation)
 * @param lpTable Table to read recipients from
 * @param recipients Reference to list of recipients to append to
 * @param setGroups Set of groups already processed, used for loop-detection in nested expansion
 * @param setRecips Set of recipients already processed, used for duplicate-recip detection
 * @param bAllowEveryone Allow sending to 'everyone'
 *
 * This function takes a MAPI table, reads all items from it, expands any groups and adds all expanded recipients into the passed
 * recipient table. Group expansion is recursive.
 */
HRESULT ECVMIMESender::HrAddRecipsFromTable(LPADRBOOK lpAdrBook, IMAPITable *lpTable, vmime::mailboxList &recipients, std::set<std::wstring> &setGroups, std::set<std::wstring> &setRecips, bool bAllowEveryone, bool bAlwaysExpandDistributionList)
{
	rowset_ptr lpRowSet;
	std::wstring strName, strEmail, strType;
	HRESULT hr = lpTable->QueryRows(-1, 0, &~lpRowSet);
	if (hr != hrSuccess)
		return hr;

	// Get all recipients from the group
	for (ULONG i = 0; i < lpRowSet->cRows; ++i) {
		auto lpPropObjectType = lpRowSet[i].cfind(PR_OBJECT_TYPE);

		// see if there's an e-mail address associated with the list
		// if that's the case, then we send to that address and not all individual recipients in that list
		bool bAddrFetchSuccess = HrGetAddress(lpAdrBook, lpRowSet[i].lpProps, lpRowSet[i].cValues, PR_ENTRYID, PR_DISPLAY_NAME_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, strName, strType, strEmail) == hrSuccess && !strEmail.empty();
		bool bItemIsAUser = lpPropObjectType == NULL || lpPropObjectType->Value.ul == MAPI_MAILUSER;

		if (bAddrFetchSuccess && bItemIsAUser) {
			if (setRecips.find(strEmail) == setRecips.end()) {
				recipients.appendMailbox(vmime::make_shared<vmime::mailbox>(convert_to<std::string>(strEmail)));
				setRecips.emplace(strEmail);
				ec_log_debug("RCPT TO: %ls", strEmail.c_str());	
			}
			continue;
		}
		if (lpPropObjectType == nullptr ||
		    lpPropObjectType->Value.ul != MAPI_DISTLIST)
			continue;

		// Group
		auto lpGroupName = lpRowSet[i].cfind(PR_EMAIL_ADDRESS_W);
		auto lpGroupEntryID = lpRowSet[i].cfind(PR_ENTRYID);
		if (lpGroupName == nullptr || lpGroupEntryID == nullptr)
			return MAPI_E_NOT_FOUND;
	
		if (bAllowEveryone == false) {
			bool bEveryone = false;
			
			if (EntryIdIsEveryone(lpGroupEntryID->Value.bin.cb, (LPENTRYID)lpGroupEntryID->Value.bin.lpb, &bEveryone) == hrSuccess && bEveryone) {
				ec_log_err("Denying send to Everyone");
				error = L"You are not allowed to send to the \"Everyone\" group"s;
				return MAPI_E_NO_ACCESS;
			}
		}

		if (bAlwaysExpandDistributionList || !bAddrFetchSuccess || wcscasecmp(strType.c_str(), L"SMTP") != 0) {
			// Recursively expand all recipients in the group
			hr = HrExpandGroup(lpAdrBook, lpGroupName, lpGroupEntryID, recipients, setGroups, setRecips, bAllowEveryone);

			if (hr == MAPI_E_TOO_COMPLEX || hr == MAPI_E_INVALID_PARAMETER) {
				// ignore group nesting loop and non user/group types (e.g. companies)
				hr = hrSuccess;
			} else if (hr != hrSuccess) {
				// e.g. MAPI_E_NOT_FOUND
				ec_log_err("Error while expanding group \"%ls\": %s (%x)",
					lpGroupName->Value.lpszW, GetMAPIErrorMessage(hr), hr);
				error = L"Error in group \""s + lpGroupName->Value.lpszW + L"\", unable to send e-mail";
				return hr;
			}
		} else if (setRecips.find(strEmail) == setRecips.end()) {
			recipients.appendMailbox(vmime::make_shared<vmime::mailbox>(convert_to<std::string>(strEmail)));
			setRecips.emplace(strEmail);
			ec_log_debug("Sending to group-address %s instead of expanded list",
				convert_to<std::string>(strEmail).c_str());
		}
	}
	return hr;
}

/**
 * Takes a group entry of a recipient table and expands the recipients for the group recursively by adding them to the recipients list
 *
 * @param lpAdrBook Pointer to the addressbook for the user sending the message (important for multi-tenancy separation)
 * @param lpGroupName Pointer to PR_EMAIL_ADDRESS_W entry for the recipient in the recipient table
 * @param lpGroupEntryId Pointer to PR_ENTRYID entry for the recipient in the recipient table
 * @param recipients Reference to list of VMIME recipients to be appended to
 * @param setGroups Set of already-processed groups (used for loop detecting in group expansion)
 * @param setRecips Set of recipients already processed, used for duplicate-recip detection
 *
 * This function expands the specified group by opening the group and adding all user entries to the recipients list, and
 * recursively expanding groups in the group. 
 *
 * lpGroupEntryID may be NULL, in which case lpGroupName is used to resolve the group via the addressbook. If
 * both parameters are set, lpGroupEntryID is used, and lpGroupName is ignored.
 */
HRESULT ECVMIMESender::HrExpandGroup(LPADRBOOK lpAdrBook,
    const SPropValue *lpGroupName, const SPropValue *lpGroupEntryID,
    vmime::mailboxList &recipients, std::set<std::wstring> &setGroups,
    std::set<std::wstring> &setRecips, bool bAllowEveryone)
{
	object_ptr<IDistList> lpGroup;
	ULONG ulType = 0;
	object_ptr<IMAPITable> lpTable;
	memory_ptr<SPropValue> lpEmailAddress;

	if (lpGroupEntryID == nullptr || lpAdrBook->OpenEntry(lpGroupEntryID->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpGroupEntryID->Value.bin.lpb), &iid_of(lpGroup), 0, &ulType, &~lpGroup) != hrSuccess || ulType != MAPI_DISTLIST) {
		// Entry id for group was not given, or the group could not be opened, or the entryid was not a group (eg one-off entryid)
		// Therefore resolve group name, and open that instead.
		if (lpGroupName == nullptr)
			return MAPI_E_NOT_FOUND;

		rowset_ptr lpRows;
		auto hr = MAPIAllocateBuffer(CbNewSRowSet(1), &~lpRows);
		if (hr != hrSuccess)
			return hr;
		lpRows->cRows = 0;
		if ((hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpRows->aRow[0].lpProps)) != hrSuccess)
			return hr;
		++lpRows->cRows;
		lpRows->aRow[0].cValues = 1;
		lpRows->aRow[0].lpProps[0].ulPropTag = PR_DISPLAY_NAME_W;
		lpRows[0].lpProps[0].Value.lpszW = lpGroupName->Value.lpszW;
		hr = lpAdrBook->ResolveName(0, MAPI_UNICODE | EMS_AB_ADDRESS_LOOKUP, NULL, reinterpret_cast<ADRLIST *>(lpRows.get()));
		if(hr != hrSuccess)
			return hr;
		lpGroupEntryID = lpRows[0].cfind(PR_ENTRYID);
		if (lpGroupEntryID == nullptr)
			return MAPI_E_NOT_FOUND;

		// Open resolved entry
		hr = lpAdrBook->OpenEntry(lpGroupEntryID->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpGroupEntryID->Value.bin.lpb), &iid_of(lpGroup), 0, &ulType, &~lpGroup);
		if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED ||
		    (hr == hrSuccess && ulType != MAPI_DISTLIST)) {
			ec_log_debug("Expected group, but opened type %d", ulType);
			return MAPI_E_INVALID_PARAMETER;
		}
		if (hr != hrSuccess)
			return hr;
	}
	auto hr = HrGetOneProp(lpGroup, PR_EMAIL_ADDRESS_W, &~lpEmailAddress);
	if(hr != hrSuccess)
		return hr;
	if (setGroups.find(lpEmailAddress->Value.lpszW) != setGroups.end())
		// Group loops in nesting
		return MAPI_E_TOO_COMPLEX;
	
	// Add group name to list of processed groups
	setGroups.emplace(lpEmailAddress->Value.lpszW);
	hr = lpGroup->GetContentsTable(MAPI_UNICODE, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	return HrAddRecipsFromTable(lpAdrBook, lpTable, recipients, setGroups,
	       setRecips, bAllowEveryone, true);
}

HRESULT ECVMIMESender::HrMakeRecipientsList(LPADRBOOK lpAdrBook,
    LPMESSAGE lpMessage, vmime::shared_ptr<vmime::message> vmMessage,
    vmime::mailboxList &recipients, bool bAllowEveryone,
    bool bAlwaysExpandDistrList)
{
	object_ptr<IMAPITable> lpRTable;
	bool bResend = false;
	std::set<std::wstring> setGroups; // Set of groups to make sure we don't get into an expansion-loop
	std::set<std::wstring> setRecips; // Set of recipients to make sure we don't send two identical RCPT TOs
	memory_ptr<SPropValue> lpMessageFlags;
	
	auto hr = HrGetOneProp(lpMessage, PR_MESSAGE_FLAGS, &~lpMessageFlags);
	if (hr != hrSuccess)
		return hr;
	if(lpMessageFlags->Value.ul & MSGFLAG_RESEND)
		bResend = true;
	hr = lpMessage->GetRecipientTable(MAPI_UNICODE, &~lpRTable);
	if (hr != hrSuccess)
		return hr;

	// When resending, only send to MAPI_P1 recipients
	if(bResend) {
		SPropValue sRestrictProp;
		sRestrictProp.ulPropTag = PR_RECIPIENT_TYPE;
		sRestrictProp.Value.ul = MAPI_P1;

		hr = ECPropertyRestriction(RELOP_EQ, PR_RECIPIENT_TYPE,
		     &sRestrictProp, ECRestriction::Cheap)
		     .RestrictTable(lpRTable, TBL_BATCH);
		if (hr != hrSuccess)
			return hr;
	}
	return HrAddRecipsFromTable(lpAdrBook, lpRTable, recipients, setGroups,
	       setRecips, bAllowEveryone, bAlwaysExpandDistrList);
}

// This function does not catch the vmime exception
// it should be handled by the calling party.

HRESULT ECVMIMESender::sendMail(LPADRBOOK lpAdrBook, LPMESSAGE lpMessage,
    vmime::shared_ptr<vmime::message> vmMessage, bool bAllowEveryone,
    bool bAlwaysExpandDistrList)
{
	HRESULT hr = hrSuccess;
	vmime::mailbox expeditor;
	vmime::mailboxList recipients;

	if (lpMessage == NULL || vmMessage == NULL)
		return MAPI_E_INVALID_PARAMETER;

	smtpresult = 0;
	error.clear();

	try {
		// Session initialization (global properties)
		auto vmSession = vmime::net::session::create();

		// set the server address and port, plus type of service by use of url
		// and get our special mapismtp mailer
		vmime::utility::url url("mapismtp", smtphost, smtpport);
		auto vmTransport = vmSession->getTransport(url);
		vmTransport->setTimeoutHandlerFactory(vmime::make_shared<mapiTimeoutHandlerFactory>());

		/* cast to access interface extras */
		auto mapiTransport = vmime::dynamicCast<vmime::net::smtp::MAPISMTPTransport>(vmTransport);

		// get expeditor for 'mail from:' smtp command
		if (vmMessage->getHeader()->hasField(vmime::fields::FROM))
			expeditor = *vmime::dynamicCast<vmime::mailbox>(vmMessage->getHeader()->findField(vmime::fields::FROM)->getValue());
		else
			throw vmime::exceptions::no_expeditor();

		if (expeditor.isEmpty()) {
			// cancel this message as unsendable, would otherwise be thrown out of transport::send()
			error = L"No expeditor in e-mail";
			return MAPI_W_CANCEL_MESSAGE;
		}

		hr = HrMakeRecipientsList(lpAdrBook, lpMessage, vmMessage, recipients, bAllowEveryone, bAlwaysExpandDistrList);
		if (hr != hrSuccess)
			return hr;

		if (recipients.isEmpty()) {
			// cancel this message as unsendable, would otherwise be thrown out of transport::send()
			error = L"No recipients in e-mail";
			return MAPI_W_CANCEL_MESSAGE;
		}

		// Remove BCC headers from the message we're about to send
		if (vmMessage->getHeader()->hasField(vmime::fields::BCC)) {
			auto bcc = vmMessage->getHeader()->findField(vmime::fields::BCC);
			vmMessage->getHeader()->removeField(bcc);
		}

		// Delivery report request
		SPropValuePtr ptrDeliveryReport;
		if (mapiTransport != nullptr &&
		    HrGetOneProp(lpMessage, PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED, &~ptrDeliveryReport) == hrSuccess &&
		    ptrDeliveryReport->Value.b == TRUE)
			mapiTransport->requestDSN(true, "");

		// Generate the message, "stream" it and delegate the sending
		// to the generic send() function.
		std::ostringstream oss;
		vmime::utility::outputStreamAdapter ossAdapter(oss);
		vmMessage->generate(imopt_default_genctx(), ossAdapter);
		/*
		 * This would be the place for spooler's
		 * log_raw_message_stage2, but this so deep in inetmapi…
		 */
		const std::string &str(oss.str()); // copy?
		vmime::utility::inputStreamStringAdapter isAdapter(str); // direct oss.str() ?
		
		// send the email already!
		bool ok = false;
		try {
			vmTransport->connect();
		} catch (const vmime::exception &e) {
			// special error, smtp server not respoding, so try later again
			ec_log_err("Connect to SMTP: %s. E-Mail will be tried again later.", e.what());
			return MAPI_W_NO_SERVICE;
		}

		try {
			vmTransport->send(expeditor, recipients, isAdapter, str.length(), NULL);
			vmTransport->disconnect();
			ok = true;
		} catch (const vmime::exceptions::command_error &e) {
			if (mapiTransport != NULL) {
				mPermanentFailedRecipients = mapiTransport->getPermanentFailedRecipients();
				mTemporaryFailedRecipients = mapiTransport->getTemporaryFailedRecipients();
			}
			ec_log_err("SMTP: %s Response: %s", e.what(), e.response().c_str());
			smtpresult = atoi(e.response().substr(0, e.response().find_first_of(" ")).c_str());
			error = convert_to<std::wstring>(e.response());
			// message should be cancelled, unsendable, test by smtp result code.
			return MAPI_W_CANCEL_MESSAGE;
		} catch (const vmime::exceptions::no_recipient &e) {
			if (mapiTransport != NULL) {
				mPermanentFailedRecipients = mapiTransport->getPermanentFailedRecipients();
				mTemporaryFailedRecipients = mapiTransport->getTemporaryFailedRecipients();
			}
			ec_log_err("SMTP: %s Name: %s", e.what(), e.name());
			//smtpresult = atoi(e.response().substr(0, e.response().find_first_of(" ")).c_str());
			//error = convert_to<std::wstring>(e.response());
			// message should be cancelled, unsendable, test by smtp result code.
			return MAPI_W_CANCEL_MESSAGE;
		} catch (const vmime::exception &e) {
		}

		if (mapiTransport != NULL) {
			/*
			 * Multiple invalid recipients can cause the opponent
			 * mail server (e.g. Postfix) to disconnect. In that
			 * case, fail those recipients.
			 */
			mPermanentFailedRecipients = mapiTransport->getPermanentFailedRecipients();
			mTemporaryFailedRecipients = mapiTransport->getTemporaryFailedRecipients();

			if (mPermanentFailedRecipients.size() == static_cast<size_t>(recipients.getMailboxCount())) {
				ec_log_err("SMTP: e-mail will be not be tried again: all recipients failed.");
				return MAPI_W_CANCEL_MESSAGE;
			} else if (!mTemporaryFailedRecipients.empty()) {
				ec_log_err("SMTP: e-mail will be tried again: some recipients failed.");
				return MAPI_W_PARTIAL_COMPLETION;
			} else if (!mPermanentFailedRecipients.empty()) {
				ec_log_err("SMTP: some recipients failed.");
				return MAPI_W_PARTIAL_COMPLETION;
			} else if (mTemporaryFailedRecipients.empty() && mPermanentFailedRecipients.empty() && !ok) {
				// special error, smtp server not respoding, so try later again
				ec_log_err("SMTP: e-mail will be tried again.");
				return MAPI_W_NO_SERVICE;
			}
		}
	} catch (const vmime::exception &e) {
		// connection_greeting_error, ...?
		ec_log_err("%s", e.what());
		error = convert_to<std::wstring>(e.what());
		return MAPI_E_NETWORK_ERROR;
	} catch (const std::exception &e) {
		ec_log_err("%s", e.what());
		error = convert_to<std::wstring>(e.what());
		return MAPI_E_NETWORK_ERROR;
	}
	return hr;
}

vmime::parsingContext imopt_default_parsectx()
{
	vmime::parsingContext c;
	c.setInternationalizedEmailSupport(true);
	return c;
}

vmime::generationContext imopt_default_genctx()
{
	vmime::generationContext c;
	/* Outlook gets confused by "Content-Id: \n<...>" */
	c.setWrapMessageId(false);
	return c;
}

} /* namespace */
