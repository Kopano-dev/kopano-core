/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <mutex>
#include <utility>
#include <pthread.h>
#include <kopano/tie.hpp>
#include "ECDatabase.h"
#include "ECDatabaseFactory.h"
#include "ECServerEntrypoint.h"

namespace KC {

// The ECDatabaseFactory creates database objects connected to the server database. Which
// database is returned is chosen by the database_engine configuration setting.

ECDatabaseFactory::ECDatabaseFactory(std::shared_ptr<ECConfig> c,
    std::shared_ptr<ECStatsCollector> s) :
	m_stats(std::move(s)), m_lpConfig(std::move(c))
{
	pthread_key_create(&m_thread_key, nullptr);
}

ECDatabaseFactory::~ECDatabaseFactory()
{
	pthread_key_delete(m_thread_key);
	for (auto &db : m_children)
		delete db;
}

ECRESULT ECDatabaseFactory::GetDatabaseFactory(ECDatabase **lppDatabase)
{
	const char *szEngine = m_lpConfig->GetSetting("database_engine");

	if(strcasecmp(szEngine, "mysql") == 0) {
		*lppDatabase = new ECDatabase(m_lpConfig, m_stats);
		(*lppDatabase)->m_filter_bmp = m_filter_bmp;
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

ECRESULT ECDatabaseFactory::get_tls_db(ECDatabase **lppDatabase)
{
	std::string error;

	// We check to see whether the calling thread already
	// has an open database connection. If so, we return that, or otherwise
	// we create a new one.
	auto db = static_cast<ECDatabase *>(pthread_getspecific(m_thread_key));
	if (db != nullptr) {
		*lppDatabase = db;
		return erSuccess;
	}

	std::unique_ptr<ECDatabase> updb;
	auto er = CreateDatabaseObject(&unique_tie(updb), error);
	if(er != erSuccess) {
		ec_log_err("Unable to get database connection: %s", error.c_str());
		return er;
	}
	std::unique_lock<std::mutex> lk(m_child_mtx);
	m_children.emplace(updb.get());
	pthread_setspecific(m_thread_key, updb.get());
	*lppDatabase = updb.release();
	return erSuccess;
}

void ECDatabaseFactory::thread_end()
{
	auto db = static_cast<ECDatabase *>(pthread_getspecific(m_thread_key));
	if (db == nullptr)
		return;
	db->ThreadEnd();
	std::unique_lock<std::mutex> lk(m_child_mtx);
	auto i = m_children.find(db);
	if (i != m_children.cend())
		m_children.erase(i);
}

void ECDatabaseFactory::filter_bmp(bool y)
{
	m_filter_bmp = y;
	std::unique_lock<std::mutex> lk(m_child_mtx);
	for (auto &db : m_children)
		db->m_filter_bmp = y;
}

} /* namespace */
