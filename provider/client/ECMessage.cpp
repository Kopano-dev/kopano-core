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
#include <kopano/lockhelper.hpp>
#include <kopano/ECInterfaceDefs.h>
#include <mapidefs.h>
#include <mapiutil.h>
#include <mapitags.h>
#include <kopano/mapiext.h>

#include "ECMessage.h"
#include "ECAttach.h"
#include <kopano/ECMemTable.h>

#include <kopano/codepage.h>
#include "rtfutil.h"
#include <kopano/Util.h>
#include "Mem.h"
#include <kopano/mapi_ptr.h>

#include <kopano/ECGuid.h>
#include <edkguid.h>
#include <kopano/ECDebug.h>
#include "WSUtil.h"

#include "ClientUtil.h"
#include "ECMemStream.h"

#include <charset/utf32string.h>
#include <kopano/charset/convert.h>
#include <librosie.h>

using namespace std;

#define MAX_TABLE_PROPSIZE 8192

SizedSPropTagArray(15, sPropRecipColumns) = {15, { PR_7BIT_DISPLAY_NAME_W, PR_EMAIL_ADDRESS_W, PR_INSTANCE_KEY, PR_RECORD_KEY, PR_SEARCH_KEY, PR_SEND_RICH_INFO, PR_DISPLAY_NAME_W, PR_RECIPIENT_TYPE, PR_ROWID, PR_DISPLAY_TYPE, PR_ENTRYID, PR_SPOOLER_STATUS, PR_OBJECT_TYPE, PR_ADDRTYPE_W, PR_RESPONSIBILITY } };
SizedSPropTagArray(8, sPropAttachColumns) = {8, { PR_ATTACH_NUM, PR_INSTANCE_KEY, PR_RECORD_KEY, PR_RENDERING_POSITION, PR_ATTACH_FILENAME_W, PR_ATTACH_METHOD, PR_DISPLAY_NAME_W, PR_ATTACH_LONG_FILENAME_W } };

HRESULT ECMessageFactory::Create(ECMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags, BOOL bEmbedded, ECMAPIProp *lpRoot, ECMessage **lpMessage) const
{
	return ECMessage::Create(lpMsgStore, fNew, fModify, ulFlags, bEmbedded, lpRoot, lpMessage);
}

ECMessage::ECMessage(ECMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags, BOOL bEmbedded, ECMAPIProp *lpRoot) : ECMAPIProp(lpMsgStore, MAPI_MESSAGE, fModify, lpRoot, "IMessage")
{
	this->m_lpParentID = NULL;
	this->m_cbParentID = 0;
	this->ulObjFlags = ulFlags & MAPI_ASSOCIATED;
	this->lpRecips = NULL;
	this->lpAttachments = NULL;
	this->ulNextAttUniqueId = 0;
	this->ulNextRecipUniqueId = 0;
	this->fNew = fNew;
	this->m_bEmbedded = bEmbedded;
	this->m_bExplicitSubjectPrefix = FALSE;
	this->m_ulBodyType = bodyTypeUnknown;
	this->m_bInhibitSync = FALSE;
	this->m_bRecipsDirty = FALSE;

	// proptag, getprop, setprops, class, bRemovable, bHidden

	this->HrAddPropHandlers(PR_RTF_IN_SYNC,				GetPropHandler       ,DefaultSetPropIgnore,		(void*) this, TRUE,  FALSE);
	this->HrAddPropHandlers(PR_HASATTACH,				GetPropHandler       ,DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_NORMALIZED_SUBJECT,		GetPropHandler		 ,DefaultSetPropIgnore,		(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_PARENT_ENTRYID,			GetPropHandler       ,DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_MESSAGE_SIZE,			GetPropHandler       ,DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_DISPLAY_TO,				GetPropHandler       ,DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_DISPLAY_CC,				GetPropHandler       ,DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_DISPLAY_BCC,				GetPropHandler       ,DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_ACCESS,					GetPropHandler       ,DefaultSetPropComputed,	(void*) this, FALSE, FALSE);

	this->HrAddPropHandlers(PR_MESSAGE_ATTACHMENTS,		GetPropHandler       ,DefaultSetPropIgnore,	(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_MESSAGE_RECIPIENTS,		GetPropHandler       ,DefaultSetPropIgnore,	(void*) this, FALSE, FALSE);

	// Handlers for the various body types
	this->HrAddPropHandlers(PR_BODY,					GetPropHandler		 ,DefaultSetPropSetReal,	(void*) this, TRUE, FALSE);
	this->HrAddPropHandlers(PR_RTF_COMPRESSED,			GetPropHandler		 ,DefaultSetPropSetReal,	(void*) this, FALSE, FALSE);

	// Workaround for support html in outlook 2000/xp need SetPropHandler
	this->HrAddPropHandlers(PR_HTML,					GetPropHandler		 ,SetPropHandler,			(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_EC_BODY_FILTERED, GetPropHandler, SetPropHandler, static_cast<void *>(this), false, false);

	// The property 0x10970003 is set by outlook when browsing in the 'unread mail' searchfolder. It is used to make sure
	// that a message that you just read is not removed directly from view. It is set for each message which should be in the view
	// even though it is 'read', and is removed when you leave the folder. When you try to export this property to a PST, you get
	// an access denied error. We therefore hide this property, ie you can GetProps/SetProps it and use it in a restriction, but
	// GetPropList will never list it (same as in a PST).
	this->HrAddPropHandlers(0x10970003,					DefaultGetPropGetReal,DefaultSetPropSetReal,	(void*) this, TRUE, TRUE);

	// Don't show the PR_EC_IMAP_ID, and mark it as computed and deletable. This makes sure that CopyTo() will not copy it to
	// the other message.
	this->HrAddPropHandlers(PR_EC_IMAP_ID,      		DefaultGetPropGetReal,DefaultSetPropComputed, 	(void*) this, TRUE, TRUE);

	// Make sure the MSGFLAG_HASATTACH flag gets added when needed.
	this->HrAddPropHandlers(PR_MESSAGE_FLAGS,      		GetPropHandler		,SetPropHandler,		 	(void*) this, FALSE, FALSE);

	// Make sure PR_SOURCE_KEY is available
	this->HrAddPropHandlers(PR_SOURCE_KEY,				GetPropHandler		,SetPropHandler,			(void*) this, TRUE, FALSE);

	// IMAP complete email, removable and hidden. setprop ignore? use interface for single-instancing
	this->HrAddPropHandlers(PR_EC_IMAP_EMAIL,			DefaultGetPropGetReal		,DefaultSetPropSetReal,			(void*) this, TRUE, TRUE);
	this->HrAddPropHandlers(PR_EC_IMAP_EMAIL_SIZE,		DefaultGetPropGetReal		,DefaultSetPropSetReal,			(void*) this, TRUE, TRUE);
	this->HrAddPropHandlers(CHANGE_PROP_TYPE(PR_EC_IMAP_BODY, PT_UNICODE),			DefaultGetPropGetReal		,DefaultSetPropSetReal,			(void*) this, TRUE, TRUE);
	this->HrAddPropHandlers(CHANGE_PROP_TYPE(PR_EC_IMAP_BODYSTRUCTURE, PT_UNICODE),	DefaultGetPropGetReal		,DefaultSetPropSetReal,			(void*) this, TRUE, TRUE);

	this->HrAddPropHandlers(PR_ASSOCIATED,				GetPropHandler,		DefaultSetPropComputed, 	(void *)this, TRUE, TRUE);
}

ECMessage::~ECMessage()
{
	MAPIFreeBuffer(m_lpParentID);
	if(lpRecips)
		lpRecips->Release();

	if(lpAttachments)
		lpAttachments->Release();
}

HRESULT	ECMessage::Create(ECMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags, BOOL bEmbedded, ECMAPIProp *lpRoot, ECMessage **lppMessage)
{
	ECMessage *lpMessage = new ECMessage(lpMsgStore, fNew, fModify, ulFlags, bEmbedded, lpRoot);
	return lpMessage->QueryInterface(IID_ECMessage, reinterpret_cast<void **>(lppMessage));
}

HRESULT	ECMessage::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMessage, this);
	REGISTER_INTERFACE2(ECMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMessage, &this->m_xMessage);
	REGISTER_INTERFACE2(IMAPIProp, &this->m_xMessage);
	REGISTER_INTERFACE2(IUnknown, &this->m_xMessage);
	REGISTER_INTERFACE3(ISelectUnicode, IUnknown, &this->m_xUnknown);
	REGISTER_INTERFACE2(IECSingleInstance, &this->m_xECSingleInstance);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMessage::GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray)
{
	HRESULT			hr = hrSuccess;
	ULONG			cValues = 0;
	SPropArrayPtr	ptrPropArray;
	LONG			lBodyIdx = 0;
	LONG			lRtfIdx = 0;
	LONG			lHtmlIdx = 0;

	if (lpPropTagArray) {
		lBodyIdx = Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_BODY_W, PT_UNSPECIFIED));
		lRtfIdx = Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_RTF_COMPRESSED, PT_UNSPECIFIED));
		lHtmlIdx = Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_HTML, PT_UNSPECIFIED));
	}
	if (lstProps == NULL && (!lpPropTagArray || lBodyIdx >= 0 || lRtfIdx >=0 || lHtmlIdx >= 0)) {
		// Get the properties from the server so we can determine the body type.
		m_ulBodyType = bodyTypeUnknown;		// Make sure no bodies are generated.
		hr = HrLoadProps();					// HrLoadProps will (re)determine the best body type.
		if (hr != hrSuccess)
			return hr;
	}

	if (m_ulBodyType != bodyTypeUnknown) {
		const ULONG ulBestMatchTable[4][3] = {
			/* unknown */ { PR_BODY_W, PR_RTF_COMPRESSED, PR_HTML },
			/* plain */   { PR_BODY_W, PR_RTF_COMPRESSED, PR_HTML },
			/* rtf */     { PR_RTF_COMPRESSED, PR_HTML, PR_BODY_W },
			/* html */    { PR_HTML, PR_RTF_COMPRESSED, PR_BODY_W }};

		SPropTagArrayPtr	ptrPropTagArray;
		ULONG				ulBestMatch = 0;

		if (lpPropTagArray) {
			// Use a temporary SPropTagArray so we can safely modify it.
			hr = Util::HrCopyPropTagArray(lpPropTagArray, &ptrPropTagArray);
			if (hr != hrSuccess)
				return hr;
		} else {
			// Get the proplist, so we can filter it.
			hr = GetPropList(ulFlags, &ptrPropTagArray);
			if (hr != hrSuccess)
				return hr;

			lBodyIdx = Util::FindPropInArray(ptrPropTagArray, CHANGE_PROP_TYPE(PR_BODY_W, PT_UNSPECIFIED));
			lRtfIdx = Util::FindPropInArray(ptrPropTagArray, CHANGE_PROP_TYPE(PR_RTF_COMPRESSED, PT_UNSPECIFIED));
			lHtmlIdx = Util::FindPropInArray(ptrPropTagArray, CHANGE_PROP_TYPE(PR_HTML, PT_UNSPECIFIED));
		}

		if (!lpPropTagArray || lBodyIdx >= 0 || lRtfIdx >= 0 || lHtmlIdx >= 0) {
			/*
			 * Exchange has a particular way of handling requests for different types of body content. There are three:
			 *
			 * - RTF (PR_RTF_COMPRESSED)
			 * - HTML (PR_HTML or PR_BODY_HTML, these are interchangeable in all cases)
			 * - Plaintext (PR_BODY)
			 *
			 * All of these properties are available (or none at all if there is no body) via OpenProperty() AT ALL TIMES, even if the
			 * item itself was not saved as that specific type of message. However, the exchange follows the following rules
			 * when multiple body types are requested in a single GetProps() call:
			 *
			 * - Only the 'best fit' property is returned as an actual value, the other properties are returned with PT_ERROR
			 * - Best fit for plaintext is (in order): PR_BODY, PR_RTF, PR_HTML
			 * - For RTF messages: PR_RTF, PR_HTML, PR_BODY
			 * - For HTML messages: PR_HTML, PR_RTF, PR_BODY
			 * - When GetProps() is called with NULL as the property list, the error value is MAPI_E_NOT_ENOUGH_MEMORY
			 * - When GetProps() is called with a list of properties, the error value is MAPI_E_NOT_ENOUGH_MEMORY or MAPI_E_NOT_FOUND depending on the following:
			 *   - When the requested property ID is higher than the best-match property, the value is MAPI_E_NOT_FOUND
			 *   - When the requested property ID is lower than the best-match property, the value is MAPI_E_NOT_ENOUGH_MEMORY
			 *
			 * Additionally, the normal rules for returning MAPI_E_NOT_ENOUGH_MEMORY apply (ie for large properties).
			 *
			 * Example: RTF message, PR_BODY, PR_HTML and PR_RTF_COMPRESSED requested in single GetProps() call:
			 * returns: PR_BODY -> MAPI_E_NOT_ENOUGH_MEMORY, PR_HTML -> MAPI_E_NOT_FOUND, PR_RTF_COMPRESSED -> actual RTF content
			 *
			 * PR_RTF_IN_SYNC is normally always TRUE, EXCEPT if the following is true:
			 * - Both PR_RTF_COMPRESSED and PR_HTML are requested
			 * - Actual body type is HTML
			 *
			 * This is used to disambiguate the situation in which you request PR_RTF_COMPRESSED and PR_HTML and receive MAPI_E_NOT_ENOUGH_MEMORY for
			 * both properties (or both are OK but we never do that).
			 *
			 * Since the values of the properties depend on the requested property tag set, a property handler cannot be used in this
			 * case, and therefore the above logic is implemented here.
			 */

			// Find best match property in requested property set
			if (lpPropTagArray == NULL)
				// No properties specified, best match is always number one choice body property
				ulBestMatch = ulBestMatchTable[m_ulBodyType][0];
			else {
				// Find best match in requested set
				for (int i = 0; i < 3; ++i)
					if (Util::FindPropInArray(ptrPropTagArray, PROP_TAG(PT_UNSPECIFIED, PROP_ID(ulBestMatchTable[m_ulBodyType][i]))) >= 0) {
						ulBestMatch = ulBestMatchTable[m_ulBodyType][i];
						break;
					}
			}

			// Remove the non-best bodies from the requested set.
			if (lBodyIdx >= 0 && PROP_ID(ulBestMatch) != PROP_ID(PR_BODY))
				ptrPropTagArray->aulPropTag[lBodyIdx] = PR_NULL;
			if (lRtfIdx >= 0 && PROP_ID(ulBestMatch) != PROP_ID(PR_RTF_COMPRESSED))
				ptrPropTagArray->aulPropTag[lRtfIdx] = PR_NULL;
			if (lHtmlIdx >= 0 && PROP_ID(ulBestMatch) != PROP_ID(PR_HTML))
				ptrPropTagArray->aulPropTag[lHtmlIdx] = PR_NULL;

			hr = ECMAPIProp::GetProps(ptrPropTagArray, ulFlags, &cValues, &ptrPropArray);
			if (HR_FAILED(hr))
				return hr;

			// Set the correct errors on the filtered properties.
			if (lBodyIdx >= 0 && PROP_ID(ulBestMatch) != PROP_ID(PR_BODY)) {
				ptrPropArray[lBodyIdx].ulPropTag = CHANGE_PROP_TYPE(PR_BODY, PT_ERROR);
				ptrPropArray[lBodyIdx].Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
				hr = MAPI_W_ERRORS_RETURNED;
			}
			if (lRtfIdx >= 0 && PROP_ID(ulBestMatch) != PROP_ID(PR_RTF_COMPRESSED)) {
				ptrPropArray[lRtfIdx].ulPropTag = CHANGE_PROP_TYPE(PR_RTF_COMPRESSED, PT_ERROR);
				if (!lpPropTagArray)
					ptrPropArray[lRtfIdx].Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
				else
					ptrPropArray[lRtfIdx].Value.err = PROP_ID(PR_RTF_COMPRESSED) < PROP_ID(ulBestMatch) ? MAPI_E_NOT_ENOUGH_MEMORY : MAPI_E_NOT_FOUND;
				hr = MAPI_W_ERRORS_RETURNED;
			}
			if (lHtmlIdx >= 0 && PROP_ID(ulBestMatch) != PROP_ID(PR_HTML)) {
				ptrPropArray[lHtmlIdx].ulPropTag = CHANGE_PROP_TYPE(PR_HTML, PT_ERROR);
				ptrPropArray[lHtmlIdx].Value.err = lpPropTagArray ? MAPI_E_NOT_FOUND : MAPI_E_NOT_ENOUGH_MEMORY;
				hr = MAPI_W_ERRORS_RETURNED;
			}

			// RTF_IN_SYNC should be false only if the message is actually HTML and both RTF and HTML are requested
			// (we are indicating that RTF should not be used in this case). Note that PR_RTF_IN_SYNC is normally
			// forced to TRUE in our property handler, so we only need to change it to FALSE if needed.
			if (lHtmlIdx >= 0 && lRtfIdx >= 0 && m_ulBodyType == bodyTypeHTML) {
				LONG lSyncIdx = Util::FindPropInArray(ptrPropTagArray, CHANGE_PROP_TYPE(PR_RTF_IN_SYNC, PT_UNSPECIFIED));
				if (lSyncIdx >= 0) {
					ptrPropArray[lSyncIdx].ulPropTag = PR_RTF_IN_SYNC;
					ptrPropArray[lSyncIdx].Value.b = false;
				}
			}
		} else {  // !lpPropTagArray || lBodyIdx >= 0 || lRtfIdx >= 0 || lHtmlIdx >= 0
		    // lpPropTagArray was specified but no body properties were requested.
			hr = ECMAPIProp::GetProps(lpPropTagArray, ulFlags, &cValues, &ptrPropArray);
			if (HR_FAILED(hr))
				return hr;
		}
	} else {  // m_ulBodyType != bodyTypeUnknown
	    // We don't know what out body type is (yet).
		hr = ECMAPIProp::GetProps(lpPropTagArray, ulFlags, &cValues, &ptrPropArray);
		if (HR_FAILED(hr))
			return hr;
	}

	*lpcValues = cValues;
	*lppPropArray = ptrPropArray.release();
	return hr;
}

