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

#include <mutex>
#include <kopano/lockhelper.hpp>
#include "ECPamAuth.h"
#ifndef HAVE_PAM
ECRESULT ECPAMAuthenticateUser(const char* szPamService, const std::string &strUsername, const std::string &strPassword, std::string *lpstrError)
{
	*lpstrError = "Server is not compiled with pam support.";
	return KCERR_NO_SUPPORT;
}
#else
#include <security/pam_appl.h>

static std::mutex g_mPAMAuthLock;

static int converse(int num_msg, const struct pam_message **msg,
    struct pam_response **resp, void *appdata_ptr)
{
	int i = 0;
	struct pam_response *response = NULL;
	char *password = (char *) appdata_ptr;

	if (!resp || !msg || !password)
		return PAM_CONV_ERR;

	response = (struct pam_response *) malloc(num_msg * sizeof(struct pam_response));
	if (!response)
		return PAM_BUF_ERR;

	for (i = 0; i < num_msg; ++i) {
		response[i].resp_retcode = 0;
		response[i].resp = 0;

		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
			response[i].resp = strdup(password);
		} else {
			free(response);
			return PAM_CONV_ERR;
		}
	}

	*resp = response;
	return PAM_SUCCESS;
}

ECRESULT ECPAMAuthenticateUser(const char* szPamService, const std::string &strUsername, const std::string &strPassword, std::string *lpstrError)
{
	int res = 0;
	pam_handle_t *pamh = NULL;
	struct pam_conv conv_info = { &converse, (void*)strPassword.c_str() };
	scoped_lock biglock(g_mPAMAuthLock);

	res = pam_start(szPamService, strUsername.c_str(), &conv_info, &pamh);
	if (res != PAM_SUCCESS) 
	{
		*lpstrError = pam_strerror(NULL, res);
		return KCERR_LOGON_FAILED;
	}

	res = pam_authenticate(pamh, PAM_SILENT);

	pam_end(pamh, res);

	if (res != PAM_SUCCESS) {
		*lpstrError = pam_strerror(NULL, res);
		return KCERR_LOGON_FAILED;
	}
	return erSuccess;
}
#endif
