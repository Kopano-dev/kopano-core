/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <kopano/MAPIErrors.h>
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/contentTypeField.hpp>
#include <vmime/parsedMessageAttachment.hpp>
#include <vmime/emptyContentHandler.hpp>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include <mapi.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <edkmdb.h>
#include <kopano/mapiguidext.h>
#include <kopano/mapi_ptr.h>
#include "tnef.h"
#include "ECMapiUtils.h"
#include "MAPIToVMIME.h"
#include "VMIMEToMAPI.h"
#include "inputStreamMAPIAdapter.h"
#include "mapiAttachment.h"
#include "mapiTextPart.h"
#include "rtfutil.h"
#include <kopano/CommonUtil.h>
#include <kopano/stringutil.h>
#include "mapicontact.h"
#include <kopano/Util.h>
#include <kopano/ECLogger.h>
#include <kopano/codepage.h>
#include <kopano/ecversion.h>
#include "ECVMIMEUtils.h"
#include "SMIMEMessage.h"
#include "MAPIToICal.h"

using std::min;
using std::string;
using std::wstring;

namespace KC {

/* These are the properties that we DON'T send through TNEF. Reasons can be:
 *
 * - Privacy (PR_MDB_PROVIDER, etc)
 * - Sent through rfc2822 (PR_SUBJECT, PR_BODY, etc)
 * - Not part of an outgoing message (PR_TRANSPORT_MESSAGE_HEADERS)
 */

// Since UNICODE is defined, the strings will be PT_UNICODE, as required by ECTNEF::AddProps()
static constexpr const SizedSPropTagArray(54, sptaExclude) = {
    54, 	{
        PR_BODY,
        PR_HTML,
        PR_RTF_COMPRESSED,
        PR_RTF_IN_SYNC,
        PR_ACCESS,
        PR_ACCESS_LEVEL,
        PR_CHANGE_KEY,
        PR_CLIENT_SUBMIT_TIME,
        PR_CREATION_TIME,
        PR_DELETE_AFTER_SUBMIT,
        PR_DISPLAY_BCC,
        PR_DISPLAY_CC,
        PR_DISPLAY_TO,
        PR_ENTRYID,
        PR_HASATTACH,
        PR_LAST_MODIFICATION_TIME,
        PR_MAPPING_SIGNATURE,
        PR_MDB_PROVIDER,
        PR_MESSAGE_DELIVERY_TIME,
        PR_MESSAGE_FLAGS,
        PR_MESSAGE_SIZE,
        PR_NORMALIZED_SUBJECT,
        PR_OBJECT_TYPE,
        PR_PARENT_ENTRYID,
        PR_PARENT_SOURCE_KEY,
        PR_PREDECESSOR_CHANGE_LIST,
        PR_RECORD_KEY,
        PR_RTF_SYNC_BODY_COUNT,
        PR_RTF_SYNC_BODY_CRC,
        PR_RTF_SYNC_BODY_TAG,
        PR_RTF_SYNC_PREFIX_COUNT,
        PR_RTF_SYNC_TRAILING_COUNT,
        PR_SEARCH_KEY,
        PR_SENDER_ADDRTYPE,
        PR_SENDER_EMAIL_ADDRESS,
        PR_SENDER_ENTRYID,
        PR_SENDER_NAME,
        PR_SENDER_SEARCH_KEY,
        PR_SENTMAIL_ENTRYID,
        PR_SENT_REPRESENTING_ADDRTYPE,
        PR_SENT_REPRESENTING_EMAIL_ADDRESS,
        PR_SENT_REPRESENTING_ENTRYID,
        PR_SENT_REPRESENTING_NAME,
        PR_SENT_REPRESENTING_SEARCH_KEY,
        PR_SOURCE_KEY,
        PR_STORE_ENTRYID,
        PR_STORE_RECORD_KEY,
        PR_STORE_SUPPORT_MASK,
        PR_SUBJECT,
        PR_SUBJECT_PREFIX,
        PR_SUBMIT_FLAGS,
        PR_TRANSPORT_MESSAGE_HEADERS,
        PR_LAST_MODIFIER_NAME, 		// IMPORTANT: exchange will drop the message if this is included
        PR_LAST_MODIFIER_ENTRYID,   // IMPORTANT: exchange will drop the message if this is included
    }
};

// These are named properties that are mapped to fixed values in Kopano, so
// we can use the ID values instead of using GetIDsFromNames every times
#define PR_EC_USE_TNEF		PROP_TAG(PT_BOOLEAN, 0x8222)
#define PR_EC_SEND_AS_ICAL 	PROP_TAG(PT_BOOLEAN, 0x8000)
#define PR_EC_OUTLOOK_VERSION PROP_TAG(PT_STRING8, 0x81F4)

/**
 * Inits the class with empty/default values.
 */
MAPIToVMIME::MAPIToVMIME() :
	m_parsectx(imopt_default_parsectx())
{
	rand_init();
	imopt_default_sending_options(&sopt);
}

/**
 * @param[in]	lpSession	current mapi session, used to open contact entryids
 * @param[in]	lpAddrBook	global addressbook
 * @param[in]	newlogger	logger object
 * @param[in]	sopt		struct with optional settings to change conversion
 */
MAPIToVMIME::MAPIToVMIME(IMAPISession *lpSession, IAddrBook *lpAddrBook,
    sending_options so) :
	m_parsectx(imopt_default_parsectx()), sopt(so), m_lpSession(lpSession)
{
	rand_init();
	if (lpSession != nullptr && lpAddrBook == nullptr)
		lpSession->OpenAddressBook(0, nullptr, AB_NO_DIALOG, &~m_lpAdrBook);
		// ignore error
	else
		lpAddrBook->QueryInterface(IID_IAddrBook, &~m_lpAdrBook);
}

/**
 * Convert recipient table to vmime recipients.
 *
 * @param[in]	lpMessage	Message with recipients to convert
 * @param[in]	lpVMMessageBuilder messagebuilder to add recipients to
 * @param[in]	charset		Charset to use for fullname encoding of recipient
 * @return	Mapi error code
 */
HRESULT MAPIToVMIME::processRecipients(IMessage *lpMessage, vmime::messageBuilder *lpVMMessageBuilder)
{
	vmime::shared_ptr<vmime::address> vmMailbox;
	object_ptr<IMAPITable> lpRecipientTable;
	rowset_ptr pRows;
	bool fToFound = false, hasRecips = false;
	static constexpr const SizedSPropTagArray(7, sPropRecipColumns) =
		{7, {PR_ENTRYID, PR_EMAIL_ADDRESS_W, PR_DISPLAY_NAME_W,
		PR_RECIPIENT_TYPE, PR_SMTP_ADDRESS_W, PR_ADDRTYPE_W,
		PR_OBJECT_TYPE}};

	auto hr = lpMessage->GetRecipientTable(MAPI_UNICODE | MAPI_DEFERRED_ERRORS, &~lpRecipientTable);
	if (hr != hrSuccess)
		return kc_perror("Unable to open recipient table", hr);
	hr = lpRecipientTable->SetColumns(sPropRecipColumns, 0);
	if (hr != hrSuccess)
		return kc_perror("Unable to set columns on recipient table", hr);
	hr = HrQueryAllRows(lpRecipientTable, nullptr, nullptr, nullptr, 0, &~pRows);
	if (hr != hrSuccess)
		return kc_perror("Unable to read recipient table", hr);
	try {
		for (ULONG i = 0; i < pRows->cRows; ++i) {
			auto pPropRecipType = pRows[i].cfind(PR_RECIPIENT_TYPE);

			if(pPropRecipType == NULL) {
				// getMailBox properties
				auto pPropDispl = pRows[i].cfind(PR_DISPLAY_NAME_W);
				auto pPropAType = pRows[i].cfind(PR_ADDRTYPE_W);
				auto pPropEAddr = pRows[i].cfind(PR_EMAIL_ADDRESS_W);
				ec_log_err("No recipient type set for recipient. DisplayName: %ls, AddrType: %ls, Email: %ls",
					pPropDispl ? pPropDispl->Value.lpszW : L"(none)",
					pPropAType ? pPropAType->Value.lpszW : L"(none)",
					pPropEAddr ? pPropEAddr->Value.lpszW : L"(none)");
				continue;
			}

			hr = getMailBox(&pRows->aRow[i], vmMailbox);
			if (hr == MAPI_E_INVALID_PARAMETER)	// skip invalid addresses
				continue;
			if (hr != hrSuccess)
				return hr; // Logging done in getMailBox

			switch (pPropRecipType->Value.ul) {
			case MAPI_TO:
				lpVMMessageBuilder->getRecipients().appendAddress(vmMailbox);
				fToFound = true;
				hasRecips = true;
				break;
			case MAPI_CC:
				lpVMMessageBuilder->getCopyRecipients().appendAddress(vmMailbox);
				hasRecips = true;
				break;
			case MAPI_BCC:
				lpVMMessageBuilder->getBlindCopyRecipients().appendAddress(vmMailbox);
				hasRecips = true;
				break;
			}
		}

		if (!fToFound) {
			if (!hasRecips && !sopt.no_recipients_workaround) {
				// spooler will exit here, gateway will continue with the workaround
				ec_log_err("No valid recipients found.");
				return MAPI_E_NOT_FOUND;
			}

			// No recipients were in the 'To' .. add 'undisclosed recipients'
			vmime::shared_ptr<vmime::mailboxGroup> undisclosed = vmime::make_shared<vmime::mailboxGroup>(vmime::text("undisclosed-recipients"));
			lpVMMessageBuilder->getRecipients().appendAddress(undisclosed);
		}
	} catch (const vmime::exception &e) {
		ec_log_err("VMIME exception: %s", e.what());
		return MAPI_E_CALL_FAILED;
	} catch (const std::exception &e) {
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
 * Convert an attachment and attach to vmime messageBuilder.
 *
 * @param[in]	lpMessage	Message to open attachments on
 * @param[in]	lpRow		Current attachment to process, row from attachment table
 * @param[in]	charset		Charset to use for filename encoding of attachment
 * @param[in]	lpVMMessageBuilder messagebuilder to add attachment to
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::handleSingleAttachment(IMessage* lpMessage, LPSRow lpRow, vmime::messageBuilder *lpVMMessageBuilder) {
	HRESULT			hr					= hrSuccess;
	object_ptr<IStream> lpStream;
	object_ptr<IAttach> lpAttach;
	memory_ptr<SPropValue> lpContentId, lpContentLocation, lpHidden;
	memory_ptr<SPropValue> lpFilename, lpAMClass, lpAMAttach, lpMIMETag;
	object_ptr<IMessage> lpAttachedMessage;
	vmime::shared_ptr<vmime::utility::inputStream> inputDataStream;
	vmime::shared_ptr<mapiAttachment> vmMapiAttach;
	std::string strContentId, strContentLocation, strBoundary;
	bool bHidden = false, bSendBinary = true;
	sending_options sopt_keep;
	const wchar_t *szFilename = NULL;  // just a reference, don't free
	vmime::mediaType vmMIMEType;

	auto pPropAttachNum = lpRow->cfind(PR_ATTACH_NUM);
	if (pPropAttachNum == NULL) {
		ec_log_err("Attachment in table not correct, no attachment number present.");
		return MAPI_E_NOT_FOUND;
	}

	unsigned int ulAttachmentNum = pPropAttachNum->Value.ul;
	// check PR_ATTACH_METHOD to determine Attachment or email
	unsigned int ulAttachmentMethod = ATTACH_BY_VALUE;
	auto pPropAttachType = lpRow->cfind(PR_ATTACH_METHOD);
	if (pPropAttachType == NULL)
		ec_log_warn("Attachment method not present for attachment %d, assuming default value", ulAttachmentNum);
	else
		ulAttachmentMethod = pPropAttachType->Value.ul;

	if (ulAttachmentMethod == ATTACH_EMBEDDED_MSG) {
		vmime::shared_ptr<vmime::message> vmNewMess;
		std::string strBuff;
		vmime::utility::outputStreamStringAdapter mout(strBuff);

		hr = lpMessage->OpenAttach(ulAttachmentNum, nullptr, MAPI_BEST_ACCESS, &~lpAttach);
		if (hr != hrSuccess) {
			ec_log_err("Could not open message attachment %d: %s (%x)",
				ulAttachmentNum, GetMAPIErrorMessage(hr), hr);
			return hr;
		}
		hr = lpAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_DEFERRED_ERRORS, &~lpAttachedMessage);
		if (hr != hrSuccess) {
			ec_log_err("Could not open data of message attachment %d: %s (%x)",
				ulAttachmentNum, GetMAPIErrorMessage(hr), hr);
			return hr;
		}

		// Check whether we're sending a calendar object
		// if so, we do not need to create a message-in-message object, but just attach the ics file.
		hr = HrGetOneProp(lpAttachedMessage, PR_MESSAGE_CLASS_A, &~lpAMClass);
		if (hr == hrSuccess &&
			strcmp(lpAMClass->Value.lpszA, "IPM.OLE.CLASS.{00061055-0000-0000-C000-000000000046}") == 0)
				// This is an exception message. We might get here because kopano-ical is able to
				// SubmitMessage for Mac Ical meeting requests. The way Outlook does this, is
				// send a message for each exception itself. We just ignore this exception, since
				// it's already in the main ical data on the top level message.
			return hr;

		// sub objects do not necessarily need to have valid recipients, so disable the test
		sopt_keep = sopt;
		sopt.no_recipients_workaround = true;
		sopt.msg_in_msg = true;
		sopt.add_received_date = false;
		
		if(sopt.alternate_boundary) {
		    strBoundary = sopt.alternate_boundary;
		    strBoundary += "_sub_";
		    strBoundary += stringify(ulAttachmentNum);
		    sopt.alternate_boundary = (char *)strBoundary.c_str();
		}

		// recursive processing of embedded message as a new e-mail
		hr = convertMAPIToVMIME(lpAttachedMessage, &vmNewMess);
		if (hr != hrSuccess)
			// Logging has been done by convertMAPIToVMIME()
			return hr;
		sopt = sopt_keep;
		lpVMMessageBuilder->appendAttachment(vmime::make_shared<vmime::parsedMessageAttachment>(vmNewMess));
	} else if (ulAttachmentMethod == ATTACH_BY_VALUE) {
		hr = lpMessage->OpenAttach(ulAttachmentNum, nullptr, MAPI_BEST_ACCESS, &~lpAttach);
		if (hr != hrSuccess) {
			ec_log_err("Could not open attachment %d: %s (%x)",
				ulAttachmentNum, GetMAPIErrorMessage(hr), hr);
			return hr;
		}
	
		if (HrGetOneProp(lpAttach, PR_ATTACH_CONTENT_ID_A, &~lpContentId) == hrSuccess)
			strContentId = lpContentId->Value.lpszA;
		if (HrGetOneProp(lpAttach, PR_ATTACH_CONTENT_LOCATION_A, &~lpContentLocation) == hrSuccess)
			strContentLocation = lpContentLocation->Value.lpszA;
		if (HrGetOneProp(lpAttach, PR_ATTACHMENT_HIDDEN, &~lpHidden) == hrSuccess)
			bHidden = lpHidden->Value.b;

        // Optimize: if we're only outputting headers, we needn't bother with attachment data
        if(sopt.headers_only)
            CreateStreamOnHGlobal(nullptr, true, &~lpStream);
        else {
            hr = lpAttach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0, MAPI_DEFERRED_ERRORS, &~lpStream);
            if (hr != hrSuccess) {
				ec_log_err("Could not open data of attachment %d: %s (%x)",
					ulAttachmentNum, GetMAPIErrorMessage(hr), hr);
			return hr;
            }
        }
        	
		try {
			// add ref to lpStream
			if (lpStream != nullptr)
				inputDataStream = vmime::make_shared<inputStreamMAPIAdapter>(lpStream);

			// Set filename
			szFilename = L"data.bin";
			if (HrGetOneProp(lpAttach, PR_ATTACH_LONG_FILENAME_W, &~lpFilename) == hrSuccess)
				szFilename = lpFilename->Value.lpszW;
			else if (HrGetOneProp(lpAttach, PR_ATTACH_FILENAME_W, &~lpFilename) == hrSuccess)
				szFilename = lpFilename->Value.lpszW;
            
            // Set MIME type
            parseMimeTypeFromFilename(szFilename, &vmMIMEType, &bSendBinary);
			if (HrGetOneProp(lpAttach, PR_ATTACH_MIME_TAG_A, &~lpMIMETag) == hrSuccess)
				vmMIMEType = lpMIMETag->Value.lpszA;

			static const char absent_note[] = "Attachment is missing.";
			vmime::mediaType note_type("text/plain");

			// inline attachments: Only make attachments inline if they are hidden and have a content-id or content-location.
			if ((!strContentId.empty() || !strContentLocation.empty()) && bHidden &&
				lpVMMessageBuilder->getTextPart()->getType() == vmime::mediaType(vmime::mediaTypes::TEXT, vmime::mediaTypes::TEXT_HTML))
			{
				// add inline object to html part
				vmime::mapiTextPart& textPart = dynamic_cast<vmime::mapiTextPart&>(*lpVMMessageBuilder->getTextPart());
				// had szFilename .. but how, on inline?
				// @todo find out how Content-Disposition receives highchar filename... always UTF-8?
				if (inputDataStream != nullptr)
					textPart.addObject(vmime::make_shared<vmime::streamContentHandler>(inputDataStream, 0), vmime::encoding("base64"), vmMIMEType, strContentId, string(), strContentLocation);
				else
					textPart.addObject(vmime::make_shared<vmime::stringContentHandler>(absent_note), vmime::encoding("base64"), note_type, strContentId, std::string(), strContentLocation);
			} else if (inputDataStream != nullptr) {
				vmMapiAttach = vmime::make_shared<mapiAttachment>(vmime::make_shared<vmime::streamContentHandler>(inputDataStream, 0),
				               bSendBinary ? vmime::encoding("base64") : vmime::encoding("quoted-printable"),
				               vmMIMEType, strContentId,
				               vmime::word(m_converter.convert_to<string>(m_strCharset.c_str(), szFilename, rawsize(szFilename), CHARSET_WCHAR), m_vmCharset));

				// add to message (copies pointer, not data)
				lpVMMessageBuilder->appendAttachment(vmMapiAttach); 
			} else {
				vmMapiAttach = vmime::make_shared<mapiAttachment>(vmime::make_shared<vmime::stringContentHandler>(absent_note),
				               bSendBinary ? vmime::encoding("base64") : vmime::encoding("quoted-printable"),
				               note_type, strContentId,
				               vmime::word(m_converter.convert_to<std::string>(m_strCharset.c_str(), szFilename, rawsize(szFilename), CHARSET_WCHAR), m_vmCharset));
				lpVMMessageBuilder->appendAttachment(vmMapiAttach);
			}
		} catch (const vmime::exception &e) {
			ec_log_err("VMIME exception: %s", e.what());
			return MAPI_E_CALL_FAILED;
		} catch (const std::exception &e) {
			ec_log_err("STD exception on attachment: %s", e.what());
			return MAPI_E_CALL_FAILED;
		}
		catch (...) {
			ec_log_err("Unknown generic exception occurred on attachment");
			return MAPI_E_CALL_FAILED;
		}
	} else if (ulAttachmentMethod == ATTACH_OLE) {
	    // Ignore ATTACH_OLE attachments, they are handled in handleTNEF()
	} else {
		ec_log_err("Attachment %d contains invalid method %d.", ulAttachmentNum, ulAttachmentMethod);
		hr = MAPI_E_INVALID_PARAMETER;
	}
	// ATTN: lpMapiAttach are linked in the VMMessageBuilder. The VMMessageBuilder will delete() it.
	return hr;
}

/**
 * Return a MIME type for the filename of an attachment.
 *
 * The MIME type is found using the extension. Also returns if the
 * attachment should be attached in text or binary mode. Currently
 * only edifact files (.edi) are forced as text attachment.
 *
 * @param[in]	strFilename	Widechar filename to find mimetype for
 * @param[out]	lpMT		mimetype in vmime class
 * @param[out]	lpbSendBinary	flag to specify if the attachment should be in binary or forced text format.
 * @return always hrSuccess
 *
 * @todo Use /etc/mime.types to find more mime types for extensions
 */
HRESULT MAPIToVMIME::parseMimeTypeFromFilename(std::wstring strFilename, vmime::mediaType *lpMT, bool *lpbSendBinary)
{
	// to lowercase
	transform(strFilename.begin(), strFilename.end(), strFilename.begin(), ::towlower);
	auto strExt = m_converter.convert_to<std::string>(m_strCharset.c_str(), strFilename, rawsize(strFilename), CHARSET_WCHAR);
	strExt.erase(0, strExt.find_last_of(".")+1);
	auto strMedType = ext_to_mime_type(strExt.c_str());
	*lpMT = vmime::mediaType(strMedType);
	*lpbSendBinary = strcmp(strMedType, "application/edifact") != 0;
	return hrSuccess;
}

/**
 * Loops through the attachment table and copies all attachments to vmime object
 *
 * @param[in]	lpMessage	Message to open attachments on
 * @param[in]	charset		Charset to use for filename encoding of attachment
 * @param[in]	lpVMMessageBuilder messagebuilder to add attachments to
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::handleAttachments(IMessage* lpMessage, vmime::messageBuilder *lpVMMessageBuilder) {
	rowset_ptr pRows;
	object_ptr<IMAPITable> lpAttachmentTable;
	static constexpr const SizedSSortOrderSet(1, sosRTFSeq) =
		{1, 0, 0, {{PR_RENDERING_POSITION, TABLE_SORT_ASCEND}}};

	// get attachment table
	auto hr = lpMessage->GetAttachmentTable(0, &~lpAttachmentTable);
	if (hr != hrSuccess)
		return kc_perror("Unable to open attachment table", hr);
	hr = HrQueryAllRows(lpAttachmentTable, nullptr, nullptr, sosRTFSeq, 0, &~pRows);
	if (hr != hrSuccess)
		return kc_perror("Unable to fetch rows of attachment table", hr);
	
	for (ULONG i = 0; i < pRows->cRows; ++i) {
		// if one attachment fails, we're not sending what the user intended to send so abort. Logging was done in handleSingleAttachment()
		hr = handleSingleAttachment(lpMessage, &pRows->aRow[i], lpVMMessageBuilder);
		if (sopt.ignore_missing_attachments && hr == MAPI_E_NOT_FOUND)
			continue;
		if (hr != hrSuccess)
			return hr;
	} 
	return hrSuccess;
}

/**
 * Force the boundary string to a predefined value.
 *
 * Used for gateway, because vmime will otherwise always use a newly
 * generated boundary string. This is not good for consistency in the
 * IMAP gateway. This is done recursively for all multipart bodies in
 * the e-mail.
 *
 * @param[in]	vmHeader	The header block to find multipart Content-Type header in, and modify.
 * @param[in]	vmBody		vmBody part of vmHeader. Used to recurse for more boundaries to change.
 * @param[in]	boundary	new boundary string to use.
 * @return Always hrSuccess
 */
HRESULT MAPIToVMIME::setBoundaries(vmime::shared_ptr<vmime::header> vmHeader,
    vmime::shared_ptr<vmime::body> vmBody, const std::string& boundary)
{
	if (!vmHeader->hasField(vmime::fields::CONTENT_TYPE))
		return hrSuccess;

	auto vmCTF = vmime::dynamicCast<vmime::contentTypeField>(vmHeader->findField(vmime::fields::CONTENT_TYPE));
	if (vmime::dynamicCast<vmime::mediaType>(vmCTF->getValue())->getType() != vmime::mediaTypes::MULTIPART)
		return hrSuccess;
	// This is a multipart, so set the boundary for this part
	vmCTF->setBoundary(boundary);
	// Set boundaries on children also
	for (size_t i = 0; i < vmBody->getPartCount(); ++i) {
		std::ostringstream os;
		vmime::shared_ptr<vmime::bodyPart> vmBodyPart = vmBody->getPartAt(i);
		os << boundary << "_" << i;
		setBoundaries(vmBodyPart->getHeader(), vmBodyPart->getBody(), os.str());
	}
	return hrSuccess;
}

/**
 * Build a normal vmime message from a MAPI lpMessage.
 *
 * @param[in]	lpMessage		MAPI message to convert
 * @param[in]	bSkipContent	set to true if only the headers of the e-mail are required to obtain
 * @param[in]	charset			Charset to convert message in, valid for headers and bodies.
 * @param[out]	lpvmMessage		vmime message version of lpMessage
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::BuildNoteMessage(IMessage *lpMessage,
    vmime::shared_ptr<vmime::message> *lpvmMessage, unsigned int flags)
{
	memory_ptr<SPropValue> lpDeliveryDate, lpTransportHeaders, prop;
	vmime::messageBuilder   vmMessageBuilder;
	vmime::shared_ptr<vmime::message> vmMessage;

	// construct the message..
	try {
		// messageBuilder is body and simple headers only (to/cc/subject/...)
		auto hr = fillVMIMEMail(lpMessage, flags & MTV_SKIP_CONTENT, &vmMessageBuilder);
		if (hr != hrSuccess)
			return hr; // Logging has been done in fillVMIMEMail()

		vmMessage = vmMessageBuilder.construct();
		// message builder has set Date header to now(). this will be overwritten.
		auto vmHeader = vmMessage->getHeader();
		hr = handleExtraHeaders(lpMessage, vmHeader, flags);
		if (hr!= hrSuccess)
			return hr;

		// from/sender headers
		hr = handleSenderInfo(lpMessage, vmHeader);
		if (hr!= hrSuccess)
			return hr;

		// add reply to email, ignore errors
		hr = handleReplyTo(lpMessage, vmHeader);
		if (hr != hrSuccess) {
			ec_log_warn("Unable to set reply-to address");
			hr = hrSuccess;
		}

		// Set consistent boundary (optional)
		if (sopt.alternate_boundary != nullptr)
			setBoundaries(vmMessage->getHeader(), vmMessage->getBody(), sopt.alternate_boundary);

		// If we're sending a msg-in-msg, use the original date of that message
		if (sopt.msg_in_msg && HrGetOneProp(lpMessage, PR_MESSAGE_DELIVERY_TIME, &~lpDeliveryDate) == hrSuccess)
			vmHeader->Date()->setValue(FiletimeTovmimeDatetime(lpDeliveryDate->Value.ft));
		else if (HrGetOneProp(lpMessage, PR_LAST_MODIFICATION_TIME, &~prop) == hrSuccess)
			vmHeader->Date()->setValue(FiletimeTovmimeDatetime(prop->Value.ft));
		
		// Regenerate some headers if available (basically a copy of the headers in
		// PR_TRANSPORT_MESSAGE_HEADERS)
		// currently includes: Received*, Return-Path, List* and Precedence.
		// New e-mails should not have this property.
		if (HrGetOneProp(lpMessage, PR_TRANSPORT_MESSAGE_HEADERS_A, &~lpTransportHeaders) == hrSuccess &&
		    lpTransportHeaders != nullptr) {
			try {
				int j=0;
				vmime::header headers;
				std::string strHeaders = lpTransportHeaders->Value.lpszA;
				strHeaders += "\r\n\r\n";
				headers.parse(strHeaders);
				
				for (size_t i = 0; i < headers.getFieldCount(); ++i) {
					vmime::shared_ptr<vmime::headerField> vmField = headers.getFieldAt(i);
					std::string name = vmField->getName();

					// Received checks start of string to accept Received-SPF
					if (strncasecmp(name.c_str(), vmime::fields::RECEIVED, strlen(vmime::fields::RECEIVED)) == 0 ||
					    strcasecmp(name.c_str(), vmime::fields::RETURN_PATH) == 0)
						// Insert in same order at start of headers
						vmHeader->insertFieldBefore(j++, vmField);
					else if (strncasecmp(name.c_str(), "list-", strlen("list-")) == 0 ||
					    strcasecmp(name.c_str(), "precedence") == 0)
						// Just append at the end of this list, order is not important
						vmHeader->appendField(vmime::dynamicCast<vmime::headerField>(vmField->clone()));
				}
			} catch (const vmime::exception &e) {
				ec_log_warn("VMIME exception adding extra headers: %s", e.what());
			}
			catch(...) { 
				// If we can't parse, just ignore
			}
		}

		// POP3 wants the delivery date as received header to correctly show in outlook
		if (sopt.add_received_date && lpDeliveryDate) {
			auto hff = vmime::headerFieldFactory::getInstance();
			vmime::shared_ptr<vmime::headerField> rf = hff->create(vmime::fields::RECEIVED);
			vmime::dynamicCast<vmime::relay>(rf->getValue())->setDate(FiletimeTovmimeDatetime(lpDeliveryDate->Value.ft));
			// set received header at the start
			vmHeader->insertFieldBefore(0, rf);
		}

		// no hr checking
		handleXHeaders(lpMessage, vmHeader, flags);
	} catch (const vmime::exception &e) {
		ec_log_err("VMIME exception: %s", e.what());
		return MAPI_E_CALL_FAILED; // set real error
	} catch (const std::exception &e) {
		ec_log_err("STD exception on note message: %s", e.what());
		return MAPI_E_CALL_FAILED; // set real error
	}
	catch (...) {
		ec_log_err("Unknown generic exception on note message");
		return MAPI_E_CALL_FAILED;
	}

	*lpvmMessage = std::move(vmMessage);
	return hrSuccess;
}

/**
 * Build an MDN vmime message from a MAPI lpMessage.
 *
 * MDN (Mail Disposition Notification) is a read receipt message,
 * which notifies a sender that the e-mail was read.
 *
 * @param[in]	lpMessage		MAPI message to convert
 * @param[in]	charset			Charset to convert message in, valid for headers and bodies.
 * @param[out]	lpvmMessage		vmime message version of lpMessage
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::BuildMDNMessage(IMessage *lpMessage,
    vmime::shared_ptr<vmime::message> *lpvmMessage)
{
	HRESULT				hr = hrSuccess;
	vmime::shared_ptr<vmime::message> vmMessage;
	object_ptr<IStream> lpBodyStream;
	memory_ptr<SPropValue> lpiNetMsgId, lpMsgClass, lpSubject;
	object_ptr<IMAPITable> lpRecipientTable;
	vmime::mailbox		expeditor; // From
	string strMDNText, reportingUA, strActionMode;
	vmime::disposition	dispo;
	std::vector<string>	reportingUAProducts; //empty
	vmime::shared_ptr<vmime::address> vmRecipientbox;
	std::wstring		strOut;

	// sender information
	std::wstring strEmailAdd, strName, strType;
	std::wstring strRepEmailAdd, strRepName, strRepType;

	if (lpMessage == nullptr || m_lpAdrBook == nullptr ||
	    lpvmMessage == nullptr)
		return MAPI_E_INVALID_OBJECT;

	try {
		rowset_ptr pRows;
		auto vmMsgOriginal = vmime::make_shared<vmime::message>();

		// Create original vmime message
		if (HrGetOneProp(lpMessage, PR_INTERNET_MESSAGE_ID_A, &~lpiNetMsgId) == hrSuccess) {
			vmMsgOriginal->getHeader()->OriginalMessageId()->setValue(lpiNetMsgId->Value.lpszA);
			vmMsgOriginal->getHeader()->MessageId()->setValue(lpiNetMsgId->Value.lpszA);
		}
		
		// Create Recipient
		hr = lpMessage->GetRecipientTable(MAPI_DEFERRED_ERRORS, &~lpRecipientTable);
		if (hr != hrSuccess)
			return kc_perror("Unable to open MDN recipient table", hr);
		hr = HrQueryAllRows(lpRecipientTable, nullptr, nullptr, nullptr, 0, &~pRows);
		if (hr != hrSuccess)
			return kc_perror("Unable to read MDN recipient table", hr);
		if (pRows->cRows == 0) {
			if (!sopt.no_recipients_workaround) {
				ec_log_err("No MDN recipient found");
				return MAPI_E_NOT_FOUND;
			}
			// no recipient, but need to continue ... is this correct??
			vmRecipientbox = vmime::make_shared<vmime::mailbox>(string("undisclosed-recipients"));
		} else {
			hr = getMailBox(&pRows->aRow[0], vmRecipientbox);
			if (hr != hrSuccess)
				return hr; // Logging done in getMailBox
		}

		// if vmRecipientbox is a vmime::mailboxGroup the dynamicCast to vmime::mailbox failed,
		// so never send a MDN to a group.
		if (vmRecipientbox->isGroup()) {
			ec_log_err("Not possible to send a MDN to a group");
			return MAPI_E_CORRUPT_DATA;
		}

		vmime::mdn::sendableMDNInfos mdnInfos(vmMsgOriginal, *vmime::dynamicCast<vmime::mailbox>(vmRecipientbox).get());
		
		//FIXME: Get the ActionMode and sending mode from the property 0x0081001E.
		//		And the type in property 0x0080001E.

		// Vmime is broken, The RFC 3798 says: "The action type can be 'manual-action' or 'automatic-action'"
		// but VMime can set only 'manual' or 'automatic' so should be add the string '-action'
		strActionMode = vmime::dispositionActionModes::MANUAL;
		strActionMode+= "-action";

		dispo.setActionMode(strActionMode);
		dispo.setSendingMode(vmime::dispositionSendingModes::SENT_MANUALLY);

		if (HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMsgClass) != hrSuccess) {
			ec_log_err("MDN message has no class.");
			return MAPI_E_CORRUPT_DATA;
		}

		if(strcasecmp(lpMsgClass->Value.lpszA, "REPORT.IPM.Note.IPNNRN") == 0)
			dispo.setType(vmime::dispositionTypes::DELETED);
		else // if(strcasecmp(lpMsgClass->Value.lpszA, "REPORT.IPM.Note.IPNRN") == 0)
			dispo.setType(vmime::dispositionTypes::DISPLAYED);		
	
		strMDNText.clear();// Default Empty body
		hr = lpMessage->OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, &~lpBodyStream);
		if (hr == hrSuccess) {
			std::wstring strBuffer;
			hr = Util::HrStreamToString(lpBodyStream, strBuffer);
			if (hr != hrSuccess) {
				ec_log_err("Unable to read MDN message body.");
				return hr;
			}
			strMDNText = m_converter.convert_to<string>(m_strCharset.c_str(), strBuffer, rawsize(strBuffer), CHARSET_WCHAR);
		}

		// Store owner, actual sender
		hr = HrGetAddress(m_lpAdrBook, lpMessage, PR_SENDER_ENTRYID, PR_SENDER_NAME_W, PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS_W, strName, strType, strEmailAdd);
		if (hr != hrSuccess)
			return kc_perror("Unable to get MDN sender information", hr);
		// Ignore errors here and let strRep* untouched
		HrGetAddress(m_lpAdrBook, lpMessage, PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, strRepName, strRepType, strRepEmailAdd);

		expeditor.setEmail(m_converter.convert_to<string>(strEmailAdd));
		if(!strName.empty())
			expeditor.setName(getVmimeTextFromWide(strName));
		else
			expeditor.setName(getVmimeTextFromWide(strEmailAdd));

		//Create MDN message, does vmime convert again with charset, or was this wrong before?
		vmMessage = vmime::mdn::MDNHelper::buildMDN(mdnInfos, strMDNText, m_vmCharset, expeditor, dispo, reportingUA, reportingUAProducts);

		// rewrite subject
		if (HrGetOneProp(lpMessage, PR_SUBJECT_W, &~lpSubject) == hrSuccess) {
			removeEnters(lpSubject->Value.lpszW);

			strOut = lpSubject->Value.lpszW;

			vmMessage->getHeader()->Subject()->setValue(getVmimeTextFromWide(strOut));
		}
		
		if (!strRepEmailAdd.empty()) {
			vmMessage->getHeader()->Sender()->setValue(expeditor);

			if (strRepName.empty() || strRepName == strRepEmailAdd) 
				vmMessage->getHeader()->From()->setValue(vmime::mailbox(m_converter.convert_to<string>(strRepEmailAdd)));
			else
				vmMessage->getHeader()->From()->setValue(vmime::mailbox(getVmimeTextFromWide(strRepName), m_converter.convert_to<string>(strRepEmailAdd)));
		}
	} catch (const vmime::exception &e) {
		ec_log_err("VMIME exception: %s", e.what());
		return MAPI_E_CALL_FAILED; // set real error
	} catch (const std::exception &e) {
		ec_log_err("STD exception on MDN message: %s", e.what());
		return MAPI_E_CALL_FAILED; // set real error
	}
	catch (...) {
		ec_log_err("Unknown generic exception on MDN message");
		return MAPI_E_CALL_FAILED;
	}

	*lpvmMessage = std::move(vmMessage);
	return hrSuccess;
}

/**
 * Returns a description of the conversion error that occurred.
 *
 * @return Description of the error if convertMAPIToVMIME returned an error.
 */
std::wstring MAPIToVMIME::getConversionError(void) const
{
	return m_strError;
}

/**
 * Entry point to start the conversion from lpMessage to message.
 *
 * @param[in]	lpMessage		MAPI message to convert
 * @param[out]	lpvmMessage		message to send to SMTP or Gateway client
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::convertMAPIToVMIME(IMessage *lpMessage,
    vmime::shared_ptr<vmime::message> *lpvmMessage, unsigned int flags)
{
	memory_ptr<SPropValue> lpInternetCPID, lpMsgClass;
	vmime::shared_ptr<vmime::message> vmMessage;
	const char *lpszCharset = NULL;
	std::unique_ptr<char[]> lpszRawSMTP;
	object_ptr<IMAPITable> lpAttachmentTable;
	const SPropValue *lpPropAttach = nullptr;
	object_ptr<IAttach> lpAttach;
	object_ptr<IStream> lpStream;
	STATSTG					sStreamStat;
	static constexpr const SizedSPropTagArray(2, sPropAttachColumns) =
		{2, { PR_ATTACH_NUM, PR_ATTACH_MIME_TAG}};

	if (HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMsgClass) != hrSuccess) {
		auto hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpMsgClass);
		if (hr != hrSuccess)
			return hr;
		lpMsgClass->ulPropTag = PR_MESSAGE_CLASS_A;
		lpMsgClass->Value.lpszA = const_cast<char *>("IPM.Note");
	}

	// Get the outgoing charset we want to be using
	if (HrGetOneProp(lpMessage, PR_INTERNET_CPID, &~lpInternetCPID) == hrSuccess &&
		HrGetCharsetByCP(lpInternetCPID->Value.ul, &lpszCharset) == hrSuccess)
	{
		m_strHTMLCharset = lpszCharset;
		m_vmCharset = m_strHTMLCharset;
	} else {
		// default to UTF-8 if not set
		m_vmCharset = MAPI_CHARSET_STRING;
	}

	if (strncasecmp(lpMsgClass->Value.lpszA, "IPM.Note", 8) && strncasecmp(lpMsgClass->Value.lpszA, "REPORT.IPM.Note", 15)) {
		// Outlook sets some other incorrect charset for meeting requests and such,
		// so for non-email we upgrade this to UTF-8
		m_vmCharset = MAPI_CHARSET_STRING;
	} else if (m_vmCharset == "us-ascii") {
		// silently upgrade, since recipients and attachment filenames may contain high-chars, while the body does not.
		if (sopt.charset_upgrade && strlen(sopt.charset_upgrade))
			m_vmCharset = sopt.charset_upgrade;
		else
			m_vmCharset = "windows-1252";
	}

	// Add iconv tag to convert non-exising chars without a fuss
	m_strCharset = m_vmCharset.getName() + "//TRANSLIT";
	if ((strcasecmp(lpMsgClass->Value.lpszA, "REPORT.IPM.Note.IPNNRN") == 0 ||
		strcasecmp(lpMsgClass->Value.lpszA, "REPORT.IPM.Note.IPNRN") == 0)
		)
	{
		// Create a read receipt message
		auto hr = BuildMDNMessage(lpMessage, &vmMessage);
		if(hr != hrSuccess)
			return hr;
	} else if ((strcasecmp(lpMsgClass->Value.lpszA, "IPM.Note.SMIME.MultiPartSigned") == 0) ||
			   (strcasecmp(lpMsgClass->Value.lpszA, "IPM.Note.SMIME") == 0))
	{
		rowset_ptr lpRows;
		// - find attachment, and convert to char, and place in lpszRawSMTP
		// - normal convert the message, but only from/to headers and such .. nothing else
		auto hr = lpMessage->GetAttachmentTable(0, &~lpAttachmentTable);
		if (hr != hrSuccess)
			return kc_perror("Could not get attachment table of signed attachment", hr);
		// set columns to get pr attach mime tag and pr attach num only.
		hr = lpAttachmentTable->SetColumns(sPropAttachColumns, 0);
		if (hr != hrSuccess)
			return kc_perror("Could set table contents of attachment table of signed attachment", hr);
		hr = HrQueryAllRows(lpAttachmentTable, nullptr, nullptr, nullptr, 0, &~lpRows);
		if (hr != hrSuccess)
			return kc_perror("Could not get table contents of attachment table of signed attachment", hr);
		if (lpRows->cRows != 1)
			goto normal;
		lpPropAttach = lpRows[0].cfind(PR_ATTACH_MIME_TAG);
		if (!lpPropAttach)
			goto normal;
		lpPropAttach = lpRows[0].cfind(PR_ATTACH_NUM);
		if (!lpPropAttach)
			goto normal;
		hr = lpMessage->OpenAttach(lpPropAttach->Value.ul, nullptr, MAPI_BEST_ACCESS, &~lpAttach);
		if (hr != hrSuccess)
			return kc_perror("Could not open signed attachment", hr);
		hr = lpAttach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0, MAPI_DEFERRED_ERRORS, &~lpStream);
		if (hr != hrSuccess)
			return kc_perror("Could not open data of signed attachment", hr);
		hr = lpStream->Stat(&sStreamStat, 0);
		if (hr != hrSuccess)
			return kc_perror("Could not find size of signed attachment", hr);
		lpszRawSMTP = make_unique_nt<char[]>(static_cast<ULONG>(sStreamStat.cbSize.QuadPart) + 1);
		if (lpszRawSMTP == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		hr = lpStream->Read(lpszRawSMTP.get(), (ULONG)sStreamStat.cbSize.QuadPart, NULL);
		if (hr != hrSuccess)
			return kc_perror("Could not read data of signed attachment", hr);
		lpszRawSMTP[(ULONG)sStreamStat.cbSize.QuadPart] = '\0';

		// build the message, but without the bodies and attachments
		hr = BuildNoteMessage(lpMessage, &vmMessage, flags | MTV_SKIP_CONTENT);
		if (hr != hrSuccess)
			return hr;

		// remove excess headers
		auto field = vmMessage->getHeader()->findField(vmime::fields::CONTENT_TYPE);
		if (field != nullptr)
			vmMessage->getHeader()->removeField(field);
		field = vmMessage->getHeader()->findField(vmime::fields::CONTENT_TRANSFER_ENCODING);
		if (field != nullptr)
			vmMessage->getHeader()->removeField(field);

		if (strcasecmp(lpMsgClass->Value.lpszA, "IPM.Note.SMIME") != 0) {
			auto vmSMIMEMessage = vmime::make_shared<SMIMEMessage>();
			
			// not sure why this was needed, and causes problems, eg ZCP-12994.
			//vmMessage->getHeader()->removeField(vmMessage->getHeader()->findField(vmime::fields::MIME_VERSION));
			
			*vmSMIMEMessage->getHeader() = *vmMessage->getHeader();
			vmSMIMEMessage->setSMIMEBody(lpszRawSMTP.get());
			vmMessage = vmSMIMEMessage;
		} else {
			// encoded mail, set data as only mail body
			memory_ptr<MAPINAMEID> lpNameID;
			memory_ptr<SPropTagArray> lpPropTags;
			memory_ptr<SPropValue> lpPropContentType;
			const char *lpszContentType = NULL;

			hr = MAPIAllocateBuffer(sizeof(MAPINAMEID), &~lpNameID);
			if (hr != hrSuccess)
				return kc_perror("Not enough memory", hr);
			lpNameID->lpguid = (GUID*)&PS_INTERNET_HEADERS;
			lpNameID->ulKind = MNID_STRING;
			lpNameID->Kind.lpwstrName = const_cast<wchar_t *>(L"Content-Type");

			hr = lpMessage->GetIDsFromNames(1, &+lpNameID, MAPI_CREATE, &~lpPropTags);
			if (hr != hrSuccess)
				return kc_perror("Unable to read encrypted mail properties", hr);
			if (HrGetOneProp(lpMessage, CHANGE_PROP_TYPE(lpPropTags->aulPropTag[0], PT_STRING8), &~lpPropContentType) == hrSuccess)
				lpszContentType = lpPropContentType->Value.lpszA;
			else
				// default, or exit?
				lpszContentType = "application/x-pkcs7-mime;smime-type=enveloped-data;name=smime.p7m";

			vmMessage->getHeader()->ContentType()->parse(m_parsectx, lpszContentType);
			// copy via string so we can set the size of the string since it's binary
			vmime::string inString(lpszRawSMTP.get(), (size_t)sStreamStat.cbSize.QuadPart);
			vmMessage->getBody()->setContents(vmime::make_shared<vmime::stringContentHandler>(inString));

			// vmime now encodes the body too, so I don't have to
			vmMessage->getHeader()->appendField(vmime::headerFieldFactory::getInstance()->create(vmime::fields::CONTENT_TRANSFER_ENCODING, "base64"));
		}
	} else {
normal:
		// Create default
		auto hr = BuildNoteMessage(lpMessage, &vmMessage, flags);
		if (hr != hrSuccess)
			return hr;
	}

	*lpvmMessage = std::move(vmMessage);
	return hrSuccess;
}

/**
 * Basic conversion of mapi message to messageBuilder.
 *
 * @param[in]	lpMessage		MAPI message to convert
 * @param[in]	bSkipContent	set to true if only the headers of the e-mail are required to obtain
 * @param[out]	lpVMMessageBuilder vmime messagebuilder, used to construct the mail later.
 * @param[in]	charset			Charset to convert message in, valid for headers and bodies.
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::fillVMIMEMail(IMessage *lpMessage, bool bSkipContent, vmime::messageBuilder *lpVMMessageBuilder) {
	std::wstring	strOut;
	memory_ptr<SPropValue> lpSubject;
	eBestBody bestBody = plaintext;

	try {
		if (HrGetOneProp(lpMessage, PR_SUBJECT_W, &~lpSubject) == hrSuccess) {
			removeEnters(lpSubject->Value.lpszW);
			strOut = lpSubject->Value.lpszW;
		}
		else
			strOut.clear();

		// we can't ignore any errors.. the sender should know if an email is not sent correctly
		lpVMMessageBuilder->setSubject(getVmimeTextFromWide(strOut));

		// handle recipients
		auto hr = processRecipients(lpMessage, lpVMMessageBuilder);
		if (hr != hrSuccess)
			// Logging has been done in processRecipients()
			return hr;

		if (!bSkipContent) {
			// handle text
			hr = handleTextparts(lpMessage, lpVMMessageBuilder, &bestBody);
			if (hr != hrSuccess)
				return hr;

			// handle attachments
			hr = handleAttachments(lpMessage, lpVMMessageBuilder);
			if (hr != hrSuccess)
				return hr;

			// check for TNEF information, and add winmail.dat if needed
			hr = handleTNEF(lpMessage, lpVMMessageBuilder, bestBody);
			if (hr != hrSuccess)
				return hr;
		}

		/*
		  To convert a VMMessageBuilder to a VMMessage object, an 'expeditor' needs to be set in vmime
		  later on, this from will be overwritten .. so maybe replace this with a dummy value?
		 */

