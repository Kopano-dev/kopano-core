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
#include "arc_database.hpp"

namespace KC {

#define ZA_TABLEDEF_SERVERS			"CREATE TABLE `za_servers` ( \
										`id` int(11) unsigned NOT NULL auto_increment, \
										`guid` binary(16) NOT NULL, \
										PRIMARY KEY (`id`), \
										UNIQUE KEY `guid` (`guid`) \
									) ENGINE=InnoDB"

#define ZA_TABLEDEF_INSTANCES		"CREATE TABLE `za_instances` ( \
										`id` int(11) unsigned NOT NULL auto_increment, \
										`tag` smallint(6) unsigned NOT NULL, \
										PRIMARY KEY (`id`), \
										UNIQUE KEY `instance` (`id`, `tag`) \
									) ENGINE=InnoDB"

#define ZA_TABLEDEF_MAPPINGS		"CREATE TABLE `za_mappings` ( \
										`server_id` int(11) unsigned NOT NULL, \
										`val_binary` blob NOT NULL, \
										`tag` smallint(6) unsigned NOT NULL, \
										`instance_id` int(11) unsigned NOT NULL, \
										PRIMARY KEY (`server_id`, `val_binary`(64), `tag`), \
										UNIQUE KEY `instance` (`instance_id`, `tag`, `server_id`), \
										FOREIGN KEY (`server_id`) REFERENCES za_servers(`id`) ON DELETE CASCADE, \
										FOREIGN KEY (`instance_id`, `tag`) REFERENCES za_instances(`id`, `tag`) ON UPDATE RESTRICT ON DELETE CASCADE \
									) ENGINE=InnoDB"

static constexpr sKCMSQLDatabase_t tables[] = {
	{"servers", ZA_TABLEDEF_SERVERS},
	{"instances", ZA_TABLEDEF_INSTANCES},
	{"mappings", ZA_TABLEDEF_MAPPINGS},
	{NULL, NULL}
};

const sKCMSQLDatabase_t *ARCDatabase::GetDatabaseDefs(void)
{
	return tables;
}

} /* namespace */
