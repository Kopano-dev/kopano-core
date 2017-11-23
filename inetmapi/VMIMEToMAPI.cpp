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
#include <exception>
#include <list>
#include <utility>
#include <vector>
#include "VMIMEToMAPI.h"
#include <kopano/ECGuid.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <fstream>
#include <iostream>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <kopano/ECDebug.h>
#include <librosie.h>
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/contentTypeField.hpp>
#include <vmime/contentDispositionField.hpp>

#include <libxml/HTMLparser.h>

// mapi
#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include <edkmdb.h>

#include <kopano/EMSAbTag.h>
#include "tnef.h"
#include <kopano/codepage.h>
#include <kopano/Util.h>
#include <kopano/CommonUtil.h>
#include <kopano/MAPIErrors.h>
#include <kopano/namedprops.h>
#include <kopano/charset/convert.h>
#include <kopano/stringutil.h>
#include <kopano/mapi_ptr.h>

// inetmapi
#include "ECMapiUtils.h"
#include "ECVMIMEUtils.h"
#include "inputStreamMAPIAdapter.h"

// vcal support
#include "ICalToMAPI.h"
#include "config.h"

using namespace KCHL;
using std::list;
using std::string;
using std::vector;
using std::wstring;

namespace KC {

static vmime::charset vtm_upgrade_charset(vmime::charset cset, const char *ascii_upgrade = nullptr);

static const char im_charset_unspec[] = "unspecified";

/**
 * Create INT date
 *
 * @param[in] day Day of month 1-31
 * @param[in] month Month of year 1-12
 * @param[in] year Full year (eg 2008)
 * @return ULONG Calculated INT date
 */
static ULONG CreateIntDate(ULONG day, ULONG month, ULONG year)
{
	return day + month * 32 + year * 32 * 16;
}

/**
 * Create INT time
 *
 * @param[in] seconds Seconds 0-59
 * @param[in] minutes Minutes 0-59
 * @param[in] hours Hours
 * @return Calculated INT time
 */
static ULONG CreateIntTime(ULONG seconds, ULONG minutes, ULONG hours)
{
	return seconds + minutes * 64 + hours * 64 * 64;
}

/**
 * Create INT date from filetime
 *
 * Discards time information from the passed FILETIME stamp, and returns the
 * date part as an INT date. The passed FILETIME is interpreted in GMT.
 *
 * @param[in] ft FileTime to convert
 * @return Converted DATE part of the file time.
 */
static ULONG FileTimeToIntDate(const FILETIME &ft)
{
	struct tm date;
	time_t t;
	FileTimeToUnixTime(ft, &t);
	gmtime_safe(&t, &date);
	return CreateIntDate(date.tm_mday, date.tm_mon+1, date.tm_year+1900);
}

/**
 * Create INT time from offset in seconds
 *
 * Creates an INT time value for the moment at which the passed amount of
 * seconds has passed on a day.
 *
 * @param[in] seconds Number of seconds since beginning of day
 * @return Converted INT time
 */
static ULONG SecondsToIntTime(ULONG seconds)
{
	ULONG hours = seconds / (60*60);
	seconds -= hours * 60 * 60;
	ULONG minutes = seconds / 60;
	seconds -= minutes * 60;
	return CreateIntTime(seconds, minutes, hours);
}

/**
 * Default empty constructor for the inetmapi library. Sets all member
 * values to sane defaults.
 */
VMIMEToMAPI::VMIMEToMAPI()
{
	imopt_default_delivery_options(&m_dopt);
	m_dopt.use_received_date = false; // use Date header
	m_dopt.html_safety_filter = false;
}

/**
 * Adds user set addressbook (to minimize opens on this object) and delivery options.
 * 
 * @param[in]	lpAdrBook	Addressbook of a user.
 * @param[in]	dopt		delivery options handle differences in DAgent and Gateway behaviour.
 */
VMIMEToMAPI::VMIMEToMAPI(LPADRBOOK lpAdrBook, delivery_options dopt) :
	m_dopt(dopt), m_lpAdrBook(lpAdrBook)
{
}

/** 
 * Parse a RFC 2822 email, and return the IMAP BODY and BODYSTRUCTURE
 * fetch values.
 * 
 * @param[in] input The email to parse
 * @param[out] lpSimple The BODY value
 * @param[out] lpExtended The BODYSTRUCTURE value
 * 
 * @return 
 */
HRESULT VMIMEToMAPI::createIMAPProperties(const std::string &input, std::string *lpEnvelope, std::string *lpBody, std::string *lpBodyStructure)
{
	auto vmMessage = vmime::make_shared<vmime::message>();
	vmMessage->parse(input);

	if (lpBody || lpBodyStructure)
		messagePartToStructure(input, vmMessage, lpBody, lpBodyStructure);

	if (lpEnvelope)
		*lpEnvelope = createIMAPEnvelope(vmMessage);

	return hrSuccess;
}

/** 
 * Entry point for the conversion from RFC 2822 mail to IMessage MAPI object.
 *
 * Finds the first block of headers to place in the
 * PR_TRANSPORT_MESSAGE_HEADERS property. Then it lets libvmime parse
 * the email and starts the main conversion function
 * fillMAPIMail. Afterwards it may handle signed messages, and set an
 * extra flag when all attachments were marked hidden.
 *
 * @param[in]	input	std::string containing the RFC 2822 mail.
 * @param[out]	lpMessage	Pointer to a message which was already created on a IMAPIFolder.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::convertVMIMEToMAPI(const string &input, IMessage *lpMessage) {
	HRESULT hr = hrSuccess;
	// signature variables
	ULONG ulAttNr;
	object_ptr<IStream> lpStream;
	ULONG nProps = 0;
	SPropValue attProps[3];
	SPropValue sPropSMIMEClass;
	object_ptr<IMAPITable> lpAttachTable;
	size_t posHeaderEnd;
	bool bUnix = false;

	try {
		if (m_mailState.ulMsgInMsg == 0)
			m_mailState.reset();

		// get raw headers
		posHeaderEnd = input.find("\r\n\r\n");
		if (posHeaderEnd == std::string::npos) {
			// input was not rfc compliant, try Unix enters
			posHeaderEnd = input.find("\n\n");
			bUnix = true;
		}
		if (posHeaderEnd != std::string::npos) {
			SPropValue sPropHeaders;
			std::string strHeaders = input.substr(0, posHeaderEnd);

			// make sure we have US-ASCII headers
			if (bUnix)
				StringLFtoCRLF(strHeaders);

			sPropHeaders.ulPropTag = PR_TRANSPORT_MESSAGE_HEADERS_A;
			sPropHeaders.Value.lpszA = (char *) strHeaders.c_str();

			HrSetOneProp(lpMessage, &sPropHeaders);
		}
		/*
		 * Add PR_MESSAGE_SIZE initially to the size of the RFC2822
		 * part. PR_MESSAGE_SIZE is needed for rule processing; if this
		 * is not added, the message size is not known at processing
		 * time because the message size is computed during save.
		 * According to MAPI documentation, PR_MESSAGE_SIZE is an
		 * estimated size of the message, therefore the size of the
		 * RFC2822 message will qualify.
		*/
		SPropValue sMessageSize;
		sMessageSize.ulPropTag = PR_MESSAGE_SIZE;
		sMessageSize.Value.ul = input.length();
		lpMessage->SetProps(1, &sMessageSize, nullptr);

		// turn buffer into a message
		auto vmMessage = vmime::make_shared<vmime::message>();
		vmMessage->parse(input);

		// save imap data first, seems vmMessage may be altered in the rest of the code.
		if (m_dopt.add_imap_data)
			createIMAPBody(input, vmMessage, lpMessage);

		hr = fillMAPIMail(vmMessage, lpMessage);
		if (hr != hrSuccess)
			return hr;

		if (m_mailState.bAttachSignature && !m_dopt.parse_smime_signed) {
			static constexpr const SizedSPropTagArray(2, sptaAttach) =
				{2, {PR_ATTACH_NUM, PR_ATTACHMENT_HIDDEN}};
			// Remove the parsed attachments since the client should be reading them from the 
			// signed RFC 2822 data we are about to add.
			
			hr = lpMessage->GetAttachmentTable(0, &~lpAttachTable);
			if(hr != hrSuccess)
				return hr;

			rowset_ptr lpAttachRows;
			hr = HrQueryAllRows(lpAttachTable, sptaAttach, nullptr, nullptr, -1, &~lpAttachRows);
			if(hr != hrSuccess)
				return hr;
				
			for (unsigned int i = 0; i < lpAttachRows->cRows; ++i) {
				hr = lpMessage->DeleteAttach(lpAttachRows->aRow[i].lpProps[0].Value.ul, 0, NULL, 0);
				if(hr != hrSuccess)
					return hr;
			}
			
			// Include the entire RFC 2822 data in an attachment for the client to check
			auto vmHeader = vmMessage->getHeader();
			object_ptr<IAttach> lpAtt;
			hr = lpMessage->CreateAttach(nullptr, 0, &ulAttNr, &~lpAtt);
			if (hr != hrSuccess)
				return hr;

			// open stream
			hr = lpAtt->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE | STGM_TRANSACTED,
			     MAPI_CREATE | MAPI_MODIFY, &~lpStream);
			if (hr != hrSuccess)
				return hr;

			outputStreamMAPIAdapter os(lpStream);
			// get the content-type string from the headers
			vmHeader->ContentType()->generate(os);

			// find the original received body
			// vmime re-generates different headers and spacings, so we can't use this.
			if (posHeaderEnd != string::npos)
				os.write(input.c_str() + posHeaderEnd, input.size() - posHeaderEnd);
			hr = lpStream->Commit(0);
			if (hr != hrSuccess)
				return hr;

			attProps[nProps].ulPropTag = PR_ATTACH_METHOD;
			attProps[nProps++].Value.ul = ATTACH_BY_VALUE;

			attProps[nProps].ulPropTag = PR_ATTACH_MIME_TAG_W;
			attProps[nProps++].Value.lpszW = const_cast<wchar_t *>(L"multipart/signed");
			attProps[nProps].ulPropTag = PR_RENDERING_POSITION;
			attProps[nProps++].Value.ul = -1;

			hr = lpAtt->SetProps(nProps, attProps, NULL);
			if (hr != hrSuccess)
				return hr;
			hr = lpAtt->SaveChanges(0);
			if (hr != hrSuccess)
				return hr;
				
			// saved, so mark the message so outlook knows how to find the encoded message
			sPropSMIMEClass.ulPropTag = PR_MESSAGE_CLASS_W;
			sPropSMIMEClass.Value.lpszW = const_cast<wchar_t *>(L"IPM.Note.SMIME.MultipartSigned");

