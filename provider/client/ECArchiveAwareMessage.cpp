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
#include "ECArchiveAwareMsgStore.h"
#include "ECArchiveAwareAttach.h"
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include <kopano/mapi_ptr.h>
#include "IECPropStorage.h"
#include "Mem.h"

#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include "ECArchiveAwareMessage.h"
#include <kopano/ECGetText.h>
#include <kopano/stringutil.h>

#include <sstream>
#include <kopano/ECDebug.h>
#include <kopano/charset/convert.h>

#define dispidStoreEntryIds			"store-entryids"
#define dispidItemEntryIds			"item-entryids"
#define dispidStubbed				"stubbed"
#define dispidDirty					"dirty"
#define dispidOrigSourceKey			"original-sourcekey"

class PropFinder {
public:
	PropFinder(ULONG ulPropTag): m_ulPropTag(ulPropTag) {}
	bool operator()(const ECProperty &prop) const { return prop.GetPropTag() == m_ulPropTag; }
private:
	ULONG m_ulPropTag;
};


HRESULT ECArchiveAwareMessageFactory::Create(ECMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags, BOOL bEmbedded, ECMAPIProp* lpRoot, ECMessage **lppMessage) const
{
	ECArchiveAwareMsgStore *lpArchiveAwareStore = dynamic_cast<ECArchiveAwareMsgStore*>(lpMsgStore);

	// New and embedded messages don't need to be archive aware. Also if the calling store
	// is not archive aware, the message won't.
	if (fNew || bEmbedded || lpArchiveAwareStore == NULL)
		return ECMessage::Create(lpMsgStore, fNew, fModify, ulFlags, bEmbedded, lpRoot, lppMessage);

	return ECArchiveAwareMessage::Create(lpArchiveAwareStore, FALSE, fModify, ulFlags, lppMessage);
}


ECArchiveAwareMessage::ECArchiveAwareMessage(ECArchiveAwareMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags)
: ECMessage(lpMsgStore, fNew, fModify, ulFlags, FALSE, NULL)
, m_bLoading(false)
, m_bNamedPropsMapped(false)
, m_mode(MODE_UNARCHIVED)
, m_bChanged(false)
{
	// Override the handler defined in ECMessage
	this->HrAddPropHandlers(PR_MESSAGE_SIZE, ECMessage::GetPropHandler, SetPropHandler, (void*)this, FALSE, FALSE);
}

ECArchiveAwareMessage::~ECArchiveAwareMessage()
{
}

HRESULT	ECArchiveAwareMessage::Create(ECArchiveAwareMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags, ECMessage **lppMessage)
{
	ECArchiveAwareMessage *lpMessage = new ECArchiveAwareMessage(lpMsgStore, fNew, fModify, ulFlags);
	return lpMessage->QueryInterface(IID_ECMessage, reinterpret_cast<void **>(lppMessage));
}

