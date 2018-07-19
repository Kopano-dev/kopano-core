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
#include <kopano/memory.hpp>
#include <mapiguid.h>
#include <mapicode.h>
#include <mapiutil.h>
#include "ECAttach.h"
#include <kopano/ECGuid.h>

using namespace KC;

HRESULT ECAttachFactory::Create(ECMsgStore *lpMsgStore, ULONG ulObjType,
    BOOL fModify, ULONG ulAttachNum, const ECMAPIProp *lpRoot,
    ECAttach **lppAttach) const
{
	return ECAttach::Create(lpMsgStore, ulObjType, fModify, ulAttachNum, lpRoot, lppAttach);
}

ECAttach::ECAttach(ECMsgStore *lpMsgStore, ULONG objtype, BOOL modify,
    ULONG anum, const ECMAPIProp *lpRoot) :
	ECMAPIProp(lpMsgStore, objtype, modify, lpRoot, "IAttach"),
	ulAttachNum(anum)
{
	HrAddPropHandlers(PR_ATTACH_DATA_OBJ, GetPropHandler, SetPropHandler, this, true, false);
	HrAddPropHandlers(PR_ATTACH_SIZE, DefaultGetProp, DefaultSetPropComputed,	this, false, false);
	HrAddPropHandlers(PR_ATTACH_NUM, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_ENTRYID, GetPropHandler, DefaultSetPropComputed, this, false, false);
}

HRESULT ECAttach::Create(ECMsgStore *lpMsgStore, ULONG ulObjType, BOOL fModify,
    ULONG ulAttachNum, const ECMAPIProp *lpRoot, ECAttach **lppAttach)
{
	return alloc_wrap<ECAttach>(lpMsgStore, ulObjType, fModify, ulAttachNum, lpRoot)
	       .put(lppAttach);
}

