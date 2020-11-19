/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/charset/convert.h>
#include <string>
#include <kopano/charset/utf8string.h>
#include <mapidefs.h>

namespace KC {

/*
 * Even if the definition of TCHAR is different between ASCII and Unicode
 * builds, some of the oldest functions in the MAPI spec have the Unicodeness
 * of arguments conveyed by a flags argument, and the char type means nothing.
 */
inline utf8string tfstring_to_utf8(const TCHAR *s, unsigned int fl)
{
	if (s == nullptr)
		return utf8string(nullptr);
	return (fl & MAPI_UNICODE) ? convert_to<utf8string>(reinterpret_cast<const wchar_t *>(s)) :
	       convert_to<utf8string>(reinterpret_cast<const char *>(s));
}

inline std::string tfstring_to_lcl(const TCHAR *s, unsigned int fl)
{
	if (s == nullptr)
		return {};
	return (fl & MAPI_UNICODE) ? convert_to<std::string>(reinterpret_cast<const wchar_t *>(s)) :
	       convert_to<std::string>(reinterpret_cast<const char *>(s));
}

} /* namespace */
