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
#include <mapiguid.h>
#include <mapicode.h>
#include <mapiutil.h>

#include "ECAttach.h"

#include <kopano/ECGuid.h>
#include <kopano/ECDebug.h>
#include <kopano/ECInterfaceDefs.h>

HRESULT ECAttachFactory::Create(ECMsgStore *lpMsgStore, ULONG ulObjType, BOOL fModify, ULONG ulAttachNum, ECMAPIProp *lpRoot, ECAttach **lppAttach) const
{
	return ECAttach::Create(lpMsgStore, ulObjType, fModify, ulAttachNum, lpRoot, lppAttach);
}

ECAttach::ECAttach(ECMsgStore *lpMsgStore, ULONG ulObjType, BOOL fModify, ULONG ulAttachNum, ECMAPIProp *lpRoot) : ECMAPIProp(lpMsgStore, ulObjType, fModify, lpRoot, "IAttach")
{
	this->ulAttachNum = ulAttachNum;

	this->HrAddPropHandlers(PR_ATTACH_DATA_OBJ,	GetPropHandler,	SetPropHandler,	(void*) this, TRUE,  FALSE);	// Includes PR_ATTACH_DATA_BIN as type is ignored
	this->HrAddPropHandlers(PR_ATTACH_SIZE,		DefaultGetProp,	DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_ATTACH_NUM,		GetPropHandler,	DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	this->HrAddPropHandlers(PR_ENTRYID,			GetPropHandler,	DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
}

HRESULT ECAttach::Create(ECMsgStore *lpMsgStore, ULONG ulObjType, BOOL fModify, ULONG ulAttachNum, ECMAPIProp *lpRoot, ECAttach **lppAttach)
{
	HRESULT hr = hrSuccess;
	ECAttach *lpAttach = NULL;

	lpAttach = new ECAttach(lpMsgStore, ulObjType, fModify, ulAttachNum, lpRoot);

	hr = lpAttach->QueryInterface(IID_ECAttach, (void **)lppAttach);
	if (hr != hrSuccess)
		delete lpAttach;

	return hr;
}

HRESULT ECAttach::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECAttach, this);
	REGISTER_INTERFACE(IID_ECMAPIProp, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IAttachment, &this->m_xAttach);
	REGISTER_INTERFACE(IID_IMAPIProp, &this->m_xAttach);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xAttach);
	// @todo add IID_ISelectUnicode ?

	REGISTER_INTERFACE(IID_IECSingleInstance, &this->m_xECSingleInstance);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECAttach::SaveChanges(ULONG ulFlags)
{
	HRESULT hr;

	if (!fModify)
		return MAPI_E_NO_ACCESS;

	if (!lstProps || lstProps->find(PROP_ID(PR_RECORD_KEY)) == lstProps->end()) {
		GUID guid;
		SPropValue sPropVal;

		CoCreateGuid(&guid);

		sPropVal.ulPropTag = PR_RECORD_KEY;
		sPropVal.Value.bin.cb = sizeof(guid);
		sPropVal.Value.bin.lpb = (LPBYTE)&guid;

		hr = HrSetRealProp(&sPropVal);
		if (hr != hrSuccess)
			return hr;
	}
	return ECMAPIProp::SaveChanges(ulFlags);
}

