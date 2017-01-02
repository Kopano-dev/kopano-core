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

#ifndef ECDATABASE_H
#define ECDATABASE_H

#include <kopano/ECConfig.h>
#include <kopano/kcodes.h>

#include <string>

namespace KC {

typedef void *			DB_RESULT;	
typedef char **			DB_ROW;	
typedef unsigned long *	DB_LENGTHS;
typedef unsigned int	DB_ERROR;


#define DB_E_UNKNOWN			DB_ERROR(-1)
#define DB_E_LOCK_WAIT_TIMEOUT	DB_ERROR(1)
#define DB_E_LOCK_DEADLOCK		DB_ERROR(2)


// Abstract base class for databases
class ECDatabase {
protected:
	std::string error;
	bool m_bForceUpdate;

public:
	virtual ~ECDatabase(void) = default;
	virtual ECRESULT		Connect() = 0;
	virtual ECRESULT		Close() = 0;

	// Table functions
	virtual ECRESULT		DoSelect(const std::string &strQuery, DB_RESULT *lpResult, bool fStreamResult = false) = 0;
	virtual ECRESULT		DoUpdate(const std::string &strQuery, unsigned int *lpulAffectedRows = NULL) = 0;
	virtual ECRESULT		DoInsert(const std::string &strQuery, unsigned int *lpulInsertId = NULL, unsigned int *lpulAffectedRows = NULL) = 0;
	virtual ECRESULT		DoDelete(const std::string &strQuery, unsigned int *lpulAffectedRows = NULL) = 0;
	virtual	ECRESULT		DoSelectMulti(const std::string &strQuery) = 0;
	// Sequence generator - Do NOT CALL THIS FROM WITHIN A TRANSACTION.
	virtual ECRESULT		DoSequence(const std::string &strSeqName, unsigned int ulCount, unsigned long long *lpllFirstId) = 0;

	// Result functions
	virtual unsigned int	GetNumRows(DB_RESULT sResult) = 0;
	virtual unsigned int	GetNumRowFields(DB_RESULT sResult) = 0;
	virtual unsigned int	GetRowIndex(DB_RESULT sResult, const std::string &strFieldname) = 0;
	virtual ECRESULT		GetNextResult(DB_RESULT *sResult) = 0;
	virtual	ECRESULT		FinalizeMulti() = 0;

	virtual DB_ROW			FetchRow(DB_RESULT sResult) = 0;
	virtual DB_LENGTHS		FetchRowLengths(DB_RESULT sResult) = 0;

	virtual std::string		FilterBMP(const std::string &strToEscape) = 0;
	virtual std::string		Escape(const std::string &strToEscape) = 0;
	virtual std::string		EscapeBinary(unsigned char *lpData, unsigned int ulLen) = 0;
	virtual std::string		EscapeBinary(const std::string& strData) = 0;

	// Reset the result set so it can be iterated again
	virtual void			ResetResult(DB_RESULT sResult) = 0;

	virtual ECRESULT		ValidateTables() = 0;

	// Freememory functions
	virtual	void			FreeResult(DB_RESULT sResult) = 0;

	// Get error string
	virtual const char *GetError(void) = 0;
	
	// Get last error code
	virtual DB_ERROR		GetLastError() = 0;

	// Enable/disable suppression of lock errors
	virtual bool			SuppressLockErrorLogging(bool bSuppress) = 0;

	// Database functions
	virtual ECRESULT		CreateDatabase() = 0;
	virtual ECRESULT		UpdateDatabase(bool bForceUpdate, std::string &strReport) = 0;
	virtual ECRESULT		InitializeDBState() = 0;

	// Get Database path
	virtual std::string		GetDatabaseDir() = 0;
	
	// Transactions
	// These functions should be used to wrap blocks of queries into transactions. This will
	// speed up writes a lot, so try to use them as much as possible. If you don't start a transaction
	// then each INSERT or UPDATE will automatically be a single transaction, causing an fsync
	// after each write-query, which is not fast to say the least.
	
	virtual ECRESULT		Begin() = 0;
	virtual ECRESULT		Commit() = 0;
	virtual ECRESULT		Rollback() = 0;
	
	// Return the maximum size of any query we can send
	virtual unsigned int	GetMaxAllowedPacket() = 0;

	virtual void			ThreadInit() = 0;
	virtual void			ThreadEnd() = 0;
	
	virtual ECRESULT		CheckExistColumn(const std::string &strTable, const std::string &strColumn, bool *lpbExist) = 0;
	virtual ECRESULT		CheckExistIndex(const std::string &strTable, const std::string &strKey, bool *lpbExist) = 0;

	// Function requires m_bForceUpdate variable
	friend ECRESULT UpdateDatabaseConvertToUnicode(ECDatabase *lpDatabase);
};

} /* namespace */

#endif // ECDATABASE_H
