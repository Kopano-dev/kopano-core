/*
 * Copyright 2005-2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License, version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <memory>
#include <string>
#include <cassert>
#include <cstring>
#include <mysql.h>
#include <mysqld_error.h>
#include <kopano/ECLogger.h>
#include <kopano/database.hpp>
#define LOG_SQL_DEBUG(_msg, ...) \
	ec_log(EC_LOGLEVEL_DEBUG | EC_LOGLEVEL_SQL, _msg, ##__VA_ARGS__)

KDatabase::KDatabase(void)
{
	memset(&m_lpMySQL, 0, sizeof(m_lpMySQL));
}

ECRESULT KDatabase::Close(void)
{
	/* No locking here */
	m_bConnected = false;
	if (m_bMysqlInitialize)
		mysql_close(&m_lpMySQL);
	m_bMysqlInitialize = false;
	return erSuccess;
}

/**
 * Perform a SELECT operation on the database
 * @q: (in) SELECT query string
 * @res_p: (out) Result output
 * @stream: (in) Whether data should be streamed instead of stored
 *
 * Sends the passed SELECT-like (any operation that outputs a result set) query
 * to the MySQL server and retrieves the result.
 *
 * Setting @stream will delay retrieving data from the network until FetchRow()
 * is called. The only drawback is that GetRowCount() can therefore not be used
 * unless all rows are fetched first. The main reason to use this is to
 * conserve memory and increase pipelining (the client can start processing
 * data before the server has completed the query)
 *
 * Returns erSuccess or %KCERR_DATABASE_ERROR.
 */
ECRESULT KDatabase::DoSelect(const std::string &q, DB_RESULT *res_p,
    bool stream)
{
	assert(q.length() != 0);
	autolock alk(*this);

	if (Query(q) != erSuccess) {
		ec_log_err("KDatabsae::DoSelect(): query failed: %s: %s", q.c_str(), GetError());
		return KCERR_DATABASE_ERROR;
	}

	ECRESULT er = erSuccess;
	DB_RESULT res;
	if (stream)
		res = mysql_use_result(&m_lpMySQL);
	else
		res = mysql_store_result(&m_lpMySQL);
	if (res == nullptr) {
		if (!m_bSuppressLockErrorLogging ||
		    GetLastError() == DB_E_UNKNOWN)
			ec_log_err("SQL [%08lu] result failed: %s, Query: \"%s\"",
				m_lpMySQL.thread_id, mysql_error(&m_lpMySQL), q.c_str());
		er = KCERR_DATABASE_ERROR;
	}
	if (res_p != nullptr)
		*res_p = res;
	else if (res != nullptr)
		FreeResult(res);
	return er;
}

/**
 * Perform an UPDATE operation on the database
 * @q: (in) UPDATE query string
 * @aff: (out) (optional) Receives the number of affected rows
 *
 * Sends the passed UPDATE query to the MySQL server, and optionally returns
 * the number of affected rows. The affected rows is the number of rows that
 * have been MODIFIED, which is not necessarily the number of rows that MATCHED
 * the WHERE clause.
 *
 * Returns erSuccess or %KCERR_DATABASE_ERROR.
 */
ECRESULT KDatabase::DoUpdate(const std::string &q, unsigned int *aff)
{
	autolock alk(*this);
	return _Update(q, aff);
}

std::string KDatabase::Escape(const std::string &s)
{
	auto size = s.length() * 2 + 1;
	std::unique_ptr<char[]> esc(new char[size]);

	memset(esc.get(), 0, size);
	mysql_real_escape_string(&m_lpMySQL, esc.get(), s.c_str(), s.length());
	return esc.get();
}

std::string KDatabase::EscapeBinary(const unsigned char *data, size_t len)
{
	auto size = len * 2 + 1;
	std::unique_ptr<char[]> esc(new char[size]);

	memset(esc.get(), 0, size);
	mysql_real_escape_string(&m_lpMySQL, esc.get(), reinterpret_cast<const char *>(data), len);
	return "'" + std::string(esc.get()) + "'";
}

std::string KDatabase::EscapeBinary(const std::string &s)
{
	return EscapeBinary(reinterpret_cast<const unsigned char *>(s.c_str()), s.size());
}

DB_ROW KDatabase::FetchRow(DB_RESULT r)
{
	return mysql_fetch_row(static_cast<MYSQL_RES *>(r));
}

DB_LENGTHS KDatabase::FetchRowLengths(DB_RESULT r)
{
	return mysql_fetch_lengths(static_cast<MYSQL_RES *>(r));
}

void KDatabase::FreeResult(DB_RESULT r)
{
	assert(r != nullptr);
	if (r != nullptr)
		mysql_free_result(static_cast<MYSQL_RES *>(r));
}

unsigned int KDatabase::GetAffectedRows(void)
{
	return mysql_affected_rows(&m_lpMySQL);
}

const char *KDatabase::GetError(void)
{
	if (!m_bMysqlInitialize)
		return "MYSQL not initialized";
	return mysql_error(&m_lpMySQL);
}

unsigned int KDatabase::GetInsertId(void)
{
	return mysql_insert_id(&m_lpMySQL);
}

DB_ERROR KDatabase::GetLastError(void)
{
	switch (mysql_errno(&m_lpMySQL)) {
	case ER_LOCK_WAIT_TIMEOUT:
		return DB_E_LOCK_WAIT_TIMEOUT;
	case ER_LOCK_DEADLOCK:
		return DB_E_LOCK_DEADLOCK;
	default:
		return DB_E_UNKNOWN;
	}
}

unsigned int KDatabase::GetNumRows(DB_RESULT r)
{
	return mysql_num_rows(static_cast<MYSQL_RES *>(r));
}

ECRESULT KDatabase::InitEngine(bool reconnect)
{
	assert(!m_bMysqlInitialize);
	if (!m_bMysqlInitialize && mysql_init(&m_lpMySQL) == nullptr) {
		ec_log_crit("KDatabase::InitEngine() mysql_init failed");
		return KCERR_DATABASE_ERROR;
	}
	m_bMysqlInitialize = true;
	m_lpMySQL.reconnect = reconnect;
	return erSuccess;
}

ECRESULT KDatabase::Query(const std::string &q)
{
	LOG_SQL_DEBUG("SQL [%08lu]: \"%s;\"", m_lpMySQL.thread_id, q.c_str());
	/* Be binary safe (http://dev.mysql.com/doc/mysql/en/mysql-real-query.html) */
	auto err = mysql_real_query(&m_lpMySQL, q.c_str(), q.length());
	if (err == 0)
		return erSuccess;
	/* Callers without reconnect will emit different messages. */
	if (m_lpMySQL.reconnect)
		ec_log_err("%p: SQL Failed: %s, Query: \"%s\"",
			static_cast<void *>(&m_lpMySQL), mysql_error(&m_lpMySQL),
			q.c_str());
	return KCERR_DATABASE_ERROR;
}

ECRESULT KDatabase::_Update(const std::string &q, unsigned int *aff)
{
	if (Query(q) != 0) {
		ec_log_err("KDatabase::_Update() query failed: %s: %s",
			q.c_str(), GetError());
		return KCERR_DATABASE_ERROR;
	}
	if (aff != nullptr)
		*aff = GetAffectedRows();
	return erSuccess;
}
