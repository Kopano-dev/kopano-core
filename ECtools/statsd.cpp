/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright 2018, Kopano and its licensors
 */
#include "config.h"
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <string>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <json/reader.h>
#include <libHX/option.h>
#include <microhttpd.h>
#include <rrd.h>
#include <kopano/ECChannel.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ecversion.h>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>
#include <kopano/UnixUtil.h>

using namespace std::string_literals;
using namespace KC;

struct cis_block {
	std::string udata;
};

static const char *opt_config_file;
static std::shared_ptr<ECConfig> sd_config;
static std::condition_variable sd_cond_exit;
static bool sd_quit;

static constexpr const struct HXoption sd_options[] = {
	{nullptr, 'c', HXTYPE_STRING, &opt_config_file, nullptr, nullptr, 0, "Specify alternate config file"},
	HXOPT_AUTOHELP,
	HXOPT_TABLEEND,
};

static constexpr const configsetting_t sd_config_defaults[] = {
	{"statsd_listen", "unix:/var/run/kopano/statsd.sock"},
	{"statsd_rrd", "/var/lib/kopano/rrd"},
	{"run_as_user", "kopano"},
	{"run_as_group", "kopano"},
	{nullptr},
};

static const char *sd_member_to_dstype(const Json::Value &v)
{
	if (v.type() != Json::objectValue)
		return "";
	auto w = v["mode"];
	if (w.type() != Json::stringValue)
		return "";
	if (w == "counter")
		return "COUNTER";
	if (w == "gauge")
		return "GAUGE";
	return "";
}

static bool sd_create_rrd(const Json::Value &node, const char *file)
{
	struct stat sb;
	if (stat(file, &sb) == 0)
		return true;
	if (errno != ENOENT) {
		ec_log_err("%s: %s", file, strerror(errno));
		return false;
	}
	auto dstype = sd_member_to_dstype(node);
	if (dstype == nullptr)
		return false;
	auto ds = "DS:value:"s + sd_member_to_dstype(node) + ":120:0:18446744073709551615";
	const char *args[] = {ds.c_str(), "RRA:LAST:0.5:1:10080", nullptr};
	auto rc = rrd_create_r(file, 60, -1, 2, args);
	if (rc == EXIT_SUCCESS)
		return true;
	ec_log_warn("rrd_create_r \"%s\" failed: %s", file, rrd_get_error());
	return false;
}

static bool sd_record(Json::Value &&root)
{
	std::string machine_id;
	try {
		if (root["version"].asInt() != 2)
			return false;
		machine_id = root["stats"]["machine_id"]["value"].asCString();
	} catch (...) {
		return false;
	}

	if (machine_id.empty())
		return false;

	for (const auto &member : root["stats"].getMemberNames()) {
		auto node = root["stats"][member];
		if (node.type() != Json::objectValue)
			continue;
		auto mtype = node["type"];
		auto value = node["value"];
		if (mtype.type() != Json::stringValue)
			continue;
		if (mtype != "int" && mtype != "real" && mtype != "unixtime")
			continue;
		if (value.type() != Json::intValue && value.type() != Json::uintValue &&
		    value.type() != Json::realValue)
			continue;

		auto file = sd_config->GetSetting("statsd_rrd") + "/"s + machine_id + "-" + member;
		if (!sd_create_rrd(node, file.c_str()))
			continue;
		auto nv = "N:" + value.asString();
		const char *args[] = {nv.c_str(), nullptr};
		auto rc = rrd_update_r(file.c_str(), nullptr, 1, args);
		if (rc != EXIT_SUCCESS)
			ec_log_warn("rrd_update \"%s\" failed: %s", file.c_str(), rrd_get_error());
	}
	return true;
}

static int sd_req_accept(void *cls, struct MHD_Connection *conn,
    const char *url, const char *method, const char *version,
    const char *upload_data, size_t *upload_size, void **conn_cls)
{
	auto rs = MHD_create_response_from_buffer(0, const_cast<char *>(""), MHD_RESPMEM_PERSISTENT);
	if (rs == nullptr)
		return MHD_NO;
	auto rsclean = make_scope_success([&]() { MHD_destroy_response(rs); });

	if (strcmp(method, "POST") != 0)
		return MHD_queue_response(conn, MHD_HTTP_METHOD_NOT_ALLOWED, rs);
	auto enc = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONTENT_TYPE);
	if (enc == nullptr || strcmp(enc, "application/json") != 0)
		return MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, rs);

	auto udata = static_cast<std::string *>(*conn_cls);
	if (udata == nullptr) {
		*conn_cls = udata = new(std::nothrow) std::string;
		return udata != nullptr ? MHD_YES : MHD_NO;
	}
	if (*upload_size > 0) {
		udata->append(upload_data, *upload_size);
		*upload_size = 0;
		return MHD_YES;
	}

	Json::Value root;
	std::istringstream sin(std::move(*udata));
	auto valid_json = Json::parseFromStream(Json::CharReaderBuilder(), sin, &root, nullptr);
	delete udata;
	*conn_cls = nullptr;
	if (!valid_json)
		return MHD_queue_response(conn, MHD_HTTP_BAD_REQUEST, rs);

	sd_record(std::move(root));
	return MHD_queue_response(conn, MHD_HTTP_OK, rs);
}

