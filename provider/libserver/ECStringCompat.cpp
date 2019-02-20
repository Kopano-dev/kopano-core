/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <string>
#include <kopano/platform.h>
#include "ECStringCompat.h"
#include "soapH.h"
#include <kopano/charset/convert.h>
#include <kopano/charset/utf8string.h>
#include "utf8/unchecked.h"

namespace KC {

char *ECStringCompat::WTF1252_to_UTF8(soap *lpsoap, const char *szWTF1252, convert_context *lpConverter)
{
	if (!szWTF1252)
		return NULL;

	std::string str1252;
	str1252.reserve(strlen(szWTF1252));

	while (*szWTF1252) {
		utf8::uint32_t cp = utf8::unchecked::next(szWTF1252);

		// Since the string was originally windows-1252, all code points
		// should be in the range 0 <= cp < 256.
		str1252.append(1, cp < 256 ? cp : '?');
	}

	// Now convert the windows-1252 string to proper UTF8.
	utf8string strUTF8;
	if (lpConverter)
		strUTF8 = lpConverter->convert_to<utf8string>(str1252, rawsize(str1252), "WINDOWS-1252");
	else
		strUTF8 = convert_to<utf8string>(str1252, rawsize(str1252), "WINDOWS-1252");

	return s_strcpy(lpsoap, strUTF8.c_str());
}

char *ECStringCompat::UTF8_to_WTF1252(soap *lpsoap, const char *szUTF8, convert_context *lpConverter)
{
	if (!szUTF8)
		return NULL;

	std::string str1252, strWTF1252;
	if (lpConverter)
		str1252 = lpConverter->convert_to<std::string>("WINDOWS-1252//TRANSLIT", szUTF8, rawsize(szUTF8), "UTF-8");
	else
		str1252 = convert_to<std::string>("WINDOWS-1252//TRANSLIT", szUTF8, rawsize(szUTF8), "UTF-8");
	auto iWTF1252 = back_inserter(strWTF1252);
	strWTF1252.reserve(std::string::size_type(str1252.size() * 1.3));	// It will probably grow a bit, 1.3 is just a guess.
	for (const auto c : str1252)
		utf8::unchecked::append(static_cast<unsigned char>(c), iWTF1252);

	return s_strcpy(lpsoap, strWTF1252.c_str());
}

ECStringCompat::~ECStringCompat()
{
	// deleting a NULL ptr is allowed.
	delete m_lpConverter;
}

ECRESULT FixPropEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct propVal *lpProp, bool bNoTagUpdate)
{
	if (PROP_TYPE(lpProp->ulPropTag) == PT_STRING8 || PROP_TYPE(lpProp->ulPropTag) == PT_UNICODE) {
		if (type == In) {
			lpProp->Value.lpszA = stringCompat.to_UTF8(soap, lpProp->Value.lpszA);
			if (!bNoTagUpdate)
				lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, PT_UNICODE);
		} else {
			lpProp->Value.lpszA = stringCompat.from_UTF8(soap, lpProp->Value.lpszA);
			if (!bNoTagUpdate)
				lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, stringCompat.string_prop_type());
		}
	} else if (PROP_TYPE(lpProp->ulPropTag) == PT_MV_STRING8 || PROP_TYPE(lpProp->ulPropTag) == PT_MV_UNICODE) {
		if (type == In) {
			for (gsoap_size_t i = 0; i < lpProp->Value.mvszA.__size; ++i)
				lpProp->Value.mvszA.__ptr[i] = stringCompat.to_UTF8(soap, lpProp->Value.mvszA.__ptr[i]);
			if (!bNoTagUpdate)
				lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, PT_MV_UNICODE);
		} else {
			for (gsoap_size_t i = 0; i < lpProp->Value.mvszA.__size; ++i)
				lpProp->Value.mvszA.__ptr[i] = stringCompat.from_UTF8(soap, lpProp->Value.mvszA.__ptr[i]);
			if (!bNoTagUpdate)
				lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, stringCompat.string_prop_type() | MV_FLAG);
		}
	}

	return erSuccess;
}

} /* namespace */
