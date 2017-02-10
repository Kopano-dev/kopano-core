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

// ECDatabaseMySQL.h: interface for the ECDatabaseMySQL class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ARC_MYSQL_HPP
#define ARC_MYSQL_HPP 1

#include <mutex>
#include <kopano/platform.h>
#include <kopano/ECConfig.h>
#include <kopano/database.hpp>
#include <kopano/kcodes.h>
#include <mysql.h>
#include <string>

using namespace std;

namespace KC {

// The max length of a group_concat function
#define MAX_GROUP_CONCAT_LEN		32768

enum {
	// The maximum packet size. This is automatically also the maximum
	// size of a single entry in the database.
	KC_DFL_MAX_PACKET_SIZE = 16777216,
};

struct sKCMSQLDatabase_t {
	const char *lpComment;
	const char *lpSQL;
};

class KCMDatabaseMySQL _kc_final : public KDatabase {
public:
	KCMDatabaseMySQL(void);
	virtual ~KCMDatabaseMySQL(void);
	ECRESULT		Connect(ECConfig *lpConfig);
	virtual ECRESULT Close(void) _kc_override;
	virtual ECRESULT DoSelect(const std::string &query, DB_RESULT *, bool stream = false) _kc_override;
	virtual ECRESULT DoUpdate(const std::string &query, unsigned int *affect = nullptr) _kc_override;
	virtual ECRESULT DoInsert(const std::string &query, unsigned int *insert_id = nullptr, unsigned int *affect = nullptr) _kc_override;
	virtual ECRESULT DoDelete(const std::string &query, unsigned int *affect = nullptr) _kc_override;
	virtual ECRESULT DoSequence(const std::string &seq, unsigned int count, unsigned long long *first_id) _kc_override;
	const sKCMSQLDatabase_t *GetDatabaseDefs(void);

	//Result functions
	virtual unsigned int GetNumRows(DB_RESULT) _kc_override;
	virtual DB_ROW FetchRow(DB_RESULT) _kc_override;
	virtual DB_LENGTHS FetchRowLengths(DB_RESULT) _kc_override;
	virtual std::string Escape(const std::string &) _kc_override;
	virtual std::string EscapeBinary(const unsigned char *, size_t) _kc_override;
	virtual std::string EscapeBinary(const std::string &) _kc_override;
	virtual const char *GetError(void) _kc_override;
	virtual ECRESULT Begin(void) _kc_override;
	virtual ECRESULT Commit(void) _kc_override;
	virtual ECRESULT Rollback(void) _kc_override;
	virtual unsigned int GetMaxAllowedPacket(void) _kc_override;

	// Database maintenance function(s)
	ECRESULT		CreateDatabase(ECConfig *lpConfig);

	// Freememory method(s)
	virtual void FreeResult(DB_RESULT) _kc_override;

private:
	class autolock : private std::unique_lock<std::recursive_mutex> {
		public:
		autolock(KCMDatabaseMySQL &p) :
			std::unique_lock<std::recursive_mutex>(p.m_hMutexMySql, std::defer_lock_t())
		{
			if (p.m_bAutoLock)
				lock();
		}
	};

	ECRESULT InitEngine();
	ECRESULT IsInnoDBSupported();

	virtual ECRESULT _Update(const std::string &q, unsigned int *affected) _kc_override;
	int Query(const string &strQuery);
	virtual unsigned int GetAffectedRows(void) _kc_override;
	virtual unsigned int GetInsertId(void) _kc_override;

	// Connection methods
	virtual bool isConnected(void) _kc_override;

	bool m_bMysqlInitialize = false, m_bConnected = false;
	bool m_bAutoLock = true;
	unsigned int m_ulMaxAllowedPacket = KC_DFL_MAX_PACKET_SIZE;
	MYSQL				m_lpMySQL;
	std::recursive_mutex m_hMutexMySql;
};

} /* namespace */

#endif
