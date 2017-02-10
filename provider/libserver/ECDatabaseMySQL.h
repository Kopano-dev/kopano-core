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

#ifndef ECDATABASEMYSQL_H
#define ECDATABASEMYSQL_H

#include <kopano/zcdefs.h>
#include <mutex>
#include <mysql.h>
#include <string>

#include "ECDatabase.h"

namespace KC {

class ECConfig;
class ECLogger;
class zcp_versiontuple;

class ECDatabaseMySQL _kc_final : public ECDatabase {
public:
	ECDatabaseMySQL(ECConfig *lpConfig);
	virtual ~ECDatabaseMySQL();

	// Embedded mysql
	static ECRESULT	InitLibrary(const char *lpDatabaseDir, const char *lpConfigFile);
	static void UnloadLibrary(void);
	ECRESULT Connect(void) _kc_override;
	ECRESULT Close(void) _kc_override;
	ECRESULT DoSelect(const std::string &query, DB_RESULT *result, bool stream_result = false) _kc_override;
	ECRESULT DoSelectMulti(const std::string &query) _kc_override;
	ECRESULT DoUpdate(const std::string &query, unsigned int *affected_rows = NULL) _kc_override;
	ECRESULT DoInsert(const std::string &query, unsigned int *insert_id = NULL, unsigned int *affected_rows = NULL) _kc_override;
	ECRESULT DoDelete(const std::string &query, unsigned int *affected_rows = NULL) _kc_override;
	ECRESULT DoSequence(const std::string &seqname, unsigned int ulCount, unsigned long long *first_id) _kc_override;

	//Result functions
	unsigned int GetNumRows(DB_RESULT) _kc_override;
	virtual ECRESULT GetNextResult(DB_RESULT *) _kc_override;
	virtual ECRESULT FinalizeMulti(void) _kc_override;
	DB_ROW FetchRow(DB_RESULT) _kc_override;
	DB_LENGTHS FetchRowLengths(DB_RESULT) _kc_override;
	std::string Escape(const std::string &) _kc_override;
	virtual std::string EscapeBinary(const unsigned char *, size_t) _kc_override;
	std::string EscapeBinary(const std::string &) _kc_override;
	std::string FilterBMP(const std::string &to_filter) _kc_override;
	ECRESULT ValidateTables(void) _kc_override;
	const char *GetError(void) _kc_override;
	DB_ERROR GetLastError(void) _kc_override;
	bool SuppressLockErrorLogging(bool suppress) _kc_override;
	ECRESULT Begin(void) _kc_override;
	ECRESULT Commit(void) _kc_override;
	ECRESULT Rollback(void) _kc_override;
	unsigned int GetMaxAllowedPacket(void) _kc_override { return m_ulMaxAllowedPacket; }
	void ThreadInit(void) _kc_override;
	void ThreadEnd(void) _kc_override;

	// Database maintenance functions
	ECRESULT CreateDatabase(void) _kc_override;
	// Main update unit
	ECRESULT UpdateDatabase(bool force_update, std::string &report) _kc_override;
	ECRESULT InitializeDBState(void) _kc_override;
	ECRESULT CheckExistColumn(const std::string &table, const std::string &column, bool *exist) _kc_override;
	ECRESULT CheckExistIndex(const std::string &table, const std::string &key, bool *exist) _kc_override;

	// Freememory methods
	void FreeResult(DB_RESULT sResult) _kc_override;

private:
	class autolock : private std::unique_lock<std::recursive_mutex> {
		public:
		autolock(ECDatabaseMySQL &p) :
			std::unique_lock<std::recursive_mutex>(p.m_hMutexMySql, std::defer_lock_t())
		{
			if (p.m_bAutoLock)
				lock();
		}
	};
    
	ECRESULT InitEngine();
	ECRESULT IsInnoDBSupported();
	ECRESULT InitializeDBStateInner(void);
	
	virtual ECRESULT _Update(const std::string &q, unsigned int *affected) _kc_override;
	ECRESULT Query(const std::string &strQuery);
	virtual unsigned int GetAffectedRows(void) _kc_override;
	virtual unsigned int GetInsertId(void) _kc_override;

	// Connection methods
	virtual bool isConnected(void) _kc_override;

// Database maintenance
	ECRESULT GetDatabaseVersion(zcp_versiontuple *);

	// Add a new database version record
	ECRESULT UpdateDatabaseVersion(unsigned int ulDatabaseRevision);
	ECRESULT IsUpdateDone(unsigned int ulDatabaseRevision, unsigned int ulRevision=0);
	ECRESULT GetFirstUpdate(unsigned int *lpulDatabaseRevision);

	bool m_bMysqlInitialize = false;
	bool m_bConnected = false;
	MYSQL m_lpMySQL;
	std::recursive_mutex m_hMutexMySql;
	bool m_bAutoLock = true;
	unsigned int m_ulMaxAllowedPacket = 0;
	bool m_bFirstResult = false;
	ECConfig *m_lpConfig = nullptr;
	bool m_bSuppressLockErrorLogging = false;
#ifdef DEBUG
	unsigned int m_ulTransactionState = 0;
#endif
};

} /* namespace */

#endif // #ifndef ECDATABASEMYSQL_H
