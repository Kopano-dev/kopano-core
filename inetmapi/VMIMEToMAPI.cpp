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

#include "VMIMEToMAPI.h"
#include <kopano/ECGuid.h>

#include <algorithm>
#include <string>
#include <fstream>
#include <iostream>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
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
#include "outputStreamMAPIAdapter.h"

// vcal support
#include "ICalToMAPI.h"

using namespace std;

static vmime::charset vtm_upgrade_charset(const vmime::charset &);

static const char im_charset_unspec[] = "unspecified";

/**
 * Default empty constructor for the inetmapi library. Sets all member
 * values to sane defaults.
 */
VMIMEToMAPI::VMIMEToMAPI()
{
	lpLogger = new ECLogger_Null();
	imopt_default_delivery_options(&m_dopt);
	m_dopt.use_received_date = false; // use Date header
	m_lpAdrBook = NULL;
	m_lpDefaultDir = NULL;
}

/**
 * Adds user set addressbook (to minimize opens on this object) and delivery options.
 * 
 * @param[in]	lpAdrBook	Addressbook of a user.
 * @param[in]	newlogger	ECLogger object for convert error messages.
 * @param[in]	dopt		delivery options handle differences in DAgent and Gateway behaviour.
 */
VMIMEToMAPI::VMIMEToMAPI(LPADRBOOK lpAdrBook, ECLogger *newlogger, delivery_options dopt)
{
	lpLogger = newlogger;
	m_dopt = dopt;
	if (!lpLogger)
		lpLogger = new ECLogger_Null();
	else
		lpLogger->AddRef();

	m_lpAdrBook = lpAdrBook;
	m_lpDefaultDir = NULL;
}

VMIMEToMAPI::~VMIMEToMAPI()
{
	lpLogger->Release();
		
	if (m_lpDefaultDir)
		m_lpDefaultDir->Release();
}

/** 
 * Parse a RFC-822 email, and return the IMAP BODY and BODYSTRUCTURE
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
	vmime::ref<vmime::message> vmMessage = vmime::create<vmime::message>();
	vmMessage->parse(input);

	if (lpBody || lpBodyStructure)
		messagePartToStructure(input, vmMessage, lpBody, lpBodyStructure);

	if (lpEnvelope)
		*lpEnvelope = createIMAPEnvelope(vmMessage);

	return hrSuccess;
}

/** 
 * Entry point for the conversion from RFC822 mail to IMessage MAPI object.
 *
 * Finds the first block of headers to place in the
 * PR_TRANSPORT_MESSAGE_HEADERS property. Then it lets libvmime parse
 * the email and starts the main conversion function
 * fillMAPIMail. Afterwards it may handle signed messages, and set an
 * extra flag when all attachments were marked hidden.
 *
 * @param[in]	input	std::string containing the RFC822 mail.
 * @param[out]	lpMessage	Pointer to a message which was already created on a IMAPIFolder.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::convertVMIMEToMAPI(const string &input, IMessage *lpMessage) {
	HRESULT hr = hrSuccess;
	// signature variables
	ULONG ulAttNr;
	LPATTACH lpAtt = NULL;
	LPSTREAM lpStream = NULL;
	ULONG nProps = 0;
	SPropValue attProps[3];
	SPropValue sPropSMIMEClass;
	IMAPITable *lpAttachTable = NULL;
	LPSRowSet lpAttachRows = NULL;
	size_t posHeaderEnd;
	bool bUnix = false;

	try {
		if (m_mailState.ulMsgInMsg == 0)
			m_mailState.reset();

		// get raw headers
		posHeaderEnd = input.find("\r\n\r\n");
		if (posHeaderEnd == std::string::npos) {
			// input was not rfc compliant, try unix enters
			posHeaderEnd = input.find("\n\n");
			bUnix = true;
		}
		if (posHeaderEnd != std::string::npos) {
			SPropValue sPropHeaders;
			std::string strHeaders = input.substr(0, posHeaderEnd);

			// make sure we have us-ascii headers
			if (bUnix)
				StringLFtoCRLF(strHeaders);

			sPropHeaders.ulPropTag = PR_TRANSPORT_MESSAGE_HEADERS_A;
			sPropHeaders.Value.lpszA = (char *) strHeaders.c_str();

			HrSetOneProp(lpMessage, &sPropHeaders);
		}

		// turn buffer into a message
		vmime::ref<vmime::message> vmMessage = vmime::create<vmime::message>();
		vmMessage->parse(input);

		// save imap data first, seems vmMessage may be altered in the rest of the code.
		if (m_dopt.add_imap_data)
			createIMAPBody(input, vmMessage, lpMessage);

		hr = fillMAPIMail(vmMessage, lpMessage);
		if (hr != hrSuccess)
			goto exit;

		if (m_mailState.bAttachSignature && !m_dopt.parse_smime_signed) {
			SizedSPropTagArray (2, sptaAttach) = { 2, { PR_ATTACH_NUM, PR_ATTACHMENT_HIDDEN } };
			// Remove the parsed attachments since the client should be reading them from the 
			// signed rfc822 data we are about to add.
			
			hr = lpMessage->GetAttachmentTable(0, &lpAttachTable);
			if(hr != hrSuccess)
				goto exit;
				
			hr = HrQueryAllRows(lpAttachTable, (LPSPropTagArray)&sptaAttach, NULL, NULL, -1, &lpAttachRows);
			if(hr != hrSuccess)
				goto exit;
				
			for (unsigned int i = 0; i < lpAttachRows->cRows; ++i) {
				hr = lpMessage->DeleteAttach(lpAttachRows->aRow[i].lpProps[0].Value.ul, 0, NULL, 0);
				if(hr != hrSuccess)
					goto exit;
			}
			
			// Include the entire rfc822 data in an attachment for the client to check
			vmime::ref<vmime::header> vmHeader = vmMessage->getHeader();

			hr = lpMessage->CreateAttach(NULL, 0, &ulAttNr, &lpAtt);
			if (hr != hrSuccess)
				goto exit;

			// open stream
			hr = lpAtt->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE|STGM_TRANSACTED,
									 MAPI_CREATE|MAPI_MODIFY, (LPUNKNOWN *)&lpStream);
			if (hr != hrSuccess)
				goto exit;

			outputStreamMAPIAdapter os(lpStream);
			// get the content-type string from the headers
			vmHeader->ContentType()->generate(os);

			// find the original received body
			// vmime re-generates different headers and spacings, so we can't use this.
			if (posHeaderEnd != string::npos) {
				os.write(input.c_str() + posHeaderEnd, input.size() - posHeaderEnd);
			}

			hr = lpStream->Commit(0);
			if (hr != hrSuccess)
				goto exit;

			attProps[nProps].ulPropTag = PR_ATTACH_METHOD;
			attProps[nProps++].Value.ul = ATTACH_BY_VALUE;

			attProps[nProps].ulPropTag = PR_ATTACH_MIME_TAG_W;
			attProps[nProps++].Value.lpszW = const_cast<wchar_t *>(L"multipart/signed");
			attProps[nProps].ulPropTag = PR_RENDERING_POSITION;
			attProps[nProps++].Value.ul = -1;

			hr = lpAtt->SetProps(nProps, attProps, NULL);
			if (hr != hrSuccess)
				goto exit;

			hr = lpAtt->SaveChanges(0);
			if (hr != hrSuccess)
				goto exit;
				
			lpAtt->Release();
			lpAtt = NULL;

			// saved, so mark the message so outlook knows how to find the encoded message
			sPropSMIMEClass.ulPropTag = PR_MESSAGE_CLASS_W;
			sPropSMIMEClass.Value.lpszW = const_cast<wchar_t *>(L"IPM.Note.SMIME.MultipartSigned");

			hr = lpMessage->SetProps(1, &sPropSMIMEClass, NULL);
			if (hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to set message class");
				goto exit;
			}
		}

		if ((m_mailState.attachLevel == ATTACH_INLINE && m_mailState.bodyLevel == BODY_HTML) || (m_mailState.bAttachSignature && m_mailState.attachLevel <= ATTACH_INLINE)) {
			/* Hide the attachment flag if:
			 * - We have an HTML body and there are only INLINE attachments (don't need to hide no attachments)
			 * - We have a signed message and there are only INLINE attachments or no attachments at all (except for the signed message)
			 */
			MAPINAMEID sNameID;
			LPMAPINAMEID lpNameID = &sNameID;
			LPSPropTagArray lpPropTag = NULL;

			sNameID.lpguid = (GUID*)&PSETID_Common;
			sNameID.ulKind = MNID_ID;
			sNameID.Kind.lID = dispidSmartNoAttach;

			hr = lpMessage->GetIDsFromNames(1, &lpNameID, MAPI_CREATE, &lpPropTag);
			if (hr != hrSuccess) {
				hr = hrSuccess;
				goto exit;
			}

			attProps[0].ulPropTag = CHANGE_PROP_TYPE(lpPropTag->aulPropTag[0], PT_BOOLEAN);
			attProps[0].Value.b = TRUE;

			MAPIFreeBuffer(lpPropTag);

			hr = lpMessage->SetProps(1, attProps, NULL);
			if (hr != hrSuccess) {
				hr = hrSuccess;
				goto exit;
			}
		}
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception occurred");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

