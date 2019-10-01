/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
extern KC_EXPORT const char *str_ifind(const char *haystack, const char *needle);

// Current locale strings
extern KC_EXPORT bool str_equals(const char *, const char *, const ECLocale &);
extern KC_EXPORT bool str_iequals(const char *, const char *, const ECLocale &);
extern KC_EXPORT bool str_startswith(const char *, const char *, const ECLocale &);
extern KC_EXPORT bool str_istartswith(const char *, const char *, const ECLocale &);
extern KC_EXPORT int str_icompare(const char *, const char *, const ECLocale &);
extern KC_EXPORT bool str_contains(const char *haystack, const char *needle, const ECLocale &);
extern KC_EXPORT bool str_icontains(const char *haystack, const char *needle, const ECLocale &);

// Wide character strings
extern KC_EXPORT bool wcs_equals(const wchar_t *s1, const wchar_t *s2, const ECLocale &locale);
extern KC_EXPORT bool wcs_iequals(const wchar_t *, const wchar_t *, const ECLocale &);
extern KC_EXPORT bool wcs_startswith(const wchar_t *, const wchar_t *, const ECLocale &);
extern KC_EXPORT bool wcs_istartswith(const wchar_t *, const wchar_t *, const ECLocale &);
extern KC_EXPORT int wcs_icompare(const wchar_t *, const wchar_t *, const ECLocale &);
extern KC_EXPORT bool wcs_contains(const wchar_t *haystack, const wchar_t *needle, const ECLocale &);
extern KC_EXPORT bool wcs_icontains(const wchar_t *haystack, const wchar_t *needle, const ECLocale &);

// UTF-8 strings
extern KC_EXPORT bool u8_equals(const char *, const char *, const ECLocale &);
extern KC_EXPORT bool u8_iequals(const char *, const char *, const ECLocale &);
extern KC_EXPORT bool u8_startswith(const char *, const char *, const ECLocale &);
extern KC_EXPORT bool u8_istartswith(const char *, const char *, const ECLocale &);
extern KC_EXPORT int u8_icompare(const char *, const char *, const ECLocale &);
extern KC_EXPORT bool u8_contains(const char *haystack, const char *needle, const ECLocale &);
extern KC_EXPORT bool u8_icontains(const char *haystack, const char *needle, const ECLocale &);
extern KC_EXPORT size_t u8_cappedbytes(const char *s, size_t max);
extern KC_EXPORT size_t u8_len(const char *, size_t = ~0ULL);
extern KC_EXPORT wchar_t u8_readbyte(const char *&);
extern KC_EXPORT ECLocale createLocaleFromName(const char *);
extern KC_EXPORT ECRESULT LocaleIdToLCID(const char *locale, unsigned int *id);
extern KC_EXPORT ECRESULT LCIDToLocaleId(unsigned int id, const char **locale);
extern KC_EXPORT std::string createSortKeyDataFromUTF8(const char *s, int ncap, const ECLocale &);
extern KC_EXPORT int compareSortKeys(const std::string &, const std::string &);
extern KC_EXPORT std::string createSortKeyData(const char *s, int ncap, const ECLocale &);
extern KC_EXPORT std::string createSortKeyData(const wchar_t *s, int ncap, const ECLocale &);

} /* namespace */

#endif // ndef ustringutil_INCLUDED
