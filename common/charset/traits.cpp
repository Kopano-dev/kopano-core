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

#include <kopano/charset/traits.h>
#include <kopano/charset/utf16string.h>
#include "utf32string.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static const char THIS_FILE[] = __FILE__;
#endif

/** 
 * Make charset string ignore invalid characters, possebly converting
 * them with '?' character in iconv.
 * 
 * @param[in,out] strCharset modified charset will cause iconv to ignore/translit invalid characters
 */
void setCharsetBestAttempt(std::string &strCharset)
{
#ifdef WIN32
	strCharset += "//IGNORE";
#else
	strCharset += "//TRANSLIT";
#endif
}

template <typename _T>
static size_t ucslen(const _T* p) {
	size_t len = 0;
	while (*p++ != 0)
		++len;
	return len;
}

#ifdef LINUX
size_t iconv_charset<unsigned short*>::rawsize(const unsigned short *from) {
	return ucslen(from) * sizeof(unsigned short);
}

size_t iconv_charset<const unsigned short*>::rawsize(const unsigned short *from) {
	return ucslen(from) * sizeof(unsigned short);
}

#else
size_t iconv_charset<unsigned int*>::rawsize(const unsigned int *from) {
	return ucslen(from) * sizeof(unsigned int);
}

size_t iconv_charset<const unsigned int*>::rawsize(const unsigned int *from) {
	return ucslen(from) * sizeof(unsigned int);
}

#endif
