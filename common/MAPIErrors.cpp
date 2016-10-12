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
 * MAPIErrors.cpp
 * Definition of GetMAPIErrorMessage()
 */
#include <kopano/platform.h>
#include <kopano/MAPIErrors.h>
#include <mapidefs.h>
#include <kopano/stringutil.h>

#include <mapicode.h>
#include <kopano/mapiext.h>
#include <string>

typedef struct tagMAPIErrorTranslateRecord {
	HRESULT errorCode;
    const char* errorMessage;
} MAPIErrorTranslateRecord;

static const MAPIErrorTranslateRecord MAPIErrorCodes[] = {
    { hrSuccess,                            "success" },
    { MAPI_E_CALL_FAILED,                   "call failed" },
    { MAPI_E_NOT_ENOUGH_MEMORY,             "not enough memory" },
    { MAPI_E_INVALID_PARAMETER,             "missing or invalid argument" },
    { MAPI_E_INTERFACE_NOT_SUPPORTED,       "interface not supported" },
    { MAPI_E_NO_ACCESS,                     "no access" },
    { MAPI_E_NO_SUPPORT,                    "action not supported by server" },
    { MAPI_E_BAD_CHARWIDTH,                 "bad character width" },
    { MAPI_E_STRING_TOO_LONG,               "string too long" },
    { MAPI_E_UNKNOWN_FLAGS,                 "unknown flags" },
    { MAPI_E_INVALID_ENTRYID,               "invalid entry" },
    { MAPI_E_INVALID_OBJECT,                "invalid object" },
    { MAPI_E_OBJECT_CHANGED,                "object changed" },
    { MAPI_E_OBJECT_DELETED,                "object deleted" },
    { MAPI_E_BUSY,                          "busy" },
    { MAPI_E_NOT_ENOUGH_DISK,               "not enough disk" },
    { MAPI_E_NOT_ENOUGH_RESOURCES,          "not enough resources" },
    { MAPI_E_NOT_FOUND,                     "not found" },
    { MAPI_E_VERSION,                       "version mismatch" },
    { MAPI_E_LOGON_FAILED,                  "logon failed" },
    { MAPI_E_SESSION_LIMIT,                 "session limit" },
    { MAPI_E_USER_CANCEL,                   "use cancel" },
    { MAPI_E_UNABLE_TO_ABORT,               "unable to abort" },
    { MAPI_E_NETWORK_ERROR,                 "network error" },
    { MAPI_E_DISK_ERROR,                    "disk error" },
    { MAPI_E_TOO_COMPLEX,                   "too complex" },
    { MAPI_E_BAD_COLUMN,                    "bad column" },
    { MAPI_E_EXTENDED_ERROR,                "extended error" },
    { MAPI_E_COMPUTED,                      "computed" },
    { MAPI_E_CORRUPT_DATA,                  "corrupt data" },
    { MAPI_E_UNCONFIGURED,                  "unconfigured" },
    { MAPI_E_FAILONEPROVIDER,               "failoneprovider" },
    { MAPI_E_UNKNOWN_CPID,                  "unknown CPID" },
    { MAPI_E_UNKNOWN_LCID,                  "unknown LCID" },
    { MAPI_E_PASSWORD_CHANGE_REQUIRED,      "password change required" },
    { MAPI_E_PASSWORD_EXPIRED,              "password expired" },
    { MAPI_E_INVALID_WORKSTATION_ACCOUNT,   "invalid workstation account" },
    { MAPI_E_INVALID_ACCESS_TIME,           "invalid access time" },
    { MAPI_E_ACCOUNT_DISABLED,              "account disabled" },
    { MAPI_E_END_OF_SESSION,                "end of session" },
    { MAPI_E_UNKNOWN_ENTRYID,               "unknown entryid" },
    { MAPI_E_MISSING_REQUIRED_COLUMN,       "missing required column" },
    { MAPI_W_NO_SERVICE,                    "no service" },
    { MAPI_E_BAD_VALUE,                     "bad value" },
    { MAPI_E_INVALID_TYPE,                  "invalid type" },
    { MAPI_E_TYPE_NO_SUPPORT,               "type no support" },
    { MAPI_E_UNEXPECTED_TYPE,               "unexpected_type" },
    { MAPI_E_TOO_BIG,                       "too big" },
    { MAPI_E_DECLINE_COPY,                  "decline copy" },
    { MAPI_E_UNEXPECTED_ID,                 "unexpected id" },
    { MAPI_W_ERRORS_RETURNED,               "errors returned" },
    { MAPI_E_UNABLE_TO_COMPLETE,            "unable to complete" },
    { MAPI_E_TIMEOUT,                       "timeout" },
    { MAPI_E_TABLE_EMPTY,                   "table empty" },
    { MAPI_E_TABLE_TOO_BIG,                 "table too big" },
    { MAPI_E_INVALID_BOOKMARK,              "invalid bookmark" },
    { MAPI_W_POSITION_CHANGED,              "position changed" },
    { MAPI_W_APPROX_COUNT,                  "approx count" },
    { MAPI_E_WAIT,                          "wait" },
    { MAPI_E_CANCEL,                        "cancel" },
    { MAPI_E_NOT_ME,                        "not me" },
    { MAPI_W_CANCEL_MESSAGE,                "cancel message" },
    { MAPI_E_CORRUPT_STORE,                 "corrupt store" },
    { MAPI_E_NOT_IN_QUEUE,                  "not in queue" },
    { MAPI_E_NO_SUPPRESS,                   "no suppress" },
    { MAPI_E_COLLISION,                     "collision" },
    { MAPI_E_NOT_INITIALIZED,               "not initialized" },
    { MAPI_E_NON_STANDARD,                  "non standard" },
    { MAPI_E_NO_RECIPIENTS,                 "no recipients" },
    { MAPI_E_SUBMITTED,                     "submitted" },
    { MAPI_E_HAS_FOLDERS,                   "has folders" },
    { MAPI_E_HAS_MESSAGES,                  "has messages" },
    { MAPI_E_FOLDER_CYCLE,                  "folder cycle" },
    { MAPI_W_PARTIAL_COMPLETION,            "partial completion" },
    { MAPI_E_AMBIGUOUS_RECIP,               "ambiguous recipient" },
    { MAPI_E_STORE_FULL,                    "store full" },
};

