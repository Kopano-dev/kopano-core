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

ECRESULT FixPropEncoding(struct soap *soap, struct propVal *lpProp)
{
	if (PROP_TYPE(lpProp->ulPropTag) == PT_STRING8 || PROP_TYPE(lpProp->ulPropTag) == PT_UNICODE)
		lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, PT_UNICODE);
	else if (PROP_TYPE(lpProp->ulPropTag) == PT_MV_STRING8 || PROP_TYPE(lpProp->ulPropTag) == PT_MV_UNICODE)
		lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, PT_MV_UNICODE);
	return erSuccess;
}

} /* namespace */
