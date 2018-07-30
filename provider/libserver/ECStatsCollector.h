/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef EC_STATS_COLLECTOR_H
#define EC_STATS_COLLECTOR_H

#include <kopano/zcdefs.h>
#include <string>
#include <map>
#include <mutex>

namespace KC {

enum SCName {
	/* server stats */
	SCN_SERVER_STARTTIME, SCN_SERVER_LAST_CACHECLEARED, SCN_SERVER_LAST_CONFIGRELOAD,
	SCN_SERVER_CONNECTIONS, SCN_MAX_SOCKET_NUMBER, SCN_REDIRECT_COUNT, SCN_SOAP_REQUESTS, SCN_RESPONSE_TIME, SCN_PROCESSING_TIME,
	/* search folder stats */
	SCN_SEARCHFOLDER_COUNT, SCN_SEARCHFOLDER_THREADS, SCN_SEARCHFOLDER_UPDATE_RETRY, SCN_SEARCHFOLDER_UPDATE_FAIL,
	/* database stats */
	SCN_DATABASE_CONNECTS, SCN_DATABASE_SELECTS, SCN_DATABASE_INSERTS, SCN_DATABASE_UPDATES, SCN_DATABASE_DELETES,
	SCN_DATABASE_FAILED_CONNECTS, SCN_DATABASE_FAILED_SELECTS, SCN_DATABASE_FAILED_INSERTS, SCN_DATABASE_FAILED_UPDATES, SCN_DATABASE_FAILED_DELETES, SCN_DATABASE_LAST_FAILED,
	SCN_DATABASE_MWOPS, SCN_DATABASE_MROPS, SCN_DATABASE_DEFERRED_FETCHES, SCN_DATABASE_MERGES, SCN_DATABASE_MERGED_RECORDS, SCN_DATABASE_ROW_READS, SCN_DATABASE_COUNTER_RESYNCS,
	/* logon stats */
	SCN_LOGIN_PASSWORD, SCN_LOGIN_SSL, SCN_LOGIN_SSO, SCN_LOGIN_SOCKET, SCN_LOGIN_DENIED,
	/* system session stats */
	SCN_SESSIONS_CREATED, SCN_SESSIONS_DELETED, SCN_SESSIONS_TIMEOUT, SCN_SESSIONS_INTERNAL_CREATED, SCN_SESSIONS_INTERNAL_DELETED,
	/* system session group stats */
	SCN_SESSIONGROUPS_CREATED, SCN_SESSIONGROUPS_DELETED,
	/* LDAP stats */
	SCN_LDAP_CONNECTS, SCN_LDAP_RECONNECTS, SCN_LDAP_CONNECT_FAILED, SCN_LDAP_CONNECT_TIME, SCN_LDAP_CONNECT_TIME_MAX,
	SCN_LDAP_AUTH_LOGINS, SCN_LDAP_AUTH_DENIED, SCN_LDAP_AUTH_TIME, SCN_LDAP_AUTH_TIME_MAX, SCN_LDAP_AUTH_TIME_AVG,
	SCN_LDAP_SEARCH, SCN_LDAP_SEARCH_FAILED, SCN_LDAP_SEARCH_TIME, SCN_LDAP_SEARCH_TIME_MAX,
	/* indexer stats */
	SCN_INDEXER_SEARCH_ERRORS, SCN_INDEXER_SEARCH_MAX, SCN_INDEXER_SEARCH_AVG, SCN_INDEXED_SEARCHES, SCN_DATABASE_SEARCHES
};

union SCData {
	float f;
	LONGLONG ll;
	time_t ts;
};

enum SCType { SCDT_FLOAT, SCDT_LONGLONG, SCDT_TIMESTAMP };

struct ECStat {
	SCData data;
	LONGLONG avginc;
	SCType type;
	const char *name;
	const char *description;
	std::mutex lock;
};

typedef std::map<SCName, ECStat> SCMap;

struct ECStrings {
	std::string description;
	std::string value;
};

class _kc_export ECStatsCollector _kc_final {
public:
	_kc_hidden ECStatsCollector(void);
	_kc_hidden void Increment(SCName name, float inc);
	void Increment(SCName name, int inc = 1);
	void Increment(SCName name, LONGLONG inc);
	_kc_hidden void Set(SCName name, float set);
	_kc_hidden void Set(SCName name, LONGLONG set);
	void SetTime(SCName name, time_t set);
	void Max(SCName name, LONGLONG max);
	_kc_hidden void Avg(SCName name, float add);
	void Avg(SCName name, LONGLONG add);

	/* strings are separate, used by ECSerial */
	_kc_hidden std::string GetValue(const SCMap::const_iterator::value_type &);
	_kc_hidden std::string GetValue(const SCName &name);
	_kc_hidden void ForEachStat(void (*cb)(const std::string &, const std::string &, const std::string &, void *), void *obj);
	_kc_hidden void ForEachString(void (*cb)(const std::string &, const std::string &, const std::string &, void *), void *obj);
	_kc_hidden void Reset(void);
	_kc_hidden void Reset(SCName name);

private:
	_kc_hidden void AddStat(SCName index, SCType type, const char *name, const char *desc);

	SCMap m_StatData;
	std::mutex m_StringsLock;
	std::map<std::string, ECStrings> m_StatStrings;
};

/* actual variable is in ECServerEntryPoint.cpp */
extern ECStatsCollector* g_lpStatsCollector;

} /* namespace */

#endif
