/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include <new>
#include <string>
#include <utility>
#include <cstdint>
#include <kopano/platform.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/scope.hpp>
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
#include "WSUtil.h"
#include "ClientUtil.h"
#include "ECMemStream.h"
#include "HtmlToTextParser.h"
#include <kopano/charset/convert.h>

using namespace KC;

#define MAX_TABLE_PROPSIZE 8192

HRESULT ECMessageFactory::Create(ECMsgStore *lpMsgStore, BOOL fNew,
    BOOL fModify, ULONG ulFlags, BOOL bEmbedded, const ECMAPIProp *lpRoot,
    ECMessage **lpMessage) const
{
	return ECMessage::Create(lpMsgStore, fNew, fModify, ulFlags, bEmbedded, lpRoot, lpMessage);
}

ECMessage::ECMessage(ECMsgStore *lpMsgStore, BOOL is_new, BOOL modify,
    ULONG ulFlags, BOOL emb, const ECMAPIProp *lpRoot) :
	ECMAPIProp(lpMsgStore, MAPI_MESSAGE, modify, lpRoot, "IMessage"),
	fNew(is_new), m_bEmbedded(emb)
{
	ulObjFlags = ulFlags & MAPI_ASSOCIATED;
	// proptag, getprop, setprops, class, bRemovable, bHidden
	HrAddPropHandlers(PR_RTF_IN_SYNC, GetPropHandler, DefaultSetPropIgnore, this, true, false);
	HrAddPropHandlers(PR_HASATTACH, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_NORMALIZED_SUBJECT, GetPropHandler, DefaultSetPropIgnore, this, false, false);
	HrAddPropHandlers(PR_PARENT_ENTRYID, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_MESSAGE_SIZE, GetPropHandler, SetPropHandler, this, false, false);
	HrAddPropHandlers(PR_DISPLAY_TO, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_DISPLAY_CC, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_DISPLAY_BCC, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_ACCESS, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_MESSAGE_ATTACHMENTS, GetPropHandler, DefaultSetPropIgnore, this, false, false);
	HrAddPropHandlers(PR_MESSAGE_RECIPIENTS, GetPropHandler, DefaultSetPropIgnore, this, false, false);
	// Handlers for the various body types
	HrAddPropHandlers(PR_BODY, GetPropHandler, DefaultSetPropSetReal, this, true, false);
	HrAddPropHandlers(PR_RTF_COMPRESSED, GetPropHandler, DefaultSetPropSetReal, this, false, false);
	// Workaround for support html in outlook 2000/xp need SetPropHandler
	HrAddPropHandlers(PR_HTML, GetPropHandler, SetPropHandler, this, false, false);
	HrAddPropHandlers(PR_EC_BODY_FILTERED, GetPropHandler, SetPropHandler, this, false, false);

	// The property 0x10970003 is set by outlook when browsing in the 'unread mail' searchfolder. It is used to make sure
	// that a message that you just read is not removed directly from view. It is set for each message which should be in the view
	// even though it is 'read', and is removed when you leave the folder. When you try to export this property to a PST, you get
	// an access denied error. We therefore hide this property, ie you can GetProps/SetProps it and use it in a restriction, but
	// GetPropList will never list it (same as in a PST).
	HrAddPropHandlers(0x10970003, DefaultGetPropGetReal, DefaultSetPropSetReal, this, true, true);

	// Don't show the PR_EC_IMAP_ID, and mark it as computed and deletable. This makes sure that CopyTo() will not copy it to
	// the other message.
	HrAddPropHandlers(PR_EC_IMAP_ID, DefaultGetPropGetReal, DefaultSetPropComputed, this, true, true);

	// Make sure the MSGFLAG_HASATTACH flag gets added when needed.
	HrAddPropHandlers(PR_MESSAGE_FLAGS, GetPropHandler, SetPropHandler, this, false, false);

	// Make sure PR_SOURCE_KEY is available
	HrAddPropHandlers(PR_SOURCE_KEY, GetPropHandler, SetPropHandler, this, true, false);

	// IMAP complete email, removable and hidden. setprop ignore? use interface for single-instancing
	HrAddPropHandlers(PR_EC_IMAP_EMAIL, DefaultGetPropGetReal, DefaultSetPropSetReal, this, true, true);
	HrAddPropHandlers(PR_EC_IMAP_EMAIL_SIZE, DefaultGetPropGetReal, DefaultSetPropSetReal, this, true, true);
	HrAddPropHandlers(CHANGE_PROP_TYPE(PR_EC_IMAP_BODY, PT_UNICODE), DefaultGetPropGetReal, DefaultSetPropSetReal, this, true, true);
	HrAddPropHandlers(CHANGE_PROP_TYPE(PR_EC_IMAP_BODYSTRUCTURE, PT_UNICODE), DefaultGetPropGetReal, DefaultSetPropSetReal, this, true, true);
	HrAddPropHandlers(PR_ASSOCIATED, GetPropHandler, DefaultSetPropComputed, this, true, true);
}

HRESULT ECMessage::Create(ECMsgStore *lpMsgStore, BOOL fNew, BOOL fModify,
    ULONG ulFlags, BOOL bEmbedded, const ECMAPIProp *lpRoot,
    ECMessage **lppMessage)
{
	return alloc_wrap<ECMessage>(lpMsgStore, fNew, fModify, ulFlags,
	       bEmbedded, lpRoot).put(lppMessage);
}

HRESULT	ECMessage::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMessage, this);
	REGISTER_INTERFACE2(ECMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMessage, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	REGISTER_INTERFACE2(IECSingleInstance, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMessage::GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags,
    ULONG *lpcValues, SPropValue **lppPropArray)
{
	HRESULT			hr = hrSuccess;
	ULONG			cValues = 0;
	SPropArrayPtr	ptrPropArray;
	int lBodyIdx = 0, lRtfIdx = 0, lHtmlIdx = 0;

	if (lpPropTagArray) {
		lBodyIdx = Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_BODY_W, PT_UNSPECIFIED));
		lRtfIdx = Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_RTF_COMPRESSED, PT_UNSPECIFIED));
		lHtmlIdx = Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_HTML, PT_UNSPECIFIED));
	}
	if (!m_props_loaded && (!lpPropTagArray || lBodyIdx >= 0 || lRtfIdx >=0 || lHtmlIdx >= 0)) {
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
			hr = Util::HrCopyPropTagArray(lpPropTagArray, &~ptrPropTagArray);
			if (hr != hrSuccess)
				return hr;
		} else {
			// Get the proplist, so we can filter it.
			hr = GetPropList(ulFlags, &~ptrPropTagArray);
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
					if (Util::FindPropInArray(ptrPropTagArray, CHANGE_PROP_TYPE(ulBestMatchTable[m_ulBodyType][i], PT_UNSPECIFIED)) >= 0) {
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
			hr = ECMAPIProp::GetProps(ptrPropTagArray, ulFlags, &cValues, &~ptrPropArray);
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
			hr = ECMAPIProp::GetProps(lpPropTagArray, ulFlags, &cValues, &~ptrPropArray);
			if (HR_FAILED(hr))
				return hr;
		}
	} else {  // m_ulBodyType != bodyTypeUnknown
	    // We don't know what out body type is (yet).
		hr = ECMAPIProp::GetProps(lpPropTagArray, ulFlags, &cValues, &~ptrPropArray);
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
	if (ulPropTag == PR_BODY_HTML)
	    ulPropTag = PR_HTML;
	auto hr = HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
	if (HR_FAILED(hr))
		return hr;

	if (PROP_TYPE(lpsPropValue->ulPropTag) != PT_ERROR ||
	    lpsPropValue->Value.err != MAPI_E_NOT_FOUND ||
	    m_ulBodyType == bodyTypeUnknown)
		return HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
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
	return HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
}

/**
 * Synchronize the best body obtained from the server to the requested body.
 *
 * @param[in]	ulPropTag		The proptag of the body to synchronize.
 */
HRESULT ECMessage::SyncBody(ULONG ulPropTag)
{
	if (!Util::IsBodyProp(ulPropTag))
		return MAPI_E_INVALID_PARAMETER;
	if (m_ulBodyType == bodyTypeUnknown)
		/*
		 * There is nothing to synchronize if we do not know what our
		 * best body type is.
		 */
		return MAPI_E_NO_SUPPORT;

	const BOOL fModifyOld = fModify;
	auto laters = make_scope_success([&]() { fModify = fModifyOld; });
	// Temporary enable write access
	fModify = TRUE;

	if (m_ulBodyType == bodyTypePlain) {
		if (PROP_ID(ulPropTag) == PROP_ID(PR_RTF_COMPRESSED))
			return SyncPlainToRtf();
		else if (PROP_ID(ulPropTag) == PROP_ID(PR_HTML))
			return SyncPlainToHtml();
	} else if (m_ulBodyType == bodyTypeRTF) {
		if (PROP_ID(ulPropTag) == PROP_ID(PR_BODY) || PROP_ID(ulPropTag) == PROP_ID(PR_HTML)) {
			std::string rtf;
			auto hr = GetRtfData(&rtf);
			if (hr != hrSuccess)
				return hr;
			return SyncRtf(rtf);
		}
	} else if (m_ulBodyType == bodyTypeHTML) {
		if (PROP_ID(ulPropTag) == PROP_ID(PR_BODY))
			return SyncHtmlToPlain();
		else if (PROP_ID(ulPropTag) == PROP_ID(PR_RTF_COMPRESSED))
			return SyncHtmlToRtf();
	}
	return hrSuccess;
}

/**
 * Synchronize a plaintext body to an RTF body.
 */
HRESULT ECMessage::SyncPlainToRtf()
{
	StreamPtr ptrBodyStream, ptrCompressedRtfStream, ptrUncompressedRtfStream;
	ULARGE_INTEGER emptySize = {{0,0}};
	assert(!m_bInhibitSync);
	m_bInhibitSync = TRUE;

	auto laters = make_scope_success([&]() { m_bInhibitSync = FALSE; });
	auto hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, &~ptrBodyStream);
	if (hr != hrSuccess)
		return hr;
	hr = ECMAPIProp::OpenProperty(PR_RTF_COMPRESSED, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~ptrCompressedRtfStream);
	if (hr != hrSuccess)
		return hr;
	//Truncate to zero
	hr = ptrCompressedRtfStream->SetSize(emptySize);
	if (hr != hrSuccess)
		return hr;
	hr = WrapCompressedRTFStream(ptrCompressedRtfStream, MAPI_MODIFY, &~ptrUncompressedRtfStream);
	if (hr != hrSuccess)
		return hr;
	// Convert it now
	hr = Util::HrTextToRtf(ptrBodyStream, ptrUncompressedRtfStream);
	if (hr != hrSuccess)
		return hr;
	// Commit uncompress data
	hr = ptrUncompressedRtfStream->Commit(0);
	if (hr != hrSuccess)
		return hr;
	// Commit compressed data
	hr = ptrCompressedRtfStream->Commit(0);
	if (hr != hrSuccess)
		return hr;

	// We generated this property but don't really want to save it to the server
	HrSetCleanProperty(PR_RTF_COMPRESSED);
	// and mark it as deleted, since we want the server to remove the old version if this was in the database
	m_setDeletedProps.emplace(PR_RTF_COMPRESSED);
	return hrSuccess;
}

