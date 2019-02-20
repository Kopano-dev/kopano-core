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

ECRESULT FixPropEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct propVal *lpProp, bool bNoTagUpdate)
{
	if (PROP_TYPE(lpProp->ulPropTag) == PT_STRING8 || PROP_TYPE(lpProp->ulPropTag) == PT_UNICODE) {
		if (type == In)
			lpProp->Value.lpszA = stringCompat.to_UTF8(soap, lpProp->Value.lpszA);
		else
			lpProp->Value.lpszA = stringCompat.from_UTF8(soap, lpProp->Value.lpszA);
		if (!bNoTagUpdate)
			lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, PT_UNICODE);
	} else if (PROP_TYPE(lpProp->ulPropTag) == PT_MV_STRING8 || PROP_TYPE(lpProp->ulPropTag) == PT_MV_UNICODE) {
		if (type == In) {
			for (gsoap_size_t i = 0; i < lpProp->Value.mvszA.__size; ++i)
				lpProp->Value.mvszA.__ptr[i] = stringCompat.to_UTF8(soap, lpProp->Value.mvszA.__ptr[i]);
		} else {
			for (gsoap_size_t i = 0; i < lpProp->Value.mvszA.__size; ++i)
				lpProp->Value.mvszA.__ptr[i] = stringCompat.from_UTF8(soap, lpProp->Value.mvszA.__ptr[i]);
		}
		if (!bNoTagUpdate)
			lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, PT_MV_UNICODE);
	}

	return erSuccess;
}

} /* namespace */
