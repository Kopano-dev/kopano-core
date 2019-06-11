/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include <atomic>
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <set>
#include <utility>
#include <climits>
#include <csignal>
#include <netdb.h>
#include <poll.h>
#include <sys/resource.h>
#include <inetmapi/inetmapi.h>
#include <mapi.h>
#include <mapix.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
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
#include <getopt.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include <kopano/MAPIErrors.h>
#include <kopano/ECChannel.h>
#include "charset/localeutil.h"
#include "POP3.h"
#include "IMAP.h"
#include <kopano/ecversion.h>
#include "SSLUtil.h"
#include <kopano/fileutil.hpp>
#include <kopano/UnixUtil.h>
#include <unicode/uclean.h>
#include <openssl/ssl.h>
#include <kopano/hl.hpp>

/**
 * @defgroup gateway Gateway for IMAP and POP3
 * @{
 */

using namespace KC;
using namespace std::string_literals;
using std::cout;
using std::endl;

struct socks {
	std::vector<struct pollfd> pollfd;
	std::vector<int> linfd;
	std::vector<bool> pop3, ssl;
};

int quit = 0;
static bool bThreads, g_dump_config;
static const char *szPath;
static std::shared_ptr<ECLogger> g_lpLogger;
static std::shared_ptr<ECConfig> g_lpConfig;
static pthread_t mainthread;
static std::atomic<int> nChildren{0};
static std::string g_strHostString;
static struct socks g_socks;

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
	ec_log_get()->SetLoglevel(new_ll);
	if (strlen(g_lpConfig->GetSetting("ssl_private_key_file")) > 0 &&
		strlen(g_lpConfig->GetSetting("ssl_certificate_file")) > 0) {
		if (ECChannel::HrSetCtx(g_lpConfig.get()) != hrSuccess)
			ec_log_err("Error reloading SSL context");
		else
			ec_log_info("Reloaded SSL context");
	}
	ec_log_get()->Reset();
	ec_log_info("Log connection was reset");
}

static void sigchld(int)
{
	int stat;
	while (waitpid(-1, &stat, WNOHANG) > 0)
		--nChildren;
}

static HRESULT running_service(char **argv);

static void print_help(const char *name)
{
	cout << "Usage:\n" << endl;
	cout << name << " [-h|--host <serverpath>] [-c|--config <configfile>]" << endl;
	cout << "  -h path\tUse alternate connect path (e.g. file:///var/run/socket).\n\t\tDefault: file:///var/run/kopano/server.sock" << endl;
	cout << "  -V Print version info." << endl;
	cout << "  -c filename\tUse alternate config file (e.g. /etc/kopano-gateway.cfg)\n\t\tDefault: /etc/kopano/gateway.cfg" << endl;
	cout << endl;
}

enum serviceType { ST_POP3 = 0, ST_IMAP };

struct HandlerArgs {
	serviceType type;
	std::unique_ptr<ECChannel> lpChannel;
	std::shared_ptr<ECLogger> lpLogger;
	std::shared_ptr<ECConfig> lpConfig;
	bool bUseSSL;
};