/**
 * Synchronize a plaintext body to an HTML body.
 */
HRESULT ECMessage::SyncPlainToHtml()
{
	unsigned int ulCodePage = 0;
	StreamPtr ptrBodyStream, ptrHtmlStream;

	ULARGE_INTEGER emptySize = {{0,0}};
	assert(!m_bInhibitSync);
	m_bInhibitSync = TRUE;

	auto laters = make_scope_success([&]() { m_bInhibitSync = FALSE; });
	auto hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, &~ptrBodyStream);
	if (hr != hrSuccess)
		return hr;
	hr = ECMAPIProp::OpenProperty(PR_HTML, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~ptrHtmlStream);
	if (hr != hrSuccess)
		return hr;
	hr = GetCodePage(&ulCodePage);
	if (hr != hrSuccess)
		return hr;
	hr = ptrHtmlStream->SetSize(emptySize);
	if (hr != hrSuccess)
		return hr;
	hr = Util::HrTextToHtml(ptrBodyStream, ptrHtmlStream, ulCodePage);
	if (hr != hrSuccess)
		return hr;
	hr = ptrHtmlStream->Commit(0);
	if (hr != hrSuccess)
		return hr;

	// We generated this property but don't really want to save it to the server
	HrSetCleanProperty(PR_HTML);
	// and mark it as deleted, since we want the server to remove the old version if this was in the database
	m_setDeletedProps.emplace(PR_HTML);
	return hrSuccess;
}

/**
 * Synchronize an RTF body to a plaintext and an HTML body.
 */
HRESULT ECMessage::SyncRtf(const std::string &strRTF)
{
	enum eRTFType { RTFTypeOther, RTFTypeFromText, RTFTypeFromHTML};
	bool bDone = false;
	unsigned int ulCodePage = 0;
	StreamPtr ptrHTMLStream;
	ULONG ulWritten = 0;
	eRTFType rtfType = RTFTypeOther;
	ULARGE_INTEGER emptySize = {{0,0}};
	LARGE_INTEGER moveBegin = {{0,0}};
	assert(!m_bInhibitSync);
	m_bInhibitSync = TRUE;

	auto laters = make_scope_success([&]() { m_bInhibitSync = FALSE; });
	auto hr = GetCodePage(&ulCodePage);
	if (hr != hrSuccess)
		return hr;
	hr = ECMAPIProp::OpenProperty(PR_HTML, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~ptrHTMLStream);
	if (hr != hrSuccess)
		return hr;
	hr = ptrHTMLStream->SetSize(emptySize);
	if (hr != hrSuccess)
		return hr;

	// Determine strategy based on RTF type.
	if (isrtfhtml(strRTF.c_str(), strRTF.size()))
		rtfType = RTFTypeFromHTML;
	else if (isrtftext(strRTF.c_str(), strRTF.size()))
		rtfType = RTFTypeFromText;
	else
		rtfType = RTFTypeOther;

	if (rtfType == RTFTypeOther) {
		BOOL bUpdated;
		hr = RTFSync(this, RTF_SYNC_RTF_CHANGED, &bUpdated);
		if (hr == hrSuccess) {
			StreamPtr ptrBodyStream;

			hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, 0, 0, &~ptrBodyStream);
			if (hr != hrSuccess)
				return hr;
			hr = ptrHTMLStream->SetSize(emptySize);
			if (hr != hrSuccess)
				return hr;
			hr = Util::HrTextToHtml(ptrBodyStream, ptrHTMLStream, ulCodePage);
			if (hr != hrSuccess)
				return hr;
			hr = ptrHTMLStream->Commit(0);
			if (hr != hrSuccess)
				return hr;
			bDone = true;
		}
	}

	if (!bDone) {
		std::string strHTML;
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
			return hr;

		hr = ptrHTMLStream->Write(strHTML.c_str(), strHTML.size(), &ulWritten);
		if (hr != hrSuccess)
			return hr;
		hr = ptrHTMLStream->Commit(0);
		if (hr != hrSuccess)
			return hr;
		hr = ptrHTMLStream->Seek(moveBegin, STREAM_SEEK_SET, NULL);
		if (hr != hrSuccess)
			return hr;
		hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~ptrBodyStream);
		if (hr != hrSuccess)
			return hr;
		hr = ptrBodyStream->SetSize(emptySize);
		if (hr != hrSuccess)
			return hr;
		hr = Util::HrHtmlToText(ptrHTMLStream, ptrBodyStream, ulCodePage);
		if (hr != hrSuccess)
			return hr;
		hr = ptrBodyStream->Commit(0);
		if (hr != hrSuccess)
			return hr;
	}

	if (rtfType == RTFTypeOther) {
		// No need to store the HTML.
		HrSetCleanProperty(PR_HTML);
		// And delete from server in case it changed.
		m_setDeletedProps.emplace(PR_HTML);
	} else if (rtfType == RTFTypeFromText) {
		// No need to store anything but the plain text.
		HrSetCleanProperty(PR_RTF_COMPRESSED);
		HrSetCleanProperty(PR_HTML);
		// And delete them both.
		m_setDeletedProps.emplace(PR_RTF_COMPRESSED);
		m_setDeletedProps.emplace(PR_HTML);
	} else if (rtfType == RTFTypeFromHTML) {
		// No need to keep the RTF version
		HrSetCleanProperty(PR_RTF_COMPRESSED);
		// And delete from server.
		m_setDeletedProps.emplace(PR_RTF_COMPRESSED);
	}
	return hr;
}

/**
 * Synchronize an HTML body to a plaintext body.
 */
HRESULT ECMessage::SyncHtmlToPlain()
{
	StreamPtr ptrHtmlStream, ptrBodyStream;
	unsigned int ulCodePage;
	ULARGE_INTEGER emptySize = {{0,0}};

	assert(m_bInhibitSync == FALSE);
	m_bInhibitSync = TRUE;

	auto laters = make_scope_success([&]() { m_bInhibitSync = FALSE; });
	auto hr = ECMAPIProp::OpenProperty(PR_HTML, &IID_IStream, 0, 0, &~ptrHtmlStream);
	if (hr != hrSuccess)
		return hr;
	hr = ECMAPIProp::OpenProperty(PR_BODY_W, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~ptrBodyStream);
	if (hr != hrSuccess)
		return hr;
	hr = ptrBodyStream->SetSize(emptySize);
	if (hr != hrSuccess)
		return hr;
	hr = GetCodePage(&ulCodePage);
	if (hr != hrSuccess)
		return hr;
	hr = Util::HrHtmlToText(ptrHtmlStream, ptrBodyStream, ulCodePage);
	if (hr != hrSuccess)
		return hr;
	return ptrBodyStream->Commit(0);
}

/**
 * Synchronize an HTML body to an RTF body.
 */
HRESULT ECMessage::SyncHtmlToRtf()
{
	StreamPtr ptrHtmlStream, ptrRtfCompressedStream, ptrRtfUncompressedStream;
	unsigned int ulCodePage;
	ULARGE_INTEGER emptySize = {{0,0}};
	assert(!m_bInhibitSync);
	m_bInhibitSync = TRUE;

	auto laters = make_scope_success([&]() { m_bInhibitSync = FALSE; });
	auto hr = ECMAPIProp::OpenProperty(PR_HTML, &IID_IStream, 0, 0, &~ptrHtmlStream);
	if (hr != hrSuccess)
		return hr;
	hr = ECMAPIProp::OpenProperty(PR_RTF_COMPRESSED, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE|MAPI_MODIFY, &~ptrRtfCompressedStream);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRtfCompressedStream->SetSize(emptySize);
	if (hr != hrSuccess)
		return hr;
	hr = WrapCompressedRTFStream(ptrRtfCompressedStream, MAPI_MODIFY, &~ptrRtfUncompressedStream);
	if (hr != hrSuccess)
		return hr;
	hr = GetCodePage(&ulCodePage);
	if (hr != hrSuccess)
		return hr;
	// Convert it now
	hr = Util::HrHtmlToRtf(ptrHtmlStream, ptrRtfUncompressedStream, ulCodePage);
	if (hr != hrSuccess)
		return hr;
	// Commit uncompress data
	hr = ptrRtfUncompressedStream->Commit(0);
	if (hr != hrSuccess)
		return hr;
	// Commit compressed data
	hr = ptrRtfCompressedStream->Commit(0);
	if (hr != hrSuccess)
		return hr;

	// We generated this property but don't really want to save it to the server
	HrSetCleanProperty(PR_RTF_COMPRESSED);
	// and mark it as deleted, since we want the server to remove the old version if this was in the database
	m_setDeletedProps.emplace(PR_RTF_COMPRESSED);
	return hrSuccess;
}

HRESULT ECMessage::GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray)
{
	const eBodyType ulBodyTypeSaved = m_ulBodyType;
	SPropTagArrayPtr ptrPropTagArray, ptrPropTagArrayMod;

	m_ulBodyType = bodyTypeUnknown;	// Make sure no bodies are generated when attempts are made to open them to check the error code if any.
	auto laters = make_scope_success([&]() { m_ulBodyType = ulBodyTypeSaved; });
	auto hr = ECMAPIProp::GetPropList(ulFlags, &~ptrPropTagArray);
	if (hr != hrSuccess)
		return hr;

	// Because the body type was set to unknown, ECMAPIProp::GetPropList does not return the proptags of the bodies that can be
	// generated unless they were already generated.
	// So we need to check which body properties are included and if at least one is, we need to make sure all of them are.
	bool bHaveBody = Util::FindPropInArray(ptrPropTagArray, CHANGE_PROP_TYPE(PR_BODY, PT_UNSPECIFIED)) >= 0;
	bool bHaveRtf  = Util::FindPropInArray(ptrPropTagArray, PR_RTF_COMPRESSED) >= 0;
	bool bHaveHtml = Util::FindPropInArray(ptrPropTagArray, PR_HTML) >= 0;

	if ((bHaveBody && bHaveRtf && bHaveHtml) ||
		(!bHaveBody && !bHaveRtf && !bHaveHtml))
	{
		// Nothing more to do
		*lppPropTagArray = ptrPropTagArray.release();
		return hr;
	}

	// We have at least one body prop. Determine which tags to add.
	hr = ECAllocateBuffer(CbNewSPropTagArray(ptrPropTagArray->cValues + 2), &~ptrPropTagArrayMod);
	if (hr != hrSuccess)
		return hr;

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
	return hrSuccess;
}