HRESULT ECArchiveAwareMessage::HrLoadProps()
{
	HRESULT hr = hrSuccess;

	m_bLoading = true;
	hr = ECMessage::HrLoadProps();
	if (hr != hrSuccess)
		goto exit;

	// If we noticed we are stubbed, we need to perform a merge here.
	if (m_mode == MODE_STUBBED) {
		const BOOL fModifyCopy = this->fModify;
		ECMsgStore *lpMsgStore = GetMsgStore();

		// @todo: Put in MergePropsFromStub
		SizedSPropTagArray(4, sptaDeleteProps) = {4, {PR_RTF_COMPRESSED, PR_BODY, PR_HTML, PR_ICON_INDEX}};
		SizedSPropTagArray(6, sptaRestoreProps) = {6, {PR_RTF_COMPRESSED, PR_BODY, PR_HTML, PR_ICON_INDEX, PR_MESSAGE_CLASS, PR_MESSAGE_SIZE}};

		if (!m_ptrArchiveMsg) {
			ECArchiveAwareMsgStore *lpStore = dynamic_cast<ECArchiveAwareMsgStore*>(lpMsgStore);
			if (lpStore == NULL) {
				// This is quite a serious error since an ECArchiveAwareMessage can only be created by an
				// ECArchiveAwareMsgStore. We won't just die here though...
				hr = MAPI_E_NOT_FOUND;
				goto exit;
			}

			hr = lpStore->OpenItemFromArchive(m_ptrStoreEntryIDs, m_ptrItemEntryIDs, &m_ptrArchiveMsg);
			if (hr != hrSuccess) {
				hr = CreateInfoMessage((LPSPropTagArray)&sptaDeleteProps, CreateErrorBodyUtf8(hr));
				goto exit;
			}
		}

		// Now merge the properties and reconstruct the attachment table.
		// We'll copy the PR_RTF_COMPRESSED property from the archive to the stub as PR_RTF_COMPRESSED is
		// obtained anyway to determine the type of the body.
		// Also if the stub's PR_MESSAGE_CLASS equals IPM.Zarafa.Stub (old migrator behaviour), we'll overwrite
		// that with the archive's PR_MESSAGE_CLASS and overwrite the PR_ICON_INDEX.

		// We need to temporary enable write access on the underlying objects in order for the following
		// 5 calls to succeed.
		this->fModify = TRUE;

		hr = DeleteProps((LPSPropTagArray)&sptaDeleteProps, NULL);
		if (hr != hrSuccess) {
			this->fModify = fModifyCopy;
			goto exit;
		}

		hr = Util::DoCopyProps(&IID_IMAPIProp, &m_ptrArchiveMsg->m_xMAPIProp, (LPSPropTagArray)&sptaRestoreProps, 0, NULL, &IID_IMAPIProp, &this->m_xMAPIProp, 0, NULL);
		if (hr != hrSuccess) {
			this->fModify = fModifyCopy;
			goto exit;
		}

		// Now remove any dummy attachment(s) and copy the attachments from the archive (except the properties
		// that are too big in the firt place).
		hr = Util::HrDeleteAttachments(&m_xMessage);
		if (hr != hrSuccess) {
			this->fModify = fModifyCopy;
			goto exit;
		}

		hr = Util::CopyAttachments(&m_ptrArchiveMsg->m_xMessage, &m_xMessage, NULL);
		this->fModify = fModifyCopy;
		if (hr != hrSuccess)
			goto exit;
	}

exit:
	m_bLoading = false;

	return hr;
}

HRESULT	ECArchiveAwareMessage::HrSetRealProp(SPropValue *lpsPropValue)
{
	HRESULT hr;

	if (m_bLoading) {
		/*
		 * This is where we end up if we're called through HrLoadProps. So this
		 * is where we check if the loaded message is unarchived, archived or stubbed.
		 */
		if (lpsPropValue &&
			PROP_TYPE(lpsPropValue->ulPropTag) != PT_ERROR &&
			PROP_ID(lpsPropValue->ulPropTag) >= 0x8500)
		{
			// We have a named property that's in the not-hardcoded range (where
			// the archive named properties are). We now need to check if that's
			// one of the properties we're interested in.
			// That might mean we need to first map the named properties now.
			if (!m_bNamedPropsMapped) {
				hr = MapNamedProps();
				if (hr != hrSuccess)
					return hr;
			}

			// Check the various props.
			if (lpsPropValue->ulPropTag == PROP_ARCHIVE_STORE_ENTRYIDS) {
				if (m_mode == MODE_UNARCHIVED)
					m_mode = MODE_ARCHIVED;

				// Store list
				hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID*)&m_ptrStoreEntryIDs);
				if (hr == hrSuccess)
					hr = Util::HrCopyProperty(m_ptrStoreEntryIDs, lpsPropValue, m_ptrStoreEntryIDs);
				if (hr != hrSuccess)
					return hr;
			}

			else if (lpsPropValue->ulPropTag == PROP_ARCHIVE_ITEM_ENTRYIDS) {
				if (m_mode == MODE_UNARCHIVED)
					m_mode = MODE_ARCHIVED;

				// Store list
				hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID*)&m_ptrItemEntryIDs);
				if (hr == hrSuccess)
					hr = Util::HrCopyProperty(m_ptrItemEntryIDs, lpsPropValue, m_ptrItemEntryIDs);
				if (hr != hrSuccess)
					return hr;
			}

			else if (lpsPropValue->ulPropTag == PROP_STUBBED) {
				if (lpsPropValue->Value.b != FALSE)
					m_mode = MODE_STUBBED;

				// The message is not stubbed once destubbed.
				// This fixes all kind of weird copy issues where the stubbed property does not
				// represent the actual state of the message.
				lpsPropValue->Value.b = FALSE;
			}

			else if (lpsPropValue->ulPropTag == PROP_DIRTY) {
				if (lpsPropValue->Value.b != FALSE)
					m_mode = MODE_DIRTY;
			}
		}
	}

	hr = ECMessage::HrSetRealProp(lpsPropValue);
	if (hr == hrSuccess && !m_bLoading) {
		/*
		 * This is where we end up if a property is actually altered through SetProps.
		 */
		m_bChanged = true;
	}
	return hr;
}

