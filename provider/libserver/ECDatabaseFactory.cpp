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
#include <memory>
#include <kopano/tie.hpp>
#include "ECDatabase.h"
#include "ECDatabaseFactory.h"

#include "ECServerEntrypoint.h"

namespace KC {

// The ECDatabaseFactory creates database objects connected to the server database. Which
// database is returned is chosen by the database_engine configuration setting.

ECDatabaseFactory::ECDatabaseFactory(ECConfig *lpConfig) :
	m_lpConfig(lpConfig)
{}

ECRESULT ECDatabaseFactory::GetDatabaseFactory(ECDatabase **lppDatabase)
{
	const char *szEngine = m_lpConfig->GetSetting("database_engine");

	if(strcasecmp(szEngine, "mysql") == 0) {
		*lppDatabase = new ECDatabase(m_lpConfig);
	} else {
		ec_log_crit("ECDatabaseFactory::GetDatabaseFactory(): database not mysql");
		return KCERR_DATABASE_ERROR;
	}
	return erSuccess;
}

ECRESULT ECDatabaseFactory::CreateDatabaseObject(ECDatabase **lppDatabase, std::string &ConnectError)
{
	std::unique_ptr<ECDatabase> lpDatabase;
	auto er = GetDatabaseFactory(&unique_tie(lpDatabase));
	if(er != erSuccess) {
		ConnectError = "Invalid database engine";
		return er;
	}

	er = lpDatabase->Connect();
	if(er != erSuccess) {
		ConnectError = lpDatabase->GetError();
		return er;
	}
	*lppDatabase = lpDatabase.release();
	return erSuccess;
}

ECRESULT ECDatabaseFactory::CreateDatabase()
{
	std::unique_ptr<ECDatabase> lpDatabase;
	auto er = GetDatabaseFactory(&unique_tie(lpDatabase));
	if(er != erSuccess)
		return er;
	return lpDatabase->CreateDatabase();
}

ECRESULT ECDatabaseFactory::UpdateDatabase(bool bForceUpdate, std::string &strReport)
{
	std::unique_ptr<ECDatabase> lpDatabase;
	auto er = CreateDatabaseObject(&unique_tie(lpDatabase), strReport);
	if(er != erSuccess)
		return er;
	return lpDatabase->UpdateDatabase(bForceUpdate, strReport);
}

extern pthread_key_t database_key;

ECRESULT GetThreadLocalDatabase(ECDatabaseFactory *lpFactory, ECDatabase **lppDatabase)
{
	std::string error;

	// We check to see whether the calling thread already
	// has an open database connection. If so, we return that, or otherwise
	// we create a new one.

	// database_key is defined in ECServer.cpp, and allocated in the running_server routine
	auto lpDatabase = static_cast<ECDatabase *>(pthread_getspecific(database_key));
	
	if(lpDatabase == NULL) {
		auto er = lpFactory->CreateDatabaseObject(&lpDatabase, error);
		if(er != erSuccess) {
			ec_log_err("Unable to get database connection: %s", error.c_str());
			lpDatabase = NULL;
			return er;
		}
		
		// Add database into a list, for close all database connections
		AddDatabaseObject(lpDatabase);
		pthread_setspecific(database_key, lpDatabase);
	}

	*lppDatabase = lpDatabase;
	return erSuccess;
}

} /* namespace */
