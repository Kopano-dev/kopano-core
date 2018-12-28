/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include "phpconfig.h"
#include <kopano/platform.h>
#include <cmath>
#include <mapiutil.h>
#include <kopano/timeutil.hpp>

extern "C" {
	// Remove these defines to remove warnings
	#undef PACKAGE_VERSION
	#undef PACKAGE_TARNAME
	#undef PACKAGE_NAME
	#undef PACKAGE_STRING
	#undef PACKAGE_BUGREPORT
	
	#include "php.h"
   	#include "php_globals.h"
	#include "ext/standard/info.h"
	#include "ext/standard/php_string.h"

#ifdef ZTS
    void*** tsrm_ls;
#endif
}

#undef inline

#include <mapi.h>
#include <mapix.h>
#include <mapidefs.h>
#include <mapitags.h>
#include <mapicode.h>
#include <edkmdb.h>
#include "typeconversion.h"
#include <kopano/charset/convert.h>

// Calls MAPIAllocateMore or MAPIAllocateBuffer according to whether an lpBase was passed or not
#define MAPI_ALLOC(n, lpBase, lpp) (lpBase ? MAPIAllocateMore(n, lpBase, lpp) : MAPIAllocateBuffer(n, lpp))
// Frees the buffer with MAPIFreeBuffer if lpBase is NOT set, we can't directly free data allocated with MAPIAllocateMore ..
#define MAPI_FREE(lpbase, lpp) \
	do { if (lpBase == nullptr) MAPIFreeBuffer(lpp); } while (false)
#define BEFORE_PHP7_2(s) const_cast<char *>(s)

using namespace KC;

ZEND_EXTERN_MODULE_GLOBALS(mapi)

static LONG PropTagToPHPTag(ULONG ulPropTag) {
	LONG PHPTag = 0;
	if (PROP_TYPE(ulPropTag) == PT_UNICODE)
		PHPTag = (LONG)CHANGE_PROP_TYPE(ulPropTag, PT_STRING8);
	else if (PROP_TYPE(ulPropTag) == PT_MV_UNICODE)
		PHPTag = (LONG)CHANGE_PROP_TYPE(ulPropTag, PT_MV_STRING8);
	else
		PHPTag = (LONG)ulPropTag;
	return PHPTag;
}

/*
* Converts a PHP Array into a SBinaryArray. This is the same as an ENTRYLIST which
* is used with DeleteMessages();
*
*/
HRESULT PHPArraytoSBinaryArray(zval * entryid_array , void *lpBase, SBinaryArray *lpBinaryArray TSRMLS_DC)
{
	// local
	HashTable		*target_hash = NULL;
	int				count;
	zval			**ppentry = NULL;
	zval			*pentry = NULL;
	int				i, n = 0;

	MAPI_G(hr) = hrSuccess;

	target_hash = HASH_OF(entryid_array);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No target_hash in PHPArraytoSBinaryArray");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		return MAPI_G(hr);
	}
	
	count = zend_hash_num_elements(Z_ARRVAL_P(entryid_array));
	if (count == 0) {
        lpBinaryArray->lpbin = NULL;
        lpBinaryArray->cValues = 0;
		return MAPI_G(hr);
	}

	MAPI_G(hr) = MAPIAllocateMore(sizeof(SBinary) * count, lpBase, (void **) &lpBinaryArray->lpbin);
	if(MAPI_G(hr) != hrSuccess)
		return MAPI_G(hr);

	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	for (i = 0; i < count; ++i) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&ppentry), &hpos);
		pentry = *ppentry;

		convert_to_string_ex(&pentry);
		lpBinaryArray->lpbin[n].cb = pentry->value.str.len;
		MAPI_G(hr) = KAllocCopy(pentry->value.str.val, pentry->value.str.len, reinterpret_cast<void **>(&lpBinaryArray->lpbin[n].lpb), lpBase);
		if(MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);
		++n;
		zend_hash_move_forward_ex(target_hash, &hpos);
	}

	lpBinaryArray->cValues = n;
	return MAPI_G(hr);
}

HRESULT PHPArraytoSBinaryArray(zval * entryid_array , void *lpBase, SBinaryArray **lppBinaryArray TSRMLS_DC)
{
	SBinaryArray *lpBinaryArray = NULL;
	
	MAPI_G(hr) = MAPI_ALLOC(sizeof(SBinaryArray), lpBase, (void **)&lpBinaryArray);
	if(MAPI_G(hr) != hrSuccess)
		return MAPI_G(hr);
		
	MAPI_G(hr) = PHPArraytoSBinaryArray(entryid_array, lpBase ? lpBase : lpBinaryArray, lpBinaryArray TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		MAPI_FREE(lpBase, lpBinaryArray);
		return MAPI_G(hr);
	}
	
	*lppBinaryArray = lpBinaryArray;
	return MAPI_G(hr);
}

HRESULT SBinaryArraytoPHPArray(const SBinaryArray *lpBinaryArray,
    zval **ppvalRet TSRMLS_DC)
{
	unsigned int i = 0;
	
	MAPI_G(hr) = hrSuccess;
	
	zval *pvalRet;
	
	MAKE_STD_ZVAL(pvalRet);
	array_init(pvalRet);
	
	for (i = 0; i < lpBinaryArray->cValues; ++i)
		add_next_index_stringl(pvalRet, (char *)lpBinaryArray->lpbin[i].lpb, lpBinaryArray->lpbin[i].cb, 1);
	*ppvalRet = pvalRet;
	
	return MAPI_G(hr);
}

/*
* Converts a PHP Array into a SortOrderSet. This SortOrderSet is used to sort a table
* when a call to IMAPITable->QueryRows is made.
* The zval array will look like this:
* array() {
*    PR_SUBJECT => TABLE_SORT_ASCEND,
*	etc..
* }
*
* The key is the property and the value is the sorting method for that property
*
* NOTE: The TABLE_SORT_COMBINE is not (yet) implemented, it should work but is not tested
*/
HRESULT PHPArraytoSortOrderSet(zval * sortorder_array, void *lpBase, LPSSortOrderSet *lppSortOrderSet TSRMLS_DC)
{
	// local
	LPSSortOrderSet lpSortOrderSet = NULL;
	int				count;
	HashTable		*target_hash = NULL;
	zval			**entry = NULL;

	MAPI_G(hr) = hrSuccess;

	target_hash = HASH_OF(sortorder_array);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No target_hash in PHPArraytoSortOrderSet");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}

	// get the number of items in the array
	count = zend_hash_num_elements(Z_ARRVAL_P(sortorder_array));

	MAPI_G(hr) = MAPI_ALLOC(CbNewSSortOrderSet(count), lpBase, (void **) &lpSortOrderSet);
	if(MAPI_G(hr) != hrSuccess)
		return MAPI_G(hr);

	lpSortOrderSet->cSorts = count;
	lpSortOrderSet->cCategories = 0;
	lpSortOrderSet->cExpanded = 0;

	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	for (int i = 0; i < count; ++i) {
		//todo: check on FAILURE
		char *key = NULL;
		ulong ind = 0;
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos);
		// when the key is a char &key is set else ind is set
		zend_hash_get_current_key_ex(target_hash, &key, nullptr, &ind, 0, &hpos);
		if (key != NULL)
			lpSortOrderSet->aSort[i].ulPropTag = atoi(key);
		else
			lpSortOrderSet->aSort[i].ulPropTag = ind;

		convert_to_long_ex(&entry[0]);
		lpSortOrderSet->aSort[i].ulOrder = (ULONG) entry[0]->value.lval;
		zend_hash_move_forward_ex(target_hash, &hpos);
	}

	*lppSortOrderSet = lpSortOrderSet;
	return MAPI_G(hr);
}

/**
* Converts a php array to a PropTagArray.
* The caller is responsible to free the memory using MAPIFreeBuffer
*/

HRESULT PHPArraytoPropTagArray(zval * prop_value_array, void *lpBase, LPSPropTagArray *lppPropTagArray TSRMLS_DC)
{
	// return value
	LPSPropTagArray lpPropTagArray = NULL;

	// local
	int				count;
	HashTable		*target_hash = NULL;
	zval ** entry = NULL;

	MAPI_G(hr) = hrSuccess;

	target_hash = HASH_OF(prop_value_array);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No target_hash in PHPArraytoPropTagArray");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}

	// get the number of items in the array
	count = zend_hash_num_elements(target_hash);

	// allocate memory to use
	MAPI_G(hr) = MAPI_ALLOC(CbNewSPropTagArray(count), lpBase, (void **)&lpPropTagArray);
	if (MAPI_G(hr) != hrSuccess)
		return MAPI_G(hr);
	lpPropTagArray->cValues = count;

	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	for (int i = 0; i < count; ++i) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos);
		convert_to_long_ex(entry);

		lpPropTagArray->aulPropTag[i] = entry[0]->value.lval;
		zend_hash_move_forward_ex(target_hash, &hpos);
	}
	
	*lppPropTagArray = lpPropTagArray;
	return MAPI_G(hr);
}

