/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include "config.h"
#include <atomic>
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>
#include <climits>
#include <cstdlib>
#include <poll.h>
#include "mapidefs.h"
#include <mapix.h>
#include <kopano/MAPIErrors.h>
#include "Http.h"
#include "CalDavUtil.h"
#include "iCal.h"
#include "WebDav.h"
#include "CalDavProto.h"
#include "ProtocolBase.h"
#include <csignal>
#include <iostream>
#include <string>
#include <kopano/ECLogger.h>
#include <kopano/ECChannel.h>
#include <kopano/memory.hpp>
#include <kopano/my_getopt.h>
#include <kopano/ecversion.h>
#include <kopano/CommonUtil.h>
#include "charset/localeutil.h"
#include "SSLUtil.h"
#include "fileutil.h"
#include <kopano/UnixUtil.h>
#include <unicode/uclean.h>
#include <openssl/ssl.h>

using namespace KC;
using namespace std::string_literals;

struct HandlerArgs {
    ECChannel *lpChannel;
	bool bUseSSL;
};

struct socks {
	std::vector<struct pollfd> pollfd;
	std::vector<int> linfd;
	std::vector<bool> ssl;
};

static bool g_bDaemonize = true, g_bQuit, g_bThreads, g_dump_config;
static std::shared_ptr<ECLogger> g_lpLogger;
static std::shared_ptr<ECConfig> g_lpConfig;
static pthread_t mainthread;
static std::atomic<int> nChildren{0};
static struct socks g_socks;
static HRESULT ical_listen(ECConfig *cfg);
static HRESULT HrProcessConnections();
static HRESULT HrStartHandlerClient(ECChannel *lpChannel, bool bUseSSL, int nCloseFDs, int *pCloseFDs);
static void *HandlerClient(void *lpArg);
static HRESULT HrHandleRequest(ECChannel *lpChannel);

#define KEEP_ALIVE_TIME 300

static void sigterm(int)
{
	g_bQuit = true;
}

static void sighup(int)
{
	if (g_bThreads && pthread_equal(pthread_self(), mainthread)==0)
		return;
	if (g_lpConfig != nullptr && !g_lpConfig->ReloadSettings() &&
	    g_lpLogger != nullptr)
		ec_log_crit("Unable to reload configuration file, continuing with current settings.");
	if (g_lpLogger) {
		if (g_lpConfig) {
			const char *ll = g_lpConfig->GetSetting("log_level");
			int new_ll = ll ? atoi(ll) : EC_LOGLEVEL_WARNING;
			g_lpLogger->SetLoglevel(new_ll);
		}
		g_lpLogger->Reset();
		ec_log_warn("Log connection was reset");
	}
}

static void sigchld(int)
{
	int stat;
	while (waitpid (-1, &stat, WNOHANG) > 0)
		--nChildren;
}

static void sigsegv(int signr, siginfo_t *si, void *uc)
{
	generic_sigsegv_handler(g_lpLogger.get(), "kopano-ical", PROJECT_VERSION, signr, si, uc);
}