exit:
	if (lpAttachRows)
		FreeProws(lpAttachRows);
		
	if (lpAttachTable)
		lpAttachTable->Release();
		
	if (lpAtt)
		lpAtt->Release();

	if (lpStream)
		lpStream->Release();

	return hr;
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
HRESULT VMIMEToMAPI::fillMAPIMail(vmime::ref<vmime::message> vmMessage, IMessage *lpMessage) {
	HRESULT	hr;
	SPropValue sPropDefaults[3];

	sPropDefaults[0].ulPropTag = PR_MESSAGE_CLASS_W;
	sPropDefaults[0].Value.lpszW = const_cast<wchar_t *>(L"IPM.Note");
	sPropDefaults[1].ulPropTag = PR_MESSAGE_FLAGS;
	sPropDefaults[1].Value.ul = (m_dopt.mark_as_read ? MSGFLAG_READ : 0) | MSGFLAG_UNMODIFIED;

	// Default codepage is UTF-8, might be overwritten when writing
	// the body (plain or html). So this is only in effect when an
	// e-mail does not specify its charset.  We use UTF-8 since it is
	// compatible with us-ascii, and the conversion from plain-text
	// only to HTML by the client will use this codepage. This makes
	// sure the generated HTML version of plaintext only mails
	// contains all characters.
	sPropDefaults[2].ulPropTag = PR_INTERNET_CPID;
	sPropDefaults[2].Value.ul = 65001;

	hr = lpMessage->SetProps(3, sPropDefaults, NULL);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to set default mail properties");
		return hr;
	}

	try {
		// turn buffer into a message

		// get the part header and find out what it is...
		vmime::ref<vmime::header> vmHeader = vmMessage->getHeader();
		vmime::ref<vmime::body> vmBody = vmMessage->getBody();
		vmime::ref<vmime::mediaType> mt = vmHeader->ContentType()->getValue().dynamicCast<vmime::mediaType>();

		// pass recipients somewhere else 
		hr = handleRecipients(vmHeader, lpMessage);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to parse mail recipients");
			return hr;
		}

		// Headers
		hr = handleHeaders(vmHeader, lpMessage);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to parse mail headers");
			return hr;
		}

		if (vmime::mdn::MDNHelper::isMDN(vmMessage) == true)
		{
			vmime::mdn::receivedMDNInfos receivedMDN = vmime::mdn::MDNHelper::getReceivedMDN(vmMessage);

			vmime::ref<vmime::body> myBody = vmMessage->getBody();
			// it is possible to get 3 bodyparts.
			// text/plain, message/disposition-notification, text/rfc822-headers
			// the third part seems optional. and some clients send multipart/alternative instead of text/plain.
			// Loop to get text/plain body or multipart/alternative.
			for (int i = 0; i < myBody->getPartCount(); ++i) {
				vmime::ref<vmime::bodyPart> bPart = myBody->getPartAt(i);
				vmime::ref<vmime::headerField> ctf = bPart->getHeader()->findField(vmime::fields::CONTENT_TYPE);

				if( (ctf->getValue().dynamicCast <vmime::mediaType>()->getType() == vmime::mediaTypes::TEXT &&
				     ctf->getValue().dynamicCast <vmime::mediaType>()->getSubType() == vmime::mediaTypes::TEXT_PLAIN)
				||  (ctf->getValue().dynamicCast <vmime::mediaType>()->getType() == vmime::mediaTypes::MULTIPART &&
				     ctf->getValue().dynamicCast <vmime::mediaType>()->getSubType() == vmime::mediaTypes::MULTIPART_ALTERNATIVE) )
				{
					hr = dissect_body(bPart->getHeader(), bPart->getBody(), lpMessage);
					if (hr != hrSuccess) {
						lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to parse MDN mail body");
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
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to set MDN mail properties");
				return hr;
			}
		} else {
			// multiparts are handled in disectBody, if any
			hr = dissect_body(vmHeader, vmBody, lpMessage);
			if (hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to parse mail body");
				return hr;
			}
		}
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception on create message: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on create message: %s", e.what());
		return MAPI_E_CALL_FAILED;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception occurred on create message");
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
HRESULT VMIMEToMAPI::handleHeaders(vmime::ref<vmime::header> vmHeader, IMessage* lpMessage) {
	HRESULT			hr = hrSuccess;
	std::string		strInternetMessageId, strInReplyTos, strReferences;
	std::wstring	wstrSubject;
	std::wstring	wstrReplyTo, wstrReplyToMail;
	std::string		strClientTime;
	std::wstring	wstrFromName, wstrSenderName;
	std::string		strFromEmail, strFromSearchKey;
	std::string		strSenderEmail,	strSenderSearchKey;
	ULONG			cbFromEntryID; // representing entry id
	LPENTRYID		lpFromEntryID = NULL;
	ULONG			cbSenderEntryID;
	LPENTRYID		lpSenderEntryID = NULL;
	LPSPropValue	lpPropNormalizedSubject = NULL;
	SPropValue		sConTopic;
	// setprops
	FLATENTRY		*lpEntry = NULL;
	FLATENTRYLIST	*lpEntryList = NULL;
	ULONG			cb = 0;
	int				nProps = 0;
	SPropValue		msgProps[22];
	// temp
	ULONG			cbEntryID;
	LPENTRYID		lpEntryID = NULL;

	LPSPropValue	lpRecipProps = NULL;
	ULONG			ulRecipProps;

	// order and types are important for modifyFromAddressBook()
	SizedSPropTagArray(7, sptaRecipPropsSentRepr) = { 7, {
		PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_NAME_W,
		PR_NULL /* PR_xxx_DISPLAY_TYPE not available */,
		PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, PR_SENT_REPRESENTING_ENTRYID,
		PR_SENT_REPRESENTING_SEARCH_KEY, PR_NULL /* PR_xxx_SMTP_ADDRESS not available */
	} };
	SizedSPropTagArray(7, sptaRecipPropsSender) = { 7, {
		PR_SENDER_ADDRTYPE_W, PR_SENDER_NAME_W,
		PR_NULL /* PR_xxx_DISPLAY_TYPE not available */,
		PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID,
		PR_SENDER_SEARCH_KEY, PR_NULL /* PR_xxx_SMTP_ADDRESS not available */
	} };

	try { 
		// internet message ID
		if(vmHeader->hasField(vmime::fields::MESSAGE_ID)) {
			strInternetMessageId = vmHeader->MessageId()->getValue()->generate();
			msgProps[nProps].ulPropTag = PR_INTERNET_MESSAGE_ID_A;
			msgProps[nProps++].Value.lpszA = (char*)strInternetMessageId.c_str();
		}

		// In-Reply-To header
		if(vmHeader->hasField(vmime::fields::IN_REPLY_TO)) {
			strInReplyTos = vmHeader->InReplyTo()->getValue()->generate();
			msgProps[nProps].ulPropTag = PR_IN_REPLY_TO_ID_A;
			msgProps[nProps++].Value.lpszA = (char*)strInReplyTos.c_str();
		}

		// References header
		if(vmHeader->hasField(vmime::fields::REFERENCES)) {
			strReferences = vmHeader->References()->getValue()->generate();
			msgProps[nProps].ulPropTag = PR_INTERNET_REFERENCES_A;
			msgProps[nProps++].Value.lpszA = (char*)strReferences.c_str();
		}

		// set subject
		if(vmHeader->hasField(vmime::fields::SUBJECT)) {
			wstrSubject = getWideFromVmimeText(*vmHeader->Subject()->getValue().dynamicCast<vmime::text>());
			msgProps[nProps].ulPropTag = PR_SUBJECT_W;
			msgProps[nProps++].Value.lpszW = (WCHAR *)wstrSubject.c_str();
		}

		// set ReplyTo
		if (!vmHeader->ReplyTo()->getValue().dynamicCast<vmime::mailbox>()->isEmpty()) {
			// First, set PR_REPLY_RECIPIENT_NAMES
			wstrReplyTo = getWideFromVmimeText(vmHeader->ReplyTo()->getValue().dynamicCast<vmime::mailbox>()->getName());
			wstrReplyToMail = m_converter.convert_to<wstring>(vmHeader->ReplyTo()->getValue().dynamicCast<vmime::mailbox>()->getEmail());
			if (wstrReplyTo.empty())
				wstrReplyTo = wstrReplyToMail;

			msgProps[nProps].ulPropTag = PR_REPLY_RECIPIENT_NAMES_W;
			msgProps[nProps++].Value.lpszW = (WCHAR *)wstrReplyTo.c_str();

			// Now, set PR_REPLY_RECIPIENT_ENTRIES (a FLATENTRYLIST)
			hr = ECCreateOneOff((LPTSTR)wstrReplyTo.c_str(), (LPTSTR)L"SMTP", (LPTSTR)wstrReplyToMail.c_str(), MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbEntryID, &lpEntryID);
			if (hr != hrSuccess)
				goto exit;

			cb = CbNewFLATENTRY(cbEntryID);
			hr = MAPIAllocateBuffer(cb, (void**)&lpEntry);
			if (hr != hrSuccess)
				goto exit;

			memcpy(lpEntry->abEntry, lpEntryID, cbEntryID);
			lpEntry->cb = cbEntryID;

			cb = CbNewFLATENTRYLIST(cb);
			hr = MAPIAllocateBuffer(cb, (void **)&lpEntryList);
			if (hr != hrSuccess)
				goto exit;

			lpEntryList->cEntries = 1;
			lpEntryList->cbEntries = CbFLATENTRY(lpEntry);
			memcpy(&lpEntryList->abEntries, lpEntry, CbFLATENTRY(lpEntry));

			msgProps[nProps].ulPropTag = PR_REPLY_RECIPIENT_ENTRIES;
			msgProps[nProps].Value.bin.cb = CbFLATENTRYLIST(lpEntryList);
			msgProps[nProps++].Value.bin.lpb = (BYTE*)lpEntryList;

			MAPIFreeBuffer(lpEntryID);
			lpEntryID = NULL;
		}

		// setting sent time
		if(vmHeader->hasField(vmime::fields::DATE)) {
			msgProps[nProps].ulPropTag = PR_CLIENT_SUBMIT_TIME;
			msgProps[nProps++].Value.ft = vmimeDatetimeToFiletime(*vmHeader->Date()->getValue().dynamicCast<vmime::datetime>());

			// set sent date (actual send date, disregarding timezone)
			vmime::datetime d = *vmHeader->Date()->getValue().dynamicCast<vmime::datetime>();
			d.setTime(0,0,0,0);
			msgProps[nProps].ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			msgProps[nProps++].Value.ft = vmimeDatetimeToFiletime(d);
		}

		// setting receive date (now)
		// parse from Received header, if possible
		vmime::datetime date = vmime::datetime::now();
		bool found_date = false;
		if (m_dopt.use_received_date || m_mailState.ulMsgInMsg) {
			try {
				vmime::ref<vmime::relay> recv = vmHeader->findField("Received")->getValue().dynamicCast<vmime::relay>();
				if (recv) {
					date = recv->getDate();
					found_date = true;
				}
			}
			catch (...) {
				if (m_mailState.ulMsgInMsg) {
					date = *vmHeader->Date()->getValue().dynamicCast<vmime::datetime>();
					found_date = true;
				} else {
					date = vmime::datetime::now();
				}
			}
		}

		// When parse_smime_signed = True, we don't want to change the delivery date, since otherwise
		// clients which decode an signed email using mapi_inetmapi_imtomapi() will have a different deliver time
		// when opening an signed email in for example the WebApp
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
			strFromEmail = vmHeader->From()->getValue().dynamicCast<vmime::mailbox>()->getEmail();
			if (!vmHeader->From()->getValue().dynamicCast<vmime::mailbox>()->getName().isEmpty()) {
				wstrFromName = getWideFromVmimeText(vmHeader->From()->getValue().dynamicCast<vmime::mailbox>()->getName());
			}

			hr = modifyFromAddressBook(&lpRecipProps, &ulRecipProps, strFromEmail.c_str(), wstrFromName.c_str(), MAPI_ORIG, (LPSPropTagArray)&sptaRecipPropsSentRepr);
			if (hr == hrSuccess) {
				hr = lpMessage->SetProps(ulRecipProps, lpRecipProps, NULL);
				if (hr != hrSuccess)
					goto exit;

				MAPIFreeBuffer(lpRecipProps);
				lpRecipProps = NULL;
			} else {
				if (wstrFromName.empty())
					wstrFromName = m_converter.convert_to<wstring>(strFromEmail);

				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_NAME_W;
				msgProps[nProps++].Value.lpszW = (WCHAR *)wstrFromName.c_str();

				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS_A;
				msgProps[nProps++].Value.lpszA = (char*)strFromEmail.c_str();

				strFromSearchKey = "SMTP:"+strFromEmail;
				transform(strFromSearchKey.begin(), strFromSearchKey.end(), strFromSearchKey.begin(), ::toupper);
				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_SEARCH_KEY;
				msgProps[nProps].Value.bin.cb = strFromSearchKey.size()+1; // include string terminator
				msgProps[nProps++].Value.bin.lpb = (BYTE*)strFromSearchKey.c_str();

				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE_W;
				msgProps[nProps++].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
				hr = ECCreateOneOff((LPTSTR)wstrFromName.c_str(), (LPTSTR)L"SMTP", (LPTSTR)m_converter.convert_to<wstring>(strFromEmail).c_str(),
									MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbFromEntryID, &lpFromEntryID);
				if(hr != hrSuccess)
					goto exit;

				msgProps[nProps].ulPropTag = PR_SENT_REPRESENTING_ENTRYID;
				msgProps[nProps].Value.bin.cb = cbFromEntryID;
				msgProps[nProps++].Value.bin.lpb = (BYTE*)lpFromEntryID;

				// SetProps is later on...
			}
		}
		
		if (vmHeader->hasField(vmime::fields::SENDER) || vmHeader->hasField(vmime::fields::FROM)) {
			// The original sender of the mail account (if non sender exist then the FROM)
			strSenderEmail = vmHeader->Sender()->getValue().dynamicCast<vmime::mailbox>()->getEmail();
			if(vmHeader->Sender()->getValue().dynamicCast<vmime::mailbox>()->getName().isEmpty() && strSenderEmail.empty()) {
				// Fallback on the original from address
				wstrSenderName = wstrFromName;
				strSenderEmail = strFromEmail;
			} else {
				if (!vmHeader->Sender()->getValue().dynamicCast<vmime::mailbox>()->getName().isEmpty()) {
					wstrSenderName = getWideFromVmimeText(vmHeader->Sender()->getValue().dynamicCast<vmime::mailbox>()->getName());
				} else {
					wstrSenderName = m_converter.convert_to<wstring>(strSenderEmail);
				}
			}

			hr = modifyFromAddressBook(&lpRecipProps, &ulRecipProps, strSenderEmail.c_str(), wstrSenderName.c_str(), MAPI_ORIG, (LPSPropTagArray)&sptaRecipPropsSender);
			if (hr == hrSuccess) {
				hr = lpMessage->SetProps(ulRecipProps, lpRecipProps, NULL);
				if (hr != hrSuccess)
					goto exit;

				MAPIFreeBuffer(lpRecipProps);
				lpRecipProps = NULL;
			} else {
				msgProps[nProps].ulPropTag = PR_SENDER_NAME_W;
				msgProps[nProps++].Value.lpszW = (WCHAR *)wstrSenderName.c_str();

				msgProps[nProps].ulPropTag = PR_SENDER_EMAIL_ADDRESS_A;
				msgProps[nProps++].Value.lpszA = (char*)strSenderEmail.c_str();

				strSenderSearchKey = "SMTP:"+strSenderEmail;
				transform(strSenderSearchKey.begin(), strSenderSearchKey.end(), strSenderSearchKey.begin(), ::toupper);
				msgProps[nProps].ulPropTag = PR_SENDER_SEARCH_KEY;
				msgProps[nProps].Value.bin.cb = strSenderSearchKey.size()+1; // include string terminator
				msgProps[nProps++].Value.bin.lpb = (BYTE*)strSenderSearchKey.c_str();

				msgProps[nProps].ulPropTag = PR_SENDER_ADDRTYPE_W;
				msgProps[nProps++].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
				hr = ECCreateOneOff((LPTSTR)wstrSenderName.c_str(), (LPTSTR)L"SMTP", (LPTSTR)m_converter.convert_to<wstring>(strSenderEmail).c_str(),
									MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbSenderEntryID, &lpSenderEntryID);
				if(hr != hrSuccess)
					goto exit;

				msgProps[nProps].ulPropTag = PR_SENDER_ENTRYID;
				msgProps[nProps].Value.bin.cb = cbSenderEntryID;
				msgProps[nProps++].Value.bin.lpb = (BYTE*)lpSenderEntryID;
			}
		}
		
		hr = lpMessage->SetProps(nProps, msgProps, NULL);
		if (hr != hrSuccess)
			goto exit;

		//Conversation topic
		if (vmHeader->hasField("Thread-Topic"))
		{
			wstring convTT = getWideFromVmimeText(*vmHeader->findField("Thread-Topic")->getValue().dynamicCast<vmime::text>());

			sConTopic.ulPropTag = PR_CONVERSATION_TOPIC_W;
			sConTopic.Value.lpszW = (WCHAR *)convTT.c_str();

			hr = lpMessage->SetProps(1, &sConTopic, NULL);
			if (hr != hrSuccess)
				goto exit;

		} else if(HrGetOneProp(lpMessage, PR_NORMALIZED_SUBJECT_W, &lpPropNormalizedSubject) == hrSuccess) {
			
			sConTopic.ulPropTag = PR_CONVERSATION_TOPIC_W;
			sConTopic.Value.lpszW = lpPropNormalizedSubject->Value.lpszW;
			
			hr = lpMessage->SetProps(1, &sConTopic, NULL);
			if (hr != hrSuccess)
				goto exit;
		}

		// Thread-Index header
		if (vmHeader->hasField("Thread-Index"))
		{
			vmime::string outString;
			SPropValue sThreadIndex;

			string threadIndex = vmHeader->findField("Thread-Index")->getValue()->generate();

			vmime::ref<vmime::utility::encoder::encoder> enc = vmime::utility::encoder::encoderFactory::getInstance()->create("base64");

			vmime::utility::inputStreamStringAdapter in(threadIndex);			
			vmime::utility::outputStreamStringAdapter out(outString);

			enc->decode(in, out);

			sThreadIndex.ulPropTag = PR_CONVERSATION_INDEX;
			sThreadIndex.Value.bin.cb = outString.size();
			sThreadIndex.Value.bin.lpb = (LPBYTE)outString.c_str();

			hr = lpMessage->SetProps(1, &sThreadIndex, NULL);
			if (hr != hrSuccess)
				goto exit;
		}
		
		if (vmHeader->hasField("Importance")) {
			SPropValue sPriority[2];
			sPriority[0].ulPropTag = PR_PRIORITY;
			sPriority[1].ulPropTag = PR_IMPORTANCE;
			string importance = vmHeader->findField("Importance")->getValue()->generate();
			
			std::transform(importance.begin(), importance.end(), importance.begin(), ::tolower);

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
				goto exit;
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
				goto exit;
		}

		// X-Kopano-Vacation header (TODO: other headers?)
		if (vmHeader->hasField("X-Kopano-Vacation")) {
			SPropValue sIcon[1];
			sIcon[0].ulPropTag = PR_ICON_INDEX;
			sIcon[0].Value.l = ICON_MAIL_OOF;
			// exchange sets PR_MESSAGE_CLASS to IPM.Note.Rules.OofTemplate.Microsoft to get the icon
			hr = lpMessage->SetProps(1, sIcon, NULL);
			if (hr != hrSuccess)
				goto exit;
		}

		// Sensitivity header
		if (vmHeader->hasField("Sensitivity")) {
			SPropValue sSensitivity[1];
			string sensitivity = vmHeader->findField("Sensitivity")->getValue()->generate();
			transform(sensitivity.begin(), sensitivity.end(), sensitivity.begin(), ::tolower);

			sSensitivity[0].ulPropTag = PR_SENSITIVITY;
			if (sensitivity.compare("personal") == 0) {
				sSensitivity[0].Value.ul = SENSITIVITY_PERSONAL;
			} else if (sensitivity.compare("private") == 0) {
				sSensitivity[0].Value.ul = SENSITIVITY_PRIVATE;
			} else if (sensitivity.compare("company-confidential") == 0) {
				sSensitivity[0].Value.ul = SENSITIVITY_COMPANY_CONFIDENTIAL;
			} else {
				sSensitivity[0].Value.ul = SENSITIVITY_NONE;
			}

			hr = lpMessage->SetProps(1, sSensitivity, NULL);
			if (hr != hrSuccess)
				goto exit;
		}

		// Expiry time header
		try {
			if (vmHeader->hasField("Expiry-Time")) {
				SPropValue sExpiryTime;

				// reparse string to datetime
				vmime::datetime expiry(vmHeader->findField("Expiry-Time")->getValue()->generate());

				sExpiryTime.ulPropTag = PR_EXPIRY_TIME;
				sExpiryTime.Value.ft = vmimeDatetimeToFiletime(expiry);

				hr = lpMessage->SetProps(1, &sExpiryTime, NULL);
				if (hr != hrSuccess)
					goto exit;
			}
		}
		catch(...) {}

		// read receipt	request
		// note: vmime never checks if the given pos to getMailboxAt() and similar functions is valid, so we check if the list is empty before using it
		if (vmHeader->hasField("Disposition-Notification-To") &&
			!vmHeader->DispositionNotificationTo()->getValue().dynamicCast<vmime::mailboxList>()->isEmpty())
		{
			vmime::ref<vmime::mailbox> mbReadReceipt = vmHeader->DispositionNotificationTo()->getValue().dynamicCast<vmime::mailboxList>()->getMailboxAt(0); // we only use the 1st
			if (mbReadReceipt && !mbReadReceipt->isEmpty())
			{
				wstring wstrRRName = getWideFromVmimeText(mbReadReceipt->getName());
				wstring wstrRREmail = m_converter.convert_to<wstring>(mbReadReceipt->getEmail());

				if (wstrRRName.empty())
					wstrRRName = wstrRREmail;

				//FIXME: Use an addressbook entry for "ZARAFA"-type addresses?
				hr = ECCreateOneOff((LPTSTR)wstrRRName.c_str(),	(LPTSTR)L"SMTP", (LPTSTR)wstrRREmail.c_str(), MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbEntryID, &lpEntryID);
				if (hr != hrSuccess)
					goto exit;

				SPropValue sRRProps[4];
				sRRProps[0].ulPropTag = PR_READ_RECEIPT_REQUESTED;
				sRRProps[0].Value.b = true;
				
				sRRProps[1].ulPropTag = PR_MESSAGE_FLAGS;
				sRRProps[1].Value.ul = (m_dopt.mark_as_read ? MSGFLAG_READ : 0) | MSGFLAG_UNMODIFIED | MSGFLAG_RN_PENDING | MSGFLAG_NRN_PENDING;

				sRRProps[2].ulPropTag = PR_REPORT_ENTRYID;
				sRRProps[2].Value.bin.cb = cbEntryID;
				sRRProps[2].Value.bin.lpb = (BYTE*)lpEntryID;

				sRRProps[3].ulPropTag = PR_REPORT_NAME_W;
				sRRProps[3].Value.lpszW = (WCHAR *)wstrRREmail.c_str();

				hr = lpMessage->SetProps(4, sRRProps, NULL);
				if (hr != hrSuccess)
					goto exit;

				MAPIFreeBuffer(lpEntryID); lpEntryID = NULL;
			}
		}

		for (const auto &field : vmHeader->getFieldList()) {
			std::string value, name = field->getName();
			
			if (name[0] != 'X')
				continue;

			// exclusion list?
			if (name == "X-Priority")
				continue;

			transform(name.begin(), name.end(), name.begin(), ::tolower);

			LPMAPINAMEID lpNameID = NULL;
			LPSPropTagArray lpPropTags = NULL;

			if ((hr = MAPIAllocateBuffer(sizeof(MAPINAMEID), (void**)&lpNameID)) != hrSuccess)
				goto exit;
			lpNameID->lpguid = (GUID*)&PS_INTERNET_HEADERS;
			lpNameID->ulKind = MNID_STRING;

			int vlen = mbstowcs(NULL, name.c_str(), 0) +1;
			if ((hr = MAPIAllocateMore(vlen*sizeof(WCHAR), lpNameID, (void**)&lpNameID->Kind.lpwstrName)) != hrSuccess)
				goto exit;
			mbstowcs(lpNameID->Kind.lpwstrName, name.c_str(), vlen);

			hr = lpMessage->GetIDsFromNames(1, &lpNameID, MAPI_CREATE, &lpPropTags);
			if (hr != hrSuccess) {
				hr = hrSuccess;
				goto next;
			}

			SPropValue sProp[1];
			value = field->getValue()->generate();
			sProp[0].ulPropTag = PROP_TAG(PT_STRING8, PROP_ID(lpPropTags->aulPropTag[0]));
			sProp[0].Value.lpszA = (char*)value.c_str();

			hr = lpMessage->SetProps(1, sProp, NULL);
			if (hr != hrSuccess) {
				hr = hrSuccess;	// ignore this x-header as named props then
				goto next;
			}

next:
			MAPIFreeBuffer(lpPropTags);
			lpPropTags = NULL;
			MAPIFreeBuffer(lpNameID);
			lpNameID = NULL;
		}
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception on parsing headers: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on parsing headers: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception occurred on parsing headers");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

exit:
	MAPIFreeBuffer(lpRecipProps);
	MAPIFreeBuffer(lpEntry);
	MAPIFreeBuffer(lpEntryList);
	MAPIFreeBuffer(lpFromEntryID);
	MAPIFreeBuffer(lpSenderEntryID);
	MAPIFreeBuffer(lpEntryID);
	MAPIFreeBuffer(lpPropNormalizedSubject);
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
	LPSPropValue lpRecipType = NULL; // non-free
	LPSPropValue lpEntryId = NULL;	// non-free
	bool bToMe = false;
	bool bCcMe = false;
	bool bRecipMe = false;
	SPropValue sProps[3];

	if (m_dopt.user_entryid == NULL)
		return hrSuccess; /* Not an error, but do not do any processing */

	// Loop through all recipients of the message to find ourselves in the recipient list.
	for (i = 0; i < lpRecipients->cEntries; ++i) {
		lpRecipType = PpropFindProp(lpRecipients->aEntries[i].rgPropVals, lpRecipients->aEntries[i].cValues, PR_RECIPIENT_TYPE);
		lpEntryId = PpropFindProp(lpRecipients->aEntries[i].rgPropVals, lpRecipients->aEntries[i].cValues, PR_ENTRYID);

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
HRESULT VMIMEToMAPI::handleRecipients(vmime::ref<vmime::header> vmHeader, IMessage* lpMessage) {
	HRESULT		hr				= hrSuccess;
	LPADRLIST 	lpRecipients	= NULL;

	try {
		vmime::ref<vmime::addressList> lpVMAListRecip = vmHeader->To()->getValue().dynamicCast<vmime::addressList>();
		vmime::ref<vmime::addressList> lpVMAListCopyRecip = vmHeader->Cc()->getValue().dynamicCast<vmime::addressList>();
		vmime::ref<vmime::addressList> lpVMAListBlCpRecip = vmHeader->Bcc()->getValue().dynamicCast<vmime::addressList>();

		int iAdresCount = lpVMAListRecip->getAddressCount() + lpVMAListCopyRecip->getAddressCount() + lpVMAListBlCpRecip->getAddressCount();

		if (iAdresCount == 0)
			goto exit;

		hr = MAPIAllocateBuffer(CbNewADRLIST(iAdresCount), (void **)&lpRecipients);
		if (hr != hrSuccess)
			goto exit;

		lpRecipients->cEntries = 0;

		if (!lpVMAListRecip->isEmpty()) {
			hr = modifyRecipientList(lpRecipients, lpVMAListRecip, MAPI_TO);
			if (hr != hrSuccess)
				goto exit;
		}

		if (!lpVMAListCopyRecip->isEmpty()) {
			hr = modifyRecipientList(lpRecipients, lpVMAListCopyRecip, MAPI_CC);
			if (hr != hrSuccess)
				goto exit;
		}

		if (!lpVMAListBlCpRecip->isEmpty()) {
			hr = modifyRecipientList(lpRecipients, lpVMAListBlCpRecip, MAPI_BCC);
			if (hr != hrSuccess)
				goto exit;
		}
		
		// Handle PR_MESSAGE_*_ME props
		hr = handleMessageToMeProps(lpMessage, lpRecipients);
		if (hr != hrSuccess)
			goto exit;

		// actually modify recipients in mapi object
		hr = lpMessage->ModifyRecipients(MODRECIP_ADD, lpRecipients);	
		if (hr != hrSuccess)
			goto exit;

	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception on recipients: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on recipients: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception occurred on recipients");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

exit:
	if (lpRecipients != NULL) {
		// Because the recipient list can be altered to delete 1 row, or add 1 row using ModifyRecipients()
		// we need to free this data with FreeProws, and not the default MAPIFreeBuffer() call.
		FreeProws((LPSRowSet) lpRecipients);
	}

	return hr;
}

/**
 * Adds recipients from a vmime list to rows for the recipient
 * table. Starts adding at offset in cEntries member of the lpRecipients
 * struct.
 *
 * Entries are either converted to an addressbook entry, or an one-off entry.
 *
 * @param[out]	lpRecipients	MAPI address list to be filled.
 * @param[in]	vmAddressList	List of recipient of a specific type (To/Cc/Bcc).
 * @param[in]	ulRecipType		Type of recipients found in vmAddressList.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::modifyRecipientList(LPADRLIST lpRecipients, vmime::ref<vmime::addressList> vmAddressList, ULONG ulRecipType) {
	HRESULT			hr				= hrSuccess;
	int				iAddressCount	= vmAddressList->getAddressCount();
	ULONG			cbEntryID		= 0;
	LPENTRYID		lpEntryID		= NULL;
	vmime::ref<vmime::mailbox> mbx	= NULL;
	vmime::ref<vmime::mailboxGroup> grp	= NULL;
	vmime::ref<vmime::address> vmAddress = NULL;
	std::wstring	wstrName;
	std::string		strEmail, strSearch;
	unsigned int 	iRecipNum		= 0;

	// order and types are important for modifyFromAddressBook()
	SizedSPropTagArray(7, sptaRecipientProps) = { 7, { PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_DISPLAY_TYPE, PR_EMAIL_ADDRESS_W, PR_ENTRYID,
													   PR_SEARCH_KEY, PR_SMTP_ADDRESS_W } };

	// walk through all recipients
	for (int iRecip = 0; iRecip < iAddressCount; ++iRecip) {
		
		try {
			vmime::text vmText;

			mbx = NULL;
			grp = NULL;

			vmAddress = vmAddressList->getAddressAt(iRecip);
			
			if (vmAddress->isGroup()) {
				grp = vmAddress.dynamicCast<vmime::mailboxGroup>();
				if (!grp)
					continue;
				strEmail.clear();
				vmText = grp->getName();
				if (grp->isEmpty() && vmText == vmime::text("undisclosed-recipients"))
					continue;
			} else {
				mbx = vmAddress.dynamicCast<vmime::mailbox>();
				if (!mbx)
					continue;
				strEmail = mbx->getEmail();
				vmText = mbx->getName();
			}

			if (!vmText.isEmpty()) {
				wstrName = getWideFromVmimeText(vmText);
			} else {
				wstrName.clear();
			}
		}
		catch (vmime::exception& e) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception on modify recipient: %s", e.what());
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}
		catch (std::exception& e) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on modify recipient: %s", e.what());
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}
		catch (...) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception occurred on modify recipient");
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}

		iRecipNum = lpRecipients->cEntries;

		// use email address or fullname to find GAB entry, do not pass fullname to keep resolved addressbook fullname
		strSearch = strEmail;
		if (strSearch.empty())
			strSearch = m_converter.convert_to<std::string>(wstrName);

		// @todo: maybe make strSearch a wide string and check if we need to use the fullname argument for modifyFromAddressBook
		hr = modifyFromAddressBook(&lpRecipients->aEntries[iRecipNum].rgPropVals, &lpRecipients->aEntries[iRecipNum].cValues, strSearch.c_str(), NULL, ulRecipType, (LPSPropTagArray)&sptaRecipientProps);
		if (hr != hrSuccess) {
			// Fallback if the entry was not found (or errored) in the addressbook
			int iNumTags = 8;
	
			iRecipNum = lpRecipients->cEntries;

			if (wstrName.empty())
				wstrName = m_converter.convert_to<wstring>(strEmail);

			// will be cleaned up by caller.
			hr = MAPIAllocateBuffer(sizeof(SPropValue) * iNumTags, (void **)&lpRecipients->aEntries[iRecipNum].rgPropVals);	
			if (hr != hrSuccess)
				goto exit;
			
			lpRecipients->aEntries[iRecipNum].cValues						= iNumTags;	
			lpRecipients->aEntries[iRecipNum].ulReserved1					= 0;
			
			lpRecipients->aEntries[iRecipNum].rgPropVals[0].ulPropTag		= PR_RECIPIENT_TYPE;
			lpRecipients->aEntries[iRecipNum].rgPropVals[0].Value.l			= ulRecipType;
		
			lpRecipients->aEntries[iRecipNum].rgPropVals[1].ulPropTag		= PR_DISPLAY_NAME_W;
			hr = MAPIAllocateMore((wstrName.size()+1) * sizeof(WCHAR), lpRecipients->aEntries[iRecipNum].rgPropVals,
								  (void **)&lpRecipients->aEntries[iRecipNum].rgPropVals[1].Value.lpszW);
			if (hr != hrSuccess)
				goto exit;
			wcscpy(lpRecipients->aEntries[iRecipNum].rgPropVals[1].Value.lpszW, wstrName.c_str());

			lpRecipients->aEntries[iRecipNum].rgPropVals[2].ulPropTag		= PR_SMTP_ADDRESS_A;
			hr = MAPIAllocateMore(strEmail.size()+1, lpRecipients->aEntries[iRecipNum].rgPropVals,
								  (void **)&lpRecipients->aEntries[iRecipNum].rgPropVals[2].Value.lpszA);
			if (hr != hrSuccess)
				goto exit;
			strcpy(lpRecipients->aEntries[iRecipNum].rgPropVals[2].Value.lpszA, strEmail.c_str());

			lpRecipients->aEntries[iRecipNum].rgPropVals[3].ulPropTag		= PR_ENTRYID;
			hr = ECCreateOneOff((LPTSTR)wstrName.c_str(), (LPTSTR)L"SMTP", (LPTSTR)m_converter.convert_to<wstring>(strEmail).c_str(),
								MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbEntryID, &lpEntryID);
			if (hr != hrSuccess)
				goto exit;
				
			hr = MAPIAllocateMore(cbEntryID, lpRecipients->aEntries[iRecipNum].rgPropVals,
								  (void **)&lpRecipients->aEntries[iRecipNum].rgPropVals[3].Value.bin.lpb);
			if (hr != hrSuccess)
				goto exit;

			lpRecipients->aEntries[iRecipNum].rgPropVals[3].Value.bin.cb = cbEntryID;
			memcpy(lpRecipients->aEntries[iRecipNum].rgPropVals[3].Value.bin.lpb, lpEntryID, cbEntryID);

			lpRecipients->aEntries[iRecipNum].rgPropVals[4].ulPropTag	= PR_ADDRTYPE_W;
			lpRecipients->aEntries[iRecipNum].rgPropVals[4].Value.lpszW = const_cast<wchar_t *>(L"SMTP");

			strSearch = "SMTP:"+strEmail;
			transform(strSearch.begin(), strSearch.end(), strSearch.begin(), ::toupper);
			lpRecipients->aEntries[iRecipNum].rgPropVals[5].ulPropTag	= PR_SEARCH_KEY;
			lpRecipients->aEntries[iRecipNum].rgPropVals[5].Value.bin.cb = strSearch.size() + 1; // we include the trailing 0 as MS does this also
			hr = MAPIAllocateMore(strSearch.size()+1, lpRecipients->aEntries[iRecipNum].rgPropVals,
								  (void **)&lpRecipients->aEntries[iRecipNum].rgPropVals[5].Value.bin.lpb);
			if (hr != hrSuccess)
				goto exit;
			memcpy(lpRecipients->aEntries[iRecipNum].rgPropVals[5].Value.bin.lpb, strSearch.c_str(), strSearch.size()+1);

			// Add Email address
			lpRecipients->aEntries[iRecipNum].rgPropVals[6].ulPropTag		= PR_EMAIL_ADDRESS_A;
			hr = MAPIAllocateMore(strEmail.size()+1, lpRecipients->aEntries[iRecipNum].rgPropVals,
								  (void **)&lpRecipients->aEntries[iRecipNum].rgPropVals[6].Value.lpszA);
			if (hr != hrSuccess)
				goto exit;
			strcpy(lpRecipients->aEntries[iRecipNum].rgPropVals[6].Value.lpszA, strEmail.c_str());

			// Add display type
			lpRecipients->aEntries[iRecipNum].rgPropVals[7].ulPropTag = PR_DISPLAY_TYPE;
			lpRecipients->aEntries[iRecipNum].rgPropVals[7].Value.ul = DT_MAILUSER;			

			MAPIFreeBuffer(lpEntryID);
			lpEntryID = NULL;
		}
		++lpRecipients->cEntries;
	}

exit:
	MAPIFreeBuffer(lpEntryID);
	return hr;
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
HRESULT VMIMEToMAPI::modifyFromAddressBook(LPSPropValue *lppPropVals, ULONG *lpulValues, const char *email, const WCHAR *fullname, ULONG ulRecipType, LPSPropTagArray lpPropsList) {
	HRESULT hr = hrSuccess;
	LPENTRYID lpDDEntryID = NULL;
	ULONG cbDDEntryID;
	ULONG ulObj = 0;
	ADRLIST *lpAdrList = NULL;
	FlagList *lpFlagList = NULL;
	LPSPropValue lpProp = NULL;
	SPropValue sRecipProps[9]; // 8 from addressbook + PR_RECIPIENT_TYPE == max
	ULONG cValues = 0;
	SizedSPropTagArray(8, sptaAddress) = { 8, { PR_SMTP_ADDRESS_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, PR_DISPLAY_TYPE, PR_DISPLAY_NAME_W, PR_ENTRYID, PR_SEARCH_KEY, PR_OBJECT_TYPE } };

	if (!m_lpAdrBook) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	if ((!email || *email == '\0') && (!fullname || *fullname == '\0')) {
		// we have no data to lookup
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	if (!m_lpDefaultDir) {
		hr = m_lpAdrBook->GetDefaultDir(&cbDDEntryID, &lpDDEntryID);
		if (hr != hrSuccess)
			goto exit;

		hr = m_lpAdrBook->OpenEntry(cbDDEntryID, lpDDEntryID, NULL, 0, &ulObj, (LPUNKNOWN*)&m_lpDefaultDir);
		if (hr != hrSuccess)
			goto exit;
	}

	hr = MAPIAllocateBuffer(CbNewSRowSet(1), (void **) &lpAdrList);
	if (hr != hrSuccess)
		goto exit;

	lpAdrList->cEntries = 1;
	lpAdrList->aEntries[0].cValues = 1;

	hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **) &lpAdrList->aEntries[0].rgPropVals);
	if (hr != hrSuccess)
		goto exit;

	// static reference is OK here
	if (!email || *email == '\0') {
		lpAdrList->aEntries[0].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_W;
		lpAdrList->aEntries[0].rgPropVals[0].Value.lpszW = (WCHAR *)fullname; // try to find with fullname for groups without email addresses
	}
	else {
		lpAdrList->aEntries[0].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_A;
		lpAdrList->aEntries[0].rgPropVals[0].Value.lpszA = (char *)email; // normally resolve on email address
	}

	hr = MAPIAllocateBuffer(CbNewFlagList(1), (void **) &lpFlagList);
	if (hr != hrSuccess)
		goto exit;

	lpFlagList->cFlags = 1;
	lpFlagList->ulFlag[0] = MAPI_UNRESOLVED;

	hr = m_lpDefaultDir->ResolveNames((LPSPropTagArray)&sptaAddress, EMS_AB_ADDRESS_LOOKUP, lpAdrList, lpFlagList);
	if (hr != hrSuccess)
		goto exit;

	if (lpFlagList->cFlags != 1 || lpFlagList->ulFlag[0] != MAPI_RESOLVED) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// the server told us the entry is here.  from this point on we
	// don't want to return MAPI_E_NOT_FOUND anymore, so we need to
	// deal with missing data (which really shouldn't be the case for
	// some, so asserts in some places).

	if (PROP_TYPE(lpPropsList->aulPropTag[0]) != PT_NULL) {
		lpProp = PpropFindProp(lpAdrList->aEntries[0].rgPropVals, lpAdrList->aEntries[0].cValues, PR_ADDRTYPE_W);
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[0]; // PR_xxx_ADDRTYPE;
		ASSERT(lpProp);
		if (!lpProp) {
			lpLogger->Log(EC_LOGLEVEL_WARNING, "Missing PR_ADDRTYPE_W for search entry: email %s, fullname %ls", email ? email : "null", fullname ? fullname : L"null");
			sRecipProps[cValues].Value.lpszW = const_cast<wchar_t *>(L"ZARAFA");
		} else {
			sRecipProps[cValues].Value.lpszW = lpProp->Value.lpszW;
		}
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[1]) != PT_NULL) {
		lpProp = PpropFindProp(lpAdrList->aEntries[0].rgPropVals, lpAdrList->aEntries[0].cValues, PR_DISPLAY_NAME_W);
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[1];	// PR_xxx_DISPLAY_NAME;
		if (lpProp)
			sRecipProps[cValues].Value.lpszW = lpProp->Value.lpszW;	// use addressbook version
		else if (fullname && *fullname != '\0')
			sRecipProps[cValues].Value.lpszW = (WCHAR *)fullname;	// use email version
		else if (email && *email != '\0')
			sRecipProps[cValues].Value.lpszW = (WCHAR *)email;	// use email address
		else {
			sRecipProps[cValues].ulPropTag = CHANGE_PROP_TYPE(lpPropsList->aulPropTag[1], PT_ERROR);
			sRecipProps[cValues].Value.err = MAPI_E_NOT_FOUND;
		}
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[2]) != PT_NULL) {
		lpProp = PpropFindProp(lpAdrList->aEntries[0].rgPropVals, lpAdrList->aEntries[0].cValues, PR_DISPLAY_TYPE);
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[2]; // PR_xxx_DISPLAY_TYPE;
		if (!lpProp) {
			sRecipProps[cValues].Value.ul = DT_MAILUSER;
		} else {
			sRecipProps[cValues].Value.ul = lpProp->Value.ul;
		}
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[3]) != PT_NULL) {
		lpProp = PpropFindProp(lpAdrList->aEntries[0].rgPropVals, lpAdrList->aEntries[0].cValues, PR_EMAIL_ADDRESS_W);
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[3]; // PR_xxx_EMAIL_ADDRESS;
		ASSERT(lpProp);
		if (!lpProp) {
			sRecipProps[cValues].ulPropTag = CHANGE_PROP_TYPE(lpPropsList->aulPropTag[3], PT_ERROR);
			sRecipProps[cValues].Value.err = MAPI_E_NOT_FOUND;
		} else {
			sRecipProps[cValues].Value.lpszW = lpProp->Value.lpszW;
		}
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[4]) != PT_NULL) {
		lpProp = PpropFindProp(lpAdrList->aEntries[0].rgPropVals, lpAdrList->aEntries[0].cValues, PR_ENTRYID);
		ASSERT(lpProp);
		if (!lpProp) {
			// the one exception I guess? Let the fallback code create a one off entryid
			hr = MAPI_E_NOT_FOUND;
			goto exit;
		}
		sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[4]; // PR_xxx_ENTRYID;
		sRecipProps[cValues].Value.bin = lpProp->Value.bin;
		++cValues;
	}

	if (PROP_TYPE(lpPropsList->aulPropTag[5]) != PT_NULL) {
		lpProp = PpropFindProp(lpAdrList->aEntries[0].rgPropVals, lpAdrList->aEntries[0].cValues, PR_SEARCH_KEY);
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
		lpProp = PpropFindProp(lpAdrList->aEntries[0].rgPropVals, lpAdrList->aEntries[0].cValues, PR_SMTP_ADDRESS_W);
		if (!lpProp) {
			sRecipProps[cValues].ulPropTag = CHANGE_PROP_TYPE(lpPropsList->aulPropTag[6], PT_ERROR); // PR_xxx_SMTP_ADDRESS;
			sRecipProps[cValues].Value.err = MAPI_E_NOT_FOUND;
		} else {
			sRecipProps[cValues].ulPropTag = lpPropsList->aulPropTag[6]; // PR_xxx_SMTP_ADDRESS;
			sRecipProps[cValues].Value.lpszW = lpProp->Value.lpszW;
		}
		++cValues;
	}

	lpProp = PpropFindProp(lpAdrList->aEntries[0].rgPropVals, lpAdrList->aEntries[0].cValues, PR_OBJECT_TYPE);
	ASSERT(lpProp);
	if (!lpProp) {
		sRecipProps[cValues].Value.ul = MAPI_MAILUSER;
	} else {
		sRecipProps[cValues].Value.ul = lpProp->Value.ul;
	}
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

exit:
	if (lpAdrList)
		FreeProws((LPSRowSet)lpAdrList);

	MAPIFreeBuffer(lpFlagList);
	MAPIFreeBuffer(lpDDEntryID);
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
static std::list<unsigned int> vtm_order_alternatives(vmime::ref<vmime::body> vmBody)
{
	vmime::ref<vmime::header> vmHeader;
	vmime::ref<vmime::bodyPart> vmBodyPart;
	vmime::ref<vmime::mediaType> mt;
	std::list<unsigned int> lBodies, pgtext;

	for (int i = 0; i < vmBody->getPartCount(); ++i) {
		vmBodyPart = vmBody->getPartAt(i);
		vmHeader = vmBodyPart->getHeader();
		if (!vmHeader->hasField(vmime::fields::CONTENT_TYPE)) {
			/* RFC 2046 §5.1 ¶2 says treat it as text/plain */
			lBodies.push_front(i);
			continue;
		}
		mt = vmHeader->ContentType()->getValue().dynamicCast<vmime::mediaType>();
		// mostly better alternatives for text/plain, so try that last
		if (mt->getType() == vmime::mediaTypes::TEXT && mt->getSubType() == vmime::mediaTypes::TEXT_PLAIN)
			lBodies.push_back(i);
		else
			lBodies.push_front(i);
	}
	return lBodies;
}

HRESULT VMIMEToMAPI::dissect_multipart(vmime::ref<vmime::header> vmHeader,
    vmime::ref<vmime::body> vmBody, IMessage *lpMessage,
    bool bFilterDouble, bool bAppendBody)
{
	bool bAlternative = false;
	HRESULT hr = hrSuccess;

	if (vmBody->getPartCount() <= 0) {
		// a lonely attachment in a multipart, may not be empty when it's a signed part.
		hr = handleAttachment(vmHeader, vmBody, lpMessage);
		if (hr != hrSuccess)
			lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_multipart: Unable to save attachment");
		return hr;
	}

	// check new multipart type
	vmime::ref<vmime::mediaType> mt = vmHeader->ContentType()->getValue().dynamicCast<vmime::mediaType>();
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
		for (int i = 0; i < vmBody->getPartCount(); ++i) {
			vmime::ref<vmime::bodyPart> vmBodyPart = vmBody->getPartAt(i);

			hr = dissect_body(vmBodyPart->getHeader(), vmBodyPart->getBody(), lpMessage, bFilterDouble, bAppendBody);
			if (hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_multipart: Unable to parse sub multipart %d of mail body", i);
				return hr;
			}
		}
		return hrSuccess;
	}

	list<unsigned int> lBodies = vtm_order_alternatives(vmBody);

	// recursively process multipart alternatives in reverse to select best body first
	for (auto body_idx : lBodies) {
		vmime::ref<vmime::bodyPart> vmBodyPart = vmBody->getPartAt(body_idx);

		ec_log_debug("Trying to parse alternative multipart %d of mail body", body_idx);

		hr = dissect_body(vmBodyPart->getHeader(), vmBodyPart->getBody(), lpMessage, bFilterDouble, bAppendBody);
		if (hr == hrSuccess)
			return hrSuccess;
		ec_log_err("Unable to parse alternative multipart %d of mail body, trying other alternatives", body_idx);
	}
	/* If lBodies was empty, we could get here, with hr being hrSuccess. */
	if (hr != hrSuccess)
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to parse all alternative multiparts of mail body");
	return hr;
}

void VMIMEToMAPI::dissect_message(vmime::ref<vmime::body> vmBody, IMessage *lpMessage)
{
	// Create Attach
	ULONG ulAttNr = 0;
	LPATTACH pAtt = NULL;
	IMessage *lpNewMessage = NULL;
	LPSPropValue lpSubject = NULL;
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

	HRESULT hr = lpMessage->CreateAttach(NULL, 0, &ulAttNr, &pAtt);
	if (hr != hrSuccess)
		goto next;

	hr = pAtt->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY, (LPUNKNOWN *)&lpNewMessage);
	if (hr != hrSuccess)
		goto next;

	// handle message-in-message, save current state variables
	savedState = m_mailState;
	m_mailState.reset();
	++m_mailState.ulMsgInMsg;

	hr = convertVMIMEToMAPI(newMessage, lpNewMessage);

	// return to previous state
	m_mailState = savedState;

	if (hr != hrSuccess)
		goto next;

	if (HrGetOneProp(lpNewMessage, PR_SUBJECT_W, &lpSubject) == hrSuccess) {
		// Set PR_ATTACH_FILENAME of attachment to message subject, (WARNING: abuse of lpSubject variable)
		lpSubject->ulPropTag = PR_DISPLAY_NAME_W;
		pAtt->SetProps(1, lpSubject, NULL);
	}

	sAttachMethod.ulPropTag = PR_ATTACH_METHOD;
	sAttachMethod.Value.ul = ATTACH_EMBEDDED_MSG;
	pAtt->SetProps(1, &sAttachMethod, NULL);

	lpNewMessage->SaveChanges(0);
	pAtt->SaveChanges(0);

 next:
	MAPIFreeBuffer(lpSubject);
	lpSubject = NULL;

	if (lpNewMessage != NULL)
		lpNewMessage->Release();
	if (pAtt != NULL)
		pAtt->Release();
}

