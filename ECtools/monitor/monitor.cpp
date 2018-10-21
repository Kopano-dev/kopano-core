/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <getopt.h>
#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapidefs.h>
#include <mapiguid.h>
#include <kopano/ECScheduler.h>
#include <kopano/automapi.hpp>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include "ECMonitorDefs.h"
#include "ECQuotaMonitor.h"
#include <kopano/CommonUtil.h>
#include <kopano/UnixUtil.h>
#include <kopano/ecversion.h>
#include "charset/localeutil.h"

using namespace KC;
using std::cout;
using std::endl;

static std::shared_ptr<ECLogger> g_lpLogger;
static std::unique_ptr<ECTHREADMONITOR> m_lpThreadMonitor;
static std::mutex m_hExitMutex;
static std::condition_variable m_hExitSignal;
static pthread_t			mainthread;
static bool g_dump_config;

static HRESULT running_service(void)
{
	AutoMAPI mapiinit;
	auto hr = mapiinit.Initialize(nullptr);
	if (hr != hrSuccess) {
		ec_log_crit("Unable to initialize MAPI");
		return hr;
	}
	auto lpECScheduler = make_unique_nt<ECScheduler>();
	if (lpECScheduler == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	unsigned int ulInterval = atoi(m_lpThreadMonitor->lpConfig->GetSetting("quota_check_interval", nullptr, "15"));
	if (ulInterval == 0)
		ulInterval = 15;

	// Add Quota monitor
	hr = lpECScheduler->AddSchedule(SCHEDULE_MINUTES, ulInterval, ECQuotaMonitor::Create, m_lpThreadMonitor.get());
	if (hr != hrSuccess) {
		ec_log_crit("Unable to add quota monitor schedule");
		return hr;
	}
	ulock_normal l_exit(m_hExitMutex);
	m_hExitSignal.wait(l_exit);
	return hrSuccess;
}

static void sighandle(int sig)
{
	// Win32 has Unix semantics and therefore requires us to reset the signal handler.
	struct sigaction act{};
	sigemptyset(&act.sa_mask);
	act.sa_flags   = SA_RESTART;
	act.sa_handler = sighandle;
	sigaction(SIGTERM, &act, nullptr);
	sigaction(SIGINT, &act, nullptr);
	if (m_lpThreadMonitor) {
		if (!m_lpThreadMonitor->bShutdown)
			/* do not log multimple shutdown messages */
			ec_log_notice("Termination requested, shutting down.");
		m_lpThreadMonitor->bShutdown = true;
	}
	m_hExitSignal.notify_one();
}

static void sighup(int signr)
{
	// In Win32, the signal is sent in a separate, special signal thread. So this test is
	// not needed or required.
	if (pthread_equal(pthread_self(), mainthread)==0)
		return;
	if (m_lpThreadMonitor == NULL)
		return;
	if (m_lpThreadMonitor->lpConfig != NULL &&
	    !m_lpThreadMonitor->lpConfig->ReloadSettings())
		ec_log_warn("Unable to reload configuration file, continuing with current settings.");
	if (m_lpThreadMonitor->lpConfig) {
		const char *ll = m_lpThreadMonitor->lpConfig->GetSetting("log_level");
		int new_ll = ll ? atoi(ll) : EC_LOGLEVEL_WARNING;
		ec_log_get()->SetLoglevel(new_ll);
	}

	ec_log_get()->Reset();
	ec_log_warn("Log connection was reset");
}

// SIGSEGV catcher
static void sigsegv(int signr, siginfo_t *si, void *uc)
{
	generic_sigsegv_handler(ec_log_get(), "kopano-monitor", PROJECT_VERSION, signr, si, uc);
}

static void print_help(const char *name)
{
	cout << "Usage:\n" << endl;
	cout << name << " [-F] [-h|--host <serverpath>] [-c|--config <configfile>]" << endl;
	cout << "  -F\t\tDo not run in the background" << endl;
	cout << "  -h path\tUse alternate connect path (e.g. file:///var/run/socket).\n\t\tDefault: file:///var/run/kopano/server.sock" << endl;
	cout << "  -c filename\tUse alternate config file (e.g. /etc/kopano-monitor.cfg)\n\t\tDefault: /etc/kopano/monitor.cfg" << endl;
	cout << endl;
}

static ECRESULT main2(int argc, char **argv)
{
	const char *szConfig = ECConfig::GetDefaultPath("monitor.cfg");
	const char *szPath = NULL;
	int daemonize = 1;
	bool bIgnoreUnknownConfigOptions = false, exp_config = false;

	// Default settings
	static const configsetting_t lpDefaults[] = {
		{ "smtp_server","localhost" },
		{ "server_socket", "default:" },
		{ "run_as_user", "kopano" },
		{ "run_as_group", "kopano" },
		{ "pid_file", "/var/run/kopano/monitor.pid" },
		{"running_path", "/var/lib/kopano/empty", CONFIGSETTING_OBSOLETE},
		{"log_method", "auto", CONFIGSETTING_NONEMPTY},
		{"log_file", ""},
		{"log_level", "3", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{ "log_timestamp","1" },
		{ "log_buffer_size", "0" },
		{ "sslkey_file", "" },
		{ "sslkey_pass", "", CONFIGSETTING_EXACT },
		{ "quota_check_interval", "15" },
		{ "mailquota_resend_interval", "1", CONFIGSETTING_RELOADABLE },
		{ "userquota_warning_template", "/etc/kopano/quotamail/userwarning.mail", CONFIGSETTING_RELOADABLE },
		{ "userquota_soft_template", "/etc/kopano/quotamail/usersoft.mail", CONFIGSETTING_RELOADABLE },
		{ "userquota_hard_template", "/etc/kopano/quotamail/userhard.mail", CONFIGSETTING_RELOADABLE },
		{ "companyquota_warning_template", "/etc/kopano/quotamail/companywarning.mail", CONFIGSETTING_RELOADABLE },
		{ "companyquota_soft_template", "", CONFIGSETTING_UNUSED },
		{ "companyquota_hard_template", "", CONFIGSETTING_UNUSED },
		{ "servers", "" },
		{ NULL, NULL },
	};

	enum {
		OPT_HELP = UCHAR_MAX + 1,
		OPT_HOST,
		OPT_CONFIG,
		OPT_FOREGROUND,
		OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS,
		OPT_DUMP_CONFIG,
	};
	static const struct option long_options[] = {
		{ "help", 0, NULL, OPT_HELP },
		{ "host", 1, NULL, OPT_HOST },
		{ "config", 1, NULL, OPT_CONFIG },
		{ "foreground", 1, NULL, OPT_FOREGROUND },
		{ "ignore-unknown-config-options", 0, NULL, OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS },
		{"dump-config", no_argument, nullptr, OPT_DUMP_CONFIG},
		{ NULL, 0, NULL, 0 }
	};

	if (!forceUTF8Locale(true))
		return E_FAIL;

	while(1) {
		auto c = my_getopt_long_permissive(argc, argv, "c:h:iuFV", long_options, NULL);
		if(c == -1)
			break;

		switch(c) {
		case OPT_CONFIG:
		case 'c':
			szConfig = optarg;
			exp_config = true;
			break;
		case OPT_HOST:
		case 'h':
			szPath = optarg;
			break;
		case 'i': // Install service
		case 'u': // Uninstall service
			break;
		case OPT_FOREGROUND:
		case 'F':
			daemonize = 0;
			break;
		case OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS:
			bIgnoreUnknownConfigOptions = true;
			break;
		case OPT_DUMP_CONFIG:
			g_dump_config = true;
			break;
		case 'V':
			cout << "kopano-monitor " PROJECT_VERSION << endl;
			return 1;
		case OPT_HELP:
		default:
			print_help(argv[0]);
			return 1;
		}
	}

	m_lpThreadMonitor.reset(new(std::nothrow) ECTHREADMONITOR);
	if (m_lpThreadMonitor == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;

	m_lpThreadMonitor->lpConfig.reset(ECConfig::Create(lpDefaults));
	if (!m_lpThreadMonitor->lpConfig->LoadSettings(szConfig, !exp_config) ||
	    m_lpThreadMonitor->lpConfig->ParseParams(argc - optind, &argv[optind]) < 0 ||
	    (!bIgnoreUnknownConfigOptions && m_lpThreadMonitor->lpConfig->HasErrors())) {
		/* Create fatal logger without a timestamp to stderr. */
		g_lpLogger.reset(new(std::nothrow) ECLogger_File(EC_LOGLEVEL_INFO, 0, "-", false));
		ec_log_set(g_lpLogger);
		LogConfigErrors(m_lpThreadMonitor->lpConfig.get());
		return E_FAIL;
	}
	if (g_dump_config)
		return m_lpThreadMonitor->lpConfig->dump_config(stdout) == 0 ? hrSuccess : E_FAIL;

	mainthread = pthread_self();

	// setup logging
	g_lpLogger.reset(CreateLogger(m_lpThreadMonitor->lpConfig.get(), argv[0], "Kopano-Monitor"));
	ec_log_set(g_lpLogger);
	if ((bIgnoreUnknownConfigOptions && m_lpThreadMonitor->lpConfig->HasErrors()) || m_lpThreadMonitor->lpConfig->HasWarnings())
		LogConfigErrors(m_lpThreadMonitor->lpConfig.get());

	// set socket filename
	if (!szPath)
		szPath = m_lpThreadMonitor->lpConfig->GetSetting("server_socket");

	ec_log_always("Starting kopano-monitor version " PROJECT_VERSION " (pid %d uid %u)", getpid(), getuid());
	auto ret = unix_runas(m_lpThreadMonitor->lpConfig.get());
	if (ret < 0) {
		return E_FAIL;
	} else if (ret == 0) {
		ec_reexec_finalize();
	} else if (ret > 0) {
		ret = ec_reexec(argv);
		if (ret < 0)
			ec_log_notice("K-1240: Failed to re-exec self: %s. "
				"Continuing with restricted coredumps.", strerror(-ret));
	}

	// SIGSEGV backtrace support
	KAlternateStack sigstack;
	struct sigaction act{};
	sigemptyset(&act.sa_mask);
	act.sa_sigaction = sigsegv;
	act.sa_flags = SA_ONSTACK | SA_RESETHAND | SA_SIGINFO;
	sigemptyset(&act.sa_mask);
	sigaction(SIGSEGV, &act, NULL);
	sigaction(SIGBUS, &act, NULL);
	sigaction(SIGABRT, &act, NULL);
	act.sa_flags   = SA_RESTART;
	act.sa_handler = sighandle;
	sigaction(SIGTERM, &act, nullptr);
	sigaction(SIGINT, &act, nullptr);
	act.sa_handler = sighup;
	sigaction(SIGHUP, &act, nullptr);

	if (daemonize && unix_daemonize(m_lpThreadMonitor->lpConfig.get()))
		return E_FAIL;
	if (!daemonize)
		setsid();
	if (unix_create_pidfile(argv[0], m_lpThreadMonitor->lpConfig.get(), false) < 0)
		return E_FAIL;
	// Init exit threads
	return running_service();
}

int main(int argc, char **argv)
{
	return main2(argc, argv) == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}
