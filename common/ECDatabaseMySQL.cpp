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

#include <iostream>
#include "ECDatabaseMySQL.h"
#include "mysqld_error.h"

#include <kopano/stringutil.h>
#include <kopano/ECDefs.h>
#include <kopano/ecversion.h>
#include <mapidefs.h>
#include <kopano/CommonUtil.h>

#ifdef DEBUG
#define DEBUG_SQL 0
#define DEBUG_TRANSACTION 0
#endif

// The maximum packet size. This is automatically also the maximum
// size of a single entry in the database.
#define MAX_ALLOWED_PACKET			16777216

ECDatabaseMySQL::ECDatabaseMySQL(ECLogger *lpLogger)
{
	m_bMysqlInitialize	= false;
	m_bConnected		= false;
	m_bLocked			= false;
	m_bAutoLock			= true;
	m_lpLogger			= lpLogger;
	m_lpLogger->AddRef();

	// Create a mutex handle for mysql
	pthread_mutexattr_t mattr;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_hMutexMySql, &mattr);
}

ECDatabaseMySQL::~ECDatabaseMySQL()
{
	Close();
	m_lpLogger->Release();

	// Close the mutex handle of mysql
	pthread_mutex_destroy(&m_hMutexMySql);
}

ECRESULT ECDatabaseMySQL::InitEngine()
{
	//Init mysql and make a connection
	if (!m_bMysqlInitialize && mysql_init(&m_lpMySQL) == NULL) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::InitEngine() mysql_init failed");
		return KCERR_DATABASE_ERROR;
	}

	m_bMysqlInitialize = true;

	// Set auto reconnect
	// mysql < 5.0.4 default on, mysql 5.0.4 > reconnection default off
	// Kopano always wants to reconnect
	my_bool xtrue = true;
	mysql_options(&m_lpMySQL, MYSQL_OPT_RECONNECT, &xtrue);
	return erSuccess;
}

ECLogger* ECDatabaseMySQL::GetLogger()
{
	ASSERT(m_lpLogger);
	return m_lpLogger;
}

ECRESULT ECDatabaseMySQL::Connect(ECConfig *lpConfig)
{
	ECRESULT		er = erSuccess;
	std::string		strQuery;
	const char *lpMysqlPort = lpConfig->GetSetting("mysql_port");
	DB_RESULT		lpDBResult = NULL;
	DB_ROW			lpDBRow = NULL;

	er = InitEngine();
	if (er != erSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::Connect(): InitEngine failed %d", er);
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

		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::Connect(): database access error %d, mysql error: %s", er, mysql_error(&m_lpMySQL));

		goto exit;
	}

	// Check if the database is available, but empty
	strQuery = "SHOW tables";
	er = DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::Connect(): \"SHOW tables\" failed %d", er);
		goto exit;
	}

	if(GetNumRows(lpDBResult) == 0) {
		er = KCERR_DATABASE_NOT_FOUND;
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::Connect(): database missing %d", er);
		goto exit;
	}

	if (lpDBResult) {
		FreeResult(lpDBResult);
		lpDBResult = NULL;
	}

	strQuery = "SHOW variables LIKE 'max_allowed_packet'";
	er = DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::Connect(): max_allowed_packet retrieval failed %d", er);
		goto exit;
	}

	lpDBRow = FetchRow(lpDBResult);
	/* lpDBRow[0] has the variable name, [1] the value */
	if (lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to retrieve max_allowed_packet value. Assuming 16M");
		m_ulMaxAllowedPacket = (unsigned int)MAX_ALLOWED_PACKET;
	} else {
		m_ulMaxAllowedPacket = atoui(lpDBRow[1]);
	}

	m_bConnected = true;

	strQuery = "SET SESSION group_concat_max_len = " + stringify((unsigned int)MAX_GROUP_CONCAT_LEN);
	if(Query(strQuery) != 0 ) {
		er = KCERR_DATABASE_ERROR;
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::Connect(): group_concat_max_len set fail %d", er);
		goto exit;
	}

	if(Query("SET NAMES 'utf8'") != 0) {
		er = KCERR_DATABASE_ERROR;
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::Connect(): set names to utf8 failed %d", er);
		goto exit;
	}

exit:
	if(lpDBResult)
		FreeResult(lpDBResult);

	if (er != erSuccess)
		Close();

	return er;
}

