/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include <algorithm>
#include <atomic>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <cerrno>
#include <cstring>
#include <getopt.h>
#include <sys/resource.h>
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <kopano/ECChannel.h>
#include <kopano/ecversion.h>
#include <kopano/MAPIErrors.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include <kopano/scope.hpp>
#include "soapH.h"
#include "ECDatabase.h"
#include "ECDatabaseFactory.h"
#include "ECDatabaseUtils.h"
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include "ECPluginFactory.h"
#include "ECNotificationManager.h"
#include "ECSessionManager.h"
#include "StatsClient.h"
#include "ECStatsTables.h"
#include <climits>
#include <csignal>
#include <kopano/UnixUtil.h>
#include <pwd.h>
#include <sys/stat.h>
#include <kopano/ECScheduler.h>
#include <kopano/kcodes.h>
#include <kopano/tie.hpp>
#include "charset/localeutil.h"
#include "cmd.hpp"
#include "ECServerEntrypoint.h"
#include "SSLUtil.h"
#include "ECSoapServerConnection.h"
#include <libintl.h>
#include <unicode/uclean.h>
#include <kopano/fileutil.hpp>
#include "ECICS.h"
#include <openssl/ssl.h>
#ifdef HAVE_KCOIDC_H
#include <kcoidc.h>
#endif
#ifdef HAVE_KUSTOMER
#include <kustomer.h>
#include <kustomer_errors.h>
#endif

// The following value is based on:
// http://dev.mysql.com/doc/refman/5.0/en/server-system-variables.html#sysvar_thread_stack
// Since the remote MySQL server can be 32- or 64-bit, we'll just go with the value specified
// for 64-bit architectures.
// We could use the 'version_compile_machine' variable, but I'm not sure if 32-bit versions
// will ever be build on 64-bit machines and what that variable does. Plus we would need a
// list of all possible 32-bit architectures because if the architecture is unknown we'll
// have to go with the safe value which is for 64-bit.
#define MYSQL_MIN_THREAD_STACK (256*1024)

using namespace KC;
using namespace std::string_literals;
using std::cout;
using std::endl;

static const char upgrade_lock_file[] = "/tmp/kopano-upgrade-lock";
static const char default_atx_backend[] = "files_v2"; /* for new installs */
static int g_Quit = 0;
std::atomic<bool> sv_sighup_flag{false};
static int restart_searches = 0;
static bool m_bIgnoreDatabaseVersionConflict = false;
static bool m_bIgnoreAttachmentStorageConflict = false;
static bool m_bForceDatabaseUpdate = false;
static bool m_bIgnoreUnknownConfigOptions = false;
static bool m_bIgnoreDbThreadStackSize = false;
std::shared_ptr<ECConfig> g_lpConfig;
static bool g_listen_http, g_listen_https, g_listen_pipe;
static std::shared_ptr<ECLogger> g_lpAudit;
static std::vector<int> g_used_fds;
static std::unique_ptr<ECSoapServerConnection> g_lpSoapServerConn;
static bool m_bDatabaseUpdateIgnoreSignals = false;
static bool g_dump_config;

static int running_server(char *, const char *, bool, int, char **, int, char **);

#ifdef HAVE_KCOIDC_H
static void kcoidc_log_debug_cb(char *s)
{
	ec_log_debug("%s", s);
	free(s); // Release memory allocated in the C heap by cgo with malloc.
}
#endif

#ifdef HAVE_KUSTOMER
static constexpr char kustomerProductName[] = "groupware";
static constexpr char kustomerProductUserAgent[] = "Kopano Storage Server/" PROJECT_VERSION;

static void kustomer_log_info_cb(char *s)
{
	ec_log_info("%s", s);
	free(s); // Release memory allocated in the C heap by cgo with malloc.
}

static int kustomer_log_ensured_claims()
{
	auto transaction = kustomer_begin_ensure();
	if (transaction.r0 != KUSTOMER_ERRSTATUSSUCCESS) {
		ec_log_err("KUSTOMER failed to start transaction: 0x%llx, %s", transaction.r0, kustomer_err_numeric_text(transaction.r0));
		return transaction.r0;
	}
	auto ensured = kustomer_dump_ensure(transaction.r1);
	if (ensured.r0 != KUSTOMER_ERRSTATUSSUCCESS) {
		ec_log_err("KUSTOMER failed to dump ensured claims: 0x%llx, %s", ensured.r0, kustomer_err_numeric_text(ensured.r0));
		return ensured.r0;
	}
	ec_log_always("KUSTOMER ensured claims: %s", ensured.r1);
	free(ensured.r1); // Release memory allocated in the C heap by cgo with malloc.
	auto res = kustomer_ensure_ok(transaction.r1, const_cast<char *>(kustomerProductName));
	if (res != KUSTOMER_ERRSTATUSSUCCESS) {
		ec_log_always("KUSTOMER was unable to find a '%s' license: 0x%llx, %s", kustomerProductName, res, kustomer_err_numeric_text(res));
	}
	res = kustomer_end_ensure(transaction.r1);
	if (res != KUSTOMER_ERRSTATUSSUCCESS) {
		ec_log_err("KUSTOMER failed to end transaction: 0x%llx, %s", res, kustomer_err_numeric_text(res));
		return res;
	}

	return KUSTOMER_ERRSTATUSSUCCESS;
}

static void kustomer_notify_update_cb()
{
	ec_log_info("KUSTOMER ensured claim notify received");
	kustomer_log_ensured_claims();
}

static void kustomer_notify_exit_cb()
{
	ec_log_info("KUSTOMER ensured claim notify watch exit");
}
#endif

