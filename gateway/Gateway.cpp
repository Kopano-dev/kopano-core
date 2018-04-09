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
#include <new>
#include <climits>
#include <csignal>
#include <netdb.h>
#include <poll.h>
#include <inetmapi/inetmapi.h>

#include <mapi.h>
#include <mapix.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <kopano/mapiext.h>
#include <mapiguid.h>

#include <kopano/CommonUtil.h>
#include <kopano/stringutil.h>
#include <iostream>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cerrno>

#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include <kopano/MAPIErrors.h>
#include <kopano/my_getopt.h>

#include <kopano/ECChannel.h>
#include "POP3.h"
#include "IMAP.h"
#include <kopano/ecversion.h>

#include "SSLUtil.h"

#include "TmpPath.h"
#include <kopano/UnixUtil.h>
#include <unicode/uclean.h>
#include <openssl/ssl.h>
#include <kopano/hl.hpp>

/**
 * @defgroup gateway Gateway for IMAP and POP3 
 * @{
 */

using namespace KC;
using std::cout;
using std::endl;

static int daemonize = 1;
int quit = 0;
static bool bThreads, g_dump_config;
static const char *szPath;
static ECLogger *g_lpLogger = NULL;
static ECConfig *g_lpConfig = NULL;
static pthread_t mainthread;
static int nChildren = 0;
static std::string g_strHostString;

static void sigterm(int s)
{
	quit = 1;
}

static void sighup(int sig)
{
	if (bThreads && pthread_equal(pthread_self(), mainthread)==0)
		return;
	if (g_lpConfig != nullptr && !g_lpConfig->ReloadSettings() &&
	    g_lpLogger != nullptr)
		ec_log_err("Unable to reload configuration file, continuing with current settings.");
	if (g_lpLogger == nullptr || g_lpConfig == nullptr)
		return;
	ec_log_info("Got SIGHUP config was reloaded");

	const char *ll = g_lpConfig->GetSetting("log_level");
	int new_ll = ll ? atoi(ll) : EC_LOGLEVEL_WARNING;
	g_lpLogger->SetLoglevel(new_ll);

	if (strlen(g_lpConfig->GetSetting("ssl_private_key_file")) > 0 &&
		strlen(g_lpConfig->GetSetting("ssl_certificate_file")) > 0) {
		if (ECChannel::HrSetCtx(g_lpConfig) != hrSuccess)
			ec_log_err("Error reloading SSL context");
		else
			ec_log_info("Reloaded SSL context");
	}
	g_lpLogger->Reset();
	ec_log_info("Log connection was reset");
}

static void sigchld(int)
{
	int stat;
	while (waitpid(-1, &stat, WNOHANG) > 0)
		--nChildren;
}

// SIGSEGV catcher
static void sigsegv(int signr, siginfo_t *si, void *uc)
{
	generic_sigsegv_handler(g_lpLogger, "kopano-gateway", PROJECT_VERSION, signr, si, uc);
}

static HRESULT running_service(const char *szPath, const char *servicename);

static void print_help(const char *name)
{
	cout << "Usage:\n" << endl;
	cout << name << " [-F] [-h|--host <serverpath>] [-c|--config <configfile>]" << endl;
	cout << "  -F\t\tDo not run in the background" << endl;
	cout << "  -h path\tUse alternate connect path (e.g. file:///var/run/socket).\n\t\tDefault: file:///var/run/kopano/server.sock" << endl;
	cout << "  -V Print version info." << endl;
	cout << "  -c filename\tUse alternate config file (e.g. /etc/kopano-gateway.cfg)\n\t\tDefault: /etc/kopano/gateway.cfg" << endl;
	cout << endl;
}

enum serviceType { ST_POP3 = 0, ST_IMAP };

struct HandlerArgs {
	serviceType type;
	ECChannel *lpChannel;
	ECLogger *lpLogger;
	ECConfig *lpConfig;
	bool bUseSSL;
};