ECRESULT ECDatabaseMySQL::Close()
{
	ECRESULT er = erSuccess;

	_ASSERT(m_bLocked == false);

	//INFO: No locking here

	m_bConnected = false;

	// Close mysql data connection and deallocate data
	if(m_bMysqlInitialize)
		mysql_close(&m_lpMySQL);

	m_bMysqlInitialize = false;

	return er;
}

// Get database ownership
bool ECDatabaseMySQL::Lock()
{

	pthread_mutex_lock(&m_hMutexMySql);

	m_bLocked = true;

	return m_bLocked;
}

// Release the database ownership
bool ECDatabaseMySQL::UnLock()
{
	pthread_mutex_unlock(&m_hMutexMySql);

	m_bLocked = false;

	return m_bLocked;
}

bool ECDatabaseMySQL::isConnected() {

	return m_bConnected;
}

int ECDatabaseMySQL::Query(const string &strQuery) {
	int err;

#ifdef DEBUG_SQL
#if DEBUG_SQL
	m_lpLogger->Log(EC_LOGLEVEL_FATAL, "%p: DO_SQL: \"%s;\"", (void*)&m_lpMySQL, strQuery.c_str());
#endif
#endif

	// use mysql_real_query to be binary safe ( http://dev.mysql.com/doc/mysql/en/mysql-real-query.html )
	err = mysql_real_query( &m_lpMySQL, strQuery.c_str(), strQuery.length() );

	if (err)
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "%p: SQL Failed: %s, Query: \"%s\"", (void*)&m_lpMySQL, mysql_error(&m_lpMySQL), strQuery.c_str());

	return err;
}

ECRESULT ECDatabaseMySQL::DoSelect(const string &strQuery, DB_RESULT *lpResult, bool bStream) {

	ECRESULT er = erSuccess;

	_ASSERT(strQuery.length()!= 0 && lpResult != NULL);

	// Autolock, lock data
	if(m_bAutoLock)
		Lock();

	if (Query(strQuery) != 0) {
		er = KCERR_DATABASE_ERROR;
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::DoSelect(): Failed invoking '%s'", strQuery.c_str());
		goto exit;
	}

	if(bStream)
		*lpResult = mysql_use_result(&m_lpMySQL);
	else
		*lpResult = mysql_store_result(&m_lpMySQL);

	if (*lpResult == NULL) {
		er = KCERR_DATABASE_ERROR;
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "%p: SQL result failed: %s, Query: \"%s\"", (void*)&m_lpMySQL, mysql_error(&m_lpMySQL), strQuery.c_str());
	}

exit:
	// Autolock, unlock data
	if(m_bAutoLock)
		UnLock();

	return er;
}

ECRESULT ECDatabaseMySQL::DoUpdate(const string &strQuery, unsigned int *lpulAffectedRows) {

	ECRESULT er = erSuccess;

	// Autolock, lock data
	if(m_bAutoLock)
		Lock();

	er = _Update(strQuery, lpulAffectedRows);

	// Autolock, unlock data
	if(m_bAutoLock)
		UnLock();

	return er;
}

ECRESULT ECDatabaseMySQL::_Update(const string &strQuery, unsigned int *lpulAffectedRows)
{
	if (Query(strQuery) != 0) {
		// FIXME: Add the mysql error system ?
		// er = nMysqlError;
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::_Update(): Failed invoking '%s'", strQuery.c_str());
		return KCERR_DATABASE_ERROR;
	}

	if(lpulAffectedRows)
		*lpulAffectedRows = GetAffectedRows();
	return erSuccess;
}

ECRESULT ECDatabaseMySQL::DoInsert(const string &strQuery, unsigned int *lpulInsertId, unsigned int *lpulAffectedRows)
{
	ECRESULT er = erSuccess;

	// Autolock, lock data
	if(m_bAutoLock)
		Lock();

	er = _Update(strQuery, lpulAffectedRows);

	if (er == erSuccess) {
		if (lpulInsertId)
			*lpulInsertId = GetInsertId();
	}

	// Autolock, unlock data
	if(m_bAutoLock)
		UnLock();

	return er;
}

