/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <string>
#include <cstring>
#include <mapidefs.h>
#include <mapitags.h>
#include <edkmdb.h>
#include <kopano/ECGuid.h>
#include "kcore.hpp"
#include "SOAPUtils.h"
#include "SOAPAlloc.h"
#include <kopano/stringutil.h>
#include <kopano/ustringutil.h>

namespace KC {

/* See m4lcommon/Util.cpp for twcmp */
template<typename T> static int twcmp(T a, T b)
{
	return (a < b) ? -1 : (a == b) ? 0 : 1;
}

class MVPropProxy {
public:
	MVPropProxy(struct propVal *lpMVProp): m_lpMVProp(lpMVProp)
	{ }

	unsigned int size() const {
		if (m_lpMVProp == NULL || (PROP_TYPE(m_lpMVProp->ulPropTag) & MV_FLAG) == 0)
			return 0;

		switch (PROP_TYPE(m_lpMVProp->ulPropTag)) {
		case PT_MV_I2:		return m_lpMVProp->Value.mvi.__size;
		case PT_MV_LONG:	return m_lpMVProp->Value.mvl.__size;
		case PT_MV_R4:		return m_lpMVProp->Value.mvflt.__size;
		case PT_MV_DOUBLE:
		case PT_MV_APPTIME:	return m_lpMVProp->Value.mvdbl.__size;
		case PT_MV_I8:		return m_lpMVProp->Value.mvl.__size;
		case PT_MV_SYSTIME:
		case PT_MV_CURRENCY:	return m_lpMVProp->Value.mvhilo.__size;
		case PT_MV_CLSID:
		case PT_MV_BINARY:	return m_lpMVProp->Value.mvbin.__size;
		case PT_MV_STRING8:
		case PT_MV_UNICODE:	return m_lpMVProp->Value.mvszA.__size;
		default: return 0;
		}
	}

