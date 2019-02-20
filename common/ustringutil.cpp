/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
@file
Unicode String Utilities

@defgroup ustringutil Unicode String Utilities
@{

The Unicode String Utilities provide some common string utilities aimed to be compliant with
all (or at least most) of the Unicode quirks.

The provided functions are:
  - str_equals, wcs_equals, u8_equals: Check if two strings are equal.
  - str_iequals, wcs_iequals, u8_iequals: Check if two strings are equal ignoring case.
  - str_startswith, wcs_startswith, u8_startswith: Check if one string starts with another.
  - str_istartswith, wcs_istartswith, u8_istartswith: Check if one string starts with another ignoring case.
  - str_icompare, wcs_icompare, u8_icompare: Compare two strings ignoring case.
  - str_contains, wcs_contains, u8_contains: Check if one string contains the other.
  - str_icontains, wcs_icontains, u8_icontains: Check if one string contains the other ignoring case.

@par Normalization
In order to compare unicode strings, the data needs to be normailized first. This is needed because Unicode allows
different binary representations of the same data. The functions provide in this module make no assumptions about
the provided data and will always perform a normalization before doing a comparison.

@par Case mapping
The case insensitive functions need a way to match code points regardless of their case. ICU provides a few methods for
this, but they use a method called case-folding to avoid the need for a locale (changing case is dependent on a locale).
Since case-folding doesn't take a locale, it's a best guess method, which will produce wrong results in certain situations.
The functions in this library apply a method called case-mapping, which basically means we perform a to-upper on all
code-points with a provided locale.

@par Collation
The functions that try to match (sub)strings, have no interest in the order in which strings would appear if they would be
sorted. However, the compare functions do produce a result that could be used for sorting. Since sorting is dependent on a
locale as well, they would need a locale. However, ICU provides a Collator class that performs the actual comparison for a
particular locale. Since we don't want to construct a Collator class for every string comparison, the string comparison
functions take a Collator object as argument. This way the caller can reuse the Collator.

@par Performance
Performance of the current (21-05-2010) implementation is probably pretty bad. This is caused by all the conversion that are
performed on the complete strings before the actual comparison is even started.

At some point we need to rewqrite these functions to do all the conversion on the fly to minimize processing.
*/
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include <kopano/platform.h>
#include <kopano/ustringutil.h>
#include <kopano/CommonUtil.h>
#include <cassert>
#include <clocale>
#include <memory>
#include <string>
#include <unicode/unorm.h>
#include <unicode/coll.h>
#include <unicode/tblcoll.h>
#include <unicode/coleitr.h>
#include <unicode/normlzr.h>
#include <unicode/ustring.h>
#include <kopano/charset/convert.h>

using U_ICU_NAMESPACE::CollationKey;
using U_ICU_NAMESPACE::Collator;
using U_ICU_NAMESPACE::Locale;
using U_ICU_NAMESPACE::UnicodeString;
typedef std::unique_ptr<Collator> unique_ptr_Collator;

namespace KC {

/**
 * US-ASCII version to find a case-insensitive string part in a
 * haystack.
 *
 * @param haystack search this haystack for a case-insensitive needle
 * @param needle search this needle in the case-insensitive haystack
 *
 * @return pointer where needle is found or NULL
 */
const char* str_ifind(const char *haystack, const char *needle)
{
	auto loc = newlocale(LC_CTYPE_MASK, "C", nullptr);
	const char *needlepos = needle;
	const char *needlestart = haystack;

	while(*haystack) {
		if (toupper_l(*haystack, loc) == toupper_l(*needlepos, loc)) {
			++needlepos;
			if(*needlepos == 0)
				goto exit;
		} else {
			haystack = needlestart++;
			needlepos = needle;
		}

		++haystack;
	}
	needlestart = NULL;
exit:
	freelocale(loc);
	return needlestart;
}

static inline UnicodeString StringToUnicode(const char *sz)
{
	// *tocode, const _From_Type &_from, size_t cbBytes, const char *fromcode
	auto strUTF16 = convert_context().convert_to<std::string>("UTF-16LE", sz, rawsize(sz), "");
	return UnicodeString(reinterpret_cast<const UChar *>(strUTF16.data()), strUTF16.length() / 2);
}

static inline UnicodeString UTF8ToUnicode(const char *utf8)
{
	return UnicodeString::fromUTF8(utf8);
}

static inline UnicodeString WCHARToUnicode(const wchar_t *sz)
{
	return UnicodeString::fromUTF32(reinterpret_cast<const UChar32 *>(sz), -1);
}

/**
 * Check if two strings are canonical equivalent.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to perform string collation.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool str_equals(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = StringToUnicode(s1);
    UnicodeString b = StringToUnicode(s2);
    return a.compare(b) == 0;
}

/**
 * Check if two strings are canonical equivalent when ignoring the case.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to convert the case of the strings.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool str_iequals(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = StringToUnicode(s1);
    UnicodeString b = StringToUnicode(s2);
    return a.caseCompare(b, 0) == 0;
}

/**
 * Check if the string s1 starts with s2.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to perform string collation.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool str_startswith(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = StringToUnicode(s1);
    UnicodeString b = StringToUnicode(s2);
    return a.compare(0, b.length(), b) == 0;
}

/**
 * Check if the string s1 starts with s2 when ignoring the case.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to convert the case of the strings.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool str_istartswith(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = StringToUnicode(s1);
    UnicodeString b = StringToUnicode(s2);
    return a.caseCompare(0, b.length(), b, 0) == 0;
}

/**
 * Compare two strings using the collator to determine the sort order.
 *
 * Both strings are expectes to be in the current locale. The comparison is
 * case insensitive. Effectively this only changes behavior compared to strcmp_unicode
 * if the two strings are the same if the case is discarded. It doesn't effect the
 * sorting in any other way.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	collator	The collator used to determine which string precedes the other.
 *
 * @return		An integer.
 * @retval		-1	s1 is smaller than s2
 * @retval		0	s1 equals s2.
 * @retval		1	s1 is greater than s2
 */
int str_icompare(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
	UErrorCode status = U_ZERO_ERROR;
	unique_ptr_Collator ptrCollator(Collator::createInstance(locale, status));

	UnicodeString a = StringToUnicode(s1);
	UnicodeString b = StringToUnicode(s2);
	a.foldCase();
	b.foldCase();
	return ptrCollator->compare(a,b,status);
}

/**
 * Find a string in another string.
 *
 * @param[in]	haystack	The string to search in
 * @param[in]	needle		The string to search for.
 * @param[in]	locale		The locale used to perform string collation.
 *
 * @return boolean
 * @retval	true	The needle was found
 * @retval	false	The needle wasn't found
 *
 * @note This function behaves different than strstr in that it returns a
 *       a boolean instead of a pointer to the found substring. This is
 *       because we search on a transformed string. Getting the correct
 *       pointer would involve additional processing while we don't need
 *       the result anyway.
 */
bool str_contains(const char *haystack, const char *needle, const ECLocale &locale)
{
	assert(haystack);
	assert(needle);
    UnicodeString a = StringToUnicode(haystack);
    UnicodeString b = StringToUnicode(needle);
    return u_strstr(a.getTerminatedBuffer(), b.getTerminatedBuffer());
}

/**
 * Find a string in another string while ignoreing case.
 *
 * @param[in]	haystack	The string to search in
 * @param[in]	needle		The string to search for.
 * @param[in]	locale		The locale used to convert the case of the strings.
 *
 * @return boolean
 * @retval	true	The needle was found
 * @retval	false	The needle wasn't found
 */
bool str_icontains(const char *haystack, const char *needle, const ECLocale &locale)
{
	assert(haystack);
	assert(needle);
    UnicodeString a = StringToUnicode(haystack);
    UnicodeString b = StringToUnicode(needle);

    a.foldCase();
    b.foldCase();
    return u_strstr(a.getTerminatedBuffer(), b.getTerminatedBuffer());
}

/**
 * Check if two strings are canonical equivalent.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to perform string collation.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool wcs_equals(const wchar_t *s1, const wchar_t *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = WCHARToUnicode(s1);
    UnicodeString b = WCHARToUnicode(s2);
    return a.compare(b) == 0;
}

/**
 * Check if two strings are canonical equivalent when ignoring the case.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to convert the case of the strings.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool wcs_iequals(const wchar_t *s1, const wchar_t *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = WCHARToUnicode(s1);
    UnicodeString b = WCHARToUnicode(s2);
    return a.caseCompare(b, 0) == 0;
}

/**
 * Check if s1 starts with s2.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to perform string collation.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool wcs_startswith(const wchar_t *s1, const wchar_t *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = WCHARToUnicode(s1);
    UnicodeString b = WCHARToUnicode(s2);
    return a.compare(0, b.length(), b) == 0;
}

/**
 * Check if s1 starts with s2 when ignoring the case.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to convert the case of the strings.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool wcs_istartswith(const wchar_t *s1, const wchar_t *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = WCHARToUnicode(s1);
    UnicodeString b = WCHARToUnicode(s2);
    return a.caseCompare(0, b.length(), b, 0) == 0;
}

/**
 * Compare two strings using the collator to determine the sort order.
 *
 * Both strings are expectes to be in the current locale. The comparison is
 * case insensitive. Effectively this only changes behavior compared to strcmp_unicode
 * if the two strings are the same if the case is discarded. It doesn't effect the
 * sorting in any other way.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	collator	The collator used to determine which string precedes the other.
 *
 * @return		An integer.
 * @retval		-1	s1 is smaller than s2
 * @retval		0	s1 equals s2.
 * @retval		1	s1 is greater than s2
 */
int wcs_icompare(const wchar_t *s1, const wchar_t *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
	UErrorCode status = U_ZERO_ERROR;
	unique_ptr_Collator ptrCollator(Collator::createInstance(locale, status));

	UnicodeString a = WCHARToUnicode(s1);
	UnicodeString b = WCHARToUnicode(s2);
	a.foldCase();
	b.foldCase();
	return ptrCollator->compare(a,b,status);
}

/**
 * Find a string in another string.
 *
 * @param[in]	haystack	The string to search in
 * @param[in]	needle		The string to search for.
 * @param[in]	locale		The locale used to perform string collation.
 *
 * @return boolean
 * @retval	true	The needle was found
 * @retval	false	The needle wasn't found
 *
 * @note This function behaves different than strstr in that it returns a
 *       a boolean instead of a pointer to the found substring. This is
 *       because we search on a transformed string. Getting the correct
 *       pointer would involve additional processing while we don't need
 *       the result anyway.
 */
bool wcs_contains(const wchar_t *haystack, const wchar_t *needle, const ECLocale &locale)
{
	assert(haystack);
	assert(needle);
    UnicodeString a = WCHARToUnicode(haystack);
    UnicodeString b = WCHARToUnicode(needle);
    return u_strstr(a.getTerminatedBuffer(), b.getTerminatedBuffer());
}

/**
 * Find a string in another string while ignoreing case.
 *
 * @param[in]	haystack	The string to search in
 * @param[in]	needle		The string to search for.
 * @param[in]	locale		The locale to use when converting case.
 *
 * @return boolean
 * @retval	true	The needle was found
 * @retval	false	The needle wasn't found
 *
 * @note This function behaves different than strstr in that it returns a
 *       a boolean instead of a pointer to the found substring. This is
 *       because we search on a transformed string. Getting the correct
 *       pointer would involve additional processing while we don't need
 *       the result anyway.
 */
bool wcs_icontains(const wchar_t *haystack, const wchar_t *needle, const ECLocale &locale)
{
	assert(haystack);
	assert(needle);
    UnicodeString a = WCHARToUnicode(haystack);
    UnicodeString b = WCHARToUnicode(needle);

    a.foldCase();
    b.foldCase();
    return u_strstr(a.getTerminatedBuffer(), b.getTerminatedBuffer());
}

/**
 * Check if two strings are canonical equivalent.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to perform string collation.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool u8_equals(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = UTF8ToUnicode(s1);
    UnicodeString b = UTF8ToUnicode(s2);
    return a.compare(b) == 0;
}

/**
 * Check if two strings are canonical equivalent when ignoring the case.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale to use when converting case.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool u8_iequals(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = UTF8ToUnicode(s1);
    UnicodeString b = UTF8ToUnicode(s2);
    return a.caseCompare(b, 0) == 0;
}

/**
 * Check if s1 starts with s2.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale used to perform string collation.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool u8_startswith(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = UTF8ToUnicode(s1);
    UnicodeString b = UTF8ToUnicode(s2);
    return a.compare(0, b.length(), b) == 0;
}

/**
 * Check if s1 starts with s2 when ignoring the case.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	locale	The locale to use when converting case.
 *
 * @return	boolean
 * @retval	true	The strings are canonical equivalent
 * @retval	false	The strings are not canonical equivalent
 */
bool u8_istartswith(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
    UnicodeString a = UTF8ToUnicode(s1);
    UnicodeString b = UTF8ToUnicode(s2);
    return a.caseCompare(0, b.length(), b, 0) == 0;
}

/**
 * Compare two strings using the collator to determine the sort order.
 *
 * Both strings are expectes to be encoded in UTF-8. The comparison is
 * case insensitive. Effectively this only changes behavior compared to strcmp_unicode
 * if the two strings are the same if the case is discarded. It doesn't effect the
 * sorting in any other way.
 *
 * @param[in]	s1		The string to compare s2 with.
 * @param[in]	s2		The string to compare s1 with.
 * @param[in]	collator	The collator used to determine which string precedes the other.
 *
 * @return		An integer.
 * @retval		-1	s1 is smaller than s2
 * @retval		0	s1 equals s2.
 * @retval		1	s1 is greater than s2
 */
int u8_icompare(const char *s1, const char *s2, const ECLocale &locale)
{
	assert(s1);
	assert(s2);
	UErrorCode status = U_ZERO_ERROR;
	unique_ptr_Collator ptrCollator(Collator::createInstance(locale, status));

	UnicodeString a = UTF8ToUnicode(s1);
	UnicodeString b = UTF8ToUnicode(s2);
	a.foldCase();
	b.foldCase();
	return ptrCollator->compare(a,b,status);
}

/**
 * Find a string in another string.
 *
 * @param[in]	haystack	The string to search in
 * @param[in]	needle		The string to search for.
 * @param[in]	locale		The locale used to perform string collation.
 *
 * @return boolean
 * @retval	true	The needle was found
 * @retval	false	The needle wasn't found
 *
 * @note This function behaves different than strstr in that it returns a
 *       a boolean instead of a pointer to the found substring. This is
 *       because we search on a transformed string. Getting the correct
 *       pointer would involve additional processing while we don't need
 *       the result anyway.
 */
bool u8_contains(const char *haystack, const char *needle, const ECLocale &locale)
{
	assert(haystack);
	assert(needle);
    UnicodeString a = UTF8ToUnicode(haystack);
    UnicodeString b = UTF8ToUnicode(needle);
    return u_strstr(a.getTerminatedBuffer(), b.getTerminatedBuffer());
}

/**
 * Find a string in another string while ignoreing case.
 *
 * @param[in]	haystack	The string to search in
 * @param[in]	needle		The string to search for.
 * @param[in]	locale		The locale to use when converting case.
 *
 * @return boolean
 * @retval	true	The needle was found
 * @retval	false	The needle wasn't found
 */
bool u8_icontains(const char *haystack, const char *needle, const ECLocale &locale)
{
	assert(haystack);
	assert(needle);
    UnicodeString a = UTF8ToUnicode(haystack);
    UnicodeString b = UTF8ToUnicode(needle);

    a.foldCase();
    b.foldCase();
    return u_strstr(a.getTerminatedBuffer(), b.getTerminatedBuffer());
}

static const uint8_t utf8_widths[] = {
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,1,1,
};

/**
 * Returns the length in bytes of the string s when capped to a maximum of
 * max characters.
 *
 * @param[in]	s		The UTF-8 string to process
 * @param[in]	max		The maximum amount of characters for which to return
 * 						the length in bytes.
 *
 * @return	The length in bytes of the capped string.
 */
size_t u8_cappedbytes(const char *src, size_t max)
{
	auto it = src;
	for (auto z = strlen(src); z > 0 && max > 0; --max) {
		uint8_t ch = *it;
		size_t nob = ch < 0xC0 ? 1 : utf8_widths[ch-0xC0];
		if (nob > z)
			break;
		it += nob;
		z -= nob;
	}
	return it - src;
}

/**
 * Returns the length in characters of the passed UTF-8 string s
 *
 * @param[in]	s	The UTF-8 string to get length of.
 *
 * @return	The length in characters of string s
 */
size_t u8_len(const char *src, size_t max)
{
	auto it = src;
	size_t u = 0;
	for (auto z = strlen(src); u < max && z > 0; ++u) {
		uint8_t ch = *it;
		size_t nob = ch < 0xC0 ? 1 : utf8_widths[ch-0xC0];
		if (nob > z)
			break;
		it += nob;
		z -= nob;
	}
	return u;
}

static const struct localemap {
	const char *lpszLocaleID;	/*< Posix locale id */
	ULONG ulLCID;				/*< Windows LCID */
	const char *lpszLocaleName;	/*< Windows locale name */
} localeMap[] = {
	{"af",54,"Afrikaans_South Africa"},
	{"af_NA",54,"Afrikaans_South Africa"},
	{"af_ZA",1078,"Afrikaans_South Africa"},
	{"ar",1,"Arabic_Saudi Arabia"},
	{"ar_BH",15361,"Arabic_Bahrain"},
	{"ar_DZ",5121,"Arabic_Algeria"},
	{"ar_EG",3073,"Arabic_Egypt"},
	{"ar_IQ",2049,"Arabic_Iraq"},
	{"ar_JO",11265,"Arabic_Jordan"},
	{"ar_KW",13313,"Arabic_Kuwait"},
	{"ar_LB",12289,"Arabic_Lebanon"},
	{"ar_LY",4097,"Arabic_Libya"},
	{"ar_MA",6145,"Arabic_Morocco"},
	{"ar_OM",8193,"Arabic_Oman"},
	{"ar_QA",16385,"Arabic_Qatar"},
	{"ar_SA",1025,"Arabic_Saudi Arabia"},
	{"ar_SD",1,"Arabic_Saudi Arabia"},
	{"ar_SY",10241,"Arabic_Syria"},
	{"ar_TN",7169,"Arabic_Tunisia"},
	{"ar_YE",9217,"Arabic_Yemen"},
	{"az",44,"Azeri (Latin)_Azerbaijan"},
	{"az_Cyrl_AZ",2092,"Azeri (Cyrillic)_Azerbaijan"},
	{"az_Latn_AZ",1068,"Azeri (Latin)_Azerbaijan"},
	{"be",35,"Belarusian_Belarus"},
	{"be_BY",1059,"Belarusian_Belarus"},
	{"bg",2,"Bulgarian_Bulgaria"},
	{"bg_BG",1026,"Bulgarian_Bulgaria"},
	{"ca",3,"Catalan_Spain"},
	{"ca_ES",1027,"Catalan_Spain"},
	{"cs",5,"Czech_Czech Republic"},
	{"cs_CZ",1029,"Czech_Czech Republic"},
	{"cy",82,"Welsh_United Kingdom"},
	{"cy_GB",1106,"Welsh_United Kingdom"},
	{"da",6,"Danish_Denmark"},
	{"da_DK",1030,"Danish_Denmark"},
	{"de",7,"German_Germany"},
	{"de_AT",3079,"German_Austria"},
	{"de_BE",7,"German_Germany"},
	{"de_CH",2055,"German_Switzerland"},
	{"de_DE",1031,"German_Germany"},
	{"de_LI",5127,"German_Liechtenstein"},
	{"de_LU",4103,"German_Luxembourg"},
	{"el",8,"Greek_Greece"},
	{"el_CY",8,"Greek_Greece"},
	{"el_GR",1032,"Greek_Greece"},
	{"en",9,"English_United States"},
	{"en_AU",3081,"English_Australia"},
	{"en_BE",9,"English_United States"},
	{"en_BW",9,"English_United States"},
	{"en_BZ",10249,"English_Belize"},
	{"en_CA",4105,"English_Canada"},
	{"en_GB",2057,"English_United Kingdom"},
	{"en_HK",9,"English_United States"},
	{"en_IE",6153,"English_Ireland"},
	{"en_JM",8201,"English_Jamaica"},
	{"en_MH",1033,"English_United States"},
	{"en_MT",9,"English_United States"},
	{"en_MU",9,"English_United States"},
	{"en_NA",9,"English_United States"},
	{"en_NZ",5129,"English_New Zealand"},
	{"en_PH",13321,"English_Republic of the Philippines"},
	{"en_PK",9,"English_United States"},
	{"en_TT",11273,"English_Trinidad and Tobago"},
	{"en_US",1033,"English_United States"},
	{"en_VI",9225,"English_Caribbean"},
	{"en_ZA",7177,"English_South Africa"},
	{"en_ZW",12297,"English_Zimbabwe"},
	{"es",10,"Spanish_Spain"},
	{"es_AR",11274,"Spanish_Argentina"},
	{"es_BO",16394,"Spanish_Bolivia"},
	{"es_CL",13322,"Spanish_Chile"},
	{"es_CO",9226,"Spanish_Colombia"},
	{"es_CR",5130,"Spanish_Costa Rica"},
	{"es_DO",7178,"Spanish_Dominican Republic"},
	{"es_EC",12298,"Spanish_Ecuador"},
	{"es_ES",3082,"Spanish_Spain"},
	{"es_GQ",10,"Spanish_Spain"},
	{"es_GT",4106,"Spanish_Guatemala"},
	{"es_HN",18442,"Spanish_Honduras"},
	{"es_MX",2058,"Spanish_Mexico"},
	{"es_NI",19466,"Spanish_Nicaragua"},
	{"es_PA",6154,"Spanish_Panama"},
	{"es_PE",10250,"Spanish_Peru"},
	{"es_PR",20490,"Spanish_Puerto Rico"},
	{"es_PY",15370,"Spanish_Paraguay"},
	{"es_SV",17418,"Spanish_El Salvador"},
	{"es_UY",14346,"Spanish_Uruguay"},
	{"es_VE",8202,"Spanish_Venezuela"},
	{"et",37,"Estonian_Estonia"},
	{"et_EE",1061,"Estonian_Estonia"},
	{"eu",45,"Basque_Spain"},
	{"eu_ES",1069,"Basque_Spain"},
	{"fa",41,"Farsi_Iran"},
	{"fa_IR",1065,"Farsi_Iran"},
	{"fi",11,"Finnish_Finland"},
	{"fi_FI",1035,"Finnish_Finland"},
	{"fil",100,"Filipino_Philippines"},
	{"fil_PH",1124,"Filipino_Philippines"},
	{"fo",56,"Faroese_Faroe Islands"},
	{"fo_FO",1080,"Faroese_Faroe Islands"},
	{"fr",12,"French_France"},
	{"fr_BE",2060,"French_Belgium"},
	{"fr_BL",12,"French_France"},
	{"fr_CA",3084,"French_Canada"},
	{"fr_CF",12,"French_France"},
	{"fr_CH",4108,"French_Switzerland"},
	{"fr_FR",1036,"French_France"},
	{"fr_GN",12,"French_France"},
	{"fr_GP",12,"French_France"},
	{"fr_LU",5132,"French_Luxembourg"},
	{"fr_MC",6156,"French_Principality of Monaco"},
	{"fr_MF",12,"French_France"},
	{"fr_MG",12,"French_France"},
	{"fr_MQ",12,"French_France"},
	{"fr_NE",12,"French_France"},
	{"ga_IE",2108,"Irish_Ireland"},
	{"gl",86,"Galician_Spain"},
	{"gl_ES",1110,"Galician_Spain"},
	{"gu",71,"Gujarati_India"},
	{"gu_IN",1095,"Gujarati_India"},
	{"he",13,"Hebrew_Israel"},
	{"he_IL",1037,"Hebrew_Israel"},
	{"hi",57,"Hindi_India"},
	{"hi_IN",1081,"Hindi_India"},
	{"hr",26,"Croatian_Croatia"},
	{"hr_HR",1050,"Croatian_Croatia"},
	{"hu",14,"Hungarian_Hungary"},
	{"hu_HU",1038,"Hungarian_Hungary"},
	{"hy",43,"Armenian_Armenia"},
	{"hy_AM",1067,"Armenian_Armenia"},
	{"id",33,"Indonesian_Indonesia"},
	{"id_ID",1057,"Indonesian_Indonesia"},
	{"is",15,"Icelandic_Iceland"},
	{"is_IS",1039,"Icelandic_Iceland"},
	{"it",16,"Italian_Italy"},
	{"it_CH",2064,"Italian_Switzerland"},
	{"it_IT",1040,"Italian_Italy"},
	{"ja",17,"Japanese_Japan"},
	{"ja_JP",1041,"Japanese_Japan"},
	{"ka",55,"Georgian_Georgia"},
	{"ka_GE",1079,"Georgian_Georgia"},
	{"kk",63,"Kazakh_Kazakhstan"},
	{"kk_Cyrl",63,"Kazakh_Kazakhstan"},
	{"kk_Cyrl_KZ",63,"Kazakh_Kazakhstan"},
	{"kn",75,"Kannada_India"},
	{"kn_IN",1099,"Kannada_India"},
	{"ko",18,"Korean_Korea"},
	{"ko_KR",1042,"Korean_Korea"},
	{"kok",87,"Konkani_India"},
	{"kok_IN",1111,"Konkani_India"},
	{"lt",39,"Lithuanian_Lithuania"},
	{"lt_LT",1063,"Lithuanian_Lithuania"},
	{"lv",38,"Latvian_Latvia"},
	{"lv_LV",1062,"Latvian_Latvia"},
	{"mk",47,"FYRO Macedonian_Former Yugoslav Republic of Macedonia"},
	{"mk_MK",1071,"FYRO Macedonian_Former Yugoslav Republic of Macedonia"},
	{"mr",78,"Marathi_India"},
	{"mr_IN",1102,"Marathi_India"},
	{"ms",62,"Malay_Malaysia"},
	{"ms_BN",2110,"Malay_Brunei Darussalam"},
	{"ms_MY",1086,"Malay_Malaysia"},
	{"mt",58,"Maltese_Malta"},
	{"mt_MT",1082,"Maltese_Malta"},
	{"nb_NO",1044,"Norwegian_Norway"},
	{"ne",97,"Nepali_Nepal"},
	{"ne_NP",1121,"Nepali_Nepal"},
	{"nl",19,"Dutch_Netherlands"},
	{"nl_BE",2067,"Dutch_Belgium"},
	{"nl_NL",1043,"Dutch_Netherlands"},
	{"nn_NO",2068,"Norwegian (Nynorsk)_Norway"},
	{"pa",70,"Punjabi_India"},
	{"pa_Arab",70,"Punjabi_India"},
	{"pa_Arab_PK",70,"Punjabi_India"},
	{"pa_Guru",70,"Punjabi_India"},
	{"pa_Guru_IN",70,"Punjabi_India"},
	{"pl",21,"Polish_Poland"},
	{"pl_PL",1045,"Polish_Poland"},
	{"ps",99,"Pashto_Afghanistan"},
	{"ps_AF",1123,"Pashto_Afghanistan"},
	{"pt",22,"Portuguese_Brazil"},
	{"pt_BR",1046,"Portuguese_Brazil"},
	{"pt_GW",22,"Portuguese_Brazil"},
	{"pt_MZ",22,"Portuguese_Brazil"},
	{"pt_PT",2070,"Portuguese_Portugal"},
	{"rm",23,"Romansh_Switzerland"},
	{"rm_CH",1047,"Romansh_Switzerland"},
	{"ro",24,"Romanian_Romania"},
	{"ro_MD",24,"Romanian_Romania"},
	{"ro_RO",1048,"Romanian_Romania"},
	{"ru",25,"Russian_Russia"},
	{"ru_MD",25,"Russian_Russia"},
	{"ru_RU",1049,"Russian_Russia"},
	{"ru_UA",25,"Russian_Russia"},
	{"sk",27,"Slovak_Slovakia"},
	{"sk_SK",1051,"Slovak_Slovakia"},
	{"sl",36,"Slovenian_Slovenia"},
	{"sl_SI",1060,"Slovenian_Slovenia"},
	{"sq",28,"Albanian_Albania"},
	{"sq_AL",1052,"Albanian_Albania"},
	{"sr_Cyrl_BA",7194,"Serbian (Cyrillic)_Bosnia and Herzegovina"},
	{"sr_Latn_BA",6170,"Serbian (Latin)_Bosnia and Herzegovina"},
	{"sv",29,"Swedish_Sweden"},
	{"sv_FI",2077,"Swedish_Finland"},
	{"sv_SE",1053,"Swedish_Sweden"},
	{"sw",65,"Swahili_Kenya"},
	{"sw_KE",1089,"Swahili_Kenya"},
	{"sw_TZ",65,"Swahili_Kenya"},
	{"ta",73,"Tamil_India"},
	{"ta_IN",1097,"Tamil_India"},
	{"ta_LK",73,"Tamil_India"},
	{"te",74,"Telugu_India"},
	{"te_IN",1098,"Telugu_India"},
	{"th",30,"Thai_Thailand"},
	{"th_TH",1054,"Thai_Thailand"},
	{"tr",31,"Turkish_Turkey"},
	{"tr_TR",1055,"Turkish_Turkey"},
	{"uk",34,"Ukrainian_Ukraine"},
	{"uk_UA",1058,"Ukrainian_Ukraine"},
	{"ur",32,"Urdu_Islamic Republic of Pakistan"},
	{"ur_PK",1056,"Urdu_Islamic Republic of Pakistan"},
	{"uz",67,"Uzbek (Latin)_Uzbekistan"},
	{"uz_Arab",67,"Uzbek (Latin)_Uzbekistan"},
	{"uz_Arab_AF",67,"Uzbek (Latin)_Uzbekistan"},
	{"uz_Cyrl_UZ",2115,"Uzbek (Cyrillic)_Uzbekistan"},
	{"uz_Latn_UZ",1091,"Uzbek (Latin)_Uzbekistan"},
	{"vi",42,"Vietnamese_Viet Nam"},
	{"vi_VN",1066,"Vietnamese_Viet Nam"},
	{"zh_Hans",4,"Chinese_Taiwan"},
	{"zh_Hans_CN",2052,"Chinese_People's Republic of China"},
	{"zh_Hans_HK",4,"Chinese_Taiwan"},
	{"zh_Hans_MO",4,"Chinese_Taiwan"},
	{"zh_Hans_SG",4100,"Chinese_Singapore"},
	{"zh_Hant_TW",1028,"Chinese_Taiwan"},
	{"zu",53,"Zulu_South Africa"},
	{"zu_ZA",1077,"Zulu_South Africa"},
};

ECLocale createLocaleFromName(const char *lpszLocale)
{
	return Locale::createFromName(lpszLocale);
}

ECRESULT LocaleIdToLCID(const char *lpszLocaleID, ULONG *lpulLcid)
{
	const struct localemap *lpMapEntry = NULL;
	assert(lpszLocaleID != NULL);
	assert(lpulLcid != NULL);

	for (size_t i = 0; lpMapEntry == nullptr && i < ARRAY_SIZE(localeMap); ++i)
		if (strcasecmp(localeMap[i].lpszLocaleID, lpszLocaleID) == 0)
			lpMapEntry = &localeMap[i];
	if (lpMapEntry == NULL)
		return KCERR_NOT_FOUND;
	*lpulLcid = lpMapEntry->ulLCID;
	return erSuccess;
}

ECRESULT LCIDToLocaleId(ULONG ulLcid, const char **lppszLocaleID)
{
	const struct localemap *lpMapEntry = NULL;
	assert(lppszLocaleID != NULL);

	for (size_t i = 0; lpMapEntry == nullptr && i < ARRAY_SIZE(localeMap); ++i)
		if (localeMap[i].ulLCID == ulLcid)
			lpMapEntry = &localeMap[i];
	if (lpMapEntry == NULL)
		return KCERR_NOT_FOUND;
	*lppszLocaleID = lpMapEntry->lpszLocaleID;
	return erSuccess;
}

/**
 * Create a locale independent blob that can be used to sort
 * strings fast. This is used when a string would be compared
 * multiple times.
 *
 * @param[in]	s			The string to compare.
 * @param[in]	nCap		Base the key on the first nCap characters of s (if larger than 0).
 * @param[in]	locale		The locale used to create the sort key.
 *
 * @returns		ECUSortKey object containing the blob
 */
static CollationKey createSortKey(UnicodeString &&s, int nCap,
    const ECLocale &locale)
{
	if (nCap > 1)
		s.truncate(nCap);
	// Quick workaround for sorting items starting with ' (like From and To) and ( and '(
	if (s.startsWith("'") || s.startsWith("("))
		s.remove(0, 1);

	CollationKey key;
	UErrorCode status = U_ZERO_ERROR;
	unique_ptr_Collator ptrCollator(Collator::createInstance(locale, status));
	ptrCollator->getCollationKey(std::move(s), key, status); // Create a collation key for sorting
	return key;
}

/**
 * Create a locale independent blob that can be used to sort
 * strings fast. This is used when a string would be compared
 * multiple times.
 *
 * @param[in]	s			The string to compare.
 * @param[in]	nCap		Base the key on the first nCap characters of s (if larger than 0).
 * @param[in]	locale		The locale used to create the sort key.
 */
static std::string createSortKeyData(UnicodeString &&s, int nCap,
    const ECLocale &locale)
{
	auto key = createSortKey(std::move(s), nCap, locale);
	int32_t 		cbKeyData = 0;
	const uint8_t	*lpKeyData = key.getByteArray(cbKeyData);
	if (lpKeyData == nullptr)
		return {};
	return std::string(reinterpret_cast<const char *>(lpKeyData), cbKeyData);
}

/**
 * Create a locale independent blob that can be used to sort
 * strings fast. This is used when a string would be compared
 * multiple times.
 *
 * @param[in]	s			The string to compare.
 * @param[in]	nCap		Base the key on the first nCap characters of s (if larger than 0).
 * @param[in]	locale		The locale used to create the sort key.
 */
std::string createSortKeyData(const char *s, int nCap, const ECLocale &locale)
{
	assert(s != NULL);
	return createSortKeyData(UnicodeString(s), nCap, locale);
}

/**
 * Create a locale independent blob that can be used to sort
 * strings fast. This is used when a string would be compared
 * multiple times.
 *
 * @param[in]	s			The string to compare.
 * @param[in]	locale		The locale used to create the sort key.
 */
std::string createSortKeyData(const wchar_t *s, int nCap, const ECLocale &locale)
{
	assert(s != NULL);
	return createSortKeyData(WCHARToUnicode(s), nCap, locale);
}

/**
 * Create a locale independent blob that can be used to sort
 * strings fast. This is used when a string would be compared
 * multiple times.
 *
 * @param[in]	s			The string to compare.
 * @param[in]	nCap		Base the key on the first nCap characters of s (if larger than 0).
 * @param[in]	locale		The locale used to create the sort key.
 */
std::string createSortKeyDataFromUTF8(const char *s, int nCap,
    const ECLocale &locale)
{
	assert(s != NULL);
	return createSortKeyData(UTF8ToUnicode(s), nCap, locale);
}

/**
 * Compare two sort keys previously created with createSortKey.
 *
 * @param[in]	cbKey1		The size i nbytes of key 1.
 * @param[in]	lpKey1		Key 1.
 * @param[in]	cbKey2		The size i nbytes of key 2.
 * @param[in]	lpKey2		Key 2.
 *
 * @retval	<0	Key1 is smaller than key2
 * @retval	0	Key1 equals key2
 * @retval	>0	Key1 is greater than key2
 */
int compareSortKeys(const std::string &a, const std::string &b)
{
	CollationKey ckA(reinterpret_cast<const uint8_t *>(a.c_str()), a.size());
	CollationKey ckB(reinterpret_cast<const uint8_t *>(b.c_str()), b.size());
	UErrorCode status = U_ZERO_ERROR;
	switch (ckA.compareTo(ckB, status)) {
	case UCOL_LESS: return -1;
	case UCOL_EQUAL: return 0;
	case UCOL_GREATER: return 1;
	}
	return 1;
}

} /* namespace */

/** @} */