			hr = lpMessage->SetProps(1, &sPropSMIMEClass, NULL);
			if (hr != hrSuccess) {
				ec_log_err("Unable to set message class");
				return hr;
			}
		}

		if ((m_mailState.attachLevel == ATTACH_INLINE && m_mailState.bodyLevel == BODY_HTML) || (m_mailState.bAttachSignature && m_mailState.attachLevel <= ATTACH_INLINE)) {
			/* Hide the attachment flag if:
			 * - We have an HTML body and there are only INLINE attachments (don't need to hide no attachments)
			 * - We have a signed message and there are only INLINE attachments or no attachments at all (except for the signed message)
			 */
			MAPINAMEID sNameID;
			LPMAPINAMEID lpNameID = &sNameID;
			memory_ptr<SPropTagArray> lpPropTag;

			sNameID.lpguid = (GUID*)&PSETID_Common;
			sNameID.ulKind = MNID_ID;
			sNameID.Kind.lID = dispidSmartNoAttach;

			hr = lpMessage->GetIDsFromNames(1, &lpNameID, MAPI_CREATE, &~lpPropTag);
			if (hr != hrSuccess)
				return hrSuccess;

			attProps[0].ulPropTag = CHANGE_PROP_TYPE(lpPropTag->aulPropTag[0], PT_BOOLEAN);
			attProps[0].Value.b = TRUE;
			hr = lpMessage->SetProps(1, attProps, NULL);
			if (hr != hrSuccess)
				return hrSuccess;
		}
	}
	catch (vmime::exception& e) {
		ec_log_err("VMIME exception: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (std::exception& e) {
		ec_log_err("STD exception: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (...) {
		ec_log_err("Unknown generic exception occurred");
		return MAPI_E_CALL_FAILED;
	}
	return hrSuccess;
}

/**
 * The main conversion function from vmime to IMessage.
 *
 * After converting recipients and headers using their functions, it
 * will handle special message disposition notification bodies (read
 * reciept messages), or loop on all body parts
 * (text/html/attachments) using dissect_body() function, which in turn
 * may call this function to iterate on message-in-message mails.
 *
 * @param[in]	vmMessage	The message object from vmime.
 * @param[out]	lpMessage	The MAPI IMessage object to be filled.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::fillMAPIMail(vmime::shared_ptr<vmime::message> vmMessage,
    IMessage *lpMessage)
{
	HRESULT	hr;
	SPropValue sPropDefaults[3];

	sPropDefaults[0].ulPropTag = PR_MESSAGE_CLASS_W;
	sPropDefaults[0].Value.lpszW = const_cast<wchar_t *>(L"IPM.Note");
	sPropDefaults[1].ulPropTag = PR_MESSAGE_FLAGS;
	sPropDefaults[1].Value.ul = (m_dopt.mark_as_read ? MSGFLAG_READ : 0) | MSGFLAG_UNMODIFIED;

	// Default codepage is UTF-8, might be overwritten when writing
	// the body (plain or html). So this is only in effect when an
	// e-mail does not specify its charset.  We use UTF-8 since it is
	// compatible with US-ASCII, and the conversion from plain-text
	// only to HTML by the client will use this codepage. This makes
	// sure the generated HTML version of plaintext only mails
	// contains all characters.
	sPropDefaults[2].ulPropTag = PR_INTERNET_CPID;
	sPropDefaults[2].Value.ul = 65001;

	hr = lpMessage->SetProps(3, sPropDefaults, NULL);
	if (hr != hrSuccess) {
		ec_log_err("Unable to set default mail properties");
		return hr;
	}

	try {
		// turn buffer into a message

		// get the part header and find out what it is...
		auto vmHeader = vmMessage->getHeader();
		auto vmBody = vmMessage->getBody();
		auto mt = vmime::dynamicCast<vmime::mediaType>(vmHeader->ContentType()->getValue());

		// pass recipients somewhere else 
		hr = handleRecipients(vmHeader, lpMessage);
		if (hr != hrSuccess) {
			ec_log_err("Unable to parse mail recipients");
			return hr;
		}

		// Headers
		hr = handleHeaders(vmHeader, lpMessage);
		if (hr != hrSuccess) {
			ec_log_err("Unable to parse mail headers");
			return hr;
		}

		if (vmime::mdn::MDNHelper::isMDN(vmMessage) == true)
		{
			vmime::mdn::receivedMDNInfos receivedMDN = vmime::mdn::MDNHelper::getReceivedMDN(vmMessage);
			auto myBody = vmMessage->getBody();
			// it is possible to get 3 bodyparts.
			// text/plain, message/disposition-notification, text/rfc822-headers
			// the third part seems optional. and some clients send multipart/alternative instead of text/plain.
			// Loop to get text/plain body or multipart/alternative.
			for (size_t i = 0; i < myBody->getPartCount(); ++i) {
				auto bPart = myBody->getPartAt(i);
				auto ctf = bPart->getHeader()->findField(vmime::fields::CONTENT_TYPE);
				if (ctf == nullptr)
					continue;
				auto cval = ctf->getValue();
				if (cval == nullptr) {
					ec_log_debug("MDN Content-Type field without value");
					continue;
				}
				auto dval = vmime::dynamicCast<vmime::mediaType>(cval);
				if (dval == nullptr) {
					ec_log_debug("MDN Content-Type field not representable as vmime::mediaType");
					continue;
				}

				if ((dval->getType() == vmime::mediaTypes::TEXT &&
				     dval->getSubType() == vmime::mediaTypes::TEXT_PLAIN) ||
				    (dval->getType() == vmime::mediaTypes::MULTIPART &&
				     dval->getSubType() == vmime::mediaTypes::MULTIPART_ALTERNATIVE)) {
					hr = dissect_body(bPart->getHeader(), bPart->getBody(), lpMessage);
					if (hr != hrSuccess) {
						ec_log_err("Unable to parse MDN mail body");
						return hr;
					}
					// we have a body, lets skip the other parts
					break;
				}
			}

			if (receivedMDN.getDisposition().getType() == vmime::dispositionTypes::DELETED)
			{
				sPropDefaults[0].ulPropTag = PR_MESSAGE_CLASS_W;
				sPropDefaults[0].Value.lpszW = const_cast<wchar_t *>(L"REPORT.IPM.Note.IPNNRN");
			} else {
				sPropDefaults[0].ulPropTag = PR_MESSAGE_CLASS_W;
				sPropDefaults[0].Value.lpszW = const_cast<wchar_t *>(L"REPORT.IPM.Note.IPNRN");
			}

			string strId = "<"+receivedMDN.getOriginalMessageId().getId()+">";
			sPropDefaults[1].ulPropTag = 0x1046001E;	// ptagOriginalInetMessageID
			sPropDefaults[1].Value.lpszA = (LPSTR)strId.c_str();

			hr = lpMessage->SetProps(2, sPropDefaults, NULL);
			if (hr != hrSuccess) {
				ec_log_err("Unable to set MDN mail properties");
				return hr;
			}
		} else {
			// multiparts are handled in disectBody, if any
			hr = dissect_body(vmHeader, vmBody, lpMessage);
			if (hr != hrSuccess) {
				ec_log_err("Unable to parse mail body");
				return hr;
			}
		}
	}
	catch (vmime::exception& e) {
		ec_log_err("VMIME exception on create message: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (std::exception& e) {
		ec_log_err("STD exception on create message: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (...) {
		ec_log_err("Unknown generic exception occurred on create message");
		return MAPI_E_CALL_FAILED;
	}

	createIMAPEnvelope(vmMessage, lpMessage);

	// ignore error/warings from fixup function: it's not critical for correct delivery
	postWriteFixups(lpMessage);
	return hr;
}

/**
 * Convert all kinds of headers into MAPI properties.
 *
 * Converts most known headers to their respective MAPI property. It
 * will not handle the To/Cc/Bcc headers, but does the From/Sender
 * headers, and might convert those to known ZARAFA addressbook entries.
 * It also converts X-headers to named properties like PSTs do.
 *
 * @param[in]	vmHeader	vmime header part of a message.
 * @param[out]	lpMessage	MAPI message to write header properties in.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::handleHeaders(vmime::shared_ptr<vmime::header> vmHeader,
    IMessage *lpMessage)
{
	HRESULT			hr = hrSuccess;
	std::string		strInternetMessageId, strInReplyTos, strReferences;
	std::wstring	wstrSubject;
	std::wstring	wstrReplyTo, wstrReplyToMail;
	std::string		strClientTime;
	std::wstring	wstrFromName, wstrSenderName;
	std::string		strFromEmail, strFromSearchKey;
	std::string		strSenderEmail,	strSenderSearchKey;
	ULONG			cbFromEntryID; // representing entry id
	memory_ptr<ENTRYID> lpFromEntryID, lpSenderEntryID;
	ULONG			cbSenderEntryID;
	SPropValue		sConTopic;
	// setprops
	memory_ptr<FLATENTRY> lpEntry;
	memory_ptr<FLATENTRYLIST> lpEntryList;
	ULONG			cb = 0;
	int				nProps = 0;
	SPropValue		msgProps[22];
	// temp
	ULONG			cbEntryID;
	memory_ptr<ENTRYID> lpEntryID;
	memory_ptr<SPropValue> lpRecipProps, lpPropNormalizedSubject;
	ULONG			ulRecipProps;

	// order and types are important for modifyFromAddressBook()
	static constexpr const SizedSPropTagArray(7, sptaRecipPropsSentRepr) = {7, {
		PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_NAME_W,
		PR_NULL /* PR_xxx_DISPLAY_TYPE not available */,
		PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, PR_SENT_REPRESENTING_ENTRYID,
		PR_SENT_REPRESENTING_SEARCH_KEY, PR_NULL /* PR_xxx_SMTP_ADDRESS not available */
	} };
	static constexpr const SizedSPropTagArray(7, sptaRecipPropsSender) = {7, {
		PR_SENDER_ADDRTYPE_W, PR_SENDER_NAME_W,
		PR_NULL /* PR_xxx_DISPLAY_TYPE not available */,
		PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID,
		PR_SENDER_SEARCH_KEY, PR_NULL /* PR_xxx_SMTP_ADDRESS not available */
	} };

	try { 
		// internet message ID
		auto field = vmHeader->findField(vmime::fields::MESSAGE_ID);
		if (field != nullptr) {
			strInternetMessageId = field->getValue()->generate();
			msgProps[nProps].ulPropTag = PR_INTERNET_MESSAGE_ID_A;
			msgProps[nProps++].Value.lpszA = (char*)strInternetMessageId.c_str();
		}

		// In-Reply-To header
		field = vmHeader->findField(vmime::fields::IN_REPLY_TO);
		if (field != nullptr) {
			strInReplyTos = field->getValue()->generate();
			msgProps[nProps].ulPropTag = PR_IN_REPLY_TO_ID_A;
			msgProps[nProps++].Value.lpszA = (char*)strInReplyTos.c_str();
		}

		// References header
		field = vmHeader->findField(vmime::fields::REFERENCES);
		if (field != nullptr) {
			strReferences = field->getValue()->generate();
			msgProps[nProps].ulPropTag = PR_INTERNET_REFERENCES_A;
			msgProps[nProps++].Value.lpszA = (char*)strReferences.c_str();
		}

		// set subject
		field = vmHeader->findField(vmime::fields::SUBJECT);
		if (field != nullptr) {
			wstrSubject = getWideFromVmimeText(*vmime::dynamicCast<vmime::text>(field->getValue()));
			msgProps[nProps].ulPropTag = PR_SUBJECT_W;
			msgProps[nProps++].Value.lpszW = (WCHAR *)wstrSubject.c_str();
		}

		// set ReplyTo
		if (!vmime::dynamicCast<vmime::mailbox>(vmHeader->ReplyTo()->getValue())->isEmpty()) {
			// First, set PR_REPLY_RECIPIENT_NAMES
			wstrReplyTo = getWideFromVmimeText(vmime::dynamicCast<vmime::mailbox>(vmHeader->ReplyTo()->getValue())->getName());
			wstrReplyToMail = m_converter.convert_to<wstring>(vmime::dynamicCast<vmime::mailbox>(vmHeader->ReplyTo()->getValue())->getEmail().toString());
			if (wstrReplyTo.empty())
				wstrReplyTo = wstrReplyToMail;

			msgProps[nProps].ulPropTag = PR_REPLY_RECIPIENT_NAMES_W;
			msgProps[nProps++].Value.lpszW = (WCHAR *)wstrReplyTo.c_str();

			// Now, set PR_REPLY_RECIPIENT_ENTRIES (a FLATENTRYLIST)
			hr = ECCreateOneOff((LPTSTR)wstrReplyTo.c_str(), (LPTSTR)L"SMTP", (LPTSTR)wstrReplyToMail.c_str(), MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbEntryID, &~lpEntryID);
			if (hr != hrSuccess)
				return hr;
			cb = CbNewFLATENTRY(cbEntryID);
			hr = MAPIAllocateBuffer(cb, &~lpEntry);
			if (hr != hrSuccess)
				return hr;
			memcpy(lpEntry->abEntry, lpEntryID, cbEntryID);
			lpEntry->cb = cbEntryID;

			cb = CbNewFLATENTRYLIST(cb);
			hr = MAPIAllocateBuffer(cb, &~lpEntryList);
			if (hr != hrSuccess)
				return hr;
			lpEntryList->cEntries = 1;
			lpEntryList->cbEntries = CbFLATENTRY(lpEntry);
			memcpy(&lpEntryList->abEntries, lpEntry, CbFLATENTRY(lpEntry));

			msgProps[nProps].ulPropTag = PR_REPLY_RECIPIENT_ENTRIES;
			msgProps[nProps].Value.bin.cb = CbFLATENTRYLIST(lpEntryList);
			msgProps[nProps++].Value.bin.lpb = reinterpret_cast<unsigned char *>(lpEntryList.get());
		}

		// setting sent time
		field = vmHeader->findField(vmime::fields::DATE);
		if (field != nullptr) {
			msgProps[nProps].ulPropTag = PR_CLIENT_SUBMIT_TIME;
			msgProps[nProps++].Value.ft = vmimeDatetimeToFiletime(*vmime::dynamicCast<vmime::datetime>(field->getValue()));

			// set sent date (actual send date, disregarding timezone)
			vmime::datetime d = *vmime::dynamicCast<vmime::datetime>(field->getValue());
			d.setTime(0,0,0,0);
			msgProps[nProps].ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			msgProps[nProps++].Value.ft = vmimeDatetimeToFiletime(d);
		}

		// setting receive date (now)
		// parse from Received header, if possible
		vmime::datetime date = vmime::datetime::now();
		bool found_date = false;
		if (m_dopt.use_received_date || m_mailState.ulMsgInMsg) {
			field = vmHeader->findField("Received");
			if (field != nullptr) {
				auto recv = vmime::dynamicCast<vmime::relay>(field->getValue());
				if (recv != nullptr) {
					date = recv->getDate();
					found_date = true;
				}
			} else if (m_mailState.ulMsgInMsg) {
				field = vmHeader->findField("Date");
				if (field != nullptr) {
					date = *vmime::dynamicCast<vmime::datetime>(field->getValue());
					found_date = true;
				}
			} else {
				date = vmime::datetime::now();
			}
		}
		// When parse_smime_signed = True, we don't want to change the delivery date, since otherwise
		// clients which decode a signed email using mapi_inetmapi_imtomapi() will have a different deliver time
		// when opening a signed email in for example the WebApp
		if (!m_dopt.parse_smime_signed && (!m_mailState.ulMsgInMsg || found_date)) {
			msgProps[nProps].ulPropTag = PR_MESSAGE_DELIVERY_TIME;
			msgProps[nProps++].Value.ft = vmimeDatetimeToFiletime(date);

			// Also save delivery DATE without timezone
			date.setTime(0,0,0,0);
			msgProps[nProps].ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			msgProps[nProps++].Value.ft = vmimeDatetimeToFiletime(date);
		}

		// The real sender of the mail
		if(vmHeader->hasField(vmime::fields::FROM)) {
			strFromEmail = vmime::dynamicCast<vmime::mailbox>(vmHeader->From()->getValue())->getEmail().toString();
			if (!vmime::dynamicCast<vmime::mailbox>(vmHeader->From()->getValue())->getName().isEmpty())
				wstrFromName = getWideFromVmimeText(vmime::dynamicCast<vmime::mailbox>(vmHeader->From()->getValue())->getName());

			hr = modifyFromAddressBook(&~lpRecipProps, &ulRecipProps,
			     strFromEmail.c_str(), wstrFromName.c_str(),
			     MAPI_ORIG, sptaRecipPropsSentRepr);
			if (hr == hrSuccess) {
				hr = lpMessage->SetProps(ulRecipProps, lpRecipProps, NULL);
				if (hr != hrSuccess)
					return hr;
			} else {
				if (wstrFromName.empty())
					wstrFromName = m_converter.convert_to<wstring>(strFromEmail);

				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_NAME_W;
				msgProps[nProps++].Value.lpszW = (WCHAR *)wstrFromName.c_str();

				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS_A;
				msgProps[nProps++].Value.lpszA = (char*)strFromEmail.c_str();

				strFromSearchKey = strToUpper("SMTP:" + strFromEmail);
				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_SEARCH_KEY;
				msgProps[nProps].Value.bin.cb = strFromSearchKey.size()+1; // include string terminator
				msgProps[nProps++].Value.bin.lpb = (BYTE*)strFromSearchKey.c_str();

				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE_W;
				msgProps[nProps++].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
				hr = ECCreateOneOff((LPTSTR)wstrFromName.c_str(), (LPTSTR)L"SMTP", (LPTSTR)m_converter.convert_to<wstring>(strFromEmail).c_str(),
				     MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbFromEntryID, &~lpFromEntryID);
				if(hr != hrSuccess)
					return hr;

				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_ENTRYID;
				msgProps[nProps].Value.bin.cb = cbFromEntryID;
				msgProps[nProps++].Value.bin.lpb = reinterpret_cast<unsigned char *>(lpFromEntryID.get());

				// SetProps is later on...
			}
		}
		
		if (vmHeader->hasField(vmime::fields::SENDER) || vmHeader->hasField(vmime::fields::FROM)) {
			// The original sender of the mail account (if non sender exist then the FROM)
			strSenderEmail = vmime::dynamicCast<vmime::mailbox>(vmHeader->Sender()->getValue())->getEmail().toString();
			if (vmime::dynamicCast<vmime::mailbox>(vmHeader->Sender()->getValue())->getName().isEmpty() &&
			    (strSenderEmail.empty() || strSenderEmail == "@" || strSenderEmail == "invalid@invalid")) {
				// Fallback on the original from address
				wstrSenderName = wstrFromName;
				strSenderEmail = strFromEmail;
			} else if (!vmime::dynamicCast<vmime::mailbox>(vmHeader->Sender()->getValue())->getName().isEmpty()) {
				wstrSenderName = getWideFromVmimeText(vmime::dynamicCast<vmime::mailbox>(vmHeader->Sender()->getValue())->getName());
			} else {
				wstrSenderName = m_converter.convert_to<wstring>(strSenderEmail);
			}

			hr = modifyFromAddressBook(&~lpRecipProps, &ulRecipProps,
			     strSenderEmail.c_str(), wstrSenderName.c_str(),
			     MAPI_ORIG, sptaRecipPropsSender);
			if (hr == hrSuccess) {
				hr = lpMessage->SetProps(ulRecipProps, lpRecipProps, NULL);
				if (hr != hrSuccess)
					return hr;
			} else {
				msgProps[nProps].ulPropTag = PR_SENDER_NAME_W;
				msgProps[nProps++].Value.lpszW = (WCHAR *)wstrSenderName.c_str();

				msgProps[nProps].ulPropTag = PR_SENDER_EMAIL_ADDRESS_A;
				msgProps[nProps++].Value.lpszA = (char*)strSenderEmail.c_str();

				strSenderSearchKey = strToUpper("SMTP:" + strSenderEmail);
				msgProps[nProps].ulPropTag = PR_SENDER_SEARCH_KEY;
				msgProps[nProps].Value.bin.cb = strSenderSearchKey.size()+1; // include string terminator
				msgProps[nProps++].Value.bin.lpb = (BYTE*)strSenderSearchKey.c_str();

				msgProps[nProps].ulPropTag = PR_SENDER_ADDRTYPE_W;
				msgProps[nProps++].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
				hr = ECCreateOneOff((LPTSTR)wstrSenderName.c_str(), (LPTSTR)L"SMTP", (LPTSTR)m_converter.convert_to<wstring>(strSenderEmail).c_str(),
				     MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbSenderEntryID, &~lpSenderEntryID);
				if(hr != hrSuccess)
					return hr;

				msgProps[nProps].ulPropTag = PR_SENDER_ENTRYID;
				msgProps[nProps].Value.bin.cb = cbSenderEntryID;
				msgProps[nProps++].Value.bin.lpb = reinterpret_cast<unsigned char *>(lpSenderEntryID.get());
			}
		}
		
		hr = lpMessage->SetProps(nProps, msgProps, NULL);
		if (hr != hrSuccess)
			return hr;

		//Conversation topic
		if (vmHeader->hasField("Thread-Topic"))
		{
			wstring convTT = getWideFromVmimeText(*vmime::dynamicCast<vmime::text>(vmHeader->findField("Thread-Topic")->getValue()));

			sConTopic.ulPropTag = PR_CONVERSATION_TOPIC_W;
			sConTopic.Value.lpszW = (WCHAR *)convTT.c_str();

			hr = lpMessage->SetProps(1, &sConTopic, NULL);
			if (hr != hrSuccess)
				return hr;
		} else if (HrGetOneProp(lpMessage, PR_NORMALIZED_SUBJECT_W, &~lpPropNormalizedSubject) == hrSuccess) {
			sConTopic.ulPropTag = PR_CONVERSATION_TOPIC_W;
			sConTopic.Value.lpszW = lpPropNormalizedSubject->Value.lpszW;
			
			hr = lpMessage->SetProps(1, &sConTopic, NULL);
			if (hr != hrSuccess)
				return hr;
		}

		// Thread-Index header
		if (vmHeader->hasField("Thread-Index"))
		{
			vmime::string outString;
			SPropValue sThreadIndex;

			string threadIndex = vmHeader->findField("Thread-Index")->getValue()->generate();

			auto enc = vmime::utility::encoder::encoderFactory::getInstance()->create("base64");
			vmime::utility::inputStreamStringAdapter in(threadIndex);			
			vmime::utility::outputStreamStringAdapter out(outString);

			enc->decode(in, out);

			sThreadIndex.ulPropTag = PR_CONVERSATION_INDEX;
			sThreadIndex.Value.bin.cb = outString.size();
			sThreadIndex.Value.bin.lpb = (LPBYTE)outString.c_str();

			hr = lpMessage->SetProps(1, &sThreadIndex, NULL);
			if (hr != hrSuccess)
				return hr;
		}
		
		if (vmHeader->hasField("Importance")) {
			SPropValue sPriority[2];
			sPriority[0].ulPropTag = PR_PRIORITY;
			sPriority[1].ulPropTag = PR_IMPORTANCE;
			auto importance = strToLower(vmHeader->findField("Importance")->getValue()->generate());
			if(importance.compare("high") == 0) {
				sPriority[0].Value.ul = PRIO_URGENT;
				sPriority[1].Value.ul = IMPORTANCE_HIGH;
			} else if(importance.compare("low") == 0) {
				sPriority[0].Value.ul = PRIO_NONURGENT;
				sPriority[1].Value.ul = IMPORTANCE_LOW;
			} else {
				sPriority[0].Value.ul = PRIO_NORMAL;
				sPriority[1].Value.ul = IMPORTANCE_NORMAL;
			}

			hr = lpMessage->SetProps(2, sPriority, NULL);
			if (hr != hrSuccess)
				return hr;
		}

		// X-Priority header
		if (vmHeader->hasField("X-Priority")) {
			SPropValue sPriority[2];
			sPriority[0].ulPropTag = PR_PRIORITY;
			sPriority[1].ulPropTag = PR_IMPORTANCE;
			string xprio = vmHeader->findField("X-Priority")->getValue()->generate();
			switch (xprio[0]) {
			case '1':
			case '2':
				sPriority[0].Value.ul = PRIO_URGENT;
				sPriority[1].Value.ul = IMPORTANCE_HIGH;
				break;
			case '4':
			case '5':
				sPriority[0].Value.ul = PRIO_NONURGENT;
				sPriority[1].Value.ul = IMPORTANCE_LOW;
				break;
			default:
			case '3':
				sPriority[0].Value.ul = PRIO_NORMAL;
				sPriority[1].Value.ul = IMPORTANCE_NORMAL;
				break;
			};
			hr = lpMessage->SetProps(2, sPriority, NULL);
			if (hr != hrSuccess)
				return hr;
		}

		// X-Kopano-Vacation header (TODO: other headers?)
		if (vmHeader->hasField("X-Kopano-Vacation")) {
			SPropValue sIcon;
			sIcon.ulPropTag = PR_ICON_INDEX;
			sIcon.Value.l = ICON_MAIL_OOF;
			// exchange sets PR_MESSAGE_CLASS to IPM.Note.Rules.OofTemplate.Microsoft to get the icon
			hr = lpMessage->SetProps(1, &sIcon, nullptr);
			if (hr != hrSuccess)
				return hr;
		}

		// Sensitivity header
		if (vmHeader->hasField("Sensitivity")) {
			SPropValue sSensitivity;
			auto sensitivity = strToLower(vmHeader->findField("Sensitivity")->getValue()->generate());
			sSensitivity.ulPropTag = PR_SENSITIVITY;
			if (sensitivity.compare("personal") == 0)
				sSensitivity.Value.ul = SENSITIVITY_PERSONAL;
			else if (sensitivity.compare("private") == 0)
				sSensitivity.Value.ul = SENSITIVITY_PRIVATE;
			else if (sensitivity.compare("company-confidential") == 0)
				sSensitivity.Value.ul = SENSITIVITY_COMPANY_CONFIDENTIAL;
			else
				sSensitivity.Value.ul = SENSITIVITY_NONE;
			hr = lpMessage->SetProps(1, &sSensitivity, nullptr);
			if (hr != hrSuccess)
				return hr;
		}

		// Expiry time header
		field = vmHeader->findField("Expiry-Time");
		if (field != nullptr) {
			SPropValue sExpiryTime;

			// reparse string to datetime
			vmime::datetime expiry(field->getValue()->generate());

			sExpiryTime.ulPropTag = PR_EXPIRY_TIME;
			sExpiryTime.Value.ft = vmimeDatetimeToFiletime(expiry);

			hr = lpMessage->SetProps(1, &sExpiryTime, NULL);
			if (hr != hrSuccess)
				return hr;
		}

		// read receipt	request
		// note: vmime never checks if the given pos to getMailboxAt() and similar functions is valid, so we check if the list is empty before using it
		if (vmHeader->hasField("Disposition-Notification-To") &&
		    !vmime::dynamicCast<vmime::mailboxList>(vmHeader->DispositionNotificationTo()->getValue())->isEmpty())
		{
			auto mbReadReceipt = vmime::dynamicCast<vmime::mailboxList>(vmHeader->DispositionNotificationTo()->getValue())->getMailboxAt(0); // we only use the 1st
			if (mbReadReceipt && !mbReadReceipt->isEmpty())
			{
				wstring wstrRRName = getWideFromVmimeText(mbReadReceipt->getName());
				wstring wstrRREmail = m_converter.convert_to<wstring>(mbReadReceipt->getEmail().toString());

				if (wstrRRName.empty())
					wstrRRName = wstrRREmail;

				//FIXME: Use an addressbook entry for "ZARAFA"-type addresses?
				hr = ECCreateOneOff((LPTSTR)wstrRRName.c_str(),	(LPTSTR)L"SMTP", (LPTSTR)wstrRREmail.c_str(), MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbEntryID, &~lpEntryID);
				if (hr != hrSuccess)
					return hr;

				SPropValue sRRProps[4];
				sRRProps[0].ulPropTag = PR_READ_RECEIPT_REQUESTED;
				sRRProps[0].Value.b = true;
				
				sRRProps[1].ulPropTag = PR_MESSAGE_FLAGS;
				sRRProps[1].Value.ul = (m_dopt.mark_as_read ? MSGFLAG_READ : 0) | MSGFLAG_UNMODIFIED | MSGFLAG_RN_PENDING | MSGFLAG_NRN_PENDING;

				sRRProps[2].ulPropTag = PR_REPORT_ENTRYID;
				sRRProps[2].Value.bin.cb = cbEntryID;
				sRRProps[2].Value.bin.lpb = reinterpret_cast<unsigned char *>(lpEntryID.get());
				sRRProps[3].ulPropTag = PR_REPORT_NAME_W;
				sRRProps[3].Value.lpszW = (WCHAR *)wstrRREmail.c_str();

				hr = lpMessage->SetProps(4, sRRProps, NULL);
				if (hr != hrSuccess)
					return hr;
			}
		}

		for (const auto &field : vmHeader->getFieldList()) {
			std::string value, name = field->getName();
			
			if (name[0] != 'X')
				continue;

			// exclusion list?
			if (name == "X-Priority")
				continue;
			name = strToLower(name);

			memory_ptr<MAPINAMEID> lpNameID;
			memory_ptr<SPropTagArray> lpPropTags;

			if ((hr = MAPIAllocateBuffer(sizeof(MAPINAMEID), &~lpNameID)) != hrSuccess)
				return hr;
			lpNameID->lpguid = (GUID*)&PS_INTERNET_HEADERS;
			lpNameID->ulKind = MNID_STRING;

			int vlen = mbstowcs(NULL, name.c_str(), 0) +1;
			if ((hr = MAPIAllocateMore(vlen*sizeof(WCHAR), lpNameID, (void**)&lpNameID->Kind.lpwstrName)) != hrSuccess)
				return hr;
			mbstowcs(lpNameID->Kind.lpwstrName, name.c_str(), vlen);
			hr = lpMessage->GetIDsFromNames(1, &+lpNameID, MAPI_CREATE, &~lpPropTags);
			if (hr != hrSuccess) {
				hr = hrSuccess;
				continue;
			}

			SPropValue sProp;
			value = field->getValue()->generate();
			sProp.ulPropTag = PROP_TAG(PT_STRING8, PROP_ID(lpPropTags->aulPropTag[0]));
			sProp.Value.lpszA = (char*)value.c_str();
			lpMessage->SetProps(1, &sProp, nullptr);
			// in case of error: ignore this x-header as named props then
		}
	}
	catch (vmime::exception& e) {
		ec_log_err("VMIME exception on parsing headers: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (std::exception& e) {
		ec_log_err("STD exception on parsing headers: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (...) {
		ec_log_err("Unknown generic exception occurred on parsing headers");
		return MAPI_E_CALL_FAILED;
	}
	return hr;
}

/**
 * Sets PR_MESSAGE_TO_ME, PR_MESSAGE_CC_ME and PR_MESSAGE_RECIP_ME appropriately.
 *
 * Delivery options struct should contain the EntryID of the user you
 * are delivering for.
 *
 * @param[out]	lpMessage		MAPI IMessage to set properties in
 * @param[in]	lpRecipients	List of MAPI recipients found in To/Cc/Bcc headers.
 * @return		MAPI error code.
 */
HRESULT VMIMEToMAPI::handleMessageToMeProps(IMessage *lpMessage, LPADRLIST lpRecipients) {
	unsigned int i = 0;
	bool bToMe = false;
	bool bCcMe = false;
	bool bRecipMe = false;
	SPropValue sProps[3];

	if (m_dopt.user_entryid == NULL)
		return hrSuccess; /* Not an error, but do not do any processing */

	// Loop through all recipients of the message to find ourselves in the recipient list.
	for (i = 0; i < lpRecipients->cEntries; ++i) {
		auto lpRecipType = lpRecipients->aEntries[i].cfind(PR_RECIPIENT_TYPE);
		auto lpEntryId = lpRecipients->aEntries[i].cfind(PR_ENTRYID);
		if(lpRecipType == NULL)
			continue;

		if(lpEntryId == NULL)
			continue;

		// The user matches if the entryid of the recipient is equal to ours
		if(lpEntryId->Value.bin.cb != m_dopt.user_entryid->cb)
			continue;

		if(memcmp(lpEntryId->Value.bin.lpb, m_dopt.user_entryid->lpb, lpEntryId->Value.bin.cb) != 0)
			continue;

		// Users match, check what type
		bRecipMe = true;

		if(lpRecipType->Value.ul == MAPI_TO)
			bToMe = true;
		else if(lpRecipType->Value.ul == MAPI_CC)
			bCcMe = true;
	}

	// Set the properties
	sProps[0].ulPropTag = PR_MESSAGE_RECIP_ME;
	sProps[0].Value.b = bRecipMe;
	sProps[1].ulPropTag = PR_MESSAGE_TO_ME;
	sProps[1].Value.b = bToMe;
	sProps[2].ulPropTag = PR_MESSAGE_CC_ME;
	sProps[2].Value.b = bCcMe;

	lpMessage->SetProps(3, sProps, NULL);
	return hrSuccess;
}

/**
 * Convert To/Cc/Bcc headers to a valid recipient table in the
 * IMessage object.
 *
 * @param[in]	vmHeader	vmime header part of a message.
 * @param[out]	lpMessage	MAPI message to write header properties in.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::handleRecipients(vmime::shared_ptr<vmime::header> vmHeader,
    IMessage *lpMessage)
{
	HRESULT		hr				= hrSuccess;
	adrlist_ptr lpRecipients;

	try {
		auto lpVMAListRecip = vmime::dynamicCast<vmime::addressList>(vmHeader->To()->getValue());
		auto lpVMAListCopyRecip = vmime::dynamicCast<vmime::addressList>(vmHeader->Cc()->getValue());
		auto lpVMAListBlCpRecip = vmime::dynamicCast<vmime::addressList>(vmHeader->Bcc()->getValue());
		int iAdresCount = lpVMAListRecip->getAddressCount() + lpVMAListCopyRecip->getAddressCount() + lpVMAListBlCpRecip->getAddressCount();

		if (iAdresCount == 0)
			return hr;
		hr = MAPIAllocateBuffer(CbNewADRLIST(iAdresCount), &~lpRecipients);
		if (hr != hrSuccess)
			return hr;
		lpRecipients->cEntries = 0;

		if (!lpVMAListRecip->isEmpty()) {
			hr = modifyRecipientList(lpRecipients, lpVMAListRecip, MAPI_TO);
			if (hr != hrSuccess)
				return hr;
		}

		if (!lpVMAListCopyRecip->isEmpty()) {
			hr = modifyRecipientList(lpRecipients, lpVMAListCopyRecip, MAPI_CC);
			if (hr != hrSuccess)
				return hr;
		}

		if (!lpVMAListBlCpRecip->isEmpty()) {
			hr = modifyRecipientList(lpRecipients, lpVMAListBlCpRecip, MAPI_BCC);
			if (hr != hrSuccess)
				return hr;
		}
		
		// Handle PR_MESSAGE_*_ME props
		hr = handleMessageToMeProps(lpMessage, lpRecipients);
		if (hr != hrSuccess)
			return hr;

		// actually modify recipients in mapi object
		hr = lpMessage->ModifyRecipients(0, lpRecipients);
		if (hr != hrSuccess)
			return hr;
	}
	catch (vmime::exception& e) {
		ec_log_err("VMIME exception on recipients: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (std::exception& e) {
		ec_log_err("STD exception on recipients: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (...) {
		ec_log_err("Unknown generic exception occurred on recipients");
		return MAPI_E_CALL_FAILED;
	}
	return hrSuccess;
}

/**
 * Adds recipients from a vmime list to rows for the recipient
 * table. Starts adding at offset in cEntries member of the lpRecipients
 * struct. The caller must ensure that lpRecipients has enough storage.
 *
 * Entries are either converted to an addressbook entry, or an one-off entry.
 *
 * @param[out]	lpRecipients	MAPI address list to be filled.
 * @param[in]	vmAddressList	List of recipient of a specific type (To/Cc/Bcc).
 * @param[in]	ulRecipType		Type of recipients found in vmAddressList.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::modifyRecipientList(LPADRLIST lpRecipients,
    vmime::shared_ptr<vmime::addressList> vmAddressList, ULONG ulRecipType)
{
	HRESULT			hr				= hrSuccess;
	int				iAddressCount	= vmAddressList->getAddressCount();
	ULONG			cbEntryID		= 0;
	memory_ptr<ENTRYID> lpEntryID;
	vmime::shared_ptr<vmime::mailbox> mbx;
	vmime::shared_ptr<vmime::mailboxGroup> grp;
	vmime::shared_ptr<vmime::address> vmAddress;
	std::wstring	wstrName;
	std::string		strEmail, strSearch;

	// order and types are important for modifyFromAddressBook()
	static constexpr const SizedSPropTagArray(7, sptaRecipientProps) =
		{7, {PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_DISPLAY_TYPE,
		PR_EMAIL_ADDRESS_W, PR_ENTRYID, PR_SEARCH_KEY,
		PR_SMTP_ADDRESS_W}};

	// walk through all recipients
	for (int iRecip = 0; iRecip < iAddressCount; ++iRecip) {
		
		try {
			vmime::text vmText;

			mbx = NULL;
			grp = NULL;

			vmAddress = vmAddressList->getAddressAt(iRecip);
			
			if (vmAddress->isGroup()) {
				grp = vmime::dynamicCast<vmime::mailboxGroup>(vmAddress);
				if (!grp)
					continue;
				strEmail.clear();
				vmText = grp->getName();
				if (grp->isEmpty() && vmText == vmime::text("undisclosed-recipients"))
					continue;
			} else {
				mbx = vmime::dynamicCast<vmime::mailbox>(vmAddress);
				if (!mbx)
					continue;
				strEmail = mbx->getEmail().toString();
				vmText = mbx->getName();
			}

			if (!vmText.isEmpty())
				wstrName = getWideFromVmimeText(vmText);
			else
				wstrName.clear();
		}
		catch (vmime::exception& e) {
			ec_log_err("VMIME exception on modify recipient: %s", e.what());
			return MAPI_E_CALL_FAILED;
		}
		catch (std::exception& e) {
			ec_log_err("STD exception on modify recipient: %s", e.what());
			return MAPI_E_CALL_FAILED;
		}
		catch (...) {
			ec_log_err("Unknown generic exception occurred on modify recipient");
			return MAPI_E_CALL_FAILED;
		}

		const unsigned int iRecipNum = lpRecipients->cEntries;
		auto &recip = lpRecipients->aEntries[iRecipNum];

		// use email address or fullname to find GAB entry, do not pass fullname to keep resolved addressbook fullname
		strSearch = strEmail;
		if (strSearch.empty())
			strSearch = m_converter.convert_to<std::string>(wstrName);

		// @todo: maybe make strSearch a wide string and check if we need to use the fullname argument for modifyFromAddressBook
		hr = modifyFromAddressBook(&recip.rgPropVals, &recip.cValues,
		     strSearch.c_str(), NULL, ulRecipType, sptaRecipientProps);
		if (hr == hrSuccess) {
			++lpRecipients->cEntries;
			continue;
		}

		// Fallback if the entry was not found (or errored) in the addressbook
		const int iNumTags = 8;
		if (wstrName.empty())
			wstrName = m_converter.convert_to<wstring>(strEmail);

		// will be cleaned up by caller.
		hr = MAPIAllocateBuffer(sizeof(SPropValue) * iNumTags, reinterpret_cast<void **>(&recip.rgPropVals));
		if (hr != hrSuccess)
			return hr;

		recip.cValues = iNumTags;
		recip.ulReserved1 = 0;
		auto &prop = recip.rgPropVals;
		prop[0].ulPropTag = PR_RECIPIENT_TYPE;
		prop[0].Value.l = ulRecipType;
		prop[1].ulPropTag = PR_DISPLAY_NAME_W;
		hr = MAPIAllocateMore((wstrName.size() + 1) * sizeof(wchar_t), prop,
		     reinterpret_cast<void **>(&prop[1].Value.lpszW));
		if (hr != hrSuccess)
			return hr;
		wcscpy(prop[1].Value.lpszW, wstrName.c_str());

		prop[2].ulPropTag = PR_SMTP_ADDRESS_A;
		hr = MAPIAllocateMore(strEmail.size() + 1, prop,
		     reinterpret_cast<void **>(&prop[2].Value.lpszA));
		if (hr != hrSuccess)
			return hr;
		strcpy(prop[2].Value.lpszA, strEmail.c_str());

		prop[3].ulPropTag = PR_ENTRYID;
		hr = ECCreateOneOff((LPTSTR)wstrName.c_str(), (LPTSTR)L"SMTP", (LPTSTR)m_converter.convert_to<wstring>(strEmail).c_str(),
		     MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbEntryID, &~lpEntryID);
		if (hr != hrSuccess)
			return hr;

		hr = MAPIAllocateMore(cbEntryID, prop,
		     reinterpret_cast<void **>(&prop[3].Value.bin.lpb));
		if (hr != hrSuccess)
			return hr;

		prop[3].Value.bin.cb = cbEntryID;
		memcpy(prop[3].Value.bin.lpb, lpEntryID, cbEntryID);

		prop[4].ulPropTag = PR_ADDRTYPE_W;
		prop[4].Value.lpszW = const_cast<wchar_t *>(L"SMTP");

		strSearch = strToUpper("SMTP:" + strEmail);
		prop[5].ulPropTag = PR_SEARCH_KEY;
		prop[5].Value.bin.cb = strSearch.size() + 1; // we include the trailing 0 as MS does this also
		hr = MAPIAllocateMore(strSearch.size() + 1, prop,
		     reinterpret_cast<void **>(&prop[5].Value.bin.lpb));
		if (hr != hrSuccess)
			return hr;
		memcpy(prop[5].Value.bin.lpb, strSearch.c_str(), strSearch.size() + 1);

		// Add Email address
		prop[6].ulPropTag = PR_EMAIL_ADDRESS_A;
		hr = MAPIAllocateMore(strEmail.size() + 1, prop,
		     reinterpret_cast<void **>(&prop[6].Value.lpszA));
		if (hr != hrSuccess)
			return hr;
		strcpy(prop[6].Value.lpszA, strEmail.c_str());

		// Add display type
		prop[7].ulPropTag = PR_DISPLAY_TYPE;
		prop[7].Value.ul = DT_MAILUSER;
		++lpRecipients->cEntries;
	}
	return hrSuccess;
}

/**
 * copies data from addressbook into lpRecipient
 *
 * @param[out]	lppPropVals	Properties from addressbook.
 * @param[out]	lpulValues	Number of properties returned in lppPropVals
 * @param[in]	email		SMTP email address
 * @param[in]	fullname	Fullname given in email for this address, can be different from fullname in addressbook.
 * @param[in]	ulRecipType	PR_RECIPIENT_TYPE if ! MAPI_ORIG
 * @param[in]	lpPropList	Properties to return in lppPropVals. Must be in specific order.
 * @return		MAPI error code.
 */
HRESULT VMIMEToMAPI::modifyFromAddressBook(LPSPropValue *lppPropVals,
    ULONG *lpulValues, const char *email, const wchar_t *fullname,
    ULONG ulRecipType, const SPropTagArray *lpPropsList)
{
	HRESULT hr = hrSuccess;
	memory_ptr<ENTRYID> lpDDEntryID;
	ULONG cbDDEntryID;
	ULONG ulObj = 0;
	adrlist_ptr lpAdrList;
	memory_ptr<FlagList> lpFlagList;
	const SPropValue *lpProp = nullptr;
	SPropValue sRecipProps[9]; // 8 from addressbook + PR_RECIPIENT_TYPE == max
	ULONG cValues = 0;
	static constexpr const SizedSPropTagArray(8, sptaAddress) =
		{8, {PR_SMTP_ADDRESS_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W,
		PR_DISPLAY_TYPE, PR_DISPLAY_NAME_W, PR_ENTRYID, PR_SEARCH_KEY,
		PR_OBJECT_TYPE}};

	if (m_lpAdrBook == nullptr)
		return MAPI_E_NOT_FOUND;

	if ((email == nullptr || *email == '\0') &&
	    (fullname == nullptr || *fullname == '\0'))
		// we have no data to lookup
		return MAPI_E_NOT_FOUND;

	if (!m_lpDefaultDir) {
		hr = m_lpAdrBook->GetDefaultDir(&cbDDEntryID, &~lpDDEntryID);
		if (hr != hrSuccess)
			return hr;
		hr = m_lpAdrBook->OpenEntry(cbDDEntryID, lpDDEntryID,
		     &iid_of(m_lpDefaultDir), 0, &ulObj, &~m_lpDefaultDir);
		if (hr != hrSuccess)
			return hr;
	}

	hr = MAPIAllocateBuffer(CbNewADRLIST(1), &~lpAdrList);
	if (hr != hrSuccess)
		return hr;
	lpAdrList->cEntries = 1;
	auto &aent = lpAdrList->aEntries[0];
	aent.cValues = 1;
	hr = MAPIAllocateBuffer(sizeof(SPropValue), reinterpret_cast<void **>(&aent.rgPropVals));
	if (hr != hrSuccess)
		return hr;

	// static reference is OK here
	if (!email || *email == '\0') {
		aent.rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_W;
		aent.rgPropVals[0].Value.lpszW = const_cast<wchar_t *>(fullname); // try to find with fullname for groups without email addresses
	}
	else {
		aent.rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_A;
		aent.rgPropVals[0].Value.lpszA = const_cast<char *>(email); // normally resolve on email address
	}
	hr = MAPIAllocateBuffer(CbNewFlagList(1), &~lpFlagList);
	if (hr != hrSuccess)
		return hr;

	lpFlagList->cFlags = 1;
	lpFlagList->ulFlag[0] = MAPI_UNRESOLVED;
	hr = m_lpDefaultDir->ResolveNames(sptaAddress, EMS_AB_ADDRESS_LOOKUP,
	     lpAdrList, lpFlagList);
	if (hr != hrSuccess)
		return hr;
	if (lpFlagList->cFlags != 1 || lpFlagList->ulFlag[0] != MAPI_RESOLVED)
		return MAPI_E_NOT_FOUND;

	// the server told us the entry is here.  from this point on we
	// don't want to return MAPI_E_NOT_FOUND anymore, so we need to
	// deal with missing data (which really shouldn't be the case for
	// some, so asserts in some places).

	if (PROP_TYPE(lpPropsList->aulPropTag[0]) != PT_NULL) {
		lpProp = aent.cfind(PR_ADDRTYPE_W);
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[0]; // PR_xxx_ADDRTYPE;
		assert(lpProp);
		if (!lpProp) {
			ec_log_warn("Missing PR_ADDRTYPE_W for search entry: email %s, fullname %ls", email ? email : "null", fullname ? fullname : L"null");
			sRecipProps[cValues].Value.lpszW = const_cast<wchar_t *>(L"ZARAFA");
		} else {
			sRecipProps[cValues].Value.lpszW = lpProp->Value.lpszW;
		}
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[1]) != PT_NULL) {
		lpProp = aent.cfind(PR_DISPLAY_NAME_W);
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[1];	// PR_xxx_DISPLAY_NAME;
		if (lpProp)
			sRecipProps[cValues].Value.lpszW = lpProp->Value.lpszW;	// use addressbook version
		else if (fullname && *fullname != '\0')
			sRecipProps[cValues].Value.lpszW = const_cast<wchar_t *>(fullname); // use email version
		else if (email && *email != '\0')
			sRecipProps[cValues].Value.lpszW = reinterpret_cast<wchar_t *>(const_cast<char *>(email)); // use email address
		else {
			sRecipProps[cValues].ulPropTag = CHANGE_PROP_TYPE(lpPropsList->aulPropTag[1], PT_ERROR);
			sRecipProps[cValues].Value.err = MAPI_E_NOT_FOUND;
		}
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[2]) != PT_NULL) {
		lpProp = aent.cfind(PR_DISPLAY_TYPE);
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[2]; // PR_xxx_DISPLAY_TYPE;
		if (lpProp == nullptr)
			sRecipProps[cValues].Value.ul = DT_MAILUSER;
		else
			sRecipProps[cValues].Value.ul = lpProp->Value.ul;
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[3]) != PT_NULL) {
		lpProp = aent.cfind(PR_EMAIL_ADDRESS_W);
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[3]; // PR_xxx_EMAIL_ADDRESS;
		assert(lpProp);
		if (!lpProp) {
			sRecipProps[cValues].ulPropTag = CHANGE_PROP_TYPE(lpPropsList->aulPropTag[3], PT_ERROR);
			sRecipProps[cValues].Value.err = MAPI_E_NOT_FOUND;
		} else {
			sRecipProps[cValues].Value.lpszW = lpProp->Value.lpszW;
		}
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[4]) != PT_NULL) {
		lpProp = aent.cfind(PR_ENTRYID);
		assert(lpProp);
		if (lpProp == nullptr)
			// the one exception I guess? Let the fallback code create a one off entryid
			return MAPI_E_NOT_FOUND;
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[4]; // PR_xxx_ENTRYID;
		sRecipProps[cValues].Value.bin = lpProp->Value.bin;
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[5]) != PT_NULL) {
		lpProp = aent.cfind(PR_SEARCH_KEY);
		if (!lpProp) {
			sRecipProps[cValues].ulPropTag = CHANGE_PROP_TYPE(lpPropsList->aulPropTag[5], PT_ERROR);
			sRecipProps[cValues].Value.err = MAPI_E_NOT_FOUND;
		} else {
			sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[5]; // PR_xxx_SEARCH_KEY;
			sRecipProps[cValues].Value.bin = lpProp->Value.bin;
		}
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[6]) != PT_NULL) {
		lpProp = aent.cfind(PR_SMTP_ADDRESS_W);
		if (!lpProp) {
			sRecipProps[cValues].ulPropTag = CHANGE_PROP_TYPE(lpPropsList->aulPropTag[6], PT_ERROR); // PR_xxx_SMTP_ADDRESS;
			sRecipProps[cValues].Value.err = MAPI_E_NOT_FOUND;
		} else {
			sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[6]; // PR_xxx_SMTP_ADDRESS;
			sRecipProps[cValues].Value.lpszW = lpProp->Value.lpszW;
		}
		++cValues;
	}

	lpProp = aent.cfind(PR_OBJECT_TYPE);
	assert(lpProp);
	if (lpProp == nullptr)
		sRecipProps[cValues].Value.ul = MAPI_MAILUSER;
	else
		sRecipProps[cValues].Value.ul = lpProp->Value.ul;
	sRecipProps[cValues].ulPropTag = PR_OBJECT_TYPE;
	++cValues;

	if (ulRecipType != MAPI_ORIG) {
		sRecipProps[cValues].ulPropTag = PR_RECIPIENT_TYPE;
		sRecipProps[cValues].Value.ul = ulRecipType;
		++cValues;
	}

	hr = Util::HrCopyPropertyArray(sRecipProps, cValues, lppPropVals, &cValues);
	if (hr == hrSuccess && lpulValues)
		*lpulValues = cValues;
	return hr;
}