HRESULT ECAttach::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk)
{
	HRESULT			hr = hrSuccess;
	ECMessage*		lpMessage = NULL;
	IECPropStorage*	lpParentStorage = NULL;

	SPropValue		sPropValue[3];
	LPSPropValue	lpPropAttachType = NULL;
	LPMAPIUID		lpMapiUID = NULL;
	ULONG			ulAttachType = 0;
	BOOL			fNew = FALSE;
	ULONG			ulObjId = 0;
	scoped_rlock lock(m_hMutexMAPIObject);

	if (lpiid == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Get the attachement method
	if(HrGetOneProp(&m_xAttach, PR_ATTACH_METHOD, &lpPropAttachType) == hrSuccess)
	{
		ulAttachType = lpPropAttachType->Value.ul;

		ECFreeBuffer(lpPropAttachType); lpPropAttachType = NULL;
	}
	// The client is creating a new attachment, which may be embedded. Fix for the next if check
	else if ((ulFlags & MAPI_CREATE) && PROP_ID(ulPropTag) == PROP_ID(PR_ATTACH_DATA_OBJ) && *lpiid == IID_IMessage) {
		ulAttachType = ATTACH_EMBEDDED_MSG;
	}

	if(ulAttachType == ATTACH_EMBEDDED_MSG && (PROP_ID(ulPropTag) == PROP_ID(PR_ATTACH_DATA_OBJ) && *lpiid == IID_IMessage)) {
		// Client is opening an IMessage submessage

		if (!m_sMapiObject->lstChildren->empty()) {
			fNew = FALSE;			// Create the submessage object from my sSavedObject data
			ulObjId = (*m_sMapiObject->lstChildren->begin())->ulObjId;
		} else {
			if(!fModify || !(ulFlags & MAPI_CREATE)) {
				hr = MAPI_E_NO_ACCESS;
				goto exit;
			}

			fNew = TRUE;			// new message in message
			ulObjId = 0;
		}

		hr = ECMessage::Create(this->GetMsgStore(), fNew, ulFlags & MAPI_MODIFY, 0, TRUE, m_lpRoot, &lpMessage);
		if(hr != hrSuccess)
			goto exit;

		// Client side unique ID is 0. Attachment can only have 1 submessage
		hr = this->GetMsgStore()->lpTransport->HrOpenParentStorage(this, 0, ulObjId, this->lpStorage->GetServerStorage(), &lpParentStorage);
		if(hr != hrSuccess)
			goto exit;

		hr = lpMessage->HrSetPropStorage(lpParentStorage, !fNew);
		if(hr != hrSuccess)
			goto exit;

		if (fNew) {
			// Load an empty property set
			hr = lpMessage->HrLoadEmptyProps();

			if(hr != hrSuccess)
				goto exit;

			//Set defaults
			// Same as ECMAPIFolder::CreateMessage
			ECAllocateBuffer(sizeof(MAPIUID), (void **) &lpMapiUID);

			hr = this->GetMsgStore()->lpSupport->NewUID(lpMapiUID);
			if(hr != hrSuccess)
				goto exit;

			sPropValue[0].ulPropTag = PR_MESSAGE_FLAGS;
			sPropValue[0].Value.l = MSGFLAG_UNSENT | MSGFLAG_READ;

			sPropValue[1].ulPropTag = PR_MESSAGE_CLASS_A;
			sPropValue[1].Value.lpszA = const_cast<char *>("IPM");
			
			sPropValue[2].ulPropTag = PR_SEARCH_KEY;
			sPropValue[2].Value.bin.cb = sizeof(MAPIUID);
			sPropValue[2].Value.bin.lpb = (LPBYTE)lpMapiUID;

			lpMessage->SetProps(3, sPropValue, NULL);
		}

		hr = lpMessage->QueryInterface(IID_IMessage, (void **)lppUnk);

		AddChild(lpMessage);

	} else {
		if(PROP_ID(ulPropTag) == PROP_ID(PR_ATTACH_DATA_OBJ))
			ulPropTag = PROP_TAG(PT_BINARY,PROP_ID(PR_ATTACH_DATA_OBJ));

		if(ulAttachType == ATTACH_OLE && (*lpiid != IID_IStorage && *lpiid != IID_IStream) ) {
			hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
			goto exit;
		}

		hr = ECMAPIProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	}

exit:
	if(lpParentStorage)
		lpParentStorage->Release();

	if(lpMessage)
		lpMessage->Release();

	if(lpMapiUID)
		ECFreeBuffer(lpMapiUID);
	return hr;
}

HRESULT	ECAttach::GetPropHandler(ULONG ulPropTag, void *lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT hr = hrSuccess;
	ECAttach *lpAttach = (ECAttach*)lpParam;

	SPropTagArray sPropArray;
	ULONG cValues = 0;
	LPSPropValue lpProps = NULL;

	switch(ulPropTag) {
	case PR_ATTACH_DATA_OBJ:
		sPropArray.cValues = 1;
		sPropArray.aulPropTag[0] = PR_ATTACH_METHOD;
		hr = lpAttach->GetProps(&sPropArray, 0, &cValues, &lpProps);
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
		hr = lpAttach->GetProps(&sPropArray, 0, &cValues, &lpProps);
		if(lpProps[0].Value.ul == ATTACH_OLE) {
			hr = MAPI_E_NOT_FOUND;
		}else {
			// 8k limit
			hr = lpAttach->HrGetRealProp(PR_ATTACH_DATA_BIN, ulFlags, lpBase, lpsPropValue, 8192);
		}
		break;
	case PR_ATTACH_NUM:
		lpsPropValue->ulPropTag = PR_ATTACH_NUM;
		lpsPropValue->Value.ul = lpAttach->ulAttachNum;
		break;
	case PR_ENTRYID:// ignore property
	default:
		hr = MAPI_E_NOT_FOUND;
	}

	if(lpProps){ ECFreeBuffer(lpProps); lpProps = NULL; }

	return hr;
}

HRESULT	ECAttach::SetPropHandler(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam)
{
	ECAttach *lpAttach = (ECAttach *)lpParam;
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
HRESULT ECAttach::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	return Util::DoCopyTo(&IID_IAttachment, &this->m_xAttach, ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

/**
 * Override for HrSetRealProp
 *
 * Overrides to detect changes to the single-instance property. If a change is detected,
 * the single-instance ID is reset since the data has now changed.
 */
HRESULT ECAttach::HrSetRealProp(LPSPropValue lpProp)
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
	ECMapiObjects::const_iterator iterSObj;
	scoped_rlock lock(m_hMutexMAPIObject);

	if (!m_sMapiObject) {
		assert(m_sMapiObject != NULL);
		AllocNewMapiObject(0, 0, MAPI_MESSAGE, &m_sMapiObject);
	}

	if (lpsMapiObject->ulObjType != MAPI_MESSAGE)
		// can only save messages in an attachment
		return MAPI_E_INVALID_OBJECT;

	// attachments can only have 1 sub-message
	iterSObj = m_sMapiObject->lstChildren->cbegin();
	if (iterSObj != m_sMapiObject->lstChildren->cend()) {
		FreeMapiObject(*iterSObj);
		m_sMapiObject->lstChildren->erase(iterSObj);
	}

	m_sMapiObject->lstChildren->insert(new MAPIOBJECT(lpsMapiObject));
	return hrSuccess;
}

// Proxy routines for IAttach
ULONG __stdcall ECAttach::xAttach::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "IAttach::AddRef", "");
	METHOD_PROLOGUE_(ECAttach , Attach);
	return pThis->AddRef();
}