	ECRESULT compare(unsigned int ulIndex, const struct propVal *lpProp, const ECLocale &locale, int *lpnCompareResult)
	{
		int nCompareResult = 0;

		if (m_lpMVProp == NULL || 
			(PROP_TYPE(m_lpMVProp->ulPropTag) & MV_FLAG) == 0 || 
			(PROP_TYPE(m_lpMVProp->ulPropTag) & ~MV_FLAG) != PROP_TYPE(lpProp->ulPropTag) ||
			ulIndex >= size())
			return KCERR_INVALID_PARAMETER;

		switch (PROP_TYPE(m_lpMVProp->ulPropTag)) {
		case PT_MV_I2:
			nCompareResult = twcmp(m_lpMVProp->Value.mvi.__ptr[ulIndex], lpProp->Value.i);
			break;
		case PT_MV_LONG:
			nCompareResult = twcmp(m_lpMVProp->Value.mvl.__ptr[ulIndex], lpProp->Value.ul);
			break;
		case PT_MV_R4:
			nCompareResult = twcmp(m_lpMVProp->Value.mvflt.__ptr[ulIndex], lpProp->Value.flt);
			break;
		case PT_MV_DOUBLE:
		case PT_MV_APPTIME:
			nCompareResult = twcmp(m_lpMVProp->Value.mvdbl.__ptr[ulIndex], lpProp->Value.dbl);
			break;
		case PT_MV_I8:
			/* promote LHS from unsigned int to int64_t */
			nCompareResult = twcmp(static_cast<int64_t>(m_lpMVProp->Value.mvl.__ptr[ulIndex]), lpProp->Value.li);
			break;
		case PT_MV_SYSTIME:
		case PT_MV_CURRENCY:
			if (m_lpMVProp->Value.mvhilo.__ptr[ulIndex].hi == lpProp->Value.hilo->hi)
				nCompareResult = twcmp(m_lpMVProp->Value.mvhilo.__ptr[ulIndex].lo, lpProp->Value.hilo->lo);
			else
				nCompareResult = twcmp(m_lpMVProp->Value.mvhilo.__ptr[ulIndex].hi, lpProp->Value.hilo->hi);
			break;
		case PT_MV_CLSID:
		case PT_MV_BINARY:
			nCompareResult = twcmp(m_lpMVProp->Value.mvbin.__ptr[ulIndex].__size, lpProp->Value.bin->__size);
			if (nCompareResult == 0)
				nCompareResult = memcmp(m_lpMVProp->Value.mvbin.__ptr[ulIndex].__ptr, lpProp->Value.bin->__ptr, lpProp->Value.bin->__size);
			break;
		case PT_MV_STRING8:
		case PT_MV_UNICODE:
			if (m_lpMVProp->Value.mvszA.__ptr[ulIndex] == NULL && lpProp->Value.lpszA != NULL)
				nCompareResult = 1;
			else if (m_lpMVProp->Value.mvszA.__ptr[ulIndex] != NULL && lpProp->Value.lpszA == NULL)
				nCompareResult = -1;
			else
				nCompareResult = u8_icompare(m_lpMVProp->Value.mvszA.__ptr[ulIndex], lpProp->Value.lpszA, locale);
			break;
		default:
			return KCERR_INVALID_PARAMETER;
		}

		*lpnCompareResult = nCompareResult;
		return erSuccess;
	}

private:
	struct propVal *m_lpMVProp;
};

int CompareSortOrderArray(const struct sortOrderArray *lpsSortOrder1,
    const struct sortOrderArray *lpsSortOrder2)
{
	if(lpsSortOrder1 == NULL && lpsSortOrder2 == NULL)
		return 0; // both NULL
	if(lpsSortOrder1 == NULL || lpsSortOrder2 == NULL)
		return -1; // not equal due to one of them being NULL
	if(lpsSortOrder1->__size != lpsSortOrder2->__size)
		return twcmp(lpsSortOrder1->__size, lpsSortOrder2->__size);

	for (gsoap_size_t i = 0; i < lpsSortOrder1->__size; ++i) {
		if(lpsSortOrder1->__ptr[i].ulPropTag != lpsSortOrder2->__ptr[i].ulPropTag)
			return -1;
		if(lpsSortOrder1->__ptr[i].ulOrder != lpsSortOrder2->__ptr[i].ulOrder)
			return -1;
	}
	// Exact match
	return 0;
}

ECRESULT CopyPropTagArray(struct soap *soap,
    const struct propTagArray *lpPTsSrc, struct propTagArray **lppsPTsDst)
{
	if (lppsPTsDst == nullptr || lpPTsSrc == nullptr)
		return KCERR_INVALID_PARAMETER;

	auto lpPTsDst = soap_new_propTagArray(soap);
	if (lpPTsDst == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	lpPTsDst->__size = lpPTsSrc->__size;

	if(lpPTsSrc->__size > 0) {
		lpPTsDst->__ptr = soap_new_unsignedInt(soap, lpPTsSrc->__size);
		memcpy(lpPTsDst->__ptr, lpPTsSrc->__ptr, sizeof(unsigned int) * lpPTsSrc->__size);
	} else {
		lpPTsDst->__ptr = NULL;
	}
	*lppsPTsDst = lpPTsDst;
	return erSuccess;
}

/**
 * Finds a specific property tag in a soap propValArray.
 *
 * @param[in]	lpPropValArray	SOAP propValArray
 * @param[in]	ulPropTagq		Property to search for in array, type may also be PT_UNSPECIFIED to find the first match on the PROP_ID
 * @return		propVal*		Direct pointer into the propValArray where the found property is, or NULL if not found.
 */
struct propVal *FindProp(const struct propValArray *lpPropValArray,
    unsigned int ulPropTag)
{
	if (lpPropValArray == NULL)
		return NULL;
	for (gsoap_size_t i = 0; i < lpPropValArray->__size; ++i)
		if (lpPropValArray->__ptr[i].ulPropTag == ulPropTag ||
			(PROP_TYPE(ulPropTag) == PT_UNSPECIFIED && PROP_ID(lpPropValArray->__ptr[i].ulPropTag) == PROP_ID(ulPropTag)))
			return &lpPropValArray->__ptr[i];
	return NULL;
}

/*
this function check if the right proptag with the value and is't null
*/
static ECRESULT PropCheck(const struct propVal *lpProp)
{
	ECRESULT er = erSuccess;
	if(lpProp == NULL)
		return KCERR_INVALID_PARAMETER;

	switch(PROP_TYPE(lpProp->ulPropTag))
	{
	case PT_I2:
		if(lpProp->__union != SOAP_UNION_propValData_i)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_LONG:
		if(lpProp->__union != SOAP_UNION_propValData_ul)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_R4:
		if(lpProp->__union != SOAP_UNION_propValData_flt)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_BOOLEAN:
		if(lpProp->__union != SOAP_UNION_propValData_b)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_DOUBLE:
		if(lpProp->__union != SOAP_UNION_propValData_dbl)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_APPTIME:
		if(lpProp->__union != SOAP_UNION_propValData_dbl)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_I8:
		if(lpProp->__union != SOAP_UNION_propValData_li)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_SYSTIME:
		if(lpProp->__union != SOAP_UNION_propValData_hilo)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_CURRENCY:
		if(lpProp->__union != SOAP_UNION_propValData_hilo)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_UNICODE:
		if(lpProp->__union != SOAP_UNION_propValData_lpszA)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_STRING8:
		if(lpProp->__union != SOAP_UNION_propValData_lpszA)
			er = KCERR_INVALID_PARAMETER;
		else if (lpProp->Value.lpszA == nullptr)
			er = KCERR_INVALID_PARAMETER;
		else
			er = erSuccess;
		break;
	case PT_BINARY:
		if(lpProp->__union != SOAP_UNION_propValData_bin)
			er = KCERR_INVALID_PARAMETER;
		else if (lpProp->Value.bin->__size > 0 && lpProp->Value.bin->__ptr == nullptr)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_CLSID:
		if(lpProp->__union != SOAP_UNION_propValData_bin)
			er = KCERR_INVALID_PARAMETER;
		else if (lpProp->Value.bin->__size > 0 &&
		    (lpProp->Value.bin->__ptr == nullptr || lpProp->Value.bin->__size % sizeof(GUID) != 0))
			er = KCERR_INVALID_PARAMETER;
		break;

		// TODO: check __ptr pointers?
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		if(lpProp->__union != SOAP_UNION_propValData_mvdbl)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_MV_CLSID:
	case PT_MV_BINARY:
		if(lpProp->__union != SOAP_UNION_propValData_mvbin)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_MV_SYSTIME:
	case PT_MV_CURRENCY:
		if(lpProp->__union != SOAP_UNION_propValData_mvhilo)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_MV_FLOAT:
		if(lpProp->__union != SOAP_UNION_propValData_mvflt)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_MV_I2:
		if(lpProp->__union != SOAP_UNION_propValData_mvi)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_MV_I8:
		if(lpProp->__union != SOAP_UNION_propValData_mvli)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_MV_LONG:
		if(lpProp->__union != SOAP_UNION_propValData_mvl)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_MV_UNICODE:
	case PT_MV_STRING8:
		if(lpProp->__union != SOAP_UNION_propValData_mvszA)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_ACTIONS:
		if(lpProp->__union != SOAP_UNION_propValData_actions)
			er = KCERR_INVALID_PARAMETER;
		break;
	case PT_SRESTRICTION:
		if(lpProp->__union != SOAP_UNION_propValData_res)
			er = KCERR_INVALID_PARAMETER;
		break;
	default:
		er = erSuccess;
		break;
	}
	return er;
}

static ECRESULT CompareABEID(const struct propVal *lpProp1,
    const struct propVal *lpProp2, int *lpCompareResult)
{
	ECRESULT er = erSuccess;
	int iResult = 0;

	assert(lpProp1 != NULL && PROP_TYPE(lpProp1->ulPropTag) == PT_BINARY);
	assert(lpProp2 != NULL && PROP_TYPE(lpProp2->ulPropTag) == PT_BINARY);
	assert(lpCompareResult != NULL);
	auto peid1 = reinterpret_cast<const ABEID *>(lpProp1->Value.bin->__ptr);
	auto peid2 = reinterpret_cast<const ABEID *>(lpProp2->Value.bin->__ptr);

	if (memcmp(&peid1->guid, &MUIDECSAB, sizeof(GUID)) || memcmp(&peid2->guid, &MUIDECSAB, sizeof(GUID))) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	if (peid1->ulVersion == peid2->ulVersion) {
		if (lpProp1->Value.bin->__size != lpProp2->Value.bin->__size)
			iResult = twcmp(lpProp1->Value.bin->__size, lpProp2->Value.bin->__size);
		else if (peid1->ulVersion == 0)
			iResult = twcmp(peid1->ulId, peid2->ulId);
		else
			iResult = strcmp((const char *)peid1->szExId, (const char *)peid2->szExId);
	} else {
		/**
		 * Different ABEID version, so check on the legacy ulId field. This implies that
		 * when a V0 ABEID is stored somewhere in the database, and the server was upgraded and
		 * an additional server was added, that the comparison will yield invalid results as
		 * we're not allowed to compare the legacy field cross server.
		 **/
		iResult = (int)(peid1->ulId - peid2->ulId);
	}

	if (iResult == 0)
		iResult = (int)(peid1->ulType - peid2->ulType);
exit:
	*lpCompareResult = iResult;
	return er;
}

ECRESULT CompareProp(const struct propVal *lpProp1,
    const struct propVal *lpProp2, const ECLocale &locale,
    int *lpCompareResult)
{
	if (lpProp1 == nullptr || lpProp2 == nullptr ||
	    lpCompareResult == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto ulPropTag1 = NormalizePropTag(lpProp1->ulPropTag);
	auto ulPropTag2 = NormalizePropTag(lpProp2->ulPropTag);
	if (PROP_TYPE(ulPropTag1) != PROP_TYPE(ulPropTag2))
		/* Treat this as equal */
		return KCERR_INVALID_PARAMETER;
	if (PropCheck(lpProp1) != erSuccess || PropCheck(lpProp2) != erSuccess)
		return KCERR_INVALID_PARAMETER;

	ECRESULT	er = erSuccess;
	int			nCompareResult = 0;
	// List of prperties that get special treatment
	static const struct {
		ULONG		ulPropTag;
		ECRESULT	(*lpfnComparer)(const struct propVal *, const struct propVal *, int *);
	} sSpecials[] = {
		{PR_ADDRESS_BOOK_ENTRYID, &CompareABEID},
	};

	// First check if the any of the properties is in the sSpecials list
	for (size_t x = 0; x < ARRAY_SIZE(sSpecials); ++x) {
		bool special = lpProp1->ulPropTag == sSpecials[x].ulPropTag && PROP_TYPE(lpProp2->ulPropTag) == PROP_TYPE(sSpecials[x].ulPropTag);
		special |= PROP_TYPE(lpProp1->ulPropTag) == PROP_TYPE(sSpecials[x].ulPropTag) && lpProp2->ulPropTag == sSpecials[x].ulPropTag;
		if (!special)
			continue;
		er = sSpecials[x].lpfnComparer(lpProp1, lpProp2, &nCompareResult);
		if (er == erSuccess)
			goto skip_check;
		er = erSuccess;
		break;
	}

	// Perform a regular comparison
	switch(PROP_TYPE(lpProp1->ulPropTag)) {
	case PT_I2:
		nCompareResult = twcmp(lpProp1->Value.i, lpProp2->Value.i);
		break;
	case PT_LONG:
		if(lpProp1->Value.ul == lpProp2->Value.ul)
			nCompareResult = 0;
		else if(lpProp1->Value.ul < lpProp2->Value.ul)
			nCompareResult = -1;
		else
			nCompareResult = 1;
		break;
	case PT_R4:
		if(lpProp1->Value.flt == lpProp2->Value.flt)
			nCompareResult = 0;
		else if(lpProp1->Value.flt < lpProp2->Value.flt)
			nCompareResult = -1;
		else
			nCompareResult = 1;
		break;
	case PT_BOOLEAN:
		nCompareResult = twcmp(lpProp1->Value.b, lpProp2->Value.b);
		break;
	case PT_DOUBLE:
	case PT_APPTIME:
		if(lpProp1->Value.dbl == lpProp2->Value.dbl)
			nCompareResult = 0;
		else if(lpProp1->Value.dbl < lpProp2->Value.dbl)
			nCompareResult = -1;
		else
			nCompareResult = 1;
		break;
	case PT_I8:
		if(lpProp1->Value.li == lpProp2->Value.li)
			nCompareResult = 0;
		else if(lpProp1->Value.li < lpProp2->Value.li)
			nCompareResult = -1;
		else
			nCompareResult = 1;
		break;
	case PT_UNICODE:
	case PT_STRING8:
		if (lpProp1->Value.lpszA == nullptr || lpProp2->Value.lpszA == nullptr)
			nCompareResult = lpProp1->Value.lpszA != lpProp2->Value.lpszA;
		else if (PROP_ID(lpProp2->ulPropTag) == PROP_ID(PR_ANR))
			nCompareResult = u8_istartswith(lpProp1->Value.lpszA, lpProp2->Value.lpszA, locale);
		else
			nCompareResult = u8_icompare(lpProp1->Value.lpszA, lpProp2->Value.lpszA, locale);
		break;
	case PT_SYSTIME:
	case PT_CURRENCY:
		if(lpProp1->Value.hilo->hi == lpProp2->Value.hilo->hi && lpProp1->Value.hilo->lo < lpProp2->Value.hilo->lo)
			nCompareResult = -1;
		else if(lpProp1->Value.hilo->hi == lpProp2->Value.hilo->hi && lpProp1->Value.hilo->lo > lpProp2->Value.hilo->lo)
			nCompareResult = 1;
		else
			nCompareResult = twcmp(lpProp1->Value.hilo->hi, lpProp2->Value.hilo->hi);
		break;
	case PT_BINARY:
	case PT_CLSID:
		if (lpProp1->Value.bin->__ptr && lpProp2->Value.bin->__ptr &&
			lpProp1->Value.bin->__size && lpProp2->Value.bin->__size &&
			lpProp1->Value.bin->__size == lpProp2->Value.bin->__size)
			nCompareResult = memcmp(lpProp1->Value.bin->__ptr, lpProp2->Value.bin->__ptr, lpProp1->Value.bin->__size);
		else
			nCompareResult = twcmp(lpProp1->Value.bin->__size, lpProp2->Value.bin->__size);
		break;

	case PT_MV_I2:
		if (lpProp1->Value.mvi.__size != lpProp2->Value.mvi.__size) {
			nCompareResult = twcmp(lpProp1->Value.mvi.__size, lpProp2->Value.mvi.__size);
			break;
		}
		for (gsoap_size_t i = 0; i < lpProp1->Value.mvi.__size; ++i) {
			nCompareResult = twcmp(lpProp1->Value.mvi.__ptr[i], lpProp2->Value.mvi.__ptr[i]);
			if (nCompareResult != 0)
				break;
		}
		break;
	case PT_MV_LONG:
		if (lpProp1->Value.mvl.__size != lpProp2->Value.mvl.__size) {
			nCompareResult = twcmp(lpProp1->Value.mvl.__size, lpProp2->Value.mvl.__size);
			break;
		}
		for (gsoap_size_t i = 0; i < lpProp1->Value.mvl.__size; ++i) {
			if (lpProp1->Value.mvl.__ptr[i] == lpProp2->Value.mvl.__ptr[i])
				nCompareResult = 0;
			else if (lpProp1->Value.mvl.__ptr[i] < lpProp2->Value.mvl.__ptr[i])
				nCompareResult = -1;
			else
				nCompareResult = 1;
			if (nCompareResult != 0)
				break;
		}
		break;
	case PT_MV_R4:
		if (lpProp1->Value.mvflt.__size != lpProp2->Value.mvflt.__size) {
			nCompareResult = twcmp(lpProp1->Value.mvflt.__size, lpProp2->Value.mvflt.__size);
			break;
		}
		for (gsoap_size_t i = 0; i < lpProp1->Value.mvflt.__size; ++i) {
			if (lpProp1->Value.mvflt.__ptr[i] == lpProp2->Value.mvflt.__ptr[i])
				nCompareResult = 0;
			else if (lpProp1->Value.mvflt.__ptr[i] < lpProp2->Value.mvflt.__ptr[i])
				nCompareResult = -1;
			else
				nCompareResult = 1;
			if (nCompareResult != 0)
				break;
		}
		break;
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		if (lpProp1->Value.mvdbl.__size != lpProp2->Value.mvdbl.__size) {
			nCompareResult = twcmp(lpProp1->Value.mvdbl.__size, lpProp2->Value.mvdbl.__size);
			break;
		}
		for (gsoap_size_t i = 0; i < lpProp1->Value.mvdbl.__size; ++i) {
			if (lpProp1->Value.mvdbl.__ptr[i] == lpProp2->Value.mvdbl.__ptr[i])
				nCompareResult = 0;
			else if (lpProp1->Value.mvdbl.__ptr[i] < lpProp2->Value.mvdbl.__ptr[i])
				nCompareResult = -1;
			else
				nCompareResult = 1;
			if (nCompareResult != 0)
				break;
		}
		break;
	case PT_MV_I8:
		if (lpProp1->Value.mvli.__size != lpProp2->Value.mvli.__size) {
			nCompareResult = twcmp(lpProp1->Value.mvli.__size, lpProp2->Value.mvli.__size);
			break;
		}
		for (gsoap_size_t i = 0; i < lpProp1->Value.mvli.__size; ++i) {
			if (lpProp1->Value.mvli.__ptr[i] == lpProp2->Value.mvli.__ptr[i])
				nCompareResult = 0;
			else if (lpProp1->Value.mvli.__ptr[i] < lpProp2->Value.mvli.__ptr[i])
				nCompareResult = -1;
			else
				nCompareResult = 1;
			if (nCompareResult != 0)
				break;
		}
		break;
	case PT_MV_SYSTIME:
	case PT_MV_CURRENCY:
		if (lpProp1->Value.mvhilo.__size != lpProp2->Value.mvhilo.__size) {
			nCompareResult = lpProp1->Value.mvhilo.__size == lpProp2->Value.mvhilo.__size;
			break;
		}
		for (gsoap_size_t i = 0; i < lpProp1->Value.mvhilo.__size; ++i) {
			if (lpProp1->Value.mvhilo.__ptr[i].hi == lpProp2->Value.mvhilo.__ptr[i].hi && lpProp1->Value.mvhilo.__ptr[i].lo < lpProp2->Value.mvhilo.__ptr[i].lo)
				nCompareResult = -1;
			else if (lpProp1->Value.mvhilo.__ptr[i].hi == lpProp2->Value.mvhilo.__ptr[i].hi && lpProp1->Value.mvhilo.__ptr[i].lo > lpProp2->Value.mvhilo.__ptr[i].lo)
				nCompareResult = 1;
			else
				nCompareResult = twcmp(lpProp1->Value.mvhilo.__ptr[i].hi, lpProp2->Value.mvhilo.__ptr[i].hi);
			if (nCompareResult != 0)
				break;
		}
		break;
	case PT_MV_CLSID:
	case PT_MV_BINARY:
		if (lpProp1->Value.mvbin.__size != lpProp2->Value.mvbin.__size) {
			nCompareResult = twcmp(lpProp1->Value.mvbin.__size, lpProp2->Value.mvbin.__size);
			break;
		}
		for (gsoap_size_t i = 0; i < lpProp1->Value.mvbin.__size; ++i) {
			if (lpProp1->Value.mvbin.__ptr[i].__ptr && lpProp2->Value.mvbin.__ptr[i].__ptr &&
			    lpProp1->Value.mvbin.__ptr[i].__size && lpProp2->Value.mvbin.__ptr[i].__size &&
			    lpProp1->Value.mvbin.__ptr[i].__size == lpProp2->Value.mvbin.__ptr[i].__size)
				nCompareResult = memcmp(lpProp1->Value.mvbin.__ptr[i].__ptr, lpProp2->Value.mvbin.__ptr[i].__ptr, lpProp1->Value.mvbin.__ptr[i].__size);
			else
				nCompareResult = twcmp(lpProp1->Value.mvbin.__ptr[i].__size, lpProp2->Value.mvbin.__ptr[i].__size);
			if (nCompareResult != 0)
				break;
		}
		break;
	case PT_MV_STRING8:
	case PT_MV_UNICODE:
		if (lpProp1->Value.mvszA.__size != lpProp2->Value.mvszA.__size) {
			nCompareResult = twcmp(lpProp1->Value.mvszA.__size, lpProp2->Value.mvszA.__size);
			break;
		}
		for (gsoap_size_t i = 0; i < lpProp1->Value.mvszA.__size; ++i) {
			if (lpProp1->Value.mvszA.__ptr[i] && lpProp2->Value.mvszA.__ptr[i])
				nCompareResult =u8_icompare(lpProp1->Value.mvszA.__ptr[i], lpProp2->Value.mvszA.__ptr[i], locale);
			else
				nCompareResult = lpProp1->Value.mvszA.__ptr[i] != lpProp2->Value.mvszA.__ptr[i];
			if (nCompareResult != 0)
				break;
		}
		break;
	default:
		return KCERR_INVALID_PARAMETER;
	}

skip_check:
	*lpCompareResult = nCompareResult;
	return er;
}

/**
 * ulType is one of the RELOP_xx types. The result returned will indicate that at least one of the values in lpMVProp positively 
 * matched the RELOP_xx comparison with lpProp2.
 **/
ECRESULT CompareMVPropWithProp(struct propVal *lpMVProp1,
    const struct propVal *lpProp2, unsigned int ulType, const ECLocale &locale,
    bool *lpfMatch)
{
	if (lpMVProp1 == nullptr || lpProp2 == nullptr || lpfMatch == nullptr)
		return KCERR_INVALID_PARAMETER;
	if ((PROP_TYPE(lpMVProp1->ulPropTag) & ~MV_FLAG) != PROP_TYPE(lpProp2->ulPropTag))
		/* Treat this as equal */
		return KCERR_INVALID_PARAMETER;
	if (PropCheck(lpMVProp1) != erSuccess || PropCheck(lpProp2) != erSuccess)
		return KCERR_INVALID_PARAMETER;

	int			nCompareResult = -1; // Default, Don't change this to 0
	bool		fMatch = false;
	MVPropProxy pxyMVProp1(lpMVProp1);

	for (unsigned int i = 0; !fMatch && i < pxyMVProp1.size(); ++i) {
		auto er = pxyMVProp1.compare(i, lpProp2, locale, &nCompareResult);
		if (er != erSuccess)
			return er;

		switch(ulType) {
		case RELOP_GE:
			fMatch = nCompareResult >= 0;
			break;
		case RELOP_GT:
			fMatch = nCompareResult > 0;
			break;
		case RELOP_LE:
			fMatch = nCompareResult <= 0;
			break;
		case RELOP_LT:
			fMatch = nCompareResult < 0;
			break;
		case RELOP_NE:
			fMatch = nCompareResult != 0;
			break;
		case RELOP_RE:
			fMatch = false; // FIXME ?? how should this work ??
			break;
		case RELOP_EQ:
			fMatch = nCompareResult == 0;
			break;
		}
	}

	*lpfMatch = fMatch;
	return erSuccess;
}

size_t PropSize(const struct propVal *lpProp)
{
	size_t ulSize;

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
	case PT_STRING8:
		return lpProp->Value.lpszA ? strlen(lpProp->Value.lpszA) : 0;
	case PT_SYSTIME:
	case PT_CURRENCY:
		return 8;
	case PT_BINARY:
	case PT_CLSID:
		return lpProp->Value.bin ? lpProp->Value.bin->__size : 0;
	case PT_MV_I2:
		return 2 * lpProp->Value.mvi.__size;
	case PT_MV_R4:
		return 4 * lpProp->Value.mvflt.__size;
	case PT_MV_LONG:
		return 4 * lpProp->Value.mvl.__size;
	case PT_MV_APPTIME:
	case PT_MV_DOUBLE:
		return 8 * lpProp->Value.mvdbl.__size;
	case PT_MV_I8:
		return 8 * lpProp->Value.mvli.__size;
	case PT_MV_UNICODE:
	case PT_MV_STRING8:
		ulSize = 0;
		for (gsoap_size_t i = 0; i < lpProp->Value.mvszA.__size; ++i)
			ulSize += lpProp->Value.mvszA.__ptr[i] ? strlen(lpProp->Value.mvszA.__ptr[i]) : 0;
		return ulSize;
	case PT_MV_SYSTIME:
	case PT_MV_CURRENCY:
		return 8 * lpProp->Value.mvhilo.__size;
	case PT_MV_BINARY:
	case PT_MV_CLSID:
		ulSize = 0;
		for (gsoap_size_t i = 0; i < lpProp->Value.mvbin.__size; ++i)
			ulSize+= lpProp->Value.mvbin.__ptr[i].__size;
		return ulSize;
	default:
		return 0;
	}
}

ECRESULT FreePropVal(struct propVal *lpProp, bool bBasePointerDel)
{
	if (lpProp == nullptr)
		return erSuccess;
	ECRESULT er = erSuccess;
	switch(PROP_TYPE(lpProp->ulPropTag)) {
	case PT_I2:
	case PT_LONG:
	case PT_R4:
	case PT_BOOLEAN:
	case PT_DOUBLE:
	case PT_APPTIME:
	case PT_I8:
		// no extra cleanup needed
		break;
	case PT_SYSTIME:
	case PT_CURRENCY:
		soap_del_PointerTohiloLong(&lpProp->Value.hilo);
		break;
	case PT_STRING8:
	case PT_UNICODE:
		soap_del_string(&lpProp->Value.lpszA);
		break;
	case PT_CLSID:
	case PT_BINARY:
		soap_del_PointerToxsd__base64Binary(&lpProp->Value.bin);
		break;
	case PT_MV_I2:
		soap_del_mv_i2(&lpProp->Value.mvi);
		break;
	case PT_MV_LONG:
		soap_del_mv_long(&lpProp->Value.mvl);
		break;
	case PT_MV_R4:
		soap_del_mv_r4(&lpProp->Value.mvflt);
		break;
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		soap_del_mv_double(&lpProp->Value.mvdbl);
		break;
	case PT_MV_I8:
		soap_del_mv_i8(&lpProp->Value.mvli);
		break;
	case PT_MV_SYSTIME:
	case PT_MV_CURRENCY:
		soap_del_mv_hiloLong(&lpProp->Value.mvhilo);
		break;
	case PT_MV_CLSID:
	case PT_MV_BINARY:
		soap_del_mv_binary(&lpProp->Value.mvbin);
		break;
	case PT_MV_STRING8:
	case PT_MV_UNICODE:
		soap_del_mv_string8(&lpProp->Value.mvszA);
		break;
	case PT_SRESTRICTION:
		if(lpProp->Value.res)
			FreeRestrictTable(lpProp->Value.res);
		break;
	case PT_ACTIONS: {
		if (lpProp->Value.actions == nullptr)
			break;

		struct actions *lpActions = lpProp->Value.actions;
		for (gsoap_size_t i = 0; i < lpActions->__size; ++i) {
			struct action *lpAction = &lpActions->__ptr[i];

			switch(lpAction->acttype) {
			case OP_COPY:
			case OP_MOVE:
				soap_del_xsd__base64Binary(&lpAction->act.moveCopy.store);
				soap_del_xsd__base64Binary(&lpAction->act.moveCopy.folder);
				break;
			case OP_REPLY:
			case OP_OOF_REPLY:
				soap_del_xsd__base64Binary(&lpAction->act.reply.message);
				soap_del_xsd__base64Binary(&lpAction->act.reply.guid);
				break;
			case OP_DEFER_ACTION:
				soap_del_xsd__base64Binary(&lpAction->act.defer.bin);
				break;
			case OP_BOUNCE:
				break;
			case OP_FORWARD:
			case OP_DELEGATE:
				FreeRowSet(lpAction->act.adrlist);
				break;
			case OP_TAG:
				FreePropVal(lpAction->act.prop, true);
				break;
			}
		}
		s_free(nullptr, lpActions->__ptr);
		s_free(nullptr, lpProp->Value.actions);
		break;
	}
	default:
		er = KCERR_INVALID_TYPE;
	}

	if(bBasePointerDel)
		s_free(nullptr, lpProp);
	return er;
}

void FreeRowSet(struct rowSet *lpRowSet)
{
	if(lpRowSet == NULL)
		return;
	for (gsoap_size_t i = 0; i < lpRowSet->__size; ++i)
		FreePropValArray(&lpRowSet->__ptr[i]);
	s_free(nullptr, lpRowSet->__ptr);
	s_free(nullptr, lpRowSet);
}

/** 
 * Frees a soap restriction table
 * 
 * @param[in] lpRestrict the soap restriction table to free and everything below it
 * @param[in] base always true, except when you know what you're doing (aka restriction optimizer for the kopano-search)
 * 
 * @return 
 */
ECRESULT FreeRestrictTable(struct restrictTable *lpRestrict, bool base)
{
	ECRESULT er;

	if(lpRestrict == NULL)
		return erSuccess;

	switch(lpRestrict->ulType) {
	case RES_OR:
		if(lpRestrict->lpOr && lpRestrict->lpOr->__ptr) {
			for (gsoap_size_t i = 0; i < lpRestrict->lpOr->__size; ++i) {
				er = FreeRestrictTable(lpRestrict->lpOr->__ptr[i]);
				if(er != erSuccess)
					return er;
			}
			s_free(nullptr, lpRestrict->lpOr->__ptr);
		}
		s_free(nullptr, lpRestrict->lpOr);
		break;
	case RES_AND:
		if(lpRestrict->lpAnd && lpRestrict->lpAnd->__ptr) {
			for (gsoap_size_t i = 0; i < lpRestrict->lpAnd->__size; ++i) {
				er = FreeRestrictTable(lpRestrict->lpAnd->__ptr[i]);
				if(er != erSuccess)
					return er;
			}
			s_free(nullptr, lpRestrict->lpAnd->__ptr);
		}
		s_free(nullptr, lpRestrict->lpAnd);
		break;
	case RES_NOT:
		if(lpRestrict->lpNot && lpRestrict->lpNot->lpNot)
			FreeRestrictTable(lpRestrict->lpNot->lpNot);
		s_free(nullptr, lpRestrict->lpNot);
		break;
	case RES_CONTENT:
		if(lpRestrict->lpContent && lpRestrict->lpContent->lpProp)
			FreePropVal(lpRestrict->lpContent->lpProp, true);
		s_free(nullptr, lpRestrict->lpContent);
		break;
	case RES_PROPERTY:
		if(lpRestrict->lpProp && lpRestrict->lpProp->lpProp)
			FreePropVal(lpRestrict->lpProp->lpProp, true);
		s_free(nullptr, lpRestrict->lpProp);
		break;
	case RES_COMPAREPROPS:
		s_free(nullptr, lpRestrict->lpCompare);
		break;
	case RES_BITMASK:
		s_free(nullptr, lpRestrict->lpBitmask);
		break;
	case RES_SIZE:
		s_free(nullptr, lpRestrict->lpSize);
		break;
	case RES_EXIST:
		s_free(nullptr, lpRestrict->lpExist);
		break;
	case RES_COMMENT:
		if (lpRestrict->lpComment == nullptr)
			break;
		if (lpRestrict->lpComment->lpResTable)
			FreeRestrictTable(lpRestrict->lpComment->lpResTable);
		FreePropValArray(&lpRestrict->lpComment->sProps);
		s_free(nullptr, lpRestrict->lpComment);
		break;
	case RES_SUBRESTRICTION:
		if(lpRestrict->lpSub && lpRestrict->lpSub->lpSubObject)
			FreeRestrictTable(lpRestrict->lpSub->lpSubObject);
		s_free(nullptr, lpRestrict->lpSub);
		break;
	default:
		er = KCERR_INVALID_TYPE;
		// NOTE: don't exit here, delete lpRestrict
		break;
	}

	// only when we're optimizing restrictions we must keep the base pointer, so we can replace it with new content
	if (base)
		s_free(nullptr, lpRestrict);
	return erSuccess;
}

ECRESULT CopyPropVal(const struct propVal *lpSrc, struct propVal *lpDst,
    struct soap *soap, bool bTruncate)
{
	ECRESULT er = PropCheck(lpSrc);
	if(er != erSuccess)
		return er;

	lpDst->ulPropTag = lpSrc->ulPropTag;
	lpDst->__union = lpSrc->__union;

	switch(PROP_TYPE(lpSrc->ulPropTag)) {
	case PT_I2:
		lpDst->Value.i = lpSrc->Value.i;
		break;
	case PT_NULL:
	case PT_ERROR:
	case PT_LONG:
		lpDst->Value.ul = lpSrc->Value.ul;
		break;
	case PT_R4:
		lpDst->Value.flt = lpSrc->Value.flt;
		break;
	case PT_BOOLEAN:
		lpDst->Value.b = lpSrc->Value.b;
		break;
	case PT_DOUBLE:
	case PT_APPTIME:
		lpDst->Value.dbl = lpSrc->Value.dbl;
		break;
	case PT_I8:
		lpDst->Value.li = lpSrc->Value.li;
		break;
	case PT_CURRENCY:
	case PT_SYSTIME:
		if (lpSrc->Value.hilo == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.hilo = soap_new_hiloLong(soap);
		lpDst->Value.hilo->hi = lpSrc->Value.hilo->hi;
		lpDst->Value.hilo->lo = lpSrc->Value.hilo->lo;
		break;
	case PT_UNICODE:
	case PT_STRING8: {
		if (lpSrc->Value.lpszA == NULL)
			return KCERR_INVALID_TYPE;
		size_t len = !bTruncate ? strlen(lpSrc->Value.lpszA) :
		             u8_cappedbytes(lpSrc->Value.lpszA, TABLE_CAP_STRING);
		lpDst->Value.lpszA = soap_new_byte(soap, len + 1);
		strncpy(lpDst->Value.lpszA, lpSrc->Value.lpszA, len);
		lpDst->Value.lpszA[len] = '\0'; // null terminate after strncpy
		break;
	}
	case PT_BINARY:
	case PT_CLSID:
		if (lpSrc->Value.bin == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.bin = soap_new_xsd__base64Binary(soap);
		lpDst->Value.bin->__size = lpSrc->Value.bin->__size;
		if (bTruncate && lpDst->Value.bin->__size > TABLE_CAP_BINARY)
			lpDst->Value.bin->__size = TABLE_CAP_BINARY;
		lpDst->Value.bin->__ptr = soap_new_unsignedByte(soap, lpSrc->Value.bin->__size);
		memcpy(lpDst->Value.bin->__ptr, lpSrc->Value.bin->__ptr, lpDst->Value.bin->__size);
		break;
	case PT_MV_I2:
		if (lpSrc->Value.mvi.__ptr == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.mvi.__size = lpSrc->Value.mvi.__size;
		lpDst->Value.mvi.__ptr  = soap_new_short(soap, lpSrc->Value.mvi.__size);
		memcpy(lpDst->Value.mvi.__ptr, lpSrc->Value.mvi.__ptr, sizeof(short int) * lpDst->Value.mvi.__size);
		break;
	case PT_MV_LONG:
		if (lpSrc->Value.mvl.__ptr == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.mvl.__size = lpSrc->Value.mvl.__size;
		lpDst->Value.mvl.__ptr  = soap_new_unsignedInt(soap, lpSrc->Value.mvl.__size);
		memcpy(lpDst->Value.mvl.__ptr, lpSrc->Value.mvl.__ptr, sizeof(unsigned int) * lpDst->Value.mvl.__size);
		break;
	case PT_MV_R4:
		if (lpSrc->Value.mvflt.__ptr == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.mvflt.__size = lpSrc->Value.mvflt.__size;
		lpDst->Value.mvflt.__ptr  = soap_new_float(soap, lpSrc->Value.mvflt.__size);
		memcpy(lpDst->Value.mvflt.__ptr, lpSrc->Value.mvflt.__ptr, sizeof(float) * lpDst->Value.mvflt.__size);
		break;
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		if (lpSrc->Value.mvdbl.__ptr == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.mvdbl.__size = lpSrc->Value.mvdbl.__size;
		lpDst->Value.mvdbl.__ptr  = soap_new_double(soap, lpSrc->Value.mvdbl.__size);
		memcpy(lpDst->Value.mvdbl.__ptr, lpSrc->Value.mvdbl.__ptr, sizeof(double) * lpDst->Value.mvdbl.__size);
		break;
	case PT_MV_I8:
		if (lpSrc->Value.mvli.__ptr == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.mvli.__size = lpSrc->Value.mvli.__size;
		lpDst->Value.mvli.__ptr  = soap_new_LONG64(soap, lpSrc->Value.mvli.__size);
		memcpy(lpDst->Value.mvli.__ptr, lpSrc->Value.mvli.__ptr, sizeof(LONG64) * lpDst->Value.mvli.__size);
		break;
	case PT_MV_CURRENCY:
	case PT_MV_SYSTIME:
		if (lpSrc->Value.mvhilo.__ptr == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.mvhilo.__size = lpSrc->Value.mvhilo.__size;
		lpDst->Value.mvhilo.__ptr  = soap_new_hiloLong(soap, lpSrc->Value.mvhilo.__size);
		memcpy(lpDst->Value.mvhilo.__ptr, lpSrc->Value.mvhilo.__ptr, sizeof(hiloLong) * lpDst->Value.mvhilo.__size);
		break;
	case PT_MV_STRING8:
	case PT_MV_UNICODE:
		if (lpSrc->Value.mvszA.__ptr == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.mvszA.__size = lpSrc->Value.mvszA.__size;
		lpDst->Value.mvszA.__ptr  = soap_new_string(soap, lpSrc->Value.mvszA.__size);
		for (gsoap_size_t i = 0; i < lpSrc->Value.mvszA.__size; ++i)
			lpDst->Value.mvszA.__ptr[i] = soap_strdup(soap, lpSrc->Value.mvszA.__ptr[i] == nullptr ? "" : lpSrc->Value.mvszA.__ptr[i]);
		break;
	case PT_MV_BINARY:
	case PT_MV_CLSID:
		if (lpSrc->Value.mvbin.__ptr == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->Value.mvbin.__size = lpSrc->Value.mvbin.__size;
		lpDst->Value.mvbin.__ptr  = soap_new_xsd__base64Binary(soap, lpSrc->Value.mvbin.__size);
		for (gsoap_size_t i = 0; i < lpSrc->Value.mvbin.__size; ++i) {
			lpDst->Value.mvbin.__ptr[i].__ptr = soap_new_unsignedByte(soap, lpSrc->Value.mvbin.__ptr[i].__size);
			if(lpSrc->Value.mvbin.__ptr[i].__ptr == NULL) {
				lpDst->Value.mvbin.__ptr[i].__size = 0;
			} else {
				memcpy(lpDst->Value.mvbin.__ptr[i].__ptr, lpSrc->Value.mvbin.__ptr[i].__ptr, lpSrc->Value.mvbin.__ptr[i].__size);
				lpDst->Value.mvbin.__ptr[i].__size = lpSrc->Value.mvbin.__ptr[i].__size;
			}
		}
		break;
	default:
		return KCERR_INVALID_TYPE;
	}
	return erSuccess;
}

ECRESULT CopyPropVal(const struct propVal *lpSrc, struct propVal **lppDst,
    struct soap *soap, bool bTruncate)
{
	auto lpDst = s_alloc<struct propVal>(soap);
	auto er = CopyPropVal(lpSrc, lpDst, soap, bTruncate);
	if (er != erSuccess) {
		// there is no sub-alloc when there's an error, so we can remove lpDst
		if (!soap)
			s_free(nullptr, lpDst);
		return er;
	}
	*lppDst = lpDst;
	return erSuccess;
}

ECRESULT CopyPropValArray(const struct propValArray *lpSrc,
    struct propValArray **lppDst, struct soap *soap)
{
	if (lpSrc == nullptr || lppDst == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto lpDst = s_alloc<struct propValArray>(soap);
	if(lpSrc->__size > 0) {
		auto er = CopyPropValArray(lpSrc, lpDst, soap);
		if(er != erSuccess)
			return er;
	}else {
		lpDst->__ptr = NULL;
		lpDst->__size = 0;
	}
	*lppDst = lpDst;
	return erSuccess;
}

ECRESULT CopyPropValArray(const struct propValArray *lpSrc,
    struct propValArray *lpDst, struct soap *soap)
{
	if (lpSrc == nullptr)
		return KCERR_INVALID_PARAMETER;
	lpDst->__ptr = s_alloc<struct propVal>(soap, lpSrc->__size);
	lpDst->__size = lpSrc->__size;

	for (gsoap_size_t i = 0; i < lpSrc->__size; ++i) {
		auto er = CopyPropVal(&lpSrc->__ptr[i], &lpDst->__ptr[i], soap);
		if(er != erSuccess) {
			if (!soap) {
				s_free(nullptr, lpDst->__ptr);
				lpDst->__ptr = NULL;
			}
			lpDst->__size = 0;
			return er;
		}
	}
	return erSuccess;
}

ECRESULT CopyRestrictTable(struct soap *soap,
    const struct restrictTable *lpSrc, struct restrictTable **lppDst)
{
	if (lpSrc == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto lpDst = s_alloc<struct restrictTable>(soap);
	lpDst->ulType = lpSrc->ulType;

	switch(lpSrc->ulType) {
	case RES_OR:
		if (lpSrc->lpOr == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->lpOr = s_alloc<restrictOr>(soap);
		lpDst->lpOr->__ptr = s_alloc<restrictTable *>(soap, lpSrc->lpOr->__size);
		lpDst->lpOr->__size = lpSrc->lpOr->__size;
		memset(lpDst->lpOr->__ptr, 0, sizeof(restrictTable *) * lpSrc->lpOr->__size);

		for (gsoap_size_t i = 0; i < lpSrc->lpOr->__size; ++i) {
			auto er = CopyRestrictTable(soap, lpSrc->lpOr->__ptr[i], &lpDst->lpOr->__ptr[i]);
			if(er != erSuccess)
				return er;
		}
		break;
	case RES_AND:
		if(lpSrc->lpAnd == NULL)
			return KCERR_INVALID_TYPE;
		lpDst->lpAnd = s_alloc<restrictAnd>(soap);
		lpDst->lpAnd->__ptr = s_alloc<restrictTable *>(soap, lpSrc->lpAnd->__size);
		lpDst->lpAnd->__size = lpSrc->lpAnd->__size;
		memset(lpDst->lpAnd->__ptr, 0, sizeof(restrictTable *) * lpSrc->lpAnd->__size);

		for (gsoap_size_t i = 0; i < lpSrc->lpAnd->__size; ++i) {
			auto er = CopyRestrictTable(soap, lpSrc->lpAnd->__ptr[i], &lpDst->lpAnd->__ptr[i]);
			if(er != erSuccess)
				return er;
		}
		break;
	case RES_NOT: {
		lpDst->lpNot = s_alloc<restrictNot>(soap);
		auto er = CopyRestrictTable(soap, lpSrc->lpNot->lpNot, &lpDst->lpNot->lpNot);
		if(er != erSuccess)
			return er;
		break;
	}
	case RES_CONTENT:
		lpDst->lpContent = s_alloc<restrictContent>(soap);
		lpDst->lpContent->ulFuzzyLevel = lpSrc->lpContent->ulFuzzyLevel;
		lpDst->lpContent->ulPropTag = lpSrc->lpContent->ulPropTag;

		if(lpSrc->lpContent->lpProp) {
			auto er = CopyPropVal(lpSrc->lpContent->lpProp, &lpDst->lpContent->lpProp, soap);
			if(er != erSuccess)
				return er;
		}
		break;
	case RES_PROPERTY: {
		lpDst->lpProp = s_alloc<restrictProp>(soap);
		lpDst->lpProp->ulType = lpSrc->lpProp->ulType;
		lpDst->lpProp->ulPropTag = lpSrc->lpProp->ulPropTag;
		auto er = CopyPropVal(lpSrc->lpProp->lpProp, &lpDst->lpProp->lpProp, soap);
		if(er != erSuccess)
			return er;
		break;
	}
	case RES_COMPAREPROPS:
		lpDst->lpCompare = s_alloc<restrictCompare>(soap);
		lpDst->lpCompare->ulType = lpSrc->lpCompare->ulType;
		lpDst->lpCompare->ulPropTag1 = lpSrc->lpCompare->ulPropTag1;
		lpDst->lpCompare->ulPropTag2 = lpSrc->lpCompare->ulPropTag2;
		break;
	case RES_BITMASK:
		lpDst->lpBitmask = s_alloc<restrictBitmask>(soap);
		lpDst->lpBitmask->ulMask = lpSrc->lpBitmask->ulMask;
		lpDst->lpBitmask->ulPropTag = lpSrc->lpBitmask->ulPropTag;
		lpDst->lpBitmask->ulType = lpSrc->lpBitmask->ulType;
		break;
	case RES_SIZE:
		lpDst->lpSize = s_alloc<restrictSize>(soap);
		lpDst->lpSize->ulPropTag = lpSrc->lpSize->ulPropTag;
		lpDst->lpSize->ulType = lpSrc->lpSize->ulType;
		lpDst->lpSize->cb = lpSrc->lpSize->cb;
		break;
	case RES_EXIST:
		lpDst->lpExist = s_alloc<restrictExist>(soap);
		lpDst->lpExist->ulPropTag = lpSrc->lpExist->ulPropTag;
		break;
	case RES_COMMENT: {
		lpDst->lpComment = s_alloc<restrictComment>(soap);
		auto er = CopyPropValArray(&lpSrc->lpComment->sProps, &lpDst->lpComment->sProps, soap);
		if (er != erSuccess)
			return er;
		er = CopyRestrictTable(soap, lpSrc->lpComment->lpResTable, &lpDst->lpComment->lpResTable);
		if(er != erSuccess)
			return er;
		break;
	}
	case RES_SUBRESTRICTION: {
	    lpDst->lpSub = s_alloc<restrictSub>(soap);
	    lpDst->lpSub->ulSubObject = lpSrc->lpSub->ulSubObject;
		auto er = CopyRestrictTable(soap, lpSrc->lpSub->lpSubObject, &lpDst->lpSub->lpSubObject);
		if(er != erSuccess)
			return er;
	        break;
	}
	default:
		return KCERR_INVALID_TYPE;
	}

	*lppDst = lpDst;
	return erSuccess;
}

ECRESULT FreePropValArray(struct propValArray *lpPropValArray, bool bFreeBase)
{
	if (lpPropValArray == nullptr)
		return erSuccess;
	for (gsoap_size_t i = 0; i < lpPropValArray->__size; ++i)
		FreePropVal(&(lpPropValArray->__ptr[i]), false);
	s_free(nullptr, lpPropValArray->__ptr);
	if (bFreeBase)
		s_free(nullptr, lpPropValArray);
	return erSuccess;
}

ECRESULT CopyEntryId(struct soap *soap, entryId* lpSrc, entryId** lppDst)
{
	if (lpSrc == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto lpDst = soap_new_entryId(soap);
	lpDst->__size = lpSrc->__size;

	if(lpSrc->__size > 0) {
		lpDst->__ptr = soap_new_unsignedByte(soap, lpSrc->__size);
		memcpy(lpDst->__ptr, lpSrc->__ptr, lpSrc->__size);
	} else {
		lpDst->__ptr = NULL;
	}

	*lppDst = lpDst;
	return erSuccess;
}

ECRESULT CopyEntryList(struct soap *soap, struct entryList *lpSrc, struct entryList **lppDst)
{
	if (lpSrc == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto lpDst = s_alloc<entryList>(soap);
	lpDst->__size = lpSrc->__size;
	if(lpSrc->__size > 0)
		lpDst->__ptr = soap_new_entryId(soap, lpSrc->__size);
	else
		lpDst->__ptr = NULL;

	for (unsigned int i = 0; i < lpSrc->__size; ++i) {
		lpDst->__ptr[i].__size = lpSrc->__ptr[i].__size;
		lpDst->__ptr[i].__ptr  = soap_new_unsignedByte(soap, lpSrc->__ptr[i].__size);
		memcpy(lpDst->__ptr[i].__ptr, lpSrc->__ptr[i].__ptr, lpSrc->__ptr[i].__size);
	}

	*lppDst = lpDst;
	return erSuccess;
}

ECRESULT FreeEntryList(struct entryList *lpEntryList, bool bFreeBase)
{
	if(lpEntryList == NULL)
		return erSuccess;
	for (unsigned int i = 0; i < lpEntryList->__size; ++i)
		soap_del_entryId(&lpEntryList->__ptr[i]);
	s_free(nullptr, lpEntryList->__ptr);
	if (bFreeBase)
		s_free(nullptr, lpEntryList);
	return erSuccess;
}

ECRESULT FreeNotificationStruct(notification *lpNotification, bool bFreeBase)
{
	if(lpNotification == NULL)
		return erSuccess;

	if(lpNotification->obj != NULL){
		soap_del_PointerTopropTagArray(&lpNotification->obj->pPropTagArray);
		soap_del_PointerToentryId(&lpNotification->obj->pEntryId);
		soap_del_PointerToentryId(&lpNotification->obj->pOldId);
		soap_del_PointerToentryId(&lpNotification->obj->pOldParentId);
		soap_del_PointerToentryId(&lpNotification->obj->pParentId);
		s_free(nullptr, lpNotification->obj);
	}

	if(lpNotification->tab != NULL) {
		if(lpNotification->tab->pRow != NULL)
			FreePropValArray(lpNotification->tab->pRow, true);
		FreePropVal(&lpNotification->tab->propIndex, false);
		FreePropVal(&lpNotification->tab->propPrior, false);
		s_free(nullptr, lpNotification->tab);
	}

	if (lpNotification->newmail != NULL) {
		soap_del_string(&lpNotification->newmail->lpszMessageClass);
		soap_del_PointerToentryId(&lpNotification->newmail->pEntryId);
		soap_del_PointerToentryId(&lpNotification->newmail->pParentId);
		s_free(nullptr, lpNotification->newmail);
	}
	if(lpNotification->ics != NULL) {
		soap_del_PointerToentryId(&lpNotification->ics->pSyncState);
		s_free(nullptr, lpNotification->ics);
	}
	if(bFreeBase)
		s_free(nullptr, lpNotification);
	return erSuccess;
}

// Make a copy of the struct notification.
ECRESULT CopyNotificationStruct(struct soap *soap,
    const notification *lpNotification, notification &rNotifyTo)
{
	if (lpNotification == NULL)
		return KCERR_INVALID_PARAMETER;
	rNotifyTo = notification();
	rNotifyTo.ulEventType	= lpNotification->ulEventType;
	rNotifyTo.ulConnection	= lpNotification->ulConnection;

	if(lpNotification->tab != NULL) {
		rNotifyTo.tab =	s_alloc<notificationTable>(soap);
		rNotifyTo.tab->hResult = lpNotification->tab->hResult;
		rNotifyTo.tab->ulTableEvent = lpNotification->tab->ulTableEvent;
		CopyPropVal(&lpNotification->tab->propIndex, &rNotifyTo.tab->propIndex, soap);
		CopyPropVal(&lpNotification->tab->propPrior, &rNotifyTo.tab->propPrior, soap);
		// Ignore errors
		CopyPropValArray(lpNotification->tab->pRow, &rNotifyTo.tab->pRow, soap);
		rNotifyTo.tab->ulObjType = lpNotification->tab->ulObjType;
	}else if(lpNotification->obj != NULL) {
		rNotifyTo.obj = s_alloc<notificationObject>(soap);
		rNotifyTo.obj->ulObjType		= lpNotification->obj->ulObjType;
		// Ignore errors, sometimes nothing to copy
		CopyEntryId(soap, lpNotification->obj->pEntryId, &rNotifyTo.obj->pEntryId);
		CopyEntryId(soap, lpNotification->obj->pParentId, &rNotifyTo.obj->pParentId);
		CopyEntryId(soap, lpNotification->obj->pOldId, &rNotifyTo.obj->pOldId);
		CopyEntryId(soap, lpNotification->obj->pOldParentId, &rNotifyTo.obj->pOldParentId);
		CopyPropTagArray(soap, lpNotification->obj->pPropTagArray, &rNotifyTo.obj->pPropTagArray);
	}else if(lpNotification->newmail != NULL){
		rNotifyTo.newmail = s_alloc<notificationNewMail>(soap);
		// Ignore errors, sometimes nothing to copy
		CopyEntryId(soap, lpNotification->newmail->pEntryId, &rNotifyTo.newmail->pEntryId);
		CopyEntryId(soap, lpNotification->newmail->pParentId, &rNotifyTo.newmail->pParentId);
		rNotifyTo.newmail->ulMessageFlags	= lpNotification->newmail->ulMessageFlags;
		if(lpNotification->newmail->lpszMessageClass)
			rNotifyTo.newmail->lpszMessageClass = soap_strdup(soap, lpNotification->newmail->lpszMessageClass);
	}else if(lpNotification->ics != NULL){
		rNotifyTo.ics = s_alloc<notificationICS>(soap);
		// We use CopyEntryId as it just copied binary data
		CopyEntryId(soap, lpNotification->ics->pSyncState, &rNotifyTo.ics->pSyncState);
	}
	return erSuccess;
}

ECRESULT FreeNotificationArrayStruct(notificationArray *lpNotifyArray, bool bFreeBase)
{
	if(lpNotifyArray == NULL)
		return erSuccess;
	for (gsoap_size_t i = 0; i < lpNotifyArray->__size; ++i)
		FreeNotificationStruct(&lpNotifyArray->__ptr[i], false);
	s_free(nullptr, lpNotifyArray->__ptr);
	if(bFreeBase)
		s_free(nullptr, lpNotifyArray);
	else
		lpNotifyArray->__size = 0;
	return erSuccess;
}

ECRESULT CopyNotificationArrayStruct(notificationArray *lpNotifyArrayFrom, notificationArray *lpNotifyArrayTo)
{
	if (lpNotifyArrayFrom == NULL)
		return KCERR_INVALID_PARAMETER;
	if (lpNotifyArrayFrom->__size > 0)
		lpNotifyArrayTo->__ptr = s_alloc<notification>(nullptr, lpNotifyArrayFrom->__size);
	else
		lpNotifyArrayTo->__ptr = NULL;
	lpNotifyArrayTo->__size = lpNotifyArrayFrom->__size;
	for (gsoap_size_t i = 0; i < lpNotifyArrayFrom->__size; ++i)
		CopyNotificationStruct(NULL, &lpNotifyArrayFrom->__ptr[i], lpNotifyArrayTo->__ptr[i]);
	return erSuccess;
}

ECRESULT CopyRightsArrayToSoap(struct soap *soap, struct rightsArray *lpRightsArraySrc, struct rightsArray **lppRightsArrayDst)
{
	if (soap == nullptr || lpRightsArraySrc == nullptr ||
	    lppRightsArrayDst == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto lpRightsArrayDst = s_alloc<struct rightsArray>(soap);
	lpRightsArrayDst->__size = lpRightsArraySrc->__size;
	lpRightsArrayDst->__ptr = s_alloc<struct rights>(soap, lpRightsArraySrc->__size);

	for (gsoap_size_t i = 0; i < lpRightsArraySrc->__size; ++i) {
		lpRightsArrayDst->__ptr[i] = lpRightsArraySrc->__ptr[i];
		lpRightsArrayDst->__ptr[i].sUserId.__ptr = soap_new_unsignedByte(soap, lpRightsArrayDst->__ptr[i].sUserId.__size);
		memcpy(lpRightsArrayDst->__ptr[i].sUserId.__ptr, lpRightsArraySrc->__ptr[i].sUserId.__ptr, lpRightsArraySrc->__ptr[i].sUserId.__size);
	}
	
	*lppRightsArrayDst = lpRightsArrayDst;
	return erSuccess;
}

ECRESULT FreeRightsArray(struct rightsArray *lpRights)
{
	if(lpRights == NULL)
		return erSuccess;
	for (gsoap_size_t i = 0; i < lpRights->__size; ++i)
		soap_del_entryId(&lpRights->__ptr[i].sUserId);
	s_free(nullptr, lpRights->__ptr);
	s_free(nullptr, lpRights);
	return erSuccess;
}

static const struct propVal *
SpropValFindPropVal(const struct propValArray *lpsPropValArray,
    unsigned int ulPropTag)
{
	if(PROP_TYPE(ulPropTag) == PT_ERROR)
		return NULL;
	for (gsoap_size_t i = 0; i < lpsPropValArray->__size; ++i)
		if(lpsPropValArray->__ptr[i].ulPropTag == ulPropTag ||
			(PROP_ID(lpsPropValArray->__ptr[i].ulPropTag) == PROP_ID(ulPropTag) &&
			 PROP_TYPE(ulPropTag) == PT_UNSPECIFIED &&
			 PROP_TYPE(lpsPropValArray->__ptr[i].ulPropTag) != PT_ERROR) )
			return &lpsPropValArray->__ptr[i];

	return NULL;
}

// NOTE: PropValArray 2 overruled PropValArray 1, except if proptype is PT_ERROR
ECRESULT MergePropValArray(struct soap *soap,
    const struct propValArray *lpsPropValArray1,
    const struct propValArray *lpsPropValArray2,
    struct propValArray *lpPropValArrayNew)
{
	lpPropValArrayNew->__ptr = s_alloc<struct propVal>(soap, lpsPropValArray1->__size + lpsPropValArray2->__size);
	lpPropValArrayNew->__size = 0;

	for (gsoap_size_t i = 0; i < lpsPropValArray1->__size; ++i) {
		auto lpsPropVal = SpropValFindPropVal(lpsPropValArray2, lpsPropValArray1->__ptr[i].ulPropTag);
		if(lpsPropVal == NULL)
			lpsPropVal = &lpsPropValArray1->__ptr[i];
		auto er = CopyPropVal(lpsPropVal, &lpPropValArrayNew->__ptr[lpPropValArrayNew->__size], soap);
		if(er != erSuccess)
			return er;
		++lpPropValArrayNew->__size;
	}

	//Merge items
	for (gsoap_size_t i = 0; i < lpsPropValArray2->__size; ++i) {
		auto lpsPropVal = SpropValFindPropVal(lpPropValArrayNew, lpsPropValArray2->__ptr[i].ulPropTag);
		if(lpsPropVal != NULL)
			continue; // Already exist
		auto er = CopyPropVal(&lpsPropValArray2->__ptr[i], &lpPropValArrayNew->__ptr[lpPropValArrayNew->__size], soap);
		if(er != erSuccess)
			return er;
		++lpPropValArrayNew->__size;
	}
	return erSuccess;
}

ECRESULT CopySearchCriteria(struct soap *soap,
    const struct searchCriteria *lpSrc, struct searchCriteria **lppDst)
{
	if (lpSrc == nullptr)
		return KCERR_NOT_FOUND;

	ECRESULT er = erSuccess;
	auto lpDst = s_alloc<searchCriteria>(nullptr);
	if(lpSrc->lpRestrict) {
		er = CopyRestrictTable(soap, lpSrc->lpRestrict, &lpDst->lpRestrict);
		if (er != erSuccess)
			goto exit;
    } else {
        lpDst->lpRestrict = NULL;
    }

	if(lpSrc->lpFolders) {
		er = CopyEntryList(soap, lpSrc->lpFolders, &lpDst->lpFolders);
		if (er != erSuccess)
			goto exit;
    } else {
        lpDst->lpFolders = NULL;
    }

	lpDst->ulFlags = lpSrc->ulFlags;
	*lppDst = lpDst;
exit:
	if (er != erSuccess && lpDst != NULL) {
		FreeRestrictTable(lpDst->lpRestrict, true);
		FreeEntryList(lpDst->lpFolders, true);
		s_free(nullptr, lpDst);
	}
	return er;
}

ECRESULT FreeSearchCriteria(struct searchCriteria *lpSearchCriteria)
{
	if (lpSearchCriteria == nullptr)
		return erSuccess;
	if(lpSearchCriteria->lpRestrict)
		FreeRestrictTable(lpSearchCriteria->lpRestrict);
	if(lpSearchCriteria->lpFolders)
		FreeEntryList(lpSearchCriteria->lpFolders);
	s_free(nullptr, lpSearchCriteria);
	return erSuccess;
}

/**
 * Copy extra user details into propmap, (only the string values)
 *
 */
static ECRESULT CopyAnonymousDetailsToSoap(struct soap *soap,
    const objectdetails_t &details, bool bCopyBinary,
    struct propmapPairArray **lppsoapPropmap,
    struct propmapMVPairArray **lppsoapMVPropmap)
{
	struct propmapPairArray *lpsoapPropmap = NULL;
	struct propmapMVPairArray *lpsoapMVPropmap = NULL;
	property_map propmap = details.GetPropMapAnonymous();
	property_mv_map propmvmap = details.GetPropMapListAnonymous(); 
	unsigned int j = 0;

	if (!propmap.empty()) {
		lpsoapPropmap = s_alloc<struct propmapPairArray>(soap, 1);
		lpsoapPropmap->__size = 0;
		lpsoapPropmap->__ptr = s_alloc<struct propmapPair>(soap, propmap.size());
		for (const auto &iter : propmap) {
			if (PROP_TYPE(iter.first) == PT_BINARY && bCopyBinary) {
				auto strData = base64_encode(iter.second.data(), iter.second.size());
				lpsoapPropmap->__ptr[lpsoapPropmap->__size].ulPropId = iter.first;
				lpsoapPropmap->__ptr[lpsoapPropmap->__size++].lpszValue = soap_strdup(soap, strData.c_str());
				continue;
			}
			if (PROP_TYPE(iter.first) != PT_STRING8 && PROP_TYPE(iter.first) != PT_UNICODE)
				continue;
			lpsoapPropmap->__ptr[lpsoapPropmap->__size].ulPropId = iter.first;
			lpsoapPropmap->__ptr[lpsoapPropmap->__size++].lpszValue = soap_strdup(soap, iter.second.c_str());
		}
	}

	if (!propmvmap.empty()) {
		lpsoapMVPropmap = s_alloc<struct propmapMVPairArray>(soap, 1);
		lpsoapMVPropmap->__size = 0;
		lpsoapMVPropmap->__ptr = s_alloc<struct propmapMVPair>(soap, propmvmap.size());
		for (const auto &iter : propmvmap) {
			if (PROP_TYPE(iter.first) == PT_MV_BINARY && bCopyBinary) {
				j = 0;
				lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].ulPropId = iter.first;
				lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].sValues.__size = iter.second.size();
				lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].sValues.__ptr  = soap_new_string(soap, lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].sValues.__size);
				for (const auto &entry : iter.second) {
					auto strData = base64_encode(entry.data(), entry.size());
					lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].sValues.__ptr[j++] = soap_strdup(soap, strData.c_str());
				}
				++lpsoapMVPropmap->__size;
				continue;
			}

			if (PROP_TYPE(iter.first) != PT_MV_STRING8 && PROP_TYPE(iter.first) != PT_MV_UNICODE)
				continue;

			j = 0;
			lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].ulPropId = iter.first;
			lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].sValues.__size = iter.second.size();
			lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].sValues.__ptr  = soap_new_string(soap, lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].sValues.__size);
			for (const auto &entry : iter.second)
				lpsoapMVPropmap->__ptr[lpsoapMVPropmap->__size].sValues.__ptr[j++] = soap_strdup(soap, entry.c_str());
			++lpsoapMVPropmap->__size;
		}
	}

	if (lppsoapPropmap)
		*lppsoapPropmap = lpsoapPropmap;
	if (lppsoapMVPropmap)
		*lppsoapMVPropmap = lpsoapMVPropmap;
	return erSuccess;
}

static ECRESULT
CopyAnonymousDetailsFromSoap(struct propmapPairArray *lpsoapPropmap,
    struct propmapMVPairArray *lpsoapMVPropmap, objectdetails_t *details)
{
	if (lpsoapPropmap) {
		for (gsoap_size_t i = 0; i < lpsoapPropmap->__size; ++i)
			if (PROP_TYPE(lpsoapPropmap->__ptr[i].ulPropId) == PT_BINARY)
				details->SetPropString(static_cast<property_key_t>(lpsoapPropmap->__ptr[i].ulPropId), base64_decode(lpsoapPropmap->__ptr[i].lpszValue));
			else if (PROP_TYPE(lpsoapPropmap->__ptr[i].ulPropId) == PT_STRING8)
				details->SetPropString(static_cast<property_key_t>(lpsoapPropmap->__ptr[i].ulPropId), lpsoapPropmap->__ptr[i].lpszValue);
	}

	if (lpsoapMVPropmap)
		for (gsoap_size_t i = 0; i < lpsoapMVPropmap->__size; ++i) {
			details->SetPropListString(static_cast<property_key_t>(lpsoapMVPropmap->__ptr[i].ulPropId), {});
			for (gsoap_size_t j = 0; j < lpsoapMVPropmap->__ptr[i].sValues.__size; ++j)
				if (PROP_TYPE(lpsoapMVPropmap->__ptr[i].ulPropId) == PT_MV_BINARY)
					details->AddPropString(static_cast<property_key_t>(lpsoapMVPropmap->__ptr[i].ulPropId), base64_decode(lpsoapMVPropmap->__ptr[i].sValues.__ptr[j]));
				else
					details->AddPropString(static_cast<property_key_t>(lpsoapMVPropmap->__ptr[i].ulPropId), lpsoapMVPropmap->__ptr[i].sValues.__ptr[j]);
		}

	return erSuccess;
}

ECRESULT CopyUserDetailsToSoap(unsigned int ulId, entryId *lpUserEid, const objectdetails_t &details, bool bCopyBinary, struct soap *soap, struct user *lpUser)
{
	const objectclass_t objClass = details.GetClass();

	// assert(OBJECTCLASS_TYPE(objClass) == OBJECTTYPE_MAILUSER);
	lpUser->ulUserId = ulId;
	lpUser->lpszUsername = soap_strdup(soap, details.GetPropString(OB_PROP_S_LOGIN).c_str());
	lpUser->ulIsNonActive = 0;
	lpUser->ulObjClass = objClass;
	lpUser->lpszMailAddress = soap_strdup(soap, details.GetPropString(OB_PROP_S_EMAIL).c_str());
	lpUser->lpszFullName = soap_strdup(soap, details.GetPropString(OB_PROP_S_FULLNAME).c_str());
	lpUser->ulIsAdmin = details.GetPropInt(OB_PROP_I_ADMINLEVEL);
	lpUser->lpszPassword = const_cast<char *>("");
	lpUser->lpszServername = soap_strdup(soap, details.GetPropString(OB_PROP_S_SERVERNAME).c_str());
	lpUser->ulIsABHidden = details.GetPropBool(OB_PROP_B_AB_HIDDEN);
	lpUser->ulCapacity = details.GetPropInt(OB_PROP_I_RESOURCE_CAPACITY);
	lpUser->lpsPropmap = NULL;
	lpUser->lpsMVPropmap = NULL;
	CopyAnonymousDetailsToSoap(soap, details, bCopyBinary, &lpUser->lpsPropmap, &lpUser->lpsMVPropmap);
	// Lazy copy
	lpUser->sUserId.__size = lpUserEid->__size;
	lpUser->sUserId.__ptr = lpUserEid->__ptr;
	return erSuccess;
}

ECRESULT CopyUserDetailsFromSoap(struct user *lpUser,
    std::string *lpstrExternId, objectdetails_t *details, struct soap *soap)
{
	if (lpUser->lpszUsername)
		details->SetPropString(OB_PROP_S_LOGIN, lpUser->lpszUsername);
	if (lpUser->lpszMailAddress)
		details->SetPropString(OB_PROP_S_EMAIL, lpUser->lpszMailAddress);
	if (lpUser->ulIsAdmin != (ULONG)-1)
		details->SetPropInt(OB_PROP_I_ADMINLEVEL, lpUser->ulIsAdmin);
	if (lpUser->ulObjClass != (ULONG)-1)
		details->SetClass(static_cast<objectclass_t>(lpUser->ulObjClass));
	if (lpUser->lpszFullName)
		details->SetPropString(OB_PROP_S_FULLNAME, lpUser->lpszFullName);
	if (lpUser->lpszPassword)
		details->SetPropString(OB_PROP_S_PASSWORD, lpUser->lpszPassword);
	if (lpstrExternId)
		details->SetPropObject(OB_PROP_O_EXTERNID, objectid_t(*lpstrExternId, details->GetClass()));
	if (lpUser->lpszServername)
		details->SetPropString(OB_PROP_S_SERVERNAME, lpUser->lpszServername);
	if (lpUser->ulIsABHidden != (ULONG)-1)
		details->SetPropBool(OB_PROP_B_AB_HIDDEN, !!lpUser->ulIsABHidden);
	if (lpUser->ulCapacity != (ULONG)-1)
		details->SetPropInt(OB_PROP_I_RESOURCE_CAPACITY, lpUser->ulCapacity);
	CopyAnonymousDetailsFromSoap(lpUser->lpsPropmap, lpUser->lpsMVPropmap, details);
	return erSuccess;
}

ECRESULT CopyGroupDetailsToSoap(unsigned int ulId, entryId *lpGroupEid, const objectdetails_t &details, bool bCopyBinary, struct soap *soap, struct group *lpGroup)
{
	// assert(OBJECTCLASS_TYPE(details.GetClass()) == OBJECTTYPE_DISTLIST);
	lpGroup->ulGroupId = ulId;
	lpGroup->lpszGroupname = soap_strdup(soap, details.GetPropString(OB_PROP_S_LOGIN).c_str());
	lpGroup->lpszFullname = soap_strdup(soap, details.GetPropString(OB_PROP_S_FULLNAME).c_str());
	lpGroup->lpszFullEmail = soap_strdup(soap, details.GetPropString(OB_PROP_S_EMAIL).c_str());
	lpGroup->ulIsABHidden = details.GetPropBool(OB_PROP_B_AB_HIDDEN);
	lpGroup->lpsPropmap = NULL;
	lpGroup->lpsMVPropmap = NULL;
	CopyAnonymousDetailsToSoap(soap, details, bCopyBinary, &lpGroup->lpsPropmap, &lpGroup->lpsMVPropmap);
	// Lazy copy
	lpGroup->sGroupId.__size = lpGroupEid->__size;
	lpGroup->sGroupId.__ptr = lpGroupEid->__ptr;
	return erSuccess;
}

ECRESULT CopyGroupDetailsFromSoap(struct group *lpGroup,
    std::string *lpstrExternId, objectdetails_t *details, struct soap *soap)
{
	if (lpGroup->lpszGroupname)
		details->SetPropString(OB_PROP_S_LOGIN, lpGroup->lpszGroupname);
	if (lpGroup->lpszFullname)
		details->SetPropString(OB_PROP_S_FULLNAME, lpGroup->lpszFullname);
	if (lpGroup->lpszFullEmail)
		details->SetPropString(OB_PROP_S_EMAIL, lpGroup->lpszFullEmail);
	if (lpstrExternId)
		details->SetPropObject(OB_PROP_O_EXTERNID, objectid_t(*lpstrExternId, details->GetClass()));
	if (lpGroup->ulIsABHidden != (ULONG)-1)
		details->SetPropBool(OB_PROP_B_AB_HIDDEN, !!lpGroup->ulIsABHidden);
	CopyAnonymousDetailsFromSoap(lpGroup->lpsPropmap, lpGroup->lpsMVPropmap, details);
	return erSuccess;
}

ECRESULT CopyCompanyDetailsToSoap(unsigned int ulId, entryId *lpCompanyEid, unsigned int ulAdmin, entryId *lpAdminEid, const objectdetails_t &details, bool bCopyBinary, struct soap *soap, struct company *lpCompany)
{
	// assert(details.GetClass() == CONTAINER_COMPANY);
	lpCompany->ulCompanyId = ulId;
	lpCompany->lpszCompanyname = soap_strdup(soap, details.GetPropString(OB_PROP_S_FULLNAME).c_str());
	lpCompany->ulAdministrator = ulAdmin;
	lpCompany->lpszServername = soap_strdup(soap, details.GetPropString(OB_PROP_S_SERVERNAME).c_str());
	lpCompany->ulIsABHidden = details.GetPropBool(OB_PROP_B_AB_HIDDEN);
	lpCompany->lpsPropmap = NULL;
	lpCompany->lpsMVPropmap = NULL;
	CopyAnonymousDetailsToSoap(soap, details, bCopyBinary, &lpCompany->lpsPropmap, &lpCompany->lpsMVPropmap);
	// Lazy copy
	lpCompany->sCompanyId.__size = lpCompanyEid->__size;
	lpCompany->sCompanyId.__ptr = lpCompanyEid->__ptr;
	lpCompany->sAdministrator.__size = lpAdminEid->__size;
	lpCompany->sAdministrator.__ptr = lpAdminEid->__ptr;
	return erSuccess;
}

ECRESULT CopyCompanyDetailsFromSoap(struct company *lpCompany,
    std::string *lpstrExternId, unsigned int ulAdmin, objectdetails_t *details,
    struct soap *soap)
{
	if (lpCompany->lpszCompanyname)
		details->SetPropString(OB_PROP_S_FULLNAME, lpCompany->lpszCompanyname);
	if (lpCompany->lpszServername)
		details->SetPropString(OB_PROP_S_SERVERNAME, lpCompany->lpszServername);
	if (lpstrExternId)
		details->SetPropObject(OB_PROP_O_EXTERNID, objectid_t(*lpstrExternId, details->GetClass()));
	if (ulAdmin)
		details->SetPropInt(OB_PROP_I_SYSADMIN, ulAdmin);
	if (lpCompany->ulIsABHidden != (ULONG)-1)
		details->SetPropBool(OB_PROP_B_AB_HIDDEN, !!lpCompany->ulIsABHidden);
	CopyAnonymousDetailsFromSoap(lpCompany->lpsPropmap, lpCompany->lpsMVPropmap, details);
	return erSuccess;
}

DynamicPropValArray::DynamicPropValArray(struct soap *soap,
    unsigned int ulHint) :
	m_soap(soap), m_ulCapacity(ulHint)
{
    m_lpPropVals = s_alloc<struct propVal>(m_soap, m_ulCapacity);
}

DynamicPropValArray::~DynamicPropValArray()
{
	if (m_lpPropVals == nullptr || m_soap != nullptr)
		return;
	for (unsigned int i = 0; i < m_ulPropCount; ++i)
		FreePropVal(&m_lpPropVals[i], false);
	s_free(nullptr, m_lpPropVals);
}
    
ECRESULT DynamicPropValArray::AddPropVal(struct propVal &propVal)
{
    if(m_ulCapacity == m_ulPropCount) {
        if(m_ulCapacity == 0)
			++m_ulCapacity;
		auto er = Resize(m_ulCapacity * 2);
        if(er != erSuccess)
			return er;
    }
	auto er = CopyPropVal(&propVal, &m_lpPropVals[m_ulPropCount], m_soap);
    if(er != erSuccess)
		return er;
	++m_ulPropCount;
    return erSuccess;
}

ECRESULT DynamicPropValArray::GetPropValArray(struct propValArray *lpPropValArray, bool release)
{
    lpPropValArray->__size = m_ulPropCount;
    lpPropValArray->__ptr = m_lpPropVals; // Transfer ownership to the caller
	if (release) {
		m_lpPropVals = nullptr;					// We don't own these anymore
		m_ulPropCount = 0;
		m_ulCapacity = 0;
	}
	return erSuccess;
}

ECRESULT DynamicPropValArray::Resize(unsigned int ulSize)
{
	if (ulSize < m_ulCapacity)
		return KCERR_INVALID_PARAMETER;
	auto lpNew = s_alloc_nothrow<struct propVal>(m_soap, ulSize);
	if (lpNew == NULL)
		return KCERR_NOT_ENOUGH_MEMORY;
    
	for (unsigned int i = 0; i < m_ulPropCount; ++i) {
		auto er = CopyPropVal(&m_lpPropVals[i], &lpNew[i], m_soap);
        if(er != erSuccess)
			return er;
	}
    
    if(!m_soap) {
		for (unsigned int i = 0; i < m_ulPropCount; ++i)
			FreePropVal(&m_lpPropVals[i], false);
		s_free(nullptr, m_lpPropVals);
	}
    m_lpPropVals = lpNew;
    m_ulCapacity = ulSize;
	return erSuccess;
}		

DynamicPropTagArray::DynamicPropTagArray(struct soap *soap)
{
    m_soap = soap;
}

ECRESULT DynamicPropTagArray::AddPropTag(unsigned int ulPropTag) {
	m_lstPropTags.emplace_back(ulPropTag);
    return erSuccess;
}

BOOL DynamicPropTagArray::HasPropTag(unsigned int ulPropTag) const {
	return std::find(m_lstPropTags.begin(), m_lstPropTags.end(), ulPropTag) != m_lstPropTags.end();
}

ECRESULT DynamicPropTagArray::GetPropTagArray(struct propTagArray *lpsPropTagArray) {
	size_t n = 0;
    
    lpsPropTagArray->__size = m_lstPropTags.size();
	lpsPropTagArray->__ptr = soap_new_unsignedInt(m_soap, lpsPropTagArray->__size);
	for (auto tag : m_lstPropTags)
		lpsPropTagArray->__ptr[n++] = tag;
	return erSuccess;
}

/**
 * Calculate the propValArray size
 *
 * @param[in] lpSrc Pointer to a propVal array object
 *
 * @return the size of the object. If there is an error, object size is zero.
 */
size_t PropValArraySize(const struct propValArray *lpSrc)
{
	if (lpSrc == NULL)
		return 0;
	size_t ulSize = sizeof(struct propValArray) * lpSrc->__size;
	for (gsoap_size_t i = 0; i < lpSrc->__size; ++i)
		ulSize += PropSize(&lpSrc->__ptr[i]);
	return ulSize;
}

/**
 * Calculate the restrict table size
 *
 * @param[in] lpSrc Pointer to a restrict table object
 * @return the size of the object. If there is an error, object size is zero.
 */
size_t RestrictTableSize(const struct restrictTable *lpSrc)
{
	size_t ulSize = 0;
	if (lpSrc == NULL)
		return 0;

	switch(lpSrc->ulType) {
	case RES_OR:
		ulSize += sizeof(restrictOr);
		for (gsoap_size_t i = 0; i < lpSrc->lpOr->__size; ++i)
			ulSize += RestrictTableSize(lpSrc->lpOr->__ptr[i]);
		break;
	case RES_AND:
		ulSize += sizeof(restrictAnd);
		for (gsoap_size_t i = 0; i < lpSrc->lpAnd->__size; ++i)
			ulSize += RestrictTableSize(lpSrc->lpAnd->__ptr[i]);
		break;
	case RES_NOT:
		ulSize += sizeof(restrictNot);
		ulSize += RestrictTableSize(lpSrc->lpNot->lpNot);
		break;
	case RES_CONTENT:
		ulSize += sizeof(restrictContent);
		if (lpSrc->lpContent->lpProp != nullptr)
			ulSize += PropSize(lpSrc->lpContent->lpProp);
		break;
	case RES_PROPERTY:
		ulSize += sizeof(restrictProp);
		ulSize += PropSize(lpSrc->lpProp->lpProp);
		break;
	case RES_COMPAREPROPS:
		ulSize += sizeof(restrictCompare);
		break;
	case RES_BITMASK:
		ulSize += sizeof(restrictBitmask);
		break;
	case RES_SIZE:
		ulSize += sizeof(restrictSize);
		break;
	case RES_EXIST:
		ulSize += sizeof(restrictExist);
		break;
	case RES_COMMENT:
		ulSize += sizeof(restrictComment) + sizeof(restrictTable);
		ulSize += PropValArraySize(&lpSrc->lpComment->sProps);
		ulSize += RestrictTableSize(lpSrc->lpComment->lpResTable);
		break;
	case RES_SUBRESTRICTION:
		ulSize += sizeof(restrictSub);
		ulSize += RestrictTableSize(lpSrc->lpSub->lpSubObject);
        break;
	default:
		break;
	}
	return ulSize;
}

/**
 * Calculate the size of a list of entries
 *
 * @param[in] lpSrc pointer to a list of entries
 * @return the size of the object. If there is an error, object size is zero.
 */
size_t EntryListSize(const struct entryList *lpSrc)
{
	if (lpSrc == NULL)
		return 0;

	size_t ulSize = sizeof(entryList);
	ulSize += sizeof(entryId) * lpSrc->__size;
	for (unsigned int i = 0; i < lpSrc->__size; ++i)
		ulSize += lpSrc->__ptr[i].__size;
	return ulSize;
}

/**
 * Calculate the size of a proptag array
 *
 * @param[in] pPropTagArray Pointer to a array of property tags
 * @return the size of the object. If there is an error, object size is zero.
 */
size_t PropTagArraySize(const struct propTagArray *pPropTagArray)
{
	return (pPropTagArray)?((sizeof(unsigned int) * pPropTagArray->__size) + sizeof(struct propTagArray)) : 0;
}

/**
 * Calculate the size of the search criteria object
 *
 * @param[in] lpSrc Pointer to a search criteria object.
 * @return the size of the object. If there is an error, object size is zero.
 */
size_t SearchCriteriaSize(const struct searchCriteria *lpSrc)
{
	if (lpSrc == NULL)
		return 0;

	size_t ulSize = sizeof(struct searchCriteria);
	if (lpSrc->lpRestrict != nullptr)
		ulSize += RestrictTableSize(lpSrc->lpRestrict);
	if (lpSrc->lpFolders != nullptr)
		ulSize += EntryListSize(lpSrc->lpFolders);
	return ulSize;
}

/**
 * Calculate the size of a entryid
 *
 * @param[in] lpEntryid Pointer to an entryid object.
 * @return the size of the object. If there is an error, object size is zero.
 */
size_t EntryIdSize(const entryId *lpEntryid)
{
	if(lpEntryid == NULL)
		return 0;
	return sizeof(entryId) + lpEntryid->__size;
}

/**
 * Calculate the size of a notification struct
 *
 * @param[in] lpNotification Pointer to a notification struct.
 * @return the size of the object. If there is an error, object size is zero.
 */
size_t NotificationStructSize(const notification *lpNotification)
{
	if (lpNotification == NULL)
		return 0;

	size_t ulSize = sizeof(notification);
	if(lpNotification->tab != NULL) {
		ulSize += sizeof(notificationTable);
		ulSize += PropSize(&lpNotification->tab->propIndex);
		ulSize += PropSize(&lpNotification->tab->propPrior);
		ulSize += PropValArraySize(lpNotification->tab->pRow);
	}else if(lpNotification->obj != NULL) {
		ulSize += sizeof(notificationObject);
		ulSize += EntryIdSize(lpNotification->obj->pEntryId);
		ulSize += EntryIdSize(lpNotification->obj->pParentId);
		ulSize += EntryIdSize(lpNotification->obj->pOldId);
		ulSize += EntryIdSize(lpNotification->obj->pOldParentId);
		ulSize += PropTagArraySize(lpNotification->obj->pPropTagArray);
	}else if(lpNotification->newmail != NULL){
		ulSize += sizeof(notificationNewMail);
		ulSize += EntryIdSize(lpNotification->newmail->pEntryId);
		ulSize += EntryIdSize(lpNotification->newmail->pParentId);
		if (lpNotification->newmail->lpszMessageClass != nullptr)
			ulSize += (unsigned int)strlen(lpNotification->newmail->lpszMessageClass)+1;
	}else if(lpNotification->ics != NULL){
		ulSize += sizeof(notificationICS);
		ulSize += EntryIdSize(lpNotification->ics->pSyncState);
	}
	return ulSize;
}

/**
 * Calculate the size of a sort order array.
 *
 * @param[in] lpsSortOrder Pointer to a sort order array.
 * @return the size of the object. If there is an error, object size is zero.
 */
size_t SortOrderArraySize(const struct sortOrderArray *lpsSortOrder)
{
	if (lpsSortOrder == NULL)
		return 0;	
	return sizeof(struct sortOrder) * lpsSortOrder->__size;
}

/**
 * Normalize the property tag to the local property type depending on -DUNICODE:
 *
 * With -DUNICODE, the function:
 * - replaces PT_STRING8 with PT_UNICODE
 * - replaces PT_MV_STRING8 with PT_MV_UNICODE
 * Without -DUNICODE, it:
 * - replaces PT_UNICODE with PT_STRING8
 * - replaces PT_MV_UNICODE with PT_MV_STRING8
 */
ULONG NormalizePropTag(ULONG ulPropTag)
{
	if ((PROP_TYPE(ulPropTag) == PT_STRING8 || PROP_TYPE(ulPropTag) == PT_UNICODE) && PROP_TYPE(ulPropTag) != PT_TSTRING)
		return CHANGE_PROP_TYPE(ulPropTag, PT_TSTRING);
	if ((PROP_TYPE(ulPropTag) == PT_MV_STRING8 || PROP_TYPE(ulPropTag) == PT_MV_UNICODE) && PROP_TYPE(ulPropTag) != PT_MV_TSTRING)
		return CHANGE_PROP_TYPE(ulPropTag, PT_MV_TSTRING);
	return ulPropTag;
}

/** 
 * Get logical source address for a request
 *
 * Normally returns the string representation of the IP address of the connected client. However,
 * when a proxy is detected (bProxy is true) and an X-Forwarded-From header is available, returns
 * the contents of that header
 *
 * @param[in] soap Soap object of the request
 * @result String representation of the requester's source address, valid for as long as the soap object exists.
 */
const char *GetSourceAddr(struct soap *soap)
{
	if (soap_info(soap)->bProxy && soap->proxy_from != nullptr)
		return soap->proxy_from;
	return soap->host;
}

void ec_soap_deleter::operator()(struct soap *s)
{
	soap_free(s);
}

} /* namespace */
