/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <exception>
#include <cwctype>
#include <mapidefs.h>
#include <mapiutil.h>
#include <mapispi.h>
#include <memory>
#include <new>
#include <string>
#include <stack>
#include <set>
#include <map>
#include <cstring>
#include <edkmdb.h>
#include <kopano/Util.h>
#include <kopano/CommonUtil.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include "ECMemStream.h"
#include <kopano/IECInterfaces.hpp>
#include <kopano/ECGuid.h>
#include <kopano/codepage.h>
#include "rtfutil.h"
#include <kopano/mapiext.h>
#include <kopano/ustringutil.h>
#include <kopano/mapi_ptr.h>
#include "HtmlToTextParser.h"
#include <kopano/ECLogger.h>
#include "HtmlEntity.h"
#include <kopano/ECGetText.h>

namespace KC {

// HACK: prototypes may differ depending on the compiler and/or system (the
// second parameter may or may not be 'const'). This redeclaration is a hack
// to have a common prototype "iconv_cast".
class iconv_HACK final {
public:
	iconv_HACK(const char** ptr) : m_ptr(ptr) { }

	// the compiler will choose the right operator
	operator const char **(void) const { return m_ptr; }
	operator char**() { return const_cast <char**>(m_ptr); }

private:
	const char** m_ptr;
};

class PropTagCompare final {
public:
	bool operator()(ULONG lhs, ULONG rhs) const { 
		if (PROP_TYPE(lhs) == PT_UNSPECIFIED || PROP_TYPE(rhs) == PT_UNSPECIFIED)
			return PROP_ID(lhs) < PROP_ID(rhs); 
		return lhs < rhs;
	}
};

typedef std::set<ULONG,PropTagCompare> PropTagSet;

static HRESULT HrCopyActions(ACTIONS *, const ACTIONS *, void *);
static HRESULT HrCopyAction(ACTION *, const ACTION *, void *);

/** 
 * Add or replaces a prop value in a an SPropValue array
 * 
 * @param[in] lpSrc Array of properties
 * @param[in] cValues Number of properties in lpSrc
 * @param[in] lpToAdd Add or replace this property
 * @param[out] lppDest All properties returned
 * @param[out] cDestValues Number of properties in lppDest
 * 
 * @return MAPI error code
 */
HRESULT Util::HrAddToPropertyArray(const SPropValue *lpSrc, ULONG cValues,
    const SPropValue *lpToAdd, SPropValue **lppDest, ULONG *cDestValues)
{
	LPSPropValue lpDest = NULL;
	unsigned int n = 0;

	HRESULT hr = MAPIAllocateBuffer(sizeof(SPropValue) * (cValues + 1),
	             reinterpret_cast<void **>(&lpDest));
	if (hr != hrSuccess)
		return hr;
	for (unsigned int i = 0; i < cValues; ++i) {
		hr = HrCopyProperty(&lpDest[n], &lpSrc[i], lpDest);

		if(hr == hrSuccess)
			++n;
		hr = hrSuccess;
	}

	auto lpFind = PpropFindProp(lpDest, n, lpToAdd->ulPropTag);
	if (lpFind != nullptr)
		hr = HrCopyProperty(lpFind, lpToAdd, lpDest);
	else
		hr = HrCopyProperty(&lpDest[n++], lpToAdd, lpDest);
	if(hr != hrSuccess)
		return hr;
	*lppDest = lpDest;
	*cDestValues = n;
	return hrSuccess;
}

/**
 * Check if object supports HTML
 *
 * @param lpMapiProp Interface to check
 * @result TRUE if object supports unicode, FALSE if not or error
 */
#ifndef STORE_HTML_OK
#define STORE_HTML_OK 0x00010000
#endif
static bool FHasHTML(IMAPIProp *lpProp)
{
	memory_ptr<SPropValue> lpPropSupport = NULL;
	auto hr = HrGetOneProp(lpProp, PR_STORE_SUPPORT_MASK, &~lpPropSupport);
	if(hr != hrSuccess)
		return false; /* hr */
	if((lpPropSupport->Value.ul & STORE_HTML_OK) == 0)
		return false; /* MAPI_E_NOT_FOUND */
	return true;
}

/** 
 * Merges to proptag arrays, lpAdds properties may overwrite properties from lpSrc
 * 
 * @param[in] lpSrc Source property array
 * @param[in] cValues Number of properties in lpSrc
 * @param[in] lpAdds Properties to combine with lpSrc, adding or replacing values
 * @param[in] cAddValues Number of properties in lpAdds
 * @param[out] lppDest New array containing all properties
 * @param[out] cDestValues Number of properties in lppDest
 * 
 * @return MAPI error code
 */
HRESULT	Util::HrMergePropertyArrays(const SPropValue *lpSrc, ULONG cValues,
    const SPropValue *lpAdds, ULONG cAddValues, SPropValue **lppDest,
    ULONG *cDestValues)
{
	std::map<ULONG, const SPropValue *> mapPropSource;
	memory_ptr<SPropValue> lpProps;

	for (unsigned int i = 0; i < cValues; ++i)
		mapPropSource[lpSrc[i].ulPropTag] = &lpSrc[i];
	for (unsigned int i = 0; i < cAddValues; ++i)
		mapPropSource[lpAdds[i].ulPropTag] = &lpAdds[i];
	auto hr = MAPIAllocateBuffer(sizeof(SPropValue)*mapPropSource.size(), &~lpProps);
	if (hr != hrSuccess)
		return hr;

	unsigned int i = 0;
	for (const auto &ips : mapPropSource) {
		hr = Util::HrCopyProperty(&lpProps[i], ips.second, lpProps);
		if (hr != hrSuccess)
			return hr;
		++i;
	}

	*cDestValues = i;
	*lppDest = lpProps.release();
	return hrSuccess;
}

/** 
 * Copies a whole array of properties, but leaves the external data
 * where it is (ie binary, string data is not copied). PT_ERROR
 * properties can be filtered.
 * 
 * @param[in] lpSrc Array of properties
 * @param[in] cValues Number of values in lpSrc
 * @param[out] lppDest Duplicate array with data pointers into lpSrc values
 * @param[out] cDestValues Number of values in lppDest
 * @param[in] bExcludeErrors if true, copy even PT_ERROR properties
 * 
 * @return MAPI error code
 */
HRESULT Util::HrCopyPropertyArrayByRef(const SPropValue *lpSrc, ULONG cValues,
    LPSPropValue *lppDest, ULONG *cDestValues, bool bExcludeErrors)
{
	memory_ptr<SPropValue> lpDest;
    unsigned int n = 0;
    
	HRESULT hr = MAPIAllocateBuffer(sizeof(SPropValue) * cValues, &~lpDest);
	if (hr != hrSuccess)
		return hr;
	for (unsigned int i = 0; i < cValues; ++i) {
		if (bExcludeErrors && PROP_TYPE(lpSrc[i].ulPropTag) == PT_ERROR)
			continue;
		hr = HrCopyPropertyByRef(&lpDest[n], &lpSrc[i]);
		if (hr == hrSuccess)
			++n;
	}
	*lppDest = lpDest.release();
	*cDestValues = n;
	return hrSuccess;
}

/** 
 * Copies a whole array of properties, data of lpSrc will also be
 * copied into lppDest.  PT_ERROR properties can be filtered.
 * 
 * @param[in] lpSrc Array of properties
 * @param[in] cValues Number of values in lpSrc
 * @param[out] lppDest Duplicate array with data pointers into lpSrc values
 * @param[out] cDestValues Number of values in lppDest
 * @param[in] bExcludeErrors if true, copy even PT_ERROR properties
 * 
 * @return MAPI error code
 */
HRESULT Util::HrCopyPropertyArray(const SPropValue *lpSrc, ULONG cValues,
    LPSPropValue *lppDest, ULONG *cDestValues, bool bExcludeErrors)
{
	memory_ptr<SPropValue> lpDest;
	unsigned int n = 0;

	HRESULT hr = MAPIAllocateBuffer(sizeof(SPropValue) * cValues, &~lpDest);
	if (hr != hrSuccess)
		return hr;
	for (unsigned int i = 0; i < cValues; ++i) {
		if (bExcludeErrors && PROP_TYPE(lpSrc[i].ulPropTag) == PT_ERROR)
			continue;
		hr = HrCopyProperty(&lpDest[n], &lpSrc[i], lpDest);
		if (hr == MAPI_E_INVALID_PARAMETER)
			/* traditionally ignored */
			continue;
		else if (hr != hrSuccess)
			return hr; /* give back memory errors */
		++n;
	}
	*lppDest = lpDest.release();
	*cDestValues = n;
	return hrSuccess;
}

/** 
 * Copy array of properties in already allocated location.
 * 
 * @param[in] lpSrc Array of properties to copy
 * @param[in] cValues Number of properties in lpSrc
 * @param[out] lpDest Array of properties with enough space to contain cValues properties
 * @param[in] lpBase Base pointer to use with MAPIAllocateMore
 * 
 * @return MAPI error code
 */
HRESULT Util::HrCopyPropertyArray(const SPropValue *lpSrc, ULONG cValues,
    LPSPropValue lpDest, void *lpBase)
{
	for (unsigned int i = 0; i < cValues; ++i) {
		HRESULT hr = HrCopyProperty(&lpDest[i], &lpSrc[i], lpBase);
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

/** 
 * Copy array of properties in already allocated location. but leaves the external data
 * where it is (ie binary, string data is not copied)
 * 
 * @param[in] lpSrc Array of properties to copy
 * @param[in] cValues Number of properties in lpSrc
 * @param[out] lpDest Array of properties with enough space to contain cValues properties
 * @param[in] lpBase Base pointer to use with MAPIAllocateMore
 * 
 * @return MAPI error code
 */
HRESULT Util::HrCopyPropertyArrayByRef(const SPropValue *lpSrc, ULONG cValues,
    LPSPropValue lpDest)
{
	for (unsigned int i = 0; i < cValues; ++i) {
		HRESULT hr = HrCopyPropertyByRef(&lpDest[i], &lpSrc[i]);
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

/** 
 * Copies one property to somewhere else, but doesn't copy external data (ie binary or string data)
 * 
 * @param[out] lpDest Destination to copy property to
 * @param[in] lpSrc Source property to make copy of
 * 
 * @return always hrSuccess
 */
HRESULT Util::HrCopyPropertyByRef(LPSPropValue lpDest, const SPropValue *lpSrc)
{
    // Just a simple memcpy !
    memcpy(lpDest, lpSrc, sizeof(SPropValue));
    
    return hrSuccess;
}

/** 
 * Copies one property to somewhere else, alloc'ing space if required
 * 
 * @param[out] lpDest Destination to copy property to
 * @param[in] lpSrc Source property to make copy of
 * @param[in] lpBase Base pointer to use with lpfAllocMore
 * @param[in] lpfAllocMore Pointer to the MAPIAllocateMore function, can be NULL
 * 
 * @return MAPI error code
 */
HRESULT Util::HrCopyProperty(LPSPropValue lpDest, const SPropValue *lpSrc,
    void *lpBase, ALLOCATEMORE *lpfAllocMore)
{
	HRESULT hr = hrSuccess;

	if(lpfAllocMore == NULL)
		lpfAllocMore = MAPIAllocateMore;

	switch(PROP_TYPE(lpSrc->ulPropTag)) {	
	case PT_I2:
		lpDest->Value.i = lpSrc->Value.i;
		break;
	case PT_LONG:
		lpDest->Value.ul = lpSrc->Value.ul;
		break;
	case PT_BOOLEAN:
		lpDest->Value.b = lpSrc->Value.b;
		break;
	case PT_R4:
		lpDest->Value.flt = lpSrc->Value.flt;
		break;
	case PT_DOUBLE:
		lpDest->Value.dbl = lpSrc->Value.dbl;
		break;
	case PT_APPTIME:
		lpDest->Value.at = lpSrc->Value.at;
		break;
	case PT_CURRENCY:
		lpDest->Value.cur = lpSrc->Value.cur;
		break;
	case PT_SYSTIME:
		lpDest->Value.ft = lpSrc->Value.ft;
		break;
	case PT_I8:
		lpDest->Value.li = lpSrc->Value.li;
		break;
	case PT_UNICODE:
		if (lpSrc->Value.lpszW == NULL)
			return MAPI_E_INVALID_PARAMETER;
		hr = lpfAllocMore(wcslen(lpSrc->Value.lpszW)*sizeof(wchar_t)+sizeof(wchar_t), lpBase, (void**)&lpDest->Value.lpszW);
		if (hr != hrSuccess)
			return hr;
		wcscpy(lpDest->Value.lpszW, lpSrc->Value.lpszW);
		break;
	case PT_STRING8:
		if (lpSrc->Value.lpszA == NULL)
			return MAPI_E_INVALID_PARAMETER;
		hr = lpfAllocMore(strlen(lpSrc->Value.lpszA) + 1, lpBase, (void**)&lpDest->Value.lpszA);
		if (hr != hrSuccess)
			return hr;
		strcpy(lpDest->Value.lpszA, lpSrc->Value.lpszA);
		break;
	case PT_BINARY:
		if(lpSrc->Value.bin.cb > 0) {
			hr = lpfAllocMore(lpSrc->Value.bin.cb, lpBase, (void **) &lpDest->Value.bin.lpb);
			if (hr != hrSuccess)
				return hr;
		}
		lpDest->Value.bin.cb = lpSrc->Value.bin.cb;
		
		if(lpSrc->Value.bin.cb > 0)
			memcpy(lpDest->Value.bin.lpb, lpSrc->Value.bin.lpb, lpSrc->Value.bin.cb);
		else
			lpDest->Value.bin.lpb = NULL;

		break;
	case PT_CLSID:
		hr = lpfAllocMore(sizeof(GUID), lpBase, (void **)&lpDest->Value.lpguid);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->Value.lpguid, lpSrc->Value.lpguid, sizeof(GUID));
		break;
	case PT_ERROR:
		lpDest->Value.err = lpSrc->Value.err;
		break;
	case PT_SRESTRICTION:
		if (lpSrc->Value.lpszA == NULL)
			return MAPI_E_INVALID_PARAMETER;
		/*
		 * NOTE: we place the object pointer in lpszA to make sure it
		 * is on the same offset as Value.x on 32-bit as 64-bit
		 * machines.
		 */
		hr = lpfAllocMore(sizeof(SRestriction), lpBase, (void **)&lpDest->Value.lpszA);
		if (hr != hrSuccess)
			return hr;
		hr = Util::HrCopySRestriction((LPSRestriction)lpDest->Value.lpszA, (LPSRestriction)lpSrc->Value.lpszA, lpBase);
		break;
	case PT_ACTIONS:
		if (lpSrc->Value.lpszA == NULL)
			return MAPI_E_INVALID_PARAMETER;
		/*
		 * NOTE: we place the object pointer in lpszA to make sure it
		 * is on the same offset as Value.x on 32-bit as 64-bit
		 * machines.
		 */
		hr = lpfAllocMore(sizeof(ACTIONS), lpBase, (void **)&lpDest->Value.lpszA);
		if (hr != hrSuccess)
			return hr;
		hr = HrCopyActions(reinterpret_cast<ACTIONS *>(lpDest->Value.lpszA), reinterpret_cast<ACTIONS *>(lpSrc->Value.lpszA), lpBase);
		break;
	case PT_NULL:
		break;
	case PT_OBJECT:
		lpDest->Value.lpszA = lpSrc->Value.lpszA;
		break;
	// MV properties
	case PT_MV_I2:
		hr = lpfAllocMore(sizeof(short int) * lpSrc->Value.MVi.cValues, lpBase, (void **)&lpDest->Value.MVi.lpi);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->Value.MVi.lpi, lpSrc->Value.MVi.lpi, sizeof(short int) * lpSrc->Value.MVi.cValues);
		lpDest->Value.MVi.cValues = lpSrc->Value.MVi.cValues;
		break;
	case PT_MV_LONG:
		hr = lpfAllocMore(sizeof(LONG) * lpSrc->Value.MVl.cValues, lpBase, (void **)&lpDest->Value.MVl.lpl);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->Value.MVl.lpl, lpSrc->Value.MVl.lpl, sizeof(LONG) * lpSrc->Value.MVl.cValues);
		lpDest->Value.MVl.cValues = lpSrc->Value.MVl.cValues;
		break;
	case PT_MV_FLOAT:
		hr = lpfAllocMore(sizeof(float) * lpSrc->Value.MVflt.cValues, lpBase, (void **)&lpDest->Value.MVflt.lpflt);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->Value.MVflt.lpflt, lpSrc->Value.MVflt.lpflt, sizeof(float) * lpSrc->Value.MVflt.cValues);
		lpDest->Value.MVflt.cValues = lpSrc->Value.MVflt.cValues;
		break;
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		hr = lpfAllocMore(sizeof(double) * lpSrc->Value.MVdbl.cValues, lpBase, (void **)&lpDest->Value.MVdbl.lpdbl);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->Value.MVdbl.lpdbl, lpSrc->Value.MVdbl.lpdbl, sizeof(double) * lpSrc->Value.MVdbl.cValues);
		lpDest->Value.MVdbl.cValues = lpSrc->Value.MVdbl.cValues;
		break;
	case PT_MV_I8:
		hr = lpfAllocMore(sizeof(LONGLONG) * lpSrc->Value.MVli.cValues, lpBase, (void **)&lpDest->Value.MVli.lpli);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->Value.MVli.lpli, lpSrc->Value.MVli.lpli, sizeof(LONGLONG) * lpSrc->Value.MVli.cValues);
		lpDest->Value.MVli.cValues = lpSrc->Value.MVli.cValues;
		break;
	case PT_MV_CURRENCY:
		hr = lpfAllocMore(sizeof(CURRENCY) * lpSrc->Value.MVcur.cValues, lpBase, (void **)&lpDest->Value.MVcur.lpcur);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->Value.MVcur.lpcur, lpSrc->Value.MVcur.lpcur, sizeof(CURRENCY) * lpSrc->Value.MVcur.cValues);
		lpDest->Value.MVcur.cValues = lpSrc->Value.MVcur.cValues;
		break;
	case PT_MV_SYSTIME:
		hr = lpfAllocMore(sizeof(FILETIME) * lpSrc->Value.MVft.cValues, lpBase, (void **)&lpDest->Value.MVft.lpft);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->Value.MVft.lpft, lpSrc->Value.MVft.lpft, sizeof(FILETIME) * lpSrc->Value.MVft.cValues);
		lpDest->Value.MVft.cValues = lpSrc->Value.MVft.cValues;
		break;
	case PT_MV_STRING8:
		hr = lpfAllocMore(sizeof(LPSTR *) * lpSrc->Value.MVszA.cValues, lpBase, (void **)&lpDest->Value.MVszA.lppszA);
		if (hr != hrSuccess)
			return hr;
		for (ULONG i = 0; i < lpSrc->Value.MVszA.cValues; ++i) {
			int datalength = strlen(lpSrc->Value.MVszA.lppszA[i]) + 1;
			hr = lpfAllocMore(datalength, lpBase, (void **)&lpDest->Value.MVszA.lppszA[i]);
			if (hr != hrSuccess)
				return hr;
			memcpy(lpDest->Value.MVszA.lppszA[i], lpSrc->Value.MVszA.lppszA[i], datalength);
		}
		lpDest->Value.MVszA.cValues = lpSrc->Value.MVszA.cValues;
		break;
	case PT_MV_UNICODE:
		hr = lpfAllocMore(sizeof(LPWSTR *) * lpSrc->Value.MVszW.cValues, lpBase, (void **)&lpDest->Value.MVszW.lppszW);
		if (hr != hrSuccess)
			return hr;
		for (ULONG i = 0; i < lpSrc->Value.MVszW.cValues; ++i) {
			hr = lpfAllocMore(wcslen(lpSrc->Value.MVszW.lppszW[i]) * sizeof(WCHAR) + sizeof(WCHAR), lpBase, (void**)&lpDest->Value.MVszW.lppszW[i]);
			if (hr != hrSuccess)
				return hr;
			wcscpy(lpDest->Value.MVszW.lppszW[i], lpSrc->Value.MVszW.lppszW[i]);
		}
		lpDest->Value.MVszW.cValues = lpSrc->Value.MVszW.cValues;
		break;
	case PT_MV_BINARY:
		hr = lpfAllocMore(sizeof(SBinary) * lpSrc->Value.MVbin.cValues, lpBase, (void **)&lpDest->Value.MVbin.lpbin);
		if (hr != hrSuccess)
			return hr;
		for (ULONG i = 0; i < lpSrc->Value.MVbin.cValues; ++i) {
			hr = lpfAllocMore(lpSrc->Value.MVbin.lpbin[i].cb, lpBase, (void **)&lpDest->Value.MVbin.lpbin[i].lpb);
			if (hr != hrSuccess)
				return hr;
			memcpy(lpDest->Value.MVbin.lpbin[i].lpb, lpSrc->Value.MVbin.lpbin[i].lpb, lpSrc->Value.MVbin.lpbin[i].cb);
			lpDest->Value.MVbin.lpbin[i].cb = lpSrc->Value.MVbin.lpbin[i].cb;
		}
		lpDest->Value.MVbin.cValues = lpSrc->Value.MVbin.cValues;
		break;
	case PT_MV_CLSID:
		hr = lpfAllocMore(sizeof(GUID) * lpSrc->Value.MVguid.cValues, lpBase, (void **)&lpDest->Value.MVguid.lpguid);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->Value.MVguid.lpguid, lpSrc->Value.MVguid.lpguid, sizeof(GUID) * lpSrc->Value.MVguid.cValues);
		lpDest->Value.MVguid.cValues = lpSrc->Value.MVguid.cValues;
		break;

	default:
		return MAPI_E_INVALID_PARAMETER;
	}

	lpDest->ulPropTag = lpSrc->ulPropTag;
	return hr;
}