static HRESULT running_service(char **argv)
{
	ec_log(EC_LOGLEVEL_ALWAYS, "Starting kopano-ical version " PROJECT_VERSION " (pid %d uid %u)", getpid(), getuid());
	unix_coredump_enable(g_lpConfig->GetSetting("coredump_enabled"));
	auto hr = ical_listen(g_lpConfig.get());
	if (hr != hrSuccess)
		return hr;
	if (unix_runas(g_lpConfig.get()))
		return MAPI_E_CALL_FAILED;
	ec_reexec_prepare_sockets();
	auto ret = ec_reexec(argv);
	if (ret < 0)
		ec_log_notice("K-1240: Failed to re-exec self: %s. "
			"Continuing with standard allocator and/or restricted coredumps.",
			strerror(-ret));

	// setup signals
	KAlternateStack sigstack;
	struct sigaction act{};
	signal(SIGPIPE, SIG_IGN);
	act.sa_sigaction = sigsegv;
	act.sa_flags = SA_ONSTACK | SA_RESETHAND | SA_SIGINFO;
	sigemptyset(&act.sa_mask);
	sigaction(SIGSEGV, &act, nullptr);
	sigaction(SIGBUS, &act, nullptr);
	sigaction(SIGABRT, &act, nullptr);
	act.sa_flags = SA_RESTART;
	act.sa_handler = sigterm;
	sigaction(SIGTERM, &act, nullptr);
	sigaction(SIGINT, &act, nullptr);
	act.sa_handler = sighup;
	sigaction(SIGHUP, &act, nullptr);
	act.sa_handler = sigchld;
	sigaction(SIGCHLD, &act, nullptr);
	if (g_bDaemonize && unix_daemonize(g_lpConfig.get()))
		return MAPI_E_CALL_FAILED;
	if (!g_bDaemonize)
		setsid();
	unix_create_pidfile(argv[0], g_lpConfig.get());
	if (!g_bThreads)
		g_lpLogger = StartLoggerProcess(g_lpConfig.get(), std::move(g_lpLogger));
	else
		g_lpLogger->SetLogprefix(LP_TID);
	ec_log_set(g_lpLogger);
	ssl_threading_setup();

	AutoMAPI mapiinit;
	hr = mapiinit.Initialize();
	if (hr != hrSuccess) {
		kc_perror("Messaging API could not be initialized", hr);
		return hr;
	}
	mainthread = pthread_self();
	hr = HrProcessConnections();
	if (hr != hrSuccess)
		return hr;
	ec_log_info("CalDAV Gateway will now exit");

	// in forked mode, send all children the exit signal
	if (!g_bThreads) {
		signal(SIGTERM, SIG_IGN);
		kill(0, SIGTERM);
		int i = 30; /* wait max 30 seconds */
		while (nChildren && i) {
			if (i % 5 == 0)
				ec_log_notice("Waiting for %d processes/threads to exit", nChildren.load());
			sleep(1);
			--i;
		}

		if (nChildren)
			ec_log_notice("Forced shutdown with %d processes/threads left", nChildren.load());
		else
			ec_log_info("CalDAV Gateway shutdown complete");
	}
	return hrSuccess;
}

using std::cout;
using std::endl;

static void PrintHelp(const char *name)
{
	cout << "Usage:\n" << endl;
	cout << name << " [-h] [-F] [-V] [-c <configfile>]" << endl;
	cout << "  -F\t\tDo not run in the background" << endl;
	cout << "  -h\t\tShows this help." << endl;
	cout << "  -V\t\tPrint version info." << endl;
	cout << "  -c filename\tUse alternate config file (e.g. /etc/kopano/ical.cfg)\n\t\tDefault: /etc/kopano/ical.cfg" << endl;
	cout << endl;
}

static void PrintVersion(void)
{
	cout << "kopano-ical " PROJECT_VERSION << endl;
}