		std::wstring strName, strType, strEmAdd;
		hr = HrGetAddress(m_lpAdrBook, lpMessage, PR_SENDER_ENTRYID, PR_SENDER_NAME_W, PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS_W, strName, strType, strEmAdd);
		if (hr != hrSuccess)
			return kc_perror("Unable to get sender information", hr);
		if (!strName.empty())
			lpVMMessageBuilder->setExpeditor(vmime::mailbox(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmAdd)));
		else
			lpVMMessageBuilder->setExpeditor(vmime::mailbox(m_converter.convert_to<string>(strEmAdd)));
		// sender and reply-to is set elsewhere because it can only be done on a message object...
	} catch (const vmime::exception &e) {
		ec_log_err("VMIME exception: %s", e.what());
		return MAPI_E_CALL_FAILED; // set real error
	} catch (const std::exception &e) {
		ec_log_err("STD exception on fill message: %s", e.what());
		return MAPI_E_CALL_FAILED; // set real error
	}
	catch (...) {
		ec_log_err("Unknown generic exception  on fill message");
		return MAPI_E_CALL_FAILED;
	}
	return hrSuccess;
}

/**
 * Make a vmime::mailbox from a recipient table row. If this function
 * fails in the gateway, thunderbird will stop parsing the whole
 * folder, and not just the single message.
 *
 * @param[in]	lpRow	One row from the recipient table.
 * @param[out]	lpvmMailbox	vmime::mailbox object containing the recipient.
 * 
 * @return MAPI error code
 * @retval MAPI_E_INVALID_PARAMTER email address in row is not usable for the SMTP protocol
 */
