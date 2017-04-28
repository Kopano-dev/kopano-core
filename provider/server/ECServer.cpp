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

#include "config.h"
#include <kopano/platform.h>
#include <kopano/ecversion.h>
#include <kopano/stringutil.h>

#include "soapH.h"

#include "ECDatabase.h"
#include "ECDatabaseFactory.h"
#include "ECDatabaseUtils.h"
#include <kopano/ECLogger.h>

#include <kopano/ECConfig.h>
#include "ECPluginFactory.h"
#include "ECNotificationManager.h"
#include "ECSessionManager.h"
#include "ECStatsCollector.h"
#include "ECStatsTables.h"
#include <climits>
#include <csignal>
#include <kopano/UnixUtil.h>
#include <pwd.h>
#include <sys/stat.h>
#include <kopano/ECScheduler.h>
#include <kopano/kcodes.h>
#include <kopano/my_getopt.h>
#include "cmd.hpp"

#include "ECServerEntrypoint.h"
#include "SSLUtil.h"

#include "ECSoapServerConnection.h"
#include <libintl.h>
#include <map>
#include <kopano/tstring.h>
#include <kopano/charset/convstring.h>
#ifdef ZCP_USES_ICU
#include <unicode/uclean.h>
#endif

#include "TmpPath.h"

// The following value is based on:
// http://dev.mysql.com/doc/refman/5.0/en/server-system-variables.html#sysvar_thread_stack
// Since the remote MySQL server can be 32 or 64 bit we'll just go with the value specified
// for 64bit architectures.
// We could use the 'version_compile_machine' variable, but I'm not sure if 32bit versions
// will ever be build on 64bit machines and what that variable does. Plus we would need a
// list of all possible 32bit architectures because if the architecture is unknown we'll
// have to go with the safe value which is for 64bit.
#define MYSQL_MIN_THREAD_STACK (256*1024)

const char upgrade_lock_file[] = "/tmp/kopano-upgrade-lock";

extern ECSessionManager*    g_lpSessionManager;

// scheduled functions
void* SoftDeleteRemover(void* lpTmpMain);
void* CleanupSyncsTable(void* lpTmpMain);
void* CleanupChangesTable(void* lpTmpMain);
void *CleanupSyncedMessagesTable(void *lpTmpMain);
// Reports information on the current state of the license
void* ReportLicense(void *);

static int running_server(char *, const char *, int, char **, int, char **);

int					g_Quit = 0;
int					daemonize = 1;
int					restart_searches = 0;
int					searchfolder_restart_required = 0; //HACK for rebuild the searchfolders with an upgrade
bool				m_bIgnoreDatabaseVersionConflict = false;
bool				m_bIgnoreAttachmentStorageConflict = false;
bool				m_bIgnoreDistributedKopanoConflict = false;
bool				m_bForceDatabaseUpdate = false;
bool				m_bIgnoreUnknownConfigOptions = false;
bool				m_bIgnoreDbThreadStackSize = false;
pthread_t			mainthread;

ECConfig*			g_lpConfig = NULL;
ECLogger*			g_lpLogger = NULL;
ECLogger*			g_lpAudit = NULL;
ECScheduler*		g_lpScheduler = NULL;
ECSoapServerConnection*	g_lpSoapServerConn = NULL;

pthread_t	signal_thread;
sigset_t	signal_mask;
bool 		m_bNPTL = true;
bool m_bDatabaseUpdateIgnoreSignals = false;

// This is the callback function for libserver/* so that it can notify that a delayed soap
// request has been handled.
void kopano_notify_done(struct soap *soap)
{
    g_lpSoapServerConn->NotifyDone(soap);
}

// Called from ECStatsTables to get server stats
void kopano_get_server_stats(unsigned int *lpulQueueLength,
    double *lpdblAge, unsigned int *lpulThreadCount,
    unsigned int *lpulIdleThreads)
{
    g_lpSoapServerConn->GetStats(lpulQueueLength, lpdblAge, lpulThreadCount, lpulIdleThreads);
}

/** 
 * actual signal handler, direct entry point if only linuxthreads is available.
 * 
 * @param[in] sig signal received
 */
static void process_signal(int sig)
{
	ZLOG_AUDIT(g_lpAudit, "server signalled sig=%d", sig);

	if (!m_bNPTL)
	{
		// Win32 has unix semantics and therefore requires us to reset the signal handler.
		signal(sig, process_signal);
		if(pthread_equal(pthread_self(), mainthread)==0)
			return;					// soap threads do not handle this signal
	}

	if (m_bDatabaseUpdateIgnoreSignals) {
		g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "WARNING: Database upgrade is taking place.");
		g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "  Please be patient, and do not try to kill the server process.");
		g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "  It may leave your database in an inconsistent state.");
		return;
	}	

	if(g_Quit == 1)
		return; // already in exit state!

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		if(g_lpLogger)
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Shutting down");
	
		if (g_lpSoapServerConn)
			g_lpSoapServerConn->ShutDown();

		// unblock the signals so the server can exit
		sigprocmask(SIG_UNBLOCK, &signal_mask, NULL);
		signal(SIGPIPE, SIG_IGN);
		signal(SIGTERM, SIG_IGN);
		signal(SIGINT, SIG_IGN);

		g_Quit = 1;

		break;
	case SIGHUP:
		{
			// g_lpSessionManager only present when kopano_init is called (last init function), signals are initialized much earlier
			if (g_lpSessionManager == NULL)
				return;

			if (!g_lpConfig->ReloadSettings())
				g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to reload configuration file, continuing with current settings.");
			if (g_lpConfig->HasErrors())
				g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to reload configuration file");

			g_lpSessionManager->GetPluginFactory()->SignalPlugins(sig);

			const char *ll = g_lpConfig->GetSetting("log_level");
			int new_ll = ll ? strtol(ll, NULL, 0) : EC_LOGLEVEL_WARNING;
			g_lpLogger->SetLoglevel(new_ll);
			g_lpLogger->Reset();
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Log connection was reset");

			if (g_lpAudit) {
				ll = g_lpConfig->GetSetting("audit_log_level");
				new_ll = ll ? strtol(ll, NULL, 0) : 1;
				g_lpAudit->SetLoglevel(new_ll);
				g_lpAudit->Reset();
			}

			g_lpStatsCollector->SetTime(SCN_SERVER_LAST_CONFIGRELOAD, time(NULL));
			g_lpSoapServerConn->DoHUP();
		}
		break;
	default:
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Unknown signal %d received", sig);
		break;
	}
}

/** 
 * Signal handler thread.
 *
 * @return NULL 
 */
static void *signal_handler(void *)
{
	int sig;

	g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Signal thread started");

	// already blocking signals

	while (!g_Quit && sigwait(&signal_mask, &sig) == 0)
	{
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Received signal %d", sig);
		process_signal(sig);
	}
	
	g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Signal thread done");
	return NULL;
}

