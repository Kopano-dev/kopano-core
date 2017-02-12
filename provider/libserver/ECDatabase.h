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

#include <kopano/zcdefs.h>
#include <kopano/ECConfig.h>
#include <kopano/database.hpp>
#include <kopano/kcodes.h>

#include <string>

namespace KC {

// Abstract base class for databases
class ECDatabase : public KDatabase {
protected:
	std::string error;
	bool m_bForceUpdate;

public:
	virtual ECRESULT		Connect() = 0;

	// Table functions
	virtual	ECRESULT		DoSelectMulti(const std::string &strQuery) = 0;

	// Result functions
	virtual ECRESULT		GetNextResult(DB_RESULT *sResult) = 0;
	virtual	ECRESULT		FinalizeMulti() = 0;

	virtual std::string		FilterBMP(const std::string &strToEscape) = 0;
	virtual ECRESULT		ValidateTables() = 0;

	// Enable/disable suppression of lock errors
	virtual bool			SuppressLockErrorLogging(bool bSuppress) = 0;

	// Database functions
	virtual ECRESULT		CreateDatabase() = 0;
	virtual ECRESULT		UpdateDatabase(bool bForceUpdate, std::string &strReport) = 0;
	virtual ECRESULT		InitializeDBState() = 0;
	
	virtual void			ThreadInit() = 0;
	virtual void			ThreadEnd() = 0;
	
	virtual ECRESULT		CheckExistColumn(const std::string &strTable, const std::string &strColumn, bool *lpbExist) = 0;
	virtual ECRESULT		CheckExistIndex(const std::string &strTable, const std::string &strKey, bool *lpbExist) = 0;

	// Function requires m_bForceUpdate variable
	friend ECRESULT UpdateDatabaseConvertToUnicode(ECDatabase *lpDatabase);
};

} /* namespace */

#endif // ECDATABASE_H
