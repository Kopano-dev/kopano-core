/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <utility>
#include <kopano/platform.h>
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include "IECPropStorage.h"
#include "Mem.h"
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include "ECArchiveAwareMessage.h"
#include <kopano/ECGetText.h>
#include <kopano/stringutil.h>
#include <sstream>
#include <kopano/MAPIErrors.h>
#include <kopano/charset/convert.h>

#define dispidStoreEntryIds			"store-entryids"
#define dispidItemEntryIds			"item-entryids"
#define dispidStubbed				"stubbed"
#define dispidDirty					"dirty"
#define dispidOrigSourceKey			"original-sourcekey"

using namespace KC;

class PropFinder {
public:
	PropFinder(ULONG ulPropTag): m_ulPropTag(ulPropTag) {}
	bool operator()(const ECProperty &prop) const { return prop.GetPropTag() == m_ulPropTag; }
private:
	ULONG m_ulPropTag;
};

HRESULT ECArchiveAwareMessageFactory::Create(ECMsgStore *lpMsgStore, BOOL fNew,
    BOOL fModify, ULONG ulFlags, BOOL bEmbedded, const ECMAPIProp *lpRoot,
    ECMessage **lppMessage) const
{
	auto lpArchiveAwareStore = dynamic_cast<ECArchiveAwareMsgStore *>(lpMsgStore);

	// New and embedded messages don't need to be archive aware. Also if the calling store
	// is not archive aware, the message won't.
	if (fNew || bEmbedded || lpArchiveAwareStore == NULL)
		return ECMessage::Create(lpMsgStore, fNew, fModify, ulFlags, bEmbedded, lpRoot, lppMessage);

	return ECArchiveAwareMessage::Create(lpArchiveAwareStore, FALSE, fModify, ulFlags, lppMessage);
}

ECArchiveAwareMessage::ECArchiveAwareMessage(ECArchiveAwareMsgStore *lpMsgStore,
    BOOL is_new, BOOL modify, ULONG ulFlags) :
	ECMessage(lpMsgStore, is_new, modify, ulFlags, false, nullptr),
	m_bLoading(false), m_bNamedPropsMapped(false), m_propmap(5),
	m_mode(MODE_UNARCHIVED), m_bChanged(false)
{
	// Override the handler defined in ECMessage
	HrAddPropHandlers(PR_MESSAGE_SIZE, ECMessage::GetPropHandler, SetPropHandler, this, false, false);
}

HRESULT	ECArchiveAwareMessage::Create(ECArchiveAwareMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags, ECMessage **lppMessage)
{
	return alloc_wrap<ECArchiveAwareMessage>(lpMsgStore, fNew, fModify,
	       ulFlags).as(IID_ECMessage, lppMessage);
}

HRESULT ECArchiveAwareMessage::HrLoadProps()
{
	m_bLoading = true;
	auto laters = make_scope_success([&]() { m_bLoading = false; });
	auto hr = ECMessage::HrLoadProps();
	if (hr != hrSuccess)
		return hr;
	// If we noticed we are stubbed, we need to perform a merge here.
	if (m_mode != MODE_STUBBED)
		return hr;

	BOOL fModifyCopy = fModify;
	auto lpMsgStore = GetMsgStore();
	// @todo: Put in MergePropsFromStub
	static constexpr const SizedSPropTagArray(4, sptaDeleteProps) =
		{4, {PR_RTF_COMPRESSED, PR_BODY, PR_HTML, PR_ICON_INDEX}};
	static constexpr const SizedSPropTagArray(6, sptaRestoreProps) =
		{6, {PR_RTF_COMPRESSED, PR_BODY, PR_HTML, PR_ICON_INDEX,
		PR_MESSAGE_CLASS, PR_MESSAGE_SIZE}};

	if (!m_ptrArchiveMsg) {
		auto lpStore = dynamic_cast<ECArchiveAwareMsgStore *>(lpMsgStore);
		if (lpStore == NULL)
			// This is quite a serious error since an ECArchiveAwareMessage can only be created by an
			// ECArchiveAwareMsgStore. We won't just die here though...
			return MAPI_E_NOT_FOUND;
		hr = lpStore->OpenItemFromArchive(m_ptrStoreEntryIDs, m_ptrItemEntryIDs, &~m_ptrArchiveMsg);
		if (hr != hrSuccess)
			return CreateInfoMessage(sptaDeleteProps, CreateErrorBodyUtf8(hr));
	}

	// Now merge the properties and reconstruct the attachment table.
	// We'll copy the PR_RTF_COMPRESSED property from the archive to the stub as PR_RTF_COMPRESSED is
	// obtained anyway to determine the type of the body.
	// Also if the stub's PR_MESSAGE_CLASS equals IPM.Zarafa.Stub (old migrator behaviour), we'll overwrite
	// that with the archive's PR_MESSAGE_CLASS and overwrite the PR_ICON_INDEX.
	// We need to temporary enable write access on the underlying objects in order for the following
	// 5 calls to succeed.
	fModify = true;
	hr = DeleteProps(sptaDeleteProps, NULL);
	if (hr != hrSuccess) {
		fModify = fModifyCopy;
		return hr;
	}
	hr = Util::DoCopyProps(&IID_IMAPIProp, static_cast<IMAPIProp *>(m_ptrArchiveMsg),
	     sptaRestoreProps, 0, NULL, &IID_IMAPIProp,
	     static_cast<IMAPIProp *>(this), 0, nullptr);
	if (hr != hrSuccess) {
		fModify = fModifyCopy;
		return hr;
	}

	// Now remove any dummy attachment(s) and copy the attachments from the archive (except the properties
	// that are too big in the firt place).
	hr = Util::HrDeleteAttachments(this);
	if (hr != hrSuccess) {
		fModify = fModifyCopy;
		return hr;
	}
	hr = Util::CopyAttachments(m_ptrArchiveMsg, this, NULL);
	fModify = fModifyCopy;
	return hr;
}

