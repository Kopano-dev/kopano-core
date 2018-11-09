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

// Damn windows header defines max which break C++ header files
#undef max
#include <memory>
#include <iostream>

#include "ECVMIMEUtils.h"
#include "MAPISMTPTransport.h"
#include <kopano/CommonUtil.h>
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

using namespace std;

class mapiTimeoutHandler : public vmime::net::timeoutHandler
{
public:
	mapiTimeoutHandler() : m_last(0) {};
	virtual ~mapiTimeoutHandler() {};

	// @todo add logging
	virtual bool isTimeOut() { return getTime() >= (m_last + 5*60); };
	virtual void resetTimeOut() { m_last = getTime(); };
	virtual bool handleTimeOut() { return false; };

	const unsigned int getTime() const {
		return vmime::platform::getHandler()->getUnixTime();
	}

private:
	unsigned int m_last;
};

class mapiTimeoutHandlerFactory : public vmime::net::timeoutHandlerFactory
{
public:
	vmime::shared_ptr<vmime::net::timeoutHandler> create(void)
	{
		return vmime::make_shared<mapiTimeoutHandler>();
	};
};

ECVMIMESender::ECVMIMESender(ECLogger *newlpLogger, std::string strSMTPHost, int port) : ECSender(newlpLogger, strSMTPHost, port) {
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
	HRESULT hr = hrSuccess;
	LPSRowSet lpRowSet = NULL;
	std::wstring strName, strEmail, strType;

	hr = lpTable->QueryRows(-1, 0, &lpRowSet);
	if (hr != hrSuccess)
		goto exit;

	// Get all recipients from the group
	for (ULONG i = 0; i < lpRowSet->cRows; ++i) {
		LPSPropValue lpPropObjectType = PpropFindProp( lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_OBJECT_TYPE);

		// see if there's an e-mail address associated with the list
		// if that's the case, then we send to that address and not all individual recipients in that list
		bool bAddrFetchSuccess = HrGetAddress(lpAdrBook, lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_ENTRYID, PR_DISPLAY_NAME_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, strName, strType, strEmail) == hrSuccess && !strEmail.empty();

		bool bItemIsAUser = lpPropObjectType == NULL || lpPropObjectType->Value.ul == MAPI_MAILUSER;

		if (bAddrFetchSuccess && bItemIsAUser) {
			if (setRecips.find(strEmail) == setRecips.end()) {
				recipients.appendMailbox(vmime::make_shared<vmime::mailbox>(convert_to<string>(strEmail)));
				setRecips.insert(strEmail);
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "RCPT TO: %ls", strEmail.c_str());	
			}
		}
		else if (lpPropObjectType->Value.ul == MAPI_DISTLIST) {
			// Group
			LPSPropValue lpGroupName = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_EMAIL_ADDRESS_W);
			LPSPropValue lpGroupEntryID = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_ENTRYID);
			if (lpGroupName == NULL || lpGroupEntryID == NULL) {
				hr = MAPI_E_NOT_FOUND;
				goto exit;
			}
	
			if(bAllowEveryone == false) {
				bool bEveryone = false;
				
				if(EntryIdIsEveryone(lpGroupEntryID->Value.bin.cb, (LPENTRYID)lpGroupEntryID->Value.bin.lpb, &bEveryone) == hrSuccess && bEveryone) {
					lpLogger->Log(EC_LOGLEVEL_ERROR, "Denying send to Everyone");
					error = std::wstring(L"You are not allowed to send to the 'Everyone' group");
					hr = MAPI_E_NO_ACCESS;
					goto exit;
				}
			}

			if (bAlwaysExpandDistributionList || !bAddrFetchSuccess || wcscasecmp(strType.c_str(), L"SMTP") != 0) {
				// Recursively expand all recipients in the group
				hr = HrExpandGroup(lpAdrBook, lpGroupName, lpGroupEntryID, recipients, setGroups, setRecips, bAllowEveryone);

				if (hr == MAPI_E_TOO_COMPLEX || hr == MAPI_E_INVALID_PARAMETER) {
					// ignore group nesting loop and non user/group types (eg. companies)
					hr = hrSuccess;
				} else if (hr != hrSuccess) {
					// eg. MAPI_E_NOT_FOUND
					lpLogger->Log(EC_LOGLEVEL_ERROR, "Error while expanding group. Group: %ls, error: 0x%08x", lpGroupName ? lpGroupName->Value.lpszW : L"<unknown>", hr);
					error = std::wstring(L"Error in group '") + (lpGroupName ? lpGroupName->Value.lpszW : L"<unknown>") + L"', unable to send e-mail";
					goto exit;
				}
			} else {
				if (setRecips.find(strEmail) == setRecips.end()) {
					recipients.appendMailbox(vmime::make_shared<vmime::mailbox>(convert_to<string>(strEmail)));
					setRecips.insert(strEmail);

					lpLogger->Log(EC_LOGLEVEL_DEBUG, "Sending to group-address %s instead of expanded list", convert_to<std::string>(strEmail).c_str());
				}
			}
		}
	}

