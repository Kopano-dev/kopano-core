/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
/**
 * MAPIErrors.cpp
 * Definition of GetMAPIErrorMessage()
 */
#include <kopano/memory.hpp>
#include <kopano/platform.h>
#include <kopano/MAPIErrors.h>
#include <mapidefs.h>
#include <kopano/stringutil.h>
#include <mapicode.h>
#include <kopano/mapiext.h>
#include <string>
#include <kopano/ECLogger.h>
#include <kopano/kcodes.h>

namespace KC {

struct MAPIErrorTranslateRecord {
	HRESULT errorCode;
    const char* errorMessage;
};

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
 * Prints a user friendly string for a given HRESULT value.
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
	std::string objectstring(object != nullptr ? object : "");
	switch (hr) {
	case MAPI_E_NOT_FOUND:
		return "\"" + objectstring + "\" " + retval;
	case MAPI_E_COLLISION:
		return "\"" + objectstring + "\" already exists";
	case MAPI_E_NO_ACCESS:
		return retval + " \"" + objectstring + "\"";
	case MAPI_E_INVALID_TYPE:
		return "invalid type combination";
	default:
		return retval + " (" + stringify_hex(hr) + ")";
	};
}

/*
 * Some helper functions to convert the SOAP-style objects
 * to MAPI-style structs and vice-versa
 */
HRESULT kcerr_to_mapierr(ECRESULT ecResult, HRESULT hrDefault)
{
	switch (ecResult) {
	case KCERR_NONE:		return S_OK;
	//case KCERR_UNKNOWN:		return MAPI_E_UNKNOWN; // No MAPI error?
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
	//case KCERR_HAS_RECIPIENTS:	return KCERR_HAS_RECIPIENTS; // No MAPI error?
	//case KCERR_HAS_ATTACHMENTS:	return KCERR_HAS_ATTACHMENTS; // No MAPI error?
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

ECRESULT er_logcode(ECRESULT code, unsigned int level, const char *func, const char *fmt, ...)
{
	if (!ec_log_get()->Log(level))
		return code;
	char *msg = nullptr;
	va_list va;
	va_start(va, fmt);
	auto ret = vasprintf(&msg, fmt, va);
	va_end(va);
	if (ret >= 0)
		hr_logcode2(kcerr_to_mapierr(code), level, func, std::unique_ptr<char[], cstdlib_deleter>(msg));
	return code;
}

ECRESULT er_logcode(ECRESULT code, unsigned int level, const char *func, const std::string &fmt, ...)
{
	if (!ec_log_get()->Log(level))
		return code;
	char *msg = nullptr;
	va_list va;
	va_start(va, fmt);
	auto ret = vasprintf(&msg, fmt.c_str(), va);
	va_end(va);
	if (ret >= 0)
		hr_logcode2(kcerr_to_mapierr(code), level, func, std::unique_ptr<char[], cstdlib_deleter>(msg));
	return code;
}

} /* namespace */