/**
 * Retrieve a body property (PR_BODY, PR_HTML or PR_RTF_COMPRESSED) and make sure it's synchronized
 * with the best body returned from the server. This implies that the body might be generated on the
 * fly.
 *
 * @param[in]		ulPropTag		The proptag of the body to retrieve.
 * @param[in]		ulFlags			Flags
 * @param[in]		lpBase			Base pointer for allocating more memory.
 * @param[in,out]	lpsPropValue	Pointer to an SPropValue that will be updated to contain the
 *									the body property.
 */
HRESULT ECMessage::GetSyncedBodyProp(ULONG ulPropTag, ULONG ulFlags, void *lpBase, LPSPropValue lpsPropValue)
{
	HRESULT hr;

	if (ulPropTag == PR_BODY_HTML)
	    ulPropTag = PR_HTML;

	hr = HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
	if (HR_FAILED(hr))
		return hr;

	if (PROP_TYPE(lpsPropValue->ulPropTag) == PT_ERROR &&
		lpsPropValue->Value.err == MAPI_E_NOT_FOUND &&
		m_ulBodyType != bodyTypeUnknown)
	{
		// If a non-best body was requested, we might need to generate it.
		if ((m_ulBodyType == bodyTypePlain && PROP_ID(ulPropTag) == PROP_ID(PR_BODY)) ||
			(m_ulBodyType == bodyTypeRTF && PROP_ID(ulPropTag) == PROP_ID(PR_RTF_COMPRESSED)) ||
			(m_ulBodyType == bodyTypeHTML && PROP_ID(ulPropTag) == PROP_ID(PR_HTML)))
			// Nothing more to do, the best body should be available or generated in HrLoadProps.
			return hr;

		hr = SyncBody(ulPropTag);
		if (hr != hrSuccess)
			return hr;

		// Retry now the body is generated.
		hr = HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
	}
	return hr;
}

/**
 * Synchronize the best body obtained from the server to the requested body.
 *
 * @param[in]	ulPropTag		The proptag of the body to synchronize.
 */
HRESULT ECMessage::SyncBody(ULONG ulPropTag)
{
	HRESULT hr = hrSuccess;
	const BOOL fModifyOld = fModify;

	if (m_ulBodyType == bodyTypeUnknown) {
	    // There's nothing to synchronize if we don't know what our best body type is.
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	if (!Util::IsBodyProp(ulPropTag)) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Temporary enable write access
	fModify = TRUE;

	if (m_ulBodyType == bodyTypePlain) {
		if (PROP_ID(ulPropTag) == PROP_ID(PR_RTF_COMPRESSED))
			hr = SyncPlainToRtf();
		else if (PROP_ID(ulPropTag) == PROP_ID(PR_HTML))
			hr = SyncPlainToHtml();
	} else if (m_ulBodyType == bodyTypeRTF) {
		if (PROP_ID(ulPropTag) == PROP_ID(PR_BODY) || PROP_ID(ulPropTag) == PROP_ID(PR_HTML))
			hr = SyncRtf();
	} else if (m_ulBodyType == bodyTypeHTML) {
		if (PROP_ID(ulPropTag) == PROP_ID(PR_BODY))
			hr = SyncHtmlToPlain();
		else if (PROP_ID(ulPropTag) == PROP_ID(PR_RTF_COMPRESSED))
			hr = SyncHtmlToRtf();
	}

exit:
	fModify = fModifyOld;

	return hr;
}

/**
 * Synchronize a plaintext body to an RTF body.
 */
HRESULT ECMessage::SyncPlainToRtf()
{
	HRESULT hr = hrSuccess;
	StreamPtr ptrBodyStream;
	StreamPtr ptrCompressedRtfStream;
	StreamPtr ptrUncompressedRtfStream;

	ULARGE_INTEGER emptySize = {{0,0}};
	assert(m_bInhibitSync == false);
	m_bInhibitSync = TRUE;

	hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, &ptrBodyStream);
	if (hr != hrSuccess)
		goto exit;

	hr = ECMAPIProp::OpenProperty(PR_RTF_COMPRESSED, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE|MAPI_MODIFY, &ptrCompressedRtfStream);
	if (hr != hrSuccess)
		goto exit;

	//Truncate to zero
	hr = ptrCompressedRtfStream->SetSize(emptySize);
	if (hr != hrSuccess)
		goto exit;

	hr = WrapCompressedRTFStream(ptrCompressedRtfStream, MAPI_MODIFY, &ptrUncompressedRtfStream);
	if (hr != hrSuccess)
		goto exit;

	// Convert it now
	hr = Util::HrTextToRtf(ptrBodyStream, ptrUncompressedRtfStream);
	if (hr != hrSuccess)
		goto exit;

	// Commit uncompress data
	hr = ptrUncompressedRtfStream->Commit(0);
	if (hr != hrSuccess)
		goto exit;

	// Commit compresed data
	hr = ptrCompressedRtfStream->Commit(0);
	if (hr != hrSuccess)
		goto exit;

	// We generated this property but don't really want to save it to the server
	HrSetCleanProperty(PR_RTF_COMPRESSED);

	// and mark it as deleted, since we want the server to remove the old version if this was in the database
	m_setDeletedProps.insert(PR_RTF_COMPRESSED);

exit:
	m_bInhibitSync = FALSE;
	return hr;
}

/**
 * Synchronize a plaintext body to an HTML body.
 */
HRESULT ECMessage::SyncPlainToHtml()
{
	HRESULT hr = hrSuccess;
	StreamPtr ptrBodyStream;
	unsigned int ulCodePage = 0;
	StreamPtr ptrHtmlStream;

	ULARGE_INTEGER emptySize = {{0,0}};
	assert(m_bInhibitSync == false);
	m_bInhibitSync = TRUE;

	hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, &ptrBodyStream);
	if (hr != hrSuccess)
		goto exit;

	hr = ECMAPIProp::OpenProperty(PR_HTML, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE|MAPI_MODIFY, &ptrHtmlStream);
	if (hr != hrSuccess)
		goto exit;

	hr = GetCodePage(&ulCodePage);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrHtmlStream->SetSize(emptySize);
	if (hr != hrSuccess)
		goto exit;

	hr = Util::HrTextToHtml(ptrBodyStream, ptrHtmlStream, ulCodePage);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrHtmlStream->Commit(0);
	if (hr != hrSuccess)
		goto exit;

	// We generated this property but don't really want to save it to the server
	HrSetCleanProperty(PR_HTML);

	// and mark it as deleted, since we want the server to remove the old version if this was in the database
	m_setDeletedProps.insert(PR_HTML);

exit:
	m_bInhibitSync = FALSE;
	return hr;
}

/**
 * Synchronize an RTF body to a plaintext and an HTML body.
 */
HRESULT ECMessage::SyncRtf()
{
	enum eRTFType { RTFTypeOther, RTFTypeFromText, RTFTypeFromHTML};

	HRESULT hr = hrSuccess;
	string strRTF;
	bool bDone = false;
	unsigned int ulCodePage = 0;
	StreamPtr ptrHTMLStream;
	ULONG ulWritten = 0;
	eRTFType rtfType = RTFTypeOther;

	ULARGE_INTEGER emptySize = {{0,0}};
	LARGE_INTEGER moveBegin = {{0,0}};
	assert(m_bInhibitSync == false);
	m_bInhibitSync = TRUE;

	hr = GetRtfData(&strRTF);
	if (hr != hrSuccess)
		goto exit;

	hr = GetCodePage(&ulCodePage);
	if (hr != hrSuccess)
		goto exit;

	hr = ECMAPIProp::OpenProperty(PR_HTML, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &ptrHTMLStream);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrHTMLStream->SetSize(emptySize);
	if (hr != hrSuccess)
		goto exit;

	// Determine strategy based on RTF type.
	if (isrtfhtml(strRTF.c_str(), strRTF.size()))
		rtfType = RTFTypeFromHTML;
	else if (isrtftext(strRTF.c_str(), strRTF.size()))
		rtfType = RTFTypeFromText;
	else
		rtfType = RTFTypeOther;

	if (rtfType == RTFTypeOther) {
		BOOL bUpdated;
		hr = RTFSync(&this->m_xMessage, RTF_SYNC_RTF_CHANGED, &bUpdated);
		if (hr == hrSuccess) {
			StreamPtr ptrBodyStream;

			hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, &ptrBodyStream);
			if (hr != hrSuccess)
				goto exit;

			hr = ptrHTMLStream->SetSize(emptySize);
			if (hr != hrSuccess)
				goto exit;

			hr = Util::HrTextToHtml(ptrBodyStream, ptrHTMLStream, ulCodePage);
			if (hr != hrSuccess)
				goto exit;

			hr = ptrHTMLStream->Commit(0);
			if (hr != hrSuccess)
				goto exit;

			bDone = true;
		}
	}

	if (!bDone) {
		string strHTML;
		StreamPtr ptrBodyStream;

		switch (rtfType) {
		case RTFTypeOther:
			hr = HrExtractHTMLFromRealRTF(strRTF, strHTML, ulCodePage);
			break;
		case RTFTypeFromText:
			hr = HrExtractHTMLFromTextRTF(strRTF, strHTML, ulCodePage);
			break;
		case RTFTypeFromHTML:
			hr = HrExtractHTMLFromRTF(strRTF, strHTML, ulCodePage);
			break;
		}
		if (hr != hrSuccess)
			goto exit;

		hr = ptrHTMLStream->Write(strHTML.c_str(), strHTML.size(), &ulWritten);
		if (hr != hrSuccess)
			goto exit;

		hr = ptrHTMLStream->Commit(0);
		if (hr != hrSuccess)
			goto exit;

		hr = ptrHTMLStream->Seek(moveBegin, STREAM_SEEK_SET, NULL);
		if (hr != hrSuccess)
			goto exit;

		hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &ptrBodyStream);
		if (hr != hrSuccess)
			goto exit;

		hr = ptrBodyStream->SetSize(emptySize);
		if (hr != hrSuccess)
			goto exit;

		hr = Util::HrHtmlToText(ptrHTMLStream, ptrBodyStream, ulCodePage);
		if (hr != hrSuccess)
			goto exit;

		hr = ptrBodyStream->Commit(0);
		if (hr != hrSuccess)
			goto exit;
	}

	if (rtfType == RTFTypeOther) {
		// No need to store the HTML.
		HrSetCleanProperty(PR_HTML);
		// And delete from server in case it changed.
		m_setDeletedProps.insert(PR_HTML);
	} else if (rtfType == RTFTypeFromText) {
		// No need to store anything but the plain text.
		HrSetCleanProperty(PR_RTF_COMPRESSED);
		HrSetCleanProperty(PR_HTML);
		// And delete them both.
		m_setDeletedProps.insert(PR_RTF_COMPRESSED);
		m_setDeletedProps.insert(PR_HTML);
	} else if (rtfType == RTFTypeFromHTML) {
		// No need to keep the RTF version
		HrSetCleanProperty(PR_RTF_COMPRESSED);
		// And delete from server.
		m_setDeletedProps.insert(PR_RTF_COMPRESSED);
	}

exit:
	m_bInhibitSync = FALSE;
	return hr;
}

/**
 * Synchronize an HTML body to a plaintext body.
 */
HRESULT ECMessage::SyncHtmlToPlain()
{
	HRESULT hr = hrSuccess;
	StreamPtr ptrHtmlStream;
	StreamPtr ptrBodyStream;
	unsigned int ulCodePage;

	ULARGE_INTEGER emptySize = {{0,0}};

	assert(m_bInhibitSync == FALSE);
	m_bInhibitSync = TRUE;

	hr = ECMAPIProp::OpenProperty(PR_HTML, &IID_IStream, 0, 0, &ptrHtmlStream);
	if (hr != hrSuccess)
		goto exit;

	hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, STGM_WRITE|STGM_TRANSACTED, MAPI_CREATE|MAPI_MODIFY, &ptrBodyStream);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrBodyStream->SetSize(emptySize);
	if (hr != hrSuccess)
		goto exit;

	hr = GetCodePage(&ulCodePage);
	if (hr != hrSuccess)
		goto exit;

	hr = Util::HrHtmlToText(ptrHtmlStream, ptrBodyStream, ulCodePage);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrBodyStream->Commit(0);
	if (hr != hrSuccess)
		goto exit;

exit:
	m_bInhibitSync = FALSE;
	return hr;
}

/**
 * Synchronize an HTML body to an RTF body.
 */