HRESULT MAPIToVMIME::getMailBox(LPSRow lpRow,
    vmime::shared_ptr<vmime::address> &vmMailboxNew)
{
	std::wstring strName, strEmail, strType;

	auto hr = HrGetAddress(m_lpAdrBook, lpRow->lpProps, lpRow->cValues, PR_ENTRYID, PR_DISPLAY_NAME_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, strName, strType, strEmail);
	if (hr != hrSuccess)
		return kc_perror("Unable to create mailbox", hr);
	auto pPropObjectType = lpRow->cfind(PR_OBJECT_TYPE);
	if (strName.empty() && !strEmail.empty()) {
		// email address only
		vmMailboxNew = vmime::make_shared<vmime::mailbox>(m_converter.convert_to<string>(strEmail));
		return hrSuccess;
	} else if (strEmail.find('@') != string::npos) {
		// email with fullname
		vmMailboxNew = vmime::make_shared<vmime::mailbox>(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail));
		return hrSuccess;
	} else if (pPropObjectType && pPropObjectType->Value.ul == MAPI_DISTLIST) {
		// if mailing to a group without email address
		vmMailboxNew = vmime::make_shared<vmime::mailboxGroup>(getVmimeTextFromWide(strName));
		return hrSuccess;
	} else if (sopt.no_recipients_workaround) {
		// gateway must always return a mailbox object
		vmMailboxNew = vmime::make_shared<vmime::mailbox>(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail));
		return hrSuccess;
	}
	if (strEmail.empty()) {
		// not an email address and not a group: invalid
		m_strError = L"Invalid email address in recipient list found: \"" + strName + L"\". Email Address is empty.";
		ec_log_err("%ls", m_strError.c_str());
		return MAPI_E_INVALID_PARAMETER;
	}

	// we only want to have this recipient fail, and not the whole message, if the user has a username
	m_strError = L"Invalid email address in recipient list found: \"" + strName + L"\" <" + strEmail + L">.";
	ec_log_err("%ls", m_strError.c_str());
	return MAPI_E_INVALID_PARAMETER;
}

