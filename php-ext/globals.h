/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef GLOBALS_H
#define GLOBALS_H

ZEND_BEGIN_MODULE_GLOBALS(mapi)
// this is the global hresult value, used in *every* php mapi function
	HRESULT hr;
// this is a reference to the MAPI exception class
	zend_class_entry *exception_ce;
	bool exceptions_enabled;
ZEND_END_MODULE_GLOBALS(mapi)

// the 'v' is probably from 'value' .. who knows
#ifdef ZTS
#define MAPI_G(v) TSRMG(mapi_globals_id, zend_mapi_globals *, v)
#else
#define MAPI_G(v) (mapi_globals.v)
#endif

#endif
