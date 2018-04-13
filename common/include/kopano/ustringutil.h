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

#ifndef ustringutil_INCLUDED
#define ustringutil_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include <string>

#include <unicode/coll.h>
#include <unicode/sortkey.h>
#include <unicode/unistr.h>

namespace KC {
using ECLocale = U_ICU_NAMESPACE::Locale;

// us-ascii strings
extern _kc_export const char *str_ifind(const char *haystack, const char *needle);

// Current locale strings
extern _kc_export bool str_equals(const char *, const char *, const ECLocale &);
extern _kc_export bool str_iequals(const char *, const char *, const ECLocale &);
extern _kc_export bool str_startswith(const char *, const char *, const ECLocale &);
extern _kc_export bool str_istartswith(const char *, const char *, const ECLocale &);
extern _kc_export int str_icompare(const char *, const char *, const ECLocale &);
extern _kc_export bool str_contains(const char *haystack, const char *needle, const ECLocale &);
extern _kc_export bool str_icontains(const char *haystack, const char *needle, const ECLocale &);

// Wide character strings
extern _kc_export bool wcs_equals(const wchar_t *s1, const wchar_t *s2, const ECLocale &locale);
extern _kc_export bool wcs_iequals(const wchar_t *, const wchar_t *, const ECLocale &);
extern _kc_export bool wcs_startswith(const wchar_t *, const wchar_t *, const ECLocale &);
extern _kc_export bool wcs_istartswith(const wchar_t *, const wchar_t *, const ECLocale &);
extern _kc_export int wcs_icompare(const wchar_t *, const wchar_t *, const ECLocale &);
extern _kc_export bool wcs_contains(const wchar_t *haystack, const wchar_t *needle, const ECLocale &);
extern _kc_export bool wcs_icontains(const wchar_t *haystack, const wchar_t *needle, const ECLocale &);

// UTF-8 strings
extern _kc_export bool u8_equals(const char *, const char *, const ECLocale &);
extern _kc_export bool u8_iequals(const char *, const char *, const ECLocale &);
extern _kc_export bool u8_startswith(const char *, const char *, const ECLocale &);
extern _kc_export bool u8_istartswith(const char *, const char *, const ECLocale &);
extern _kc_export int u8_icompare(const char *, const char *, const ECLocale &);
extern _kc_export bool u8_contains(const char *haystack, const char *needle, const ECLocale &);
extern _kc_export bool u8_icontains(const char *haystack, const char *needle, const ECLocale &);

extern _kc_export unsigned int u8_ncpy(const char *src, unsigned int n, std::string *dst);
extern _kc_export unsigned int u8_cappedbytes(const char *s, unsigned int max);
extern _kc_export unsigned int u8_len(const char *);

extern _kc_export ECLocale createLocaleFromName(const char *);
extern _kc_export ECRESULT LocaleIdToLCID(const char *locale, ULONG *id);
extern _kc_export ECRESULT LCIDToLocaleId(ULONG id, const char **locale);
extern _kc_export std::string createSortKeyDataFromUTF8(const char *s, int ncap, const ECLocale &);
extern _kc_export int compareSortKeys(const std::string &, const std::string &);
extern _kc_export std::string createSortKeyData(const char *s, int ncap, const ECLocale &);
extern _kc_export std::string createSortKeyData(const wchar_t *s, int ncap, const ECLocale &);

} /* namespace */

#endif // ndef ustringutil_INCLUDED
