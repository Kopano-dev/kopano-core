/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef TYPECONVERSION_H
#define TYPECONVERSION_H

/*
 * These functions convert from MAPI types (structs) to PHP arrays and types and vice versa
 */

#include "globals.h"
ZEND_EXTERN_MODULE_GLOBALS(mapi)

#include <kopano/charset/convert.h>
#include <inetmapi/options.h>

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
 
extern HRESULT SBinaryArraytoPHPArray(const SBinaryArray *, zval *ret TSRMLS_DC);
extern HRESULT PropTagArraytoPHPArray(ULONG nvals, const SPropTagArray *, zval *ret TSRMLS_DC);
extern HRESULT PropValueArraytoPHPArray(ULONG nvals, const SPropValue *, zval *ret TSRMLS_DC);
extern HRESULT SRestrictiontoPHPArray(const SRestriction *, int level, zval *ret TSRMLS_DC);
extern HRESULT RowSettoPHPArray(const SRowSet *, zval *ret TSRMLS_DC);
extern HRESULT ReadStateArraytoPHPArray(ULONG nvals, const READSTATE *, zval *ret TSRMLS_DC);
extern HRESULT NotificationstoPHPArray(ULONG nvals, const NOTIFICATION *, zval *ret TSRMLS_DC);

#endif
