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
#include <new>
#include "ECPropertyEntry.h"
#include "Mem.h"
#include <kopano/charset/convert.h>

using namespace KC;

//
// ECPropertyEntry
//
DEF_INVARIANT_CHECK(ECPropertyEntry) {
	// There should always be a proptag set.
	assert(ulPropTag != 0);
	assert(PROP_ID(ulPropTag) != 0);

	// PT_STRING8 and PT_MV_STRING8 are never stored.
	assert(PROP_TYPE(ulPropTag) != PT_STRING8);
	assert(PROP_TYPE(ulPropTag) != PT_MV_STRING8);
}

ECPropertyEntry::ECPropertyEntry(ULONG tag) :
	ulPropTag(tag)
{
	DEBUG_CHECK_INVARIANT;
}

ECPropertyEntry::ECPropertyEntry(std::unique_ptr<ECProperty> &&p) :
	ulPropTag(p->GetPropTag()), lpProperty(std::move(p))
{
	DEBUG_CHECK_INVARIANT;
}

ECPropertyEntry::~ECPropertyEntry()
{
	DEBUG_CHECK_INVARIANT;
}

// NOTE: lpsPropValue must be checked already
HRESULT ECPropertyEntry::HrSetProp(const SPropValue *lpsPropValue)
{
	DEBUG_GUARD;
	assert(ulPropTag != 0);
	assert(ulPropTag == lpsPropValue->ulPropTag);
	if (lpProperty != nullptr)
		lpProperty->CopyFrom(lpsPropValue);
	else
		lpProperty.reset(new ECProperty(lpsPropValue));
	fDirty = true;
	return hrSuccess;
}

HRESULT ECPropertyEntry::HrSetProp(ECProperty *property)
{
	DEBUG_GUARD;
	assert(property->GetPropTag() != 0);
	assert(lpProperty == nullptr);
	lpProperty.reset(property);
	fDirty = true;
	return hrSuccess;
}

HRESULT ECPropertyEntry::HrSetClean()
{
	DEBUG_GUARD;
	fDirty = false;
	return hrSuccess;
}

// ECProperty
//
// C++ class representing a property
//
//
// 
//
DEF_INVARIANT_CHECK(ECProperty) {
	// There should always be a proptag set.
	assert(ulPropTag != 0);
	assert(PROP_ID(ulPropTag) != 0);

	// PT_STRING8 and PT_MV_STRING8 are never stored.
	assert(PROP_TYPE(ulPropTag) != PT_STRING8);
	assert(PROP_TYPE(ulPropTag) != PT_MV_STRING8);
}

ECProperty::ECProperty(const ECProperty &Property) :
	ulSize(0)
{
	SPropValue sPropValue;

	assert(Property.ulPropTag != 0);
	sPropValue.ulPropTag = Property.ulPropTag;
	sPropValue.Value = Property.Value;
	memset(&Value, 0, sizeof(union __UPV));
	ulSize = 0;
	CopyFromInternal(&sPropValue);

	DEBUG_CHECK_INVARIANT;
}
	
ECProperty::ECProperty(const SPropValue *lpsProp) :
	ulSize(0)
{
	memset(&Value, 0, sizeof(union __UPV));
	assert(lpsProp->ulPropTag != 0);
	CopyFromInternal(lpsProp);

	DEBUG_CHECK_INVARIANT;
}

HRESULT ECProperty::CopyFrom(const SPropValue *lpsProp)
{
	DEBUG_GUARD;
	return CopyFromInternal(lpsProp);
}