/**
 * Converts RTF (using TNEF) or HTML with plain to body parts.
 *
 * @param[in]	lpMessage	Message to convert body parts from 
 * @param[in]	lpVMMessageBuilder	messageBuilder object place bodyparts in
 * @param[in]	charset		Use this charset for the HTML and plain text bodies.
 * @param[out]	lpbRealRTF	true if real rtf should be used, which is attached later, since it can't be a body.
 */
HRESULT MAPIToVMIME::handleTextparts(IMessage* lpMessage, vmime::messageBuilder *lpVMMessageBuilder, eBestBody *bestBody) {
	std::string strHTMLOut, strRtf, strBodyConverted;
	std::wstring strBody;
	object_ptr<IStream> lpCompressedRTFStream, lpUncompressedRTFStream;

	// set the encoder of plaintext body part
	vmime::encoding bodyEncoding("quoted-printable");
	// will skip enters in q-p encoding, "fixes" plaintext mails to be more plain text
	vmime::shared_ptr<vmime::utility::encoder::encoder> vmBodyEncoder = bodyEncoding.getEncoder();
	vmBodyEncoder->getProperties().setProperty("text", true);

	*bestBody = plaintext;

	// grabbing rtf
	HRESULT hr = lpMessage->OpenProperty(PR_RTF_COMPRESSED, &IID_IStream,
	             0, 0, &~lpCompressedRTFStream);
	if(hr == hrSuccess) {
		hr = WrapCompressedRTFStream(lpCompressedRTFStream, 0, &~lpUncompressedRTFStream);
		if (hr != hrSuccess) {
			kc_perror("Unable to create RTF text stream", hr);
			goto exit;
		}

		hr = Util::HrStreamToString(lpUncompressedRTFStream, strRtf);
		if (hr != hrSuccess) {
			kc_perror("Unable to read RTF text stream", hr);
			goto exit;
		}

		if (isrtfhtml(strRtf.c_str(), strRtf.size()) ||
			(sopt.use_tnef == -1 && !isrtftext(strRtf.c_str(), strRtf.size())))
		{
			// Open html
			object_ptr<IStream> lpHTMLStream;

			hr = lpMessage->OpenProperty(PR_HTML, &IID_IStream, 0, 0, &~lpHTMLStream);
			if (hr == hrSuccess) {
				hr = Util::HrStreamToString(lpHTMLStream, strHTMLOut);
				if (hr != hrSuccess) {
					kc_perror("Unable to read HTML text stream", hr);
					goto exit;
				}

				// strHTMLout is now escaped us-ascii HTML, this is what we will be sending
				// Or, if something failed, the HTML is now empty
				*bestBody = html;
			} else {
				kc_perror("Unable to open HTML text stream", hr);
				// continue with plaintext
			}
		} else if (isrtftext(strRtf.c_str(), strRtf.size())) {
			//Do nothing, only plain/text
		} else {
			// Real rtf
			*bestBody = realRTF;
		}
	} else {
		if (hr != MAPI_E_NOT_FOUND)
			kc_perror("Unable to open RTF text stream", hr);
		hr = hrSuccess;
	}
	
	try {
		object_ptr<IStream> lpBody;

		hr = lpMessage->OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, &~lpBody);
		if (hr == hrSuccess) {
			hr = Util::HrStreamToString(lpBody, strBody);
		} else {
			if (hr != MAPI_E_NOT_FOUND)
				kc_perror("Unable to open plain text stream", hr);
			hr = hrSuccess;
		}

		// Convert body to correct charset
		strBodyConverted = m_converter.convert_to<string>(m_strCharset.c_str(), strBody, rawsize(strBody), CHARSET_WCHAR);

		// always use our textpart class
		lpVMMessageBuilder->constructTextPart(vmime::mediaType(vmime::mediaTypes::TEXT, "mapi"));

		// If HTML, create HTML and plaintext parts
		if (!strHTMLOut.empty()) {
			if (m_vmCharset.getName() != m_strHTMLCharset)
				// convert from HTML charset to vmime output charset
				strHTMLOut = m_converter.convert_to<string>(m_vmCharset.getName().c_str(), strHTMLOut, rawsize(strHTMLOut), m_strHTMLCharset.c_str());
			vmime::shared_ptr<vmime::mapiTextPart> textPart = vmime::dynamicCast<vmime::mapiTextPart>(lpVMMessageBuilder->getTextPart());
			textPart->setText(vmime::make_shared<vmime::stringContentHandler>(strHTMLOut));
			textPart->setCharset(m_vmCharset);
			if (!strBodyConverted.empty())
				textPart->setPlainText(vmime::make_shared<vmime::stringContentHandler>(strBodyConverted));
		}
		// else just plaintext
		else if (!strBodyConverted.empty()) {
			// make sure we give vmime CRLF data, so SMTP servers (qmail) won't complain on the forced plaintext
			auto crlfconv = make_unique_nt<char[]>(strBodyConverted.length() * 2 + 1);
			if (crlfconv == nullptr)
				return MAPI_E_NOT_ENOUGH_MEMORY;
			size_t outsize = 0;
			BufferLFtoCRLF(strBodyConverted.length(), strBodyConverted.c_str(), crlfconv.get(), &outsize);
			strBodyConverted.assign(crlfconv.get(), outsize);

			// encode to q-p ourselves
			vmime::utility::inputStreamStringAdapter in(strBodyConverted);
			vmime::string outString;
			vmime::utility::outputStreamStringAdapter out(outString);
			vmBodyEncoder->encode(in, out);
			lpVMMessageBuilder->getTextPart()->setCharset(m_vmCharset);
			vmime::dynamicCast<vmime::mapiTextPart>(lpVMMessageBuilder->getTextPart())->setPlainText(vmime::make_shared<vmime::stringContentHandler>(outString, bodyEncoding));
		}
	} catch (const vmime::exception &e) {
		ec_log_err("VMIME exception: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	} catch (const std::exception &e) {
		ec_log_err("STD exception on text part: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		ec_log_err("Unknown generic exception on text part");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

exit:
	if (hr != hrSuccess)
		ec_log_warn("Could not parse mail body");
	return hr;
}

/**
 * Add X-Headers to message from named properties.
 * Contents of X-Headers can only be US-Ascii.
 *
 * @param[in]	lpMessage	MAPI Message to get X headers from
 * @param[in]	vmHeader	vmime header object to add X headers to
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::handleXHeaders(IMessage *lpMessage,
    vmime::shared_ptr<vmime::header> vmHeader, unsigned int flags)
{
	ULONG cValues;
	memory_ptr<SPropTagArray> lpsNamedTags, lpsAllTags;
	memory_ptr<SPropValue> lpPropArray;
	memory_ptr<MAPINAMEID *> lppNames;
	auto hff = vmime::headerFieldFactory::getInstance();

	// get all props on message
	auto hr = lpMessage->GetPropList(0, &~lpsAllTags);
	if (FAILED(hr))
		return hr;

	// find number of named props, which contain a string
	unsigned int cNames = 0;
	for (unsigned int i = 0; i < lpsAllTags->cValues; ++i)
		if (PROP_ID(lpsAllTags->aulPropTag[i]) >= 0x8000 && PROP_TYPE(lpsAllTags->aulPropTag[i]) == PT_STRING8)
			++cNames;

	// no string named properties found, we're done.
	if (cNames == 0)
		return hr;
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(cNames), &~lpsNamedTags);
	if (hr != hrSuccess)
		return hr;
	lpsNamedTags->cValues = cNames;

	// make named prop array
	cNames = 0;
	for (unsigned int i = 0; i < lpsAllTags->cValues; ++i)
		if (PROP_ID(lpsAllTags->aulPropTag[i]) >= 0x8000 && PROP_TYPE(lpsAllTags->aulPropTag[i]) == PT_STRING8)
			lpsNamedTags->aulPropTag[cNames++] = lpsAllTags->aulPropTag[i];
	
	hr = lpMessage->GetNamesFromIDs(&+lpsNamedTags, NULL, 0, &cNames, &~lppNames);
	if (FAILED(hr))
		return hr;
	hr = lpMessage->GetProps(lpsNamedTags, 0, &cValues, &~lpPropArray);
	if (FAILED(hr))
		return hr;

	for (unsigned int i = 0; i < cNames; ++i) {
		if (lppNames[i] == nullptr ||
		    lppNames[i]->ulKind != MNID_STRING ||
		    lppNames[i]->Kind.lpwstrName == nullptr ||
		    PROP_TYPE(lpPropArray[i].ulPropTag) != PT_STRING8)
			continue;

		int l = wcstombs(NULL, lppNames[i]->Kind.lpwstrName, 0) +1;
		auto str = make_unique_nt<char[]>(l);
		if (str == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		wcstombs(str.get(), lppNames[i]->Kind.lpwstrName, l);

		bool xh = (str[0] == 'X' || str[0] == 'x') && str[1] == '-';
		if (!xh)
			continue;
		if (str[0] == 'x')
			capitalize(str.get());

		// keep the original x-mailer header under a different name
		// we still want to know that this mail was generated by kopano in the x-mailer header from handleExtraHeaders
		if (flags & MTV_SPOOL && strcasecmp(str.get(), "X-Mailer") == 0) {
			str.reset(new char[18]);
			strcpy(str.get(), "X-Original-Mailer");
		}
		if (!vmHeader->hasField(str.get()))
			vmHeader->appendField(hff->create(str.get(), lpPropArray[i].Value.lpszA));
	}
	return hr;
}

/**
 * Adds extra special headers to the e-mail.
 *
 * List of current extra e-mail headers:
 * \li In-Reply-To
 * \li References
 * \li Importance with X-Priority
 * \li X-Mailer
 * \li Thread-Index
 * \li Thread-Topic
 * \li Sensitivity
 * \li Expiry-Time
 *  
 * @param[in]	lpMessage	Message to convert extra headers for
 * @param[in]	charset		Charset to use in Thread-Topic header
 * @param[in]	vmHeader	Add headers to this vmime header object
 * @return always hrSuccess
 */
HRESULT MAPIToVMIME::handleExtraHeaders(IMessage *lpMessage,
    vmime::shared_ptr<vmime::header> vmHeader, unsigned int flags)
{
	SPropValuePtr ptrMessageId;
	memory_ptr<SPropValue> lpImportance, lpPriority, lpConversationIndex;
	memory_ptr<SPropValue> lpConversationTopic, lpNormSubject;
	memory_ptr<SPropValue> lpSensitivity, lpExpiryTime;
	auto hff = vmime::headerFieldFactory::getInstance();

	// Conversation headers. New Message-Id header is set just before sending.
	if (HrGetOneProp(lpMessage, PR_IN_REPLY_TO_ID_A, &~ptrMessageId) == hrSuccess && ptrMessageId->Value.lpszA[0]) {
		vmime::shared_ptr<vmime::messageId> mid = vmime::make_shared<vmime::messageId>(ptrMessageId->Value.lpszA);
		vmime::dynamicCast<vmime::messageIdSequence>(vmHeader->InReplyTo()->getValue())->appendMessageId(mid);
	}

	// Outlook never adds this property
	if (HrGetOneProp(lpMessage, PR_INTERNET_REFERENCES_A, &~ptrMessageId) == hrSuccess && ptrMessageId->Value.lpszA[0]) {
		std::vector<std::string> ids = tokenize(ptrMessageId->Value.lpszA, ' ', true);

		const size_t n = ids.size();
		for (size_t i = 0; i < n; ++i) {
			vmime::shared_ptr<vmime::messageId> mid = vmime::make_shared<vmime::messageId>(ids.at(i));
			vmime::dynamicCast<vmime::messageIdSequence>(vmHeader->References()->getValue())->appendMessageId(mid);
		}
	}

	// only for message-in-message items, add Message-ID header from MAPI
	if (sopt.msg_in_msg && HrGetOneProp(lpMessage, PR_INTERNET_MESSAGE_ID_A, &~ptrMessageId) == hrSuccess && ptrMessageId->Value.lpszA[0])
		vmHeader->MessageId()->setValue(ptrMessageId->Value.lpszA);

	// priority settings
	static const char *const priomap[3] = { "5 (Lowest)", "3 (Normal)", "1 (Highest)" }; // 2 and 4 cannot be set from outlook
	if (HrGetOneProp(lpMessage, PR_IMPORTANCE, &~lpImportance) == hrSuccess)
		vmHeader->appendField(hff->create("X-Priority", priomap[min(2, (int)(lpImportance->Value.ul)&3)])); // IMPORTANCE_* = 0..2
	else if (HrGetOneProp(lpMessage, PR_PRIORITY, &~lpPriority) == hrSuccess)
		vmHeader->appendField(hff->create("X-Priority", priomap[min(2, (int)(lpPriority->Value.ul+1)&3)])); // PRIO_* = -1..1

	// When adding a X-Priority, spamassassin may add a severe punishment because no User-Agent header
	// or X-Mailer header is present. So we set the X-Mailer header :)
	if (flags & MTV_SPOOL)
		vmHeader->appendField(hff->create("X-Mailer", "Kopano " PROJECT_VERSION));

	// PR_CONVERSATION_INDEX
	if (HrGetOneProp(lpMessage, PR_CONVERSATION_INDEX, &~lpConversationIndex) == hrSuccess) {
		vmime::string inString;
		inString.assign((const char*)lpConversationIndex->Value.bin.lpb, lpConversationIndex->Value.bin.cb);

		vmime::shared_ptr<vmime::utility::encoder::encoder> enc = vmime::utility::encoder::encoderFactory::getInstance()->create("base64");
		vmime::utility::inputStreamStringAdapter in(inString);
		vmime::string outString;
		vmime::utility::outputStreamStringAdapter out(outString);

		enc->encode(in, out);

		vmHeader->appendField(hff->create("Thread-Index", outString));
	}

	// PR_CONVERSATION_TOPIC is always the original started topic
	if (HrGetOneProp(lpMessage, PR_CONVERSATION_TOPIC_W, &~lpConversationTopic) == hrSuccess &&
	    (HrGetOneProp(lpMessage, PR_NORMALIZED_SUBJECT_W, &~lpNormSubject) != hrSuccess ||
	    wcscmp(lpNormSubject->Value.lpszW, lpConversationTopic->Value.lpszW) != 0)) {
		removeEnters(lpConversationTopic->Value.lpszW);
		vmHeader->appendField(hff->create("Thread-Topic", getVmimeTextFromWide(lpConversationTopic->Value.lpszW).generate()));
	}
	if (HrGetOneProp(lpMessage, PR_SENSITIVITY, &~lpSensitivity) == hrSuccess) {
		const char *strSens;
		switch (lpSensitivity->Value.ul) {
		case SENSITIVITY_PERSONAL:
			strSens = "Personal";
			break;
		case SENSITIVITY_PRIVATE:
			strSens = "Private";
			break;
		case SENSITIVITY_COMPANY_CONFIDENTIAL:
			strSens = "Company-Confidential";
			break;
		case SENSITIVITY_NONE:
		default:
			strSens = NULL;
			break;
		};
		if (strSens)
			vmHeader->appendField(hff->create("Sensitivity", strSens));
	}
	if (HrGetOneProp(lpMessage, PR_EXPIRY_TIME, &~lpExpiryTime) == hrSuccess)
		vmHeader->appendField(hff->create("Expiry-Time", FiletimeTovmimeDatetime(lpExpiryTime->Value.ft).generate()));

	if (!(flags & MTV_SPOOL))
		return hrSuccess;
	char buffer[4096] = {0};
	if (gethostname(buffer, sizeof buffer) == -1)
		strcpy(buffer, "???");
	vmime::relay relay;
	relay.setBy(std::string(buffer) + " (kopano-spooler)");
	relay.getWithList().emplace_back("MAPI");
	auto now = vmime::datetime::now();
	relay.setDate(now);
	auto header_field = hff->create("Received");
	header_field->setValue(relay);
	vmHeader->insertFieldBefore(0, header_field);
	return hrSuccess;
}

/**
 * Open the contact of the user if it is a contact folder and rewrite it to a
 * usable e-mail recipient.
 *
 * @param[in]	cValues	Number of properties in lpProps (unused)
 * @param[in]	lpProps	EntryID of contact in 1st property
 * @param[out]	strName	Fullname of the contact
 * @param[out]	strName	Type of the resolved contact
 * @param[out]	strName	SMTP e-mail address of the contact
 * @return Mapi error code
 *
 * @todo fix first two parameters
 */
HRESULT MAPIToVMIME::handleContactEntryID(ULONG cValues, LPSPropValue lpProps, wstring &strName, wstring &strType, wstring &strEmail)
{
	ULONG ulObjType;
	memory_ptr<SPropTagArray> lpNameTags;
	memory_ptr<SPropValue> lpNamedProps;
	object_ptr<IMailUser> lpContact;
	memory_ptr<MAPINAMEID *> lppNames;
	ULONG ulNames = 5;
	MAPINAMEID mnNamedProps[5] = {
		// offset 0, every offset < 3 is + 0x10
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8080}}, // display name
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8082}}, // address type
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8083}}, // email address
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8084}}, // original display name (unused)
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8085}}, // real entryid
	};

	if (PROP_TYPE(lpProps[0].ulPropTag) != PT_BINARY)
		return MAPI_E_NOT_FOUND;
	auto lpContabEntryID = reinterpret_cast<CONTAB_ENTRYID *>(lpProps[0].Value.bin.lpb);
	if (lpContabEntryID == NULL)
		return MAPI_E_NOT_FOUND;
	auto guid = reinterpret_cast<GUID *>(&lpContabEntryID->muid);
	if (sizeof(CONTAB_ENTRYID) > lpProps[0].Value.bin.cb ||
	    *guid != PSETID_CONTACT_FOLDER_RECIPIENT ||
	    lpContabEntryID->email_offset > 2)
		return MAPI_E_NOT_FOUND;
	auto hr = m_lpSession->OpenEntry(lpContabEntryID->cbeid, reinterpret_cast<ENTRYID *>(lpContabEntryID->abeid), &iid_of(lpContact), 0, &ulObjType, &~lpContact);
	if (hr != hrSuccess)
		return hr;

	// add offset to get correct named properties
	for (unsigned int i = 0; i < ulNames; ++i)
		mnNamedProps[i].Kind.lID += (lpContabEntryID->email_offset * 0x10);

	hr = MAPIAllocateBuffer(sizeof(LPMAPINAMEID) * (ulNames), &~lppNames);
	if (hr != hrSuccess) {
		ec_log_err("No memory for named ids from contact");
		return hr;
	}
	for (unsigned int i = 0; i < ulNames; ++i)
		lppNames[i] = &mnNamedProps[i];

	hr = lpContact->GetIDsFromNames(ulNames, lppNames, MAPI_CREATE, &~lpNameTags);
	if (FAILED(hr))
		return hr;

	lpNameTags->aulPropTag[0] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[0], PT_UNICODE);
	lpNameTags->aulPropTag[1] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[1], PT_UNICODE);
	lpNameTags->aulPropTag[2] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[2], PT_UNICODE);
	lpNameTags->aulPropTag[3] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[3], PT_UNICODE); // unused
	lpNameTags->aulPropTag[4] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[4], PT_BINARY);
	hr = lpContact->GetProps(lpNameTags, 0, &ulNames, &~lpNamedProps);
	if (FAILED(hr))
		return hr;

	return HrGetAddress(m_lpAdrBook, lpNamedProps, ulNames,
	       lpNameTags->aulPropTag[4], lpNameTags->aulPropTag[0],
	       lpNameTags->aulPropTag[1], lpNameTags->aulPropTag[2],
	       strName, strType, strEmail);
}

