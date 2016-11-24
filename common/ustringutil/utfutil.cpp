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

#include <kopano/platform.h>
#include "utfutil.h"

#include <unicode/ustring.h>
#include <kopano/charset/convert.h>

UnicodeString WCHARToUnicode(const wchar_t *sz)
{
	return UTF32ToUnicode((const UChar32 *)sz);
}

UnicodeString StringToUnicode(const char *sz)
{
	std::string strUTF16;

	convert_context converter;

	// *tocode, const _From_Type &_from, size_t cbBytes, const char *fromcode
	strUTF16 = converter.convert_to<std::string>("UTF-16LE", sz, rawsize(sz), "");

	return UnicodeString((UChar *)strUTF16.data(), strUTF16.length() / 2);
}