exit:
	if(lpRowSet)
		FreeProws(lpRowSet);
		
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
HRESULT ECVMIMESender::HrExpandGroup(LPADRBOOK lpAdrBook, LPSPropValue lpGroupName, LPSPropValue lpGroupEntryID, vmime::mailboxList &recipients, std::set<std::wstring> &setGroups, std::set<std::wstring> &setRecips, bool bAllowEveryone)
{
	HRESULT hr = hrSuccess;
	IDistList *lpGroup = NULL;
	ULONG ulType = 0;
	IMAPITable *lpTable = NULL;
	LPSRowSet lpRows = NULL;
	LPSPropValue lpEmailAddress = NULL;

	if(lpGroupEntryID == NULL || lpAdrBook->OpenEntry(lpGroupEntryID->Value.bin.cb, (LPENTRYID)lpGroupEntryID->Value.bin.lpb, NULL, 0, &ulType, (IUnknown **)&lpGroup) != hrSuccess || ulType != MAPI_DISTLIST) {
		// Entry id for group was not given, or the group could not be opened, or the entryid was not a group (eg one-off entryid)
		// Therefore resolve group name, and open that instead.
		
		if(lpGroupName == NULL) {
			hr = MAPI_E_NOT_FOUND;
			goto exit;
		}
		
		if ((hr = MAPIAllocateBuffer(sizeof(SRowSet), (void **)&lpRows)) != hrSuccess)
			goto exit;
		lpRows->cRows = 1;
		if ((hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpRows->aRow[0].lpProps)) != hrSuccess)
			goto exit;
		lpRows->aRow[0].cValues = 1;
		
		lpRows->aRow[0].lpProps[0].ulPropTag = PR_DISPLAY_NAME_W;
		lpRows->aRow[0].lpProps[0].Value.lpszW = lpGroupName->Value.lpszW;
		
		hr = lpAdrBook->ResolveName(0, MAPI_UNICODE | EMS_AB_ADDRESS_LOOKUP, NULL, (LPADRLIST)lpRows);
		if(hr != hrSuccess)
			goto exit;
			
		lpGroupEntryID = PpropFindProp(lpRows->aRow[0].lpProps, lpRows->aRow[0].cValues, PR_ENTRYID);
		if(!lpGroupEntryID) {
			hr = MAPI_E_NOT_FOUND;
			goto exit;
		}

		if (lpGroup)
			lpGroup->Release();
		lpGroup = NULL;

		// Open resolved entry
		hr = lpAdrBook->OpenEntry(lpGroupEntryID->Value.bin.cb, (LPENTRYID)lpGroupEntryID->Value.bin.lpb, NULL, 0, &ulType, (IUnknown **)&lpGroup);
		if(hr != hrSuccess)
			goto exit;
			
		if(ulType != MAPI_DISTLIST) {
			lpLogger->Log(EC_LOGLEVEL_DEBUG, "Expected group, but opened type %d", ulType);	
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
	}
	
	hr = HrGetOneProp(lpGroup, PR_EMAIL_ADDRESS_W, &lpEmailAddress);
	if(hr != hrSuccess)
		goto exit;
		
	if(setGroups.find(lpEmailAddress->Value.lpszW) != setGroups.end()) {
		// Group loops in nesting
		hr = MAPI_E_TOO_COMPLEX;
		goto exit;
	}
	
	// Add group name to list of processed groups
	setGroups.insert(lpEmailAddress->Value.lpszW);
	
	hr = lpGroup->GetContentsTable(MAPI_UNICODE, &lpTable);
	if(hr != hrSuccess)
		goto exit;

	hr = HrAddRecipsFromTable(lpAdrBook, lpTable, recipients, setGroups, setRecips, bAllowEveryone, true);
	if(hr != hrSuccess)
		goto exit;
	
exit:
	if(lpTable)
		lpTable->Release();
		
	if(lpRows)
		FreeProws(lpRows);
		
	if(lpGroup)
		lpGroup->Release();
	MAPIFreeBuffer(lpEmailAddress);
	return hr;
}

