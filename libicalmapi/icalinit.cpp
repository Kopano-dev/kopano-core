/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
 * @par libicalmapi initialization
 * This is a static class to initialize the libicalmapi library, which
 * in turn initializes the libical. There is no point in this library
 * where this could go. We want to make sure we free all used memory
 * used by libical.
 */
#include <libical/ical.h>

namespace KC {

class libicalmapi_init {
public:
	libicalmapi_init() {
		icaltimezone_get_builtin_timezones();
	}

	~libicalmapi_init() {
		icaltimezone_free_builtin_timezones();
	}
} libicalmapi_init;

} /* namespace */