HRESULT	ECArchiveAwareMessage::HrSetRealProp(const SPropValue *lpsPropValue)
{
	SPropValue copy;

	if (lpsPropValue != nullptr)
		copy = *lpsPropValue;
	/*
	 * m_bLoading: This is where we end up if we're called through HrLoadProps. So this
	 * is where we check if the loaded message is unarchived, archived or stubbed.
	 */
	if (m_bLoading && lpsPropValue != nullptr &&
	    PROP_TYPE(lpsPropValue->ulPropTag) != PT_ERROR &&
	    PROP_ID(lpsPropValue->ulPropTag) >= 0x8500) {
		// We have a named property that's in the not-hardcoded range (where
		// the archive named properties are). We now need to check if that's
		// one of the properties we're interested in.
		// That might mean we need to first map the named properties now.
		if (!m_bNamedPropsMapped) {
			auto hr = MapNamedProps();
			if (hr != hrSuccess)
				return hr;
		}

		// Check the various props.
		if (lpsPropValue->ulPropTag == PROP_ARCHIVE_STORE_ENTRYIDS) {
			if (m_mode == MODE_UNARCHIVED)
				m_mode = MODE_ARCHIVED;
			// Store list
			auto hr = MAPIAllocateBuffer(sizeof(SPropValue), &~m_ptrStoreEntryIDs);
			if (hr == hrSuccess)
				hr = Util::HrCopyProperty(m_ptrStoreEntryIDs, lpsPropValue, m_ptrStoreEntryIDs);
			if (hr != hrSuccess)
				return hr;
		}
		else if (lpsPropValue->ulPropTag == PROP_ARCHIVE_ITEM_ENTRYIDS) {
			if (m_mode == MODE_UNARCHIVED)
				m_mode = MODE_ARCHIVED;
			// Store list
			auto hr = MAPIAllocateBuffer(sizeof(SPropValue), &~m_ptrItemEntryIDs);
			if (hr == hrSuccess)
				hr = Util::HrCopyProperty(m_ptrItemEntryIDs, lpsPropValue, m_ptrItemEntryIDs);
			if (hr != hrSuccess)
				return hr;
		}
		else if (lpsPropValue->ulPropTag == PROP_STUBBED) {
			if (lpsPropValue->Value.b)
				m_mode = MODE_STUBBED;
			// The message is not stubbed once destubbed.
			// This fixes all kind of weird copy issues where the stubbed property does not
			// represent the actual state of the message.
			copy.Value.b = FALSE;
		}
		else if (lpsPropValue->ulPropTag == PROP_DIRTY) {
			if (lpsPropValue->Value.b)
				m_mode = MODE_DIRTY;
		}
	}

	auto hr = ECMessage::HrSetRealProp(lpsPropValue != nullptr ? &copy : nullptr);
	if (hr == hrSuccess && !m_bLoading)
		/*
		 * This is where we end up if a property is actually altered through SetProps.
		 */
		m_bChanged = true;
	return hr;
}

