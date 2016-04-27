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
#include <kopano/kcodes.h>
#include <kopano/mapiext.h>

#ifdef _DEBUG
#undef THIS_FILE
static const char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif


/*
 * Some helper functions to convert the SOAP-style objects
 * to MAPI-style structs and vice-versa
 */
HRESULT kcerr_to_mapierr(ECRESULT ecResult, HRESULT hrDefault)
{
	HRESULT hr = S_OK;

	switch(ecResult) {
		case KCERR_NONE:
			hr = S_OK;
			break;
		/*case KCERR_UNKNOWN://FIXME: No MAPI error?
			hr = MAPI_E_UNKNOWN;
			break;*/
		case KCERR_NOT_FOUND:
			hr = MAPI_E_NOT_FOUND;
			break;
		case KCERR_NO_ACCESS:
			hr = MAPI_E_NO_ACCESS;
			break;
		case KCERR_NETWORK_ERROR:
			hr = MAPI_E_NETWORK_ERROR;
			break;
		case KCERR_SERVER_NOT_RESPONDING:
			hr = MAPI_E_NETWORK_ERROR;
			break;
		case KCERR_INVALID_TYPE:
			hr = MAPI_E_INVALID_TYPE;
			break;
		case KCERR_DATABASE_ERROR:
			hr = MAPI_E_DISK_ERROR;
			break;
		case KCERR_COLLISION:
			hr = MAPI_E_COLLISION;
			break;
		case KCERR_LOGON_FAILED:
			hr = MAPI_E_LOGON_FAILED;
			break;
		case KCERR_HAS_MESSAGES:
			hr = MAPI_E_HAS_MESSAGES;
			break;
		case KCERR_HAS_FOLDERS:
			hr = MAPI_E_HAS_FOLDERS;
			break;
/*		case KCERR_HAS_RECIPIENTS://FIXME: No MAPI error?
			hr = KCERR_HAS_RECIPIENTS; 
			break;*/
/*		case KCERR_HAS_ATTACHMENTS://FIXME: No MAPI error?
			hr = KCERR_HAS_ATTACHMENTS;
			break;*/
		case KCERR_NOT_ENOUGH_MEMORY:
			hr = MAPI_E_NOT_ENOUGH_MEMORY;
			break;
		case KCERR_TOO_COMPLEX:
			hr = MAPI_E_TOO_COMPLEX;
			break;
		case KCERR_END_OF_SESSION:
			hr = MAPI_E_END_OF_SESSION;
			break;
		case KCERR_UNABLE_TO_ABORT:
			hr = MAPI_E_UNABLE_TO_ABORT;
			break;
		case KCWARN_CALL_KEEPALIVE: //Internal information
			hr = KCWARN_CALL_KEEPALIVE;
			break;
		case KCERR_NOT_IN_QUEUE:
			hr = MAPI_E_NOT_IN_QUEUE;
			break;
		case KCERR_INVALID_PARAMETER:
			hr = MAPI_E_INVALID_PARAMETER;
			break;
		case KCWARN_PARTIAL_COMPLETION:
			hr = MAPI_W_PARTIAL_COMPLETION;
			break;
		case KCERR_INVALID_ENTRYID:
			hr = MAPI_E_INVALID_ENTRYID;
			break;
		case KCERR_NO_SUPPORT:
		case KCERR_NOT_IMPLEMENTED:
			hr = MAPI_E_NO_SUPPORT;
			break;
		case KCERR_TOO_BIG:
			hr = MAPI_E_TOO_BIG;
			break;
		case KCWARN_POSITION_CHANGED:
			hr = MAPI_W_POSITION_CHANGED;
			break;
		case KCERR_FOLDER_CYCLE:
			hr = MAPI_E_FOLDER_CYCLE;
			break;
		case KCERR_STORE_FULL:
			hr = MAPI_E_STORE_FULL;
			break;
		case KCERR_NOT_INITIALIZED:
			hr = MAPI_E_NOT_INITIALIZED;
			break;
		case KCERR_CALL_FAILED:
			hr = MAPI_E_CALL_FAILED;
			break;
		case KCERR_TIMEOUT:
			hr = MAPI_E_TIMEOUT;
			break;
		case KCERR_INVALID_BOOKMARK:
			hr = MAPI_E_INVALID_BOOKMARK;
			break;
		case KCERR_UNABLE_TO_COMPLETE:
			hr = MAPI_E_UNABLE_TO_COMPLETE;
			break;
		case KCERR_INVALID_VERSION:
			hr = MAPI_E_VERSION;
			break;
		case KCERR_OBJECT_DELETED:
			hr = MAPI_E_OBJECT_DELETED;
			break;
		case KCERR_USER_CANCEL:
			hr = MAPI_E_USER_CANCEL;
			break;
		case KCERR_UNKNOWN_FLAGS:
			hr = MAPI_E_UNKNOWN_FLAGS;
			break;
		case KCERR_SUBMITTED:
			hr = MAPI_E_SUBMITTED;
			break;
		default:
			hr = hrDefault;
			break;
	}
	
	return hr;
}