HRESULT ECMessage::SyncHtmlToRtf()
{
	HRESULT hr = hrSuccess;
	StreamPtr ptrHtmlStream;
	StreamPtr ptrRtfCompressedStream;
	StreamPtr ptrRtfUncompressedStream;
	unsigned int ulCodePage;

	ULARGE_INTEGER emptySize = {{0,0}};
	assert(!m_bInhibitSync);
	m_bInhibitSync = TRUE;

	hr = ECMAPIProp::OpenProperty(PR_HTML, &IID_IStream, 0, 0, &ptrHtmlStream);
	if (hr != hrSuccess)
		goto exit;

	hr = ECMAPIProp::OpenProperty(PR_RTF_COMPRESSED, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE|MAPI_MODIFY, &ptrRtfCompressedStream);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrRtfCompressedStream->SetSize(emptySize);
	if (hr != hrSuccess)
		goto exit;

	hr = WrapCompressedRTFStream(ptrRtfCompressedStream, MAPI_MODIFY, &ptrRtfUncompressedStream);
	if (hr != hrSuccess)
		goto exit;

	hr = GetCodePage(&ulCodePage);
	if (hr != hrSuccess)
		goto exit;

	// Convert it now
	hr = Util::HrHtmlToRtf(ptrHtmlStream, ptrRtfUncompressedStream, ulCodePage);
	if (hr != hrSuccess)
		goto exit;

	// Commit uncompress data
	hr = ptrRtfUncompressedStream->Commit(0);
	if (hr != hrSuccess)
		goto exit;

	// Commit compresed data
	hr = ptrRtfCompressedStream->Commit(0);
	if (hr != hrSuccess)
		goto exit;

	// We generated this property but don't really want to save it to the server
	HrSetCleanProperty(PR_RTF_COMPRESSED);

	// and mark it as deleted, since we want the server to remove the old version if this was in the database
	m_setDeletedProps.insert(PR_RTF_COMPRESSED);

exit:
	m_bInhibitSync = FALSE;
	return hr;
}

HRESULT ECMessage::GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray)
{
	HRESULT hr = hrSuccess;
	const eBodyType ulBodyTypeSaved = m_ulBodyType;
	SPropTagArrayPtr ptrPropTagArray;
	SPropTagArrayPtr ptrPropTagArrayMod;
	bool bHaveBody;
	bool bHaveRtf;
	bool bHaveHtml;

	m_ulBodyType = bodyTypeUnknown;	// Make sure no bodies are generated when attempts are made to open them to check the error code if any.

	hr = ECMAPIProp::GetPropList(ulFlags, &ptrPropTagArray);
	if (hr != hrSuccess)
		goto exit;

	// Because the body type was set to unknown, ECMAPIProp::GetPropList does not return the proptags of the bodies that can be
	// generated unless they were already generated.
	// So we need to check which body properties are included and if at least one is, we need to make sure all of them are.
	bHaveBody = Util::FindPropInArray(ptrPropTagArray, CHANGE_PROP_TYPE(PR_BODY, PT_UNSPECIFIED)) >= 0;
	bHaveRtf = Util::FindPropInArray(ptrPropTagArray, PR_RTF_COMPRESSED) >= 0;
	bHaveHtml = Util::FindPropInArray(ptrPropTagArray, PR_HTML) >= 0;

	if ((bHaveBody && bHaveRtf && bHaveHtml) ||
		(!bHaveBody && !bHaveRtf && !bHaveHtml))
	{
		// Nothing more to do
		*lppPropTagArray = ptrPropTagArray.release();
		goto exit;
	}

	// We have at least one body prop. Determine which tags to add.
	hr = ECAllocateBuffer(CbNewSPropTagArray(ptrPropTagArray->cValues + 2), &ptrPropTagArrayMod);
	if (hr != hrSuccess)
		goto exit;

	ptrPropTagArrayMod->cValues = ptrPropTagArray->cValues;
	memcpy(ptrPropTagArrayMod->aulPropTag, ptrPropTagArray->aulPropTag, sizeof(*ptrPropTagArrayMod->aulPropTag) * ptrPropTagArrayMod->cValues);

	// All three booleans can't be NULL at the same time, so two additions max.
	if (!bHaveBody)
		ptrPropTagArrayMod->aulPropTag[ptrPropTagArrayMod->cValues++] = (ulFlags & MAPI_UNICODE ? PR_BODY_W : PR_BODY_A);
	if (!bHaveRtf)
		ptrPropTagArrayMod->aulPropTag[ptrPropTagArrayMod->cValues++] = PR_RTF_COMPRESSED;
	if (!bHaveHtml)
		ptrPropTagArrayMod->aulPropTag[ptrPropTagArrayMod->cValues++] = PR_HTML;

	*lppPropTagArray = ptrPropTagArrayMod.release();

exit:
	m_ulBodyType = ulBodyTypeSaved;
	return hr;
}

HRESULT ECMessage::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	HRESULT hr = MAPI_E_INTERFACE_NOT_SUPPORTED;

	if (lpiid == NULL)
		return MAPI_E_INVALID_PARAMETER;

//FIXME: Support the flags ?
	if(ulPropTag == PR_MESSAGE_ATTACHMENTS) {
		if(*lpiid == IID_IMAPITable)
			hr = GetAttachmentTable(ulInterfaceOptions, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_MESSAGE_RECIPIENTS) {
		if (*lpiid == IID_IMAPITable)
			hr = GetRecipientTable(ulInterfaceOptions, (LPMAPITABLE*)lppUnk);
	} else {
		// Workaround for support html in outlook 2000/xp
		if(ulPropTag == PR_BODY_HTML)
			ulPropTag = PR_HTML;

		hr = ECMAPIProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
		if (hr == MAPI_E_NOT_FOUND && m_ulBodyType != bodyTypeUnknown && Util::IsBodyProp(ulPropTag)) {
			hr = SyncBody(ulPropTag);
			if (hr != hrSuccess)
				return hr;

			// Retry now the body is generated.
			hr = ECMAPIProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
		}
	}
	return hr;
}

HRESULT ECMessage::GetAttachmentTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	HRESULT hr = hrSuccess;
	ECMemTableView *lpView = NULL;
	LPSPropValue lpPropID = NULL;
	LPSPropValue lpPropType = NULL;
	LPSPropTagArray lpPropTagArray = NULL;
	scoped_rlock lock(m_hMutexMAPIObject);

	if(lstProps == NULL) {
		hr = HrLoadProps();
		if (hr != hrSuccess)
			goto exit;
		if(lstProps == NULL) {
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}
	}

	if (this->lpAttachments == NULL) {
		hr = Util::HrCopyUnicodePropTagArray(ulFlags,
		     sPropAttachColumns, &lpPropTagArray);
		if(hr != hrSuccess)
			goto exit;

		hr = ECMemTable::Create(lpPropTagArray, PR_ATTACH_NUM, &this->lpAttachments);
		if(hr != hrSuccess)
			goto exit;

		// This code is resembles the table-copying code in GetRecipientTable, but we do some slightly different
		// processing on the data that we receive from the table. Basically, data is copied to the attachment
		// table received from the server through m_sMapiObject, but the PR_ATTACH_NUM is re-generated locally
		if (!fNew) {
			// existing message has "table" in m_sMapiObject data
			for (const auto &obj : *m_sMapiObject->lstChildren) {
				if (obj->ulObjType != MAPI_ATTACH)
					continue;
				if (obj->bDelete)
					continue;

				this->ulNextAttUniqueId = obj->ulUniqueId > this->ulNextAttUniqueId ? obj->ulUniqueId : this->ulNextAttUniqueId;
				++this->ulNextAttUniqueId;

				ULONG ulProps = obj->lstProperties->size();
				LPSPropValue lpProps = NULL;
				SPropValue sKeyProp;
				ULONG i;

				// +1 for maybe missing PR_ATTACH_NUM property
				// +1 for maybe missing PR_OBJECT_TYPE property
				ECAllocateBuffer(sizeof(SPropValue)*(ulProps+2), (void**)&lpProps);

				lpPropID = NULL;
				lpPropType = NULL;

				i = 0;
				for (const auto &pv : *obj->lstProperties) {
					pv.CopyToByRef(&lpProps[i]);
					if (lpProps[i].ulPropTag == PR_ATTACH_NUM) {
						lpPropID = &lpProps[i];
					} else if (lpProps[i].ulPropTag == PR_OBJECT_TYPE) {
						lpPropType = &lpProps[i];
					} else if (PROP_ID(lpProps[i].ulPropTag) == PROP_ID(PR_ATTACH_DATA_OBJ)) {
						lpProps[i].ulPropTag = CHANGE_PROP_TYPE(lpProps[i].ulPropTag, PT_ERROR);
						lpProps[i].Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
					} else if (PROP_TYPE(lpProps[i].ulPropTag) == PT_BINARY && lpProps[i].Value.bin.cb > MAX_TABLE_PROPSIZE) {
						lpProps[i].ulPropTag = CHANGE_PROP_TYPE(lpProps[i].ulPropTag, PT_ERROR);
						lpProps[i].Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
					}
					++i;
				}

				if (lpPropID == NULL) {
					++ulProps;
					lpPropID = &lpProps[i++];
				}
				lpPropID->ulPropTag = PR_ATTACH_NUM;
				lpPropID->Value.ul = obj->ulUniqueId; // use uniqueid from "recount" code in WSMAPIPropStorage::desoapertize()

				if (lpPropType == NULL) {
					++ulProps;
					lpPropType = &lpProps[i++];
				}
				lpPropType->ulPropTag = PR_OBJECT_TYPE;
				lpPropType->Value.ul = MAPI_ATTACH;

				sKeyProp.ulPropTag = PR_EC_HIERARCHYID;
				sKeyProp.Value.ul = obj->ulObjId;
				hr = lpAttachments->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &sKeyProp, lpProps, i);
				if (hr != hrSuccess)
					goto exit; // continue?

				ECFreeBuffer(lpProps);
				lpProps = NULL;
			}

			// since we just loaded the table, all enties are clean (actually not required for attachments, but it doesn't hurt)
			hr = lpAttachments->HrSetClean();
			if (hr != hrSuccess)
				goto exit;
		} // !new == empty table
	}

	if(this->lpAttachments == NULL) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = lpAttachments->HrGetView(createLocaleFromName(""), ulFlags & MAPI_UNICODE, &lpView);

	if(hr != hrSuccess)
		goto exit;

	hr = lpView->QueryInterface(IID_IMAPITable, (void **)lppTable);

	lpView->Release();

exit:
	MAPIFreeBuffer(lpPropTagArray);
	return hr;
}

HRESULT ECMessage::OpenAttach(ULONG ulAttachmentNum, LPCIID lpInterface, ULONG ulFlags, LPATTACH *lppAttach)
{
	HRESULT				hr = hrSuccess;
	IMAPITable			*lpTable = NULL;
	ECAttach			*lpAttach = NULL;
	IECPropStorage		*lpParentStorage = NULL;
	SPropValue			sID;
	LPSPropValue		lpObjId = NULL;
	ULONG				ulObjId;

	if(this->lpAttachments == NULL) {
		hr = this->GetAttachmentTable(fMapiUnicode, &lpTable);

		if(hr != hrSuccess)
			goto exit;

		lpTable->Release();
	}

	if(this->lpAttachments == NULL) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = ECAttach::Create(this->GetMsgStore(), MAPI_ATTACH, TRUE, ulAttachmentNum, m_lpRoot, &lpAttach);

	if(hr != hrSuccess)
		goto exit;

	sID.ulPropTag = PR_ATTACH_NUM;
	sID.Value.ul = ulAttachmentNum;
	if (lpAttachments->HrGetRowID(&sID, &lpObjId) == hrSuccess)
		ulObjId = lpObjId->Value.ul;
	else
		ulObjId = 0;

	hr = this->GetMsgStore()->lpTransport->HrOpenParentStorage(this, ulAttachmentNum, ulObjId, this->lpStorage->GetServerStorage(), &lpParentStorage);
	if(hr != hrSuccess)
		goto exit;

	hr = lpAttach->HrSetPropStorage(lpParentStorage, TRUE);
	if(hr != hrSuccess)
		goto exit;

	hr = lpAttach->QueryInterface(IID_IAttachment, (void **)lppAttach);

	// Register the object as a child of ours
	AddChild(lpAttach);

	lpAttach->Release();

exit:
	if (hr != hrSuccess && lpAttach != nullptr)
		lpAttach->Release();
	if (lpParentStorage)
		lpParentStorage->Release();

	if(lpObjId)
		ECFreeBuffer(lpObjId);

	return hr;
}

HRESULT ECMessage::CreateAttach(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulAttachmentNum, LPATTACH *lppAttach)
{
	return CreateAttach(lpInterface, ulFlags, ECAttachFactory(), lpulAttachmentNum, lppAttach);
}