/*
* Converts a PHP property value array to a MAPI property value structure
*/
HRESULT PHPArraytoPropValueArray(zval* phpArray, void *lpBase, ULONG *lpcValues, LPSPropValue *lppPropValArray TSRMLS_DC)
{
	// return value
	LPSPropValue	lpPropValue	= NULL;
	ULONG			cvalues = 0;
	// local
	int				count;
	HashTable		*target_hash = NULL;
	HashTable		*dataHash = NULL;
	char			*keyIndex;
	ulong			numIndex = 0;
	zval			**entry = NULL;
	ULONG			countarray = 0;
	zval			**dataEntry = NULL;
	HashTable		*actionHash = NULL;
	ULONG			j, h;
	ACTIONS			*lpActions = NULL;
	LPSRestriction	lpRestriction = NULL;
	ULONG			ulCountTmp = 0; // temp value
	LPSPropValue	lpPropTmp = NULL;

	if (!phpArray) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No phpArray in PHPArraytoPropValueArray");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}

	target_hash = HASH_OF(phpArray);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No target_hash in PHPArraytoPropValueArray");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}

	// Get the number of items in the array, also the number of item to use for the LPSPropValue array
	count = zend_hash_num_elements(target_hash);
	if (count == 0) {
	    *lppPropValArray = NULL;
	    *lpcValues = 0;
	}

	HashPosition thpos, dhpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &thpos);
	MAPI_G(hr) = MAPI_ALLOC(sizeof(SPropValue) * count, lpBase, (void**)&lpPropValue);

	for (int i = 0; i < count; ++i) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &thpos);
		zend_hash_get_current_key_ex(target_hash, &keyIndex, nullptr, &numIndex, 0, &thpos);

		// assume a numeric index
		lpPropValue[cvalues].ulPropTag = numIndex;
		switch(PROP_TYPE(numIndex))	{
		case PT_SHORT:
			convert_to_long_ex(entry);
			lpPropValue[cvalues++].Value.i = (short)entry[0]->value.lval;
			break;
		case PT_LONG:
			convert_to_long_ex(entry);
			lpPropValue[cvalues++].Value.l = entry[0]->value.lval;
			break;
		case PT_FLOAT:
			convert_to_double_ex(entry);
			lpPropValue[cvalues++].Value.flt = (float)entry[0]->value.dval;
			break;
		case PT_DOUBLE:
			convert_to_double_ex(entry);
			lpPropValue[cvalues++].Value.dbl = entry[0]->value.dval;
			break;
		case PT_LONGLONG:
			convert_to_double_ex(entry);
			lpPropValue[cvalues++].Value.li.QuadPart = (LONGLONG)entry[0]->value.dval;
			break;
		case PT_BOOLEAN:
			convert_to_boolean_ex(entry);
			// lval will be 1 or 0 for true or false
			lpPropValue[cvalues++].Value.b = (unsigned short)entry[0]->value.lval;
			break;
		case PT_SYSTIME:
			convert_to_long_ex(entry);
			// convert timestamp to windows FileTime
			lpPropValue[cvalues++].Value.ft = UnixTimeToFileTime(entry[0]->value.lval);
			break;
		case PT_BINARY:
			convert_to_string_ex(entry);

			// Allocate and copy data
			lpPropValue[cvalues].Value.bin.cb =  entry[0]->value.str.len;
			MAPI_G(hr) = KAllocCopy(entry[0]->value.str.val, entry[0]->value.str.len, reinterpret_cast<void **>(&lpPropValue[cvalues].Value.bin.lpb), lpBase != nullptr ? lpBase : lpPropValue);
			if (MAPI_G(hr) != hrSuccess)
				return MAPI_G(hr);
			++cvalues;
			break;
		case PT_STRING8:
			convert_to_string_ex(entry);

			// Allocate and copy data
			MAPI_G(hr) = KAllocCopy(entry[0]->value.str.val, entry[0]->value.str.len + 1, reinterpret_cast<void **>(&lpPropValue[cvalues].Value.lpszA), lpBase != nullptr ? lpBase : lpPropValue);
			if (MAPI_G(hr) != hrSuccess)
				return MAPI_G(hr);
			++cvalues;
			break;
		case PT_APPTIME:
			convert_to_double_ex(entry);
			lpPropValue[cvalues++].Value.at = entry[0]->value.dval;
			break;
		case PT_CLSID:
			convert_to_string_ex(entry);
			if (entry[0]->value.str.len != sizeof(GUID)) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "GUID must be 16 bytes");
				return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			}
			MAPI_G(hr) = KAllocCopy(entry[0]->value.str.val, sizeof(GUID), reinterpret_cast<void **>(&lpPropValue[cvalues].Value.lpguid), lpBase != nullptr ? lpBase : lpPropValue);
			if (MAPI_G(hr) != hrSuccess)
				return MAPI_G(hr);
			++cvalues;
			break;

#define GET_MV_HASH() \
	{ \
		dataHash = HASH_OF(entry[0]); \
		if (!dataHash) { \
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "No MV dataHash"); \
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER; \
		} \
	}

#define CHECK_EMPTY_MV_ARRAY(mapimvmember, mapilpmember) \
	{ \
		countarray = zend_hash_num_elements(dataHash); \
		if (countarray == 0) { \
			lpPropValue[cvalues].Value.mapimvmember.cValues = 0;  \
			lpPropValue[cvalues].Value.mapimvmember.mapilpmember = NULL; \
			break; \
		} \
		zend_hash_internal_pointer_reset_ex(dataHash, &dhpos); \
	}