server_stats::server_stats(std::shared_ptr<ECConfig> cfg) :
	ECStatsCollector(cfg)
{
	set(SCN_PROGRAM_NAME, "kopano-server");
	AddStat(SCN_SERVER_GUID, SCT_STRING, "server_guid");
	AddStat(SCN_SERVER_STARTTIME, SCT_TIME, "server_start_date", "Time when the server was started");
	AddStat(SCN_SERVER_LAST_CACHECLEARED, SCT_TIME, "cache_purge_date", "Time when the cache was cleared");
	AddStat(SCN_SERVER_LAST_CONFIGRELOAD, SCT_TIME, "config_reload_date", "Time when the configuration file was reloaded / logrotation (SIGHUP)");
	AddStat(SCN_SERVER_CONNECTIONS, SCT_INTEGER, "connections", "Number of handled incoming connections");
	AddStat(SCN_MAX_SOCKET_NUMBER, SCT_INTGAUGE, "max_socket", "Highest socket number used");
	AddStat(SCN_REDIRECT_COUNT, SCT_INTEGER, "redirections", "Number of redirected requests");
	AddStat(SCN_SEARCHFOLDER_COUNT, SCT_INTGAUGE, "searchfld_loaded", "Total number of searchfolders");
	AddStat(SCN_SEARCHFOLDER_THREADS, SCT_INTGAUGE, "searchfld_threads", "Current number of running searchfolder threads");
	AddStat(SCN_SEARCHFOLDER_UPDATE_RETRY, SCT_INTEGER, "searchupd_retry", "The number of times a search folder update was restarted");
	AddStat(SCN_SEARCHFOLDER_UPDATE_FAIL, SCT_INTEGER, "searchupd_fail", "The number of failed search folder updates after retrying");
	AddStat(SCN_SOAP_REQUESTS, SCT_INTEGER, "soap_request", "Number of soap requests handled by server");
	AddStat(SCN_RESPONSE_TIME, SCT_REAL, "response_time", "Cumulated response time (includes queue time) of SOAP requests, in seconds.");
	AddStat(SCN_PROCESSING_TIME, SCT_REAL, "processing_time", "Cumulated wallclock time taken to process SOAP requests, in seconds.");

	AddStat(SCN_DATABASE_CONNECTS, SCT_INTEGER, "sql_connect", "Number of connections made to SQL server");
	AddStat(SCN_DATABASE_SELECTS, SCT_INTEGER, "sql_select", "Number of SQL Select commands executed");
	AddStat(SCN_DATABASE_INSERTS, SCT_INTEGER, "sql_insert", "Number of SQL Insert commands executed");
	AddStat(SCN_DATABASE_UPDATES, SCT_INTEGER, "sql_update", "Number of SQL Update commands executed");
	AddStat(SCN_DATABASE_DELETES, SCT_INTEGER, "sql_delete", "Number of SQL Delete commands executed");
	AddStat(SCN_DATABASE_FAILED_CONNECTS, SCT_INTEGER, "sql_connect_fail", "Number of failed connections made to SQL server");
	AddStat(SCN_DATABASE_FAILED_SELECTS, SCT_INTEGER, "sql_select_fail", "Number of failed SQL Select commands");
	AddStat(SCN_DATABASE_FAILED_INSERTS, SCT_INTEGER, "sql_insert_fail", "Number of failed SQL Insert commands");
	AddStat(SCN_DATABASE_FAILED_UPDATES, SCT_INTEGER, "sql_update_fail", "Number of failed SQL Update commands");
	AddStat(SCN_DATABASE_FAILED_DELETES, SCT_INTEGER, "sql_delete_fail", "Number of failed SQL Delete commands");
	AddStat(SCN_DATABASE_LAST_FAILED, SCT_TIME, "sql_last_fail_time", "Timestamp of last failed SQL command");
	AddStat(SCN_DATABASE_MWOPS, SCT_INTEGER, "mwops", "MAPI Write Operations");
	AddStat(SCN_DATABASE_MROPS, SCT_INTEGER, "mrops", "MAPI Read Operations");
	AddStat(SCN_DATABASE_DEFERRED_FETCHES, SCT_INTEGER, "deferred_fetches", "Number rows retrieved via deferred write table");
	AddStat(SCN_DATABASE_MERGES, SCT_INTEGER, "deferred_merges", "Number of merges applied to the deferred write table");
	AddStat(SCN_DATABASE_MERGED_RECORDS, SCT_INTEGER, "deferred_records", "Number records merged in the deferred write table");
	AddStat(SCN_DATABASE_ROW_READS, SCT_INTEGER, "row_reads", "Number of table rows read in row order");
	AddStat(SCN_DATABASE_COUNTER_RESYNCS, SCT_INTEGER, "counter_resyncs", "Number of time a counter resync was required");
	AddStat(SCN_DATABASE_MAX_OBJECTID, SCT_INTGAUGE, "max_objectid", "Highest object number used");

	AddStat(SCN_LOGIN_PASSWORD, SCT_INTEGER, "login_password", "Number of logins through password authentication");
	AddStat(SCN_LOGIN_SSL, SCT_INTEGER, "login_ssl", "Number of logins through SSL certificate authentication");
	AddStat(SCN_LOGIN_SSO, SCT_INTEGER, "login_sso", "Number of logins through Single Sign-on");
	AddStat(SCN_LOGIN_SOCKET, SCT_INTEGER, "login_unix", "Number of logins through Unix socket");
	AddStat(SCN_LOGIN_DENIED, SCT_INTEGER, "login_failed", "Number of failed logins");

	AddStat(SCN_SESSIONS_CREATED, SCT_INTEGER, "sessions_created", "Number of created sessions");
	AddStat(SCN_SESSIONS_DELETED, SCT_INTEGER, "sessions_deleted", "Number of deleted sessions");
	AddStat(SCN_SESSIONS_TIMEOUT, SCT_INTEGER, "sessions_timeout", "Number of timed-out sessions");

	AddStat(SCN_SESSIONS_INTERNAL_CREATED, SCT_INTEGER, "sess_int_created", "Number of created internal sessions");
	AddStat(SCN_SESSIONS_INTERNAL_DELETED, SCT_INTEGER, "sess_int_deleted", "Number of deleted internal sessions");

	AddStat(SCN_SESSIONGROUPS_CREATED, SCT_INTEGER, "sess_grp_created", "Number of created sessiongroups");
	AddStat(SCN_SESSIONGROUPS_DELETED, SCT_INTEGER, "sess_grp_deleted", "Number of deleted sessiongroups");

	AddStat(SCN_LDAP_CONNECTS, SCT_INTEGER, "ldap_connect", "Number of connections made to LDAP server");
	AddStat(SCN_LDAP_RECONNECTS, SCT_INTEGER, "ldap_reconnect", "Number of re-connections made to LDAP server");
	AddStat(SCN_LDAP_CONNECT_FAILED, SCT_INTEGER, "ldap_connect_fail", "Number of failed connections made to LDAP server");
	AddStat(SCN_LDAP_CONNECT_TIME, SCT_INTEGER, "ldap_connect_time", "Total duration (µs) of connections made to LDAP server");
	AddStat(SCN_LDAP_CONNECT_TIME_MAX, SCT_INTGAUGE, "ldap_max_connect", "Longest connection time (µs) made to LDAP server");

	/* potentially useless because of SCN_LOGIN_* */
	AddStat(SCN_LDAP_AUTH_LOGINS, SCT_INTEGER, "ldap_auth", "Number of LDAP authentications");
	AddStat(SCN_LDAP_AUTH_DENIED, SCT_INTEGER, "ldap_auth_fail", "Number of failed authentications");
	AddStat(SCN_LDAP_AUTH_TIME, SCT_INTEGER, "ldap_auth_time", "Total authentication time (µs)");
	AddStat(SCN_LDAP_AUTH_TIME_MAX, SCT_INTGAUGE, "ldap_max_auth", "Longest duration (µs) of authentication made to LDAP server");
	AddStat(SCN_LDAP_AUTH_TIME_AVG, SCT_INTGAUGE, "ldap_avg_auth", "Average duration (µs) of authentication made to LDAP server");

	AddStat(SCN_LDAP_SEARCH, SCT_INTEGER, "ldap_search", "Number of searches made to LDAP server");
	AddStat(SCN_LDAP_SEARCH_FAILED, SCT_INTEGER, "ldap_search_fail", "Number of failed searches made to LDAP server");
	AddStat(SCN_LDAP_SEARCH_TIME, SCT_INTEGER, "ldap_search_time", "Total duration (µs) of LDAP searches");
	AddStat(SCN_LDAP_SEARCH_TIME_MAX, SCT_INTGAUGE, "ldap_max_search", "Longest duration (µs) of LDAP search");

	AddStat(SCN_INDEXER_SEARCH_ERRORS, SCT_INTEGER, "index_search_errors", "Number of failed indexer queries");
	AddStat(SCN_INDEXER_SEARCH_MAX, SCT_INTGAUGE, "index_search_max", "Maximum duration (in µs) of an indexed search query");
	AddStat(SCN_INDEXER_SEARCH_AVG, SCT_INTGAUGE, "index_search_avg", "Average duration (in µs) of an indexed search query");
	AddStat(SCN_INDEXED_SEARCHES, SCT_INTEGER, "search_indexed", "Number of indexed searches performed");
	AddStat(SCN_DATABASE_SEARCHES, SCT_INTEGER, "search_database", "Number of database searches performed");

	AddStat(SCN_SERVER_USERDB_BACKEND, SCT_STRING, "userplugin", "User backend plugin");
	AddStat(SCN_SERVER_ATTACH_BACKEND, SCT_STRING, "attachment_storage", "Attachment backend type");
	set(SCN_SERVER_USERDB_BACKEND, cfg->GetSetting("user_plugin"));
	set(SCN_SERVER_ATTACH_BACKEND, cfg->GetSetting("attachment_storage"));
}

void server_stats::stop()
{
	if (m_thread_running)
		g_lpSessionManager->get_db_factory()->thread_end();
	ECStatsCollector::stop();
}

// This is the callback function for libserver/* so that it can notify that a delayed soap
// request has been handled.
static void kcsrv_notify_done(struct soap *soap)
{
	if (g_lpSoapServerConn != nullptr)
		g_lpSoapServerConn->NotifyDone(soap);
}

// Called from ECStatsTables to get server stats
static void kcsrv_get_server_stats(unsigned int *lpulQueueLength,
    time_duration *lpdblAge, unsigned int *lpulThreadCount,
    unsigned int *lpulIdleThreads)
{
	if (g_lpSoapServerConn != nullptr)
		g_lpSoapServerConn->GetStats(lpulQueueLength, lpdblAge, lpulThreadCount, lpulIdleThreads);
}

static void sv_sigterm_async(int)
{
	g_Quit = true;
	if (g_lpSoapServerConn != nullptr)
		/* sets quit flag in dispatcher */
		g_lpSoapServerConn->ShutDown();
}

static void sv_sighup_async(int)
{
	if (g_Quit || g_lpSessionManager == nullptr)
		return;
	sv_sighup_flag = true;
}

void sv_sighup_sync()
{
	bool expect_one = true;
	if (!sv_sighup_flag.compare_exchange_strong(expect_one, false))
		return;
	// g_lpSessionManager only present when kopano_init is called (last init function), signals are initialized much earlier
	if (!g_lpConfig->ReloadSettings())
		ec_log_warn("Unable to reload configuration file, continuing with current settings.");
	if (g_lpConfig->HasErrors())
		ec_log_err("Failed to reload configuration file");

	g_lpSessionManager->GetPluginFactory()->SignalPlugins(SIGHUP);
	auto ll = g_lpConfig->GetSetting("log_level");
	auto new_ll = ll ? strtol(ll, NULL, 0) : EC_LOGLEVEL_WARNING;
	ec_log_get()->SetLoglevel(new_ll);
	ec_log_get()->Reset();
	ec_log_warn("Log connection was reset");

	if (g_lpAudit) {
		ll = g_lpConfig->GetSetting("audit_log_level");
		new_ll = ll ? strtol(ll, NULL, 0) : 1;
		g_lpAudit->SetLoglevel(new_ll);
		g_lpAudit->Reset();
	}
	g_lpSessionManager->m_stats->SetTime(SCN_SERVER_LAST_CONFIGRELOAD, time(nullptr));
	g_lpSoapServerConn->DoHUP();
}