HRESULT ECMessage::CreateAttach(LPCIID lpInterface, ULONG ulFlags, const IAttachFactory &refFactory, ULONG *lpulAttachmentNum, LPATTACH *lppAttach)
{
	HRESULT				hr = hrSuccess;
	IMAPITable*			lpTable = NULL;
	ECAttach*			lpAttach = NULL;
	SPropValue			sID;
	IECPropStorage*		lpStorage = NULL;

	if(this->lpAttachments == NULL) {
		hr = this->GetAttachmentTable(fMapiUnicode, &lpTable);

		if(hr != hrSuccess)
			goto exit;

		lpTable->Release();
	}

	if(this->lpAttachments == NULL) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = refFactory.Create(this->GetMsgStore(), MAPI_ATTACH, TRUE, this->ulNextAttUniqueId, m_lpRoot, &lpAttach);

	if(hr != hrSuccess)
		goto exit;

	hr = lpAttach->HrLoadEmptyProps();

	if(hr != hrSuccess)
		goto exit;

	sID.ulPropTag = PR_ATTACH_NUM;
	sID.Value.ul = this->ulNextAttUniqueId;

	hr = this->GetMsgStore()->lpTransport->HrOpenParentStorage(this, this->ulNextAttUniqueId, 0, NULL, &lpStorage);
	if(hr != hrSuccess)
		goto exit;

	hr = lpAttach->HrSetPropStorage(lpStorage, FALSE);
	if(hr != hrSuccess)
		goto exit;

	hr = lpAttach->SetProps(1, &sID, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpAttach->QueryInterface(IID_IAttachment, (void **)lppAttach);

	AddChild(lpAttach);

	lpAttach->Release();

	*lpulAttachmentNum = sID.Value.ul;

	// successfully created attachment, so increment counter for the next
	++this->ulNextAttUniqueId;

exit:
	if(lpStorage)
		lpStorage->Release();

	return hr;
}

HRESULT ECMessage::DeleteAttach(ULONG ulAttachmentNum, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	HRESULT hr;
	IMAPITable *lpTable = NULL;
	SPropValue sPropID;

	if(this->lpAttachments == NULL) {
		hr = this->GetAttachmentTable(fMapiUnicode, &lpTable);

		if(hr != hrSuccess)
			return hr;

		lpTable->Release();
	}

	if (this->lpAttachments == NULL)
		return MAPI_E_CALL_FAILED;

	sPropID.ulPropTag = PR_ATTACH_NUM;
	sPropID.Value.ul = ulAttachmentNum;

	hr = this->lpAttachments->HrModifyRow(ECKeyTable::TABLE_ROW_DELETE, NULL, &sPropID, 1);
	if (hr !=hrSuccess)
		return hr;
	// the object is deleted from the child list when SaveChanges is called, which calls SyncAttachments()

	return hrSuccess;
}

HRESULT ECMessage::GetRecipientTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	HRESULT hr = hrSuccess;
	ECMemTableView *lpView = NULL;
	LPSPropTagArray lpPropTagArray = NULL;
	scoped_rlock lock(m_hMutexMAPIObject);

	if(lstProps == NULL) {
		hr = HrLoadProps();
		if (hr != hrSuccess)
			goto exit;
		if(lstProps == NULL) {
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}
	}

	if (this->lpRecips == NULL) {
		hr = Util::HrCopyUnicodePropTagArray(ulFlags,
		     sPropRecipColumns, &lpPropTagArray);
		if(hr != hrSuccess)
			goto exit;

		hr = ECMemTable::Create(lpPropTagArray, PR_ROWID, &lpRecips);
		if(hr != hrSuccess)
			goto exit;

		// What we do here is that we reconstruct a recipient table from the m_sMapiObject data, and then process it in two ways:
		// 1. Remove PR_ROWID values and replace them with client-side values
		// 2. Replace PR_EC_CONTACT_ENTRYID with PR_ENTRYID in the table
		// This means that the PR_ENTRYID value from the original recipient is not actually in the table (only
		// in the lpID value passed to HrModifyRow)

		// Get the existing table for this message (there is none if the message is unsaved)
		if (!fNew) {
			// existing message has "table" in m_sMapiObject data
			for (const auto &obj : *m_sMapiObject->lstChildren) {
				// The only valid types are MAPI_MAILUSER and MAPI_DISTLIST. However some MAPI clients put in other
				// values as object type. We know about the existence of MAPI_ATTACH as another valid subtype for
				// Messages, so we'll skip those, treat MAPI_DISTLIST as MAPI_DISTLIST and anything else as
				// MAPI_MAILUSER.
				if (obj->ulObjType == MAPI_ATTACH)
					continue;
				if (obj->bDelete)
					continue;

				this->ulNextRecipUniqueId = obj->ulUniqueId > this->ulNextRecipUniqueId ? obj->ulUniqueId : this->ulNextRecipUniqueId;
				++this->ulNextRecipUniqueId;

				ULONG ulProps = obj->lstProperties->size();
				LPSPropValue lpProps = NULL;
				SPropValue sKeyProp;
				ULONG i = 0;
				LPSPropValue lpPropID = NULL;
				LPSPropValue lpPropObjType = NULL;

				// +1 for maybe missing PR_ROWID property
				// +1 for maybe missing PR_OBJECT_TYPE property
				ECAllocateBuffer(sizeof(SPropValue)*(ulProps+2), (void**)&lpProps);
				for (const auto &pv : *obj->lstProperties) {
					pv.CopyToByRef(&lpProps[i]);
					if (lpProps[i].ulPropTag == PR_ROWID)
						lpPropID = &lpProps[i];
					else if (lpProps[i].ulPropTag == PR_OBJECT_TYPE)
						lpPropObjType = &lpProps[i];
					else if (lpProps[i].ulPropTag == PR_EC_CONTACT_ENTRYID)
						// rename to PR_ENTRYID
						lpProps[i].ulPropTag = PR_ENTRYID;
					++i;
				}

				if (lpPropID == NULL) {
					++ulProps;
					lpPropID = &lpProps[i++];
				}
				lpPropID->ulPropTag = PR_ROWID;
				lpPropID->Value.ul = obj->ulUniqueId; // use uniqueid from "recount" code in WSMAPIPropStorage::ECSoapObjectToMapiObject()

				if (lpPropObjType == NULL) {
					++ulProps;
					lpPropObjType = &lpProps[i++];
				}
				lpPropObjType->ulPropTag = PR_OBJECT_TYPE;
				lpPropObjType->Value.ul = obj->ulObjType;
				sKeyProp.ulPropTag = PR_EC_HIERARCHYID;
				sKeyProp.Value.ul = obj->ulObjId;
				hr = lpRecips->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &sKeyProp, lpProps, i);
				if (hr != hrSuccess)
					goto exit;
				ECFreeBuffer(lpProps);
				lpProps = NULL;
			}

			// since we just loaded the table, all enties are clean
			hr = lpRecips->HrSetClean();
			if (hr != hrSuccess)
				goto exit;
		} // !fNew
	}

	hr = lpRecips->HrGetView(createLocaleFromName(""), ulFlags & MAPI_UNICODE, &lpView);
	if(hr != hrSuccess)
		goto exit;
	hr = lpView->QueryInterface(IID_IMAPITable, (void **)lppTable);
	lpView->Release();
exit:
	MAPIFreeBuffer(lpPropTagArray);
	return hr;
}

/*
 * This is not as easy as it seems. This is how we handle modifyrecipients:
 *
 * If the user specified a PR_ROWID, we always use their ROW ID
 * If the user specifies no PR_ROWID, we generate one, starting at 1 and going upward
 * MODIFY and ADD are the same
 *
 * This makes the following scenario possible:
 *
 * - Add row without id (row id 1 is generated)
 * - Add row without id (row id 2 is generated)
 * - Add row with id 5 (row 5 is added, we now have row 1,2,5)
 * - Add row with id 1 (row 1 is now modified, possibly without the caller wanting this)
 *
 * However, this seem to be what is required by outlook, as for example ExpandRecips from
 * the support object assumes the row id's to stay the same when it does ModifyRecipients(0, lpMods)
 * so we can't generate the ID's whenever ADD or 0 is specified.
 */

HRESULT ECMessage::ModifyRecipients(ULONG ulFlags, LPADRLIST lpMods)
{
	HRESULT hr = hrSuccess;
	IMAPITable *lpTable = NULL;
	LPSPropValue lpRecipProps = NULL;
	ULONG cValuesRecipProps = 0;
	SPropValue sPropAdd[2];
	SPropValue sKeyProp;
	unsigned int i = 0;

	if (lpMods == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (!fModify) {
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	// Load the recipients table object
	if(lpRecips == NULL) {
		hr = GetRecipientTable(fMapiUnicode, &lpTable);

		if(hr != hrSuccess)
			goto exit;

		lpTable->Release();
	}

	if(lpRecips == NULL) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	if(ulFlags == 0) {
		hr = lpRecips->HrDeleteAll();

		if(hr != hrSuccess)
			goto exit;

		ulNextRecipUniqueId = 0;
	}

	for (i = 0; i < lpMods->cEntries; ++i) {
		if(ulFlags & MODRECIP_ADD || ulFlags == 0) {
			// Add a new PR_ROWID
			sPropAdd[0].ulPropTag = PR_ROWID;
			sPropAdd[0].Value.ul = this->ulNextRecipUniqueId++;

			// Add a PR_INSTANCE_KEY which is equal to the row id
			sPropAdd[1].ulPropTag = PR_INSTANCE_KEY;
			sPropAdd[1].Value.bin.cb = sizeof(ULONG);
			sPropAdd[1].Value.bin.lpb = (unsigned char *)&sPropAdd[0].Value.ul;

			hr = Util::HrMergePropertyArrays(lpMods->aEntries[i].rgPropVals, lpMods->aEntries[i].cValues, sPropAdd, 2, &lpRecipProps, &cValuesRecipProps);
			if (hr != hrSuccess)
				continue;

			sKeyProp.ulPropTag = PR_EC_HIERARCHYID;
			sKeyProp.Value.ul = 0;

			// Add the new row
			hr = lpRecips->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &sKeyProp, lpRecipProps, cValuesRecipProps);

			if (lpRecipProps) {
				ECFreeBuffer(lpRecipProps);
				lpRecipProps = NULL;
			}
		} else if(ulFlags & MODRECIP_MODIFY) {
			// Simply update the existing row, leave the PR_EC_HIERARCHY key prop intact.
			hr = lpRecips->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, lpMods->aEntries[i].rgPropVals, lpMods->aEntries[i].cValues);
		} else if(ulFlags & MODRECIP_REMOVE) {
			hr = lpRecips->HrModifyRow(ECKeyTable::TABLE_ROW_DELETE, NULL, lpMods->aEntries[i].rgPropVals, lpMods->aEntries[i].cValues);
		}

		if(hr != hrSuccess)
			goto exit;
	}

	m_bRecipsDirty = TRUE;

exit:
	if(lpRecipProps)
		ECFreeBuffer(lpRecipProps);

	return hr;
}

HRESULT ECMessage::SubmitMessage(ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	SPropTagArray sPropTagArray;
	ULONG cValue = 0;
	ULONG ulRepCount = 0;
	ULONG ulPreprocessFlags = 0;
	ULONG ulSubmitFlag = 0;
	LPSPropValue lpsPropArray = NULL;
	LPMAPITABLE lpRecipientTable = NULL;
	LPSRowSet lpsRow = NULL;
	LPSPropValue lpRecip = NULL;
	ULONG cRecip = 0;
	SRowSet sRowSetRecip;
	SPropValue sPropResponsibility;
	FILETIME ft;

	// Get message flag to check for resubmit. PR_MESSAGE_FLAGS
	sPropTagArray.cValues = 1;
	sPropTagArray.aulPropTag[0] = PR_MESSAGE_FLAGS;

	hr = ECMAPIProp::GetProps(&sPropTagArray, 0, &cValue, &lpsPropArray);
	if(HR_FAILED(hr))
		goto exit;

	if(lpsPropArray->ulPropTag == PR_MESSAGE_FLAGS) {
		// Re-set 'unsent' as it is obviously not sent if we're submitting it ... This allows you to send a message
		// multiple times, but only if the client calls SubmitMessage multiple times.
		lpsPropArray->Value.ul |= MSGFLAG_UNSENT;

		hr = this->SetProps(1, lpsPropArray, NULL);
		if(hr != hrSuccess)
			goto exit;
	}

	// Get the recipientslist
	hr = this->GetRecipientTable(fMapiUnicode, &lpRecipientTable);
	if(hr != hrSuccess)
		goto exit;

	// Check if recipientslist is empty
	hr = lpRecipientTable->GetRowCount(0, &ulRepCount);
	if(hr != hrSuccess)
		goto exit;

	if(ulRepCount == 0) {
		hr = MAPI_E_NO_RECIPIENTS;
		goto exit;
	}

	// Step through recipient list, set PR_RESPONSIBILITY to FALSE for all recipients
	while(TRUE){
		hr = lpRecipientTable->QueryRows(1, 0L, &lpsRow);

		if (hr != hrSuccess)
			goto exit;

		if (lpsRow->cRows == 0)
			break;

		sPropResponsibility.ulPropTag = PR_RESPONSIBILITY;
		sPropResponsibility.Value.b = FALSE;

		// Set PR_RESPONSIBILITY
		hr = Util::HrAddToPropertyArray(lpsRow->aRow[0].lpProps, lpsRow->aRow[0].cValues, &sPropResponsibility, &lpRecip, &cRecip);

		if(hr != hrSuccess)
			goto exit;

		sRowSetRecip.cRows = 1;
		sRowSetRecip.aRow[0].lpProps = lpRecip;
		sRowSetRecip.aRow[0].cValues = cRecip;

		if(lpsRow->aRow[0].cValues > 1){
			hr = this->ModifyRecipients(MODRECIP_MODIFY, (LPADRLIST) &sRowSetRecip);
			if (hr != hrSuccess)
				goto exit;
		}

		ECFreeBuffer(lpRecip);
		lpRecip = NULL;

		FreeProws(lpsRow);
		lpsRow = NULL;
	}

	lpRecipientTable->Release();
	lpRecipientTable = NULL;

	// Get the time to add to the message as PR_CLIENT_SUBMIT_TIME
    GetSystemTimeAsFileTime(&ft);
        ECFreeBuffer(lpsPropArray);
        lpsPropArray = NULL;
	hr = ECAllocateBuffer(sizeof(SPropValue)*2, (void**)&lpsPropArray);
	if (hr != hrSuccess)
		goto exit;

	lpsPropArray[0].ulPropTag = PR_CLIENT_SUBMIT_TIME;
	lpsPropArray[0].Value.ft = ft;

	lpsPropArray[1].ulPropTag = PR_MESSAGE_DELIVERY_TIME;
	lpsPropArray[1].Value.ft = ft;

	hr = SetProps(2, lpsPropArray, NULL);
  	if (hr != hrSuccess)
		goto exit;

	ECFreeBuffer(lpsPropArray);
	lpsPropArray = NULL;

	// Resolve recipients
	hr = this->GetMsgStore()->lpSupport->ExpandRecips(&this->m_xMessage, &ulPreprocessFlags);
	if (hr != hrSuccess)
		goto exit;
	if (this->GetMsgStore()->IsOfflineStore())
		ulPreprocessFlags |= NEEDS_SPOOLER;

	// Setup PR_SUBMIT_FLAGS
	if (ulPreprocessFlags & NEEDS_PREPROCESSING)
		ulSubmitFlag = SUBMITFLAG_PREPROCESS;
	if (ulPreprocessFlags & NEEDS_SPOOLER)
		ulSubmitFlag = 0L;

	hr = ECAllocateBuffer(sizeof(SPropValue)*1, (void**)&lpsPropArray);

	if (hr != hrSuccess)
		goto exit;

	lpsPropArray[0].ulPropTag = PR_SUBMIT_FLAGS;
	lpsPropArray[0].Value.l = ulSubmitFlag;

	hr = SetProps(1, lpsPropArray, NULL);
	if (hr != hrSuccess)
		goto exit;

	ECFreeBuffer(lpsPropArray);
	lpsPropArray = NULL;

	// All done, save changes
	hr = SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		goto exit;

	// We look al ulPreprocessFlags to see whether to submit the message via the
	// spooler or not

	if(ulPreprocessFlags & NEEDS_SPOOLER) {
		TRACE_MAPI(TRACE_ENTRY, "Submitting through local queue, flags", "%d", ulPreprocessFlags);

		// Add this message into the local outgoing queue

		hr = this->GetMsgStore()->lpTransport->HrSubmitMessage(this->m_cbEntryId, this->m_lpEntryId, EC_SUBMIT_LOCAL);

		if(hr != hrSuccess)
			goto exit;

	} else {
		TRACE_MAPI(TRACE_ENTRY, "Submitting through master queue, flags", "%d", ulPreprocessFlags);

		// Add the message to the master outgoing queue, and request the spooler to DoSentMail()
		hr = this->GetMsgStore()->lpTransport->HrSubmitMessage(this->m_cbEntryId, this->m_lpEntryId, EC_SUBMIT_MASTER | EC_SUBMIT_DOSENTMAIL);

		if(hr != hrSuccess)
			goto exit;
	}

exit:
	if (lpRecip != NULL)
		ECFreeBuffer(lpRecip);
	if(lpsRow)
		FreeProws(lpsRow);

	if(lpsPropArray)
		ECFreeBuffer(lpsPropArray);

	if(lpRecipientTable)
		lpRecipientTable->Release();

	return hr;
}

