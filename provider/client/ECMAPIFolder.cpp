/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include "kcore.hpp"
#include "ics.h"
#include "pcutil.hpp"
#include "ECMessage.h"
#include "ECMAPIFolder.h"
#include "ECMAPITable.h"
#include "ECMsgStorePublic.h"
#include "ECExchangeModifyTable.h"
#include "ECExchangeImportHierarchyChanges.h"
#include "ECExchangeImportContentsChanges.h"
#include "ECExchangeExportChanges.h"
#include "WSTransport.h"
#include "WSMessageStreamExporter.h"
#include "WSMessageStreamImporter.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include <kopano/Util.h>
#include "ClientUtil.h"
#include <kopano/mapi_ptr.h>
#include <edkmdb.h>
#include <kopano/mapiext.h>
#include <mapiutil.h>
#include <cstdio>
#include <kopano/stringutil.h>
#include <kopano/charset/convstring.h>

using namespace KC;

static LONG AdviseECFolderCallback(void *lpContext, ULONG cNotif,
    LPNOTIFICATION lpNotif)
{
	if (lpContext == NULL)
		return S_OK;
	static_cast<ECMAPIFolder *>(lpContext)->m_bReload = TRUE;
	return S_OK;
}

ECMAPIFolder::ECMAPIFolder(ECMsgStore *lpMsgStore, BOOL modify,
    WSMAPIFolderOps *ops, const char *cls_name) :
	ECMAPIContainer(lpMsgStore, MAPI_FOLDER, modify, cls_name),
	lpFolderOps(ops)
{
	// Folder counters
	HrAddPropHandlers(PR_ASSOC_CONTENT_COUNT, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_CONTENT_COUNT, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_CONTENT_UNREAD, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_SUBFOLDERS, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_FOLDER_CHILD_COUNT, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_DELETED_MSG_COUNT, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_DELETED_FOLDER_COUNT, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_DELETED_ASSOC_MSG_COUNT, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_CONTAINER_CONTENTS, GetPropHandler, DefaultSetPropIgnore, this, false, false);
	HrAddPropHandlers(PR_FOLDER_ASSOCIATED_CONTENTS, GetPropHandler, DefaultSetPropIgnore, this, false, false);
	HrAddPropHandlers(PR_CONTAINER_HIERARCHY, GetPropHandler, DefaultSetPropIgnore, this, false, false);
	HrAddPropHandlers(PR_ACCESS, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_RIGHTS, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_MESSAGE_SIZE, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_FOLDER_TYPE, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	// ACLs are only offline
	HrAddPropHandlers(PR_ACL_DATA, GetPropHandler, SetPropHandler, this);
	isTransactedObject = false;
}

ECMAPIFolder::~ECMAPIFolder()
{
	enable_transaction(false);
	lpFolderOps.reset();
	if (m_ulConnection > 0)
		GetMsgStore()->m_lpNotifyClient->UnRegisterAdvise(m_ulConnection);
}

HRESULT ECMAPIFolder::Create(ECMsgStore *lpMsgStore, BOOL fModify, WSMAPIFolderOps *lpFolderOps, ECMAPIFolder **lppECMAPIFolder)
{
	return alloc_wrap<ECMAPIFolder>(lpMsgStore, fModify, lpFolderOps,
	       "IMAPIFolder").put(lppECMAPIFolder);
}

HRESULT ECMAPIFolder::GetPropHandler(unsigned int ulPropTag, void *lpProvider,
     unsigned int ulFlags, SPropValue *lpsPropValue, ECGenericProp *lpParam,
     void *lpBase)
{
	HRESULT hr = hrSuccess;
	auto lpFolder = static_cast<ECMAPIFolder *>(lpParam);

	switch(ulPropTag) {
	case PR_CONTENT_COUNT:
	case PR_CONTENT_UNREAD:
	case PR_DELETED_MSG_COUNT:
	case PR_DELETED_FOLDER_COUNT:
	case PR_DELETED_ASSOC_MSG_COUNT:
	case PR_ASSOC_CONTENT_COUNT:
	case PR_FOLDER_CHILD_COUNT:
		if (lpFolder->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue) == hrSuccess)
			break;
		// Don't return an error here: outlook is relying on PR_CONTENT_COUNT, etc being available at all times. Especially the
		// exit routine (which checks to see how many items are left in the outbox) will crash if PR_CONTENT_COUNT is MAPI_E_NOT_FOUND
		lpsPropValue->ulPropTag = ulPropTag;
		lpsPropValue->Value.ul = 0;
		break;
	case PR_SUBFOLDERS:
		if (lpFolder->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue) == hrSuccess)
			break;
		lpsPropValue->ulPropTag = PR_SUBFOLDERS;
		lpsPropValue->Value.b = FALSE;
		break;
	case PR_ACCESS:
		if (lpFolder->HrGetRealProp(PR_ACCESS, ulFlags, lpBase, lpsPropValue) == hrSuccess)
			break;
		lpsPropValue->ulPropTag = PR_ACCESS;
		lpsPropValue->Value.l = 0; // FIXME: tijdelijk voor test
		break;
	case PR_CONTAINER_CONTENTS:
	case PR_FOLDER_ASSOCIATED_CONTENTS:
	case PR_CONTAINER_HIERARCHY:
		lpsPropValue->ulPropTag = ulPropTag;
		lpsPropValue->Value.x = 1;
		break;
	case PR_ACL_DATA:
		hr = lpFolder->GetSerializedACLData(lpBase, lpsPropValue);
		if (hr == hrSuccess)
			lpsPropValue->ulPropTag = PR_ACL_DATA;
		else {
			lpsPropValue->ulPropTag = CHANGE_PROP_TYPE(PR_ACL_DATA, PT_ERROR);
			lpsPropValue->Value.err = hr;
		}
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}

	return hr;
}