HRESULT ECMessage::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	if (lpiid == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
//FIXME: Support the flags ?
	if(ulPropTag == PR_MESSAGE_ATTACHMENTS) {
		if(*lpiid == IID_IMAPITable)
			hr = GetAttachmentTable(ulInterfaceOptions, reinterpret_cast<IMAPITable **>(lppUnk));
		return hr;
	} else if(ulPropTag == PR_MESSAGE_RECIPIENTS) {
		if (*lpiid == IID_IMAPITable)
			hr = GetRecipientTable(ulInterfaceOptions, reinterpret_cast<IMAPITable **>(lppUnk));
		return hr;
	}
	// Workaround for support html in outlook 2000/xp
	if (ulPropTag == PR_BODY_HTML)
		ulPropTag = PR_HTML;
	hr = ECMAPIProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	bool sync = hr == MAPI_E_NOT_FOUND && m_ulBodyType != bodyTypeUnknown && Util::IsBodyProp(ulPropTag);
	if (!sync)
		return hr;
	hr = SyncBody(ulPropTag);
	if (hr != hrSuccess)
		return hr;
	// Retry now the body is generated.
	return ECMAPIProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
}

HRESULT ECMessage::GetAttachmentTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	SizedSPropTagArray(8, sPropAttachColumns) =
		{8, {PR_ATTACH_NUM, PR_INSTANCE_KEY, PR_RECORD_KEY,
		PR_RENDERING_POSITION, PR_ATTACH_FILENAME_W, PR_ATTACH_METHOD,
		PR_DISPLAY_NAME_W, PR_ATTACH_LONG_FILENAME_W}};
	memory_ptr<SPropTagArray> lpPropTagArray;
	scoped_rlock lock(m_hMutexMAPIObject);

	if (!m_props_loaded) {
		auto hr = HrLoadProps();
		if (hr != hrSuccess)
			return hr;
	}

	if (lpAttachments == nullptr) {
		Util::proptag_change_unicode(ulFlags, sPropAttachColumns);
		auto hr = ECMemTable::Create(sPropAttachColumns, PR_ATTACH_NUM, &~lpAttachments);
		if(hr != hrSuccess)
			return hr;

		// This code is resembles the table-copying code in GetRecipientTable, but we do some slightly different
		// processing on the data that we receive from the table. Basically, data is copied to the attachment
		// table received from the server through m_sMapiObject, but the PR_ATTACH_NUM is re-generated locally
		if (!fNew) {
			// existing message has "table" in m_sMapiObject data
			for (const auto &obj : m_sMapiObject->lstChildren) {
				if (obj->ulObjType != MAPI_ATTACH)
					continue;
				if (obj->bDelete)
					continue;
				ulNextAttUniqueId = std::max(obj->ulUniqueId, ulNextAttUniqueId);
				++ulNextAttUniqueId;

				unsigned int i = 0, ulProps = obj->lstProperties.size();
				ecmem_ptr<SPropValue> lpProps;
				SPropValue sKeyProp;

				// +1 for maybe missing PR_ATTACH_NUM property
				// +1 for maybe missing PR_OBJECT_TYPE property
				hr = ECAllocateBuffer(sizeof(SPropValue) * (ulProps + 2), &~lpProps);
				if (hr != hrSuccess)
					return hr;

				SPropValue *lpPropID = nullptr, *lpPropType = nullptr;
				for (const auto &pv : obj->lstProperties) {
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
					return hr; // continue?
			}

			// since we just loaded the table, all enties are clean (actually not required for attachments, but it doesn't hurt)
			hr = lpAttachments->HrSetClean();
			if (hr != hrSuccess)
				return hr;
		} // !new == empty table
	}

	if (lpAttachments == nullptr)
		return MAPI_E_CALL_FAILED;

	object_ptr<ECMemTableView> lpView;
	auto hr = lpAttachments->HrGetView(createLocaleFromName(""),
	          ulFlags & MAPI_UNICODE, &~lpView);
	if(hr != hrSuccess)
		return hr;
	return lpView->QueryInterface(IID_IMAPITable,
	       reinterpret_cast<void **>(lppTable));
}

HRESULT ECMessage::OpenAttach(ULONG ulAttachmentNum, LPCIID lpInterface, ULONG ulFlags, LPATTACH *lppAttach)
{
	object_ptr<ECAttach> lpAttach;
	object_ptr<IECPropStorage> lpParentStorage;
	SPropValue			sID;
	ecmem_ptr<SPropValue> lpObjId;
	ULONG				ulObjId;

	if (lpAttachments == nullptr) {
		object_ptr<IMAPITable> lpTable;
		auto hr = GetAttachmentTable(fMapiUnicode, &~lpTable);
		if(hr != hrSuccess)
			return hr;
	}
	if (lpAttachments == nullptr)
		return MAPI_E_CALL_FAILED;
	auto hr = ECAttach::Create(GetMsgStore(), MAPI_ATTACH, true,
	          ulAttachmentNum, m_lpRoot, &~lpAttach);
	if(hr != hrSuccess)
		return hr;

	sID.ulPropTag = PR_ATTACH_NUM;
	sID.Value.ul = ulAttachmentNum;
	if (lpAttachments->HrGetRowID(&sID, &~lpObjId) == hrSuccess)
		ulObjId = lpObjId->Value.ul;
	else
		ulObjId = 0;

	hr = GetMsgStore()->lpTransport->HrOpenParentStorage(this,
	     ulAttachmentNum, ulObjId, lpStorage->GetServerStorage(),
	     &~lpParentStorage);
	if(hr != hrSuccess)
		return hr;
	hr = lpAttach->HrSetPropStorage(lpParentStorage, TRUE);
	if(hr != hrSuccess)
		return hr;
	hr = lpAttach->QueryInterface(IID_IAttachment, reinterpret_cast<void **>(lppAttach));
	// Register the object as a child of ours
	AddChild(lpAttach);
	return hr;
}

HRESULT ECMessage::CreateAttach(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulAttachmentNum, LPATTACH *lppAttach)
{
	return CreateAttach(lpInterface, ulFlags, ECAttachFactory(), lpulAttachmentNum, lppAttach);
}

HRESULT ECMessage::CreateAttach(LPCIID lpInterface, ULONG ulFlags, const IAttachFactory &refFactory, ULONG *lpulAttachmentNum, LPATTACH *lppAttach)
{
	SPropValue			sID;
	object_ptr<IECPropStorage> storage;

	if (lpAttachments == nullptr) {
		object_ptr<IMAPITable> lpTable;
		auto hr = GetAttachmentTable(fMapiUnicode, &~lpTable);
		if(hr != hrSuccess)
			return hr;
	}
	if (lpAttachments == nullptr)
		return MAPI_E_CALL_FAILED;

	object_ptr<ECAttach> lpAttach;
	auto hr = refFactory.Create(GetMsgStore(), MAPI_ATTACH, true,
	          ulNextAttUniqueId, m_lpRoot, &~lpAttach);
	if(hr != hrSuccess)
		return hr;
	hr = lpAttach->HrLoadEmptyProps();

	if(hr != hrSuccess)
		return hr;
	sID.ulPropTag = PR_ATTACH_NUM;
	sID.Value.ul = ulNextAttUniqueId;
	hr = GetMsgStore()->lpTransport->HrOpenParentStorage(this,
	     ulNextAttUniqueId, 0, nullptr, &~storage);
	if(hr != hrSuccess)
		return hr;
	hr = lpAttach->HrSetPropStorage(storage, false);
	if(hr != hrSuccess)
		return hr;
	hr = lpAttach->SetProps(1, &sID, NULL);
	if(hr != hrSuccess)
		return hr;
	hr = lpAttach->QueryInterface(IID_IAttachment, reinterpret_cast<void **>(lppAttach));
	AddChild(lpAttach);
	*lpulAttachmentNum = sID.Value.ul;
	// successfully created attachment, so increment counter for the next
	++ulNextAttUniqueId;
	return hr;
}

HRESULT ECMessage::DeleteAttach(ULONG ulAttachmentNum, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	SPropValue sPropID;

	if (lpAttachments == nullptr) {
		object_ptr<IMAPITable> lpTable;
		auto hr = GetAttachmentTable(fMapiUnicode, &~lpTable);
		if(hr != hrSuccess)
			return hr;
	}
	if (lpAttachments == nullptr)
		return MAPI_E_CALL_FAILED;

	sPropID.ulPropTag = PR_ATTACH_NUM;
	sPropID.Value.ul = ulAttachmentNum;
	return lpAttachments->HrModifyRow(ECKeyTable::TABLE_ROW_DELETE, nullptr, &sPropID, 1);
	// the object is deleted from the child list when SaveChanges is called, which calls SyncAttachments()
}