HRESULT	ECArchiveAwareMessage::HrDeleteRealProp(ULONG ulPropTag, BOOL fOverwriteRO)
{
	HRESULT hr = hrSuccess;

	hr = ECMessage::HrDeleteRealProp(ulPropTag, fOverwriteRO);
	if (hr == hrSuccess && !m_bLoading)
		m_bChanged = true;

	return hr;
}

HRESULT ECArchiveAwareMessage::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk)
{
	HRESULT hr = hrSuccess;

	hr = ECMessage::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	if (!m_bLoading && hr == hrSuccess && ((ulFlags & MAPI_MODIFY) || (fModify && (ulFlags & MAPI_BEST_ACCESS))))
	{
		// We have no way of knowing if the property will modified since it operates directly
		// on the MAPIOBJECT data, which bypasses this subclass.
		// @todo wrap the property to track if it was altered.
		m_bChanged = true;
	}

	return hr;
}

HRESULT ECArchiveAwareMessage::OpenAttach(ULONG ulAttachmentNum, LPCIID lpInterface, ULONG ulFlags, LPATTACH *lppAttach)
{
	HRESULT hr = hrSuccess;

	hr = ECMessage::OpenAttach(ulAttachmentNum, lpInterface, ulFlags, lppAttach);
	// According to MSDN an attachment must explicitly be opened with MAPI_MODIFY or MAPI_BEST_ACCESS
	// in order to get write access. However, practice has thought that that's not always the case. So
	// if the parent object was openend with write access, we'll assume the object is changed the moment
	// the attachment is openend.
	if (hr == hrSuccess && ((ulFlags & MAPI_MODIFY) || fModify))
	{
		// We have no way of knowing if the attachment will modified since it operates directly
		// on the MAPIOBJECT data, which bypasses this subclass.
		// @todo wrap the attachment to track if it was altered.
		m_bChanged = true;
	}

	return hr;
}

HRESULT ECArchiveAwareMessage::CreateAttach(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulAttachmentNum, LPATTACH *lppAttach)
{
	HRESULT hr = hrSuccess;

	/*
	 * Here, we want to create an ECArchiveAwareAttach when we are still
	 * loading. We need that because an ECArchiveAwareAttach allows its
	 * size to be set during load time.
	 */
	if (m_bLoading)
		hr = ECMessage::CreateAttach(lpInterface, ulFlags, ECArchiveAwareAttachFactory(), lpulAttachmentNum, lppAttach);

	else {
		hr = ECMessage::CreateAttach(lpInterface, ulFlags, ECAttachFactory(), lpulAttachmentNum, lppAttach);
		if (hr == hrSuccess)
			m_bChanged = true;	// Definitely changed.
	}

	return hr;
}

HRESULT ECArchiveAwareMessage::DeleteAttach(ULONG ulAttachmentNum, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;

	hr = ECMessage::DeleteAttach(ulAttachmentNum, ulUIParam, lpProgress, ulFlags);
	if (hr == hrSuccess && !m_bLoading)
		m_bChanged = true;	// Definitely changed.

	return hr;
}

