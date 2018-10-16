/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECGetText_INCLUDED
#define ECGetText_INCLUDED

#include <kopano/zcdefs.h>
#include <libintl.h>
/* Input is always char * [C locale]. Output is either char * [C locale] or wchar_t * [Unicode] */
#define KC_A(string) dcgettext("kopano", string, LC_MESSAGES)
/* Often, this will be assigned to SPropValue::lpszW, so a non-const type is preferable */
#define KC_W(string) const_cast<wchar_t *>(kopano_dcgettext_wide("kopano", string))

namespace KC {
extern _kc_export const wchar_t *kopano_dcgettext_wide(const char *domain, const char *msg);
}

#ifdef UNICODE
#	define KC_TX(s) KC_W(s)
#else
#	define KC_TX(s) KC_A(s)
#endif

#endif // ndef ECGetText_INCLUDED