HRESULT ECMessage::GetRecipientTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	SizedSPropTagArray(15, sPropRecipColumns) =
		{15, {PR_7BIT_DISPLAY_NAME_W, PR_EMAIL_ADDRESS_W,
		PR_INSTANCE_KEY, PR_RECORD_KEY, PR_SEARCH_KEY,
		PR_SEND_RICH_INFO, PR_DISPLAY_NAME_W, PR_RECIPIENT_TYPE,
		PR_ROWID, PR_DISPLAY_TYPE, PR_ENTRYID, PR_SPOOLER_STATUS,
		PR_OBJECT_TYPE, PR_ADDRTYPE_W, PR_RESPONSIBILITY}};
	memory_ptr<SPropTagArray> lpPropTagArray;
	scoped_rlock lock(m_hMutexMAPIObject);

	if (!m_props_loaded) {
		auto hr = HrLoadProps();
		if (hr != hrSuccess)
			return hr;
	}

	if (lpRecips == nullptr) {
		Util::proptag_change_unicode(ulFlags, sPropRecipColumns);
		auto hr = ECMemTable::Create(sPropRecipColumns, PR_ROWID, &~lpRecips);
		if(hr != hrSuccess)
			return hr;

		// What we do here is that we reconstruct a recipient table from the m_sMapiObject data, and then process it in two ways:
		// 1. Remove PR_ROWID values and replace them with client-side values
		// 2. Replace PR_EC_CONTACT_ENTRYID with PR_ENTRYID in the table
		// This means that the PR_ENTRYID value from the original recipient is not actually in the table (only
		// in the lpID value passed to HrModifyRow)

		// Get the existing table for this message (there is none if the message is unsaved)
		if (!fNew) {
			// existing message has "table" in m_sMapiObject data
			for (const auto &obj : m_sMapiObject->lstChildren) {
				// The only valid types are MAPI_MAILUSER and MAPI_DISTLIST. However some MAPI clients put in other
				// values as object type. We know about the existence of MAPI_ATTACH as another valid subtype for
				// Messages, so we'll skip those, treat MAPI_DISTLIST as MAPI_DISTLIST and anything else as
				// MAPI_MAILUSER.
				if (obj->ulObjType == MAPI_ATTACH)
					continue;
				if (obj->bDelete)
					continue;
				ulNextRecipUniqueId = std::max(obj->ulUniqueId, ulNextRecipUniqueId);
				++ulNextRecipUniqueId;

				unsigned int i = 0, ulProps = obj->lstProperties.size();
				ecmem_ptr<SPropValue> lpProps;
				SPropValue sKeyProp, *lpPropID = nullptr, *lpPropObjType = nullptr;

				// +1 for maybe missing PR_ROWID property
				// +1 for maybe missing PR_OBJECT_TYPE property
				hr = ECAllocateBuffer(sizeof(SPropValue) * (ulProps + 2), &~lpProps);
				if (hr != hrSuccess)
					return hr;

				for (const auto &pv : obj->lstProperties) {
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
					return hr;
			}

			// since we just loaded the table, all enties are clean
			hr = lpRecips->HrSetClean();
			if (hr != hrSuccess)
				return hr;
		} // !fNew
	}

	object_ptr<ECMemTableView> lpView;
	auto hr = lpRecips->HrGetView(createLocaleFromName(""),
	          ulFlags & MAPI_UNICODE, &~lpView);
	if(hr != hrSuccess)
		return hr;
	return lpView->QueryInterface(IID_IMAPITable,
	       reinterpret_cast<void **>(lppTable));
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
 * the support object assumes the row IDs to stay the same when it does ModifyRecipients(0, lpMods)
 * so we can't generate the IDs whenever ADD or 0 is specified.
 */

HRESULT ECMessage::ModifyRecipients(ULONG ulFlags, const ADRLIST *lpMods)
{
	if (lpMods == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (!fModify)
		return MAPI_E_NO_ACCESS;

	HRESULT hr = hrSuccess;
	ecmem_ptr<SPropValue> lpRecipProps;
	ULONG cValuesRecipProps = 0;
	SPropValue sPropAdd[2], sKeyProp;

	// Load the recipients table object
	if(lpRecips == NULL) {
		object_ptr<IMAPITable> lpTable;
		hr = GetRecipientTable(fMapiUnicode, &~lpTable);
		if(hr != hrSuccess)
			return hr;
	}
	if (lpRecips == nullptr)
		return MAPI_E_CALL_FAILED;
	if(ulFlags == 0) {
		hr = lpRecips->HrDeleteAll();

		if(hr != hrSuccess)
			return hr;
		ulNextRecipUniqueId = 0;
	}

	for (unsigned int i = 0; i < lpMods->cEntries; ++i) {
		if(ulFlags & MODRECIP_ADD || ulFlags == 0) {
			// Add a new PR_ROWID
			sPropAdd[0].ulPropTag = PR_ROWID;
			sPropAdd[0].Value.ul = ulNextRecipUniqueId++;
			// Add a PR_INSTANCE_KEY which is equal to the row id
			sPropAdd[1].ulPropTag = PR_INSTANCE_KEY;
			sPropAdd[1].Value.bin.cb = sizeof(ULONG);
			sPropAdd[1].Value.bin.lpb = (unsigned char *)&sPropAdd[0].Value.ul;

			hr = Util::HrMergePropertyArrays(lpMods->aEntries[i].rgPropVals, lpMods->aEntries[i].cValues, sPropAdd, 2, &+lpRecipProps, &cValuesRecipProps);
			if (hr != hrSuccess)
				continue;

			sKeyProp.ulPropTag = PR_EC_HIERARCHYID;
			sKeyProp.Value.ul = 0;

			// Add the new row
			hr = lpRecips->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &sKeyProp, lpRecipProps, cValuesRecipProps);
			lpRecipProps.reset();
		} else if(ulFlags & MODRECIP_MODIFY) {
			// Simply update the existing row, leave the PR_EC_HIERARCHY key prop intact.
			hr = lpRecips->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, lpMods->aEntries[i].rgPropVals, lpMods->aEntries[i].cValues);
		} else if(ulFlags & MODRECIP_REMOVE) {
			hr = lpRecips->HrModifyRow(ECKeyTable::TABLE_ROW_DELETE, NULL, lpMods->aEntries[i].rgPropVals, lpMods->aEntries[i].cValues);
		}

		if(hr != hrSuccess)
			return hr;
	}

	m_bRecipsDirty = TRUE;
	return hrSuccess;
}

HRESULT ECMessage::SubmitMessage(ULONG ulFlags)
{
	SizedSPropTagArray(1, sPropTagArray);
	unsigned int cValue = 0, ulRepCount = 0, cRecip = 0;
	unsigned int ulPreprocessFlags = 0, ulSubmitFlag = 0;
	ecmem_ptr<SPropValue> lpsPropArray, lpRecip;
	object_ptr<IMAPITable> lpRecipientTable;
	SizedADRLIST(1, sRowSetRecip);
	SPropValue sPropResponsibility;
	FILETIME ft;

	// Get message flag to check for resubmit. PR_MESSAGE_FLAGS
	sPropTagArray.cValues = 1;
	sPropTagArray.aulPropTag[0] = PR_MESSAGE_FLAGS;
	auto hr = ECMAPIProp::GetProps(sPropTagArray, 0, &cValue, &~lpsPropArray);
	if(HR_FAILED(hr))
		return hr;

	if(lpsPropArray->ulPropTag == PR_MESSAGE_FLAGS) {
		// Re-set 'unsent' as it is obviously not sent if we're submitting it ... This allows you to send a message
		// multiple times, but only if the client calls SubmitMessage multiple times.
		lpsPropArray->Value.ul |= MSGFLAG_UNSENT;
		hr = SetProps(1, lpsPropArray, nullptr);
		if(hr != hrSuccess)
			return hr;
	}

	// Get the recipientslist
	hr = GetRecipientTable(fMapiUnicode, &~lpRecipientTable);
	if(hr != hrSuccess)
		return hr;
	// Check if recipientslist is empty
	hr = lpRecipientTable->GetRowCount(0, &ulRepCount);
	if(hr != hrSuccess)
		return hr;
	if (ulRepCount == 0)
		return MAPI_E_NO_RECIPIENTS;

	// Step through recipient list, set PR_RESPONSIBILITY to FALSE for all recipients
	while(TRUE){
		rowset_ptr lpsRow;
		hr = lpRecipientTable->QueryRows(1, 0, &~lpsRow);
		if (hr != hrSuccess)
			return hr;
		if (lpsRow->cRows == 0)
			break;

		sPropResponsibility.ulPropTag = PR_RESPONSIBILITY;
		sPropResponsibility.Value.b = FALSE;

		// Set PR_RESPONSIBILITY
		hr = Util::HrAddToPropertyArray(lpsRow[0].lpProps,
		     lpsRow[0].cValues, &sPropResponsibility, &+lpRecip, &cRecip);
		if(hr != hrSuccess)
			return hr;

		sRowSetRecip.cEntries = 1;
		sRowSetRecip.aEntries[0].rgPropVals = lpRecip;
		sRowSetRecip.aEntries[0].cValues = cRecip;

		if (lpsRow[0].cValues > 1) {
			hr = ModifyRecipients(MODRECIP_MODIFY, sRowSetRecip);
			if (hr != hrSuccess)
				return hr;
		}
		lpRecip.reset();
	}

	// Get the time to add to the message as PR_CLIENT_SUBMIT_TIME
    GetSystemTimeAsFileTime(&ft);
	hr = ECAllocateBuffer(sizeof(SPropValue) * 2, &~lpsPropArray);
	if (hr != hrSuccess)
		return hr;

	lpsPropArray[0].ulPropTag = PR_CLIENT_SUBMIT_TIME;
	lpsPropArray[0].Value.ft = ft;
	lpsPropArray[1].ulPropTag = PR_MESSAGE_DELIVERY_TIME;
	lpsPropArray[1].Value.ft = ft;
	hr = SetProps(2, lpsPropArray, NULL);
  	if (hr != hrSuccess)
		return hr;

	// Resolve recipients
	hr = GetMsgStore()->lpSupport->ExpandRecips(this, &ulPreprocessFlags);
	if (hr != hrSuccess)
		return hr;

	// Setup PR_SUBMIT_FLAGS
	if (ulPreprocessFlags & NEEDS_PREPROCESSING)
		ulSubmitFlag = SUBMITFLAG_PREPROCESS;
	hr = ECAllocateBuffer(sizeof(SPropValue), &~lpsPropArray);
	if (hr != hrSuccess)
		return hr;

	lpsPropArray[0].ulPropTag = PR_SUBMIT_FLAGS;
	lpsPropArray[0].Value.l = ulSubmitFlag;

	hr = SetProps(1, lpsPropArray, NULL);
	if (hr != hrSuccess)
		return hr;
	// All done, save changes
	hr = SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		return hr;
	// Add the message to the master outgoing queue, and request the spooler to DoSentMail()
	return GetMsgStore()->lpTransport->HrSubmitMessage(m_cbEntryId,
	       m_lpEntryId, EC_SUBMIT_MASTER | EC_SUBMIT_DOSENTMAIL);
}

HRESULT ECMessage::SetReadFlag2(unsigned int ulFlags)
{
	ecmem_ptr<SPropValue> lpReadReceiptRequest;
	memory_ptr<SPropValue> lpsPropUserName;
	SPropValue		sProp;
	object_ptr<IMAPIFolder> lpRootFolder;
	object_ptr<IMessage> lpNewMessage, lpThisMessage;
	unsigned int objtype = 0, cValues = 0, cbStoreID = 0;
	memory_ptr<ENTRYID> lpStoreID;
	object_ptr<IMsgStore> lpDefMsgStore;

	// see if read receipts are requested
	static constexpr const SizedSPropTagArray(2, proptags) =
		{2, {PR_MESSAGE_FLAGS, PR_READ_RECEIPT_REQUESTED}};
	auto hr = ECMAPIProp::GetProps(proptags, 0, &cValues, &~lpReadReceiptRequest);
	if(hr == hrSuccess && (!(ulFlags&(SUPPRESS_RECEIPT|CLEAR_READ_FLAG | CLEAR_NRN_PENDING | CLEAR_RN_PENDING)) || (ulFlags&GENERATE_RECEIPT_ONLY )) &&
	    lpReadReceiptRequest[1].Value.b && ((lpReadReceiptRequest[0].Value.ul & MSGFLAG_RN_PENDING) || (lpReadReceiptRequest[0].Value.ul & MSGFLAG_NRN_PENDING)))
	{
		hr = QueryInterface(IID_IMessage, &~lpThisMessage);
		if (hr != hrSuccess)
			return hr;

		if((ulFlags & (GENERATE_RECEIPT_ONLY | SUPPRESS_RECEIPT)) == (GENERATE_RECEIPT_ONLY | SUPPRESS_RECEIPT) )
		{
			sProp.ulPropTag = PR_READ_RECEIPT_REQUESTED;
			sProp.Value.b = FALSE;
			hr = HrSetOneProp(lpThisMessage, &sProp);
			if (hr != hrSuccess)
				return hr;
			hr = lpThisMessage->SaveChanges(KEEP_OPEN_READWRITE);
			if (hr != hrSuccess)
				return hr;
		}else {
			// Open the default store, by using the username property
			hr = HrGetOneProp(GetMsgStore(), PR_USER_NAME, &~lpsPropUserName);
			if (hr != hrSuccess)
				return hr;
			hr = GetMsgStore()->CreateStoreEntryID(nullptr, lpsPropUserName->Value.LPSZ, fMapiUnicode, &cbStoreID, &~lpStoreID);
			if (hr != hrSuccess)
				return hr;
			hr = GetMsgStore()->lpSupport->OpenEntry(cbStoreID, lpStoreID, &iid_of(lpDefMsgStore), MAPI_MODIFY, &objtype, &~lpDefMsgStore);
			if (hr != hrSuccess)
				return hr;

			// Open the root folder of the default store to create a new message
			hr = lpDefMsgStore->OpenEntry(0, nullptr, &iid_of(lpRootFolder), MAPI_MODIFY, &objtype, &~lpRootFolder);
			if (hr != hrSuccess)
				return hr;
			hr = lpRootFolder->CreateMessage(nullptr, 0, &~lpNewMessage);
			if (hr != hrSuccess)
				return hr;
			hr = ClientUtil::ReadReceipt(0, lpThisMessage, &+lpNewMessage);
			if(hr != hrSuccess)
				return hr;
			hr = lpNewMessage->SubmitMessage(FORCE_SUBMIT);
			if(hr != hrSuccess)
				return hr;

			// Oke, everything is fine, so now remove MSGFLAG_RN_PENDING and MSGFLAG_NRN_PENDING from PR_MESSAGE_FLAGS
			// Sent CLEAR_NRN_PENDING and CLEAR_RN_PENDING  for remove those properties
			ulFlags |= CLEAR_NRN_PENDING | CLEAR_RN_PENDING;
		}
	}

	return GetMsgStore()->lpTransport->HrSetReadFlag(m_cbEntryId, m_lpEntryId, ulFlags, 0);
}

