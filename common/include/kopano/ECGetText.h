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

#ifndef ECGetText_INCLUDED
#define ECGetText_INCLUDED

#ifndef NO_GETTEXT
	#include <libintl.h>
	#define _A(string) dcgettext("kopano", string, LC_MESSAGES)
	#define _W(string) kopano_dcgettext_wide("kopano", string)

	LPWSTR kopano_dcgettext_wide(const char *domainname, const char *msgid);
#else
	#define _A(string) string
	#define _W(string) _T(string)
#endif

// This must go. Obviously someone was trying to be clever, but a macro named _
// can cause all sorts of mischief that can be hard to trace. Unfortunately
// it's in use in 51 different files all over the project, so changing it is
// a bit of a bother. NS 16 October 2013
#ifdef UNICODE
#define _(string) _W(string)
#else
#define _(string) _A(string)
#endif

#endif // ndef ECGetText_INCLUDED