int main(int argc, char **argv) {
	HRESULT hr = hrSuccess;
	bool bIgnoreUnknownConfigOptions = false;
	// Configuration
	const char *lpszCfg = ECConfig::GetDefaultPath("ical.cfg");
	bool exp_config = false;
	static const configsetting_t lpDefaults[] = {
		{ "run_as_user", "kopano" },
		{ "run_as_group", "kopano" },
		{ "pid_file", "/var/run/kopano/ical.pid" },
		{"running_path", "/var/lib/kopano/empty", CONFIGSETTING_OBSOLETE},
		{ "process_model", "thread" },
		{ "server_bind", "" },
		{"coredump_enabled", "systemdefault"},
		{"socketspec", "v1", CONFIGSETTING_NONEMPTY},
		{"ical_listen", "*:8080"},
		{"icals_listen", ""},
		{"ical_port", "8080", CONFIGSETTING_NONEMPTY | CONFIGSETTING_OBSOLETE},
		{"ical_enable", "yes", CONFIGSETTING_NONEMPTY | CONFIGSETTING_OBSOLETE},
		{"icals_port", "8443", CONFIGSETTING_NONEMPTY | CONFIGSETTING_OBSOLETE},
		{"icals_enable", "no", CONFIGSETTING_NONEMPTY | CONFIGSETTING_OBSOLETE},
		{ "enable_ical_get", "yes", CONFIGSETTING_RELOADABLE },
		{ "server_socket", "http://localhost:236/" },
		{ "server_timezone","Europe/Amsterdam"},
		{ "default_charset","utf-8"},
		{"log_method", "auto", CONFIGSETTING_NONEMPTY},
		{"log_file", ""},
		{"log_level", "3", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{ "log_timestamp", "1" },
		{ "log_buffer_size", "0" },
        { "ssl_private_key_file", "/etc/kopano/ical/privkey.pem" },
        { "ssl_certificate_file", "/etc/kopano/ical/cert.pem" },
		{"ssl_protocols", KC_DEFAULT_SSLPROTOLIST},
		{"ssl_ciphers", KC_DEFAULT_CIPHERLIST},
		{"ssl_prefer_server_ciphers", "yes"},
		{"ssl_curves", KC_DEFAULT_ECDH_CURVES},
        { "ssl_verify_client", "no" },
        { "ssl_verify_file", "" },
        { "ssl_verify_path", "" },
		{ "tmp_path", "/tmp" },
		{ NULL, NULL },
	};
	enum {
		OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS = UCHAR_MAX + 1,
		OPT_DUMP_CONFIG,
	};
	static const struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"config", required_argument, NULL, 'c'},
		{"version", no_argument, NULL, 'v'},
		{"foreground", no_argument, NULL, 'F'},
		{"ignore-unknown-config-options", 0, NULL, OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS },
		{"dump-config", no_argument, nullptr, OPT_DUMP_CONFIG},
		{NULL, 0, NULL, 0}
	};

	forceUTF8Locale(true);
	while (1) {
		int opt = my_getopt_long_permissive(argc, argv, "Fhc:V", long_options, nullptr);
		if (opt == -1)
			break;

		switch (opt) {
		case 'c':
			lpszCfg = optarg;
			exp_config = true;
			break;
		case 'F':
			g_bDaemonize = false;
			break;
		case OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS:
			bIgnoreUnknownConfigOptions = true;
			break;
		case OPT_DUMP_CONFIG:
			g_dump_config = true;
			break;
		case 'V':
			PrintVersion();
			goto exit;
		case 'h':
		default:
			PrintHelp(argv[0]);
			goto exit;
		}
	}

	// init xml parser
	xmlInitParser();
	g_lpConfig.reset(ECConfig::Create(lpDefaults));
	if (!g_lpConfig->LoadSettings(lpszCfg, !exp_config) ||
	    g_lpConfig->ParseParams(argc - optind, &argv[optind]) < 0 ||
	    (!bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors())) {
		g_lpLogger.reset(new(std::nothrow) ECLogger_File(1, 0, "-", false));
		if (g_lpLogger == nullptr) {
			hr = MAPI_E_NOT_ENOUGH_MEMORY;
			goto exit;
		}
		ec_log_set(g_lpLogger);
		LogConfigErrors(g_lpConfig.get());
		goto exit;
	}
	if (g_dump_config)
		return g_lpConfig->dump_config(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	g_lpLogger.reset(CreateLogger(g_lpConfig.get(), argv[0], "KopanoICal"));
	if (!g_lpLogger) {
		fprintf(stderr, "Error loading configuration or parsing commandline arguments.\n");
		goto exit;
	}
	ec_log_set(g_lpLogger);
	if ((bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors()) || g_lpConfig->HasWarnings())
		LogConfigErrors(g_lpConfig.get());
	if (!TmpPath::instance.OverridePath(g_lpConfig.get()))
		ec_log_err("Ignoring invalid path-setting!");
	if (strcmp(g_lpConfig->GetSetting("process_model"), "thread") == 0) {
		g_bThreads = true;
		g_lpLogger->SetLogprefix(LP_TID);
	}

	hr = running_service(argv);
exit:
	ECChannel::HrFreeCtx();
	SSL_library_cleanup(); // Remove SSL data for the main application and other related libraries
	// Cleanup SSL parts
	ssl_threading_cleanup();
	// Cleanup libxml2 library
	xmlCleanupParser();
	// cleanup ICU data so valgrind is happy
	u_cleanup();
	return hr == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}

