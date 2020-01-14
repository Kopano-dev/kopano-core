/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright 2018, Kopano and its licensors
 */
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/stat.h>
#include <json/reader.h>
#include <libHX/option.h>
#include <rrd.h>
#include <mapicode.h>
#include <mapidefs.h>
#include <stdsoap2.h>
#include <kopano/ECChannel.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ecversion.h>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>
#include <kopano/UnixUtil.h>
#include "../provider/soap/soap.nsmap"

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
	{"coredump_enabled", "systemdefault"},
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

static void sd_handle_request(struct soap &&x)
{
	soap_begin(&x);
	if (soap_begin_recv(&x) != 0)
		return;
	if (x.status != SOAP_POST) {
		if (soap_end_recv(&x) != 0)
			return;
		soap_send_empty_response(&x, 501);
		return;
	}
	/* check for application/json content type */
	auto data = soap_http_get_body(&x, nullptr);
	if (soap_end_recv(&x) != 0)
		return;
	Json::Value root;
	std::istringstream sin(data);
	auto valid_json = Json::parseFromStream(Json::CharReaderBuilder(), sin, &root, nullptr);
	if (!valid_json) {
		soap_send_empty_response(&x, 415);
		return;
	}
	auto ok = sd_record(std::move(root));
	soap_send_empty_response(&x, ok ? 202 : 400);
	x.destroy();
}

static void sd_check_sockets(std::vector<struct pollfd> &pollfd)
{
	for (size_t i = 0; i < pollfd.size(); ++i) {
		if (!(pollfd[i].revents & POLLIN))
			continue;
		struct soap x;
		int domain = AF_UNSPEC;
		socklen_t dlen = sizeof(domain);
		if (getsockopt(pollfd[i].fd, SOL_SOCKET, SO_DOMAIN, &domain, &dlen) == 0 &&
		    domain != AF_LOCAL) {
			x.master = pollfd[i].fd;
			soap_accept(&x);
			x.master = -1;
		} else {
			socklen_t peerlen = sizeof(x.peer.addr);
			x.socket = accept(pollfd[i].fd, &x.peer.addr, &peerlen);
			x.peerlen = peerlen;
			if (x.socket == SOAP_INVALID_SOCKET ||
			    peerlen > sizeof(x.peer.storage)) {
				x.peerlen = 0;
				memset(&x.peer, 0, sizeof(x.peer));
			}
			/* Do like gsoap's soap_accept would */
			x.keep_alive = -(((x.imode | x.omode) & SOAP_IO_KEEPALIVE) != 0);
		}
		sd_handle_request(std::move(x));
	}
}

static void sd_mainloop(std::vector<struct pollfd> &&sk)
{
	while (!sd_quit) {
		auto n = poll(&sk[0], sk.size(), 86400 * 1000);
		if (n < 0)
			continue; /* signalled */
		sd_check_sockets(sk);
	}
}

static HRESULT sd_listen(ECConfig *cfg, std::vector<struct pollfd> &pollfd)
{
	auto info = ec_bindspec_to_sockets(tokenize(sd_config->GetSetting("statsd_listen"), ' ', true),
	            S_IRWUG, cfg->GetSetting("run_as_user"), cfg->GetSetting("run_as_group"));
	if (info.first < 0)
		return EXIT_FAILURE;

	struct pollfd pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.events = POLLIN;
	for (auto &spec : info.second) {
		pfd.fd = spec.m_fd;
		spec.m_fd = -1;
		pollfd.push_back(pfd);
	}
	return hrSuccess;
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

static void sd_term(int)
{
	sd_quit = true;
	sd_cond_exit.notify_all();
}

int main(int argc, const char **argv)
{
	setlocale(LC_ALL, "");
	ec_log_get()->SetLoglevel(EC_LOGLEVEL_INFO);
	if (!sd_parse_options(argc, argv))
		return EXIT_FAILURE;

	ec_log_always("Starting kopano-statsd " PROJECT_VERSION " (uid %u)", getuid());
	struct sigaction act{};
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_ONSTACK | SA_RESETHAND;
	act.sa_handler = sd_term;
	sigaction(SIGINT, &act, nullptr);
	sigaction(SIGTERM, &act, nullptr);
	ec_setup_segv_handler("kopano-statsd", PROJECT_VERSION);

	std::vector<struct pollfd> sk;
	auto ret = sd_listen(sd_config.get(), sk);
	if (ret != hrSuccess)
		return EXIT_FAILURE;
	unix_coredump_enable(sd_config->GetSetting("coredump_enabled"));
	ret = unix_runas(sd_config.get());
	if (ret < 0) {
		return EXIT_FAILURE;
	} else if (ret == 0) {
		ec_reexec_finalize();
	} else {
		ec_reexec_prepare_sockets();
		ret = ec_reexec(argv);
		if (ret < 0)
			ec_log_notice("K-1240: Failed to re-exec self: %s. "
				"Continuing with restricted coredumps.", strerror(-ret));
	}
	sd_mainloop(std::move(sk));
	return EXIT_SUCCESS;
}