/**
 * Set From and possibly Sender header.
 *
 * @param[in]	lpMessage	Message to get From and Sender of.
 * @param[in]	charset		charset to use for Fullname headers.
 * @param[in]	vmHeader	vmime header object to modify.
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::handleSenderInfo(IMessage *lpMessage,
    vmime::shared_ptr<vmime::header> vmHeader)
{
	ULONG cValues;
	memory_ptr<SPropValue> lpProps, lpReadReceipt;

	// sender information
	std::wstring strEmail, strName, strType;
	std::wstring strResEmail, strResName, strResType;
	static constexpr const SizedSPropTagArray(4, sender_proptags) =
		{4, {PR_SENDER_ENTRYID, PR_SENDER_NAME_W,
		PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS_W}};
	HRESULT hr = lpMessage->GetProps(sender_proptags, 0, &cValues, &~lpProps);
	if (FAILED(hr))
		return hr;

	hr = handleContactEntryID(cValues, lpProps, strName, strType, strEmail);
	if (hr != hrSuccess) {
		// Store owner, actual sender
		hr = HrGetAddress(m_lpAdrBook, lpProps, cValues, PR_SENDER_ENTRYID, PR_SENDER_NAME_W, PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS_W, strName, strType, strEmail);
		if (hr != hrSuccess)
			return kc_perror("Unable to get sender information", hr);
	}

	// -- sender
	static constexpr const SizedSPropTagArray(4, repr_proptags) =
		{4, {PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_NAME_W,
		PR_SENT_REPRESENTING_ADDRTYPE_W,
		PR_SENT_REPRESENTING_EMAIL_ADDRESS_W}};
	hr = lpMessage->GetProps(repr_proptags, 0, &cValues, &~lpProps);
	if (FAILED(hr))
		return hr;

	hr = handleContactEntryID(cValues, lpProps, strResName, strResType, strResEmail);
	if (hr != hrSuccess) {
		hr = HrGetAddress(m_lpAdrBook, lpProps, cValues, PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, strResName, strResType, strResEmail);
		if (hr != hrSuccess) {
			kc_pwarn("Unable to get representing information", hr);
			// ignore error, since we still have enough information to send, maybe not just under the correct name
			hr = hrSuccess;
		}
		if (!sopt.no_recipients_workaround && strResEmail.empty() &&
		    PROP_TYPE(lpProps[0].ulPropTag) != PT_ERROR) {
			m_strError = L"Representing email address is empty";
			ec_log_err("%ls", m_strError.c_str());
			return MAPI_E_NOT_FOUND;
		}
	}

	// Set representing as from address, when possible
	// Ignore PR_SENT_REPRESENTING if the email adress is the same as the PR_SENDER email address
	if (!strResEmail.empty() && strResEmail != strEmail) {
		if (strResName.empty() || strResName == strResEmail) 
			vmHeader->From()->setValue(vmime::mailbox(m_converter.convert_to<string>(strResEmail)));
		else
			vmHeader->From()->setValue(vmime::mailbox(getVmimeTextFromWide(strResName), m_converter.convert_to<string>(strResEmail)));

		// spooler checked if this is allowed
		if (strResEmail != strEmail) {
			// Set store owner as sender
			if (strName.empty() || strName == strEmail) 
				vmHeader->Sender()->setValue(vmime::mailbox(m_converter.convert_to<string>(strEmail)));
			else
				vmHeader->Sender()->setValue(vmime::mailbox(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail)));
		}
	} else if (strName.empty() || strName == strEmail) {
		// Set store owner as from, sender does not need to be set
		vmHeader->From()->setValue(vmime::mailbox(m_converter.convert_to<string>(strEmail)));
	} else {
		vmHeader->From()->setValue(vmime::mailbox(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail)));
	}

	if (HrGetOneProp(lpMessage, PR_READ_RECEIPT_REQUESTED, &~lpReadReceipt) != hrSuccess || !lpReadReceipt->Value.b)
		return hrSuccess;
	// read receipt request
	vmime::mailboxList mbl;
	if (!strResEmail.empty() && strResEmail != strEmail) {
		// use user added from address
		if (strResName.empty() || strName == strResEmail)
			mbl.appendMailbox(vmime::make_shared<vmime::mailbox>(m_converter.convert_to<string>(strResEmail)));
		else
			mbl.appendMailbox(vmime::make_shared<vmime::mailbox>(getVmimeTextFromWide(strResName), m_converter.convert_to<string>(strResEmail)));
	} else if (strName.empty() || strName == strEmail) {
		mbl.appendMailbox(vmime::make_shared<vmime::mailbox>(m_converter.convert_to<string>(strEmail)));
	} else {
		mbl.appendMailbox(vmime::make_shared<vmime::mailbox>(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail)));
	}
	vmHeader->DispositionNotificationTo()->setValue(mbl);
	return hrSuccess;
}

/**
 * Set Reply-To header.
 *
 * @note In RFC 2822 and MAPI, you can set multiple Reply-To
 * values. However, in vmime this is currently not possible, so we
 * only convert the first.
 *
 * @param[in]	lpMessage	Message to get Reply-To value of.
 * @param[in]	charset		charset to use for Fullname headers.
 * @param[in]	vmHeader	vmime header object to modify.
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::handleReplyTo(IMessage *lpMessage,
    vmime::shared_ptr<vmime::header> vmHeader)
{
	ULONG			ulObjType;
	object_ptr<IMailUser> lpContact;
	wstring			strName, strType, strEmail;

	// "Email1DisplayName","Email1AddressType","Email1Address","Email1EntryID"
	static const ULONG lpulNamesIDs[] = {0x8080, 0x8082, 0x8083, 0x8085,
				0x8090, 0x8092, 0x8093, 0x8095,
				0x80A0, 0x80A2, 0x80A3, 0x80A5};
	memory_ptr<MAPINAMEID> lpNames;
	memory_ptr<MAPINAMEID *> lppNames;
	memory_ptr<SPropTagArray> lpNameTagArray;
	memory_ptr<SPropValue> lpAddressProps, lpReplyTo;

	if (HrGetOneProp(lpMessage, PR_REPLY_RECIPIENT_ENTRIES, &~lpReplyTo) != hrSuccess)
		return hrSuccess;
	if (lpReplyTo->Value.bin.cb == 0)
		return hrSuccess;
	auto lpEntryList = reinterpret_cast<FLATENTRYLIST *>(lpReplyTo->Value.bin.lpb);
	if (lpEntryList->cEntries == 0)
		return hrSuccess;
	auto lpEntry = reinterpret_cast<FLATENTRY *>(&lpEntryList->abEntries);
	auto hr = HrGetAddress(m_lpAdrBook, reinterpret_cast<ENTRYID *>(lpEntry->abEntry), lpEntry->cb, strName, strType, strEmail);
	if (hr != hrSuccess) {
		if (m_lpSession == nullptr)
			return MAPI_E_INVALID_PARAMETER;

		// user selected a contact (or distrolist ?) in the reply-to
		auto lpContabEntryID = reinterpret_cast<CONTAB_ENTRYID *>(lpEntry->abEntry);
		auto guid = reinterpret_cast<GUID *>(&lpContabEntryID->muid);

		if (sizeof(CONTAB_ENTRYID) > lpEntry->cb || *guid != PSETID_CONTACT_FOLDER_RECIPIENT || lpContabEntryID->email_offset > 2)
			return hr;
		hr = m_lpSession->OpenEntry(lpContabEntryID->cbeid, reinterpret_cast<ENTRYID *>(lpContabEntryID->abeid), &iid_of(lpContact), 0, &ulObjType, &~lpContact);
		if (hr != hrSuccess)
			return hr;
		unsigned int cNames = ARRAY_SIZE(lpulNamesIDs);
		hr = MAPIAllocateBuffer(sizeof(MAPINAMEID) * cNames, &~lpNames);
		if (hr != hrSuccess)
			return hr;
		hr = MAPIAllocateBuffer(sizeof(LPMAPINAMEID) * cNames, &~lppNames);
		if (hr != hrSuccess)
			return hr;
		for (unsigned int i = 0; i < cNames; ++i) {
			lpNames[i].lpguid = (GUID*)&PSETID_Address;
			lpNames[i].ulKind = MNID_ID;
			lpNames[i].Kind.lID = lpulNamesIDs[i];
			lppNames[i] = &lpNames[i];
		}
		hr = lpContact->GetIDsFromNames(cNames, lppNames, 0, &~lpNameTagArray);
		if (FAILED(hr))
			return hr;

		// PT_UNSPECIFIED in tagarray, but we want PT_UNICODE
		hr = lpContact->GetProps(lpNameTagArray, MAPI_UNICODE, &cNames, &~lpAddressProps);
		if (FAILED(hr))
			return hr;
		unsigned int offset = lpContabEntryID->email_offset * 4; // 4 props per email address
		if (PROP_TYPE(lpAddressProps[offset+0].ulPropTag) == PT_ERROR || PROP_TYPE(lpAddressProps[offset+1].ulPropTag) == PT_ERROR ||
			PROP_TYPE(lpAddressProps[offset+2].ulPropTag) == PT_ERROR || PROP_TYPE(lpAddressProps[offset+3].ulPropTag) == PT_ERROR)
			return hr;

		if (wcscmp(lpAddressProps[offset+1].Value.lpszW, L"SMTP") == 0) {
			strName = lpAddressProps[offset+0].Value.lpszW;
			strEmail = lpAddressProps[offset+2].Value.lpszW;
		} else if (wcscmp(lpAddressProps[offset+1].Value.lpszW, L"ZARAFA") == 0) {
			hr = HrGetAddress(m_lpAdrBook, (LPENTRYID)lpAddressProps[offset+2].Value.bin.lpb, lpAddressProps[offset+2].Value.bin.cb, strName, strType, strEmail);
			if (hr != hrSuccess)
				return hr;
		}
	}

	// vmime can only set 1 email address in the ReplyTo field.
	if (!strName.empty() && strName != strEmail)
		vmHeader->ReplyTo()->setValue(vmime::make_shared<vmime::mailbox>(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail)));
	else
		vmHeader->ReplyTo()->setValue(vmime::make_shared<vmime::mailbox>(m_converter.convert_to<string>(strEmail)));
	return hrSuccess;
}

/**
 * check if named property exists which is used to hold voting options
 */