static void *Handler(void *lpArg)
{
	std::unique_ptr<HandlerArgs> lpHandlerArgs(static_cast<HandlerArgs *>(lpArg));
	std::shared_ptr<ECChannel> lpChannel(std::move(lpHandlerArgs->lpChannel));
	auto lpLogger = lpHandlerArgs->lpLogger;
	auto lpConfig = std::move(lpHandlerArgs->lpConfig);
	auto bUseSSL = lpHandlerArgs->bUseSSL;

	// szPath is global, pointing to argv variable, or lpConfig variable
	ClientProto *client;
	if (lpHandlerArgs->type == ST_POP3)
		client = new POP3(szPath, lpChannel, lpConfig);
	else
		client = new IMAP(szPath, lpChannel, lpConfig);
	// not required anymore
	lpHandlerArgs.release();
	// make sure the pipe logger does not exit when this handler exits, but only frees the memory.
	auto pipelog = dynamic_cast<ECLogger_Pipe *>(lpLogger.get());
	if (pipelog != nullptr)
		pipelog->Disown();

	std::string inBuffer;
	HRESULT hr;
	bool bQuit = false;
	int timeouts = 0;

	if (bUseSSL && lpChannel->HrEnableTLS() != hrSuccess) {
		ec_log_err("Unable to negotiate SSL connection");
		goto exit;
	}

	try {
		hr = client->HrSendGreeting(g_strHostString);
	} catch (const KMAPIError &e) {
		hr = e.code();
	}
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
			ec_log_err("Connection closed because of timeout");
			bQuit = true;
			break;
		} else if (hr == MAPI_E_NETWORK_ERROR) {
			ec_log_err("Socket error: %s", strerror(errno));
			bQuit = true;
			break;
		}

		timeouts = 0;
		inBuffer.clear();
		hr = lpChannel->HrReadLine(inBuffer);
		if (hr != hrSuccess) {
			if (errno)
				ec_log_err("Failed to read line: %s", strerror(errno));
			else
				ec_log_err("Client disconnected");
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
			try {
				client->HrProcessContinue(inBuffer);
			} catch (const KMAPIError &e) {
			}
			// no matter what happens, we continue handling the connection.
			continue;
		}

		try {
			/* Process IMAP command */
			hr = client->HrProcessCommand(inBuffer);
		} catch (const KC::KMAPIError &e) {
			hr = e.code();
		}
		if (hr == MAPI_E_NETWORK_ERROR) {
			ec_log_err("Connection error.");
			bQuit = true;
		}
		if (hr == MAPI_E_END_OF_SESSION) {
			ec_log_notice("Disconnecting client.");
			bQuit = true;
		}
	}
exit:
	ec_log_notice("Client %s thread exiting", lpChannel->peer_addr());
	client->HrDone(false);	// HrDone does not send an error string to the client
	delete client;
	/** free SSL error data **/
        #ifdef OLD_API
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
	++nChildren;
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
		{ "run_as_user", "kopano" },
		{ "run_as_group", "kopano" },
		{ "pid_file", "/var/run/kopano/gateway.pid" },
		{ "process_model", "thread" },
		{"coredump_enabled", "systemdefault"},
		{"pop3_listen", "*:110"},
		{"pop3s_listen", ""},
		{"imap_listen", "*:143"},
		{"imaps_listen", ""},
		{"socketspec", "", CONFIGSETTING_OBSOLETE},
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
		{"ssl_protocols", KC_DEFAULT_SSLPROTOLIST, CONFIGSETTING_RELOADABLE},
		{"ssl_ciphers", KC_DEFAULT_CIPHERLIST, CONFIGSETTING_RELOADABLE},
		{"ssl_prefer_server_ciphers", "yes", CONFIGSETTING_RELOADABLE},
		{"ssl_curves", KC_DEFAULT_ECDH_CURVES, CONFIGSETTING_RELOADABLE},
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
		OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS,
		OPT_DUMP_CONFIG,
	};
	static const struct option long_options[] = {
		{"help", 0, NULL, OPT_HELP},
		{"host", 1, NULL, OPT_HOST},
		{"config", 1, NULL, OPT_CONFIG},
		{ "ignore-unknown-config-options", 0, NULL, OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS },
		{"dump-config", no_argument, nullptr, OPT_DUMP_CONFIG},
		{NULL, 0, NULL, 0}
	};

	forceUTF8Locale(true);
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
		case 'F': /* foreground operation */
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
	g_lpConfig.reset(ECConfig::Create(lpDefaults));
	if (!g_lpConfig->LoadSettings(szConfig, !exp_config) ||
	    g_lpConfig->ParseParams(argc - optind, &argv[optind]) < 0 ||
	    (!bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors())) {
		g_lpLogger.reset(new(std::nothrow) ECLogger_File(EC_LOGLEVEL_INFO, 0, "-", false)); // create logger without a timestamp to stderr
		if (g_lpLogger == nullptr) {
			hr = MAPI_E_NOT_ENOUGH_MEMORY;
			goto exit;
		}
		ec_log_set(g_lpLogger);
		LogConfigErrors(g_lpConfig.get());
		hr = E_FAIL;
		goto exit;
	}
	if (g_dump_config)
		return g_lpConfig->dump_config(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;

	// Setup logging
	g_lpLogger.reset(CreateLogger(g_lpConfig.get(), argv[0], "KopanoGateway"));
	ec_log_set(g_lpLogger);
	if ((bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors()) || g_lpConfig->HasWarnings())
		LogConfigErrors(g_lpConfig.get());
	if (!TmpPath::instance.OverridePath(g_lpConfig.get()))
		ec_log_err("Ignoring invalid path-setting!");
	if (parseBool(g_lpConfig->GetSetting("bypass_auth")))
		ec_log_warn("Gateway is started with bypass_auth=yes meaning username and password will not be checked.");
	if (strcmp(g_lpConfig->GetSetting("process_model"), "thread") == 0) {
		bThreads = true;
		g_lpLogger->SetLogprefix(LP_TID);
	}
	mainthread = pthread_self();
	if (!szPath)
		szPath = g_lpConfig->GetSetting("server_socket");
	g_strHostString = g_lpConfig->GetSetting("server_hostname", NULL, "");
	if (g_strHostString.empty())
		g_strHostString = GetServerFQDN();
	g_strHostString.insert(0, " on ");
	hr = running_service(argv);
	ECChannel::HrFreeCtx();
exit:
	if (hr != hrSuccess)
		fprintf(stderr, "%s: Startup failed: %s (%x). Please check the logfile (%s) for details.\n",
			argv[0], GetMAPIErrorMessage(hr), hr, g_lpConfig->GetSetting("log_file"));
	SSL_library_cleanup();
	ssl_threading_cleanup();
	u_cleanup();
	return hr == hrSuccess ? 0 : 1;
}