// SIGSEGV catcher
#include <execinfo.h>

static void sigsegv(int signr, siginfo_t *si, void *uc)
{
	generic_sigsegv_handler(g_lpLogger, "Server",
		PROJECT_VERSION_SERVER_STR, signr, si, uc);
}

static ECRESULT check_database_innodb(ECDatabase *lpDatabase)
{
	ECRESULT er = erSuccess;
#ifndef EMBEDDED_MYSQL
	string strQuery;
	DB_RESULT lpResult = NULL;
	DB_ROW lpRow = NULL;

	// Only supported from mysql 5.0
	er = lpDatabase->DoSelect("SHOW TABLE STATUS WHERE engine != 'InnoDB'", &lpResult);
	if (er != erSuccess)
		goto exit;

	while( (lpRow = lpDatabase->FetchRow(lpResult)) ) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Database table '%s' not in InnoDB format: %s", lpRow[0] ? lpRow[0] : "unknown table", lpRow[1] ? lpRow[1] : "unknown engine");
		er = KCERR_DATABASE_ERROR;
	}

	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Your database was incorrectly created. Please upgrade all tables to the InnoDB format using this query:");
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "  ALTER TABLE <table name> ENGINE='InnoDB';");
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "This process may take a very long time, depending on the size of your database.");
	}
	
exit:
	if (lpResult)
		lpDatabase->FreeResult(lpResult);
#endif
	return er;
}

static ECRESULT check_database_attachments(ECDatabase *lpDatabase)
{
	ECRESULT er = erSuccess;
	string strQuery;
	DB_RESULT lpResult = NULL;
	DB_ROW lpRow = NULL;

	er = lpDatabase->DoSelect("SELECT value FROM settings WHERE name = 'attachment_storage'", &lpResult);
	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to read from database");
		goto exit;
	}

	lpRow = lpDatabase->FetchRow(lpResult);

	if (lpRow && lpRow[0]) {
		// check if the mode is the same as last time
		if (strcmp(lpRow[0], g_lpConfig->GetSetting("attachment_storage")) != 0) {
			if (!m_bIgnoreAttachmentStorageConflict) {
				g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Attachments are stored with option '%s', but '%s' is selected.", lpRow[0], g_lpConfig->GetSetting("attachment_storage"));
				er = KCERR_DATABASE_ERROR;
				goto exit;
			} else {
				g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Ignoring attachment storing conflict as requested. Attachments are now stored with option '%s'", g_lpConfig->GetSetting("attachment_storage"));
			}
		}
	}

	// first time we start, set the database to the selected mode
	strQuery = (string)"REPLACE INTO settings VALUES ('attachment_storage', '" + g_lpConfig->GetSetting("attachment_storage") + "')";

	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to update database settings");
		goto exit;
	}

	// Create attachment directories
	if (strcmp(g_lpConfig->GetSetting("attachment_storage"), "files") == 0)
		// These values are hard coded .. if they change, the hash algorithm will fail, and you'll be FUCKED.
		for (int i = 0; i < ATTACH_PATHDEPTH_LEVEL1; ++i)
			for (int j = 0; j < ATTACH_PATHDEPTH_LEVEL2; ++j) {
				string path = (string)g_lpConfig->GetSetting("attachment_path") + PATH_SEPARATOR + stringify(i) + PATH_SEPARATOR + stringify(j);
				CreatePath(path.c_str());
			}

exit:
	if (lpResult)
		lpDatabase->FreeResult(lpResult);

	return er;
}

static ECRESULT check_distributed_kopano(ECDatabase *lpDatabase)
{
	ECRESULT er = erSuccess;
	string strQuery;
	DB_RESULT lpResult = NULL;
	DB_ROW lpRow = NULL;
	bool bConfigEnabled = parseBool(g_lpConfig->GetSetting("enable_distributed_kopano"));

	er = lpDatabase->DoSelect("SELECT value FROM settings WHERE name = 'lock_distributed_kopano'", &lpResult);
	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to read from database");
		goto exit;
	}

	lpRow = lpDatabase->FetchRow(lpResult);

	// If no value is found in the database any setting is valid
	if (lpRow == NULL || lpRow[0] == NULL) 
		goto exit;

	// If any value is found, distributed is not allowed. The value specifies the reason.
	if (bConfigEnabled) {
		if (!m_bIgnoreDistributedKopanoConflict) {
			g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Multiserver mode is locked, reason: '%s'. Contact Kopano for support.", lpRow[0]);
			er = KCERR_DATABASE_ERROR;
			goto exit;
		} else {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Ignoring multiserver mode lock as requested.");
		}
	}

exit:
	if (lpResult)
		lpDatabase->FreeResult(lpResult);

	return er;
}

static ECRESULT check_attachment_storage_permissions(void)
{
	ECRESULT er = erSuccess;

	FILE *tmpfile = NULL;
	string strtestpath;

	if (strcmp(g_lpConfig->GetSetting("attachment_storage"), "files") == 0) {
		strtestpath = g_lpConfig->GetSetting("attachment_path");
		strtestpath += "/testfile";

		tmpfile = fopen(strtestpath.c_str(), "w");
		if (!tmpfile) {
			 g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to write attachments to the directory '%s' - %s. Please check the directory and sub directories.",  g_lpConfig->GetSetting("attachment_path"), strerror(errno));
			 er = KCERR_NO_ACCESS;
			 goto exit;
		}
	}
exit:
	if (tmpfile) {
		fclose(tmpfile);
		unlink(strtestpath.c_str());
	}
	return er;
}

static ECRESULT check_database_tproperties_key(ECDatabase *lpDatabase)
{
	ECRESULT er = erSuccess;
	string strQuery, strTable;
	string::size_type start, end;
	DB_RESULT lpResult = NULL;
	DB_ROW lpRow = NULL;

	strQuery = "SHOW CREATE TABLE `tproperties`";
	er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to read from database");
		goto exit;
	}

	er = KCERR_DATABASE_ERROR;

	lpRow = lpDatabase->FetchRow(lpResult);
	if (!lpRow || !lpRow[1]) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "No tproperties table definition found");
		goto exit;
	}

	strTable = lpRow[1];

	start = strTable.find("PRIMARY KEY");
	if (start == string::npos) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "No primary key found in tproperties table");
		goto exit;
	}

	end = strTable.find(")", start);
	if (end == string::npos) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "No end of primary key found in tproperties table");
		goto exit;
	}

	strTable.erase(end, string::npos);
	strTable.erase(0, start);

	// correct:
	// PRIMARY KEY (`folderid`,`tag`,`hierarchyid`,`type`),
	// incorrect:
	// PRIMARY KEY `ht` (`folderid`,`tag`,`type`,`hierarchyid`)
	// `ht` part seems to be optional
	start = strTable.find_first_of(',');
	if (start != string::npos)
		start = strTable.find_first_of(',', start+1);
	if (start == string::npos) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Primary key of tproperties table incorrect, trying: %s", strTable.c_str());
		goto exit;
	}

	// start+1:end == `type`,`hierarchyid`
	strTable.erase(0, start+1);

	// if not correct...
	if (strTable.compare("`hierarchyid`,`type`") != 0) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "**** WARNING: Installation is not optimal! ****");
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "  The primary key of the tproperties table is incorrect.");
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "  Since updating the primary key on a large table is slow, the server will not automatically update this for you.");
	}

	er = erSuccess;

