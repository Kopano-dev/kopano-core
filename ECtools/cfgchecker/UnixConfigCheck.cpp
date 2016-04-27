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

#include "UnixConfigCheck.h"

#include <cstdlib>

UnixConfigCheck::UnixConfigCheck(const char *lpszConfigFile) : ECConfigCheck("Unix Configuration file", lpszConfigFile)
{
}

UnixConfigCheck::~UnixConfigCheck()
{
}

void UnixConfigCheck::loadChecks()
{
	addCheck("default_domain", CONFIG_MANDATORY);

	addCheck("fullname_charset", 0, &testCharset);

	addCheck("min_user_uid", "max_user_uid", CONFIG_MANDATORY, &testId);
	addCheck("min_group_gid", "max_group_gid", CONFIG_MANDATORY, &testId);
}

int UnixConfigCheck::testId(const config_check_t *check)
{
	if (atoi(check->value1.c_str()) < atoi(check->value2.c_str()))
		return CHECK_OK;

	printError(check->option1, "is equal or greater then \"" + check->option2 + "\" (" + check->value1 + ">=" + check->value2 + ")");
	return CHECK_ERROR;
}