/** 
 * Make a copy of an SRestriction structure
 * 
 * @param[out] lppDest Copy of the restiction in lpSrc
 * @param[in] lpSrc restriction to make a copy of
 * 
 * @return MAPI error code
 */
HRESULT Util::HrCopySRestriction(LPSRestriction *lppDest,
    const SRestriction *lpSrc)
{
	LPSRestriction lpDest = NULL;
	HRESULT hr = MAPIAllocateBuffer(sizeof(SRestriction),
	             reinterpret_cast<void **>(&lpDest));
	if (hr != hrSuccess)
		return hr;
	hr = HrCopySRestriction(lpDest, lpSrc, lpDest);
	if(hr != hrSuccess)
		return hr;
	*lppDest = lpDest;
	return hrSuccess;
}

/** 
 * Make a copy of an SRestriction struction on a preallocated destination
 * 
 * @param[out] lppDest Copy of the restiction in lpSrc
 * @param[in] lpSrc restriction to make a copy of
 * @param[in] lpBase Base pointer to use with MAPIAllocateMore
 * 
 * @return MAPI error code
 */
HRESULT	Util::HrCopySRestriction(LPSRestriction lpDest,
    const SRestriction *lpSrc, void *lpBase)
{
	HRESULT hr = hrSuccess;
	unsigned int i;

	if (lpDest == NULL || lpSrc == NULL || lpBase == NULL)
		return MAPI_E_INVALID_PARAMETER;

	lpDest->rt = lpSrc->rt;

	switch(lpSrc->rt) {
	case RES_AND:
		if (lpSrc->res.resAnd.cRes > 0 && lpSrc->res.resAnd.lpRes == nullptr)
			return MAPI_E_INVALID_PARAMETER;
		lpDest->res.resAnd.cRes = lpSrc->res.resAnd.cRes;
		hr = MAPIAllocateMore(sizeof(SRestriction) * lpSrc->res.resAnd.cRes, lpBase, (void **)&lpDest->res.resAnd.lpRes);
		if (hr != hrSuccess)
			return hr;
		for (i = 0; i < lpSrc->res.resAnd.cRes; ++i) {
			hr = HrCopySRestriction(&lpDest->res.resAnd.lpRes[i], &lpSrc->res.resAnd.lpRes[i], lpBase);
			if(hr != hrSuccess)
				return hr;
		}
		break;
	case RES_OR:
		if (lpSrc->res.resOr.cRes > 0 && lpSrc->res.resOr.lpRes == nullptr)
			return MAPI_E_INVALID_PARAMETER;
		lpDest->res.resOr.cRes = lpSrc->res.resOr.cRes;
		hr = MAPIAllocateMore(sizeof(SRestriction) * lpSrc->res.resOr.cRes, lpBase, (void **)&lpDest->res.resOr.lpRes);
		if (hr != hrSuccess)
			return hr;
		for (i = 0; i < lpSrc->res.resOr.cRes; ++i) {
			hr = HrCopySRestriction(&lpDest->res.resOr.lpRes[i], &lpSrc->res.resOr.lpRes[i], lpBase);
			if(hr != hrSuccess)
				return hr;
		}
		break;
	case RES_NOT:
		if (lpSrc->res.resNot.lpRes == nullptr)
			return MAPI_E_INVALID_PARAMETER;
		hr = MAPIAllocateMore(sizeof(SRestriction), lpBase, (void **) &lpDest->res.resNot.lpRes);
		if (hr != hrSuccess)
			return hr;
		return HrCopySRestriction(lpDest->res.resNot.lpRes, lpSrc->res.resNot.lpRes, lpBase);
	case RES_CONTENT:
		if (lpSrc->res.resContent.lpProp == nullptr)
			return MAPI_E_INVALID_PARAMETER;
		lpDest->res.resContent.ulFuzzyLevel = lpSrc->res.resContent.ulFuzzyLevel;
		lpDest->res.resContent.ulPropTag = lpSrc->res.resContent.ulPropTag;
		hr = MAPIAllocateMore(sizeof(SPropValue), lpBase, (void **) &lpDest->res.resContent.lpProp);
		if (hr != hrSuccess)
			return hr;
		return HrCopyProperty(lpDest->res.resContent.lpProp, lpSrc->res.resContent.lpProp, lpBase);
	case RES_PROPERTY:
		if (lpSrc->res.resProperty.lpProp == nullptr)
			return MAPI_E_INVALID_PARAMETER;
		lpDest->res.resProperty.relop = lpSrc->res.resProperty.relop;
		lpDest->res.resProperty.ulPropTag = lpSrc->res.resProperty.ulPropTag;
		hr = MAPIAllocateMore(sizeof(SPropValue), lpBase, (void **) &lpDest->res.resProperty.lpProp);
		if (hr != hrSuccess)
			return hr;
		return HrCopyProperty(lpDest->res.resProperty.lpProp, lpSrc->res.resProperty.lpProp, lpBase);
	case RES_COMPAREPROPS:
		lpDest->res.resCompareProps.relop = lpSrc->res.resCompareProps.relop;
		lpDest->res.resCompareProps.ulPropTag1 = lpSrc->res.resCompareProps.ulPropTag1;
		lpDest->res.resCompareProps.ulPropTag2 = lpSrc->res.resCompareProps.ulPropTag2;
		break;
	case RES_BITMASK:
		lpDest->res.resBitMask.relBMR = lpSrc->res.resBitMask.relBMR;
		lpDest->res.resBitMask.ulMask = lpSrc->res.resBitMask.ulMask;
		lpDest->res.resBitMask.ulPropTag = lpSrc->res.resBitMask.ulPropTag;
		break;
	case RES_SIZE:
		lpDest->res.resSize.cb = lpSrc->res.resSize.cb;
		lpDest->res.resSize.relop = lpSrc->res.resSize.relop;
		lpDest->res.resSize.ulPropTag = lpSrc->res.resSize.ulPropTag;
		break;
	case RES_EXIST:
		lpDest->res.resExist.ulPropTag = lpSrc->res.resExist.ulPropTag;
		break;
	case RES_SUBRESTRICTION:
		if (lpSrc->res.resSub.lpRes == nullptr)
			return MAPI_E_INVALID_PARAMETER;
		lpDest->res.resSub.ulSubObject = lpSrc->res.resSub.ulSubObject;
		hr = MAPIAllocateMore(sizeof(SRestriction), lpBase, (void **)&lpDest->res.resSub.lpRes);
		if (hr != hrSuccess)
			return hr;
		return HrCopySRestriction(lpDest->res.resSub.lpRes, lpSrc->res.resSub.lpRes, lpBase);
	case RES_COMMENT: // What a weird restriction type
		lpDest->res.resComment.cValues	= lpSrc->res.resComment.cValues;
		lpDest->res.resComment.lpRes	= NULL;

		if (lpSrc->res.resComment.cValues > 0)
		{
			if (lpSrc->res.resComment.lpProp == nullptr)
				return MAPI_E_INVALID_PARAMETER;
			hr = MAPIAllocateMore(sizeof(SPropValue) * lpSrc->res.resComment.cValues, lpBase, (void **) &lpDest->res.resComment.lpProp);
			if (hr != hrSuccess)
				return hr;
			hr = HrCopyPropertyArray(lpSrc->res.resComment.lpProp, lpSrc->res.resComment.cValues, lpDest->res.resComment.lpProp, lpBase);
			if (hr != hrSuccess)
				return hr;
		}
		if (lpSrc->res.resComment.lpRes) {
			if (lpSrc->res.resComment.lpRes == nullptr)
				return MAPI_E_INVALID_PARAMETER;
			hr = MAPIAllocateMore(sizeof(SRestriction), lpBase, (void **) &lpDest->res.resComment.lpRes);
			if (hr != hrSuccess)
				return hr;
			hr = HrCopySRestriction(lpDest->res.resComment.lpRes, lpSrc->res.resComment.lpRes, lpBase);
		}
		break;
	}
	return hr;
}

/** 
 * Make a copy of an ACTIONS structure (rules) on a preallocated destination
 * 
 * @param lpDest Copy of the actions in lpSrc
 * @param lpSrc actions to make a copy of
 * @param lpBase Base pointer to use with MAPIAllocateMore
 * 
 * @return MAPI error code
 */
static HRESULT HrCopyActions(ACTIONS *lpDest, const ACTIONS *lpSrc,
    void *lpBase)
{
	lpDest->cActions = lpSrc->cActions;
	lpDest->ulVersion = lpSrc->ulVersion;
	HRESULT hr = MAPIAllocateMore(sizeof(ACTION) * lpSrc->cActions, lpBase,
	             reinterpret_cast<void **>(&lpDest->lpAction));
	if (hr != hrSuccess)
		return hr;

	memset(lpDest->lpAction, 0, sizeof(ACTION) * lpSrc->cActions);
	for (unsigned int i = 0; i < lpSrc->cActions; ++i) {
		hr = HrCopyAction(&lpDest->lpAction[i], &lpSrc->lpAction[i], lpBase);
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

/** 
 * Make a copy of one ACTION structure (rules) on a preallocated destination
 * 
 * @param lpDest Copy of the action in lpSrc
 * @param lpSrc action to make a copy of
 * @param lpBase Base pointer to use with MAPIAllocateMore
 * 
 * @return MAPI error code
 */
static HRESULT HrCopyAction(ACTION *lpDest, const ACTION *lpSrc, void *lpBase)
{
	HRESULT hr = hrSuccess;

	lpDest->acttype = lpSrc->acttype;
	lpDest->ulActionFlavor = lpSrc->ulActionFlavor;
	lpDest->lpRes = NULL; // also unused
	lpDest->lpPropTagArray = NULL; // unused according to edkmdb.h
	lpDest->ulFlags = lpSrc->ulFlags;

	switch(lpSrc->acttype) {
	case OP_MOVE:
	case OP_COPY:
		lpDest->actMoveCopy.cbStoreEntryId = lpSrc->actMoveCopy.cbStoreEntryId;
		hr = MAPIAllocateMore(lpSrc->actMoveCopy.cbStoreEntryId, lpBase, (void **) &lpDest->actMoveCopy.lpStoreEntryId);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->actMoveCopy.lpStoreEntryId, lpSrc->actMoveCopy.lpStoreEntryId, lpSrc->actMoveCopy.cbStoreEntryId);
		lpDest->actMoveCopy.cbFldEntryId = lpSrc->actMoveCopy.cbFldEntryId;
		hr = MAPIAllocateMore(lpSrc->actMoveCopy.cbFldEntryId, lpBase, (void **) &lpDest->actMoveCopy.lpFldEntryId);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->actMoveCopy.lpFldEntryId, lpSrc->actMoveCopy.lpFldEntryId, lpSrc->actMoveCopy.cbFldEntryId);
		break;
	case OP_REPLY:
	case OP_OOF_REPLY:
		lpDest->actReply.cbEntryId = lpSrc->actReply.cbEntryId;
		hr = MAPIAllocateMore(lpSrc->actReply.cbEntryId, lpBase, (void **) &lpDest->actReply.lpEntryId);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->actReply.lpEntryId, lpSrc->actReply.lpEntryId, lpSrc->actReply.cbEntryId);
		lpDest->actReply.guidReplyTemplate = lpSrc->actReply.guidReplyTemplate;
		break;
	case OP_DEFER_ACTION:
		lpDest->actDeferAction.cbData = lpSrc->actDeferAction.cbData;
		hr = MAPIAllocateMore(lpSrc->actDeferAction.cbData, lpBase, (void **)&lpDest->actDeferAction.pbData);
		if (hr != hrSuccess)
			return hr;
		memcpy(lpDest->actDeferAction.pbData, lpSrc->actDeferAction.pbData, lpSrc->actDeferAction.cbData);
		break;
	case OP_BOUNCE:
		lpDest->scBounceCode = lpSrc->scBounceCode;
		break;
	case OP_FORWARD:
	case OP_DELEGATE:
		hr = MAPIAllocateMore(CbNewSRowSet(lpSrc->lpadrlist->cEntries), lpBase, reinterpret_cast<void **>(&lpDest->lpadrlist));
		if (hr != hrSuccess)
			return hr;
		return Util::HrCopySRowSet(reinterpret_cast<SRowSet *>(lpDest->lpadrlist), reinterpret_cast<SRowSet *>(lpSrc->lpadrlist), lpBase);
	case OP_TAG:
		return Util::HrCopyProperty(&lpDest->propTag, &lpSrc->propTag, lpBase);
	case OP_DELETE:
	case OP_MARK_AS_READ:
		break;
	default:
		break;
	}
	return hr;
}

/** 
 * Make a copy of a complete rowset
 * 
 * @param[out] lpDest Preallocated SRowSet structure for destination. Should have enough place for lpSrc->cRows.
 * @param[in] lpSrc Make a copy of the rows in this set
 * @param[in] lpBase Use MAPIAllocateMore with this pointer
 * 
 * @return MAPI error code
 */
HRESULT Util::HrCopySRowSet(LPSRowSet lpDest, const SRowSet *lpSrc,
    void *lpBase)
{
	lpDest->cRows = 0;
	for (unsigned int i = 0; i < lpSrc->cRows; ++i) {
		HRESULT hr = HrCopySRow(&lpDest->aRow[i], &lpSrc->aRow[i], lpBase);
		if (hr != hrSuccess)
			return hr;
		++lpDest->cRows;
	}
	return hrSuccess;
}

/** 
 * Make a copy one row of a rowset.
 *
 * @note According to MSDN, rows in an SRowSet should use separate
 * MAPIAllocate() calls (not MAPIAllocateMore) so that rows can be
 * freed individually.  Make sure to free your RowSet with
 * FreeProws(), which frees the rows individually.
 *
 * However, when you have a rowset within a rowset (e.g. lpadrlist in
 * OP_FORWARD and OP_DELEGATE rules) these need to be allocated to the
 * original row, and not separate
 * 
 * @param[out] lpDest Preallocated destination base pointer of the new row
 * @param[in] lpSrc Row to make a copy of
 * @param[in] lpBase Optional base pointer to allocate memory for properties
 * 
 * @return MAPI error code
 */
HRESULT	Util::HrCopySRow(LPSRow lpDest, const SRow *lpSrc, void *lpBase)
{
	lpDest->cValues = lpSrc->cValues;
	auto hr = MAPIAllocateMore(sizeof(SPropValue) * lpSrc->cValues, lpBase, reinterpret_cast<void **>(&lpDest->lpProps));
	if (hr != hrSuccess)
		return hr;

	return HrCopyPropertyArray(lpSrc->lpProps, lpSrc->cValues, lpDest->lpProps, lpBase ? lpBase : lpDest->lpProps);
}

HRESULT	Util::HrCopyPropTagArray(const SPropTagArray *lpSrc,
    LPSPropTagArray *lppDest)
{
	SPropTagArrayPtr ptrPropTagArray;
	HRESULT hr = MAPIAllocateBuffer(CbNewSPropTagArray(lpSrc->cValues), &~ptrPropTagArray);
	if (hr != hrSuccess)
		return hr;

	memcpy(ptrPropTagArray->aulPropTag, lpSrc->aulPropTag, lpSrc->cValues * sizeof *lpSrc->aulPropTag);
	ptrPropTagArray->cValues = lpSrc->cValues;

	*lppDest = ptrPropTagArray.release();
	return hrSuccess;
}

/**
 * Forces all string types in an SPropTagArray to either PT_STRING8 or
 * PT_UNICODE according to the MAPI_UNICODE flag in ulFlags.
 *
 * @param[in]	ulFlags	0 or MAPI_UNICODE for PT_STRING8 or PT_UNICODE proptags
 * @param[in]	lpSrc	Source SPropTagArray to copy to lppDest
 * @param[out]	lppDest	Destination SPropTagArray with fixed types for strings
 */
void Util::proptag_change_unicode(ULONG flags, SPropTagArray &src)
{
	unsigned int newtype = (flags & MAPI_UNICODE) ? PT_UNICODE : PT_STRING8;
	for (ULONG n = 0; n < src.cValues; ++n) {
		if (PROP_TYPE(src.aulPropTag[n]) != PT_STRING8 &&
		    PROP_TYPE(src.aulPropTag[n]) != PT_UNICODE)
			continue;
		src.aulPropTag[n] = CHANGE_PROP_TYPE(src.aulPropTag[n], newtype);
	}
}

/** 
 * Make a copy of a byte array using MAPIAllocate functions. This
 * function has a special case: when the input size is 0, the returned
 * pointer is not allocated and NULL is returned.
 * 
 * @param[in] ulSize number of bytes in lpSrc to copy
 * @param[in] lpSrc bytes to copy into lppDest
 * @param[out] lpulDestSize number of bytes copied from lpSrc
 * @param[out] lppDest copied buffer
 * @param[in] lpBase Optional base pointer for MAPIAllocateMore
 * 
 * @return MAPI error code
 */
HRESULT Util::HrCopyBinary(ULONG ulSize, const BYTE *lpSrc,
    ULONG *lpulDestSize, LPBYTE *lppDest, LPVOID lpBase)
{
	LPBYTE lpDest = NULL;

	if (ulSize == 0) {
		*lpulDestSize = 0;
		*lppDest = NULL;
		return hrSuccess;
	}
	auto hr = MAPIAllocateMore(ulSize, lpBase, reinterpret_cast<void **>(&lpDest));
	if (hr != hrSuccess)
		return hr;

	memcpy(lpDest, lpSrc, ulSize);

	*lppDest = lpDest;
	*lpulDestSize = ulSize;
	return hrSuccess;
}

/** 
 * Make a copy of an EntryID. Since this actually uses HrCopyBinary, this
 * function has a special case: when the input size is 0, the returned
 * pointer is not allocated and NULL is returned.
 * 
 * @param[in] ulSize size of the entryid
 * @param[in] lpSrc the entryid to make a copy of
 * @param[out] lpulDestSize output size of the entryid
 * @param[out] lppDest the copy of the entryid
 * @param[in] lpBase Optional pointer for MAPIAllocateMore
 * 
 * @return MAPI Error code
 */