HRESULT VMIMEToMAPI::dissect_ical(vmime::ref<vmime::header> vmHeader,
    vmime::ref<vmime::body> vmBody, IMessage *lpMessage, bool bIsAttachment)
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
	ICalToMapi *lpIcalMapi = NULL;
	SPropValuePtr ptrSubject;

	// Some senders send utf-8 iCalendar information without a charset (Exchange does this). Default
	// to utf-8 if no charset was specified
	strCharset = vmBody->getCharset().getName();
	if (strCharset == "us-ascii")
		// We can safely upgrade from us-ascii to utf-8 since it is compatible
		strCharset = "utf-8";

	vmBody->getContents()->extract(os);

	if (bIsAttachment) {
		// create message in message to create calendar message
		SPropValue sAttProps[3];

		hr = lpMessage->CreateAttach(NULL, 0, &ulAttNr, &ptrAttach);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_ical-1790: Unable to create attachment for ical data: %s (%x)", GetMAPIErrorMessage(hr), hr);
			goto exit;
		}

		hr = ptrAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY, &ptrNewMessage);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_ical-1796: Unable to create message attachment for ical data: %s (%x)", GetMAPIErrorMessage(hr), hr);
			goto exit;
		}

		sAttProps[0].ulPropTag = PR_ATTACH_METHOD;
		sAttProps[0].Value.ul = ATTACH_EMBEDDED_MSG;

		sAttProps[1].ulPropTag = PR_ATTACHMENT_HIDDEN;
		sAttProps[1].Value.b = FALSE;

		sAttProps[2].ulPropTag = PR_ATTACH_FLAGS;
		sAttProps[2].Value.ul = 0;

		hr = ptrAttach->SetProps(3, sAttProps, NULL);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_ical-1811: Unable to create message attachment for ical data: %s (%x)", GetMAPIErrorMessage(hr), hr);
			goto exit;
		}

		lpIcalMessage = ptrNewMessage.get();
	}

	hr = CreateICalToMapi(lpMessage, m_lpAdrBook, true, &lpIcalMapi);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_ical-1820: Unable to create ical converter: %s (%x)", GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	hr = lpIcalMapi->ParseICal(icaldata, strCharset, "UTC" , NULL, 0);
	if (hr != hrSuccess || lpIcalMapi->GetItemCount() != 1) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_ical-1826: Unable to parse ical information: %s (%x), items: %d, adding as normal attachment",
			GetMAPIErrorMessage(hr), hr, lpIcalMapi->GetItemCount());
		hr = handleAttachment(vmHeader, vmBody, lpMessage);
		goto exit;
	}

	hr = lpIcalMapi->GetItem(0, IC2M_NO_RECIPIENTS | IC2M_APPEND_ONLY, lpIcalMessage);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_ical-1834: Error while converting ical to mapi: %s (%x)", GetMAPIErrorMessage(hr), hr);
		goto exit;
	}
	if (!bIsAttachment)
		goto exit;

	// give attachment name of calendar item
	if (HrGetOneProp(ptrNewMessage, PR_SUBJECT_W, &ptrSubject) == hrSuccess) {
		ptrSubject->ulPropTag = PR_DISPLAY_NAME_W;

		hr = ptrAttach->SetProps(1, ptrSubject, NULL);
		if (hr != hrSuccess)
			goto exit;
	}

	hr = ptrNewMessage->SaveChanges(0);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_ical-1851: Unable to save ical message: %s (%x)", GetMAPIErrorMessage(hr), hr);
		goto exit;
	}
	hr = ptrAttach->SaveChanges(0);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "dissect_ical-1856: Unable to save ical message attachment: %s (%x)", GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	// make sure we show the attachment icon
	m_mailState.attachLevel = ATTACH_NORMAL;
 exit:
	delete lpIcalMapi;
	return hr;
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
 *	message				rfc822, partial ( please no fragmentation and reassembly ), external-body
 *
 * @param[in]	vmHeader		vmime header part which describes the contents of the body in vmBody.
 * @param[in]	vmBody			a body part of the mail.
 * @param[out]	lpMessage		MAPI message to write header properties in.
 * @param[in]	filterDouble	skips some attachments when true, only happens then an appledouble attachment marker is found.
 * @param[in]	bAppendBody		Concatenate with existing body if true, makes an attachment when false and a body was previously saved.
 * @return		MAPI error code.
 * @retval		MAPI_E_CALL_FAILED	Caught an exception, which breaks the conversion.
 */
HRESULT VMIMEToMAPI::dissect_body(vmime::ref<vmime::header> vmHeader,
    vmime::ref<vmime::body> vmBody, IMessage *lpMessage, bool filterDouble,
    bool appendBody)
{
	HRESULT	hr = hrSuccess;
	IStream *lpStream = NULL;
	SPropValue sPropSMIMEClass;
	bool bFilterDouble = filterDouble;
	bool bAppendBody = appendBody;
	bool bIsAttachment = false;

	if (vmHeader->hasField(vmime::fields::MIME_VERSION))
		++m_mailState.mime_vtag_nest;

	try {
		vmime::ref<vmime::mediaType> mt = vmHeader->ContentType()->getValue().dynamicCast<vmime::mediaType>();
		bool force_raw = false;

		try {
			bIsAttachment = vmHeader->ContentDisposition()->getValue().dynamicCast<vmime::contentDisposition>()->getName() == vmime::contentDispositionTypes::ATTACHMENT;
		} catch (vmime::exception) {
			// ignore exception, a header needed to detect attachment status could not be used
			// probably can not happen, but better safe than sorry.
		}

		try {
			vmBody->getContents()->getEncoding().getEncoder();
		} catch (vmime::exceptions::no_encoder_available &) {
			/* RFC 2045 §6.4 page 17 */
			lpLogger->Log(EC_LOGLEVEL_DEBUG, "Encountered unknown Content-Transfer-Encoding \"%s\".",
				vmBody->getContents()->getEncoding().getName().c_str());
			force_raw = true;
		}

		if (force_raw) {
			hr = handleAttachment(vmHeader, vmBody, lpMessage, true);
			if (hr != hrSuccess)
				goto exit;
		} else if (mt->getType() == "multipart") {
			hr = dissect_multipart(vmHeader, vmBody, lpMessage, bFilterDouble, bAppendBody);
			if (hr != hrSuccess)
				goto exit;
		// Only handle as inline text if no filename is specified and not specified as 'attachment'
		} else if (	mt->getType() == vmime::mediaTypes::TEXT &&
					(mt->getSubType() == vmime::mediaTypes::TEXT_PLAIN || mt->getSubType() == vmime::mediaTypes::TEXT_HTML) &&
					!bIsAttachment) {
			if (mt->getSubType() == vmime::mediaTypes::TEXT_HTML || (m_mailState.bodyLevel == BODY_HTML && bAppendBody)) {
				// handle real html part, or append a plain text bodypart to the html main body
				// subtype guaranteed html or plain.
				hr = handleHTMLTextpart(vmHeader, vmBody, lpMessage, bAppendBody);
				if (hr != hrSuccess) {
					lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to parse mail HTML text");
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
			
			hr = CreateStreamOnHGlobal(NULL, TRUE, &lpStream);	
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
					lpLogger->Log(EC_LOGLEVEL_WARNING, "TNEF attachment saving failed: 0x%08X", hr);
			} else {
				lpLogger->Log(EC_LOGLEVEL_WARNING, "TNEF attachment parsing failed: 0x%08X", hr);
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
			hr = handleAttachment(vmHeader, vmBody, lpMessage, false);
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
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to set message class");
				goto exit;
			}
		} else if (mt->getType() == vmime::mediaTypes::APPLICATION && mt->getSubType() == vmime::mediaTypes::APPLICATION_OCTET_STREAM) {
			if (vmHeader->ContentDisposition().dynamicCast<vmime::contentDispositionField>()->hasParameter("filename") ||
				vmHeader->ContentType().dynamicCast<vmime::contentTypeField>()->hasParameter("name")) {
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
			hr = handleAttachment(vmHeader, vmBody, lpMessage);
			if (hr != hrSuccess)
				goto exit;
		}
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception on parsing body: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on parsing body: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception occurred on parsing body");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

exit:
	if (vmHeader->hasField(vmime::fields::MIME_VERSION))
		--m_mailState.mime_vtag_nest;
	if(lpStream)
		lpStream->Release();
	return hr;
}

/**
 * Decode the MIME part as per its Content-Transfer-Encoding header.
 * @im_body:	Internet Message / VMIME body object
 *
 * Returns the transfer-decoded data.
 */
std::string
VMIMEToMAPI::content_transfer_decode(vmime::ref<vmime::body> im_body) const
{
	/* TODO: Research how conversion could be minimized using streams. */
	std::string data;
	vmime::utility::outputStreamStringAdapter str_adap(data);
	vmime::ref<const vmime::contentHandler> im_cont =
		im_body->getContents();

	try {
		im_cont->extract(str_adap);
	} catch (vmime::exceptions::no_encoder_available &e) {
		lpLogger->Log(EC_LOGLEVEL_WARNING,
			"VMIME could not process the Content-Transfer-Encoding \"%s\" (%s). Reading part raw.",
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
VMIMEToMAPI::get_mime_encoding(vmime::ref<vmime::header> im_header,
    vmime::ref<vmime::body> im_body) const
{
	vmime::ref<const vmime::contentTypeField> ctf =
		im_header->ContentType().dynamicCast<vmime::contentTypeField>();

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
			lpLogger->Log(EC_LOGLEVEL_DEBUG,
				"renovate_encoding: reading data using charset \"%s\" succeeded.",
				name);
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
			lpLogger->Log(lvl,
				"renovate_encoding: reading data using charset \"%s\" produced partial results: %s",
				name, ce.what());
		} catch (unknown_charset_exception &) {
			lpLogger->Log(EC_LOGLEVEL_WARNING, "renovate_encoding: unknown charset \"%s\", skipping", name);
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
		lpLogger->Log(EC_LOGLEVEL_DEBUG,
			"renovate_encoding: forced interpretation as charset \"%s\".", name);
		return i;
	}
	return -1;
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
HRESULT VMIMEToMAPI::handleTextpart(vmime::ref<vmime::header> vmHeader, vmime::ref<vmime::body> vmBody, IMessage* lpMessage, bool bAppendBody) {
	HRESULT hr = S_OK;
	IStream *lpStream = NULL;

	bool append = m_mailState.bodyLevel < BODY_PLAIN ||
	              (m_mailState.bodyLevel == BODY_PLAIN && bAppendBody);

	if (!append) {
		// we already had a plaintext or html body, so attach this text part
		hr = handleAttachment(vmHeader, vmBody, lpMessage);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to parse attached text mail");
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
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "No charset (case #1). Defaulting to \"%s\".", m_dopt.default_charset);
				mime_charset = m_dopt.default_charset;
			} else {
				/* RFC 2045 §5.2 */
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "No charset (case #2). Defaulting to \"us-ascii\".");
				mime_charset = vmime::charsets::US_ASCII;
			}
		}
		mime_charset = vtm_upgrade_charset(mime_charset);
		if (!ValidateCharset(mime_charset.getName().c_str())) {
			/* RFC 2049 §2 item 6 subitem 5 */
			lpLogger->Log(EC_LOGLEVEL_DEBUG, "Unknown Content-Type charset \"%s\". Storing as attachment instead.", mime_charset.getName().c_str());
			return handleAttachment(vmHeader, vmBody, lpMessage, true);
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

		hr = lpMessage->OpenProperty(PR_BODY_W, &IID_IStream, STGM_TRANSACTED, ulFlags, (LPUNKNOWN *)&lpStream);
		if (hr != hrSuccess)
			goto exit;

		if (bAppendBody) {
			static const LARGE_INTEGER liZero = {{0, 0}};
			hr = lpStream->Seek(liZero, SEEK_END, NULL);
			if (hr != hrSuccess)
				goto exit;
		}

		hr = lpStream->Write(strUnicodeText.c_str(), strUnicodeText.length() * sizeof(wstring::value_type), NULL);
		if (hr != hrSuccess)
			goto exit;

		// commit triggers plain -> html/rtf conversion, PR_INTERNET_CPID must be set.
		hr = lpStream->Commit(0);
		if (hr != hrSuccess)
			goto exit;
	}
	catch (vmime::exception &e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception on text body: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception &e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on text body: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception occurred on text body");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	m_mailState.bodyLevel = BODY_PLAIN;

exit:
	if (lpStream)
		lpStream->Release();

	return hr;
}

static bool vtm_ascii_compatible(const char *s)
{
	static const char in[] = {
		0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,
		24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,
		45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,
		66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,
		87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,
		106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,
		121,122,123,124,125,126,127,
	};
	char out[sizeof(in)];
	iconv_t cd = iconv_open(s, "us-ascii");
	if (cd == reinterpret_cast<iconv_t>(-1))
		return false;
	char *inbuf = const_cast<char *>(in), *outbuf = out;
	size_t insize = sizeof(in), outsize = sizeof(out);
	bool mappable = iconv(cd, &inbuf, &insize, &outbuf, &outsize) >= 0;
	iconv_close(cd);
	return mappable && memcmp(in, out, sizeof(in)) == 0;
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
HRESULT VMIMEToMAPI::handleHTMLTextpart(vmime::ref<vmime::header> vmHeader, vmime::ref<vmime::body> vmBody, IMessage* lpMessage, bool bAppendBody) {
	HRESULT		hr				= hrSuccess;
	IStream*	lpHTMLStream	= NULL; 
	ULONG		cbWritten		= 0;
	std::string strHTML;
	const char *lpszCharset = NULL;
	SPropValue sCodepage;
	LONG ulFlags;

	bool new_text = m_mailState.bodyLevel < BODY_HTML ||
                        (m_mailState.bodyLevel == BODY_HTML && bAppendBody);

	if (!new_text) {
		// already found html as body, so this is an attachment
		hr = handleAttachment(vmHeader, vmBody, lpMessage);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to parse attached text mail");
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
			lpLogger->Log(EC_LOGLEVEL_DEBUG, "MIME headers declare charset \"%s\", while HTML meta tag declares \"%s\".",
				mime_charset.getName().c_str(),
				html_charset.getName().c_str());

		if (mime_charset == im_charset_unspec &&
		    html_charset == im_charset_unspec) {
			if (m_mailState.mime_vtag_nest > 0) {
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "No charset (case #3), defaulting to \"us-ascii\".");
				mime_charset = html_charset = vmime::charsets::US_ASCII;
			} else if (html_analyze < 0) {
				/*
				 * No HTML structure found when assuming ASCII,
				 * so we can just directly fallback to default_charset.
				 */
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "No charset (case #4), defaulting to \"%s\".", m_dopt.default_charset);
				mime_charset = html_charset = m_dopt.default_charset;
			} else if (vtm_ascii_compatible(m_dopt.default_charset)) {
				/*
				 * HTML structure recognized when interpreting as ASCII.
				 * If default_charset is compatible, that is our pick.
				 */
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "No charset (case #5), defaulting to \"%s\".", m_dopt.default_charset);
				mime_charset = html_charset = m_dopt.default_charset;
			} else {
				/*
				 * HTML structure recognized when interpreting as ASCII.
				 * default_charset is not compatible, so cannot be
				 * the actual encoding.
				 */
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "No charset (case #6), defaulting to \"us-ascii\".");
				mime_charset = html_charset = vmime::charsets::US_ASCII;
			}
		} else if (mime_charset == im_charset_unspec) {
			/* only place to name cset is <meta> */
			lpLogger->Log(EC_LOGLEVEL_DEBUG, "Charset is \"%s\" (case #7).", html_charset.getName().c_str());
			mime_charset = html_charset;
		} else if (html_charset == im_charset_unspec) {
			/* only place to name cset is MIME header */
			lpLogger->Log(EC_LOGLEVEL_DEBUG, "Charset is \"%s\" (case #8).", mime_charset.getName().c_str());
			html_charset = mime_charset;
		}
		mime_charset = vtm_upgrade_charset(mime_charset);
		html_charset = vtm_upgrade_charset(html_charset);

		/* Add secondary candidates and try all in order */
		std::vector<std::string> cs_cand;
		cs_cand.push_back(mime_charset.getName());
		if (!m_dopt.charset_strict_rfc) {
			if (mime_charset != html_charset)
				cs_cand.push_back(html_charset.getName());
			cs_cand.push_back(m_dopt.default_charset);
			cs_cand.push_back(vmime::charsets::US_ASCII);
		}
		int cs_best = renovate_encoding(strHTML, cs_cand);
		if (cs_best < 0) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "HTML part not readable in any charset. Storing as attachment instead.");
			return handleAttachment(vmHeader, vmBody, lpMessage, true);
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
			lpLogger->Log(EC_LOGLEVEL_INFO, "No Win32 CPID for \"%s\" - upgrading text/html MIME body to UTF-8", cs_cand[cs_best].c_str());
		}

		if (bAppendBody && m_mailState.bodyLevel == BODY_HTML && m_mailState.ulLastCP && sCodepage.Value.ul != m_mailState.ulLastCP) {
			// we're appending but the new body part has a different codepage than the previous one. To support this
			// we have to upgrade the old data to utf-8, convert the new data to utf-8 and append that.

			if(m_mailState.ulLastCP != 65001) {
				hr = HrGetCharsetByCP(m_mailState.ulLastCP, &lpszCharset);
				if (hr != hrSuccess) {
					ASSERT(false); // Should not happen since ulLastCP was generated by HrGetCPByCharset()
					goto exit;
				}

				// Convert previous body part to utf-8
				std::string strCurrentHTML;

				hr = Util::ReadProperty(lpMessage, PR_HTML, strCurrentHTML);
				if (hr != hrSuccess)
					goto exit;

				strCurrentHTML = m_converter.convert_to<std::string>("UTF-8", strCurrentHTML, rawsize(strCurrentHTML), lpszCharset);

				hr = Util::WriteProperty(lpMessage, PR_HTML, strCurrentHTML);
				if (hr != hrSuccess)
					goto exit;
			}

			if(sCodepage.Value.ul != 65001) {
				// Convert new body part to utf-8
				strHTML = m_converter.convert_to<std::string>("UTF-8", strHTML, rawsize(strHTML), mime_charset.getName().c_str());
			}

			// Everything is utf-8 now
			sCodepage.Value.ul = 65001;
			mime_charset = "utf-8";
		}

		m_mailState.ulLastCP = sCodepage.Value.ul;

		sCodepage.ulPropTag = PR_INTERNET_CPID;
		HrSetOneProp(lpMessage, &sCodepage);

		// we may have received a text part to append to the HTML body
		if (vmHeader->ContentType()->getValue().dynamicCast<vmime::mediaType>()->getSubType() == vmime::mediaTypes::TEXT_PLAIN) {
			// escape and wrap with <pre> tags
			std::wstring strwBody = m_converter.convert_to<std::wstring>(CHARSET_WCHAR "//IGNORE", strHTML, rawsize(strHTML), mime_charset.getName().c_str());
			strHTML = "<pre>";
			hr = Util::HrTextToHtml(strwBody.c_str(), strHTML, sCodepage.Value.ul);
			if (hr != hrSuccess)
				goto exit;
			strHTML += "</pre>";
		}
	}
	catch (vmime::exception &e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception on html body: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception &e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on html body: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception occurred on html body");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	// create new or reset body
	ulFlags = MAPI_MODIFY;
	if (m_mailState.bodyLevel == BODY_NONE || (m_mailState.bodyLevel < BODY_HTML && !bAppendBody))
		ulFlags |= MAPI_CREATE;

	hr = lpMessage->OpenProperty(PR_HTML, &IID_IStream, STGM_TRANSACTED, ulFlags, (LPUNKNOWN *)&lpHTMLStream);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "OpenProperty PR_HTML failed: %s", GetMAPIErrorMessage(hr));
		goto exit;
	}

	if (bAppendBody) {
		static const LARGE_INTEGER liZero = {{0, 0}};
		hr = lpHTMLStream->Seek(liZero, SEEK_END, NULL);
		if (hr != hrSuccess)
			goto exit;
	}

	hr = lpHTMLStream->Write(strHTML.c_str(), strHTML.length(), &cbWritten);
	if (hr != hrSuccess)		// check cbWritten too?
		goto exit;

	hr = lpHTMLStream->Commit(0);
	if (hr != hrSuccess)
		goto exit;

	m_mailState.bodyLevel = BODY_HTML;
	if (bAppendBody)
		m_mailState.strHTMLBody.append(strHTML);
	else
		swap(strHTML, m_mailState.strHTMLBody);