ULONG __stdcall ECAttach::xAttach::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IAttach::Release", "");
	METHOD_PROLOGUE_(ECAttach , Attach);
	return pThis->Release();
}

DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, GetLastError, (HRESULT, hError), (ULONG, ulFlags), (LPMAPIERROR *, lppMapiError))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, SaveChanges, (ULONG, ulFlags))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, GetProps, (LPSPropTagArray, lpPropTagArray), (ULONG, ulFlags), (ULONG FAR *, lpcValues, LPSPropValue FAR *, lppPropArray))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, GetPropList, (ULONG, ulFlags), (LPSPropTagArray FAR *, lppPropTagArray))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, OpenProperty, (ULONG, ulPropTag), (LPCIID, lpiid), (ULONG, ulInterfaceOptions), (ULONG, ulFlags), (LPUNKNOWN FAR *, lppUnk))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, SetProps, (ULONG, cValues, LPSPropValue, lpPropArray), (LPSPropProblemArray FAR *, lppProblems))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, DeleteProps, (LPSPropTagArray, lpPropTagArray), (LPSPropProblemArray FAR *, lppProblems))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, CopyTo, (ULONG, ciidExclude, LPCIID, rgiidExclude), (LPSPropTagArray, lpExcludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray FAR *, lppProblems))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, CopyProps, (LPSPropTagArray, lpIncludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray FAR *, lppProblems))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, GetNamesFromIDs, (LPSPropTagArray *, pptaga), (LPGUID, lpguid), (ULONG, ulFlags), (ULONG *, pcNames, LPMAPINAMEID **, pppNames))
DEF_HRMETHOD(TRACE_MAPI, ECAttach, Attach, GetIDsFromNames, (ULONG, cNames, LPMAPINAMEID *, ppNames), (ULONG, ulFlags), (LPSPropTagArray *, pptaga))