HRESULT	Util::HrCopyEntryId(ULONG ulSize, const ENTRYID *lpSrc,
    ULONG *lpulDestSize, LPENTRYID* lppDest, LPVOID lpBase)
{
	return HrCopyBinary(ulSize, (const BYTE *)lpSrc, lpulDestSize,
	       (LPBYTE *)lppDest, lpBase);
}

/*
 * The full checks need to be done. One cannot take a shortcut à la
 * "result = (a - b)", as this can lead to underflow and falsify the result.
 * (Consider: short a=-16385, b=16385;)
 */
template<typename T> static int twcmp(T a, T b)
{
	return (a < b) ? -1 : (a == b) ? 0 : 1;
}

/**
 * Compare two SBinary values.
 * A shorter binary value always compares less to a longer binary value. So only
 * when the two values are equal in size the actual data is compared.
 * 
 * @param[in]	left
 *					The left SBinary value that should be compared to right.
 * @param[in]	right
 *					The right SBinary value that should be compared to left.
 *
 * @return		integer
 * @retval		0	The two values are equal.
 * @retval		<0	The left value is 'less than' the right value.
 * @retval		>0  The left value is 'greater than' the right value.
 */
int Util::CompareSBinary(const SBinary &sbin1, const SBinary &sbin2)
{
	if (sbin1.lpb && sbin2.lpb && sbin1.cb > 0 && sbin1.cb == sbin2.cb)
		return memcmp(sbin1.lpb, sbin2.lpb, sbin1.cb);
	else
		return twcmp(sbin1.cb, sbin2.cb);
}

/** 
 * Compare two properties, optionally using an ECLocale object for
 * correct string compares. String compares are always done case
 * insensitive.
 *
 * The function cannot compare different typed properties.  The PR_ANR
 * property is a special case, where it checks for 'contains' rather
 * than 'is equal'.
 * 
 * @param[in] lpProp1 property to compare
 * @param[in] lpProp2 property to compare
 * @param[in] locale current locale object
 * @param[out] lpCompareResult the compare result
 *             0, the properties are equal
 *            <0, The left value is 'less than' the right value.
 *            >0, The left value is 'greater than' the right value.
 * 
 * @return MAPI Error code
 * @retval MAPI_E_INVALID_PARAMETER input parameters are NULL or property types are different
 * @retval MAPI_E_INVALID_TYPE the type of the properties is not a valid MAPI type
 */