HRESULT ECProperty::CopyFromInternal(const SPropValue *lpsProp)
{
	if (lpsProp == NULL)
		return dwLastError = MAPI_E_INVALID_PARAMETER;
	dwLastError = 0;
	ulPropTag = lpsProp->ulPropTag;
	assert(lpsProp->ulPropTag != 0);

	switch(PROP_TYPE(lpsProp->ulPropTag)) {
	case PT_I2:
		ulSize = 4;
		Value.i = lpsProp->Value.i;
		break;
	case PT_I4:
		ulSize = 4;
		Value.l = lpsProp->Value.l;
		break;
	case PT_R4:
		ulSize = 4;
		Value.flt = lpsProp->Value.flt;
		break;
	case PT_R8:
		ulSize = 4;
		Value.dbl = lpsProp->Value.dbl;
		break;
	case PT_BOOLEAN:
		ulSize = 4;
		Value.b = lpsProp->Value.b;
		break;
	case PT_CURRENCY:
		ulSize = 4;
		Value.cur = lpsProp->Value.cur;
		break;
	case PT_APPTIME:
		ulSize = 4;
		Value.at = lpsProp->Value.at;
		break;
	case PT_SYSTIME:
		ulSize = 4;
		Value.ft = lpsProp->Value.ft;
		break;
	case PT_STRING8: {
		std::wstring wstrTmp;

		if (lpsProp->Value.lpszA == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		if (TryConvert(lpsProp->Value.lpszA, wstrTmp) != hrSuccess)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = wstrTmp.length() + 1;
		if(ulSize < ulNewSize) {
			delete[] Value.lpszW;
			Value.lpszW = new(std::nothrow) wchar_t[ulNewSize];
			if (Value.lpszW == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}

		ulSize = ulNewSize;
		ulPropTag = CHANGE_PROP_TYPE(lpsProp->ulPropTag, PT_UNICODE);
		wcscpy(Value.lpszW, wstrTmp.c_str());
		break;
	}
	case PT_BINARY: {
		if (lpsProp->Value.bin.lpb == NULL && lpsProp->Value.bin.cb)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = lpsProp->Value.bin.cb;
		if(ulNewSize == 0)	{
			delete[] Value.bin.lpb;
			Value.bin.lpb = nullptr;
			Value.bin.cb = 0;
			ulSize = 0;
			break;
		}

		if(ulSize < ulNewSize) {
			delete[] Value.bin.lpb;
			Value.bin.lpb = new(std::nothrow) BYTE[ulNewSize];
			if (Value.bin.lpb == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}
		ulSize = ulNewSize;
		Value.bin.cb = lpsProp->Value.bin.cb;
		memcpy(Value.bin.lpb, lpsProp->Value.bin.lpb, lpsProp->Value.bin.cb);
		break;
	}
	case PT_UNICODE: {
		if (lpsProp->Value.lpszW == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = wcslen(lpsProp->Value.lpszW) + 1;
		if(ulSize < ulNewSize) {
			delete[] Value.lpszW;
			Value.lpszW = new(std::nothrow) wchar_t[ulNewSize];
			if (Value.lpszW == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}

		ulSize = ulNewSize;
		wcscpy(Value.lpszW, lpsProp->Value.lpszW);
		break;
	}
	case PT_CLSID: {
		if (lpsProp->Value.lpguid == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		if(ulSize != sizeof(GUID)) {
			ulSize = sizeof(GUID);
			Value.lpguid = new(std::nothrow) GUID;
			if (Value.lpguid == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}
		memcpy(Value.lpguid, lpsProp->Value.lpguid, sizeof(GUID));
		break;
	}
	case PT_I8:
		ulSize = 8;
		Value.li = lpsProp->Value.li;
		break;
	case PT_MV_I2: {
		if (lpsProp->Value.MVi.lpi == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(short int) * lpsProp->Value.MVi.cValues;
		if(ulSize < ulNewSize) {
			delete[] Value.MVi.lpi;
			Value.MVi.lpi = new(std::nothrow) short int[lpsProp->Value.MVi.cValues];
			if (Value.MVi.lpi == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}

		ulSize = ulNewSize;
		Value.MVi.cValues = lpsProp->Value.MVi.cValues;
		memcpy(Value.MVi.lpi, lpsProp->Value.MVi.lpi, lpsProp->Value.MVi.cValues * sizeof(short int));
		break;
	}
	case PT_MV_LONG: {
		if (lpsProp->Value.MVl.lpl == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(LONG) * lpsProp->Value.MVl.cValues;
		if(ulSize < ulNewSize) {
			delete[] Value.MVl.lpl;
			Value.MVl.lpl = new(std::nothrow) LONG[lpsProp->Value.MVl.cValues];
			if (Value.MVl.lpl == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}
		
		ulSize = ulNewSize;
		Value.MVl.cValues = lpsProp->Value.MVl.cValues;
		memcpy(Value.MVl.lpl, lpsProp->Value.MVl.lpl, lpsProp->Value.MVl.cValues * sizeof(int));
		break;
	}
	case PT_MV_R4: {
		if (lpsProp->Value.MVflt.lpflt == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(float) * lpsProp->Value.MVflt.cValues;
		if(ulSize < ulNewSize) {
			delete[] Value.MVflt.lpflt;
			Value.MVflt.lpflt = new(std::nothrow) float[lpsProp->Value.MVflt.cValues];
			if (Value.MVflt.lpflt == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}
		
		ulSize = ulNewSize;
		Value.MVflt.cValues = lpsProp->Value.MVflt.cValues;
		memcpy(Value.MVflt.lpflt, lpsProp->Value.MVflt.lpflt, lpsProp->Value.MVflt.cValues * sizeof(float));
		break;
	}
	case PT_MV_DOUBLE: {
		if (lpsProp->Value.MVdbl.lpdbl == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(double) * lpsProp->Value.MVdbl.cValues;
		if(ulSize < ulNewSize) {
			delete[] Value.MVdbl.lpdbl;
			Value.MVdbl.lpdbl = new(std::nothrow) double[lpsProp->Value.MVdbl.cValues];
			if (Value.MVdbl.lpdbl == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}

		ulSize = ulNewSize;
		Value.MVdbl.cValues = lpsProp->Value.MVdbl.cValues;
		memcpy(Value.MVdbl.lpdbl, lpsProp->Value.MVdbl.lpdbl, lpsProp->Value.MVdbl.cValues * sizeof(double));
		break;
	}
	case PT_MV_CURRENCY: {
		if (lpsProp->Value.MVcur.lpcur == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(CURRENCY) * lpsProp->Value.MVcur.cValues;
		if(ulSize < ulNewSize) {
			delete[] Value.MVcur.lpcur;
			Value.MVcur.lpcur = new(std::nothrow) CURRENCY[lpsProp->Value.MVcur.cValues];
			if (Value.MVcur.lpcur == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}
		
		ulSize = ulNewSize;
		Value.MVcur.cValues = lpsProp->Value.MVcur.cValues;
		memcpy(Value.MVcur.lpcur, lpsProp->Value.MVcur.lpcur, lpsProp->Value.MVcur.cValues * sizeof(CURRENCY));
		break;
	}
	case PT_MV_APPTIME: {
		if (lpsProp->Value.MVat.lpat == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(double) * lpsProp->Value.MVat.cValues;
		if(ulSize < ulNewSize) {
			delete[] Value.MVat.lpat;
			Value.MVat.lpat = new(std::nothrow) double[lpsProp->Value.MVat.cValues];
			if (Value.MVat.lpat == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}

		ulSize = ulNewSize;
		Value.MVat.cValues = lpsProp->Value.MVat.cValues;
		memcpy(Value.MVat.lpat, lpsProp->Value.MVat.lpat, lpsProp->Value.MVat.cValues * sizeof(double));
		break;
	}
	case PT_MV_SYSTIME: {
		if (lpsProp->Value.MVft.lpft == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(FILETIME) * lpsProp->Value.MVft.cValues;
		if(ulSize < ulNewSize) {
			delete[] Value.MVft.lpft;
			Value.MVft.lpft = new(std::nothrow) FILETIME[lpsProp->Value.MVft.cValues];
			if (Value.MVft.lpft == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}
		
		ulSize = ulNewSize;
		Value.MVft.cValues = lpsProp->Value.MVft.cValues;
		memcpy(Value.MVft.lpft, lpsProp->Value.MVft.lpft, lpsProp->Value.MVft.cValues * sizeof(FILETIME));
		break;
	}
	case PT_MV_BINARY: {
		if (lpsProp->Value.MVbin.lpbin == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(void *) * lpsProp->Value.MVbin.cValues;
		if(ulSize < ulNewSize) {
			if (Value.MVbin.lpbin){
				for (unsigned int i = 0; i < Value.MVbin.cValues; ++i)
					delete[] Value.MVbin.lpbin[i].lpb;
				delete[] Value.MVbin.lpbin;
			}
			Value.MVbin.lpbin = new(std::nothrow) SBinary[lpsProp->Value.MVbin.cValues];
			if (Value.MVbin.lpbin == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
			memset(Value.MVbin.lpbin, 0, sizeof(SBinary) * lpsProp->Value.MVbin.cValues);
		}
		else {
			for(unsigned int i = lpsProp->Value.MVbin.cValues; i < Value.MVbin.cValues; ++i)
				delete[] Value.MVbin.lpbin[i].lpb;
		}

		ulSize = ulNewSize;
		Value.MVbin.cValues = lpsProp->Value.MVbin.cValues;

		for (unsigned int i = 0; i < lpsProp->Value.MVbin.cValues; ++i) {
			if(lpsProp->Value.MVbin.lpbin[i].cb > 0)
			{
				if (lpsProp->Value.MVbin.lpbin[i].lpb == NULL)
					return dwLastError = MAPI_E_INVALID_PARAMETER;
				if (Value.MVbin.lpbin[i].lpb == nullptr || Value.MVbin.lpbin[i].cb < lpsProp->Value.MVbin.lpbin[i].cb) {
					delete[] Value.MVbin.lpbin[i].lpb;
					Value.MVbin.lpbin[i].lpb = new BYTE[lpsProp->Value.MVbin.lpbin[i].cb];
				}
				memcpy(Value.MVbin.lpbin[i].lpb, lpsProp->Value.MVbin.lpbin[i].lpb, lpsProp->Value.MVbin.lpbin[i].cb);
			}else {
				delete[] Value.MVbin.lpbin[i].lpb;
				Value.MVbin.lpbin[i].lpb = nullptr;
			}
			Value.MVbin.lpbin[i].cb = lpsProp->Value.MVbin.lpbin[i].cb;
		}

		break;
	}
	case PT_MV_STRING8: {
		convert_context converter;

		if (lpsProp->Value.MVszA.lppszA == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(void *) * lpsProp->Value.MVszA.cValues;
		if(ulSize < ulNewSize) {
			if (Value.MVszW.lppszW != nullptr) {
				for (unsigned int i = 0; i < Value.MVszW.cValues; ++i)
					delete[] Value.MVszW.lppszW[i];
				delete[] Value.MVszW.lppszW;
			}
			Value.MVszW.lppszW = new(std::nothrow) wchar_t *[lpsProp->Value.MVszA.cValues];
			if (Value.MVszW.lppszW == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
			memset(Value.MVszW.lppszW, 0, sizeof(wchar_t *) * lpsProp->Value.MVszA.cValues);
		}
		else {
			for (unsigned int i = lpsProp->Value.MVszW.cValues; i < Value.MVszW.cValues; ++i)
				delete[] Value.MVszW.lppszW[i];
		}

		ulSize = ulNewSize;
		Value.MVszW.cValues = lpsProp->Value.MVszA.cValues;

		for (unsigned int i = 0; i < lpsProp->Value.MVszA.cValues; ++i) {
			std::wstring wstrTmp;

			if (lpsProp->Value.MVszA.lppszA[i] == NULL)
				return dwLastError = MAPI_E_INVALID_PARAMETER;
			if (TryConvert(lpsProp->Value.MVszA.lppszA[i], wstrTmp) != hrSuccess)
				return dwLastError = MAPI_E_INVALID_PARAMETER;
			if (Value.MVszW.lppszW[i] == nullptr || wcslen(Value.MVszW.lppszW[i]) < wstrTmp.length()) {
				delete[] Value.MVszW.lppszW[i];
				Value.MVszW.lppszW[i] = new wchar_t[wstrTmp.length() + sizeof(wchar_t)];
			}
			wcscpy(Value.MVszW.lppszW[i], wstrTmp.c_str());
		}

		ulPropTag = CHANGE_PROP_TYPE(lpsProp->ulPropTag, PT_MV_UNICODE);
		break;
	}
	case PT_MV_UNICODE: {
		if (lpsProp->Value.MVszW.lppszW == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(void *) * lpsProp->Value.MVszW.cValues;
		if(ulSize < ulNewSize) {
			if (Value.MVszW.lppszW != nullptr) {
				for (unsigned int i = 0; i < Value.MVszW.cValues; ++i)
					delete[] Value.MVszW.lppszW[i];
				delete[] Value.MVszW.lppszW;
			}
			Value.MVszW.lppszW = new(std::nothrow) wchar_t *[lpsProp->Value.MVszW.cValues];
			if (Value.MVszW.lppszW == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
			memset(Value.MVszW.lppszW, 0, sizeof(wchar_t *) * lpsProp->Value.MVszW.cValues);
		}
		else {
			for (unsigned int i = lpsProp->Value.MVszW.cValues; i < Value.MVszW.cValues; ++i)
				delete[] Value.MVszW.lppszW[i];
		}
		
		ulSize = ulNewSize;
		Value.MVszW.cValues = lpsProp->Value.MVszW.cValues;

		for (unsigned int i = 0; i < lpsProp->Value.MVszW.cValues; ++i) {
			if (lpsProp->Value.MVszW.lppszW[i] == NULL)
				return dwLastError = MAPI_E_INVALID_PARAMETER;
			if (Value.MVszW.lppszW[i] == nullptr || wcslen(Value.MVszW.lppszW[i]) < wcslen(lpsProp->Value.MVszW.lppszW[i])) {
				delete[] Value.MVszW.lppszW[i];
				Value.MVszW.lppszW[i] = new wchar_t[wcslen(lpsProp->Value.MVszW.lppszW[i]) + sizeof(wchar_t)];
			}
			wcscpy(Value.MVszW.lppszW[i], lpsProp->Value.MVszW.lppszW[i]);
		}

		break;
	}
	case PT_MV_CLSID: {
		if (lpsProp->Value.MVguid.lpguid == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(GUID) * lpsProp->Value.MVguid.cValues;
		if(ulSize < ulNewSize) {
			delete[] Value.MVguid.lpguid;
			Value.MVguid.lpguid = new(std::nothrow) GUID[lpsProp->Value.MVguid.cValues];
			if (Value.MVguid.lpguid == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}

		ulSize = ulNewSize;
	        Value.MVguid.cValues = lpsProp->Value.MVguid.cValues;
		memcpy(Value.MVguid.lpguid, lpsProp->Value.MVguid.lpguid, sizeof(GUID) * lpsProp->Value.MVguid.cValues);
		break;
	}
	case PT_MV_I8: {
		if (lpsProp->Value.MVli.lpli == NULL)
			return dwLastError = MAPI_E_INVALID_PARAMETER;

		unsigned int ulNewSize = sizeof(LARGE_INTEGER) * lpsProp->Value.MVli.cValues;
		if(ulSize < ulNewSize) {
			delete[] Value.MVli.lpli;
			Value.MVli.lpli = new(std::nothrow) LARGE_INTEGER[lpsProp->Value.MVli.cValues];
			if (Value.MVli.lpli == nullptr)
				return dwLastError = MAPI_E_NOT_ENOUGH_MEMORY;
		}

		ulSize = ulNewSize;
		Value.MVli.cValues = lpsProp->Value.MVli.cValues;
		memcpy(Value.MVli.lpli, lpsProp->Value.MVli.lpli, lpsProp->Value.MVli.cValues * sizeof(LARGE_INTEGER));
		break;
	}
	case PT_ERROR:
		Value.err = lpsProp->Value.err;
		break;
	default:
		// Unknown type (PR_NULL not include)
		assert(false);
		dwLastError = MAPI_E_INVALID_PARAMETER;
		break;
	}
	return dwLastError;
}

ECProperty::~ECProperty()
{
	DEBUG_GUARD;

	switch(PROP_TYPE(ulPropTag)) {
	case PT_I2:
	case PT_I4:
	case PT_R4:
	case PT_R8:
	case PT_BOOLEAN:
	case PT_CURRENCY:
	case PT_APPTIME:
	case PT_SYSTIME:
	case PT_I8:
		break;
	case PT_BINARY:
		delete[] Value.bin.lpb;
		break;
	case PT_STRING8:
		assert("We should never have PT_STRING8 storage" == nullptr);
		// Deliberate fallthrough
	case PT_UNICODE:
		delete[] Value.lpszW;
		break;
	case PT_CLSID:
		delete Value.lpguid;
		break;
	case PT_MV_I2:
		delete[] Value.MVi.lpi;
		break;
	case PT_MV_LONG:
		delete[] Value.MVl.lpl;
		break;
	case PT_MV_R4:
		delete[] Value.MVflt.lpflt;
		break;
	case PT_MV_DOUBLE:
		delete[] Value.MVdbl.lpdbl;
		break;
	case PT_MV_CURRENCY:
		delete[] Value.MVcur.lpcur;
		break;
	case PT_MV_APPTIME:
		delete[] Value.MVat.lpat;
		break;
	case PT_MV_SYSTIME:
		delete[] Value.MVft.lpft;
		break;
	case PT_MV_BINARY: {
		for (unsigned int i = 0; i < Value.MVbin.cValues; ++i)
			delete[] Value.MVbin.lpbin[i].lpb;
		delete[] Value.MVbin.lpbin;
		break;
	}
	case PT_MV_STRING8:
		assert("We should never have PT_MV_STRING8 storage" == nullptr);
		// Deliberate fallthrough
	case PT_MV_UNICODE: {
		for (unsigned int i = 0; i < Value.MVszW.cValues; ++i)
			delete[] Value.MVszW.lppszW[i];
		delete[] Value.MVszW.lppszW;
		break;
	}
	case PT_MV_CLSID: {
		delete[] Value.MVguid.lpguid;
		break;
	}
	case PT_MV_I8:
		delete[] Value.MVli.lpli;
		break;
	case PT_ERROR:
		break;
	default:
		break;
	}
}

HRESULT ECProperty::CopyToByRef(LPSPropValue lpsProp) const
{
	DEBUG_GUARD;
	lpsProp->ulPropTag = ulPropTag;
	memcpy(&lpsProp->Value, &Value, sizeof(union __UPV));
	return hrSuccess;
}

HRESULT ECProperty::CopyTo(LPSPropValue lpsProp, void *lpBase, ULONG ulRequestPropTag) {
	DEBUG_GUARD;

	HRESULT hr = hrSuccess;

	assert
	(
		PROP_TYPE(ulRequestPropTag) == PROP_TYPE(ulPropTag) ||
		((PROP_TYPE(ulRequestPropTag) == PT_STRING8 || PROP_TYPE(ulRequestPropTag) == PT_UNICODE) && PROP_TYPE(ulPropTag) == PT_UNICODE) ||
		((PROP_TYPE(ulRequestPropTag) == PT_MV_STRING8 || PROP_TYPE(ulRequestPropTag) == PT_MV_UNICODE) && PROP_TYPE(ulPropTag) == PT_MV_UNICODE)
	);

	lpsProp->ulPropTag = ulRequestPropTag;

	switch (PROP_TYPE(ulPropTag)) {
	case PT_I2:
		lpsProp->Value.i = Value.i;
		break;
	case PT_I4:
		lpsProp->Value.l = Value.l;
		break;
	case PT_R4:
		lpsProp->Value.flt = Value.flt;
		break;
	case PT_R8:
		lpsProp->Value.dbl = Value.dbl;
		break;
	case PT_BOOLEAN:
		lpsProp->Value.b = Value.b;
		break;
	case PT_CURRENCY:
		lpsProp->Value.cur = Value.cur;
		break;
	case PT_APPTIME:
		lpsProp->Value.at = Value.at;
		break;
	case PT_SYSTIME:
		lpsProp->Value.ft = Value.ft;
		break;
	case PT_BINARY: {
		if (Value.bin.cb == 0) {
			lpsProp->Value.bin.lpb = NULL;
			lpsProp->Value.bin.cb = Value.bin.cb;
			break;
		}
		BYTE *lpBin = NULL;
		hr = ECAllocateMore(Value.bin.cb, lpBase, reinterpret_cast<void **>(&lpBin));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		memcpy(lpBin, Value.bin.lpb, Value.bin.cb);
		lpsProp->Value.bin.lpb = lpBin;
		lpsProp->Value.bin.cb = Value.bin.cb;
		break;
	}
	case PT_STRING8:
		assert("We should never have PT_STRING8 storage" == nullptr);
		// Deliberate fallthrough
	case PT_UNICODE: {
		if (PROP_TYPE(ulRequestPropTag) == PT_UNICODE) {
			hr = ECAllocateMore(sizeof(wchar_t) * (wcslen(Value.lpszW) + 1), lpBase, reinterpret_cast<void **>(&lpsProp->Value.lpszW));
			if (hr != hrSuccess)
				dwLastError = hr;
			else
				wcscpy(lpsProp->Value.lpszW, Value.lpszW);
			break;
		}
		std::string dst;
		if (TryConvert(Value.lpszW, dst) != hrSuccess) {
			dwLastError = MAPI_E_INVALID_PARAMETER;
			return hr;
		}
		hr = ECAllocateMore(dst.length() + 1, lpBase, (LPVOID*)&lpsProp->Value.lpszA);
		if (hr != hrSuccess)
			dwLastError = hr;
		else
			strcpy(lpsProp->Value.lpszA, dst.c_str());
		break;
	}
	case PT_CLSID: {
		GUID *lpGUID;
		hr = ECAllocateMore(sizeof(GUID), lpBase, (LPVOID *)&lpGUID);
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		memcpy(lpGUID, Value.lpguid, sizeof(GUID));
		lpsProp->Value.lpguid = lpGUID;
		break;
	}
	case PT_I8:
		lpsProp->Value.li = Value.li;
		break;
	case PT_MV_I2: {
		short int *lpShort;
		hr = ECAllocateMore(Value.MVi.cValues * sizeof(short int), lpBase, reinterpret_cast<void **>(&lpShort));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		lpsProp->Value.MVi.cValues = Value.MVi.cValues;
		memcpy(lpShort, Value.MVi.lpi, Value.MVi.cValues * sizeof(short int));
		lpsProp->Value.MVi.lpi = lpShort;
		break;
	}
	case PT_MV_LONG: {
		LONG *lpLong;
		hr = ECAllocateMore(Value.MVl.cValues * sizeof(LONG), lpBase, reinterpret_cast<void **>(&lpLong));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		lpsProp->Value.MVl.cValues = Value.MVl.cValues;
		memcpy(lpLong, Value.MVl.lpl, Value.MVl.cValues * sizeof(LONG));
		lpsProp->Value.MVl.lpl = lpLong;
		break;
	}
	case PT_MV_R4: {
		float *lpFloat;
		hr = ECAllocateMore(Value.MVflt.cValues * sizeof(float), lpBase, reinterpret_cast<void **>(&lpFloat));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		lpsProp->Value.MVflt.cValues = Value.MVflt.cValues;
		memcpy(lpFloat, Value.MVflt.lpflt, Value.MVflt.cValues * sizeof(float));
		lpsProp->Value.MVflt.lpflt = lpFloat;
		break;
	}
	case PT_MV_DOUBLE: {
		double *lpDouble;
		hr = ECAllocateMore(Value.MVdbl.cValues * sizeof(double), lpBase, reinterpret_cast<void **>(&lpDouble));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		lpsProp->Value.MVdbl.cValues = Value.MVdbl.cValues;
		memcpy(lpDouble, Value.MVdbl.lpdbl, Value.MVdbl.cValues * sizeof(double));
		lpsProp->Value.MVdbl.lpdbl = lpDouble;
		break;
	}
	case PT_MV_CURRENCY: {
		CURRENCY *lpCurrency;
		hr = ECAllocateMore(Value.MVcur.cValues * sizeof(CURRENCY), lpBase, reinterpret_cast<void **>(&lpCurrency));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		lpsProp->Value.MVcur.cValues = Value.MVcur.cValues;
		memcpy(lpCurrency, Value.MVcur.lpcur, Value.MVcur.cValues * sizeof(CURRENCY));
		lpsProp->Value.MVcur.lpcur = lpCurrency;
		break;
	}
	case PT_MV_APPTIME: {
		double *lpApptime;
		hr = ECAllocateMore(Value.MVat.cValues * sizeof(double), lpBase, reinterpret_cast<void **>(&lpApptime));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		lpsProp->Value.MVat.cValues = Value.MVat.cValues;
		memcpy(lpApptime, Value.MVat.lpat, Value.MVat.cValues * sizeof(double));
		lpsProp->Value.MVat.lpat = lpApptime;
		break;
	}
	case PT_MV_SYSTIME: {
		FILETIME *lpFiletime;
		hr = ECAllocateMore(Value.MVft.cValues * sizeof(FILETIME), lpBase, reinterpret_cast<void **>(&lpFiletime));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		lpsProp->Value.MVft.cValues = Value.MVft.cValues;
		memcpy(lpFiletime, Value.MVft.lpft, Value.MVft.cValues * sizeof(FILETIME));
		lpsProp->Value.MVft.lpft = lpFiletime;
		break;
	}
	case PT_MV_BINARY: {
		SBinary *lpBin;
		hr = ECAllocateMore(Value.MVbin.cValues * sizeof(SBinary), lpBase, reinterpret_cast<void **>(&lpBin));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		lpsProp->Value.MVbin.cValues = Value.MVbin.cValues;
		lpsProp->Value.MVbin.lpbin = lpBin;
		for (unsigned int i = 0; i < Value.MVbin.cValues; ++i) {
			lpsProp->Value.MVbin.lpbin[i].cb = Value.MVbin.lpbin[i].cb;
			if (lpsProp->Value.MVbin.lpbin[i].cb == 0) {
				lpsProp->Value.MVbin.lpbin[i].lpb = NULL;
				continue;
			}
			hr = ECAllocateMore(Value.MVbin.lpbin[i].cb, lpBase, reinterpret_cast<void **>(&lpsProp->Value.MVbin.lpbin[i].lpb));
			if (hr != hrSuccess)
				return hr;
			memcpy(lpsProp->Value.MVbin.lpbin[i].lpb, Value.MVbin.lpbin[i].lpb, lpsProp->Value.MVbin.lpbin[i].cb);
		}
		break;
	}
	case PT_MV_STRING8:
		assert("We should never have PT_MV_STRING8 storage" == nullptr);
		// Deliberate fallthrough
	case PT_MV_UNICODE: {
		if (PROP_TYPE(ulRequestPropTag) == PT_MV_STRING8) {
			lpsProp->Value.MVszA.cValues = Value.MVszW.cValues;
			hr = ECAllocateMore(Value.MVszW.cValues * sizeof(LPSTR), lpBase, reinterpret_cast<void **>(&lpsProp->Value.MVszA.lppszA));
			if (hr != hrSuccess) {
				dwLastError = hr;
				break;
			}
			convert_context converter;
			for (ULONG i = 0; hr == hrSuccess && i < Value.MVszW.cValues; ++i) {
				std::string strDst;
				if (TryConvert(Value.MVszW.lppszW[i], strDst) != hrSuccess) {
					dwLastError = MAPI_E_INVALID_PARAMETER;
					return hr;
				}
				hr = ECAllocateMore(strDst.size() + 1, lpBase, (LPVOID*)&lpsProp->Value.MVszA.lppszA[i]);
				if (hr != hrSuccess)
					dwLastError = hr;
				else
					strcpy(lpsProp->Value.MVszA.lppszA[i], strDst.c_str());
			}
			break;
		}
		lpsProp->Value.MVszW.cValues = Value.MVszW.cValues;
		hr = ECAllocateMore(Value.MVszW.cValues * sizeof(LPWSTR), lpBase, reinterpret_cast<void **>(&lpsProp->Value.MVszW.lppszW));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		for (ULONG i = 0; hr == hrSuccess && i < Value.MVszW.cValues; ++i) {
			hr = ECAllocateMore(sizeof(wchar_t) * (wcslen(Value.MVszW.lppszW[i]) + 1), lpBase, reinterpret_cast<void **>(&lpsProp->Value.MVszW.lppszW[i]));
			if (hr != hrSuccess)
				dwLastError = hr;
			else
				wcscpy(lpsProp->Value.MVszW.lppszW[i], Value.MVszW.lppszW[i]);
		}
		break;
	}
	case PT_MV_CLSID: {
		GUID *lpGuid;
		hr = ECAllocateMore(Value.MVguid.cValues * sizeof(GUID), lpBase, reinterpret_cast<void **>(&lpGuid));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		memcpy(lpGuid, Value.MVguid.lpguid, sizeof(GUID) * Value.MVguid.cValues);
		lpsProp->Value.MVguid.cValues = Value.MVguid.cValues;
		lpsProp->Value.MVguid.lpguid = lpGuid;
		break;
	}
	case PT_MV_I8: {
		LARGE_INTEGER *lpLarge;
		hr = ECAllocateMore(Value.MVli.cValues * sizeof(LARGE_INTEGER), lpBase, reinterpret_cast<void **>(&lpLarge));
		if (hr != hrSuccess) {
			dwLastError = hr;
			break;
		}
		lpsProp->Value.MVli.cValues = Value.MVli.cValues;
		memcpy(lpLarge, Value.MVli.lpli, Value.MVli.cValues * sizeof(LARGE_INTEGER));
		lpsProp->Value.MVli.lpli = lpLarge;
		break;
	}
	case PT_ERROR:
		lpsProp->Value.err = Value.err;
		break;
	default: // I hope there are no pointers involved here!
		assert(false);
		lpsProp->Value = Value;
		break;
	}
	return hr;
}

bool ECProperty::operator==(const ECProperty &property) const {
	DEBUG_GUARD;

	if (property.ulPropTag == ulPropTag)
		return true;
	if (PROP_ID(property.ulPropTag) != PROP_ID(ulPropTag))
		return false;
	if (PROP_TYPE(property.ulPropTag) == PT_STRING8 && PROP_TYPE(ulPropTag) == PT_UNICODE)
		return true;
	if (PROP_TYPE(property.ulPropTag) == PT_MV_STRING8 && PROP_TYPE(ulPropTag) == PT_MV_UNICODE)
		return true;
	return false;
}

SPropValue ECProperty::GetMAPIPropValRef(void) const
{
	DEBUG_GUARD;

	SPropValue ret;
	ret.ulPropTag = ulPropTag;
	ret.dwAlignPad = 0;
	ret.Value = Value;
	return ret;
}
