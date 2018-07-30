/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef EC_STATS_TABLES_H
#define EC_STATS_TABLES_H

#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include "ECGenericObjectTable.h"
#include "ECSession.h"
#include <string>
#include <list>
#include <map>

namespace KC {

struct statstrings {
	std::string name;
	std::string description;
	std::string value;
};

class ECSystemStatsTable _kc_final : public ECGenericObjectTable {
protected:
	ECSystemStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *, unsigned int flags, const ECLocale &, ECGenericObjectTable **);
	virtual ECRESULT Load();
	void load_tcmalloc(void);
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
	std::string srcaddress;
	unsigned int port;
	unsigned int idletime;
	unsigned int capability;
	bool locked;
	int peerpid;
	std::string username;
	std::list<BUSYSTATE> busystates;
	double dblUser, dblSystem, dblReal;
	std::string version;
	std::string clientapp;
	unsigned int requests;
	std::string url;
	std::string proxyhost;
	std::string client_application_version, client_application_misc;
};

class ECSessionStatsTable _kc_final : public ECGenericObjectTable {
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

class ECUserStatsTable _kc_final : public ECGenericObjectTable {
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

class ECCompanyStatsTable _kc_final : public ECGenericObjectTable {
protected:
	ECCompanyStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *, unsigned int flags, const ECLocale &, ECGenericObjectTable **);
	virtual ECRESULT Load();
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit);
	ALLOC_WRAP_FRIEND;
};

class ECServerStatsTable _kc_final : public ECGenericObjectTable {
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

// Link to provider/server
extern _kc_export void (*kopano_get_server_stats)(unsigned int *qlen, double *qage, unsigned int *nthr, unsigned int *nidlethr);

} /* namespace */

#endif