// @todo: Check if we need unicode string functions here
HRESULT Util::CompareProp(const SPropValue *lpProp1, const SPropValue *lpProp2,
    const ECLocale &locale, int *lpCompareResult)
{
	HRESULT	hr = hrSuccess;
	int		nCompareResult = 0;

	if (lpProp1 == NULL || lpProp2 == NULL || lpCompareResult == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (PROP_TYPE(lpProp1->ulPropTag) != PROP_TYPE(lpProp2->ulPropTag))
		return MAPI_E_INVALID_PARAMETER;

	switch(PROP_TYPE(lpProp1->ulPropTag)) {
	case PT_I2:
		nCompareResult = twcmp(lpProp1->Value.i, lpProp2->Value.i);
		break;
	case PT_LONG:
		nCompareResult = twcmp(lpProp1->Value.ul, lpProp2->Value.ul);
		break;
	case PT_R4:
		nCompareResult = twcmp(lpProp1->Value.flt, lpProp2->Value.flt);
		break;
	case PT_BOOLEAN:
		nCompareResult = twcmp(lpProp1->Value.b, lpProp2->Value.b);
		break;
	case PT_DOUBLE:
	case PT_APPTIME:
		nCompareResult = twcmp(lpProp1->Value.dbl, lpProp2->Value.dbl);
		break;
	case PT_I8:
		nCompareResult = twcmp(lpProp1->Value.li.QuadPart, lpProp2->Value.li.QuadPart);
		break;
	case PT_UNICODE:
		if (lpProp1->Value.lpszW && lpProp2->Value.lpszW)
			if (lpProp2->ulPropTag == PR_ANR)
				nCompareResult = wcs_icontains(lpProp1->Value.lpszW, lpProp2->Value.lpszW, locale);
			else
				nCompareResult = wcs_icompare(lpProp1->Value.lpszW, lpProp2->Value.lpszW, locale);
		else
			nCompareResult = lpProp1->Value.lpszW != lpProp2->Value.lpszW;
		break;
	case PT_STRING8:
		if (lpProp1->Value.lpszA && lpProp2->Value.lpszA)
			if(lpProp2->ulPropTag == PR_ANR) {
				nCompareResult = str_icontains(lpProp1->Value.lpszA, lpProp2->Value.lpszA, locale);
			} else
				nCompareResult = str_icompare(lpProp1->Value.lpszA, lpProp2->Value.lpszA, locale);
		else
			nCompareResult = lpProp1->Value.lpszA != lpProp2->Value.lpszA;
		break;
	case PT_SYSTIME:
	case PT_CURRENCY:
		if (lpProp1->Value.cur.Hi == lpProp2->Value.cur.Hi)
			nCompareResult = twcmp(lpProp1->Value.cur.Lo, lpProp2->Value.cur.Lo);
		else
			nCompareResult = twcmp(lpProp1->Value.cur.Hi, lpProp2->Value.cur.Hi);
		break;
	case PT_BINARY:
		nCompareResult = CompareSBinary(lpProp1->Value.bin, lpProp2->Value.bin);
		break;
	case PT_CLSID:
		nCompareResult = memcmp(lpProp1->Value.lpguid, lpProp2->Value.lpguid, sizeof(GUID));
		break;
		
	case PT_MV_I2:
		if (lpProp1->Value.MVi.cValues == lpProp2->Value.MVi.cValues) {
			for (unsigned int i = 0; i < lpProp1->Value.MVi.cValues; ++i) {
				nCompareResult = twcmp(lpProp1->Value.MVi.lpi[i], lpProp2->Value.MVi.lpi[i]);
				if(nCompareResult != 0)
					break;
			}
		} else
			nCompareResult = twcmp(lpProp1->Value.MVi.cValues, lpProp2->Value.MVi.cValues);
		break;
	case PT_MV_LONG:
		if (lpProp1->Value.MVl.cValues == lpProp2->Value.MVl.cValues) {
			for (unsigned int i = 0; i < lpProp1->Value.MVl.cValues; ++i) {
				nCompareResult = twcmp(lpProp1->Value.MVl.lpl[i], lpProp2->Value.MVl.lpl[i]);
				if(nCompareResult != 0)
					break;
			}
		} else
			nCompareResult = twcmp(lpProp1->Value.MVl.cValues, lpProp2->Value.MVl.cValues);
		break;
	case PT_MV_R4:
		if (lpProp1->Value.MVflt.cValues == lpProp2->Value.MVflt.cValues) {
			for (unsigned int i = 0; i < lpProp1->Value.MVflt.cValues; ++i) {
				nCompareResult = twcmp(lpProp1->Value.MVflt.lpflt[i], lpProp2->Value.MVflt.lpflt[i]);
				if(nCompareResult != 0)
					break;
			}
		} else
			nCompareResult = twcmp(lpProp1->Value.MVflt.cValues, lpProp2->Value.MVflt.cValues);
		break;
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		if (lpProp1->Value.MVdbl.cValues == lpProp2->Value.MVdbl.cValues) {
			for (unsigned int i = 0; i < lpProp1->Value.MVdbl.cValues; ++i) {
				nCompareResult = twcmp(lpProp1->Value.MVdbl.lpdbl[i], lpProp2->Value.MVdbl.lpdbl[i]);
				if(nCompareResult != 0)
					break;
			}
		} else
			nCompareResult = twcmp(lpProp1->Value.MVdbl.cValues, lpProp2->Value.MVdbl.cValues);
		break;
	case PT_MV_I8:
		if (lpProp1->Value.MVli.cValues == lpProp2->Value.MVli.cValues) {
			for (unsigned int i = 0; i < lpProp1->Value.MVli.cValues; ++i) {
				nCompareResult = twcmp(lpProp1->Value.MVli.lpli[i].QuadPart, lpProp2->Value.MVli.lpli[i].QuadPart);
				if(nCompareResult != 0)
					break;
			}
		} else
			nCompareResult = twcmp(lpProp1->Value.MVli.cValues, lpProp2->Value.MVli.cValues);
		break;
	case PT_MV_SYSTIME:
	case PT_MV_CURRENCY:
		if (lpProp1->Value.MVcur.cValues == lpProp2->Value.MVcur.cValues) {
			for (unsigned int i = 0; i < lpProp1->Value.MVcur.cValues; ++i) {
				if(lpProp1->Value.MVcur.lpcur[i].Hi == lpProp2->Value.MVcur.lpcur[i].Hi)
					nCompareResult = twcmp(lpProp1->Value.MVcur.lpcur[i].Lo, lpProp2->Value.MVcur.lpcur[i].Lo);
				else
					nCompareResult = twcmp(lpProp1->Value.MVcur.lpcur[i].Hi, lpProp2->Value.MVcur.lpcur[i].Hi);

				if(nCompareResult != 0)
					break;
			}
		} else
			nCompareResult = lpProp1->Value.MVcur.cValues == lpProp2->Value.MVcur.cValues;
		break;
	case PT_MV_CLSID:
		if (lpProp1->Value.MVguid.cValues == lpProp2->Value.MVguid.cValues)
			nCompareResult = memcmp(lpProp1->Value.MVguid.lpguid, lpProp2->Value.MVguid.lpguid, sizeof(GUID)*lpProp1->Value.MVguid.cValues);
		else
			nCompareResult = twcmp(lpProp1->Value.MVguid.cValues, lpProp2->Value.MVguid.cValues);
		break;
	case PT_MV_BINARY:
		if (lpProp1->Value.MVbin.cValues == lpProp2->Value.MVbin.cValues) {
			for (unsigned int i = 0; i < lpProp1->Value.MVbin.cValues; ++i) {
				nCompareResult = CompareSBinary(lpProp1->Value.MVbin.lpbin[i], lpProp2->Value.MVbin.lpbin[i]);
				if(nCompareResult != 0)
					break;
			}
		} else
			nCompareResult = twcmp(lpProp1->Value.MVbin.cValues, lpProp2->Value.MVbin.cValues);
		break;
	case PT_MV_UNICODE:
		if (lpProp1->Value.MVszW.cValues == lpProp2->Value.MVszW.cValues) {
			for (unsigned int i = 0; i < lpProp1->Value.MVszW.cValues; ++i) {
				if (lpProp1->Value.MVszW.lppszW[i] && lpProp2->Value.MVszW.lppszW[i])
					nCompareResult = wcscasecmp(lpProp1->Value.MVszW.lppszW[i], lpProp2->Value.MVszW.lppszW[i]);
				else
					nCompareResult = lpProp1->Value.MVszW.lppszW[i] != lpProp2->Value.MVszW.lppszW[i];

				if(nCompareResult != 0)
					break;
			}
		} else
			nCompareResult = twcmp(lpProp1->Value.MVszA.cValues, lpProp2->Value.MVszA.cValues);
		break;
	case PT_MV_STRING8:
		if (lpProp1->Value.MVszA.cValues == lpProp2->Value.MVszA.cValues) {
			for (unsigned int i = 0; i < lpProp1->Value.MVszA.cValues; ++i) {
				if (lpProp1->Value.MVszA.lppszA[i] && lpProp2->Value.MVszA.lppszA[i])
					nCompareResult = strcasecmp(lpProp1->Value.MVszA.lppszA[i], lpProp2->Value.MVszA.lppszA[i]);
				else
					nCompareResult = lpProp1->Value.MVszA.lppszA[i] != lpProp2->Value.MVszA.lppszA[i];

				if(nCompareResult != 0)
					break;
			}
		} else
			nCompareResult = twcmp(lpProp1->Value.MVszA.cValues, lpProp2->Value.MVszA.cValues);
		break;
	default:
		return MAPI_E_INVALID_TYPE;
	}

	*lpCompareResult = nCompareResult;
	return hr;
}

/** 
 * Calculates the number of bytes the property uses of memory,
 * excluding the SPropValue struct itself, and any pointers required in this struct.
 * 
 * @param[in] lpProp The property to calculate the size of
 * 
 * @return size of the property
 */
unsigned int Util::PropSize(const SPropValue *lpProp)
{
	unsigned int ulSize;

	if(lpProp == NULL)
		return 0;

	switch(PROP_TYPE(lpProp->ulPropTag)) {
	case PT_I2:
		return 2;
	case PT_BOOLEAN:
	case PT_R4:
	case PT_LONG:
		return 4;
	case PT_APPTIME:
	case PT_DOUBLE:
	case PT_I8:
		return 8;
	case PT_UNICODE:
		return lpProp->Value.lpszW ? wcslen(lpProp->Value.lpszW) : 0;
	case PT_STRING8:
		return lpProp->Value.lpszA ? strlen(lpProp->Value.lpszA) : 0;
	case PT_SYSTIME:
	case PT_CURRENCY:
		return 8;
	case PT_BINARY:
		return lpProp->Value.bin.cb;
	case PT_CLSID:
		return sizeof(GUID);
	case PT_MV_I2:
		return 2 * lpProp->Value.MVi.cValues;
	case PT_MV_R4:
		return 4 * lpProp->Value.MVflt.cValues;
	case PT_MV_LONG:
		return 4 * lpProp->Value.MVl.cValues;
	case PT_MV_APPTIME:
	case PT_MV_DOUBLE:
		return 8 * lpProp->Value.MVdbl.cValues;
	case PT_MV_I8:
		return 8 * lpProp->Value.MVli.cValues;
	case PT_MV_UNICODE:
		ulSize = 0;
		for (unsigned int i = 0; i < lpProp->Value.MVszW.cValues; ++i)
			ulSize += (lpProp->Value.MVszW.lppszW[i]) ? wcslen(lpProp->Value.MVszW.lppszW[i]) : 0;
		return ulSize;
	case PT_MV_STRING8:
		ulSize = 0;
		for (unsigned int i = 0; i < lpProp->Value.MVszA.cValues; ++i)
			ulSize += (lpProp->Value.MVszA.lppszA[i]) ? strlen(lpProp->Value.MVszA.lppszA[i]) : 0;
		return ulSize;
	case PT_MV_SYSTIME:
	case PT_MV_CURRENCY:
		return 8 * lpProp->Value.MVcur.cValues;	
	case PT_MV_BINARY:
		ulSize = 0;
		for (unsigned int i = 0; i < lpProp->Value.MVbin.cValues; ++i)
			ulSize+= lpProp->Value.MVbin.lpbin[i].cb;
		return ulSize;
	case PT_MV_CLSID:
		return sizeof(GUID) * lpProp->Value.MVguid.cValues;
	default:
		return 0;
	}
}

/**
 * Convert plaintext to HTML using streams.
 *
 * Converts the text stream to HTML, and writes in the html stream.
 * Both streams will be at the end on return.  This function does no
 * error checking.  If a character in the text cannot be represented
 * in the given codepage, it will make a unicode HTML entity instead.
 *
 * @param[in]	text	IStream object as plain text input, must be PT_UNICODE
 * @param[out]	html	IStream object for HTML output
 * @param[in]	ulCodePage Codepage to convert HTML stream to
 *
 * @return		HRESULT				Mapi error code, from IMemStream
 * @retval		MAPI_E_NOT_FOUND	No suitable charset found for given ulCodepage
 * @retval		MAPI_E_BAD_CHARWIDTH Iconv error
 */
// @todo please optimize function, quite slow. (mostly due to HtmlEntityFromChar)
#define BUFSIZE 65536
HRESULT Util::HrTextToHtml(IStream *text, IStream *html, ULONG ulCodepage)
{
	ULONG cRead;
	std::wstring strHtml;
	WCHAR lpBuffer[BUFSIZE];
	static const char header1[] = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\n" \
					"<HTML>\n" \
					"<HEAD>\n" \
					"<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=";
	// inserts charset in header here
	static const char header2[] = "\">\n"												\
					"<META NAME=\"Generator\" CONTENT=\"Kopano HTML builder 1.0\">\n" \
					"<TITLE></TITLE>\n" \
					"</HEAD>\n" \
					"<BODY>\n" \
					"<!-- Converted from text/plain format -->\n" \
					"\n" \
					"<P><FONT STYLE=\"font-family: courier\" SIZE=2>\n";
					
	static const char footer[] = "</FONT>\n" \
					"</P>\n" \
					"\n" \
					"</BODY>" \
					"</HTML>";
	const char	*readBuffer = NULL;
	std::unique_ptr<char[]> writeBuffer;
	const char *lpszCharset;

	auto hr = HrGetCharsetByCP(ulCodepage, &lpszCharset);
	if (hr != hrSuccess)
		// client actually should have set the PR_INTERNET_CPID to the correct value
		lpszCharset = "us-ascii";

	auto cd = iconv_open(lpszCharset, CHARSET_WCHAR);
	if (cd == reinterpret_cast<iconv_t>(-1))
		return MAPI_E_BAD_CHARWIDTH;
	writeBuffer.reset(new(std::nothrow) char[BUFSIZE * 2]);
	if (writeBuffer == nullptr) {
		hr = MAPI_E_NOT_ENOUGH_MEMORY;
		goto exit;
	}

	// @todo, run this through iconv aswell?
	hr = html->Write(header1, strlen(header1), NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = html->Write(lpszCharset, strlen(lpszCharset), NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = html->Write(header2, strlen(header2), NULL);
	if (hr != hrSuccess)
		goto exit;

	while (1) {
		strHtml.clear();

		hr = text->Read(lpBuffer, BUFSIZE*sizeof(WCHAR), &cRead);
		if (hr != hrSuccess)
			goto exit;

		if (cRead == 0)
			break;

		cRead /= sizeof(WCHAR);

		// escape some characters in HTML
		for (unsigned int i = 0; i < cRead; ++i) {
			if (lpBuffer[i] != ' ') {
				std::wstring str;
				CHtmlEntity::CharToHtmlEntity(lpBuffer[i], str);
				strHtml += str;
				continue;
			}
			if ((i + 1) < cRead && lpBuffer[i+1] == ' ')
				strHtml += L"&nbsp;";
			else
				strHtml += L" ";
		}

		/* Convert WCHAR to wanted (8-bit) charset */
		readBuffer = (const char*)strHtml.c_str();
		auto stRead = strHtml.size() * sizeof(wchar_t);

		while (stRead > 0) {
			auto wPtr = writeBuffer.get();
			size_t stWrite = BUFSIZE * 2;
			auto err = iconv(cd, iconv_HACK(&readBuffer), &stRead, &wPtr, &stWrite);
			size_t stWritten = (BUFSIZE * 2) - stWrite;
			// write to stream
			hr = html->Write(writeBuffer.get(), stWritten, NULL);
			if (hr != hrSuccess)
				goto exit;

			if (err != static_cast<size_t>(-1))
				continue;
			// make html number from WCHAR entry
			std::string strHTMLUnicode = "&#";
			strHTMLUnicode += stringify(*reinterpret_cast<const wchar_t *>(readBuffer));
			strHTMLUnicode += ";";

			hr = html->Write(strHTMLUnicode.c_str(), strHTMLUnicode.length(), NULL);
			if (hr != hrSuccess)
				goto exit;

			// skip unknown character
			readBuffer += sizeof(WCHAR);
			stRead -= sizeof(WCHAR);
		}
	}
	// @todo, run through iconv?
	hr = html->Write(footer, strlen(footer), NULL);

exit:
	if (cd != (iconv_t)-1)
		iconv_close(cd);
	return hr;
}

/** 
 * Convert a plain-text widestring text to html data in specific
 * codepage. No html headers or footers are set in the string like the
 * stream version does.
 * 
 * @param[in] text plaintext to convert to html
 * @param[out] strHTML append to this html string
 * @param[in] ulCodepage html will be in this codepage
 * 
 * @return MAPI Error code
 */
HRESULT Util::HrTextToHtml(const WCHAR *text, std::string &strHTML, ULONG ulCodepage)
{
	const char *lpszCharset;
	std::wstring wHTML;
	auto hr = HrGetCharsetByCP(ulCodepage, &lpszCharset);
	if (hr != hrSuccess) {
		// client actually should have set the PR_INTERNET_CPID to the correct value
		lpszCharset = "us-ascii";
		hr = hrSuccess;
	}

	// escape some characters in HTML
	for (ULONG i = 0; text[i] != '\0'; ++i) {
		if (text[i] != ' ') {
			std::wstring str;
			CHtmlEntity::CharToHtmlEntity(text[i], str);
			wHTML += str;
			continue;
		}
		if (text[i+1] == ' ')
			wHTML += L"&nbsp;";
		else
			wHTML += L" ";
	}

	try {
		strHTML += convert_to<std::string>(lpszCharset, wHTML, rawsize(wHTML), CHARSET_WCHAR);
	} catch (const convert_exception &) {
	}

	return hr;
}

/**
 * Convert plaintext to uncompressed RTF using streams.
 *
 * Converts the text stream to RTF, and writes in the rtf stream.
 * Both streams will be at the end on return.  This function does no
 * error checking.
 *
 * @param[in]	text	IStream object as plain text input, must be PT_UNICODE
 * @param[out]	rtf		IStream object for RTF output
 *
 * @return		HRESULT		hrSuccess
 */
// @todo: remove this shizzle ?
HRESULT Util::HrTextToRtf(IStream *text, IStream *rtf)
{
	ULONG cRead;
	WCHAR c[BUFSIZE];
	static const char header[] = "{\\rtf1\\ansi\\ansicpg1252\\fromtext \\deff0{\\fonttbl\n" \
					"{\\f0\\fswiss Arial;}\n" \
					"{\\f1\\fmodern Courier New;}\n" \
					"{\\f2\\fnil\\fcharset2 Symbol;}\n" \
					"{\\f3\\fmodern\\fcharset0 Courier New;}}\n" \
					"{\\colortbl\\red0\\green0\\blue0;\\red0\\green0\\blue255;}\n" \
					"\\uc1\\pard\\plain\\deftab360 \\f0\\fs20 ";
	static const char footer[] = "}";

	rtf->Write(header, strlen(header), NULL);

	while(1) {
		auto ret = text->Read(c, BUFSIZE * sizeof(WCHAR), &cRead);
		if (ret != hrSuccess || cRead == 0)
			break;

		cRead /= sizeof(WCHAR);
		for (unsigned int i = 0; i < cRead; ++i) {
			switch (c[i]) {
			case 0:
				break;
			case '\r':
				break;
			case '\n':
				rtf->Write("\\line\n", 6, nullptr);
				break;

			case '\\':
				rtf->Write("\\\\",2,NULL);
				break;

			case '{':
				rtf->Write("\\{",2,NULL);
				break;

			case '}':
				rtf->Write("\\}",2,NULL);
				break;

			case '\t':
				rtf->Write("\\tab ",5,NULL);
				break;

			case '\f':			// formfeed, ^L
				rtf->Write("\\page\n",6,NULL);
				break;
			default:
				if (c[i] < ' ' || (c[i] > 127 && c[i] <= 255)) {
					char hex[16];
					snprintf(hex, 16, "\\'%X", c[i]);
					rtf->Write(hex, strlen(hex), NULL);
				} else if (c[i] > 255) {
					// make a unicode char. in signed short
					char hex[16];
					snprintf(hex, 16, "\\u%hd ?", (signed short)c[i]); // %hd is signed short (h is the inverse of l modifier)
					rtf->Write(hex, strlen(hex), NULL);
				} else {
					rtf->Write(&c[i], 1, NULL);
				}
			}
		}
	}

	rtf->Write(footer, strlen(footer), NULL);

	return hrSuccess;
}

/** 
 * Find a given property tag in an array of property tags.  Use
 * PT_UNSPECIFIED in the proptype to find the first matching property
 * id in the property array.
 * 
 * @param[in] lpPropTags The property tag array to search in
 * @param[in] ulPropTag The property tag to search for
 * 
 * @return index in the lpPropTags array
 */
LONG Util::FindPropInArray(const SPropTagArray *lpPropTags, ULONG ulPropTag)
{
	unsigned int i = 0;

	if (!lpPropTags)
		return -1;

	for (i = 0; i < lpPropTags->cValues; ++i) {
		if(lpPropTags->aulPropTag[i] == ulPropTag)
			break;
		if(PROP_TYPE(ulPropTag) == PT_UNSPECIFIED && PROP_ID(lpPropTags->aulPropTag[i]) == PROP_ID(ulPropTag))
			break;
	}
	return (i != lpPropTags->cValues) ? i : -1;
}

/** 
 * Return a human readable string for a specified HRESULT code.  You
 * should only call this function if hr contains an error. If an error
 * code is not specified in this function, it will return 'access
 * denied' as default error string.
 * 
 * @param[in]	hr			return string version for the given value.
 * @param[out]	lppszError	Pointer to a character pointer that will contain the error
 * 							message on success. If no lpBase was provided, the result
 * 							must be freed with MAPIFreeBuffer.
 * @param[in]	lpBase		optional base pointer for use with MAPIAllocateMore.
 * 
 * @retval	hrSuccess on success.
 */
HRESULT Util::HrMAPIErrorToText(HRESULT hr, LPTSTR *lppszError, void *lpBase)
{
	tstring strError;
	LPCTSTR lpszError = NULL;

	if (lppszError == NULL)
		return MAPI_E_INVALID_PARAMETER;

	switch(hr)
	{
	case MAPI_E_END_OF_SESSION:
		lpszError = KC_TX("End of Session");
		break;
	case MAPI_E_NETWORK_ERROR:
		lpszError = KC_TX("Connection lost");
		break;
	case MAPI_E_NO_ACCESS:
		lpszError = KC_TX("Access denied");
		break;
	case MAPI_E_FOLDER_CYCLE:
		lpszError = KC_TX("Unable to move or copy folders. Can't copy folder. A top-level can't be copied to one of its subfolders. Or, you may not have appropriate permissions for the folder. To check your permissions for the folder, right-click the folder, and then click Properties on the shortcut menu.");
		break;
	case MAPI_E_STORE_FULL:
		lpszError = KC_TX("The message store has reached its maximum size. To reduce the amount of data in this message store, select some items that you no longer need, and permanently (SHIFT + DEL) delete them.");
		break;
	case MAPI_E_USER_CANCEL:
		lpszError = KC_TX("The user canceled the operation, typically by clicking the Cancel button in a dialog box.");
		break;
	case MAPI_E_LOGON_FAILED:
		lpszError = KC_TX("A logon session could not be established.");
		break;
	case MAPI_E_COLLISION:
		lpszError = KC_TX("The name of the folder being moved or copied is the same as that of a subfolder in the destination folder. The message store provider requires that folder names be unique. The operation stops without completing.");
		break;
	case MAPI_W_PARTIAL_COMPLETION:
		lpszError = KC_TX("The operation succeeded, but not all entries were successfully processed, copied, deleted or moved");// emptied
		break;
	case MAPI_E_UNCONFIGURED:
		lpszError = KC_TX("The provider does not have enough information to complete the logon. Or, the service provider has not been configured.");
		break;
	case MAPI_E_FAILONEPROVIDER:
		lpszError = KC_TX("One of the providers cannot log on, but this error should not disable the other services.");
		break;
	case MAPI_E_DISK_ERROR:
		lpszError = KC_TX("A database error or I/O error has occurred.");
		break;
	case MAPI_E_HAS_FOLDERS:
		lpszError = KC_TX("The subfolder being deleted contains subfolders.");
		break;
	case MAPI_E_HAS_MESSAGES:
		lpszError = KC_TX("The subfolder being deleted contains messages.");
		break;
	default: {
			strError = KC_TX("No description available.");
			strError.append(1, ' ');
			strError.append(KC_TX("MAPI error code:"));
			strError.append(1, ' ');
			strError.append(tstringify_hex(hr));
			lpszError = strError.c_str();
		}
		break;
	}

	hr = MAPIAllocateMore((_tcslen(lpszError) + 1) * sizeof(*lpszError), lpBase, reinterpret_cast<void **>(lppszError));
	if (hr != hrSuccess)
		return hr;
	_tcscpy(*lppszError, lpszError);
	return hrSuccess;
}

/** 
 * Checks for invalid data in a proptag array. NULL input is
 * considered valid. Any known property type is considered valid,
 * including PT_UNSPECIFIED, PT_NULL and PT_ERROR.
 * 
 * @param[in] lpPropTagArray property tag array to validate
 * 
 * @return true for valid, false for invalid
 */
bool Util::ValidatePropTagArray(const SPropTagArray *lpPropTagArray)
{
	bool bResult = false;

	if (lpPropTagArray == NULL)
		return true;
	for (unsigned int i = 0; i < lpPropTagArray->cValues; ++i) {
		switch (PROP_TYPE(lpPropTagArray->aulPropTag[i]))
		{
		case PT_UNSPECIFIED:
		case PT_NULL:
		case PT_I2:
		case PT_I4:
		case PT_R4:
		case PT_R8:
		case PT_BOOLEAN:
		case PT_CURRENCY:
		case PT_APPTIME:
		case PT_SYSTIME:
		case PT_I8:
		case PT_STRING8:
		case PT_BINARY:
		case PT_UNICODE:
		case PT_CLSID:
		case PT_OBJECT:
		case PT_MV_I2:
		case PT_MV_LONG:
		case PT_MV_R4:
		case PT_MV_DOUBLE:
		case PT_MV_CURRENCY:
		case PT_MV_APPTIME:
		case PT_MV_SYSTIME:
		case PT_MV_BINARY:
		case PT_MV_STRING8:
		case PT_MV_UNICODE:
		case PT_MV_CLSID:
		case PT_MV_I8:
		case PT_ERROR:
			bResult = true;
			break;
		default:
			return false;
		}
	}
	return bResult;
}

/** 
 * Append the full contents of a stream into a std::string. It might use
 * the ECMemStream interface if available, otherwise it will do a
 * normal stream copy. The position of the stream on return is not
 * stable.
 * 
 * @param[in] sInput The stream to copy data from
 * @param[in] strOutput The string to place data in
 * 
 * @return MAPI Error code
 */
HRESULT Util::HrStreamToString(IStream *sInput, std::string &strOutput) {
	object_ptr<ECMemStream> lpMemStream;
	ULONG ulRead = 0;
	char buffer[BUFSIZE];
	LARGE_INTEGER zero = {{0,0}};

	if (sInput->QueryInterface(IID_ECMemStream, &~lpMemStream) == hrSuccess) {
		// getsize, getbuffer, assign
		strOutput.append(lpMemStream->GetBuffer(), lpMemStream->GetSize());
		return hrSuccess;
	}
	// manual copy
	auto hr = sInput->Seek(zero, SEEK_SET, nullptr);
	if (hr != hrSuccess)
		return hr;
	while (1) {
		hr = sInput->Read(buffer, BUFSIZE, &ulRead);
		if (hr != hrSuccess || ulRead == 0)
			break;
		strOutput.append(buffer, ulRead);
	}
	return hr;
}

/** 
 * Append the full contents of a stream into a std::string. It might use
 * the ECMemStream interface if available, otherwise it will do a
 * normal stream copy. The position of the stream on return is not
 * stable.
 * 
 * @param[in] sInput The stream to copy data from
 * @param[in] strOutput The string to place data in
 * 
 * @return MAPI Error code
 */
HRESULT Util::HrStreamToString(IStream *sInput, std::wstring &strOutput) {
	object_ptr<ECMemStream> lpMemStream;
	ULONG ulRead = 0;
	char buffer[BUFSIZE];
	LARGE_INTEGER zero = {{0,0}};

	if (sInput->QueryInterface(IID_ECMemStream, &~lpMemStream) == hrSuccess) {
		// getsize, getbuffer, assign
		strOutput.append(reinterpret_cast<wchar_t *>(lpMemStream->GetBuffer()), lpMemStream->GetSize() / sizeof(WCHAR));
		return hrSuccess;
	}
	// manual copy
	auto hr = sInput->Seek(zero, SEEK_SET, nullptr);
	if (hr != hrSuccess)
		return hr;
	while (1) {
		hr = sInput->Read(buffer, BUFSIZE, &ulRead);
		if (hr != hrSuccess || ulRead == 0)
			break;
		strOutput.append(reinterpret_cast<wchar_t *>(buffer), ulRead / sizeof(WCHAR));
	}
	return hr;
}

/**
 * Convert a byte stream from ulCodepage to WCHAR.
 *
 * @param[in]	sInput		Input stream
 * @param[in]	ulCodepage	codepage of input stream
 * @param[out]	lppwOutput	output in WCHAR
 * @return MAPI error code
 */
HRESULT Util::HrConvertStreamToWString(IStream *sInput, ULONG ulCodepage, std::wstring *wstrOutput)
{
	const char *lpszCharset;
	convert_context converter;
	std::string data;
	HRESULT hr = HrGetCharsetByCP(ulCodepage, &lpszCharset);
	if (hr != hrSuccess) {
		lpszCharset = "us-ascii";
		hr = hrSuccess;
	}

	hr = HrStreamToString(sInput, data);
	if (hr != hrSuccess)
		return hr;

	try {
		wstrOutput->assign(converter.convert_to<std::wstring>(CHARSET_WCHAR"//IGNORE", data, rawsize(data), lpszCharset));
	} catch (const std::exception &) {
		return MAPI_E_INVALID_PARAMETER;
	}
	return hrSuccess;
}

/**
 * Converts HTML (PT_BINARY, with specified codepage) to plain text (PT_UNICODE)
 *
 * @param[in]	html	IStream to PR_HTML
 * @param[out]	text	IStream to PR_BODY_W
 * @param[in]	ulCodepage	codepage of html stream
 * @return		HRESULT	MAPI error code
 */
HRESULT Util::HrHtmlToText(IStream *html, IStream *text, ULONG ulCodepage)
{
	std::wstring wstrHTML;
	CHtmlToTextParser	parser;
	HRESULT hr = HrConvertStreamToWString(html, ulCodepage, &wstrHTML);
	if(hr != hrSuccess)
		return hr;
	if (!parser.Parse(string_strip_nuls(wstrHTML).c_str()))
		return MAPI_E_CORRUPT_DATA;

	std::wstring &strText = parser.GetText();
	return text->Write(strText.data(), (strText.size()+1)*sizeof(WCHAR), NULL);
}

template<size_t N> static bool StrCaseCompare(const wchar_t *lpString,
    const wchar_t (&lpFind)[N], size_t pos = 0)
{
	return wcsncasecmp(lpString + pos, lpFind, N - 1) == 0;
}

/**
 * This converts from HTML to RTF by doing to following:
 *
 * Always escape { and } to \{ and \}
 * Always escape \r\n to \par (dfq?)
 * All HTML tags are converted from, say <BODY onclick=bla> to \r\n{\htmltagX <BODY onclick=bla>}
 * Each tag with text content gets an extra {\htmltag64} to suppress generated <P>s in the final HTML output
 * Some tags output \htmlrtf \par \htmlrtf0 so that the plaintext version of the RTF has newlines in the right places
 * Some effort is done so that data between <STYLE> tags is output as a single entity
 * <!-- and --> tags are supported and output as a single htmltagX entity
 *
 * This gives an RTF stream that converts back to the original HTML when viewed in OL, but also preserves plaintext content
 * when all HTML content is removed.
 *
 * @param[in]	strHTML	HTML string in WCHAR for uniformity
 * @param[out]	strRTF	RTF output, containing unicode chars
 * @return mapi error code
 */
HRESULT Util::HrHtmlToRtf(const WCHAR *lpwHTML, std::string &strRTF)
{
	std::stack<unsigned int> stackTag;
	size_t pos = 0;
	int ulCommentMode = 0;		// 0=no comment, 1=just starting top-level comment, 2=inside comment level 1, 3=inside comment level 2, etc
	int tag = 0, type = 0, ulStyleMode = 0, ulParMode = 0;
	bool inTag = false, bFirstText = true, bPlainCRLF = false;

	if (lpwHTML == NULL)
		return MAPI_E_INVALID_PARAMETER;

	// @todo default codepage is set on windows-1252, but is this correct for non-western outlooks?
	strRTF = "{\\rtf1\\ansi\\ansicpg1252\\fromhtml1 \\deff0{\\fonttbl\r\n"
	        "{\\f0\\fswiss\\fcharset0 Arial;}\r\n"
            "{\\f1\\fmodern Courier New;}\r\n"
            "{\\f2\\fnil\\fcharset2 Symbol;}\r\n"
            "{\\f3\\fmodern\\fcharset0 Courier New;}\r\n"
            "{\\f4\\fswiss\\fcharset0 Arial;}\r\n"
            "{\\f5\\fswiss Tahoma;}\r\n"
            "{\\f6\\fswiss\\fcharset0 Times New Roman;}}\r\n"
            "{\\colortbl\\red0\\green0\\blue0;\\red0\\green0\\blue255;\\red0\\green0\\blue255;}\r\n"
            "\\uc1\\pard\\plain\\deftab360 \\f0\\fs24 ";
	// \\uc1 is important, for characters that are not supported by the reader of this rtf.

	stackTag.push(RTF_OUTHTML);
    	
	// We 'convert' from HTML to text by doing a rather simple stripping
	// of tags, and conversion of some strings
	while (lpwHTML[pos]) {
		type = RTF_TAG_TYPE_UNK;

		// Remember if this tag should output a CRLF
		if(lpwHTML[pos] == '<') {
            bPlainCRLF = false;
            		
			// Process important tags first
			if(StrCaseCompare(lpwHTML, L"<HTML", pos)) {
				type = RTF_TAG_TYPE_HTML;
			} else if(StrCaseCompare(lpwHTML, L"</HTML", pos)) {
				type = RTF_TAG_TYPE_HTML;
			} else if(StrCaseCompare(lpwHTML, L"<HEAD", pos)) {
				type = RTF_TAG_TYPE_HEAD;
			} else if(StrCaseCompare(lpwHTML, L"</HEAD", pos)) {
				type = RTF_TAG_TYPE_HEAD;
			} else if(StrCaseCompare(lpwHTML, L"<BODY", pos)) {
				type = RTF_TAG_TYPE_BODY;
			} else if(StrCaseCompare(lpwHTML, L"</BODY", pos)) {
				type = RTF_TAG_TYPE_BODY;
			} else if(StrCaseCompare(lpwHTML, L"<P", pos)) {
				type = RTF_TAG_TYPE_P;
				bPlainCRLF = true;
			} else if(StrCaseCompare(lpwHTML, L"</P", pos)) {
				type = RTF_TAG_TYPE_P;
				bPlainCRLF = true;
			} else if(StrCaseCompare(lpwHTML, L"<DIV", pos)) {
				type = RTF_TAG_TYPE_ENDP;
			} else if(StrCaseCompare(lpwHTML, L"</DIV", pos)) {
				type = RTF_TAG_TYPE_ENDP;
				bPlainCRLF = true;
			} else if(StrCaseCompare(lpwHTML, L"<SPAN", pos)) {
				type = RTF_TAG_TYPE_STARTP;
			} else if(StrCaseCompare(lpwHTML, L"</SPAN", pos)) {
				type = RTF_TAG_TYPE_STARTP;
			} else if(StrCaseCompare(lpwHTML, L"<A", pos)) {
				type = RTF_TAG_TYPE_STARTP;
			} else if(StrCaseCompare(lpwHTML, L"</A", pos)) {
				type = RTF_TAG_TYPE_STARTP;
			} else if(StrCaseCompare(lpwHTML, L"<BR", pos)) {
				type = RTF_TAG_TYPE_BR;
				bPlainCRLF = true;
			} else if(StrCaseCompare(lpwHTML, L"<PRE", pos)) {
				type = RTF_TAG_TYPE_PRE;
			} else if(StrCaseCompare(lpwHTML, L"</PRE", pos)) {
				type = RTF_TAG_TYPE_PRE;
			} else if(StrCaseCompare(lpwHTML, L"<FONT", pos)) {
				type = RTF_TAG_TYPE_FONT;
			} else if(StrCaseCompare(lpwHTML, L"</FONT", pos)) {
				type = RTF_TAG_TYPE_FONT;
			} else if(StrCaseCompare(lpwHTML, L"<META", pos)) {
				type = RTF_TAG_TYPE_HEADER;
			} else if(StrCaseCompare(lpwHTML, L"<LINK", pos)) {
				type = RTF_TAG_TYPE_HEADER;
			} else if (StrCaseCompare(lpwHTML, L"<H", pos) && iswdigit(lpwHTML[pos+2])) {
				type = RTF_TAG_TYPE_HEADER;
			} else if (StrCaseCompare(lpwHTML, L"</H", pos) && iswdigit(lpwHTML[pos+3])) {
				type = RTF_TAG_TYPE_HEADER;
			} else if(StrCaseCompare(lpwHTML, L"<TITLE", pos)) {
				type = RTF_TAG_TYPE_TITLE;
			} else if(StrCaseCompare(lpwHTML, L"</TITLE", pos)) {
				type = RTF_TAG_TYPE_TITLE;
			} else if(StrCaseCompare(lpwHTML, L"<PLAIN", pos)) {
				type = RTF_TAG_TYPE_FONT;
			} else if(StrCaseCompare(lpwHTML, L"</PLAIN", pos)) {
				type = RTF_TAG_TYPE_FONT;
			}
			if (StrCaseCompare(lpwHTML, L"</", pos))
				type |= RTF_FLAG_CLOSE;
		}

        // Set correct state flag if closing tag (RTF_IN*)
        if(type & RTF_FLAG_CLOSE) {
            switch(type & 0xF0) {
                case RTF_TAG_TYPE_HEAD:
                    if(!stackTag.empty() && stackTag.top() == RTF_INHEAD)
                        stackTag.pop();
                    break;
                case RTF_TAG_TYPE_BODY:
                    if(!stackTag.empty() && stackTag.top() == RTF_INBODY)
                        stackTag.pop();
                    break;
                case RTF_TAG_TYPE_HTML:
                    if(!stackTag.empty() && stackTag.top() == RTF_INHTML)
                        stackTag.pop();
                    break;
                default:
                    break;
            }
        }
        
        // Process special tag input
        if(lpwHTML[pos] == '<' && !inTag) {
            if(StrCaseCompare(lpwHTML, L"<!--", pos))
                ++ulCommentMode;
            
            if(ulCommentMode == 0) {
                if(StrCaseCompare(lpwHTML, L"<STYLE", pos))
                    ulStyleMode = 1;
                else if(StrCaseCompare(lpwHTML, L"</STYLE", pos)) {
                    if(ulStyleMode == 3) {
                        // Close the style content tag
                        strRTF += "}";
                    }
                    ulStyleMode = 0;
                } else if(StrCaseCompare(lpwHTML, L"<DIV", pos) || StrCaseCompare(lpwHTML, L"<P", pos)) {
                    ulParMode = 1;
                } else if(StrCaseCompare(lpwHTML, L"</DIV", pos) || StrCaseCompare(lpwHTML, L"</P", pos)) {
                    ulParMode = 0;
                }
            }
            
            if(ulCommentMode < 2 && ulStyleMode < 2) {
                strRTF += "\r\n{\\*\\htmltag" + stringify((ulParMode == 2 ? RTF_FLAG_INPAR : 0) | tag | type | stackTag.top()) + " ";
                inTag = true;
                bFirstText = true;
                
                if(ulCommentMode)
                    // Inside comment now
                    ++ulCommentMode;
            }
        }
        
        // Do actual output
        if(lpwHTML[pos] == '\r') {
            // Ingore \r
        } else if(lpwHTML[pos] == '\n') {
            if(inTag || ulCommentMode || ulStyleMode)
                strRTF += " ";
            else
                strRTF += "\r\n{\\*\\htmltag" + stringify((ulParMode == 2 ? RTF_FLAG_INPAR : 0) | stackTag.top()) + " \\par }";
        } else if(lpwHTML[pos] == '\t') {
            if(inTag || ulCommentMode || ulStyleMode)
                strRTF += "\\tab ";
            else
                strRTF += "\r\n{\\*\\htmltag" + stringify((ulParMode == 2 ? RTF_FLAG_INPAR : 0) | stackTag.top()) + " \\tab }";
        } else if(lpwHTML[pos] == '{') {
            strRTF += "\\{";
        } else if(lpwHTML[pos] == '}') {
            strRTF += "\\}";
        } else if(lpwHTML[pos] == '\\') {
            strRTF += "\\\\";
		} else if(lpwHTML[pos] > 127) {
			// Unicode character
			char hex[12];
			snprintf(hex, 12, "\\u%hd ?", (signed short)lpwHTML[pos]);
			strRTF += hex;
        } else if(StrCaseCompare(lpwHTML, L"&nbsp;", pos)) {
            if(inTag || ulCommentMode || ulStyleMode)
                strRTF += "&nbsp;";
            else
                strRTF += "\r\n{\\*\\htmltag64}{\\*\\htmltag" + stringify((ulParMode == 2 ? RTF_FLAG_INPAR : 0) | RTF_TAG_TYPE_STARTP | stackTag.top()) + " &nbsp;}";
                
            pos+=5;
		} else if(!inTag && !ulCommentMode && !ulStyleMode && lpwHTML[pos] == '&' && CHtmlEntity::validateHtmlEntity(std::wstring(lpwHTML + pos, 10)) ) {
			size_t semicolon = pos;
			while (lpwHTML[semicolon] && lpwHTML[semicolon] != ';')
				++semicolon;

            if (lpwHTML[semicolon]) {
                std::wstring strEntity;
				WCHAR c;
                std::string strChar;

				strEntity.assign(lpwHTML + pos+1, semicolon-pos-1);
				c = CHtmlEntity::HtmlEntityToChar(strEntity);

				if (c > 32 && c < 128)
					strChar = c;
				else {
					// Unicode character
					char hex[12];
					snprintf(hex, 12, "\\u%hd ?", (signed short)c); // unicode char + ascii representation (see \ucN rtf command)
					strChar = hex;
				}

				// both strChar and strEntity in output, unicode in rtf space, entity in html space
                strRTF += std::string("\\htmlrtf ") + strChar + "\\htmlrtf0{\\*\\htmltag" +
					stringify((ulParMode == 2 ? RTF_FLAG_INPAR : 0) | RTF_TAG_TYPE_STARTP | stackTag.top()) +
					"&" + convert_to<std::string>(strEntity) + ";}";
                pos += strEntity.size() + 2;
                continue;
            }
        } else {
            if (!inTag && bFirstText)
                bFirstText = false;
            strRTF += lpwHTML[pos];
        }

        // Do post-processing output
        if(lpwHTML[pos] == '>' && (inTag || ulCommentMode)) {
            if(!ulCommentMode && ulStyleMode < 2)
                strRTF += "}";
                
            if(pos > 2 && StrCaseCompare(lpwHTML, L"-->", pos-2) && ulCommentMode) {
                --ulCommentMode;
                
                if(ulCommentMode == 1) {
                    ulCommentMode = 0;
                    strRTF += "}";
                }
            }

            if(pos > 6 && StrCaseCompare(lpwHTML, L"/STYLE>", pos-6) && ulStyleMode) {
                ulStyleMode = 0;
                strRTF += "}";
            }

            if(ulStyleMode == 1)
                ++ulStyleMode;
                
            if(ulParMode == 1)
                ++ulParMode;
                
            if(ulStyleMode == 2) {
                // Output the style content as a tag
                ulStyleMode = 3;
                strRTF += "\r\n{\\*\\htmltag" + stringify(RTF_TAG_TYPE_UNK | stackTag.top()) + " ";
            } 
            
            if (!ulStyleMode && !ulCommentMode)
                // Normal text must have \*\htmltag64 to suppress <p> in the final html output
                strRTF += "{\\*\\htmltag64}";
            inTag = false;
            if (bPlainCRLF && !ulCommentMode && !ulStyleMode)
                // Add a plaintext newline if needed, but only for non-style and non-comment parts
                strRTF += "\\htmlrtf \\line \\htmlrtf0 ";
        }
        
        
        // Next char
        ++pos;

        // Set correct state flag (RTF_IN*)
        if(!(type & RTF_FLAG_CLOSE)) {
            switch(type & 0xF0) {
                case RTF_TAG_TYPE_HTML:
                    stackTag.push(RTF_INHTML);
                    break;
                case RTF_TAG_TYPE_BODY:
                    stackTag.push(RTF_INBODY);
                    break;
                case RTF_TAG_TYPE_HEAD:
                    stackTag.push(RTF_INHEAD);
                    break;
                default:
                    break;
            }
        }
	}
	
	strRTF +="}\r\n";
	return hrSuccess;
}

/**
 * Convert html stream to rtf stream, using a codepage for the html input.
 *
 * We convert the HTML Stream from the codepage to wstring, because of
 * the codepage of the html string, which can be any codepage.
 *
 * @param[in]	html	Stream to the HTML string, read only
 * @param[in]	rtf		Stream to the RTF string, write only
 * @param[in]	ulCodepage	codepage of the HTML input.
 * @return MAPI error code
 */
HRESULT	Util::HrHtmlToRtf(IStream *html, IStream *rtf, unsigned int ulCodepage)
{
	std::wstring wstrHTML;
	std::string strRTF;
	HRESULT hr = HrConvertStreamToWString(html, ulCodepage, &wstrHTML);
	if(hr != hrSuccess)
		return hr;
	hr = HrHtmlToRtf(wstrHTML.c_str(), strRTF);
	if(hr != hrSuccess)
		return hr;
	return rtf->Write(strRTF.c_str(), strRTF.size(), NULL);
}

/** 
 * Converts a string containing hexidecimal numbers into binary
 * data. And it adds a 0 at the end of the data.
 * 
 * @todo, check usage of this function to see if the terminating 0 is
 * really useful. should really be removed.
 *
 * @param[in] input string to convert
 * @param[in] len length of the input (must be a multiple of 2)
 * @param[out] outLength length of the output
 * @param[out] output binary version of the input
 * @param[in] parent optional pointer used for MAPIAllocateMore
 * 
 * @return MAPI Error code
 */
HRESULT Util::hex2bin(const char *input, size_t len, ULONG *outLength, LPBYTE *output, void *parent)
{
	HRESULT hr;
	LPBYTE buffer = NULL;

	if (len % 2 != 0)
		return MAPI_E_INVALID_PARAMETER;
	if (parent)
		hr = MAPIAllocateMore(len/2+1, parent, (void**)&buffer);
	else
		hr = MAPIAllocateBuffer(len/2+1, (void**)&buffer);
	if (hr != hrSuccess)
		return hr;
	hr = hex2bin(input, len, buffer);
	if(hr != hrSuccess)
		goto exit;
	buffer[len/2] = '\0';

	*outLength = len/2;
	*output = buffer;
 exit:
	if (hr != hrSuccess && parent == nullptr)
		MAPIFreeBuffer(buffer);
	return hr;
}

/** 
 * Converts a string containing hexidecimal numbers into binary
 * data.
 * 
 * @param[in] input string to convert
 * @param[in] len length of the input (must be a multiple of 2)
 * @param[out] output binary version of the input, must be able to receive len/2 bytes
 * 
 * @return MAPI Error code
 */
HRESULT Util::hex2bin(const char *input, size_t len, LPBYTE output)
{
	if (len % 2 != 0)
		return MAPI_E_INVALID_PARAMETER;

	for (unsigned int i = 0, j = 0; i < len; ++j) {
		output[j] = x2b(input[i++]) << 4;
		output[j] |= x2b(input[i++]);
	}
	return hrSuccess;
}

/** 
 * Return the original body property tag of a message, or PR_NULL when unknown.
 * 
 * @param[in]	lpBody			Pointer to the SPropValue containing the PR_BODY property.
 * @param[in]	lpHtml			Pointer to the SPropValue containing the PR_HTML property.
 * @param[in]	lpRtfCompressed	Pointer to the SPropValue containing the PR_RTF_COMPRESSED property.
 * @param[in]	lpRtfInSync		Pointer to the SPropValue containing the PR_RTF_IN_SYNC property.
 * @param[in]	ulFlags			If MAPI_UNICODE is specified, the PR_BODY proptag
 * 								will be the PT_UNICODE version. Otherwise the
 * 								PT_STRING8 version is returned.
 * 
 * @return 
 */
ULONG Util::GetBestBody(const SPropValue *lpBody, const SPropValue *lpHtml,
    const SPropValue *lpRtfCompressed, const SPropValue *lpRtfInSync,
    ULONG ulFlags)
{
	/**
	 * In this function we try to determine the best body based on the combination of values and error values
	 * for PR_BODY, PR_HTML, PR_RTF_COMPRESSED and PR_RTF_IN_SYNC according to the rules as described in ECMessage.cpp.
	 * Some checks performed here seem redundant, but are actualy required to determine if the source provider
	 * implements this scheme as we expect it (Scalix doesn't always seem to do so).
	 */
	const ULONG ulBodyTag = ((ulFlags & MAPI_UNICODE) ? PR_BODY_W : PR_BODY_A);

	if (lpRtfInSync->ulPropTag != PR_RTF_IN_SYNC)
		return PR_NULL;

	if ((lpBody->ulPropTag == ulBodyTag || (PROP_TYPE(lpBody->ulPropTag) == PT_ERROR && lpBody->Value.err == MAPI_E_NOT_ENOUGH_MEMORY)) &&
		(PROP_TYPE(lpHtml->ulPropTag) == PT_ERROR && lpHtml->Value.err == MAPI_E_NOT_FOUND) &&
		(PROP_TYPE(lpRtfCompressed->ulPropTag) == PT_ERROR && lpRtfCompressed->Value.err == MAPI_E_NOT_FOUND))
		return ulBodyTag;

	if ((lpHtml->ulPropTag == PR_HTML || (PROP_TYPE(lpHtml->ulPropTag) == PT_ERROR && lpHtml->Value.err == MAPI_E_NOT_ENOUGH_MEMORY)) &&
		(PROP_TYPE(lpBody->ulPropTag) == PT_ERROR && lpBody->Value.err == MAPI_E_NOT_ENOUGH_MEMORY) &&
		(PROP_TYPE(lpRtfCompressed->ulPropTag) == PT_ERROR && lpRtfCompressed->Value.err == MAPI_E_NOT_ENOUGH_MEMORY) &&
		lpRtfInSync->Value.b == FALSE)
		return PR_HTML;

	if ((lpRtfCompressed->ulPropTag == PR_RTF_COMPRESSED || (PROP_TYPE(lpRtfCompressed->ulPropTag) == PT_ERROR && lpRtfCompressed->Value.err == MAPI_E_NOT_ENOUGH_MEMORY)) &&
		(PROP_TYPE(lpBody->ulPropTag) == PT_ERROR && lpBody->Value.err == MAPI_E_NOT_ENOUGH_MEMORY) &&
		(PROP_TYPE(lpHtml->ulPropTag) == PT_ERROR && lpHtml->Value.err == MAPI_E_NOT_FOUND) &&
		lpRtfInSync->Value.b == TRUE)
		return PR_RTF_COMPRESSED;

	return PR_NULL;
}

/** 
 * Return the original body property tag of a message, or PR_NULL when unknown.
 * 
 * @param[in]	lpPropObj	The object to get the best body proptag from.
 * @param[in]	ulFlags		If MAPI_UNICODE is specified, the PR_BODY proptag
 * 							will be the PT_UNICODE version. Otherwise the
 * 							PT_STRING8 version is returned.
 * 
 * @return 
 */
ULONG Util::GetBestBody(IMAPIProp* lpPropObj, ULONG ulFlags)
{
	SPropArrayPtr ptrBodies;
	const ULONG ulBodyTag = ((ulFlags & MAPI_UNICODE) ? PR_BODY_W : PR_BODY_A);
	SizedSPropTagArray (4, sBodyTags) = { 4, {
			ulBodyTag,
			PR_HTML,
			PR_RTF_COMPRESSED,
			PR_RTF_IN_SYNC
		} };
	ULONG cValues = 0;
	auto hr = lpPropObj->GetProps(sBodyTags, 0, &cValues, &~ptrBodies);
	if (FAILED(hr))
		return PR_NULL;
	return GetBestBody(&ptrBodies[0], &ptrBodies[1], &ptrBodies[2], &ptrBodies[3], ulFlags);
}

/** 
 * Return the original body property tag of a message, or PR_NULL when unknown.
 * 
 * @param[in]	lpPropArray	The array of properties on which to base the result.
 *                          This array must include PR_BODY, PR_HTML, PR_RTF_COMPRESSED
 *							and PR_RTF_IN_SYNC.
 * @param[in]	cValues		The number of properties in lpPropArray.
 * @param[in]	ulFlags		If MAPI_UNICODE is specified, the PR_BODY proptag
 * 							will be the PT_UNICODE version. Otherwise the
 * 							PT_STRING8 version is returned.
 * 
 * @return 
 */
ULONG Util::GetBestBody(LPSPropValue lpPropArray, ULONG cValues, ULONG ulFlags)
{
	auto lpBody = PCpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_BODY, PT_UNSPECIFIED));
	if (!lpBody)
		return PR_NULL;
	auto lpHtml = PCpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_HTML, PT_UNSPECIFIED));
	if (!lpHtml)
		return PR_NULL;
	auto lpRtfCompressed = PCpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_RTF_COMPRESSED, PT_UNSPECIFIED));
	if (!lpRtfCompressed)
		return PR_NULL;
	auto lpRtfInSync = PCpropFindProp(lpPropArray, cValues, CHANGE_PROP_TYPE(PR_RTF_IN_SYNC, PT_UNSPECIFIED));
	if (!lpRtfInSync)
		return PR_NULL;
	return GetBestBody(lpBody, lpHtml, lpRtfCompressed, lpRtfInSync, ulFlags);
}