exit:
	if (lpHTMLStream)
		lpHTMLStream->Release(); 

	return hr;
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
HRESULT VMIMEToMAPI::handleAttachment(vmime::ref<vmime::header> vmHeader, vmime::ref<vmime::body> vmBody, IMessage* lpMessage, bool bAllowEmpty) {
	HRESULT		hr			= hrSuccess;
	IStream		*lpStream	= NULL; 
	LPATTACH	lpAtt		= NULL;
	ULONG		ulAttNr		= 0;
	std::string	strId, strMimeType, strLocation, strTmp;
	std::wstring strLongFilename;
	int			nProps = 0;
	SPropValue	attProps[12];
	vmime::ref<vmime::contentDispositionField> cdf;	// parameters of Content-Disposition header
	vmime::ref<vmime::contentDisposition> cdv;		// value of Content-Disposition header
	vmime::ref<vmime::contentTypeField> ctf;	
	vmime::ref<vmime::mediaType> mt;

	memset(attProps, 0, sizeof(attProps));

	// Create Attach
	hr = lpMessage->CreateAttach(NULL, 0, &ulAttNr, &lpAtt);
	if (hr != hrSuccess)
		goto exit;

	// open stream
	hr = lpAtt->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE|STGM_TRANSACTED,
							MAPI_CREATE|MAPI_MODIFY, (LPUNKNOWN *)&lpStream);
	if (hr != hrSuccess)
		goto exit;

	try {
		// attach adapter, generate in right encoding
		outputStreamMAPIAdapter osMAPI(lpStream);
		cdf = vmHeader->ContentDisposition().dynamicCast<vmime::contentDispositionField>();
		cdv = cdf->getValue().dynamicCast<vmime::contentDisposition>();
		ctf = vmHeader->ContentType().dynamicCast<vmime::contentTypeField>();
		mt = ctf->getValue().dynamicCast<vmime::mediaType>();

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
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Empty attachment found when not allowed, dropping empty attachment.");
				hr = MAPI_E_NOT_FOUND;
				goto exit;
			}
		}

		hr = lpStream->Commit(0);
		if (hr != hrSuccess)
			goto exit;
			
		// Free memory used by the stream
		lpStream->Release();
		lpStream = NULL;

		// set info on attachment
		attProps[nProps].ulPropTag = PR_ATTACH_METHOD;
		attProps[nProps++].Value.ul = ATTACH_BY_VALUE;

		// vmHeader->ContentId() is headerField ->getValue() returns headerFieldValue, which messageId is.
		strId = vmHeader->ContentId()->getValue().dynamicCast<vmime::messageId>()->getId();
		if (!strId.empty()) {
			// only set this property when string is present
			// otherwise, you don't get the 'save attachments' list in the main menu of outlook
			attProps[nProps].ulPropTag = PR_ATTACH_CONTENT_ID_A;
			attProps[nProps++].Value.lpszA = (char*)strId.c_str();
		}

		try {
			strLocation = vmHeader->ContentLocation()->getValue().dynamicCast<vmime::text>()->getConvertedText(MAPI_CHARSET);
		}
		catch (vmime::exceptions::charset_conv_error) { }
		if (!strLocation.empty()) {
			attProps[nProps].ulPropTag = PR_ATTACH_CONTENT_LOCATION_A;
			attProps[nProps++].Value.lpszA = (char*)strLocation.c_str();
		}

		// make hidden when inline, is an image or text, has an content id or location, is an HTML mail,
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
		if (cdf->hasParameter("filename")) {
			strLongFilename = getWideFromVmimeText(vmime::text(cdf->getFilename()));
		} else if (ctf->hasParameter("name")) {
			strLongFilename = getWideFromVmimeText(vmime::text(ctf->getParameter("name")->getValue()));
		} else if (mt->getType() == vmime::mediaTypes::TEXT && mt->getSubType() == "calendar") {
			// already catched in message-in-message code.
			strLongFilename = L"calendar.ics";
		} else {
			// TODO: add guessFilenameFromContentType()
			strLongFilename = L"inline.txt";
		}

		attProps[nProps].ulPropTag = PR_ATTACH_LONG_FILENAME_W;
		attProps[nProps++].Value.lpszW = (WCHAR*)strLongFilename.c_str();

		// outlook internal rendering sequence in RTF bodies. When set
		// to -1, outlook will ignore it, when set to 0 or higher,
		// outlook (mapi) will regenerate the numbering
		attProps[nProps].ulPropTag = PR_RENDERING_POSITION;
		attProps[nProps++].Value.ul = 0;

		try {
			if (!mt->getType().empty() &&
				!mt->getSubType().empty()) {
				strMimeType = mt->getType() + "/" + mt->getSubType();
				// due to a bug in vmime 0.7, the continuation header text can be prefixed in the string, so strip it (easiest way to fix)
				while (strMimeType[0] == '\r' || strMimeType[0] == '\n' || strMimeType[0] == '\t' || strMimeType[0] == ' ')
					strMimeType.erase(0, 1);
				attProps[nProps].ulPropTag = PR_ATTACH_MIME_TAG_A;
				attProps[nProps++].Value.lpszA = (char*)strMimeType.c_str();
			}
		}
		catch (vmime::exceptions::no_such_field) {
		}

		hr = lpAtt->SetProps(nProps, attProps, NULL);
		if (hr != hrSuccess)
			goto exit;
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception on attachment: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on attachment: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception occurred on attachment");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = lpAtt->SaveChanges(0);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (hr != hrSuccess)
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to create attachment");

	if (lpAtt)
		lpAtt->Release();

	if (lpStream)
		lpStream->Release();

	return hr;
}