exit:

	if (lpResult)
		lpDatabase->FreeResult(lpResult);

	return er;
}

static ECRESULT check_database_thread_stack(ECDatabase *lpDatabase)
{
	ECRESULT er = erSuccess;
	string strQuery;
	DB_RESULT lpResult = NULL;
	DB_ROW lpRow = NULL;
	unsigned ulThreadStack = 0;

	// only required when procedures are used
	if (!parseBool(g_lpConfig->GetSetting("enable_sql_procedures")))
		goto exit;

	strQuery = "SHOW VARIABLES LIKE 'thread_stack'";
	er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to read from database");
		goto exit;
	}

	lpRow = lpDatabase->FetchRow(lpResult);
	if (!lpRow || !lpRow[1]) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "No thread_stack variable returned");
		goto exit;
	}

	ulThreadStack = atoui(lpRow[1]);
	if (ulThreadStack < MYSQL_MIN_THREAD_STACK) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "MySQL thread_stack is set to %u, which is too small", ulThreadStack);
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Please set thread_stack to %uK or higher in your MySQL configuration", MYSQL_MIN_THREAD_STACK / 1024);
		if (m_bIgnoreDbThreadStackSize)
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "MySQL thread_stack setting ignored. Please reconsider when 'Thread stack overrun' errors appear in the log.");
		else
			er = KCERR_DATABASE_ERROR;
	}

exit:
	if (lpResult)
		lpDatabase->FreeResult(lpResult);

	return er;
}

/**
 * Checks the server_hostname value of the configuration, and if
 * empty, gets the current FQDN through DNS lookups, and updates the
 * server_hostname value in the config object.
 */
static ECRESULT check_server_fqdn(void)
{
	ECRESULT er = erSuccess;
	int rc;
	char hostname[256] = {0};
	struct sockaddr_in saddr = {0};
	struct addrinfo hints = {0};
	struct addrinfo *aiResult = NULL;
	const char *option;

	// If admin has set the option, we're not using DNS to check the name
	option = g_lpConfig->GetSetting("server_hostname");
	if (option && option[0] != '\0')
		goto exit;
	
	rc = gethostname(hostname, sizeof(hostname));
	if (rc != 0) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	// if we exit hereon after, hostname will always contain a correct hostname, which we can set in the config.

	rc = getaddrinfo(hostname, NULL, &hints, &aiResult);
	if (rc != 0) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	// no need to set other contents of saddr struct, we're just intrested in the DNS lookup.
	saddr = *((sockaddr_in*)aiResult->ai_addr);

	// Name lookup is required, so set that flag
	rc = getnameinfo((const sockaddr*)&saddr, sizeof(saddr), hostname, sizeof(hostname), NULL, 0, NI_NAMEREQD);
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
static ECRESULT check_server_configuration(void)
{
	ECRESULT		er = erSuccess;
	bool			bHaveErrors = false;
	bool			bCheck = false;
	std::string		strServerName;
	ECSession		*lpecSession = NULL;
	serverdetails_t	sServerDetails;
	unsigned		ulPort = 0;

	// Upgrade 'enable_sso_ntlmauth' to 'enable_sso'
	bCheck = parseBool(g_lpConfig->GetSetting("enable_sso_ntlmauth"));
	if (bCheck)
		g_lpConfig->AddSetting("enable_sso", g_lpConfig->GetSetting("enable_sso_ntlmauth"));

	// Find FQDN if Kerberos is enabled (remove check if we're using 'server_hostname' for other purposes)
	bCheck = parseBool(g_lpConfig->GetSetting("enable_sso"));
	if (bCheck && check_server_fqdn() != erSuccess)
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "WARNING: Unable to find FQDN, please specify in 'server_hostname'. Now using '%s'.", g_lpConfig->GetSetting("server_hostname"));

	// all other checks are only required for multi-server environments
	bCheck = parseBool(g_lpConfig->GetSetting("enable_distributed_kopano"));
	if (!bCheck)
		goto exit;
	
	strServerName = g_lpConfig->GetSetting("server_name");
	if (strServerName.empty()) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "ERROR: No 'server_name' specified while operating in multiserver mode.");
		er = KCERR_INVALID_PARAMETER;
		// unable to check any other server details if we have no name, skip other tests
		goto exit;
	}

	er = g_lpSessionManager->CreateSessionInternal(&lpecSession);
	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Internal error 0x%08x while checking distributed configuration", er);
		goto exit;
	}

	lpecSession->Lock();
	
	er = lpecSession->GetUserManagement()->GetServerDetails(strServerName, &sServerDetails);
	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "ERROR: Unable to find server information on LDAP for '%s', error 0x%08X. Check your server name.", strServerName.c_str(), er);
		// unable to check anything else if we have no details, skip other tests
		goto exit;
	}
		
	// Check the various connection parameters for consistency
	if (parseBool(g_lpConfig->GetSetting("server_pipe_enabled")) == true) {
		if (sServerDetails.GetFilePath().empty()) {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: 'server_pipe_enabled' is set, but LDAP returns nothing");
			bHaveErrors = true;
		}
		if (sServerDetails.GetFilePath().compare((std::string)"file://" + g_lpConfig->GetSetting("server_pipe_name")) != 0) {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: 'server_pipe_name' is set to '%s', but LDAP returns '%s'", g_lpConfig->GetSetting("server_pipe_name"), sServerDetails.GetFilePath().c_str());
			bHaveErrors = true;
		}
	} else if (!sServerDetails.GetFilePath().empty()) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: 'server_pipe_enabled' is unset, but LDAP returns '%s'", sServerDetails.GetFilePath().c_str());
		bHaveErrors = true;
	}
	
	if (parseBool(g_lpConfig->GetSetting("server_tcp_enabled")) == true) {
		if (sServerDetails.GetHttpPath().empty()) {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: 'server_tcp_enabled' is set, but LDAP returns nothing");
			bHaveErrors = true;
		}
		
		ulPort = atoui(g_lpConfig->GetSetting("server_tcp_port"));
		if (sServerDetails.GetHttpPort() != ulPort) {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: 'server_tcp_port' is set to '%u', but LDAP returns '%u'", ulPort, sServerDetails.GetHttpPort());
			bHaveErrors = true;
		}
	} else if (!sServerDetails.GetHttpPath().empty()) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: 'server_tcp_enabled' is unset, but LDAP returns '%s'", sServerDetails.GetHttpPath().c_str());
		bHaveErrors = true;
	}
	
	if (parseBool(g_lpConfig->GetSetting("server_ssl_enabled")) == true) {
		if (sServerDetails.GetSslPath().empty()) {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: 'server_ssl_enabled' is set, but LDAP returns nothing");
			bHaveErrors = true;
		}
		
		ulPort = atoui(g_lpConfig->GetSetting("server_ssl_port"));
		if (sServerDetails.GetSslPort() != ulPort) {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: 'server_ssl_port' is set to '%u', but LDAP returns '%u'", ulPort, sServerDetails.GetSslPort());
			bHaveErrors = true;
		}
	} else if (!sServerDetails.GetSslPath().empty()) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: 'server_ssl_enabled' is unset, but LDAP returns '%s'", sServerDetails.GetSslPath().c_str());
		bHaveErrors = true;
	}	
	