HRESULT ECMessage::SetReadFlag(ULONG ulFlags)
{
	if ((ulFlags & ~(CLEAR_READ_FLAG | CLEAR_NRN_PENDING | CLEAR_RN_PENDING | GENERATE_RECEIPT_ONLY | MAPI_DEFERRED_ERRORS | SUPPRESS_RECEIPT)) != 0 ||
	    (ulFlags & (SUPPRESS_RECEIPT | CLEAR_READ_FLAG)) == (SUPPRESS_RECEIPT | CLEAR_READ_FLAG) ||
	    (ulFlags & (SUPPRESS_RECEIPT | CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY)) == (SUPPRESS_RECEIPT | CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY) ||
	    (ulFlags & (CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY)) == (CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY))
		return MAPI_E_INVALID_PARAMETER;
	/*
	 * parent!=nullptr is an unsaved message for which read receipt
	 * handling probably makes no sense.
	 */
	if (m_lpParentID == nullptr) {
		auto ret = SetReadFlag2(ulFlags);
		if (ret != hrSuccess)
			return ret;
	}

    // Server update OK, change local flags also
	memory_ptr<SPropValue> lpPropFlags;
	auto hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpPropFlags);
	if (hr != hrSuccess)
		return hr;
    hr = HrGetRealProp(PR_MESSAGE_FLAGS, ulFlags, lpPropFlags, lpPropFlags);
    if(hr != hrSuccess)
		return hr;
	if (ulFlags & CLEAR_READ_FLAG)
		lpPropFlags->Value.ul &= ~MSGFLAG_READ;
	else
		lpPropFlags->Value.ul |= MSGFLAG_READ;

	return HrSetRealProp(lpPropFlags);
}

/**
 * Synchronizes this object's PR_DISPLAY_* properties from the
 * contents of the recipient table. They are pushed to the server
 * on save.
 */
