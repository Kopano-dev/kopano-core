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

#include "SpoolerConfigCheck.h"

SpoolerConfigCheck::SpoolerConfigCheck(const char *lpszConfigFile) : ECConfigCheck("Spooler Configuration file", lpszConfigFile)
{
}

SpoolerConfigCheck::~SpoolerConfigCheck()
{
}

void SpoolerConfigCheck::loadChecks()
{
	addCheck("max_threads", 0, &testNonZero);
	addCheck("always_send_delegates", 0, &testBoolean);
	addCheck("allow_redirect_spoofing", 0, &testBoolean);
	addCheck("copy_delegate_mails", 0, &testBoolean);
	addCheck("always_send_tnef", 0, &testBoolean);
}