static ECRESULT check_database_engine(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;
	DB_ROW lpRow = NULL;

	auto engine = g_lpConfig->GetSetting("mysql_engine");
	// Only supported from mysql 5.0
	auto er = lpDatabase->DoSelect(format("SHOW TABLE STATUS WHERE engine != '%s'", engine), &lpResult);
	if (er != erSuccess)
		return er;
	while ((lpRow = lpResult.fetch_row()) != nullptr) {
		ec_log_crit("Database table '%s' not in %s format: %s", lpRow[0] ? lpRow[0] : "unknown table", engine, lpRow[1] ? lpRow[1] : "unknown engine");
		er = KCERR_DATABASE_ERROR;
	}
	if (er != erSuccess) {
		ec_log_crit("Your database was incorrectly created. Please upgrade all tables to the %s format using this query:", engine);
		ec_log_crit("  ALTER TABLE <table name> ENGINE='%s';", engine);
		ec_log_crit("This process may take a very long time, depending on the size of your database.");
	}
	return er;
}

static bool filesv1_extract_fanout(const char *s, unsigned int *x, unsigned int *y)
{
	if (strcmp(s, "files") == 0)
		s = "files_v1-10-20";
	return sscanf(s, "files_v1-%u-%u", x, y) == 2;
}

static inline bool is_filesv1(const char *s)
{
	unsigned int ign;
	return filesv1_extract_fanout(s, &ign, &ign);
}

static ECRESULT check_database_attachments(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;
	auto er = lpDatabase->DoSelect("SELECT value FROM settings WHERE name = 'attachment_storage'", &lpResult);
	if (er != erSuccess) {
		ec_log_crit("Unable to read from database");
		return er;
	}

	auto lpRow = lpResult.fetch_row();
	auto backend = g_lpConfig->GetSetting("attachment_storage");
	auto autopick = strcmp(backend, "auto") == 0;
	if (autopick)
		backend = lpRow != nullptr && lpRow[0] != nullptr ? lpRow[0] : default_atx_backend;
	if (lpRow != nullptr && lpRow[0] != nullptr &&
	    // check if the mode is the same as last time
	    !autopick && strcmp(lpRow[0], backend) != 0) {
		if (!m_bIgnoreAttachmentStorageConflict) {
			ec_log_err("Attachments are stored with option \"%s\", but \"%s\" is selected.", lpRow[0], backend);
			return KCERR_DATABASE_ERROR;
		}
		ec_log_warn("Ignoring attachment storing conflict as requested. Attachments are now stored with option \"%s\".", backend);
	}

	// first time we start, set the database to the selected mode
	auto strQuery = "REPLACE INTO settings VALUES ('attachment_storage', '"s + lpDatabase->Escape(backend) + "')";
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) {
		ec_log_err("Unable to update database settings");
		return er;
	}
	ec_log_info("Using the \"%s\" attachment storage backend", backend);
	g_lpConfig->AddSetting("attachment_storage", backend, 0);

	unsigned int l1, l2;
	if (!filesv1_extract_fanout(backend, &l1, &l2))
		return erSuccess;
	// Create attachment directories
	for (unsigned int i = 0; i < l1; ++i)
		for (unsigned int j = 0; j < l2; ++j) {
			auto path = std::string(g_lpConfig->GetSetting("attachment_path")) + PATH_SEPARATOR + stringify(i) + PATH_SEPARATOR + stringify(j);
			auto ret = CreatePath(path.c_str());
			if (ret != 0) {
				ec_log_err("Cannot create %s: %s", path.c_str(), strerror(errno));
				return MAPI_E_UNABLE_TO_COMPLETE;
			}
		}
	return erSuccess;
}

static ECRESULT check_attachment_storage_permissions()
{
	auto backend = g_lpConfig->GetSetting("attachment_storage");

	if (strcmp(backend, "files_v2") == 0 || is_filesv1(backend)) {
		std::string strtestpath = g_lpConfig->GetSetting("attachment_path");
		strtestpath += "/testfile";
		auto tmpfile = fopen(strtestpath.c_str(), "w");
		if (!tmpfile) {
			 ec_log_err("Unable to write attachments to the directory '%s' - %s. Please check the directory and sub directories.",  g_lpConfig->GetSetting("attachment_path"), strerror(errno));
			return KCERR_NO_ACCESS;
		}
		fclose(tmpfile);
		unlink(strtestpath.c_str());
	}
	return erSuccess;
}

static ECRESULT check_database_tproperties_key(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;

	std::string strQuery = "SHOW CREATE TABLE `tproperties`";
	auto er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess) {
		ec_log_err("Unable to read from database");
		return er;
	}
	er = KCERR_DATABASE_ERROR;
	auto lpRow = lpResult.fetch_row();
	if (!lpRow || !lpRow[1]) {
		ec_log_crit("No tproperties table definition found");
		return er;
	}
	std::string strTable = lpRow[1];
	auto start = strTable.find("PRIMARY KEY");
	if (start == strTable.npos) {
		ec_log_crit("No primary key found in tproperties table");
		return er;
	}
	auto end = strTable.find(")", start);
	if (end == strTable.npos) {
		ec_log_crit("No end of primary key found in tproperties table");
		return er;
	}

	strTable.erase(end, strTable.npos);
	strTable.erase(0, start);
	// correct:
	// PRIMARY KEY (`folderid`,`tag`,`hierarchyid`,`type`),
	// incorrect:
	// PRIMARY KEY `ht` (`folderid`,`tag`,`type`,`hierarchyid`)
	// `ht` part seems to be optional
	start = strTable.find_first_of(',');
	if (start != strTable.npos)
		start = strTable.find_first_of(',', start+1);
	if (start == strTable.npos) {
		ec_log_warn("Primary key of tproperties table incorrect, trying: %s", strTable.c_str());
		return er;
	}

	// start+1:end == `type`,`hierarchyid`
	strTable.erase(0, start+1);
	// if not correct...
	if (strTable.compare("`hierarchyid`,`type`") != 0) {
		ec_log_warn("**** WARNING: Installation is not optimal! ****");
		ec_log_warn("  The primary key of the tproperties table is incorrect.");
		ec_log_warn("  Since updating the primary key on a large table is slow, the server will not automatically update this for you.");
	}
	return erSuccess;
}

static ECRESULT check_database_thread_stack(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;

	// only required when procedures are used
	if (!parseBool(g_lpConfig->GetSetting("enable_sql_procedures")))
		return erSuccess;
	std::string strQuery = "SHOW VARIABLES LIKE 'thread_stack'";
	auto er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess) {
		ec_log_err("Unable to read from database");
		return er;
	}
	auto lpRow = lpResult.fetch_row();
	if (!lpRow || !lpRow[1]) {
		ec_log_err("No thread_stack variable returned");
		return er;
	}
	auto ulThreadStack = atoui(lpRow[1]);
	if (ulThreadStack < MYSQL_MIN_THREAD_STACK) {
		ec_log_warn("MySQL thread_stack is set to %u, which is too small", ulThreadStack);
		ec_log_warn("Please set thread_stack to %uK or higher in your MySQL configuration", MYSQL_MIN_THREAD_STACK / 1024);
		if (!m_bIgnoreDbThreadStackSize)
			return KCERR_DATABASE_ERROR;
		ec_log_warn("MySQL thread_stack setting ignored. Please reconsider when 'Thread stack overrun' errors appear in the log.");
	}
	return erSuccess;
}

/**
 * Checks the server_hostname value of the configuration, and if
 * empty, gets the current FQDN through DNS lookups, and updates the
 * server_hostname value in the config object.
 */
static ECRESULT check_server_fqdn()
{
	ECRESULT er = erSuccess;
	char hostname[256]{};
	struct addrinfo *aiResult = NULL;

	// If admin has set the option, we're not using DNS to check the name
	auto option = g_lpConfig->GetSetting("server_hostname");
	if (option && option[0] != '\0')
		return erSuccess;
	auto rc = gethostname(hostname, sizeof(hostname));
	if (rc != 0)
		return KCERR_NOT_FOUND;

	// if we exit hereon after, hostname will always contain a correct hostname, which we can set in the config.
	rc = getaddrinfo(hostname, nullptr, nullptr, &aiResult);
	if (rc != 0) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}
	// Name lookup is required, so set that flag
	rc = getnameinfo(aiResult->ai_addr, aiResult->ai_addrlen, hostname, sizeof(hostname), nullptr, 0, NI_NAMEREQD);
	if (rc != 0) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}
exit:
	if (hostname[0] != '\0')
		g_lpConfig->AddSetting("server_hostname", hostname);
	if (aiResult)
		freeaddrinfo(aiResult);
	return er;
}

/**
 * Checks config options for sane multi-server environments and
 * updates some SSO options if detauls may be inadequate.
 *
 * @return always returns erSuccess
 */