/**
 * Change a character set name which is unknown to iconv to one that it knows
 * and which is 100% compatible.
 * 
 * @param[in] vmCharset original charset
 * 
 * @return same or compatible charset
 */
namespace charsetHelper {
	static const struct sets {
		const char *original;
		const char *update;
	} fixes[] = {
		{"gb2312", "gb18030"},			// gb18030 is an extended version of gb2312
		{"x-gbk", "gb18030"},			// gb18030 > gbk > gb2312. x-gbk is an alias of gbk, which is not listed in iconv.
		{"ks_c_5601-1987", "cp949"},	// cp949 is euc-kr with UHC extensions
		{"iso-8859-8-i", "iso-8859-8"},	// logical vs visual order, does not matter. http://mirror.hamakor.org.il/archives/linux-il/08-2004/11445.html

		/*
		 * This particular "unicode" is different from iconv's
		 * "unicode" character set. It is UTF-8 content with a UTF-16
		 * BOM (which we can just drop because it carries no
		 * relevant information).
		 */
		{"unicode", "utf-8"}, /* UTF-16 BOM + UTF-8 content */
	};
}

static vmime::charset vtm_upgrade_charset(const vmime::charset &cset)
{
	for (size_t i = 0; i < ARRAY_SIZE(charsetHelper::fixes); ++i)
		if (strcasecmp(charsetHelper::fixes[i].original, cset.getName().c_str()) == 0)
			return charsetHelper::fixes[i].update;

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
		lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to parse HTML document");
		ret = -1;
		goto exit;
	}

	/*
	 * The HTML parser is very forgiving, so lpDoc is almost never %NULL
	 * (only if input buffer is size 0 apparently). But, if we have data
	 * in, for example, UTF-32 encoding, then @root will be NULL.
	 */
	root = xmlDocGetRootElement(lpDoc);
	if (root == NULL) {
		lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to parse HTML document");
		ret = -1;
		goto exit;
	}
	lpNode = find_node(root, "head");
	if (!lpNode) {
		lpLogger->Log(EC_LOGLEVEL_DEBUG, "HTML document contains no HEAD tag");
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
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "HTML4 meta tag found: charset=\"%s\"", lpValue);
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
			lpLogger->Log(EC_LOGLEVEL_DEBUG, "HTML5 meta tag found: charset=\"%s\"", lpValue);
			charset = reinterpret_cast<char *>(lpValue);
			break;
		}
	}
	if (!lpValue) {
		lpLogger->Log(EC_LOGLEVEL_DEBUG, "HTML body does not contain meta charset information");
		goto exit;
	}
	*htmlCharset = charset.size() != 0 ? vtm_upgrade_charset(charset) :
	               vmime::charsets::US_ASCII;
	lpLogger->Log(EC_LOGLEVEL_DEBUG, "HTML charset adjusted to \"%s\"", htmlCharset->getName().c_str());
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

	const std::vector<vmime::ref<const vmime::word> >& words = vmText.getWordList();
	std::vector<vmime::ref<const vmime::word> >::const_iterator i, j;
	for (i = words.begin(); i != words.end(); ++i) {
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
		 * (a) display input as-is:
		 *     if (!ValidateCharset(..))
		 *         ret += m_converter.convert_to<std::wstring>((*i)->generate());
		 * (b) best effort conversion (which we pick) or
		 * (c) substitute by a message that decoding failed.
		 *
		 * We pick (b). However, reinterpreting the input as
		 * m_dopt.default_charset produces the _worst_ possible result,
		 * since codepoints get transcoded into different dingbats.
		 * With ASCII as the forced charset, the user at least gets
		 * consistent placeholders. These placeholders may be the empty
		 * string. (a) is also a good choice, but the "placeholders"
		 * may be longer for not much benefit to the human reader.
		 */
		if (!ValidateCharset(wordCharset.getName().c_str()))
			wordCharset = vmime::charsets::US_ASCII;

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
		for (j = i + 1; j != words.end() && (*j)->getCharset() == wordCharset; ++j, ++i)
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
	LPSPropValue lpMessageClass = NULL;
	ULONG cValues = 0;
	LPSPropValue lpProps = NULL;
	LPSPropValue lpRecProps = NULL;
	ULONG cRecProps = 0;
	ULONG cbConversationIndex = 0;
	LPBYTE lpConversationIndex = NULL;

	PROPMAP_START
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

	hr = HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &lpMessageClass);
	if (hr != hrSuccess)
		goto exit;

	if (strncasecmp(lpMessageClass->Value.lpszA, "IPM.Schedule.Meeting.", strlen( "IPM.Schedule.Meeting." )) == 0)
	{
		// IPM.Schedule.Meeting.*

		SizedSPropTagArray(6, sptaMeetingReqProps) = {6, {PROP_RESPONSESTATUS, PROP_RECURRING, PROP_ATTENDEECRITICALCHANGE, PROP_OWNERCRITICALCHANGE, PR_OWNER_APPT_ID, PR_CONVERSATION_INDEX }};

		hr = lpMessage->GetProps((LPSPropTagArray)&sptaMeetingReqProps, 0, &cValues, &lpProps);
		if(FAILED(hr))
			goto exit;

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
				hr = ScCreateConversationIndex(0, NULL, &cbConversationIndex, &lpConversationIndex);
				if(hr != hrSuccess)
					goto exit;

				lpProps[5].Value.bin.cb = cbConversationIndex;
				lpProps[5].Value.bin.lpb = lpConversationIndex;
			}

			hr = lpMessage->SetProps(6, lpProps, NULL);
			if(hr != hrSuccess)
				goto exit;
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
			hr = lpMessage->GetProps((LPSPropTagArray)&sptaRecProps, 0, &cRecProps, &lpRecProps);
			if(hr != hrSuccess) // Warnings not accepted
				goto exit;
			
			hr = rec.ParseBlob((char *)lpRecProps[0].Value.bin.lpb, (unsigned int)lpRecProps[0].Value.bin.cb, 0);
			if(FAILED(hr))
				goto exit;
			
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
					if(rec.ulPatternType == 0) {
						// Daily
						sMeetingProps[4].Value.i = rec.ulPeriod / 1440; // DayInterval
						sMeetingProps[11].Value.i = 64;					// RecurrenceType
					} else {
						// Every workday, actually a weekly recurrence (weekly every workday)
						sMeetingProps[5].Value.i = 1;					// WeekInterval
						sMeetingProps[8].Value.ul = 62; // Mo-Fri
						sMeetingProps[11].Value.i = 48;	// Weekly	
					}
					break;
				case RF_WEEKLY:
					sMeetingProps[5].Value.i = rec.ulPeriod;			// WeekInterval
					sMeetingProps[8].Value.ul = rec.ulWeekDays;			// DayOfWeekMask
					sMeetingProps[11].Value.i = 48;						// RecurrenceType
					break;
				case RF_MONTHLY:
					sMeetingProps[6].Value.i = rec.ulPeriod;			// MonthInterval
					if(rec.ulPatternType == 3) { // Every Nth [weekday] of the month
						sMeetingProps[5].Value.ul = rec.ulWeekNumber;	// WeekInterval
						sMeetingProps[8].Value.ul = rec.ulWeekDays;		// DayOfWeekMask
						sMeetingProps[11].Value.i = 56;					// RecurrenceType
					} else {
						sMeetingProps[9].Value.ul = 1 << (rec.ulDayOfMonth-1); // day of month 1..31 mask
						sMeetingProps[11].Value.i = 12;					// RecurrenceType
					}
					break;
				case RF_YEARLY:
					sMeetingProps[6].Value.i = rec.ulPeriod;			// YearInterval
					sMeetingProps[7].Value.i = rec.ulPeriod / 12;		// MonthInterval
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
					
					if(rec.ulPatternType == 3) { // Every Nth [weekday] in Month X
						sMeetingProps[5].Value.ul = rec.ulWeekNumber;	// WeekInterval
						sMeetingProps[8].Value.ul = rec.ulWeekDays;		// DayOfWeekMask
						sMeetingProps[11].Value.i = 51;					// RecurrenceType
					} else {
						sMeetingProps[9].Value.ul = 1 << (rec.ulDayOfMonth-1); // day of month 1..31 mask
						sMeetingProps[11].Value.i = 7;					// RecurrenceType
					}
					break;
				default:
					break;
			}
			
			hr = lpMessage->SetProps(14, sMeetingProps, NULL);
			if(hr != hrSuccess)
				goto exit;
		}
	}

