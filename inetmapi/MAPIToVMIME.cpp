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
#include <string>
#include <vector>
#include <kopano/MAPIErrors.h>

// Damn windows header defines max which break C++ header files
#undef max

// vmime
#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/contentTypeField.hpp>
#include <vmime/parsedMessageAttachment.hpp>

// mapi
#include <mapi.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <edkmdb.h>
#include <kopano/mapiguidext.h>
#include <kopano/mapi_ptr.h>
#include "tnef.h"

// inetmapi
#include "ECMapiUtils.h"
#include "MAPIToVMIME.h"
#include "VMIMEToMAPI.h"
#include "outputStreamMAPIAdapter.h"
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
#include "SMIMEMessage.h"

// icalmapi
#include "MAPIToICal.h"

using namespace std;

/* These are the properties that we DON'T send through TNEF. Reasons can be:
 *
 * - Privacy (PR_MDB_PROVIDER, etc)
 * - Sent through rfc2822 (PR_SUBJECT, PR_BODY, etc)
 * - Not part of an outgoing message (PR_TRANSPORT_MESSAGE_HEADERS)
 */

SizedSPropTagArray(1, sptaBestBodyInclude) = {1, {PR_RTF_COMPRESSED} };

// Since UNICODE is defined, the strings will be PT_UNICODE, as required by ECTNEF::AddProps()
SizedSPropTagArray(54, sptaExclude) = {
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
MAPIToVMIME::MAPIToVMIME()
{
	srand((unsigned)time(NULL));
	lpLogger = new ECLogger_Null();
	m_lpAdrBook = NULL;
	imopt_default_sending_options(&sopt);
}

/**
 * @param[in]	lpSession	current mapi session, used to open contact entryid's
 * @param[in]	lpAddrBook	global addressbook
 * @param[in]	newlogger	logger object
 * @param[in]	sopt		struct with optional settings to change conversion
 */
MAPIToVMIME::MAPIToVMIME(IMAPISession *lpSession, IAddrBook *lpAddrBook, ECLogger *newlogger, sending_options sopt)
{
	srand((unsigned)time(NULL));
	lpLogger = newlogger;
	if (!lpLogger)
		lpLogger = new ECLogger_Null();
	else
		lpLogger->AddRef();
	
	this->sopt = sopt;
	if (lpSession && !lpAddrBook) {
		lpSession->OpenAddressBook(0, NULL, AB_NO_DIALOG, &m_lpAdrBook);
		// ignore error
	} else {
		lpAddrBook->QueryInterface(IID_IAddrBook, (void**)&m_lpAdrBook);
	}

	m_lpSession = lpSession;
}

MAPIToVMIME::~MAPIToVMIME()
{
	lpLogger->Release();

	if (m_lpAdrBook)
		m_lpAdrBook->Release();
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
	HRESULT			hr					= hrSuccess;
	vmime::ref<vmime::address>	vmMailbox;
	LPMAPITABLE		lpRecipientTable	= NULL;
	LPSRowSet		pRows				= NULL;
	LPSPropValue	pPropRecipType		= NULL;
	LPSPropValue	pPropDispl			= NULL;
	LPSPropValue	pPropAType			= NULL;
	LPSPropValue	pPropEAddr			= NULL;
	bool			fToFound			= false;
	bool			hasRecips			= false;
	
	SizedSPropTagArray(7, sPropRecipColumns) = {7, { PR_ENTRYID, PR_EMAIL_ADDRESS_W, PR_DISPLAY_NAME_W, PR_RECIPIENT_TYPE, PR_SMTP_ADDRESS_W, PR_ADDRTYPE_W, PR_OBJECT_TYPE} };

	hr = lpMessage->GetRecipientTable(MAPI_UNICODE | MAPI_DEFERRED_ERRORS, &lpRecipientTable);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open recipient table. Error: 0x%08X", hr);
		goto exit;
	}

	hr = lpRecipientTable->SetColumns((LPSPropTagArray)&sPropRecipColumns, 0);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to set columns on recipient table. Error: 0x%08X", hr);
		goto exit;
	}

	hr = HrQueryAllRows(lpRecipientTable, NULL, NULL, NULL, 0, &pRows);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to read recipient table. Error: 0x%08X", hr);
		goto exit;
	}

	try {
		for (ULONG i = 0; i < pRows->cRows; ++i) {
			pPropRecipType = PpropFindProp(pRows->aRow[i].lpProps, pRows->aRow[i].cValues, PR_RECIPIENT_TYPE);

			if(pPropRecipType == NULL) {
				// getMailBox properties
				pPropDispl = PpropFindProp(pRows->aRow[i].lpProps, pRows->aRow[i].cValues, PR_DISPLAY_NAME_W);
				pPropAType = PpropFindProp(pRows->aRow[i].lpProps, pRows->aRow[i].cValues, PR_ADDRTYPE_W);
				pPropEAddr = PpropFindProp(pRows->aRow[i].lpProps, pRows->aRow[i].cValues, PR_EMAIL_ADDRESS_W);
				lpLogger->Log(EC_LOGLEVEL_ERROR, "No recipient type set for recipient. DisplayName: %ls, AddrType: %ls, Email: %ls",
							  pPropDispl?pPropDispl->Value.lpszW:L"(none)",
							  pPropAType?pPropAType->Value.lpszW:L"(none)",
							  pPropEAddr?pPropEAddr->Value.lpszW:L"(none)");
				continue;
			}

			hr = getMailBox(&pRows->aRow[i], &vmMailbox);
			if (hr == MAPI_E_INVALID_PARAMETER)	// skip invalid addresses
				continue;
			if (hr != hrSuccess)
				goto exit;			// Logging done in getMailBox

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
			if (!hasRecips && sopt.no_recipients_workaround == false) {
				// spooler will exit here, gateway will continue with the workaround
				lpLogger->Log(EC_LOGLEVEL_ERROR, "No valid recipients found.");
				hr = MAPI_E_NOT_FOUND;
				goto exit;
			}

			// No recipients were in the 'To' .. add 'undisclosed recipients'
			vmime::ref<vmime::mailboxGroup> undisclosed = vmime::create<vmime::mailboxGroup>(vmime::text("undisclosed-recipients"));
			lpVMMessageBuilder->getRecipients().appendAddress(undisclosed);
		}
			
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception: %s", e.what());
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
	if (lpRecipientTable)
		lpRecipientTable->Release();

	if (pRows)
		FreeProws(pRows);

	return hr;
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
	IStream*		lpStream			= NULL; 
	LPATTACH		lpAttach			= NULL;
	LPSPropValue	pPropAttachNum		= NULL;
	LPSPropValue	pPropAttachType		= NULL;
	LPSPropValue	lpContentId 		= NULL;
	LPSPropValue	lpContentLocation	= NULL;
	LPSPropValue	lpHidden			= NULL;
	LPSPropValue	lpFilename			= NULL;
	ULONG			ulAttachmentNum		= 0;
	ULONG			ulAttachmentMethod	= 0;
	IMessage*		lpAttachedMessage	= NULL;

	vmime::ref<vmime::utility::inputStream> inputDataStream = NULL;
	vmime::ref<mapiAttachment> vmMapiAttach = NULL;
	vmime::ref<vmime::attachment> vmMsgAtt = NULL;
	std::string		strContentId;
	std::string		strContentLocation;
	bool			bHidden = false;
	sending_options sopt_keep;
	LPSPropValue	lpAMClass = NULL;
	LPSPropValue	lpAMAttach = NULL;
	LPSPropValue	lpMIMETag = NULL;
	const wchar_t *szFilename = NULL;  // just a reference, don't free
	vmime::mediaType vmMIMEType;
	std::string		strBoundary;
	bool			bSendBinary = true;
	MapiToICal		*mapiical = NULL;

	pPropAttachNum = PpropFindProp(lpRow->lpProps, lpRow->cValues, PR_ATTACH_NUM);
	if (pPropAttachNum == NULL) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Attachment in table not correct, no attachment number present.");
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	ulAttachmentNum = pPropAttachNum->Value.ul;

	// check PR_ATTACH_METHOD to determine Attachment or email
	ulAttachmentMethod = ATTACH_BY_VALUE;
	pPropAttachType	= PpropFindProp(lpRow->lpProps, lpRow->cValues, PR_ATTACH_METHOD);
	if (pPropAttachType == NULL) {
		lpLogger->Log(EC_LOGLEVEL_WARNING, "Attachment method not present for attachment %d, assuming default value", ulAttachmentNum);
	} else {
		ulAttachmentMethod = pPropAttachType->Value.ul;
	}

	if (ulAttachmentMethod == ATTACH_EMBEDDED_MSG) {
		vmime::ref<vmime::message> vmNewMess;
		std::string strBuff;
		vmime::utility::outputStreamStringAdapter mout(strBuff);

		hr = lpMessage->OpenAttach(ulAttachmentNum, NULL, MAPI_BEST_ACCESS, &lpAttach);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not open message attachment %d. Error: 0x%08X", ulAttachmentNum, hr);
			goto exit;
		}

		hr = lpAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_DEFERRED_ERRORS, (LPUNKNOWN *)&lpAttachedMessage);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not open data of message attachment %d. Error: 0x%08X", ulAttachmentNum, hr);
			goto exit;
		}

		// Check whether we're sending a calendar object
		// if so, we do not need to create a message-in-message object, but just attach the ics file.
		hr = HrGetOneProp(lpAttachedMessage, PR_MESSAGE_CLASS_A, &lpAMClass);
		if (hr == hrSuccess &&
			strcmp(lpAMClass->Value.lpszA, "IPM.OLE.CLASS.{00061055-0000-0000-C000-000000000046}") == 0)
		{
				// This is an exception message. We might get here because kopano-ical is able to
				// SubmitMessage for Mac Ical meeting requests. The way Outlook does this, is
				// send a message for each exception itself. We just ignore this exception, since
				// it's already in the main ical data on the top level message.
				goto exit;
		}

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
		if (hr != hrSuccess) {
			// Logging has been done by convertMAPIToVMIME()
			goto exit;
		}
		sopt = sopt_keep;

		vmMsgAtt = vmime::create<vmime::parsedMessageAttachment>(vmNewMess);
		lpVMMessageBuilder->appendAttachment(vmMsgAtt);
	} else if (ulAttachmentMethod == ATTACH_BY_VALUE) {
		hr = lpMessage->OpenAttach(ulAttachmentNum, NULL, MAPI_BEST_ACCESS, &lpAttach);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not open attachment %d. Error: 0x%08X", ulAttachmentNum, hr);
			goto exit;
		}
		
		if (HrGetOneProp(lpAttach, PR_ATTACH_CONTENT_ID_A, &lpContentId) == hrSuccess)
			strContentId = lpContentId->Value.lpszA;

		if (HrGetOneProp(lpAttach, PR_ATTACH_CONTENT_LOCATION_A, &lpContentLocation) == hrSuccess)
			strContentLocation = lpContentLocation->Value.lpszA;
			
		if (HrGetOneProp(lpAttach, PR_ATTACHMENT_HIDDEN, &lpHidden) == hrSuccess)
			bHidden = lpHidden->Value.b;

        // Optimize: if we're only outputting headers, we needn't bother with attachment data
        if(sopt.headers_only)
            CreateStreamOnHGlobal(NULL, true, &lpStream);
        else {
            hr = lpAttach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0, MAPI_DEFERRED_ERRORS, (LPUNKNOWN *)&lpStream);
            if (hr != hrSuccess) {
                lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not open data of attachment %d. Error: 0x%08X", ulAttachmentNum, hr);
                goto exit;
            }
        }
        	
		try {
			inputDataStream = vmime::create<inputStreamMAPIAdapter>(lpStream);	// add ref to lpStream

			// Set filename
			szFilename = L"data.bin";

			if (HrGetOneProp(lpAttach, PR_ATTACH_LONG_FILENAME_W, &lpFilename) == hrSuccess) {
				szFilename = lpFilename->Value.lpszW;
			} else {
				if (HrGetOneProp(lpAttach, PR_ATTACH_FILENAME_W, &lpFilename) == hrSuccess) {
					szFilename = lpFilename->Value.lpszW;
				}
			}
            
            // Set MIME type
            parseMimeTypeFromFilename(szFilename, &vmMIMEType, &bSendBinary);
            
			if (HrGetOneProp(lpAttach, PR_ATTACH_MIME_TAG_A, &lpMIMETag) == hrSuccess) {
				vmMIMEType = lpMIMETag->Value.lpszA;
			}

			// inline attachments: Only make attachments inline if they are hidden and have a content-id or content-location.
			if ((!strContentId.empty() || !strContentLocation.empty()) && bHidden &&
				lpVMMessageBuilder->getTextPart()->getType() == vmime::mediaType(vmime::mediaTypes::TEXT, vmime::mediaTypes::TEXT_HTML))
			{
				// add inline object to html part
				vmime::mapiTextPart& textPart = dynamic_cast<vmime::mapiTextPart&>(*lpVMMessageBuilder->getTextPart());
				// had szFilename .. but how, on inline?
				// @todo find out how Content-Disposition receives highchar filename... always utf-8?
				textPart.addObject(vmime::create<vmime::streamContentHandler>(inputDataStream, 0), vmime::encoding("base64"), vmMIMEType, strContentId, string(), strContentLocation);
			} else {
				vmMapiAttach = vmime::create<mapiAttachment>(vmime::create<vmime::streamContentHandler>(inputDataStream, 0),
															 bSendBinary ? vmime::encoding("base64") : vmime::encoding("quoted-printable"),
															 vmMIMEType, strContentId,
															 vmime::word(m_converter.convert_to<string>(m_strCharset.c_str(), szFilename, rawsize(szFilename), CHARSET_WCHAR), m_vmCharset));

				// add to message (copies pointer, not data)
				lpVMMessageBuilder->appendAttachment(vmMapiAttach); 
			}
		}
		catch (vmime::exception& e) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception: %s", e.what());
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
	} else if (ulAttachmentMethod == ATTACH_OLE) {
	    // Ignore ATTACH_OLE attachments, they are handled in handleTNEF()
	} else {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Attachment %d contains invalid method %d.", ulAttachmentNum, ulAttachmentMethod);
		hr = MAPI_E_INVALID_PARAMETER;
	}