HRESULT ECMessage::SetReadFlag(ULONG ulFlags)
{
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpReadReceiptRequest = NULL;
	LPSPropValue	lpPropFlags = NULL;
	LPSPropValue	lpsPropUserName = NULL;
	LPSPropTagArray	lpsPropTagArray = NULL;
	SPropValue		sProp;
	IMAPIFolder*	lpRootFolder = NULL;
	IMessage*		lpNewMessage = NULL;
	IMessage*		lpThisMessage = NULL;
	ULONG			ulObjType = 0;
	ULONG			cValues = 0;
	ULONG			cbStoreID = 0;
	LPENTRYID		lpStoreID = NULL;
	IMsgStore*		lpDefMsgStore = NULL;

	if((ulFlags &~ (CLEAR_READ_FLAG | CLEAR_NRN_PENDING | CLEAR_RN_PENDING | GENERATE_RECEIPT_ONLY | MAPI_DEFERRED_ERRORS | SUPPRESS_RECEIPT)) != 0 ||
		(ulFlags & (SUPPRESS_RECEIPT | CLEAR_READ_FLAG)) == (SUPPRESS_RECEIPT | CLEAR_READ_FLAG)||
		(ulFlags & (SUPPRESS_RECEIPT | CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY)) == (SUPPRESS_RECEIPT | CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY) ||
		(ulFlags & (CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY)) == (CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY) )
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if(m_lpParentID) {
		// Unsaved message, ignore (FIXME ?)
		hr = hrSuccess;
		goto exit;
	}

	// see if read receipts are requested
	hr = ECAllocateBuffer(CbNewSPropTagArray(2), (void**)&lpsPropTagArray);
	if(hr != hrSuccess)
		goto exit;

	// Check for Read receipts
	lpsPropTagArray->cValues = 2;
	lpsPropTagArray->aulPropTag[0] = PR_MESSAGE_FLAGS;
	lpsPropTagArray->aulPropTag[1] = PR_READ_RECEIPT_REQUESTED;

	hr = ECMAPIProp::GetProps(lpsPropTagArray, 0, &cValues, &lpReadReceiptRequest);

	if(hr == hrSuccess && (!(ulFlags&(SUPPRESS_RECEIPT|CLEAR_READ_FLAG | CLEAR_NRN_PENDING | CLEAR_RN_PENDING)) || (ulFlags&GENERATE_RECEIPT_ONLY )) &&
		lpReadReceiptRequest[1].Value.b == TRUE && ((lpReadReceiptRequest[0].Value.ul & MSGFLAG_RN_PENDING) || (lpReadReceiptRequest[0].Value.ul & MSGFLAG_NRN_PENDING)))
	{
		hr = QueryInterface(IID_IMessage, (void**)&lpThisMessage);
		if (hr != hrSuccess)
			goto exit;

		if((ulFlags & (GENERATE_RECEIPT_ONLY | SUPPRESS_RECEIPT)) == (GENERATE_RECEIPT_ONLY | SUPPRESS_RECEIPT) )
		{
			sProp.ulPropTag = PR_READ_RECEIPT_REQUESTED;
			sProp.Value.b = FALSE;
			hr = HrSetOneProp(lpThisMessage, &sProp);
			if (hr != hrSuccess)
				goto exit;

			hr = lpThisMessage->SaveChanges(KEEP_OPEN_READWRITE);
			if (hr != hrSuccess)
				goto exit;

		}else {
			// Open the default store, by using the username property
			hr = HrGetOneProp(&GetMsgStore()->m_xMsgStore, PR_USER_NAME, &lpsPropUserName);
			if (hr != hrSuccess)
				goto exit;

			hr = GetMsgStore()->CreateStoreEntryID(NULL, lpsPropUserName->Value.LPSZ, fMapiUnicode, &cbStoreID, &lpStoreID);
			if (hr != hrSuccess)
				goto exit;

			hr = GetMsgStore()->lpSupport->OpenEntry(cbStoreID, lpStoreID, NULL, MAPI_MODIFY, &ulObjType, (LPUNKNOWN *) &lpDefMsgStore);
			if (hr != hrSuccess)
				goto exit;

			// Open the root folder of the default store to create a new message
			hr = lpDefMsgStore->OpenEntry(0, NULL, NULL, MAPI_MODIFY, &ulObjType, (LPUNKNOWN *) &lpRootFolder);
			if (hr != hrSuccess)
				goto exit;

			hr = lpRootFolder->CreateMessage(NULL, 0, &lpNewMessage);
			if (hr != hrSuccess)
				goto exit;

			hr = ClientUtil::ReadReceipt(0, lpThisMessage, &lpNewMessage);
			if(hr != hrSuccess)
				goto exit;

			hr = lpNewMessage->SubmitMessage(FORCE_SUBMIT);
			if(hr != hrSuccess)
				goto exit;

			// Oke, everything is fine, so now remove MSGFLAG_RN_PENDING and MSGFLAG_NRN_PENDING from PR_MESSAGE_FLAGS
			// Sent CLEAR_NRN_PENDING and CLEAR_RN_PENDING  for remove those properties
			ulFlags |= CLEAR_NRN_PENDING | CLEAR_RN_PENDING;
		}

	}

	hr = this->GetMsgStore()->lpTransport->HrSetReadFlag(this->m_cbEntryId, this->m_lpEntryId, ulFlags, 0);
	if(hr != hrSuccess)
	    goto exit;

    // Server update OK, change local flags also
    if ((hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpPropFlags)) != hrSuccess)
		goto exit;
    hr = HrGetRealProp(PR_MESSAGE_FLAGS, ulFlags, lpPropFlags, lpPropFlags);
    if(hr != hrSuccess)
        goto exit;

    if (ulFlags & CLEAR_READ_FLAG)
        lpPropFlags->Value.ul &= ~MSGFLAG_READ;
    else
        lpPropFlags->Value.ul |= MSGFLAG_READ;

    hr = HrSetRealProp(lpPropFlags);
    if(hr != hrSuccess)
        goto exit;

exit:
	if (lpPropFlags != NULL)
		ECFreeBuffer(lpPropFlags);
	if(lpsPropTagArray)
		ECFreeBuffer(lpsPropTagArray);

	if(lpReadReceiptRequest)
		ECFreeBuffer(lpReadReceiptRequest);
	MAPIFreeBuffer(lpsPropUserName);
	MAPIFreeBuffer(lpStoreID);
	if(lpRootFolder)
		lpRootFolder->Release();

	if(lpNewMessage)
		lpNewMessage->Release();

	if(lpThisMessage)
		lpThisMessage->Release();

	if(lpDefMsgStore)
		lpDefMsgStore->Release();

	return hr;
}

/**
 * Synchronizes this object's PR_DISPLAY_* properties from the
 * contents of the recipient table. They are pushed to the server
 * on save.
 */
HRESULT ECMessage::SyncRecips()
{
	HRESULT hr = hrSuccess;
	std::wstring wstrTo;
	std::wstring wstrCc;
	std::wstring wstrBcc;
	SPropValue sPropRecip;
	IMAPITable *lpTable = NULL;
	LPSRowSet lpRows = NULL;
	SizedSPropTagArray(2, sPropDisplay) = {2, { PR_RECIPIENT_TYPE, PR_DISPLAY_NAME_W} };

	if (this->lpRecips) {
		hr = GetRecipientTable(fMapiUnicode, &lpTable);
		if (hr != hrSuccess)
			goto exit;
		hr = lpTable->SetColumns(sPropDisplay, 0);
		while (TRUE) {
			hr = lpTable->QueryRows(1, 0, &lpRows);
			if (hr != hrSuccess || lpRows->cRows != 1)
				break;

			if (lpRows->aRow[0].lpProps[0].ulPropTag == PR_RECIPIENT_TYPE && lpRows->aRow[0].lpProps[0].Value.ul == MAPI_TO) {
				if (lpRows->aRow[0].lpProps[1].ulPropTag == PR_DISPLAY_NAME_W) {
					if (wstrTo.length() > 0)
						wstrTo += L"; ";

					wstrTo += lpRows->aRow[0].lpProps[1].Value.lpszW;
				}
			}
			else if (lpRows->aRow[0].lpProps[0].ulPropTag == PR_RECIPIENT_TYPE && lpRows->aRow[0].lpProps[0].Value.ul == MAPI_CC) {
				if (lpRows->aRow[0].lpProps[1].ulPropTag == PR_DISPLAY_NAME_W) {
					if (wstrCc.length() > 0)
						wstrCc += L"; ";

					wstrCc += lpRows->aRow[0].lpProps[1].Value.lpszW;
				}
			}
			else if (lpRows->aRow[0].lpProps[0].ulPropTag == PR_RECIPIENT_TYPE && lpRows->aRow[0].lpProps[0].Value.ul == MAPI_BCC) {
				if (lpRows->aRow[0].lpProps[1].ulPropTag == PR_DISPLAY_NAME_W) {
					if (wstrBcc.length() > 0)
						wstrBcc += L"; ";

					wstrBcc += lpRows->aRow[0].lpProps[1].Value.lpszW;
				}
			}

			FreeProws(lpRows);
			lpRows = NULL;
		}

		sPropRecip.ulPropTag = PR_DISPLAY_TO_W;
		sPropRecip.Value.lpszW = (WCHAR *)wstrTo.c_str();

		HrSetRealProp(&sPropRecip);

		sPropRecip.ulPropTag = PR_DISPLAY_CC_W;
		sPropRecip.Value.lpszW = (WCHAR *)wstrCc.c_str();

		HrSetRealProp(&sPropRecip);

		sPropRecip.ulPropTag = PR_DISPLAY_BCC_W;
		sPropRecip.Value.lpszW = (WCHAR *)wstrBcc.c_str();

		HrSetRealProp(&sPropRecip);
	}

	m_bRecipsDirty = FALSE;

exit:
	if(lpRows)
		FreeProws(lpRows);
	lpRows = NULL;
	if(lpTable)
		lpTable->Release();

	return hr;
}

HRESULT ECMessage::SaveRecips()
{
	HRESULT				hr = hrSuccess;
	LPSRowSet			lpRowSet = NULL;
	LPSPropValue		lpObjIDs = NULL;
	LPSPropValue		lpRowId = NULL;
	LPULONG				lpulStatus = NULL;
	LPSPropValue		lpEntryID = NULL;
	unsigned int		i = 0,
						j = 0;
	ULONG				ulRealObjType;
	LPSPropValue		lpObjType = NULL;
	scoped_rlock lock(m_hMutexMAPIObject);

	// Get any changes and set it in the child list of this message
	hr = lpRecips->HrGetAllWithStatus(&lpRowSet, &lpObjIDs, &lpulStatus);
	if (hr != hrSuccess)
		goto exit;

	for (i = 0; i < lpRowSet->cRows; ++i) {
		MAPIOBJECT *mo = NULL;

		// Get the right object type for a DistList
		lpObjType = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_OBJECT_TYPE);
		if(lpObjType != NULL)
			ulRealObjType = lpObjType->Value.ul; // MAPI_MAILUSER or MAPI_DISTLIST
		else
			ulRealObjType = MAPI_MAILUSER; // add in list?

		lpRowId = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_ROWID); // unique value of recipient
		if (!lpRowId) {
			assert(lpRowId != NULL);
			continue;
		}

		AllocNewMapiObject(lpRowId->Value.ul, lpObjIDs[i].Value.ul, ulRealObjType, &mo);

		// Move any PR_ENTRYID's to PR_EC_CONTACT_ENTRYID
		lpEntryID = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_ENTRYID);
		if(lpEntryID)
			lpEntryID->ulPropTag = PR_EC_CONTACT_ENTRYID;

		if (lpulStatus[i] == ECROW_MODIFIED || lpulStatus[i] == ECROW_ADDED) {
			mo->bChanged = true;
			for (j = 0; j < lpRowSet->aRow[i].cValues; ++j)
				if(PROP_TYPE(lpRowSet->aRow[i].lpProps[j].ulPropTag) != PT_NULL) {
					mo->lstModified->push_back(ECProperty(&lpRowSet->aRow[i].lpProps[j]));
					// as in ECGenericProp.cpp, we also save the properties to the known list,
					// since this is used when we reload the object from memory.
					mo->lstProperties->push_back(ECProperty(&lpRowSet->aRow[i].lpProps[j]));
				}
		} else if (lpulStatus[i] == ECROW_DELETED) {
			mo->bDelete = true;
		} else {
			// ECROW_NORMAL, untouched recipient
			for (j = 0; j < lpRowSet->aRow[i].cValues; ++j)
				if(PROP_TYPE(lpRowSet->aRow[i].lpProps[j].ulPropTag) != PT_NULL)
					mo->lstProperties->push_back(ECProperty(&lpRowSet->aRow[i].lpProps[j]));
		}

		// find old recipient in child list, and remove if present
		auto iterSObj = m_sMapiObject->lstChildren->find(mo);
		if (iterSObj != m_sMapiObject->lstChildren->cend()) {
			FreeMapiObject(*iterSObj);
			m_sMapiObject->lstChildren->erase(iterSObj);
		}

		m_sMapiObject->lstChildren->insert(mo);
	}

	hr = lpRecips->HrSetClean();
	if(hr != hrSuccess)
		goto exit;

exit:
	if(lpObjIDs)
		ECFreeBuffer(lpObjIDs);

	if(lpRowSet)
		FreeProws(lpRowSet);

	if(lpulStatus)
		ECFreeBuffer(lpulStatus);
	return hr;
}

void ECMessage::RecursiveMarkDelete(MAPIOBJECT *lpObj) {
	lpObj->bDelete = true;
	lpObj->lstDeleted->clear();
	lpObj->lstAvailable->clear();
	lpObj->lstModified->clear();
	lpObj->lstProperties->clear();
	for (const auto &obj : *lpObj->lstChildren)
		RecursiveMarkDelete(obj);
}

BOOL ECMessage::HasAttachment()
{
	HRESULT hr = hrSuccess;
	BOOL bRet = TRUE;
	ECMapiObjects::const_iterator iterObjects;
	scoped_rlock lock(m_hMutexMAPIObject);

	if(lstProps == NULL) {
		hr = HrLoadProps();
		if (hr != hrSuccess)
			goto exit;
		if(lstProps == NULL) {
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}
	}

	for (iterObjects = m_sMapiObject->lstChildren->cbegin();
	     iterObjects != m_sMapiObject->lstChildren->cend(); ++iterObjects)
		if ((*iterObjects)->ulObjType == MAPI_ATTACH)
			break;

	bRet = (iterObjects != m_sMapiObject->lstChildren->end());

exit:
	if(hr != hrSuccess)
		bRet = FALSE;
	return bRet;
}

// Syncs the Attachment table to the child list in the saved object
HRESULT ECMessage::SyncAttachments()
{
	HRESULT				hr = hrSuccess;
	LPSRowSet			lpRowSet = NULL;
	LPSPropValue		lpObjIDs = NULL;
	LPSPropValue		lpAttachNum = NULL;
	LPULONG				lpulStatus = NULL;
	unsigned int		i = 0;
	LPSPropValue		lpObjType = NULL;
	scoped_rlock lock(m_hMutexMAPIObject);

	// Get any changes and set it in the child list of this message
	// Although we only need to know the deleted attachments, I also need to know the PR_ATTACH_NUM, which is in the rowset
	hr = lpAttachments->HrGetAllWithStatus(&lpRowSet, &lpObjIDs, &lpulStatus);
	if (hr != hrSuccess)
		goto exit;

	for (i = 0; i < lpRowSet->cRows; ++i) {
		if (lpulStatus[i] != ECROW_DELETED)
			continue;

		lpObjType = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_OBJECT_TYPE);
		if(lpObjType == NULL || lpObjType->Value.ul != MAPI_ATTACH)
			continue;

		lpAttachNum = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_ATTACH_NUM); // unique value of attachment
		if (!lpAttachNum) {
			assert(lpAttachNum != NULL);
			continue;
		}

		// delete complete attachment
		MAPIOBJECT find(lpObjType->Value.ul, lpAttachNum->Value.ul);
		auto iterSObj = m_sMapiObject->lstChildren->find(&find);
		if (iterSObj != m_sMapiObject->lstChildren->cend())
			RecursiveMarkDelete(*iterSObj);
	}

	hr = lpAttachments->HrSetClean();
	if(hr != hrSuccess)
		goto exit;

