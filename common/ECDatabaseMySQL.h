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

#include <kopano/platform.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/kcodes.h>

#include <pthread.h>
#include <mysql.h>
#include <string>

using namespace std;

typedef void *			DB_RESULT;	
typedef char **			DB_ROW;	
typedef unsigned long *	DB_LENGTHS;

// The max length of a group_concat function
#define MAX_GROUP_CONCAT_LEN		32768

typedef struct _sDatabase {
	const char *lpComment;
	const char *lpSQL;
} sSQLDatabase_t;

class ECDatabaseMySQL
{
public:
	ECDatabaseMySQL(ECLogger *lpLogger);
	virtual ~ECDatabaseMySQL();

	ECRESULT		Connect(ECConfig *lpConfig);
	ECRESULT		Close();
	ECRESULT		DoSelect(const std::string &strQuery, DB_RESULT *lpResult, bool bStream = false);
	ECRESULT		DoUpdate(const std::string &strQuery, unsigned int *lpulAffectedRows = NULL);
	ECRESULT		DoInsert(const std::string &strQuery, unsigned int *lpulInsertId = NULL, unsigned int *lpulAffectedRows = NULL);
	ECRESULT		DoDelete(const std::string &strQuery, unsigned int *lpulAffectedRows = NULL);
		ECRESULT		DoSequence(const std::string &strSeqName, unsigned int ulCount, uint64_t *lpllFirstId);
	
	virtual const sSQLDatabase_t *GetDatabaseDefs(void) = 0;

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

	ECLogger*		GetLogger();

	// Freememory method(s)
	void			FreeResult(DB_RESULT sResult);

private:
	ECRESULT InitEngine();
	ECRESULT IsInnoDBSupported();

	ECRESULT _Update(const string &strQuery, unsigned int *lpulAffectedRows);
	int Query(const string &strQuery);
	unsigned int GetAffectedRows();
	unsigned int GetInsertId();

	// Datalocking methods
	bool Lock();
	bool UnLock();

	// Connection methods
	bool isConnected();

private:
	bool				m_bMysqlInitialize;
	bool				m_bConnected;
	MYSQL				m_lpMySQL;
	pthread_mutex_t		m_hMutexMySql;
	bool				m_bAutoLock;
	unsigned int 		m_ulMaxAllowedPacket;
	bool				m_bLocked;
	ECLogger*			m_lpLogger;
};

#endif