HRESULT ECMessage::SyncRecips()
{
	std::wstring wstrTo, wstrCc, wstrBcc;
	SPropValue sPropRecip;
	object_ptr<IMAPITable> lpTable;
	static constexpr const SizedSPropTagArray(2, sPropDisplay) =
		{2, {PR_RECIPIENT_TYPE, PR_DISPLAY_NAME_W}};

	if (lpRecips == nullptr) {
		m_bRecipsDirty = false;
		return hrSuccess;
	}
	auto hr = GetRecipientTable(fMapiUnicode, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SetColumns(sPropDisplay, 0);

	while (TRUE) {
		rowset_ptr lpRows;
		hr = lpTable->QueryRows(1, 0, &~lpRows);
		if (hr != hrSuccess || lpRows->cRows != 1)
			break;

		if (lpRows[0].lpProps[0].ulPropTag == PR_RECIPIENT_TYPE &&
		    lpRows[0].lpProps[0].Value.ul == MAPI_TO) {
			if (lpRows[0].lpProps[1].ulPropTag != PR_DISPLAY_NAME_W)
				continue;
			if (wstrTo.length() > 0)
				wstrTo += L"; ";
			wstrTo += lpRows[0].lpProps[1].Value.lpszW;
			continue;
		} else if (lpRows[0].lpProps[0].ulPropTag == PR_RECIPIENT_TYPE &&
		    lpRows[0].lpProps[0].Value.ul == MAPI_CC) {
			if (lpRows[0].lpProps[1].ulPropTag != PR_DISPLAY_NAME_W)
				continue;
			if (wstrCc.length() > 0)
				wstrCc += L"; ";
			wstrCc += lpRows[0].lpProps[1].Value.lpszW;
			continue;
		} else if (lpRows[0].lpProps[0].ulPropTag == PR_RECIPIENT_TYPE &&
		    lpRows[0].lpProps[0].Value.ul == MAPI_BCC) {
			if (lpRows[0].lpProps[1].ulPropTag != PR_DISPLAY_NAME_W)
				continue;
			if (wstrBcc.length() > 0)
				wstrBcc += L"; ";
			wstrBcc += lpRows[0].lpProps[1].Value.lpszW;
		}
	}

	sPropRecip.ulPropTag = PR_DISPLAY_TO_W;
	sPropRecip.Value.lpszW = const_cast<wchar_t *>(wstrTo.c_str());
	HrSetRealProp(&sPropRecip);

	sPropRecip.ulPropTag = PR_DISPLAY_CC_W;
	sPropRecip.Value.lpszW = const_cast<wchar_t *>(wstrCc.c_str());
	HrSetRealProp(&sPropRecip);

	sPropRecip.ulPropTag = PR_DISPLAY_BCC_W;
	sPropRecip.Value.lpszW = const_cast<wchar_t *>(wstrBcc.c_str());
	HrSetRealProp(&sPropRecip);

	m_bRecipsDirty = FALSE;
	return hr;
}

HRESULT ECMessage::SaveRecips()
{
	rowset_ptr lpRowSet;
	ecmem_ptr<SPropValue> lpObjIDs;
	ecmem_ptr<ULONG> lpulStatus;
	ULONG				ulRealObjType;
	scoped_rlock lock(m_hMutexMAPIObject);

	// Get any changes and set it in the child list of this message
	auto hr = lpRecips->HrGetAllWithStatus(&~lpRowSet, &~lpObjIDs, &~lpulStatus);
	if (hr != hrSuccess)
		return hr;

	for (unsigned int i = 0; i < lpRowSet->cRows; ++i) {
		// Get the right object type for a DistList
		auto lpObjType = lpRowSet[i].cfind(PR_OBJECT_TYPE);
		if(lpObjType != NULL)
			ulRealObjType = lpObjType->Value.ul; // MAPI_MAILUSER or MAPI_DISTLIST
		else
			ulRealObjType = MAPI_MAILUSER; // add in list?

		auto lpRowId = lpRowSet[i].cfind(PR_ROWID); /* unique value of recipient */
		if (!lpRowId) {
			assert(lpRowId != NULL);
			continue;
		}

		auto mo = new MAPIOBJECT(lpRowId->Value.ul, lpObjIDs[i].Value.ul, ulRealObjType);
		// Move any PR_ENTRYIDs to PR_EC_CONTACT_ENTRYID
		auto lpEntryID = lpRowSet[i].find(PR_ENTRYID);
		if(lpEntryID)
			lpEntryID->ulPropTag = PR_EC_CONTACT_ENTRYID;

		if (lpulStatus[i] == ECROW_MODIFIED || lpulStatus[i] == ECROW_ADDED) {
			mo->bChanged = true;
			for (unsigned int j = 0; j < lpRowSet[i].cValues; ++j)
				if (PROP_TYPE(lpRowSet[i].lpProps[j].ulPropTag) != PT_NULL) {
					mo->lstModified.emplace_back(&lpRowSet[i].lpProps[j]);
					// as in ECGenericProp.cpp, we also save the properties to the known list,
					// since this is used when we reload the object from memory.
					mo->lstProperties.emplace_back(&lpRowSet[i].lpProps[j]);
				}
		} else if (lpulStatus[i] == ECROW_DELETED) {
			mo->bDelete = true;
		} else {
			// ECROW_NORMAL, untouched recipient
			for (unsigned int j = 0; j < lpRowSet[i].cValues; ++j)
				if(PROP_TYPE(lpRowSet[i].lpProps[j].ulPropTag) != PT_NULL)
					mo->lstProperties.emplace_back(&lpRowSet[i].lpProps[j]);
		}

		// find old recipient in child list, and remove if present
		auto iterSObj = m_sMapiObject->lstChildren.find(mo);
		if (iterSObj != m_sMapiObject->lstChildren.cend()) {
			delete *iterSObj;
			m_sMapiObject->lstChildren.erase(iterSObj);
		}
		m_sMapiObject->lstChildren.emplace(mo);
	}
	return lpRecips->HrSetClean();
}

void ECMessage::RecursiveMarkDelete(MAPIOBJECT *lpObj) {
	lpObj->bDelete = true;
	lpObj->lstDeleted.clear();
	lpObj->lstAvailable.clear();
	lpObj->lstModified.clear();
	lpObj->lstProperties.clear();
	for (const auto &obj : lpObj->lstChildren)
		RecursiveMarkDelete(obj);
}

BOOL ECMessage::HasAttachment()
{
	ECMapiObjects::const_iterator iterObjects;
	scoped_rlock lock(m_hMutexMAPIObject);

	if (!m_props_loaded) {
		auto hr = HrLoadProps();
		if (hr != hrSuccess)
			return false; /* hr */
	}
	for (iterObjects = m_sMapiObject->lstChildren.cbegin();
	     iterObjects != m_sMapiObject->lstChildren.cend(); ++iterObjects)
		if ((*iterObjects)->ulObjType == MAPI_ATTACH)
			break;
	return iterObjects != m_sMapiObject->lstChildren.cend();
}

// Syncs the Attachment table to the child list in the saved object
HRESULT ECMessage::SyncAttachments()
{
	rowset_ptr lpRowSet;
	ecmem_ptr<SPropValue> lpObjIDs;
	ecmem_ptr<ULONG> lpulStatus;
	scoped_rlock lock(m_hMutexMAPIObject);

	// Get any changes and set it in the child list of this message
	// Although we only need to know the deleted attachments, I also need to know the PR_ATTACH_NUM, which is in the rowset
	auto hr = lpAttachments->HrGetAllWithStatus(&~lpRowSet, &~lpObjIDs, &~lpulStatus);
	if (hr != hrSuccess)
		return hr;

	for (unsigned int i = 0; i < lpRowSet->cRows; ++i) {
		if (lpulStatus[i] != ECROW_DELETED)
			continue;
		auto lpObjType = lpRowSet[i].cfind(PR_OBJECT_TYPE);
		if(lpObjType == NULL || lpObjType->Value.ul != MAPI_ATTACH)
			continue;
		auto lpAttachNum = lpRowSet[i].cfind(PR_ATTACH_NUM); /* unique value of attachment */
		if (!lpAttachNum) {
			assert(lpAttachNum != NULL);
			continue;
		}

		// delete complete attachment
		MAPIOBJECT find(lpObjType->Value.ul, lpAttachNum->Value.ul);
		auto iterSObj = m_sMapiObject->lstChildren.find(&find);
		if (iterSObj != m_sMapiObject->lstChildren.cend())
			RecursiveMarkDelete(*iterSObj);
	}

	return lpAttachments->HrSetClean();
}

HRESULT ECMessage::UpdateTable(ECMemTable *lpTable, ULONG objtype,
    ULONG ulObjKeyProp)
{
	SPropValue sKeyProp, sUniqueProp;
	unsigned int cAllValues = 0, cValues = 0;
	scoped_rlock lock(m_hMutexMAPIObject);

	if (m_sMapiObject == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// update hierarchy id in table
	for (const auto &obj : m_sMapiObject->lstChildren) {
		memory_ptr<SPropValue> lpProps, lpNewProps, lpAllProps;

		if (obj->ulObjType != objtype)
			continue;
		sUniqueProp.ulPropTag = ulObjKeyProp;
		sUniqueProp.Value.ul = obj->ulUniqueId;
		sKeyProp.ulPropTag = PR_EC_HIERARCHYID;
		sKeyProp.Value.ul = obj->ulObjId;
		auto hr = lpTable->HrUpdateRowID(&sKeyProp, &sUniqueProp, 1);
		if (hr != hrSuccess)
			return hr;
		// put new server props in table too
		unsigned int ulProps = obj->lstProperties.size();
		if (ulProps == 0)
			continue;
		// retrieve old row from table
		hr = lpTable->HrGetRowData(&sUniqueProp, &cValues, &~lpProps);
		if (hr != hrSuccess)
			return hr;
		// add new props
		hr = MAPIAllocateBuffer(sizeof(SPropValue) * ulProps, &~lpNewProps);
		if (hr != hrSuccess)
			return hr;
		unsigned int i = 0;
		for (const auto &pv : obj->lstProperties) {
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

		hr = Util::HrMergePropertyArrays(lpProps, cValues, lpNewProps, ulProps, &~lpAllProps, &cAllValues);
		if (hr != hrSuccess)
			return hr;
		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_MODIFY, &sKeyProp, lpAllProps, cAllValues);
		if (hr != hrSuccess)
			return hr;
	}
	return lpTable->HrSetClean();
}

HRESULT ECMessage::SaveChanges(ULONG ulFlags)
{
	ecmem_ptr<SPropValue> lpsPropMessageFlags;
	ULONG				cValues = 0;
	scoped_rlock lock(m_hMutexMAPIObject);

	// could not have modified (easy way out of my bug)
	if (!fModify)
		return MAPI_E_NO_ACCESS;
	// nothing changed -> no need to save
	if (!m_props_loaded)
		return hrSuccess;

	assert(m_sMapiObject != NULL); // the actual bug .. keep open on submessage
	if (lpRecips != nullptr) {
		auto hr = SaveRecips();
		if (hr != hrSuccess)
			return hr;
		// Synchronize PR_DISPLAY_* ... FIXME should we do this after each ModifyRecipients ?
		SyncRecips();
	}
	if (lpAttachments != nullptr) {
		// set deleted attachments in saved child list
		auto hr = SyncAttachments();
		if (hr != hrSuccess)
			return hr;
	}

	// Property change of a new item
	if (fNew && GetMsgStore()->IsSpooler()) {
		static constexpr const SizedSPropTagArray(1, proptag) = {1, {PR_MESSAGE_FLAGS}};
		auto hr = ECMAPIProp::GetProps(proptag, 0, &cValues, &~lpsPropMessageFlags);
		if(hr != hrSuccess)
			return hr;
		lpsPropMessageFlags->ulPropTag = PR_MESSAGE_FLAGS;
		lpsPropMessageFlags->Value.l &= ~(MSGFLAG_READ|MSGFLAG_UNSENT);
		lpsPropMessageFlags->Value.l |= MSGFLAG_UNMODIFIED;

		hr = SetProps(1, lpsPropMessageFlags, NULL);
		if(hr != hrSuccess)
			return hr;
	}

	// don't re-sync bodies that are returned from server
	assert(!m_bInhibitSync);
	m_bInhibitSync = TRUE;
	auto hr = ECMAPIProp::SaveChanges(ulFlags);
	m_bInhibitSync = FALSE;
	m_bExplicitSubjectPrefix = FALSE;

	if(hr != hrSuccess)
		return hr;
	if (m_sMapiObject == nullptr || m_bEmbedded)
		return hrSuccess;
	// resync recip and attachment table, because of hierarchy IDs, only on actual saved object
	if (lpRecips) {
		hr = UpdateTable(lpRecips, MAPI_MAILUSER, PR_ROWID);
		if (hr != hrSuccess)
			return hr;
		hr = UpdateTable(lpRecips, MAPI_DISTLIST, PR_ROWID);
		if (hr != hrSuccess)
			return hr;
	}
	if (lpAttachments) {
		hr = UpdateTable(lpAttachments, MAPI_ATTACH, PR_ATTACH_NUM);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

/**
 * Sync PR_SUBJECT, PR_SUBJECT_PREFIX
 */
HRESULT ECMessage::SyncSubject()
{
	BOOL bDirtySubject = false, bDirtySubjectPrefix = false;
	ecmem_ptr<SPropValue> lpPropArray;
	ULONG			cValues = 0;
	wchar_t *lpszColon = nullptr, *lpszEnd = nullptr;
	int				sizePrefix1 = 0;
	static constexpr const SizedSPropTagArray(2, sPropSubjects) =
		{2, {PR_SUBJECT_W, PR_SUBJECT_PREFIX_W}};
	unsigned int hr1 = IsPropDirty(CHANGE_PROP_TYPE(PR_SUBJECT, PT_UNSPECIFIED), &bDirtySubject);
	unsigned int hr2 = IsPropDirty(CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED), &bDirtySubjectPrefix);

	// if both not present or not dirty
	if( (hr1 != hrSuccess && hr2 != hrSuccess) || (hr1 == hr2 && bDirtySubject == FALSE && bDirtySubjectPrefix == FALSE) )
		return hrSuccess;
	// If subject is deleted but the prefix is not, delete it
	if(hr1 != hrSuccess && hr2 == hrSuccess)
		return HrDeleteRealProp(CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED), FALSE);

	// Check if subject and prefix in sync
	auto hr = ECMAPIProp::GetProps(sPropSubjects, 0, &cValues, &~lpPropArray);
	if(HR_FAILED(hr))
		return hr;
	if(lpPropArray[0].ulPropTag == PR_SUBJECT_W)
		lpszColon = wcschr(lpPropArray[0].Value.lpszW, L':');

	if(lpszColon == NULL) {
		//Set empty PR_SUBJECT_PREFIX
		lpPropArray[1].ulPropTag = PR_SUBJECT_PREFIX_W;
		lpPropArray[1].Value.lpszW = const_cast<wchar_t *>(L"");
		return HrSetRealProp(&lpPropArray[1]);
	}

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
		lpPropArray[1].Value.lpszW = const_cast<wchar_t *>(L""); // empty PR_SUBJECT_PREFIX

	return HrSetRealProp(&lpPropArray[1]);
	// PR_SUBJECT_PREFIX and PR_SUBJECT are synchronized
}

// Override IMAPIProp::SetProps
HRESULT ECMessage::SetProps(ULONG cValues, const SPropValue *lpPropArray,
    SPropProblemArray **lppProblems)
{
	const BOOL bInhibitSyncOld = m_bInhibitSync;
	m_bInhibitSync = TRUE; // We want to override the logic in ECMessage::HrSetRealProp.

	auto laters = make_scope_success([&]() { m_bInhibitSync = bInhibitSyncOld; });

	// Send to IMAPIProp first
	auto hr = ECMAPIProp::SetProps(cValues, lpPropArray, lppProblems);
	if (hr != hrSuccess)
		return hr;

	m_bInhibitSync = bInhibitSyncOld;

	/* We only sync the subject (like a PST does) in the following conditions:
	 * 1) PR_SUBJECT is modified, and PR_SUBJECT_PREFIX was not set
	 * 2) PR_SUBJECT is modified, and PR_SUBJECT_PREFIX was modified by a previous SyncSubject() call
	 * If the caller ever does a SetProps on the PR_SUBJECT_PREFIX itself, we must never touch it ourselves again, until SaveChanges().
	 */
	auto pvalSubject = PCpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_SUBJECT, PT_UNSPECIFIED));
	auto pvalSubjectPrefix = PCpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED));
	if (pvalSubjectPrefix)
		m_bExplicitSubjectPrefix = TRUE;
	if (pvalSubject && m_bExplicitSubjectPrefix == FALSE)
		SyncSubject();

	// Now, sync RTF
	auto pvalRtf  = PCpropFindProp(lpPropArray, cValues, PR_RTF_COMPRESSED);
	auto pvalHtml = PCpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_BODY_HTML, PT_UNSPECIFIED));
	auto pvalBody = PCpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_BODY, PT_UNSPECIFIED));

	// IF the user sets both the body and the RTF, assume RTF overrides
	if (pvalRtf) {
		m_ulBodyType = bodyTypeUnknown; // Make sure GetBodyType doesn't use the cached value
		std::string rtf;
		hr = GetRtfData(&rtf);
		if (hr == hrSuccess) {
			hr = GetBodyType(rtf, &m_ulBodyType);
			if (hr == hrSuccess)
				SyncRtf(rtf);
		}
	} else if (pvalHtml) {
		m_ulBodyType = bodyTypeHTML;
		SyncHtmlToPlain();
		HrDeleteRealProp(PR_RTF_COMPRESSED, FALSE);
	} else if(pvalBody) {
		m_ulBodyType = bodyTypePlain;
		HrDeleteRealProp(PR_RTF_COMPRESSED, FALSE);
		HrDeleteRealProp(PR_HTML, FALSE);
	}
	return hrSuccess;
}