exit:
	if(lpObjIDs)
		ECFreeBuffer(lpObjIDs);

	if(lpRowSet)
		FreeProws(lpRowSet);

	if(lpulStatus)
		ECFreeBuffer(lpulStatus);
	return hr;
}

HRESULT ECMessage::UpdateTable(ECMemTable *lpTable, ULONG ulObjType, ULONG ulObjKeyProp) {
	HRESULT hr = hrSuccess;
	SPropValue sKeyProp;
	SPropValue sUniqueProp;
	LPSPropValue lpProps = NULL;
	LPSPropValue lpNewProps = NULL;
	LPSPropValue lpAllProps = NULL;
	ULONG cAllValues = 0;
	ULONG cValues = 0;
	ULONG ulProps = 0;
	ULONG i = 0;
	scoped_rlock lock(m_hMutexMAPIObject);

	if (!m_sMapiObject) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// update hierarchy id in table
	for (const auto &obj : *m_sMapiObject->lstChildren) {
		if (obj->ulObjType != ulObjType)
			continue;
		sUniqueProp.ulPropTag = ulObjKeyProp;
		sUniqueProp.Value.ul = obj->ulUniqueId;
		sKeyProp.ulPropTag = PR_EC_HIERARCHYID;
		sKeyProp.Value.ul = obj->ulObjId;

		hr = lpTable->HrUpdateRowID(&sKeyProp, &sUniqueProp, 1);
		if (hr != hrSuccess)
			goto exit;
		// put new server props in table too
		ulProps = obj->lstProperties->size();
		if (ulProps == 0)
			continue;
		// retrieve old row from table
		hr = lpTable->HrGetRowData(&sUniqueProp, &cValues, &lpProps);
		if (hr != hrSuccess)
			goto exit;
		// add new props
		if ((hr = MAPIAllocateBuffer(sizeof(SPropValue)*ulProps, (void**)&lpNewProps)) != hrSuccess)
			goto exit;
		i = 0;
		for (const auto &pv : *obj->lstProperties) {
			pv.CopyToByRef(&lpNewProps[i]);
			if (PROP_ID(lpNewProps[i].ulPropTag) == PROP_ID(PR_ATTACH_DATA_OBJ)) {
				lpNewProps[i].ulPropTag = CHANGE_PROP_TYPE(lpNewProps[i].ulPropTag, PT_ERROR);
				lpNewProps[i].Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
			} else if (PROP_TYPE(lpNewProps[i].ulPropTag) == PT_BINARY && lpNewProps[i].Value.bin.cb > MAX_TABLE_PROPSIZE) {
				lpNewProps[i].ulPropTag = CHANGE_PROP_TYPE(lpNewProps[i].ulPropTag, PT_ERROR);
				lpNewProps[i].Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
			}
			++i;
		}

		hr = Util::HrMergePropertyArrays(lpProps, cValues, lpNewProps, ulProps, &lpAllProps, &cAllValues);
		if (hr != hrSuccess)
			goto exit;
		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_MODIFY, &sKeyProp, lpAllProps, cAllValues);
		if (hr != hrSuccess)
			goto exit;
		MAPIFreeBuffer(lpNewProps);
		lpNewProps = NULL;
		MAPIFreeBuffer(lpAllProps);
		lpAllProps = NULL;
		MAPIFreeBuffer(lpProps);
		lpProps = NULL;
	}

	hr = lpTable->HrSetClean();
	if (hr != hrSuccess)
		goto exit;
exit:
	MAPIFreeBuffer(lpAllProps);
	MAPIFreeBuffer(lpNewProps);
	MAPIFreeBuffer(lpProps);
	return hr;
}

HRESULT ECMessage::SaveChanges(ULONG ulFlags)
{
	HRESULT				hr = hrSuccess;
	LPSPropTagArray		lpPropTagArray = NULL;
	LPSPropValue		lpsPropMessageFlags = NULL;
	ULONG				cValues = 0;
	scoped_rlock lock(m_hMutexMAPIObject);

	// could not have modified (easy way out of my bug)
	if (!fModify) {
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	// nothing changed -> no need to save
 	if (this->lstProps == NULL)
 		goto exit;

	assert(m_sMapiObject != NULL); // the actual bug .. keep open on submessage
	if (this->lpRecips) {
		hr = SaveRecips();
		if (hr != hrSuccess)
			goto exit;

		// Synchronize PR_DISPLAY_* ... FIXME should we do this after each ModifyRecipients ?
		SyncRecips();
	}

	if (this->lpAttachments) {
		// set deleted attachments in saved child list
		hr = SyncAttachments();
		if (hr != hrSuccess)
			goto exit;
	}

	// Property change of a new item
	if (fNew && this->GetMsgStore()->IsSpooler() == TRUE) {

		ECAllocateBuffer(CbNewSPropTagArray(1), (void**)&lpPropTagArray);
		lpPropTagArray->cValues = 1;
		lpPropTagArray->aulPropTag[0] = PR_MESSAGE_FLAGS;

		hr = ECMAPIProp::GetProps(lpPropTagArray, 0, &cValues, &lpsPropMessageFlags);
		if(hr != hrSuccess)
			goto exit;

		lpsPropMessageFlags->ulPropTag = PR_MESSAGE_FLAGS;
		lpsPropMessageFlags->Value.l &= ~(MSGFLAG_READ|MSGFLAG_UNSENT);
		lpsPropMessageFlags->Value.l |= MSGFLAG_UNMODIFIED;

		hr = SetProps(1, lpsPropMessageFlags, NULL);
		if(hr != hrSuccess)
			goto exit;
	}

	// don't re-sync bodies that are returned from server
	assert(!m_bInhibitSync);
	m_bInhibitSync = TRUE;

	hr = ECMAPIProp::SaveChanges(ulFlags);

	m_bInhibitSync = FALSE;
	m_bExplicitSubjectPrefix = FALSE;

	if(hr != hrSuccess)
		goto exit;

	// resync recip and attachment table, because of hierarchy id's, only on actual saved object
	if (m_sMapiObject && m_bEmbedded == false) {
		if (lpRecips) {
			hr = UpdateTable(lpRecips, MAPI_MAILUSER, PR_ROWID);
			if(hr != hrSuccess)
				goto exit;

			hr = UpdateTable(lpRecips, MAPI_DISTLIST, PR_ROWID);
			if(hr != hrSuccess)
				goto exit;
		}
		if (lpAttachments) {
			hr = UpdateTable(lpAttachments, MAPI_ATTACH, PR_ATTACH_NUM);
			if(hr != hrSuccess)
				goto exit;
		}
	}

exit:
	if (lpPropTagArray)
		ECFreeBuffer(lpPropTagArray);

	if (lpsPropMessageFlags)
		ECFreeBuffer(lpsPropMessageFlags);
	return hr;
}

/**
 * Sync PR_SUBJECT, PR_SUBJECT_PREFIX
 */
HRESULT ECMessage::SyncSubject()
{
    HRESULT			hr = hrSuccess;
	HRESULT			hr1 = hrSuccess;
	HRESULT			hr2 = hrSuccess;
	BOOL			bDirtySubject = FALSE;
	BOOL			bDirtySubjectPrefix = FALSE;
	LPSPropValue	lpPropArray = NULL;
	ULONG			cValues = 0;
	WCHAR*			lpszColon = NULL;
	WCHAR*			lpszEnd = NULL;
	int				sizePrefix1 = 0;

	SizedSPropTagArray(2, sPropSubjects) = {2, { PR_SUBJECT_W, PR_SUBJECT_PREFIX_W} };

	hr1 = IsPropDirty(CHANGE_PROP_TYPE(PR_SUBJECT, PT_UNSPECIFIED), &bDirtySubject);
	hr2 = IsPropDirty(CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED), &bDirtySubjectPrefix);

	// if both not present or not dirty
	if( (hr1 != hrSuccess && hr2 != hrSuccess) || (hr1 == hr2 && bDirtySubject == FALSE && bDirtySubjectPrefix == FALSE) )
		goto exit;

	// If subject is deleted but the prefix is not, delete it
	if(hr1 != hrSuccess && hr2 == hrSuccess)
	{
		hr = HrDeleteRealProp(CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED), FALSE);
		goto exit;
	}

	// Check if subject and prefix in sync

	hr = ECMAPIProp::GetProps(sPropSubjects, 0, &cValues, &lpPropArray);
	if(HR_FAILED(hr))
		goto exit;

	if(lpPropArray[0].ulPropTag == PR_SUBJECT_W)
		lpszColon = wcschr(lpPropArray[0].Value.lpszW, L':');

	if(lpszColon == NULL) {
		//Set emtpy PR_SUBJECT_PREFIX
		lpPropArray[1].ulPropTag = PR_SUBJECT_PREFIX_W;
		lpPropArray[1].Value.lpszW = const_cast<wchar_t *>(L"");

		hr = HrSetRealProp(&lpPropArray[1]);
		if(hr != hrSuccess)
			goto exit;

	} else {
		sizePrefix1 = lpszColon - lpPropArray[0].Value.lpszW + 1;

		// synchronized PR_SUBJECT_PREFIX
		lpPropArray[1].ulPropTag = PR_SUBJECT_PREFIX_W;	// If PROP_TYPE(lpPropArray[1].ulPropTag) == PT_ERROR, we lose that info here.

		if (sizePrefix1 > 1 && sizePrefix1 <= 4)
		{
			if (lpPropArray[0].Value.lpszW[sizePrefix1] == L' ')
				lpPropArray[0].Value.lpszW[sizePrefix1+1] = 0; // with space "fwd: "
			else
				lpPropArray[0].Value.lpszW[sizePrefix1] = 0; // "fwd:"

			assert(lpPropArray[0].Value.lpszW[sizePrefix1-1] == L':');
			lpPropArray[1].Value.lpszW = lpPropArray[0].Value.lpszW;

			wcstol(lpPropArray[1].Value.lpszW, &lpszEnd, 10);
			if (lpszEnd == lpszColon)
				lpPropArray[1].Value.lpszW = const_cast<wchar_t *>(L""); // skip a numeric prefix
		} else
			lpPropArray[1].Value.lpszW = const_cast<wchar_t *>(L""); // emtpy PR_SUBJECT_PREFIX

		hr = HrSetRealProp(&lpPropArray[1]);
		if (hr != hrSuccess)
			goto exit;

		// PR_SUBJECT_PREFIX and PR_SUBJECT are synchronized
	}

exit:
	if(lpPropArray)
		ECFreeBuffer(lpPropArray);

	return hr;
}

// Override IMAPIProp::SetProps
HRESULT ECMessage::SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray *lppProblems)
{
	HRESULT hr = hrSuccess;
	LPSPropValue pvalSubject;
	LPSPropValue pvalSubjectPrefix;
	LPSPropValue pvalRtf;
	LPSPropValue pvalHtml;
	LPSPropValue pvalBody;

	const BOOL bInhibitSyncOld = m_bInhibitSync;
	m_bInhibitSync = TRUE; // We want to override the logic in ECMessage::HrSetRealProp.

	// Send to IMAPIProp first
	hr = ECMAPIProp::SetProps(cValues, lpPropArray, lppProblems);
	if (hr != hrSuccess)
		goto exit;

	m_bInhibitSync = bInhibitSyncOld;

	/* We only sync the subject (like a PST does) in the following conditions:
	 * 1) PR_SUBJECT is modified, and PR_SUBJECT_PREFIX was not set
	 * 2) PR_SUBJECT is modified, and PR_SUBJECT_PREFIX was modified by a previous SyncSubject() call
	 * If the caller ever does a SetProps on the PR_SUBJECT_PREFIX itself, we must never touch it ourselves again, until SaveChanges().
	 */
	pvalSubject = PpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_SUBJECT, PT_UNSPECIFIED));
	pvalSubjectPrefix = PpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED));
	if (pvalSubjectPrefix)
		m_bExplicitSubjectPrefix = TRUE;
	if (pvalSubject && m_bExplicitSubjectPrefix == FALSE)
		SyncSubject();

	// Now, sync RTF
	pvalRtf = PpropFindProp(lpPropArray, cValues, PR_RTF_COMPRESSED);
	pvalHtml = PpropFindProp(lpPropArray, cValues, PROP_TAG(PT_UNSPECIFIED, PROP_ID(PR_BODY_HTML)) );
	pvalBody = PpropFindProp(lpPropArray, cValues, PROP_TAG(PT_UNSPECIFIED, PROP_ID(PR_BODY)) );

	// IF the user sets both the body and the RTF, assume RTF overrides
	if (pvalRtf) {
		m_ulBodyType = bodyTypeUnknown; // Make sure GetBodyType doesn't use the cached value
		GetBodyType(&m_ulBodyType);
		SyncRtf();
	} else if (pvalHtml) {
		m_ulBodyType = bodyTypeHTML;
		SyncHtmlToPlain();
		HrDeleteRealProp(PR_RTF_COMPRESSED, FALSE);
	} else if(pvalBody) {
		m_ulBodyType = bodyTypePlain;
		HrDeleteRealProp(PR_RTF_COMPRESSED, FALSE);
		HrDeleteRealProp(PR_HTML, FALSE);
	}

exit:
	m_bInhibitSync = bInhibitSyncOld;
	return hr;
}

HRESULT ECMessage::DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray *lppProblems)
{
	HRESULT hr;
	SPropTagArray sSubjectPrefix = {1, { CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED) } };

	// Send to IMAPIProp first
	hr = ECMAPIProp::DeleteProps(lpPropTagArray, lppProblems);
	if (FAILED(hr))
		return hr;

	// If the PR_SUBJECT is removed and we generated the prefix, we need to remove that property too.
	if (m_bExplicitSubjectPrefix == FALSE && Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_SUBJECT, PT_UNSPECIFIED)) >= 0)
		ECMAPIProp::DeleteProps(&sSubjectPrefix, NULL);

	// If an explicit prefix was set and now removed, we must sync it again on the next SetProps of the subject
	if (m_bExplicitSubjectPrefix == TRUE && Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED)) >= 0)
		m_bExplicitSubjectPrefix = FALSE;

	return hrSuccess;
}

HRESULT ECMessage::TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType)
{
	HRESULT hr = hrSuccess;
	ECMsgStore *lpMsgStore = (ECMsgStore *)lpProvider;

	if (lpsPropValSrc->ulPropTag != PR_SOURCE_KEY)
		return MAPI_E_NOT_FOUND;
	if ((lpMsgStore->m_ulProfileFlags & EC_PROFILE_FLAGS_TRUNCATE_SOURCEKEY) &&
	    lpsPropValSrc->Value.bin->__size > 22) {
		lpsPropValSrc->Value.bin->__size = 22;
		lpsPropValSrc->Value.bin->__ptr[lpsPropValSrc->Value.bin->__size-1] |= 0x80; // Set top bit
		hr = CopySOAPPropValToMAPIPropVal(lpsPropValDst, lpsPropValSrc, lpBase);
	} else {
		hr = MAPI_E_NOT_FOUND;
	}
	return hr;
}

