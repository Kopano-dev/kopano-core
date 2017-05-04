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

#ifndef __PHP_EXT_UTIL_H
#define __PHP_EXT_UTIL_H

#include <string>

HRESULT mapi_util_createprof(const char *szProfName, const char *szServiceName, ULONG cValues, LPSPropValue lpPropVals);
HRESULT mapi_util_deleteprof(const char *szProfName);
std::string mapi_util_getlasterror();

#endif