#define COPY_MV_PROPS(type, mapimvmember, mapilpmember, phpmember) \
	GET_MV_HASH() \
	CHECK_EMPTY_MV_ARRAY(mapimvmember, mapilpmember) \
	lpPropValue[cvalues].Value.mapimvmember.cValues = countarray; \
	MAPI_G(hr) = MAPIAllocateMore(sizeof(lpPropValue[cvalues].Value.mapimvmember.mapilpmember[0]) * countarray, lpBase ? lpBase : lpPropValue, (void**)&lpPropValue[cvalues].Value.mapimvmember.mapilpmember); \
	for (j = 0; j < countarray; ++j) { \
		zend_hash_get_current_data_ex(dataHash, reinterpret_cast<void **>(&dataEntry), &dhpos); \
		convert_to_##type##_ex(dataEntry); \
		lpPropValue[cvalues].Value.mapimvmember.mapilpmember[j] = dataEntry[0]->value.phpmember; \
		zend_hash_move_forward_ex(dataHash, &dhpos); \
	}

		case PT_MV_I2:
			COPY_MV_PROPS(long, MVi, lpi, lval);
			++cvalues;
			break;
		case PT_MV_LONG:
			COPY_MV_PROPS(long, MVl, lpl, lval);
			++cvalues;
			break;
		case PT_MV_R4:
			COPY_MV_PROPS(double, MVflt, lpflt, dval);
			++cvalues;
			break;
		case PT_MV_DOUBLE:
			COPY_MV_PROPS(double, MVdbl, lpdbl, dval);
			++cvalues;
			break;
		case PT_MV_APPTIME:
			COPY_MV_PROPS(double, MVat, lpat, dval);
			++cvalues;
			break;
		case PT_MV_SYSTIME:
			GET_MV_HASH();
			CHECK_EMPTY_MV_ARRAY(MVft, lpft);

			lpPropValue[cvalues].Value.MVft.cValues = countarray;
			if ((MAPI_G(hr) = MAPIAllocateMore(sizeof(FILETIME) * countarray, lpBase ? lpBase : lpPropValue, (void **)&lpPropValue[cvalues].Value.MVft.lpft)) != hrSuccess)
				return MAPI_G(hr);
			for (j = 0; j < countarray; ++j) {
				zend_hash_get_current_data_ex(dataHash, reinterpret_cast<void **>(&dataEntry), &dhpos);
				convert_to_long_ex(dataEntry);
				lpPropValue[cvalues].Value.MVft.lpft[j] = UnixTimeToFileTime(dataEntry[0]->value.lval);
				zend_hash_move_forward_ex(dataHash, &dhpos);
			}
			++cvalues;
			break;
		case PT_MV_UNICODE: // PT_MV_UNICODE is binary-compatible with PT_MV_BINARY in this case ..
		case PT_MV_BINARY:
			GET_MV_HASH();
			CHECK_EMPTY_MV_ARRAY(MVbin, lpbin);
			if ((MAPI_G(hr) = MAPIAllocateMore(sizeof(SBinary) * countarray, lpBase ? lpBase : lpPropValue, (void **)&lpPropValue[cvalues].Value.MVbin.lpbin)) != hrSuccess)
				return MAPI_G(hr);
			for (h = 0, j = 0; j < countarray; ++j) {
				zend_hash_get_current_data_ex(dataHash, reinterpret_cast<void **>(&dataEntry), &dhpos);
				convert_to_string_ex(dataEntry);
				lpPropValue[cvalues].Value.MVbin.lpbin[h].cb = dataEntry[0]->value.str.len;
				MAPI_G(hr) = KAllocCopy(dataEntry[0]->value.str.val, dataEntry[0]->value.str.len, reinterpret_cast<void **>(&lpPropValue[cvalues].Value.MVbin.lpbin[h].lpb), lpBase != nullptr ? lpBase : lpPropValue);
				if (MAPI_G(hr) != hrSuccess)
					return MAPI_G(hr);
				++h;
				zend_hash_move_forward_ex(dataHash, &dhpos);
			}
			lpPropValue[cvalues++].Value.MVbin.cValues = h;
			break;
		case PT_MV_STRING8:
			GET_MV_HASH();
			CHECK_EMPTY_MV_ARRAY(MVszA, lppszA);
			if ((MAPI_G(hr) = MAPIAllocateMore(sizeof(char*) * countarray, lpBase ? lpBase : lpPropValue, (void **)&lpPropValue[cvalues].Value.MVszA.lppszA)) != hrSuccess)
				return MAPI_G(hr);
			for (h = 0, j = 0; j < countarray; ++j) {
				zend_hash_get_current_data_ex(dataHash, reinterpret_cast<void **>(&dataEntry), &dhpos);
				convert_to_string_ex(dataEntry);
				MAPI_G(hr) = KAllocCopy(dataEntry[0]->value.str.val, dataEntry[0]->value.str.len + 1, reinterpret_cast<void **>(&lpPropValue[cvalues].Value.MVszA.lppszA[h]), lpBase != nullptr ? lpBase : lpPropValue);
				if (MAPI_G(hr) != hrSuccess)
					return MAPI_G(hr);
				++h;
				zend_hash_move_forward_ex(dataHash, &dhpos);
			}
			lpPropValue[cvalues++].Value.MVszA.cValues = h;
			break;
		case PT_MV_CLSID:
			GET_MV_HASH();
			CHECK_EMPTY_MV_ARRAY(MVguid, lpguid);
			if ((MAPI_G(hr) = MAPIAllocateMore(sizeof(GUID) * countarray, lpBase ? lpBase : lpPropValue, (void **)&lpPropValue[cvalues].Value.MVguid.lpguid)) != hrSuccess)
				return MAPI_G(hr);
			for (h = 0, j = 0; j < countarray; ++j) {
				zend_hash_get_current_data_ex(dataHash, reinterpret_cast<void **>(&dataEntry), &dhpos);
				convert_to_string_ex(dataEntry);
				if (dataEntry[0]->value.str.len != sizeof(GUID))
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "invalid value for PT_MV_CLSID property in proptag 0x%08X, position %d,%d", lpPropValue[cvalues].ulPropTag, i, j);
				else
					memcpy(&lpPropValue[cvalues].Value.MVguid.lpguid[h++], dataEntry[0]->value.str.val, sizeof(GUID));
				zend_hash_move_forward_ex(dataHash, &dhpos);
			}
			lpPropValue[cvalues++].Value.MVguid.cValues = h;
			break;
		case PT_MV_I8:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "PT_MV_I8 not supported");
			return MAPI_G(hr) = MAPI_E_NO_SUPPORT;
		case PT_MV_CURRENCY:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "PT_MV_CURRENCY not supported");
			return MAPI_G(hr) = MAPI_E_NO_SUPPORT;
		case PT_ACTIONS:
			dataHash = HASH_OF(entry[0]);
			if (!dataHash)
				break;
			zend_hash_internal_pointer_reset_ex(dataHash, &dhpos);
			countarray = zend_hash_num_elements(dataHash); // # of actions
			if (countarray == 0) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "PT_ACTIONS is empty");
				return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			}
			if ((MAPI_G(hr) = MAPIAllocateMore(sizeof(ACTIONS), lpBase ? lpBase : lpPropValue, (void **)&lpPropValue[cvalues].Value.lpszA)) != hrSuccess)
				return MAPI_G(hr);
			lpActions = (ACTIONS*)lpPropValue[cvalues].Value.lpszA;
			lpActions->ulVersion = EDK_RULES_VERSION;
			lpActions->cActions = countarray;
			if ((MAPI_G(hr) = MAPIAllocateMore(sizeof(ACTION) * lpActions->cActions, lpBase ? lpBase : lpPropValue, (void**)&lpActions->lpAction)) != hrSuccess)
				return MAPI_G(hr);
			memset(lpActions->lpAction, 0, sizeof(ACTION) * lpActions->cActions);

			for (j = 0; j < countarray; ++j) {
				zend_hash_get_current_data_ex(dataHash, reinterpret_cast<void **>(&entry), &dhpos);
				actionHash = HASH_OF(entry[0]);
				if (!actionHash) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "ACTIONS structure has a wrong ACTION");
					return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
				}
				if (zend_hash_find(actionHash, "action", sizeof("action"), (void **)&dataEntry) != SUCCESS) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "PT_ACTIONS type has no action type in array");
					return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
				}

				convert_to_long_ex(dataEntry);
				lpActions->lpAction[j].acttype = (ACTTYPE)Z_LVAL_PP(dataEntry);

				// Option field user defined flags, default 0
				if (zend_hash_find(actionHash, "flags", sizeof("flags"), (void **)&dataEntry) == SUCCESS) {
					convert_to_long_ex(dataEntry);
					lpActions->lpAction[j].ulFlags = Z_LVAL_PP(dataEntry);
				}

				// Option field used with OP_REPLAY and OP_FORWARD, default 0
				if (zend_hash_find(actionHash, "flavor", sizeof("flavor"), (void **)&dataEntry) == SUCCESS) {
					convert_to_long_ex(dataEntry);
					lpActions->lpAction[j].ulActionFlavor = Z_LVAL_PP(dataEntry);
				}

				switch (lpActions->lpAction[j].acttype) {
				case OP_MOVE:
				case OP_COPY:
					if (zend_hash_find(actionHash, "storeentryid", sizeof("storeentryid"), (void **)&dataEntry) != SUCCESS) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_COPY/OP_MOVE but no storeentryid entry");
						return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
					}
					convert_to_string_ex(dataEntry);
					lpActions->lpAction[j].actMoveCopy.cbStoreEntryId = dataEntry[0]->value.str.len;
					MAPI_G(hr) = MAPIAllocateMore(dataEntry[0]->value.str.len, lpBase ? lpBase : lpPropValue, (void **)&lpActions->lpAction[j].actMoveCopy.lpStoreEntryId);
					if (MAPI_G(hr) != hrSuccess)
						return MAPI_G(hr);
					memcpy(lpActions->lpAction[j].actMoveCopy.lpStoreEntryId, dataEntry[0]->value.str.val, dataEntry[0]->value.str.len);
					if (zend_hash_find(actionHash, "folderentryid", sizeof("folderentryid"), (void **)&dataEntry) != SUCCESS) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_COPY/OP_MOVE but no folderentryid entry");
						return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
					}

					convert_to_string_ex(dataEntry);
					lpActions->lpAction[j].actMoveCopy.cbFldEntryId = dataEntry[0]->value.str.len;
					MAPI_G(hr) = MAPIAllocateMore(dataEntry[0]->value.str.len, lpBase ? lpBase : lpPropValue, (void **)&lpActions->lpAction[j].actMoveCopy.lpFldEntryId);
					if (MAPI_G(hr) != hrSuccess)
						return MAPI_G(hr);
					memcpy(lpActions->lpAction[j].actMoveCopy.lpFldEntryId, dataEntry[0]->value.str.val, dataEntry[0]->value.str.len);
					break;
				case OP_REPLY:
				case OP_OOF_REPLY:
					if (zend_hash_find(actionHash, "replyentryid", sizeof("replyentryid"), (void **)&dataEntry) != SUCCESS) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_REPLY but no replyentryid entry");
						return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
					}
					convert_to_string_ex(dataEntry);
					lpActions->lpAction[j].actReply.cbEntryId = dataEntry[0]->value.str.len;
					MAPI_G(hr) = MAPIAllocateMore(dataEntry[0]->value.str.len, lpBase ? lpBase : lpPropValue, (void **)&lpActions->lpAction[j].actReply.lpEntryId);
					if (MAPI_G(hr) != hrSuccess)
						return MAPI_G(hr);
					memcpy(lpActions->lpAction[j].actReply.lpEntryId, dataEntry[0]->value.str.val, dataEntry[0]->value.str.len);

					// optional field
					if (zend_hash_find(actionHash, "replyguid", sizeof("replyguid"), (void **)&dataEntry) == SUCCESS) {
						convert_to_string_ex(dataEntry);
						if (dataEntry[0]->value.str.len != sizeof(GUID)) {
							php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_REPLY replyguid not sizeof(GUID)");
							return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
						}
						memcpy(&lpActions->lpAction[j].actReply.guidReplyTemplate, dataEntry[0]->value.str.val, sizeof(GUID));
					}
					break;
				case OP_DEFER_ACTION:
					if (zend_hash_find(actionHash, "dam", sizeof("dam"), (void **)&dataEntry) != SUCCESS) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_DEFER_ACTION but no dam entry");
						return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
					}
					convert_to_string_ex(dataEntry);
					lpActions->lpAction[j].actDeferAction.cbData = dataEntry[0]->value.str.len;
					MAPI_G(hr) = MAPIAllocateMore(dataEntry[0]->value.str.len, lpBase ? lpBase : lpPropValue, (void **)&lpActions->lpAction[j].actDeferAction.pbData);
					if (MAPI_G(hr) != hrSuccess)
						return MAPI_G(hr);
					memcpy(lpActions->lpAction[j].actDeferAction.pbData, dataEntry[0]->value.str.val, dataEntry[0]->value.str.len);
					break;
				case OP_BOUNCE:
					if (zend_hash_find(actionHash, "code", sizeof("code"), (void **)&dataEntry) != SUCCESS) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_BOUNCE but no code entry");
						return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
					}
					convert_to_long_ex(dataEntry);
					lpActions->lpAction[j].scBounceCode = Z_LVAL_PP(dataEntry);
					break;
				case OP_FORWARD:
				case OP_DELEGATE:
					if (zend_hash_find(actionHash, "adrlist", sizeof("adrlist"), (void **)&dataEntry) != SUCCESS) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_FORWARD/OP_DELEGATE but no adrlist entry");
						return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
					}
					if (dataEntry[0]->type != IS_ARRAY) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_FORWARD/OP_DELEGATE adrlist entry must be an array");
						return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
					}
					MAPI_G(hr) = PHPArraytoAdrList(dataEntry[0], lpBase ? lpBase : lpPropValue, &lpActions->lpAction[j].lpadrlist TSRMLS_CC);
					if (MAPI_G(hr) != hrSuccess)
						return MAPI_G(hr);
					if (MAPI_G(hr) != hrSuccess){
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_DELEGATE/OP_FORWARD wrong data in adrlist entry");
						return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
					}
					break;
				case OP_TAG:
					if (zend_hash_find(actionHash, "proptag", sizeof("proptag"), (void **)&dataEntry) != SUCCESS) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_TAG but no proptag entry");
						return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
					}
					MAPI_G(hr) = PHPArraytoPropValueArray(dataEntry[0], lpBase ? lpBase : lpPropValue, &ulCountTmp, &lpPropTmp TSRMLS_CC);
					if (MAPI_G(hr) != hrSuccess)
						return MAPI_G(hr);
					if (ulCountTmp > 1)
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "OP_TAG has 'proptag' member which contains more than one property. Using the first in the array.");
					lpActions->lpAction[j].propTag = *lpPropTmp;
					break;
				case OP_DELETE:
				case OP_MARK_AS_READ:
					// Nothing to do
					break;
				};
				zend_hash_move_forward_ex(dataHash, &dhpos);
			}
			++cvalues;
			break;

		case PT_SRESTRICTION:
			MAPI_G(hr) = PHPArraytoSRestriction(entry[0], lpBase ? lpBase : lpPropValue, &lpRestriction TSRMLS_CC);
			if (MAPI_G(hr) != hrSuccess) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "PHPArray to SRestriction failed");
				return MAPI_G(hr);
			}
			lpPropValue[cvalues++].Value.lpszA = (char *)lpRestriction;
			break;
		case PT_ERROR:
			convert_to_long_ex(entry);
			lpPropValue[cvalues].Value.err = entry[0]->value.lval;
			break;
		default:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown property type %08X", PROP_TYPE(numIndex));
			return MAPI_G(hr) = MAPI_E_INVALID_TYPE;
		}
		zend_hash_move_forward_ex(target_hash, &thpos);
	}

	*lpcValues = cvalues;
	*lppPropValArray = lpPropValue;
	return MAPI_G(hr);
}

