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

/*
 *	Definitions used throughout the ZCP code
 *
 *	platform.h seems to be never included from header files, which is
 *	reasonable for definitions that would otherwise influence third-party
 *	programs. However, we also need some definitions that are truly
 *	visible everywhere, and that is zcdefs.h.
 */
#ifndef ZCOMMON_DEFS_H
#define ZCOMMON_DEFS_H 1

/* Minimum requirement for KC is g++ 4.6, g++0x mode. */
/* Swig is too stupid to grok C++11 at all. */
#if defined(SWIG) || defined(__GNUG__) && __GNUG__ == 4 && __GNUG_MINOR__ < 7
#	define _kc_final
#	define _kc_override
#	define _zcp_final
#	define _zcp_override
#else
	/* From g++ 4.7 onwards */
#	define _kc_final final
#	define _kc_override override
#	define _zcp_final _kc_final
#	define _zcp_override _kc_override
#endif

/* Mark classes which explicitly must not be final in the C++ side… for SWIG */
#define _no_final

/*
 * This is a marker for structs where we expect gsoap 2.8.30 or ourselves to
 * actually zero it.
 */
#define __gszeroinit

#endif /* ZCOMMON_DEFS_H */