/**
 * Check if a proptag specifies a body property. This is PR_BODY, PR_HTML
 * or PR_RTF_COMPRESSED. If the type is set to error, it can still classified
 * as a body proptag.
 *
 * @param[in]	ulPropTag	The proptag to check,
 * @retval true if the proptag specified a body property.
 * @retval false otherwise.
 */
bool Util::IsBodyProp(ULONG ulPropTag)
{
	switch (PROP_ID(ulPropTag)) {
	case PROP_ID(PR_BODY):
	case PROP_ID(PR_HTML):
	case PROP_ID(PR_RTF_COMPRESSED):
		return true;
	default:
		return false;
	}
}

/** 
 * Find an interface IID in an array of interface definitions.
 * 
 * @param[in] lpIID interface to find
 * @param[in] ulIIDs number of entries in lpIIDs
 * @param[in] lpIIDs array of interfaces
 * 
 * @return MAPI error code
 * @retval MAPI_E_NOT_FOUND interface not found in array
 */
static HRESULT FindInterface(LPCIID lpIID, ULONG ulIIDs, LPCIID lpIIDs)
{
	if (!lpIIDs || !lpIID)
		return MAPI_E_NOT_FOUND;
	for (unsigned int i = 0; i < ulIIDs; ++i)
		if (*lpIID == lpIIDs[i])
			return hrSuccess;
	return MAPI_E_NOT_FOUND;
}

/** 
 * Copy a complete stream to another.
 * 
 * @param[in] lpSrc Input stream to copy
 * @param[in] lpDest Stream to append data of lpSrc to
 * 
 * @return MAPI error code
 */
static HRESULT CopyStream(IStream *lpSrc, IStream *lpDest)
{
	ULARGE_INTEGER liRead = {{0}}, liWritten = {{0}};
	STATSTG stStatus;
	HRESULT hr = lpSrc->Stat(&stStatus, 0);
	if (FAILED(hr))
		return hr;
	hr = lpSrc->CopyTo(lpDest, stStatus.cbSize, &liRead, &liWritten);
	if (FAILED(hr))
		return hr;
	if (liRead.QuadPart != liWritten.QuadPart)
		return MAPI_W_PARTIAL_COMPLETION;
	return lpDest->Commit(0);
}

