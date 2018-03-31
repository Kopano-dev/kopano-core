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

#include <kopano/ECDebug.h>
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
	this->isTransactedObject = FALSE;
}

ECMAPIFolder::~ECMAPIFolder()
{
	lpFolderOps.reset();
	if (m_ulConnection > 0)
		GetMsgStore()->m_lpNotifyClient->UnRegisterAdvise(m_ulConnection);
}

HRESULT ECMAPIFolder::Create(ECMsgStore *lpMsgStore, BOOL fModify, WSMAPIFolderOps *lpFolderOps, ECMAPIFolder **lppECMAPIFolder)
{
	return alloc_wrap<ECMAPIFolder>(lpMsgStore, fModify, lpFolderOps,
	       "IMAPIFolder").put(lppECMAPIFolder);
}

HRESULT ECMAPIFolder::GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
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
		if(lpFolder->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue) != hrSuccess)
		{
			// Don't return an error here: outlook is relying on PR_CONTENT_COUNT, etc being available at all times. Especially the
			// exit routine (which checks to see how many items are left in the outbox) will crash if PR_CONTENT_COUNT is MAPI_E_NOT_FOUND
			lpsPropValue->ulPropTag = ulPropTag;
			lpsPropValue->Value.ul = 0;
		}
		break;
	case PR_SUBFOLDERS:
		if(lpFolder->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue) != hrSuccess)
		{
			lpsPropValue->ulPropTag = PR_SUBFOLDERS;
			lpsPropValue->Value.b = FALSE;
		}
		break;
	case PR_ACCESS:
		if(lpFolder->HrGetRealProp(PR_ACCESS, ulFlags, lpBase, lpsPropValue) != hrSuccess)
		{
			lpsPropValue->ulPropTag = PR_ACCESS;
			lpsPropValue->Value.l = 0; // FIXME: tijdelijk voor test
		}
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

HRESULT	ECMAPIFolder::SetPropHandler(ULONG ulPropTag, void *lpProvider,
    const SPropValue *lpsPropValue, void *lpParam)
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
	HRESULT hr = hrSuccess;

	switch(lpsPropValSrc->ulPropTag) {

	case PROP_TAG(PT_ERROR,PROP_ID(PR_DISPLAY_TYPE)):
		lpsPropValDst->Value.l = DT_FOLDER;
		lpsPropValDst->ulPropTag = PR_DISPLAY_TYPE;
		break;
	
	default:
		hr = MAPI_E_NOT_FOUND;
	}

	return hr;
}

