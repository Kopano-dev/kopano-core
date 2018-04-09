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

#ifndef utfutil_INCLUDED
#define utfutil_INCLUDED

#include <unicode/unistr.h>

using namespace U_ICU_NAMESPACE;
class UTF8Iterator;
class UTF32Iterator;

#if U_ICU_VERSION_MAJOR_NUM < 4 || (U_ICU_VERSION_MAJOR_NUM == 4 && U_ICU_VERSION_MINOR_NUM < 2)

UnicodeString UTF8ToUnicode(const char *utf8);
UnicodeString UTF32ToUnicode(const UChar32 *utf32);

#else

static inline UnicodeString UTF8ToUnicode(const char *utf8) {
	return UnicodeString::fromUTF8(utf8);
}

static inline UnicodeString UTF32ToUnicode(const UChar32 *utf32) {
	return UnicodeString::fromUTF32(utf32, -1);
}

#endif

UnicodeString WCHARToUnicode(const wchar_t *str);
UnicodeString StringToUnicode(const char *str);

#endif // ndef utfutil_INCLUDED