static HRESULT ical_listen(ECConfig *cfg)
{
	std::set<std::string, ec_bindaddr_less> ical_sock, icals_sock;
	auto ss = cfg->GetSetting("socketspec");
	if (strcmp(ss, "v2") == 0) {
		ical_sock  = vector_to_set<std::string, ec_bindaddr_less>(tokenize(cfg->GetSetting("ical_listen"), ' ', true));
		icals_sock = vector_to_set<std::string, ec_bindaddr_less>(tokenize(cfg->GetSetting("icals_listen"), ' ', true));
	} else if (strcmp(ss, "v1") == 0) {
		auto addr = cfg->GetSetting("server_bind");
		if (parseBool(cfg->GetSetting("ical_enable"))) {
			auto port = cfg->GetSetting("ical_port");
			if (port[0] != '\0')
				ical_sock.emplace("["s + addr + "]:" + port);
		}
		if (parseBool(cfg->GetSetting("icals_enable"))) {
			auto port = cfg->GetSetting("icals_port");
			if (port[0] != '\0')
				icals_sock.emplace("["s + addr + "]:" + port);
		}
	}

	if (!icals_sock.empty()) {
		auto hr = ECChannel::HrSetCtx(g_lpConfig.get());
		if (hr != hrSuccess) {
			kc_perror("Error loading SSL context, ICALS will be disabled", hr);
			icals_sock.clear();
		}
	}

	/* Launch */
	struct pollfd pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.events = POLLIN;
	for (const auto &spec : ical_sock) {
		auto ret = ec_fdtable_socket(spec.c_str(), nullptr, nullptr);
		if (ret >= 0)
			pfd.fd = ret;
		else
			ret = ec_listen_generic(spec.c_str(), &pfd.fd);
		if (ret < 0)
			return MAPI_E_NETWORK_ERROR;
		g_socks.pollfd.push_back(pfd);
		g_socks.linfd.push_back(pfd.fd);
		g_socks.ssl.push_back(false);
	}
	for (const auto &spec : icals_sock) {
		auto ret = ec_fdtable_socket(spec.c_str(), nullptr, nullptr);
		if (ret >= 0)
			pfd.fd = ret;
		else
			ret = ec_listen_generic(spec.c_str(), &pfd.fd);
		if (ret < 0)
			return MAPI_E_NETWORK_ERROR;
		g_socks.pollfd.push_back(pfd);
		g_socks.linfd.push_back(pfd.fd);
		g_socks.ssl.push_back(true);
	}
	return hrSuccess;
}

/**
 * Listen to the passed sockets and calls HrStartHandlerClient for
 * every incoming connection.
 *
 * @retval MAPI error code
 */
static HRESULT HrProcessConnections()
{
	HRESULT hr = hrSuccess;
	ECChannel *lpChannel = NULL;

	// main program loop
	while (!g_bQuit) {
		auto nfds = g_socks.pollfd.size();
		for (size_t i = 0; i < nfds; ++i)
			g_socks.pollfd[i].revents = 0;

		// Check whether there are incoming connections.
		auto err = poll(&g_socks.pollfd[0], nfds, 10 * 1000);
		if (err < 0) {
			if (errno != EINTR) {
				ec_log_crit("An unknown socket error has occurred.");
				g_bQuit = true;
				hr = MAPI_E_NETWORK_ERROR;
			}
			continue;
		} else if (err == 0) {
			continue;
		}

		if (g_bQuit) {
			hr = hrSuccess;
			break;
		}

		for (size_t i = 0; i < nfds; ++i) {
			if (!(g_socks.pollfd[i].revents & POLLIN))
				/* OS might set more bits than requested */
				continue;

			hr = HrAccept(g_socks.linfd[i], &lpChannel);
			if (hr != hrSuccess) {
				kc_perror("Could not accept incoming connection", hr);
				continue;
			}

		hr = HrStartHandlerClient(lpChannel, g_socks.ssl[i], g_socks.linfd.size(), &g_socks.linfd[0]);
		if (hr != hrSuccess) {
			delete lpChannel;	// destructor closes sockets
			kc_perror("Handling client connection failed", hr);
			continue;
		}
		if (!g_bThreads)
			delete lpChannel;	// always cleanup channel in main process
		}
	}

	return hr;
}

/**
 * Starts a new thread or forks a child to process the incoming
 * connection.
 *
 * @param[in]	lpChannel	The accepted connection in ECChannel object
 * @param[in]	bUseSSL		The ECChannel object is an SSL connection
 * @param[in]	nCloseFDs	Number of FDs in pCloseFDs, used on forks only
 * @param[in]	pCloseFDs	Array of FDs to close in child process
 * @retval E_FAIL when thread of child did not start
 */
