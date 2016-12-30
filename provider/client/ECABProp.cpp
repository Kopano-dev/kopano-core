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
#include <kopano/ECInterfaceDefs.h>
#include "kcore.hpp"
#include "ECABProp.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include <kopano/ECDefs.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECDebug.h>

ECABProp::ECABProp(void *lpProvider, ULONG ulObjType, BOOL fModify,
    const char *szClassName) :
	ECGenericProp(lpProvider, ulObjType, fModify, szClassName)
{

	this->HrAddPropHandlers(PR_RECORD_KEY,		DefaultABGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_STORE_SUPPORT_MASK,	DefaultABGetProp,	DefaultSetPropComputed, (void*) this);
}

HRESULT ECABProp::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECABProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMAPIProp, &this->m_xMAPIProp);
	REGISTER_INTERFACE2(IUnknown, &this->m_xMAPIProp);
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

// Interface IMAPIProp
DEF_ULONGMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, QueryInterface, (REFIID, id), (void **, intf))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, GetLastError, (HRESULT, hError), (ULONG, ulFlags), (LPMAPIERROR *, lppMapiError))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, SaveChanges, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, GetProps, (const SPropTagArray *, lpPropTagArray), (ULONG, ulFlags), (ULONG *, lpcValues), (SPropValue **, lppPropArray))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, GetPropList, (ULONG, ulFlags), (LPSPropTagArray *, lppPropTagArray))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, OpenProperty, (ULONG, ulPropTag), (LPCIID, lpiid), (ULONG, ulInterfaceOptions), (ULONG, ulFlags), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, SetProps, (ULONG, cValues), (LPSPropValue, lpPropArray), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, DeleteProps, (LPSPropTagArray, lpPropTagArray), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, CopyTo, (ULONG, ciidExclude), (LPCIID, rgiidExclude), (LPSPropTagArray, lpExcludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, CopyProps, (LPSPropTagArray, lpIncludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, GetNamesFromIDs, (LPSPropTagArray *, pptaga), (LPGUID, lpguid), (ULONG, ulFlags), (ULONG *, pcNames), (LPMAPINAMEID **, pppNames))
DEF_HRMETHOD1(TRACE_MAPI, ECABProp, MAPIProp, GetIDsFromNames, (ULONG, cNames), (LPMAPINAMEID *, ppNames), (ULONG, ulFlags), (LPSPropTagArray *, pptaga))
