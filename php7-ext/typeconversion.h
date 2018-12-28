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

#ifndef TYPECONVERSION_H
#define TYPECONVERSION_H

/*
 * These functions convert from MAPI types (structs) to PHP arrays and types and vice versa
 */

#include "globals.h"
ZEND_EXTERN_MODULE_GLOBALS(mapi)

#include <memory>
#include <kopano/charset/convert.h>
#include <inetmapi/options.h>

struct zstr_delete {
	public:
	void operator()(zend_string *s) { zend_string_release(s); }
};

struct zvalplus : public zval {
	zvalplus() { ZVAL_NULL(this); }
	~zvalplus() { zval_ptr_dtor(this); }
};

typedef std::unique_ptr<zend_string, zstr_delete> zstrplus;

/*
 * PHP -> MAPI
 *
 * All functions return a newly allocation MAPI structure which must be MAPIFreeBuffer()'ed by
 * the caller.
 */

// These allocate the structure and copy the data into it, then returns the entire allocated structure, allocation
// via lpBase if non null, otherwise just as MAPIAllocateBuffer

HRESULT			PHPArraytoSBinaryArray(zval * entryid_array, void *lpBase, LPENTRYLIST *lppEntryList TSRMLS_DC);
HRESULT			PHPArraytoSortOrderSet(zval * sortorder_array, void *lpBase, LPSSortOrderSet *lppSortOrderSet TSRMLS_DC);
HRESULT			PHPArraytoPropTagArray(zval * prop_value_array, void *lpBase, LPSPropTagArray *lppPropTagArray TSRMLS_DC);
HRESULT			PHPArraytoPropValueArray(zval* phpArray, void *lpBase, ULONG *lpcValues, LPSPropValue *lppPropValues TSRMLS_DC);
HRESULT			PHPArraytoAdrList(zval *phpArray, void *lpBase, LPADRLIST *lppAdrList TSRMLS_DC);
HRESULT			PHPArraytoRowList(zval *phpArray, void *lpBase, LPROWLIST *lppRowList TSRMLS_DC);
HRESULT			PHPArraytoSRestriction(zval *phpVal, void *lpBase, LPSRestriction *lppRestriction TSRMLS_DC);
HRESULT			PHPArraytoReadStateArray(zval *phpVal, void *lpBase, ULONG *lpcValues, LPREADSTATE *lppReadStates TSRMLS_DC);
HRESULT			PHPArraytoGUIDArray(zval *phpVal, void *lpBase, ULONG *lpcValues, LPGUID *lppGUIDs TSRMLS_DC);

// These functions fill a pre-allocated structure, possibly allocating more memory via lpBase

HRESULT		 	PHPArraytoSBinaryArray(zval * entryid_array, void *lpBase, LPENTRYLIST lpEntryList TSRMLS_DC);
extern HRESULT PHPArraytoSRestriction(zval *, void *base, SRestriction * TSRMLS_DC) __attribute__((nonnull(2)));

/* imtoinet, imtomapi options */
extern HRESULT PHPArraytoSendingOptions(zval *, KC::sending_options *);
extern HRESULT PHPArraytoDeliveryOptions(zval *, KC::delivery_options *);

/*
 * MAPI -> PHP
 *
 * All functions return a newly allocated ZVAL structure which must be FREE_ZVAL()'ed by the caller.
 */
 
HRESULT			SBinaryArraytoPHPArray(SBinaryArray *lpBinaryArray, zval *ret TSRMLS_DC);
HRESULT			PropTagArraytoPHPArray(ULONG cValues, LPSPropTagArray lpPropTagArray, zval *ret TSRMLS_DC);
HRESULT			PropValueArraytoPHPArray(ULONG cValues, LPSPropValue pPropValueArray, zval *ret TSRMLS_DC);
HRESULT 		SRestrictiontoPHPArray(LPSRestriction lpRes, int level, zval *ret TSRMLS_DC);
HRESULT			RowSettoPHPArray(LPSRowSet lpRowSet, zval *ret TSRMLS_DC);
HRESULT 		ReadStateArraytoPHPArray(ULONG cValues, LPREADSTATE lpReadStates, zval *ret TSRMLS_DC);
HRESULT			NotificationstoPHPArray(ULONG cNotifs, LPNOTIFICATION lpNotifs, zval *ret TSRMLS_DC);

#endif
