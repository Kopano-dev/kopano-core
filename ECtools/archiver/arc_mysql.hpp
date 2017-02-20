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
#include <kopano/kcodes.h>
#include <mysql.h>
#include <string>

using namespace std;

namespace KC {

typedef void *			DB_RESULT;	
typedef char **			DB_ROW;	
typedef unsigned long *	DB_LENGTHS;

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

class KCMDatabaseMySQL {
public:
	KCMDatabaseMySQL(void);
	virtual ~KCMDatabaseMySQL(void);
	ECRESULT		Connect(ECConfig *lpConfig);
	ECRESULT		Close();
	ECRESULT		DoSelect(const std::string &strQuery, DB_RESULT *lpResult, bool bStream = false);
	ECRESULT		DoUpdate(const std::string &strQuery, unsigned int *lpulAffectedRows = NULL);
	ECRESULT		DoInsert(const std::string &strQuery, unsigned int *lpulInsertId = NULL, unsigned int *lpulAffectedRows = NULL);
	ECRESULT		DoDelete(const std::string &strQuery, unsigned int *lpulAffectedRows = NULL);
		ECRESULT		DoSequence(const std::string &strSeqName, unsigned int ulCount, uint64_t *lpllFirstId);
	virtual const sKCMSQLDatabase_t *GetDatabaseDefs(void) = 0;

	//Result functions
	unsigned int	GetNumRows(DB_RESULT sResult);
	DB_ROW			FetchRow(DB_RESULT sResult);
	DB_LENGTHS		FetchRowLengths(DB_RESULT sResult);

	std::string		Escape(const std::string &strToEscape);
	std::string		EscapeBinary(const unsigned char *lpData, unsigned int ulLen);
	std::string		EscapeBinary(const std::string &strData);

	const char *GetError(void);
	
	ECRESULT		Begin();
	ECRESULT		Commit();
	ECRESULT		Rollback();
	
	unsigned int	GetMaxAllowedPacket();

	// Database maintenance function(s)
	ECRESULT		CreateDatabase(ECConfig *lpConfig);

	// Freememory method(s)
	void			FreeResult(DB_RESULT sResult);

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

	ECRESULT _Update(const string &strQuery, unsigned int *lpulAffectedRows);
	int Query(const string &strQuery);
	unsigned int GetAffectedRows();
	unsigned int GetInsertId();

	// Connection methods
	bool isConnected();

	bool m_bMysqlInitialize = false, m_bConnected = false;
	bool m_bAutoLock = true;
	unsigned int m_ulMaxAllowedPacket = KC_DFL_MAX_PACKET_SIZE;
	MYSQL				m_lpMySQL;
	std::recursive_mutex m_hMutexMySql;
};

} /* namespace */

#endif
