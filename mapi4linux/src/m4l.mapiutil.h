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
STDAPI_(ULONG) CbOfEncoded(LPCSTR lpszEnc);
STDAPI_(ULONG) CchOfEncoding(LPCSTR lpszEnd);
STDAPI_(LPWSTR) EncodeID(ULONG cbEID, LPENTRYID rgbID, LPWSTR *lpWString);
STDAPI_(void) FDecodeID(LPCSTR lpwEncoded, LPENTRYID *lpDecoded, ULONG *cbEncoded);

STDAPI_(FILETIME) FtDivFtBogus(FILETIME f, FILETIME f2, DWORD n);

extern STDAPI_(BOOL) FBadRglpszA(const TCHAR *, ULONG cStrings);
extern STDAPI_(BOOL) FBadRglpszW(const wchar_t *, ULONG cStrings);
extern STDAPI_(BOOL) FBadRowSet(const SRowSet *);
STDAPI_(BOOL) FBadRglpNameID(LPMAPINAMEID *lppNameId, ULONG cNames);
extern STDAPI_(BOOL) FBadEntryList(const SBinaryArray *);
extern STDAPI_(ULONG) FBadRestriction(const SRestriction *);
STDAPI_(ULONG) FBadPropTag(ULONG ulPropTag);
extern STDAPI_(ULONG) FBadRow(const SRow *);
extern STDAPI_(ULONG) FBadProp(const SPropValue *);
extern STDAPI_(ULONG) FBadSortOrderSet(const SSortOrderSet *);
extern STDAPI_(ULONG) FBadColumnSet(const SPropTagArray *lpptaCols);

/*
 * Non mapi32 utility function (only used internally withint M4L)
 */
HRESULT GetConnectionProperties(LPSPropValue lpServer, LPSPropValue lpUsername, ULONG *lpcValues, LPSPropValue *lppProps);

#endif