HRESULT	ECMessage::GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT hr = hrSuccess;
	unsigned int ulSize = 0;
	LPBYTE	lpData = NULL;
	ECMessage *lpMessage = (ECMessage *)lpParam;

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_RTF_IN_SYNC):
		lpsPropValue->ulPropTag = PR_RTF_IN_SYNC;
		lpsPropValue->Value.ul = TRUE; // Always in sync because we sync internally
		break;
	case PROP_ID(PR_HASATTACH):
		lpsPropValue->ulPropTag = PR_HASATTACH;
		lpsPropValue->Value.b = lpMessage->HasAttachment();
		break;
	case PROP_ID(PR_ASSOCIATED):
		hr = lpMessage->HrGetRealProp(PR_MESSAGE_FLAGS, ulFlags, lpBase, lpsPropValue);
		if(hr != hrSuccess) {
			hr = hrSuccess;
			lpsPropValue->ulPropTag = PR_ASSOCIATED;
			lpsPropValue->Value.b = false;
		} else {
			lpsPropValue->ulPropTag = PR_ASSOCIATED;
			lpsPropValue->Value.b = !!(lpsPropValue->Value.ul & MSGFLAG_ASSOCIATED);
		}
		break;
	case PROP_ID(PR_MESSAGE_FLAGS):
	{
		hr = lpMessage->HrGetRealProp(PR_MESSAGE_FLAGS, ulFlags, lpBase, lpsPropValue);
		if(hr != hrSuccess) {
			hr = hrSuccess;
			lpsPropValue->ulPropTag = PR_MESSAGE_FLAGS;
			lpsPropValue->Value.ul = MSGFLAG_READ;
		}
		// Force MSGFLAG_HASATTACH to the correct value
		lpsPropValue->Value.ul = (lpsPropValue->Value.ul & ~MSGFLAG_HASATTACH) | (lpMessage->HasAttachment() ? MSGFLAG_HASATTACH : 0);
		break;
	}
	case PROP_ID(PR_NORMALIZED_SUBJECT):
		hr = lpMessage->HrGetRealProp(CHANGE_PROP_TYPE(PR_SUBJECT, PROP_TYPE(ulPropTag)), ulFlags, lpBase, lpsPropValue);
		if (hr != hrSuccess) {
			// change PR_SUBJECT in PR_NORMALIZED_SUBJECT
			lpsPropValue->ulPropTag = CHANGE_PROP_TYPE(PR_NORMALIZED_SUBJECT, PT_ERROR);
			break;
		}

		if (PROP_TYPE(ulPropTag) == PT_UNICODE) {
			lpsPropValue->ulPropTag = PR_NORMALIZED_SUBJECT_W;

			WCHAR *lpszColon = wcschr(lpsPropValue->Value.lpszW, ':');
			if (lpszColon && (lpszColon - lpsPropValue->Value.lpszW) > 1 && (lpszColon - lpsPropValue->Value.lpszW) < 4) {
				WCHAR *c = lpsPropValue->Value.lpszW;
				while (c < lpszColon && iswdigit(*c))
					++c; // test for all digits prefix
				if (c != lpszColon) {
					++lpszColon;
					if (*lpszColon == ' ')
						++lpszColon;
					lpsPropValue->Value.lpszW = lpszColon; // set new subject string
				}
			}
		} else {
			lpsPropValue->ulPropTag = PR_NORMALIZED_SUBJECT_A;

			char *lpszColon = strchr(lpsPropValue->Value.lpszA, ':');
			if (lpszColon && (lpszColon - lpsPropValue->Value.lpszA) > 1 && (lpszColon - lpsPropValue->Value.lpszA) < 4) {
				char *c = lpsPropValue->Value.lpszA;
				while (c < lpszColon && isdigit(*c))
					++c; // test for all digits prefix
				if (c != lpszColon) {
					++lpszColon;
					if (*lpszColon == ' ')
						++lpszColon;
					lpsPropValue->Value.lpszA = lpszColon; // set new subject string
				}
			}
		}
		break;
	case PROP_ID(PR_PARENT_ENTRYID):

		if(!lpMessage->m_lpParentID)
			hr = lpMessage->HrGetRealProp(PR_PARENT_ENTRYID, ulFlags, lpBase, lpsPropValue);
		else{
			lpsPropValue->ulPropTag = PR_PARENT_ENTRYID;
			lpsPropValue->Value.bin.cb = lpMessage->m_cbParentID;

			ECAllocateMore(lpsPropValue->Value.bin.cb, lpBase, (LPVOID *)&lpsPropValue->Value.bin.lpb);
			memcpy(lpsPropValue->Value.bin.lpb, lpMessage->m_lpParentID, lpsPropValue->Value.bin.cb);
		}
		break;
	case PROP_ID(PR_MESSAGE_SIZE):
		lpsPropValue->ulPropTag = PR_MESSAGE_SIZE;
		if(lpMessage->m_lpEntryId == NULL) //new message
			lpsPropValue->Value.l = 1024;
		else
			hr = lpMessage->HrGetRealProp(PR_MESSAGE_SIZE, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_DISPLAY_TO):
	case PROP_ID(PR_DISPLAY_CC):
	case PROP_ID(PR_DISPLAY_BCC):
		if((lpMessage->m_bRecipsDirty && lpMessage->SyncRecips() != erSuccess) || lpMessage->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue) != erSuccess) {
			lpsPropValue->ulPropTag = ulPropTag;
			if(PROP_TYPE(ulPropTag) == PT_UNICODE)
				lpsPropValue->Value.lpszW = const_cast<wchar_t *>(L"");
			else
				lpsPropValue->Value.lpszA = const_cast<char *>("");
		}
		break;

	case PROP_ID(PR_ACCESS):
		if(lpMessage->HrGetRealProp(PR_ACCESS, ulFlags, lpBase, lpsPropValue) != hrSuccess)
		{
			lpsPropValue->ulPropTag = PR_ACCESS;
			lpsPropValue->Value.l = MAPI_ACCESS_READ | MAPI_ACCESS_MODIFY | MAPI_ACCESS_DELETE;
		}
		break;
	case PROP_ID(PR_MESSAGE_ATTACHMENTS):
		lpsPropValue->ulPropTag = PR_MESSAGE_ATTACHMENTS;
		lpsPropValue->Value.x = 1;
		break;
	case PROP_ID(PR_MESSAGE_RECIPIENTS):
		lpsPropValue->ulPropTag = PR_MESSAGE_RECIPIENTS;
		lpsPropValue->Value.x = 1;
		break;
	case PROP_ID(PR_EC_BODY_FILTERED): {
		// does it already exist? (eg inserted by dagent/gateway)
		hr = lpMessage->GetSyncedBodyProp(PR_EC_BODY_FILTERED, ulFlags, lpBase, lpsPropValue);
		if (hr == hrSuccess) // yes, then use that
			break;

		// else generate it on the fly
		LPSPropValue lpsTempProp;
		hr = MAPIAllocateBuffer(sizeof(SPropValue), reinterpret_cast<void **>(&lpsTempProp));
		if (hr != hrSuccess)
			break;

		hr = lpMessage->GetSyncedBodyProp(PR_HTML, ulFlags, lpsTempProp, lpsTempProp);
		if (hr != hrSuccess) {
			ECFreeBuffer(lpsTempProp);
			hr = MAPI_E_NOT_FOUND;
			break;
		}

		std::string in(lpsTempProp->Value.lpszA);
		std::string result;
		std::vector<std::string> errors;
		bool rc = rosie_clean_html(in, &result, &errors);

		// FIXME emit error somewhere somehow
		if (rc) {
			ULONG ulSize = result.size();

			hr = ECAllocateMore(ulSize + 1, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.lpszA));
			if (hr == hrSuccess) {
				memcpy(lpsPropValue->Value.lpszA, result.c_str(), ulSize);
				lpsPropValue->Value.lpszA[ulSize] = '\0';
				// FIXME store in database if that is what the SysOp wants
			} else {
				ulSize = 0;
			}
			lpsPropValue->Value.bin.cb = ulSize;
		}
		ECFreeBuffer(lpsTempProp);

		if (rc == 0 || hr != hrSuccess) {
			hr = MAPI_E_NOT_FOUND;
			break;
		}
		break;
	}
	case PROP_ID(PR_BODY):
	case PROP_ID(PR_RTF_COMPRESSED):
	case PROP_ID(PR_HTML):
		hr = lpMessage->GetSyncedBodyProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
		if (hr != hrSuccess)
			break;

		if (ulPropTag == PR_BODY_HTML) {
			// Workaround for support html in outlook 2000/xp
			if (lpsPropValue->ulPropTag != PR_HTML){
				hr = MAPI_E_NOT_FOUND;
				break;
			}

			lpsPropValue->ulPropTag = PR_BODY_HTML;
			ulSize = lpsPropValue->Value.bin.cb;
			lpData = lpsPropValue->Value.bin.lpb;

			hr = ECAllocateMore(ulSize + 1, lpBase, (void**)&lpsPropValue->Value.lpszA);
			if(hr != hrSuccess)
				break;

		if(ulSize>0 && lpData){
				memcpy(lpsPropValue->Value.lpszA, lpData, ulSize);
			}else
				ulSize = 0;

			lpsPropValue->Value.lpszA[ulSize] = 0;
		}
		break;
	case PROP_ID(PR_SOURCE_KEY): {
		std::string strServerGUID;
		std::string strID;
		std::string strSourceKey;

		if(ECMAPIProp::DefaultMAPIGetProp(PR_SOURCE_KEY, lpProvider, ulFlags, lpsPropValue, lpParam, lpBase) == hrSuccess)
			return hr;

		// The server did not supply a PR_SOURCE_KEY, generate one ourselves.

		strServerGUID.assign((char*)&lpMessage->GetMsgStore()->GetStoreGuid(), sizeof(GUID));

		if(lpMessage->m_sMapiObject)
			strID.assign((char *)&lpMessage->m_sMapiObject->ulObjId, sizeof(lpMessage->m_sMapiObject->ulObjId));
							
		// Resize so it trails 6 null bytes
		strID.resize(6,0);

		strSourceKey = strServerGUID + strID;

		hr = MAPIAllocateMore(strSourceKey.size(), lpBase, (void **)&lpsPropValue->Value.bin.lpb);
		if(hr != hrSuccess)
			return hr;

		lpsPropValue->ulPropTag = PR_SOURCE_KEY;
		lpsPropValue->Value.bin.cb = strSourceKey.size();
		memcpy(lpsPropValue->Value.bin.lpb, strSourceKey.c_str(), strSourceKey.size());

		break;
	}
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}
	return hr;
}

HRESULT ECMessage::SetPropHandler(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam)
{
	ECMessage *lpMessage = (ECMessage *)lpParam;
	HRESULT hr = hrSuccess;

	switch(ulPropTag) {
	case PR_HTML:
		hr = lpMessage->HrSetRealProp(lpsPropValue);
		break;
	case PR_BODY_HTML: {
		// Set PR_BODY_HTML to PR_HTML
		SPropValue copy;
		copy.ulPropTag = PR_HTML;
		auto lpData = copy.Value.lpszA;

		if(lpData) {
			copy.Value.bin.cb = strlen(lpData);
			copy.Value.bin.lpb = (LPBYTE)lpData;
		}
		else {
			copy.Value.bin.cb = 0;
		}

		hr = lpMessage->HrSetRealProp(&copy);
		break;
	}
	case PR_MESSAGE_FLAGS:
		if (lpMessage->m_sMapiObject == NULL || lpMessage->m_sMapiObject->ulObjId == 0) {
			// filter any invalid flags
			SPropValue copy = *lpsPropValue;
			copy.Value.l &= 0x03FF;
			if (lpMessage->HasAttachment())
				copy.Value.l |= MSGFLAG_HASATTACH;
			hr = lpMessage->HrSetRealProp(&copy);
		}
		break;
	case PR_SOURCE_KEY:
		hr = ECMAPIProp::SetPropHandler(ulPropTag, lpProvider, lpsPropValue, lpParam);
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}
	return hr;
}

// Use the support object to do the copying
HRESULT ECMessage::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems)
{
	HRESULT hr = hrSuccess;
	IECUnknown *lpECUnknown = NULL;
	LPSPropValue lpECObject = NULL;
	ECMAPIProp *lpECMAPIProp = NULL;
	ECMAPIProp *lpDestTop = NULL;
	ECMAPIProp *lpSourceTop = NULL;
	GUID sDestServerGuid = {0};
	GUID sSourceServerGuid = {0};

	if(lpDestObj == NULL) {
	    hr = MAPI_E_INVALID_PARAMETER;
	    goto exit;
    }

	// Wrap mapi object to kopano object
	if (HrGetOneProp((LPMAPIPROP)lpDestObj, PR_EC_OBJECT, &lpECObject) == hrSuccess) {
		lpECUnknown = (IECUnknown*)lpECObject->Value.lpszA;
		lpECUnknown->AddRef();

		MAPIFreeBuffer(lpECObject);
	}

	// Deny copying within the same object. This is not allowed in exchange either and is required to deny
	// creating large recursive objects.
	if(lpECUnknown && lpECUnknown->QueryInterface(IID_ECMAPIProp, (void **)&lpECMAPIProp) == hrSuccess) {
		// Find the top-level objects for both source and destination objects
		lpDestTop = lpECMAPIProp->m_lpRoot;
		lpSourceTop = this->m_lpRoot;

		// destination may not be a child of the source, but source can be a child of destination
		if (!this->IsChildOf(lpDestTop)) {
			// ICS expects the entryids to be equal. So check if the objects reside on
			// the same server as well.
			hr = lpDestTop->GetMsgStore()->lpTransport->GetServerGUID(&sDestServerGuid);
			if (hr != hrSuccess)
				goto exit;

			hr = lpSourceTop->GetMsgStore()->lpTransport->GetServerGUID(&sSourceServerGuid);
			if (hr != hrSuccess)
				goto exit;

			if(lpDestTop->m_lpEntryId && lpSourceTop->m_lpEntryId &&
			   lpDestTop->m_cbEntryId == lpSourceTop->m_cbEntryId &&
			   memcmp(lpDestTop->m_lpEntryId, lpSourceTop->m_lpEntryId, lpDestTop->m_cbEntryId) == 0 &&
			   sDestServerGuid == sSourceServerGuid) {
				// Source and destination are the same on-disk objects (entryids are equal)

				hr = MAPI_E_NO_ACCESS;
				goto exit;
			}
		}

		lpECMAPIProp->Release();
		lpECMAPIProp = NULL;
	}

	hr = Util::DoCopyTo(&IID_IMessage, &this->m_xMessage, ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);

exit:
	if(lpECMAPIProp)
		lpECMAPIProp->Release();

	if (lpECUnknown)
		lpECUnknown->Release();

	return hr;
}

