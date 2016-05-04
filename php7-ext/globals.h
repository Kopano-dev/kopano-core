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
