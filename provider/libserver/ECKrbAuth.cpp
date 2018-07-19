/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#include "ECKrbAuth.h"
#ifndef HAVE_KRB5
namespace KC {
ECRESULT ECKrb5AuthenticateUser(const std::string &user,
    const std::string &pass, std::string *lpstrError)
{
	*lpstrError = "Server is not compiled with kerberos support.";
	return KCERR_NO_SUPPORT;
}
}
#else
// error_message() is wrongly typed in c++ context
extern "C" {
#include <krb5.h>
#include <et/com_err.h>
}

namespace KC {

ECRESULT ECKrb5AuthenticateUser(const std::string &strUsername, const std::string &strPassword, std::string *lpstrError)
{
	ECRESULT er = erSuccess;
	krb5_get_init_creds_opt options;
	krb5_creds my_creds;
	krb5_context ctx;
	krb5_principal me;
	char *name = NULL;

	memset(&ctx, 0, sizeof(ctx));
	memset(&me, 0, sizeof(me));

	auto code = krb5_init_context(&ctx);
	if (code) {
		*lpstrError = std::string("Unable to initialize kerberos 5 library: code ") + error_message(code);
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	code = krb5_parse_name(ctx, strUsername.c_str(), &me);
	if (code) {
		*lpstrError = std::string("Error parsing kerberos 5 username: code ") + error_message(code);
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	code = krb5_unparse_name(ctx, me, &name);
	if (code) {
		*lpstrError = std::string("Error unparsing kerberos 5 username: code ") + error_message(code);
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	krb5_get_init_creds_opt_init(&options);
	memset(&my_creds, 0, sizeof(my_creds));

	code = krb5_get_init_creds_password(ctx, &my_creds, me, (char*)strPassword.c_str(), 0, 0, 0, NULL, &options);
	if (code) {
		*lpstrError = error_message(code);
		er = KCERR_LOGON_FAILED;
		goto exit;
	} 
exit:
	if (name)
		krb5_free_unparsed_name(ctx, name);
	if (me)
		krb5_free_principal(ctx, me);
	if (ctx)
		krb5_free_context(ctx);
	memset(&ctx, 0, sizeof(ctx));
	memset(&me, 0, sizeof(me));
	return er;
}

} /* namespace */
#endif /* HAVE_KRB5 */

#ifndef HAVE_PAM
namespace KC {
ECRESULT ECPAMAuthenticateUser(const char *szPamService,
    const std::string &strUsername, const std::string &strPassword,
    std::string *lpstrError)
{
	*lpstrError = "Server is not compiled with pam support.";
	return KCERR_NO_SUPPORT;
}
}
#else
#include <security/pam_appl.h>

namespace KC {

static int converse(int num_msg, const struct pam_message **msg,
    struct pam_response **resp, void *appdata_ptr)
{
	auto password = static_cast<const char *>(appdata_ptr);

	if (!resp || !msg || !password)
		return PAM_CONV_ERR;
	auto response = static_cast<struct pam_response *>(malloc(num_msg * sizeof(**resp)));
	if (!response)
		return PAM_BUF_ERR;

	for (int i = 0; i < num_msg; ++i) {
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

ECRESULT ECPAMAuthenticateUser(const char *szPamService,
    const std::string &strUsername, const std::string &strPassword,
    std::string *lpstrError)
{
	pam_handle_t *pamh = nullptr;
	struct pam_conv conv_info = {&converse, const_cast<char *>(strPassword.c_str())};
	auto res = pam_start(szPamService, strUsername.c_str(), &conv_info, &pamh);
	if (res != PAM_SUCCESS) {
		*lpstrError = pam_strerror(nullptr, res);
		return KCERR_LOGON_FAILED;
	}
	res = pam_authenticate(pamh, PAM_SILENT);
	pam_end(pamh, res);
	if (res != PAM_SUCCESS) {
		*lpstrError = pam_strerror(nullptr, res);
		return KCERR_LOGON_FAILED;
	}
	return erSuccess;
}

} /* namespace */
#endif /* HAVE_PAM */