/** 
 * Copy all recipients from a source message to another message.
 * 
 * @param[in] lpSrc Message containing recipients to copy
 * @param[out] lpDest Message to add (append) all recipients to
 * 
 * @return MAPI error code
 */
static HRESULT CopyRecipients(IMessage *lpSrc, IMessage *lpDest)
{
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpRows;
	memory_ptr<SPropTagArray> lpTableColumns;
	ULONG ulRows = 0;

	auto hr = lpSrc->GetRecipientTable(MAPI_UNICODE, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->QueryColumns(TBL_ALL_COLUMNS, &~lpTableColumns);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SetColumns(lpTableColumns, 0);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->GetRowCount(0, &ulRows);
	if (hr != hrSuccess)
		return hr;
	if (ulRows == 0)	// Nothing to do!
		return hrSuccess;
	hr = lpTable->QueryRows(ulRows, 0, &~lpRows);
	if (hr != hrSuccess)
		return hr;
	// LPADRLIST and LPSRowSet are binary compatible \o/
	return lpDest->ModifyRecipients(MODRECIP_ADD,
	       reinterpret_cast<ADRLIST *>(lpRows.get()));
}

/** 
 * Copy a single-instance id to another object, if possible.
 * 
 * @param lpSrc Source object (message or attachment)
 * @param lpDst Destination object to have the same contents as source
 * 
 * @return always hrSuccess
 */
static HRESULT CopyInstanceIds(IMAPIProp *lpSrc, IMAPIProp *lpDst)
{
	object_ptr<IECSingleInstance> lpSrcInstance, lpDstInstance;
	ULONG cbInstanceID = 0;
	memory_ptr<ENTRYID> lpInstanceID;

	/* 
	 * We are always going to return hrSuccess, if for some reason we can't copy the single instance,
	 * we always have the real data as fallback.
	 */
	if (lpSrc->QueryInterface(IID_IECSingleInstance, &~lpSrcInstance) != hrSuccess)
		return hrSuccess;
	if (lpDst->QueryInterface(IID_IECSingleInstance, &~lpDstInstance) != hrSuccess)
		return hrSuccess;

	/*
	 * Transfer instance Id, if this succeeds we're in luck and we might not
	 * have to send the attachment to the server. Note that while SetSingleInstanceId()
	 * might succeed now, the attachment might be deleted on the server between
	 * SetSingleInstanceId() and SaveChanges(). In that case SaveChanges will fail
	 * and we will have to resend the attachment data.
	 */
	if (lpSrcInstance->GetSingleInstanceId(&cbInstanceID, &~lpInstanceID) != hrSuccess)
		return hrSuccess;
	if (lpDstInstance->SetSingleInstanceId(cbInstanceID, lpInstanceID) != hrSuccess)
		return hrSuccess;
	return hrSuccess;
}

/** 
 * Copy all attachment properties from one attachment to another. The
 * exclude property tag array is optional.
 * 
 * @param[in] lpSrcAttach Attachment to copy data from
 * @param[out] lpDstAttach Attachment to copy data to
 * @param[in] lpExcludeProps Optional list of properties to not copy
 * 
 * @return MAPI error code
 */
static HRESULT CopyAttachmentProps(IAttach *lpSrcAttach, IAttach *lpDstAttach,
    SPropTagArray *lpExcludeProps = nullptr)
{
	return Util::DoCopyTo(&IID_IAttachment, lpSrcAttach, 0, NULL,
	       lpExcludeProps, 0, NULL, &IID_IAttachment, lpDstAttach, 0, NULL);
}

/** 
 * Copy all attachments from one message to another.
 * 
 * @param[in] lpSrc Source message to copy from
 * @param[in] lpDest Message to copy attachments to
 * @param[in] lpRestriction Optional restriction to apply before copying
 *                          the attachments.
 * 
 * @return MAPI error code
 */
HRESULT Util::CopyAttachments(LPMESSAGE lpSrc, LPMESSAGE lpDest, LPSRestriction lpRestriction) {
	bool bPartial = false;

	// table
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpRows;
	memory_ptr<SPropTagArray> lpTableColumns;
	ULONG ulRows = 0;

	// attachments
	memory_ptr<SPropValue> lpHasAttach;
	ULONG ulAttachNr = 0;

	auto hr = HrGetOneProp(lpSrc, PR_HASATTACH, &~lpHasAttach);
	if (hr != hrSuccess)
		return hrSuccess;
	if (lpHasAttach->Value.b == FALSE)
		return hrSuccess;
	hr = lpSrc->GetAttachmentTable(MAPI_UNICODE, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->QueryColumns(TBL_ALL_COLUMNS, &~lpTableColumns);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SetColumns(lpTableColumns, 0);
	if (hr != hrSuccess)
		return hr;
	if (lpRestriction) {
		hr = lpTable->Restrict(lpRestriction, 0);
		if (hr != hrSuccess)
			return hr;
	}

	hr = lpTable->GetRowCount(0, &ulRows);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->QueryRows(ulRows, 0, &~lpRows);
	if (hr != hrSuccess)
		return hr;

	for (ULONG i = 0; i < lpRows->cRows; ++i) {
		object_ptr<IAttach> lpDestAttach, lpSrcAttach;
		auto lpAttachNum = lpRows[i].cfind(PR_ATTACH_NUM);
		if (!lpAttachNum) {
			bPartial = true;
			continue;
		}
		hr = lpSrc->OpenAttach(lpAttachNum->Value.ul, NULL, 0, &~lpSrcAttach);
		if (hr != hrSuccess) {
			bPartial = true;
			continue;
		}
		hr = lpDest->CreateAttach(NULL, 0, &ulAttachNr, &~lpDestAttach);
		if (hr != hrSuccess) {
			bPartial = true;
			continue;
		}

		hr = CopyAttachmentProps(lpSrcAttach, lpDestAttach);
		if (hr != hrSuccess) {
			bPartial = true;
			continue;
		}

		/*
		 * Try making a single instance copy (without sending the attachment data to server).
		 * No error checking, we do not care if this fails, we still have all the data.
		 */
		CopyInstanceIds(lpSrcAttach, lpDestAttach);

		hr = lpDestAttach->SaveChanges(0);
		if (hr != hrSuccess)
			return hr;
	}

	if (bPartial)
		hr = MAPI_W_PARTIAL_COMPLETION;
	return hr;
}

/**
 * Copies all folders and contents from lpSrc to lpDest folder.
 *
 * Recursively copies contents from one folder to another. Location of
 * lpSrc and lpDest does not matter, as long as there is read rights
 * in lpSrc and write rights in lpDest.
 *
 * @param[in]	lpSrc	Source folder to copy
 * @param[in]	lpDest	Source folder to copy
 * @param[in]	ulFlags	See ISupport::DoCopyTo for valid flags
 * @param[in]	ulUIParam	Unused in Linux.
 * @param[in]	lpProgress	IMAPIProgress object. Unused in Linux.
 *
 * @return MAPI error code.
 */
static HRESULT CopyHierarchy(IMAPIFolder *lpSrc, IMAPIFolder *lpDest,
    ULONG ulFlags, ULONG ulUIParam, IMAPIProgress *lpProgress)
{
	bool bPartial = false;
	object_ptr<IMAPITable> lpTable;
	static constexpr const SizedSPropTagArray(2, sptaName) =
		{2, {PR_DISPLAY_NAME_W, PR_ENTRYID}};
	object_ptr<IMAPIFolder> lpSrcParam, lpDestParam;
	ULONG ulObj;

	// sanity checks
	if (lpSrc == nullptr || lpDest == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpSrc->QueryInterface(IID_IMAPIFolder, &~lpSrcParam);
	if (hr != hrSuccess)
		return hr;
	hr = lpDest->QueryInterface(IID_IMAPIFolder, &~lpDestParam);
	if (hr != hrSuccess)
		return hr;
	hr = lpSrc->GetHierarchyTable(MAPI_UNICODE, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SetColumns(sptaName, 0);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		object_ptr<IMAPIFolder> lpSrcFolder, lpDestFolder;
		rowset_ptr lpRowSet;

		hr = lpTable->QueryRows(1, 0, &~lpRowSet);
		if (hr != hrSuccess)
			return hr;
		if (lpRowSet->cRows == 0)
			break;
		hr = lpSrc->OpenEntry(lpRowSet[0].lpProps[1].Value.bin.cb, reinterpret_cast<ENTRYID *>(lpRowSet[0].lpProps[1].Value.bin.lpb), &IID_IMAPIFolder, 0, &ulObj, &~lpSrcFolder);
		if (hr != hrSuccess) {
			bPartial = true;
			continue;
		}
		hr = lpDest->CreateFolder(FOLDER_GENERIC, reinterpret_cast<const TCHAR *>(lpRowSet[0].lpProps[0].Value.lpszW), nullptr, &IID_IMAPIFolder,
		     MAPI_UNICODE | (ulFlags & MAPI_NOREPLACE ? 0 : OPEN_IF_EXISTS), &~lpDestFolder);
		if (hr != hrSuccess) {
			bPartial = true;
			continue;
		}

		hr = Util::DoCopyTo(&IID_IMAPIFolder, lpSrcFolder, 0, NULL, NULL, ulUIParam, lpProgress, &IID_IMAPIFolder, lpDestFolder, ulFlags, NULL);
		if (FAILED(hr))
			return hr;
		else if (hr != hrSuccess) {
			bPartial = true;
			continue;
		}

		if (ulFlags & MAPI_MOVE)
			lpSrc->DeleteFolder(lpRowSet[0].lpProps[1].Value.bin.cb, reinterpret_cast<const ENTRYID *>(lpRowSet[0].lpProps[1].Value.bin.lpb), 0, nullptr, 0);
	}

	if (bPartial)
		hr = MAPI_W_PARTIAL_COMPLETION;
	return hr;
}

/** 
 * Copy all messages from a folder to another.
 * 
 * @param[in] ulWhat 0 for normal messages, MAPI_ASSOCIATED for associated messages
 * @param[in] lpSrc The source folder to copy messages from
 * @param[out] lpDest The destination folder to copy messages in
 * @param[in] ulFlags CopyTo flags, like MAPI_MOVE, or 0
 * @param[in] ulUIParam Unused parameter passed to CopyTo functions
 * @param[in] lpProgress Unused progress object
 * 
 * @return MAPI error code
 */
#define MAX_ROWS 50
static HRESULT CopyContents(ULONG ulWhat, IMAPIFolder *lpSrc,
    IMAPIFolder *lpDest, ULONG ulFlags, ULONG ulUIParam,
    IMAPIProgress *lpProgress)
{
	bool bPartial = false;
	object_ptr<IMAPITable> lpTable;
	static constexpr const SizedSPropTagArray(1, sptaEntryID) = {1, {PR_ENTRYID}};
	ULONG ulObj;
	memory_ptr<ENTRYLIST> lpDeleteEntries;

	auto hr = lpSrc->GetContentsTable(MAPI_UNICODE | ulWhat, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SetColumns(sptaEntryID, 0);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~lpDeleteEntries);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(sizeof(SBinary)*MAX_ROWS, lpDeleteEntries, (void**)&lpDeleteEntries->lpbin);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		rowset_ptr lpRowSet;
		hr = lpTable->QueryRows(MAX_ROWS, 0, &~lpRowSet);
		if (hr != hrSuccess)
			return hr;
		if (lpRowSet->cRows == 0)
			break;

		lpDeleteEntries->cValues = 0;

		for (ULONG i = 0; i < lpRowSet->cRows; ++i) {
			object_ptr<IMessage> lpSrcMessage, lpDestMessage;

			hr = lpSrc->OpenEntry(lpRowSet[i].lpProps[0].Value.bin.cb, reinterpret_cast<const ENTRYID *>(lpRowSet[i].lpProps[0].Value.bin.lpb), &IID_IMessage, 0, &ulObj, &~lpSrcMessage);
			if (hr != hrSuccess) {
				bPartial = true;
				continue;
			}
			hr = lpDest->CreateMessage(&IID_IMessage, ulWhat | MAPI_MODIFY, &~lpDestMessage);
			if (hr != hrSuccess) {
				bPartial = true;
				continue;
			}

			hr = Util::DoCopyTo(&IID_IMessage, lpSrcMessage, 0, NULL, NULL, ulUIParam, lpProgress, &IID_IMessage, lpDestMessage, ulFlags, NULL);
			if (FAILED(hr))
				return hr;
			else if (hr != hrSuccess) {
				bPartial = true;
				continue;
			}

			hr = lpDestMessage->SaveChanges(0);
			if (hr != hrSuccess)
				bPartial = true;
			else if (ulFlags & MAPI_MOVE)
				lpDeleteEntries->lpbin[lpDeleteEntries->cValues++] = lpRowSet[i].lpProps[0].Value.bin;
		}
		if (ulFlags & MAPI_MOVE && lpDeleteEntries->cValues > 0 &&
		    lpSrc->DeleteMessages(lpDeleteEntries, 0, NULL, 0) != hrSuccess)
			bPartial = true;
	}

	if (bPartial)
		hr = MAPI_W_PARTIAL_COMPLETION;
	return hr;
}

/** 
 * Call OpenProperty on a property of an object to get the streamed
 * version of that property. Will try to open with STGM_TRANSACTED,
 * and disable this flag if an error was received.
 * 
 * @param[in] ulPropType The type of the property to open.
 * @param ulSrcPropTag The source property tag to open on the source object
 * @param lpPropSrc The source object containing the property to open
 * @param ulDestPropTag The destination property tag to open on the destination object
 * @param lpPropDest The destination object where the property should be copied to
 * @param lppSrcStream The source property as stream
 * @param lppDestStream The destination property as stream
 * 
 * @return MAPI error code
 */
static HRESULT TryOpenProperty(ULONG ulPropType, ULONG ulSrcPropTag,
    IMAPIProp *lpPropSrc, ULONG ulDestPropTag, IMAPIProp *lpPropDest,
    IStream **lppSrcStream, IStream **lppDestStream)
{
	object_ptr<IStream> lpSrc, lpDest;
	auto hr = lpPropSrc->OpenProperty(CHANGE_PROP_TYPE(ulSrcPropTag, ulPropType), &IID_IStream, 0, 0, &~lpSrc);
	if (hr != hrSuccess)
		return hr;

	// some mapi functions/providers don't implement STGM_TRANSACTED, retry again without this flag
	hr = lpPropDest->OpenProperty(CHANGE_PROP_TYPE(ulDestPropTag, ulPropType), &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~lpDest);
	if (hr != hrSuccess)
		hr = lpPropDest->OpenProperty(CHANGE_PROP_TYPE(ulDestPropTag, ulPropType), &IID_IStream, STGM_WRITE, MAPI_CREATE | MAPI_MODIFY, &~lpDest);
	if (hr != hrSuccess)
		return hr;
	*lppSrcStream = lpSrc.release();
	*lppDestStream = lpDest.release();
	return hrSuccess;
}

/** 
 * Adds a SPropProblem structure to an SPropProblemArray. If the
 * problem array already contains data, it will first be copied to a
 * new array, and one problem will be appended.
 * 
 * @param[in] lpProblem The new problem to add to the array
 * @param[in,out] lppProblems *lppProblems is NULL for a new array, otherwise a copy plus the addition is returned
 * 
 * @return MAPI error code
 */
static HRESULT AddProblemToArray(const SPropProblem *lpProblem,
    SPropProblemArray **lppProblems)
{
	SPropProblemArray *lpNewProblems = nullptr, *lpOrigProblems = *lppProblems;

	if (!lpOrigProblems) {
		auto hr = MAPIAllocateBuffer(CbNewSPropProblemArray(1), reinterpret_cast<void **>(&lpNewProblems));
		if (hr != hrSuccess)
			return hr;
		lpNewProblems->cProblem = 1;
	} else {
		auto hr = MAPIAllocateBuffer(CbNewSPropProblemArray(lpOrigProblems->cProblem + 1), reinterpret_cast<void **>(&lpNewProblems));
		if (hr != hrSuccess)
			return hr;
		lpNewProblems->cProblem = lpOrigProblems->cProblem +1;
		memcpy(lpNewProblems->aProblem, lpOrigProblems->aProblem, sizeof(SPropProblem) * lpOrigProblems->cProblem);
		MAPIFreeBuffer(lpOrigProblems);
	}

	memcpy(&lpNewProblems->aProblem[lpNewProblems->cProblem -1], lpProblem, sizeof(SPropProblem));

	*lppProblems = lpNewProblems;
	return hrSuccess;
}

HRESULT qi_void_to_imapiprop(void *p, const IID &iid, IMAPIProp **pptr)
{
#define R(Tp) do { return static_cast<Tp *>(p)->QueryInterface(IID_IMAPIProp, reinterpret_cast<void **>(pptr)); } while (false)
	if (iid == IID_IMAPIProp)		R(IMAPIProp);
	else if (iid == IID_IMailUser)		R(IMailUser);
	else if (iid == IID_IMAPIContainer)	R(IMAPIContainer);
	else if (iid == IID_IMAPIFolder)	R(IMAPIFolder);
	else if (iid == IID_IABContainer)	R(IABContainer);
	else if (iid == IID_IDistList)		R(IDistList);
	else if (iid == IID_IAddrBook)		R(IAddrBook);
	else if (iid == IID_IAttachment)	R(IAttach);
	else if (iid == IID_IMessage)		R(IMessage);
	else if (iid == IID_IMsgStore)		R(IMsgStore);
	else if (iid == IID_IProfSect)		R(IProfSect);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
#undef R
}

/** 
 * Copies a MAPI object in-memory to a new MAPI object. Only
 * IID_IStream or IID_IMAPIProp compatible interfaces can be copied.
 * 
 * @param[in] lpSrcInterface The expected interface of lpSrcObj. Cannot be NULL.
 * @param[in] lpSrcObj The source object to copy. Cannot be NULL.
 * @param[in] ciidExclude Number of interfaces in rgiidExclude
 * @param[in] rgiidExclude NULL or Interfaces to exclude in the copy, will return MAPI_E_INTERFACE_NOT_SUPPORTED if requested interface is found in the exclude list.
 * @param[in] lpExcludeProps NULL or Array of properties to exclude in the copy process
 * @param[in] ulUIParam Parameter for the callback in lpProgress, unused
 * @param[in] lpProgress Unused progress object
 * @param[in] lpDestInterface The expected interface of lpDstObj. Cannot be NULL.
 * @param[out] lpDestObj The existing destination object. Cannot be NULL.
 * @param[in] ulFlags can contain CopyTo flags, like MAPI_MOVE or MAPI_NOREPLACE
 * @param[in] lppProblems Optional array containing problems encountered during the copy.
 * 
 * @return MAPI error code
 */
HRESULT Util::DoCopyTo(LPCIID lpSrcInterface, LPVOID lpSrcObj,
    ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpDestInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	bool bPartial = false;
	// Properties that can never be copied (if you do this wrong, copying a message to a PST will fail)
	SizedSPropTagArray(23, sExtraExcludes) = { 19, { PR_STORE_ENTRYID, PR_STORE_RECORD_KEY, PR_STORE_SUPPORT_MASK, PR_MAPPING_SIGNATURE,
													 PR_MDB_PROVIDER, PR_ACCESS_LEVEL, PR_RECORD_KEY, PR_HASATTACH, PR_NORMALIZED_SUBJECT,
													 PR_MESSAGE_SIZE, PR_DISPLAY_TO, PR_DISPLAY_CC, PR_DISPLAY_BCC, PR_ACCESS, PR_SUBJECT_PREFIX,
													 PR_OBJECT_TYPE, PR_ENTRYID, PR_PARENT_ENTRYID, PR_INTERNET_CONTENT,
													 PR_NULL, PR_NULL, PR_NULL, PR_NULL }};

	object_ptr<IMAPIProp> lpPropSrc, lpPropDest;
	memory_ptr<SPropTagArray> lpSPropTagArray;

	if (lpSrcInterface == nullptr || lpSrcObj == nullptr ||
	    lpDestInterface == nullptr || lpDestObj == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// source is "usually" the same as dest .. so we don't check (as ms mapi doesn't either)
	auto hr = FindInterface(lpSrcInterface, ciidExclude, rgiidExclude);
	if (hr == hrSuccess)
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	hr = FindInterface(lpDestInterface, ciidExclude, rgiidExclude);
	if (hr == hrSuccess)
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	// first test IID_IStream .. the rest is IID_IMAPIProp compatible
	if (*lpSrcInterface == IID_IStream) {
		hr = FindInterface(&IID_IStream, ciidExclude, rgiidExclude);
		if (hr == hrSuccess)
			return MAPI_E_INTERFACE_NOT_SUPPORTED;
		if (*lpDestInterface != IID_IStream)
			return MAPI_E_INTERFACE_NOT_SUPPORTED;
		return CopyStream((LPSTREAM)lpSrcObj, (LPSTREAM)lpDestObj);
	}

	hr = FindInterface(&IID_IMAPIProp, ciidExclude, rgiidExclude);
	if (hr == hrSuccess)
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	// end sanity checks

	// check message, folder, attach, recipients, stream, mapitable, ... ?

	if (*lpSrcInterface == IID_IMAPIFolder) {
		// MS MAPI does not perform this check
		if (*lpDestInterface != IID_IMAPIFolder)
			// on store, create folder and still go ?
			return MAPI_E_INTERFACE_NOT_SUPPORTED;

		if (!lpExcludeProps || Util::FindPropInArray(lpExcludeProps, PR_CONTAINER_CONTENTS) == -1) {
			sExtraExcludes.aulPropTag[sExtraExcludes.cValues++] = PR_CONTAINER_CONTENTS;
			hr = CopyContents(0, (LPMAPIFOLDER)lpSrcObj, (LPMAPIFOLDER)lpDestObj, ulFlags, ulUIParam, lpProgress);
			if (hr != hrSuccess)
				bPartial = true;
		}

		if (!lpExcludeProps || Util::FindPropInArray(lpExcludeProps, PR_FOLDER_ASSOCIATED_CONTENTS) == -1) {
			sExtraExcludes.aulPropTag[sExtraExcludes.cValues++] = PR_FOLDER_ASSOCIATED_CONTENTS;
			hr = CopyContents(MAPI_ASSOCIATED, (LPMAPIFOLDER)lpSrcObj, (LPMAPIFOLDER)lpDestObj, ulFlags, ulUIParam, lpProgress);
			if (hr != hrSuccess)
				bPartial = true;
		}

		if (!lpExcludeProps || Util::FindPropInArray(lpExcludeProps, PR_CONTAINER_HIERARCHY) == -1) {
			// add to lpExcludeProps so CopyProps ignores them
			sExtraExcludes.aulPropTag[sExtraExcludes.cValues++] = PR_CONTAINER_HIERARCHY;
			hr = CopyHierarchy((LPMAPIFOLDER)lpSrcObj, (LPMAPIFOLDER)lpDestObj, ulFlags, ulUIParam, lpProgress);
			if (hr != hrSuccess)
				bPartial = true;
		}
	} else if (*lpSrcInterface == IID_IMessage) {
		// recipients & attachments
		// this is done in CopyProps ()
	} else if (*lpSrcInterface == IID_IAttachment) {
		// data stream
		// this is done in CopyProps ()
	} else if (*lpSrcInterface == IID_IMAPIContainer || *lpSrcInterface == IID_IMAPIProp) {
		// props only
		// this is done in CopyProps ()
	} else if (*lpSrcInterface == IID_IMailUser || *lpSrcInterface == IID_IDistList) {
		// in one if() ?
		// this is done in CopyProps () ???
		// what else besides props ???
	} else {
		// stores, ... ?
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	}

	// we have a IMAPIProp compatible interface here, and we don't want to crash
	hr = qi_void_to_imapiprop(lpSrcObj, *lpSrcInterface, &~lpPropSrc);
	if (hr != hrSuccess)
		goto exit;
	hr = qi_void_to_imapiprop(lpDestObj, *lpDestInterface, &~lpPropDest);
	if (hr != hrSuccess)
		goto exit;
	if (!FHasHTML(lpPropDest))
		sExtraExcludes.aulPropTag[sExtraExcludes.cValues++] = PR_HTML;
	hr = lpPropSrc->GetPropList(MAPI_UNICODE, &~lpSPropTagArray);
	if (FAILED(hr))
		goto exit;

	// filter excludes
	if (lpExcludeProps || sExtraExcludes.cValues != 0) {
		for (ULONG i = 0; i < lpSPropTagArray->cValues; ++i) {
			if (lpExcludeProps && Util::FindPropInArray(lpExcludeProps, CHANGE_PROP_TYPE(lpSPropTagArray->aulPropTag[i], PT_UNSPECIFIED)) != -1)
				lpSPropTagArray->aulPropTag[i] = PR_NULL;
			else if (Util::FindPropInArray(sExtraExcludes, CHANGE_PROP_TYPE(lpSPropTagArray->aulPropTag[i], PT_UNSPECIFIED)) != -1)
				lpSPropTagArray->aulPropTag[i] = PR_NULL;
		}
	}
	
	// Force some extra properties
	if (*lpSrcInterface == IID_IMessage) {
		bool bAddAttach = false;
		bool bAddRecip = false;

		if (Util::FindPropInArray(lpExcludeProps, PR_MESSAGE_ATTACHMENTS) == -1 &&	// not in exclude
			Util::FindPropInArray(lpSPropTagArray, PR_MESSAGE_ATTACHMENTS) == -1)		// not yet in props to copy
			bAddAttach = true;

		if (Util::FindPropInArray(lpExcludeProps, PR_MESSAGE_RECIPIENTS) == -1 &&		// not in exclude
			Util::FindPropInArray(lpSPropTagArray, PR_MESSAGE_RECIPIENTS) == -1)		// not yet in props to copy
			bAddRecip = true;

		if (bAddAttach || bAddRecip) {
			memory_ptr<SPropTagArray> lpTempSPropTagArray;
			ULONG ulNewPropCount = lpSPropTagArray->cValues + (bAddAttach ? (bAddRecip ? 2 : 1) : 1);
			
			hr = MAPIAllocateBuffer(CbNewSPropTagArray(ulNewPropCount), &~lpTempSPropTagArray);
			if (hr != hrSuccess)
				goto exit;

			memcpy(lpTempSPropTagArray->aulPropTag, lpSPropTagArray->aulPropTag, lpSPropTagArray->cValues * sizeof *lpSPropTagArray->aulPropTag);

			if (bAddAttach)
				lpTempSPropTagArray->aulPropTag[ulNewPropCount - (bAddRecip ? 2 : 1)] = PR_MESSAGE_ATTACHMENTS;
			if (bAddRecip)
				lpTempSPropTagArray->aulPropTag[ulNewPropCount - 1] = PR_MESSAGE_RECIPIENTS;
			lpTempSPropTagArray->cValues = ulNewPropCount;

			std::swap(lpTempSPropTagArray, lpSPropTagArray);
		}
	}

	// this is input for CopyProps
	hr = Util::DoCopyProps(lpSrcInterface, lpSrcObj, lpSPropTagArray, ulUIParam, lpProgress, lpDestInterface, lpDestObj, 0, lppProblems);
	if (hr != hrSuccess)
		goto exit;

	// TODO: mapi move, delete OPEN message ???

exit:
	// Partial warning when data was copied.
	if (bPartial)
		hr = MAPI_W_PARTIAL_COMPLETION;
	return hr;
}

/** 
 * Copy properties of one MAPI object to another. Only IID_IMAPIProp
 * compatible objects are supported.
 * 
 * @param[in] lpSrcInterface The expected interface of lpSrcObj. Cannot be NULL.
 * @param[in] lpSrcObj The source object to copy. Cannot be NULL.
 * @param[in] lpIncludeProps List of properties to copy, or NULL for all properties.
 * @param[in] ulUIParam Parameter for the callback in lpProgress, unused
 * @param[in] lpProgress Unused progress object
 * @param[in] lpDestInterface The expected interface of lpDstObj. Cannot be NULL.
 * @param[out] lpDestObj The existing destination object. Cannot be NULL.
 * @param[in] ulFlags can contain CopyTo flags, like MAPI_MOVE or MAPI_NOREPLACE
 * @param[in] lppProblems Optional array containing problems encountered during the copy.
 * 
 * @return MAPI error code
 */
HRESULT Util::DoCopyProps(LPCIID lpSrcInterface, void *lpSrcObj,
    const SPropTagArray *inclprop, ULONG ulUIParam, LPMAPIPROGRESS lpProgress,
    LPCIID lpDestInterface, void *lpDestObj, ULONG ulFlags,
    SPropProblemArray **lppProblems)
{
	object_ptr<IUnknown> lpKopano;
	memory_ptr<SPropValue> lpZObj, lpProps;
	bool bPartial = false;

	object_ptr<IMAPIProp> lpSrcProp, lpDestProp;
	ULONG cValues = 0;
	memory_ptr<SPropTagArray> lpsDestPropArray;
	memory_ptr<SPropProblemArray> lpProblems;

	// named props
	ULONG cNames = 0;
	memory_ptr<SPropTagArray> lpIncludeProps, lpsSrcNameTagArray;
	memory_ptr<SPropTagArray> lpsDestNameTagArray, lpsDestTagArray;
	memory_ptr<MAPINAMEID *> lppNames;

	// attachments
	memory_ptr<SPropValue> lpAttachMethod;
	LONG ulIdCPID;
	ULONG ulBodyProp = PR_BODY;

	if (lpSrcInterface == nullptr || lpDestInterface == nullptr ||
	    lpSrcObj == nullptr || lpDestObj == nullptr ||
	    inclprop == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	auto hr = qi_void_to_imapiprop(lpSrcObj, *lpSrcInterface, &~lpSrcProp);
	if (hr != hrSuccess)
		return hr;
	hr = qi_void_to_imapiprop(lpDestObj, *lpDestInterface, &~lpDestProp);
	if (hr != hrSuccess)
		return hr;

	// take some shortcuts if we're dealing with a Kopano message destination
	if (HrGetOneProp(lpDestProp, PR_EC_OBJECT, &~lpZObj) == hrSuccess &&
	    lpZObj->Value.lpszA != NULL)
		reinterpret_cast<IUnknown *>(lpZObj->Value.lpszA)->QueryInterface(IID_IUnknown, &~lpKopano);

	/* remember which props not to copy */
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(inclprop->cValues), &~lpIncludeProps);
	if (hr != hrSuccess)
		return hr;
	memcpy(lpIncludeProps, inclprop, CbNewSPropTagArray(inclprop->cValues));

	if (ulFlags & MAPI_NOREPLACE) {
		hr = lpDestProp->GetPropList(MAPI_UNICODE, &~lpsDestPropArray);
		if (hr != hrSuccess)
			return hr;

		for (ULONG i = 0; i < lpIncludeProps->cValues; ++i) {
			if (Util::FindPropInArray(lpsDestPropArray, lpIncludeProps->aulPropTag[i]) == -1)
				continue;
			// hr = MAPI_E_COLLISION;
			// goto exit;
			// MSDN says collision, MS MAPI ignores these properties.
			lpIncludeProps->aulPropTag[i] = PR_NULL;
		}
	}

	if (lpKopano) {
		// Use only one body text property, RTF, HTML or BODY when we're copying to another Kopano message.
		auto ulIdRTF = Util::FindPropInArray(lpIncludeProps, PR_RTF_COMPRESSED);
		auto ulIdHTML = Util::FindPropInArray(lpIncludeProps, PR_HTML);
		auto ulIdBODY = Util::FindPropInArray(lpIncludeProps, PR_BODY_W);

		// find out the original body type, and only copy that version
		ulBodyProp = GetBestBody(lpSrcProp, fMapiUnicode);

		if (ulBodyProp == PR_BODY && ulIdBODY != -1) {
			// discard html and rtf
			if(ulIdHTML != -1)
				lpIncludeProps->aulPropTag[ulIdHTML] = PR_NULL;
			if(ulIdRTF != -1)
				lpIncludeProps->aulPropTag[ulIdRTF] = PR_NULL;
		} else if (ulBodyProp == PR_HTML && ulIdHTML != -1) {
			// discard plain and rtf
			if(ulIdBODY != -1)
				lpIncludeProps->aulPropTag[ulIdBODY] = PR_NULL;
			if(ulIdRTF != -1)
				lpIncludeProps->aulPropTag[ulIdRTF] = PR_NULL;
		} else if (ulBodyProp == PR_RTF_COMPRESSED && ulIdRTF != -1) {
			// discard plain and html
			if(ulIdHTML != -1)
				lpIncludeProps->aulPropTag[ulIdHTML] = PR_NULL;
			if(ulIdBODY != -1)
				lpIncludeProps->aulPropTag[ulIdBODY] = PR_NULL;
		}
	}

	for (ULONG i = 0; i < lpIncludeProps->cValues; ++i) {
		bool isProblem = false;
		// TODO: ?
		// for all PT_OBJECT properties on IMAPIProp, MS MAPI tries:
		// IID_IMessage, IID_IStreamDocfile, IID_IStorage

		if (PROP_TYPE(lpIncludeProps->aulPropTag[i]) != PT_OBJECT &&
		    PROP_ID(lpIncludeProps->aulPropTag[i]) != PROP_ID(PR_ATTACH_DATA_BIN))
			continue;
		// if IMessage: PR_MESSAGE_RECIPIENTS, PR_MESSAGE_ATTACHMENTS
		if (*lpSrcInterface == IID_IMessage) {
			if (lpIncludeProps->aulPropTag[i] == PR_MESSAGE_RECIPIENTS)
				// TODO: add ulFlags, and check for MAPI_NOREPLACE
				hr = CopyRecipients(static_cast<IMessage *>(lpSrcObj), static_cast<IMessage *>(lpDestObj));
			else if (lpIncludeProps->aulPropTag[i] == PR_MESSAGE_ATTACHMENTS)
				// TODO: add ulFlags, and check for MAPI_NOREPLACE
				hr = Util::CopyAttachments((LPMESSAGE)lpSrcObj, (LPMESSAGE)lpDestObj, NULL);
			else
				hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
			if (hr != hrSuccess) {
				isProblem = true;
				goto next_include_check;
			}
		} else if (*lpSrcInterface == IID_IMAPIFolder) {
			// MS MAPI skips these in CopyProps(), for unknown reasons
			if (lpIncludeProps->aulPropTag[i] == PR_CONTAINER_CONTENTS ||
				lpIncludeProps->aulPropTag[i] == PR_CONTAINER_HIERARCHY ||
				lpIncludeProps->aulPropTag[i] == PR_FOLDER_ASSOCIATED_CONTENTS) {
				lpIncludeProps->aulPropTag[i] = PR_NULL;
			} else {
				isProblem = true;
			}
		} else if (*lpSrcInterface == IID_IAttachment) {
			object_ptr<IStream> lpSrcStream, lpDestStream;
			object_ptr<IMessage> lpSrcMessage, lpDestMessage;
			ULONG ulAttachMethod;

			// In attachments, IID_IMessage can be present!  for PR_ATTACH_DATA_OBJ
			// find method and copy this PT_OBJECT
			if (HrGetOneProp(lpSrcProp, PR_ATTACH_METHOD, &~lpAttachMethod) != hrSuccess)
				ulAttachMethod = ATTACH_BY_VALUE;
			else
				ulAttachMethod = lpAttachMethod->Value.ul;
			switch (ulAttachMethod) {
			case ATTACH_BY_VALUE:
			case ATTACH_OLE:
				// stream
				// Not being able to open the source message is not an error: it may just not be there
				if (((LPATTACH)lpSrcObj)->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0, 0, &~lpSrcStream) == hrSuccess) {
					// While dragging and dropping, Outlook 2007 (at least) returns an internal MAPI object to CopyTo as destination
					// The internal MAPI object is unable to make a stream STGM_TRANSACTED, so we retry the action without that flag
					// to get the stream without the transaction feature.
					hr = ((LPATTACH)lpDestObj)->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~lpDestStream);
					if (hr != hrSuccess)
						hr = ((LPATTACH)lpDestObj)->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE, MAPI_CREATE | MAPI_MODIFY, &~lpDestStream);
					if (hr != hrSuccess) {
						isProblem = true;
						goto next_include_check;
					}
					hr = CopyStream(lpSrcStream, lpDestStream);
					if (hr != hrSuccess) {
						isProblem = true;
						goto next_include_check;
					}
				} else if(lpAttachMethod->Value.ul == ATTACH_OLE &&
				    ((LPATTACH)lpSrcObj)->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IStream, 0, 0, &~lpSrcStream) == hrSuccess) {
					// OLE 2.0 must be open with PR_ATTACH_DATA_OBJ
					hr = ((LPATTACH)lpDestObj)->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~lpDestStream);
					if (hr == E_FAIL)
						hr = ((LPATTACH)lpDestObj)->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IStream, STGM_WRITE, MAPI_CREATE | MAPI_MODIFY, &~lpDestStream);
					if (hr != hrSuccess) {
						isProblem = true;
						goto next_include_check;
					}
					hr = CopyStream(lpSrcStream, lpDestStream);
					if (hr != hrSuccess) {
						isProblem = true;
						goto next_include_check;
					}
				}
				break;
			case ATTACH_EMBEDDED_MSG:
				// message
				if (((LPATTACH)lpSrcObj)->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, 0, &~lpSrcMessage) == hrSuccess) {
					// Not being able to open the source message is not an error: it may just not be there
					hr = ((LPATTACH)lpDestObj)->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY, &~lpDestMessage);
					if (hr != hrSuccess) {
						isProblem = true;
						goto next_include_check;
					}
					hr = Util::DoCopyTo(&IID_IMessage, lpSrcMessage, 0, NULL, NULL, ulUIParam, lpProgress, &IID_IMessage, lpDestMessage, 0, NULL);
					if (hr != hrSuccess) {
						isProblem = true;
						goto next_include_check;
					}
					hr = lpDestMessage->SaveChanges(0);
					if (hr != hrSuccess) {
						isProblem = true;
						goto next_include_check;
					}
				}
				break;
			default:
				// OLE objects?
				isProblem = true;
				break;
			};
		} else {
			isProblem = true;
		}
		// TODO: try the 3 MSMAPI interfaces (message, stream, storage) if unhandled?
