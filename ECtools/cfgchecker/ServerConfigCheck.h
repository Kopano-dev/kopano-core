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

#ifndef SERVERCONFIGCHECK_H
#define SERVERCONFIGCHECK_H

#include <kopano/zcdefs.h>
#include "ECConfigCheck.h"

class ServerConfigCheck _kc_final : public ECConfigCheck {
public:
	ServerConfigCheck(const char *lpszConfigFile);
	void loadChecks();

private:
	static int testAttachment(const config_check_t *);
	static int testPluginConfig(const config_check_t *);
	static int testAttachmentPath(const config_check_t *);
	static int testPlugin(const config_check_t *);
	static int testPluginPath(const config_check_t *);
	static int testStorename(const config_check_t *);
	static int testLoginname(const config_check_t *);
	static int testAuthMethod(const config_check_t *);
};

#endif

