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

#ifndef KC_VERSIONS_H
#define KC_VERSIONS_H 1

#include <kopano/ecversion.h>

#define MAKE_KOPANO_VERSION(major, minor, update) \
	((((major) & 0xff) << 24) | (((minor) & 0xff) << 16) | ((update) & 0xffff))

#define MAKE_KOPANO_MAJOR(major, minor) \
	MAKE_KOPANO_VERSION((major), (minor), 0)

#define MAKE_KOPANO_GENERAL(major) \
	MAKE_KOPANO_MAJOR((major), 0)

#define KOPANO_MAOJR_MASK	0xffff0000
#define KOPANO_GENERAL_MASK	0xff000000

#define KOPANO_GET_MAJOR(version)	\
	((version) & KOPANO_MAOJR_MASK)

#define KOPANO_GET_GENERAL(version)	\
	((version) & KOPANO_GENERAL_MASK)


// Current thing
#define KOPANO_CUR_MAJOR		MAKE_KOPANO_MAJOR(PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR)
#define KOPANO_CUR_GENERAL		MAKE_KOPANO_GENERAL(PROJECT_VERSION_MAJOR)

// Important version(s) we check for
#define ZARAFA_VERSION_6_40_0	MAKE_KOPANO_VERSION(6, 40, 0)
#define KOPANO_VERSION_UNKNOWN	MAKE_KOPANO_VERSION(0xff, 0xff, 0xffff)


#define KOPANO_COMPARE_VERSION_TO_MAJOR(version, major)	\
	((version) < (major) ? -1 : (KOPANO_GET_MAJOR(version) > (major) ? 1 : 0))

#define KOPANO_COMPARE_VERSION_TO_GENERAL(version, general) \
	((version) < (general) ? -1 : (KOPANO_GET_GENERAL(version) > (general) ? 1 : 0))


#endif /* KC_VERSIONS_H */