exit:
	if (lpecSession) {
		lpecSession->Unlock();
		g_lpSessionManager->RemoveSessionInternal(lpecSession);
	}

	// we could return an error when bHaveErrors is set, but we currently find this not fatal as a sysadmin might be smarter than us.
	if (bHaveErrors)
 		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: Inconsistencies detected between local and LDAP based configuration.");

	// we do return er, since if that is set GetServerDetails() does not work and that is quite vital to work in distributed systems.
	return er;
}

int main(int argc, char* argv[])
{
	int nReturn = 0;
	const char *config = ECConfig::GetDefaultPath("server.cfg");
	const char *default_config = config;

	enum {
		OPT_HELP = UCHAR_MAX + 1,
		OPT_CONFIG,
		OPT_RESTART_SEARCHES,
		OPT_IGNORE_DATABASE_VERSION_CONFLICT,
		OPT_IGNORE_ATTACHMENT_STORAGE_CONFLICT,
		OPT_OVERRIDE_DISTRIBUTED_LOCK,
		OPT_FORCE_DATABASE_UPGRADE,
		OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS,
		OPT_IGNORE_DB_THREAD_STACK_SIZE
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
		{ NULL, 0, NULL, 0 }
	};

	//FIXME: By start as service current path is the system32 dir ??? <-- use '-c' option in service to be sure?
	// check for configfile
	while (1) {
		int c = my_getopt_long_permissive(argc, argv, "c:VFiuR", long_options, NULL);

		if(c == -1)
			break;

		switch (c) {
		case 'c':
		case OPT_CONFIG:
			config = optarg;
			break;
		case OPT_HELP:
			cout << "Kopano " PROJECT_VERSION_SERVER_STR " ";
#ifdef EMBEDDED_MYSQL
			cout << "with embedded SQL server";
#endif
			cout << endl;
			cout << argv[0] << " [options...]" << endl;
			cout << "  -c --config=FILE                           Set new config file location. Default: " << default_config << endl;
			cout << "  -F                                         Do not start in the background." << endl;
			cout << "  -V                                         Print version info." << endl;
			cout << "  -R --restart-searches                      Rebuild searchfolders." << endl;
			cout << "     --ignore-database-version-conflict      Start even if database version conflict with server version" << endl;
			cout << "     --ignore-attachment-storage-conflict    Start even if the attachment_storage config option changed" << endl;
			cout << "     --override-multiserver-lock             Start in multiserver mode even if multiserver mode is locked" << endl;
			cout << "     --force-database-upgrade                Start upgrade from 6.x database and continue running if upgrade is complete" << endl;
			cout << "     --ignore-db-thread-stack-size           Start even if the thread_stack setting for MySQL is too low" << endl;
			return 0;
		case 'V':
			cout << "Product version:\t" <<  PROJECT_VERSION_SERVER_STR << endl
				<< "File version:\t\t" << PROJECT_SVN_REV_STR << endl;
			return 0;
		case 'F':
			daemonize = 0;
			break;
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
		};
	}
	nReturn = running_server(argv[0], config, argc, argv,
	          argc - optind, &argv[optind]);
	return nReturn;
}

#define KOPANO_SERVER_PIPE "/var/run/kopano/server.sock"
#define KOPANO_SERVER_PRIO "/var/run/kopano/prio.sock"

static void InitBindTextDomain(void)
{
	// Set gettext codeset, used for generated folder name translations
	bind_textdomain_codeset("kopano", "UTF-8");
}