HRESULT ECArchiveAwareMessage::ModifyRecipients(ULONG ulFlags, LPADRLIST lpMods)
{
	HRESULT hr = hrSuccess;

	hr = ECMessage::ModifyRecipients(ulFlags, lpMods);
	if (hr == hrSuccess)
		m_bChanged = true;

	return hr;
}

HRESULT ECArchiveAwareMessage::SaveChanges(ULONG ulFlags)
{
	HRESULT hr;
	SizedSPropTagArray(1, sptaStubbedProp) = {1, {PROP_STUBBED}};

	if (!fModify)
		return MAPI_E_NO_ACCESS;

	// We can't use this->lstProps here since that would suggest things have changed because we might have
	// destubbed ourselves, which is a change from the object model point of view.
	if (!m_bChanged)
		return hrSuccess;

	// From here on we're no longer stubbed.
	if (m_bNamedPropsMapped) {
		hr = DeleteProps((LPSPropTagArray)&sptaStubbedProp, NULL);
		if (hr != hrSuccess)
			return hr;
	}

	if (m_mode == MODE_STUBBED || m_mode == MODE_ARCHIVED) {
		SPropValue propDirty;

		propDirty.ulPropTag = PROP_DIRTY;
		propDirty.Value.b = TRUE;

		hr = SetProps(1, &propDirty, NULL);
		if (hr != hrSuccess)
			return hr;

		m_mode = MODE_DIRTY;	// We have an archived version that's now out of sync.
	}
	return ECMessage::SaveChanges(ulFlags);
}

HRESULT ECArchiveAwareMessage::SetPropHandler(ULONG ulPropTag, void* /*lpProvider*/, LPSPropValue lpsPropValue, void *lpParam)
{
	ECArchiveAwareMessage *lpMessage = (ECArchiveAwareMessage *)lpParam;
	HRESULT hr = hrSuccess;

	switch(ulPropTag) {
	case PR_MESSAGE_SIZE:
		if (lpMessage->m_bLoading)
			hr = lpMessage->ECMessage::HrSetRealProp(lpsPropValue);	// Don't call our own overridden HrSetRealProp
		else
			hr = MAPI_E_COMPUTED;
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}
	return hr;
}

HRESULT ECArchiveAwareMessage::MapNamedProps()
{
	HRESULT hr = hrSuccess;

	PROPMAP_INIT_NAMED_ID(ARCHIVE_STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidStoreEntryIds);
	PROPMAP_INIT_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS,  PT_MV_BINARY, PSETID_Archive, dispidItemEntryIds);
	PROPMAP_INIT_NAMED_ID(STUBBED,                PT_BOOLEAN,   PSETID_Archive, dispidStubbed);
	PROPMAP_INIT_NAMED_ID(DIRTY,				  PT_BOOLEAN,   PSETID_Archive, dispidDirty);
	PROPMAP_INIT_NAMED_ID(ORIGINAL_SOURCE_KEY,    PT_BINARY,    PSETID_Archive, dispidOrigSourceKey);
	PROPMAP_INIT(&this->m_xMAPIProp);

	m_bNamedPropsMapped = true;

exit:
	return hr;
}

