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

#include <kopano/buildconfig.h>

#ifdef SWIG
	/* why does this not surprise me */
#	define KC_HIDDEN
#	define KC_EXPORT
#else
#	if !defined(__cplusplus) || __cplusplus < 201400L
#		error KGWC needs at least C++14
#	endif
#	define KC_HIDDEN __attribute__((visibility("hidden")))
#	define KC_EXPORT __attribute__((visibility("default")))
#endif

/* Exported because something was using dynamic_cast<C> */
#define KC_EXPORT_DYCAST KC_EXPORT
/* Exported because something was using throw C; */
#define KC_EXPORT_THROW KC_EXPORT

/* Swig is not bright enough to grok all C++11. */
#if defined(SWIG)
#	define KC_FINAL
#	define KC_OVERRIDE
#else
#	define KC_FINAL final
#	define KC_OVERRIDE override
#endif

/* Mark classes which explicitly must not be final in the C++ side… for SWIG */
#define _no_final

#ifdef KC_DISALLOW_OBJECTPTR_REFMOD
/* Add object_ptr build-time checks */
#define KC_FINAL_OPG
#else
#define KC_FINAL_OPG final
#endif

namespace KC {}

#endif /* ZCOMMON_DEFS_H */