/**
 * Get a string describing a MAPI error
 *
 * @param[in] errorCode MAPI error code
 * @retval    C string describing error code, or NULL if not found
 */
const char* GetMAPIErrorMessage(HRESULT errorCode)
{
    const char* retval = "Unknown error code";
    for (size_t i = 0; i < ARRAY_SIZE(MAPIErrorCodes); ++i) {
        if (MAPIErrorCodes[i].errorCode == errorCode)
        {
            retval = MAPIErrorCodes[i].errorMessage;
            break;
        }
    }
    return retval;
}

/**
 * Prints a user friendly string for an given HRESULT value.
 *
 * We should try to be as informative as possible to the user, try to
 * get a nice descriptive message for the error and in the last case
 * just print the hex code.
 *
 * @param[in]	hr		A Mapierror code
 * @param[in]	object	Optional user object type name, default "object"
 * @return		string	A description for the given error
 */
std::string getMapiCodeString(HRESULT hr, const char* object /* = "object" */)
{
	std::string retval = GetMAPIErrorMessage(hr);
	std::string space(" ");
	std::string objectstring(object);
	switch (hr) {
	case MAPI_E_NOT_FOUND:
		retval = objectstring + space + retval;
		break;
	case MAPI_E_COLLISION:
		retval = objectstring + " already exists";
		break;
	case MAPI_E_NO_ACCESS:
		retval = retval + space + objectstring;
		break;
	case MAPI_E_UNABLE_TO_COMPLETE:
		retval = "please check your license";
		break;
	case MAPI_E_INVALID_TYPE:
		retval = "invalid type combination";
		break;
	default:
		retval = retval + std::string(" (") + stringify(hr, true) + std::string(")");
	};

	return retval;
}
