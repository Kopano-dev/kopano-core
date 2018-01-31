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

ECABProp::ECABProp(ECABLogon *lpProvider, ULONG ulObjType, BOOL fModify,
    const char *szClassName) :
	ECGenericProp(lpProvider, ulObjType, fModify, szClassName)
{

	this->HrAddPropHandlers(PR_RECORD_KEY,		DefaultABGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_STORE_SUPPORT_MASK,	DefaultABGetProp,	DefaultSetPropComputed, (void*) this);
}

HRESULT ECABProp::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECABProp, this);
	return ECGenericProp::QueryInterface(refiid, lppInterface);
}

HRESULT	ECABProp::DefaultABGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT		hr = hrSuccess;
	auto lpProp = static_cast<ECABProp *>(lpParam);

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_RECORD_KEY):
		lpsPropValue->ulPropTag = PR_RECORD_KEY;

		if(lpProp->m_lpEntryId && lpProp->m_cbEntryId > 0) {
			lpsPropValue->Value.bin.cb = lpProp->m_cbEntryId;
			hr = ECAllocateMore(lpsPropValue->Value.bin.cb, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
			if (hr != hrSuccess)
				break;
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

HRESULT ECABProp::TableRowGetProp(void *lpProvider,
    const struct propVal *lpsPropValSrc, SPropValue *lpsPropValDst,
    void **lpBase, ULONG ulType)
{
	HRESULT hr = hrSuccess;

	switch(lpsPropValSrc->ulPropTag) {
	case PROP_TAG(PT_ERROR,PROP_ID(PR_AB_PROVIDER_ID)):
		lpsPropValDst->ulPropTag = PR_AB_PROVIDER_ID;
		lpsPropValDst->Value.bin.cb = sizeof(GUID);
		hr = ECAllocateMore(sizeof(GUID), lpBase, reinterpret_cast<void **>(&lpsPropValDst->Value.bin.lpb));
		if (hr != hrSuccess)
			break;
		memcpy(lpsPropValDst->Value.bin.lpb, &MUIDECSAB, sizeof(GUID));
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}

	return hr;
}
