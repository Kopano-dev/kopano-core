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

#ifndef __M4L_UTIL_IMPL_H
#define __M4L_UTIL_IMPL_H

#include <mapiutil.h>



/*
 * Define some additional functions which are not defined in the Microsoft mapiutil.h
 * Although they are in fact mapiutil functions hidden somewhere in mapi32.
 */
STDAPI_(int) MNLS_CompareStringW(LCID Locale, DWORD dwCmpFlags, LPCWSTR lpString1, int cchCount1, LPCWSTR lpString2, int cchCount2);
STDAPI_(int) MNLS_lstrlenW(LPCWSTR lpString);
STDAPI_(int) MNLS_lstrlen(LPCSTR lpString);
STDAPI_(int) MNLS_lstrcmpW(LPCWSTR lpString1, LPCWSTR lpString2);
STDAPI_(LPWSTR) MNLS_lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2);

STDAPI_(ULONG) CbOfEncoded(LPCSTR lpszEnc);
STDAPI_(ULONG) CchOfEncoding(LPCSTR lpszEnd);
STDAPI_(LPWSTR) EncodeID(ULONG cbEID, LPENTRYID rgbID, LPWSTR *lpWString);
STDAPI_(void) FDecodeID(LPCSTR lpwEncoded, LPENTRYID *lpDecoded, ULONG *cbEncoded);

STDAPI_(FILETIME) FtDivFtBogus(FILETIME f, FILETIME f2, DWORD n);


STDAPI_(BOOL) FBadRglpszA(LPTSTR *lppszT, ULONG cStrings);
STDAPI_(BOOL) FBadRglpszW(LPWSTR *lppszW, ULONG cStrings);
STDAPI_(BOOL) FBadRowSet(LPSRowSet lpRowSet);
STDAPI_(BOOL) FBadRglpNameID(LPMAPINAMEID *lppNameId, ULONG cNames);
STDAPI_(BOOL) FBadEntryList(LPENTRYLIST lpEntryList);
STDAPI_(ULONG) FBadRestriction(LPSRestriction lpres);
STDAPI_(ULONG) FBadPropTag(ULONG ulPropTag);
STDAPI_(ULONG) FBadRow(LPSRow lprow);
STDAPI_(ULONG) FBadProp(LPSPropValue lpprop);
STDAPI_(ULONG) FBadSortOrderSet(LPSSortOrderSet lpsos);
extern STDAPI_(ULONG) FBadColumnSet(const SPropTagArray *lpptaCols);

/*
 * Non mapi32 utility function (only used internally withint M4L)
 */
HRESULT GetConnectionProperties(LPSPropValue lpServer, LPSPropValue lpUsername, ULONG *lpcValues, LPSPropValue *lppProps);

#endif