exit:
	MAPIFreeBuffer(lpRecProps);
	MAPIFreeBuffer(lpConversationIndex);
	MAPIFreeBuffer(lpMessageClass);
	MAPIFreeBuffer(lpProps);
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
 * Convert an vmime mailbox to an IMAP envelope list part
 * 
 * @param[in] mbox vmime mailbox (email address) to convert
 * 
 * @return string with IMAP envelope list part
 */
std::string VMIMEToMAPI::mailboxToEnvelope(vmime::ref<vmime::mailbox> mbox)
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
	lMBox.push_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");

	lMBox.push_back("NIL");	// at-domain-list (source route) ... whatever that means

	buffer = "\"" + mbox->getEmail() + "\"";
	pos = buffer.find("@");
	if (pos != string::npos)
		buffer.replace(pos, 1, "\" \"");
	lMBox.push_back(buffer);
	if (pos == string::npos)
		lMBox.push_back("NIL");	// domain was missing
	return "(" + kc_join(lMBox, " ") + ")";
}

/** 
 * Convert an vmime addresslist (To/Cc/Bcc) to an IMAP envelope list part.
 * 
 * @param[in] aList vmime addresslist to convert
 * 
 * @return string with IMAP envelope list part
 */
std::string VMIMEToMAPI::addressListToEnvelope(vmime::ref<vmime::addressList> aList)
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
			buffer += mailboxToEnvelope(aList->getAddressAt(i).dynamicCast<vmime::mailbox>());
			lAddr.push_back(buffer);
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
HRESULT VMIMEToMAPI::createIMAPEnvelope(vmime::ref<vmime::message> vmMessage, IMessage* lpMessage)
{
	HRESULT hr = hrSuccess;
	std::string buffer;
	SPropValue sEnvelope;

	PROPMAP_START;
	PROPMAP_NAMED_ID(ENVELOPE, PT_STRING8, PS_EC_IMAP, dispidIMAPEnvelope);
	PROPMAP_INIT(lpMessage);

	buffer = createIMAPEnvelope(vmMessage);

	sEnvelope.ulPropTag = PROP_ENVELOPE;
	sEnvelope.Value.lpszA = (char*)buffer.c_str();

	hr = lpMessage->SetProps(1, &sEnvelope, NULL);
exit: /* label still needed for expansion of PROPMAP_INIT */
	return hr;
}

