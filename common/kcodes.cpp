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

#include <kopano/platform.h>
#include <mapicode.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/kcodes.h>
#include <kopano/mapiext.h>

namespace KC {

/*
 * Some helper functions to convert the SOAP-style objects
 * to MAPI-style structs and vice-versa
 */
HRESULT kcerr_to_mapierr(ECRESULT ecResult, HRESULT hrDefault)
{
	switch(ecResult) {
	case KCERR_NONE:		return S_OK;
//	case KCERR_UNKNOWN:		return MAPI_E_UNKNOWN; // No MAPI error?
	case KCERR_NOT_FOUND:		return MAPI_E_NOT_FOUND;
	case KCERR_NO_ACCESS:		return MAPI_E_NO_ACCESS;
	case KCERR_NETWORK_ERROR:	return MAPI_E_NETWORK_ERROR;
	case KCERR_SERVER_NOT_RESPONDING:	return MAPI_E_NETWORK_ERROR;
	case KCERR_INVALID_TYPE:	return MAPI_E_INVALID_TYPE;
	case KCERR_DATABASE_ERROR:	return MAPI_E_DISK_ERROR;
	case KCERR_COLLISION:		return MAPI_E_COLLISION;
	case KCERR_LOGON_FAILED:	return MAPI_E_LOGON_FAILED;
	case KCERR_HAS_MESSAGES:	return MAPI_E_HAS_MESSAGES;
	case KCERR_HAS_FOLDERS:		return MAPI_E_HAS_FOLDERS;
//	case KCERR_HAS_RECIPIENTS:	return KCERR_HAS_RECIPIENTS; // No MAPI error?
//	case KCERR_HAS_ATTACHMENTS:	return KCERR_HAS_ATTACHMENTS; // No MAPI error?
	case KCERR_NOT_ENOUGH_MEMORY:	return MAPI_E_NOT_ENOUGH_MEMORY;
	case KCERR_TOO_COMPLEX:		return MAPI_E_TOO_COMPLEX;
	case KCERR_END_OF_SESSION:	return MAPI_E_END_OF_SESSION;
	case KCWARN_CALL_KEEPALIVE: 	return KCWARN_CALL_KEEPALIVE; // Internal information
	case KCERR_UNABLE_TO_ABORT:	return MAPI_E_UNABLE_TO_ABORT;
	case KCERR_NOT_IN_QUEUE:	return MAPI_E_NOT_IN_QUEUE;
	case KCERR_INVALID_PARAMETER:	return MAPI_E_INVALID_PARAMETER;
	case KCWARN_PARTIAL_COMPLETION:	return MAPI_W_PARTIAL_COMPLETION;
	case KCERR_INVALID_ENTRYID:	return MAPI_E_INVALID_ENTRYID;
	case KCERR_NO_SUPPORT:		return MAPI_E_NO_SUPPORT;
	case KCERR_TOO_BIG:		return MAPI_E_TOO_BIG;
	case KCWARN_POSITION_CHANGED:	return MAPI_W_POSITION_CHANGED;
	case KCERR_FOLDER_CYCLE:	return MAPI_E_FOLDER_CYCLE;
	case KCERR_STORE_FULL:		return MAPI_E_STORE_FULL;
	case KCERR_NOT_IMPLEMENTED:	return MAPI_E_NO_SUPPORT;
	case KCERR_INVALID_VERSION:	return MAPI_E_VERSION;
	case KCERR_NOT_INITIALIZED:	return MAPI_E_NOT_INITIALIZED;
	case KCERR_CALL_FAILED:		return MAPI_E_CALL_FAILED;
	case KCERR_TIMEOUT:		return MAPI_E_TIMEOUT;
	case KCERR_INVALID_BOOKMARK:	return MAPI_E_INVALID_BOOKMARK;
	case KCERR_UNABLE_TO_COMPLETE:	return MAPI_E_UNABLE_TO_COMPLETE;
	case KCERR_BUSY:		return MAPI_E_BUSY;
	case KCERR_OBJECT_DELETED:	return MAPI_E_OBJECT_DELETED;
	case KCERR_USER_CANCEL:		return MAPI_E_USER_CANCEL;
	case KCERR_UNKNOWN_FLAGS:	return MAPI_E_UNKNOWN_FLAGS;
	case KCERR_SUBMITTED:		return MAPI_E_SUBMITTED;

	default:			return hrDefault;
	}
}

ECRESULT ec_log_ercode(ECRESULT code, unsigned int level,
    const char *fmt, const char *func)
{
	if (func == nullptr)
		ec_log(level, fmt, GetMAPIErrorMessage(kcerr_to_mapierr(code)), code);
	else
		ec_log(level, fmt, func, GetMAPIErrorMessage(kcerr_to_mapierr(code)), code);
	return code;
}

} /* namespace */
