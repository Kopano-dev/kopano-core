/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright 2018 Kopano and its licensors
 */
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <libHX/option.h>
#include <mapicode.h>
#include <kopano/ECChannel.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/stringutil.h>
#include <kopano/tie.hpp>
#include <kopano/UnixUtil.h>
#include <libHX/string.h>
#include "indexer.hpp"
#include "idx_plugin.hpp"

using namespace KC;
using namespace std::string_literals;

static const char *opt_config_file;
static unsigned int opt_from_stdin;
static bool g_quit;
static std::unique_ptr<IIndexer> g_indexer;

static constexpr const struct HXoption idx_options[] = {
	{nullptr, 'c', HXTYPE_STRING, &opt_config_file, nullptr, nullptr, 0, "Alternate config file", "FILENAME"},
	{nullptr, 'S', HXTYPE_NONE, &opt_from_stdin, nullptr, nullptr, 0, "Take commands from stdin"},
	HXOPT_AUTOHELP,
	HXOPT_TABLEEND,
};

static HRESULT idx_thread_loop(std::unique_ptr<ECChannel> &&channel)
{
	IIndexer::client_state st;
	while (true) {
		std::string buffer;
		auto ret = channel->HrReadLine(buffer);
		if (ret != hrSuccess)
			return ret;
		if (buffer.size() == 0)
			continue;
		auto result = g_indexer->exec1(st, buffer.c_str());
		ret = channel->HrWriteLine(result);
		if (ret != hrSuccess)
			return ret;
	}
	return hrSuccess;
}

static void *idx_thread_start(void *arg)
{
	idx_thread_loop(std::unique_ptr<ECChannel>(static_cast<ECChannel *>(arg)));
	return nullptr;
}

static int idx_listen(ECConfig *cfg, std::vector<struct pollfd> &pollers)
{
	auto lsock = tokenize(cfg->GetSetting("indexer_listen"), ' ', true);
	auto old_addr = cfg->GetSetting("server_bind_name");
	if (old_addr != nullptr && *old_addr != '\0')
		lsock.emplace_back("unix:"s + old_addr);
	auto rsock = ec_bindspec_to_sockets(std::move(lsock), S_IRWUG,
	             cfg->GetSetting("run_as_user"), cfg->GetSetting("run_as_group"));
	auto &idx_sock = rsock.second;

	struct pollfd x;
	memset(&x, 0, sizeof(x));
	x.events = POLLIN;
	pollers.reserve(idx_sock.size());
	for (auto &spec : idx_sock) {
		x.fd = spec.m_fd;
		spec.m_fd = -1;
		pollers.emplace_back(x);
	}
	return 0;
}

static HRESULT idx_startup_stdin(const char **argv)
{
	auto cfg = g_indexer->get_config();
	if (unix_runas(cfg.get()))
		return MAPI_E_CALL_FAILED;
#if 0
	auto ret = ec_reexec(argv);
	if (ret < 0)
		ec_log_notice("K-1240: Failed to re-exec self: %s", strerror(-ret));
#endif

	IIndexer::client_state cs;
	while (!std::cin.eof()) {
		std::string buffer;
		printf("\e[1m>\e[0m ");
		fflush(stdout);
		std::getline(std::cin, buffer);
		if (buffer.size() == 0)
			continue;
		auto result = g_indexer->exec1(cs, buffer.c_str());
		HX_chomp(&result[0]);
		printf("%s\n", result.c_str());
	}
	return hrSuccess;
}

static HRESULT idx_startup_net(const char **argv)
{
	ec_log_always("Starting kopano-indexd " PACKAGE_VERSION " (uid %u)", getuid());
	auto cfg = g_indexer->get_config();
	auto logger = CreateLogger(cfg.get(), "kindexd");
	if (logger == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	logger->SetLogprefix(LP_TID);
	ec_log_set(logger);

	std::vector<struct pollfd> idx_poll;
	auto err = idx_listen(cfg.get(), idx_poll);
	if (err < 0)
		return MAPI_E_NETWORK_ERROR;
	if (unix_runas(cfg.get()))
		return MAPI_E_CALL_FAILED;
	ec_reexec_prepare_sockets();
	//err = ec_reexec(argv);
	if (err < 0)
		ec_log_notice("K-1240: Failed to re-exec self: %s", strerror(-err));
	auto ret = g_indexer->service_start();
	if (ret != hrSuccess)
		return kc_perror("service_start", ret);
	ec_log_info("Starting incremental sync");

	pthread_attr_t thr_attr;
	pthread_attr_init(&thr_attr);
	pthread_attr_setdetachstate(&thr_attr, PTHREAD_CREATE_DETACHED);

	while (!g_quit) {
		for (size_t i = 0; i < idx_poll.size(); ++i)
			idx_poll[i].revents = 0;
		err = poll(&idx_poll[0], idx_poll.size(), 10 * 1000);
		if (err < 0) {
			if (errno != EINTR)
				ec_log_err("socket error: %s", strerror(errno));
			continue;
		} else if (err == 0) {
			continue;
		}

		for (size_t i = 0; i < idx_poll.size(); ++i) {
			if (!(idx_poll[i].revents & POLLIN))
				/* OS might set more bits than requested */
				continue;
			std::unique_ptr<ECChannel> ch;
			auto ret = HrAccept(idx_poll[i].fd, &unique_tie(ch));
			if (ret != hrSuccess) {
				kc_perrorf("HrAccept failed", ret);
				ret = hrSuccess;
				continue;
			}
			pthread_t tid;
			/* ECSearchWorker */
			err = pthread_create(&tid, &thr_attr, idx_thread_start, ch.get());
			if (err != 0) {
				ec_log_err("Could not create indexer thread: %s", strerror(err));
				continue;
			}
			ch.release();
		}
	}

	g_indexer->service_stop();
	return EXIT_SUCCESS;
}

static bool idx_parse_options(int &argc, const char **&argv)
{
	if (HX_getopt(idx_options, &argc, &argv, HXOPT_USAGEONERR) != HXOPT_ERR_SUCCESS)
		return false;
	if (opt_config_file == nullptr)
		opt_config_file = ECConfig::GetDefaultPath("search.cfg");
	auto ret = IIndexer::create(opt_config_file, &unique_tie(g_indexer));
	if (ret != hrSuccess) {
		kc_perror("Indexer::init", ret);
		return false;
	} else if (g_indexer->get_config()->HasErrors()) {
		fprintf(stderr, "Error reading config file %s\n", opt_config_file);
		LogConfigErrors(g_indexer->get_config().get());
		return false;
	}
	return true;
}

int main(int argc, const char **argv) try
{
	setlocale(LC_ALL, "");
	ec_log_get()->SetLoglevel(EC_LOGLEVEL_INFO);
	if (!idx_parse_options(argc, argv))
		return EXIT_FAILURE;
	auto ret = opt_from_stdin ? idx_startup_stdin(argv) : idx_startup_net(argv);
	return ret == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
} catch (...) {
	std::terminate();
}