ECRESULT ECDatabaseMySQL::DoDelete(const string &strQuery, unsigned int *lpulAffectedRows) {

	ECRESULT er = erSuccess;

	// Autolock, lock data
	if(m_bAutoLock)
		Lock();

	er = _Update(strQuery, lpulAffectedRows);

	// Autolock, unlock data
	if(m_bAutoLock)
		UnLock();

	return er;
}

/*
 * This function updates a sequence in an atomic fashion - if called correctly;
 *
 * To make it work correctly, the state of the database connection should *NOT* be in a transaction; this would delay
 * committing of the data until a later time, causing other concurrent threads to possibly generate the same ID or lock
 * while waiting for this transaction to end. So, don't call Begin() before calling this function unless you really
 * know what you're doing.
 */
ECRESULT ECDatabaseMySQL::DoSequence(const std::string &strSeqName, unsigned int ulCount, uint64_t *lpllFirstId) {
	ECRESULT er;
	unsigned int ulAffected = 0;

	// Attempt to update the sequence in an atomic fashion
	er = DoUpdate("UPDATE settings SET value=LAST_INSERT_ID(value+1)+" + stringify(ulCount-1) + " WHERE name = '" + strSeqName + "'", &ulAffected);
	if(er != erSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::DoSequence() UPDATE failed %d", er);
		return er;
	}

	// If the setting was missing, insert it now, starting at sequence 1 (not 0 for safety - maybe there's some if(ulSequenceId) code somewhere)
	if(ulAffected == 0) {
		er = Query("INSERT INTO settings (name, value) VALUES('" + strSeqName + "',LAST_INSERT_ID(1)+" + stringify(ulCount-1) + ")");
		if(er != erSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "ECDatabaseMySQL::DoSequence() INSERT INTO failed %d", er);
			return er;
		}
	}

	*lpllFirstId = mysql_insert_id(&m_lpMySQL);
	return er;
}

unsigned int ECDatabaseMySQL::GetAffectedRows() {

	return (unsigned int)mysql_affected_rows(&m_lpMySQL);
}

unsigned int ECDatabaseMySQL::GetInsertId() {

	return (unsigned int)mysql_insert_id(&m_lpMySQL);
}

void ECDatabaseMySQL::FreeResult(DB_RESULT sResult) {

	_ASSERT(sResult != NULL);

	if(sResult)
		mysql_free_result((MYSQL_RES *)sResult);
}

unsigned int ECDatabaseMySQL::GetNumRows(DB_RESULT sResult) {

	return (unsigned int)mysql_num_rows((MYSQL_RES *)sResult);
}

DB_ROW ECDatabaseMySQL::FetchRow(DB_RESULT sResult) {

	return mysql_fetch_row((MYSQL_RES *)sResult);
}

DB_LENGTHS ECDatabaseMySQL::FetchRowLengths(DB_RESULT sResult) {

	return (DB_LENGTHS)mysql_fetch_lengths((MYSQL_RES *)sResult);
}

std::string ECDatabaseMySQL::Escape(const std::string &strToEscape)
{
	ULONG size = strToEscape.length()*2+1;
	char *szEscaped = new char[size];
	std::string escaped;

	memset(szEscaped, 0, size);

	mysql_real_escape_string(&this->m_lpMySQL, szEscaped, strToEscape.c_str(), strToEscape.length());

	escaped = szEscaped;

	delete [] szEscaped;

	return escaped;
}

std::string ECDatabaseMySQL::EscapeBinary(const unsigned char *lpData, unsigned int ulLen)
{
	ULONG size = ulLen*2+1;
	char *szEscaped = new char[size];
	std::string escaped;

	memset(szEscaped, 0, size);

	mysql_real_escape_string(&this->m_lpMySQL, szEscaped, (const char *)lpData, ulLen);

	escaped = szEscaped;

	delete [] szEscaped;

	return "'" + escaped + "'";
}

std::string ECDatabaseMySQL::EscapeBinary(const std::string &strData)
{
	return EscapeBinary(reinterpret_cast<const unsigned char *>(strData.c_str()), strData.size());
}

const char *ECDatabaseMySQL::GetError(void)
{
	if (m_bMysqlInitialize == false)
		return "MYSQL not initialized";

	return mysql_error(&m_lpMySQL);
}

ECRESULT ECDatabaseMySQL::Begin() {
	int err;
	err = Query("BEGIN");

	if(err)
		return KCERR_DATABASE_ERROR;

	return erSuccess;
}