static void *Handler(void *lpArg)
{
	auto lpHandlerArgs = static_cast<HandlerArgs *>(lpArg);
	auto lpChannel = lpHandlerArgs->lpChannel;
	auto lpLogger = lpHandlerArgs->lpLogger;
	auto lpConfig = lpHandlerArgs->lpConfig;
	auto bUseSSL = lpHandlerArgs->bUseSSL;

	// szPath is global, pointing to argv variable, or lpConfig variable
	ClientProto *client;
	if (lpHandlerArgs->type == ST_POP3)
		client = new POP3(szPath, lpChannel, lpLogger, lpConfig);
	else
		client = new IMAP(szPath, lpChannel, lpLogger, lpConfig);
	// not required anymore
	delete lpHandlerArgs;

	// make sure the pipe logger does not exit when this handler exits, but only frees the memory.
	auto pipelog = dynamic_cast<ECLogger_Pipe *>(lpLogger);
	if (pipelog != nullptr)
		pipelog->Disown();

	std::string inBuffer;
	HRESULT hr;
	bool bQuit = false;
	int timeouts = 0;

	if (bUseSSL && lpChannel->HrEnableTLS() != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to negotiate SSL connection");
		goto exit;
	}

	hr = client->HrSendGreeting(g_strHostString);
	if (hr != hrSuccess)
		goto exit;

	// Main command loop
	while (!bQuit && !quit) {
		// check for data
		hr = lpChannel->HrSelect(60);
		if (hr == MAPI_E_CANCEL)
			/* signalled - reevaluate bQuit */
			continue;
		if (hr == MAPI_E_TIMEOUT) {
			if (++timeouts < client->getTimeoutMinutes())
				// ignore select() timeout for 5 (POP3) or 30 (IMAP) minutes
				continue;
			// close idle first, so we don't have a race condition with the channel
			client->HrCloseConnection("BYE Connection closed because of timeout");
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Connection closed because of timeout");
			bQuit = true;
			break;
		} else if (hr == MAPI_E_NETWORK_ERROR) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Socket error: %s", strerror(errno));
			bQuit = true;
			break;
		}

		timeouts = 0;

		inBuffer.clear();
		hr = lpChannel->HrReadLine(&inBuffer);
		if (hr != hrSuccess) {
			if (errno)
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to read line: %s", strerror(errno));
			else
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Client disconnected");
			bQuit = true;
			break;
		}

		if (quit) {
			client->HrCloseConnection("BYE server shutting down");
			hr = MAPI_E_CALL_FAILED;
			bQuit = true;
			break;
		}

		if (client->isContinue()) {
			// we asked the client for more data, do not parse the buffer, but send it "to the previous command"
			// that last part is currently only HrCmdAuthenticate(), so no difficulties here.
			// also, PLAIN is the only supported auth method.
			client->HrProcessContinue(inBuffer);
			// no matter what happens, we continue handling the connection.
			continue;
		}

		HRESULT hr = hrSuccess;
		try {
			/* Process IMAP command */
			hr = client->HrProcessCommand(inBuffer);
		} catch (const KC::KMAPIError &e) {
			hr = e.code();
		}

		if (hr == MAPI_E_NETWORK_ERROR) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Connection error.");
			bQuit = true;
		}
		if (hr == MAPI_E_END_OF_SESSION) {
			lpLogger->Log(EC_LOGLEVEL_NOTICE, "Disconnecting client.");
			bQuit = true;
		}
	}

exit:
	lpLogger->Log(EC_LOGLEVEL_NOTICE, "Client %s thread exiting", lpChannel->peer_addr());

	client->HrDone(false);	// HrDone does not send an error string to the client
	delete client;

	delete lpChannel;
	if (bThreads == false) {
		g_lpLogger->Release();
		delete g_lpConfig;
	}

	/** free SSL error data **/
	#if OPENSSL_VERSION_NUMBER < 0x10100000L
		ERR_remove_state(0);
	#endif

	if (bThreads)
		--nChildren;

	return NULL;
}

static void *Handler_Threaded(void *a)
{
	/*
	 * Steer the control signals to the main thread for consistency with
	 * the forked mode.
	 */
	kcsrv_blocksigs();
	return Handler(a);
}

