/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef EC_STATS_TABLES_H
#define EC_STATS_TABLES_H

#include <sys/types.h>
#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include "ECGenericObjectTable.h"
#include "ECSession.h"
#include <string>
#include <list>
#include <map>

namespace KC {

struct statstrings {
	std::string name, description, value;
};

class ECSystemStatsTable final : public ECGenericObjectTable {
protected:
	ECSystemStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *, unsigned int flags, const ECLocale &, ECGenericObjectTable **);
	virtual ECRESULT Load();
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit);

private:
	static void GetStatsCollectorData(const std::string &name, const std::string &description, const std::string &value, void *obj);

	std::map<unsigned int, statstrings> m_mapStatData;
	unsigned int id;
	ALLOC_WRAP_FRIEND;
};

struct sessiondata {
	ECSESSIONID sessionid;
	ECSESSIONGROUPID sessiongroupid;
	unsigned int idletime, capability, requests;
	bool locked;
	pid_t peerpid = 0;
	std::list<BUSYSTATE> busystates;
	double dblUser, dblSystem, dblReal;
	std::string srcaddress, username, version, clientapp, proxyhost;
	std::string client_application_version, client_application_misc;
};

class ECSessionStatsTable final : public ECGenericObjectTable {
protected:
	ECSessionStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *, unsigned int flags, const ECLocale &, ECGenericObjectTable **);
	virtual ECRESULT Load();
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit);

private:
	static void GetSessionData(ECSession *lpSession, void *obj);

	std::map<unsigned int, sessiondata> m_mapSessionData;
	unsigned int id;
	ALLOC_WRAP_FRIEND;
};

class ECUserStatsTable final : public ECGenericObjectTable {
protected:
	ECUserStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *, unsigned int flags, const ECLocale &, ECGenericObjectTable **);
	virtual ECRESULT Load();
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit);

private:
	ECRESULT LoadCompanyUsers(ULONG ulCompanyId);
	ALLOC_WRAP_FRIEND;
};

class ECCompanyStatsTable final : public ECGenericObjectTable {
protected:
	ECCompanyStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *, unsigned int flags, const ECLocale &, ECGenericObjectTable **);
	virtual ECRESULT Load();
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit);
	ALLOC_WRAP_FRIEND;
};

class ECServerStatsTable final : public ECGenericObjectTable {
protected:
	ECServerStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *, unsigned int flags, const ECLocale &, ECGenericObjectTable **);
	virtual ECRESULT Load();
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit);

private:
	std::map<unsigned int, std::string> m_mapServers;
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif
