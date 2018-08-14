/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/*
 *	Definitions used throughout the code
 *
 *	platform.h seems to be never included from header files, which is
 *	reasonable for definitions that would otherwise influence third-party
 *	programs. However, we also need some definitions that are truly
 *	visible everywhere, and that is zcdefs.h.
 */
#ifndef ZCOMMON_DEFS_H
#define ZCOMMON_DEFS_H 1

#ifdef SWIG
	/* why does this not surprise me */
#	define _kc_hidden
#	define _kc_export
#else
#	define _kc_hidden __attribute__((visibility("hidden")))
#	define _kc_export __attribute__((visibility("default")))
#endif

/* Exported because something was using dynamic_cast<C> */
#define _kc_export_dycast _kc_export
/* Exported because something was using throw C; */
#define _kc_export_throw _kc_export

/* Minimum requirement for KC is g++ 4.7, g++0x mode. */
/* Swig is not bright enough to grok all C++11. */
#if defined(SWIG)
#	define _kc_final
#	define _kc_override
#else
	/* From g++ 4.7 onwards */
#	define _kc_final final
#	define _kc_override override
#endif

/* Mark classes which explicitly must not be final in the C++ side… for SWIG */
#define _no_final

namespace KC {}

#endif /* ZCOMMON_DEFS_H */