/** 
 * Create IMAP ENVELOPE() data from a vmime::message.
 * 
 * @param[in] vmMessage message to create envelope for
 * 
 * @return ENVELOPE data
 */
std::string VMIMEToMAPI::createIMAPEnvelope(vmime::ref<vmime::message> vmMessage)
{
	vector<string> lItems;
	vmime::ref<vmime::header> vmHeader = vmMessage->getHeader();
	std::string buffer;
	vmime::utility::outputStreamStringAdapter os(buffer);

	// date
	try {
		vmime::ref<vmime::datetime> date;
		try {
			date = vmHeader->Date()->getValue().dynamicCast<vmime::datetime>();
		} catch (vmime::exception &e) {
			// date must not be empty, so force now() as the timestamp
			date = vmime::create<vmime::datetime>(vmime::datetime::now());
		}
		date->generate(os);
		lItems.push_back("\"" + buffer + "\"");
	} catch (vmime::exception &e) {
		// this is not allowed, but better than nothing
		lItems.push_back("NIL");
	}
	buffer.clear();

	// subject
	try {
		vmHeader->Subject()->getValue()->generate(os);
		// encoded subjects never contain ", so escape won't break those.
		buffer = StringEscape(buffer.c_str(), "\"", '\\');
		lItems.push_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	} catch (vmime::exception &e) {
		lItems.push_back("NIL");
	}
	buffer.clear();

	// from
	try {
		buffer = mailboxToEnvelope(vmHeader->From()->getValue().dynamicCast<vmime::mailbox>());
		lItems.push_back("(" + buffer + ")");
	} catch (vmime::exception &e) {
		// this is not allowed, but better than nothing
		lItems.push_back("NIL");
	}
	buffer.clear();

	// sender
	try {
		buffer = mailboxToEnvelope(vmHeader->Sender()->getValue().dynamicCast<vmime::mailbox>());
		lItems.push_back("(" + buffer + ")");
	} catch (vmime::exception &e) {
		lItems.push_back(lItems.back());
	}
	buffer.clear();

	// reply-to
	try {
		buffer = mailboxToEnvelope(vmHeader->ReplyTo()->getValue().dynamicCast<vmime::mailbox>());
		lItems.push_back("(" + buffer + ")");
	} catch (vmime::exception &e) {
		lItems.push_back(lItems.back());
	}
	buffer.clear();

	// ((to),(to))
	try {
		buffer = addressListToEnvelope(vmHeader->To()->getValue().dynamicCast<vmime::addressList>());
		lItems.push_back(buffer);
	} catch (vmime::exception &e) {
		lItems.push_back("NIL");
	}
	buffer.clear();

	// ((cc),(cc))
	try {
		vmime::ref<vmime::addressList> aList = vmHeader->Cc()->getValue().dynamicCast<vmime::addressList>();
		int aCount = aList->getAddressCount();
		for (int i = 0; i < aCount; ++i)
			buffer += mailboxToEnvelope(aList->getAddressAt(i).dynamicCast<vmime::mailbox>());
		lItems.push_back(buffer.empty() ? "NIL" : "(" + buffer + ")");
	} catch (vmime::exception &e) {
		lItems.push_back("NIL");
	}
	buffer.clear();

	// ((bcc),(bcc))
	try {
		vmime::ref<vmime::addressList> aList = vmHeader->Bcc()->getValue().dynamicCast<vmime::addressList>();
		int aCount = aList->getAddressCount();
		for (int i = 0; i < aCount; ++i)
			buffer += mailboxToEnvelope(aList->getAddressAt(i).dynamicCast<vmime::mailbox>());
		lItems.push_back(buffer.empty() ? "NIL" : "(" + buffer + ")");
	} catch (vmime::exception &e) {
		lItems.push_back("NIL");
	}
	buffer.clear();

	// in-reply-to
	try {
		vmHeader->InReplyTo()->getValue()->generate(os);
		lItems.push_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	} catch (vmime::exception &e) {
		lItems.push_back("NIL");
	}
	buffer.clear();

	// message-id
	try {
		vmHeader->MessageId()->getValue()->generate(os);
		if (buffer.compare("<>") == 0)
			buffer.clear();
		lItems.push_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	} catch (vmime::exception &e) {
		lItems.push_back("NIL");
	}
	buffer.clear();
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
HRESULT VMIMEToMAPI::createIMAPBody(const string &input, vmime::ref<vmime::message> vmMessage, IMessage* lpMessage)
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
HRESULT VMIMEToMAPI::messagePartToStructure(const string &input, vmime::ref<vmime::bodyPart> vmBodyPart, std::string *lpSimple, std::string *lpExtended)
{
	HRESULT hr = hrSuccess;
	list<string> lBody;
	list<string> lBodyStructure;
	vmime::ref<vmime::header> vmHeaderPart = vmBodyPart->getHeader();

	try {
		vmime::ref<vmime::contentTypeField> ctf;
		if (vmHeaderPart->hasField(vmime::fields::CONTENT_TYPE)) {
			// use Content-Type header from part
			ctf = vmHeaderPart->ContentType().dynamicCast<vmime::contentTypeField>();
		} else {
			// create empty default Content-Type header
			ctf = vmime::headerFieldFactory::getInstance()->create("Content-Type", "").dynamicCast<vmime::contentTypeField>();
		}
		vmime::ref<vmime::mediaType> mt = ctf->getValue().dynamicCast<vmime::mediaType>();

		if (mt->getType() == vmime::mediaTypes::MULTIPART) {
			// handle multipart
			// alternative, mixed, related

			if (vmBodyPart->getBody()->getPartCount() == 0)
				return hr;		// multipart without any real parts? let's completely skip this.

			// function please:
			string strBody;
			string strBodyStructure;
			for (int i = 0; i < vmBodyPart->getBody()->getPartCount(); ++i) {
				messagePartToStructure(input, vmBodyPart->getBody()->getPartAt(i), &strBody, &strBodyStructure);
				lBody.push_back(strBody);
				lBodyStructure.push_back(strBodyStructure);
				strBody.clear();
				strBodyStructure.clear();
			}
			// concatenate without spaces, result: ((text)(html))
			strBody = kc_join(lBody, "");
			strBodyStructure = kc_join(lBodyStructure, "");

			lBody.clear();
			lBody.push_back(strBody);

			lBodyStructure.clear();
			lBodyStructure.push_back(strBodyStructure);

			// body:
			//   (<SUB> "subtype")
			// bodystructure:
			//   (<SUB> "subtype" ("boundary" "value") "disposition" "language")
			lBody.push_back("\"" + mt->getSubType() + "\"");
			lBodyStructure.push_back("\"" + mt->getSubType() + "\"");

			lBodyStructure.push_back(parameterizedFieldToStructure(ctf));

			lBodyStructure.push_back(getStructureExtendedFields(vmHeaderPart));

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
		lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to create optimized bodystructure: %s", e.what());
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
HRESULT VMIMEToMAPI::bodyPartToStructure(const string &input, vmime::ref<vmime::bodyPart> vmBodyPart, std::string *lpSimple, std::string *lpExtended)
{
	string strPart;
	list<string> lBody;
	list<string> lBodyStructure;
	string buffer;
	vmime::utility::outputStreamStringAdapter os(buffer);
	vmime::ref<vmime::header> vmHeaderPart = vmBodyPart->getHeader();

	vmime::ref<vmime::contentTypeField> ctf;
	vmime::ref<vmime::mediaType> mt;

	try {
		ctf = vmHeaderPart->findField(vmime::fields::CONTENT_TYPE).dynamicCast<vmime::contentTypeField>();
		mt = ctf->getValue().dynamicCast<vmime::mediaType>();
	}
	catch (vmime::exception &e) {
		// create with text/plain; charset=us-ascii ?
		lBody.push_back("NIL");
		lBodyStructure.push_back("NIL");
		goto nil;
	}

	lBody.push_back("\"" + mt->getType() + "\"");
	lBody.push_back("\"" + mt->getSubType() + "\"");

	// if string == () force add charset.
	lBody.push_back(parameterizedFieldToStructure(ctf));

	try {
		buffer = vmHeaderPart->findField(vmime::fields::CONTENT_ID)->getValue().dynamicCast<vmime::messageId>()->getId();
		lBody.push_back(buffer.empty() ? "NIL" : "\"<" + buffer + ">\"");
	}
	catch (vmime::exception &e) {
		lBody.push_back("NIL");
	}

	try {
		buffer.clear();
		vmHeaderPart->findField(vmime::fields::CONTENT_DESCRIPTION)->getValue()->generate(os);
		lBody.push_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	}
	catch (vmime::exception &e) {
		lBody.push_back("NIL");
	}

	try {
		buffer.clear();
		vmHeaderPart->findField(vmime::fields::CONTENT_TRANSFER_ENCODING)->getValue()->generate(os);
		lBody.push_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	}
	catch (vmime::exception &e) {
		lBody.push_back("NIL");
	}

	if (mt->getType() == vmime::mediaTypes::TEXT) {
		// body part size
		buffer = stringify(vmBodyPart->getBody()->getParsedLength());
		lBody.push_back(buffer);

		// body part number of lines
		buffer = stringify(countBodyLines(input, vmBodyPart->getBody()->getParsedOffset(), vmBodyPart->getBody()->getParsedLength()));
		lBody.push_back(buffer);
	} else {
		// attachment: size only
		buffer = stringify(vmBodyPart->getBody()->getParsedLength());
		lBody.push_back(buffer);
	}

	// up until now, they were the same
	lBodyStructure = lBody;

	if (mt->getType() == vmime::mediaTypes::MESSAGE && mt->getSubType() == vmime::mediaTypes::MESSAGE_RFC822) {
		string strSubSingle;
		string strSubExtended;
		vmime::ref<vmime::message> subMessage = vmime::create<vmime::message>();

		// From RFC:
		// A body type of type MESSAGE and subtype RFC822 contains,
		// immediately after the basic fields, the envelope structure,
		// body structure, and size in text lines of the encapsulated
		// message.

		// envelope eerst, dan message, dan lines
		vmBodyPart->getBody()->getContents()->extractRaw(os); // generate? raw?
		subMessage->parse(buffer);

		lBody.push_back("("+createIMAPEnvelope(subMessage)+")");
		lBodyStructure.push_back("("+createIMAPEnvelope(subMessage)+")");

		// recurse message-in-message
		messagePartToStructure(buffer, subMessage, &strSubSingle, &strSubExtended);

		lBody.push_back(strSubSingle);
		lBodyStructure.push_back(strSubExtended);

		// dus hier nog de line count van vmBodyPart->getBody buffer?
		lBody.push_back(stringify(countBodyLines(buffer, 0, buffer.length())));
	}

nil:
	if (lpSimple)
		*lpSimple = "(" + kc_join(lBody, " ") + ")";

	// just push some NIL's or also inbetween?
	lBodyStructure.push_back("NIL");	// MD5 of body (use Content-MD5 header?)

	lBodyStructure.push_back(getStructureExtendedFields(vmHeaderPart));

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
std::string VMIMEToMAPI::getStructureExtendedFields(vmime::ref<vmime::header> vmHeaderPart)
{
	list<string> lItems;
	string buffer;
	vmime::utility::outputStreamStringAdapter os(buffer);

	// content-disposition header
	try {
		// use findField because we want an exception when missing
		vmime::ref<vmime::contentDispositionField> cdf = vmHeaderPart->findField(vmime::fields::CONTENT_DISPOSITION).dynamicCast<vmime::contentDispositionField>();
		vmime::ref<vmime::contentDisposition> cd = cdf->getValue().dynamicCast<vmime::contentDisposition>();

		lItems.push_back("(\"" + cd->getName() + "\" " + parameterizedFieldToStructure(cdf) + ")");
	}
	catch (vmime::exception &e) {
		lItems.push_back("NIL");
	}

	// language
	lItems.push_back("NIL");

	// location
	try {
		buffer.clear();
		vmHeaderPart->ContentLocation()->getValue()->generate(os);
		lItems.push_back(buffer.empty() ? "NIL" : "\"" + buffer + "\"");
	}
	catch (vmime::exception &e) {
		lItems.push_back("NIL");
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
std::string VMIMEToMAPI::parameterizedFieldToStructure(vmime::ref<vmime::parameterizedHeaderField> vmParamField)
{
	list<string> lParams;
	string buffer;
	vmime::utility::outputStreamStringAdapter os(buffer);

	try {
		for (const auto &param : vmParamField->getParameterList()) {
			lParams.push_back("\"" + param->getName() + "\"");
			param->getValue().generate(os);
			lParams.push_back("\"" + buffer + "\"");
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
	dopt->default_charset = "iso-8859-15";
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
}