bool MAPIToVMIME::is_voting_request(IMessage *lpMessage) const
{
	memory_ptr<SPropTagArray> lpPropTags;
	memory_ptr<SPropValue> lpPropContentType;
	MAPINAMEID named_prop = {(LPGUID)&PSETID_Common, MNID_ID, {0x8520}};
	MAPINAMEID *named_proplist = &named_prop;

	auto hr = lpMessage->GetIDsFromNames(1, &named_proplist, MAPI_CREATE, &~lpPropTags);
	if (hr != hrSuccess)
		kc_perror("Unable to read voting property", hr);
	else
		hr = HrGetOneProp(lpMessage, CHANGE_PROP_TYPE(lpPropTags->aulPropTag[0], PT_BINARY), &~lpPropContentType);
	return hr == hrSuccess;
}

/**
 * CCheck if the named property exists which denotes if reminder is set
 */
bool MAPIToVMIME::has_reminder(IMessage *msg) const
{
	memory_ptr<SPropTagArray> tags;
	memory_ptr<SPropValue> content_type;
	MAPINAMEID named_prop = {const_cast<GUID *>(&PSETID_Common), MNID_ID, {0x8503}};
	auto named_proplist = &named_prop;
	bool result = false;

	auto hr = msg->GetIDsFromNames(1, &named_proplist, MAPI_CREATE, &~tags);
	if (hr != hrSuccess)
		kc_perror("Unable to read reminder property", hr);
	else {
		hr = HrGetOneProp(msg, CHANGE_PROP_TYPE(tags->aulPropTag[0], PT_BOOLEAN), &~content_type);
		if(hr == hrSuccess)
			result = content_type->Value.b;
		else
			ec_log_debug("Message has no reminder property");
	}
	return result;
}

