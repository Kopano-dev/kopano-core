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

/**
 * @par libicalmapi initialization
 * This is a static class to initialize the libicalmapi library, which
 * in turn initializes the libical. There is no point in this library
 * where this could go. We want to make sure we free all used memory
 * used by libical.
 */
#include <libical/ical.h>

namespace KC {

class __libicalmapi_init {
public:
	__libicalmapi_init() {
		icaltimezone_get_builtin_timezones();
	}

	~__libicalmapi_init() {
		icaltimezone_free_builtin_timezones();
	}
}  __libicalmapi_init;

} /* namespace */
