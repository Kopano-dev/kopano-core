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

#ifndef ECLIBSYNC_INCLUDED
#define ECLIBSYNC_INCLUDED

#if defined(WIN32) && !defined(SWIG)
	#ifdef LIBSYNC_EXPORTS
		#define ECLIBSYNC_API __declspec(dllexport)
	#else
		#define ECLIBSYNC_API __declspec(dllimport)
	#endif
#endif

#ifndef ECLIBSYNC_API
	#define ECLIBSYNC_API
#endif

#endif // ndef ECLIBSYNC_INCLUDED
