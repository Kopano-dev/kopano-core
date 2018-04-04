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
#include <memory>
#include <new>
#include <string>
#include "Archiver.h"
#include <kopano/ECConfig.h>
#include "ArchiverImpl.h"

namespace KC {

const char* Archiver::GetConfigPath()
{
	static std::string s_strConfigPath;

	if (s_strConfigPath.empty()) {
		const char *lpszConfigPath = getenv("KOPANO_ARCHIVER_CONF");
		if (!lpszConfigPath || lpszConfigPath[0] == '\0')
			s_strConfigPath = ECConfig::GetDefaultPath("archiver.cfg");
		else
			s_strConfigPath = lpszConfigPath;
	}

	return s_strConfigPath.c_str();
}

const configsetting_t* Archiver::GetConfigDefaults()
{
	static const configsetting_t s_lpDefaults[] = {
		// Connect settings
		{ "server_socket", "default:" },
		{ "sslkey_file",	"" },
		{ "sslkey_pass",	"", CONFIGSETTING_EXACT },

		// Archive settings
		{ "archive_enable",	"yes" },
		{ "archive_after", 	"30" },

		{ "stub_enable",	"no" },
		{ "stub_unread",	"no" },
		{ "stub_after", 	"0" },

		{ "delete_enable",	"no" },
		{ "delete_unread",	"no" },
		{ "delete_after", 	"0" },

		{ "purge_enable",	"no" },
		{ "purge_after", 	"2555" },

		{ "track_history",	"no" },
		{ "cleanup_action",	"store" },
		{ "cleanup_follow_purge_after",	"no" },
		{ "enable_auto_attach",	"no" },
		{ "auto_attach_writable",	"yes" },

		// Log options
		{"log_method", "auto", CONFIGSETTING_NONEMPTY},
		{"log_file", ""},
		{"log_level", "3", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{ "log_timestamp",	"yes" },
		{ "log_buffer_size",    "0" },

		{ "mysql_host",		"localhost" },
		{ "mysql_port",		"3306" },
		{ "mysql_user",		"root" },
		{ "mysql_password",	"",	CONFIGSETTING_EXACT },
		{ "mysql_database",	"kopano-archiver" },
		{ "mysql_socket",	"" },
		{"mysql_engine", "InnoDB"},
		{ "purge-soft-deleted", "no" },

		{ NULL, NULL },
	};

	return s_lpDefaults;
}

eResult Archiver::Create(std::unique_ptr<Archiver> *lpptrArchiver)
{
	if (lpptrArchiver == NULL)
		return InvalidParameter;
	std::unique_ptr<Archiver> ptrArchiver(new(std::nothrow) ArchiverImpl);
	if (ptrArchiver == nullptr)
		return OutOfMemory;
	*lpptrArchiver = std::move(ptrArchiver);
	return Success;
}

} /* namespace */
