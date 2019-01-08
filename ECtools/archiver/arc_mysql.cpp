/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/ECConfig.h>
#include <kopano/database.hpp>
#include "arc_mysql.hpp"

namespace KC {

ECRESULT KCMDatabaseMySQL::Connect(ECConfig *lpConfig)
{
	/* The max length of a group_concat function */
	static constexpr const unsigned int gcm = 32768;
	/*
	 * Set auto reconnect. mysql < 5.0.4 default on, mysql 5.0.4 >
	 * reconnection default off. Archiver always wants to reconnect.
	 */
	return KDatabase::Connect(lpConfig, true, 0, gcm);
}

#define ZA_TABLEDEF_SERVERS \
	"CREATE TABLE `za_servers` ( \
		`id` int(11) unsigned NOT NULL auto_increment, \
		`guid` binary(16) NOT NULL, \
		PRIMARY KEY (`id`), \
		UNIQUE KEY `guid` (`guid`) \
	) ENGINE=%s"

#define ZA_TABLEDEF_INSTANCES \
	"CREATE TABLE `za_instances` ( \
		`id` int(11) unsigned NOT NULL auto_increment, \
		`tag` smallint(6) unsigned NOT NULL, \
		PRIMARY KEY (`id`), \
		UNIQUE KEY `instance` (`id`, `tag`) \
	) ENGINE=%s"

#define ZA_TABLEDEF_MAPPINGS \
	"CREATE TABLE `za_mappings` ( \
		`server_id` int(11) unsigned NOT NULL, \
		`val_binary` blob NOT NULL, \
		`tag` smallint(6) unsigned NOT NULL, \
		`instance_id` int(11) unsigned NOT NULL, \
		PRIMARY KEY (`server_id`, `val_binary`(64), `tag`), \
		UNIQUE KEY `instance` (`instance_id`, `tag`, `server_id`), \
		FOREIGN KEY (`server_id`) REFERENCES za_servers(`id`) ON DELETE CASCADE, \
		FOREIGN KEY (`instance_id`, `tag`) REFERENCES za_instances(`id`, `tag`) ON UPDATE RESTRICT ON DELETE CASCADE \
	) ENGINE=%s"

static constexpr const struct sSQLDatabase_t kcmsql_tables[] = {
	{"servers", ZA_TABLEDEF_SERVERS},
	{"instances", ZA_TABLEDEF_INSTANCES},
	{"mappings", ZA_TABLEDEF_MAPPINGS},
	{nullptr, nullptr},
};

const struct sSQLDatabase_t *KCMDatabaseMySQL::GetDatabaseDefs(void)
{
	return kcmsql_tables;
}

} /* namespace */
