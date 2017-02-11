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
