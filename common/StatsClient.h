/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __STATSCLIENT_H__
#define __STATSCLIENT_H__

#include <kopano/zcdefs.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <ctime>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>

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
	const char *name, *description;
	std::mutex lock;
};

typedef std::map<SCName, ECStat> SCMap;

struct ECStrings {
	std::string description, value;
};

class _kc_export ECStatsCollector _kc_final {
	public:
	ECStatsCollector();
	void inc(enum SCName, float inc);
	void inc(enum SCName, int inc = 1);
	void inc(enum SCName, LONGLONG inc);
	void Set(SCName name, float set);
	void Set(SCName name, LONGLONG set);
	void SetTime(SCName name, time_t set);
	void Max(SCName name, LONGLONG max);
	void Avg(SCName name, float add);
	void Avg(SCName name, LONGLONG add);

	/* strings are separate, used by ECSerial */
	std::string GetValue(const SCMap::const_iterator::value_type &);
	std::string GetValue(const SCName &name);
	void ForEachStat(void (*cb)(const std::string &, const std::string &, const std::string &, void *), void *obj);
	void Reset();
	void Reset(SCName name);

	private:
	_kc_hidden void AddStat(SCName index, SCType type, const char *name, const char *desc);

	SCMap m_StatData;
};

class _kc_export StatsClient _kc_final {
private:
	int fd = -1;
	struct sockaddr_un addr{};
	int addr_len = 0;
	bool thread_running = false;
	pthread_t countsSubmitThread{};
public:
	std::atomic<bool> terminate{false};
	std::mutex mapsLock;
	std::map<std::string, double> countsMapDouble;
	std::map<std::string, int64_t> countsMapInt64;

	~StatsClient();

	int startup(const std::string &collector);
	void countInc(const std::string & key, const std::string & key_sub);
	_kc_hidden void countAdd(const std::string &key, const std::string &key_sub, double n);
	void countAdd(const std::string & key, const std::string & key_sub, const int64_t n);
	_kc_hidden void submit(const std::string &key, time_t ts, double value);
	_kc_hidden void submit(const std::string &key, time_t ts, int64_t value);
};

} /* namespace */

#endif