HRESULT ECArchiveAwareMessage::CreateInfoMessage(LPSPropTagArray lpptaDeleteProps, const std::string &strBodyHtml)
{
	HRESULT hr = hrSuccess;
	SPropValue sPropVal;
	StreamPtr ptrHtmlStream;
	ULARGE_INTEGER liZero = {{0, 0}};

	this->fModify = TRUE;

	hr = DeleteProps(lpptaDeleteProps, NULL);
	if (hr != hrSuccess)
		goto exit;

	sPropVal.ulPropTag = PR_INTERNET_CPID;
	sPropVal.Value.l = 65001;
	hr = HrSetOneProp(&this->m_xMAPIProp, &sPropVal);
	if (hr != hrSuccess)
		goto exit;

	hr = OpenProperty(PR_HTML, &ptrHtmlStream.iid, 0, MAPI_CREATE|MAPI_MODIFY, &ptrHtmlStream);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrHtmlStream->SetSize(liZero);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrHtmlStream->Write(strBodyHtml.c_str(), strBodyHtml.size(), NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrHtmlStream->Commit(0);

exit:
	this->fModify = FALSE;

	return hr;
}

std::string ECArchiveAwareMessage::CreateErrorBodyUtf8(HRESULT hResult) {
	std::basic_ostringstream<TCHAR> ossHtmlBody;

	ossHtmlBody << _T("<HTML><HEAD><STYLE type=\"text/css\">")
				   _T("BODY {font-family: \"sans-serif\";margin-left: 1em;}")
				   _T("P {margin: .1em 0;}")
				   _T("P.spacing {margin: .8em 0;}")
				   _T("H1 {margin: .3em 0;}")
				   _T("SPAN#errcode {display: inline;font-weight: bold;}")
				   _T("SPAN#errmsg {display: inline;font-style: italic;}")
				   _T("DIV.indented {margin-left: 4em;}")
				   _T("</STYLE></HEAD><BODY><H1>")
				<< _("Kopano Archiver")
				<< _T("</H1><P>")
				<< _("An error has occurred while fetching the message from the archive.")
				<< _T(" ")
				<< _("Please contact your system administrator.")
				<< _T("</P><P class=\"spacing\"></P>")
				   _T("<P>")
				<< _("Error code:")
				<< _T("<SPAN id=\"errcode\">")
				<< tstringify(hResult, true)
				<< _T("</SPAN> (<SPAN id=\"errmsg\">")
				<< convert_to<tstring>(GetMAPIErrorDescription(hResult))
				<< _T("</SPAN>)</P>");

	if (hResult == MAPI_E_NO_SUPPORT) {
		ossHtmlBody << _T("<P class=\"spacing\"></P><P>")
				    << _("It seems no valid archiver license is installed.")
					<< _T("</P>");
	} else if (hResult == MAPI_E_NOT_FOUND) {
		ossHtmlBody << _T("<P class=\"spacing\"></P><P>")
				    << _("The archive could not be found.")
					<< _T("</P>");
	} else if (hResult == MAPI_E_NO_ACCESS) {
		ossHtmlBody << _T("<P class=\"spacing\"></P><P>")
				    << _("You don't have sufficient access to the archive.")
					<< _T("</P>");
	} else {
		LPTSTR	lpszDescription = NULL;
		HRESULT hr = Util::HrMAPIErrorToText(hResult, &lpszDescription);
		if (hr == hrSuccess) {
			ossHtmlBody << _T("<P>")
						<< _("Error description:")
						<< _T("<DIV class=\"indented\">")
						<< lpszDescription
						<< _T("</DIV></P>");
			MAPIFreeBuffer(lpszDescription);
		}
	}

	ossHtmlBody << _T("</BODY></HTML>");

	tstring strHtmlBody = ossHtmlBody.str();
	return convert_to<std::string>("UTF-8", strHtmlBody, rawsize(strHtmlBody), CHARSET_TCHAR);
}

std::string ECArchiveAwareMessage::CreateOfflineWarnBodyUtf8()
{
	std::basic_ostringstream<TCHAR> ossHtmlBody;

	ossHtmlBody << _T("<HTML><HEAD><STYLE type=\"text/css\">")
				   _T("BODY {font-family: \"sans-serif\";margin-left: 1em;}")
				   _T("P {margin: .1em 0;}")
				   _T("P.spacing {margin: .8em 0;}")
				   _T("H1 {margin: .3em 0;}")
				   _T("SPAN#errcode {display: inline;font-weight: bold;}")
				   _T("SPAN#errmsg {display: inline;font-style: italic;}")
				   _T("DIV.indented {margin-left: 4em;}")
				   _T("</STYLE></HEAD><BODY><H1>")
				<< _("Kopano Archiver")
				<< _T("</H1><P>")
				<< _("Archives can not be destubbed when working offline.")
				<< _T("</P></BODY></HTML>");

	tstring strHtmlBody = ossHtmlBody.str();
	return convert_to<std::string>("UTF-8", strHtmlBody, rawsize(strHtmlBody), CHARSET_TCHAR);
}