/** 
 * Order alternatives in a body according to local preference.
 *
 * This function (currently) only deprioritizes text/plain parts, and leaves
 * the priority of everything else as-is.
 *
 * This function also reverses the list. Whereas MIME parts in @vmBody are
 * ordered from boring-to-interesting, the list returned by this function is
 * interesting-to-boring.
 */
static std::list<unsigned int>
vtm_order_alternatives(vmime::shared_ptr<vmime::body> vmBody)
{
	vmime::shared_ptr<vmime::header> vmHeader;
	vmime::shared_ptr<vmime::bodyPart> vmBodyPart;
	vmime::shared_ptr<vmime::mediaType> mt;
	std::list<unsigned int> lBodies;

	for (size_t i = 0; i < vmBody->getPartCount(); ++i) {
		vmBodyPart = vmBody->getPartAt(i);
		vmHeader = vmBodyPart->getHeader();
		if (!vmHeader->hasField(vmime::fields::CONTENT_TYPE)) {
			/* RFC 2046 §5.1 ¶2 says treat it as text/plain */
			lBodies.emplace_front(i);
			continue;
		}
		mt = vmime::dynamicCast<vmime::mediaType>(vmHeader->ContentType()->getValue());
		// mostly better alternatives for text/plain, so try that last
		if (mt->getType() == vmime::mediaTypes::TEXT && mt->getSubType() == vmime::mediaTypes::TEXT_PLAIN)
			lBodies.emplace_back(i);
		else
			lBodies.emplace_front(i);
	}
	return lBodies;
}

