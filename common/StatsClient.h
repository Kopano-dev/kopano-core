/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __STATSCLIENT_H__
#define __STATSCLIENT_H__

#include <kopano/zcdefs.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <pthread.h>

namespace KC {

class ECConfig;

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
	SCN_INDEXER_SEARCH_ERRORS, SCN_INDEXER_SEARCH_MAX, SCN_INDEXER_SEARCH_AVG, SCN_INDEXED_SEARCHES, SCN_DATABASE_SEARCHES,

	SCN_DAGENT_ATTACHMENT_COUNT,
	SCN_DAGENT_AUTOACCEPT,
	SCN_DAGENT_AUTOPROCESS,
	SCN_DAGENT_DELIVER_INBOX,
	SCN_DAGENT_DELIVER_JUNK,
	SCN_DAGENT_DELIVER_PUBLIC,
	SCN_DAGENT_FALLBACKDELIVERY,
	SCN_DAGENT_INCOMING_SESSION,
	SCN_DAGENT_IS_HAM,
	SCN_DAGENT_IS_SPAM,
	SCN_DAGENT_MAX_THREAD_COUNT,
	SCN_DAGENT_MSG_EXPIRED,
	SCN_DAGENT_MSG_NOT_EXPIRED,
	SCN_DAGENT_NWITHATTACHMENT,
	SCN_DAGENT_OUTOFOFFICE,
	SCN_DAGENT_RECIPS,
	SCN_DAGENT_STDIN_RECEIVED,
	SCN_DAGENT_STRINGTOMAPI,
	SCN_DAGENT_TO_COMPANY,
	SCN_DAGENT_TO_LIST,
	SCN_DAGENT_TO_SERVER,
	SCN_DAGENT_TO_SINGLE_RECIP,
	SCN_LMTP_BAD_RECIP_ADDR,
	SCN_LMTP_BAD_SENDER_ADDRESS,
	SCN_LMTP_INTERNAL_ERROR,
	SCN_LMTP_LHLO_FAIL,
	SCN_LMTP_NO_RECIP,
	SCN_LMTP_RECEIVED,
	SCN_LMTP_SESSIONS,
	SCN_LMTP_TMPFILEFAIL,
	SCN_LMTP_UNKNOWN_COMMAND,
	SCN_RULES_BOUNCE,
	SCN_RULES_COPYMOVE,
	SCN_RULES_DEFER,
	SCN_RULES_DELEGATE,
	SCN_RULES_DELETE,
	SCN_RULES_FORWARD,
	SCN_RULES_INVOKES,
	SCN_RULES_INVOKES_FAIL,
	SCN_RULES_MARKREAD,
	SCN_RULES_NACTIONS,
	SCN_RULES_NRULES,
	SCN_RULES_REPLY_AND_OOF,
	SCN_RULES_TAG,
	SCN_SPOOLER_ABNORM_TERM,
	SCN_SPOOLER_BATCH_COUNT,
	SCN_SPOOLER_BATCH_INVOKES,
	SCN_SPOOLER_EXIT_WAIT,
	SCN_SPOOLER_SEND_FAILED,
	SCN_SPOOLER_SENT,
	SCN_SPOOLER_SIGKILLED,
	SCN_MACHINE_ID, SCN_UTSNAME, SCN_OSRELEASE,
	SCN_SERVER_GUID,
};

union SCData {
	double f;
	LONGLONG ll;
	time_t ts;
};

enum SCType {
	SCT_INTEGER,
	SCT_INTGAUGE,
	SCT_REAL,
	SCT_REALGAUGE,
	SCT_TIME,
	SCT_STRING,
};

struct ECStat {
	const char *name, *description;
	SCData data;
	LONGLONG avginc;
	SCType type;
	std::mutex lock;
	std::string strdata;
};

struct ECStat2 {
	std::string desc, strdata;
	SCType type;
	SCData data;
};

typedef std::map<SCName, ECStat> SCMap;

class _kc_export ECStatsCollector {
	public:
	ECStatsCollector(std::shared_ptr<ECConfig>);
	~ECStatsCollector();
	virtual void fill_odm() {}
	void start();
	void stop();
	void mainloop();
	void submit(std::string &&url, std::string &&data, bool sslverify = true);
	void inc(enum SCName, double inc);
	void inc(enum SCName, int inc = 1);
	void inc(enum SCName, LONGLONG inc);
	void set_dbl(enum SCName, double set);
	void setg_dbl(const std::string &, const std::string &, double);
	void set(const std::string &, const std::string &, int64_t);
	void setg(const std::string &, const std::string &, int64_t);
	void set(enum SCName, LONGLONG set);
	void SetTime(SCName name, time_t set);
	void set(SCName, const std::string &);
	void set(const std::string &, const std::string &, const std::string &);
	void Max(SCName name, LONGLONG max);
	void avg_dbl(enum SCName, double add);
	void avg(enum SCName, LONGLONG add);

	/* strings are separate, used by ECSerial */
	std::string GetValue(const SCMap::const_iterator::value_type &);
	std::string GetValue(const SCName &name);
	void ForEachStat(void (*cb)(const std::string &, const std::string &, const std::string &, void *), void *obj);
	void Reset();
	void Reset(SCName name);

	protected:
	/*
	 * The "name" parameter may not be longer than 19 characters, since we
	 * want to use those in RRDtool.
	 */
	void AddStat(enum SCName index, SCType type, const char *name, const char *desc = "");
	std::string GetValue(const ECStat2 &);

	private:
	std::string stats_as_text();
	std::string survey_as_text();

	SCMap m_StatData;
	std::unordered_map<std::string, ECStat2> m_ondemand;
	bool thread_running = false;
	std::atomic<bool> terminate{false};
	pthread_t countsSubmitThread{};
	std::shared_ptr<ECConfig> m_config;
	std::condition_variable m_exitsig;
	std::mutex m_odm_lock;
};

typedef ECStatsCollector StatsClient;

} /* namespace */

#endif