next_include_check:
		if (isProblem) {
			SPropProblem sProblem;
			bPartial = true;
			sProblem.ulIndex = i;
			sProblem.ulPropTag = lpIncludeProps->aulPropTag[i];
			sProblem.scode = MAPI_E_INTERFACE_NOT_SUPPORTED; // hr?
			hr = AddProblemToArray(&sProblem, &+lpProblems);
			if (hr != hrSuccess)
				goto exit;
		}
		// skip this prop for the final SetProps()
		lpIncludeProps->aulPropTag[i] = PR_NULL;
	}

	hr = lpSrcProp->GetProps(lpIncludeProps, 0, &cValues, &~lpProps);
	if (FAILED(hr))
		goto exit;

	// make map for destination property tags, because named IDs may differ in src and dst
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(cValues), &~lpsDestTagArray);
	if (hr != hrSuccess)
		goto exit;

	// get named props
	for (ULONG i = 0; i < cValues; ++i) {
		lpsDestTagArray->aulPropTag[i] = lpProps[i].ulPropTag;

		if (PROP_ID(lpProps[i].ulPropTag) >= 0x8000)
			++cNames;
	}

	if (cNames) {
		hr = MAPIAllocateBuffer(CbNewSPropTagArray(cNames), &~lpsSrcNameTagArray);
		if (hr != hrSuccess)
			goto exit;

		lpsSrcNameTagArray->cValues = cNames;
		cNames = 0;
		for (ULONG i = 0; i < cValues; ++i)
			if (PROP_ID(lpProps[i].ulPropTag) >= 0x8000)
				lpsSrcNameTagArray->aulPropTag[cNames++] = lpProps[i].ulPropTag;

		// ignore warnings on unknown named properties, but don't copy those either (see PT_ERROR below)
		hr = lpSrcProp->GetNamesFromIDs(&+lpsSrcNameTagArray, NULL, 0, &cNames, &~lppNames);
		if (FAILED(hr))
			goto exit;
		hr = lpDestProp->GetIDsFromNames(cNames, lppNames, MAPI_CREATE, &~lpsDestNameTagArray);
		if (FAILED(hr))
			goto exit;

		// make new lookup map for lpProps[] -> lpsDestNameTag[]
		for (ULONG i = 0, j = 0; i < cValues && j < cNames; ++i) {
			if (PROP_ID(lpProps[i].ulPropTag) != PROP_ID(lpsSrcNameTagArray->aulPropTag[j]))
				continue;
			if (PROP_TYPE(lpsDestNameTagArray->aulPropTag[j]) != PT_ERROR)
				// replace with new proptag, so we can open the correct property
				lpsDestTagArray->aulPropTag[i] = CHANGE_PROP_TYPE(lpsDestNameTagArray->aulPropTag[j], PROP_TYPE(lpProps[i].ulPropTag));
			else
				// leave on PT_ERROR, so we don't copy the property
				lpsDestTagArray->aulPropTag[i] = CHANGE_PROP_TYPE(lpsDestNameTagArray->aulPropTag[j], PT_ERROR);
				// don't even return a warning because although not all data could be copied
			++j;
		}
	}

	// before we copy bodies possibly as streams, find the PR_INTERNET_CPID, which is required for correct high-char translations.
	ulIdCPID = Util::FindPropInArray(lpIncludeProps, PR_INTERNET_CPID);
	if (ulIdCPID != -1) {
	    hr = lpDestProp->SetProps(1, &lpProps[ulIdCPID], NULL);
	    if (FAILED(hr))
		goto exit;
	}

	// find all MAPI_E_NOT_ENOUGH_MEMORY errors
	for (ULONG i = 0; i < cValues; ++i) {
		bool err = false;
		if(PROP_TYPE(lpProps[i].ulPropTag) == PT_ERROR)
			err = lpProps[i].Value.err == MAPI_E_NOT_ENOUGH_MEMORY ||
				PROP_ID(lpProps[i].ulPropTag) == PROP_ID(PR_BODY) ||
				PROP_ID(lpProps[i].ulPropTag) == PROP_ID(PR_HTML) ||
				PROP_ID(lpProps[i].ulPropTag) == PROP_ID(PR_RTF_COMPRESSED);
		if (!err)
			continue;
		assert(PROP_ID(lpIncludeProps->aulPropTag[i]) == PROP_ID(lpProps[i].ulPropTag));
		object_ptr<IStream> lpSrcStream, lpDestStream;
		hr = TryOpenProperty(PROP_TYPE(lpIncludeProps->aulPropTag[i]), lpProps[i].ulPropTag, lpSrcProp, lpsDestTagArray->aulPropTag[i], lpDestProp, &~lpSrcStream, &~lpDestStream);
		if (hr != hrSuccess) {
			// TODO: check, partial or problemarray?
			// when the prop was not found (body property), it actually wasn't present, so don't mark as partial
			if (hr != MAPI_E_NOT_FOUND)
				bPartial = true;
			continue;
		}
		hr = CopyStream(lpSrcStream, lpDestStream);
		if (hr != hrSuccess)
			bPartial = true;
	}

	// set destination proptags in original properties
	for (ULONG i = 0; i < cValues; ++i) {
		lpProps[i].ulPropTag = lpsDestTagArray->aulPropTag[i];

		// Reset PT_ERROR properties because outlook xp pst doesn't support to set props this.
		if (PROP_TYPE(lpProps[i].ulPropTag) == PT_ERROR)
			lpProps[i].ulPropTag = PR_NULL;
	}

	hr = lpDestProp->SetProps(cValues, lpProps, NULL);
	if (FAILED(hr))
		goto exit;

	// TODO: test how this should work on CopyProps() !!
	if (ulFlags & MAPI_MOVE) {
		// TODO: add problem array
		hr = lpSrcProp->DeleteProps(lpIncludeProps, NULL);
		if (FAILED(hr))
			goto exit;
	}