static int running_server(char *szName, const char *szConfig,
    int argc, char **argv, int trim_argc, char **trim_argv)
{
	int retval = -1;
	ECRESULT		er = erSuccess;
	ECDatabaseFactory*	lpDatabaseFactory = NULL;
	ECDatabase*		lpDatabase = NULL;
	std::string		dbError;

	// Connections
	bool			bSSLEnabled = false;
	bool			bPipeEnabled = false;
	bool			bTCPEnabled = false;
	bool			hosted = false;
	bool			distributed = false;

	// SIGSEGV backtrace support
	stack_t st = {0};
	struct sigaction act = {{0}};
	int tmplock = -1;
	struct stat dir = {0};
	struct passwd *runasUser = NULL;

	const configsetting_t lpDefaults[] = {
		// Aliases
		{ "server_port",				"server_tcp_port", CONFIGSETTING_ALIAS },
		{ "unix_socket",				"server_pipe_name", CONFIGSETTING_ALIAS },
		// Default settings
		{ "enable_hosted_kopano",		"false" },	// Will only be checked when license allows hosted
		{ "enable_distributed_kopano",	"false" },
		{ "server_name",				"" }, // used by multi-server
		{ "server_hostname",			"" }, // used by kerberos, if empty, gethostbyname is used
		// server connections
		{ "server_bind",				"" },
		{ "server_tcp_port",			"236" },
		{ "server_tcp_enabled",			"yes" },
		{ "server_pipe_enabled",		"yes" },
		{ "server_pipe_name",			KOPANO_SERVER_PIPE },
		{ "server_pipe_priority",		KOPANO_SERVER_PRIO },
		{ "server_recv_timeout",		"5", CONFIGSETTING_RELOADABLE },	// timeout before reading next XML request
		{ "server_read_timeout",		"60", CONFIGSETTING_RELOADABLE }, // timeout during reading of XML request
		{ "server_send_timeout",		"60", CONFIGSETTING_RELOADABLE },
		{ "server_max_keep_alive_requests",	"100" },
		{ "thread_stacksize",			"512" },
		{ "allow_local_users",			"yes", CONFIGSETTING_RELOADABLE },			// allow any user connect through the unix socket
		{ "local_admin_users",			"root", CONFIGSETTING_RELOADABLE },			// this local user is admin
		{ "run_as_user",			"kopano" }, // drop root privileges, and run as this user/group
		{ "run_as_group",			"kopano" },
		{ "pid_file",					"/var/run/kopano/server.pid" },
		{ "running_path",			"/var/lib/kopano" },
		{"allocator_library", "libtcmalloc_minimal.so.4"},
		{ "coredump_enabled",			"yes" },

		{ "license_path",			"/etc/kopano/license", CONFIGSETTING_UNUSED },
		{ "license_socket",			"/var/run/kopano/licensed.sock" },
		{ "license_timeout", 		"10", CONFIGSETTING_RELOADABLE},
		{ "system_email_address",		"postmaster@localhost", CONFIGSETTING_RELOADABLE },

		{ "server_ssl_enabled",			"no" },
		{ "server_ssl_port",			"237" },
		{ "server_ssl_key_file",		"/etc/kopano/ssl/server.pem" },
		{ "server_ssl_key_pass",		"server",	CONFIGSETTING_EXACT },
		{ "server_ssl_ca_file",			"/etc/kopano/ssl/cacert.pem" },
		{ "server_ssl_ca_path",			"" },
		{ "server_ssl_protocols",		"!SSLv2" },
		{ "server_ssl_ciphers",			"ALL:!LOW:!SSLv2:!EXP:!aNULL" },
		{ "server_ssl_prefer_server_ciphers",	"no" },
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
#if defined(EMBEDDED_MYSQL)
		{ "mysql_database_path",		"/var/kopano/data" },
		{ "mysql_config_file",			"/etc/kopano/my.cnf" },
#endif
		{ "attachment_storage",			"database" },
#ifdef HAVE_LIBS3_H
		{"attachment_s3_hostname", ""},
		{"attachment_s3_protocol", "https"},
		{"attachment_s3_uristyle", "virtualhost"},
		{"attachment_s3_accesskeyid", ""},
		{"attachment_s3_secretaccesskey", ""},
		{"attachment_s3_bucketname", ""},
#endif
		{ "attachment_path",			"Kopano Data" },
		{ "attachment_compression",		"6" },

		// Log options
		{ "log_method",					"file" },
		{ "log_file",					"-" },
		{ "log_level",					"3", CONFIGSETTING_RELOADABLE },
		{ "log_timestamp",				"1" },
		{ "log_buffer_size", "0" },
		// security log options
		{ "audit_log_enabled",			"no" },
		{ "audit_log_method",			"syslog" },
		{ "audit_log_file",				"-" },
		{ "audit_log_level",			"1", CONFIGSETTING_RELOADABLE },
		{ "audit_log_timestamp",		"0" },

		// user plugin
		{ "plugin_path",				PKGLIBDIR },
		{ "user_plugin",				"db" },
		{ "user_plugin_config",			"/etc/kopano/ldap.cfg" },
		{ "createuser_script",			"/etc/kopano/userscripts/createuser", CONFIGSETTING_RELOADABLE },
		{ "deleteuser_script",			"/etc/kopano/userscripts/deleteuser", CONFIGSETTING_RELOADABLE },
		{ "creategroup_script",			"/etc/kopano/userscripts/creategroup", CONFIGSETTING_RELOADABLE },
		{ "deletegroup_script",			"/etc/kopano/userscripts/deletegroup", CONFIGSETTING_RELOADABLE},
		{ "createcompany_script",		"/etc/kopano/userscripts/createcompany", CONFIGSETTING_RELOADABLE },
		{ "deletecompany_script",		"/etc/kopano/userscripts/deletecompany", CONFIGSETTING_RELOADABLE },
		{ "user_safe_mode",				"no", CONFIGSETTING_RELOADABLE },

		// Storename format
		{ "storename_format",			"%f" },
		{ "loginname_format",			"%u" },

		// internal server contols
		{ "softdelete_lifetime",		"0" },							// time expressed in days, 0 == never delete anything
		{ "cache_cell_size",			"16M", CONFIGSETTING_SIZE },	// default 16 Mb, default in config 256M
		{ "cache_object_size",		"16M", CONFIGSETTING_SIZE },
		{ "cache_indexedobject_size",	"32M", CONFIGSETTING_SIZE },
		{ "cache_quota_size",			"1M", CONFIGSETTING_SIZE },		// 1Mb
		{ "cache_quota_lifetime",		"1" },							// 1 minute
		{ "cache_user_size",			"1M", CONFIGSETTING_SIZE },		// 48 bytes per struct, can hold 21k+ users, allocated 2x (user and ueid cache)
		{ "cache_userdetails_size",		"25M", CONFIGSETTING_SIZE },		// 120 bytes per struct, can hold 21k+ users (was 2.5Mb, no float in size)
		{ "cache_userdetails_lifetime", "5" },							// 5 minutes
		{ "cache_acl_size",				"1M", CONFIGSETTING_SIZE },		// 1Mb, acl table cache
		{ "cache_store_size",			"1M", CONFIGSETTING_SIZE },		// 1Mb, store table cache (storeid, storeguid), 40 bytes
		{ "cache_server_size",			"1M", CONFIGSETTING_SIZE },		// 1Mb
		{ "cache_server_lifetime",		"30" },							// 30 minutes
		// default no quota's. Note: quota values are in Mb, and thus have no size flag.
		{ "quota_warn",				"0", CONFIGSETTING_RELOADABLE },
		{ "quota_soft",				"0", CONFIGSETTING_RELOADABLE },
		{ "quota_hard",				"0", CONFIGSETTING_RELOADABLE },
		{ "companyquota_warn",		"0", CONFIGSETTING_RELOADABLE },
		{ "companyquota_soft",		"0", CONFIGSETTING_UNUSED },
		{ "companyquota_hard",		"0", CONFIGSETTING_UNUSED },
		{ "session_timeout",		"300", CONFIGSETTING_RELOADABLE },		// 5 minutes
		{ "sync_lifetime",			"365", CONFIGSETTING_RELOADABLE },		// 1 year
		{ "sync_log_all_changes",	"yes", CONFIGSETTING_RELOADABLE },	// Log All ICS changes
		{ "auth_method",			"plugin", CONFIGSETTING_RELOADABLE },		// plugin (default), pam, kerberos
		{ "pam_service",			"passwd", CONFIGSETTING_RELOADABLE },		// pam service, found in /etc/pam.d/
		{ "enable_sso_ntlmauth",	"no", CONFIGSETTING_UNUSED },			// default disables ntlm_auth, so we don't log errors on useless things
		{ "enable_sso",				"no", CONFIGSETTING_RELOADABLE },			// autodetect between Kerberos and NTLM
		{ "session_ip_check",		"yes", CONFIGSETTING_RELOADABLE },			// check session id comes from same ip address (or not)
		{ "hide_everyone",			"yes", CONFIGSETTING_RELOADABLE },			// whether internal group Everyone should be removed for users
		{ "hide_system",			"yes", CONFIGSETTING_RELOADABLE },			// whether internal user SYSTEM should be removed for users
		{ "enable_gab",				"yes", CONFIGSETTING_RELOADABLE },			// whether the GAB is enabled
        { "enable_enhanced_ics",    "yes", CONFIGSETTING_RELOADABLE },			// (dis)allow enhanced ICS operations (stream and notifications)
        { "enable_sql_procedures",  "no" },			// (dis)allow SQL procedures (requires mysql config stack adjustment), not reloadable because in the middle of the streaming flip
		
		{ "report_path",			"/etc/kopano/report", CONFIGSETTING_RELOADABLE },
		{ "report_ca_path",			"/etc/kopano/report-ca", CONFIGSETTING_RELOADABLE },
		
		{ "cache_sortkey_size",		"0", CONFIGSETTING_UNUSED }, // Option not support, only for backward compatibility of all configurations under the 6.20

		{ "client_update_enabled",	"no" }, /* OBSOLETE */
		{ "client_update_log_level", "1", CONFIGSETTING_RELOADABLE }, /* OBSOLETE */
		{ "client_update_path",		"/var/lib/kopano/client", CONFIGSETTING_RELOADABLE }, /* OBSOLETE */
		{ "client_update_log_path",	"/var/log/kopano/autoupdate", CONFIGSETTING_RELOADABLE }, /* OBSOLETE */
		{ "index_services_enabled", "", CONFIGSETTING_UNUSED },
		{ "index_services_path",    "", CONFIGSETTING_UNUSED },
		{ "index_services_search_timeout", "", CONFIGSETTING_UNUSED },
		{ "search_enabled",			"yes", CONFIGSETTING_RELOADABLE }, 
		{ "search_socket",			"file:///var/run/kopano/search.sock", CONFIGSETTING_RELOADABLE },
		{ "search_timeout",			"10", CONFIGSETTING_RELOADABLE },

		{ "threads",				"8", CONFIGSETTING_RELOADABLE },
		{ "watchdog_max_age",		"500", CONFIGSETTING_RELOADABLE },
		{ "watchdog_frequency",		"1", CONFIGSETTING_RELOADABLE },
        
		{ "folder_max_items",		"1000000", CONFIGSETTING_RELOADABLE },
		{ "default_sort_locale_id",		"en_US", CONFIGSETTING_RELOADABLE },
		{ "sync_gab_realtime",			"yes", CONFIGSETTING_RELOADABLE },
		{ "max_deferred_records",		"0", CONFIGSETTING_RELOADABLE },
		{ "max_deferred_records_folder", "20", CONFIGSETTING_RELOADABLE },
		{ "enable_test_protocol",		"no", CONFIGSETTING_RELOADABLE },
		{ "disabled_features", "imap pop3", CONFIGSETTING_RELOADABLE },
		{ "counter_reset", "yes", CONFIGSETTING_RELOADABLE },
		{ "mysql_group_concat_max_len", "21844", CONFIGSETTING_RELOADABLE },
		{ "restrict_admin_permissions", "no", 0 },
		{ "embedded_attachment_limit", "20", CONFIGSETTING_RELOADABLE },
		{ "proxy_header", "", CONFIGSETTING_RELOADABLE },
		{ "owner_auto_full_access", "true" },
		{ "attachment_files_fsync", "false", 0 },
		{ "tmp_path", "/tmp" },
		{ NULL, NULL },
	};

	char buffer[256];
	confstr(_CS_GNU_LIBPTHREAD_VERSION, buffer, sizeof(buffer));
	if (strncmp(buffer, "linuxthreads", strlen("linuxthreads")) == 0)
		m_bNPTL = false;

	// Init random generator
	rand_init();

	// init translations according to environment variables
	setlocale(LC_ALL, "");

	InitBindTextDomain();

	// Load settings
	g_lpConfig = ECConfig::Create(lpDefaults);
	
	if (!g_lpConfig->LoadSettings(szConfig) ||
	    !g_lpConfig->ParseParams(trim_argc, trim_argv, NULL) ||
	    (!m_bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors()) ) {
		g_lpLogger = new ECLogger_File(EC_LOGLEVEL_INFO, 0, "-", false); // create info logger without a timestamp to stderr
		ec_log_set(g_lpLogger);
		LogConfigErrors(g_lpConfig);
		er = MAPI_E_UNCONFIGURED;
		goto exit;
	}

	kc_reexec_with_allocator(argv, g_lpConfig->GetSetting("allocator_library"));

	// setup logging
	g_lpLogger = CreateLogger(g_lpConfig, szName, "KopanoServer");
	if (!g_lpLogger) {
		fprintf(stderr, "Error in log configuration, unable to resume.\n");
		er = MAPI_E_UNCONFIGURED;
		goto exit;
	}
	ec_log_set(g_lpLogger);
	if (m_bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors())
		LogConfigErrors(g_lpConfig);

	if (!TmpPath::getInstance() -> OverridePath(g_lpConfig))
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Ignoring invalid path-setting!");

	g_lpAudit = CreateLogger(g_lpConfig, szName, "KopanoServer", true);
	if (g_lpAudit)
		g_lpAudit->Log(EC_LOGLEVEL_NOTICE, "server startup uid=%d", getuid());
	else
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Audit logging not enabled.");

	g_lpLogger->Log(EC_LOGLEVEL_ALWAYS, "Starting server version " PROJECT_VERSION_SERVER_STR ", pid %d", getpid());

	if (g_lpConfig->HasWarnings())
		LogConfigErrors(g_lpConfig);

	if (strcmp(g_lpConfig->GetSetting("attachment_storage"), "files") == 0) {
		// directory will be created using startup (probably root) and then chowned to the new 'runas' username
		if (CreatePath(g_lpConfig->GetSetting("attachment_path")) != 0) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to create attachment directory '%s'", g_lpConfig->GetSetting("attachment_path"));
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}
		if (stat(g_lpConfig->GetSetting("attachment_path"), &dir) != 0) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to stat attachment directory '%s', error: %s", g_lpConfig->GetSetting("attachment_path"), strerror(errno));
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}
		runasUser = getpwnam(g_lpConfig->GetSetting("run_as_user","","root"));
		if (runasUser == NULL) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Fatal: run_as_user '%s' is unknown", g_lpConfig->GetSetting("run_as_user","","root"));
			er = MAPI_E_UNCONFIGURED;
			goto exit;
		}
		if (runasUser->pw_uid != dir.st_uid) {
			if (unix_chown(g_lpConfig->GetSetting("attachment_path"), g_lpConfig->GetSetting("run_as_user"), g_lpConfig->GetSetting("run_as_group")) != 0) {
				g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to change ownership for attachment directory '%s'", g_lpConfig->GetSetting("attachment_path"));
				er = KCERR_DATABASE_ERROR;
				goto exit;
			}
		}
