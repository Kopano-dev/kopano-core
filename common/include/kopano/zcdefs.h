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

#if __cplusplus >= 201100L
#	define _zcp_final final
#	define _zcp_override override
#else
#	define _zcp_final
#	define _zcp_override
#endif

#endif /* ZCOMMON_DEFS_H */
