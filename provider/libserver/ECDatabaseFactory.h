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
	struct dfpair {
		ECDatabaseFactory *factory;
		std::shared_ptr<ECDatabase> db;
		bool operator==(const dfpair &o) const { return db == o.db; }
	};

	ECDatabaseFactory(std::shared_ptr<ECConfig>, std::shared_ptr<ECStatsCollector>);
	~ECDatabaseFactory();
	static void S_thread_end(void *);
	ECRESULT		CreateDatabaseObject(ECDatabase **lppDatabase, std::string &ConnectError);
	ECRESULT		CreateDatabase();
	ECRESULT		UpdateDatabase(bool bForceUpdate, std::string &strError);
	ECRESULT get_tls_db(ECDatabase **);
	void filter_bmp(bool);

	std::shared_ptr<ECStatsCollector> m_stats;

private:
	struct dfhash {
		size_t operator()(const dfpair &a) const { return reinterpret_cast<size_t>(a.db.get()); }
	};

	_kc_hidden void destroy_database(ECDatabase *);
	_kc_hidden ECRESULT GetDatabaseFactory(ECDatabase **);

	std::shared_ptr<ECConfig> m_lpConfig;
	pthread_key_t m_thread_key;
	std::unordered_set<dfpair, dfhash> m_children;
	std::mutex m_child_mtx;
	bool m_filter_bmp = false;
};

} /* namespace */

#endif // ECDATABASEFACTORY_H
