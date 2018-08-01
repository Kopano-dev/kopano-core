/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECGetText_INCLUDED
#define ECGetText_INCLUDED

#include <kopano/zcdefs.h>
#include <libintl.h>
#define KC_A(string) dcgettext("kopano", string, LC_MESSAGES)
#define KC_W(string) kopano_dcgettext_wide("kopano", string)

namespace KC {
	extern _kc_export LPWSTR kopano_dcgettext_wide(const char *domainname, const char *msgid);
}

// This must go. Obviously someone was trying to be clever, but a macro named _
// can cause all sorts of mischief that can be hard to trace. Unfortunately
// it's in use in 51 different files all over the project, so changing it is
// a bit of a bother. NS 16 October 2013
#define _(string) KC_W(string)

#endif // ndef ECGetText_INCLUDED
