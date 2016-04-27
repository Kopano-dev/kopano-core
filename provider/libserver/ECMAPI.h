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

#ifndef ECMAPI_H
#define ECMAPI_H

// For PR_MESSAGE_FLAGS
#define MSGFLAG_DELETED				((ULONG) 0x00000400)
#define MSGFLAG_NOTIFY_FLAGS		(MSGFLAG_DELETED | MSGFLAG_ASSOCIATED)
#define MSGFLAG_SETTABLE_BY_USER	(MSGFLAG_UNMODIFIED| MSGFLAG_READ | MSGFLAG_UNSENT | MSGFLAG_FROMME | MSGFLAG_RESEND)
#define MSGFLAG_SETTABLE_BY_SPOOLER	(MSGFLAG_RN_PENDING| MSGFLAG_NRN_PENDING)
#define MSGFLAG_UNSETTABLE			(MSGFLAG_DELETED | MSGFLAG_ASSOCIATED)


#endif // ECMAPI_H
