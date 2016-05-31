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

#include "kcore.hpp"
#include "ECABProp.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include <kopano/ECDefs.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECDebug.h>


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

ECABProp::ECABProp(void *lpProvider, ULONG ulObjType, BOOL fModify,
    const char *szClassName) :
	ECGenericProp(lpProvider, ulObjType, fModify, szClassName)
{

	this->HrAddPropHandlers(PR_RECORD_KEY,		DefaultABGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_STORE_SUPPORT_MASK,	DefaultABGetProp,	DefaultSetPropComputed, (void*) this);
}

HRESULT ECABProp::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECABProp, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IMAPIProp, &this->m_xMAPIProp);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xMAPIProp);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT	ECABProp::DefaultABGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT		hr = hrSuccess;
	ECABProp*	lpProp = (ECABProp *)lpParam;

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_RECORD_KEY):
		lpsPropValue->ulPropTag = PR_RECORD_KEY;

		if(lpProp->m_lpEntryId && lpProp->m_cbEntryId > 0) {
			lpsPropValue->Value.bin.cb = lpProp->m_cbEntryId;

			ECAllocateMore(lpsPropValue->Value.bin.cb, lpBase, (LPVOID *)&lpsPropValue->Value.bin.lpb);
			memcpy(lpsPropValue->Value.bin.lpb, lpProp->m_lpEntryId, lpsPropValue->Value.bin.cb);
		} else {
			hr = MAPI_E_NOT_FOUND;
		}
		break;
	case PROP_ID(PR_STORE_SUPPORT_MASK):
	{
		unsigned int ulClientVersion = -1;
		GetClientVersion(&ulClientVersion);

		// No real unicode support in outlook 2000 and xp
		if (ulClientVersion > CLIENT_VERSION_OLK2002) {
			lpsPropValue->Value.l = STORE_UNICODE_OK;
			lpsPropValue->ulPropTag = PR_STORE_SUPPORT_MASK;
		} else {
			hr = MAPI_E_NOT_FOUND;
		}
		break;
	}
	default:
		hr = lpProp->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
		break;
	}
	
	return hr;
}

HRESULT ECABProp::TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType)
{
	HRESULT hr = hrSuccess;

	switch(lpsPropValSrc->ulPropTag) {
		case PROP_TAG(PT_ERROR,PROP_ID(PR_AB_PROVIDER_ID)):
			lpsPropValDst->ulPropTag = PR_AB_PROVIDER_ID;

			lpsPropValDst->Value.bin.cb = sizeof(GUID);
			ECAllocateMore(sizeof(GUID), lpBase, (void**)&lpsPropValDst->Value.bin.lpb);

			memcpy(lpsPropValDst->Value.bin.lpb, &MUIDECSAB, sizeof(GUID));

			break;
		default:
			hr = MAPI_E_NOT_FOUND;
			break;
	}

	return hr;
}

ECABLogon* ECABProp::GetABStore()
{
	return (ECABLogon*)lpProvider;
}

////////////////////////////////////////////
// Interface IMAPIProp

HRESULT __stdcall ECABProp::xMAPIProp::QueryInterface(REFIID refiid, void ** lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

ULONG __stdcall ECABProp::xMAPIProp::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::AddRef", "");
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	return pThis->AddRef();
}

ULONG __stdcall ECABProp::xMAPIProp::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::Release", "");
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	return pThis->Release();
}

HRESULT __stdcall ECABProp::xMAPIProp::GetLastError(HRESULT hError, ULONG ulFlags, LPMAPIERROR * lppMapiError)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::GetLastError", "");
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->GetLastError(hError, ulFlags, lppMapiError);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::GetLastError", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::SaveChanges(ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::SaveChanges", "");
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->SaveChanges(ulFlags);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::SaveChanges", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG FAR * lpcValues, LPSPropValue FAR * lppPropArray)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::GetProps", "%s,  flags=%08X", PropNameFromPropTagArray(lpPropTagArray).c_str(), ulFlags);
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->GetProps(lpPropTagArray, ulFlags, lpcValues, lppPropArray);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::GetProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::GetPropList(ULONG ulFlags, LPSPropTagArray FAR * lppPropTagArray)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::GetPropList", "");
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->GetPropList(ulFlags, lppPropTagArray);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::GetPropList", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::OpenProperty", "proptag=%s, flags=%d, lpiid=%s, InterfaceOptions=%d", PropNameFromPropTag(ulPropTag).c_str(), ulFlags, (lpiid)?DBGGUIDToString(*lpiid).c_str():"NULL", ulInterfaceOptions);
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::OpenProperty", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::SetProps", "%s", PropNameFromPropArray(cValues, lpPropArray).c_str());
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->SetProps(cValues, lpPropArray, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::SetProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::DeleteProps", "%s", PropNameFromPropTagArray(lpPropTagArray).c_str());
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->DeleteProps(lpPropTagArray, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::DeleteProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::CopyTo", "");
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->CopyTo(ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::CopyTo", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::CopyProps", "");
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->CopyProps(lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::CopyProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::GetNamesFromIDs(LPSPropTagArray * pptaga, LPGUID lpguid, ULONG ulFlags, ULONG * pcNames, LPMAPINAMEID ** pppNames)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::GetNamesFromIDs", "");
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->GetNamesFromIDs(pptaga, lpguid, ulFlags, pcNames, pppNames);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::GetNamesFromIDs", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECABProp::xMAPIProp::GetIDsFromNames(ULONG cNames, LPMAPINAMEID * ppNames, ULONG ulFlags, LPSPropTagArray * pptaga)
{
	TRACE_MAPI(TRACE_ENTRY, "AB::IMAPIProp::GetIDsFromNames", "");
	METHOD_PROLOGUE_(ECABProp , MAPIProp);
	HRESULT hr = pThis->GetIDsFromNames(cNames, ppNames, ulFlags, pptaga);
	TRACE_MAPI(TRACE_RETURN, "AB::IMAPIProp::GetIDsFromNames", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}