#ifdef HAVE_LIBS3_H
	} else if (strcmp(g_lpConfig->GetSetting("attachment_storage"), "s3") == 0) {
		// @todo check S3 settings and connectivity
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Attachment storage set to S3 Storage");
#endif
	} else if (strcmp(g_lpConfig->GetSetting("attachment_storage"), "database") != 0) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unknown attachment_storage option '%s', reverting to default 'database' method.", g_lpConfig->GetSetting("attachment_storage"));
		g_lpConfig->AddSetting("attachment_storage", "database");
	}

	if (strcasecmp(g_lpConfig->GetSetting("user_plugin"), "db") == 0 && parseBool(g_lpConfig->GetSetting("sync_gab_realtime")) == false) {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Unsupported sync_gab_realtime = no when using DB plugin. Enabling sync_gab_realtime.");
		g_lpConfig->AddSetting("sync_gab_realtime", "yes");
	}

	kopano_initlibrary(g_lpConfig->GetSetting("mysql_database_path"), g_lpConfig->GetSetting("mysql_config_file"));

	if(!strcmp(g_lpConfig->GetSetting("server_pipe_enabled"), "yes"))
		bPipeEnabled = true;
	else
		bPipeEnabled = false;

	if(!strcmp(g_lpConfig->GetSetting("server_tcp_enabled"), "yes"))
		bTCPEnabled = true;
	else
		bTCPEnabled = false;

	if(!strcmp(g_lpConfig->GetSetting("server_ssl_enabled"), "yes"))
		bSSLEnabled = true;
	else
		bSSLEnabled = false;

    soap_ssl_init(); // Always call this in the main thread once!

    ssl_threading_setup();

	// setup connection handler
	g_lpSoapServerConn = new ECSoapServerConnection(g_lpConfig, g_lpLogger);

	// Setup a tcp connection
	if (bTCPEnabled)
	{
		er = g_lpSoapServerConn->ListenTCP(g_lpConfig->GetSetting("server_bind"), atoi(g_lpConfig->GetSetting("server_tcp_port")));
		if(er != erSuccess) {
			goto exit;
		}
	}

	// Setup ssl connection
	if (bSSLEnabled) {
		er = g_lpSoapServerConn->ListenSSL(g_lpConfig->GetSetting("server_bind"),		// servername
							atoi(g_lpConfig->GetSetting("server_ssl_port")),		// sslPort
							g_lpConfig->GetSetting("server_ssl_key_file","",NULL),	// key file
							g_lpConfig->GetSetting("server_ssl_key_pass","",NULL),	// key password
							g_lpConfig->GetSetting("server_ssl_ca_file","",NULL),	// CA certificate file which signed clients
							g_lpConfig->GetSetting("server_ssl_ca_path","",NULL)	// CA certificate path of thrusted sources
							);
		if(er != erSuccess) {
			goto exit;
		}
	}

	// Set max open file descriptors to FD_SETSIZE .. higher than this number
	// is a bad idea, as it will start breaking select() calls.
	struct rlimit limit;

	limit.rlim_cur = FD_SETSIZE;
	limit.rlim_max = FD_SETSIZE;
	if(setrlimit(RLIMIT_NOFILE, &limit) < 0) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: setrlimit(RLIMIT_NOFILE, %d) failed, you will only be able to connect up to %d sockets.", FD_SETSIZE, getdtablesize());
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: Either start the process as root, or increase user limits for open file descriptors.");
	}

	if (parseBool(g_lpConfig->GetSetting("coredump_enabled")))
		unix_coredump_enable(g_lpLogger);
	if (unix_runas(g_lpConfig, g_lpLogger)) {
		er = MAPI_E_CALL_FAILED;
		goto exit;
	}

	// Priority queue is always enabled, create as first socket, so this socket is returned first too on activity
	er = g_lpSoapServerConn->ListenPipe(g_lpConfig->GetSetting("server_pipe_priority"), true);
	if (er != erSuccess) {
		goto exit;
	}
	// Setup a pipe connection
	if (bPipeEnabled) {
		er = g_lpSoapServerConn->ListenPipe(g_lpConfig->GetSetting("server_pipe_name"));
		if (er != erSuccess) {
			goto exit;
		}
	}
	// Test database settings
	lpDatabaseFactory = new ECDatabaseFactory(g_lpConfig);

	// open database
	er = lpDatabaseFactory->CreateDatabaseObject(&lpDatabase, dbError);
	if(er == KCERR_DATABASE_NOT_FOUND) {
		er = lpDatabaseFactory->CreateDatabase();
		if(er != erSuccess) {
			goto exit;
		}

		er = lpDatabaseFactory->CreateDatabaseObject(&lpDatabase, dbError);
	}

	if(er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to connect to database: %s", dbError.c_str());
		goto exit;
	}
	g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "Connection to database '%s' succeeded", g_lpConfig->GetSetting("mysql_database"));

	hosted = parseBool(g_lpConfig->GetSetting("enable_hosted_kopano"));
	distributed = parseBool(g_lpConfig->GetSetting("enable_distributed_kopano"));
	// fork if needed and drop privileges as requested.
	// this must be done before we do anything with pthreads
	if (daemonize && unix_daemonize(g_lpConfig, g_lpLogger)) {
		er = MAPI_E_CALL_FAILED;
		goto exit;
	}
	if (!daemonize)
		setsid();
	unix_create_pidfile(szName, g_lpConfig, g_lpLogger);

	mainthread = pthread_self();

	// SIGSEGV backtrace support
	memset(&st, 0, sizeof(st));
	memset(&act, 0, sizeof(act));

	st.ss_sp = malloc(65536);
	st.ss_flags = 0;
	st.ss_size = 65536;

	act.sa_sigaction = sigsegv;
	act.sa_flags = SA_ONSTACK | SA_RESETHAND | SA_SIGINFO;

	sigaltstack(&st, NULL);
	sigaction(SIGSEGV, &act, NULL);
	sigaction(SIGBUS , &act, NULL);
	sigaction(SIGABRT, &act, NULL);

	if (m_bNPTL) {
		// normally ignore these signals
		signal(SIGUSR1, SIG_IGN);
		signal(SIGUSR2, SIG_IGN);
		signal(SIGPIPE, SIG_IGN);

		// block these signals to handle only in the thread by sigwait()
		sigemptyset(&signal_mask);
		sigaddset(&signal_mask, SIGINT);
		sigaddset(&signal_mask, SIGTERM);
		sigaddset(&signal_mask, SIGHUP);

		// valid for all threads afterwards
		pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
		// create signal handler thread, will handle all blocked signals
		// must be done after the daemonize
		pthread_create(&signal_thread, NULL, signal_handler, NULL);
	        set_thread_name(signal_thread, "SignalHThread");
	} else
	{
		// reset signals to normal server usage
		signal(SIGTERM , process_signal);
		signal(SIGINT  , process_signal);	//CTRL+C
		signal(SIGHUP , process_signal);	// logrotate
		signal(SIGUSR1, process_signal);
		signal(SIGUSR2, process_signal);
		signal(SIGPIPE, process_signal);
	}

	// ignore ignorable signals that might stop the server during database upgrade
	// all these signals will be reset after the database upgrade part.
	m_bDatabaseUpdateIgnoreSignals = true;

	// add a lock file to disable the /etc/init.d scripts
	tmplock = open(upgrade_lock_file, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);

	if (tmplock == -1)
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: Unable to place upgrade lockfile: %s", strerror(errno));

