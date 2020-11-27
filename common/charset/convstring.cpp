/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <string>
#include <kopano/platform.h>
#include <kopano/charset/convstring.h>

namespace KC {

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

/** Convert this convstring object to an utf8string.
 *
 * @return	An utf8string representing the internal string encoded in UTF-8.
 */
utf8string convstring::to_utf8() const
{
	return m_lpsz == nullptr ? utf8string(nullptr) : convert_to<utf8string>();
}

/** Convert this convstring object to a std::string.
 *
 * @return	A std::string representing the internal string encoded in the current locale.
 */
std::string convstring::to_str() const
{
	return convert_to<std::string>();
}

} /* namespace */