exit:
	// ATTN: lpMapiAttach are linked in the VMMessageBuilder. The VMMessageBuilder will delete() it.
	delete mapiical;
	MAPIFreeBuffer(lpMIMETag);
	MAPIFreeBuffer(lpAMClass);
	MAPIFreeBuffer(lpAMAttach);
	MAPIFreeBuffer(lpFilename);
	MAPIFreeBuffer(lpContentId);
	MAPIFreeBuffer(lpContentLocation);
	MAPIFreeBuffer(lpHidden);
	if (lpStream)
		lpStream->Release();

	if (lpAttach)
		lpAttach->Release();

	if (lpAttachedMessage)
		lpAttachedMessage->Release();

	return hr;
}

/**
 * Return an mimetype for filename of an attachment.
 *
 * Mimetype is found by using the extension. Also returns if the
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
	std::string strExt;
	std::string strMedType;
	bool bSendBinary = true;

	// to lowercase
	transform(strFilename.begin(), strFilename.end(), strFilename.begin(), ::tolower);
	strExt = m_converter.convert_to<string>(m_strCharset.c_str(), strFilename, rawsize(strFilename), CHARSET_WCHAR);
	strExt.erase(0, strExt.find_last_of(".")+1);

	// application
	if (strExt == "bin" || strExt == "exe") {
		strMedType = "application/octet-stream";
	} else if (strExt == "ai" || strExt == "eps" || strExt == "ps") {
		strMedType = "application/postscript";
	} else if (strExt == "pdf") {
		strMedType = "application/pdf";
	} else if (strExt == "rtf") {
		strMedType = "application/rtf";
	} else if (strExt == "zip") {
		strMedType = "application/zip";
	} else if (strExt == "doc" || strExt == "dot") {
		strMedType = "application/msword";
	} else if (strExt == "mdb") {
		strMedType = "application/x-msaccess";
	} else if (strExt == "xla" || strExt == "xls" || strExt == "xlt" || strExt == "xlw") {
		strMedType = "application/vnd.ms-excel";
	} else if (strExt == "pot" || strExt == "ppt" || strExt == "pps") {
		strMedType = "application/vnd.ms-powerpoint";
	} else if (strExt == "mpp") {
		strMedType = "application/vnd.ms-project";
	} else if (strExt == "edi") {
		strMedType = "application/edifact";
		bSendBinary = false;
	} else if(strExt == "docm") {
		strMedType = "application/vnd.ms-word.document.macroEnabled.12";
	} else if(strExt == "docx") {
		strMedType = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
	} else if(strExt == "dotm") {
		strMedType = "application/vnd.ms-word.template.macroEnabled.12";
	} else if(strExt == "dotx") {
		strMedType = "application/vnd.openxmlformats-officedocument.wordprocessingml.template";
	} else if(strExt == "potm") {
		strMedType = "application/vnd.ms-powerpoint.template.macroEnabled.12";
	} else if(strExt == "potx") {
		strMedType = "application/vnd.openxmlformats-officedocument.presentationml.template";
	} else if(strExt == "ppam") {
		strMedType = "application/vnd.ms-powerpoint.addin.macroEnabled.12";
	} else if(strExt == "ppsm") {
		strMedType = "application/vnd.ms-powerpoint.slideshow.macroEnabled.12";
	} else if(strExt == "ppsx") {
		strMedType = "application/vnd.openxmlformats-officedocument.presentationml.slideshow";
	} else if(strExt == "pptm") {
		strMedType = "application/vnd.ms-powerpoint.presentation.macroEnabled.12";
	} else if(strExt == "pptx") {
		strMedType = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
	} else if(strExt == "xlam") {
		strMedType = "application/vnd.ms-excel.addin.macroEnabled.12";
	} else if(strExt == "xlsb") {
		strMedType = "application/vnd.ms-excel.sheet.binary.macroEnabled.12";
	} else if(strExt == "xlsm") {
		strMedType = "application/vnd.ms-excel.sheet.macroEnabled.12";
	} else if(strExt == "xltm") {
		strMedType = "application/vnd.ms-excel.template.macroEnabled.12";
	} else if(strExt == "xlsx") {
		strMedType = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
	} else if(strExt == "xltx") {
		strMedType = "application/vnd.openxmlformats-officedocument.spreadsheetml.template";
	}
	
	// audio

	else if (strExt == "ua") {
		strMedType = "audio/basic";
	} else if (strExt == "wav") {
		strMedType = "audio/x-wav";
	} else if (strExt == "mid") {
		strMedType = "audio/x-midi";
	}
	
	// image

	else if (strExt == "gif") {
		strMedType = "image/gif";
	} else if (strExt == "jpg" || strExt == "jpe" || strExt == "jpeg") {
		strMedType = "image/jpeg";
	} else if (strExt == "png") {
		strMedType = "image/png";
	} else if (strExt == "bmp") {
		strMedType = "image/x-ms-bmp";
	} else if (strExt == "tiff") {
		strMedType = "image/tiff";
	} else if (strExt == "xbm") {
		strMedType = "image/xbm";
	}
	
	// video

	else if (strExt == "mpg" || strExt == "mpe" || strExt == "mpeg") {
		strMedType = "video/mpeg";
	} else if (strExt == "qt" || strExt == "mov") {
		strMedType = "video/quicktime";
	} else if (strExt == "avi") {
		strMedType = "video/x-msvideo";
	}

	else {
		strMedType = "application/octet-stream";
	}

	*lpMT = vmime::mediaType(strMedType);
	*lpbSendBinary = bSendBinary;

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
	HRESULT		hr					= hrSuccess;
	LPSRowSet	pRows				= NULL;
	LPMAPITABLE	lpAttachmentTable	= NULL;
	SizedSSortOrderSet(1, sosRTFSeq) = {1, 0, 0, { {PR_RENDERING_POSITION, TABLE_SORT_ASCEND} } };

	// get attachment table
	hr = lpMessage->GetAttachmentTable(0, &lpAttachmentTable);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open attachment table. Error: 0x%08X", hr);
		goto exit;
	}

	hr = HrQueryAllRows(lpAttachmentTable, NULL, NULL, (LPSSortOrderSet)&sosRTFSeq, 0, &pRows);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to fetch rows of attachment table. Error: 0x%08X", hr);
		goto exit;
	}
	
	for (ULONG i = 0; i < pRows->cRows; ++i) {
		// if one attachment fails, we're not sending what the user intended to send so abort. Logging was done in handleSingleAttachment()
		hr = handleSingleAttachment(lpMessage, &pRows->aRow[i], lpVMMessageBuilder);
		if (hr != hrSuccess)
			goto exit;
	} 

exit:
	if(pRows)
		FreeProws(pRows);

	if (lpAttachmentTable)
		lpAttachmentTable->Release();

	return hr;
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
HRESULT MAPIToVMIME::setBoundaries(vmime::ref<vmime::header> vmHeader, vmime::ref<vmime::body> vmBody, const std::string& boundary)
{
	int i = 0;
	vmime::ref<vmime::contentTypeField> vmCTF;

	try {
		vmCTF = vmHeader->findField(vmime::fields::CONTENT_TYPE).dynamicCast <vmime::contentTypeField>();
	}
	catch (vmime::exceptions::no_such_field) {
		return hrSuccess;
	}

	if(vmCTF->getValue().dynamicCast<vmime::mediaType>()->getType() == vmime::mediaTypes::MULTIPART) {
		// This is a multipart, so set the boundary for this part
		vmCTF->setBoundary(boundary);
        
		// Set boundaries on children also
		for (i = 0; i < vmBody->getPartCount(); ++i) {
			std::ostringstream os;

			vmime::ref<vmime::bodyPart> vmBodyPart = vmBody->getPartAt(i);

			os << boundary << "_" << i;
            
			setBoundaries(vmBodyPart->getHeader(), vmBodyPart->getBody(), os.str());
		}
	}
	return hrSuccess;
}

/**
 * Build a normal vmime message from an MAPI lpMessage.
 *
 * @param[in]	lpMessage		MAPI message to convert
 * @param[in]	bSkipContent	set to true if only the headers of the e-mail are required to obtain
 * @param[in]	charset			Charset to convert message in, valid for headers and bodies.
 * @param[out]	lpvmMessage		vmime message version of lpMessage
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::BuildNoteMessage(IMessage *lpMessage, bool bSkipContent, vmime::ref<vmime::message> *lpvmMessage)
{
	HRESULT					hr					= hrSuccess;
	LPSPropValue			lpDeliveryDate		= NULL;
	LPSPropValue			lpTransportHeaders	= NULL;
	vmime::messageBuilder   vmMessageBuilder;
	vmime::ref<vmime::message>			vmMessage;

	// construct the message..
	try {
		// messageBuilder is body and simple headers only (to/cc/subject/...)
		hr = fillVMIMEMail(lpMessage, bSkipContent, &vmMessageBuilder);
		if (hr != hrSuccess)
			goto exit;			// Logging has been done in fillVMIMEMail()

		vmMessage = vmMessageBuilder.construct();
		// message builder has set Date header to now(). this will be overwritten.

		vmime::ref<vmime::header> vmHeader = vmMessage->getHeader();

		hr = handleExtraHeaders(lpMessage, vmHeader);
		if (hr!= hrSuccess)
			goto exit;

		// from/sender headers
		hr = handleSenderInfo(lpMessage, vmHeader);
		if (hr!= hrSuccess)
			goto exit;

		// add reply to email, ignore errors
		hr = handleReplyTo(lpMessage, vmHeader);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to set reply-to address");
			hr = hrSuccess;
		}

		// Set consistent boundary (optional)
		if(sopt.alternate_boundary) {
			setBoundaries(vmMessage->getHeader(), vmMessage->getBody(), sopt.alternate_boundary);
		}

		HrGetOneProp(lpMessage, PR_MESSAGE_DELIVERY_TIME, &lpDeliveryDate);

		// If we're sending a msg-in-msg, use the original date of that message
		if (sopt.msg_in_msg && lpDeliveryDate) {
			vmHeader->Date()->setValue(FiletimeTovmimeDatetime(lpDeliveryDate->Value.ft));
		}
		
		// Regenerate some headers if available (basically a copy of the headers in
		// PR_TRANSPORT_MESSAGE_HEADERS)
		// currently includes: Received*, Return-Path, List* and Precedence.
		// New e-mails should not have this property.
		HrGetOneProp(lpMessage, PR_TRANSPORT_MESSAGE_HEADERS_A, &lpTransportHeaders);

		if(lpTransportHeaders) {
			try {
				int j=0;
				vmime::header headers;
				std::string strHeaders = lpTransportHeaders->Value.lpszA;
				strHeaders += "\r\n\r\n";
				headers.parse(strHeaders);
				
				for (int i = 0; i < headers.getFieldCount(); ++i) {
					vmime::ref<vmime::headerField> vmField = headers.getFieldAt(i);
					std::string name = vmField->getName();

					// Received checks start of string to accept Received-SPF
					if (strncasecmp(name.c_str(), vmime::fields::RECEIVED, strlen(vmime::fields::RECEIVED)) == 0 ||
						strcasecmp(name.c_str(), vmime::fields::RETURN_PATH) == 0) {
						// Insert in same order at start of headers
						vmHeader->insertFieldBefore(j++, vmField);
					} else if (strncasecmp(name.c_str(), "list-", strlen("list-")) == 0 ||
							   strcasecmp(name.c_str(), "precedence") == 0) {
						// Just append at the end of this list, order is not important
						vmHeader->appendField(vmField->clone().dynamicCast<vmime::headerField>());
					}
				}
			} catch (vmime::exception& e) {
				lpLogger->Log(EC_LOGLEVEL_WARNING, "VMIME exception adding extra headers: %s", e.what());
			}
			catch(...) { 
				// If we can't parse, just ignore
			}
		}

		// POP3 wants the delivery date as received header to correctly show in outlook
		if (sopt.add_received_date && lpDeliveryDate) {
			vmime::headerFieldFactory* hff = vmime::headerFieldFactory::getInstance();
			vmime::ref<vmime::headerField> rf = hff->create(vmime::fields::RECEIVED);
			rf->getValue().dynamicCast<vmime::relay>()->setDate(FiletimeTovmimeDatetime(lpDeliveryDate->Value.ft));
			// set received header at the start
			vmHeader->insertFieldBefore(0, rf);
		}

		// no hr checking
		handleXHeaders(lpMessage, vmHeader);
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception: %s", e.what());
		hr = MAPI_E_CALL_FAILED; // set real error
		goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on note message: %s", e.what());
		hr = MAPI_E_CALL_FAILED; // set real error
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception on note message");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	*lpvmMessage = vmMessage;

exit:
	MAPIFreeBuffer(lpTransportHeaders);
	MAPIFreeBuffer(lpDeliveryDate);
	return hr;
}

/**
 * Build an MDN vmime message from an MAPI lpMessage.
 *
 * MDN (Mail Disposition Notification) is a read receipt message,
 * which notifies a sender that the e-mail was read.
 *
 * @param[in]	lpMessage		MAPI message to convert
 * @param[in]	charset			Charset to convert message in, valid for headers and bodies.
 * @param[out]	lpvmMessage		vmime message version of lpMessage
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::BuildMDNMessage(IMessage *lpMessage, vmime::ref<vmime::message> *lpvmMessage)
{
	HRESULT				hr = hrSuccess;
	vmime::ref<vmime::message>	vmMessage = NULL;
	IStream*			lpBodyStream = NULL;
	LPSPropValue		lpiNetMsgId = NULL;
	LPSPropValue		lpMsgClass = NULL;
	LPSPropValue		lpSubject = NULL;
	LPMAPITABLE			lpRecipientTable	= NULL;
	LPSRowSet			pRows				= NULL;
	
	vmime::mailbox		expeditor; // From
	string				strMDNText;
	vmime::disposition	dispo;
	string				reportingUA; //empty
	std::vector<string>	reportingUAProducts; //empty
	vmime::ref<vmime::message>	vmMsgOriginal;
	vmime::ref<vmime::address>	vmRecipientbox;
	string				strActionMode;
	std::wstring		strOut;

	// sender information
	std::wstring strEmailAdd, strName, strType;
	std::wstring strRepEmailAdd, strRepName, strRepType;

	if (lpMessage == NULL || m_lpAdrBook == NULL || lpvmMessage == NULL) {
		hr = MAPI_E_INVALID_OBJECT;
		goto exit;
	}

	try {
		vmMsgOriginal = vmime::create<vmime::message>();

		// Create original vmime message
		if (HrGetOneProp(lpMessage, PR_INTERNET_MESSAGE_ID_A, &lpiNetMsgId) == hrSuccess)
		{
			vmMsgOriginal->getHeader()->OriginalMessageId()->setValue(lpiNetMsgId->Value.lpszA);
			vmMsgOriginal->getHeader()->MessageId()->setValue(lpiNetMsgId->Value.lpszA);
		}
		
		// Create Recipient
		hr = lpMessage->GetRecipientTable(MAPI_DEFERRED_ERRORS, &lpRecipientTable);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open MDN recipient table. Error: 0x%08X", hr);
			goto exit;
		}

		hr = HrQueryAllRows(lpRecipientTable, NULL, NULL, NULL, 0, &pRows);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to read MDN recipient table. Error: 0x%08X", hr);
			goto exit;
		}

		if (pRows->cRows == 0) {
			if (sopt.no_recipients_workaround == false) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "No MDN recipient found");
				hr = MAPI_E_NOT_FOUND;
				goto exit;
			} else {
				// no recipient, but need to continue ... is this correct??
				vmRecipientbox = vmime::create<vmime::mailbox>(string("undisclosed-recipients"));
			}
		} else {
			hr = getMailBox(&pRows->aRow[0], &vmRecipientbox);
			if (hr != hrSuccess)
				goto exit;			// Logging done in getMailBox
		}

		// if vmRecipientbox is a vmime::mailboxGroup the dynamicCast to vmime::mailbox failed,
		// so never send a MDN to a group.
		if (vmRecipientbox->isGroup()) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Not possible to send a MDN to a group");
			hr = MAPI_E_CORRUPT_DATA;
			goto exit;
		}

		vmime::mdn::sendableMDNInfos mdnInfos(vmMsgOriginal, *vmRecipientbox.dynamicCast<vmime::mailbox>().get());
		
		//FIXME: Get the ActionMode and sending mode from the property 0x0081001E.
		//		And the type in property 0x0080001E.

		// Vmime is broken, The RFC 3798 says: "The action type can be 'manual-action' or 'automatic-action'"
		// but VMime can set only 'manual' or 'automatic' so should be add the string '-action'
		strActionMode = vmime::dispositionActionModes::MANUAL;
		strActionMode+= "-action";

		dispo.setActionMode(strActionMode);
		dispo.setSendingMode(vmime::dispositionSendingModes::SENT_MANUALLY);

		if (HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &lpMsgClass) != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "MDN message has no class.");
			hr = MAPI_E_CORRUPT_DATA;
			goto exit;
		}

		if(strcasecmp(lpMsgClass->Value.lpszA, "REPORT.IPM.Note.IPNNRN") == 0)
			dispo.setType(vmime::dispositionTypes::DELETED);
		else // if(strcasecmp(lpMsgClass->Value.lpszA, "REPORT.IPM.Note.IPNRN") == 0)
			dispo.setType(vmime::dispositionTypes::DISPLAYED);		
	
		strMDNText.clear();// Default Empty body

		hr = lpMessage->OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, (IUnknown**)&lpBodyStream);
		if (hr == hrSuccess) {
			std::wstring strBuffer;
			hr = Util::HrStreamToString(lpBodyStream, strBuffer);
			if (hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to read MDN message body.");
				goto exit;
			}
			strMDNText = m_converter.convert_to<string>(m_strCharset.c_str(), strBuffer, rawsize(strBuffer), CHARSET_WCHAR);
		}

		// Store owner, actual sender
		hr = HrGetAddress(m_lpAdrBook, lpMessage, PR_SENDER_ENTRYID, PR_SENDER_NAME_W, PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS_W, strName, strType, strEmailAdd);
		if(hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get MDN sender information. Error: 0x%08X", hr);
			goto exit;
		}
		
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
		if (HrGetOneProp(lpMessage, PR_SUBJECT_W, &lpSubject) == hrSuccess) {
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

	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception: %s", e.what());
		hr = MAPI_E_CALL_FAILED; // set real error
		goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on MDN message: %s", e.what());
		hr = MAPI_E_CALL_FAILED; // set real error
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception on MDN message");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	*lpvmMessage = vmMessage;

exit:
	if(lpBodyStream)
		lpBodyStream->Release();
	MAPIFreeBuffer(lpiNetMsgId);
	MAPIFreeBuffer(lpMsgClass);
	if(lpRecipientTable)
		lpRecipientTable->Release();
	MAPIFreeBuffer(lpSubject);
	if(pRows)
		FreeProws(pRows);

	return hr;
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
HRESULT MAPIToVMIME::convertMAPIToVMIME(IMessage *lpMessage, vmime::ref<vmime::message> *lpvmMessage) {
	HRESULT					hr					= hrSuccess;
	LPSPropValue			lpInternetCPID		= NULL;
	LPSPropValue			lpMsgClass			= NULL;
	vmime::ref<vmime::message> 	vmMessage;
	const char *lpszCharset = NULL;
	char *					lpszRawSMTP			= NULL;
	LPMAPITABLE				lpAttachmentTable	= NULL;
	LPSRowSet				lpRows				= NULL;
	LPSPropValue			lpPropAttach		= NULL;
	LPATTACH				lpAttach			= NULL;
	LPSTREAM				lpStream			= NULL;
	STATSTG					sStreamStat;
	SizedSPropTagArray(2, sPropAttachColumns) = {2, { PR_ATTACH_NUM, PR_ATTACH_MIME_TAG} };

	if (HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &lpMsgClass) != hrSuccess) {
		if ((hr = MAPIAllocateBuffer(sizeof(SPropValue), (void**)&lpMsgClass)) != hrSuccess)
			goto exit;
		lpMsgClass->ulPropTag = PR_MESSAGE_CLASS_A;
		lpMsgClass->Value.lpszA = const_cast<char *>("IPM.Note");
	}

	// Get the outgoing charset we want to be using
	if (HrGetOneProp(lpMessage, PR_INTERNET_CPID, &lpInternetCPID) == hrSuccess &&
		HrGetCharsetByCP(lpInternetCPID->Value.ul, &lpszCharset) == hrSuccess)
	{
		m_strHTMLCharset = lpszCharset;
		if (sopt.force_utf8)
			m_vmCharset = MAPI_CHARSET_STRING;
		else
			m_vmCharset = m_strHTMLCharset;
	} else {
		// default to utf-8 if not set
		m_vmCharset = MAPI_CHARSET_STRING;
	}

	if (strncasecmp(lpMsgClass->Value.lpszA, "IPM.Note", 8) && strncasecmp(lpMsgClass->Value.lpszA, "REPORT.IPM.Note", 15)) {
		// Outlook sets some other incorrect charset for meeting requests and such,
		// so for non-email we upgrade this to utf-8
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
		hr = BuildMDNMessage(lpMessage, &vmMessage);
		if(hr != hrSuccess)
			goto exit;		
	} else if ((strcasecmp(lpMsgClass->Value.lpszA, "IPM.Note.SMIME.MultiPartSigned") == 0) ||
			   (strcasecmp(lpMsgClass->Value.lpszA, "IPM.Note.SMIME") == 0))
	{
		// - find attachment, and convert to char, and place in lpszRawSMTP
		// - normal convert the message, but only from/to headers and such .. nothing else

		hr = lpMessage->GetAttachmentTable(0, &lpAttachmentTable);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not get attachment table of signed attachment. Error: 0x%08X", hr);
			goto exit;
		}

		// set columns to get pr attach mime tag and pr attach num only.
		hr = lpAttachmentTable->SetColumns((LPSPropTagArray)&sPropAttachColumns, 0);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could set table contents of attachment table of signed attachment. Error: 0x%08X", hr);
			goto exit;
		}

		hr = HrQueryAllRows(lpAttachmentTable, NULL, NULL, NULL, 0, &lpRows);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not get table contents of attachment table of signed attachment. Error: 0x%08X", hr);
			goto exit;
		}

		if (lpRows->cRows != 1)
			goto normal;

		lpPropAttach = PpropFindProp(lpRows->aRow[0].lpProps, lpRows->aRow[0].cValues, PR_ATTACH_MIME_TAG);
		if (!lpPropAttach)
			goto normal;

		lpPropAttach = PpropFindProp(lpRows->aRow[0].lpProps, lpRows->aRow[0].cValues, PR_ATTACH_NUM);
		if (!lpPropAttach)
			goto normal;

		hr = lpMessage->OpenAttach(lpPropAttach->Value.ul, NULL, MAPI_BEST_ACCESS, &lpAttach);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not open signed attachment. Error: 0x%08X", hr);
			goto exit;
		}

		hr = lpAttach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0, MAPI_DEFERRED_ERRORS, (LPUNKNOWN *)&lpStream);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not open data of signed attachment. Error: 0x%08X", hr);
			goto exit;
		}

		hr = lpStream->Stat(&sStreamStat, 0);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not find size of signed attachment. Error: 0x%08X", hr);
			goto exit;
		}

		lpszRawSMTP = new char[(ULONG)sStreamStat.cbSize.QuadPart+1];

		hr = lpStream->Read(lpszRawSMTP, (ULONG)sStreamStat.cbSize.QuadPart, NULL);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Could not read data of signed attachment. Error: 0x%08X", hr);
			goto exit;
		}
		lpszRawSMTP[(ULONG)sStreamStat.cbSize.QuadPart] = '\0';

		// build the message, but without the bodies and attachments
		hr = BuildNoteMessage(lpMessage, true, &vmMessage);
		if (hr != hrSuccess)
			goto exit;

		// remove excess headers
		vmMessage->getHeader()->removeField(vmMessage->getHeader()->findField(vmime::fields::CONTENT_TYPE));
		vmMessage->getHeader()->removeField(vmMessage->getHeader()->findField(vmime::fields::CONTENT_TRANSFER_ENCODING));

		if (strcasecmp(lpMsgClass->Value.lpszA, "IPM.Note.SMIME") != 0) {
			std::string strRawSMTP = lpszRawSMTP;
			vmime::ref<SMIMEMessage> vmSMIMEMessage = vmime::create<SMIMEMessage>();
			
			// not sure why this was needed, and causes problems, eg ZCP-12994.
			//vmMessage->getHeader()->removeField(vmMessage->getHeader()->findField(vmime::fields::MIME_VERSION));
			
			*vmSMIMEMessage->getHeader() = *vmMessage->getHeader();
			vmSMIMEMessage->setSMIMEBody(strRawSMTP);
			
			vmMessage = vmSMIMEMessage;
		} else {
			// encoded mail, set data as only mail body
			LPMAPINAMEID lpNameID = NULL;
			LPSPropTagArray lpPropTags = NULL;
			LPSPropValue lpPropContentType = NULL;
			const char *lpszContentType = NULL;

			hr = MAPIAllocateBuffer(sizeof(MAPINAMEID), (void**)&lpNameID);
			if (hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Not enough memory. Error: 0x%08X", hr);
				goto exit;
			}

			lpNameID->lpguid = (GUID*)&PS_INTERNET_HEADERS;
			lpNameID->ulKind = MNID_STRING;
			lpNameID->Kind.lpwstrName = const_cast<wchar_t *>(L"Content-Type");

			hr = lpMessage->GetIDsFromNames(1, &lpNameID, MAPI_CREATE, &lpPropTags);
			if (hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to read encrypted mail properties. Error: 0x%08X", hr);
				goto exit;
			}

			// Free Memory
			MAPIFreeBuffer(lpNameID);
			lpNameID = NULL;

			if (HrGetOneProp(lpMessage, CHANGE_PROP_TYPE(lpPropTags->aulPropTag[0], PT_STRING8), &lpPropContentType) == hrSuccess) {
				lpszContentType = lpPropContentType->Value.lpszA;
			} else {
				// default, or exit?
				lpszContentType = "application/x-pkcs7-mime;smime-type=enveloped-data;name=smime.p7m";
			}

			vmMessage->getHeader()->ContentType()->parse(lpszContentType);
			MAPIFreeBuffer(lpPropContentType);

			// copy via string so we can set the size of the string since it's binary
			vmime::string inString(lpszRawSMTP, (size_t)sStreamStat.cbSize.QuadPart);
			vmMessage->getBody()->setContents(vmime::create<vmime::stringContentHandler>(inString));

			// vmime now encodes the body too, so I don't have to
			vmMessage->getHeader()->appendField(vmime::headerFieldFactory::getInstance()->create(vmime::fields::CONTENT_TRANSFER_ENCODING, "base64"));
		}

	} else {
normal:
		// Create default
		hr = BuildNoteMessage(lpMessage, false, &vmMessage);
		if (hr != hrSuccess)
			goto exit;
	}

	*lpvmMessage = vmMessage;

exit:
	delete [] lpszRawSMTP;
	if (lpStream)
		lpStream->Release();

	if (lpAttach)
		lpAttach->Release();

	if (lpRows)
		FreeProws(lpRows);

	if (lpAttachmentTable)
		lpAttachmentTable->Release();
	MAPIFreeBuffer(lpInternetCPID);
	MAPIFreeBuffer(lpMsgClass);
	return hr;
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
	HRESULT			hr				= hrSuccess;
	LPSRowSet		prows			= NULL;
	LPSPropValue	lpSubject		= NULL;
	eBestBody bestBody = plaintext;

	try {
		if (HrGetOneProp(lpMessage, PR_SUBJECT_W, &lpSubject) == hrSuccess) {
			removeEnters(lpSubject->Value.lpszW);
			strOut = lpSubject->Value.lpszW;
		}
		else
			strOut.clear();

		// we can't ignore any errors.. the sender should know if an email is not sent correctly
		lpVMMessageBuilder->setSubject(getVmimeTextFromWide(strOut));

		// handle recipients
		hr = processRecipients(lpMessage, lpVMMessageBuilder);
		if (hr != hrSuccess)
			goto exit;			// Logging has been done in processRecipients()

		if (!bSkipContent) {
			// handle text
			hr = handleTextparts(lpMessage, lpVMMessageBuilder, &bestBody);
			if (hr != hrSuccess)
				goto exit;

			// handle attachments
			hr = handleAttachments(lpMessage, lpVMMessageBuilder);
			if (hr != hrSuccess)
				goto exit;

			// check for TNEF information, and add winmail.dat if needed
			hr = handleTNEF(lpMessage, lpVMMessageBuilder, bestBody);
			if (hr != hrSuccess)
				goto exit;
		}

		/*
		  To convert a VMMessageBuilder to a VMMessage object, an 'expeditor' needs to be set in vmime
		  later on, this from will be overwritten .. so maybe replace this with a dummy value?
		 */

		std::wstring strName, strType, strEmAdd;
		hr = HrGetAddress(m_lpAdrBook, lpMessage, PR_SENDER_ENTRYID, PR_SENDER_NAME_W, PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS_W, strName, strType, strEmAdd);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get sender information. Error: 0x%08X", hr);
			goto exit;
		}

		if (!strName.empty()) {
			lpVMMessageBuilder->setExpeditor(vmime::mailbox(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmAdd)));
		} else {
			lpVMMessageBuilder->setExpeditor(vmime::mailbox(m_converter.convert_to<string>(strEmAdd)));
		}
		// sender and reply-to is set elsewhere because it can only be done on a message object...
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception: %s", e.what());
		hr = MAPI_E_CALL_FAILED; // set real error
		goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on fill message: %s", e.what());
		hr = MAPI_E_CALL_FAILED; // set real error
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception  on fill message");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

