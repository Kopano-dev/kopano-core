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

#include "MonitorConfigCheck.h"

MonitorConfigCheck::MonitorConfigCheck(const char *lpszConfigFile) : ECConfigCheck("Monitor Configuration file", lpszConfigFile)
{
}

void MonitorConfigCheck::loadChecks()
{
	addCheck("companyquota_warning_template", CONFIG_MANDATORY | CONFIG_HOSTED_USED, &testFile);
	addCheck("companyquota_soft_template", CONFIG_MANDATORY | CONFIG_HOSTED_USED, &testFile);
	addCheck("companyquota_hard_template", CONFIG_MANDATORY | CONFIG_HOSTED_USED, &testFile);

	addCheck("userquota_warning_template", CONFIG_MANDATORY, &testFile);
	addCheck("userquota_soft_template", CONFIG_MANDATORY, &testFile);
	addCheck("userquota_hard_template", CONFIG_MANDATORY, &testFile);
}