HRESULT VMIMEToMAPI::dissect_multipart(vmime::shared_ptr<vmime::header> vmHeader,
    vmime::shared_ptr<vmime::body> vmBody, IMessage *lpMessage,
    bool bFilterDouble, bool bAppendBody)
{
	bool bAlternative = false;
	HRESULT hr = hrSuccess;

	if (vmBody->getPartCount() <= 0) {
		// a lonely attachment in a multipart, may not be empty when it's a signed part.
		hr = handleAttachment(vmHeader, vmBody, lpMessage);
		if (hr != hrSuccess)
			ec_log_err("dissect_multipart: Unable to save attachment");
		return hr;
	}

	// check new multipart type
	auto mt = vmime::dynamicCast<vmime::mediaType>(vmHeader->ContentType()->getValue());
	if (mt->getSubType() == "appledouble")
		bFilterDouble = true;
	else if (mt->getSubType() == "mixed")
		bAppendBody = true;
	else if (mt->getSubType() == "alternative")
		bAlternative = true;

		/*
		 * RFC 2046 §5.1.7: all unrecognized subtypes are to be
		 * treated like multipart/mixed.
		 *
		 * At least that is what it said back then. RFC 2387 then came
		 * along,… and now we don't set bAppendBody for unresearched
		 * reasons.
		 */

	if (!bAlternative) {
		// recursively process multipart message
		for (size_t i = 0; i < vmBody->getPartCount(); ++i) {
			auto vmBodyPart = vmBody->getPartAt(i);
			hr = dissect_body(vmBodyPart->getHeader(), vmBodyPart->getBody(), lpMessage, bFilterDouble, bAppendBody);
			if (hr != hrSuccess) {
				ec_log_err("dissect_multipart: Unable to parse sub multipart %zu of mail body", i);
				return hr;
			}
		}
		return hrSuccess;
	}

	list<unsigned int> lBodies = vtm_order_alternatives(vmBody);

	// recursively process multipart alternatives in reverse to select best body first
	for (auto body_idx : lBodies) {
		auto vmBodyPart = vmBody->getPartAt(body_idx);
		ec_log_debug("Trying to parse alternative multipart %d of mail body", body_idx);

		hr = dissect_body(vmBodyPart->getHeader(), vmBodyPart->getBody(), lpMessage, bFilterDouble, bAppendBody);
		if (hr == hrSuccess)
			return hrSuccess;
		ec_log_err("Unable to parse alternative multipart %d of mail body, trying other alternatives", body_idx);
	}
	/* If lBodies was empty, we could get here, with hr being hrSuccess. */
	if (hr != hrSuccess)
		ec_log_err("Unable to parse all alternative multiparts of mail body");
	return hr;
}

void VMIMEToMAPI::dissect_message(vmime::shared_ptr<vmime::body> vmBody,
    IMessage *lpMessage)
{
	// Create Attach
	ULONG ulAttNr = 0;
	object_ptr<IAttach> pAtt;
	object_ptr<IMessage> lpNewMessage;
	memory_ptr<SPropValue> lpSubject;
	SPropValue sAttachMethod;
	char *lpszBody = NULL, *lpszBodyOrig = NULL;
	sMailState savedState;

	std::string newMessage;
	vmime::utility::outputStreamStringAdapter os(newMessage);
	vmBody->generate(os);

	lpszBodyOrig = lpszBody = (char *)newMessage.c_str();

	// Skip any leading newlines from the e-mail (attached messaged produced by Microsoft MimeOLE seem to do this)
	while (*lpszBody != '\0' && (*lpszBody == '\r' || *lpszBody == '\n'))
		++lpszBody;

	// and remove from string
	newMessage.erase(0, lpszBody - lpszBodyOrig);

	HRESULT hr = lpMessage->CreateAttach(nullptr, 0, &ulAttNr, &~pAtt);
	if (hr != hrSuccess)
		return;
	hr = pAtt->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0,
	     MAPI_CREATE | MAPI_MODIFY, &~lpNewMessage);
	if (hr != hrSuccess)
		return;

	// handle message-in-message, save current state variables
	savedState = m_mailState;
	m_mailState.reset();
	++m_mailState.ulMsgInMsg;

	hr = convertVMIMEToMAPI(newMessage, lpNewMessage);

	// return to previous state
	m_mailState = savedState;

	if (hr != hrSuccess)
		return;
	if (HrGetOneProp(lpNewMessage, PR_SUBJECT_W, &~lpSubject) == hrSuccess) {
		// Set PR_ATTACH_FILENAME of attachment to message subject, (WARNING: abuse of lpSubject variable)
		lpSubject->ulPropTag = PR_DISPLAY_NAME_W;
		pAtt->SetProps(1, lpSubject, NULL);
	}

	sAttachMethod.ulPropTag = PR_ATTACH_METHOD;
	sAttachMethod.Value.ul = ATTACH_EMBEDDED_MSG;
	pAtt->SetProps(1, &sAttachMethod, NULL);

	lpNewMessage->SaveChanges(0);
	pAtt->SaveChanges(0);
}

HRESULT VMIMEToMAPI::dissect_ical(vmime::shared_ptr<vmime::header> vmHeader,
    vmime::shared_ptr<vmime::body> vmBody, IMessage *lpMessage,
    bool bIsAttachment)
{
	HRESULT hr;
	// ical file
	string icaldata;
	vmime::utility::outputStreamStringAdapter os(icaldata);
	std::string strCharset;
	MessagePtr ptrNewMessage;
	LPMESSAGE lpIcalMessage = lpMessage;
	AttachPtr ptrAttach;
	ULONG ulAttNr = 0;
	std::unique_ptr<ICalToMapi> lpIcalMapi;
	ICalToMapi *tmpicalmapi;
	SPropValuePtr ptrSubject;
	ULONG ical_mapi_flags = IC2M_NO_RECIPIENTS | IC2M_APPEND_ONLY;
	/*
	 * Some senders send UTF-8 iCalendar information without a charset
	 * (Exchange does this). Default to UTF-8 if no charset was specified,
	 * as mandated by RFC 5545 § 3.1.4.
	 */
	strCharset = vmBody->getCharset().getName();
	if (strCharset == "us-ascii")
		// We can safely upgrade from US-ASCII to UTF-8 since that is compatible
		strCharset = "utf-8";

	vmBody->getContents()->extract(os);

	if (m_mailState.bodyLevel > BODY_NONE)
		/* Force attachment if we already have some text. */
		bIsAttachment = true;

	if (bIsAttachment) {
		// create message in message to create calendar message
		SPropValue sAttProps[3];

		hr = lpMessage->CreateAttach(nullptr, 0, &ulAttNr, &~ptrAttach);
		if (hr != hrSuccess)
			return kc_perror("dissect_ical-1790: Unable to create attachment for iCal data", hr);
		hr = ptrAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY, &~ptrNewMessage);
		if (hr != hrSuccess)
			return kc_perror("dissect_ical-1796: Unable to create message attachment for iCal data", hr);

		sAttProps[0].ulPropTag = PR_ATTACH_METHOD;
		sAttProps[0].Value.ul = ATTACH_EMBEDDED_MSG;

		sAttProps[1].ulPropTag = PR_ATTACHMENT_HIDDEN;
		sAttProps[1].Value.b = FALSE;

		sAttProps[2].ulPropTag = PR_ATTACH_FLAGS;
		sAttProps[2].Value.ul = 0;

		hr = ptrAttach->SetProps(3, sAttProps, NULL);
		if (hr != hrSuccess)
			return kc_perror("dissect_ical-1811: Unable to create message attachment for iCal data", hr);
		lpIcalMessage = ptrNewMessage.get();
	}

	hr = CreateICalToMapi(lpMessage, m_lpAdrBook, true, &tmpicalmapi);
	lpIcalMapi.reset(tmpicalmapi);
	if (hr != hrSuccess)
		return kc_perror("dissect_ical-1820: Unable to create iCal converter", hr);
	hr = lpIcalMapi->ParseICal(icaldata, strCharset, "UTC" , NULL, 0);
	if (hr != hrSuccess || lpIcalMapi->GetItemCount() != 1) {
		ec_log_err("dissect_ical-1826: Unable to parse ical information: %s (%x), items: %d, adding as normal attachment",
			GetMAPIErrorMessage(hr), hr, lpIcalMapi->GetItemCount());
		return handleAttachment(vmHeader, vmBody, lpMessage, L"unparsable_ical");
	}

	if (lpIcalMessage != lpMessage) {
		hr = lpIcalMapi->GetItem(0, 0, lpIcalMessage);
		if (hr != hrSuccess)
			return kc_perror("dissect_ical-1833: Error while converting iCal to MAPI", hr);
	}

	if (bIsAttachment)
		ical_mapi_flags |= IC2M_NO_BODY;

	/* Calendar properties need to be on the main message in any case. */
	hr = lpIcalMapi->GetItem(0, ical_mapi_flags, lpMessage);
	if (hr != hrSuccess)
		return kc_perror("dissect_ical-1834: Error while converting iCal to MAPI", hr);

	/* Evaluate whether vconverter gave us an initial body */
	if (!bIsAttachment && m_mailState.bodyLevel < BODY_PLAIN &&
	    (FPropExists(lpMessage, PR_BODY_A) ||
	    FPropExists(lpMessage, PR_BODY_W)))
		m_mailState.bodyLevel = BODY_PLAIN;
	if (!bIsAttachment)
		return hr;

	// give attachment name of calendar item
	if (HrGetOneProp(ptrNewMessage, PR_SUBJECT_W, &~ptrSubject) == hrSuccess) {
		ptrSubject->ulPropTag = PR_DISPLAY_NAME_W;

		hr = ptrAttach->SetProps(1, ptrSubject, NULL);
		if (hr != hrSuccess)
			return hr;
	}

	hr = ptrNewMessage->SaveChanges(0);
	if (hr != hrSuccess)
		return kc_perror("dissect_ical-1851: Unable to save iCal message", hr);
	hr = ptrAttach->SaveChanges(0);
	if (hr != hrSuccess)
		return kc_perror("dissect_ical-1856: Unable to save iCal message attachment", hr);

	// make sure we show the attachment icon
	m_mailState.attachLevel = ATTACH_NORMAL;
	return hrSuccess;
}