ECRESULT ECDatabaseMySQL::Commit() {
	int err;
	err = Query("COMMIT");

	if(err)
		return KCERR_DATABASE_ERROR;

	return erSuccess;
}

ECRESULT ECDatabaseMySQL::Rollback() {
	int err;
	err = Query("ROLLBACK");

	if(err)
		return KCERR_DATABASE_ERROR;

	return erSuccess;
}

unsigned int ECDatabaseMySQL::GetMaxAllowedPacket() {
    return m_ulMaxAllowedPacket;
}

ECRESULT ECDatabaseMySQL::IsInnoDBSupported()
{
	ECRESULT	er = erSuccess;
	DB_RESULT	lpResult = NULL;
	DB_ROW		lpDBRow = NULL;

	er = DoSelect("SHOW ENGINES", &lpResult);
	if(er != erSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to query supported database engines. Error: %s", GetError());
		goto exit;
	}

	while ((lpDBRow = FetchRow(lpResult)) != NULL) {
		if (strcasecmp(lpDBRow[0], "InnoDB") != 0)
			continue;

		if (strcasecmp(lpDBRow[1], "DISABLED") == 0) {
			// mysql has run with innodb enabled once, but disabled this.. so check your log.
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "INNODB engine is disabled. Please re-enable the INNODB engine. Check your MySQL log for more information or comment out skip-innodb in the mysql configuration file.");
			er = KCERR_DATABASE_ERROR;
			goto exit;
		} else if (strcasecmp(lpDBRow[1], "YES") != 0 && strcasecmp(lpDBRow[1], "DEFAULT") != 0) {
			// mysql is incorrectly configured or compiled.
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "INNODB engine is not supported. Please enable the INNODB engine in the mysql configuration file.");
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}
		break;
	}
	if (lpDBRow == NULL) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to find 'InnoDB' engine from the mysql server. Probably INNODB is not supported.");
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}

exit:
	if(lpResult)
		FreeResult(lpResult);

	return er;
}

ECRESULT ECDatabaseMySQL::CreateDatabase(ECConfig *lpConfig)
{
	ECRESULT er;
	string		strQuery;
	const char *lpDatabase = lpConfig->GetSetting("mysql_database");
	const char *lpMysqlPort = lpConfig->GetSetting("mysql_port");
	const char *lpMysqlSocket = lpConfig->GetSetting("mysql_socket");

	if(*lpMysqlSocket == '\0')
		lpMysqlSocket = NULL;

	// Kopano archiver database tables
	const sSQLDatabase_t *sDatabaseTables = GetDatabaseDefs();

	er = InitEngine();
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
		m_lpLogger->Log(EC_LOGLEVEL_FATAL,"Failed to connect to database: Error: %s", mysql_error(&m_lpMySQL));
		return KCERR_DATABASE_ERROR;
	}

	if(lpDatabase == NULL) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL,"Unable to create database: Unknown database");
		return KCERR_DATABASE_ERROR;
	}

	m_lpLogger->Log(EC_LOGLEVEL_NOTICE,"Create database %s", lpDatabase);

	er = IsInnoDBSupported();
	if(er != erSuccess)
		return er;

	strQuery = "CREATE DATABASE IF NOT EXISTS `"+std::string(lpConfig->GetSetting("mysql_database"))+"`";
	if(Query(strQuery) != erSuccess){
		m_lpLogger->Log(EC_LOGLEVEL_FATAL,"Unable to create database: %s", GetError());
		return KCERR_DATABASE_ERROR;
	}

	strQuery = "USE `"+std::string(lpConfig->GetSetting("mysql_database"))+"`";
	er = DoInsert(strQuery);
	if(er != erSuccess)
		return er;

	// Database tables
	for (unsigned int i = 0; sDatabaseTables[i].lpSQL != NULL; ++i) {
		m_lpLogger->Log(EC_LOGLEVEL_NOTICE,"Create table: %s", sDatabaseTables[i].lpComment);
		er = DoInsert(sDatabaseTables[i].lpSQL);
		if(er != erSuccess)
			return er;
	}

	m_lpLogger->Log(EC_LOGLEVEL_NOTICE,"Database is created");
	return erSuccess;
}
