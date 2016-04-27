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

#ifndef IECSTATSCOLLECTOR_H
#define IECSTATSCOLLECTOR_H

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


class IECStatsCollector {
public:
	virtual ~IECStatsCollector() {};

	virtual void Increment(SCName name, float inc) = 0;
	virtual void Increment(SCName name, int inc = 1) = 0;
	virtual void Increment(SCName name, LONGLONG inc) = 0;

	virtual void Set(SCName name, float set) = 0;
	virtual void Set(SCName name, LONGLONG set) = 0;
	virtual void SetTime(SCName name, time_t set) = 0;

	virtual void Min(SCName name, float min) = 0;
	virtual void Min(SCName name, LONGLONG min) = 0;
	virtual void MinTime(SCName name, time_t min) = 0;

	virtual void Max(SCName name, float max) = 0;
	virtual void Max(SCName name, LONGLONG max) = 0;
	virtual void MaxTime(SCName name, time_t max) = 0;

	virtual void Avg(SCName name, float add) = 0;
	virtual void Avg(SCName name, LONGLONG add) = 0;
	virtual void AvgTime(SCName name, time_t add) = 0;

	virtual void Set(const std::string &name, const std::string &description, const std::string &value) = 0;
	virtual void Remove(const std::string &name) = 0;

	virtual std::string GetValue(SCName name) = 0;
	// These functions are not in the interface
//	virtual std::string GetValue(SCMap::iterator iSD) = 0;

//	virtual void ForEachStat(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj) = 0;
//	virtual void ForEachString(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj) = 0;
	
	virtual void Reset() = 0;
	virtual void Reset(SCName name) = 0;
};

#endif