HRESULT	ECArchiveAwareMessage::HrDeleteRealProp(ULONG ulPropTag, BOOL fOverwriteRO)
{
	auto hr = ECMessage::HrDeleteRealProp(ulPropTag, fOverwriteRO);
	if (hr == hrSuccess && !m_bLoading)
		m_bChanged = true;
	return hr;
}

HRESULT ECArchiveAwareMessage::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	auto hr = ECMessage::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	if (!m_bLoading && hr == hrSuccess && ((ulFlags & MAPI_MODIFY) || (fModify && (ulFlags & MAPI_BEST_ACCESS))))
		// We have no way of knowing if the property will modified since it operates directly
		// on the MAPIOBJECT data, which bypasses this subclass.
		// @todo wrap the property to track if it was altered.
		m_bChanged = true;
	return hr;
}

HRESULT ECArchiveAwareMessage::OpenAttach(ULONG ulAttachmentNum, LPCIID lpInterface, ULONG ulFlags, LPATTACH *lppAttach)
{
	auto hr = ECMessage::OpenAttach(ulAttachmentNum, lpInterface, ulFlags, lppAttach);
	// According to MSDN an attachment must explicitly be opened with MAPI_MODIFY or MAPI_BEST_ACCESS
	// in order to get write access. However, practice has thought that that's not always the case. So
	// if the parent object was opened with write access, we'll assume the object is changed the moment
	// the attachment is opened.
	if (hr == hrSuccess && ((ulFlags & MAPI_MODIFY) || fModify))
		// We have no way of knowing if the attachment will modified since it operates directly
		// on the MAPIOBJECT data, which bypasses this subclass.
		// @todo wrap the attachment to track if it was altered.
		m_bChanged = true;
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
	auto hr = ECMessage::DeleteAttach(ulAttachmentNum, ulUIParam, lpProgress, ulFlags);
	if (hr == hrSuccess && !m_bLoading)
		m_bChanged = true;	// Definitely changed.
	return hr;
}

HRESULT ECArchiveAwareMessage::ModifyRecipients(ULONG ulFlags,
    const ADRLIST *lpMods)
{
	auto hr = ECMessage::ModifyRecipients(ulFlags, lpMods);
	if (hr == hrSuccess)
		m_bChanged = true;
	return hr;
}

HRESULT ECArchiveAwareMessage::SaveChanges(ULONG ulFlags)
{
	SizedSPropTagArray(1, sptaStubbedProp) = {1, {PROP_STUBBED}};

	if (!fModify)
		return MAPI_E_NO_ACCESS;
	// We can't use this->lstProps here since that would suggest things have changed because we might have
	// destubbed ourselves, which is a change from the object model point of view.
	if (!m_bChanged)
		return hrSuccess;
	// From here on we're no longer stubbed.
	if (m_bNamedPropsMapped) {
		auto hr = DeleteProps(sptaStubbedProp, NULL);
		if (hr != hrSuccess)
			return hr;
	}

	if (m_mode == MODE_STUBBED || m_mode == MODE_ARCHIVED) {
		SPropValue propDirty;

		propDirty.ulPropTag = PROP_DIRTY;
		propDirty.Value.b = TRUE;
		auto hr = SetProps(1, &propDirty, nullptr);
		if (hr != hrSuccess)
			return hr;
		m_mode = MODE_DIRTY;	// We have an archived version that's now out of sync.
	}
	return ECMessage::SaveChanges(ulFlags);
}

HRESULT ECArchiveAwareMessage::SetPropHandler(unsigned int ulPropTag,
    void *lpProvider, const SPropValue *lpsPropValue, ECGenericProp *lpParam)
{
	auto lpMessage = static_cast<ECArchiveAwareMessage *>(lpParam);
	switch(ulPropTag) {
	case PR_MESSAGE_SIZE:
		if (lpMessage->m_bLoading)
			return lpMessage->ECMessage::HrSetRealProp(lpsPropValue); /* Do not call our own overridden HrSetRealProp */
		return MAPI_E_COMPUTED;
	default:
		return MAPI_E_NOT_FOUND;
	}
}