static ECRESULT check_server_configuration()
{
	bool bHaveErrors = false;
	ECSession		*lpecSession = NULL;
	serverdetails_t	sServerDetails;

	// Find FQDN if Kerberos is enabled (remove check if we're using 'server_hostname' for other purposes)
	auto bCheck = parseBool(g_lpConfig->GetSetting("enable_sso"));
	if (bCheck && check_server_fqdn() != erSuccess)
		ec_log_err("WARNING: Unable to find FQDN, please specify in 'server_hostname'. Now using '%s'.", g_lpConfig->GetSetting("server_hostname"));
	// all other checks are only required for multi-server environments
	if (!parseBool(g_lpConfig->GetSetting("enable_distributed_kopano")))
		return erSuccess;
	std::string strServerName = g_lpConfig->GetSetting("server_name");
	if (strServerName.empty()) {
		ec_log_crit("ERROR: No 'server_name' specified while operating in multiserver mode.");
		return KCERR_INVALID_PARAMETER;
		// unable to check any other server details if we have no name, skip other tests
	}

	auto er = g_lpSessionManager->CreateSessionInternal(&lpecSession);
	if (er != erSuccess) {
		ec_log_crit("Internal error 0x%08x while checking distributed configuration", er);
		return er;
	}
	auto cleanup = make_scope_success([&]() {
		g_lpSessionManager->RemoveSessionInternal(lpecSession);
		// we could return an error when bHaveErrors is set, but we currently find this not fatal as a sysadmin might be smarter than us.
		if (bHaveErrors)
			ec_log_warn("WARNING: Inconsistencies detected between local and LDAP based configuration.");
	});
	std::lock_guard<ECSession> holder(*lpecSession);
	er = lpecSession->GetUserManagement()->GetServerDetails(strServerName, &sServerDetails);
	if (er != erSuccess) {
		ec_log_crit("ERROR: Unable to find server information on LDAP for \"%s\": %s (%x). Check your server name.",
			strServerName.c_str(), GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
		// unable to check anything else if we have no details, skip other tests
		// we do return er, since if that is set GetServerDetails() does not work and that is quite vital to work in distributed systems.
		return er;
	}

	// Check the various connection parameters for consistency
	if (g_listen_pipe) {
		if (sServerDetails.GetFilePath().empty()) {
			ec_log_warn("WARNING: 'server_pipe_enabled' is set, but LDAP returns nothing");
			bHaveErrors = true;
		}
		if (sServerDetails.GetFilePath().compare("file://"s + g_lpConfig->GetSetting("server_pipe_name")) != 0 &&
		    sServerDetails.GetFilePath().compare("file:"s + g_lpConfig->GetSetting("server_pipe_name")) != 0) {
			ec_log_warn("WARNING: 'server_pipe_name' is set to '%s', but LDAP returns '%s'", g_lpConfig->GetSetting("server_pipe_name"), sServerDetails.GetFilePath().c_str());
			bHaveErrors = true;
		}
	} else if (!sServerDetails.GetFilePath().empty()) {
		ec_log_warn("WARNING: 'server_pipe_enabled' is unset, but LDAP returns '%s'", sServerDetails.GetFilePath().c_str());
		bHaveErrors = true;
	}

	auto http = tokenize(g_lpConfig->GetSetting("server_listen"), ' ', true);
	auto https = tokenize(g_lpConfig->GetSetting("server_listen_tls"), ' ', true);
	auto http_ok = http.size() == 0 || std::any_of(http.cbegin(), http.cend(),
		[&](const auto &i) { return sServerDetails.GetHttpPort() == ec_parse_bindaddr(i.c_str()).m_port; });
	auto https_ok = https.size() == 0 || std::any_of(https.cbegin(), https.cend(),
		[&](const auto &i) { return sServerDetails.GetSslPort() == ec_parse_bindaddr(i.c_str()).m_port; });
	if (http.size() > 0) {
		if (sServerDetails.GetHttpPath().empty()) {
			ec_log_warn("WARNING: \"server_listen\" is set, but LDAP host entry contains nothing.");
			bHaveErrors = true;
		}
		if (!http_ok) {
			ec_log_warn("WARNING: No entry in \"server_listen\" matches LDAP host entry port %u", sServerDetails.GetHttpPort());
			bHaveErrors = true;
		}
	} else if (!sServerDetails.GetHttpPath().empty()) {
		ec_log_warn("WARNING: \"server_listen\" is empty, but LDAP returns \"%s\"", sServerDetails.GetHttpPath().c_str());
		bHaveErrors = true;
	}

	if (https.size() > 0) {
		if (sServerDetails.GetSslPath().empty()) {
			ec_log_warn("WARNING: \"server_listen_tls\" is set, but LDAP host entry contains nothing.");
			bHaveErrors = true;
		}
		if (!https_ok) {
			ec_log_warn("WARNING: No entry in \"server_listen_tls\" matches LDAP host entry port %u", sServerDetails.GetSslPort());
			bHaveErrors = true;
		}
	} else if (!sServerDetails.GetSslPath().empty()) {
		ec_log_warn("WARNING: \"server_listen_tls\" is empty, but LDAP returns \"%s\"", sServerDetails.GetSslPath().c_str());
		bHaveErrors = true;
	}
	return erSuccess;
}

/**
 * Setup env vars for loading a new allocator on restart
 * @argv:	full argv of current invocation
 * @lib:	library to load via LD_PRELOAD
 *
 * As every program under Linux is linked to libc and symbol resolution is done
 * breadth-first, having just libkcserver.so linked to the alternate allocator
 * is not enough to ensure the allocator is being used in favor of libc malloc.
 * As such, one will have to use the exec syscall to completely re-launch the
 * program.
 *
 * A program built against glibc will have a record for e.g.
 * "malloc@GLIBC_2.2.5". The use of LD_PRELOAD appears to relax the version
 * requirement, though; the benefit would be that libtcmalloc's malloc will
 * take over _all_ malloc calls.
 *
 * Returns 0 on success (no action), or >0 (actioned).
 */
static int kc_reexec_setup_allocator(const char *lib)
{
	if (*lib == '\0' || getenv("KC_AVOID_REEXEC") != nullptr ||
	    getenv("KC_REEXEC_DONE") != nullptr)
		return 0;
	auto handle = dlopen(lib, RTLD_LAZY | RTLD_GLOBAL);
	if (handle == nullptr)
		/*
		 * Ignore libraries that won't load anyway. This avoids
		 * ld.so emitting a scary warning if we did re-exec.
		 */
		return 0;
	dlclose(handle);
	auto s = getenv("LD_PRELOAD");
	if (s == nullptr) {
		unsetenv("KC_ORIGINAL_PRELOAD");
		setenv("LD_PRELOAD", lib, true);
	} else if (strstr(s, "/valgrind/") != nullptr) {
		/*
		 * Within vg, everything is a bit different — since it catches
		 * execve itself. Execing /proc/self/exe therefore won't work,
		 * we would need to use argv[0]. But… don't bother.
		 */
		return 0;
	} else {
		setenv("KC_ORIGINAL_PRELOAD", s, true);
		setenv("LD_PRELOAD", (std::string(s) + ":" + lib).c_str(), true);
	}
	ec_log_debug("Preparing relaunch with allocator lib %s", lib);
	return 1;
}

int main(int argc, char* argv[])
{
	int nReturn = 0;
	const char *config = ECConfig::GetDefaultPath("server.cfg");
	const char *default_config = config;
	bool exp_config = false;

	enum {
		OPT_HELP = UCHAR_MAX + 1,
		OPT_CONFIG,
		OPT_RESTART_SEARCHES,
		OPT_IGNORE_DATABASE_VERSION_CONFLICT,
		OPT_IGNORE_ATTACHMENT_STORAGE_CONFLICT,
		OPT_OVERRIDE_DISTRIBUTED_LOCK,
		OPT_FORCE_DATABASE_UPGRADE,
		OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS,
		OPT_IGNORE_DB_THREAD_STACK_SIZE,
		OPT_DUMP_CONFIG,
	};
	static const struct option long_options[] = {
		{ "help", 0, NULL, OPT_HELP },	// help text
		{ "config", 1, NULL, OPT_CONFIG },	// config file
		{ "restart-searches", 0, NULL, OPT_RESTART_SEARCHES },
		{ "ignore-database-version-conflict", 0, NULL, OPT_IGNORE_DATABASE_VERSION_CONFLICT },
		{ "ignore-attachment-storage-conflict", 0, NULL, OPT_IGNORE_ATTACHMENT_STORAGE_CONFLICT },
		{ "override-multiserver-lock", 0, NULL, OPT_OVERRIDE_DISTRIBUTED_LOCK },
		{ "force-database-upgrade", 0, NULL, OPT_FORCE_DATABASE_UPGRADE },
		{ "ignore-unknown-config-options", 0, NULL, OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS },
		{ "ignore-db-thread-stack-size", 0, NULL, OPT_IGNORE_DB_THREAD_STACK_SIZE },
		{"dump-config", 0, nullptr, OPT_DUMP_CONFIG},
		{ NULL, 0, NULL, 0 }
	};

	// check for configfile
	while (1) {
		int c = my_getopt_long_permissive(argc, argv, "c:VFiuR", long_options, NULL);
		if(c == -1)
			break;

		switch (c) {
		case 'c':
		case OPT_CONFIG:
			config = optarg;
			exp_config = true;
			break;
		case OPT_HELP:
			cout << "kopano-server " PROJECT_VERSION;
			cout << endl;
			cout << argv[0] << " [options...]" << endl;
			cout << "  -c --config=FILE                           Set new config file location. Default: " << default_config << endl;
			cout << "  -V                                         Print version info." << endl;
			cout << "  -R --restart-searches                      Rebuild searchfolders." << endl;
			cout << "     --ignore-database-version-conflict      Start even if database version conflict with server version" << endl;
			cout << "     --ignore-attachment-storage-conflict    Start even if the attachment_storage config option changed" << endl;
			cout << "     --override-multiserver-lock             Start in multiserver mode even if multiserver mode is locked" << endl;
			cout << "     --force-database-upgrade                Start upgrade from 6.x database and continue running if upgrade is complete" << endl;
			cout << "     --ignore-db-thread-stack-size           Start even if the thread_stack setting for MySQL is too low" << endl;
			return 0;
		case 'V':
			cout << "kopano-server " PROJECT_VERSION << endl;
			return 0;
		case 'R':
		case OPT_RESTART_SEARCHES:
			restart_searches = 1;
			break;
		case OPT_IGNORE_DATABASE_VERSION_CONFLICT:
			m_bIgnoreDatabaseVersionConflict = true;
			break;
		case OPT_IGNORE_ATTACHMENT_STORAGE_CONFLICT:
			m_bIgnoreAttachmentStorageConflict = true;
			break;
		case OPT_FORCE_DATABASE_UPGRADE:
			m_bForceDatabaseUpdate = true;
			break;
		case OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS:
			m_bIgnoreUnknownConfigOptions = true;
			break;
		case OPT_IGNORE_DB_THREAD_STACK_SIZE:
			m_bIgnoreDbThreadStackSize = true;
			break;
		case OPT_DUMP_CONFIG:
			g_dump_config = true;
			break;
		};
	}
	try {
		nReturn = running_server(argv[0], config, exp_config, argc, argv,
		          argc - optind, &argv[optind]);
	} catch (const std::exception &e) {
		ec_log_err("Exception caught: %s", e.what());
	}
	return nReturn;
}

#define KOPANO_SERVER_PIPE "/var/run/kopano/server.sock"
#define KOPANO_SERVER_PRIO "/var/run/kopano/prio.sock"

static void InitBindTextDomain()
{
	// Set gettext codeset, used for generated folder name translations
	bind_textdomain_codeset("kopano", "UTF-8");
}

static int ksrv_listen_inet(ECSoapServerConnection *ssc, ECConfig *cfg)
{
	auto info = ec_bindspec_to_sockets(tokenize(cfg->GetSetting("server_listen"), ' ', true),
	            S_IRWUGO, cfg->GetSetting("run_as_user"),
	            cfg->GetSetting("run_as_group"), g_used_fds);
	if (info.first < 0)
		return info.first;
	auto http_sock = std::move(info.second);
	info = ec_bindspec_to_sockets(tokenize(cfg->GetSetting("server_listen_tls"), ' ', true),
	       S_IRWUGO, cfg->GetSetting("run_as_user"),
	       cfg->GetSetting("run_as_group"), g_used_fds);
	if (info.first < 0)
		return info.first;
	auto https_sock = std::move(info.second);

	/* Launch */
	for (auto &spec : http_sock) {
		auto er = ssc->ListenTCP(spec);
		if (er != erSuccess)
			return er;
	}

	auto keyfile = cfg->GetSetting("server_ssl_key_file", "", nullptr);
	auto keypass = cfg->GetSetting("server_ssl_key_pass", "", nullptr);
	auto cafile  = cfg->GetSetting("server_ssl_ca_file", "", nullptr);
	auto capath  = cfg->GetSetting("server_ssl_ca_path", "", nullptr);
	for (auto &spec : https_sock) {
		auto er = ssc->ListenSSL(spec, keyfile, keypass, cafile, capath);
		if (er != erSuccess)
			return er;
	}
	g_listen_http  = !http_sock.empty();
	g_listen_https = !https_sock.empty();
	return erSuccess;
}

static int ksrv_listen_pipe(ECSoapServerConnection *ssc, ECConfig *cfg)
{
	auto list = tokenize(cfg->GetSetting("server_pipe_priority"), ' ', true);
	for (auto &e : list)
		e.insert(0, "unix:");
	auto prio = ec_bindspec_to_sockets(std::move(list), S_IRWUG,
	            cfg->GetSetting("run_as_user"),
	            cfg->GetSetting("run_as_group"), g_used_fds);
	if (prio.first < 0)
		return prio.first;
	/*
	 * Priority queue is always enabled, create as first socket, so this
	 * socket is returned first too on activity. [This is no longer true…
	 * need to create INET sockets beforehand because of privilege drop.]
	 */
	for (auto &spec : prio.second) {
		auto er = ssc->ListenPipe(spec, true);
		if (er != erSuccess)
			return er;
	}
	if (strcmp(cfg->GetSetting("server_pipe_enabled"), "yes") == 0) {
		list = tokenize(cfg->GetSetting("server_pipe_name"), ' ', true);
		for (auto &e : list)
			e.insert(0, "unix:");
		auto info = ec_bindspec_to_sockets(std::move(list), S_IRWUGO,
		            cfg->GetSetting("run_as_user"),
		            cfg->GetSetting("run_as_group"), g_used_fds);
		if (info.first < 0)
			return info.first;
		auto pipe_sock = std::move(info.second);
		for (auto &spec : pipe_sock) {
			auto er = ssc->ListenPipe(spec, false);
			if (er != erSuccess)
				return er;
		}
		g_listen_pipe = !pipe_sock.empty();
	}
	return erSuccess;
}

#ifdef HAVE_KCOIDC_H
	bool kcoidc_initialized = false;
#endif
#ifdef HAVE_KUSTOMER
	bool kustomer_initialized = false;
#endif

static bool setup_kustomer()
{
#ifdef HAVE_KUSTOMER
	int kustomerDebug = -1;
	if (*g_lpConfig->GetSetting("kustomer_debug") != '\0')
		kustomerDebug = parseBool(g_lpConfig->GetSetting("kustomer_debug"));
	ec_log_info("KUSTOMER initializing");
	auto res = kustomer_set_logger(kustomer_log_info_cb, kustomerDebug);
	if (res != KUSTOMER_ERRSTATUSSUCCESS) {
		ec_log_err("KUSTOMER failed set logger: 0x%llx, %s", res, kustomer_err_numeric_text(res));
		return false;
	}
	res = kustomer_set_productuseragent(const_cast<char *>(kustomerProductUserAgent));
	if (res != KUSTOMER_ERRSTATUSSUCCESS) {
		ec_log_err("KUSTOMER failed to set product user agent: 0x%llx, %s", res, kustomer_err_numeric_text(res));
		return false;
	}
	res = kustomer_set_autorefresh(1);
	if (res != KUSTOMER_ERRSTATUSSUCCESS) {
		ec_log_err("KUSTOMER failed to enable auto refresh: 0x%llx, %s", res, kustomer_err_numeric_text(res));
		return false;
	}
	res = kustomer_initialize(const_cast<char *>(kustomerProductName));
	if (res != KUSTOMER_ERRSTATUSSUCCESS) {
		ec_log_err("KUSTOMER initialization failed: 0x%llx, %s", res, kustomer_err_numeric_text(res));
		return false;
	}
	auto kustomer_initialize_timeout = atoi(g_lpConfig->GetSetting("kustomer_initialize_timeout"));
	ec_log_debug("KUSTOMER waiting on initialization for %d seconds", kustomer_initialize_timeout);
	if (kustomer_initialize_timeout > 0) {
		res = kustomer_wait_until_ready(kustomer_initialize_timeout);
		if (res != KUSTOMER_ERRSTATUSSUCCESS) {
			ec_log_err("KUSTOMER failed to initialize: 0x%llx, %s", res, kustomer_err_numeric_text(res));
			return false;
		}
		ec_log_info("KUSTOMER initialized");
	}
	kustomer_initialized = true;
	res = kustomer_log_ensured_claims();
	if (res != KUSTOMER_ERRSTATUSSUCCESS)
		return false;
	res = kustomer_set_notify_when_updated(kustomer_notify_update_cb, kustomer_notify_exit_cb);
	if (res != KUSTOMER_ERRSTATUSSUCCESS) {
		ec_log_err("KUSTOMER failed to watch for updates: 0x%llx, %s", res, kustomer_err_numeric_text(res));
		return false;
	}
#endif
	return true;
}

static bool setup_kcoidc()
{
#ifdef HAVE_KCOIDC_H
	int kcoidcDebug = -1;
	if (*g_lpConfig->GetSetting("kcoidc_debug") != '\0')
		kcoidcDebug = parseBool(g_lpConfig->GetSetting("kcoidc_debug"));
	auto res = kcoidc_set_logger(kcoidc_log_debug_cb, kcoidcDebug);
	if (res != 0) {
		ec_log_err("KCOIDC failed set logger: 0x%llx", res);
		return false;
	}
	if (parseBool(g_lpConfig->GetSetting("kcoidc_insecure_skip_verify"))) {
		res = kcoidc_insecure_skip_verify(1);
		if (res != 0) {
			ec_log_err("KCOIDC insecure_skip_verify failed: 0x%llx", res);
			return false;
		}
	}
	auto issuer = g_lpConfig->GetSetting("kcoidc_issuer_identifier");
	if (issuer && strlen(issuer) > 0) {
		ec_log_info("KCOIDC initializing provider (%s)", issuer);
		res = kcoidc_initialize(const_cast<char *>(issuer));
		if (res != 0) {
			ec_log_err("KCOIDC provider (%s) initialization failed: 0x%llx", issuer, res);
			return false;
		}
		auto kcoidc_initialize_timeout = atoi(g_lpConfig->GetSetting("kcoidc_initialize_timeout"));
		ec_log_debug("KCOIDC provider (%s) waiting on initialization for %d seconds", issuer, kcoidc_initialize_timeout);
		if (kcoidc_initialize_timeout > 0) {
			res = kcoidc_wait_until_ready(kcoidc_initialize_timeout);
			if (res != 0) {
				ec_log_err("KCOIDC provider (%s) failed to initialize: 0x%llx", issuer, res);
				return false;
			}
			ec_log_info("KCOIDC initialized oidc provider (%s)", issuer);
		}
		kcoidc_initialized = true;
	}
#endif
	return true;
}

static void cleanup(ECRESULT er)
{
	if (er != erSuccess) {
		auto msg = format("An error occurred: %s (0x%x).", GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
		if (g_lpConfig)
			msg += format(" Please check logfile %s:%s for details.",
			       g_lpConfig->GetSetting("log_method"), g_lpConfig->GetSetting("log_file"));
		else
			msg += " Please check logfile for details.";
		fprintf(stderr, "\n%s\n\n", msg.c_str());
	}

	if (g_lpAudit)
		g_lpAudit->Log(EC_LOGLEVEL_ALWAYS, "server shutdown in progress");

	/* Ensure threads are stopped before ripping away the underlying session state */
	kopano_notify_done = nullptr;
	kopano_get_server_stats = nullptr;
	g_lpSoapServerConn.reset();
	if (g_lpSessionManager != nullptr)
		g_lpSessionManager->RemoveAllSessions();
	kopano_exit();
#ifdef HAVE_KCOIDC_H
	if (kcoidc_initialized) {
		auto res = kcoidc_uninitialize();
		if (res != 0)
			ec_log_always("KCOIDC failed to uninitialize: 0x%llx", res);
	}
#endif
#ifdef HAVE_KUSTOMER
	if (kustomer_initialized) {
		auto res = kustomer_uninitialize();
		if (res != KUSTOMER_ERRSTATUSSUCCESS) {
			ec_log_always("KUSTOMER failed to uninitialize: 0x%llx", res);
		}
	}
#endif
	kopano_unloadlibrary();
	ec_log_always("Server shutdown complete.");
	// cleanup ICU data so valgrind is happy
	u_cleanup();
}

static int running_server(char *szName, const char *szConfig, bool exp_config,
    int argc, char **argv, int trim_argc, char **trim_argv)
{
	int retval = -1;
	ECRESULT		er = erSuccess;
	const configsetting_t lpDefaults[] = {
		// Aliases
		{"server_port", "server_tcp_port", CONFIGSETTING_ALIAS | CONFIGSETTING_OBSOLETE},
		{ "unix_socket",				"server_pipe_name", CONFIGSETTING_ALIAS },
		// Default settings
		{ "enable_hosted_kopano",		"false" },	// Will only be checked when license allows hosted
		{ "enable_distributed_kopano",	"false" },
		{ "server_name",				"" }, // used by multi-server
		{ "server_hostname",			"" }, // used by kerberos, if empty, gethostbyname is used
		// server connections
		{"server_pipe_enabled", "yes", CONFIGSETTING_NONEMPTY},
		{"server_pipe_name", KOPANO_SERVER_PIPE, CONFIGSETTING_NONEMPTY},
		{"server_pipe_priority", KOPANO_SERVER_PRIO, CONFIGSETTING_NONEMPTY},
		{ "server_recv_timeout",		"5", CONFIGSETTING_RELOADABLE },	// timeout before reading next XML request
		{ "server_read_timeout",		"60", CONFIGSETTING_RELOADABLE }, // timeout during reading of XML request
		{ "server_send_timeout",		"60", CONFIGSETTING_RELOADABLE },
		{ "allow_local_users",			"yes", CONFIGSETTING_RELOADABLE },			// allow any user connect through the Unix socket
		{ "local_admin_users",			"root", CONFIGSETTING_RELOADABLE },			// this local user is admin
		{ "run_as_user",			"kopano" }, // drop root privileges, and run as this user/group
		{ "run_as_group",			"kopano" },
		{"pid_file", "", CONFIGSETTING_OBSOLETE},
		{"allocator_library", "libtcmalloc_minimal.so.4"},
		{ "coredump_enabled",			"yes" },

		{ "system_email_address",		"postmaster@localhost", CONFIGSETTING_RELOADABLE },
		{"server_ssl_key_file", "/etc/kopano/ssl/server.pem", CONFIGSETTING_RELOADABLE},
		{"server_ssl_key_pass", "server", CONFIGSETTING_EXACT | CONFIGSETTING_RELOADABLE},
		{"server_ssl_ca_file", "/etc/kopano/ssl/cacert.pem", CONFIGSETTING_RELOADABLE},
		{"server_ssl_ca_path", "", CONFIGSETTING_RELOADABLE},
		{"server_tls_min_proto", KC_DEFAULT_TLSMINPROTO, CONFIGSETTING_RELOADABLE},
		{"server_ssl_protocols", "", CONFIGSETTING_UNUSED},
		{"server_ssl_ciphers", KC_DEFAULT_CIPHERLIST, CONFIGSETTING_RELOADABLE},
		{"server_ssl_prefer_server_ciphers", "yes", CONFIGSETTING_RELOADABLE},
		{"server_ssl_curves", KC_DEFAULT_ECDH_CURVES, CONFIGSETTING_RELOADABLE},
		{"server_listen", "*%lo:236"},
		{"server_listen_tls", ""},
		{ "sslkeys_path",				"/etc/kopano/sslkeys" },	// login keys
		// Database options
		{ "database_engine",			"mysql" },
		// MySQL Settings
		{ "mysql_host",					"localhost" },
		{ "mysql_port",					"3306" },
		{ "mysql_user",					"root" },
		{ "mysql_password",				"",	CONFIGSETTING_EXACT },
		{ "mysql_database",				"kopano" },
		{ "mysql_socket",				"" },
		{ "mysql_engine",				"InnoDB"},
		{"attachment_storage", "auto"},
#ifdef HAVE_LIBS3_H
		{"attachment_s3_hostname", ""},
		{"attachment_s3_protocol", "https"},
		{"attachment_s3_uristyle", "virtualhost"},
		{"attachment_s3_accesskeyid", ""},
		{"attachment_s3_secretaccesskey", ""},
		{"attachment_s3_bucketname", ""},
		{"attachment_s3_region", ""},
#endif
		{"attachment_path", "/var/lib/kopano/attachments"},
		{ "attachment_compression",		"6" },

		// Log options
		{"log_method", "auto", CONFIGSETTING_NONEMPTY},
		{"log_file", ""},
		{"log_level", "3", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{ "log_timestamp",				"1" },
		{ "log_buffer_size", "0" },
		{"request_log_method", "off"},
		{"request_log_file", "-"},
		// security log options
		{"audit_log_enabled", "no", CONFIGSETTING_NONEMPTY},
		{"audit_log_method", "syslog", CONFIGSETTING_NONEMPTY},
		{"audit_log_file", "-", CONFIGSETTING_NONEMPTY},
		{"audit_log_level", "1", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{ "audit_log_timestamp",		"0" },

		// user plugin
		{ "user_plugin",				"db" },
		{ "user_plugin_config",			"/etc/kopano/ldap.cfg" },
		{"createuser_script", "internal", CONFIGSETTING_RELOADABLE},
		{"deleteuser_script", "internal", CONFIGSETTING_RELOADABLE},
		{"creategroup_script", "internal", CONFIGSETTING_RELOADABLE},
		{"deletegroup_script", "internal", CONFIGSETTING_RELOADABLE},
		{"createcompany_script", "internal", CONFIGSETTING_RELOADABLE},
		{"deletecompany_script", "internal", CONFIGSETTING_RELOADABLE},
		{ "user_safe_mode",				"no", CONFIGSETTING_RELOADABLE },

		// Storename format
		{ "storename_format",			"%f" },
		{ "loginname_format",			"%u" },

		// internal server controls
		{ "softdelete_lifetime",		"30", CONFIGSETTING_RELOADABLE },	// time expressed in days, 0 == never delete anything
		{ "cache_cell_size",			"0", CONFIGSETTING_SIZE },
		{ "cache_object_size",		"0", CONFIGSETTING_SIZE },
		{ "cache_indexedobject_size",	"0", CONFIGSETTING_SIZE },
		{ "cache_quota_size",			"0", CONFIGSETTING_SIZE },
		{ "cache_quota_lifetime",		"1" },							// 1 minute
		{ "cache_user_size",			"1M", CONFIGSETTING_SIZE },		// 48 bytes per struct, can hold 21k+ users, allocated 2x (user and ueid cache)
		{ "cache_userdetails_size",		"0", CONFIGSETTING_SIZE },
		{ "cache_userdetails_lifetime", "0" },							// 0 minutes - forever
		{ "cache_acl_size",				"1M", CONFIGSETTING_SIZE },		// 1Mb, acl table cache
		{ "cache_store_size",			"1M", CONFIGSETTING_SIZE },		// 1Mb, store table cache (storeid, storeguid), 40 bytes
		{ "cache_server_size",			"1M", CONFIGSETTING_SIZE },		// 1Mb
		{ "cache_server_lifetime",		"30" },							// 30 minutes
		/* Default no quotas. Note: quota values are in Mb, and thus have no size flag. */
		{ "quota_warn",				"0", CONFIGSETTING_RELOADABLE },
		{ "quota_soft",				"0", CONFIGSETTING_RELOADABLE },
		{ "quota_hard",				"0", CONFIGSETTING_RELOADABLE },
		{ "companyquota_warn",		"0", CONFIGSETTING_RELOADABLE },
		{ "session_timeout",		"300", CONFIGSETTING_RELOADABLE },		// 5 minutes
		{ "sync_lifetime",			"90", CONFIGSETTING_RELOADABLE },		// 90 days
		{ "auth_method",			"plugin", CONFIGSETTING_RELOADABLE },		// plugin (default), pam, kerberos
		{ "pam_service",			"passwd", CONFIGSETTING_RELOADABLE },		// pam service, found in /etc/pam.d/
		{ "enable_sso",				"no", CONFIGSETTING_RELOADABLE },			// autodetect between Kerberos and NTLM
		{"ntlm_auth", "/usr/bin/ntlm_auth", CONFIGSETTING_RELOADABLE},
		{ "session_ip_check",		"yes", CONFIGSETTING_RELOADABLE },			// check session id comes from same ip address (or not)
		{ "hide_everyone",			"no", CONFIGSETTING_RELOADABLE },			// whether internal group Everyone should be removed for users
		{ "hide_system",			"yes", CONFIGSETTING_RELOADABLE },			// whether internal user SYSTEM should be removed for users
		{ "enable_gab",				"yes", CONFIGSETTING_RELOADABLE },			// whether the GAB is enabled
		{"abtable_initially_empty", "no", CONFIGSETTING_RELOADABLE},
        { "enable_enhanced_ics",    "yes", CONFIGSETTING_RELOADABLE },			// (dis)allow enhanced ICS operations (stream and notifications)
        { "enable_sql_procedures",  "no" },			// (dis)allow SQL procedures (requires mysql config stack adjustment), not reloadable because in the middle of the streaming flip

		{ "search_enabled",			"yes", CONFIGSETTING_RELOADABLE },
		{ "search_socket",			"file:///var/run/kopano/search.sock", CONFIGSETTING_RELOADABLE },
		{ "search_timeout",			"10", CONFIGSETTING_RELOADABLE },

		{ "threads",				"8", CONFIGSETTING_RELOADABLE },
		{"thread_limit", "40", CONFIGSETTING_RELOADABLE},
		{ "watchdog_max_age",		"500", CONFIGSETTING_RELOADABLE },
		{ "watchdog_frequency",		"1", CONFIGSETTING_RELOADABLE },

		{ "folder_max_items",		"1000000", CONFIGSETTING_RELOADABLE },
		{ "default_sort_locale_id",		"en_US", CONFIGSETTING_RELOADABLE },
		{ "sync_gab_realtime",			"yes", CONFIGSETTING_RELOADABLE },
		{ "max_deferred_records",		"0", CONFIGSETTING_RELOADABLE },
		{ "max_deferred_records_folder", "20", CONFIGSETTING_RELOADABLE },
		{ "enable_test_protocol",		"no", CONFIGSETTING_RELOADABLE },
		{ "disabled_features", "imap pop3", CONFIGSETTING_RELOADABLE },
		{ "mysql_group_concat_max_len", "21844", CONFIGSETTING_RELOADABLE },
		{ "restrict_admin_permissions", "no", 0 },
		{"embedded_attachment_limit", "20", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{ "proxy_header", "", CONFIGSETTING_RELOADABLE },
		{ "owner_auto_full_access", "true" },
		{ "attachment_files_fsync", "yes", 0 },
		{ "tmp_path", "/tmp" },
		{ "shared_reminders", "yes", CONFIGSETTING_RELOADABLE }, // enable/disable reminders for shared stores
		{"statsclient_url", "unix:/var/run/kopano/statsd.sock", CONFIGSETTING_RELOADABLE},
		{"statsclient_interval", "0", CONFIGSETTING_RELOADABLE},
		{"statsclient_ssl_verify", "yes", CONFIGSETTING_RELOADABLE},
		{"surveyclient_url", "https://stats.kopano.io/api/stats/v1/submit", CONFIGSETTING_RELOADABLE},
		{"surveyclient_interval", "3600", CONFIGSETTING_RELOADABLE},
		{"surveyclient_ssl_verify", "yes", CONFIGSETTING_RELOADABLE},
#ifdef HAVE_KCOIDC_H
		{ "kcoidc_issuer_identifier", "", 0},
		{ "kcoidc_insecure_skip_verify", "no", 0},
		{ "kcoidc_initialize_timeout", "60", 0 },
		{ "kcoidc_debug", "", 0 },
#endif
#ifdef HAVE_KUSTOMER
		{ "kustomer_initialize_timeout", "60", 0 },
		{ "kustomer_debug", "", 0 },
#endif
		{"cache_cellcache_reads", "yes", CONFIGSETTING_RELOADABLE},
		{ NULL, NULL },
	};

	// Init random generator
	rand_init();
	/*
	 * Init translations according to environment variables.
	 * It also changes things like decimal separator, which gsoap < 2.8.39
	 * fails to cope with properly.
	 */
	setlocale(LC_ALL, "");
	forceUTF8Locale(true);
	InitBindTextDomain();

	auto laters = make_scope_success([&]() {
		cleanup(er);
		ssl_threading_cleanup();
		SSL_library_cleanup(); //cleanup memory so valgrind is happy
	});
	// Load settings
	g_lpConfig.reset(ECConfig::Create(lpDefaults));
	if (!g_lpConfig->LoadSettings(szConfig, !exp_config) ||
	    g_lpConfig->ParseParams(trim_argc, trim_argv) < 0 ||
	    (!m_bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors()) ) {
		LogConfigErrors(g_lpConfig.get());
		er = KCERR_UNCONFIGURED;
		return retval;
	}
	if (g_dump_config)
		return g_lpConfig->dump_config(stdout) == 0 ? hrSuccess : MAPI_E_CALL_FAILED;

	// setup logging
	auto g_lpLogger = CreateLogger(g_lpConfig.get(), szName);
	if (!g_lpLogger) {
		fprintf(stderr, "Error in log configuration, unable to resume.\n");
		er = KCERR_UNCONFIGURED;
		return retval;
	}
	ec_log_set(g_lpLogger);
	if (m_bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors())
		LogConfigErrors(g_lpConfig.get());
	if (!TmpPath::instance.OverridePath(g_lpConfig.get()))
		ec_log_err("Ignoring invalid path-setting!");
	g_lpAudit = CreateLogger(g_lpConfig.get(), szName, LOGTYPE_AUDIT);
	if (g_lpAudit)
		g_lpAudit->logf(EC_LOGLEVEL_NOTICE, "server startup uid=%d", getuid());
	else
		ec_log_info("Audit logging not enabled.");

	ec_log_always("Starting kopano-server version " PROJECT_VERSION " (pid %d uid %u)", getpid(), getuid());
	if (g_lpConfig->HasWarnings())
		LogConfigErrors(g_lpConfig.get());

	/* setup connection handler */
	g_lpSoapServerConn.reset(new(std::nothrow) ECSoapServerConnection(g_lpConfig));
	er = ksrv_listen_inet(g_lpSoapServerConn.get(), g_lpConfig.get());
	if (er != erSuccess)
		return retval;
	er = ksrv_listen_pipe(g_lpSoapServerConn.get(), g_lpConfig.get());
	if (er != erSuccess)
		return retval;

	struct rlimit limit;
	limit.rlim_cur = KC_DESIRED_FILEDES;
	limit.rlim_max = KC_DESIRED_FILEDES;
	if (setrlimit(RLIMIT_NOFILE, &limit) < 0) {
		ec_log_warn("setrlimit(RLIMIT_NOFILE, %d) failed, you will only be able to connect up to %d sockets.", KC_DESIRED_FILEDES, getdtablesize());
		ec_log_warn("Either start the process as root, or increase user limits for open file descriptors.");
	}
	unix_coredump_enable(g_lpConfig->GetSetting("coredump_enabled"));
	auto did_setid = unix_runas(g_lpConfig.get());
	if (did_setid < 0) {
		er = KCERR_CALL_FAILED;
		return retval;
	}
	auto do_allocator = kc_reexec_setup_allocator(g_lpConfig->GetSetting("allocator_library"));
	if (did_setid == 0 && do_allocator == 0) {
		ec_reexec_finalize();
	} else if (did_setid > 0 || do_allocator > 0) {
		ec_reexec_prepare_sockets(g_lpSoapServerConn->maxlistenfds());
		auto ret = ec_reexec(argv);
		if (ret < 0)
			ec_log_notice("K-1240: Failed to re-exec self: %s. "
				"Continuing with standard allocator and/or restricted coredumps.",
				strerror(-ret));
	}

	g_request_logger = CreateLogger(g_lpConfig.get(), szName, LOGTYPE_REQUEST);
	auto aback = g_lpConfig->GetSetting("attachment_storage");
	if (strcmp(aback, "files_v2") == 0 || is_filesv1(aback) ||
	    strcmp(aback, "auto") == 0) {
		/*
		 * Either (1.) the attachment directory or (2.) its immediate
		 * parent directory needs to exist with right permissions.
		 * (Official KC builds use #2 as of this writing.)
		 */
		auto ret = CreatePath(g_lpConfig->GetSetting("attachment_path"));
		if (ret < 0) {
			ec_log_err("Unable to create attachment directory \"%s\": %s", g_lpConfig->GetSetting("attachment_path"), strerror(-ret));
			er = KCERR_DATABASE_ERROR;
			return retval;
		}
		struct stat dir;
		if (stat(g_lpConfig->GetSetting("attachment_path"), &dir) != 0) {
			ec_log_err("Unable to stat attachment directory '%s', error: %s", g_lpConfig->GetSetting("attachment_path"), strerror(errno));
			er = KCERR_DATABASE_ERROR;
			return retval;
		}
#ifdef HAVE_LIBS3_H
	} else if (strcmp(aback, "s3") == 0) {
		// @todo check S3 settings and connectivity
		ec_log_info("Attachment storage set to S3 Storage");
#endif
	} else if (strcmp(aback, "database") != 0) {
		ec_log_err("Unknown attachment_storage option '%s', reverting to default 'database' method.", g_lpConfig->GetSetting("attachment_storage"));
		g_lpConfig->AddSetting("attachment_storage", "database");
	}

	if (strcasecmp(g_lpConfig->GetSetting("user_plugin"), "db") == 0 &&
	    !parseBool(g_lpConfig->GetSetting("sync_gab_realtime"))) {
		ec_log_info("Unsupported sync_gab_realtime = no when using DB plugin. Enabling sync_gab_realtime.");
		g_lpConfig->AddSetting("sync_gab_realtime", "yes");
	}

	kopano_notify_done = kcsrv_notify_done;
	kopano_get_server_stats = kcsrv_get_server_stats;
	kopano_initlibrary(g_lpConfig->GetSetting("mysql_database_path"), g_lpConfig->GetSetting("mysql_config_file"));
    soap_ssl_init(); // Always call this in the main thread once!
    ssl_threading_setup();
	if (!setup_kustomer())
		return retval;
	if (!setup_kcoidc())
		return retval;

	// Test database settings
	auto stats = std::make_shared<server_stats>(g_lpConfig);
	auto lpDatabaseFactory = std::make_unique<ECDatabaseFactory>(g_lpConfig, stats);
	std::unique_ptr<ECDatabase> lpDatabase;
	std::string dbError;
	// open database
	er = lpDatabaseFactory->CreateDatabaseObject(&unique_tie(lpDatabase), dbError);
	if(er == KCERR_DATABASE_NOT_FOUND) {
		er = lpDatabaseFactory->CreateDatabase();
		if (er != erSuccess)
			return retval;
	}

	unsigned int attempts = 1;
	while (lpDatabase == nullptr && attempts < 10) {
		er = lpDatabaseFactory->CreateDatabaseObject(&unique_tie(lpDatabase), dbError);
		if (er == erSuccess)
			break;
		ec_log_info("Unable to connect, retrying in 10s");
		sleep(10);
		attempts++;
	}
	if(er != erSuccess) {
		ec_log_crit("Unable to connect to database: %s", dbError.c_str());
		return retval;
	}

	ec_log_notice("Connection to database '%s' succeeded", g_lpConfig->GetSetting("mysql_database"));
	auto hosted = parseBool(g_lpConfig->GetSetting("enable_hosted_kopano"));
	auto distributed = parseBool(g_lpConfig->GetSetting("enable_distributed_kopano"));
	struct sigaction act{};
	sigemptyset(&act.sa_mask);
	// normally ignore these signals
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	act.sa_handler = sv_sighup_async;
	act.sa_flags = SA_ONSTACK;
	sigemptyset(&act.sa_mask);
	sigaction(SIGHUP, &act, nullptr);
	act.sa_handler = sv_sigterm_async;
	act.sa_flags = SA_ONSTACK | SA_RESETHAND;
	sigaction(SIGINT, &act, nullptr);
	sigaction(SIGTERM, &act, nullptr);
	ec_setup_segv_handler("kopano-server", PROJECT_VERSION);

	// ignore ignorable signals that might stop the server during database upgrade
	// all these signals will be reset after the database upgrade part.
	m_bDatabaseUpdateIgnoreSignals = true;
	// add a lock file to disable the /etc/init.d scripts
	auto tmplock = open(upgrade_lock_file, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (tmplock == -1)
		ec_log_warn("WARNING: Unable to place upgrade lockfile: %s", strerror(errno));

	// perform database upgrade .. may take a very long time
	er = lpDatabaseFactory->UpdateDatabase(m_bForceDatabaseUpdate, dbError);
	// remove lock file
	if (tmplock != -1) {
		if (unlink(upgrade_lock_file) == -1)
			ec_log_warn("WARNING: Unable to delete upgrade lockfile (%s): %s", upgrade_lock_file, strerror(errno));
		close(tmplock);
	}

	if(er == KCERR_INVALID_VERSION) {
		ec_log_warn("WARNING: %s", dbError.c_str());
		if (!m_bIgnoreDatabaseVersionConflict) {
			ec_log_warn("   You can force the server to start with --ignore-database-version-conflict");
			ec_log_warn("   Warning, you can lose data! If you don't know what you're doing, you shouldn't be using this option!");
			return retval;
		}
	}else if(er != erSuccess) {
		if (er != KCERR_USER_CANCEL)
			ec_log_err("Can't update the database: %s", dbError.c_str());
		return retval;
	}
	er = lpDatabase->InitializeDBState();
	if(er != erSuccess) {
		ec_log_err("Can't initialize database settings");
		return retval;
	}
	m_bDatabaseUpdateIgnoreSignals = false;
	if(searchfolder_restart_required) {
		ec_log_warn("Update requires searchresult folders to be rebuilt. This may take some time. You can restart this process with the --restart-searches option");
		restart_searches = 1;
	}

	// check database tables for requested engine
	er = check_database_engine(lpDatabase.get());
	if (er != erSuccess)
		return retval;
	// check attachment database started with, and maybe reject startup
	er = check_database_attachments(lpDatabase.get());
	if (er != erSuccess)
		return retval;
	// check you can write into the file attachment storage
	er = check_attachment_storage_permissions();
	if (er != erSuccess)
		return retval;
	// check upgrade problem with wrong sequence in tproperties table primary key
	er = check_database_tproperties_key(lpDatabase.get());
	if (er != erSuccess)
		return retval;
	// check whether the thread_stack is large enough.
	er = check_database_thread_stack(lpDatabase.get());
	if (er != erSuccess)
		return retval;
	//Init the main system, now you can use the values like session manager
	// This also starts several threads, like SessionCleaner, NotificationThread and TPropsPurge.
	er = kopano_init(g_lpConfig, g_lpAudit, stats, hosted, distributed);
	if (er != erSuccess) { // create SessionManager
		ec_log_err("Unable to initialize kopano session manager");
		return retval;
	}
	// check for conflicting settings in local config and LDAP, after kopano_init since this needs the sessionmanager.
	er = check_server_configuration();
	if (er != erSuccess)
		return retval;
	// Load search folders from disk
	er = g_lpSessionManager->GetSearchFolders()->LoadSearchFolders();
	if (er != erSuccess) {
		ec_log_err("Unable to load searchfolders");
		return retval;
	}
	if (restart_searches) // restart_searches if specified
		g_lpSessionManager->GetSearchFolders()->RestartSearches();

	// Create scheduler system
	ECScheduler sch;
	// Add a task on the scheduler
	sch.AddSchedule(SCHEDULE_HOUR, 00, &SoftDeleteRemover, &g_Quit);
	sch.AddSchedule(SCHEDULE_HOUR, 15, &CleanupSyncsTable);
	sch.AddSchedule(SCHEDULE_HOUR, 16, &CleanupSyncedMessagesTable);

	// high loglevel to always see when server is started.
	ec_log_notice("Startup succeeded on pid %d", getpid() );
	stats->SetTime(SCN_SERVER_STARTTIME, time(nullptr));

	// Enter main accept loop
	auto scleanup = make_scope_success([&]() { stats->stop(); });
	stats->start();
	retval = 0;
	while(!g_Quit) {
		if (sv_sighup_flag)
			sv_sighup_sync();
		// Select on the sockets
		er = g_lpSoapServerConn->MainLoop();
		if(er == KCERR_NETWORK_ERROR) {
			retval = -1;
			break; // Exit loop
		} else if (er != erSuccess) {
			continue;
		}
	}
	return retval;
}