/**
 * Adds a TNEF (winmail.dat) attachment to the message, if special
 * outlook data needs to be sent. May add iCal for calendar items
 * instead of TNEF.
 *
 * @param[in]	lpMessage	Message to get Reply-To value of.
 * @param[in]	charset		charset to use for Fullname headers.
 * @param[in]	vmHeader	vmime header object to modify.
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::handleTNEF(IMessage* lpMessage, vmime::messageBuilder* lpVMMessageBuilder, eBestBody bestBody) {
	HRESULT			hr				= hrSuccess;
	memory_ptr<SPropValue> lpSendAsICal, lpOutlookVersion, lpMessageClass;
	memory_ptr<SPropValue> lpDelegateRule;
	object_ptr<IStream> lpStream;
	std::unique_ptr<MapiToICal> mapiical;
	memory_ptr<MAPINAMEID> lpNames;
	memory_ptr<SPropTagArray> lpNameTagArray;
	int				iUseTnef = sopt.use_tnef;
	std::string		strTnefReason;

	std::list<ULONG> lstOLEAttach; // list of OLE attachments that must be sent via TNEF
	object_ptr<IMAPITable> lpAttachTable;
	static constexpr const SizedSPropTagArray(2, sptaAttachProps) =
		{2, {PR_ATTACH_METHOD, PR_ATTACH_NUM}};
	static constexpr const SizedSPropTagArray(5, sptaOLEAttachProps) =
		{5, {PR_ATTACH_FILENAME, PR_ATTACH_LONG_FILENAME,
		PR_ATTACH_DATA_OBJ, PR_ATTACH_CONTENT_ID, PR_ATTACHMENT_HIDDEN}};
	static constexpr const SizedSSortOrderSet(1, sosRTFSeq) =
		{1, 0, 0, {{PR_RENDERING_POSITION, TABLE_SORT_ASCEND}}};

	try {
		rowset_ptr lpAttachRows;
	    // Find all ATTACH_OLE attachments and put them in lstOLEAttach
		hr = lpMessage->GetAttachmentTable(0, &~lpAttachTable);
	    if(hr != hrSuccess)
			return hr;
		hr = HrQueryAllRows(lpAttachTable, sptaAttachProps, NULL,
	             sosRTFSeq, 0, &~lpAttachRows);
		if (hr != hrSuccess)
			return hr;
            
        for (unsigned int i = 0; i < lpAttachRows->cRows; ++i)
			if (lpAttachRows[i].lpProps[0].ulPropTag == PR_ATTACH_METHOD &&
			    lpAttachRows[i].lpProps[1].ulPropTag == PR_ATTACH_NUM &&
			    lpAttachRows[i].lpProps[0].Value.ul == ATTACH_OLE)
				lstOLEAttach.emplace_back(lpAttachRows[i].lpProps[1].Value.ul);
	
        // Start processing TNEF properties
		if (HrGetOneProp(lpMessage, PR_EC_SEND_AS_ICAL, &~lpSendAsICal) != hrSuccess)
			lpSendAsICal = NULL;
		if (HrGetOneProp(lpMessage, PR_EC_OUTLOOK_VERSION, &~lpOutlookVersion) != hrSuccess)
			lpOutlookVersion = NULL;
		if (HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMessageClass) != hrSuccess)
			lpMessageClass = NULL;
		if (HrGetOneProp(lpMessage, PR_DELEGATED_BY_RULE, &~lpDelegateRule) != hrSuccess)
			lpDelegateRule = NULL;
		if (iUseTnef > 0)
			strTnefReason = "Force TNEF on request";

		// currently no task support for ical
		if (iUseTnef <= 0 && lpMessageClass && strncasecmp("IPM.Task", lpMessageClass->Value.lpszA, 8) == 0) {
			iUseTnef = 1;
			strTnefReason = "Force TNEF because of task request";
		}

		// delegation of meeting requests need to be in tnef too because of special properties
		if (iUseTnef <= 0 && lpDelegateRule && lpDelegateRule->Value.b == TRUE) {
			iUseTnef = 1;
			strTnefReason = "Force TNEF because of delegation";
		}

		if(iUseTnef <= 0 && is_voting_request(lpMessage)) {
			iUseTnef = 1;
			strTnefReason = "Force TNEF because of voting request";
		}

		if (iUseTnef == 0 && has_reminder(lpMessage)) {
			iUseTnef = 1;
			strTnefReason = "Force TNEF because of reminder";
		}
        /*
         * Outlook 2000 always sets PR_EC_SEND_AS_ICAL to FALSE, because the
         * iCal option is somehow missing from the options property sheet, and 
         * PR_EC_USE_TNEF is never set. So for outlook 2000 (9.0), we check the
         * _existence_ of PR_EC_SEND_AS_ICAL as a hint to use TNEF.
         */
		if (iUseTnef == 1 ||
			(lpSendAsICal && lpSendAsICal->Value.b) || 
			(lpSendAsICal && lpOutlookVersion && strcmp(lpOutlookVersion->Value.lpszA, "9.0") == 0) ||
			(lpMessageClass && (strncasecmp("IPM.Note", lpMessageClass->Value.lpszA, 8) != 0) ) || 
			bestBody == realRTF)
		{
		    // Send either TNEF or iCal data
			/* 
			 * Send TNEF information for this message if we really need to, or otherwise iCal
			 */
			
			if (lstOLEAttach.size() == 0 && iUseTnef <= 0 && lpMessageClass && (strncasecmp("IPM.Note", lpMessageClass->Value.lpszA, 8) != 0)) {
				// iCAL
				string ical, method;
				vmime::shared_ptr<mapiAttachment> vmAttach = NULL;

				ec_log_info("Adding ICS attachment for extra information");
				hr = CreateMapiToICal(m_lpAdrBook, "utf-8", &unique_tie(mapiical));
				if (hr != hrSuccess) {
					kc_pwarn("Cannot create MAPIToICal object, sending as TNEF", hr);
					goto tnef_anyway;
				}
				hr = mapiical->AddMessage(lpMessage, std::string(), 0);
				if (hr != hrSuccess) {
					ec_log_warn("Unable to create ical object, sending as TNEF");
					goto tnef_anyway;
				}

				hr = mapiical->Finalize(0, &method, &ical);
				if (hr != hrSuccess) {
					ec_log_warn("Unable to create ical object, sending as TNEF");
					goto tnef_anyway;
				}

				vmime::shared_ptr<vmime::mapiTextPart> lpvmMapiText = vmime::dynamicCast<vmime::mapiTextPart>(lpVMMessageBuilder->getTextPart());
				lpvmMapiText->setOtherText(vmime::make_shared<vmime::stringContentHandler>(ical));
				lpvmMapiText->setOtherContentType(vmime::mediaType(vmime::mediaTypes::TEXT, "calendar"));
				lpvmMapiText->setOtherContentEncoding(vmime::encoding(vmime::encodingTypes::EIGHT_BIT));
				lpvmMapiText->setOtherMethod(method);
				lpvmMapiText->setOtherCharset(vmime::charset("utf-8"));
			} else {
				if (lstOLEAttach.size())
					ec_log_info("TNEF because of OLE attachments");
				else if (iUseTnef == 0)
					ec_log_info("TNEF because of RTF body");
				else
					ec_log_info(strTnefReason);

tnef_anyway:
				hr = CreateStreamOnHGlobal(nullptr, TRUE, &~lpStream);
				if (hr != hrSuccess)
					return kc_perror("Unable to create stream for TNEF attachment", hr);

				ECTNEF tnef(TNEF_ENCODE, lpMessage, lpStream);
				// Encode the properties now, add all message properties except for the exclude list
				hr = tnef.AddProps(TNEF_PROP_EXCLUDE, sptaExclude);
				if(hr != hrSuccess) {
					ec_log_err("Unable to exclude properties from TNEF object");
					return hr;
				}
			
				// plaintext is never added to TNEF, only HTML or "real" RTF
				if (bestBody != plaintext) {
					SizedSPropTagArray(1, sptaBestBodyInclude) = {1, {PR_RTF_COMPRESSED}};

					if (bestBody == html) sptaBestBodyInclude.aulPropTag[0] = PR_HTML;
					else if (bestBody == realRTF) sptaBestBodyInclude.aulPropTag[0] = PR_RTF_COMPRESSED;

					hr = tnef.AddProps(TNEF_PROP_INCLUDE, sptaBestBodyInclude);
					if(hr != hrSuccess) {
						ec_log_err("Unable to include body property 0x%08x to TNEF object", sptaBestBodyInclude.aulPropTag[0]);
						return hr;
					}
				}
				
				// Add all OLE attachments
				for (const auto atnum : lstOLEAttach)
					tnef.FinishComponent(0x00002000, atnum, sptaOLEAttachProps);
		
				// Write the stream
				hr = tnef.Finish();
				auto inputDataStream = vmime::make_shared<inputStreamMAPIAdapter>(lpStream);
				// Now, add the stream as an attachment to the message, filename winmail.dat 
				// and MIME type 'application/ms-tnef', no content-id
				auto vmTNEFAtt = vmime::make_shared<mapiAttachment>(vmime::make_shared<vmime::streamContentHandler>(inputDataStream, 0),
				                 vmime::encoding("base64"), vmime::mediaType("application/ms-tnef"), string(),
				                 vmime::word("winmail.dat"));

				// add to message (copies pointer, not data)
				lpVMMessageBuilder->appendAttachment(vmTNEFAtt); 
			}
		}
	} catch (const vmime::exception &e) {
		ec_log_err("VMIME exception: %s", e.what());
		return MAPI_E_CALL_FAILED; // set real error
	} catch (const std::exception &e) {
		ec_log_err("STD exception on fill message: %s", e.what());
		return MAPI_E_CALL_FAILED; // set real error
	}
	catch (...) {
		ec_log_err("Unknown generic exception on fill message");
		return MAPI_E_CALL_FAILED;
	}
	return hr;
}

