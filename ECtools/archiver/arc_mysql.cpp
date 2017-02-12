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

ECRESULT KCMDatabaseMySQL::DoInsert(const string &strQuery,
    unsigned int *lpulInsertId, unsigned int *lpulAffectedRows)
{
	ECRESULT er = erSuccess;
	autolock alk(*this);

	er = _Update(strQuery, lpulAffectedRows);

	if (er == erSuccess) {
		if (lpulInsertId)
			*lpulInsertId = GetInsertId();
	}
	return er;
}

ECRESULT KCMDatabaseMySQL::DoDelete(const string &strQuery,
    unsigned int *lpulAffectedRows)
{
	autolock alk(*this);
	return _Update(strQuery, lpulAffectedRows);
}

/*
 * This function updates a sequence in an atomic fashion - if called correctly;
 *
 * To make it work correctly, the state of the database connection should *NOT* be in a transaction; this would delay
 * committing of the data until a later time, causing other concurrent threads to possibly generate the same ID or lock
 * while waiting for this transaction to end. So, don't call Begin() before calling this function unless you really
 * know what you're doing.
 */
ECRESULT KCMDatabaseMySQL::DoSequence(const std::string &strSeqName,
    unsigned int ulCount, unsigned long long *lpllFirstId)
{
	ECRESULT er;
	unsigned int ulAffected = 0;

	// Attempt to update the sequence in an atomic fashion
	er = DoUpdate("UPDATE settings SET value=LAST_INSERT_ID(value+1)+" + stringify(ulCount-1) + " WHERE name = '" + strSeqName + "'", &ulAffected);
	if(er != erSuccess) {
		ec_log_crit("KCMDatabaseMySQL::DoSequence() UPDATE failed %d", er);
		return er;
	}

	// If the setting was missing, insert it now, starting at sequence 1 (not 0 for safety - maybe there's some if(ulSequenceId) code somewhere)
	if(ulAffected == 0) {
		er = Query("INSERT INTO settings (name, value) VALUES('" + strSeqName + "',LAST_INSERT_ID(1)+" + stringify(ulCount-1) + ")");
		if(er != erSuccess) {
			ec_log_crit("KCMDatabaseMySQL::DoSequence() INSERT INTO failed %d", er);
			return er;
		}
	}

	*lpllFirstId = mysql_insert_id(&m_lpMySQL);
	return er;
}

ECRESULT KCMDatabaseMySQL::Begin(void)
{
	int err;
	err = Query("BEGIN");

	if(err)
		return KCERR_DATABASE_ERROR;

	return erSuccess;
}

ECRESULT KCMDatabaseMySQL::Commit(void)
{
	int err;
	err = Query("COMMIT");

	if(err)
		return KCERR_DATABASE_ERROR;

	return erSuccess;
}

ECRESULT KCMDatabaseMySQL::Rollback(void)
{
	int err;
	err = Query("ROLLBACK");

	if(err)
		return KCERR_DATABASE_ERROR;

	return erSuccess;
}

ECRESULT KCMDatabaseMySQL::IsInnoDBSupported(void)
{
	ECRESULT	er = erSuccess;
	DB_RESULT	lpResult = NULL;
	DB_ROW		lpDBRow = NULL;

	er = DoSelect("SHOW ENGINES", &lpResult);
	if(er != erSuccess) {
		ec_log_crit("Unable to query supported database engines. Error: %s", GetError());
		goto exit;
	}

	while ((lpDBRow = FetchRow(lpResult)) != NULL) {
		if (strcasecmp(lpDBRow[0], "InnoDB") != 0)
			continue;

		if (strcasecmp(lpDBRow[1], "DISABLED") == 0) {
			// mysql has run with innodb enabled once, but disabled this.. so check your log.
			ec_log_crit("INNODB engine is disabled. Please re-enable the INNODB engine. Check your MySQL log for more information or comment out skip-innodb in the mysql configuration file.");
			er = KCERR_DATABASE_ERROR;
			goto exit;
		} else if (strcasecmp(lpDBRow[1], "YES") != 0 && strcasecmp(lpDBRow[1], "DEFAULT") != 0) {
			// mysql is incorrectly configured or compiled.
			ec_log_crit("INNODB engine is not supported. Please enable the INNODB engine in the mysql configuration file.");
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}
		break;
	}
	if (lpDBRow == NULL) {
		ec_log_crit("Unable to find 'InnoDB' engine from the mysql server. Probably INNODB is not supported.");
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}

exit:
	if(lpResult)
		FreeResult(lpResult);

	return er;
}

ECRESULT KCMDatabaseMySQL::CreateDatabase(ECConfig *lpConfig)
{
	ECRESULT er;
	string		strQuery;
	const char *lpDatabase = lpConfig->GetSetting("mysql_database");
	const char *lpMysqlPort = lpConfig->GetSetting("mysql_port");
	const char *lpMysqlSocket = lpConfig->GetSetting("mysql_socket");

	if(*lpMysqlSocket == '\0')
		lpMysqlSocket = NULL;

	// Kopano archiver database tables
	auto sDatabaseTables = GetDatabaseDefs();
	er = InitEngine(true);
	if(er != erSuccess)
		return er;

	// Connect
	if (mysql_real_connect
        (
            &m_lpMySQL,                             // address of an existing MYSQL, Before calling mysql_real_connect(), call mysql_init() to initialize the MYSQL structure.
			lpConfig->GetSetting("mysql_host"),     // may be either a host name or an IP address.
			lpConfig->GetSetting("mysql_user"),
			lpConfig->GetSetting("mysql_password"),
			NULL,                                   // database name. If db is not NULL, the connection sets the default database to this value.
			(lpMysqlPort)?atoi(lpMysqlPort):0,
			lpMysqlSocket,
			0
        ) == NULL)
	{
		ec_log_crit("Failed to connect to database: Error: %s", mysql_error(&m_lpMySQL));
		return KCERR_DATABASE_ERROR;
	}

	if(lpDatabase == NULL) {
		ec_log_crit("Unable to create database: Unknown database");
		return KCERR_DATABASE_ERROR;
	}

	ec_log_info("Create database %s", lpDatabase);

	er = IsInnoDBSupported();
	if(er != erSuccess)
		return er;

	strQuery = "CREATE DATABASE IF NOT EXISTS `"+std::string(lpConfig->GetSetting("mysql_database"))+"`";
	if(Query(strQuery) != erSuccess){
		ec_log_crit("Unable to create database: %s", GetError());
		return KCERR_DATABASE_ERROR;
	}

	strQuery = "USE `"+std::string(lpConfig->GetSetting("mysql_database"))+"`";
	er = DoInsert(strQuery);
	if(er != erSuccess)
		return er;

	// Database tables
	for (unsigned int i = 0; sDatabaseTables[i].lpSQL != NULL; ++i) {
		ec_log_info("Create table: %s", sDatabaseTables[i].lpComment);
		er = DoInsert(sDatabaseTables[i].lpSQL);
		if(er != erSuccess)
			return er;
	}

	ec_log_info("Database is created");
	return erSuccess;
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

static constexpr const sKCMSQLDatabase_t kcmsql_tables[] = {
	{"servers", ZA_TABLEDEF_SERVERS},
	{"instances", ZA_TABLEDEF_INSTANCES},
	{"mappings", ZA_TABLEDEF_MAPPINGS},
	{nullptr, nullptr},
};

const sKCMSQLDatabase_t *KCMDatabaseMySQL::GetDatabaseDefs(void)
{
	return kcmsql_tables;
}

} /* namespace */