/**
 * Disect Body
 *
 * Here we are going to split the body into pieces and throw every
 * part into its container.  We make decisions on the basis of Content
 * Types...
 *
 * Content Types...
 * http://www.faqs.org/rfcs/rfc2046.html
 *
 * Top level				Subtypes
 * discrete:
 *	text				plain, html, richtext, enriched
 *	image				jpeg, gif, png.. etc
 *	audio				basic, wav, ai.. etc
 *	video				mpeg, avi.. etc
 *	application			octet-stream, postscript
 *
 * composite:
 *	multipart			mixed, alternative, digest ( contains message ), paralell, 
 *	message				rfc 2822, partial ( please no fragmentation and reassembly ), external-body
 *
 * @param[in]	vmHeader		vmime header part which describes the contents of the body in vmBody.
 * @param[in]	vmBody			a body part of the mail.
 * @param[out]	lpMessage		MAPI message to write header properties in.
 * @param[in]	filterDouble	skips some attachments when true, only happens then an appledouble attachment marker is found.
 * @param[in]	bAppendBody		Concatenate with existing body if true, makes an attachment when false and a body was previously saved.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::dissect_body(vmime::shared_ptr<vmime::header> vmHeader,
    vmime::shared_ptr<vmime::body> vmBody, IMessage *lpMessage,
    bool filterDouble, bool appendBody)
{
	HRESULT	hr = hrSuccess;
	object_ptr<IStream> lpStream;
	SPropValue sPropSMIMEClass;
	bool bFilterDouble = filterDouble;
	bool bAppendBody = appendBody;
	bool bIsAttachment = false;

	if (vmHeader->hasField(vmime::fields::MIME_VERSION))
		++m_mailState.mime_vtag_nest;

	try {
		auto mt = vmime::dynamicCast<vmime::mediaType>(vmHeader->ContentType()->getValue());
		bool force_raw = false;

		bIsAttachment = vmime::dynamicCast<vmime::contentDisposition>(vmHeader->ContentDisposition()->getValue())->getName() == vmime::contentDispositionTypes::ATTACHMENT;

		try {
			vmBody->getContents()->getEncoding().getEncoder();
		} catch (vmime::exceptions::no_encoder_available &) {
			/* RFC 2045 §6.4 page 17 */
			ec_log_debug("Encountered unknown Content-Transfer-Encoding \"%s\".",
				vmBody->getContents()->getEncoding().getName().c_str());
			force_raw = true;
		}

		if (force_raw) {
			hr = handleAttachment(vmHeader, vmBody, lpMessage, L"unknown_transfer_encoding", true);
			if (hr != hrSuccess)
				goto exit;
		} else if (mt->getType() == "multipart") {
			hr = dissect_multipart(vmHeader, vmBody, lpMessage, bFilterDouble, bAppendBody);
			if (hr != hrSuccess)
				goto exit;
		// Only handle as inline text if no filename is specified and not specified as 'attachment'
		} else if (mt->getType() == vmime::mediaTypes::TEXT &&
		    (mt->getSubType() == vmime::mediaTypes::TEXT_PLAIN || mt->getSubType() == vmime::mediaTypes::TEXT_HTML) &&
		    !bIsAttachment) {
			if (mt->getSubType() == vmime::mediaTypes::TEXT_HTML || (m_mailState.bodyLevel == BODY_HTML && bAppendBody)) {
				// handle real html part, or append a plain text bodypart to the html main body
				// subtype guaranteed html or plain.
				hr = handleHTMLTextpart(vmHeader, vmBody, lpMessage, bAppendBody);
				if (hr != hrSuccess) {
					ec_log_err("Unable to parse mail HTML text");
					goto exit;
				}
			} else {
				hr = handleTextpart(vmHeader, vmBody, lpMessage, bAppendBody);
				if (hr != hrSuccess)
					goto exit;
			}
		
		} else if (mt->getType() == vmime::mediaTypes::MESSAGE) {
			dissect_message(vmBody, lpMessage);
		} else if(mt->getType() == vmime::mediaTypes::APPLICATION && mt->getSubType() == "ms-tnef") {
			LARGE_INTEGER zero = {{0,0}};
			
			hr = CreateStreamOnHGlobal(nullptr, TRUE, &~lpStream);
			if(hr != hrSuccess)
				goto exit;
				
			outputStreamMAPIAdapter str(lpStream);
			vmBody->getContents()->extract(str);
			hr = lpStream->Seek(zero, STREAM_SEEK_SET, NULL);
			if(hr != hrSuccess)
				goto exit;
			
			ECTNEF tnef(TNEF_DECODE, lpMessage, lpStream);

			hr = tnef.ExtractProps(TNEF_PROP_EXCLUDE, NULL);
			if (hr == hrSuccess) {
				hr = tnef.Finish();
				if (hr != hrSuccess)
					ec_log_warn("TNEF attachment saving failed: 0x%08X", hr);
			} else {
				ec_log_warn("TNEF attachment parsing failed: 0x%08X", hr);
			}
			hr = hrSuccess;
		} else if (mt->getType() == vmime::mediaTypes::TEXT && mt->getSubType() == "calendar") {
			hr = dissect_ical(vmHeader, vmBody, lpMessage, bIsAttachment);
			if (hr != hrSuccess)
				goto exit;
		} else if (filterDouble && mt->getType() == vmime::mediaTypes::APPLICATION && mt->getSubType() == "applefile") {
		} else if (filterDouble && mt->getType() == vmime::mediaTypes::APPLICATION && mt->getSubType() == "mac-binhex40") {
				// ignore appledouble parts
				// mac-binhex40 is appledouble v1, applefile is v2
				// see: http://www.iana.org/assignments/media-types/multipart/appledouble			
		} else if (mt->getType() == vmime::mediaTypes::APPLICATION && (mt->getSubType() == "pkcs7-signature" || mt->getSubType() == "x-pkcs7-signature")) {
			// smime signature (smime.p7s)
			// must be handled a level above to get all headers and bodies beloning to the signed message
			m_mailState.bAttachSignature = true;
		} else if (mt->getType() == vmime::mediaTypes::APPLICATION && (mt->getSubType() == "pkcs7-mime" || mt->getSubType() == "x-pkcs7-mime")) {
			// smime encrypted message (smime.p7m), attachment may not be empty
			hr = handleAttachment(vmHeader, vmBody, lpMessage, L"smime.p7m", false);
			if (hr == MAPI_E_NOT_FOUND) {
				// skip empty attachment
				hr = hrSuccess;
				goto exit;
			}
			if (hr != hrSuccess)
				goto exit;

			// Mark the message so outlook knows how to find the encoded message
			sPropSMIMEClass.ulPropTag = PR_MESSAGE_CLASS_W;
			sPropSMIMEClass.Value.lpszW = const_cast<wchar_t *>(L"IPM.Note.SMIME");

			hr = lpMessage->SetProps(1, &sPropSMIMEClass, NULL);
			if (hr != hrSuccess) {
				ec_log_err("Unable to set message class");
				goto exit;
			}
		} else if (mt->getType() == vmime::mediaTypes::APPLICATION && mt->getSubType() == vmime::mediaTypes::APPLICATION_OCTET_STREAM) {
			if (vmime::dynamicCast<vmime::contentDispositionField>(vmHeader->ContentDisposition())->hasParameter("filename") ||
			    vmime::dynamicCast<vmime::contentTypeField>(vmHeader->ContentType())->hasParameter("name")) {
				// should be attachment
				hr = handleAttachment(vmHeader, vmBody, lpMessage);
				if (hr != hrSuccess)
					goto exit;
			} else {
				/*
				 * Possibly text?
				 * Unknown character set for text-* causes it
				 * the part to get interpreted as
				 * application-octet-stream (RFC 2049 §2
				 * item 6), and vmime presents it to us as
				 * such, making it impossible to know
				 * whether it was originally text-* or
				 * application-*.
				 */
				hr = handleTextpart(vmHeader, vmBody, lpMessage, false);
				if (hr != hrSuccess)
					goto exit;
			}
		} else {
			/* RFC 2049 §2 item 7 */
			hr = handleAttachment(vmHeader, vmBody, lpMessage, L"unknown_content_type");
			if (hr != hrSuccess)
				goto exit;
		}
	}
	catch (vmime::exception& e) {
		ec_log_err("VMIME exception on parsing body: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception& e) {
		ec_log_err("STD exception on parsing body: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		ec_log_err("Unknown generic exception occurred on parsing body");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

exit:
	if (vmHeader->hasField(vmime::fields::MIME_VERSION))
		--m_mailState.mime_vtag_nest;
	return hr;
}

/**
 * Decode the MIME part as per its Content-Transfer-Encoding header.
 * @im_body:	Internet Message / VMIME body object
 *
 * Returns the transfer-decoded data.
 */
std::string
VMIMEToMAPI::content_transfer_decode(vmime::shared_ptr<vmime::body> im_body) const
{
	/* TODO: Research how conversion could be minimized using streams. */
	std::string data;
	vmime::utility::outputStreamStringAdapter str_adap(data);
	auto im_cont = im_body->getContents();

	try {
		im_cont->extract(str_adap);
	} catch (vmime::exceptions::no_encoder_available &e) {
		ec_log_warn("VMIME could not process the Content-Transfer-Encoding \"%s\" (%s). Reading part raw.",
			im_cont->getEncoding().generate().c_str(), e.what());
		im_cont->extractRaw(str_adap);
	}
	return data;
}

/**
 * Attempt to repair some data streams with illegal/unknown encodings.
 * @charset:	character set as specified in Content-Type,
 * 		or what we so far know the encoding to be
 * @data:	data stream
 *
 * The function changes (may change) the mail @data in-place and returns the
 * new character set for it.
 */
vmime::charset
VMIMEToMAPI::get_mime_encoding(vmime::shared_ptr<vmime::header> im_header,
    vmime::shared_ptr<vmime::body> im_body) const
{
	auto ctf = vmime::dynamicCast<vmime::contentTypeField>(im_header->ContentType());

	if (ctf != NULL && ctf->hasParameter("charset"))
		return im_body->getCharset();

	return vmime::charset(im_charset_unspec);
}

/**
 * Try decoding the MIME body with a bunch of character sets
 * @data:	input body text, modified in-place if transformation successful
 * @cs:		list of character sets to try, ordered by descending preference
 *
 * Interpret the body text in various character sets and see in which one
 * all input characters appear to be valid codepoints. If none match, it
 * will be forcibly sanitized, possibly losing characters.
 * The string will also be type-converted in the process.
 * The index of the chosen character set will be returned.
 */
int VMIMEToMAPI::renovate_encoding(std::string &data,
    const std::vector<std::string> &cs)
{
	/*
	 * First check if any charset converts without raising
	 * illegal_sequence_exceptions.
	 */
	for (size_t i = 0; i < cs.size(); ++i) {
		const char *name = cs[i].c_str();
		try {
			data = m_converter.convert_to<std::string>(
			       (cs[i] + "//NOIGNORE").c_str(),
			       data, rawsize(data), name);
			ec_log_debug("renovate_encoding: reading data using charset \"%s\" succeeded.", name);
			return i;
		} catch (illegal_sequence_exception &ce) {
			/*
			 * Basically, choices other than the first are subpar
			 * and may not yield an RFC-compliant result (but
			 * perhaps a readable one nevertheless). Therefore,
			 * be very vocant about bad mail on the first failed
			 * one.
			 */
			unsigned int lvl = EC_LOGLEVEL_DEBUG;
			if (i == 0)
				lvl = EC_LOGLEVEL_WARNING;
			ec_log(lvl, "renovate_encoding: reading data using charset \"%s\" produced partial results: %s",
				name, ce.what());
		} catch (unknown_charset_exception &) {
			ec_log_warn("renovate_encoding: unknown charset \"%s\", skipping", name);
		}
	}
	/*
	 * Take the hit, convert with the next best thing and
	 * drop illegal sequences.
	 */
	for (size_t i = 0; i < cs.size(); ++i) {
		const char *name = cs[i].c_str();
		try {
			data = m_converter.convert_to<std::string>(
			       (cs[i] + "//IGNORE").c_str(), data, rawsize(data), name);
		} catch (unknown_charset_exception &) {
			continue;
		}
		ec_log_debug("renovate_encoding: forced interpretation as charset \"%s\".", name);
		return i;
	}
	return -1;
}

static bool ValidateCharset(const char *charset)
{
	/*
	 * iconv does not like to convert wchar_t to wchar_t, so filter that
	 * one. https://sourceware.org/bugzilla/show_bug.cgi?id=20804
	 */
	if (strcmp(charset, CHARSET_WCHAR) == 0)
		return true;
	iconv_t cd = iconv_open(CHARSET_WCHAR, charset);
	if (cd == (iconv_t)(-1))
		return false;
	iconv_close(cd);
	return true;
}

/**
 * Saves a plain text body part in the body or creates a new attachment.
 *
 * @param[in]	vmHeader	header describing contents of vmBody.
 * @param[in]	vmBody		body part contents.
 * @param[out]	lpMessage	IMessage object to be altered.
 * @param[in]	bAppendBody	Concatenate with existing body when still processing plain body parts (no HTML version already found).
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::handleTextpart(vmime::shared_ptr<vmime::header> vmHeader,
    vmime::shared_ptr<vmime::body> vmBody, IMessage* lpMessage, bool bAppendBody)
{
	HRESULT hr = S_OK;
	object_ptr<IStream> lpStream;

	bool append = m_mailState.bodyLevel < BODY_PLAIN ||
	              (m_mailState.bodyLevel == BODY_PLAIN && bAppendBody);

	if (!append) {
		// we already had a plaintext or html body, so attach this text part
		hr = handleAttachment(vmHeader, vmBody, lpMessage, L"secondary_object");
		if (hr != hrSuccess) {
			ec_log_err("Unable to parse attached text mail");
			return hr;
		}
		return hrSuccess;
	}

	// we have no body, or need to append more plain text body parts
	try {
		SPropValue sCodepage;

		/* determine first choice character set */
		vmime::charset mime_charset =
			get_mime_encoding(vmHeader, vmBody);
		if (mime_charset == im_charset_unspec) {
			if (m_mailState.mime_vtag_nest == 0) {
				/* RFC 2045 §4 page 9 */
				ec_log_debug("No charset (case #1). Defaulting to \"%s\".", m_dopt.ascii_upgrade);
				mime_charset = m_dopt.ascii_upgrade;
			} else {
				/* RFC 2045 §5.2 */
				ec_log_debug("No charset (case #2). Defaulting to \"us-ascii\".");
				mime_charset = vmime::charsets::US_ASCII;
			}
		}
		mime_charset = vtm_upgrade_charset(mime_charset, m_dopt.ascii_upgrade);
		if (!ValidateCharset(mime_charset.getName().c_str())) {
			auto newcs = mime_charset;
			auto r = m_dopt.cset_subst.find(mime_charset.getName());
			if (r != m_dopt.cset_subst.cend())
				newcs = r->second;
			if (!ValidateCharset(newcs.getName().c_str())) {
				/* RFC 2049 §2 item 6 subitem 5 */
				ec_log_debug("Unknown Content-Type charset \"%s\". Storing as attachment instead.", mime_charset.getName().c_str());
				return handleAttachment(vmHeader, vmBody, lpMessage, L"unknown_content_type", true);
			}
			mime_charset = std::move(newcs);
		}
		/*
		 * Because PR_BODY is not of type PT_BINARY, the length is
		 * determined by looking for the first \0 rather than a
		 * dedicated length field. This interferes with multibyte
		 * encodings which use 0x00 bytes in their sequences, such as
		 * UTF-16. (For example '!' in UTF-16BE is 0x00 0x21.)
		 *
		 * To cure this, the input is converted to a wide string, so
		 * that we work with codepoints instead of bytes. Then, we only
		 * have to consider U+0000 codepoints, which we will just strip
		 * as they are not very useful in text.
		 *
		 * The data will be stored in PR_BODY_W, and since the encoding
		 * is prescribed for that, PR_INTERNET_CPID is not needed, but
		 * we record it anyway… for the testsuite, and for its
		 * unreviewed use in MAPIToVMIME.
		 */
		std::string strBuffOut = content_transfer_decode(vmBody);
		std::wstring strUnicodeText = m_converter.convert_to<std::wstring>(CHARSET_WCHAR "//IGNORE", strBuffOut, rawsize(strBuffOut), mime_charset.getName().c_str());
		strUnicodeText.erase(std::remove(strUnicodeText.begin(), strUnicodeText.end(), L'\0'), strUnicodeText.end());

		if (HrGetCPByCharset(mime_charset.getName().c_str(), &sCodepage.Value.ul) != hrSuccess)
			/* pretend original input was UTF-8 */
			sCodepage.Value.ul = 65001;
		sCodepage.ulPropTag = PR_INTERNET_CPID;
		HrSetOneProp(lpMessage, &sCodepage);

		// create new or reset body
		ULONG ulFlags = MAPI_MODIFY;
		if (m_mailState.bodyLevel < BODY_PLAIN || !bAppendBody)
			ulFlags |= MAPI_CREATE;
		hr = lpMessage->OpenProperty(PR_BODY_W, &IID_IStream, STGM_TRANSACTED, ulFlags, &~lpStream);
		if (hr != hrSuccess)
			return hr;

		if (bAppendBody) {
			static const LARGE_INTEGER liZero = {{0, 0}};
			hr = lpStream->Seek(liZero, SEEK_END, NULL);
			if (hr != hrSuccess)
				return hr;
		}

		hr = lpStream->Write(strUnicodeText.c_str(), strUnicodeText.length() * sizeof(wstring::value_type), NULL);
		if (hr != hrSuccess)
			return hr;
		// commit triggers plain -> html/rtf conversion, PR_INTERNET_CPID must be set.
		hr = lpStream->Commit(0);
		if (hr != hrSuccess)
			return hr;
	}
	catch (vmime::exception &e) {
		ec_log_err("VMIME exception on text body: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (std::exception &e) {
		ec_log_err("STD exception on text body: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (...) {
		ec_log_err("Unknown generic exception occurred on text body");
		return MAPI_E_CALL_FAILED;
	}
	m_mailState.bodyLevel = BODY_PLAIN;
	return hrSuccess;
}

bool VMIMEToMAPI::filter_html(IMessage *msg, IStream *stream, ULONG flags,
    const std::string &html)
{
#ifdef HAVE_TIDY_H
	std::string clean_html;
	std::vector<std::string> error;
	HRESULT ret;

	bool clean_ok = rosie_clean_html(html, &clean_html, &error);
	for (size_t i = 0; i < error.size(); ++i)
		ec_log_debug("HTML clean: %s", error[i].c_str());
	if (!clean_ok)
		return false;

	ret = msg->OpenProperty(PR_EC_BODY_FILTERED, &IID_IStream,
	      STGM_TRANSACTED, flags, reinterpret_cast<LPUNKNOWN *>(&stream));
	if (ret != hrSuccess) {
		ec_log_warn("OpenProperty(PR_EC_BODY_FILTERED) failed: %s (%x)",
			GetMAPIErrorDescription(ret).c_str(), ret);
		return false;
	}

	ULONG written = 0;
	ret = stream->Write(clean_html.c_str(), clean_html.length(), &written);
	if (ret != hrSuccess) {
		/* check cbWritten too? */
		ec_log_warn("Write(PR_EC_BODY_FILTERED) failed: %s (%x)",
			GetMAPIErrorDescription(ret).c_str(), ret);
		return false;
	}

	ret = stream->Commit(0);
	if (ret != hrSuccess) {
		ec_log_warn("Commit(PR_EC_BODY_FILTERED) failed: %s (%x)",
			GetMAPIErrorDescription(ret).c_str(), ret);
		return false;
	}
#endif
	return true;
}

/**
 * Converts a html body to the MAPI PR_HTML property using
 * streams. Clients syncs this to PR_BODY and PR_RTF_COMPRESSED
 * versions, to previously processed plain text bodies will be
 * overwritten.
 *
 * @param[in]	vmHeader	header part describing the vmBody parameter.
 * @param[in]	vmBody		body part containing HTML.
 * @param[out]	lpMessage	IMessage to be modified.
 * @param[in] bAppendBody	Concatenate with existing body when still
 *   processing HTML body parts when set to true, otherwise it will
 *   become an attachment.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 *
 * On the matter of character sets:
 *
 * 	“Using a <meta> tag for something like content-type and encoding is
 * 	highly ironic, since without knowing those things, you couldn't parse
 * 	the file to get the value of the meta tag.”
 *	— https://stackoverflow.com/q/4696499
 *
 * From that alone it already follows that encodings given inside the object
 * itself are second-class.
 *
 * Two other considerations remain:
 *
 * 1. If a mail relay in the transport chain decides to recode a message (say,
 *    change it from ISO-8859-1 to ISO-8859-15), it should not modify the
 *    message content. (I claim that most MTAs do not even know HTML, nor
 *    should they.) Therefore, the new encoding must be conveyed external to
 *    the content, namely by means of the Content-Type field. => We must ignore
 *    the <meta> tag.
 *
 * 2. If decoding the MIME part with the Content-Type encoding produces an
 *    error (e.g. found a sequence that is undefined in this encoding), yet
 *    decoding the MIME part with the <meta> encoding succeeds, we still
 *    cannot be sure that the <meta> tag is the right one to use.
 *    => Could be transmission corruption or willful malignent mangling of
 *    the message.
 *
 * MIME hdr   META hdr   RFC says   MUAs do    Desired result
 * --------------------------------------------------------------
 * unspec     unspec     us-ascii   us-ascii   us-ascii
 * unspec     present    unspec     us-ascii   meta
 * present    unspec     mime       mime       mime
 * present    present    mime       mime       mime
 *
 * Ideally, the message should be stored raw, and the mail body never be
 * changed unless it is 100% certain that the transformation is unambiguously
 * reversible. Like, how mbox systems actually do it.
 * But with conversion to MAPI, we have this seemingly lossy conversion
 * stage. :-(
 */
HRESULT VMIMEToMAPI::handleHTMLTextpart(vmime::shared_ptr<vmime::header> vmHeader,
    vmime::shared_ptr<vmime::body> vmBody, IMessage *lpMessage, bool bAppendBody)
{
	HRESULT		hr				= hrSuccess;
	object_ptr<IStream> lpHTMLStream;
	ULONG		cbWritten		= 0;
	std::string strHTML;
	const char *lpszCharset = NULL;
	SPropValue sCodepage;
	LONG ulFlags;

	bool new_text = m_mailState.bodyLevel < BODY_HTML ||
                        (m_mailState.bodyLevel == BODY_HTML && bAppendBody);

	if (!new_text) {
		// already found html as body, so this is an attachment
		hr = handleAttachment(vmHeader, vmBody, lpMessage, L"secondary_html_body");
		if (hr != hrSuccess) {
			ec_log_err("Unable to parse attached text mail");
			return hr;
		}
		return hrSuccess;
	}

	// we're overriding a plain text body, setting a new HTML body or appending HTML data
	try {
		/* process Content-Transfer-Encoding */
		strHTML = content_transfer_decode(vmBody);
		vmime::charset mime_charset =
			get_mime_encoding(vmHeader, vmBody);

		/* Look for alternative in HTML */
		vmime::charset html_charset(im_charset_unspec);
		int html_analyze = getCharsetFromHTML(strHTML, &html_charset);
		if (html_analyze > 0 && html_charset != mime_charset &&
		    mime_charset != im_charset_unspec)
			/*
			 * This is not actually a problem, it can
			 * happen when an MTA transcodes it.
			 */
			ec_log_debug("MIME headers declare charset \"%s\", while HTML meta tag declares \"%s\".",
				mime_charset.getName().c_str(),
				html_charset.getName().c_str());

		if (mime_charset == im_charset_unspec &&
		    html_charset == im_charset_unspec) {
			if (m_mailState.mime_vtag_nest > 0) {
				ec_log_debug("No charset (case #3), defaulting to \"us-ascii\".");
				mime_charset = html_charset = vmime::charsets::US_ASCII;
			} else if (html_analyze < 0) {
				/*
				 * No HTML structure found when assuming ASCII,
				 * so we can just directly fallback to default_charset.
				 */
				ec_log_debug("No charset (case #4), defaulting to \"%s\".", m_dopt.ascii_upgrade);
				mime_charset = html_charset = m_dopt.ascii_upgrade;
			} else {
				/* HTML structure recognized when interpreting as ASCII. */
				ec_log_debug("No charset (case #6), defaulting to \"us-ascii\".");
				mime_charset = html_charset = vmime::charsets::US_ASCII;
			}
		} else if (mime_charset == im_charset_unspec) {
			/* only place to name cset is <meta> */
			ec_log_debug("Charset is \"%s\" (case #7).", html_charset.getName().c_str());
			mime_charset = html_charset;
		} else if (html_charset == im_charset_unspec) {
			/* only place to name cset is MIME header */
			ec_log_debug("Charset is \"%s\" (case #8).", mime_charset.getName().c_str());
			html_charset = mime_charset;
		}
		mime_charset = vtm_upgrade_charset(mime_charset, m_dopt.ascii_upgrade);
		html_charset = vtm_upgrade_charset(html_charset, m_dopt.ascii_upgrade);

		/* Add secondary candidates and try all in order */
		std::vector<std::string> cs_cand;
		cs_cand.emplace_back(mime_charset.getName());
		if (!m_dopt.charset_strict_rfc) {
			if (mime_charset != html_charset)
				cs_cand.emplace_back(html_charset.getName());
			cs_cand.emplace_back(vmime::charsets::US_ASCII);
		}
		int cs_best = renovate_encoding(strHTML, cs_cand);
		if (cs_best < 0) {
			ec_log_err("HTML part not readable in any charset. Storing as attachment instead.");
			return handleAttachment(vmHeader, vmBody, lpMessage, L"unknown_html_charset", true);
		}
		/*
		 * PR_HTML is a PT_BINARY, and can handle 0x00 bytes
		 * (e.g. in case of UTF-16 encoding).
		 */

		// write codepage for PR_HTML property
		if (HrGetCPByCharset(cs_cand[cs_best].c_str(), &sCodepage.Value.ul) != hrSuccess) {
			/* Win32 does not know the charset — change encoding to something it knows. */
			sCodepage.Value.ul = 65001;
			strHTML = m_converter.convert_to<std::string>("UTF-8", strHTML, rawsize(strHTML), cs_cand[cs_best].c_str());
			ec_log_info("No Win32 CPID for \"%s\" - upgrading text/html MIME body to UTF-8", cs_cand[cs_best].c_str());
		}

		if (bAppendBody && m_mailState.bodyLevel == BODY_HTML && m_mailState.ulLastCP && sCodepage.Value.ul != m_mailState.ulLastCP) {
			// we're appending but the new body part has a different codepage than the previous one. To support this
			// we have to upgrade the old data to UTF-8, convert the new data to UTF-8 and append that.

			if(m_mailState.ulLastCP != 65001) {
				hr = HrGetCharsetByCP(m_mailState.ulLastCP, &lpszCharset);
				if (hr != hrSuccess) {
					assert(false); // Should not happen since ulLastCP was generated by HrGetCPByCharset()
					return hr;
				}

				// Convert previous body part to UTF-8
				std::string strCurrentHTML;

				hr = Util::ReadProperty(lpMessage, PR_HTML, strCurrentHTML);
				if (hr != hrSuccess)
					return hr;
				strCurrentHTML = m_converter.convert_to<std::string>("UTF-8", strCurrentHTML, rawsize(strCurrentHTML), lpszCharset);

				hr = Util::WriteProperty(lpMessage, PR_HTML, strCurrentHTML);
				if (hr != hrSuccess)
					return hr;
			}

			if (sCodepage.Value.ul != 65001)
				// Convert new body part to UTF-8
				strHTML = m_converter.convert_to<std::string>("UTF-8", strHTML, rawsize(strHTML), mime_charset.getName().c_str());
			// Everything is UTF-8 now
			sCodepage.Value.ul = 65001;
			mime_charset = "utf-8";
		}

		m_mailState.ulLastCP = sCodepage.Value.ul;

		sCodepage.ulPropTag = PR_INTERNET_CPID;
		HrSetOneProp(lpMessage, &sCodepage);

		// we may have received a text part to append to the HTML body
		if (vmime::dynamicCast<vmime::mediaType>(vmHeader->ContentType()->getValue())->getSubType() ==
		    vmime::mediaTypes::TEXT_PLAIN) {
			// escape and wrap with <pre> tags
			std::wstring strwBody = m_converter.convert_to<std::wstring>(CHARSET_WCHAR "//IGNORE", strHTML, rawsize(strHTML), mime_charset.getName().c_str());
			strHTML = "<pre>";
			hr = Util::HrTextToHtml(strwBody.c_str(), strHTML, sCodepage.Value.ul);
			if (hr != hrSuccess)
				return hr;
			strHTML += "</pre>";
		}
	}
	catch (vmime::exception &e) {
		ec_log_err("VMIME exception on html body: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (std::exception &e) {
		ec_log_err("STD exception on html body: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (...) {
		ec_log_err("Unknown generic exception occurred on html body");
		return MAPI_E_CALL_FAILED;
	}

	// create new or reset body
	ulFlags = MAPI_MODIFY;
	if (m_mailState.bodyLevel == BODY_NONE || (m_mailState.bodyLevel < BODY_HTML && !bAppendBody))
		ulFlags |= MAPI_CREATE;
	hr = lpMessage->OpenProperty(PR_HTML, &IID_IStream, STGM_TRANSACTED, ulFlags, &~lpHTMLStream);
	if (hr != hrSuccess)
		return kc_perror("OpenProperty PR_HTML failed", hr);
	if (bAppendBody) {
		static const LARGE_INTEGER liZero = {{0, 0}};
		hr = lpHTMLStream->Seek(liZero, SEEK_END, NULL);
		if (hr != hrSuccess)
			return hr;
	}

	hr = lpHTMLStream->Write(strHTML.c_str(), strHTML.length(), &cbWritten);
	if (hr != hrSuccess)		// check cbWritten too?
		return hr;
	hr = lpHTMLStream->Commit(0);
	if (hr != hrSuccess)
		return hr;
	m_mailState.bodyLevel = BODY_HTML;
	if (bAppendBody)
		m_mailState.strHTMLBody.append(strHTML);
	else
		swap(strHTML, m_mailState.strHTMLBody);
	if (m_dopt.html_safety_filter)
		filter_html(lpMessage, lpHTMLStream, ulFlags, strHTML);
	return hrSuccess;
}

/**
 * Handle Attachments.. Now works for inlines and attachments...
 *
 * @param[in]	vmHeader	headers describing vmBody parameter
 * @param[in]	vmBody		body part
 * @param[out]	lpMessage	IMessage to be modified.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::handleAttachment(vmime::shared_ptr<vmime::header> vmHeader,
    vmime::shared_ptr<vmime::body> vmBody, IMessage *lpMessage,
    const wchar_t *sugg_filename, bool bAllowEmpty)
{
	HRESULT		hr			= hrSuccess;
	object_ptr<IStream> lpStream;
	object_ptr<IAttach> lpAtt;
	ULONG		ulAttNr		= 0;
	std::string	strId, strMimeType, strLocation, strTmp;
	std::wstring strLongFilename;
	int			nProps = 0;
	SPropValue	attProps[12];
	vmime::shared_ptr<vmime::contentDispositionField> cdf;	// parameters of Content-Disposition header
	vmime::shared_ptr<vmime::contentDisposition> cdv;		// value of Content-Disposition header
	vmime::shared_ptr<vmime::contentTypeField> ctf;
	vmime::shared_ptr<vmime::mediaType> mt;

	memset(attProps, 0, sizeof(attProps));

	// Create Attach
	hr = lpMessage->CreateAttach(nullptr, 0, &ulAttNr, &~lpAtt);
	if (hr != hrSuccess)
		goto exit;

	// open stream
	hr = lpAtt->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE|STGM_TRANSACTED,
	     MAPI_CREATE | MAPI_MODIFY, &~lpStream);
	if (hr != hrSuccess)
		goto exit;

	try {
		// attach adapter, generate in right encoding
		outputStreamMAPIAdapter osMAPI(lpStream);
		cdf = vmime::dynamicCast<vmime::contentDispositionField>(vmHeader->ContentDisposition());
		cdv = vmime::dynamicCast<vmime::contentDisposition>(cdf->getValue());
		ctf = vmime::dynamicCast<vmime::contentTypeField>(vmHeader->ContentType());
		mt = vmime::dynamicCast<vmime::mediaType>(ctf->getValue());

		try {
			vmBody->getContents()->generate(osMAPI, vmime::encoding(vmime::encodingTypes::BINARY));
		} catch (vmime::exceptions::no_encoder_available &) {
			/* RFC 2045 §6.4 page 17 */
			vmBody->getContents()->extractRaw(osMAPI);
			mt->setType(vmime::mediaTypes::APPLICATION);
			mt->setSubType(vmime::mediaTypes::APPLICATION_OCTET_STREAM);
		}

		if (!bAllowEmpty) {
			STATSTG stat;

			hr = lpStream->Stat(&stat, 0);
			if (hr != hrSuccess)
				goto exit;

			if (stat.cbSize.QuadPart == 0) {
				ec_log_err("Empty attachment found when not allowed, dropping empty attachment.");
				hr = MAPI_E_NOT_FOUND;
				goto exit;
			}
		}

		hr = lpStream->Commit(0);
		if (hr != hrSuccess)
			goto exit;
			
		// Free memory used by the stream
		lpStream.reset();

		// set info on attachment
		attProps[nProps].ulPropTag = PR_ATTACH_METHOD;
		attProps[nProps++].Value.ul = ATTACH_BY_VALUE;

		// vmHeader->ContentId() is headerField ->getValue() returns headerFieldValue, which messageId is.
		strId = vmime::dynamicCast<vmime::messageId>(vmHeader->ContentId()->getValue())->getId();
		if (!strId.empty()) {
			// only set this property when string is present
			// otherwise, you don't get the 'save attachments' list in the main menu of outlook
			attProps[nProps].ulPropTag = PR_ATTACH_CONTENT_ID_A;
			attProps[nProps++].Value.lpszA = (char*)strId.c_str();
		}

		try {
			strLocation = vmime::dynamicCast<vmime::text>(vmHeader->ContentLocation()->getValue())->getConvertedText(MAPI_CHARSET);
		}
		catch (vmime::exceptions::charset_conv_error) { }
		if (!strLocation.empty()) {
			attProps[nProps].ulPropTag = PR_ATTACH_CONTENT_LOCATION_A;
			attProps[nProps++].Value.lpszA = (char*)strLocation.c_str();
		}

		// make hidden when inline, is an image or text, has a content id or location, is an HTML mail,
		// has a CID reference in the HTML or has a location reference in the HTML.
		if (cdv->getName() == vmime::contentDispositionTypes::INLINE &&
			(mt->getType() == vmime::mediaTypes::IMAGE || mt->getType() == vmime::mediaTypes::TEXT) &&
			(!strId.empty() || !strLocation.empty()) &&
			m_mailState.bodyLevel == BODY_HTML &&
			((!strId.empty() && strcasestr(m_mailState.strHTMLBody.c_str(), string("cid:"+strId).c_str())) ||
			 (!strLocation.empty() && strcasestr(m_mailState.strHTMLBody.c_str(), strLocation.c_str())) ))
		{
			attProps[nProps].ulPropTag = PR_ATTACHMENT_HIDDEN;
			attProps[nProps++].Value.b = TRUE;

			attProps[nProps].ulPropTag = PR_ATTACH_FLAGS;
			attProps[nProps++].Value.ul = 4; // ATT_MHTML_REF

			attProps[nProps].ulPropTag = PR_ATTACHMENT_FLAGS;
			attProps[nProps++].Value.ul = 8; // unknown, for now

			if (m_mailState.attachLevel < ATTACH_NORMAL)
				m_mailState.attachLevel = ATTACH_INLINE;

		} else {
			attProps[nProps].ulPropTag = PR_ATTACHMENT_HIDDEN;
			attProps[nProps++].Value.b = FALSE;

			attProps[nProps].ulPropTag = PR_ATTACH_FLAGS;
			attProps[nProps++].Value.ul = 0;

			m_mailState.attachLevel = ATTACH_NORMAL;
		}

		// filenames
		if (cdf->hasParameter("filename"))
			strLongFilename = getWideFromVmimeText(vmime::text(cdf->getFilename()));
		else if (ctf->hasParameter("name"))
			strLongFilename = getWideFromVmimeText(vmime::text(ctf->getParameter("name")->getValue()));
		else {
			auto mime_type = mt->getType() + "/" + mt->getSubType();
			auto ext = mime_type_to_ext(mime_type.c_str(), "bin");
			strLongFilename = sugg_filename != nullptr ? sugg_filename : L"inline";
			strLongFilename += L".";
			strLongFilename += m_converter.convert_to<std::wstring>(ext);
		}

		attProps[nProps].ulPropTag = PR_ATTACH_LONG_FILENAME_W;
		attProps[nProps++].Value.lpszW = (WCHAR*)strLongFilename.c_str();

		// outlook internal rendering sequence in RTF bodies. When set
		// to -1, outlook will ignore it, when set to 0 or higher,
		// outlook (mapi) will regenerate the numbering
		attProps[nProps].ulPropTag = PR_RENDERING_POSITION;
		attProps[nProps++].Value.ul = 0;

		if (!mt->getType().empty() &&
			!mt->getSubType().empty()) {
			strMimeType = mt->getType() + "/" + mt->getSubType();
			// due to a bug in vmime 0.7, the continuation header text can be prefixed in the string, so strip it (easiest way to fix)
			while (strMimeType[0] == '\r' || strMimeType[0] == '\n' || strMimeType[0] == '\t' || strMimeType[0] == ' ')
				strMimeType.erase(0, 1);
			attProps[nProps].ulPropTag = PR_ATTACH_MIME_TAG_A;
			attProps[nProps++].Value.lpszA = (char*)strMimeType.c_str();
		}

		hr = lpAtt->SetProps(nProps, attProps, NULL);
		if (hr != hrSuccess)
			goto exit;
	}
	catch (vmime::exception& e) {
		ec_log_err("VMIME exception on attachment: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception& e) {
		ec_log_err("STD exception on attachment: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		ec_log_err("Unknown generic exception occurred on attachment");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = lpAtt->SaveChanges(0);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (hr != hrSuccess)
		ec_log_err("Unable to create attachment");
	return hr;
}

static const struct {
	const char *original;
	const char *update;
} vtm_cs_upgrade_list[] = {
	{"cp-850", "cp850"},
	{"gb2312", "gb18030"},			// gb18030 is an extended version of gb2312
	{"x-gbk", "gb18030"},			// gb18030 > gbk > gb2312. x-gbk is an alias of gbk, which is not listed in iconv.
	{"ks_c_5601-1987", "cp949"},	// cp949 is euc-kr with UHC extensions
	{"iso-8859-8-i", "iso-8859-8"},	// logical vs visual order, does not matter. http://mirror.hamakor.org.il/archives/linux-il/08-2004/11445.html
	{"win-1252", "windows-1252"},
	/*
	 * This particular "unicode" is different from iconv's
	 * "unicode" character set. It is UTF-8 content with a UTF-16
	 * BOM (which we can just drop because it carries no
	 * relevant information).
	 */
	{"unicode", "utf-8"}, /* UTF-16 BOM + UTF-8 content */
};

/**
 * Perform upgrades of the character set name, or the character set itself.
 *
 * 1. Some e-mails carry strange unregistered names ("unicode"), or simply
 * names which are registered with IANA but uncommon enough ("iso-8859-8-i")
 * that iconv does not know about them. This function returns a compatible
 * replacement usable with iconv.
 *
 * 2. The function performs compatible upgrades (such as gb2312→gb18030, both
 * of which are known to iconv) which repairs some mistagged email and does not
 * break properly-tagged mail.
 *
 * 3. The function also performs admin-configured compatible upgrades
 * (such as us-ascii→utf-8).
 */
static vmime::charset vtm_upgrade_charset(vmime::charset cset, const char *upg)
{
	if (upg != nullptr && cset == vmime::charsets::US_ASCII &&
	    cset != upg) {
		/*
		 * It is expected that the caller made sure that the
		 * replacement is in fact ASCII compatible.
		 */
		ec_log_debug("Admin forced charset upgrade \"%s\" -> \"%s\".",
			cset.getName().c_str(), upg);
		cset = upg;
	}
	for (size_t i = 0; i < ARRAY_SIZE(vtm_cs_upgrade_list); ++i)
		if (strcasecmp(vtm_cs_upgrade_list[i].original, cset.getName().c_str()) == 0)
			return vtm_cs_upgrade_list[i].update;
	return cset;
}

static htmlNodePtr find_node(htmlNodePtr lpNode, const char *name)
{
	htmlNodePtr node = NULL;

	for (node = lpNode; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;
		htmlNodePtr child = NULL;
		if (xmlStrcasecmp(node->name, reinterpret_cast<const xmlChar *>(name)) == 0)
			break;
		child = find_node(node->children, name);
		if (child)
			return child;
	}
	return node;
}

void ignoreError(void *ctx, const char *msg, ...)
{
}

/**
 * Determine character set from a possibly broken Content-Type value.
 * @in:		string in the form of m{^text/foo\s*(;?\s*key=value)*}
 *
 * Attempt to extract the character set parameter, e.g. from a HTML <meta> tag,
 * or from a Content-Type MIME header (though we do not use it for MIME headers
 * currently).
 */
static std::string fix_content_type_charset(const char *in)
{
	const char *cset = im_charset_unspec, *cset_end = im_charset_unspec;

	while (!isspace(*in) && *in != '\0')	/* skip type */
		++in;
	while (*in != '\0') {
		while (isspace(*in))
			++in; /* skip possible whitespace before ';' */
		if (*in == ';') {
			++in;
			while (isspace(*in))	/* skip WS after ';' */
				++in;
		}
		if (strncasecmp(in, "charset=", 8) == 0) {
			in += 8;
			cset = in;
			while (!isspace(*in) && *in != ';' && *in != '\0')
				++in;	/* skip value */
			cset_end = in;
			continue;
			/* continue parsing for more charset= values */
		}
		while (!isspace(*in) && *in != ';' && *in != '\0')
			++in;
	}
	return std::string(cset, cset_end - cset);
}

/**
 * Find alternate backup character set declaration
 *
 * @strHTML:		input MIME body part (HTML document)
 * @htmlCharset:	result from HTML <meta>
 *
 * In the MIME body, attempt to find the character set declaration in the
 * <meta> tag of the HTML document. This function requires that the HTML
 * skeleton is encoded in US-ASCII.
 *
 * If the MIME header specifies, for example, Content-Type: text/html;
 * charset=utf-16, then this function will not find anything -- and that is
 * correct, because if the MIME body is encoded in UTF-16, whatever else there
 * is in <meta> is, if it is not UTF-16, is likely wrong to begin with.
 *
 * Returns -1 if it does not appear to be HTML at all,
 * returns 0 if it looked like HTML/XML, but no character set was specified,
 * and returns 1 if a character set was declared.
 */
int VMIMEToMAPI::getCharsetFromHTML(const string &strHTML, vmime::charset *htmlCharset)
{
	int ret = 0;
	htmlDocPtr lpDoc = NULL;
	htmlNodePtr root = NULL, lpNode = NULL;
	xmlChar *lpValue = NULL;
	std::string charset;

	// really lazy html parsing and disable all error reporting
        xmlSetGenericErrorFunc(NULL, ignoreError); // disable stderr output (ZCP-13337)

	/*
	 * Parser will automatically lower-case element and attribute names.
	 * It appears to try decoding as UTF-16 as well.
	 */
	lpDoc = htmlReadMemory(strHTML.c_str(), strHTML.length(), "", NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOWARNING | HTML_PARSE_NOERROR);
	if (!lpDoc) {
		ec_log_warn("Unable to parse HTML document");
		return -1;
	}

	/*
	 * The HTML parser is very forgiving, so lpDoc is almost never %NULL
	 * (only if input buffer is size 0 apparently). But, if we have data
	 * in, for example, UTF-32 encoding, then @root will be NULL.
	 */
	root = xmlDocGetRootElement(lpDoc);
	if (root == NULL) {
		ec_log_warn("Unable to parse HTML document");
		ret = -1;
		goto exit;
	}
	lpNode = find_node(root, "head");
	if (!lpNode) {
		ec_log_debug("HTML document contains no HEAD tag");
		goto exit;
	}

	for (lpNode = lpNode->children; lpNode != NULL; lpNode = lpNode->next) {
		if (lpNode->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrcasecmp(lpNode->name,
		    reinterpret_cast<const xmlChar *>("meta")) != 0)
			continue;
		// HTML 4, <meta http-equiv="Content-Type" content="text/html; charset=...">
		lpValue = xmlGetProp(lpNode, (const xmlChar*)"http-equiv");
		if (lpValue && xmlStrcasecmp(lpValue, (const xmlChar*)"Content-Type") == 0) {
			xmlFree(lpValue);
			lpValue = xmlGetProp(lpNode, (const xmlChar*)"content");
			if (lpValue) {
				ec_log_debug("HTML4 meta tag found: charset=\"%s\"", lpValue);
				charset = fix_content_type_charset(reinterpret_cast<const char *>(lpValue));
			}
			break;
		}
		if (lpValue)
			xmlFree(lpValue);
		lpValue = NULL;

		// HTML 5, <meta charset="...">
		lpValue = xmlGetProp(lpNode, (const xmlChar*)"charset");
		if (lpValue) {
			ec_log_debug("HTML5 meta tag found: charset=\"%s\"", lpValue);
			charset = reinterpret_cast<char *>(lpValue);
			break;
		}
	}
	if (!lpValue) {
		ec_log_debug("HTML body does not contain meta charset information");
		goto exit;
	}
	*htmlCharset = charset.size() != 0 ? vtm_upgrade_charset(charset) :
	               vmime::charsets::US_ASCII;
	ec_log_debug("HTML charset adjusted to \"%s\"", htmlCharset->getName().c_str());
	ret = 1;

exit:
	if (lpValue)
		xmlFree(lpValue);
	if (lpDoc)
		xmlFreeDoc(lpDoc);
	return ret;
}

/** 
 * Convert a vmime::text object to wstring.  This function may force
 * another charset on the words in the text object for compatibility
 * reasons.
 * 
 * @param[in] vmText vmime text object containing encoded words (string + charset)
 * 
 * @return converted text in unicode
 */
std::wstring VMIMEToMAPI::getWideFromVmimeText(const vmime::text &vmText)
{
	std::string myword;
	std::wstring ret;

	const auto &words = vmText.getWordList();
	for (auto i = words.cbegin(); i != words.cend(); ++i) {
		/*
		 * RFC 5322 §2.2 specifies header field bodies consist of
		 * US-ASCII characters only, and the only way to get other
		 * encodings is by RFC 2047. In other words, the use of
		 * m_dopt.default_charset is disallowed.
		 */
		vmime::charset wordCharset = vtm_upgrade_charset((*i)->getCharset());

		/*
		 * In case of unknown character sets, RFC 2047 §6.2 ¶5
		 * gives the following options:
		 *
		 * (a) display input as-is, e.g. as =?utf-8?Q?VielSpa=C3=9F?=
		 *     if (!ValidateCharset(..))
		 *         ret += m_converter.convert_to<std::wstring>((*i)->generate());
		 * (b) best effort conversion (which we pick) or
		 * (c) substitute by a message that decoding failed.
		 *
		 * We pick (b), which means something ASCII-compatible.
		 * (a) is also a good choice, but the unreadable parts may be
		 * longer for not much benefit to the human reader.
		 */
		if (!ValidateCharset(wordCharset.getName().c_str()))
			wordCharset = m_dopt.ascii_upgrade;

		/*
		 * Concatenate words having the same charset, as the original
		 * input bytes may not have been safely split up. I cannot make
		 * out whether RFC 2047 §6.2 ¶6 actually discourages this
		 * concatenation, but permitting it gives the most pleasing
		 * result without violently disagreeing with the RFC. Hence,
		 * we also will not be adding if (m_dopt.charset_strict_rfc)
		 * here anytime soon.
		 */
		myword = (*i)->getBuffer();
		for (auto j = i + 1; j != words.cend() && (*j)->getCharset() == wordCharset; ++j, ++i)
			myword += (*j)->getBuffer();

		std::string tmp = vmime::word(myword, wordCharset).getConvertedText(CHARSET_WCHAR);
		ret.append(reinterpret_cast<const wchar_t *>(tmp.c_str()), tmp.size() / sizeof(wchar_t));
	}

	return ret;
}

/**
 * Do various fixups of missing or incorrect data.
 *
 * @param[in,out]	lpMessage	IMessage object to process
 * @return	MAPI error code.
 */
HRESULT VMIMEToMAPI::postWriteFixups(IMessage *lpMessage)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpMessageClass, lpProps, lpRecProps;
	ULONG cValues = 0;
	ULONG cRecProps = 0;
	ULONG cbConversationIndex = 0;
	memory_ptr<unsigned char> lpConversationIndex;

	PROPMAP_START(21)
		PROPMAP_NAMED_ID(RECURRENCESTATE,			PT_BINARY,	PSETID_Appointment, dispidRecurrenceState)

		PROPMAP_NAMED_ID(RESPONSESTATUS,			PT_LONG,	PSETID_Appointment, dispidResponseStatus)
		PROPMAP_NAMED_ID(RECURRING,					PT_BOOLEAN, PSETID_Appointment, dispidRecurring)
		PROPMAP_NAMED_ID(ATTENDEECRITICALCHANGE,	PT_SYSTIME, PSETID_Meeting, dispidAttendeeCriticalChange)
		PROPMAP_NAMED_ID(OWNERCRITICALCHANGE,		PT_SYSTIME, PSETID_Meeting, dispidOwnerCriticalChange)

		PROPMAP_NAMED_ID(MEETING_RECURRING,			PT_BOOLEAN,	PSETID_Meeting, dispidIsRecurring)
		PROPMAP_NAMED_ID(MEETING_STARTRECDATE,		PT_LONG,	PSETID_Meeting, dispidStartRecurrenceDate)
		PROPMAP_NAMED_ID(MEETING_STARTRECTIME,		PT_LONG,	PSETID_Meeting, dispidStartRecurrenceTime)
		PROPMAP_NAMED_ID(MEETING_ENDRECDATE,		PT_LONG,	PSETID_Meeting, dispidEndRecurrenceDate)
		PROPMAP_NAMED_ID(MEETING_ENDRECTIME,		PT_LONG,	PSETID_Meeting, dispidEndRecurrenceTime)

		PROPMAP_NAMED_ID(MEETING_DAYINTERVAL,		PT_I2,		PSETID_Meeting, dispidDayInterval)
		PROPMAP_NAMED_ID(MEETING_WEEKINTERVAL,		PT_I2,		PSETID_Meeting, dispidWeekInterval)
		PROPMAP_NAMED_ID(MEETING_MONTHINTERVAL,		PT_I2,		PSETID_Meeting, dispidMonthInterval)
		PROPMAP_NAMED_ID(MEETING_YEARINTERVAL,		PT_I2,		PSETID_Meeting, dispidYearInterval)

		PROPMAP_NAMED_ID(MEETING_DOWMASK,			PT_LONG,	PSETID_Meeting, dispidDayOfWeekMask)
		PROPMAP_NAMED_ID(MEETING_DOMMASK,			PT_LONG,	PSETID_Meeting, dispidDayOfMonthMask)
		PROPMAP_NAMED_ID(MEETING_MOYMASK,			PT_LONG,	PSETID_Meeting, dispidMonthOfYearMask)

		PROPMAP_NAMED_ID(MEETING_RECURRENCETYPE,	PT_I2,		PSETID_Meeting, dispidOldRecurrenceType)
		PROPMAP_NAMED_ID(MEETING_DOWSTART,			PT_I2,		PSETID_Meeting, dispidDayOfWeekStart)

		PROPMAP_NAMED_ID(CLIPSTART,					PT_SYSTIME,	PSETID_Appointment, dispidClipStart)
		PROPMAP_NAMED_ID(CLIPEND,					PT_SYSTIME,	PSETID_Appointment, dispidClipEnd)
	PROPMAP_INIT(lpMessage)

	hr = HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMessageClass);
	if (hr != hrSuccess)
		return hr;

	if (strncasecmp(lpMessageClass->Value.lpszA, "IPM.Schedule.Meeting.", strlen( "IPM.Schedule.Meeting." )) == 0)
	{
		// IPM.Schedule.Meeting.*

		SizedSPropTagArray(6, sptaMeetingReqProps) = {6, {PROP_RESPONSESTATUS, PROP_RECURRING, PROP_ATTENDEECRITICALCHANGE, PROP_OWNERCRITICALCHANGE, PR_OWNER_APPT_ID, PR_CONVERSATION_INDEX }};

		hr = lpMessage->GetProps(sptaMeetingReqProps, 0, &cValues, &~lpProps);
		if(FAILED(hr))
			return hr;

		// If hr is hrSuccess then all properties are available, and we don't need to do anything
		if(hr != hrSuccess) {
			hr = hrSuccess;

			if(lpProps[0].ulPropTag != PROP_RESPONSESTATUS) {
				lpProps[0].ulPropTag = PROP_RESPONSESTATUS;
				lpProps[0].Value.ul = 0;
			}

			if(lpProps[1].ulPropTag != PROP_RECURRING) {
				lpProps[1].ulPropTag = PROP_RECURRING;
				lpProps[1].Value.b = false;
			}

			if(lpProps[2].ulPropTag != PROP_ATTENDEECRITICALCHANGE) {
				lpProps[2].ulPropTag = PROP_ATTENDEECRITICALCHANGE;
				UnixTimeToFileTime(time(NULL), &lpProps[2].Value.ft);
			}

			if(lpProps[3].ulPropTag != PROP_OWNERCRITICALCHANGE) {
				lpProps[3].ulPropTag = PROP_OWNERCRITICALCHANGE;
				UnixTimeToFileTime(time(NULL), &lpProps[3].Value.ft);
			}

			if(lpProps[4].ulPropTag != PR_OWNER_APPT_ID) {
				lpProps[4].ulPropTag = PR_OWNER_APPT_ID;
				lpProps[4].Value.ul = -1;
			}

			if(lpProps[5].ulPropTag != PR_CONVERSATION_INDEX) {
				lpProps[5].ulPropTag = PR_CONVERSATION_INDEX;
				hr = ScCreateConversationIndex(0, NULL, &cbConversationIndex, &~lpConversationIndex);
				if(hr != hrSuccess)
					return hr;

				lpProps[5].Value.bin.cb = cbConversationIndex;
				lpProps[5].Value.bin.lpb = lpConversationIndex;
			}

			hr = lpMessage->SetProps(6, lpProps, NULL);
			if(hr != hrSuccess)
				return hr;
		}

		// @todo
		// this code should be in a separate function, which can easily
		// do 'goto exit', and we can continue here with other fixes.
		if(lpProps[1].Value.b)
		{
			// This is a recurring appointment. Generate the properties needed by CDO, which can be
			// found in the recurrence state. Since these properties are completely redundant we always
			// write them to correct any possible errors in the incoming message.
			SPropValue sMeetingProps[14];
			SizedSPropTagArray (3, sptaRecProps) =  { 3, { PROP_RECURRENCESTATE, PROP_CLIPSTART, PROP_CLIPEND } };
			RecurrenceState rec;

			// @todo, if all properties are not available: remove recurrence true marker
			hr = lpMessage->GetProps(sptaRecProps, 0, &cRecProps, &~lpRecProps);
			if(hr != hrSuccess) // Warnings not accepted
				return hr;
			
			hr = rec.ParseBlob(reinterpret_cast<const char *>(lpRecProps[0].Value.bin.lpb),
			     static_cast<unsigned int>(lpRecProps[0].Value.bin.cb), 0);
			if(FAILED(hr))
				return hr;
			
			// Ignore warnings	
			hr = hrSuccess;
			
			sMeetingProps[0].ulPropTag = PROP_MEETING_STARTRECDATE;
			sMeetingProps[0].Value.ul = FileTimeToIntDate(lpRecProps[1].Value.ft);
			
			sMeetingProps[1].ulPropTag = PROP_MEETING_STARTRECTIME;
			sMeetingProps[1].Value.ul = SecondsToIntTime(rec.ulStartTimeOffset * 60);

			if(rec.ulEndType != ET_NEVER) {
				sMeetingProps[2].ulPropTag = PROP_MEETING_ENDRECDATE;
				sMeetingProps[2].Value.ul = FileTimeToIntDate(lpRecProps[2].Value.ft);
			} else {
				sMeetingProps[2].ulPropTag = PR_NULL;
			}
			
			sMeetingProps[3].ulPropTag = PROP_MEETING_ENDRECTIME;
			sMeetingProps[3].Value.ul = SecondsToIntTime(rec.ulEndTimeOffset * 60);

			// Default the following values to 0 and set them later if needed
			sMeetingProps[4].ulPropTag = PROP_MEETING_DAYINTERVAL;
			sMeetingProps[4].Value.i = 0;
			sMeetingProps[5].ulPropTag = PROP_MEETING_WEEKINTERVAL;
			sMeetingProps[5].Value.i = 0;
			sMeetingProps[6].ulPropTag = PROP_MEETING_MONTHINTERVAL;
			sMeetingProps[6].Value.i = 0;
			sMeetingProps[7].ulPropTag = PROP_MEETING_YEARINTERVAL;
			sMeetingProps[7].Value.i = 0;
			
			sMeetingProps[8].ulPropTag = PROP_MEETING_DOWMASK;
			sMeetingProps[8].Value.ul = 0 ;

			sMeetingProps[9].ulPropTag = PROP_MEETING_DOMMASK;
			sMeetingProps[9].Value.ul = 0;
			
			sMeetingProps[10].ulPropTag = PROP_MEETING_MOYMASK;
			sMeetingProps[10].Value.ul = 0;
			
			sMeetingProps[11].ulPropTag = PROP_MEETING_RECURRENCETYPE;
			sMeetingProps[11].Value.ul = 0;
			
			sMeetingProps[12].ulPropTag = PROP_MEETING_DOWSTART;
			sMeetingProps[12].Value.i = rec.ulFirstDOW;
			
			sMeetingProps[13].ulPropTag = PROP_MEETING_RECURRING;
			sMeetingProps[13].Value.b = true;

			// Set the values depending on the type
			switch(rec.ulRecurFrequency) {
			case RF_DAILY:
				if (rec.ulPatternType == PT_DAY) {
					// Daily
					sMeetingProps[4].Value.i = rec.ulPeriod / 1440; // DayInterval
					sMeetingProps[11].Value.i = 64; // RecurrenceType
				} else {
					// Every workday, actually a weekly recurrence (weekly every workday)
					sMeetingProps[5].Value.i = 1; // WeekInterval
					sMeetingProps[8].Value.ul = 62; // Mo-Fri
					sMeetingProps[11].Value.i = 48; // Weekly
				}
				break;
			case RF_WEEKLY:
				sMeetingProps[5].Value.i = rec.ulPeriod; // WeekInterval
				sMeetingProps[8].Value.ul = rec.ulWeekDays; // DayOfWeekMask
				sMeetingProps[11].Value.i = 48; // RecurrenceType
				break;
			case RF_MONTHLY:
				sMeetingProps[6].Value.i = rec.ulPeriod; // MonthInterval
				if (rec.ulPatternType == PT_MONTH_NTH) { // Every Nth [weekday] of the month
					sMeetingProps[5].Value.ul = rec.ulWeekNumber; // WeekInterval
					sMeetingProps[8].Value.ul = rec.ulWeekDays; // DayOfWeekMask
					sMeetingProps[11].Value.i = 56; // RecurrenceType
				} else {
					sMeetingProps[9].Value.ul = 1 << (rec.ulDayOfMonth - 1); // day of month 1..31 mask
					sMeetingProps[11].Value.i = 12; // RecurrenceType
				}
				break;
			case RF_YEARLY:
				sMeetingProps[6].Value.i = rec.ulPeriod; // YearInterval
				sMeetingProps[7].Value.i = rec.ulPeriod / 12; // MonthInterval
				/*
				 * The following calculation is needed because the month of the year is encoded as minutes since
				 * the beginning of a (non-leap-year) year until the beginning of the month. We can therefore
				 * divide the minutes by the minimum number of minutes in one month (24*60*29) and round down
				 * (which is automatic since it is an int), giving us month 0-11.
				 *
				 * Put a different way, lets just ASSUME each month has 29 days. Let X be the minute-offset, and M the
				 * month (0-11), then M = X/(24*60*29). In real life though, some months have more than 29 days, so X will
				 * actually be larger. Due to rounding, this keeps working until we have more then 29 days of error. In a
				 * year, you will have a maximum of 17 ((31-29)+(29-29)+(31-29)+(30-29)...etc) days of error which is under
				 * so this formula always gives a correct value if 0 < M < 12.
				 */
				sMeetingProps[10].Value.ul = 1 << ((rec.ulFirstDateTime/(24*60*29)) % 12); // month of year (minutes since beginning of the year)

				if (rec.ulPatternType == PT_MONTH_NTH) { // Every Nth [weekday] in Month X
					sMeetingProps[5].Value.ul = rec.ulWeekNumber; // WeekInterval
					sMeetingProps[8].Value.ul = rec.ulWeekDays; // DayOfWeekMask
					sMeetingProps[11].Value.i = 51; // RecurrenceType
				} else {
					sMeetingProps[9].Value.ul = 1 << (rec.ulDayOfMonth - 1); // day of month 1..31 mask
					sMeetingProps[11].Value.i = 7; // RecurrenceType
				}
				break;
			default:
				break;
			}
			
			hr = lpMessage->SetProps(14, sMeetingProps, NULL);
			if(hr != hrSuccess)
				return hr;
		}
	}
 exitpm:
	return hr;
}

static std::string StringEscape(const char *input, const char *tokens,
    const char escape)
{
	std::string strEscaped;
	int i = 0;
	int t;

	while (true) {
		if (input[i] == 0)
			break;
		for (t = 0; tokens[t] != 0; ++t)
			if (input[i] == tokens[t])
				strEscaped += escape;
		strEscaped += input[i];
		++i;
	}
	return strEscaped;
}

/** 
 * Convert a vmime mailbox to an IMAP envelope list part
 * 
 * @param[in] mbox vmime mailbox (email address) to convert
 * 
 * @return string with IMAP envelope list part
 */
std::string VMIMEToMAPI::mailboxToEnvelope(vmime::shared_ptr<vmime::mailbox> mbox)
{
	vector<string> lMBox;
	string buffer;
	string::size_type pos;
	vmime::utility::outputStreamStringAdapter os(buffer);

	if (!mbox || mbox->isEmpty())
		throw vmime::exceptions::no_such_field();
	
	// (( "personal name" NIL "mailbox name" "domain name" ))

	mbox->getName().generate(os);
	// encoded names never contain "
	buffer = StringEscape(buffer.c_str(), "\"", '\\');
	lMBox.emplace_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	lMBox.emplace_back("NIL");	// at-domain-list (source route) ... whatever that means
	buffer = "\"" + mbox->getEmail().toString() + "\"";
	pos = buffer.find("@");
	if (pos != string::npos)
		buffer.replace(pos, 1, "\" \"");
	lMBox.emplace_back(std::move(buffer));
	if (pos == string::npos)
		lMBox.emplace_back("NIL");	// domain was missing
	return "(" + kc_join(lMBox, " ") + ")";
}

/** 
 * Convert a vmime addresslist (To/Cc/Bcc) to an IMAP envelope list part.
 * 
 * @param[in] aList vmime addresslist to convert
 * 
 * @return string with IMAP envelope list part
 */
std::string VMIMEToMAPI::addressListToEnvelope(vmime::shared_ptr<vmime::addressList> aList)
{
	list<string> lAddr;
	string buffer;
	int aCount = 0;

	if (!aList)
		throw vmime::exceptions::no_such_field();

	aCount = aList->getAddressCount();
	if (aCount == 0)
		throw vmime::exceptions::no_such_field();
		
	for (int i = 0; i < aCount; ++i) {
		try {
			buffer += mailboxToEnvelope(vmime::dynamicCast<vmime::mailbox>(aList->getAddressAt(i)));
			lAddr.emplace_back(buffer);
		} catch (vmime::exception &e) {
		}
	}
	if (lAddr.empty())
		return string("NIL");

	return "(" + buffer + ")";
}

/** 
 * Create the IMAP ENVELOPE property, so we don't need to open the
 * message to create this in the gateway.
 * 
 * Format:
 * ENVELOPE ("date" "subject" (from) (sender) (reply-to) ((to)*) ((cc)*) ((bcc)*) "in-reply-to" "message-id")
 *
 * If any of the fields aren't present in the received email, it should be substrituted by NIL.
 *
 * @param[in] vmMessage vmime message to create the envelope from
 * @param[in] lpMessage message to store the data in
 * 
 * @return MAPI Error code
 */
HRESULT VMIMEToMAPI::createIMAPEnvelope(vmime::shared_ptr<vmime::message> vmMessage,
    IMessage *lpMessage)
{
	HRESULT hr = hrSuccess;
	std::string buffer;
	SPropValue sEnvelope;

	PROPMAP_START(1)
	PROPMAP_NAMED_ID(ENVELOPE, PT_STRING8, PS_EC_IMAP, dispidIMAPEnvelope);
	PROPMAP_INIT(lpMessage);

	buffer = createIMAPEnvelope(vmMessage);

	sEnvelope.ulPropTag = PROP_ENVELOPE;
	sEnvelope.Value.lpszA = (char*)buffer.c_str();

	hr = lpMessage->SetProps(1, &sEnvelope, NULL);
 exitpm:
	return hr;
}

/** 
 * Create IMAP ENVELOPE() data from a vmime::message.
 * 
 * @param[in] vmMessage message to create envelope for
 * 
 * @return ENVELOPE data
 */
std::string VMIMEToMAPI::createIMAPEnvelope(vmime::shared_ptr<vmime::message> vmMessage)
{
	vector<string> lItems;
	auto vmHeader = vmMessage->getHeader();
	std::string buffer;
	vmime::utility::outputStreamStringAdapter os(buffer);
	vmime::generationContext ctx;
	ctx.setMaxLineLength(vmime::lineLengthLimits::infinite);
	ctx.setWrapMessageId(false);

	// date
	vmime::shared_ptr<vmime::datetime> date;
	if (vmHeader->hasField("Date")) {
		date = vmime::dynamicCast<vmime::datetime>(vmHeader->Date()->getValue());
	}
	else {
		// date must not be empty, so force epoch as the timestamp
		date = vmime::make_shared<vmime::datetime>(0);
	}
	date->generate(ctx, os);
	lItems.emplace_back("\"" + buffer + "\"");
	buffer.clear();
	vmHeader->Subject()->getValue()->generate(ctx, os);
	// encoded subjects never contain ", so escape won't break those.
	buffer = StringEscape(buffer.c_str(), "\"", '\\');
	lItems.emplace_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	buffer.clear();

	// from
	try {
		buffer = mailboxToEnvelope(vmime::dynamicCast<vmime::mailbox>(vmHeader->From()->getValue()));
		lItems.emplace_back("(" + buffer + ")");
	} catch (vmime::exception &e) {
		// this is not allowed, but better than nothing
		lItems.emplace_back("NIL");
	}
	buffer.clear();

	// sender
	try {
		buffer = mailboxToEnvelope(vmime::dynamicCast<vmime::mailbox>(vmHeader->Sender()->getValue()));
		lItems.emplace_back("(" + buffer + ")");
	} catch (vmime::exception &e) {
		lItems.emplace_back(lItems.back());
	}
	buffer.clear();

	// reply-to
	try {
		buffer = mailboxToEnvelope(vmime::dynamicCast<vmime::mailbox>(vmHeader->ReplyTo()->getValue()));
		lItems.emplace_back("(" + buffer + ")");
	} catch (vmime::exception &e) {
		lItems.emplace_back(lItems.back());
	}
	buffer.clear();

	// ((to),(to))
	try {
		buffer = addressListToEnvelope(vmime::dynamicCast<vmime::addressList>(vmHeader->To()->getValue()));
		lItems.emplace_back(buffer);
	} catch (vmime::exception &e) {
		lItems.emplace_back("NIL");
	}
	buffer.clear();

	// ((cc),(cc))
	try {
		auto aList = vmime::dynamicCast<vmime::addressList>(vmHeader->Cc()->getValue());
		int aCount = aList->getAddressCount();
		for (int i = 0; i < aCount; ++i)
			buffer += mailboxToEnvelope(vmime::dynamicCast<vmime::mailbox>(aList->getAddressAt(i)));
		lItems.emplace_back(buffer.empty() ? "NIL" : "(" + buffer + ")");
	} catch (vmime::exception &e) {
		lItems.emplace_back("NIL");
	}
	buffer.clear();

	// ((bcc),(bcc))
	try {
		auto aList = vmime::dynamicCast<vmime::addressList>(vmHeader->Bcc()->getValue());
		int aCount = aList->getAddressCount();
		for (int i = 0; i < aCount; ++i)
			buffer += mailboxToEnvelope(vmime::dynamicCast<vmime::mailbox>(aList->getAddressAt(i)));
		lItems.emplace_back(buffer.empty() ? "NIL" : "(" + buffer + ")");
	} catch (vmime::exception &e) {
		lItems.emplace_back("NIL");
	}
	buffer.clear();

	// in-reply-to
	vmHeader->InReplyTo()->getValue()->generate(ctx, os);
	lItems.emplace_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	buffer.clear();

	// message-id
	vmHeader->MessageId()->getValue()->generate(ctx, os);
	if (buffer.compare("<>") == 0)
		buffer.clear();
	lItems.emplace_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	return kc_join(lItems, " ");
}

/** 
 * Store the complete received email in a hidden property and the size
 * of that property too, for RFC822.SIZE requests.
 * 
 * @param[in] input the received email
 * @param[in] lpMessage message to store the data in
 * 
 * @return MAPI error code
 */
HRESULT VMIMEToMAPI::createIMAPBody(const string &input,
    vmime::shared_ptr<vmime::message> vmMessage, IMessage *lpMessage)
{
	SPropValue sProps[4];
	string strBody;
	string strBodyStructure;

	messagePartToStructure(input, vmMessage, &strBody, &strBodyStructure);

	sProps[0].ulPropTag = PR_EC_IMAP_EMAIL_SIZE;
	sProps[0].Value.ul = input.length();

	sProps[1].ulPropTag = PR_EC_IMAP_EMAIL;
	sProps[1].Value.bin.lpb = (BYTE*)input.c_str();
	sProps[1].Value.bin.cb = input.length();

	sProps[2].ulPropTag = PR_EC_IMAP_BODY;
	sProps[2].Value.lpszA = (char*)strBody.c_str();

	sProps[3].ulPropTag = PR_EC_IMAP_BODYSTRUCTURE;
	sProps[3].Value.lpszA = (char*)strBodyStructure.c_str();
	return lpMessage->SetProps(4, sProps, NULL);
}

/** 
 * Convert a vmime message to a 
 * 
 * @param[in] input The original email
 * @param[in] vmBodyPart Any message or body part to convert
 * @param[out] lpSimple BODY result
 * @param[out] lpExtended BODYSTRUCTURE result
 * 
 * @return always success
 */
HRESULT VMIMEToMAPI::messagePartToStructure(const string &input,
    vmime::shared_ptr<vmime::bodyPart> vmBodyPart, std::string *lpSimple,
    std::string *lpExtended)
{
	HRESULT hr = hrSuccess;
	list<string> lBody;
	list<string> lBodyStructure;
	auto vmHeaderPart = vmBodyPart->getHeader();

	try {
		vmime::shared_ptr<vmime::contentTypeField> ctf;
		if (vmHeaderPart->hasField(vmime::fields::CONTENT_TYPE))
			// use Content-Type header from part
			ctf = vmime::dynamicCast<vmime::contentTypeField>(vmHeaderPart->ContentType());
		else
			// create empty default Content-Type header
			ctf = vmime::dynamicCast<vmime::contentTypeField>(vmime::headerFieldFactory::getInstance()->create("Content-Type", ""));

		auto mt = vmime::dynamicCast<vmime::mediaType>(ctf->getValue());
		if (mt->getType() == vmime::mediaTypes::MULTIPART) {
			// handle multipart
			// alternative, mixed, related

			if (vmBodyPart->getBody()->getPartCount() == 0)
				return hr;		// multipart without any real parts? let's completely skip this.

			// function please:
			string strBody;
			string strBodyStructure;
			for (size_t i = 0; i < vmBodyPart->getBody()->getPartCount(); ++i) {
				messagePartToStructure(input, vmBodyPart->getBody()->getPartAt(i), &strBody, &strBodyStructure);
				lBody.emplace_back(std::move(strBody));
				lBodyStructure.emplace_back(std::move(strBodyStructure));
				strBody.clear();
				strBodyStructure.clear();
			}
			// concatenate without spaces, result: ((text)(html))
			strBody = kc_join(lBody, "");
			strBodyStructure = kc_join(lBodyStructure, "");

			lBody.clear();
			lBody.emplace_back(std::move(strBody));
			lBodyStructure.clear();
			lBodyStructure.emplace_back(std::move(strBodyStructure));

			// body:
			//   (<SUB> "subtype")
			// bodystructure:
			//   (<SUB> "subtype" ("boundary" "value") "disposition" "language")
			lBody.emplace_back("\"" + mt->getSubType() + "\"");
			lBodyStructure.emplace_back("\"" + mt->getSubType() + "\"");
			lBodyStructure.emplace_back(parameterizedFieldToStructure(ctf));
			lBodyStructure.emplace_back(getStructureExtendedFields(vmHeaderPart));
			if (lpSimple)
				*lpSimple = "(" + kc_join(lBody, " ") + ")";
			if (lpExtended)
				*lpExtended = "(" + kc_join(lBodyStructure, " ") + ")";
		} else {
			// just one part
			bodyPartToStructure(input, vmBodyPart, lpSimple, lpExtended);
		}
	}
	catch (vmime::exception &e) {
		ec_log_warn("Unable to create optimized bodystructure: %s", e.what());
	}

	// add () around results?

	return hr;
}

/** 
 * Convert a non-multipart body part to an IMAP BODY and BODYSTRUCTURE
 * string.
 * 
 * @param[in] input The original email
 * @param[in] vmBodyPart the bodyPart to convert
 * @param[out] lpSimple BODY result
 * @param[out] lpExtended BODYSTRUCTURE result
 * 
 * @return always success
 */
HRESULT VMIMEToMAPI::bodyPartToStructure(const string &input,
    vmime::shared_ptr<vmime::bodyPart> vmBodyPart, std::string *lpSimple,
    std::string *lpExtended)
{
	string strPart;
	list<string> lBody;
	list<string> lBodyStructure;
	string buffer;
	vmime::utility::outputStreamStringAdapter os(buffer);
	vmime::shared_ptr<vmime::contentTypeField> ctf;
	vmime::shared_ptr<vmime::mediaType> mt;

	auto vmHeaderPart = vmBodyPart->getHeader();
	if (!vmHeaderPart->hasField(vmime::fields::CONTENT_TYPE)) {
		// create with text/plain; charset=us-ascii ?
		lBody.emplace_back("NIL");
		lBodyStructure.emplace_back("NIL");
		goto nil;
	}
	ctf = vmime::dynamicCast<vmime::contentTypeField>(vmHeaderPart->findField(vmime::fields::CONTENT_TYPE));
	mt = vmime::dynamicCast<vmime::mediaType>(ctf->getValue());
	lBody.emplace_back("\"" + mt->getType() + "\"");
	lBody.emplace_back("\"" + mt->getSubType() + "\"");

	// if string == () force add charset.
	lBody.emplace_back(parameterizedFieldToStructure(ctf));
	if (vmHeaderPart->hasField(vmime::fields::CONTENT_ID)) {
		buffer = vmime::dynamicCast<vmime::messageId>(vmHeaderPart->findField(vmime::fields::CONTENT_ID)->getValue())->getId();
		lBody.emplace_back(buffer.empty() ? "NIL" : "\"<" + buffer + ">\"");
	} else {
		lBody.emplace_back("NIL");
	}

	if (vmHeaderPart->hasField(vmime::fields::CONTENT_DESCRIPTION)) {
		buffer.clear();
		vmHeaderPart->findField(vmime::fields::CONTENT_DESCRIPTION)->getValue()->generate(os);
		lBody.emplace_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	} else {
		lBody.emplace_back("NIL");
	}

	if (vmHeaderPart->hasField(vmime::fields::CONTENT_TRANSFER_ENCODING)) {
		buffer.clear();
		vmHeaderPart->findField(vmime::fields::CONTENT_TRANSFER_ENCODING)->getValue()->generate(os);
		lBody.emplace_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	} else {
		lBody.emplace_back("NIL");
	}

	if (mt->getType() == vmime::mediaTypes::TEXT) {
		// body part size
		buffer = stringify(vmBodyPart->getBody()->getParsedLength());
		lBody.emplace_back(buffer);

		// body part number of lines
		buffer = stringify(countBodyLines(input, vmBodyPart->getBody()->getParsedOffset(), vmBodyPart->getBody()->getParsedLength()));
		lBody.emplace_back(buffer);
	} else {
		// attachment: size only
		buffer = stringify(vmBodyPart->getBody()->getParsedLength());
		lBody.emplace_back(buffer);
	}

	// up until now, they were the same
	lBodyStructure = lBody;

	if (mt->getType() == vmime::mediaTypes::MESSAGE && mt->getSubType() == vmime::mediaTypes::MESSAGE_RFC822) {
		string strSubSingle;
		string strSubExtended;
		auto subMessage = vmime::make_shared<vmime::message>();

		// From RFC:
		// A body type of type MESSAGE and subtype RFC822 contains,
		// immediately after the basic fields, the envelope structure,
		// body structure, and size in text lines of the encapsulated
		// message.

		// envelope eerst, dan message, dan lines
		vmBodyPart->getBody()->getContents()->extractRaw(os); // generate? raw?
		subMessage->parse(buffer);
		lBody.emplace_back("(" + createIMAPEnvelope(subMessage) + ")");
		lBodyStructure.emplace_back("(" + createIMAPEnvelope(subMessage) + ")");

		// recurse message-in-message
		messagePartToStructure(buffer, subMessage, &strSubSingle, &strSubExtended);
		lBody.emplace_back(std::move(strSubSingle));
		lBodyStructure.emplace_back(std::move(strSubExtended));

		// dus hier nog de line count van vmBodyPart->getBody buffer?
		lBody.emplace_back(stringify(countBodyLines(buffer, 0, buffer.length())));
	}

nil:
	if (lpSimple)
		*lpSimple = "(" + kc_join(lBody, " ") + ")";

	/* just push some NILs or also inbetween? */
	lBodyStructure.emplace_back("NIL"); // MD5 of body (use Content-MD5 header?)
	lBodyStructure.emplace_back(getStructureExtendedFields(vmHeaderPart));
	if (lpExtended)
		*lpExtended = "(" + kc_join(lBodyStructure, " ") + ")";

	return hrSuccess;
}

/** 
 * Return an IMAP list part containing the extended properties for a
 * BODYSTRUCTURE.
 * Adds disposition list, language and location. 
 *
 * @param[in] vmHeaderPart The header to get the values from
 * 
 * @return IMAP list part
 */
std::string VMIMEToMAPI::getStructureExtendedFields(vmime::shared_ptr<vmime::header> vmHeaderPart)
{
	list<string> lItems;
	string buffer;
	vmime::utility::outputStreamStringAdapter os(buffer);

	// content-disposition header
	if (vmHeaderPart->hasField(vmime::fields::CONTENT_DISPOSITION)) {
		// use findField because we want an exception when missing
		auto cdf = vmime::dynamicCast<vmime::contentDispositionField>(vmHeaderPart->findField(vmime::fields::CONTENT_DISPOSITION));
		auto cd = vmime::dynamicCast<vmime::contentDisposition>(cdf->getValue());
		lItems.emplace_back("(\"" + cd->getName() + "\" " + parameterizedFieldToStructure(cdf) + ")");
	} else {
		lItems.emplace_back("NIL");
	}

	// language
	lItems.emplace_back("NIL");

	// location
	try {
		buffer.clear();
		vmHeaderPart->ContentLocation()->getValue()->generate(os);
		lItems.emplace_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	}
	catch (vmime::exception &e) {
		lItems.emplace_back("NIL");
	}
	return kc_join(lItems, " ");
}

/** 
 * Return an IMAP list containing the parameters of a specified header field as ("name" "value")
 * 
 * @param[in] vmParamField The paramiterized header field to "convert"
 * 
 * @return IMAP list
 */
std::string VMIMEToMAPI::parameterizedFieldToStructure(vmime::shared_ptr<vmime::parameterizedHeaderField> vmParamField)
{
	list<string> lParams;
	string buffer;
	vmime::utility::outputStreamStringAdapter os(buffer);

	try {
		for (const auto &param : vmParamField->getParameterList()) {
			lParams.emplace_back("\"" + param->getName() + "\"");
			param->getValue().generate(os);
			lParams.emplace_back("\"" + buffer + "\"");
			buffer.clear();
		}
	}
	catch (vmime::exception &e) {
		return "NIL";
	}
	if (lParams.empty())
		return "NIL";
	return "(" + kc_join(lParams, " ") + ")";
}

/** 
 * Return the number of lines in a string, with defined start and
 * length.
 * 
 * @param[in] input count number of \n chars in this string
 * @param[in] start start from this point in input
 * @param[in] length until the end, but no further than this length
 * 
 * @return number of lines
 */
std::string::size_type VMIMEToMAPI::countBodyLines(const std::string &input, std::string::size_type start, std::string::size_type length)
{
	string::size_type lines = 0;
	string::size_type pos = start;

	while (true) {
		pos = input.find_first_of('\n', pos);
		if (pos == string::npos || pos > start+length)
			break;
		++pos;
		++lines;
	} 

	return lines;
}

// options.h code
/**
 * Set all members in the delivery_options struct to their defaults
 * (DAgent, not Gateway).
 *
 * @param[out]	dopt	struct filled with default values
 */
void imopt_default_delivery_options(delivery_options *dopt) {
	dopt->use_received_date = true;
	dopt->mark_as_read = false;
	dopt->add_imap_data = false;
	dopt->charset_strict_rfc = true;
	dopt->user_entryid = NULL;
	dopt->parse_smime_signed = false;
	dopt->ascii_upgrade = nullptr;
	dopt->html_safety_filter = false;
}

/**
 * Set all members in the sending_options struct to their defaults
 * (Spooler, not Gateway).
 *
 * @param[out]	sopt	struct filled with default values
 */
void imopt_default_sending_options(sending_options *sopt) {
	sopt->alternate_boundary = NULL;
	sopt->no_recipients_workaround = false;
	sopt->msg_in_msg = false;
	sopt->headers_only = false;
	sopt->add_received_date = false;
	sopt->use_tnef = 0;
	sopt->force_utf8 = false;
	sopt->charset_upgrade = const_cast<char *>("windows-1252");
	sopt->allow_send_to_everyone = true;
	sopt->enable_dsn = true;
	sopt->always_expand_distr_list = false;
	sopt->ignore_missing_attachments = false;
}

} /* namespace */