/**
 * converts x-mail-sender -> X-Mail-Sender
 *
 * @param[in,out]	s	String to capitalize
 */
void MAPIToVMIME::capitalize(char *s) {
	s[0] = toupper(s[0]);		// x to X
	auto p = s;
	while ((p = strchr(p, '-'))) { // capitalize every char after a -
		++p;
		if (*p != '\0')
			*p = toupper(*p);
	}
}

/**
 * makes spaces from enters, avoid enters in Subject
 *
 * @param[in,out]	s	String to fix enter to spaces in
 */
void MAPIToVMIME::removeEnters(WCHAR *s) {
	WCHAR *p = s;

	while (*p) {
		if (*p == '\r' || *p == '\n')
			*p = ' ';
		++p;
	}
}

/**
 * Shortcut for common conversion used in this file.
 * Note: Uses class members m_converter, m_vmCharset and m_strCharset.
 * 
 * @param[in]	lpszwInput	input string in WCHAR
 * @return	the converted text from WCHAR to vmime::text with specified charset
 */
vmime::text MAPIToVMIME::getVmimeTextFromWide(const WCHAR* lpszwInput, bool bWrapInWord) {
	std::string output = m_converter.convert_to<std::string>(m_strCharset.c_str(), lpszwInput, rawsize(lpszwInput), CHARSET_WCHAR);
	if (bWrapInWord)
		return vmime::text(vmime::word(output, m_vmCharset));
	else
		return vmime::text(output, m_vmCharset);
}

/**
 * Shortcut for common conversion used in this file.
 * Note: Uses class members m_converter, m_vmCharset and m_strCharset.
 * 
 * @param[in]	lpszwInput	input string in std::wstring
 * @return	the converted text from WCHAR to vmime::text with specified charset
 */
vmime::text MAPIToVMIME::getVmimeTextFromWide(const std::wstring& strwInput, bool bWrapInWord) {
	std::string output = m_converter.convert_to<std::string>(m_strCharset.c_str(), strwInput, rawsize(strwInput), CHARSET_WCHAR);
	if (bWrapInWord)
		return vmime::text(vmime::word(output, m_vmCharset));
	else
		return vmime::text(output, m_vmCharset);
}

} /* namespace */
