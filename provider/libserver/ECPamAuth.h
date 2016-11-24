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

#ifndef __ECAUTHPAM_H_

#include <string>
#include <kopano/platform.h>
#include <kopano/kcodes.h>

namespace KC {

/**
 * Authenticate a user through a PAM service
 * @param szPamService The PAM service name which exists in /etc/pam.d/
 * @param strUsername Username
 * @param strPassword Password
 * @param *lpstrError On error, an error string will be returned
 * @return erSuccess, KCERR_LOGON_FAILURE or other error
 */
ECRESULT ECPAMAuthenticateUser(const char* szPamService, const std::string &strUsername, const std::string &strPassword, std::string *lpstrError);

} /* namespace */

#endif