HRESULT	ECMAPIFolder::QueryInterface(REFIID refiid, void **lppInterface) 
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
	HRESULT hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
	SPropValuePtr ptrSK, ptrDisplay;
	
	if (lpiid == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if(ulPropTag == PR_CONTAINER_CONTENTS) {
		if (*lpiid == IID_IMAPITable)
			hr = GetContentsTable(ulInterfaceOptions, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_FOLDER_ASSOCIATED_CONTENTS) {
		if (*lpiid == IID_IMAPITable)
			hr = GetContentsTable( (ulInterfaceOptions|MAPI_ASSOCIATED), (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_CONTAINER_HIERARCHY) {
		if(*lpiid == IID_IMAPITable)
			hr = GetHierarchyTable(ulInterfaceOptions, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_RULES_TABLE) {
		if(*lpiid == IID_IExchangeModifyTable)
			hr = ECExchangeModifyTable::CreateRulesTable(this, ulInterfaceOptions, (LPEXCHANGEMODIFYTABLE*)lppUnk);
	} else if(ulPropTag == PR_ACL_TABLE) {
		if(*lpiid == IID_IExchangeModifyTable)
			hr = ECExchangeModifyTable::CreateACLTable(this, ulInterfaceOptions, (LPEXCHANGEMODIFYTABLE*)lppUnk);
	} else if(ulPropTag == PR_COLLECTOR) {
		if(*lpiid == IID_IExchangeImportHierarchyChanges)
			hr = ECExchangeImportHierarchyChanges::Create(this, (LPEXCHANGEIMPORTHIERARCHYCHANGES*)lppUnk);
		else if(*lpiid == IID_IExchangeImportContentsChanges)
			hr = ECExchangeImportContentsChanges::Create(this, (LPEXCHANGEIMPORTCONTENTSCHANGES*)lppUnk);
	} else if(ulPropTag == PR_HIERARCHY_SYNCHRONIZER) {
		hr = HrGetOneProp(this, PR_SOURCE_KEY, &~ptrSK);
		if(hr != hrSuccess)
			return hr;
		HrGetOneProp(this, PR_DISPLAY_NAME_W, &~ptrDisplay); // ignore error
		hr = ECExchangeExportChanges::Create(this->GetMsgStore(), *lpiid, std::string((const char*)ptrSK->Value.bin.lpb, ptrSK->Value.bin.cb), !ptrDisplay ? L"" : ptrDisplay->Value.lpszW, ICS_SYNC_HIERARCHY, (LPEXCHANGEEXPORTCHANGES*) lppUnk);
	} else if(ulPropTag == PR_CONTENTS_SYNCHRONIZER) {
		hr = HrGetOneProp(this, PR_SOURCE_KEY, &~ptrSK);
		if(hr != hrSuccess)
			return hr;
		auto dsp = HrGetOneProp(this, PR_DISPLAY_NAME, &~ptrDisplay) == hrSuccess ?
		           ptrDisplay->Value.lpszW : L"";
		hr = ECExchangeExportChanges::Create(this->GetMsgStore(),
		     *lpiid, std::string(reinterpret_cast<const char *>(ptrSK->Value.bin.lpb), ptrSK->Value.bin.cb),
		     dsp, ICS_SYNC_CONTENTS, reinterpret_cast<IExchangeExportChanges **>(lppUnk));
	} else {
		hr = ECMAPIProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	}
	return hr;
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
	HRESULT hr;

	hr = ECMAPIContainer::SetProps(cValues, lpPropArray, lppProblems);
	if (hr != hrSuccess)
		return hr;

	return ECMAPIContainer::SaveChanges(KEEP_OPEN_READWRITE);
}

HRESULT ECMAPIFolder::DeleteProps(const SPropTagArray *lpPropTagArray,
    SPropProblemArray **lppProblems)
{
	HRESULT hr;

	hr = ECMAPIContainer::DeleteProps(lpPropTagArray, lppProblems);
	if (hr != hrSuccess)
		return hr;

	return ECMAPIContainer::SaveChanges(KEEP_OPEN_READWRITE);
}

HRESULT ECMAPIFolder::SaveChanges(ULONG ulFlags)
{
	return hrSuccess;
}

HRESULT ECMAPIFolder::SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags)
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

HRESULT ECMAPIFolder::CreateMessageWithEntryID(LPCIID lpInterface, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID, LPMESSAGE *lppMessage)
{
	HRESULT		hr = hrSuccess;
	object_ptr<ECMessage> lpMessage;
	ecmem_ptr<MAPIUID> lpMapiUID;
	ULONG		cbNewEntryId = 0;
	ecmem_ptr<ENTRYID> lpNewEntryId;
	SPropValue	sPropValue[3];
	object_ptr<IECPropStorage> storage;

	if (!fModify)
		return MAPI_E_NO_ACCESS;
	hr = ECMessage::Create(this->GetMsgStore(), TRUE, TRUE, ulFlags & MAPI_ASSOCIATED, FALSE, nullptr, &~lpMessage);
	if(hr != hrSuccess)
		return hr;

    if(cbEntryID == 0 || lpEntryID == NULL || HrCompareEntryIdWithStoreGuid(cbEntryID, lpEntryID, &this->GetMsgStore()->GetStoreGuid()) != hrSuccess) {
		// No entryid passed or bad entryid passed, create one
		hr = HrCreateEntryId(GetMsgStore()->GetStoreGuid(), MAPI_MESSAGE, &cbNewEntryId, &~lpNewEntryId);
		if (hr != hrSuccess)
			return hr;
		hr = lpMessage->SetEntryId(cbNewEntryId, lpNewEntryId);
		if (hr != hrSuccess)
			return hr;
		hr = this->GetMsgStore()->lpTransport->HrOpenPropStorage(m_cbEntryId, m_lpEntryId, cbNewEntryId, lpNewEntryId, ulFlags & MAPI_ASSOCIATED, &~storage);
		if(hr != hrSuccess)
			return hr;
	} else {
		// use the passed entryid
        hr = lpMessage->SetEntryId(cbEntryID, lpEntryID);
        if(hr != hrSuccess)
			return hr;
		hr = this->GetMsgStore()->lpTransport->HrOpenPropStorage(m_cbEntryId, m_lpEntryId, cbEntryID, lpEntryID, ulFlags & MAPI_ASSOCIATED, &~storage);
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
	hr = this->GetMsgStore()->lpSupport->NewUID(lpMapiUID);
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
	if(lpInterface)
		hr = lpMessage->QueryInterface(*lpInterface, (void **)lppMessage);
	else
		hr = lpMessage->QueryInterface(IID_IMessage, (void **)lppMessage);

	AddChild(lpMessage);
	return hr;
}

HRESULT ECMAPIFolder::CopyMessages(LPENTRYLIST lpMsgList, LPCIID lpInterface, LPVOID lpDestFolder, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	HRESULT hrEC = hrSuccess;
	object_ptr<IMAPIFolder> lpMapiFolder;
	ecmem_ptr<SPropValue> lpDestPropArray;
	ecmem_ptr<ENTRYLIST> lpMsgListEC, lpMsgListSupport;
	unsigned int i;
	GUID		guidFolder;
	GUID		guidMsg;

	if(lpMsgList == NULL || lpMsgList->cValues == 0)
		return hrSuccess;
	if (lpMsgList->lpbin == nullptr)
		return MAPI_E_INVALID_PARAMETER;

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
	hr = HrGetOneProp(lpMapiFolder, PR_ORIGINAL_ENTRYID, &~lpDestPropArray);
	if (hr != hrSuccess)
		hr = HrGetOneProp(lpMapiFolder, PR_ENTRYID, &~lpDestPropArray);
	if (hr != hrSuccess)
		return hr;

	// Check if the destination entryid is a kopano entryid and if there is a folder transport
	if (!IsKopanoEntryId(lpDestPropArray[0].Value.bin.cb, lpDestPropArray[0].Value.bin.lpb) ||
	    lpFolderOps == nullptr) {
		// Do copy with the storeobject
		// Copy between two or more different stores
		hr = this->GetMsgStore()->lpSupport->CopyMessages(&IID_IMAPIFolder, static_cast<IMAPIFolder *>(this), lpMsgList, lpInterface, lpDestFolder, ulUIParam, lpProgress, ulFlags);
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
	hr = ECAllocateMore(sizeof(SBinary) * lpMsgList->cValues, lpMsgListEC, (void **)&lpMsgListEC->lpbin);
	if (hr != hrSuccess)
		return hr;
	hr = ECAllocateBuffer(sizeof(ENTRYLIST), &~lpMsgListSupport);
	if (hr != hrSuccess)
		return hr;
	lpMsgListSupport->cValues = 0;
	hr = ECAllocateMore(sizeof(SBinary) * lpMsgList->cValues, lpMsgListSupport, (void **)&lpMsgListSupport->lpbin);
	if (hr != hrSuccess)
		return hr;
	//FIXME
	//hr = lpMapiFolder->SetReadFlags(GENERATE_RECEIPT_ONLY);
	//if (hr != hrSuccess)
		//goto exit;

	// Check if right store	
	for (i = 0; i < lpMsgList->cValues; ++i) {
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
		hr = this->lpFolderOps->HrCopyMessage(lpMsgListEC, lpDestPropArray[0].Value.bin.cb, (LPENTRYID)lpDestPropArray[0].Value.bin.lpb, ulFlags, 0);
		if(FAILED(hr))
			return hr;
		hrEC = hr;
	}
	if (lpMsgListSupport->cValues > 0)
	{
		hr = this->GetMsgStore()->lpSupport->CopyMessages(&IID_IMAPIFolder, static_cast<IMAPIFolder *>(this), lpMsgListSupport, lpInterface, lpDestFolder, ulUIParam, lpProgress, ulFlags);
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
	return this->GetMsgStore()->lpTransport->HrDeleteObjects(ulFlags, lpMsgList, 0);
}

HRESULT ECMAPIFolder::CreateFolder(ULONG ulFolderType,
    const TCHAR *lpszFolderName, const TCHAR *lpszFolderComment,
    const IID *lpInterface, ULONG ulFlags, IMAPIFolder **lppFolder)
{
	HRESULT			hr = hrSuccess;
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
	hr = lpFolderOps->HrCreateFolder(ulFolderType,
	     convstring(lpszFolderName, ulFlags),
	     convstring(lpszFolderComment, ulFlags), ulFlags & OPEN_IF_EXISTS,
	     0, nullptr, 0, nullptr, &cbEntryId, &~lpEntryId);
	if(hr != hrSuccess)
		return hr;

	// Open the folder we just created
	hr = this->GetMsgStore()->OpenEntry(cbEntryId, lpEntryId, lpInterface, MAPI_MODIFY | MAPI_DEFERRED_ERRORS, &objtype, &~lpFolder);
	if(hr != hrSuccess)
		return hr;
	*lppFolder = lpFolder.release();
	return hrSuccess;
}

// @note if you change this function please look also at ECMAPIFolderPublic::CopyFolder
HRESULT ECMAPIFolder::CopyFolder(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, void *lpDestFolder, const TCHAR *lpszNewFolderName,
    ULONG_PTR ulUIParam, IMAPIProgress *lpProgress, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPIFolder> lpMapiFolder;
	ecmem_ptr<SPropValue> lpPropArray;
	GUID guidDest;
	GUID guidFrom;

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
	if( IsKopanoEntryId(cbEntryID, (LPBYTE)lpEntryID) && 
		IsKopanoEntryId(lpPropArray[0].Value.bin.cb, lpPropArray[0].Value.bin.lpb) &&
		HrGetStoreGuidFromEntryId(cbEntryID, (LPBYTE)lpEntryID, &guidFrom) == hrSuccess && 
		HrGetStoreGuidFromEntryId(lpPropArray[0].Value.bin.cb, lpPropArray[0].Value.bin.lpb, &guidDest) == hrSuccess &&
		memcmp(&guidFrom, &guidDest, sizeof(GUID)) == 0 &&
		lpFolderOps != NULL)
		//FIXME: Progressbar
		hr = this->lpFolderOps->HrCopyFolder(cbEntryID, lpEntryID, lpPropArray[0].Value.bin.cb, (LPENTRYID)lpPropArray[0].Value.bin.lpb, convstring(lpszNewFolderName, ulFlags), ulFlags, 0);
	else
		// Support object handled de copy/move
		hr = this->GetMsgStore()->lpSupport->CopyFolder(&IID_IMAPIFolder, static_cast<IMAPIFolder *>(this), cbEntryID, lpEntryID, lpInterface, lpDestFolder, lpszNewFolderName, ulUIParam, lpProgress, ulFlags);
	return hr;
}

HRESULT ECMAPIFolder::DeleteFolder(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulUIParam, IMAPIProgress *, ULONG ulFlags)
{
	if (!ValidateZEntryId(cbEntryID, reinterpret_cast<const BYTE *>(lpEntryID), MAPI_FOLDER))
		return MAPI_E_INVALID_ENTRYID;
	if (lpFolderOps == NULL)
		return MAPI_E_NO_SUPPORT;
	return this->lpFolderOps->HrDeleteFolder(cbEntryID, lpEntryID, ulFlags, 0);
}

HRESULT ECMAPIFolder::SetReadFlags(LPENTRYLIST lpMsgList, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	HRESULT		hr = hrSuccess;
	BOOL		bError = FALSE;
	unsigned int objtype = 0;

	// Progress bar
	ULONG ulPGMin = 0;
	ULONG ulPGMax = 0;
	ULONG ulPGDelta = 0;
	ULONG ulPGFlags = 0;
	
	if((ulFlags &~ (CLEAR_READ_FLAG | CLEAR_NRN_PENDING | CLEAR_RN_PENDING | GENERATE_RECEIPT_ONLY | MAPI_DEFERRED_ERRORS | MESSAGE_DIALOG | SUPPRESS_RECEIPT)) != 0 ||
		(ulFlags & (SUPPRESS_RECEIPT | CLEAR_READ_FLAG)) == (SUPPRESS_RECEIPT | CLEAR_READ_FLAG) ||
		(ulFlags & (SUPPRESS_RECEIPT | CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY)) == (SUPPRESS_RECEIPT | CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY) ||
		(ulFlags & (CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY)) == (CLEAR_READ_FLAG | GENERATE_RECEIPT_ONLY)	)
		return MAPI_E_INVALID_PARAMETER;
	if (lpFolderOps == nullptr)
		return MAPI_E_NO_SUPPORT;

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
	if (lpEntryID == nullptr || !IsKopanoEntryId(cbEntryID, reinterpret_cast<const BYTE *>(lpEntryID)))
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
	if (lpEntryID == nullptr || !IsKopanoEntryId(cbEntryID, reinterpret_cast<const BYTE *>(lpEntryID)))
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
	HRESULT hr;
	
	// Check if there is a storage needed because favorites and ipmsubtree of the public folder 
	// doesn't have a prop storage.
	if(lpStorage != NULL) {
		hr = HrLoadProps();
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

HRESULT ECMAPIFolder::CreateMessageFromStream(ULONG ulFlags, ULONG ulSyncId, ULONG cbEntryID, LPENTRYID lpEntryID, WSMessageStreamImporter **lppsStreamImporter)
{
	HRESULT hr;
	WSMessageStreamImporterPtr	ptrStreamImporter;

	hr = GetMsgStore()->lpTransport->HrGetMessageStreamImporter(ulFlags, ulSyncId, cbEntryID, lpEntryID, m_cbEntryId, m_lpEntryId, true, nullptr, &~ptrStreamImporter);
	if (hr != hrSuccess)
		return hr;

	*lppsStreamImporter = ptrStreamImporter.release();
	return hrSuccess;
}

HRESULT ECMAPIFolder::GetChangeInfo(ULONG cbEntryID, LPENTRYID lpEntryID, LPSPropValue *lppPropPCL, LPSPropValue *lppPropCK)
{
	return lpFolderOps->HrGetChangeInfo(cbEntryID, lpEntryID, lppPropPCL, lppPropCK);
}

HRESULT ECMAPIFolder::UpdateMessageFromStream(ULONG ulSyncId, ULONG cbEntryID, LPENTRYID lpEntryID, LPSPropValue lpConflictItems, WSMessageStreamImporter **lppsStreamImporter)
{
	HRESULT hr;
	WSMessageStreamImporterPtr	ptrStreamImporter;

	hr = GetMsgStore()->lpTransport->HrGetMessageStreamImporter(0, ulSyncId, cbEntryID, lpEntryID, m_cbEntryId, m_lpEntryId, false, lpConflictItems, &~ptrStreamImporter);
	if (hr != hrSuccess)
		return hr;

	*lppsStreamImporter = ptrStreamImporter.release();
	return hrSuccess;
}
