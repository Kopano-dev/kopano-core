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

#ifndef ICALMAPI_LIBICALMAPI_H
#define ICALMAPI_LIBICALMAPI_H

#ifdef _WIN32
	#ifdef LIBICALMAPI_EXPORTS
		#define ICALMAPI_API __declspec(dllexport)
	#else
		#define ICALMAPI_API __declspec(dllimport)
	#endif
#else
	#define ICALMAPI_API
#endif

#endif //ICALMAPI_H