exit:
	MAPIFreeBuffer(lpSubject);
	if (prows)
		FreeProws(prows);

	return hr;
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
HRESULT MAPIToVMIME::getMailBox(LPSRow lpRow, vmime::ref<vmime::address> *lpvmMailbox)
{
	HRESULT hr;
	vmime::ref<vmime::address> vmMailboxNew;
	std::wstring strName, strEmail, strType;
	LPSPropValue pPropObjectType = NULL;

	hr = HrGetAddress(m_lpAdrBook, lpRow->lpProps, lpRow->cValues, PR_ENTRYID, PR_DISPLAY_NAME_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, strName, strType, strEmail);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to create mailbox. Error: %08X", hr);
		return hr;
	}

	pPropObjectType = PpropFindProp(lpRow->lpProps, lpRow->cValues, PR_OBJECT_TYPE);

	if (strName.empty() && !strEmail.empty()) {
		// email address only
		vmMailboxNew = vmime::create<vmime::mailbox>(m_converter.convert_to<string>(strEmail));
	} else if (strEmail.find('@') != string::npos) {
		// email with fullname
		vmMailboxNew = vmime::create<vmime::mailbox>(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail));
	} else if (pPropObjectType && pPropObjectType->Value.ul == MAPI_DISTLIST) {
		// if mailing to a group without email address
		vmMailboxNew = vmime::create<vmime::mailboxGroup>(getVmimeTextFromWide(strName));
	} else if (sopt.no_recipients_workaround == true) {
		// gateway must always return an mailbox object
		if (strEmail.empty())
			strEmail = L"@";	// force having an address to avoid vmime problems
		vmMailboxNew = vmime::create<vmime::mailbox>(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail));
	} else {
		if (strEmail.empty()) {
			// not an email address and not a group: invalid
			m_strError = L"Invalid email address in recipient list found: \"" + strName + L"\". Email Address is empty.";
			lpLogger->Log(EC_LOGLEVEL_ERROR, "%ls", m_strError.c_str());
			return MAPI_E_INVALID_PARAMETER;
		}

		// we only want to have this recipient fail, and not the whole message, if the user has a username
		m_strError = L"Invalid email address in recipient list found: \"" + strName + L"\" <" + strEmail + L">.";
		lpLogger->Log(EC_LOGLEVEL_ERROR, "%ls", m_strError.c_str());
		return MAPI_E_INVALID_PARAMETER;
	}

	*lpvmMailbox = vmMailboxNew;
	return hr;
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
	HRESULT		hr						= hrSuccess;
	IStream*	lpCompressedRTFStream	= NULL; 
	IStream*	lpUncompressedRTFStream	= NULL; 
	IStream*	lpBody					= NULL;
	IStream*	lpHTMLStream			= NULL;
	vmime::charset HTMLcharset;

	// set the encoder of plaintext body part
	vmime::encoding bodyEncoding("quoted-printable");
	// will skip enters in q-p encoding, "fixes" plaintext mails to be more plain text
	vmime::ref<vmime::utility::encoder::encoder> vmBodyEncoder = bodyEncoding.getEncoder();
	vmBodyEncoder->getProperties().setProperty("text", true);

	*bestBody = plaintext;

	// grabbing rtf
	hr = lpMessage->OpenProperty(PR_RTF_COMPRESSED, &IID_IStream, 0, 0, (LPUNKNOWN *)&lpCompressedRTFStream);
	if(hr == hrSuccess) {

		hr = WrapCompressedRTFStream(lpCompressedRTFStream, 0, &lpUncompressedRTFStream);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to create RTF-text stream. Error: 0x%08X", hr);
			goto exit;
		}

		hr = Util::HrStreamToString(lpUncompressedRTFStream, strRtf);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to read RTF-text stream. Error: 0x%08X", hr);
			goto exit;
		}

		if (isrtfhtml(strRtf.c_str(), strRtf.size()) || sopt.use_tnef == -1)
		{
			// Open html
			hr = lpMessage->OpenProperty(PR_HTML, &IID_IStream, 0, 0, (LPUNKNOWN *)&lpHTMLStream);
			if (hr == hrSuccess) {
				hr = Util::HrStreamToString(lpHTMLStream, strHTMLOut);
				if (hr != hrSuccess) {
					lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to read HTML-text stream. Error: 0x%08X", hr);
					goto exit;
				}

				// strHTMLout is now escaped us-ascii HTML, this is what we will be sending
				// Or, if something failed, the HTML is now empty
				lpHTMLStream->Release(); lpHTMLStream = NULL;
				*bestBody = html;
			} else {
				lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to open HTML-text stream. Error: 0x%08X", hr);
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
			lpLogger->Log(EC_LOGLEVEL_INFO, "Unable to open rtf-text stream. Error: 0x%08X", hr);
		hr = hrSuccess;
	}
	
	try {
		hr = lpMessage->OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, (IUnknown**)&lpBody);
		if (hr == hrSuccess) {
			hr = Util::HrStreamToString(lpBody, strBody);
			lpBody->Release(); lpBody = NULL;
		} else {
			if (hr != MAPI_E_NOT_FOUND)
				lpLogger->Log(EC_LOGLEVEL_INFO, "Unable to open plain-text stream. Error: 0x%08X", hr);
			hr = hrSuccess;
		}

		// Convert body to correct charset
		strBodyConverted = m_converter.convert_to<string>(m_strCharset.c_str(), strBody, rawsize(strBody), CHARSET_WCHAR);

		// always use our textpart class
		lpVMMessageBuilder->constructTextPart(vmime::mediaType(vmime::mediaTypes::TEXT, "mapi"));

		// If HTML, create HTML and plaintext parts
		if (!strHTMLOut.empty()) {
			if (m_vmCharset.getName() != m_strHTMLCharset) {
				// convert from HTML charset to vmime output charset
				strHTMLOut = m_converter.convert_to<string>(m_vmCharset.getName().c_str(), strHTMLOut, rawsize(strHTMLOut), m_strHTMLCharset.c_str());
			}
			vmime::ref<vmime::mapiTextPart> textPart = lpVMMessageBuilder->getTextPart().dynamicCast<vmime::mapiTextPart>();
			textPart->setText(vmime::create<vmime::stringContentHandler>(strHTMLOut));
			textPart->setCharset(m_vmCharset);
			if (!strBodyConverted.empty())
				textPart->setPlainText(vmime::create<vmime::stringContentHandler>(strBodyConverted));
		} else { // else just plaintext
			if (!strBodyConverted.empty()) {
				// make sure we give vmime CRLF data, so SMTP servers (qmail) won't complain on the forced plaintext
				char *crlfconv = NULL;
				size_t outsize = 0;
				crlfconv = new char[strBodyConverted.length()*2+1];
				BufferLFtoCRLF(strBodyConverted.length(), strBodyConverted.c_str(), crlfconv, &outsize);
				strBodyConverted.assign(crlfconv, outsize);
				delete [] crlfconv;

				// encode to q-p ourselves
				vmime::utility::inputStreamStringAdapter in(strBodyConverted);
				vmime::string outString;
				vmime::utility::outputStreamStringAdapter out(outString);

				vmBodyEncoder->encode(in, out);

				lpVMMessageBuilder->getTextPart()->setCharset(m_vmCharset);
				lpVMMessageBuilder->getTextPart().dynamicCast<vmime::mapiTextPart>()->setPlainText(vmime::create<vmime::stringContentHandler>(outString, bodyEncoding));
			}
		}
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on text part: %s", e.what());
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception on text part");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

exit:
	if (hr != hrSuccess)
		lpLogger->Log(EC_LOGLEVEL_WARNING, "Could not parse mail body");

	if (lpHTMLStream)
		lpHTMLStream->Release();

	if (lpBody)
		lpBody->Release();

	if (lpUncompressedRTFStream)
		lpUncompressedRTFStream->Release();
		
	if (lpCompressedRTFStream)
		lpCompressedRTFStream->Release(); 

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
HRESULT MAPIToVMIME::handleXHeaders(IMessage *lpMessage, vmime::ref<vmime::header> vmHeader) {
	HRESULT hr;
	ULONG i;
	ULONG cValues;
	LPSPropTagArray lpsAllTags = NULL;
	LPSPropTagArray lpsNamedTags = NULL;
	LPSPropValue lpPropArray = NULL;
	ULONG cNames;
	LPMAPINAMEID *lppNames = NULL;
	vmime::headerFieldFactory* hff = vmime::headerFieldFactory::getInstance();

	// get all props on message
	hr = lpMessage->GetPropList(0, &lpsAllTags);
	if (FAILED(hr))
		goto exit;

	// find number of named props, which contain a string
	cNames = 0;
	for (i = 0; i < lpsAllTags->cValues; ++i)
		if (PROP_ID(lpsAllTags->aulPropTag[i]) >= 0x8000 && PROP_TYPE(lpsAllTags->aulPropTag[i]) == PT_STRING8)
			++cNames;

	// no string named properties found, we're done.
	if (cNames == 0)
		goto exit;

	hr = MAPIAllocateBuffer(CbNewSPropTagArray(cNames), (void **)&lpsNamedTags);
	if (hr != hrSuccess)
		goto exit;
	lpsNamedTags->cValues = cNames;

	// make named prop array
	cNames = 0;
	for (i = 0; i < lpsAllTags->cValues; ++i)
		if (PROP_ID(lpsAllTags->aulPropTag[i]) >= 0x8000 && PROP_TYPE(lpsAllTags->aulPropTag[i]) == PT_STRING8)
			lpsNamedTags->aulPropTag[cNames++] = lpsAllTags->aulPropTag[i];
	
	hr = lpMessage->GetNamesFromIDs(&lpsNamedTags, NULL, 0, &cNames, &lppNames);
	if (FAILED(hr))
		goto exit;

	hr = lpMessage->GetProps(lpsNamedTags, 0, &cValues, &lpPropArray);
	if (FAILED(hr))
		goto exit;

	for (i = 0; i < cNames; ++i) {
		if (lppNames[i] && lppNames[i]->ulKind == MNID_STRING && lppNames[i]->Kind.lpwstrName &&
			PROP_TYPE(lpPropArray[i].ulPropTag) == PT_STRING8)
		{
			char *str;
			int l = wcstombs(NULL, lppNames[i]->Kind.lpwstrName, 0) +1;
			str = new char[l];
			wcstombs(str, lppNames[i]->Kind.lpwstrName, l);

			if ((str[0] == 'X' || str[0] == 'x') && str[1] == '-') {
				if (str[0] == 'x')
					capitalize(str);

				// keep the original x-mailer header under a different name
				// we still want to know that this mail was generated by kopano in the x-mailer header from handleExtraHeaders
				if (strcasecmp(str, "X-Mailer") == 0) {
					delete [] str;
					str = new char[18];
					strcpy(str, "X-Original-Mailer");
				}

				try {
					vmHeader->findField(str);
				}
				catch (vmime::exceptions::no_such_field) {
					vmHeader->appendField(hff->create(str, lpPropArray[i].Value.lpszA));
				}
			}
			delete [] str;
		}
	}

exit:
	MAPIFreeBuffer(lpPropArray);
	MAPIFreeBuffer(lpsNamedTags);
	MAPIFreeBuffer(lpsAllTags);
	MAPIFreeBuffer(lppNames);
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
 * \li Expity-Time
 *  
 * @param[in]	lpMessage	Message to convert extra headers for
 * @param[in]	charset		Charset to use in Thread-Topic header
 * @param[in]	vmHeader	Add headers to this vmime header object
 * @return always hrSuccess
 */
HRESULT MAPIToVMIME::handleExtraHeaders(IMessage *lpMessage, vmime::ref<vmime::header> vmHeader) {
	HRESULT hr = hrSuccess;
	SPropValuePtr ptrMessageId;
	LPSPropValue lpImportance = NULL;
	LPSPropValue lpPriority = NULL;
	LPSPropValue lpConversationIndex = NULL;
	LPSPropValue lpConversationTopic = NULL;
	LPSPropValue lpNormSubject = NULL;
	LPSPropValue lpSensitivity = NULL;
	vmime::headerFieldFactory* hff = vmime::headerFieldFactory::getInstance();
	LPSPropValue lpExpiryTime = NULL;

	// Conversation headers. New Message-Id header is set just before sending.
	if (HrGetOneProp(lpMessage, PR_IN_REPLY_TO_ID_A, &ptrMessageId) == hrSuccess && ptrMessageId->Value.lpszA[0]) {
		vmime::ref<vmime::messageId> mid = vmime::create<vmime::messageId>(ptrMessageId->Value.lpszA);
		vmHeader->InReplyTo()->getValue().dynamicCast<vmime::messageIdSequence>()->appendMessageId(mid);
	}

	// Outlook never adds this property
	if (HrGetOneProp(lpMessage, PR_INTERNET_REFERENCES_A, &ptrMessageId) == hrSuccess && ptrMessageId->Value.lpszA[0]) {
		std::vector<std::string> ids = tokenize(ptrMessageId->Value.lpszA, ' ', true);

		const size_t n = ids.size();
		for (size_t i = 0; i < n; ++i) {
			vmime::ref<vmime::messageId> mid = vmime::create<vmime::messageId>(ids.at(i));

			vmHeader->References()->getValue().dynamicCast<vmime::messageIdSequence>()->appendMessageId(mid);
		}
	}

	// only for message-in-message items, add Message-ID header from MAPI
	if (sopt.msg_in_msg && HrGetOneProp(lpMessage, PR_INTERNET_MESSAGE_ID_A, &ptrMessageId) == hrSuccess && ptrMessageId->Value.lpszA[0])
		vmHeader->MessageId()->setValue(ptrMessageId->Value.lpszA);

	// priority settings
	static const char *const priomap[3] = { "5 (Lowest)", "3 (Normal)", "1 (Highest)" }; // 2 and 4 cannot be set from outlook
	if (HrGetOneProp(lpMessage, PR_IMPORTANCE, &lpImportance) == hrSuccess)
		vmHeader->appendField(hff->create("X-Priority", priomap[min(2, (int)(lpImportance->Value.ul)&3)])); // IMPORTANCE_* = 0..2
	else if (HrGetOneProp(lpMessage, PR_PRIORITY, &lpPriority) == hrSuccess)
		vmHeader->appendField(hff->create("X-Priority", priomap[min(2, (int)(lpPriority->Value.ul+1)&3)])); // PRIO_* = -1..1

	// When adding a X-Priority, spamassassin may add a severe punishment because no User-Agent header
	// or X-Mailer header is present. So we set the X-Mailer header :)
	vmHeader->appendField(hff->create("X-Mailer", "Kopano " PROJECT_VERSION_DOT_STR "-" PROJECT_SVN_REV_STR));

	// PR_CONVERSATION_INDEX
	if (HrGetOneProp(lpMessage, PR_CONVERSATION_INDEX, &lpConversationIndex) == hrSuccess) {
		vmime::string inString;
		inString.assign((const char*)lpConversationIndex->Value.bin.lpb, lpConversationIndex->Value.bin.cb);

		vmime::ref<vmime::utility::encoder::encoder> enc = vmime::utility::encoder::encoderFactory::getInstance()->create("base64");
		vmime::utility::inputStreamStringAdapter in(inString);
		vmime::string outString;
		vmime::utility::outputStreamStringAdapter out(outString);

		enc->encode(in, out);

		vmHeader->appendField(hff->create("Thread-Index", outString));
	}

	// PR_CONVERSATION_TOPIC is always the original started topic
	if (HrGetOneProp(lpMessage, PR_CONVERSATION_TOPIC_W, &lpConversationTopic) == hrSuccess) {
		if (HrGetOneProp(lpMessage, PR_NORMALIZED_SUBJECT_W, &lpNormSubject) != hrSuccess ||
			wcscmp(lpNormSubject->Value.lpszW, lpConversationTopic->Value.lpszW) != 0)
		{
			removeEnters(lpConversationTopic->Value.lpszW);
			vmHeader->appendField(hff->create("Thread-Topic", getVmimeTextFromWide(lpConversationTopic->Value.lpszW).generate()));
		}
	}

	if (HrGetOneProp(lpMessage, PR_SENSITIVITY, &lpSensitivity) == hrSuccess) {
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

	if (HrGetOneProp(lpMessage, PR_EXPIRY_TIME, &lpExpiryTime) == hrSuccess)
		vmHeader->appendField(hff->create("Expiry-Time", FiletimeTovmimeDatetime(lpExpiryTime->Value.ft).generate()));

//exit:
	MAPIFreeBuffer(lpExpiryTime);
	MAPIFreeBuffer(lpImportance);
	MAPIFreeBuffer(lpPriority);
	MAPIFreeBuffer(lpSensitivity);
	MAPIFreeBuffer(lpConversationIndex);
	MAPIFreeBuffer(lpConversationTopic);
	MAPIFreeBuffer(lpNormSubject);
	return hr;
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
	HRESULT hr = hrSuccess;
	LPCONTAB_ENTRYID lpContabEntryID = NULL;
	GUID* guid = NULL;
	ULONG ulObjType;
	LPSPropTagArray lpNameTags = NULL;
	LPSPropValue lpNamedProps = NULL;
	LPMAILUSER lpContact = NULL;
	LPMAPINAMEID *lppNames = NULL;
	ULONG i;
	ULONG ulNames = 5;
	MAPINAMEID mnNamedProps[5] = {
		// offset 0, every offset < 3 is + 0x10
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8080}}, // display name
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8082}}, // address type
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8083}}, // email address
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8084}}, // original display name (unused)
		{(LPGUID)&PSETID_Address, MNID_ID, {0x8085}}, // real entryid
	};

	if (PROP_TYPE(lpProps[0].ulPropTag) != PT_BINARY) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	lpContabEntryID = (LPCONTAB_ENTRYID)lpProps[0].Value.bin.lpb;
	if (lpContabEntryID == NULL) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	guid = (GUID*)&lpContabEntryID->muid;
	if (sizeof(CONTAB_ENTRYID) > lpProps[0].Value.bin.cb || *guid != PSETID_CONTACT_FOLDER_RECIPIENT || lpContabEntryID->email_offset > 2) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	hr = m_lpSession->OpenEntry(lpContabEntryID->cbeid, (LPENTRYID)lpContabEntryID->abeid, NULL, 0, &ulObjType, (LPUNKNOWN*)&lpContact);
	if (hr != hrSuccess)
		goto exit;

	// add offset to get correct named properties
	for (i = 0; i < ulNames; ++i)
		mnNamedProps[i].Kind.lID += (lpContabEntryID->email_offset * 0x10);

	hr = MAPIAllocateBuffer(sizeof(LPMAPINAMEID) * (ulNames), (void**)&lppNames);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "No memory for named ids from contact");
		goto exit;
	}

	for (i = 0; i < ulNames; ++i)
		lppNames[i] = &mnNamedProps[i];

	hr = lpContact->GetIDsFromNames(ulNames, lppNames, MAPI_CREATE, &lpNameTags);
	if (FAILED(hr))
		goto exit;

	lpNameTags->aulPropTag[0] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[0], PT_UNICODE);
	lpNameTags->aulPropTag[1] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[1], PT_UNICODE);
	lpNameTags->aulPropTag[2] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[2], PT_UNICODE);
	lpNameTags->aulPropTag[3] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[3], PT_UNICODE); // unused
	lpNameTags->aulPropTag[4] = CHANGE_PROP_TYPE(lpNameTags->aulPropTag[4], PT_BINARY);

	hr = lpContact->GetProps(lpNameTags, 0, &ulNames, &lpNamedProps);
	if (FAILED(hr))
		goto exit;

	hr = HrGetAddress(m_lpAdrBook, lpNamedProps, ulNames,
					  lpNameTags->aulPropTag[4], lpNameTags->aulPropTag[0], lpNameTags->aulPropTag[1], lpNameTags->aulPropTag[2],
					  strName, strType, strEmail);