// We override HrLoadProps to setup PR_BODY and PR_RTF_COMPRESSED in the initial message
// Normally, this should never be needed, as messages should store both the PR_BODY as the PR_RTF_COMPRESSED
// when saving.
HRESULT ECMessage::HrLoadProps()
{
	HRESULT hr = hrSuccess;
	LPSPropValue lpsBodyProps = NULL;
	SizedSPropTagArray(3, sPropBodyTags) = { 3, { PR_BODY_W, PR_RTF_COMPRESSED, PR_HTML } };
	ULONG cValues = 0;
	BOOL fBodyOK = FALSE;
	BOOL fRTFOK = FALSE;
	BOOL fHTMLOK = FALSE;

	m_bInhibitSync = TRUE; // We don't want the logic in ECMessage::HrSetRealProp to kick in yet.

	hr = ECMAPIProp::HrLoadProps();

	m_bInhibitSync = FALSE;

	if (hr != hrSuccess)
		goto exit;

	/*
	 * Now we're going to determine what the best body is.
	 * This works as follows, the db will always contain the best body, but possibly
	 * more. The plaintext body should also always be available.
	 *
	 * So if we only get a plaintext body, plaintext was the best body.
	 * If we get HTML but not RTF, HTML is the best body.
	 * If we get RTF, we'll check the RTF content to determine what the best body was.
	 *
	 * We won't generate any body except the best body if it wasn't returned by the
	 * server, which is actually wrong.
	 */
	hr = ECMAPIProp::GetProps(sPropBodyTags, 0, &cValues, &lpsBodyProps);
	if (HR_FAILED(hr))
		goto exit;

	hr = hrSuccess;

	if (lpsBodyProps[0].ulPropTag == PR_BODY_W || (lpsBodyProps[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_BODY)) && lpsBodyProps[0].Value.err == MAPI_E_NOT_ENOUGH_MEMORY))
		fBodyOK = TRUE;

	if (lpsBodyProps[1].ulPropTag == PR_RTF_COMPRESSED || (lpsBodyProps[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_RTF_COMPRESSED)) && lpsBodyProps[1].Value.err == MAPI_E_NOT_ENOUGH_MEMORY))
		fRTFOK = TRUE;

	if (lpsBodyProps[2].ulPropTag == PR_HTML || (lpsBodyProps[2].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_HTML)) && lpsBodyProps[2].Value.err == MAPI_E_NOT_ENOUGH_MEMORY))
		fHTMLOK = TRUE;

	if (fRTFOK) {
		HRESULT hrTmp = hrSuccess;

		hrTmp = GetBodyType(&m_ulBodyType);
		if (FAILED(hrTmp)) {
			// eg. this fails then RTF property is present but empty
			TRACE_MAPI(TRACE_WARNING, "GetBestBody", "Unable to determine body type based on RTF data, hr=0x%08x", hrTmp);
		} else if ((m_ulBodyType == bodyTypePlain && !fBodyOK) ||
		    (m_ulBodyType == bodyTypeHTML && !fHTMLOK)) {
			hr = SyncRtf();
			if (hr != hrSuccess)
				goto exit;
		}
	}

	if (m_ulBodyType == bodyTypeUnknown) {
		// We get here if there was no RTF data or when determining the body type based
		// on that data failed.
		if (fHTMLOK)
			m_ulBodyType = bodyTypeHTML;

		else if (fBodyOK)
			m_ulBodyType = bodyTypePlain;
	}

exit:
	if(lpsBodyProps)
		ECFreeBuffer(lpsBodyProps);

	return hr;
}

HRESULT ECMessage::HrSetRealProp(SPropValue *lpsPropValue)
{
	HRESULT hr;

	hr = ECMAPIProp::HrSetRealProp(lpsPropValue);
	if(hr != hrSuccess)
		return hr;

	// If we're in the middle of syncing bodies, we don't want any more logic to kick in.
	if (m_bInhibitSync)
		return hrSuccess;

	if (lpsPropValue->ulPropTag == PR_RTF_COMPRESSED) {
		m_ulBodyType = bodyTypeUnknown; // Make sure GetBodyType doesn't use the cached value
		GetBodyType(&m_ulBodyType);
		SyncRtf();
	} else if (lpsPropValue->ulPropTag == PR_HTML) {
		m_ulBodyType = bodyTypeHTML;
		SyncHtmlToPlain();
		HrDeleteRealProp(PR_RTF_COMPRESSED, FALSE);
	} else if (lpsPropValue->ulPropTag == PR_BODY_W || lpsPropValue->ulPropTag == PR_BODY_A) {
		m_ulBodyType = bodyTypePlain;
		HrDeleteRealProp(PR_RTF_COMPRESSED, FALSE);
		HrDeleteRealProp(PR_HTML, FALSE);
	}
	return hrSuccess;
}

struct findobject_if {
    unsigned int m_ulUniqueId;
    unsigned int m_ulObjType;

    findobject_if(unsigned int ulObjType, unsigned int ulUniqueId) : m_ulUniqueId(ulUniqueId), m_ulObjType(ulObjType) {}

    bool operator()(const MAPIOBJECT *entry)
    {
        return entry->ulUniqueId == m_ulUniqueId && entry->ulObjType == m_ulObjType;
    }
};

// Copies the server object IDs from lpSrc into lpDest by matching the correct object type
// and unique ID for each object.
static HRESULT HrCopyObjIDs(MAPIOBJECT *lpDest, const MAPIOBJECT *lpSrc)
{
    HRESULT hr;

    lpDest->ulObjId = lpSrc->ulObjId;

	for (const auto &src : *lpSrc->lstChildren) {
		auto iterDest = lpDest->lstChildren->find(src);
		if (iterDest != lpDest->lstChildren->cend()) {
			hr = HrCopyObjIDs(*iterDest, src);
            if(hr != hrSuccess)
                return hr;
        }
    }
    return hrSuccess;
}

HRESULT ECMessage::HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject) {
	HRESULT hr = hrSuccess;
	IMAPITable *lpTable = NULL;
	ECMapiObjects::const_iterator iterSObj;
	SPropValue sKeyProp;
	LPSPropValue lpProps = NULL;
	ULONG ulProps = 0;
	LPSPropValue lpPropID = NULL;
	LPSPropValue lpPropObjType = NULL;
	ULONG i;
	scoped_rlock lock(m_hMutexMAPIObject);

	if (lpsMapiObject->ulObjType != MAPI_ATTACH) {
		// can only save attachments as child objects
		// (recipients are saved through SaveRecips() from SaveChanges() on this object)
		hr = MAPI_E_INVALID_OBJECT;
		goto exit;
	}

	if(this->lpAttachments == NULL) {
		hr = this->GetAttachmentTable(fMapiUnicode, &lpTable);

		if(hr != hrSuccess)
			goto exit;

		lpTable->Release();
	}

	if(this->lpAttachments == NULL) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	if (!m_sMapiObject) {
		// when does this happen? .. just a simple precaution for now
		assert(m_sMapiObject != NULL);
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// Replace the attachment in the object hierarchy with this one, but preserve server object id. This is needed
	// if the entire object has been saved to the server in the mean time.
	iterSObj = m_sMapiObject->lstChildren->find(lpsMapiObject);
	if (iterSObj != m_sMapiObject->lstChildren->cend()) {
		// Preserve server IDs
		hr = HrCopyObjIDs(lpsMapiObject, (*iterSObj));
		if(hr != hrSuccess)
			goto exit;

		// Remove item
		FreeMapiObject(*iterSObj);
		m_sMapiObject->lstChildren->erase(iterSObj);
	}

	m_sMapiObject->lstChildren->insert(new MAPIOBJECT(lpsMapiObject));

	// Update the attachment table. The attachment table contains all properties of the attachments
	ulProps = lpsMapiObject->lstProperties->size();

	// +2 for maybe missing PR_ATTACH_NUM and PR_OBJECT_TYPE properties
	ECAllocateBuffer(sizeof(SPropValue)*(ulProps+2), (void**)&lpProps);

	lpPropID = NULL;
	i = 0;
	for (const auto &pv : *lpsMapiObject->lstProperties) {
		pv.CopyToByRef(&lpProps[i]);
		if (lpProps[i].ulPropTag == PR_ATTACH_NUM) {
			lpPropID = &lpProps[i];
		} else if(lpProps[i].ulPropTag == PR_OBJECT_TYPE) {
			lpPropObjType = &lpProps[i];
		} else if (PROP_ID(lpProps[i].ulPropTag) == PROP_ID(PR_ATTACH_DATA_OBJ)) {
			lpProps[i].ulPropTag = CHANGE_PROP_TYPE(lpProps[i].ulPropTag, PT_ERROR);
			lpProps[i].Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
		} else if (PROP_TYPE(lpProps[i].ulPropTag) == PT_BINARY && lpProps[i].Value.bin.cb > MAX_TABLE_PROPSIZE) {
			lpProps[i].ulPropTag = CHANGE_PROP_TYPE(lpProps[i].ulPropTag, PT_ERROR);
			lpProps[i].Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
		}
		++i;
	}

	if (lpPropID == NULL) {
		++ulProps;
		lpPropID = &lpProps[i++];
	}

	if (lpPropObjType == NULL) {
		++ulProps;
		lpPropObjType = &lpProps[i++];
	}

	lpPropObjType->ulPropTag = PR_OBJECT_TYPE;
	lpPropObjType->Value.ul = MAPI_ATTACH;

	lpPropID->ulPropTag = PR_ATTACH_NUM;
	lpPropID->Value.ul = lpsMapiObject->ulUniqueId;

	sKeyProp.ulPropTag = PR_EC_HIERARCHYID;
	sKeyProp.Value.ul = lpsMapiObject->ulObjId;

	hr = lpAttachments->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &sKeyProp, lpProps, ulProps);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (lpProps)
		ECFreeBuffer(lpProps);
	return hr;
}

HRESULT ECMessage::GetBodyType(eBodyType *lpulBodyType)
{
	HRESULT		hr = hrSuccess;
	LPSTREAM	lpRTFCompressedStream = NULL;
	LPSTREAM	lpRTFUncompressedStream = NULL;
	char		szRtfBuf[64] = {0};
	ULONG		cbRtfBuf = 0;

	if (m_ulBodyType == bodyTypeUnknown) {
		hr = OpenProperty(PR_RTF_COMPRESSED, &IID_IStream, 0, 0, (LPUNKNOWN *)&lpRTFCompressedStream);
		if (hr != hrSuccess)
			goto exit;

		hr = WrapCompressedRTFStream(lpRTFCompressedStream, 0, &lpRTFUncompressedStream);
		if (hr != hrSuccess)
			goto exit;

		hr = lpRTFUncompressedStream->Read(szRtfBuf, sizeof(szRtfBuf), &cbRtfBuf);
		if (hr != hrSuccess)
			goto exit;
		if (isrtftext(szRtfBuf, cbRtfBuf))
			m_ulBodyType = bodyTypePlain;
		else if (isrtfhtml(szRtfBuf, cbRtfBuf))
			m_ulBodyType = bodyTypeHTML;
		else
			m_ulBodyType = bodyTypeRTF;
	}

	*lpulBodyType = m_ulBodyType;

exit:
	if (lpRTFUncompressedStream)
		lpRTFUncompressedStream->Release();

	if (lpRTFCompressedStream)
		lpRTFCompressedStream->Release();

	return hr;
}

HRESULT ECMessage::GetRtfData(std::string *lpstrRtfData)
{
	HRESULT hr;
	StreamPtr ptrRtfCompressedStream;
	StreamPtr ptrRtfUncompressedStream;
	char lpBuf[4096];
	std::string strRtfData;

	hr = OpenProperty(PR_RTF_COMPRESSED, &IID_IStream, 0, 0, &ptrRtfCompressedStream);
	if (hr != hrSuccess)
		return hr;

	// Read the RTF stream
	hr = WrapCompressedRTFStream(ptrRtfCompressedStream, 0, &ptrRtfUncompressedStream);
	if(hr != hrSuccess)
	{
		mapi_object_ptr<ECMemStream> ptrEmptyMemStream;

		// Broken RTF, fallback on empty stream
		hr = ECMemStream::Create(NULL, 0, 0, NULL, NULL, NULL, &ptrEmptyMemStream);
		if (hr != hrSuccess)
			return hr;

		hr = ptrEmptyMemStream->QueryInterface(IID_IStream, (void**)&ptrRtfUncompressedStream);
		if (hr != hrSuccess)
			return hr;
	}

	// Read the entire uncompressed RTF stream into strRTF
	while (1) {
		ULONG ulRead;

		hr = ptrRtfUncompressedStream->Read(lpBuf, 4096, &ulRead);
		if (hr != hrSuccess)
			return hr;

		if (ulRead == 0)
			break;

		strRtfData.append(lpBuf, ulRead);
	}

	lpstrRtfData->swap(strRtfData);
	return hrSuccess;
}

HRESULT ECMessage::GetCodePage(unsigned int *lpulCodePage)
{
	HRESULT hr;
	SPropValuePtr ptrPropValue;

	hr = ECAllocateBuffer(sizeof(SPropValue), &ptrPropValue);
	if (hr != hrSuccess)
		return hr;

	if (HrGetRealProp(PR_INTERNET_CPID, 0, ptrPropValue, ptrPropValue) == hrSuccess &&
	    ptrPropValue->ulPropTag == PR_INTERNET_CPID)
		*lpulCodePage = ptrPropValue->Value.ul;
	else
		*lpulCodePage = 0;

	return hrSuccess;
}

// Use the support object to do the copying
HRESULT ECMessage::CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems)
{
	return Util::DoCopyProps(&IID_IMessage, &this->m_xMessage, lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMessage, Message, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMessage, Message, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, GetLastError, (HRESULT, hError), (ULONG, ulFlags), (LPMAPIERROR *, lppMapiError))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, SaveChanges, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, GetProps, (LPSPropTagArray, lpPropTagArray), (ULONG, ulFlags), (ULONG *, lpcValues, LPSPropValue *, lppPropArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, GetPropList, (ULONG, ulFlags), (LPSPropTagArray *, lppPropTagArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, OpenProperty, (ULONG, ulPropTag), (LPCIID, lpiid), (ULONG, ulInterfaceOptions), (ULONG, ulFlags), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, SetProps, (ULONG, cValues), (LPSPropValue, lpPropArray), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, DeleteProps, (LPSPropTagArray, lpPropTagArray), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, CopyTo, (ULONG, ciidExclude), (LPCIID, rgiidExclude), (LPSPropTagArray, lpExcludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, CopyProps, (LPSPropTagArray, lpIncludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, GetNamesFromIDs, (LPSPropTagArray *, pptaga), (LPGUID, lpguid), (ULONG, ulFlags), (ULONG *, pcNames, LPMAPINAMEID **, pppNames))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, GetIDsFromNames, (ULONG, cNames), (LPMAPINAMEID *, ppNames), (ULONG, ulFlags), (LPSPropTagArray *, pptaga))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, GetAttachmentTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, OpenAttach, (ULONG, ulAttachmentNum), (LPCIID, lpInterface), (ULONG, ulFlags), (LPATTACH *, lppAttach))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, CreateAttach, (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulAttachmentNum), (LPATTACH *, lppAttach))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, DeleteAttach, (ULONG, ulAttachmentNum), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, GetRecipientTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, ModifyRecipients, (ULONG, ulFlags), (LPADRLIST, lpMods))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, SubmitMessage, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMessage, Message, SetReadFlag, (ULONG, ulFlags))
