/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECDATABASEFACTORY_H
#define ECDATABASEFACTORY_H

#include <memory>
#include <kopano/zcdefs.h>
#include "ECDatabase.h"
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>

namespace KC {

class ECStatsCollector;

// The ECDatabaseFactory creates database objects connected to the server database. Which
// database is returned is chosen by the database_engine configuration setting.

class _kc_export ECDatabaseFactory final {
public:
	ECDatabaseFactory(std::shared_ptr<ECConfig>, std::shared_ptr<ECStatsCollector>);
	ECRESULT		CreateDatabaseObject(ECDatabase **lppDatabase, std::string &ConnectError);
	ECRESULT		CreateDatabase();
	ECRESULT		UpdateDatabase(bool bForceUpdate, std::string &strError);
	ECRESULT get_tls_db(ECDatabase **);

	std::shared_ptr<ECStatsCollector> m_stats;

private:
	_kc_hidden ECRESULT GetDatabaseFactory(ECDatabase **);

	std::shared_ptr<ECConfig> m_lpConfig;
};

} /* namespace */

#endif // ECDATABASEFACTORY_H