exit:
	MAPIFreeBuffer(lpNamedProps);
	MAPIFreeBuffer(lpNameTags);
	if (lpContact)
		lpContact->Release();
	MAPIFreeBuffer(lppNames);
	return hr;
}

/**
 * Set From and possibly Sender header.
 *
 * @param[in]	lpMessage	Message to get From and Sender of.
 * @param[in]	charset		charset to use for Fullname's of headers.
 * @param[in]	vmHeader	vmime header object to modify.
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::handleSenderInfo(IMessage *lpMessage, vmime::ref<vmime::header> vmHeader) {
	HRESULT hr = hrSuccess;
	ULONG cValues;
	LPSPropValue lpProps = NULL;
	LPSPropTagArray lpPropTags = NULL;
	LPSPropValue lpReadReceipt = NULL;

	// sender information
	std::wstring strEmail, strName, strType;
	std::wstring strResEmail, strResName, strResType;

	hr = MAPIAllocateBuffer(CbNewSPropTagArray(4), (void**)&lpPropTags);
	if (hr != hrSuccess)
		goto exit;

	lpPropTags->cValues = 4;
	lpPropTags->aulPropTag[0] = PR_SENDER_ENTRYID;
	lpPropTags->aulPropTag[1] = PR_SENDER_NAME_W;
	lpPropTags->aulPropTag[2] = PR_SENDER_ADDRTYPE_W;
	lpPropTags->aulPropTag[3] = PR_SENDER_EMAIL_ADDRESS_W;

	hr = lpMessage->GetProps(lpPropTags, 0, &cValues, &lpProps);
	if (FAILED(hr))
		goto exit;

	hr = handleContactEntryID(cValues, lpProps, strName, strType, strEmail);
	if (hr != hrSuccess) {
		// Store owner, actual sender
		hr = HrGetAddress(m_lpAdrBook, lpProps, cValues, PR_SENDER_ENTRYID, PR_SENDER_NAME_W, PR_SENDER_ADDRTYPE_W, PR_SENDER_EMAIL_ADDRESS_W, strName, strType, strEmail);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get sender information. Error: 0x%08X", hr);
			goto exit;
		}
	}

	MAPIFreeBuffer(lpProps);
	lpProps = NULL;

	// -- sender
	lpPropTags->cValues = 4;
	lpPropTags->aulPropTag[0] = PR_SENT_REPRESENTING_ENTRYID;
	lpPropTags->aulPropTag[1] = PR_SENT_REPRESENTING_NAME_W;
	lpPropTags->aulPropTag[2] = PR_SENT_REPRESENTING_ADDRTYPE_W;
	lpPropTags->aulPropTag[3] = PR_SENT_REPRESENTING_EMAIL_ADDRESS_W;

	hr = lpMessage->GetProps(lpPropTags, 0, &cValues, &lpProps);
	if (FAILED(hr))
		goto exit;

	hr = handleContactEntryID(cValues, lpProps, strResName, strResType, strResEmail);
	if (hr != hrSuccess) {
		hr = HrGetAddress(m_lpAdrBook, lpProps, cValues, PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, strResName, strResType, strResEmail);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to get representing information. Error: 0x%08X", hr);
			// ignore error, since we still have enough information to send, maybe not just under the correct name
			hr = hrSuccess;
		}
		if (sopt.no_recipients_workaround == false && strResEmail.empty() && PROP_TYPE(lpProps[0].ulPropTag) != PT_ERROR) {
			m_strError = L"Representing email address is empty";
			lpLogger->Log(EC_LOGLEVEL_ERROR, "%ls", m_strError.c_str());
			hr = MAPI_E_NOT_FOUND;
			goto exit;
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
	} else {
		// Set store owner as from, sender does not need to be set
		if (strName.empty() || strName == strEmail) 
			vmHeader->From()->setValue(vmime::mailbox(m_converter.convert_to<string>(strEmail)));
		else
			vmHeader->From()->setValue(vmime::mailbox(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail)));
	}

	// read receipt request
	if (HrGetOneProp(lpMessage, PR_READ_RECEIPT_REQUESTED, &lpReadReceipt) == hrSuccess && lpReadReceipt->Value.b == TRUE) {
		vmime::mailboxList mbl;
		if (!strResEmail.empty() && strResEmail != strEmail) {
			// use user added from address
			if (strResName.empty() || strName == strResEmail)
				mbl.appendMailbox(vmime::create<vmime::mailbox>(m_converter.convert_to<string>(strResEmail)));
			else
				mbl.appendMailbox(vmime::create<vmime::mailbox>(getVmimeTextFromWide(strResName), m_converter.convert_to<string>(strResEmail)));
		} else {
			if (strName.empty() || strName == strEmail)
				mbl.appendMailbox(vmime::create<vmime::mailbox>(m_converter.convert_to<string>(strEmail)));
			else
				mbl.appendMailbox(vmime::create<vmime::mailbox>(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail)));
		}
		vmHeader->DispositionNotificationTo()->setValue(mbl);
	}

exit:
	MAPIFreeBuffer(lpPropTags);
	MAPIFreeBuffer(lpProps);
	MAPIFreeBuffer(lpReadReceipt);
	return hr;
}

/**
 * Set Reply-To header.
 *
 * @note In RFC-822 and MAPI, you can set multiple Reply-To
 * values. However, in vmime this is currently not possible, so we
 * only convert the first.
 *
 * @param[in]	lpMessage	Message to get Reply-To value of.
 * @param[in]	charset		charset to use for Fullname's of headers.
 * @param[in]	vmHeader	vmime header object to modify.
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::handleReplyTo(IMessage *lpMessage, vmime::ref<vmime::header> vmHeader) {
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpReplyTo = NULL;
	FLATENTRYLIST	*lpEntryList = NULL;
	FLATENTRY		*lpEntry = NULL;
	LPCONTAB_ENTRYID lpContabEntryID = NULL;
	GUID*			guid = NULL;
	ULONG			ulObjType;
	LPMAILUSER		lpContact = NULL;
	wstring			strName, strType, strEmail;

	// "Email1DisplayName","Email1AddressType","Email1Address","Email1EntryID"
	ULONG lpulNamesIDs[] = {0x8080, 0x8082, 0x8083, 0x8085,
							0x8090, 0x8092, 0x8093, 0x8095,
							0x80A0, 0x80A2, 0x80A3, 0x80A5};
	ULONG cNames, i, offset;
	LPMAPINAMEID lpNames = NULL;
	LPMAPINAMEID *lppNames = NULL;
	LPSPropTagArray lpNameTagArray = NULL;
	LPSPropValue lpAddressProps = NULL;

	if (HrGetOneProp(lpMessage, PR_REPLY_RECIPIENT_ENTRIES, &lpReplyTo) != hrSuccess)
		goto exit;

	if (lpReplyTo->Value.bin.cb == 0)
		goto exit;

	lpEntryList = (FLATENTRYLIST *)lpReplyTo->Value.bin.lpb;

	if (lpEntryList->cEntries == 0)
		goto exit;

	lpEntry = (FLATENTRY *)&lpEntryList->abEntries;

	hr = HrGetAddress(m_lpAdrBook, (LPENTRYID)lpEntry->abEntry, lpEntry->cb, strName, strType, strEmail);
	if (hr != hrSuccess) {
		if (!m_lpSession) {
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}

		// user selected a contact (or distrolist ?) in the reply-to
		lpContabEntryID = (LPCONTAB_ENTRYID)lpEntry->abEntry;
		guid = (GUID*)&lpContabEntryID->muid;

		if (sizeof(CONTAB_ENTRYID) > lpEntry->cb || *guid != PSETID_CONTACT_FOLDER_RECIPIENT || lpContabEntryID->email_offset > 2)
			goto exit;

		hr = m_lpSession->OpenEntry(lpContabEntryID->cbeid, (LPENTRYID)lpContabEntryID->abeid, NULL, 0, &ulObjType, (LPUNKNOWN*)&lpContact);
		if (hr != hrSuccess)
			goto exit;

		cNames = ARRAY_SIZE(lpulNamesIDs);

		hr = MAPIAllocateBuffer(sizeof(MAPINAMEID) * cNames, (void**)&lpNames);
		if (hr != hrSuccess)
			goto exit;

		hr = MAPIAllocateBuffer(sizeof(LPMAPINAMEID) * cNames, (void**)&lppNames);
		if (hr != hrSuccess)
			goto exit;

		for (i = 0; i < cNames; ++i) {
			lpNames[i].lpguid = (GUID*)&PSETID_Address;
			lpNames[i].ulKind = MNID_ID;
			lpNames[i].Kind.lID = lpulNamesIDs[i];
			lppNames[i] = &lpNames[i];
		}

		hr = lpContact->GetIDsFromNames(cNames, lppNames, 0, &lpNameTagArray);
		if (FAILED(hr))
			goto exit;

		// PT_UNSPECIFIED in tagarray, but we want PT_UNICODE
		hr = lpContact->GetProps(lpNameTagArray, MAPI_UNICODE, &cNames, &lpAddressProps);
		if (FAILED(hr))
			goto exit;

		offset = lpContabEntryID->email_offset * 4; // 4 props per email address

		if (PROP_TYPE(lpAddressProps[offset+0].ulPropTag) == PT_ERROR || PROP_TYPE(lpAddressProps[offset+1].ulPropTag) == PT_ERROR ||
			PROP_TYPE(lpAddressProps[offset+2].ulPropTag) == PT_ERROR || PROP_TYPE(lpAddressProps[offset+3].ulPropTag) == PT_ERROR)
			goto exit;

		if (wcscmp(lpAddressProps[offset+1].Value.lpszW, L"SMTP") == 0) {
			strName = lpAddressProps[offset+0].Value.lpszW;
			strEmail = lpAddressProps[offset+2].Value.lpszW;
		} else if (wcscmp(lpAddressProps[offset+1].Value.lpszW, L"ZARAFA") == 0) {
			hr = HrGetAddress(m_lpAdrBook, (LPENTRYID)lpAddressProps[offset+2].Value.bin.lpb, lpAddressProps[offset+2].Value.bin.cb, strName, strType, strEmail);
			if (hr != hrSuccess)
				goto exit;
		}
	}

	// vmime can only set 1 email address in the ReplyTo field.
	if (!strName.empty() && strName != strEmail)
		vmHeader->ReplyTo()->setValue(vmime::create<vmime::mailbox>(getVmimeTextFromWide(strName), m_converter.convert_to<string>(strEmail)));
	else
		vmHeader->ReplyTo()->setValue(vmime::create<vmime::mailbox>(m_converter.convert_to<string>(strEmail)));

	hr = hrSuccess;

exit:
	MAPIFreeBuffer(lpNames);
	MAPIFreeBuffer(lppNames);
	MAPIFreeBuffer(lpNameTagArray);
	MAPIFreeBuffer(lpAddressProps);
	if (lpContact)
		lpContact->Release();
	MAPIFreeBuffer(lpReplyTo);
	return hr;
}

/**
 * check if named property exists which is used to hold voting options
 */

