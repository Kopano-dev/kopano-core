/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <string>
#include <kopano/platform.h>
#include <kopano/charset/convstring.h>

namespace KC {

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

/** Create a new convstring object based on a raw pointer.
 *
 * Creates an object and assumes that the provided string is encoded in
 * the current locale or as a wide character string depending on the
 * presence of the MAPI_UNICODE flag in ulFlags.
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
	return m_lpsz == nullptr ? utf8string(nullptr) : convert_to<utf8string>();
}

/** Convert this convstring object to a std::string.
 *
 * @return	A std::string representing the internal string encoded in the current locale.
 */
convstring::operator std::string() const
{
	return convert_to<std::string>();
}

} /* namespace */
