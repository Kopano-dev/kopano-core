/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef EC_PHPEXT_UTIL_H
#define EC_PHPEXT_UTIL_H

#include <string>

HRESULT mapi_util_createprof(const char *szProfName, const char *szServiceName, ULONG cValues, LPSPropValue lpPropVals);
HRESULT mapi_util_deleteprof(const char *szProfName);
std::string mapi_util_getlasterror();

#endif
