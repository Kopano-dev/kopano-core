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

#ifndef EC_STATS_TABLES_H
#define EC_STATS_TABLES_H

#include <kopano/zcdefs.h>
#include "ECGenericObjectTable.h"
#include "ECSession.h"

#include <string>
#include <list>
#include <map>

typedef struct _statstrings {
	std::string name;
	std::string description;
	std::string value;
} statstrings;

class ECSystemStatsTable _kc_final : public ECGenericObjectTable {
protected:
	ECSystemStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECSystemStatsTable **lppTable);

	virtual ECRESULT Load();
	void load_tcmalloc(void);
	static ECRESULT QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit);

private:
	static void GetStatsCollectorData(const std::string &name, const std::string &description, const std::string &value, void *obj);

	std::map<unsigned int, statstrings> m_mapStatData;
	unsigned int id;
};


typedef struct _sessiondata {
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
} sessiondata;

class ECSessionStatsTable _kc_final : public ECGenericObjectTable {
protected:
	ECSessionStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECSessionStatsTable **lppTable);

	virtual ECRESULT Load();

	static ECRESULT QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit);

private:
	static void GetSessionData(ECSession *lpSession, void *obj);

	std::map<unsigned int, sessiondata> m_mapSessionData;
	unsigned int id;
};

class ECUserStatsTable _kc_final : public ECGenericObjectTable {
protected:
	ECUserStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECUserStatsTable **lppTable);

	virtual ECRESULT Load();

	static ECRESULT QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit);

private:
	ECRESULT LoadCompanyUsers(ULONG ulCompanyId);
};

class ECCompanyStatsTable _kc_final : public ECGenericObjectTable {
protected:
	ECCompanyStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECCompanyStatsTable **lppTable);

	virtual ECRESULT Load();

	static ECRESULT QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit);

private:
};

class ECServerStatsTable _kc_final : public ECGenericObjectTable {
protected:
	ECServerStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECServerStatsTable **lppTable);

	virtual ECRESULT Load();

	static ECRESULT QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit);

private:
	std::map<unsigned int, std::string> m_mapServers;
};

// Link to provider/server
void kopano_get_server_stats(unsigned int *lpulQueueLen, double *lpDblQueueAge, unsigned int *lpulThreads, unsigned int *lpulIdleThreads);

#endif
