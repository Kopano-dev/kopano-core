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
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <kopano/ecversion.h>
#include <kopano/stringutil.h>

#include "soapH.h"

#include "ECDatabase.h"
#include "ECDatabaseFactory.h"
#include "ECDatabaseUpdate.h"
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
#include "ECICS.h"
#include <openssl/ssl.h>

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

// Reports information on the current state of the license
void* ReportLicense(void *);

static int running_server(char *, const char *, int, char **, int, char **);

int					g_Quit = 0;
int					daemonize = 1;
int					restart_searches = 0;
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

bool m_bDatabaseUpdateIgnoreSignals = false;

// This is the callback function for libserver/* so that it can notify that a delayed soap
// request has been handled.
static void kcsrv_notify_done(struct soap *soap)
{
    g_lpSoapServerConn->NotifyDone(soap);
}

// Called from ECStatsTables to get server stats
static void kcsrv_get_server_stats(unsigned int *lpulQueueLength,
    double *lpdblAge, unsigned int *lpulThreadCount,
    unsigned int *lpulIdleThreads)
{
    g_lpSoapServerConn->GetStats(lpulQueueLength, lpdblAge, lpulThreadCount, lpulIdleThreads);
}

static void process_signal(int sig)
{
	ec_log_debug("Received signal %d", sig);
	ZLOG_AUDIT(g_lpAudit, "server signalled sig=%d", sig);

	if (m_bDatabaseUpdateIgnoreSignals) {
		ec_log_notice("WARNING: Database upgrade is taking place.");
		ec_log_notice("  Please be patient, and do not try to kill the server process.");
		ec_log_notice("  It may leave your database in an inconsistent state.");
		return;
	}	

	const char *ll;
	int new_ll;

	if(g_Quit == 1)
		return; // already in exit state!

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		ec_log_warn("Shutting down");
		if (g_lpSoapServerConn)
			g_lpSoapServerConn->ShutDown();
		g_Quit = 1;

		break;
	case SIGHUP:
		// g_lpSessionManager only present when kopano_init is called (last init function), signals are initialized much earlier
		if (g_lpSessionManager == NULL)
			return;

		if (!g_lpConfig->ReloadSettings())
			ec_log_warn("Unable to reload configuration file, continuing with current settings.");
		if (g_lpConfig->HasErrors())
			ec_log_err("Failed to reload configuration file");

		g_lpSessionManager->GetPluginFactory()->SignalPlugins(sig);

		ll = g_lpConfig->GetSetting("log_level");
		new_ll = ll ? strtol(ll, NULL, 0) : EC_LOGLEVEL_WARNING;
		g_lpLogger->SetLoglevel(new_ll);
		g_lpLogger->Reset();
		ec_log_warn("Log connection was reset");

		if (g_lpAudit) {
			ll = g_lpConfig->GetSetting("audit_log_level");
			new_ll = ll ? strtol(ll, NULL, 0) : 1;
			g_lpAudit->SetLoglevel(new_ll);
			g_lpAudit->Reset();
		}

		g_lpStatsCollector->SetTime(SCN_SERVER_LAST_CONFIGRELOAD, time(NULL));
		g_lpSoapServerConn->DoHUP();
		break;
	default:
		ec_log_debug("Unknown signal %d received", sig);
		break;
	}
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
		ec_log_crit("Database table '%s' not in InnoDB format: %s", lpRow[0] ? lpRow[0] : "unknown table", lpRow[1] ? lpRow[1] : "unknown engine");
		er = KCERR_DATABASE_ERROR;
	}

	if (er != erSuccess) {
		ec_log_crit("Your database was incorrectly created. Please upgrade all tables to the InnoDB format using this query:");
		ec_log_crit("  ALTER TABLE <table name> ENGINE='InnoDB';");
		ec_log_crit("This process may take a very long time, depending on the size of your database.");
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
		ec_log_crit("Unable to read from database");
		goto exit;
	}

	lpRow = lpDatabase->FetchRow(lpResult);
	if (lpRow != nullptr && lpRow[0] != nullptr &&
	    // check if the mode is the same as last time
	    strcmp(lpRow[0], g_lpConfig->GetSetting("attachment_storage")) != 0) {
		if (!m_bIgnoreAttachmentStorageConflict) {
			ec_log_err("Attachments are stored with option '%s', but '%s' is selected.", lpRow[0], g_lpConfig->GetSetting("attachment_storage"));
			er = KCERR_DATABASE_ERROR;
			goto exit;
		} else {
			ec_log_warn("Ignoring attachment storing conflict as requested. Attachments are now stored with option '%s'", g_lpConfig->GetSetting("attachment_storage"));
		}
	}

	// first time we start, set the database to the selected mode
	strQuery = (string)"REPLACE INTO settings VALUES ('attachment_storage', '" + g_lpConfig->GetSetting("attachment_storage") + "')";

	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) {
		ec_log_err("Unable to update database settings");
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
		ec_log_err("Unable to read from database");
		goto exit;
	}

	lpRow = lpDatabase->FetchRow(lpResult);

	// If no value is found in the database any setting is valid
	if (lpRow == NULL || lpRow[0] == NULL) 
		goto exit;

	// If any value is found, distributed is not allowed. The value specifies the reason.
	if (bConfigEnabled) {
		if (!m_bIgnoreDistributedKopanoConflict) {
			ec_log_crit("Multiserver mode is locked, reason: '%s'. Contact Kopano for support.", lpRow[0]);
			er = KCERR_DATABASE_ERROR;
			goto exit;
		} else {
			ec_log_warn("Ignoring multiserver mode lock as requested.");
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
			 ec_log_err("Unable to write attachments to the directory '%s' - %s. Please check the directory and sub directories.",  g_lpConfig->GetSetting("attachment_path"), strerror(errno));
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
		ec_log_err("Unable to read from database");
		goto exit;
	}

	er = KCERR_DATABASE_ERROR;

	lpRow = lpDatabase->FetchRow(lpResult);
	if (!lpRow || !lpRow[1]) {
		ec_log_crit("No tproperties table definition found");
		goto exit;
	}

	strTable = lpRow[1];

	start = strTable.find("PRIMARY KEY");
	if (start == string::npos) {
		ec_log_crit("No primary key found in tproperties table");
		goto exit;
	}

	end = strTable.find(")", start);
	if (end == string::npos) {
		ec_log_crit("No end of primary key found in tproperties table");
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
		ec_log_warn("Primary key of tproperties table incorrect, trying: %s", strTable.c_str());
		goto exit;
	}

	// start+1:end == `type`,`hierarchyid`
	strTable.erase(0, start+1);

	// if not correct...
	if (strTable.compare("`hierarchyid`,`type`") != 0) {
		ec_log_warn("**** WARNING: Installation is not optimal! ****");
		ec_log_warn("  The primary key of the tproperties table is incorrect.");
		ec_log_warn("  Since updating the primary key on a large table is slow, the server will not automatically update this for you.");
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
		ec_log_err("Unable to read from database");
		goto exit;
	}

	lpRow = lpDatabase->FetchRow(lpResult);
	if (!lpRow || !lpRow[1]) {
		ec_log_err("No thread_stack variable returned");
		goto exit;
	}

	ulThreadStack = atoui(lpRow[1]);
	if (ulThreadStack < MYSQL_MIN_THREAD_STACK) {
		ec_log_warn("MySQL thread_stack is set to %u, which is too small", ulThreadStack);
		ec_log_warn("Please set thread_stack to %uK or higher in your MySQL configuration", MYSQL_MIN_THREAD_STACK / 1024);
		if (m_bIgnoreDbThreadStackSize)
			ec_log_warn("MySQL thread_stack setting ignored. Please reconsider when 'Thread stack overrun' errors appear in the log.");
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
		ec_log_err("WARNING: Unable to find FQDN, please specify in 'server_hostname'. Now using '%s'.", g_lpConfig->GetSetting("server_hostname"));

	// all other checks are only required for multi-server environments
	bCheck = parseBool(g_lpConfig->GetSetting("enable_distributed_kopano"));
	if (!bCheck)
		goto exit;
	
	strServerName = g_lpConfig->GetSetting("server_name");
	if (strServerName.empty()) {
		ec_log_crit("ERROR: No 'server_name' specified while operating in multiserver mode.");
		er = KCERR_INVALID_PARAMETER;
		// unable to check any other server details if we have no name, skip other tests
		goto exit;
	}

	er = g_lpSessionManager->CreateSessionInternal(&lpecSession);
	if (er != erSuccess) {
		ec_log_crit("Internal error 0x%08x while checking distributed configuration", er);
		goto exit;
	}

	lpecSession->Lock();
	
	er = lpecSession->GetUserManagement()->GetServerDetails(strServerName, &sServerDetails);
	if (er != erSuccess) {
		ec_log_crit("ERROR: Unable to find server information on LDAP for '%s', error 0x%08X. Check your server name.", strServerName.c_str(), er);
		// unable to check anything else if we have no details, skip other tests
		goto exit;
	}
		
	// Check the various connection parameters for consistency
	if (parseBool(g_lpConfig->GetSetting("server_pipe_enabled")) == true) {
		if (sServerDetails.GetFilePath().empty()) {
			ec_log_warn("WARNING: 'server_pipe_enabled' is set, but LDAP returns nothing");
			bHaveErrors = true;
		}
		if (sServerDetails.GetFilePath().compare((std::string)"file://" + g_lpConfig->GetSetting("server_pipe_name")) != 0) {
			ec_log_warn("WARNING: 'server_pipe_name' is set to '%s', but LDAP returns '%s'", g_lpConfig->GetSetting("server_pipe_name"), sServerDetails.GetFilePath().c_str());
			bHaveErrors = true;
		}
	} else if (!sServerDetails.GetFilePath().empty()) {
		ec_log_warn("WARNING: 'server_pipe_enabled' is unset, but LDAP returns '%s'", sServerDetails.GetFilePath().c_str());
		bHaveErrors = true;
	}
	
	if (parseBool(g_lpConfig->GetSetting("server_tcp_enabled")) == true) {
		if (sServerDetails.GetHttpPath().empty()) {
			ec_log_warn("WARNING: 'server_tcp_enabled' is set, but LDAP returns nothing");
			bHaveErrors = true;
		}
		
		ulPort = atoui(g_lpConfig->GetSetting("server_tcp_port"));
		if (sServerDetails.GetHttpPort() != ulPort) {
			ec_log_warn("WARNING: 'server_tcp_port' is set to '%u', but LDAP returns '%u'", ulPort, sServerDetails.GetHttpPort());
			bHaveErrors = true;
		}
	} else if (!sServerDetails.GetHttpPath().empty()) {
		ec_log_warn("WARNING: 'server_tcp_enabled' is unset, but LDAP returns '%s'", sServerDetails.GetHttpPath().c_str());
		bHaveErrors = true;
	}
	
	if (parseBool(g_lpConfig->GetSetting("server_ssl_enabled")) == true) {
		if (sServerDetails.GetSslPath().empty()) {
			ec_log_warn("WARNING: 'server_ssl_enabled' is set, but LDAP returns nothing");
			bHaveErrors = true;
		}
		
		ulPort = atoui(g_lpConfig->GetSetting("server_ssl_port"));
		if (sServerDetails.GetSslPort() != ulPort) {
			ec_log_warn("WARNING: 'server_ssl_port' is set to '%u', but LDAP returns '%u'", ulPort, sServerDetails.GetSslPort());
			bHaveErrors = true;
		}
	} else if (!sServerDetails.GetSslPath().empty()) {
		ec_log_warn("WARNING: 'server_ssl_enabled' is unset, but LDAP returns '%s'", sServerDetails.GetSslPath().c_str());
		bHaveErrors = true;
	}	
	
exit:
	if (lpecSession) {
		lpecSession->Unlock();
		g_lpSessionManager->RemoveSessionInternal(lpecSession);
	}

	// we could return an error when bHaveErrors is set, but we currently find this not fatal as a sysadmin might be smarter than us.
	if (bHaveErrors)
 		ec_log_warn("WARNING: Inconsistencies detected between local and LDAP based configuration.");

	// we do return er, since if that is set GetServerDetails() does not work and that is quite vital to work in distributed systems.
	return er;
}

/**
 * Restart the program with a new preloaded library.
 * @argv:	full argv of current invocation
 * @lib:	library to load via LD_PRELOAD
 *
 * As every program under Linux is linked to libc and symbol resolution is done
 * breadth-first, having just libkcserver.so linked to the alternate allocator
 * is not enough to ensure the allocator is being used in favor of libc malloc.
 *
 * A program built against glibc will have a record for e.g.
 * "malloc@GLIBC_2.2.5". The use of LD_PRELOAD appears to relax the version
 * requirement, though; the benefit would be that libtcmalloc's malloc will
 * take over _all_ malloc calls.
 */
static int kc_reexec_with_allocator(char **argv, const char *lib)
{
	if (lib == NULL || *lib == '\0')
		return 0;
	const char *s = getenv("KC_ALLOCATOR_DONE");
	if (s != NULL)
		/* avoid repeatedly reexecing ourselves */
		return 0;
	s = getenv("LD_PRELOAD");
	if (s == NULL)
		setenv("LD_PRELOAD", lib, true);
	else if (strstr(s, "/valgrind/") != NULL)
		/*
		 * Within vg, everything is a bit different — since it catches
		 * execve itself. Execing /proc/self/exe therefore won't work,
		 * we would need to use argv[0]. But… don't bother.
		 */
		return 0;
	else
		setenv("LD_PRELOAD", (std::string(s) + ":" + lib).c_str(), true);
	void *handle = dlopen(lib, RTLD_LAZY | RTLD_LOCAL);
	if (handle == NULL)
		/*
		 * Ignore libraries that won't load anyway. This avoids
		 * ld.so emitting a scary warning if we did re-exec.
		 */
		return 0;
	dlclose(handle);
	setenv("KC_ALLOCATOR_DONE", lib, true);

	/* Resolve "exe" symlink before exec to please the sysadmin */
	std::vector<char> linkbuf(16);
	ssize_t linklen;
	while (true) {
		linklen = readlink("/proc/self/exe", &linkbuf[0], linkbuf.size());
		if (linklen < 0 || static_cast<size_t>(linklen) < linkbuf.size())
			break;
		linkbuf.resize(linkbuf.size() * 2);
	}
	if (linklen < 0) {
		int ret = -errno;
		ec_log_warn("kc_reexec_with_allocator: readlink: %s", strerror(errno));
		return ret;
	}
	linkbuf[linklen] = '\0';
	ec_log_debug("Reexecing %s with %s", &linkbuf[0], lib);
	execv(&linkbuf[0], argv);
	int ret = -errno;
	ec_log_info("Failed to reexec self: %s. Continuing with standard allocator.", strerror(errno));
	return ret;
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
	struct sigaction act;
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
		{ "allow_local_users",			"yes", CONFIGSETTING_RELOADABLE },			// allow any user connect through the Unix socket
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
		{"server_ssl_key_file", "/etc/kopano/ssl/server.pem", CONFIGSETTING_RELOADABLE},
		{"server_ssl_key_pass", "server", CONFIGSETTING_EXACT | CONFIGSETTING_RELOADABLE},
		{"server_ssl_ca_file", "/etc/kopano/ssl/cacert.pem", CONFIGSETTING_RELOADABLE},
		{"server_ssl_ca_path", "", CONFIGSETTING_RELOADABLE},
#ifdef SSL_TXT_SSLV2
		{"server_ssl_protocols", "!SSLv2", CONFIGSETTING_RELOADABLE},
#else
		{"server_ssl_protocols", "", CONFIGSETTING_RELOADABLE},
#endif
		{"server_ssl_ciphers", "ALL:!LOW:!SSLv2:!EXP:!aNULL", CONFIGSETTING_RELOADABLE},
		{"server_ssl_prefer_server_ciphers", "no", CONFIGSETTING_RELOADABLE},
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
		{"attachment_s3_region", ""},
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
		{ "softdelete_lifetime",		"0", CONFIGSETTING_RELOADABLE },	// time expressed in days, 0 == never delete anything
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

		{ "client_update_enabled",	"no" },
		{ "client_update_log_level", "1", CONFIGSETTING_RELOADABLE },
		{ "client_update_path",		"/var/lib/kopano/client", CONFIGSETTING_RELOADABLE },
		{ "client_update_log_path",	"/var/log/kopano/autoupdate", CONFIGSETTING_RELOADABLE },
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

	// Init random generator
	rand_init();
#if GSOAP_VERSION >= 20839
	/*
	 * Init translations according to environment variables.
	 * It also changes things like decimal separator, which gsoap < 2.8.39
	 * fails to cope with properly.
	 */
	setlocale(LC_ALL, "");
#endif
	InitBindTextDomain();

	// Load settings
	g_lpConfig = ECConfig::Create(lpDefaults);
	
	if (!g_lpConfig->LoadSettings(szConfig) ||
	    g_lpConfig->ParseParams(trim_argc, trim_argv) < 0 ||
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
		ec_log_err("Ignoring invalid path-setting!");

	g_lpAudit = CreateLogger(g_lpConfig, szName, "KopanoServer", true);
	if (g_lpAudit)
		g_lpAudit->Log(EC_LOGLEVEL_NOTICE, "server startup uid=%d", getuid());
	else
		ec_log_info("Audit logging not enabled.");

	ec_log_always("Starting server version " PROJECT_VERSION_SERVER_STR ", pid %d", getpid());
	if (g_lpConfig->HasWarnings())
		LogConfigErrors(g_lpConfig);

	if (strcmp(g_lpConfig->GetSetting("attachment_storage"), "files") == 0) {
		// directory will be created using startup (probably root) and then chowned to the new 'runas' username
		if (CreatePath(g_lpConfig->GetSetting("attachment_path")) != 0) {
			ec_log_err("Unable to create attachment directory '%s'", g_lpConfig->GetSetting("attachment_path"));
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}
		if (stat(g_lpConfig->GetSetting("attachment_path"), &dir) != 0) {
			ec_log_err("Unable to stat attachment directory '%s', error: %s", g_lpConfig->GetSetting("attachment_path"), strerror(errno));
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}
		runasUser = getpwnam(g_lpConfig->GetSetting("run_as_user","","root"));
		if (runasUser == NULL) {
			ec_log_err("Fatal: run_as_user '%s' is unknown", g_lpConfig->GetSetting("run_as_user","","root"));
			er = MAPI_E_UNCONFIGURED;
			goto exit;
		}
		if (runasUser->pw_uid != dir.st_uid) {
			if (unix_chown(g_lpConfig->GetSetting("attachment_path"), g_lpConfig->GetSetting("run_as_user"), g_lpConfig->GetSetting("run_as_group")) != 0) {
				ec_log_err("Unable to change ownership for attachment directory '%s'", g_lpConfig->GetSetting("attachment_path"));
				er = KCERR_DATABASE_ERROR;
				goto exit;
			}
		}
#ifdef HAVE_LIBS3_H
	} else if (strcmp(g_lpConfig->GetSetting("attachment_storage"), "s3") == 0) {
		// @todo check S3 settings and connectivity
		ec_log_info("Attachment storage set to S3 Storage");
#endif
	} else if (strcmp(g_lpConfig->GetSetting("attachment_storage"), "database") != 0) {
		ec_log_err("Unknown attachment_storage option '%s', reverting to default 'database' method.", g_lpConfig->GetSetting("attachment_storage"));
		g_lpConfig->AddSetting("attachment_storage", "database");
	}

	if (strcasecmp(g_lpConfig->GetSetting("user_plugin"), "db") == 0 && parseBool(g_lpConfig->GetSetting("sync_gab_realtime")) == false) {
		ec_log_info("Unsupported sync_gab_realtime = no when using DB plugin. Enabling sync_gab_realtime.");
		g_lpConfig->AddSetting("sync_gab_realtime", "yes");
	}

	kopano_notify_done = kcsrv_notify_done;
	kopano_get_server_stats = kcsrv_get_server_stats;
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
	g_lpSoapServerConn = new ECSoapServerConnection(g_lpConfig);

	// Setup a TCP connection
	if (bTCPEnabled)
	{
		er = g_lpSoapServerConn->ListenTCP(g_lpConfig->GetSetting("server_bind"), atoi(g_lpConfig->GetSetting("server_tcp_port")),
										   parseBool(g_lpConfig->GetSetting("client_update_enabled")));
		if (er != erSuccess)
			goto exit;
	}

	// Setup SSL connection
	if (bSSLEnabled) {
		er = g_lpSoapServerConn->ListenSSL(g_lpConfig->GetSetting("server_bind"),		// servername
							atoi(g_lpConfig->GetSetting("server_ssl_port")),		// sslPort
							parseBool(g_lpConfig->GetSetting("client_update_enabled")),	// HTTP GET command is enabled
							g_lpConfig->GetSetting("server_ssl_key_file","",NULL),	// key file
							g_lpConfig->GetSetting("server_ssl_key_pass","",NULL),	// key password
							g_lpConfig->GetSetting("server_ssl_ca_file","",NULL),	// CA certificate file which signed clients
							g_lpConfig->GetSetting("server_ssl_ca_path","",NULL)	// CA certificate path of thrusted sources
							);
		if (er != erSuccess)
			goto exit;
	}

	// Set max open file descriptors to FD_SETSIZE .. higher than this number
	// is a bad idea, as it will start breaking select() calls.
	struct rlimit limit;

	limit.rlim_cur = FD_SETSIZE;
	limit.rlim_max = FD_SETSIZE;
	if(setrlimit(RLIMIT_NOFILE, &limit) < 0) {
		ec_log_warn("WARNING: setrlimit(RLIMIT_NOFILE, %d) failed, you will only be able to connect up to %d sockets.", FD_SETSIZE, getdtablesize());
		ec_log_warn("WARNING: Either start the process as root, or increase user limits for open file descriptors.");
	}

	if (parseBool(g_lpConfig->GetSetting("coredump_enabled")))
		unix_coredump_enable();
	if (unix_runas(g_lpConfig)) {
		er = MAPI_E_CALL_FAILED;
		goto exit;
	}

	// Priority queue is always enabled, create as first socket, so this socket is returned first too on activity
	er = g_lpSoapServerConn->ListenPipe(g_lpConfig->GetSetting("server_pipe_priority"), true);
	if (er != erSuccess)
		goto exit;
	// Setup a pipe connection
	if (bPipeEnabled) {
		er = g_lpSoapServerConn->ListenPipe(g_lpConfig->GetSetting("server_pipe_name"));
		if (er != erSuccess)
			goto exit;
	}
	// Test database settings
	lpDatabaseFactory = new ECDatabaseFactory(g_lpConfig);

	// open database
	er = lpDatabaseFactory->CreateDatabaseObject(&lpDatabase, dbError);
	if(er == KCERR_DATABASE_NOT_FOUND) {
		er = lpDatabaseFactory->CreateDatabase();
		if (er != erSuccess)
			goto exit;
		er = lpDatabaseFactory->CreateDatabaseObject(&lpDatabase, dbError);
	}

	if(er != erSuccess) {
		ec_log_crit("Unable to connect to database: %s", dbError.c_str());
		goto exit;
	}
	ec_log_notice("Connection to database '%s' succeeded", g_lpConfig->GetSetting("mysql_database"));

	hosted = parseBool(g_lpConfig->GetSetting("enable_hosted_kopano"));
	distributed = parseBool(g_lpConfig->GetSetting("enable_distributed_kopano"));
	// fork if needed and drop privileges as requested.
	// this must be done before we do anything with pthreads
	if (daemonize && unix_daemonize(g_lpConfig)) {
		er = MAPI_E_CALL_FAILED;
		goto exit;
	}
	if (!daemonize)
		setsid();
	unix_create_pidfile(szName, g_lpConfig);
	mainthread = pthread_self();

	// SIGSEGV backtrace support
	memset(&st, 0, sizeof(st));
	memset(&act, 0, sizeof(act));

	st.ss_sp = malloc(65536);
	st.ss_flags = 0;
	st.ss_size = 65536;

	act.sa_sigaction = sigsegv;
	act.sa_flags = SA_ONSTACK | SA_RESETHAND | SA_SIGINFO;
	sigemptyset(&act.sa_mask);

	sigaltstack(&st, NULL);
	sigaction(SIGSEGV, &act, NULL);
	sigaction(SIGBUS , &act, NULL);
	sigaction(SIGABRT, &act, NULL);

	// normally ignore these signals
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	act.sa_handler = process_signal;
	act.sa_flags = SA_ONSTACK | SA_RESTART;
	sigemptyset(&act.sa_mask);
	sigaction(SIGINT, &act, nullptr);
	sigaction(SIGHUP, &act, nullptr);
	sigaction(SIGTERM, &act, nullptr);

	// ignore ignorable signals that might stop the server during database upgrade
	// all these signals will be reset after the database upgrade part.
	m_bDatabaseUpdateIgnoreSignals = true;

	// add a lock file to disable the /etc/init.d scripts
	tmplock = open(upgrade_lock_file, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);

	if (tmplock == -1)
		ec_log_warn("WARNING: Unable to place upgrade lockfile: %s", strerror(errno));

#ifdef EMBEDDED_MYSQL
{
	unsigned int ulResult = 0;
	// setting upgrade_tables
	// 1 = upgrade from mysql 4.1.23 to 5.22
	if(GetDatabaseSettingAsInteger(lpDatabase, "upgrade_tables", &ulResult) != erSuccess || ulResult == 0) {

		er = lpDatabase->ValidateTables();
		if (er != erSuccess) {
			ec_log_err("Unable to validate the database.");
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
			ec_log_warn("WARNING: Unable to delete upgrade lockfile (%s): %s", upgrade_lock_file, strerror(errno));

		close(tmplock);
	}

	if(er == KCERR_INVALID_VERSION) {
		ec_log_warn("WARNING: %s", dbError.c_str());

		if(m_bIgnoreDatabaseVersionConflict == false) {
			ec_log_warn("   You can force the server to start with --ignore-database-version-conflict");
			ec_log_warn("   Warning, you can lose data! If you don't know what you're doing, you shouldn't be using this option!");
			goto exit;
		}
	}else if(er != erSuccess) {
		if (er != KCERR_USER_CANCEL)
			ec_log_err("Can't update the database: %s", dbError.c_str());
		goto exit;
	}
	
	er = lpDatabase->InitializeDBState();
	if(er != erSuccess) {
		ec_log_err("Can't initialize database settings");
		goto exit;
	}
	
	m_bDatabaseUpdateIgnoreSignals = false;
	if(searchfolder_restart_required) {
		ec_log_warn("Update requires searchresult folders to be rebuilt. This may take some time. You can restart this process with the --restart-searches option");
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
		ec_log_err("Unable to initialize kopano session manager");
		goto exit;
	}

	// check for conflicting settings in local config and LDAP, after kopano_init since this needs the sessionmanager.
	er = check_server_configuration();
	if (er != erSuccess)
		goto exit;

	// Load search folders from disk
	ec_log_notice("Loading searchfolders");
	er = g_lpSessionManager->GetSearchFolders()->LoadSearchFolders();
	if (er != erSuccess) {
		ec_log_err("Unable to load searchfolders");
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
	ec_log_notice("Startup succeeded on pid %d", getpid() );
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
		ec_log_always("Server shutdown complete.");
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