HRESULT ECMessage::DeleteProps(const SPropTagArray *lpPropTagArray,
    SPropProblemArray **lppProblems)
{
	SizedSPropTagArray(1, sSubjectPrefix) =
		{1, {CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED)}};

	// Send to IMAPIProp first
	auto hr = ECMAPIProp::DeleteProps(lpPropTagArray, lppProblems);
	if (FAILED(hr))
		return hr;

	// If the PR_SUBJECT is removed and we generated the prefix, we need to remove that property too.
	if (m_bExplicitSubjectPrefix == FALSE && Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_SUBJECT, PT_UNSPECIFIED)) >= 0)
		ECMAPIProp::DeleteProps(sSubjectPrefix, NULL);

	// If an explicit prefix was set and now removed, we must sync it again on the next SetProps of the subject
	if (m_bExplicitSubjectPrefix == TRUE && Util::FindPropInArray(lpPropTagArray, CHANGE_PROP_TYPE(PR_SUBJECT_PREFIX, PT_UNSPECIFIED)) >= 0)
		m_bExplicitSubjectPrefix = FALSE;

	return hrSuccess;
}

HRESULT ECMessage::TableRowGetProp(void *lpProvider,
    const struct propVal *lpsPropValSrc, SPropValue *lpsPropValDst,
    void **lpBase, ULONG ulType)
{
	auto lpMsgStore = static_cast<ECMsgStore *>(lpProvider);

	if (lpsPropValSrc->ulPropTag != PR_SOURCE_KEY)
		return MAPI_E_NOT_FOUND;
	if ((lpMsgStore->m_ulProfileFlags & EC_PROFILE_FLAGS_TRUNCATE_SOURCEKEY) &&
	    lpsPropValSrc->Value.bin->__size > 22) {
		lpsPropValSrc->Value.bin->__size = 22;
		lpsPropValSrc->Value.bin->__ptr[lpsPropValSrc->Value.bin->__size-1] |= 0x80; // Set top bit
		return CopySOAPPropValToMAPIPropVal(lpsPropValDst, lpsPropValSrc, lpBase);
	}
	return MAPI_E_NOT_FOUND;
}

HRESULT ECMessage::GetPropHandler(unsigned int ulPropTag, void *lpProvider,
    unsigned int ulFlags, SPropValue *lpsPropValue, ECGenericProp *lpParam,
    void *lpBase)
{
	HRESULT hr = hrSuccess;
	LPBYTE	lpData = NULL;
	auto lpMessage = static_cast<ECMessage *>(lpParam);

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
	case PROP_ID(PR_NORMALIZED_SUBJECT): {
		hr = lpMessage->HrGetRealProp(CHANGE_PROP_TYPE(PR_SUBJECT, PROP_TYPE(ulPropTag)), ulFlags, lpBase, lpsPropValue);
		if (hr != hrSuccess) {
			// change PR_SUBJECT in PR_NORMALIZED_SUBJECT
			lpsPropValue->ulPropTag = CHANGE_PROP_TYPE(PR_NORMALIZED_SUBJECT, PT_ERROR);
			break;
		}

		if (PROP_TYPE(ulPropTag) == PT_UNICODE) {
			lpsPropValue->ulPropTag = PR_NORMALIZED_SUBJECT_W;

			auto lpszColon = wcschr(lpsPropValue->Value.lpszW, ':');
			if (lpszColon && (lpszColon - lpsPropValue->Value.lpszW) > 1 && (lpszColon - lpsPropValue->Value.lpszW) < 4) {
				auto c = lpsPropValue->Value.lpszW;
				while (c < lpszColon && iswdigit(*c))
					++c; // test for all digits prefix
				if (c != lpszColon) {
					++lpszColon;
					if (*lpszColon == ' ')
						++lpszColon;
					lpsPropValue->Value.lpszW = lpszColon; // set new subject string
				}
			}
			break;
		}
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
		break;
	}
	case PROP_ID(PR_PARENT_ENTRYID):
		if (lpMessage->m_lpParentID == nullptr) {
			hr = lpMessage->HrGetRealProp(PR_PARENT_ENTRYID, ulFlags, lpBase, lpsPropValue);
			break;
		}
		lpsPropValue->ulPropTag = PR_PARENT_ENTRYID;
		lpsPropValue->Value.bin.cb = lpMessage->m_cbParentID;
		hr = ECAllocateMore(lpsPropValue->Value.bin.cb, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
		if (hr != hrSuccess)
			break;
		memcpy(lpsPropValue->Value.bin.lpb, lpMessage->m_lpParentID, lpsPropValue->Value.bin.cb);
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
		if ((!lpMessage->m_bRecipsDirty || lpMessage->SyncRecips() == erSuccess) &&
		    lpMessage->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue) == erSuccess)
			break;
		lpsPropValue->ulPropTag = ulPropTag;
		if (PROP_TYPE(ulPropTag) == PT_UNICODE)
			lpsPropValue->Value.lpszW = const_cast<wchar_t *>(L"");
		else
			lpsPropValue->Value.lpszA = const_cast<char *>("");
		break;

	case PROP_ID(PR_ACCESS):
		if (lpMessage->HrGetRealProp(PR_ACCESS, ulFlags, lpBase, lpsPropValue) == hrSuccess)
			break;
		lpsPropValue->ulPropTag = PR_ACCESS;
		lpsPropValue->Value.l = MAPI_ACCESS_READ | MAPI_ACCESS_MODIFY | MAPI_ACCESS_DELETE;
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
#ifdef HAVE_TIDYBUFFIO_H
		// does it already exist? (e.g. inserted by dagent/gateway)
		hr = lpMessage->HrGetRealProp(PR_EC_BODY_FILTERED, ulFlags, lpBase, lpsPropValue);
		if (hr == hrSuccess) // yes, then use that
			break;
		const char *codepage;
		hr = lpMessage->HrGetRealProp(PR_INTERNET_CPID, ulFlags, lpBase, lpsPropValue);
		if (hr != hrSuccess || HrGetCharsetByCP(lpsPropValue->Value.ul, &codepage) != hrSuccess)
			codepage = "iso-8859-15"; /* [MS-OXCMAIL] §2.1.3.3.1 */

		lpsPropValue->ulPropTag = PR_EC_BODY_FILTERED;
		/* generate it on the fly */
		memory_ptr<SPropValue> tprop;
		hr = MAPIAllocateBuffer(sizeof(SPropValue), &~tprop);
		if (hr != hrSuccess)
			break;
		hr = lpMessage->GetSyncedBodyProp(PR_HTML, ulFlags, tprop, tprop);
		if (hr != hrSuccess) {
			hr = MAPI_E_NOT_FOUND;
			break;
		}

		std::string result, fltblk = convert_to<std::string>("UTF-8", reinterpret_cast<const char *>(tprop->Value.bin.lpb), tprop->Value.bin.cb, codepage);
		std::vector<std::string> errors;
		bool rc = rosie_clean_html(fltblk, &result, &errors);

		// FIXME emit error somewhere somehow
		if (rc) {
			result = convert_to<std::string>((codepage + std::string("//IGNORE")).c_str(), result.c_str(), result.size(), "UTF-8");
			ULONG ulSize = result.size();

			hr = ECAllocateMore(ulSize + 1, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
			if (hr == hrSuccess) {
				memcpy(lpsPropValue->Value.bin.lpb, result.c_str(), ulSize);
				lpsPropValue->Value.bin.lpb[ulSize] = '\0';
				// FIXME store in database if that is what the SysOp wants
			} else {
				ulSize = 0;
			}
			lpsPropValue->Value.bin.cb = ulSize;
		}
		if (rc == 0 || hr != hrSuccess) {
			hr = MAPI_E_NOT_FOUND;
			break;
		}
		break;
#else
		hr = MAPI_E_NO_SUPPORT;
		break;
#endif
	}
	case PROP_ID(PR_BODY):
	case PROP_ID(PR_RTF_COMPRESSED):
	case PROP_ID(PR_HTML): {
		hr = lpMessage->GetSyncedBodyProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
		if (hr != hrSuccess)
			break;
		if (ulPropTag != PR_BODY_HTML)
			break;
		// Workaround for support html in outlook 2000/xp
		if (lpsPropValue->ulPropTag != PR_HTML) {
			hr = MAPI_E_NOT_FOUND;
			break;
		}

		lpsPropValue->ulPropTag = PR_BODY_HTML;
		unsigned int ulSize = lpsPropValue->Value.bin.cb;
		lpData = lpsPropValue->Value.bin.lpb;
		hr = ECAllocateMore(ulSize + 1, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.lpszA));
		if (hr != hrSuccess)
			break;
		if (ulSize > 0 && lpData != nullptr)
			memcpy(lpsPropValue->Value.lpszA, lpData, ulSize);
		else
			ulSize = 0;
		lpsPropValue->Value.lpszA[ulSize] = 0;
		break;
	}
	case PROP_ID(PR_SOURCE_KEY): {
		std::string strServerGUID, strID, strSourceKey;
		if(ECMAPIProp::DefaultMAPIGetProp(PR_SOURCE_KEY, lpProvider, ulFlags, lpsPropValue, lpParam, lpBase) == hrSuccess)
			return hr;

		// The server did not supply a PR_SOURCE_KEY, generate one ourselves.
		strServerGUID.assign((char*)&lpMessage->GetMsgStore()->GetStoreGuid(), sizeof(GUID));
		if (lpMessage->m_sMapiObject != nullptr) {
			uint32_t tmp4 = cpu_to_le32(lpMessage->m_sMapiObject->ulObjId);
			strID.assign(reinterpret_cast<const char *>(&tmp4), sizeof(tmp4));
		}

		// Resize so it trails 6 null bytes
		strID.resize(6,0);
		strSourceKey = strServerGUID + strID;
		lpsPropValue->ulPropTag = PR_SOURCE_KEY;
		lpsPropValue->Value.bin.cb = strSourceKey.size();
		return KAllocCopy(strSourceKey.c_str(), strSourceKey.size(), reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb), lpBase);
	}
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}
	return hr;
}