HRESULT PHPArraytoAdrList(zval *phpArray, void *lpBase, LPADRLIST *lppAdrList TSRMLS_DC) 
{
	HashTable		*target_hash = NULL;
	ULONG			countProperties = 0;		// number of properties
	ULONG			count = 0;					// number of elements in the array
	ULONG			countRecipients = 0;		// number of actual recipients
	LPADRLIST		lpAdrList = NULL;
	zval			**entry = NULL;
	LPSPropValue	pPropValue = NULL;

	MAPI_G(hr) = hrSuccess;

	if (!phpArray) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No phpArray in PHPArraytoAdrList");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}

	target_hash = HASH_OF(phpArray);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "phparraytoadrlist wrong data, unknown error");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}

	if(phpArray->type != IS_ARRAY) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "phparray to adrlist must include an array");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}

	count = zend_hash_num_elements(target_hash);
	// We allow allocing a 0 count addresslist, since we need this the OP_DELEGATE rule

	MAPI_G(hr) = MAPI_ALLOC(CbNewADRLIST(count), lpBase, (void **)&lpAdrList);
	if(MAPI_G(hr) != hrSuccess)
		return MAPI_G(hr);
	lpAdrList->cEntries = 0;
	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);

	// FIXME: It is possible that the memory allocated is more than actually needed. We should first
	//		  count the number of elements needed then allocate memory and then fill the memory.
	//        but since this waste is probably very minimal, we could not care less about this.
	for (unsigned int i = 0; i < count; ++i) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos);
		if(entry[0]->type != IS_ARRAY) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "phparraytoadrlist array must include an array with array of propvalues");
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}

		MAPI_G(hr) = PHPArraytoPropValueArray(entry[0], lpBase, &countProperties, &pPropValue TSRMLS_CC);
		if(MAPI_G(hr) != hrSuccess)
			goto exit;
		++lpAdrList->cEntries;
		lpAdrList->aEntries[countRecipients].ulReserved1 = 0;
		lpAdrList->aEntries[countRecipients].rgPropVals = pPropValue;
		lpAdrList->aEntries[countRecipients].cValues = countProperties;
		zend_hash_move_forward_ex(target_hash, &hpos);
		++countRecipients;
	}
	*lppAdrList = lpAdrList;

exit:
	if(MAPI_G(hr) != hrSuccess && lpBase == NULL && lpAdrList != NULL)
		FreePadrlist(lpAdrList);
	return MAPI_G(hr);
}

HRESULT PHPArraytoRowList(zval *phpArray, void *lpBase, LPROWLIST *lppRowList TSRMLS_DC) {
	HashTable		*target_hash = NULL;
	ULONG			countProperties = 0;		// number of properties
	ULONG			count = 0;					// number of elements in the array
	ULONG			countRows = 0;		// number of actual recipients
	rowlist_ptr lpRowList;
	zval			**entry = NULL;
	zval			**data = NULL;
	LPSPropValue	pPropValue = NULL;

	MAPI_G(hr) = hrSuccess;

	if (!phpArray || phpArray->type != IS_ARRAY) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No phpArray in PHPArraytoRowList");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}

	target_hash = HASH_OF(phpArray);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No target_hash in PHPArraytoRowList");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}
	
	count = zend_hash_num_elements(target_hash);

	// allocate memory to store the array of pointers
	MAPI_G(hr) = MAPIAllocateBuffer(CbNewROWLIST(count), &~lpRowList);
	if (MAPI_G(hr) != hrSuccess)
		return MAPI_G(hr);
	lpRowList->cEntries = 0;
	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);

	// FIXME: It is possible that the memory allocated is more than actually needed. We should first
	//		  count the number of elements needed then allocate memory and then fill the memory.
	//        but since this waste is probably very minimal, we could not care less about this.
	for (unsigned int i = 0; i < count; ++i) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos);
		if (Z_TYPE_PP(entry) != IS_ARRAY) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "PHPArraytoRowList, Row not wrapped in array");
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}

		if (zend_hash_find(HASH_OF(entry[0]), "properties", sizeof("properties"), (void**)&data) == SUCCESS){
			MAPI_G(hr) = PHPArraytoPropValueArray(data[0], NULL, &countProperties, &pPropValue TSRMLS_CC);
			if(MAPI_G(hr) != hrSuccess)
				goto exit;
		}else {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "PHPArraytoRowList, Missing field properties");
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}

		if (pPropValue) {
			if (zend_hash_find(HASH_OF(entry[0]), "rowflags", sizeof("rowflags"), (void**)&data) == SUCCESS) {
				lpRowList->aEntries[countRows].ulRowFlags = Z_LVAL_PP(data);
			} else {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "PHPArraytoRowList, Missing field rowflags");
				MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
				goto exit;
			}
			++lpRowList->cEntries;
			lpRowList->aEntries[countRows].rgPropVals = pPropValue;
			lpRowList->aEntries[countRows++].cValues = countProperties;
		}else {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "PHPArraytoRowList, critical error");
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
		zend_hash_move_forward_ex(target_hash, &hpos);
	}
	*lppRowList = lpRowList.release();
exit:
	return MAPI_G(hr);
}

/**
 Function to convert a PHP array to a SRestriction structure.
 The SRectriction structure is a structure with multiple level which defines a sort of
 tree with restrictions for a table view. It is used with de setSearchCriteria, findRow and other
 functions to select rows for a specified criteria.

 The PHP array should be in the following form:

 array(RES_AND,
 	array(
		array(RES_OR,
			array(
				array(RES_PROPERTY,
					array(
						RELOP => RELOP_LE,
						ULPROPTAG => PR_BODY,
						VALUE => "test"
					),
				),
				array(RES_PROPERTY,
					array(
						RELOP => RELOP_RE,
						ULPROPTAG => PR_DISPLAY_NAME,
						VALUE => "leon"
					),
				),
			),			// OR array
		),
		array(RES_EXIST,
			array(
				ULPROPTAG => PR_HASATTACH
			),
		),
	),					// AND array
 )

 This will mean the following in some pseudocode:
    (((PROPERTY PR_BODY = "test") or (PROPERTY PR_DISPLAY_NAME like "leon")) and (PROPERTY TAG EXIST PR_ATTACH))

 To fix the collision problem, an array is added. This gives the following assumption:
 An entry always consists of:
    array(RES_xxxx, array(values))
    values = array(RES_xxxx, array(values)) || array(restriction data)

 when we read the php array, we use this assumption.

*/

// see include/mapi/mapidefs.php restriction array index values
#define VALUE		0
#define RELOP		1
#define FUZZYLEVEL	2
#define CB			3
#define ULTYPE		4
#define ULMASK		5
#define ULPROPTAG	6
#define ULPROPTAG1	7
#define ULPROPTAG2	8
#define PROPS		9
#define RESTRICTION	10

/*
* A lpRes pointer should be given here, the idea is that every time this
* function is run the SRestriction struct will become bigger, using lpBase as base pointer for MAPIAllocateMore().
* TODO: combine code from here and from PHPArraytoPropValueArray in a pval/zval to Proptag function (?)
*/