static HRESULT gw_listen(ECConfig *cfg)
{
	std::set<std::string, ec_bindaddr_less> pop3_sock, pop3s_sock, imap_sock, imaps_sock;
	pop3_sock  = vector_to_set<std::string, ec_bindaddr_less>(tokenize(cfg->GetSetting("pop3_listen"), ' ', true));
	pop3s_sock = vector_to_set<std::string, ec_bindaddr_less>(tokenize(cfg->GetSetting("pop3s_listen"), ' ', true));
	imap_sock  = vector_to_set<std::string, ec_bindaddr_less>(tokenize(cfg->GetSetting("imap_listen"), ' ', true));
	imaps_sock = vector_to_set<std::string, ec_bindaddr_less>(tokenize(cfg->GetSetting("imaps_listen"), ' ', true));

	if ((!pop3s_sock.empty() || !imaps_sock.empty()) &&
	    ECChannel::HrSetCtx(g_lpConfig.get()) != hrSuccess) {
		ec_log_err("Error loading SSL context, POP3S and IMAPS will be disabled");
		pop3s_sock.clear();
		imaps_sock.clear();
	}
	if (pop3_sock.empty() && pop3s_sock.empty() &&
	    imap_sock.empty() && imaps_sock.empty()) {
		ec_log_crit("POP3, POP3S, IMAP and IMAPS are all four disabled");
		return E_FAIL;
	}

	/* Launch */
	struct pollfd pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.events = POLLIN;
	for (const auto &spec : pop3_sock) {
		auto ret = ec_listen_generic(spec.c_str(), &pfd.fd);
		if (ret < 0) {
			ec_log_err("Listening on %s failed: %s", spec.c_str(), strerror(-ret));
			return MAPI_E_NETWORK_ERROR;
		} else if (ret == 0) {
			ec_log_notice("Listening on %s for pop3", spec.c_str());
		} else if (ret == 1) {
			ec_log_info("Re-using fd %d to listen on %s for pop3", ret, spec.c_str());
		}
		g_socks.pollfd.push_back(pfd);
		g_socks.linfd.push_back(pfd.fd);
		g_socks.pop3.push_back(true);
		g_socks.ssl.push_back(false);
	}
	for (const auto &spec : pop3s_sock) {
		auto ret = ec_listen_generic(spec.c_str(), &pfd.fd);
		if (ret < 0) {
			ec_log_err("Listening on %s failed: %s", spec.c_str(), strerror(-ret));
			return MAPI_E_NETWORK_ERROR;
		} else if (ret == 0) {
			ec_log_notice("Listening on %s for pop3s", spec.c_str());
		} else if (ret == 1) {
			ec_log_info("Re-using fd %d to listen on %s for pop3s", ret, spec.c_str());
		}
		g_socks.pollfd.push_back(pfd);
		g_socks.linfd.push_back(pfd.fd);
		g_socks.pop3.push_back(true);
		g_socks.ssl.push_back(true);
	}
	for (const auto &spec : imap_sock) {
		auto ret = ec_listen_generic(spec.c_str(), &pfd.fd);
		if (ret < 0) {
			ec_log_err("Listening on %s failed: %s", spec.c_str(), strerror(-ret));
			return MAPI_E_NETWORK_ERROR;
		} else if (ret == 0) {
			ec_log_notice("Listening on %s for imap", spec.c_str());
		} else if (ret == 1) {
			ec_log_info("Re-using fd %d to listen on %s for imap", ret, spec.c_str());
		}
		g_socks.pollfd.push_back(pfd);
		g_socks.linfd.push_back(pfd.fd);
		g_socks.pop3.push_back(false);
		g_socks.ssl.push_back(false);
	}
	for (const auto &spec : imaps_sock) {
		auto ret = ec_listen_generic(spec.c_str(), &pfd.fd);
		if (ret < 0) {
			ec_log_err("Listening on %s failed: %s", spec.c_str(), strerror(-ret));
			return MAPI_E_NETWORK_ERROR;
		} else if (ret == 0) {
			ec_log_notice("Listening on %s for imaps", spec.c_str());
		} else if (ret == 1) {
			ec_log_info("Re-using fd %d to listen on %s for imaps", ret, spec.c_str());
		}
		g_socks.pollfd.push_back(pfd);
		g_socks.linfd.push_back(pfd.fd);
		g_socks.pop3.push_back(false);
		g_socks.ssl.push_back(true);
	}
	return hrSuccess;
}