HRESULT ECMessage::SetPropHandler(unsigned int ulPropTag, void *lpProvider,
    const SPropValue *lpsPropValue, ECGenericProp *lpParam)
{
	auto lpMessage = static_cast<ECMessage *>(lpParam);
	HRESULT hr = hrSuccess;

	switch(ulPropTag) {
	case PR_MESSAGE_SIZE:
		/*
		 * Accept manipulation of the message size is only accepted
		 * while the message has not yet been saved. When parsing the
		 * RFC2822 message, SetProp(PR_MESSAGE_SIZE) is called since
		 * the message size needs to be known during rules processing.
		 * Whenever the message is saved, the size will be recomputed
		 * (including properties, etc.), therefore modifications will
		 * not be accepted.
		 */
		if (lpMessage->fNew)
			hr = lpMessage->HrSetRealProp(lpsPropValue);
		else
			hr = MAPI_E_COMPUTED;
		break;
	case PR_HTML:
		hr = lpMessage->HrSetRealProp(lpsPropValue);
		break;
	case PR_BODY_HTML: {
		// Set PR_BODY_HTML to PR_HTML
		SPropValue copy = *lpsPropValue;
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
HRESULT ECMessage::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	if (lpInterface == nullptr || lpDestObj == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<IMAPIProp> destiprop;
	object_ptr<ECMAPIProp> lpECMAPIProp;
	memory_ptr<SPropValue> lpECObject;
	GUID sDestServerGuid = {0}, sSourceServerGuid = {0};

	// Deny copying within the same object. This is not allowed in exchange either and is required to deny
	// creating large recursive objects.
	if (qi_void_to_imapiprop(lpDestObj, *lpInterface, &~destiprop) == hrSuccess &&
	    GetECObject(destiprop, iid_of(lpECMAPIProp), &~lpECMAPIProp) == hrSuccess) {
		// Find the top-level objects for both source and destination objects
		auto lpDestTop = lpECMAPIProp->m_lpRoot;
		auto lpSourceTop = m_lpRoot;

		// destination may not be a child of the source, but source can be a child of destination
		if (!IsChildOf(lpDestTop)) {
			// ICS expects the entryids to be equal. So check if the objects reside on
			// the same server as well.
			auto hr = lpDestTop->GetMsgStore()->lpTransport->GetServerGUID(&sDestServerGuid);
			if (hr != hrSuccess)
				return hr;
			hr = lpSourceTop->GetMsgStore()->lpTransport->GetServerGUID(&sSourceServerGuid);
			if (hr != hrSuccess)
				return hr;
			if(lpDestTop->m_lpEntryId && lpSourceTop->m_lpEntryId &&
			   lpDestTop->m_cbEntryId == lpSourceTop->m_cbEntryId &&
			   memcmp(lpDestTop->m_lpEntryId, lpSourceTop->m_lpEntryId, lpDestTop->m_cbEntryId) == 0 &&
			   sDestServerGuid == sSourceServerGuid)
				// Source and destination are the same on-disk objects (entryids are equal)
				return MAPI_E_NO_ACCESS;
		}
	}
	return Util::DoCopyTo(&IID_IMessage, static_cast<IMessage *>(this), ciidExclude,
	       rgiidExclude, lpExcludeProps, ulUIParam, lpProgress,
	       lpInterface, lpDestObj, ulFlags, lppProblems);
}

// We override HrLoadProps to setup PR_BODY and PR_RTF_COMPRESSED in the initial message
// Normally, this should never be needed, as messages should store both the PR_BODY as the PR_RTF_COMPRESSED
// when saving.
HRESULT ECMessage::HrLoadProps()
{
	ecmem_ptr<SPropValue> lpsBodyProps;
	static constexpr const SizedSPropTagArray(3, sPropBodyTags) =
		{3, {PR_BODY_W, PR_RTF_COMPRESSED, PR_HTML}};
	ULONG cValues = 0;
	BOOL fBodyOK = false, fRTFOK = false, fHTMLOK = false;

	m_bInhibitSync = TRUE; // We don't want the logic in ECMessage::HrSetRealProp to kick in yet.
	auto hr = ECMAPIProp::HrLoadProps();
	m_bInhibitSync = FALSE;

	if (hr != hrSuccess)
		return hr;
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
	hr = ECMAPIProp::GetProps(sPropBodyTags, 0, &cValues, &~lpsBodyProps);
	if (HR_FAILED(hr))
		return hr;

	hr = hrSuccess;

	if (lpsBodyProps[0].ulPropTag == PR_BODY_W ||
	    (lpsBodyProps[0].ulPropTag == CHANGE_PROP_TYPE(PR_BODY, PT_ERROR) &&
	    lpsBodyProps[0].Value.err == MAPI_E_NOT_ENOUGH_MEMORY))
		fBodyOK = TRUE;
	if (lpsBodyProps[1].ulPropTag == PR_RTF_COMPRESSED ||
	    (lpsBodyProps[1].ulPropTag == CHANGE_PROP_TYPE(PR_RTF_COMPRESSED, PT_ERROR) &&
	    lpsBodyProps[1].Value.err == MAPI_E_NOT_ENOUGH_MEMORY))
		fRTFOK = TRUE;
	if (lpsBodyProps[2].ulPropTag == PR_HTML ||
	    (lpsBodyProps[2].ulPropTag == CHANGE_PROP_TYPE(PR_HTML, PT_ERROR) &&
	    lpsBodyProps[2].Value.err == MAPI_E_NOT_ENOUGH_MEMORY))
		fHTMLOK = TRUE;

	if (fRTFOK) {
		std::string rtf;
		auto hrTmp = GetRtfData(&rtf);
		if (hrTmp != hrSuccess) {
			kc_pwarn("GetBestBody: GetRtfData", hrTmp);
		} else {
			hrTmp = GetBodyType(rtf, &m_ulBodyType);
			if (FAILED(hrTmp)) {
				// e.g. this fails then RTF property is present but empty
				kc_pwarn("GetBestBody: Unable to determine body type based on RTF data", hrTmp);
			} else if ((m_ulBodyType == bodyTypePlain && !fBodyOK) ||
			    (m_ulBodyType == bodyTypeHTML && !fHTMLOK)) {
				hr = SyncRtf(rtf);
				if (hr != hrSuccess)
					return hr;
			}
		}
	}

	if (m_ulBodyType != bodyTypeUnknown)
		return hrSuccess;

	// We get here if there was no RTF data or when determining the body type based
	// on that data failed.
	if (fHTMLOK)
		m_ulBodyType = bodyTypeHTML;
	else if (fBodyOK)
		m_ulBodyType = bodyTypePlain;
	return hrSuccess;
}

HRESULT ECMessage::HrSetRealProp(const SPropValue *lpsPropValue)
{
	auto hr = ECMAPIProp::HrSetRealProp(lpsPropValue);
	if(hr != hrSuccess)
		return hr;
	// If we're in the middle of syncing bodies, we don't want any more logic to kick in.
	if (m_bInhibitSync)
		return hrSuccess;

	if (lpsPropValue->ulPropTag == PR_RTF_COMPRESSED) {
		m_ulBodyType = bodyTypeUnknown; // Make sure GetBodyType doesn't use the cached value
		std::string rtf;
		hr = GetRtfData(&rtf);
		if (hr == hrSuccess) {
			hr = GetBodyType(rtf, &m_ulBodyType);
			if (hr == hrSuccess)
				SyncRtf(rtf);
		}
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
	unsigned int m_ulUniqueId, m_ulObjType;

    findobject_if(unsigned int ulObjType, unsigned int ulUniqueId) : m_ulUniqueId(ulUniqueId), m_ulObjType(ulObjType) {}

    bool operator()(const MAPIOBJECT *entry) const
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

	for (const auto &src : lpSrc->lstChildren) {
		auto iterDest = lpDest->lstChildren.find(src);
		if (iterDest != lpDest->lstChildren.cend()) {
			hr = HrCopyObjIDs(*iterDest, src);
            if(hr != hrSuccess)
                return hr;
        }
    }
    return hrSuccess;
}

HRESULT ECMessage::HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject) {
	if (lpsMapiObject->ulObjType != MAPI_ATTACH)
		/*
		 * Can only save attachments as child objects. (Recipients are
		 * saved through SaveRecips() from SaveChanges() on this
		 * object.)
		 */
		return MAPI_E_INVALID_OBJECT;

	SPropValue sKeyProp;
	ecmem_ptr<SPropValue> lpProps;
	scoped_rlock lock(m_hMutexMAPIObject);

	if (lpAttachments == nullptr) {
		object_ptr<IMAPITable> lpTable;
		auto hr = GetAttachmentTable(fMapiUnicode, &~lpTable);
		if(hr != hrSuccess)
			return hr;
	}
	if (lpAttachments == nullptr)
		return MAPI_E_CALL_FAILED;
	if (!m_sMapiObject) {
		// when does this happen? .. just a simple precaution for now
		assert(m_sMapiObject != NULL);
		return MAPI_E_NOT_FOUND;
	}

	// Replace the attachment in the object hierarchy with this one, but preserve server object id. This is needed
	// if the entire object has been saved to the server in the mean time.
	auto iterSObj = m_sMapiObject->lstChildren.find(lpsMapiObject);
	if (iterSObj != m_sMapiObject->lstChildren.cend()) {
		// Preserve server IDs
		auto hr = HrCopyObjIDs(lpsMapiObject, (*iterSObj));
		if(hr != hrSuccess)
			return hr;
		// Remove item
		delete *iterSObj;
		m_sMapiObject->lstChildren.erase(iterSObj);
	}

	m_sMapiObject->lstChildren.emplace(new MAPIOBJECT(*lpsMapiObject));
	// Update the attachment table. The attachment table contains all properties of the attachments
	unsigned int i = 0, ulProps = lpsMapiObject->lstProperties.size();

	// +2 for maybe missing PR_ATTACH_NUM and PR_OBJECT_TYPE properties
	auto hr = ECAllocateBuffer(sizeof(SPropValue) * (ulProps + 2), &~lpProps);
	if (hr != hrSuccess)
		return hr;

	SPropValue *lpPropID = nullptr, *lpPropObjType = nullptr;
	for (const auto &pv : lpsMapiObject->lstProperties) {
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
	return lpAttachments->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &sKeyProp, lpProps, ulProps);
}

HRESULT ECMessage::GetBodyType(const std::string &rtf, eBodyType *lpulBodyType)
{
	if (m_ulBodyType != bodyTypeUnknown) {
		*lpulBodyType = m_ulBodyType;
		return hrSuccess;
	}
	if (isrtftext(rtf.c_str(), rtf.size()))
		m_ulBodyType = bodyTypePlain;
	else if (isrtfhtml(rtf.c_str(), rtf.size()))
		m_ulBodyType = bodyTypeHTML;
	else
		m_ulBodyType = bodyTypeRTF;
	*lpulBodyType = m_ulBodyType;
	return hrSuccess;
}

HRESULT ECMessage::GetRtfData(std::string *lpstrRtfData)
{
	StreamPtr ptrRtfCompressedStream, ptrRtfUncompressedStream;
	char lpBuf[4096];
	std::string strRtfData;

	auto hr = OpenProperty(PR_RTF_COMPRESSED, &IID_IStream, 0, 0, &~ptrRtfCompressedStream);
	if (hr != hrSuccess)
		return hr;

	// Read the RTF stream
	hr = WrapCompressedRTFStream(ptrRtfCompressedStream, 0, &~ptrRtfUncompressedStream);
	if(hr != hrSuccess)
	{
		object_ptr<ECMemStream> ptrEmptyMemStream;
		// Broken RTF, fallback on empty stream
		hr = ECMemStream::Create(nullptr, 0, 0, nullptr, nullptr, nullptr, &~ptrEmptyMemStream);
		if (hr != hrSuccess)
			return hr;
		hr = ptrEmptyMemStream->QueryInterface(IID_IStream, &~ptrRtfUncompressedStream);
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

	*lpstrRtfData = std::move(strRtfData);
	return hrSuccess;
}

HRESULT ECMessage::GetCodePage(unsigned int *lpulCodePage)
{
	SPropValuePtr ptrPropValue;
	auto hr = ECAllocateBuffer(sizeof(SPropValue), &~ptrPropValue);
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
HRESULT ECMessage::CopyProps(const SPropTagArray *lpIncludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyProps(&IID_IMessage, static_cast<IMessage *>(this), lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}