#ifdef EMBEDDED_MYSQL
{
	unsigned int ulResult = 0;
	// setting upgrade_tables
	// 1 = upgrade from mysql 4.1.23 to 5.22
	if(GetDatabaseSettingAsInteger(lpDatabase, "upgrade_tables", &ulResult) != erSuccess || ulResult == 0) {

		er = lpDatabase->ValidateTables();
		if (er != erSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to validate the database.");
			goto exit;
		}

		SetDatabaseSetting(lpDatabase, "upgrade_tables", 1);
	}
}
#endif

	// perform database upgrade .. may take a very long time
	er = lpDatabaseFactory->UpdateDatabase(m_bForceDatabaseUpdate, dbError);
	// remove lock file
	if (tmplock != -1) {
		if (unlink(upgrade_lock_file) == -1)
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: Unable to delete upgrade lockfile (%s): %s", upgrade_lock_file, strerror(errno));

		close(tmplock);
	}

	if(er == KCERR_INVALID_VERSION) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: %s", dbError.c_str());

		if(m_bIgnoreDatabaseVersionConflict == false) {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "   You can force the server to start with --ignore-database-version-conflict");
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "   Warning, you can lose data! If you don't know what you're doing, you shouldn't be using this option!");
			goto exit;
		}
	}else if(er != erSuccess) {
		if (er != KCERR_USER_CANCEL)
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Can't update the database: %s", dbError.c_str());
		goto exit;
	}
	
	er = lpDatabase->InitializeDBState();
	if(er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Can't initialize database settings");
		goto exit;
	}
	
	m_bDatabaseUpdateIgnoreSignals = false;
	if(searchfolder_restart_required) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Update requires searchresult folders to be rebuilt. This may take some time. You can restart this process with the --restart-searches option");
		restart_searches = 1;
	}

	// check database for MyISAM tables
	er = check_database_innodb(lpDatabase);
	if (er != erSuccess)
		goto exit;

	// check attachment database started with, and maybe reject startup
	er = check_database_attachments(lpDatabase);
	if (er != erSuccess)
		goto exit;

	// check you can write into the file attachment storage
	er = check_attachment_storage_permissions();
	if (er != erSuccess)
		goto exit;

	// check upgrade problem with wrong sequence in tproperties table primary key
	er = check_database_tproperties_key(lpDatabase);
	if (er != erSuccess)
		goto exit;

	// check distributed mode started with, and maybe reject startup
	er = check_distributed_kopano(lpDatabase);
	if (er != erSuccess)
		goto exit;

	// check whether the thread_stack is large enough.
	er = check_database_thread_stack(lpDatabase);
	if (er != erSuccess)
		goto exit;

	//Init the main system, now you can use the values like session manager
	// This also starts several threads, like SessionCleaner, NotificationThread and TPropsPurge.
	er = kopano_init(g_lpConfig, g_lpAudit, hosted, distributed);
	if (er != erSuccess) { // create SessionManager
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to initialize kopano session manager");
		goto exit;
	}

	// check for conflicting settings in local config and LDAP, after kopano_init since this needs the sessionmanager.
	er = check_server_configuration();
	if (er != erSuccess)
		goto exit;

	// Load search folders from disk
	g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "Loading searchfolders");
	er = g_lpSessionManager->GetSearchFolders()->LoadSearchFolders();
	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to load searchfolders");
		goto exit;
	}
	
	if (restart_searches) // restart_searches if specified
		g_lpSessionManager->GetSearchFolders()->RestartSearches();

	// Create scheduler system
	g_lpScheduler = new ECScheduler(g_lpLogger);
	// Add a task on the scheduler
	g_lpScheduler->AddSchedule(SCHEDULE_HOUR, 00, &SoftDeleteRemover, (void*)&g_Quit);

	g_lpScheduler->AddSchedule(SCHEDULE_HOUR, 15, &CleanupSyncsTable);
	g_lpScheduler->AddSchedule(SCHEDULE_HOUR, 16, &CleanupSyncedMessagesTable);

	// high loglevel to always see when server is started.
	g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "Startup succeeded on pid %d", getpid() );
	g_lpStatsCollector->SetTime(SCN_SERVER_STARTTIME, time(NULL));

	// Enter main accept loop
	retval = 0;
	while(!g_Quit) {
		// Select on the sockets
		er = g_lpSoapServerConn->MainLoop();
		if(er == KCERR_NETWORK_ERROR) {
			retval = -1;
			break; // Exit loop
		} else if (er != erSuccess) {
			continue;
		}
	}
	// Close All sessions
	kopano_removeallsessions();
	if (m_bNPTL) {
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Joining signal thread");
		pthread_join(signal_thread, NULL);
	}
exit:
	if (er != erSuccess) {
		auto msg = format("An error occurred (%x).", er);
		if (g_lpConfig)
			msg += format(" Please check %s for details.", g_lpConfig->GetSetting("log_file"));
		else
			msg += " Please check logfile for details.";

		fprintf(stderr, "\n%s\n\n", msg.c_str());
	}

	if (g_lpAudit)
		g_lpAudit->Log(EC_LOGLEVEL_ALWAYS, "server shutdown in progress");

	delete g_lpSoapServerConn;

	delete g_lpScheduler;
	free(st.ss_sp);
	delete lpDatabase;
	delete lpDatabaseFactory;

	kopano_exit();

	ssl_threading_cleanup();

	SSL_library_cleanup(); //cleanup memory so valgrind is happy
	kopano_unloadlibrary();
	rand_free();

	delete g_lpConfig;

	if (g_lpLogger) {
		g_lpLogger->Log(EC_LOGLEVEL_ALWAYS, "Server shutdown complete.");
		g_lpLogger->Release();
	}
	if (g_lpAudit)
		g_lpAudit->Release();

#ifdef ZCP_USES_ICU
	// cleanup ICU data so valgrind is happy
	u_cleanup();
#endif

	return retval;
}