static HRESULT handler_client(size_t i)
{
	// One socket has signalled a new incoming connection
	auto lpHandlerArgs = make_unique_nt<HandlerArgs>();
	if (lpHandlerArgs == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	lpHandlerArgs->lpLogger = g_lpLogger;
	lpHandlerArgs->lpConfig = g_lpConfig;
	lpHandlerArgs->type = g_socks.pop3[i] ? ST_POP3 : ST_IMAP;
	lpHandlerArgs->bUseSSL = g_socks.ssl[i];
	const char *method = "", *model = bThreads ? "thread" : "process";

	if (lpHandlerArgs->type == ST_POP3)
		method = lpHandlerArgs->bUseSSL ? "POP3s" : "POP3";
	else if (lpHandlerArgs->type == ST_IMAP)
		method = lpHandlerArgs->bUseSSL ? "IMAPs" : "IMAP";
	auto hr = HrAccept(g_socks.pollfd[i].fd, &unique_tie(lpHandlerArgs->lpChannel));
	if (hr != hrSuccess) {
		ec_log_err("Unable to accept %s socket connection.", method);
		return hr;
	}

	pthread_t tid;
	ec_log_notice("Starting worker %s for %s request", model, method);
	if (!bThreads) {
		++nChildren;
		if (unix_fork_function(Handler, lpHandlerArgs.get(), g_socks.linfd.size(), &g_socks.linfd[0]) < 0) {
			ec_log_err("Could not create %s %s: %s", method, model, strerror(errno));
			--nChildren;
		}
		return MAPI_E_CALL_FAILED;
	}
	pthread_attr_t attr;
	if (pthread_attr_init(&attr) != 0)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
		ec_log_warn("Could not set thread attribute to detached.");
		pthread_attr_destroy(&attr);
		return MAPI_E_CALL_FAILED;
	}
	if (pthread_attr_setstacksize(&attr, 1U << 20)) {
		ec_log_err("Could not set thread stack size to 1Mb");
		pthread_attr_destroy(&attr);
		return MAPI_E_CALL_FAILED;
	}
	auto err = pthread_create(&tid, &attr, Handler_Threaded, lpHandlerArgs.get());
	pthread_attr_destroy(&attr);
	if (err != 0) {
		ec_log_err("Could not create %s %s: %s", method, model, strerror(err));
		return MAPI_E_CALL_FAILED;
	}
	set_thread_name(tid, "net/" + strToLower(method));
	lpHandlerArgs.release();
	return hrSuccess;
}

