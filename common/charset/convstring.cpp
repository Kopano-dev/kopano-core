/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#include <kopano/platform.h>
#include <kopano/charset/convstring.h>

namespace KC {

/** Create a convstring instance from a SPropValue.
 *
 * Extarcts the lpszA or lpszW depending on the PROP_TYPE of the provided
 * prop value and creates a convstring object representing the data.
 *
 * @param[in]	lpsPropVal
 *			Pointer to the SPropValue object to extract the data from.
 * @return	A new convstring object.
 */
convstring convstring::from_SPropValue(const SPropValue *lpsPropVal)
{
	if (!lpsPropVal)
		return convstring();
	
	switch (PROP_TYPE(lpsPropVal->ulPropTag)) {
	case PT_STRING8:
		return convstring(lpsPropVal->Value.lpszA);
	case PT_UNICODE:
		return convstring(lpsPropVal->Value.lpszW);
	default:
		return convstring();
	}
}

/** Create a convstring instance from a SPropValue.
 *
 * Extarcts the lpszA or lpszW depending on the PROP_TYPE of the provided
 * prop value and creates a convstring object representing the data.
 *
 * @param[in]	sPropVal
 *			Reference to the SPropValue object to extract the data from.
 * @return	A new convstring object.
 */
convstring convstring::from_SPropValue(const SPropValue &sPropVal)
{
	return from_SPropValue(&sPropVal);
}

/**
 * Create a new convstring object based on another convstring object.
 *
 * @param[in]	other
 *			The convstring object to base the new object on.
 */
convstring::convstring(const convstring &other) :
	m_ulFlags(other.m_ulFlags), m_str(other.m_str)
{
	// We create a new convert_context as the context of other contains
	// nothing we really need. If we would copy its map with iconv_context's, we would
	// still need to create all the internal iconv objects, which is usseless since we
	// might not even use them all.
	// Also the lists with stored strings are useless as they're only used as storage.
	if (other.m_lpsz)
		m_lpsz = m_str.c_str();
}

/** Create a new convstring object based on a raw char pointer.
 *
 * Creates an object and assumes that the provided string is encoded
 * in the current locale.
 *
 * @param[in]	lpsz
 *			The string to base the new object on. This string
 *			is expected to be encoded in the current locale.
 */
convstring::convstring(const char *lpsz)
: m_lpsz(reinterpret_cast<const TCHAR*>(lpsz))
{
}

/** Create a new convstring object based on a raw char pointer.
 *
 * Creates an object and assumes that the provided string is encoded
 * as a wide character string.
 *
 * @param[in]	lpsz
 *			The string to base the new object on. This string
 *			is expected to be encoded as a wide character string.
 */
convstring::convstring(const wchar_t *lpsz)
: m_lpsz(reinterpret_cast<const TCHAR*>(lpsz))
, m_ulFlags(MAPI_UNICODE)
{
}

/** Create a new convstring object based on a raw pointer.
 *
 * Creates an object and assumes that the provided string is encoded in
 * the current locale or as a wide character string depending on the 
 * precense of the MAPI_UNICODE flag in ulFlags.
 *
 * @param[in]	lpsz
 *			The string to base the new object on.
 * @param[in]	ulFlags
 *			Flags changing the behaviour of the convstring.
 *			If the MAPI_UNICODE flag is specified, the provided
 *			string is assumed to be encoded as a wide character
 *			string. Otherwise it is assumed to be encoded in the
 *			current locale.
 */
convstring::convstring(const TCHAR *lpsz, ULONG ulFlags)
: m_lpsz(lpsz)
, m_ulFlags(ulFlags)
{
}

/** Perform a conversion from the internal string to the requested encoding.
 *
 * @tparam	T
 *			The type to convert to string to. The actual encoding
 *			is determined implicitly by this type.
 * @return	An object of type T.
 */
template <typename T>
T convstring::convert_to() const {
	if (m_lpsz == NULL)
		return T();
	if (m_ulFlags & MAPI_UNICODE)
		return m_converter.convert_to<T>(reinterpret_cast<const wchar_t*>(m_lpsz));
	else
		return m_converter.convert_to<T>(reinterpret_cast<const char*>(m_lpsz));
}

/** Perform a conversion from the internal string to the requested encoding.
 *
 * With this version the to charset is explicitly specified.
 * @tparam	T
 *			The type to convert to string to. The actual encoding
 *			is determined implicitly by this type.
 * @param[in]	tocode
 *					The charset in which to encode the converted string.
 * @return	An object of type T.
 */
template<typename T>
T convstring::convert_to(const char *tocode) const {
	if (m_lpsz == NULL)
		return T();
	if (m_ulFlags & MAPI_UNICODE) {
		auto lpszw = reinterpret_cast<const wchar_t *>(m_lpsz);
		return m_converter.convert_to<T>(tocode, lpszw, rawsize(lpszw), CHARSET_WCHAR);
	} else {
		auto lpsza = reinterpret_cast<const char *>(m_lpsz);
		return m_converter.convert_to<T>(tocode, lpsza, rawsize(lpsza), CHARSET_CHAR);
	}
}

/** Check if the convstring is initialized with a NULL pointer or with an empty string.
 *
 * @return	A boolean specifying of the internal string is a NULL pointer or an empty string.
 * @retval	true	The internal string is a NULL pointer or an empty string.
 * @retval	false	The internal string is valid and non-empty.
 */
bool convstring::null_or_empty() const
{
	if (m_lpsz == NULL)
		return true;
	if (m_ulFlags & MAPI_UNICODE)
		return *reinterpret_cast<const wchar_t*>(m_lpsz) == L'\0';
	else
		return *reinterpret_cast<const char*>(m_lpsz) == '\0';
}

/** Convert this convstring object to an utf8string.
 *
 * @return	An utf8string representing the internal string encoded in UTF-8.
 */
convstring::operator utf8string() const
{
	return (m_lpsz == NULL ? utf8string::null_string() : convert_to<utf8string>());
}

/** Convert this convstring object to a std::string.
 *
 * @return	A std::string representing the internal string encoded in the current locale.
 */
convstring::operator std::string() const 
{
	return convert_to<std::string>();
}

/** Convert this convstring object to a std::wstring.
 *
 * @return	A std::wstring representing the internal string encoded as a wide character string.
 */
convstring::operator std::wstring() const
{
	return convert_to<std::wstring>();
}

/**
 * Convert this convstring object to a raw char pointer.
 *
 * @return	A character pointer that represents the internal string encoded in the current locale.
 *
 * @note	Don't call this too often as the results are stored internally since storage needs to be
 *		guaranteed for the caller to be able to use the data.
 */
const char* convstring::c_str() const
{
	return (m_lpsz ? convert_to<char*>() : NULL);
}

/**
 * Convert this convstring object to a raw char pointer encoded in UTF-8.
 *
 * @return	A character pointer that represents the internal string encoded in the current locale.
 *
 * @note	Don't call this too often as the results are stored internally since storage needs to be
 *		guaranteed for the caller to be able to use the data.
 */
const char* convstring::u8_str() const
{
	return (m_lpsz ? convert_to<char*>("UTF-8") : NULL);
}

} /* namespace */