HRESULT ECVMIMESender::HrMakeRecipientsList(LPADRBOOK lpAdrBook,
    LPMESSAGE lpMessage, vmime::shared_ptr<vmime::message> vmMessage,
    vmime::mailboxList &recipients, bool bAllowEveryone,
    bool bAlwaysExpandDistrList)
{
	HRESULT hr = hrSuccess;
	SRestriction sRestriction;
	SPropValue sRestrictProp;
	LPMAPITABLE lpRTable = NULL;
	bool bResend = false;
	std::set<std::wstring> setGroups; // Set of groups to make sure we don't get into an expansion-loop
	std::set<std::wstring> setRecips; // Set of recipients to make sure we don't send two identical RCPT TO's
	LPSPropValue lpMessageFlags = NULL;
	
	hr = HrGetOneProp(lpMessage, PR_MESSAGE_FLAGS, &lpMessageFlags);
	if (hr != hrSuccess)
		goto exit;
		
	if(lpMessageFlags->Value.ul & MSGFLAG_RESEND)
		bResend = true;
	
	hr = lpMessage->GetRecipientTable(MAPI_UNICODE, &lpRTable);
	if (hr != hrSuccess)
		goto exit;

	// When resending, only send to MAPI_P1 recipients
	if(bResend) {
		sRestriction.rt = RES_PROPERTY;
		sRestriction.res.resProperty.relop = RELOP_EQ;
		sRestriction.res.resProperty.ulPropTag = PR_RECIPIENT_TYPE;
		sRestriction.res.resProperty.lpProp = &sRestrictProp;

		sRestrictProp.ulPropTag = PR_RECIPIENT_TYPE;
		sRestrictProp.Value.ul = MAPI_P1;

		hr = lpRTable->Restrict(&sRestriction, TBL_BATCH);
		if (hr != hrSuccess)
			goto exit;
	}

	hr = HrAddRecipsFromTable(lpAdrBook, lpRTable, recipients, setGroups, setRecips, bAllowEveryone, bAlwaysExpandDistrList);
	if (hr != hrSuccess)
		goto exit;
	
exit:
	MAPIFreeBuffer(lpMessageFlags);
	if (lpRTable)
		lpRTable->Release();

	return hr;
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
	vmime::shared_ptr<vmime::messaging::session> vmSession;
	vmime::shared_ptr<vmime::messaging::transport> vmTransport;
	vmime::shared_ptr<vmime::net::smtp::MAPISMTPTransport> mapiTransport;

	if (lpMessage == NULL || vmMessage == NULL)
		return MAPI_E_INVALID_PARAMETER;

	smtpresult = 0;
	error.clear();

	try {
		// Session initialization (global properties)
		vmSession = vmime::net::session::create();

		// set the server address and port, plus type of service by use of url
		// and get our special mapismtp mailer
		vmime::utility::url url("mapismtp", smtphost, smtpport);
		vmTransport = vmSession->getTransport(url);
		vmTransport->setTimeoutHandlerFactory(vmime::make_shared<mapiTimeoutHandlerFactory>());

		// cast to access interface extra's
		mapiTransport = vmime::dynamicCast<vmime::net::smtp::MAPISMTPTransport>(vmTransport);

		// get expeditor for 'mail from:' smtp command
		try {
			expeditor = *vmime::dynamicCast<vmime::mailbox>(vmMessage->getHeader()->findField(vmime::fields::FROM)->getValue());
		}
		catch (vmime::exceptions::no_such_field&) {
			throw vmime::exceptions::no_expeditor();
		}

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
        try {
		auto bcc = vmMessage->getHeader()->findField(vmime::fields::BCC);
			vmMessage->getHeader()->removeField(bcc);
        }
        catch (vmime::exceptions::no_such_field&) { }

		// Delivery report request
		SPropValuePtr ptrDeliveryReport;
		if (mapiTransport && HrGetOneProp(lpMessage, PR_ORIGINATOR_DELIVERY_REPORT_REQUESTED, &ptrDeliveryReport) == hrSuccess && ptrDeliveryReport->Value.b == TRUE) {
			mapiTransport->requestDSN(true, "");
		}

		// Generate the message, "stream" it and delegate the sending
		// to the generic send() function.
		std::ostringstream oss;
		vmime::utility::outputStreamAdapter ossAdapter(oss);

		vmMessage->generate(ossAdapter);

		const string& str(oss.str()); // copy?
		vmime::utility::inputStreamStringAdapter isAdapter(str); // direct oss.str() ?
		
		if (mapiTransport)
			mapiTransport->setLogger(lpLogger);

		// send the email already!
		bool ok = false;
		try {
			vmTransport->connect();
		} catch (vmime::exception &e) {
			// special error, smtp server not respoding, so try later again
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Connect to SMTP: %s. E-Mail will be tried again later.", e.what());
			return MAPI_W_NO_SERVICE;
		}

		try {
			vmTransport->send(expeditor, recipients, isAdapter, str.length(), NULL);
			vmTransport->disconnect();
			ok = true;
		}
		catch (vmime::exceptions::command_error& e) {
			if (mapiTransport != NULL) {
				mPermanentFailedRecipients = mapiTransport->getPermanentFailedRecipients();
				mTemporaryFailedRecipients = mapiTransport->getTemporaryFailedRecipients();
			}
			lpLogger->Log(EC_LOGLEVEL_ERROR, "SMTP: %s Response: %s", e.what(), e.response().c_str());
			smtpresult = atoi(e.response().substr(0, e.response().find_first_of(" ")).c_str());
			error = convert_to<wstring>(e.response());
			// message should be cancelled, unsendable, test by smtp result code.
			return MAPI_W_CANCEL_MESSAGE;
		} 
		catch (vmime::exceptions::no_recipient& e) {
			if (mapiTransport != NULL) {
				mPermanentFailedRecipients = mapiTransport->getPermanentFailedRecipients();
				mTemporaryFailedRecipients = mapiTransport->getTemporaryFailedRecipients();
			}
			lpLogger->Log(EC_LOGLEVEL_ERROR, "SMTP: %s Name: %s", e.what(), e.name());
			//smtpresult = atoi(e.response().substr(0, e.response().find_first_of(" ")).c_str());
			//error = convert_to<wstring>(e.response());
			// message should be cancelled, unsendable, test by smtp result code.
			return MAPI_W_CANCEL_MESSAGE;
		} 
		catch (vmime::exception &e) {
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
	}
	catch (vmime::exception& e) {
		// connection_greeting_error, ...?
		lpLogger->Log(EC_LOGLEVEL_ERROR, "%s", e.what());
		error = convert_to<wstring>(e.what());
		return MAPI_E_NETWORK_ERROR;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "%s",e.what());
		error = convert_to<wstring>(e.what());
		return MAPI_E_NETWORK_ERROR;
	}
	return hr;
}