HRESULT ECArchiveAwareMessage::MapNamedProps()
{
	PROPMAP_INIT_NAMED_ID(ARCHIVE_STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidStoreEntryIds);
	PROPMAP_INIT_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS,  PT_MV_BINARY, PSETID_Archive, dispidItemEntryIds);
	PROPMAP_INIT_NAMED_ID(STUBBED,                PT_BOOLEAN,   PSETID_Archive, dispidStubbed);
	PROPMAP_INIT_NAMED_ID(DIRTY,				  PT_BOOLEAN,   PSETID_Archive, dispidDirty);
	PROPMAP_INIT_NAMED_ID(ORIGINAL_SOURCE_KEY,    PT_BINARY,    PSETID_Archive, dispidOrigSourceKey);
	PROPMAP_INIT(this);
	m_bNamedPropsMapped = true;
	return hrSuccess;
}

HRESULT ECArchiveAwareMessage::CreateInfoMessage(const SPropTagArray *lpptaDeleteProps,
    const std::string &strBodyHtml)
{
	SPropValue sPropVal;
	StreamPtr ptrHtmlStream;
	ULARGE_INTEGER liZero = {{0, 0}};

	fModify = true;
	auto laters = make_scope_success([&]() { fModify = false; });
	auto hr = DeleteProps(lpptaDeleteProps, nullptr);
	if (hr != hrSuccess)
		return hr;

	sPropVal.ulPropTag = PR_INTERNET_CPID;
	sPropVal.Value.l = 65001;
	hr = HrSetOneProp(this, &sPropVal);
	if (hr != hrSuccess)
		return hr;
	hr = OpenProperty(PR_HTML, &iid_of(ptrHtmlStream), 0, MAPI_CREATE | MAPI_MODIFY, &~ptrHtmlStream);
	if (hr != hrSuccess)
		return hr;
	hr = ptrHtmlStream->SetSize(liZero);
	if (hr != hrSuccess)
		return hr;
	hr = ptrHtmlStream->Write(strBodyHtml.c_str(), strBodyHtml.size(), NULL);
	if (hr != hrSuccess)
		return hr;
	return ptrHtmlStream->Commit(0);
}

std::string ECArchiveAwareMessage::CreateErrorBodyUtf8(HRESULT hResult) {
	std::basic_ostringstream<TCHAR> ossHtmlBody;

	ossHtmlBody << KC_T("<HTML><HEAD><STYLE type=\"text/css\">")
				   KC_T("BODY {font-family: \"sans-serif\";margin-left: 1em;}")
				   KC_T("P {margin: .1em 0;}")
				   KC_T("P.spacing {margin: .8em 0;}")
				   KC_T("H1 {margin: .3em 0;}")
				   KC_T("SPAN#errcode {display: inline;font-weight: bold;}")
				   KC_T("SPAN#errmsg {display: inline;font-style: italic;}")
				   KC_T("DIV.indented {margin-left: 4em;}")
				   KC_T("</STYLE></HEAD><BODY><H1>")
				<< "Kopano Archiver"
				<< KC_T("</H1><P>")
				<< KC_TX("An error has occurred while fetching the message from the archive.")
				<< KC_T(" ")
				<< KC_TX("Please contact your system administrator.")
				<< KC_T("</P><P class=\"spacing\"></P>")
				   KC_T("<P>")
				<< KC_TX("Error code:")
				<< KC_T("<SPAN id=\"errcode\">")
				<< tstringify_hex(hResult)
				<< KC_T("</SPAN> (<SPAN id=\"errmsg\">")
				<< convert_to<tstring>(GetMAPIErrorMessage(hResult))
				<< KC_T(" (") << tstringify_hex(hResult) << KC_T(")")
				<< KC_T("</SPAN>)</P>");

	if (hResult == MAPI_E_NO_SUPPORT) {
		ossHtmlBody << KC_T("<P class=\"spacing\"></P><P>")
			<< KC_TX("It seems no valid archiver license is installed.")
					<< KC_T("</P>");
	} else if (hResult == MAPI_E_NOT_FOUND) {
		ossHtmlBody << KC_T("<P class=\"spacing\"></P><P>")
			<< KC_TX("The archive could not be found.")
					<< KC_T("</P>");
	} else if (hResult == MAPI_E_NO_ACCESS) {
		ossHtmlBody << KC_T("<P class=\"spacing\"></P><P>")
			<< KC_TX("You don't have sufficient access to the archive.")
					<< KC_T("</P>");
	} else {
		memory_ptr<TCHAR> lpszDescription;
		HRESULT hr = Util::HrMAPIErrorToText(hResult, &~lpszDescription);
		if (hr == hrSuccess)
			ossHtmlBody << KC_T("<P>")
						<< KC_TX("Error description:")
						<< KC_T("<DIV class=\"indented\">")
						<< lpszDescription
						<< KC_T("</DIV></P>");
	}

	ossHtmlBody << KC_T("</BODY></HTML>");
	tstring strHtmlBody = ossHtmlBody.str();
	return convert_to<std::string>("UTF-8", strHtmlBody, rawsize(strHtmlBody), CHARSET_TCHAR);
}