HRESULT ECMAPIFolder::SetPropHandler(unsigned int ulPropTag, void *lpProvider,
    const SPropValue *lpsPropValue, ECGenericProp *lpParam)
{
	if (ulPropTag != PR_ACL_DATA)
		return MAPI_E_NOT_FOUND;
	return static_cast<ECMAPIFolder *>(lpParam)->SetSerializedACLData(lpsPropValue);
}

// This is similar to GetPropHandler, but works is a static function, and therefore cannot access
// an open ECMAPIFolder object. (The folder is most probably also not open, so ...
HRESULT ECMAPIFolder::TableRowGetProp(void *lpProvider,
    const struct propVal *lpsPropValSrc, SPropValue *lpsPropValDst,
    void **lpBase, ULONG ulType)
{
	switch(lpsPropValSrc->ulPropTag) {
	case CHANGE_PROP_TYPE(PR_DISPLAY_TYPE, PT_ERROR):
		lpsPropValDst->Value.l = DT_FOLDER;
		lpsPropValDst->ulPropTag = PR_DISPLAY_TYPE;
		return hrSuccess;
	default:
		return MAPI_E_NOT_FOUND;
	}
}

HRESULT ECMAPIFolder::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMAPIFolder, this);
	REGISTER_INTERFACE2(ECMAPIContainer, this);
	REGISTER_INTERFACE2(ECMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMAPIFolder, this);
	REGISTER_INTERFACE2(IMAPIContainer, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	REGISTER_INTERFACE2(IFolderSupport, this);
	REGISTER_INTERFACE2(IECSecurity, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMAPIFolder::HrSetPropStorage(IECPropStorage *storage, BOOL fLoadProps)
{
	ULONG ulEventMask = fnevObjectModified  | fnevObjectDeleted | fnevObjectMoved | fnevObjectCreated;
	object_ptr<WSMAPIPropStorage> lpMAPIPropStorage;
	ULONG cbEntryId;
	LPENTRYID lpEntryId = NULL;

	auto hr = HrAllocAdviseSink(AdviseECFolderCallback, this, &~m_lpFolderAdviseSink);
	if (hr != hrSuccess)
		return hr;
	hr = storage->QueryInterface(IID_WSMAPIPropStorage, &~lpMAPIPropStorage);
	if (hr != hrSuccess)
		return hr;
	hr = lpMAPIPropStorage->GetEntryIDByRef(&cbEntryId, &lpEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = GetMsgStore()->InternalAdvise(cbEntryId, lpEntryId, ulEventMask, m_lpFolderAdviseSink, &m_ulConnection);
	if (hr == MAPI_E_NO_SUPPORT)
		hr = hrSuccess;			// there is no spoon
	else if (hr != hrSuccess)
		return hr;
	else
		lpMAPIPropStorage->RegisterAdvise(ulEventMask, m_ulConnection);
	return ECGenericProp::HrSetPropStorage(storage, fLoadProps);
}

HRESULT ECMAPIFolder::SetEntryId(ULONG cbEntryId, const ENTRYID *lpEntryId)
{
	return ECGenericProp::SetEntryId(cbEntryId, lpEntryId);
}

HRESULT ECMAPIFolder::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	if (lpiid == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	SPropValuePtr ptrSK, ptrDisplay;
	if(ulPropTag == PR_CONTAINER_CONTENTS) {
		if (*lpiid == IID_IMAPITable)
			return GetContentsTable(ulInterfaceOptions, reinterpret_cast<IMAPITable **>(lppUnk));
	} else if(ulPropTag == PR_FOLDER_ASSOCIATED_CONTENTS) {
		if (*lpiid == IID_IMAPITable)
			return GetContentsTable(ulInterfaceOptions | MAPI_ASSOCIATED, reinterpret_cast<IMAPITable **>(lppUnk));
	} else if(ulPropTag == PR_CONTAINER_HIERARCHY) {
		if(*lpiid == IID_IMAPITable)
			return GetHierarchyTable(ulInterfaceOptions, reinterpret_cast<IMAPITable **>(lppUnk));
	} else if(ulPropTag == PR_RULES_TABLE) {
		if(*lpiid == IID_IExchangeModifyTable)
			return ECExchangeModifyTable::CreateRulesTable(this, ulInterfaceOptions, reinterpret_cast<IExchangeModifyTable **>(lppUnk));
	} else if(ulPropTag == PR_ACL_TABLE) {
		if(*lpiid == IID_IExchangeModifyTable)
			return ECExchangeModifyTable::CreateACLTable(this, ulInterfaceOptions, reinterpret_cast<IExchangeModifyTable **>(lppUnk));
	} else if(ulPropTag == PR_COLLECTOR) {
		if(*lpiid == IID_IExchangeImportHierarchyChanges)
			return ECExchangeImportHierarchyChanges::Create(this, reinterpret_cast<IExchangeImportHierarchyChanges **>(lppUnk));
		else if(*lpiid == IID_IExchangeImportContentsChanges)
			return ECExchangeImportContentsChanges::Create(this, reinterpret_cast<IExchangeImportContentsChanges **>(lppUnk));
	} else if(ulPropTag == PR_HIERARCHY_SYNCHRONIZER) {
		auto hr = HrGetOneProp(this, PR_SOURCE_KEY, &~ptrSK);
		if(hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(this, PR_DISPLAY_NAME_W, &~ptrDisplay);
		if (hr != hrSuccess)
			/* ignore error */;
		return ECExchangeExportChanges::Create(GetMsgStore(), *lpiid,
		     std::string(reinterpret_cast<const char *>(ptrSK->Value.bin.lpb), ptrSK->Value.bin.cb),
		     ptrDisplay == nullptr ? L"" : ptrDisplay->Value.lpszW,
		     ICS_SYNC_HIERARCHY, reinterpret_cast<IExchangeExportChanges **>(lppUnk));
	} else if(ulPropTag == PR_CONTENTS_SYNCHRONIZER) {
		auto hr = HrGetOneProp(this, PR_SOURCE_KEY, &~ptrSK);
		if(hr != hrSuccess)
			return hr;
		auto dsp = HrGetOneProp(this, PR_DISPLAY_NAME, &~ptrDisplay) == hrSuccess ?
		           ptrDisplay->Value.lpszW : L"";
		return ECExchangeExportChanges::Create(GetMsgStore(),
		     *lpiid, std::string(reinterpret_cast<const char *>(ptrSK->Value.bin.lpb), ptrSK->Value.bin.cb),
		     dsp, ICS_SYNC_CONTENTS, reinterpret_cast<IExchangeExportChanges **>(lppUnk));
	} else {
		return ECMAPIProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	}
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMAPIFolder::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyTo(&IID_IMAPIFolder, static_cast<IMAPIFolder *>(this), ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECMAPIFolder::CopyProps(const SPropTagArray *lpIncludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyProps(&IID_IMAPIFolder, static_cast<IMAPIFolder *>(this), lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECMAPIFolder::SetProps(ULONG cValues, const SPropValue *lpPropArray,
    SPropProblemArray **lppProblems)
{
	auto hr = ECMAPIContainer::SetProps(cValues, lpPropArray, lppProblems);
	if (hr != hrSuccess)
		return hr;
	if (m_transact)
		return hrSuccess;
	/* MSDN be like: "MAPI Folders do not support transactions" */
	return ECMAPIContainer::SaveChanges(KEEP_OPEN_READWRITE);
}

HRESULT ECMAPIFolder::DeleteProps(const SPropTagArray *lpPropTagArray,
    SPropProblemArray **lppProblems)
{
	auto hr = ECMAPIContainer::DeleteProps(lpPropTagArray, lppProblems);
	if (hr != hrSuccess)
		return hr;
	if (m_transact)
		return hrSuccess;
	return ECMAPIContainer::SaveChanges(KEEP_OPEN_READWRITE);
}

HRESULT ECMAPIFolder::SaveChanges(ULONG ulFlags)
{
	if (!m_transact)
		return hrSuccess;
	return ECMAPIContainer::SaveChanges(ulFlags);
}

HRESULT ECMAPIFolder::SetSearchCriteria(const SRestriction *lpRestriction,
    const ENTRYLIST *lpContainerList, ULONG ulSearchFlags)
{
	if (lpFolderOps == NULL)
		return MAPI_E_NO_SUPPORT;
	return lpFolderOps->HrSetSearchCriteria(lpContainerList, lpRestriction, ulSearchFlags);
}

HRESULT ECMAPIFolder::GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState)
{
	if (lpFolderOps == NULL)
		return MAPI_E_NO_SUPPORT;
	// FIXME ulFlags ignore
	return lpFolderOps->HrGetSearchCriteria(lppContainerList, lppRestriction, lpulSearchState);
}

HRESULT ECMAPIFolder::CreateMessage(LPCIID lpInterface, ULONG ulFlags, LPMESSAGE *lppMessage)
{
    return CreateMessageWithEntryID(lpInterface, ulFlags, 0, NULL, lppMessage);
}

HRESULT ECMAPIFolder::CreateMessageWithEntryID(const IID *lpInterface,
    ULONG ulFlags, ULONG cbEntryID, const ENTRYID *lpEntryID,
    IMessage **lppMessage)
{
	object_ptr<ECMessage> lpMessage;
	ecmem_ptr<MAPIUID> lpMapiUID;
	ULONG		cbNewEntryId = 0;
	ecmem_ptr<ENTRYID> lpNewEntryId;
	SPropValue	sPropValue[3];
	object_ptr<IECPropStorage> storage;

	if (!fModify)
		return MAPI_E_NO_ACCESS;
	auto hr = ECMessage::Create(GetMsgStore(), true, true, ulFlags & MAPI_ASSOCIATED, false, nullptr, &~lpMessage);
	if(hr != hrSuccess)
		return hr;

	if (cbEntryID == 0 || lpEntryID == nullptr ||
	    HrCompareEntryIdWithStoreGuid(cbEntryID, lpEntryID, &(GetMsgStore()->GetStoreGuid())) != hrSuccess) {
		// No entryid passed or bad entryid passed, create one
		hr = HrCreateEntryId(GetMsgStore()->GetStoreGuid(), MAPI_MESSAGE, &cbNewEntryId, &~lpNewEntryId);
		if (hr != hrSuccess)
			return hr;
		hr = lpMessage->SetEntryId(cbNewEntryId, lpNewEntryId);
		if (hr != hrSuccess)
			return hr;
		hr = GetMsgStore()->lpTransport->HrOpenPropStorage(m_cbEntryId,
		     m_lpEntryId, cbNewEntryId, lpNewEntryId,
		     ulFlags & MAPI_ASSOCIATED, &~storage);
		if(hr != hrSuccess)
			return hr;
	} else {
		// use the passed entryid
        hr = lpMessage->SetEntryId(cbEntryID, lpEntryID);
        if(hr != hrSuccess)
			return hr;
		hr = GetMsgStore()->lpTransport->HrOpenPropStorage(m_cbEntryId,
		     m_lpEntryId, cbEntryID, lpEntryID,
		     ulFlags & MAPI_ASSOCIATED, &~storage);
		if(hr != hrSuccess)
			return hr;
    }

	hr = lpMessage->HrSetPropStorage(storage, false);
	if(hr != hrSuccess)
		return hr;
	// Load an empty property set
	hr = lpMessage->HrLoadEmptyProps();
	if(hr != hrSuccess)
		return hr;
	//Set defaults
	// Same as ECAttach::OpenProperty
	hr = ECAllocateBuffer(sizeof(MAPIUID), &~lpMapiUID);
	if (hr != hrSuccess)
		return hr;
	hr = GetMsgStore()->lpSupport->NewUID(lpMapiUID);
	if(hr != hrSuccess)
		return hr;

	sPropValue[0].ulPropTag = PR_MESSAGE_FLAGS;
	sPropValue[0].Value.l = MSGFLAG_UNSENT | MSGFLAG_READ;
	sPropValue[1].ulPropTag = PR_MESSAGE_CLASS_A;
	sPropValue[1].Value.lpszA = const_cast<char *>("IPM");
	sPropValue[2].ulPropTag = PR_SEARCH_KEY;
	sPropValue[2].Value.bin.cb = sizeof(MAPIUID);
	sPropValue[2].Value.bin.lpb = reinterpret_cast<BYTE *>(lpMapiUID.get());
	lpMessage->SetProps(3, sPropValue, NULL);

	// We don't actually create the object until savechanges is called, so remember in which
	// folder it was created
	hr = Util::HrCopyEntryId(m_cbEntryId, m_lpEntryId, &lpMessage->m_cbParentID, &~lpMessage->m_lpParentID);
	if(hr != hrSuccess)
		return hr;
	hr = lpMessage->QueryInterface(lpInterface != nullptr ? *lpInterface : IID_IMessage, reinterpret_cast<void **>(lppMessage));
	AddChild(lpMessage);
	return hr;
}

HRESULT ECMAPIFolder::CopyMessages(LPENTRYLIST lpMsgList, LPCIID lpInterface, LPVOID lpDestFolder, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	return CopyMessages2(ECSTORE_TYPE_PRIVATE, lpMsgList, lpInterface,
	       lpDestFolder, ulUIParam, lpProgress, ulFlags);
}

HRESULT ECMAPIFolder::CopyMessages2(unsigned int ftype, ENTRYLIST *lpMsgList,
    const IID *lpInterface, void *lpDestFolder, unsigned int ulUIParam,
    IMAPIProgress *lpProgress, unsigned int ulFlags)
{
	if (lpMsgList == nullptr || lpMsgList->cValues == 0)
		return hrSuccess;
	if (lpMsgList->lpbin == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (ftype != ECSTORE_TYPE_PRIVATE && ftype != ECSTORE_TYPE_PUBLIC)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = hrSuccess, hrEC = hrSuccess;
	object_ptr<IMAPIFolder> lpMapiFolder;
	ecmem_ptr<SPropValue> lpDestPropArray;
	ecmem_ptr<ENTRYLIST> lpMsgListEC, lpMsgListSupport;
	GUID guidFolder, guidMsg;

	// FIXME progress bar
	//Get the interface of destinationfolder
	if(lpInterface == NULL || *lpInterface == IID_IMAPIFolder)
		lpMapiFolder.reset(static_cast<IMAPIFolder *>(lpDestFolder));
	else if(*lpInterface == IID_IMAPIContainer)
		hr = ((IMAPIContainer *)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else if(*lpInterface == IID_IUnknown)
		hr = ((IUnknown *)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else if(*lpInterface == IID_IMAPIProp)
		hr = ((IMAPIProp *)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else
		hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
	if(hr != hrSuccess)
		return hr;

	// Get the destination entry ID, and check for favories public folders, so get PR_ORIGINAL_ENTRYID first.
	if (ftype == ECSTORE_TYPE_PRIVATE)
		hr = HrGetOneProp(lpMapiFolder, PR_ORIGINAL_ENTRYID, &~lpDestPropArray);
	if (hr != hrSuccess || ftype == ECSTORE_TYPE_PUBLIC)
		hr = HrGetOneProp(lpMapiFolder, PR_ENTRYID, &~lpDestPropArray);
	if (hr != hrSuccess)
		return hr;
	unsigned int result = false;
	if (ftype == ECSTORE_TYPE_PUBLIC &&
	    static_cast<ECMsgStorePublic *>(GetMsgStore())->ComparePublicEntryId(ePE_PublicFolders,
	    lpDestPropArray[0].Value.bin.cb, reinterpret_cast<const ENTRYID *>(lpDestPropArray[0].Value.bin.lpb), &result) == hrSuccess &&
	    result == true)
		return MAPI_E_NO_ACCESS;

	// Check if the destination entryid is a kopano entryid and if there is a folder transport
	if (!IsKopanoEntryId(lpDestPropArray[0].Value.bin.cb, lpDestPropArray[0].Value.bin.lpb) ||
	    lpFolderOps == nullptr) {
		// Do copy with the storeobject
		// Copy between two or more different stores
		hr = GetMsgStore()->lpSupport->CopyMessages(&IID_IMAPIFolder,
		     static_cast<IMAPIFolder *>(this), lpMsgList, lpInterface,
		     lpDestFolder, ulUIParam, lpProgress, ulFlags);
		return hr == hrSuccess ? hrEC : hr;
	}

	hr = HrGetStoreGuidFromEntryId(lpDestPropArray[0].Value.bin.cb, lpDestPropArray[0].Value.bin.lpb, &guidFolder);
	if(hr != hrSuccess)
		return hr;
	// Allocate memory for support list and kopano list
	hr = ECAllocateBuffer(sizeof(ENTRYLIST), &~lpMsgListEC);
	if (hr != hrSuccess)
		return hr;
	lpMsgListEC->cValues = 0;
	hr = ECAllocateMore(sizeof(SBinary) * lpMsgList->cValues, lpMsgListEC, reinterpret_cast<void **>(&lpMsgListEC->lpbin));
	if (hr != hrSuccess)
		return hr;
	hr = ECAllocateBuffer(sizeof(ENTRYLIST), &~lpMsgListSupport);
	if (hr != hrSuccess)
		return hr;
	lpMsgListSupport->cValues = 0;
	hr = ECAllocateMore(sizeof(SBinary) * lpMsgList->cValues, lpMsgListSupport, reinterpret_cast<void **>(&lpMsgListSupport->lpbin));
	if (hr != hrSuccess)
		return hr;
	//FIXME
	//hr = lpMapiFolder->SetReadFlags(GENERATE_RECEIPT_ONLY);
	//if (hr != hrSuccess)
		//goto exit;

	// Check if right store
	for (unsigned int i = 0; i < lpMsgList->cValues; ++i) {
		hr = HrGetStoreGuidFromEntryId(lpMsgList->lpbin[i].cb, lpMsgList->lpbin[i].lpb, &guidMsg);
		// check if the message in the store of the folder (serverside copy possible)
		if (hr == hrSuccess && IsKopanoEntryId(lpMsgList->lpbin[i].cb, lpMsgList->lpbin[i].lpb) && memcmp(&guidMsg, &guidFolder, sizeof(MAPIUID)) == 0)
			lpMsgListEC->lpbin[lpMsgListEC->cValues++] = lpMsgList->lpbin[i]; // cheap copy
		else
			lpMsgListSupport->lpbin[lpMsgListSupport->cValues++] = lpMsgList->lpbin[i]; // cheap copy
		hr = hrSuccess;
	}
	if (lpMsgListEC->cValues > 0)
	{
		hr = lpFolderOps->HrCopyMessage(lpMsgListEC,
		     lpDestPropArray[0].Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(lpDestPropArray[0].Value.bin.lpb),
		     ulFlags, 0);
		if(FAILED(hr))
			return hr;
		hrEC = hr;
	}
	if (lpMsgListSupport->cValues > 0)
	{
		hr = GetMsgStore()->lpSupport->CopyMessages(&IID_IMAPIFolder,
		     static_cast<IMAPIFolder *>(this), lpMsgListSupport,
		     lpInterface, lpDestFolder, ulUIParam, lpProgress, ulFlags);
		if(FAILED(hr))
			return hr;
	}

	return (hr == hrSuccess)?hrEC:hr;
}

HRESULT ECMAPIFolder::DeleteMessages(LPENTRYLIST lpMsgList, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	if (lpMsgList == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (!ValidateZEntryList(lpMsgList, MAPI_MESSAGE))
		return MAPI_E_INVALID_ENTRYID;
	// FIXME progress bar
	return GetMsgStore()->lpTransport->HrDeleteObjects(ulFlags, lpMsgList, 0);
}

HRESULT ECMAPIFolder::CreateFolder(ULONG ulFolderType,
    const TCHAR *lpszFolderName, const TCHAR *lpszFolderComment,
    const IID *lpInterface, ULONG ulFlags, IMAPIFolder **lppFolder)
{
	unsigned int cbEntryId = 0, objtype = 0;
	ecmem_ptr<ENTRYID> lpEntryId;
	object_ptr<IMAPIFolder> lpFolder;

	// SC TODO: new code:
	// create new lpFolder object (load empty props ?)
	// create entryid and set it
	// create storage and set it
	// set props (comment)
	// save changes(keep open readwrite)  <- the only call to the server

	if (lpFolderOps == nullptr)
		return MAPI_E_NO_SUPPORT;

	// Create the actual folder on the server
	auto hr = lpFolderOps->HrCreateFolder(ulFolderType,
	          convstring(lpszFolderName, ulFlags),
	          convstring(lpszFolderComment, ulFlags), ulFlags & OPEN_IF_EXISTS,
	          0, nullptr, 0, nullptr, &cbEntryId, &~lpEntryId);
	if(hr != hrSuccess)
		return hr;

	// Open the folder we just created
	hr = GetMsgStore()->OpenEntry(cbEntryId, lpEntryId, lpInterface,
	     MAPI_MODIFY | MAPI_DEFERRED_ERRORS, &objtype, &~lpFolder);
	if(hr != hrSuccess)
		return hr;
	*lppFolder = lpFolder.release();
	return hrSuccess;
}

/**
 * Create the specified folder set.
 *
 * Note that if the server does not support batch folder creation, this will
 * automatically fall back to sequential folder creation.
 */
HRESULT ECMAPIFolder::create_folders(std::vector<ECFolder> &folders)
{
	if (lpFolderOps == nullptr)
		return MAPI_E_NO_SUPPORT;

	const auto count = folders.size();
	std::vector<std::pair<unsigned int, ecmem_ptr<ENTRYID>>> entry_ids(count);
	std::vector<WSMAPIFolderOps::WSFolder> batch(count);
	for (unsigned int i = 0; i < count; ++i) {
		auto &src          = folders[i];
		auto &dst          = batch[i];
		dst.folder_type    = src.folder_type;
		dst.name           = convstring(src.name, src.flags);
		dst.comment        = convstring(src.comment, src.flags);
		dst.open_if_exists = src.flags & OPEN_IF_EXISTS;
		dst.sync_id        = 0;
		dst.sourcekey      = nullptr;
		dst.m_cbNewEntryId = 0;
		dst.m_lpNewEntryId = nullptr;
		dst.m_lpcbEntryId  = &entry_ids[i].first;
		dst.m_lppEntryId   = &~entry_ids[i].second;
	}

	auto ret = lpFolderOps->create_folders(batch);
	if (ret == MAPI_E_NETWORK_ERROR) {
		/*
		 * If the server does not support creating a batch of folders
		 * at once, fall back to sequential creation.
		 */
		for (unsigned int i = 0; i < count; ++i) {
			const ECFolder &f = folders[i];
			/* Create the actual folder on the server */
			ret = lpFolderOps->HrCreateFolder(f.folder_type,
			      convstring(f.name, f.flags),
			      convstring(f.comment, f.flags),
			      f.flags & OPEN_IF_EXISTS, 0, nullptr, 0, nullptr,
			      &entry_ids[i].first, &~entry_ids[i].second);
			if (ret != hrSuccess)
				return ret;
		}
	} else if (ret != hrSuccess) {
		return ret;
	}

	for (unsigned int i = 0; i < count; ++i) {
		unsigned int objtype = 0;
		/* Open the folder we just created */
		ret = GetMsgStore()->OpenEntry(entry_ids[i].first,
		      entry_ids[i].second, folders[i].interface,
		      MAPI_MODIFY | MAPI_DEFERRED_ERRORS, &objtype,
		      &~folders[i].folder);
		if (ret != hrSuccess)
			return ret;
	}
	return hrSuccess;
}

HRESULT ECMAPIFolder::CopyFolder(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, void *lpDestFolder, const TCHAR *lpszNewFolderName,
    ULONG_PTR ulUIParam, IMAPIProgress *lpProgress, ULONG ulFlags)
{
	return CopyFolder2(cbEntryID, lpEntryID, lpInterface, lpDestFolder, lpszNewFolderName, ulUIParam, lpProgress, ulFlags, false);
}

HRESULT ECMAPIFolder::CopyFolder2(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, void *lpDestFolder, const TCHAR *lpszNewFolderName,
    ULONG_PTR ulUIParam, IMAPIProgress *lpProgress, ULONG ulFlags, bool is_public)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPIFolder> lpMapiFolder;
	ecmem_ptr<SPropValue> lpPropArray;
	GUID guidDest, guidFrom;

	//Get the interface of destinationfolder
	if(lpInterface == NULL || *lpInterface == IID_IMAPIFolder)
		lpMapiFolder.reset(static_cast<IMAPIFolder *>(lpDestFolder));
	else if(*lpInterface == IID_IMAPIContainer)
		hr = ((IMAPIContainer*)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else if(*lpInterface == IID_IUnknown)
		hr = ((IUnknown*)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else if(*lpInterface == IID_IMAPIProp)
		hr = ((IMAPIProp*)lpDestFolder)->QueryInterface(IID_IMAPIFolder, &~lpMapiFolder);
	else
		hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
	if(hr != hrSuccess)
		return hr;

	// Get the destination entry ID
	hr = HrGetOneProp(lpMapiFolder, PR_ENTRYID, &~lpPropArray);
	if(hr != hrSuccess)
		return hr;

	// Check if it's  the same store of kopano so we can copy/move fast
	if (!IsKopanoEntryId(cbEntryID, lpEntryID) ||
	    !IsKopanoEntryId(lpPropArray[0].Value.bin.cb, lpPropArray[0].Value.bin.lpb) ||
	    HrGetStoreGuidFromEntryId(cbEntryID, lpEntryID, &guidFrom) != hrSuccess ||
	    HrGetStoreGuidFromEntryId(lpPropArray[0].Value.bin.cb, lpPropArray[0].Value.bin.lpb, &guidDest) != hrSuccess ||
	    memcmp(&guidFrom, &guidDest, sizeof(GUID)) != 0 ||
	    lpFolderOps == nullptr)
		/* Support object handled de copy/move */
		return GetMsgStore()->lpSupport->CopyFolder(&IID_IMAPIFolder,
		       static_cast<IMAPIFolder *>(this), cbEntryID, lpEntryID,
		       lpInterface, lpDestFolder, lpszNewFolderName, ulUIParam,
		       lpProgress, ulFlags);

	/* If the entryid is a public folder's entryid, just change the entryid to a server entryid */
	unsigned int ulResult = 0;
	if (is_public && static_cast<ECMsgStorePublic *>(GetMsgStore())->ComparePublicEntryId(ePE_PublicFolders,
	    lpPropArray[0].Value.bin.cb, reinterpret_cast<const ENTRYID *>(lpPropArray[0].Value.bin.lpb), &ulResult) == hrSuccess &&
	    ulResult == true) {
		hr = HrGetOneProp(lpMapiFolder, PR_ORIGINAL_ENTRYID, &~lpPropArray);
		if (hr != hrSuccess)
			return hr;
	}
	/* FIXME: Progressbar */
	return lpFolderOps->HrCopyFolder(cbEntryID, lpEntryID,
	       lpPropArray[0].Value.bin.cb, reinterpret_cast<ENTRYID *>(lpPropArray[0].Value.bin.lpb),
	       convstring(lpszNewFolderName, ulFlags), ulFlags, 0);
}

HRESULT ECMAPIFolder::DeleteFolder(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulUIParam, IMAPIProgress *, ULONG ulFlags)
{
	if (!ValidateZEntryId(cbEntryID, lpEntryID, MAPI_FOLDER))
		return MAPI_E_INVALID_ENTRYID;
	if (lpFolderOps == NULL)
		return MAPI_E_NO_SUPPORT;
	return lpFolderOps->HrDeleteFolder(cbEntryID, lpEntryID, ulFlags, 0);
}

HRESULT ECMAPIFolder::SetReadFlags(LPENTRYLIST lpMsgList, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	if ((ulFlags & ~(CLEAR_READ_FLAG | CLEAR_NRN_PENDING | CLEAR_RN_PENDING | GENERATE_RECEIPT_ONLY | MAPI_DEFERRED_ERRORS | MESSAGE_DIALOG | SUPPRESS_RECEIPT)) != 0 ||
	    (ulFlags & (SUPPRESS_RECEIPT | CLEAR_READ_FLAG)) == (SUPPRESS_RECEIPT | CLEAR_READ_FLAG) ||
	    (ulFlags & (SUPPRESS_RECEIPT | CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY)) == (SUPPRESS_RECEIPT | CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY) ||
	    (ulFlags & (CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY)) == (CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY))
		return MAPI_E_INVALID_PARAMETER;
	if (lpFolderOps == nullptr)
		return MAPI_E_NO_SUPPORT;

	HRESULT		hr = hrSuccess;
	BOOL		bError = FALSE;
	unsigned int objtype = 0;
	// Progress bar
	unsigned int ulPGMin = 0, ulPGMax = 0, ulPGDelta = 0, ulPGFlags = 0;

	//FIXME: (GENERATE_RECEIPT_ONLY | SUPPRESS_RECEIPT) not yet implement ok on the server (update PR_READ_RECEIPT_REQUESTED to false)
	if( (!(ulFlags & (SUPPRESS_RECEIPT|CLEAR_READ_FLAG|CLEAR_NRN_PENDING|CLEAR_RN_PENDING)) || (ulFlags&GENERATE_RECEIPT_ONLY))&& lpMsgList){
		if((ulFlags&MESSAGE_DIALOG ) && lpProgress) {
			lpProgress->GetMin(&ulPGMin);
			lpProgress->GetMax(&ulPGMax);
			ulPGDelta = (ulPGMax-ulPGMin);
			lpProgress->GetFlags(&ulPGFlags);
		}

		for (ULONG i = 0; i < lpMsgList->cValues; ++i) {
			object_ptr<IMessage> lpMessage;
			if (OpenEntry(lpMsgList->lpbin[i].cb, reinterpret_cast<ENTRYID *>(lpMsgList->lpbin[i].lpb), &IID_IMessage, MAPI_MODIFY, &objtype, &~lpMessage) == hrSuccess) {
				if(lpMessage->SetReadFlag(ulFlags&~MESSAGE_DIALOG) != hrSuccess)
					bError = TRUE;
			}else
				bError = TRUE;

			// Progress bar
			if((ulFlags&MESSAGE_DIALOG ) && lpProgress) {
				if (ulPGFlags & MAPI_TOP_LEVEL)
					hr = lpProgress->Progress((int)((float)i * ulPGDelta / lpMsgList->cValues + ulPGMin), i, lpMsgList->cValues);
				else
					hr = lpProgress->Progress((int)((float)i * ulPGDelta / lpMsgList->cValues + ulPGMin), 0, 0);

				if(hr == MAPI_E_USER_CANCEL) {// MAPI_E_USER_CANCEL is user click on the Cancel button.
					hr = hrSuccess;
					bError = TRUE;
					return MAPI_W_PARTIAL_COMPLETION;
				}else if(hr != hrSuccess) {
					return hr;
				}
			}
		}
	}else {
		hr = lpFolderOps->HrSetReadFlags(lpMsgList, ulFlags, 0);
	}

	if(hr == hrSuccess && bError == TRUE)
		hr = MAPI_W_PARTIAL_COMPLETION;
	return hr;
}

HRESULT ECMAPIFolder::GetMessageStatus(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulFlags, ULONG *lpulMessageStatus)
{
	if (lpEntryID == nullptr || !IsKopanoEntryId(cbEntryID, lpEntryID))
		return MAPI_E_INVALID_ENTRYID;
	if (lpulMessageStatus == NULL)
		return MAPI_E_INVALID_OBJECT;
	if (lpFolderOps == NULL)
		return MAPI_E_NO_SUPPORT;
	return lpFolderOps->HrGetMessageStatus(cbEntryID, lpEntryID, ulFlags, lpulMessageStatus);
}

HRESULT ECMAPIFolder::SetMessageStatus(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulNewStatus, ULONG ulNewStatusMask, ULONG *lpulOldStatus)
{
	if (lpEntryID == nullptr || !IsKopanoEntryId(cbEntryID, lpEntryID))
		return MAPI_E_INVALID_ENTRYID;
	if (lpFolderOps == NULL)
		return MAPI_E_NO_SUPPORT;
	return lpFolderOps->HrSetMessageStatus(cbEntryID, lpEntryID, ulNewStatus, ulNewStatusMask, 0, lpulOldStatus);
}

HRESULT ECMAPIFolder::SaveContentsSort(const SSortOrderSet *lpSortCriteria, ULONG ulFlags)
{
	return MAPI_E_NO_ACCESS;
}

HRESULT ECMAPIFolder::EmptyFolder(ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	if((ulFlags &~ (DEL_ASSOCIATED | FOLDER_DIALOG | DELETE_HARD_DELETE)) != 0)
		return MAPI_E_INVALID_PARAMETER;
	if (lpFolderOps == NULL)
		return MAPI_E_NO_SUPPORT;
	return lpFolderOps->HrEmptyFolder(ulFlags, 0);
}

HRESULT ECMAPIFolder::GetProps(const SPropTagArray *lpPropTagArray,
    ULONG ulFlags, ULONG *lpcValues, SPropValue **lppPropArray)
{
	// Check if there is a storage needed because favorites and ipmsubtree of the public folder
	// doesn't have a prop storage.
	if(lpStorage != NULL) {
		auto hr = HrLoadProps();
		if (hr != hrSuccess)
			return hr;
	}
	return ECMAPIProp::GetProps(lpPropTagArray, ulFlags, lpcValues, lppPropArray);
}

HRESULT ECMAPIFolder::GetSupportMask(DWORD * pdwSupportMask)
{
	if (pdwSupportMask == NULL)
		return MAPI_E_INVALID_PARAMETER;
	*pdwSupportMask = FS_SUPPORTS_SHARING; //Indicates that the folder supports sharing.
	return hrSuccess;
}

HRESULT ECMAPIFolder::CreateMessageFromStream(ULONG ulFlags, ULONG ulSyncId,
    ULONG cbEntryID, const ENTRYID *lpEntryID,
    WSMessageStreamImporter **lppsStreamImporter)
{
	WSMessageStreamImporterPtr	ptrStreamImporter;
	auto hr = GetMsgStore()->lpTransport->HrGetMessageStreamImporter(ulFlags,
	          ulSyncId, cbEntryID, lpEntryID, m_cbEntryId, m_lpEntryId,
	          true, nullptr, &~ptrStreamImporter);
	if (hr != hrSuccess)
		return hr;
	*lppsStreamImporter = ptrStreamImporter.release();
	return hrSuccess;
}

HRESULT ECMAPIFolder::GetChangeInfo(ULONG cbEntryID, const ENTRYID *lpEntryID,
    SPropValue **lppPropPCL, SPropValue **lppPropCK)
{
	return lpFolderOps->HrGetChangeInfo(cbEntryID, lpEntryID, lppPropPCL, lppPropCK);
}

HRESULT ECMAPIFolder::UpdateMessageFromStream(ULONG ulSyncId, ULONG cbEntryID,
    const ENTRYID *lpEntryID, const SPropValue *lpConflictItems,
    WSMessageStreamImporter **lppsStreamImporter)
{
	WSMessageStreamImporterPtr	ptrStreamImporter;
	auto hr = GetMsgStore()->lpTransport->HrGetMessageStreamImporter(0,
	          ulSyncId, cbEntryID, lpEntryID, m_cbEntryId, m_lpEntryId,
	          false, lpConflictItems, &~ptrStreamImporter);
	if (hr != hrSuccess)
		return hr;
	*lppsStreamImporter = ptrStreamImporter.release();
	return hrSuccess;
}

HRESULT ECMAPIFolder::enable_transaction(bool x)
{
	HRESULT ret = hrSuccess;
	if (m_transact && !x) {
		/* It's being turned off now */
		for (auto c : lstChildren) {
			object_ptr<ECMAPIFolder> ecf;
			if (c->QueryInterface(IID_ECMAPIFolder, &~ecf) != hrSuccess)
				continue;
			ecf->enable_transaction(false);
		}
		ret = SaveChanges(KEEP_OPEN_READWRITE);
	}
	m_transact = x;
	return ret;
}