static HRESULT HrStartHandlerClient(ECChannel *lpChannel, bool bUseSSL,
    int nCloseFDs, int *pCloseFDs)
{
	pthread_attr_t pThreadAttr;
	pthread_t pThread;
	auto lpHandlerArgs = make_unique_nt<HandlerArgs>();
	if (lpHandlerArgs == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	lpHandlerArgs->lpChannel = lpChannel;
	lpHandlerArgs->bUseSSL = bUseSSL;

	if (!g_bThreads) {
		++nChildren;
		if (unix_fork_function(HandlerClient, lpHandlerArgs.get(), nCloseFDs, pCloseFDs) < 0) {
			ec_log_err("Could not create ZCalDAV process: %s", strerror(errno));
			--nChildren;
			return E_FAIL;
		}
		return hrSuccess;
	}
	if (pthread_attr_init(&pThreadAttr) != 0)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	if (pthread_attr_setdetachstate(&pThreadAttr, PTHREAD_CREATE_DETACHED) != 0)
		ec_log_warn("Could not set thread attribute to detached.");
	++nChildren;
	auto ret = pthread_create(&pThread, &pThreadAttr, HandlerClient, lpHandlerArgs.get());
	pthread_attr_destroy(&pThreadAttr);
	if (ret != 0) {
		--nChildren;
		ec_log_err("Could not create ZCalDAV thread: %s", strerror(ret));
		return E_FAIL;
	} else {
		lpHandlerArgs.release();
	}
	set_thread_name(pThread, std::string("ZCalDAV") + lpChannel->peer_addr());
	return hrSuccess;
}

static void *HandlerClient(void *lpArg)
{
	HRESULT hr = hrSuccess;
	auto lpHandlerArgs = static_cast<HandlerArgs *>(lpArg);
	ECChannel *lpChannel = lpHandlerArgs->lpChannel;
	bool bUseSSL = lpHandlerArgs->bUseSSL;

	delete lpHandlerArgs;

	if (bUseSSL && lpChannel->HrEnableTLS() != hrSuccess) {
		ec_log_err("Unable to negotiate SSL connection");
		goto exit;
    }

	while (!g_bQuit) {
		hr = lpChannel->HrSelect(KEEP_ALIVE_TIME);
		if (hr == MAPI_E_CANCEL)
			/* signalled - reevaluate g_bQuit */
			continue;
		if (hr != hrSuccess) {
			ec_log_info("Request timeout, closing connection");
			break;
		}
		//Save mapi session between Requests
		hr = HrHandleRequest(lpChannel);
		if (hr != hrSuccess)
			break;
	}

exit:
	ec_log_info("Connection closed");
	delete lpChannel;
	return NULL;
}

static HRESULT HrHandleRequest(ECChannel *lpChannel)
{
	std::wstring wstrUser, wstrPass;
	std::string strUrl, strMethod, strCharset;
	std::string strServerTZ = g_lpConfig->GetSetting("server_timezone");
	std::string strUserAgent, strUserAgentVersion;
	KC::object_ptr<IMAPISession> lpSession;
	Http lpRequest(lpChannel, g_lpConfig);
	std::unique_ptr<ProtocolBase> lpBase;
	ULONG ulFlag = 0;

	ec_log_debug("New Request");
	auto hr = lpRequest.HrReadHeaders();
	if(hr != hrSuccess) {
		hr = MAPI_E_USER_CANCEL; // connection is closed by client no data to be read
		goto exit;
	}
	hr = lpRequest.HrValidateReq();
	if(hr != hrSuccess) {
		lpRequest.HrResponseHeader(501, "Not Implemented");
		lpRequest.HrResponseBody("\nRequest not implemented");
		goto exit;
	}

	// ignore Empty Body
	lpRequest.HrReadBody();
	// no error, defaults to UTF-8
	lpRequest.HrGetCharSet(&strCharset);
	// ignore Empty User field.
	lpRequest.HrGetUser(&wstrUser);
	// ignore Empty Password field
	lpRequest.HrGetPass(&wstrPass);
	// no checks required as HrValidateReq() checks Method
	lpRequest.HrGetMethod(&strMethod);
	//
	lpRequest.HrSetKeepAlive(KEEP_ALIVE_TIME);
	lpRequest.HrGetUserAgent(&strUserAgent);
	lpRequest.HrGetUserAgentVersion(&strUserAgentVersion);

	hr = lpRequest.HrGetUrl(&strUrl);
	if (hr != hrSuccess) {
		ec_log_debug("URl is empty for method %s", strMethod.c_str());
		lpRequest.HrResponseHeader(400,"Bad Request");
		lpRequest.HrResponseBody("Bad Request");
		goto exit;
	}
	hr = HrParseURL(strUrl, &ulFlag);
	if (hr != hrSuccess) {
		kc_perror("Client request is invalid", hr);
		lpRequest.HrResponseHeader(400, "Bad Request: " + stringify_hex(hr));
		goto exit;
	}
	if (ulFlag & SERVICE_CALDAV)
		// this header is always present in a caldav response, but not in ical.
		lpRequest.HrResponseHeader("DAV", "1, access-control, calendar-access, calendar-schedule, calendarserver-principal-property-search");

	if(!strMethod.compare("OPTIONS"))
	{
		lpRequest.HrResponseHeader(200, "OK");
		// @todo, if ical get is disabled, do not add GET as allowed option
		// @todo, should check write access on url and only return read actions if not available
		lpRequest.HrResponseHeader("Allow", "OPTIONS, GET, POST, PUT, DELETE, MOVE");
		lpRequest.HrResponseHeader("Allow", "PROPFIND, PROPPATCH, REPORT, MKCALENDAR");
		// most clients do not login with this action, no need to complain.
		hr = hrSuccess;
		goto exit;
	}

	if (wstrUser.empty() || wstrPass.empty()) {
		ec_log_info("Sending authentication request");
		hr = MAPI_E_CALL_FAILED;
	} else {
		lpRequest.HrGetMethod(&strMethod);
		hr = HrAuthenticate(strUserAgent, strUserAgentVersion, wstrUser, wstrPass, g_lpConfig->GetSetting("server_socket"), &~lpSession);
		if (hr != hrSuccess)
			ec_log_warn("Login failed (0x%08X %s), resending authentication request", hr, GetMAPIErrorMessage(hr));
	}
	if (hr != hrSuccess) {
		if(ulFlag & SERVICE_ICAL)
			lpRequest.HrRequestAuth("Kopano iCal Gateway");
		else
			lpRequest.HrRequestAuth("Kopano CalDav Gateway");
		hr = hrSuccess; //keep connection open.
		goto exit;
	}

	//GET & ical Requests
	// @todo fix caldav GET request
	static_assert(std::is_polymorphic<ProtocolBase>::value, "ProtocolBase needs to be polymorphic for unique_ptr to work");
	if( !strMethod.compare("GET") || !strMethod.compare("HEAD") || ((ulFlag & SERVICE_ICAL) && strMethod.compare("PROPFIND")) )
	{
		lpBase.reset(new iCal(lpRequest, lpSession, strServerTZ, strCharset));
	}
	//CALDAV Requests
	else if((ulFlag & SERVICE_CALDAV) || ( !strMethod.compare("PROPFIND") && !(ulFlag & SERVICE_ICAL)))
	{
		lpBase.reset(new CalDAV(lpRequest, lpSession, strServerTZ, strCharset));
	}
	else
	{
		hr = MAPI_E_CALL_FAILED;
		lpRequest.HrResponseHeader(404, "Request not valid for ical or caldav services");
		goto exit;
	}

	hr = lpBase->HrInitializeClass();
	if (hr != hrSuccess) {
		if (hr != MAPI_E_NOT_ME)
			hr = lpRequest.HrToHTTPCode(hr);
		goto exit;
	}
	hr = lpBase->HrHandleCommand(strMethod);
exit:
	if(hr != hrSuccess && !strMethod.empty() && hr != MAPI_E_NOT_ME)
		ec_log_err("Error processing %s request, error code 0x%08x %s", strMethod.c_str(), hr, GetMAPIErrorMessage(hr));
	if (hr != MAPI_E_USER_CANCEL) // do not send response to client if connection closed by client.
		hr = lpRequest.HrFinalize();
	ec_log_debug("End Of Request");
	return hr;
}