bool MAPIToVMIME::is_voting_request(IMessage *lpMessage)
{
	HRESULT hr = hrSuccess;
	LPSPropTagArray lpPropTags = NULL;
	LPSPropValue lpPropContentType = NULL;
	MAPINAMEID named_prop = {(LPGUID)&PSETID_Common, MNID_ID, {0x8520}};
	MAPINAMEID *named_proplist = &named_prop;

	hr = lpMessage->GetIDsFromNames(1, &named_proplist, MAPI_CREATE, &lpPropTags);
	if (hr != hrSuccess)
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to read voting property. Error: %s (0x%08X)",
			GetMAPIErrorMessage(hr), hr);
	else
		hr = HrGetOneProp(lpMessage, CHANGE_PROP_TYPE(lpPropTags->aulPropTag[0], PT_BINARY), &lpPropContentType);

	MAPIFreeBuffer(lpPropTags);
	MAPIFreeBuffer(lpPropContentType);
	return hr == hrSuccess;
}

/**
 * Adds a TNEF (winmail.dat) attachment to the message, if special
 * outlook data needs to be sent. May add iCal for calendar items
 * instead of TNEF.
 *
 * @param[in]	lpMessage	Message to get Reply-To value of.
 * @param[in]	charset		charset to use for Fullname's of headers.
 * @param[in]	vmHeader	vmime header object to modify.
 * @return Mapi error code
 */
