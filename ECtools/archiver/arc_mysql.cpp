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
#include <iostream>
#include "arc_mysql.hpp"
#include "mysqld_error.h"

#include <kopano/ECLogger.h>
#include <kopano/stringutil.h>
#include <kopano/ECDefs.h>
#include <kopano/ecversion.h>
#include <mapidefs.h>
#include <kopano/CommonUtil.h>

#ifdef DEBUG
#define DEBUG_SQL 0
#define DEBUG_TRANSACTION 0
#endif

#define MAX_ALLOWED_PACKET KC_DFL_MAX_PACKET_SIZE

namespace KC {

KCMDatabaseMySQL::~KCMDatabaseMySQL(void)
{
	Close();
}

ECRESULT KCMDatabaseMySQL::Connect(ECConfig *lpConfig)
{
	ECRESULT		er = erSuccess;
	std::string		strQuery;
	const char *lpMysqlPort = lpConfig->GetSetting("mysql_port");
	DB_RESULT		lpDBResult = NULL;
	DB_ROW			lpDBRow = NULL;

	/*
	 * Set auto reconnect. mysql < 5.0.4 default on, mysql 5.0.4 > reconnection default off.
	 * Kopano always wants to reconnect.
	 */
	er = InitEngine(true);
	if (er != erSuccess) {
		ec_log_crit("KCMDatabaseMySQL::Connect(): InitEngine failed %d", er);
		goto exit;
	}

	if(mysql_real_connect(&m_lpMySQL,
			lpConfig->GetSetting("mysql_host"),
			lpConfig->GetSetting("mysql_user"),
			lpConfig->GetSetting("mysql_password"),
			lpConfig->GetSetting("mysql_database"),
			(lpMysqlPort)?atoi(lpMysqlPort):0, NULL, 0) == NULL)
	{
		if (mysql_errno(&m_lpMySQL) == ER_BAD_DB_ERROR) // Database does not exist
			er = KCERR_DATABASE_NOT_FOUND;
		else
			er = KCERR_DATABASE_ERROR;

		ec_log_crit("KCMDatabaseMySQL::Connect(): database access error %d, mysql error: %s", er, mysql_error(&m_lpMySQL));
		goto exit;
	}

	// Check if the database is available, but empty
	strQuery = "SHOW tables";
	er = DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess) {
		ec_log_crit("KCMDatabaseMySQL::Connect(): \"SHOW tables\" failed %d", er);
		goto exit;
	}

	if(GetNumRows(lpDBResult) == 0) {
		er = KCERR_DATABASE_NOT_FOUND;
		ec_log_crit("KCMDatabaseMySQL::Connect(): database missing %d", er);
		goto exit;
	}

	if (lpDBResult) {
		FreeResult(lpDBResult);
		lpDBResult = NULL;
	}

	strQuery = "SHOW variables LIKE 'max_allowed_packet'";
	er = DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess) {
		ec_log_crit("KCMDatabaseMySQL::Connect(): max_allowed_packet retrieval failed %d", er);
		goto exit;
	}

	lpDBRow = FetchRow(lpDBResult);
	/* lpDBRow[0] has the variable name, [1] the value */
	if (lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL) {
		ec_log_crit("Unable to retrieve max_allowed_packet value. Assuming 16M");
		m_ulMaxAllowedPacket = (unsigned int)MAX_ALLOWED_PACKET;
	} else {
		m_ulMaxAllowedPacket = atoui(lpDBRow[1]);
	}

	m_bConnected = true;

	strQuery = "SET SESSION group_concat_max_len = " + stringify((unsigned int)MAX_GROUP_CONCAT_LEN);
	if(Query(strQuery) != 0 ) {
		er = KCERR_DATABASE_ERROR;
		ec_log_crit("KCMDatabaseMySQL::Connect(): group_concat_max_len set fail %d", er);
		goto exit;
	}

	if(Query("SET NAMES 'utf8'") != 0) {
		er = KCERR_DATABASE_ERROR;
		ec_log_crit("KCMDatabaseMySQL::Connect(): set names to utf8 failed %d", er);
		goto exit;
	}

exit:
	if(lpDBResult)
		FreeResult(lpDBResult);

	if (er != erSuccess)
		Close();

	return er;
}

#define ZA_TABLEDEF_SERVERS \
	"CREATE TABLE `za_servers` ( \
		`id` int(11) unsigned NOT NULL auto_increment, \
		`guid` binary(16) NOT NULL, \
		PRIMARY KEY (`id`), \
		UNIQUE KEY `guid` (`guid`) \
	) ENGINE=InnoDB"

#define ZA_TABLEDEF_INSTANCES \
	"CREATE TABLE `za_instances` ( \
		`id` int(11) unsigned NOT NULL auto_increment, \
		`tag` smallint(6) unsigned NOT NULL, \
		PRIMARY KEY (`id`), \
		UNIQUE KEY `instance` (`id`, `tag`) \
	) ENGINE=InnoDB"

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
	) ENGINE=InnoDB"

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