exit:
	if (bPartial)
		hr = MAPI_W_PARTIAL_COMPLETION;
	if (hr == hrSuccess && lppProblems != nullptr)
		// may not return a problem set when we have a warning/error code in hr
		*lppProblems = lpProblems.release();
	return hr;
}

/** 
 * Copy the IMAP data properties if available, with single instance on
 * the IMAP Email.
 * 
 * @param[in] lpSrcMsg Copy IMAP data from this message
 * @param[in] lpDstMsg Copy IMAP data to this message
 * 
 * @return MAPI error code
 */
HRESULT Util::HrCopyIMAPData(LPMESSAGE lpSrcMsg, LPMESSAGE lpDstMsg)
{
	object_ptr<IStream> lpSrcStream, lpDestStream;
	static constexpr const SizedSPropTagArray(3, sptaIMAP) =
		{3, {PR_EC_IMAP_EMAIL_SIZE, PR_EC_IMAP_BODY,
		PR_EC_IMAP_BODYSTRUCTURE}};
	ULONG cValues = 0;
	memory_ptr<SPropValue> lpIMAPProps;

	// special case: get PR_EC_IMAP_BODY if present, and copy with single instance
	// hidden property in kopano, try to copy contents
	if (TryOpenProperty(PT_BINARY, PR_EC_IMAP_EMAIL, lpSrcMsg,
	    PR_EC_IMAP_EMAIL, lpDstMsg, &~lpSrcStream, &~lpDestStream) != hrSuccess ||
	    CopyStream(lpSrcStream, lpDestStream) != hrSuccess)
		return hrSuccess;
	/*
	 * Try making a single instance copy for IMAP body data (without sending the data to server).
	 * No error checking, we do not care if this fails, we still have all the data.
	 */
	CopyInstanceIds(lpSrcMsg, lpDstMsg);

	// Since we have a copy of the original email body, copy the other properties for IMAP too
	auto hr = lpSrcMsg->GetProps(sptaIMAP, 0, &cValues, &~lpIMAPProps);
	if (FAILED(hr))
		return hr;
	hr = lpDstMsg->SetProps(cValues, lpIMAPProps, NULL);
	if (FAILED(hr))
		return hr;
	return hrSuccess;
}

HRESULT Util::HrDeleteIMAPData(LPMESSAGE lpMsg)
{
	static constexpr const SizedSPropTagArray(4, sptaIMAP) =
		{4, { PR_EC_IMAP_EMAIL_SIZE,  PR_EC_IMAP_EMAIL,
		 PR_EC_IMAP_BODY, PR_EC_IMAP_BODYSTRUCTURE}};
	return lpMsg->DeleteProps(sptaIMAP, NULL);
}

/** 
 * Get the quota status object for a store with given quota limits.
 * 
 * @param[in] lpMsgStore Store to get the quota for
 * @param[in] lpsQuota The (optional) quota limits to check
 * @param[out] lppsQuotaStatus Quota status struct
 * 
 * @return MAPI error code
 */
HRESULT Util::HrGetQuotaStatus(IMsgStore *lpMsgStore, ECQUOTA *lpsQuota,
    ECQUOTASTATUS **lppsQuotaStatus)
{
	memory_ptr<ECQUOTASTATUS> lpsQuotaStatus;
	memory_ptr<SPropValue> lpProps;
	static constexpr const SizedSPropTagArray(1, sptaProps) = {1, {PR_MESSAGE_SIZE_EXTENDED}};
    ULONG 			cValues = 0;
	
	if (lpMsgStore == nullptr || lppsQuotaStatus == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpMsgStore->GetProps(sptaProps, 0, &cValues, &~lpProps);
	if (hr != hrSuccess)
		return hr;		
	if (cValues != 1 || lpProps[0].ulPropTag != PR_MESSAGE_SIZE_EXTENDED)
		return MAPI_E_NOT_FOUND;
	hr = MAPIAllocateBuffer(sizeof *lpsQuotaStatus, &~lpsQuotaStatus);
	if (hr != hrSuccess)
		return hr;
	memset(lpsQuotaStatus, 0, sizeof *lpsQuotaStatus);
	
	lpsQuotaStatus->llStoreSize = lpProps[0].Value.li.QuadPart;
	lpsQuotaStatus->quotaStatus = QUOTA_OK;
	if (lpsQuota && lpsQuotaStatus->llStoreSize > 0) {
		if (lpsQuota->llHardSize > 0 && lpsQuotaStatus->llStoreSize > lpsQuota->llHardSize)
			lpsQuotaStatus->quotaStatus = QUOTA_HARDLIMIT;
		else if (lpsQuota->llSoftSize > 0 && lpsQuotaStatus->llStoreSize > lpsQuota->llSoftSize)
			lpsQuotaStatus->quotaStatus = QUOTA_SOFTLIMIT;
		else if (lpsQuota->llWarnSize > 0 && lpsQuotaStatus->llStoreSize > lpsQuota->llWarnSize)
			lpsQuotaStatus->quotaStatus = QUOTA_WARN;
	}
	*lppsQuotaStatus = lpsQuotaStatus.release();
	return hrSuccess;
}

/** 
 * Removes properties from lpDestMsg, which do are not listed in
 * lpsValidProps.
 *
 * Named properties listed in lpsValidProps map to names in
 * lpSourceMsg. The corresponding property tags are checked in
 * lpDestMsg.
 * 
 * @param[out] lpDestMsg The message to delete properties from, which are found "invalid"
 * @param[in] lpSourceMsg The message for which named properties may be lookupped, listed in lpsValidProps
 * @param[in] lpsValidProps Properties which are valid in lpDestMsg. All others should be removed.
 * 
 * @return MAPI error code
 */
HRESULT Util::HrDeleteResidualProps(LPMESSAGE lpDestMsg, LPMESSAGE lpSourceMsg, LPSPropTagArray lpsValidProps)
{
	memory_ptr<SPropTagArray> lpsPropArray, lpsNamedPropArray;
	memory_ptr<SPropTagArray> lpsMappedPropArray;
	ULONG			cPropNames = 0;
	memory_ptr<MAPINAMEID *> lppPropNames;
	PropTagSet		sPropTagSet;

	if (lpDestMsg == nullptr || lpSourceMsg == nullptr || lpsValidProps == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpDestMsg->GetPropList(0, &~lpsPropArray);
	if (hr != hrSuccess || lpsPropArray->cValues == 0)
		return hr;
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(lpsValidProps->cValues), &~lpsNamedPropArray);
	if (hr != hrSuccess)
		return hr;
	memset(lpsNamedPropArray, 0, CbNewSPropTagArray(lpsValidProps->cValues));

	for (unsigned i = 0; i < lpsValidProps->cValues; ++i)
		if (PROP_ID(lpsValidProps->aulPropTag[i]) >= 0x8000)
			lpsNamedPropArray->aulPropTag[lpsNamedPropArray->cValues++] = lpsValidProps->aulPropTag[i];

	if (lpsNamedPropArray->cValues > 0) {
		hr = lpSourceMsg->GetNamesFromIDs(&+lpsNamedPropArray, NULL, 0, &cPropNames, &~lppPropNames);
		if (FAILED(hr))
			return hr;
		hr = lpDestMsg->GetIDsFromNames(cPropNames, lppPropNames, MAPI_CREATE, &~lpsMappedPropArray);
		if (FAILED(hr))
			return hr;
	}

	// Add the PropTags the message currently has
	for (unsigned i = 0; i < lpsPropArray->cValues; ++i)
		sPropTagSet.emplace(lpsPropArray->aulPropTag[i]);

	// Remove the regular properties we want to keep
	for (unsigned i = 0; i < lpsValidProps->cValues; ++i)
		if (PROP_ID(lpsValidProps->aulPropTag[i]) < 0x8000)
			sPropTagSet.erase(lpsValidProps->aulPropTag[i]);

	// Remove the mapped named properties we want to keep. Filter failed named properties, so they will be removed
	for (unsigned i = 0; lpsMappedPropArray != NULL && i < lpsMappedPropArray->cValues; ++i)
		if (PROP_TYPE(lpsMappedPropArray->aulPropTag[i]) != PT_ERROR)
			sPropTagSet.erase(lpsMappedPropArray->aulPropTag[i]);

	if (sPropTagSet.empty())
		return hrSuccess;

	// Reuse lpsPropArray to hold the properties we're going to delete
	assert(lpsPropArray->cValues >= sPropTagSet.size());
	memset(lpsPropArray->aulPropTag, 0, lpsPropArray->cValues * sizeof *lpsPropArray->aulPropTag);
	lpsPropArray->cValues = 0;

	for (const auto &i : sPropTagSet)
		lpsPropArray->aulPropTag[lpsPropArray->cValues++] = i;

	hr = lpDestMsg->DeleteProps(lpsPropArray, NULL);
	if (hr != hrSuccess)
		return hr;
	return lpDestMsg->SaveChanges(KEEP_OPEN_READWRITE);
}

HRESULT Util::HrDeleteAttachments(LPMESSAGE lpMsg)
{
	MAPITablePtr ptrAttachTable;
	SRowSetPtr ptrRows;
	static constexpr const SizedSPropTagArray(1, sptaAttachNum) = {1, {PR_ATTACH_NUM}};

	if (lpMsg == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = lpMsg->GetAttachmentTable(0, &~ptrAttachTable);
	if (hr != hrSuccess)
		return hr;
	hr = HrQueryAllRows(ptrAttachTable, sptaAttachNum, nullptr, nullptr, 0, &~ptrRows);
	if (hr != hrSuccess)
		return hr;

	for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
		hr = lpMsg->DeleteAttach(ptrRows[i].lpProps[0].Value.l, 0, NULL, 0);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT Util::HrDeleteRecipients(LPMESSAGE lpMsg)
{
	MAPITablePtr ptrRecipTable;
	SRowSetPtr ptrRows;
	static constexpr const SizedSPropTagArray(1, sptaRowId) = {1, {PR_ROWID}};

	if (lpMsg == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = lpMsg->GetRecipientTable(0, &~ptrRecipTable);
	if (hr != hrSuccess)
		return hr;
	hr = HrQueryAllRows(ptrRecipTable, sptaRowId, nullptr, nullptr, 0, &~ptrRows);
	if (hr != hrSuccess)
		return hr;
	return lpMsg->ModifyRecipients(MODRECIP_REMOVE, (LPADRLIST)ptrRows.get());
}

HRESULT Util::HrDeleteMessage(IMAPISession *lpSession, IMessage *lpMessage)
{
	ULONG cMsgProps;
	SPropArrayPtr ptrMsgProps;
	MsgStorePtr ptrStore;
	ULONG ulType;
	MAPIFolderPtr ptrFolder;
	ENTRYLIST entryList = {1, NULL};
	static constexpr const SizedSPropTagArray(3, sptaMessageProps) =
		{3, {PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID}};
	enum {IDX_ENTRYID, IDX_STORE_ENTRYID, IDX_PARENT_ENTRYID};

	HRESULT hr = lpMessage->GetProps(sptaMessageProps, 0, &cMsgProps, &~ptrMsgProps);
	if (hr != hrSuccess)
		return hr;
	hr = lpSession->OpenMsgStore(0, ptrMsgProps[IDX_STORE_ENTRYID].Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(ptrMsgProps[IDX_STORE_ENTRYID].Value.bin.lpb),
	     &iid_of(ptrStore), MDB_WRITE, &~ptrStore);
	if (hr != hrSuccess)
		return hr;
	hr = ptrStore->OpenEntry(ptrMsgProps[IDX_PARENT_ENTRYID].Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(ptrMsgProps[IDX_PARENT_ENTRYID].Value.bin.lpb),
	     &iid_of(ptrFolder), MAPI_MODIFY, &ulType, &~ptrFolder);
	if (hr != hrSuccess)
		return hr;

	entryList.cValues = 1;
	entryList.lpbin = &ptrMsgProps[IDX_ENTRYID].Value.bin;
	return ptrFolder->DeleteMessages(&entryList, 0, NULL, DELETE_HARD_DELETE);
}

/**
 * Read a property via OpenProperty and put the output in std::string
 *
 * @param[in] lpProp Object to read from
 * @param[in] ulPropTag Proptag to open
 * @param[out] strData String to write to
 * @return result
 */
HRESULT Util::ReadProperty(IMAPIProp *lpProp, ULONG ulPropTag, std::string &strData)
{
	object_ptr<IStream> lpStream;
	auto hr = lpProp->OpenProperty(ulPropTag, &IID_IStream, 0, 0, &~lpStream);
	if(hr != hrSuccess)
		return hr;
	return HrStreamToString(lpStream, strData);
}

/**
 * Write a property using OpenProperty()
 *
 * This function will open a stream to the given property and write all data from strData into
 * it usin STGM_DIRECT and MAPI_MODIFY | MAPI_CREATE. This means the existing data will be over-
 * written
 *
 * @param[in] lpProp Object to write to
 * @param[in] ulPropTag Property to write
 * @param[in] strData Data to write
 * @return result
 */
HRESULT Util::WriteProperty(IMAPIProp *lpProp, ULONG ulPropTag, const std::string &strData)
{
	object_ptr<IStream> lpStream;
	ULONG len = 0;
	auto hr = lpProp->OpenProperty(ulPropTag, &IID_IStream, STGM_DIRECT, MAPI_CREATE | MAPI_MODIFY, &~lpStream);
	if(hr != hrSuccess)
		return hr;
	hr = lpStream->Write(strData.data(), strData.size(), &len);
	if(hr != hrSuccess)
		return hr;
	return lpStream->Commit(0);
}

HRESULT Util::ExtractSuggestedContactsEntryID(LPSPropValue lpPropBlob, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	return ExtractAdditionalRenEntryID(lpPropBlob, RSF_PID_SUGGESTED_CONTACTS , lpcbEntryID, lppEntryID);
}

HRESULT Util::ExtractAdditionalRenEntryID(LPSPropValue lpPropBlob, unsigned short usBlockType, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	HRESULT hr;

	LPBYTE lpPos = lpPropBlob->Value.bin.lpb;
	LPBYTE lpEnd = lpPropBlob->Value.bin.lpb + lpPropBlob->Value.bin.cb;
		
	while (true) {
		if (lpPos + 8 > lpEnd)
			return MAPI_E_NOT_FOUND;
		unsigned short pos;
		memcpy(&pos, lpPos, sizeof(pos));
		pos = le16_to_cpu(pos);
		if (pos == 0)
			return MAPI_E_NOT_FOUND;
		if (pos != usBlockType) {
			unsigned short usLen = 0;
			lpPos += 2;	// Skip ID
			memcpy(&usLen, lpPos, sizeof(usLen));
			usLen = le16_to_cpu(usLen);
			lpPos += 2;
			if (lpPos + usLen > lpEnd)
				return MAPI_E_CORRUPT_DATA;
			lpPos += usLen;
			continue;
		}
		unsigned short usLen = 0;
		lpPos += 4;	// Skip ID + total length
		memcpy(&pos, lpPos, sizeof(pos));
		pos = le32_to_cpu(pos);
		if (pos != RSF_ELID_ENTRYID)
			return MAPI_E_CORRUPT_DATA;
		lpPos += 2;	// Skip check
		memcpy(&usLen, lpPos, sizeof(usLen));
		usLen = le16_to_cpu(usLen);
		lpPos += 2;
		if (lpPos + usLen > lpEnd)
			return MAPI_E_CORRUPT_DATA;
		hr = MAPIAllocateBuffer(usLen, reinterpret_cast<void **>(lppEntryID));
		if (hr != hrSuccess)
			return hr;
		memcpy(*lppEntryID, lpPos, usLen);
		*lpcbEntryID = usLen;
		return hrSuccess;
	}
	return hrSuccess;
}

} /* namespace */
