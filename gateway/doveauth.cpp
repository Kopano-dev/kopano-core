/*
 * Copyright 2017 Kopano and its licensors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <memory>
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <mapix.h>
#include <kopano/zcdefs.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/stringutil.h>
#include "doveauth.h"

using namespace KC;

int authdb_mapi_logonxx(void *rq, const char *username, const char *password)
{
	std::unique_ptr<wchar_t[]> long_user, long_pass;
	object_ptr<IMAPISession> session;

	if (username == nullptr)
		return 0;
	if (password == nullptr)
		password = "";
	long_user.reset(new(std::nothrow) wchar_t[strlen(username)+1]);
	long_pass.reset(new(std::nothrow) wchar_t[strlen(password)+1]);
	if (long_user == nullptr || long_pass == nullptr ||
	    mbstowcs(long_user.get(), username, strlen(username) + 1) != strlen(username) ||
	    mbstowcs(long_pass.get(), password, strlen(password) + 1) != strlen(password))
		return -1;
	/*
	 * Don't emit PASSDB_RESULT_USER_UNKNOWN, it seems like it would allow
	 * attackers to probe for usernames. (Maybe dovecot sanitizes this
	 * later on before going to the wire?)
	 */
	auto ret = HrOpenECSession(&~session, "app_vers", "app_misc",
	           long_user.get(), long_pass.get(), nullptr, 0, 0, 0);
	if (ret != hrSuccess) {
		auto txt = format("HrOpenECSession failed for user %s: %s (%x)",
		           username, GetMAPIErrorMessage(ret), ret);
		auth_request_log_debugcc(rq, txt.c_str());
		return 0;
	}
	return 1;
}

void authdb_mapi_initxx()
{
	MAPIInitialize(nullptr);
}

void authdb_mapi_deinitxx()
{
	MAPIUninitialize();
}
