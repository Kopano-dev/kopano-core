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

#ifndef KC_KCODES_HPP
#define KC_KCODES_HPP 1

#include <kopano/zcdefs.h>
#include <kopano/platform.h>

namespace KC {

#define MAKE_KSCODE(sev,code) ( (((unsigned int)(sev)<<31) | ((unsigned int)(code))) )
#define MAKE_KCERR( err ) (MAKE_KSCODE( 1, err ))
#define MAKE_KCWARN( warn ) (MAKE_KSCODE( 1, warn )) // This macro is broken, should be 0

#define KCERR_NONE					0
#define KCERR_UNKNOWN				MAKE_KCERR( 1 )
#define KCERR_NOT_FOUND				MAKE_KCERR( 2 )
#define KCERR_NO_ACCESS				MAKE_KCERR( 3 )
#define KCERR_NETWORK_ERROR			MAKE_KCERR( 4 )
#define KCERR_SERVER_NOT_RESPONDING	MAKE_KCERR( 5 )
#define KCERR_INVALID_TYPE			MAKE_KCERR( 6 )
#define KCERR_DATABASE_ERROR			MAKE_KCERR( 7 )
#define KCERR_COLLISION				MAKE_KCERR( 8 )
#define KCERR_LOGON_FAILED			MAKE_KCERR( 9 )
#define KCERR_HAS_MESSAGES			MAKE_KCERR( 10 )
#define KCERR_HAS_FOLDERS			MAKE_KCERR( 11 )
#define KCERR_HAS_RECIPIENTS			MAKE_KCERR( 12 )
#define KCERR_HAS_ATTACHMENTS		MAKE_KCERR( 13 )
#define KCERR_NOT_ENOUGH_MEMORY		MAKE_KCERR( 14 )
#define KCERR_TOO_COMPLEX			MAKE_KCERR( 15 )
#define KCERR_END_OF_SESSION			MAKE_KCERR( 16 )
#define KCWARN_CALL_KEEPALIVE			MAKE_KCWARN( 17 )
#define KCERR_UNABLE_TO_ABORT		MAKE_KCERR( 18 )
#define KCERR_NOT_IN_QUEUE			MAKE_KCERR( 19 )
#define KCERR_INVALID_PARAMETER		MAKE_KCERR( 20 )
#define KCWARN_PARTIAL_COMPLETION		MAKE_KCWARN( 21 )
#define KCERR_INVALID_ENTRYID		MAKE_KCERR( 22 )
#define KCERR_BAD_VALUE				MAKE_KCERR( 23 )
#define KCERR_NO_SUPPORT				MAKE_KCERR( 24 )
#define KCERR_TOO_BIG				MAKE_KCERR( 25 )
#define KCWARN_POSITION_CHANGED		MAKE_KCWARN( 26 )
#define KCERR_FOLDER_CYCLE			MAKE_KCERR( 27 )
#define KCERR_STORE_FULL				MAKE_KCERR( 28 )
#define KCERR_PLUGIN_ERROR			MAKE_KCERR( 29 )
#define KCERR_UNKNOWN_OBJECT			MAKE_KCERR( 30 )
#define KCERR_NOT_IMPLEMENTED		MAKE_KCERR( 31 )
#define KCERR_DATABASE_NOT_FOUND		MAKE_KCERR( 32 )
#define KCERR_INVALID_VERSION		MAKE_KCERR( 33 )
#define KCERR_UNKNOWN_DATABASE		MAKE_KCERR( 34 )
#define KCERR_NOT_INITIALIZED		MAKE_KCERR( 35 )
#define KCERR_CALL_FAILED			MAKE_KCERR( 36 )
#define KCERR_SSO_CONTINUE			MAKE_KCERR( 37 )
#define KCERR_TIMEOUT				MAKE_KCERR( 38 )
#define KCERR_INVALID_BOOKMARK		MAKE_KCERR( 39 )
#define KCERR_UNABLE_TO_COMPLETE		MAKE_KCERR( 40 )
#define KCERR_UNKNOWN_INSTANCE_ID	MAKE_KCERR( 41 )
#define KCERR_IGNORE_ME				MAKE_KCERR( 42 )
#define KCERR_BUSY					MAKE_KCERR( 43 )
#define KCERR_OBJECT_DELETED			MAKE_KCERR( 44 )
#define KCERR_USER_CANCEL			MAKE_KCERR( 45 )
#define KCERR_UNKNOWN_FLAGS			MAKE_KCERR( 46 )
#define KCERR_SUBMITTED				MAKE_KCERR( 47 )

#define erSuccess	KCERR_NONE

typedef unsigned int ECRESULT;

// FIXME which of these numbers are mapped 1-to-1 with actual
// MAPI values ? Move all fixed values to ECMAPI.h

#define KOPANO_TAG_DISPLAY_NAME		0x3001

// These must match MAPI types !
#define KOPANO_OBJTYPE_FOLDER			3
#define KOPANO_OBJTYPE_MESSAGE			5

// Sessions
typedef uint64_t ECSESSIONID;
typedef uint64_t ECSESSIONGROUPID;

#define EC_NOTIFICATION_CHECK_FREQUENTY		(1000*2)
#define EC_NOTIFICATION_CLOSE_TIMEOUT		(1000)
#define EC_SESSION_TIMEOUT					(60*5)		// In seconds
#define EC_SESSION_KEEPALIVE_TIME			(60)	// In seconds
#define EC_SESSION_TIMEOUT_CHECK			(1000*60*5)	// In microsecconds

/* the same definetions as MAPI */
#define EC_ACCESS_MODIFY                ((unsigned int) 0x00000001)
#define EC_ACCESS_READ					((unsigned int) 0x00000002)
#define EC_ACCESS_DELETE				((unsigned int) 0x00000004)
#define EC_ACCESS_CREATE_HIERARCHY		((unsigned int) 0x00000008)
#define EC_ACCESS_CREATE_CONTENTS		((unsigned int) 0x00000010)
#define EC_ACCESS_CREATE_ASSOCIATED		((unsigned int) 0x00000020)

#define EC_ACCESS_OWNER					((unsigned int) 0x0000003F)

#define ecSecurityRead			1
#define ecSecurityCreate		2
#define ecSecurityEdit			3
#define ecSecurityDelete		4
#define ecSecurityCreateFolder	5
#define ecSecurityFolderVisible	6
#define ecSecurityFolderAccess	7
#define ecSecurityOwner			8
#define ecSecurityAdmin			9

// Property under which to store the search criteria for search folders
#define PR_EC_SEARCHCRIT	PROP_TAG(PT_STRING8, 0x6706)
#define PR_EC_SUGGESTION	PROP_TAG(PT_UNICODE, 0x6707)

// create 200 directories for non-database attachments
#define ATTACH_PATHDEPTH_LEVEL1 10
#define ATTACH_PATHDEPTH_LEVEL2 20

#define ec_perror(s, r)     ec_log_ercode((r), EC_LOGLEVEL_ERROR, s ": %s (%x)", nullptr)

enum CONNECTION_TYPE {
	CONNECTION_TYPE_TCP,
	CONNECTION_TYPE_SSL,
	CONNECTION_TYPE_NAMED_PIPE,
	CONNECTION_TYPE_NAMED_PIPE_PRIORITY,
};

//Functions
extern _kc_export HRESULT kcerr_to_mapierr(ECRESULT, HRESULT hrDefault = 0x80070005 /* MAPI_E_NO_ACCESS */);
extern _kc_export ECRESULT ec_log_ercode(ECRESULT, unsigned int level, const char *fmt, const char *func);

} /* namespace */

#endif /* KC_KCODES_HPP */