/**
 * Runs the gateway service, starting a new thread or fork child for
 * incoming connections on any configured service.
 */
static HRESULT running_service(char **argv)
{
	int err = 0;
	ec_log_always("Starting kopano-gateway version " PROJECT_VERSION " (pid %d uid %u)", getpid(), getuid());

	struct rlimit file_limit;
	file_limit.rlim_cur = KC_DESIRED_FILEDES;
	file_limit.rlim_max = KC_DESIRED_FILEDES;
	if (setrlimit(RLIMIT_NOFILE, &file_limit) < 0)
		ec_log_warn("setrlimit(RLIMIT_NOFILE, %d) failed, you will only be able to connect up to %d sockets. Either start the process as root, or increase user limits for open file descriptors", KC_DESIRED_FILEDES, getdtablesize());
	unix_coredump_enable(g_lpConfig->GetSetting("coredump_enabled"));
	auto hr = gw_listen(g_lpConfig.get());
	if (hr != hrSuccess)
		return hr;
	err = unix_runas(g_lpConfig.get());
	if (err < 0) {
		return MAPI_E_CALL_FAILED;
	} else if (err == 0) {
		ec_reexec_finalize();
	} else if (err > 0) {
		ec_reexec_prepare_sockets();
		err = ec_reexec(argv);
		if (err < 0)
			ec_log_notice("K-1240: Failed to re-exec self: %s. "
				"Continuing with restricted coredumps.", strerror(-err));
	}

	struct sigaction act;
	memset(&act, 0, sizeof(act));

	// Setup signals
	signal(SIGPIPE, SIG_IGN);
	sigemptyset(&act.sa_mask);
	act.sa_flags   = SA_ONSTACK | SA_RESETHAND;
	act.sa_handler = sigterm;
	sigaction(SIGTERM, &act, nullptr);
	sigaction(SIGINT, &act, nullptr);
	act.sa_flags   = SA_ONSTACK;
	act.sa_handler = sighup;
	sigaction(SIGHUP, &act, nullptr);
	act.sa_handler = sigchld;
	sigaction(SIGCHLD, &act, nullptr);
	ec_setup_segv_handler("kopano-dagent", PROJECT_VERSION);
	unix_create_pidfile(argv[0], g_lpConfig.get());
	if (!bThreads)
		g_lpLogger = StartLoggerProcess(g_lpConfig.get(), std::move(g_lpLogger)); // maybe replace logger
	ec_log_set(g_lpLogger);

	hr = MAPIInitialize(NULL);
	if (hr != hrSuccess) {
		ec_log_crit("Unable to initialize MAPI: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	// Mainloop
	while (!quit) {
		auto nfds = g_socks.pollfd.size();
		for (size_t i = 0; i < nfds; ++i)
			g_socks.pollfd[i].revents = 0;
		err = poll(&g_socks.pollfd[0], nfds, 10 * 1000);
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
		for (size_t i = 0; i < nfds; ++i) {
			if (!(g_socks.pollfd[i].revents & POLLIN))
				/* OS might set more bits than requested */
				continue;
			handler_client(i);
		}
	}

	ec_log_always("POP3/IMAP Gateway will now exit");
	// in forked mode, send all children the exit signal
	if (!bThreads) {
		signal(SIGTERM, SIG_IGN);
		kill(0, SIGTERM);
	}
	// wait max 10 seconds (init script waits 15 seconds)
	for (int i = 10; nChildren != 0 && i != 0; --i) {
		if (i % 5 == 0)
			ec_log_warn("Waiting for %d processes/threads to exit", nChildren.load());
		sleep(1);
	}
	if (nChildren)
		ec_log_warn("Forced shutdown with %d processes/threads left", nChildren.load());
	else
		ec_log_notice("POP3/IMAP Gateway shutdown complete");
	MAPIUninitialize();
	return hrSuccess;
}

/** @} */