HRESULT PHPArraytoSRestriction(zval *phpVal, void* lpBase, LPSRestriction lpRes TSRMLS_DC)
{
	HashTable *resHash	= NULL;
	HashTable *dataHash = NULL;
	zval **typeEntry  = NULL;
	zval **valueEntry = NULL;
	ULONG cValues = 0;
	int count;
	int i;

	if (!phpVal || lpRes == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "critical error");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}

	resHash = HASH_OF(phpVal);
	if (!resHash || zend_hash_num_elements(resHash) != 2) {		// should always be array(RES_ , array(values))
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Wrong array should be array(RES_, array(values))");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}
	HashPosition rhpos, dhpos;
	zend_hash_internal_pointer_reset_ex(resHash, &rhpos);

	// structure assumption: add more checks that valueEntry becomes php array pointer?
	zend_hash_get_current_data_ex(resHash, reinterpret_cast<void **>(&typeEntry), &rhpos); // 0=type, 1=array
	zend_hash_move_forward_ex(resHash, &rhpos);
	zend_hash_get_current_data_ex(resHash, reinterpret_cast<void **>(&valueEntry), &rhpos);

	lpRes->rt = typeEntry[0]->value.lval;		// set restriction type (RES_AND, RES_OR, ...)
	dataHash = HASH_OF(valueEntry[0]);			// from resHash
	if (!dataHash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "critical error, wrong array");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}
	count = zend_hash_num_elements(dataHash);
	zend_hash_internal_pointer_reset_ex(dataHash, &dhpos);

	switch(lpRes->rt) {
	/*
	 * type restrictions
	 */
	case RES_AND:
		// Recursively add all AND-ed restrictions
		lpRes->res.resAnd.cRes = count;
		MAPI_G(hr) = MAPIAllocateMore(sizeof(SRestriction) * count, lpBase, (void **) &lpRes->res.resAnd.lpRes);
		if (MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);
		for (i = 0; i < count; ++i) {
			zend_hash_get_current_data_ex(dataHash, reinterpret_cast<void **>(&valueEntry), &dhpos);
			MAPI_G(hr) = PHPArraytoSRestriction(valueEntry[0], lpBase, &lpRes->res.resAnd.lpRes[i] TSRMLS_CC);
			
			if (MAPI_G(hr) != hrSuccess)
				return MAPI_G(hr);
			zend_hash_move_forward_ex(dataHash, &dhpos);
		}
		break;
	case RES_OR:
		// Recursively add all OR-ed restrictions
		lpRes->res.resOr.cRes = count;
		MAPI_G(hr) = MAPIAllocateMore(sizeof(SRestriction) * count, lpBase, (void **) &lpRes->res.resOr.lpRes);
		if (MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);

		for (i = 0; i < count; ++i) {
			zend_hash_get_current_data_ex(dataHash, reinterpret_cast<void **>(&valueEntry), &dhpos);
			MAPI_G(hr) = PHPArraytoSRestriction(valueEntry[0], lpBase, &lpRes->res.resOr.lpRes[i] TSRMLS_CC);

			if (MAPI_G(hr) != hrSuccess)
				return MAPI_G(hr);
			zend_hash_move_forward_ex(dataHash, &dhpos);
		}
		break;
	case RES_NOT:
		// NOT has only one restriction
		MAPI_G(hr) = MAPIAllocateMore(sizeof(SRestriction), lpBase, (void **) &lpRes->res.resNot.lpRes);
		if (MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);
		zend_hash_get_current_data_ex(dataHash, reinterpret_cast<void **>(&valueEntry), &dhpos);
		MAPI_G(hr) = PHPArraytoSRestriction(valueEntry[0], lpBase, lpRes->res.resNot.lpRes TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);
		break;
	case RES_SUBRESTRICTION:
		if (zend_hash_index_find(dataHash, RESTRICTION, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_SUBRESTRICTION, Missing field RESTRICTION");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}

		MAPI_G(hr) = PHPArraytoSRestriction(valueEntry[0], lpBase, &lpRes->res.resSub.lpRes TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);

		// ULPROPTAG as resSubObject
		if (zend_hash_index_find(dataHash, ULPROPTAG, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_SUBRESTRICTION, Missing field ULPROPTAG");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		lpRes->res.resSub.ulSubObject = valueEntry[0]->value.lval;

		break;
	case RES_COMMENT:
		if (zend_hash_index_find(dataHash, RESTRICTION, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_COMMENT, Missing field RESTRICTION");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}

		MAPI_G(hr) = PHPArraytoSRestriction(valueEntry[0], lpBase, &lpRes->res.resComment.lpRes TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_COMMENT, Wrong data in field RESTRICTION");
			return MAPI_G(hr);
		}

		if (zend_hash_index_find(dataHash, PROPS, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_COMMENT, Missing field PROPS");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}

		MAPI_G(hr) = PHPArraytoPropValueArray(valueEntry[0], lpBase, &lpRes->res.resComment.cValues, &lpRes->res.resComment.lpProp TSRMLS_CC);
		if(MAPI_G(hr) != hrSuccess) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_COMMENT, Wrong data in field PROPS");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		break;

	/*
	 * content restrictions
	 */
	case RES_CONTENT:
	case RES_PROPERTY: {
		LPSPropValue lpProp;
		if (lpRes->rt == RES_PROPERTY) {
			// ULPROPTAG
			if (zend_hash_index_find(dataHash, ULPROPTAG, (void **)&valueEntry) == FAILURE) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_PROPERTY, Missing field ULPROPTAG");
				return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			}
			convert_to_long_ex(valueEntry);
			lpRes->res.resProperty.ulPropTag = valueEntry[0]->value.lval;

			// RELOP
			if (zend_hash_index_find(dataHash, RELOP, (void **)&valueEntry) == FAILURE) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_PROPERTY, Missing field RELOP");
				return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			}
			convert_to_long_ex(valueEntry);
			lpRes->res.resProperty.relop = valueEntry[0]->value.lval;
		} else {
			// ULPROPTAG
			if (zend_hash_index_find(dataHash, ULPROPTAG, (void **)&valueEntry) == FAILURE) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_CONTENT, Missing field ULPROPTAG");
				return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			}
			
			convert_to_long_ex(valueEntry);
			lpRes->res.resContent.ulPropTag = valueEntry[0]->value.lval;

			// possible FUZZYLEVEL
			switch (PROP_TYPE(lpRes->res.resContent.ulPropTag)) {
			case PT_STRING8:
			case PT_UNICODE:
			case PT_BINARY:
			case PT_MV_BINARY:
			case PT_MV_STRING8:
				if (zend_hash_index_find(dataHash, FUZZYLEVEL, (void **)&valueEntry) == FAILURE) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_CONTENT, Missing field FUZZYLEVEL");
					return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
				}
				convert_to_long_ex(valueEntry);
				lpRes->res.resContent.ulFuzzyLevel = valueEntry[0]->value.lval;
				break;
			default:
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_CONTENT, Not supported property type");
				return MAPI_G(hr) = MAPI_E_TOO_COMPLEX;
			};
		}

		// VALUE
		if (zend_hash_index_find(dataHash, VALUE, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_PROPERTY or RES_CONTENT, Missing field VALUE");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}

		if (valueEntry[0]->type == IS_ARRAY) {
			MAPI_G(hr) = PHPArraytoPropValueArray(valueEntry[0], lpBase, &cValues, &lpProp TSRMLS_CC);
			if (MAPI_G(hr) != hrSuccess) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_PROPERTY or RES_CONTENT, Wrong data in field VALUE ");
				return MAPI_G(hr);
			}
		} else {
			// backward compatibility code <= 5.20
			MAPI_G(hr) = MAPIAllocateMore(sizeof(SPropValue), lpBase, (void **)&lpProp);
			if (MAPI_G(hr) != hrSuccess)
				return MAPI_G(hr);
			lpProp->dwAlignPad = 0;
			if (lpRes->rt == RES_PROPERTY)
				lpProp->ulPropTag = lpRes->res.resProperty.ulPropTag;
			else
				lpProp->ulPropTag = lpRes->res.resContent.ulPropTag;

			switch (PROP_TYPE(lpProp->ulPropTag)) {		// sets in either resContent or resProperty
			case PT_STRING8:
				convert_to_string_ex(valueEntry);
				MAPI_G(hr) = MAPIAllocateMore(valueEntry[0]->value.str.len + 1, lpBase, (void **)&lpProp->Value.lpszA);
				if (MAPI_G(hr) != hrSuccess)
					return MAPI_G(hr);
				strncpy(lpProp->Value.lpszA, valueEntry[0]->value.str.val, valueEntry[0]->value.str.len+1);
				break;
			case PT_UNICODE:
				return MAPI_G(hr) = MAPI_E_NO_SUPPORT;
				break;
			case PT_LONG:
				convert_to_long_ex(valueEntry);
				lpProp->Value.l = valueEntry[0]->value.lval;
				break;
			case PT_LONGLONG:
				convert_to_double_ex(valueEntry);
				lpProp->Value.li.QuadPart = (LONGLONG)valueEntry[0]->value.dval;
				break;
			case PT_SHORT:
				convert_to_long_ex(valueEntry);
				lpProp->Value.i = (short)valueEntry[0]->value.lval;
				break;
			case PT_DOUBLE:
				convert_to_double_ex(valueEntry);
				lpProp->Value.dbl = valueEntry[0]->value.dval;
				break;
			case PT_FLOAT:
				convert_to_double_ex(valueEntry);
				lpProp->Value.flt = (float)valueEntry[0]->value.dval;
				break;
			case PT_BOOLEAN:
				convert_to_boolean_ex(valueEntry);
				lpProp->Value.b = (unsigned short)valueEntry[0]->value.lval;
				break;
			case PT_SYSTIME:
				convert_to_long_ex(valueEntry);
				lpProp->Value.ft = UnixTimeToFileTime(valueEntry[0]->value.lval);
				break;
			case PT_BINARY:
				convert_to_string_ex(valueEntry);
				lpProp->Value.bin.cb = valueEntry[0]->value.str.len;
				MAPI_G(hr) = MAPIAllocateMore(valueEntry[0]->value.str.len, lpBase, (void **) &lpProp->Value.bin.lpb);
				if (MAPI_G(hr) != hrSuccess)
					return MAPI_G(hr);
				memcpy(lpProp->Value.bin.lpb, valueEntry[0]->value.str.val, valueEntry[0]->value.str.len);
				break;
			case PT_APPTIME:
				convert_to_double_ex(valueEntry);
				lpProp->Value.at = valueEntry[0]->value.dval;
				break;
			case PT_CLSID:
				convert_to_string_ex(valueEntry);
				if (valueEntry[0]->value.str.len != sizeof(GUID)) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "invalid value for PT_CLSID property in proptag 0x%08X", lpProp->ulPropTag);
					return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
				}
				MAPI_G(hr) = MAPIAllocateMore(sizeof(GUID), lpBase, (void **)&lpProp->Value.lpguid);
				memcpy(lpProp->Value.lpguid, valueEntry[0]->value.str.val, sizeof(GUID));
				break;
			default:
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_PROPERTY or RES_CONTENT, field VALUE no backward compatibility support");
				return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			}
		}

		if (lpRes->rt == RES_PROPERTY)
			lpRes->res.resProperty.lpProp = lpProp;
		else
			lpRes->res.resContent.lpProp = lpProp;
		break;
	}
	case RES_COMPAREPROPS:
		// RELOP
		if (zend_hash_index_find(dataHash, RELOP, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_COMPAREPROPS, Missing field RELOP");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resCompareProps.relop = valueEntry[0]->value.lval;
		// ULPROPTAG1
		if (zend_hash_index_find(dataHash, ULPROPTAG1, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_COMPAREPROPS, Missing field ULPROPTAG1");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resCompareProps.ulPropTag1 = valueEntry[0]->value.lval;
		// ULPROPTAG2
		if (zend_hash_index_find(dataHash, ULPROPTAG2, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_COMPAREPROPS, Missing field ULPROPTAG2");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resCompareProps.ulPropTag2 = valueEntry[0]->value.lval;
		break;
	case RES_BITMASK:
		// ULTYPE
		if (zend_hash_index_find(dataHash, ULTYPE, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_BITMASK, Missing field ULTYPE");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resBitMask.relBMR = valueEntry[0]->value.lval;
		// ULMASK
		if (zend_hash_index_find(dataHash, ULMASK, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_BITMASK, Missing field ULMASK");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resBitMask.ulMask = valueEntry[0]->value.lval;
		// ULPROPTAG
		if (zend_hash_index_find(dataHash, ULPROPTAG, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_BITMASK, Missing field ULPROPTAG");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resBitMask.ulPropTag = valueEntry[0]->value.lval;
		break;
	case RES_SIZE:
		// CB
		if (zend_hash_index_find(dataHash, CB, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_SIZE, Missing field CB");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resSize.cb = valueEntry[0]->value.lval;
		// RELOP
		if (zend_hash_index_find(dataHash, RELOP, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_SIZE, Missing field RELOP");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resSize.relop = valueEntry[0]->value.lval;
		// ULPROPTAG
		if (zend_hash_index_find(dataHash, ULPROPTAG, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_SIZE, Missing field ULPROPTAG");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resSize.ulPropTag = valueEntry[0]->value.lval;
		break;
	case RES_EXIST:
		// ULPROPTAG
		if (zend_hash_index_find(dataHash, ULPROPTAG, (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "RES_EXIST, Missing field ULPROPTAG");
			return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		}
		convert_to_long_ex(valueEntry);
		lpRes->res.resExist.ulPropTag = valueEntry[0]->value.lval;
		break;
	default:
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown restriction type");
		break;
	}
	return MAPI_G(hr);
}

HRESULT PHPArraytoSRestriction(zval *phpVal, void* lpBase, LPSRestriction *lppRes TSRMLS_DC) {
	LPSRestriction lpRes = NULL;
	
	MAPI_G(hr) = MAPI_ALLOC(sizeof(SRestriction), lpBase, (void **)&lpRes);
	if(MAPI_G(hr) != hrSuccess)
		return MAPI_G(hr);
	MAPI_G(hr) = PHPArraytoSRestriction(phpVal, lpBase ? lpBase : lpRes, lpRes TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;
		
	*lppRes = lpRes;
		
exit:
	if (MAPI_G(hr) != hrSuccess && lpBase == NULL)
		MAPIFreeBuffer(lpRes);

	return MAPI_G(hr);
}

HRESULT SRestrictiontoPHPArray(const SRestriction *lpRes, int level,
    zval **pret TSRMLS_DC)
{
	zval *entry = NULL;
	char key[16];
	zval *array = NULL;
	zval *props = NULL;
	zval *restriction = NULL;
	zval *ret;

	if (!lpRes) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No restriction in SRestrictiontoPHPArray");
		return MAPI_E_INVALID_PARAMETER;
	}

	// use define depth
	if (level > 16)
		return MAPI_G(hr) =  MAPI_E_TOO_COMPLEX;
    MAKE_STD_ZVAL(ret);
	array_init(ret);

	switch (lpRes->rt) {
	case RES_AND:
		MAKE_STD_ZVAL(array);
		array_init(array);
		for (ULONG c = 0; c < lpRes->res.resAnd.cRes; ++c) {
		    entry = NULL;
			sprintf(key, "%i", c);
			MAPI_G(hr) = SRestrictiontoPHPArray(&lpRes->res.resAnd.lpRes[c], level+1, &entry TSRMLS_CC);
			if (MAPI_G(hr) != hrSuccess)
				return MAPI_G(hr);
			add_assoc_zval(array, key, entry);
		}
		add_assoc_long(ret, "0", RES_AND);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_OR:
		MAKE_STD_ZVAL(array);
		array_init(array);
		for (ULONG c = 0; c < lpRes->res.resOr.cRes; ++c) {
		    entry = NULL;
			sprintf(key, "%i", c);
			MAPI_G(hr) = SRestrictiontoPHPArray(&lpRes->res.resOr.lpRes[c], level+1, &entry TSRMLS_CC);
			if (MAPI_G(hr) != hrSuccess)
				return MAPI_G(hr);
			add_assoc_zval(array, key, entry);
		}
		add_assoc_long(ret, "0", RES_OR);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_NOT:
		// although it's only one value, it still is wrapped in an array.
		MAKE_STD_ZVAL(array);
		array_init(array);

		MAPI_G(hr) = SRestrictiontoPHPArray(lpRes->res.resNot.lpRes, level+1, &entry TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);
		add_assoc_zval(array, "0", entry);

		add_assoc_long(ret, "0", RES_NOT);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_CONTENT:
		MAPI_G(hr) = PropValueArraytoPHPArray(1, lpRes->res.resContent.lpProp, &props TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);
		MAKE_STD_ZVAL(array);
		array_init(array);
		sprintf(key, "%i", VALUE);
		add_assoc_zval(array, key, props);		
		sprintf(key, "%i", ULPROPTAG);
		add_assoc_long(array, key, PropTagToPHPTag(lpRes->res.resContent.ulPropTag));
		sprintf(key, "%i", FUZZYLEVEL);
		add_assoc_long(array, key, (LONG)lpRes->res.resContent.ulFuzzyLevel);

		add_assoc_long(ret, "0", RES_CONTENT);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_PROPERTY:
		MAPI_G(hr) = PropValueArraytoPHPArray(1, lpRes->res.resProperty.lpProp, &props TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);
		MAKE_STD_ZVAL(array);
		array_init(array);
		sprintf(key, "%i", RELOP);
		add_assoc_long(array, key, (LONG)lpRes->res.resProperty.relop);
		sprintf(key, "%i", ULPROPTAG);
		add_assoc_long(array, key, PropTagToPHPTag(lpRes->res.resProperty.ulPropTag));
		sprintf(key, "%i", VALUE);
		add_assoc_zval(array, key, props);

		add_assoc_long(ret, "0", RES_PROPERTY);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_COMPAREPROPS:
		MAKE_STD_ZVAL(array);
		array_init(array);
		sprintf(key, "%i", RELOP);
		add_assoc_long(array, key, (LONG)lpRes->res.resCompareProps.relop);
		sprintf(key, "%i", ULPROPTAG1);
		add_assoc_long(array, key, PropTagToPHPTag(lpRes->res.resCompareProps.ulPropTag1));
		sprintf(key, "%i", ULPROPTAG2);
		add_assoc_long(array, key, PropTagToPHPTag(lpRes->res.resCompareProps.ulPropTag2));

		add_assoc_long(ret, "0", RES_COMPAREPROPS);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_BITMASK:
		MAKE_STD_ZVAL(array);
		array_init(array);
		sprintf(key, "%i", ULTYPE);
		add_assoc_long(array, key, (LONG)lpRes->res.resBitMask.relBMR);
		sprintf(key, "%i", ULPROPTAG);
		add_assoc_long(array, key, PropTagToPHPTag(lpRes->res.resBitMask.ulPropTag));
		sprintf(key, "%i", ULMASK);
		add_assoc_long(array, key, (LONG)lpRes->res.resBitMask.ulMask);

		add_assoc_long(ret, "0", RES_BITMASK);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_SIZE:
		MAKE_STD_ZVAL(array);
		array_init(array);
		sprintf(key, "%i", RELOP);
		add_assoc_long(array, key, (LONG)lpRes->res.resSize.relop);
		sprintf(key, "%i", ULPROPTAG);
		add_assoc_long(array, key, PropTagToPHPTag(lpRes->res.resSize.ulPropTag));
		sprintf(key, "%i", CB);
		add_assoc_long(array, key, (LONG)lpRes->res.resSize.cb);

		add_assoc_long(ret, "0", RES_SIZE);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_EXIST:
		MAKE_STD_ZVAL(array);
		array_init(array);
		sprintf(key, "%i", ULPROPTAG);
		add_assoc_long(array, key, PropTagToPHPTag(lpRes->res.resExist.ulPropTag));

		add_assoc_long(ret, "0", RES_EXIST);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_SUBRESTRICTION:
	    restriction = NULL;
		MAPI_G(hr) = SRestrictiontoPHPArray(lpRes->res.resSub.lpRes, level+1, &restriction TSRMLS_CC);
		if (!restriction)
			return MAPI_G(hr);
		MAKE_STD_ZVAL(array);
		array_init(array);
		sprintf(key, "%i", ULPROPTAG);
		add_assoc_long(array, key, PropTagToPHPTag(lpRes->res.resSub.ulSubObject));
		sprintf(key, "%i", RESTRICTION);
		add_assoc_zval(array, key, restriction);

		add_assoc_long(ret, "0", RES_SUBRESTRICTION);
		add_assoc_zval(ret, "1", array);
		break;

	case RES_COMMENT:
		MAPI_G(hr) = PropValueArraytoPHPArray(lpRes->res.resComment.cValues, lpRes->res.resComment.lpProp, &props TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess)
			return MAPI_G(hr);
	    restriction = NULL;
		MAPI_G(hr) = SRestrictiontoPHPArray(lpRes->res.resComment.lpRes, level+1, &restriction TSRMLS_CC);
		if (!restriction)
			return MAPI_G(hr);
		MAKE_STD_ZVAL(array);
		array_init(array);
		sprintf(key, "%i", PROPS);
		add_assoc_zval(array, key, props);
		sprintf(key, "%i", RESTRICTION);
		add_assoc_zval(array, key, restriction);

		add_assoc_long(ret, "0", RES_COMMENT);
		add_assoc_zval(ret, "1", array);
		break;
	};
	
	*pret = ret;
	return MAPI_G(hr);
}

/**
* Function to conver a PropTagArray to a PHP Array
*
*/

HRESULT PropTagArraytoPHPArray(ULONG cValues,
    const SPropTagArray *lpPropTagArray, zval **pret TSRMLS_DC)
{
	zval *zvalRet = NULL;
	unsigned int i = 0;
	
	MAPI_G(hr) = hrSuccess;
	
	MAKE_STD_ZVAL(zvalRet);
	array_init(zvalRet);
	
	for (i = 0; i < cValues; ++i)
		add_next_index_long(zvalRet, PropTagToPHPTag(lpPropTagArray->aulPropTag[i]));
	*pret = zvalRet;
	
	return MAPI_G(hr);
}

/**
* Function to convert a PropValueArray to a PHP Array
*
*
*/
HRESULT PropValueArraytoPHPArray(ULONG cValues,
    const SPropValue *pPropValueArray, zval **pret TSRMLS_DC)
{
	// return value
	zval * zval_prop_value;
	// local
	zval * zval_mvprop_value;	// mvprops converts
	zval * zval_action_array;	// action converts
	zval * zval_action_value;	// action converts
	zval * zval_alist_value;	// adrlist in action convert
	const SPropValue *pPropValue;
	ULONG col, j;
	char ulKey[16];
	ACTIONS *lpActions = NULL;
	const SRestriction *lpRestriction = nullptr;
	convert_context converter;

	MAPI_G(hr) = hrSuccess;

	MAKE_STD_ZVAL(zval_prop_value);
	array_init(zval_prop_value);

	for (col = 0; col < cValues; ++col) {
		char pulproptag[16];

		pPropValue = &pPropValueArray[col];

		/*
		* PHP wants a string as array key. PHP will transform this to zval integer when possible.
		* Because MAPI works with ULONGS, some properties (namedproperties) are bigger than LONG_MAX
		* and they will be stored as a zval string.
		* To prevent this we cast the ULONG to a signed long. The number will look a bit weird but it
		* will work.
		*/
		sprintf(pulproptag, "%i",PropTagToPHPTag(pPropValue->ulPropTag));
		switch(PROP_TYPE(pPropValue->ulPropTag)) {
		case PT_NULL:
			add_assoc_null(zval_prop_value, pulproptag);
			break;
			
		case PT_LONG:
			add_assoc_long(zval_prop_value, pulproptag, pPropValue->Value.l);
			break;

		case PT_SHORT:
			add_assoc_long(zval_prop_value, pulproptag, pPropValue->Value.i);
			break;

		case PT_DOUBLE:
			add_assoc_double(zval_prop_value, pulproptag, pPropValue->Value.dbl);
			break;

		case PT_LONGLONG:
 			add_assoc_double(zval_prop_value, pulproptag, pPropValue->Value.li.QuadPart);
			break;

		case PT_FLOAT:
			add_assoc_double(zval_prop_value, pulproptag, pPropValue->Value.flt);
			break;

		case PT_BOOLEAN:
			add_assoc_bool(zval_prop_value, pulproptag, pPropValue->Value.b);
			break;

		case PT_STRING8:
			add_assoc_string(zval_prop_value, pulproptag, pPropValue->Value.lpszA, 1);
			break;

		case PT_UNICODE:
			add_assoc_string(zval_prop_value, pulproptag, BEFORE_PHP7_2(converter.convert_to<std::string>(pPropValue->Value.lpszW).c_str()), 1);
			break;

		case PT_BINARY:
			add_assoc_stringl(zval_prop_value, pulproptag, (char *)pPropValue->Value.bin.lpb,pPropValue->Value.bin.cb,1);
			break;

		case PT_CURRENCY:
			// to implement
			break;

		case PT_ERROR:
			add_assoc_long(zval_prop_value, pulproptag, (LONG)pPropValue->Value.err);
			break;

		case PT_APPTIME:
			add_assoc_double(zval_prop_value, pulproptag, pPropValue->Value.at);
			break;

		case PT_SYSTIME:
			// convert time to Unix timestamp
			add_assoc_long(zval_prop_value, pulproptag, FileTimeToUnixTime(pPropValue->Value.ft));
			break;
		case PT_CLSID:
			add_assoc_stringl(zval_prop_value, pulproptag, (char *)pPropValue->Value.lpguid, sizeof(GUID),1);
			break;

		case PT_MV_I2:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVi.cValues; ++j) {
					sprintf(ulKey, "%i", j);
					add_assoc_long(zval_mvprop_value, ulKey, pPropValue->Value.MVi.lpi[j]);
				}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
		case PT_MV_LONG:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVl.cValues; ++j) {
				sprintf(ulKey, "%i", j);
				add_assoc_long(zval_mvprop_value, ulKey, pPropValue->Value.MVl.lpl[j]);
			}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
		case PT_MV_R4:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVflt.cValues; ++j) {
				sprintf(ulKey, "%i", j);
				add_assoc_double(zval_mvprop_value, ulKey, pPropValue->Value.MVflt.lpflt[j]);
			}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
		case PT_MV_DOUBLE:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVdbl.cValues; ++j) {
				sprintf(ulKey, "%i", j);
				add_assoc_double(zval_mvprop_value, ulKey, pPropValue->Value.MVdbl.lpdbl[j]);
			}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
		case PT_MV_APPTIME:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVat.cValues; ++j) {
				sprintf(ulKey, "%i", j);
				add_assoc_double(zval_mvprop_value, ulKey, pPropValue->Value.MVat.lpat[j]);
			}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
		case PT_MV_SYSTIME:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVft.cValues; ++j) {
				sprintf(ulKey, "%i", j);
				add_assoc_long(zval_mvprop_value, ulKey, FileTimeToUnixTime(pPropValue->Value.MVft.lpft[j]));
			}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
		case PT_MV_BINARY:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVbin.cValues; ++j) {
				sprintf(ulKey, "%i", j);
				add_assoc_stringl(zval_mvprop_value, ulKey,
								  (char*)pPropValue->Value.MVbin.lpbin[j].lpb, pPropValue->Value.MVbin.lpbin[j].cb, 1);
			}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
		case PT_MV_STRING8:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVszA.cValues; ++j) {
				sprintf(ulKey, "%i", j);
				add_assoc_string(zval_mvprop_value, ulKey, pPropValue->Value.MVszA.lppszA[j], 1);
			}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
		case PT_MV_UNICODE:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVszW.cValues; ++j) {
				sprintf(ulKey, "%i", j);
				add_assoc_string(zval_mvprop_value, ulKey, BEFORE_PHP7_2(converter.convert_to<std::string>(pPropValue->Value.MVszW.lppszW[j]).c_str()), 1);
			}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
		case PT_MV_CLSID:
			MAKE_STD_ZVAL(zval_mvprop_value);
			array_init(zval_mvprop_value);

			for (j = 0; j < pPropValue->Value.MVguid.cValues; ++j) {
				sprintf(ulKey, "%i", j);
				add_assoc_stringl(zval_mvprop_value, pulproptag, (char *)&pPropValue->Value.MVguid.lpguid[j], sizeof(GUID), 1);
			}

			add_assoc_zval(zval_prop_value, pulproptag, zval_mvprop_value);
			break;
			//case PT_MV_CURRENCY:
			//case PT_MV_I8:

			// rules table properties
		case PT_ACTIONS:
			lpActions = (ACTIONS*)pPropValue->Value.lpszA;
			MAKE_STD_ZVAL(zval_action_array);
			array_init(zval_action_array);
			for (j = 0; j < lpActions->cActions; ++j) {
				MAKE_STD_ZVAL(zval_action_value);
				array_init(zval_action_value);

				add_assoc_long(zval_action_value, "action", lpActions->lpAction[j].acttype);
				add_assoc_long(zval_action_value, "flags", lpActions->lpAction[j].ulFlags);
				add_assoc_long(zval_action_value, "flavor", lpActions->lpAction[j].ulActionFlavor);
				
				switch (lpActions->lpAction[j].acttype) {
				case OP_MOVE:
				case OP_COPY:
					add_assoc_stringl(zval_action_value, "storeentryid",
									  (char*)lpActions->lpAction[j].actMoveCopy.lpStoreEntryId,
									  lpActions->lpAction[j].actMoveCopy.cbStoreEntryId, 1);
					add_assoc_stringl(zval_action_value, "folderentryid",
									  (char*)lpActions->lpAction[j].actMoveCopy.lpFldEntryId,
									  lpActions->lpAction[j].actMoveCopy.cbFldEntryId, 1);
					break;
				case OP_REPLY:
				case OP_OOF_REPLY:
					add_assoc_stringl(zval_action_value, "replyentryid",
									  (char*)lpActions->lpAction[j].actReply.lpEntryId,
									  lpActions->lpAction[j].actReply.cbEntryId, 1);

					add_assoc_stringl(zval_action_value, "replyguid", (char*)&lpActions->lpAction[j].actReply.guidReplyTemplate, sizeof(GUID), 1);
					break;
				case OP_DEFER_ACTION:
					add_assoc_stringl(zval_action_value, "dam",
									  (char*)lpActions->lpAction[j].actDeferAction.pbData,
									  lpActions->lpAction[j].actDeferAction.cbData, 1);
					break;
				case OP_BOUNCE:
					add_assoc_long(zval_action_value, "code", lpActions->lpAction[j].scBounceCode);
					break;
				case OP_FORWARD:
				case OP_DELEGATE:
					MAPI_G(hr) = RowSettoPHPArray((LPSRowSet)lpActions->lpAction[j].lpadrlist, &zval_alist_value TSRMLS_CC); // binary compatible
					if(MAPI_G(hr) != hrSuccess)
						return MAPI_G(hr);
					add_assoc_zval(zval_action_value, "adrlist", zval_alist_value);
					break;
				case OP_TAG:
					MAPI_G(hr) = PropValueArraytoPHPArray(1, &lpActions->lpAction[j].propTag, &zval_alist_value TSRMLS_CC);
					if(MAPI_G(hr) != hrSuccess)
						return MAPI_G(hr);
					add_assoc_zval(zval_action_value, "proptag", zval_alist_value);
					break;
				case OP_DELETE:
				case OP_MARK_AS_READ:
					// nothing to add
					break;
				};

				sprintf(ulKey, "%i", j);
				add_assoc_zval(zval_action_array, ulKey, zval_action_value);
			}
			add_assoc_zval(zval_prop_value, pulproptag, zval_action_array);
			break;
		case PT_SRESTRICTION:
			lpRestriction = (LPSRestriction)pPropValue->Value.lpszA;
			zval_action_value = NULL;
			MAPI_G(hr) = SRestrictiontoPHPArray(lpRestriction, 0, &zval_action_value TSRMLS_CC);
			if (MAPI_G(hr) != hrSuccess)
				continue;
			add_assoc_zval(zval_prop_value, pulproptag, zval_action_value);
			break;
		}
	}
	
	*pret = zval_prop_value;
	return MAPI_G(hr);
}

HRESULT RowSettoPHPArray(const SRowSet *lpRowSet, zval **pret TSRMLS_DC)
{
	zval	*zval_prop_value = NULL;
	ULONG	crow	= 0;
	zval 	*ret;
	
	MAPI_G(hr) = hrSuccess;

	MAKE_STD_ZVAL(ret);
	array_init(ret);

	// make a PHP-array from the rowset resource.
	for (crow = 0; crow < lpRowSet->cRows; ++crow) {
		PropValueArraytoPHPArray(lpRowSet->aRow[crow].cValues, lpRowSet->aRow[crow].lpProps, &zval_prop_value TSRMLS_CC);
		zend_hash_next_index_insert(HASH_OF(ret), &zval_prop_value, sizeof(zval *), NULL);
	}
	
	*pret = ret;
	
	return MAPI_G(hr);
}

/*
 * Convert from READSTATE array to PHP. Returns a list of arrays, each containing "sourcekey" and "flags" per entry
 */
HRESULT ReadStateArraytoPHPArray(ULONG cValues, const READSTATE *lpReadStates,
    zval **ppvalRet TSRMLS_DC)
{
	zval *pvalRet;
	unsigned int i=0;
	
	MAPI_G(hr) = hrSuccess;
	
	MAKE_STD_ZVAL(pvalRet);
	array_init(pvalRet);
	
	for (i = 0; i < cValues; ++i) {
		zval *pvalEntry;
		MAKE_STD_ZVAL(pvalEntry);
		array_init(pvalEntry);
		
		add_assoc_stringl(pvalEntry, "sourcekey", (char *)lpReadStates[i].pbSourceKey, lpReadStates[i].cbSourceKey, 1);
		add_assoc_long(pvalEntry, "flags", lpReadStates[i].ulFlags);
		
		add_next_index_zval(pvalRet, pvalEntry);
	}
	
	*ppvalRet = pvalRet;
	
	return MAPI_G(hr);
}

/*
 * Convert from PHP to READSTATE array.
 */
 
HRESULT PHPArraytoReadStateArray(zval *zvalReadStates, void *lpBase, ULONG *lpcValues, LPREADSTATE *lppReadStates TSRMLS_DC)
{
	LPREADSTATE 	lpReadStates = NULL;
	HashTable		*target_hash = NULL;
	int				count;
	zval			**ppentry = NULL;
	zval			*pentry = NULL;
	zval			**valueEntry = NULL;
	int				n = 0, i = 0;

	MAPI_G(hr) = hrSuccess;

	target_hash = HASH_OF(zvalReadStates);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No target_hash in PHPArraytoReadStateArray");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}
	
	count = zend_hash_num_elements(Z_ARRVAL_P(zvalReadStates));

	MAPI_G(hr) = MAPI_ALLOC(sizeof(READSTATE) * count, lpBase, (void **) &lpReadStates);
	if(MAPI_G(hr) != hrSuccess) 
		return MAPI_G(hr);

	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	for (i = 0; i < count; ++i) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&ppentry), &hpos);
		pentry = *ppentry;

		if(zend_hash_find(HASH_OF(pentry), "sourcekey", sizeof("sourcekey"), (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "No 'sourcekey' entry for one of the entries in the readstate list");
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
		
		convert_to_string_ex(valueEntry);
		
		MAPI_G(hr) = MAPIAllocateMore(valueEntry[0]->value.str.len, lpBase ? lpBase : lpReadStates, (void **) &lpReadStates[n].pbSourceKey);
		if(MAPI_G(hr) != hrSuccess)
			goto exit;
			
		memcpy(lpReadStates[n].pbSourceKey, valueEntry[0]->value.str.val, valueEntry[0]->value.str.len);
		lpReadStates[n].cbSourceKey = valueEntry[0]->value.str.len;
		
		if(zend_hash_find(HASH_OF(pentry), "flags", sizeof("flags"), (void **)&valueEntry) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "No 'flags' entry for one of the entries in the readstate list");
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
		
		convert_to_long_ex(valueEntry);
		lpReadStates[n++].ulFlags = valueEntry[0]->value.lval;
	}
	
	*lppReadStates = lpReadStates;
	*lpcValues = n;

exit:
	if (MAPI_G(hr) != hrSuccess && lpBase == NULL)
		MAPIFreeBuffer(lpReadStates);

	return MAPI_G(hr);
}

HRESULT PHPArraytoGUIDArray(zval *phpVal, void *lpBase, ULONG *lpcValues, LPGUID *lppGUIDs TSRMLS_DC)
{
	HashTable *target_hash = NULL;
	LPGUID lpGUIDs = NULL;
	int	count = 0;
	int n = 0;
	int i = 0;
	zval			**ppentry = NULL;
	zval			*pentry = NULL;

	MAPI_G(hr) = hrSuccess;

	target_hash = HASH_OF(phpVal);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No target_hash in PHPArraytoGUIDArray");
		return MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	}
	
	count = zend_hash_num_elements(Z_ARRVAL_P(phpVal));
	if (count == 0) {
		*lppGUIDs = NULL;
		*lpcValues = 0;
		return MAPI_G(hr);
	}

	MAPI_G(hr) = MAPI_ALLOC(sizeof(GUID) * count, lpBase, (void **) &lpGUIDs);
	if(MAPI_G(hr) != hrSuccess)
		return MAPI_G(hr);
	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	for (i = 0; i < count; ++i) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&ppentry), &hpos);
		pentry = *ppentry;

		convert_to_string_ex(&pentry);
		
		if(pentry->value.str.len != sizeof(GUID)){
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "GUID must be 16 bytes");
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
		
		memcpy(&lpGUIDs[n++], pentry->value.str.val, sizeof(GUID));
		zend_hash_move_forward_ex(target_hash, &hpos);
	}

	*lppGUIDs = lpGUIDs;
	*lpcValues = n;
exit:
	if (MAPI_G(hr) != hrSuccess && lpBase == NULL)
		MAPIFreeBuffer(lpGUIDs);
	return MAPI_G(hr);
}

HRESULT NotificationstoPHPArray(ULONG cNotifs, const NOTIFICATION *lpNotifs,
    zval **pret TSRMLS_DC)
{
	zval *zvalRet = NULL;
	zval *zvalProps = NULL;
	unsigned int i = 0;
	
	MAPI_G(hr) = hrSuccess;
	
	MAKE_STD_ZVAL(zvalRet);
	array_init(zvalRet);
	
	for (i = 0; i < cNotifs; ++i) {
		zval *zvalNotif = NULL;
		MAKE_STD_ZVAL(zvalNotif);
		array_init(zvalNotif);
		
		add_assoc_long(zvalNotif, "eventtype", lpNotifs[i].ulEventType);
		switch(lpNotifs[i].ulEventType) {
		case fnevNewMail:
			add_assoc_stringl(zvalNotif, "entryid", (char *)lpNotifs[i].info.newmail.lpEntryID, lpNotifs[i].info.newmail.cbEntryID, 1);
			add_assoc_stringl(zvalNotif, "parentid", (char *)lpNotifs[i].info.newmail.lpParentID, lpNotifs[i].info.newmail.cbParentID, 1);
			add_assoc_long(zvalNotif, "flags", lpNotifs[i].info.newmail.ulFlags);
			add_assoc_string(zvalNotif, "messageclass", (char *)lpNotifs[i].info.newmail.lpszMessageClass, 1);
			add_assoc_long(zvalNotif, "messageflags", lpNotifs[i].info.newmail.ulMessageFlags);
			break;
		case fnevObjectCreated:
		case fnevObjectDeleted:
		case fnevObjectModified:
		case fnevObjectMoved:
		case fnevObjectCopied:
		case fnevSearchComplete:
			if (lpNotifs[i].info.obj.lpEntryID)
				add_assoc_stringl(zvalNotif, "entryid", (char *)lpNotifs[i].info.obj.lpEntryID, lpNotifs[i].info.obj.cbEntryID, 1);
			add_assoc_long(zvalNotif, "objtype", lpNotifs[i].info.obj.ulObjType);
			if (lpNotifs[i].info.obj.lpParentID)
				add_assoc_stringl(zvalNotif, "parentid", (char *)lpNotifs[i].info.obj.lpParentID, lpNotifs[i].info.obj.cbParentID, 1);
			if (lpNotifs[i].info.obj.lpOldID)
				add_assoc_stringl(zvalNotif, "oldid", (char *)lpNotifs[i].info.obj.lpOldID, lpNotifs[i].info.obj.cbOldID, 1);
			if (lpNotifs[i].info.obj.lpOldParentID)
				add_assoc_stringl(zvalNotif, "oldparentid", (char *)lpNotifs[i].info.obj.lpOldParentID, lpNotifs[i].info.obj.cbOldParentID, 1);
			if (lpNotifs[i].info.obj.lpPropTagArray) {
				MAPI_G(hr) = PropTagArraytoPHPArray(lpNotifs[i].info.obj.lpPropTagArray->cValues, lpNotifs[i].info.obj.lpPropTagArray, &zvalProps TSRMLS_CC);
				if (MAPI_G(hr) != hrSuccess)
					return MAPI_G(hr);
				add_assoc_zval(zvalNotif, "proptagarray", zvalProps);
			}
			break;
		default:
			break;
		}
			
		add_next_index_zval(zvalRet, zvalNotif);
	}
	
	*pret = zvalRet;
	return MAPI_G(hr);
}

HRESULT PHPArraytoSendingOptions(zval *phpArray, sending_options *lpSOPT)
{
	HRESULT hr = hrSuccess;
	// local
	int				count;
	HashTable		*target_hash = NULL;
	zval			**entry = NULL;
	char			*keyIndex;
	ulong			numIndex = 0;

	if (!phpArray) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No phpArray in PHPArraytoSendingOptions");
		// not an error
		return hr;
	}

	target_hash = HASH_OF(phpArray);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No target_hash in PHPArraytoSendingOptions");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		return hr;
	}

	count = zend_hash_num_elements(target_hash);
	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	for (int i = 0; i < count; ++i) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos);
		zend_hash_get_current_key_ex(target_hash, &keyIndex, nullptr, &numIndex, 0, &hpos);

		if (strcmp(keyIndex, "alternate_boundary") == 0) {
			convert_to_string_ex(entry);
			lpSOPT->alternate_boundary = Z_STRVAL_PP(entry);
		} else if (strcmp(keyIndex, "no_recipients_workaround") == 0) {
			convert_to_boolean_ex(entry);
			lpSOPT->no_recipients_workaround = Z_BVAL_PP(entry);
		} else if (strcmp(keyIndex, "headers_only") == 0) {
			convert_to_boolean_ex(entry);
			lpSOPT->headers_only = Z_BVAL_PP(entry);
		} else if (strcmp(keyIndex, "add_received_date") == 0) {
			convert_to_boolean_ex(entry);
			lpSOPT->add_received_date = Z_BVAL_PP(entry);
		} else if (strcmp(keyIndex, "use_tnef") == 0) {
			convert_to_long_ex(entry);
			lpSOPT->use_tnef = Z_LVAL_PP(entry);
		} else if (strcmp(keyIndex, "charset_upgrade") == 0) {
			convert_to_string_ex(entry);
			lpSOPT->charset_upgrade = Z_STRVAL_PP(entry);
		} else if (strcmp(keyIndex, "allow_send_to_everyone") == 0) {
			convert_to_boolean_ex(entry);
			lpSOPT->allow_send_to_everyone = Z_BVAL_PP(entry);
		} else if (strcmp(keyIndex, "ignore_missing_attachments") == 0) {
			convert_to_boolean_ex(entry);
			lpSOPT->ignore_missing_attachments = Z_BVAL_PP(entry);
		} else {
			// msg_in_msg and enable_dsn not allowed, others unknown
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown or disallowed sending option %s", keyIndex);
		}

		zend_hash_move_forward_ex(target_hash, &hpos);
	}
	return hr;
}

HRESULT PHPArraytoDeliveryOptions(zval *phpArray, delivery_options *lpDOPT)
{
	HRESULT hr = hrSuccess;
	// local
	int				count;
	HashTable		*target_hash = NULL;
	zval			**entry = NULL;
	char			*keyIndex;
	ulong			numIndex = 0;

	if (!phpArray) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No phpArray in PHPArraytoDeliveryOptions");
		// not an error
		return hr;
	}

	target_hash = HASH_OF(phpArray);
	if (!target_hash) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No target_hash in PHPArraytoDeliveryOptions");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		return hr;
	}

	count = zend_hash_num_elements(target_hash);
	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	for (int i = 0; i < count; ++i) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos);
		zend_hash_get_current_key_ex(target_hash, &keyIndex, nullptr, &numIndex, 0, &hpos);

		if (strcmp(keyIndex, "use_received_date") == 0) {
			convert_to_boolean_ex(entry);
			lpDOPT->use_received_date = Z_BVAL_PP(entry);
		} else if (strcmp(keyIndex, "mark_as_read") == 0) {
			convert_to_boolean_ex(entry);
			lpDOPT->mark_as_read = Z_BVAL_PP(entry);
		} else if (strcmp(keyIndex, "add_imap_data") == 0) {
			convert_to_boolean_ex(entry);
			lpDOPT->add_imap_data = Z_BVAL_PP(entry);
		} else if (strcmp(keyIndex, "parse_smime_signed") == 0) {
			convert_to_boolean_ex(entry);
			lpDOPT->parse_smime_signed = Z_BVAL_PP(entry);
		} else if (strcmp(keyIndex, "default_charset") == 0) {
			convert_to_string_ex(entry);
			lpDOPT->ascii_upgrade = Z_STRVAL_PP(entry);
		} else if (strcmp(keyIndex, "header_strict_rfc") == 0) {
			convert_to_boolean_ex(entry);
			lpDOPT->header_strict_rfc = Z_BVAL_PP(entry);
		} else {
			// user_entryid not supported, others unknown
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown or disallowed delivery option %s", keyIndex);
		}

		zend_hash_move_forward_ex(target_hash, &hpos);
	}
	return hr;
}