static std::string GetServerFQDN()
{
	std::string retval = "localhost";
	char hostname[256] = {0};
	struct addrinfo *result = nullptr;

	auto rc = gethostname(hostname, sizeof(hostname));
	if (rc != 0)
		return retval;
	retval = hostname;
	rc = getaddrinfo(hostname, nullptr, nullptr, &result);
	if (rc != 0 || result == nullptr)
		return retval;
	/* Name lookup is required, so set that flag */
	rc = getnameinfo(result->ai_addr, result->ai_addrlen, hostname,
	     sizeof(hostname), nullptr, 0, NI_NAMEREQD);
	if (rc != 0)
		goto exit;
	if (hostname[0] != '\0')
		retval = hostname;
 exit:
	if (result)
		freeaddrinfo(result);
	return retval;
}

int main(int argc, char *argv[]) {
	HRESULT hr = hrSuccess;
	int c = 0;
	bool bIgnoreUnknownConfigOptions = false;

	ssl_threading_setup();
	const char *szConfig = ECConfig::GetDefaultPath("gateway.cfg");
	bool exp_config = false;
	static const configsetting_t lpDefaults[] = {
		{ "server_bind", "" },
		{ "run_as_user", "kopano" },
		{ "run_as_group", "kopano" },
		{ "pid_file", "/var/run/kopano/gateway.pid" },
		{ "running_path", "/var/lib/kopano" },
		{ "process_model", "thread" },
		{"coredump_enabled", "systemdefault"},
		{"pop3_enable", "yes", CONFIGSETTING_NONEMPTY},
		{"pop3_port", "110", CONFIGSETTING_NONEMPTY},
		{"pop3s_enable", "no", CONFIGSETTING_NONEMPTY},
		{"pop3s_port", "995", CONFIGSETTING_NONEMPTY},
		{"imap_enable", "yes", CONFIGSETTING_NONEMPTY},
		{"imap_port", "143", CONFIGSETTING_NONEMPTY},
		{"imaps_enable", "no", CONFIGSETTING_NONEMPTY},
		{"imaps_port", "993", CONFIGSETTING_NONEMPTY},
		{ "imap_only_mailfolders", "yes", CONFIGSETTING_RELOADABLE },
		{ "imap_public_folders", "yes", CONFIGSETTING_RELOADABLE },
		{ "imap_capability_idle", "yes", CONFIGSETTING_RELOADABLE },
		{ "imap_always_generate", "no", CONFIGSETTING_UNUSED },
		{ "imap_max_fail_commands", "10", CONFIGSETTING_RELOADABLE },
		{ "imap_max_messagesize", "128M", CONFIGSETTING_RELOADABLE | CONFIGSETTING_SIZE },
		{ "imap_generate_utf8", "no", CONFIGSETTING_UNUSED },
		{ "imap_expunge_on_delete", "no", CONFIGSETTING_RELOADABLE },
		{ "imap_store_rfc822", "", CONFIGSETTING_UNUSED },
		{ "imap_cache_folders_time_limit", "", CONFIGSETTING_UNUSED },
		{ "imap_ignore_command_idle", "no", CONFIGSETTING_RELOADABLE },
		{ "disable_plaintext_auth", "no", CONFIGSETTING_RELOADABLE },
		{ "server_socket", "http://localhost:236/" },
		{ "server_hostname", "" },
		{ "server_hostname_greeting", "no", CONFIGSETTING_RELOADABLE },
		{"ssl_private_key_file", "/etc/kopano/gateway/privkey.pem", CONFIGSETTING_RELOADABLE},
		{"ssl_certificate_file", "/etc/kopano/gateway/cert.pem", CONFIGSETTING_RELOADABLE},
		{"ssl_verify_client", "no", CONFIGSETTING_RELOADABLE},
		{"ssl_verify_file", "", CONFIGSETTING_RELOADABLE},
		{"ssl_verify_path", "", CONFIGSETTING_RELOADABLE},
#ifdef SSL_TXT_SSLV2
		{"ssl_protocols", "!SSLv2", CONFIGSETTING_RELOADABLE},
#else
		{"ssl_protocols", "", CONFIGSETTING_RELOADABLE},
#endif
		{"ssl_ciphers", "ALL:!LOW:!SSLv2:!EXP:!aNULL", CONFIGSETTING_RELOADABLE},
		{"ssl_prefer_server_ciphers", "no", CONFIGSETTING_RELOADABLE},
		{"log_method", "auto", CONFIGSETTING_NONEMPTY},
		{"log_file", ""},
		{"log_level", "3", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{ "log_timestamp", "1" },
		{ "log_buffer_size", "0" },
		{ "tmp_path", "/tmp" },
		{"bypass_auth", "no"},
		{"html_safety_filter", "no"},
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
		{"help", 0, NULL, OPT_HELP},
		{"host", 1, NULL, OPT_HOST},
		{"config", 1, NULL, OPT_CONFIG},
		{"foreground", 1, NULL, OPT_FOREGROUND},
		{ "ignore-unknown-config-options", 0, NULL, OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS },
		{"dump-config", no_argument, nullptr, OPT_DUMP_CONFIG},
		{NULL, 0, NULL, 0}
	};

	// Get commandline options
	while (1) {
		c = my_getopt_long_permissive(argc, argv, "c:h:iuFV", long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case OPT_CONFIG:
		case 'c':
			szConfig = optarg;
			exp_config = true;
			break;
		case OPT_HOST:
		case 'h':
			szPath = optarg;
			break;
		case 'i':				// Install service
		case 'u':				// Uninstall service
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
			cout << "kopano-gateway " PROJECT_VERSION << endl;
			return 1;
		case OPT_HELP:
		default:
			print_help(argv[0]);
			return 1;
		}
	}

	// Setup config
	g_lpConfig = ECConfig::Create(lpDefaults);
	if (!g_lpConfig->LoadSettings(szConfig, !exp_config) ||
	    g_lpConfig->ParseParams(argc - optind, &argv[optind]) < 0 ||
	    (!bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors())) {
		g_lpLogger = new ECLogger_File(EC_LOGLEVEL_INFO, 0, "-", false);	// create logger without a timestamp to stderr
		if (g_lpLogger == nullptr) {
			hr = MAPI_E_NOT_ENOUGH_MEMORY;
			goto exit;
		}
		ec_log_set(g_lpLogger);
		LogConfigErrors(g_lpConfig);
		hr = E_FAIL;
		goto exit;
	}
	if (g_dump_config)
		return g_lpConfig->dump_config(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;

	// Setup logging
	g_lpLogger = CreateLogger(g_lpConfig, argv[0], "KopanoGateway");
	ec_log_set(g_lpLogger);

	if ((bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors()) || g_lpConfig->HasWarnings())
		LogConfigErrors(g_lpConfig);
	if (!TmpPath::instance.OverridePath(g_lpConfig))
		ec_log_err("Ignoring invalid path-setting!");
	if (parseBool(g_lpConfig->GetSetting("bypass_auth")))
		ec_log_warn("Gateway is started with bypass_auth=yes meaning username and password will not be checked.");
	if (strncmp(g_lpConfig->GetSetting("process_model"), "thread", strlen("thread")) == 0) {
		bThreads = true;
		g_lpLogger->SetLogprefix(LP_TID);
	}

	if (bThreads)
		mainthread = pthread_self();

	if (!szPath)
		szPath = g_lpConfig->GetSetting("server_socket");

	g_strHostString = g_lpConfig->GetSetting("server_hostname", NULL, "");
	if (g_strHostString.empty())
		g_strHostString = GetServerFQDN();
	g_strHostString.insert(0, " on ");
	hr = running_service(szPath, argv[0]);

exit:
	if (hr != hrSuccess)
		fprintf(stderr, "%s: Startup failed: %s (%x). Please check the logfile (%s) for details.\n",
			argv[0], GetMAPIErrorMessage(hr), hr, g_lpConfig->GetSetting("log_file"));
	ssl_threading_cleanup();
	delete g_lpConfig;
	DeleteLogger(g_lpLogger);

	return hr == hrSuccess ? 0 : 1;
}

static int gw_listen_on(const char *service, const char *interface,
    const char *port_str, int *fd, int *fdlist, size_t *lsize)
{
	if (port_str == NULL) {
		ec_log_crit("No port selected for %s", service);
		return E_FAIL;
	}
	char *end = NULL;
	uint16_t port = strtoul(port_str, &end, 0);
	if (port == 0 || (end != NULL && *end != '\0')) {
		ec_log_crit("\"%s\" is not an acceptable port number", port_str);
		return E_FAIL;
	}
	HRESULT hr = HrListen(interface, port, fd);
	if (hr != hrSuccess) {
		ec_log_crit("Unable to listen on port %u", port);
		return E_FAIL;
	}
	ec_log_info("Listening on port %u for %s", port, service);
	fdlist[*lsize] = *fd;
	++*lsize;
	return hrSuccess;
}

/**
 * Runs the gateway service, starting a new thread or fork child for
 * incoming connections on any configured service.
 *
 * @param[in]	szPath		Unused, should be removed.
 * @param[in]	servicename	Name of the service, used to create a Unix pidfile.
 */
static HRESULT running_service(const char *szPath, const char *servicename)
{
	HRESULT hr = hrSuccess;
	int ulListenPOP3 = 0, ulListenPOP3s = 0;
	int ulListenIMAP = 0, ulListenIMAPs = 0;
	bool bListenPOP3, bListenPOP3s;
	bool bListenIMAP, bListenIMAPs;
	int pCloseFDs[4] = {0};
	size_t nCloseFDs = 0;
	struct pollfd pollfd[4];
	int nfds = 0, pfd_pop3 = -1, pfd_pop3s = -1, pfd_imap = -1, pfd_imaps = -1;
	int err = 0;
	pthread_attr_t ThreadAttr;
	const char *const interface = g_lpConfig->GetSetting("server_bind");

	// SIGSEGV backtrace support
	stack_t st;
	struct sigaction act;

	memset(&st, 0, sizeof(st));
	memset(&act, 0, sizeof(act));

	if (bThreads) {
		pthread_attr_init(&ThreadAttr);
		if (pthread_attr_setdetachstate(&ThreadAttr, PTHREAD_CREATE_DETACHED) != 0) {
			ec_log_err("Can't set thread attribute to detached");
			goto exit;
		}
		// 1Mb of stack space per thread
		if (pthread_attr_setstacksize(&ThreadAttr, 1024 * 1024)) {
			ec_log_err("Can't set thread stack size to 1Mb");
			goto exit;
		}
	}

	bListenPOP3 = (strcmp(g_lpConfig->GetSetting("pop3_enable"), "yes") == 0);
	bListenPOP3s = (strcmp(g_lpConfig->GetSetting("pop3s_enable"), "yes") == 0);
	bListenIMAP = (strcmp(g_lpConfig->GetSetting("imap_enable"), "yes") == 0);
	bListenIMAPs = (strcmp(g_lpConfig->GetSetting("imaps_enable"), "yes") == 0);

	// Setup SSL context
	if ((bListenPOP3s || bListenIMAPs) &&
	    ECChannel::HrSetCtx(g_lpConfig) != hrSuccess) {
		ec_log_err("Error loading SSL context, POP3S and IMAPS will be disabled");
		bListenPOP3s = false;
		bListenIMAPs = false;
	}
	
	if (!bListenPOP3 && !bListenPOP3s && !bListenIMAP && !bListenIMAPs) {
		ec_log_crit("POP3, POP3S, IMAP and IMAPS are all four disabled");
		hr = E_FAIL;
		goto exit;
	}

	// Setup sockets
	if (bListenPOP3) {
		hr = gw_listen_on("pop3", interface, g_lpConfig->GetSetting("pop3_port"),
		     &ulListenPOP3, pCloseFDs, &nCloseFDs);
		if (hr != hrSuccess)
			goto exit;
	}
	if (bListenPOP3s) {
		hr = gw_listen_on("pop3s", interface, g_lpConfig->GetSetting("pop3s_port"),
		     &ulListenPOP3s, pCloseFDs, &nCloseFDs);
		if (hr != hrSuccess)
			goto exit;
	}
	if (bListenIMAP) {
		hr = gw_listen_on("imap", interface, g_lpConfig->GetSetting("imap_port"),
		     &ulListenIMAP, pCloseFDs, &nCloseFDs);
		if (hr != hrSuccess)
			goto exit;
	}
	if (bListenIMAPs) {
		hr = gw_listen_on("imaps", interface, g_lpConfig->GetSetting("imaps_port"),
		     &ulListenIMAPs, pCloseFDs, &nCloseFDs);
		if (hr != hrSuccess)
			goto exit;
	}

	// Setup signals
	signal(SIGTERM, sigterm);
	signal(SIGINT, sigterm);
	signal(SIGHUP, sighup);
	signal(SIGCHLD, sigchld);
	signal(SIGPIPE, SIG_IGN);
  
    st.ss_sp = malloc(65536);
    st.ss_flags = 0;
    st.ss_size = 65536;
  
	act.sa_sigaction = sigsegv;
	act.sa_flags = SA_ONSTACK | SA_RESETHAND | SA_SIGINFO;
	sigemptyset(&act.sa_mask);
  
    sigaltstack(&st, NULL);
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
    sigaction(SIGABRT, &act, NULL);

    struct rlimit file_limit;
	file_limit.rlim_cur = KC_DESIRED_FILEDES;
	file_limit.rlim_max = KC_DESIRED_FILEDES;
    
	if (setrlimit(RLIMIT_NOFILE, &file_limit) < 0)
		ec_log_warn("setrlimit(RLIMIT_NOFILE, %d) failed, you will only be able to connect up to %d sockets. Either start the process as root, or increase user limits for open file descriptors", KC_DESIRED_FILEDES, getdtablesize());
	unix_coredump_enable(g_lpConfig->GetSetting("coredump_enabled"));

	// fork if needed and drop privileges as requested.
	// this must be done before we do anything with pthreads
	if (unix_runas(g_lpConfig))
		goto exit;
	if (daemonize && unix_daemonize(g_lpConfig))
		goto exit;
	if (!daemonize)
		setsid();
	unix_create_pidfile(servicename, g_lpConfig);
	if (bThreads == false)
		g_lpLogger = StartLoggerProcess(g_lpConfig, g_lpLogger); // maybe replace logger
	ec_log_set(g_lpLogger);

	hr = MAPIInitialize(NULL);
	if (hr != hrSuccess) {
		ec_log_crit("Unable to initialize MAPI: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	ec_log(EC_LOGLEVEL_ALWAYS, "Starting kopano-gateway version " PROJECT_VERSION " (pid %d)", getpid());
	if (bListenPOP3) {
		pfd_pop3 = nfds;
		pollfd[nfds++].fd = ulListenPOP3;
	}
	if (bListenPOP3s) {
		pfd_pop3s = nfds;
		pollfd[nfds++].fd = ulListenPOP3s;
	}
	if (bListenIMAP) {
		pfd_imap = nfds;
		pollfd[nfds++].fd = ulListenIMAP;
	}
	if (bListenIMAPs) {
		pfd_imaps = nfds;
		pollfd[nfds++].fd = ulListenIMAPs;
	}
	for (size_t i = 0; i < nfds; ++i)
		pollfd[i].events = POLLIN | POLLRDHUP;

	// Mainloop
	while (!quit) {
		for (size_t i = 0; i < nfds; ++i)
			pollfd[i].revents = 0;

		err = poll(pollfd, nfds, 10 * 1000);
		if (err < 0) {
			if (errno != EINTR) {
				ec_log_err("Socket error: %s", strerror(errno));
				quit = 1;
				hr = MAPI_E_NETWORK_ERROR;
			}
			continue;
		} else if (err == 0) {
			continue;
		}

		// One socket has signalled a new incoming connection
		std::unique_ptr<HandlerArgs> lpHandlerArgs(new HandlerArgs);
		lpHandlerArgs->lpLogger = g_lpLogger;
		lpHandlerArgs->lpConfig = g_lpConfig;

		bool pop3_event = pfd_pop3 >= 0 && pollfd[pfd_pop3].revents & (POLLIN | POLLRDHUP);
		bool pop3s_event = pfd_pop3s >= 0 && pollfd[pfd_pop3s].revents & (POLLIN | POLLRDHUP);
		if (pop3_event || pop3s_event) {
			bool usessl;

			lpHandlerArgs->type = ST_POP3;

			// Incoming POP3(s) connection
			if (pop3s_event) {
				usessl = true;
				hr = HrAccept(ulListenPOP3s, &lpHandlerArgs->lpChannel);
			} else {
				usessl = false;
				hr = HrAccept(ulListenPOP3, &lpHandlerArgs->lpChannel);
			}
			if (hr != hrSuccess) {
				ec_log_err("Unable to accept POP3 socket connection.");
				// just keep running
				hr = hrSuccess;
				continue;
			}

			lpHandlerArgs->bUseSSL = usessl;

			pthread_t POP3Thread;
			const char *method = usessl ? "POP3s" : "POP3";
			const char *model = bThreads ? "thread" : "process";
			ec_log_notice("Starting worker %s for %s request", model, method);
			if (bThreads) {
				if (pthread_create(&POP3Thread, &ThreadAttr, Handler_Threaded, lpHandlerArgs.get()) != 0) {
					ec_log_err("Can't create %s %s.", method, model);
					// just keep running
					delete lpHandlerArgs->lpChannel;
					lpHandlerArgs.reset();
					hr = hrSuccess;
				}
				else {
					lpHandlerArgs.release();
					++nChildren;
				}

				set_thread_name(POP3Thread, "ZGateway " + std::string(method));
			}
			else {
				if (unix_fork_function(Handler, lpHandlerArgs.get(), nCloseFDs, pCloseFDs) < 0)
					ec_log_err("Can't create %s %s.", method, model);
					// just keep running
				else
					++nChildren;
				// main handler always closes information it doesn't need
				delete lpHandlerArgs->lpChannel;
				hr = hrSuccess;
			}
			continue;
		}

		bool imap_event = pfd_imap >= 0 && pollfd[pfd_imap].revents & (POLLIN | POLLRDHUP);
		bool imaps_event = pfd_imaps >= 0 && pollfd[pfd_imaps].revents & (POLLIN | POLLRDHUP);
		if (imap_event || imaps_event) {
			bool usessl;

			lpHandlerArgs->type = ST_IMAP;

			// Incoming IMAP(s) connection
			if (imaps_event) {
				usessl = true;
				hr = HrAccept(ulListenIMAPs, &lpHandlerArgs->lpChannel);
			} else {
				usessl = false;
				hr = HrAccept(ulListenIMAP, &lpHandlerArgs->lpChannel);
			}
			if (hr != hrSuccess) {
				ec_log_err("Unable to accept IMAP socket connection.");
				// just keep running
				hr = hrSuccess;
				continue;
			}

			lpHandlerArgs->bUseSSL = usessl;

			pthread_t IMAPThread;
			const char *method = usessl ? "IMAPs" : "IMAP";
			const char *model = bThreads ? "thread" : "process";
			ec_log_notice("Starting worker %s for %s request", model, method);
			if (bThreads) {
				if (pthread_create(&IMAPThread, &ThreadAttr, Handler_Threaded, lpHandlerArgs.get()) != 0) {
					ec_log_err("Could not create %s %s.", method, model);
					// just keep running
					delete lpHandlerArgs->lpChannel;
					lpHandlerArgs.reset();
					hr = hrSuccess;
				}
				else {
					lpHandlerArgs.release();
					++nChildren;
				}

				set_thread_name(IMAPThread, "ZGateway " + std::string(method));
			}
			else {
				if (unix_fork_function(Handler, lpHandlerArgs.get(), nCloseFDs, pCloseFDs) < 0)
					ec_log_err("Could not create %s %s.", method, model);
					// just keep running
				else
					++nChildren;
				// main handler always closes information it doesn't need
				delete lpHandlerArgs->lpChannel;
				hr = hrSuccess;
			}
			continue;
		}

		// should not be able to get here because of continues
		ec_log_warn("Incoming traffic was not for me??");
	}

	ec_log(EC_LOGLEVEL_ALWAYS, "POP3/IMAP Gateway will now exit");
	// in forked mode, send all children the exit signal
	if (bThreads == false) {
		signal(SIGTERM, SIG_IGN);
		kill(0, SIGTERM);
	}

	// wait max 10 seconds (init script waits 15 seconds)
	for (int i = 10; nChildren != 0 && i != 0; --i) {
		if (i % 5 == 0)
			ec_log_warn("Waiting for %d processes to exit", nChildren);
		sleep(1);
	}

	if (nChildren)
		ec_log_warn("Forced shutdown with %d processes left", nChildren);
	else
		ec_log_notice("POP3/IMAP Gateway shutdown complete");
	MAPIUninitialize();

exit:
	ECChannel::HrFreeCtx();
	SSL_library_cleanup(); // Remove SSL data for the main application and other related libraries
	if (bThreads)
		pthread_attr_destroy(&ThreadAttr);
	free(st.ss_sp);
	// cleanup ICU data so valgrind is happy
	u_cleanup();
	return hr;
}

/** @} */