HRESULT ECAttach::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECAttach, this);
	REGISTER_INTERFACE2(ECMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE3(IAttachment, IAttach, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	REGISTER_INTERFACE2(IECSingleInstance, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECAttach::SaveChanges(ULONG ulFlags)
{
	if (!fModify)
		return MAPI_E_NO_ACCESS;

	if (!m_props_loaded || lstProps.find(PROP_ID(PR_RECORD_KEY)) == lstProps.cend()) {
		GUID guid;
		SPropValue sPropVal;

		CoCreateGuid(&guid);

		sPropVal.ulPropTag = PR_RECORD_KEY;
		sPropVal.Value.bin.cb = sizeof(guid);
		sPropVal.Value.bin.lpb = (LPBYTE)&guid;
		auto hr = HrSetRealProp(&sPropVal);
		if (hr != hrSuccess)
			return hr;
	}
	return ECMAPIProp::SaveChanges(ulFlags);
}

HRESULT ECAttach::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	if (lpiid == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<ECMessage> lpMessage;
	object_ptr<IECPropStorage> lpParentStorage;
	SPropValue		sPropValue[3];
	ecmem_ptr<SPropValue> lpPropAttachType;
	ecmem_ptr<MAPIUID> lpMapiUID;
	ULONG ulAttachType = 0, ulObjId = 0;
	BOOL			fNew = FALSE;
	scoped_rlock lock(m_hMutexMAPIObject);

	// Get the attachement method
	if (HrGetOneProp(this, PR_ATTACH_METHOD, &~lpPropAttachType) == hrSuccess)
		ulAttachType = lpPropAttachType->Value.ul;
	// The client is creating a new attachment, which may be embedded. Fix for the next if check
	else if ((ulFlags & MAPI_CREATE) && PROP_ID(ulPropTag) == PROP_ID(PR_ATTACH_DATA_OBJ) && *lpiid == IID_IMessage)
		ulAttachType = ATTACH_EMBEDDED_MSG;

	if (ulAttachType != ATTACH_EMBEDDED_MSG ||
	    PROP_ID(ulPropTag) != PROP_ID(PR_ATTACH_DATA_OBJ) ||
	    *lpiid != IID_IMessage) {
		if (PROP_ID(ulPropTag) == PROP_ID(PR_ATTACH_DATA_OBJ))
			ulPropTag = CHANGE_PROP_TYPE(PR_ATTACH_DATA_OBJ, PT_BINARY);
		if (ulAttachType == ATTACH_OLE && *lpiid != IID_IStorage && *lpiid != IID_IStream)
			return MAPI_E_INTERFACE_NOT_SUPPORTED;
		return ECMAPIProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	}

	/* Client is opening an IMessage submessage */
	if (!m_sMapiObject->lstChildren.empty()) {
		fNew = FALSE; // Create the submessage object from my sSavedObject data
		ulObjId = (*m_sMapiObject->lstChildren.begin())->ulObjId;
	} else {
		if (!fModify || !(ulFlags & MAPI_CREATE))
			return MAPI_E_NO_ACCESS;
		fNew = TRUE; // new message in message
		ulObjId = 0;
	}

	auto hr = ECMessage::Create(GetMsgStore(), fNew, ulFlags & MAPI_MODIFY, 0, true, m_lpRoot, &~lpMessage);
	if (hr != hrSuccess)
		return hr;

	// Client side unique ID is 0. Attachment can only have 1 submessage
	hr = GetMsgStore()->lpTransport->HrOpenParentStorage(this, 0, ulObjId,
	     lpStorage->GetServerStorage(), &~lpParentStorage);
	if (hr != hrSuccess)
		return hr;
	hr = lpMessage->HrSetPropStorage(lpParentStorage, !fNew);
	if (hr != hrSuccess)
		return hr;

	if (fNew) {
		// Load an empty property set
		hr = lpMessage->HrLoadEmptyProps();
		if (hr != hrSuccess)
			return hr;

		//Set defaults
		// Same as ECMAPIFolder::CreateMessage
		hr = ECAllocateBuffer(sizeof(MAPIUID), &~lpMapiUID);
		if (hr != hrSuccess)
			return hr;
		hr = GetMsgStore()->lpSupport->NewUID(lpMapiUID);
		if (hr != hrSuccess)
			return hr;
		sPropValue[0].ulPropTag = PR_MESSAGE_FLAGS;
		sPropValue[0].Value.l = MSGFLAG_UNSENT | MSGFLAG_READ;
		sPropValue[1].ulPropTag = PR_MESSAGE_CLASS_A;
		sPropValue[1].Value.lpszA = const_cast<char *>("IPM");
		sPropValue[2].ulPropTag = PR_SEARCH_KEY;
		sPropValue[2].Value.bin.cb = sizeof(MAPIUID);
		sPropValue[2].Value.bin.lpb = reinterpret_cast<BYTE *>(lpMapiUID.get());
		lpMessage->SetProps(3, sPropValue, NULL);
	}
	hr = lpMessage->QueryInterface(IID_IMessage, (void **)lppUnk);
	AddChild(lpMessage);
	return hr;
}

HRESULT	ECAttach::GetPropHandler(ULONG ulPropTag, void *lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT hr = hrSuccess;
	auto lpAttach = static_cast<ECAttach *>(lpParam);
	SizedSPropTagArray(1, sPropArray);
	ULONG cValues = 0;
	ecmem_ptr<SPropValue> lpProps;

	switch(ulPropTag) {
	case PR_ATTACH_DATA_OBJ:
		sPropArray.cValues = 1;
		sPropArray.aulPropTag[0] = PR_ATTACH_METHOD;
		hr = lpAttach->GetProps(sPropArray, 0, &cValues, &~lpProps);
		if(hr == hrSuccess && cValues == 1 && lpProps[0].ulPropTag == PR_ATTACH_METHOD && (lpProps[0].Value.ul == ATTACH_EMBEDDED_MSG || lpProps[0].Value.ul == ATTACH_OLE) )
		{
			lpsPropValue->ulPropTag = PR_ATTACH_DATA_OBJ;
			lpsPropValue->Value.x = 1;
		}else
			hr = MAPI_E_NOT_FOUND;
	
		break;
	case PR_ATTACH_DATA_BIN:
		sPropArray.cValues = 1;
		sPropArray.aulPropTag[0] = PR_ATTACH_METHOD;
		hr = lpAttach->GetProps(sPropArray, 0, &cValues, &~lpProps);
		if (lpProps[0].Value.ul == ATTACH_OLE)
			hr = MAPI_E_NOT_FOUND;
		else
			// 8k limit
			hr = lpAttach->HrGetRealProp(PR_ATTACH_DATA_BIN, ulFlags, lpBase, lpsPropValue, 8192);
		break;
	case PR_ATTACH_NUM:
		lpsPropValue->ulPropTag = PR_ATTACH_NUM;
		lpsPropValue->Value.ul = lpAttach->ulAttachNum;
		break;
	case PR_ENTRYID:// ignore property
	default:
		hr = MAPI_E_NOT_FOUND;
	}
	return hr;
}

HRESULT	ECAttach::SetPropHandler(ULONG ulPropTag, void *lpProvider,
    const SPropValue *lpsPropValue, void *lpParam)
{
	auto lpAttach = static_cast<ECAttach *>(lpParam);
	switch (ulPropTag) {
	case PR_ATTACH_DATA_BIN:
		return lpAttach->HrSetRealProp(lpsPropValue);
	case PR_ATTACH_DATA_OBJ:
		return MAPI_E_COMPUTED;
	default:
		return MAPI_E_NOT_FOUND;
	}
	return MAPI_E_NOT_FOUND;
}

// Use the support object to do the copying
HRESULT ECAttach::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyTo(&IID_IAttachment, static_cast<IAttach *>(this),
	       ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress,
	       lpInterface, lpDestObj, ulFlags, lppProblems);
}

/**
 * Override for HrSetRealProp
 *
 * Overrides to detect changes to the single-instance property. If a change is detected,
 * the single-instance ID is reset since the data has now changed.
 */
HRESULT ECAttach::HrSetRealProp(const SPropValue *lpProp)
{
	scoped_rlock lock(m_hMutexMAPIObject);

	if (lpStorage == NULL)
		return MAPI_E_NOT_FOUND;
	if (!fModify)
		return MAPI_E_NO_ACCESS;
	return ECMAPIProp::HrSetRealProp(lpProp);
}

HRESULT ECAttach::HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject)
{
	if (lpsMapiObject->ulObjType != MAPI_MESSAGE)
		/* can only save messages in an attachment */
		return MAPI_E_INVALID_OBJECT;

	scoped_rlock lock(m_hMutexMAPIObject);
	if (!m_sMapiObject) {
		assert(m_sMapiObject != NULL);
		m_sMapiObject.reset(new MAPIOBJECT(0, 0, MAPI_MESSAGE));
	}

	// attachments can only have 1 sub-message
	auto iterSObj = m_sMapiObject->lstChildren.cbegin();
	if (iterSObj != m_sMapiObject->lstChildren.cend()) {
		delete *iterSObj;
		m_sMapiObject->lstChildren.erase(iterSObj);
	}
	m_sMapiObject->lstChildren.emplace(new MAPIOBJECT(*lpsMapiObject));
	return hrSuccess;
}
