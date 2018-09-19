/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include "kcore.hpp"
#include "ECABProp.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include <kopano/ECDefs.h>
#include <kopano/CommonUtil.h>

ECABProp::ECABProp(ECABLogon *prov, ULONG objtype, BOOL modify,
    const char *cls_name) :
	ECGenericProp(prov, objtype, modify, cls_name)
{
	HrAddPropHandlers(PR_RECORD_KEY, DefaultABGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_STORE_SUPPORT_MASK, DefaultABGetProp, DefaultSetPropComputed, this);
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
		KC::GetClientVersion(&ulClientVersion);

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
	case CHANGE_PROP_TYPE(PR_AB_PROVIDER_ID, PT_ERROR):
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