HRESULT ECArchiveAwareAttachFactory::Create(ECMsgStore *lpMsgStore,
    ULONG ulObjType, BOOL fModify, ULONG ulAttachNum, const ECMAPIProp *lpRoot,
    ECAttach **lppAttach) const
{
	return ECArchiveAwareAttach::Create(lpMsgStore, ulObjType, fModify, ulAttachNum, lpRoot, lppAttach);
}

ECArchiveAwareAttach::ECArchiveAwareAttach(ECMsgStore *lpMsgStore,
    ULONG objtype, BOOL modify, ULONG atnum, const ECMAPIProp *lpRoot) :
	ECAttach(lpMsgStore, objtype, modify, atnum, lpRoot),
	m_lpRoot(dynamic_cast<const ECArchiveAwareMessage *>(lpRoot))
{
	assert(m_lpRoot != NULL);	// We don't expect an ECArchiveAwareAttach to be ever created by any other object than a ECArchiveAwareMessage.
	// Override the handler defined in ECAttach
	HrAddPropHandlers(PR_ATTACH_SIZE, ECAttach::GetPropHandler, SetPropHandler, this, false, false);
}

HRESULT ECArchiveAwareAttach::Create(ECMsgStore *lpMsgStore, ULONG ulObjType,
    BOOL fModify, ULONG ulAttachNum, const ECMAPIProp *lpRoot,
    ECAttach **lppAttach)
{
	return alloc_wrap<ECArchiveAwareAttach>(lpMsgStore, ulObjType, fModify,
	       ulAttachNum, lpRoot).as(IID_ECAttach, lppAttach);
}

HRESULT ECArchiveAwareAttach::SetPropHandler(unsigned int ulPropTag,
    void *lpProvider, const SPropValue *lpsPropValue, ECGenericProp *lpParam)
{
	auto lpAttach = static_cast<ECArchiveAwareAttach *>(lpParam);
	switch(ulPropTag) {
	case PR_ATTACH_SIZE:
		if (lpAttach->m_lpRoot && lpAttach->m_lpRoot->IsLoading())
			return lpAttach->HrSetRealProp(lpsPropValue);
		return MAPI_E_COMPUTED;
	default:
		return MAPI_E_NOT_FOUND;
	}
}
ECArchiveAwareMsgStore::ECArchiveAwareMsgStore(const char *lpszProfname,
    IMAPISupport *sup, WSTransport *tp, BOOL modify, ULONG ulProfileFlags,
    BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore) :
	ECMsgStore(lpszProfname, sup, tp, modify, ulProfileFlags, fIsSpooler,
	    fIsDefaultStore, bOfflineStore)
{ }

HRESULT ECArchiveAwareMsgStore::Create(const char *lpszProfname,
    IMAPISupport *lpSupport, WSTransport *lpTransport, BOOL fModify,
    ULONG ulProfileFlags, BOOL fIsSpooler, BOOL fIsDefaultStore,
    BOOL bOfflineStore, ECMsgStore **lppECMsgStore)
{
	return alloc_wrap<ECArchiveAwareMsgStore>(lpszProfname, lpSupport,
	       lpTransport, fModify, ulProfileFlags, fIsSpooler, fIsDefaultStore,
	       bOfflineStore).as(IID_ECMsgStore, lppECMsgStore);
}

