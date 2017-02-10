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
