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

#include "ECKrbAuth.h"
#ifndef HAVE_KRB5
namespace KC {
ECRESULT ECKrb5AuthenticateUser(const std::string &strUsername, const std::string &strPassword, std::string *lpstrError)
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
#endif