HRESULT ECArchiveAwareMsgStore::OpenEntry(ULONG cbEntryID,
    const ENTRYID *lpEntryID, const IID *lpInterface, ULONG ulFlags,
    ULONG *lpulObjType, IUnknown **lppUnk)
{
	// By default we'll try to open an archive aware message when a message is opened. The exception
	// is when the client is not licensed to do so or when it's explicitly disabled by passing
	// IID_IECMessageRaw as the lpInterface parameter. This is for instance needed for the archiver
	// itself because it needs to operate on the non-stubbed (or raw) message.
	// In this override, we only check for the presence of IID_IECMessageRaw. If that's found, we'll
	// pass an ECMessageFactory instance to our parents OpenEntry.
	// Otherwise we'll pass an ECArchiveAwareMessageFactory instance, which will check the license
	// create the appropriate message type. If the object turns out to be a message that is.
	if (lpInterface != nullptr && memcmp(lpInterface, &IID_IECMessageRaw, sizeof(IID)) == 0)
		return ECMsgStore::OpenEntry(cbEntryID, lpEntryID, &IID_IMessage, ulFlags, ECMessageFactory(), lpulObjType, lppUnk);
	return ECMsgStore::OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, ECArchiveAwareMessageFactory(), lpulObjType, lppUnk);
}

HRESULT ECArchiveAwareMsgStore::OpenItemFromArchive(LPSPropValue lpPropStoreEIDs, LPSPropValue lpPropItemEIDs, ECMessage **lppMessage)
{
	if (lpPropStoreEIDs == nullptr || lpPropItemEIDs == nullptr ||
	    lppMessage == nullptr ||
	    PROP_TYPE(lpPropStoreEIDs->ulPropTag) != PT_MV_BINARY ||
	    PROP_TYPE(lpPropItemEIDs->ulPropTag) != PT_MV_BINARY ||
	    lpPropStoreEIDs->Value.MVbin.cValues != lpPropItemEIDs->Value.MVbin.cValues)
		return MAPI_E_INVALID_PARAMETER;

	BinaryList lstStoreEIDs, lstItemEIDs;
	object_ptr<ECMessage> ptrArchiveMessage;
	// First get a list of items that could be retrieved from cached archive stores.
	auto hr = CreateCacheBasedReorderedList(lpPropStoreEIDs->Value.MVbin, lpPropItemEIDs->Value.MVbin, &lstStoreEIDs, &lstItemEIDs);
	if (hr != hrSuccess)
		return hr;

	auto iterStoreEID = lstStoreEIDs.begin();
	auto iterIterEID = lstItemEIDs.begin();
	for (; iterStoreEID != lstStoreEIDs.end(); ++iterStoreEID, ++iterIterEID) {
		ECMsgStorePtr	ptrArchiveStore;
		ULONG			ulType = 0;

		hr = GetArchiveStore(*iterStoreEID, &~ptrArchiveStore);
		if (hr == MAPI_E_NO_SUPPORT)
			return hr;	// No need to try any other archives.
		if (hr != hrSuccess)
			continue;
		hr = ptrArchiveStore->OpenEntry((*iterIterEID)->cb, reinterpret_cast<ENTRYID *>((*iterIterEID)->lpb), &IID_ECMessage, 0, &ulType, &~ptrArchiveMessage);
		if (hr != hrSuccess)
			continue;
		break;
	}

	if (iterStoreEID == lstStoreEIDs.end())
		return MAPI_E_NOT_FOUND;
	if (ptrArchiveMessage)
		hr = ptrArchiveMessage->QueryInterface(IID_ECMessage, reinterpret_cast<void **>(lppMessage));
	return hr;
}

HRESULT ECArchiveAwareMsgStore::CreateCacheBasedReorderedList(const SBinaryArray &sbaStoreEIDs,
    const SBinaryArray &sbaItemEIDs, BinaryList *lplstStoreEIDs, BinaryList *lplstItemEIDs)
{
	BinaryList lstStoreEIDs, lstItemEIDs;
	BinaryList lstUncachedStoreEIDs, lstUncachedItemEIDs;

	for (ULONG i = 0; i < sbaStoreEIDs.cValues; ++i) {
		const std::vector<BYTE> eid(sbaStoreEIDs.lpbin[i].lpb, sbaStoreEIDs.lpbin[i].lpb + sbaStoreEIDs.lpbin[i].cb);
		if (m_mapStores.find(eid) != m_mapStores.end()) {
			lstStoreEIDs.emplace_back(sbaStoreEIDs.lpbin + i);
			lstItemEIDs.emplace_back(sbaItemEIDs.lpbin + i);
		} else {
			lstUncachedStoreEIDs.emplace_back(sbaStoreEIDs.lpbin + i);
			lstUncachedItemEIDs.emplace_back(sbaItemEIDs.lpbin + i);
		}
	}

	lstStoreEIDs.splice(lstStoreEIDs.end(), lstUncachedStoreEIDs);
	lstItemEIDs.splice(lstItemEIDs.end(), lstUncachedItemEIDs);
	*lplstStoreEIDs = std::move(lstStoreEIDs);
	*lplstItemEIDs = std::move(lstItemEIDs);
	return hrSuccess;
}

