/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef EC_KRBAUTH_H
#define EC_KRBAUTH_H

#include <string>
#include <kopano/platform.h>
#include <kopano/kcodes.h>

namespace KC {

/**
 * Authenticate a user through Kerberos
 * @param strUsername Username
 * @param strPassword Password
 * @param *lpstrError On error, an error string will be returned
 * @return erSuccess, KCERR_LOGON_FAILURE or other error
 */
ECRESULT ECKrb5AuthenticateUser(const std::string &strUsername, const std::string &strPassword, std::string *lpstrError);

/**
 * Authenticate a user through a PAM service
 * @param szPamService The PAM service name which exists in /etc/pam.d/
 * @param strUsername Username
 * @param strPassword Password
 * @param *lpstrError On error, an error string will be returned
 * @return erSuccess, KCERR_LOGON_FAILURE or other error
 */
extern ECRESULT ECPAMAuthenticateUser(const char *service, const std::string &user, const std::string &pass, std::string *error);

} /* namespace */

#endif