HRESULT MAPIToVMIME::handleTNEF(IMessage* lpMessage, vmime::messageBuilder* lpVMMessageBuilder, eBestBody bestBody) {
	HRESULT			hr				= hrSuccess;
	LPSPropValue	lpSendAsICal	= NULL;
	LPSPropValue	lpOutlookVersion = NULL;
	LPSPropValue	lpMessageClass	= NULL;
	LPSPropValue	lpDelegateRule	= NULL;
	IStream 		*lpStream = NULL;
	MapiToICal		*mapiical = NULL;
	
	LPMAPINAMEID	lpNames			= NULL;	
	LPSPropTagArray lpNameTagArray	= NULL;
	int				iUseTnef = sopt.use_tnef;
	std::string		strTnefReason;

	std::list<ULONG> lstOLEAttach; // list of OLE attachments that must be sent via TNEF
	std::list<ULONG>::const_iterator iterAttach;
	IMAPITable *	lpAttachTable = NULL;
	LPSRowSet		lpAttachRows = NULL;
	SizedSPropTagArray(2, sptaAttachProps) = {2, {PR_ATTACH_METHOD, PR_ATTACH_NUM }};
	SizedSPropTagArray(5, sptaOLEAttachProps) = {5, {PR_ATTACH_FILENAME, PR_ATTACH_LONG_FILENAME, PR_ATTACH_DATA_OBJ, PR_ATTACH_CONTENT_ID, PR_ATTACHMENT_HIDDEN}};
	SizedSSortOrderSet(1, sosRTFSeq) = {1, 0, 0, { {PR_RENDERING_POSITION, TABLE_SORT_ASCEND} } };

	try {
	    // Find all ATTACH_OLE attachments and put them in lstOLEAttach
	    hr = lpMessage->GetAttachmentTable(0, &lpAttachTable);
	    if(hr != hrSuccess)
	        goto exit;
	    
        hr = HrQueryAllRows(lpAttachTable, (LPSPropTagArray)&sptaAttachProps, NULL, (LPSSortOrderSet)&sosRTFSeq, 0, &lpAttachRows);
        if(hr != hrSuccess)
            goto exit;
            
        for ( unsigned int i = 0; i < lpAttachRows->cRows; ++i) {
            if(lpAttachRows->aRow[i].lpProps[0].ulPropTag == PR_ATTACH_METHOD && 
                lpAttachRows->aRow[i].lpProps[1].ulPropTag == PR_ATTACH_NUM &&
                lpAttachRows->aRow[i].lpProps[0].Value.ul == ATTACH_OLE)
            {
                lstOLEAttach.push_back(lpAttachRows->aRow[i].lpProps[1].Value.ul);
            }
        }
	
        // Start processing TNEF properties

		if(HrGetOneProp(lpMessage, PR_EC_SEND_AS_ICAL, &lpSendAsICal) != hrSuccess) {
			lpSendAsICal = NULL;
		}
		
		if(HrGetOneProp(lpMessage, PR_EC_OUTLOOK_VERSION, &lpOutlookVersion) != hrSuccess) {
			lpOutlookVersion = NULL;
		} 
		
		if(HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &lpMessageClass) != hrSuccess) {
			lpMessageClass = NULL;
		}

		if(HrGetOneProp(lpMessage, PR_DELEGATED_BY_RULE, &lpDelegateRule) != hrSuccess) {
			lpDelegateRule = NULL;
		}

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
		    
			vmime::ref<vmime::attachment> vmTNEFAtt;
			vmime::ref<vmime::utility::inputStream> inputDataStream = NULL;

			/* 
			 * Send TNEF information for this message if we really need to, or otherwise iCal
			 */
			
			if (lstOLEAttach.size() == 0 && iUseTnef <= 0 && lpMessageClass && (strncasecmp("IPM.Note", lpMessageClass->Value.lpszA, 8) != 0)) {
				// iCAL
				string ical, method;
				vmime::ref<mapiAttachment> vmAttach = NULL;

				lpLogger->Log(EC_LOGLEVEL_INFO, "Adding ICS attachment for extra information");

				CreateMapiToICal(m_lpAdrBook, "utf-8", &mapiical);

				hr = mapiical->AddMessage(lpMessage, std::string(), 0);
				if (hr != hrSuccess) {
					lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to create ical object, sending as TNEF");
					goto tnef_anyway;
				}

				hr = mapiical->Finalize(0, &method, &ical);
				if (hr != hrSuccess) {
					lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to create ical object, sending as TNEF");
					goto tnef_anyway;
				}

				vmime::ref<vmime::mapiTextPart> lpvmMapiText = lpVMMessageBuilder->getTextPart().dynamicCast<vmime::mapiTextPart>();
				lpvmMapiText->setOtherText(vmime::create<vmime::stringContentHandler>(ical));
				lpvmMapiText->setOtherContentType(vmime::mediaType(vmime::mediaTypes::TEXT, "calendar"));
				lpvmMapiText->setOtherContentEncoding(vmime::encoding(vmime::encodingTypes::EIGHT_BIT));
				lpvmMapiText->setOtherMethod(method);
				lpvmMapiText->setOtherCharset(vmime::charset("utf-8"));

			} else {
				if (lstOLEAttach.size())
					lpLogger->Log(EC_LOGLEVEL_INFO, "TNEF because of OLE attachments");
				else if (iUseTnef == 0)
					lpLogger->Log(EC_LOGLEVEL_INFO, "TNEF because of RTF body");
				else
					lpLogger->Log(EC_LOGLEVEL_INFO, strTnefReason);

tnef_anyway:
				hr = CreateStreamOnHGlobal(NULL, TRUE, &lpStream);
				if (hr != hrSuccess) {
					lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to create stream for TNEF attachment. Error 0x%08X", hr);
					goto exit;
				}
			 
				ECTNEF tnef(TNEF_ENCODE, lpMessage, lpStream);
			
				// Encode the properties now, add all message properties except for the exclude list
				hr = tnef.AddProps(TNEF_PROP_EXCLUDE, (LPSPropTagArray)&sptaExclude);
				if(hr != hrSuccess) {
					lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to exclude properties from TNEF object");
					goto exit;
				}
			
				// plaintext is never added to TNEF, only HTML or "real" RTF
				if (bestBody != plaintext) {
					if (bestBody == html) sptaBestBodyInclude.aulPropTag[0] = PR_HTML;
					else if (bestBody == realRTF) sptaBestBodyInclude.aulPropTag[0] = PR_RTF_COMPRESSED;

					hr = tnef.AddProps(TNEF_PROP_INCLUDE, (LPSPropTagArray)&sptaBestBodyInclude);
					if(hr != hrSuccess) {
						lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to include body property 0x%08x to TNEF object", sptaBestBodyInclude.aulPropTag[0]);
						goto exit;
					}
				}
				
				// Add all OLE attachments
				for (iterAttach = lstOLEAttach.begin();
				     iterAttach != lstOLEAttach.end();
				     ++iterAttach)
					tnef.FinishComponent(0x00002000, *iterAttach, (LPSPropTagArray)&sptaOLEAttachProps);
		
				// Write the stream
				hr = tnef.Finish();

				inputDataStream = vmime::create<inputStreamMAPIAdapter>(lpStream);
			
				// Now, add the stream as an attachment to the message, filename winmail.dat 
				// and MIME type 'application/ms-tnef', no content-id
				vmTNEFAtt = vmime::create<mapiAttachment>(vmime::create<vmime::streamContentHandler>(inputDataStream, 0),
														  vmime::encoding("base64"), vmime::mediaType("application/ms-tnef"), string(),
														  vmime::word("winmail.dat"));
											  
				// add to message (copies pointer, not data)
				lpVMMessageBuilder->appendAttachment(vmTNEFAtt); 
			}
		}
	}
	catch (vmime::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "VMIME exception: %s", e.what());
	    hr = MAPI_E_CALL_FAILED; // set real error
	    goto exit;
	}
	catch (std::exception& e) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "STD exception on fill message: %s", e.what());
	    hr = MAPI_E_CALL_FAILED; // set real error
	    goto exit;
	}
	catch (...) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown generic exception on fill message");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

exit:
	if (lpAttachRows != NULL)
		FreeProws(lpAttachRows);
	if (lpAttachTable != NULL)
		lpAttachTable->Release();
	delete mapiical;
	if (lpStream)
		lpStream->Release();
	MAPIFreeBuffer(lpNames);
	MAPIFreeBuffer(lpDelegateRule);
	MAPIFreeBuffer(lpNameTagArray);
	MAPIFreeBuffer(lpMessageClass);
	MAPIFreeBuffer(lpOutlookVersion);
	MAPIFreeBuffer(lpSendAsICal);
	return hr;
}

/**
 * converts x-mail-sender -> X-Mail-Sender
 *
 * @param[in,out]	s	String to capitalize
 */
void MAPIToVMIME::capitalize(char *s) {
	char *p;

	s[0] = toupper(s[0]);		// x to X
	p = s;
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