static void sd_req_complete(void *cls, struct MHD_Connection *conn,
    void **conn_cls, enum MHD_RequestTerminationCode tcode)
{
	if (conn_cls == nullptr)
		return;
	auto cis = static_cast<std::string *>(*conn_cls);
	delete cis;
	*conn_cls = nullptr;
}

static HRESULT sd_mainloop(int sockfd)
{
	unsigned int flags = MHD_USE_POLL;
#if defined(HAVE_EPOLL_CREATE) && defined(MHD_VERSION) && MHD_VERSION >= 0x00095000
	flags = MHD_USE_EPOLL;
#endif
	flags |= MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG;
	auto daemon = MHD_start_daemon(flags, 0, nullptr, nullptr,
	              sd_req_accept, nullptr,
	              MHD_OPTION_NOTIFY_COMPLETED, sd_req_complete, nullptr,
	              MHD_OPTION_LISTEN_SOCKET, sockfd, MHD_OPTION_END);
	if (daemon == nullptr)
		return MAPI_E_CALL_FAILED;
	while (!sd_quit) {
		std::mutex mtx;
		std::unique_lock<std::mutex> lk(mtx);
		sd_cond_exit.wait(lk);
	}
	MHD_stop_daemon(daemon);
	return 0;
}

static bool sd_parse_options(int &argc, const char **&argv)
{
	sd_config.reset(ECConfig::Create(sd_config_defaults));
	if (HX_getopt(sd_options, &argc, &argv, HXOPT_USAGEONERR) != HXOPT_ERR_SUCCESS)
		return false;
	if (opt_config_file != nullptr) {
		sd_config->LoadSettings(opt_config_file);
		if (sd_config->HasErrors()) {
			fprintf(stderr, "Error reading config file %s\n", opt_config_file);
			LogConfigErrors(sd_config.get());
			return false;
		}
	} else {
		sd_config->LoadSettings(ECConfig::GetDefaultPath("statsd.cfg"));
	}
	return true;
}

static void sd_segv(int nr, siginfo_t *si, void *uc)
{
	generic_sigsegv_handler(ec_log_get(), "kopano-statsd", PROJECT_VERSION, nr, si, uc);
}

static void sd_term(int)
{
	sd_quit = true;
	sd_cond_exit.notify_all();
}

int main(int argc, const char **argv) try
{
	setlocale(LC_ALL, "");
	ec_log_get()->SetLoglevel(EC_LOGLEVEL_INFO);
	if (!sd_parse_options(argc, argv))
		return EXIT_FAILURE;

	ec_log_always("Starting kopano-statsd " PACKAGE_VERSION " (uid %u)", getuid());
	KAlternateStack kstk;
	struct sigaction act{};
	act.sa_sigaction = sd_segv;
	act.sa_flags = SA_ONSTACK | SA_RESETHAND | SA_SIGINFO;
	sigemptyset(&act.sa_mask);
	sigaction(SIGSEGV, &act, nullptr);
	sigaction(SIGBUS, &act, nullptr);
	sigaction(SIGABRT, &act, nullptr);
	act.sa_flags = SA_ONSTACK | SA_RESETHAND;
	act.sa_handler = sd_term;
	sigaction(SIGINT, &act, nullptr);
	sigaction(SIGTERM, &act, nullptr);

	auto v = vector_to_set<std::string, ec_bindaddr_less>(tokenize(sd_config->GetSetting("statsd_listen"), ' ', true));
	if (v.size() > 1) {
		ec_log_err("Only one socket can be specified in statsd_listen at this time\n");
		return EXIT_FAILURE;
	}
	int sockfd;
	auto ret = ec_listen_generic(v.begin()->c_str(), &sockfd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (ret < 0)
		return ret;
	if (unix_runas(sd_config.get()))
		return EXIT_FAILURE;
	ec_reexec_prepare_sockets();
	ret = ec_reexec(argv);
	if (ret < 0)
		ec_log_notice("K-1240: Failed to re-exec self: %s", strerror(-ret));

	return sd_mainloop(sockfd) == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
} catch (...) {
	std::terminate();
}