HRESULT ECArchiveAwareMsgStore::GetArchiveStore(LPSBinary lpStoreEID, ECMsgStore **lppArchiveStore)
{
	const std::vector<BYTE> eid(lpStoreEID->lpb, lpStoreEID->lpb + lpStoreEID->cb);
	MsgStoreMap::const_iterator iterStore = m_mapStores.find(eid);
	if (iterStore != m_mapStores.cend())
		return iterStore->second->QueryInterface(IID_ECMsgStore, reinterpret_cast<void **>(lppArchiveStore));

	// @todo: Consolidate this with ECMSProvider::LogonByEntryID
	object_ptr<IMsgStore> ptrUnknown;
	ECMsgStorePtr ptrOnlineStore;
	ULONG cbEntryID = 0;
	EntryIdPtr ptrEntryID;
	std::string ServerURL, strServer;
	bool bIsPseudoUrl = false, bIsPeer = false;
	object_ptr<WSTransport> ptrTransport;
	ECMsgStorePtr ptrArchiveStore;
	object_ptr<IECPropStorage> ptrPropStorage;

	auto hr = QueryInterface(IID_ECMsgStoreOnline, &~ptrUnknown);
	if (hr != hrSuccess)
		return hr;
	hr = ptrUnknown->QueryInterface(IID_ECMsgStore, &~ptrOnlineStore);
	if (hr != hrSuccess)
		return hr;
	hr = UnWrapStoreEntryID(lpStoreEID->cb, reinterpret_cast<ENTRYID *>(lpStoreEID->lpb), &cbEntryID, &~ptrEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetServerURLFromStoreEntryId(cbEntryID, ptrEntryID, ServerURL, &bIsPseudoUrl);
	if (hr != hrSuccess)
		return hr;

	if (bIsPseudoUrl) {
		hr = HrResolvePseudoUrl(ptrOnlineStore->lpTransport, ServerURL.c_str(), strServer, &bIsPeer);
		if (hr != hrSuccess)
			return hr;
		if (!bIsPeer)
			ServerURL = strServer;
		else {
			// We can't just use the transport from ptrOnlineStore as that will be
			// logged off when ptrOnlineStore gets destroyed (at the end of this finction).
			hr = ptrOnlineStore->lpTransport->CloneAndRelogon(&~ptrTransport);
			if (hr != hrSuccess)
				return hr;
		}
	}

	if (!ptrTransport) {
		// We get here if lpszServer wasn't a pseudo URL or if it was and it resolved
		// to another server than the one we're connected with.
		hr = ptrOnlineStore->lpTransport->CreateAndLogonAlternate(ServerURL.c_str(), &~ptrTransport);
		if (hr != hrSuccess)
			return hr;
	}

	hr = ECMsgStore::Create(const_cast<char *>(GetProfileName()), lpSupport,
	     ptrTransport, false, 0, false, false, false, &~ptrArchiveStore);
	if (hr != hrSuccess)
		return hr;
	// Get a propstorage for the message store
	hr = ptrTransport->HrOpenPropStorage(0, nullptr, cbEntryID, ptrEntryID, 0, &~ptrPropStorage);
	if (hr != hrSuccess)
		return hr;
	// Set up the message store to use this storage
	hr = ptrArchiveStore->HrSetPropStorage(ptrPropStorage, FALSE);
	if (hr != hrSuccess)
		return hr;
	// Setup callback for session change
	hr = ptrTransport->AddSessionReloadCallback(ptrArchiveStore, ECMsgStore::Reload, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveStore->SetEntryId(cbEntryID, ptrEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveStore->QueryInterface(IID_ECMsgStore, reinterpret_cast<void **>(lppArchiveStore));
	if (hr != hrSuccess)
		return hr;
	m_mapStores.emplace(eid, ptrArchiveStore);
	return hrSuccess;
}
