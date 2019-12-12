/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECDATABASEFACTORY_H
#define ECDATABASEFACTORY_H

#include <memory>
#include <mutex>
#include <unordered_set>
#include <pthread.h>
#include <kopano/zcdefs.h>
#include "ECDatabase.h"
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>

namespace KC {

class ECStatsCollector;

// The ECDatabaseFactory creates database objects connected to the server database. Which
// database is returned is chosen by the database_engine configuration setting.

class KC_EXPORT ECDatabaseFactory final {
public:
	ECDatabaseFactory(std::shared_ptr<ECConfig>, std::shared_ptr<ECStatsCollector>);
	~ECDatabaseFactory();
	ECRESULT		CreateDatabaseObject(ECDatabase **lppDatabase, std::string &ConnectError);
	ECRESULT		CreateDatabase();
	ECRESULT		UpdateDatabase(bool bForceUpdate, std::string &strError);
	ECRESULT get_tls_db(ECDatabase **);
	void thread_end();
	void filter_bmp(bool);

	std::shared_ptr<ECStatsCollector> m_stats;

private:
	KC_HIDDEN ECRESULT GetDatabaseFactory(ECDatabase **);

	std::shared_ptr<ECConfig> m_lpConfig;
	pthread_key_t m_thread_key;
	std::unordered_set<ECDatabase *> m_children;
	std::mutex m_child_mtx;
	bool m_filter_bmp = false;
};

} /* namespace */

#endif // ECDATABASEFACTORY_H
