/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2016+, Kopano and its licensors
 */
#include <chrono>
#include <memory>
#include <mutex>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <getopt.h>
#include <pthread.h>
#include <spawn.h>
#include <unistd.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/ECMemTable.h>
#include <kopano/automapi.hpp>
#include <kopano/ecversion.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include "TimeUtil.h"
#include <kopano/IECInterfaces.hpp>
#ifdef HAVE_CURL_CURL_H
#	include <curl/curl.h>
#endif
#define NO_NOTIFY EC_PROFILE_FLAGS_NO_NOTIFICATIONS

using namespace KC;
using clk = std::chrono::steady_clock;

struct mpt_stat_entry {
	time_point start, stop;
};

class mpt_job {
	public:
	virtual int init();
	virtual int run() = 0;
	private:
	AutoMAPI m_mapi;
};

static pthread_t mpt_ticker;
static std::list<struct mpt_stat_entry> mpt_stat_list;
static std::mutex mpt_stat_lock;
static const char *mpt_user, *mpt_pass, *mpt_socket;
static size_t mpt_repeat = ~0U;
static int mpt_loglevel = EC_LOGLEVEL_NOTICE;

static void *mpt_stat_dump(void *)
{
	for (;; sleep(1)) {
		std::unique_lock<std::mutex> locker(mpt_stat_lock);
		size_t z = mpt_stat_list.size();
		if (z == 0)
			continue;
		decltype(mpt_stat_list.front().start - mpt_stat_list.front().start) dt;
		dt = dt.zero();
		for (const auto &i : mpt_stat_list)
			dt += i.stop - i.start;
		locker.unlock();
		if (dt.count() == 0)
			continue;
		printf("\r\x1b\x5b""2K%.1f per second", z / std::chrono::duration_cast<std::chrono::duration<double>>(dt).count());
		fflush(stdout);
	}
	return nullptr;
}

static void mpt_stat_record(const struct mpt_stat_entry &dp, size_t limit = 300)
{
	std::lock_guard<std::mutex> locker(mpt_stat_lock);
	mpt_stat_list.emplace_back(dp);
	if (mpt_stat_list.size() > limit)
		mpt_stat_list.pop_front();
}

static int mpt_main_init(void)
{
	struct mpt_stat_entry dp;
	while (mpt_repeat-- > 0) {
		dp.start = clk::now();
		HRESULT ret = MAPIInitialize(NULL);
		if (ret == erSuccess)
			MAPIUninitialize();
		dp.stop = clk::now();
		mpt_stat_record(dp);
	}
	return EXIT_SUCCESS;
}

int mpt_job::init()
{
	auto ret = m_mapi.Initialize();
	if (ret != hrSuccess) {
		perror("MAPIInitialize");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int mpt_basic_open(object_ptr<IMAPISession> &ses,
    object_ptr<IMsgStore> &store)
{
	auto ret = HrOpenECSession(&~ses, PROJECT_VERSION, "mapitime", mpt_user,
	           mpt_pass, mpt_socket, NO_NOTIFY, nullptr, nullptr);
	if (ret != hrSuccess) {
		fprintf(stderr, "Logon failed: %s\n", GetMAPIErrorMessage(ret));
		sleep(1);
		return ret;
	}
	ret = HrOpenDefaultStore(ses, &~store);
	if (ret != hrSuccess) {
		fprintf(stderr, "OpenDefaultStore: %s\n", GetMAPIErrorMessage(ret));
		sleep(1);
		return ret;
	}
	return hrSuccess;
}

static int mpt_basic_work(IMsgStore *store)
{
	memory_ptr<ENTRYID> eid;
	ULONG neid = 0;
	auto ret = store->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &neid, &~eid, nullptr);
	if (ret != hrSuccess) {
		fprintf(stderr, "GRF failed: %s\n", GetMAPIErrorMessage(ret));
		return EXIT_FAILURE;
	}
	object_ptr<IMAPIFolder> folder;
	ULONG type = 0;
	ret = store->OpenEntry(0, nullptr, &iid_of(folder), MAPI_MODIFY, &type, &~folder);
	if (ret != hrSuccess) {
		fprintf(stderr, "OE failed: %s\n", GetMAPIErrorMessage(ret));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

class mpt_open1 : public mpt_job {
	public:
	int run() override;
};

int mpt_open1::run()
{
	object_ptr<IMAPISession> ses;
	object_ptr<IMsgStore> store;
	auto ret = mpt_basic_open(ses, store);
	if (ret != hrSuccess)
		return EXIT_FAILURE;
	return mpt_basic_work(store);
}

class mpt_open2 : public mpt_job {
	public:
	int init() override;
	int run() override;
	private:
	std::string m_data;
};

int mpt_open2::init()
{
	auto ret = mpt_job::init();
	if (ret != hrSuccess)
		return EXIT_FAILURE;
	object_ptr<IMAPISession> ses;
	object_ptr<IMsgStore> store;
	ret = mpt_basic_open(ses, store);
	if (ret != hrSuccess) {
		fprintf(stderr, "mpt_open_base: %s\n", GetMAPIErrorMessage(ret));
		return EXIT_FAILURE;
	}
	ret = kc_session_save(ses, m_data);
	if (ret != hrSuccess) {
		fprintf(stderr, "save failed: %s\n", GetMAPIErrorMessage(ret));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int mpt_open2::run()
{
	object_ptr<IMAPISession> ses;
	auto ret = kc_session_restore(m_data, &~ses);
	if (ret != hrSuccess) {
		fprintf(stderr, "restore failed: %s\n", GetMAPIErrorMessage(ret));
		return EXIT_FAILURE;
	}
	object_ptr<IMsgStore> store;
	ret = HrOpenDefaultStore(ses, &~store);
	if (ret != hrSuccess) {
		fprintf(stderr, "OpenDefaultStore: %s\n", GetMAPIErrorMessage(ret));
		return EXIT_FAILURE;
	}
	return mpt_basic_work(store);
}

static int mpt_runner(mpt_job &&fct)
{
	auto ret = fct.init();
	if (ret != hrSuccess) {
		perror("MAPIInitialize");
		return EXIT_FAILURE;
	}
	struct mpt_stat_entry dp;
	while (mpt_repeat-- > 0) {
		dp.start = clk::now();
		ret = fct.run();
		if (ret != EXIT_SUCCESS)
			break;
		dp.stop = clk::now();
		mpt_stat_record(dp);
	}
	return ret;
}

static int mpt_main_pagetime(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Need URL to test\n");
		return EXIT_FAILURE;
	}
#ifndef HAVE_CURL_CURL_H
	fprintf(stderr, "Not built with curl support\n");
	return EXIT_FAILURE;
#else
	auto curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, true);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, true);
	curl_easy_setopt(curl, CURLOPT_URL, argv[1]);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, static_cast<curl_write_callback>([](char *, size_t, size_t n, void *) { return n; }));
	struct mpt_stat_entry dp;
	while (mpt_repeat-- > 0) {
		dp.start = clk::now();
		curl_easy_perform(curl);
		dp.stop = clk::now();
		mpt_stat_record(dp);
	}
	return EXIT_SUCCESS;
#endif
}

static int mpt_main_exectime(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Need program to test\n");
		return EXIT_FAILURE;
	}
	--argc;
	++argv; // skip "exectime"
	struct mpt_stat_entry dp;
	while (mpt_repeat-- > 0) {
		pid_t pid;
		int st;

		dp.start = clk::now();
		if (posix_spawn(&pid, argv[0], nullptr, nullptr, const_cast<char **>(argv), nullptr) == 0)
			wait(&st);
		dp.stop = clk::now();
		mpt_stat_record(dp);
	}
	return EXIT_SUCCESS;
}

static int mpt_main_cast(bool which)
{
	AutoMAPI mapiinit;
	HRESULT ret = mapiinit.Initialize();
	if (ret != hrSuccess) {
		perror("MAPIInitialize");
		return EXIT_FAILURE;
	}
	object_ptr<IProfAdmin> profadm;
	ret = MAPIAdminProfiles(0, &~profadm);
	if (ret != hrSuccess)
		return EXIT_FAILURE;
	object_ptr<IUnknown> unk;
	ret = profadm->QueryInterface(IID_IUnknown, &~unk);
	if (ret != hrSuccess)
		return EXIT_FAILURE;

	if (which == 0) { /* qicast */
		while (mpt_repeat-- > 0) {
			struct mpt_stat_entry dp;
			unsigned int rep = 100000;
			dp.start = clk::now();
			while (rep-- > 0)
				unk->QueryInterface(IID_IProfAdmin, &~profadm);
			dp.stop = clk::now();
			mpt_stat_record(dp);
		}
	} else if (which == 1) { /* dycast */
		while (mpt_repeat-- > 0) {
			struct mpt_stat_entry dp;
			unsigned int rep = 100000;
			dp.start = clk::now();
			while (rep-- > 0)
				profadm.reset(dynamic_cast<IProfAdmin *>(unk.get()));
			dp.stop = clk::now();
			mpt_stat_record(dp);
		}
	}
	return EXIT_SUCCESS;
}

static int mpt_main_malloc(void)
{
	struct mpt_stat_entry dp;
	while (mpt_repeat-- > 0) {
		dp.start = clk::now();
		memory_ptr<MAPIUID> base;
		auto ret = MAPIAllocateBuffer(sizeof(MAPIUID), &~base);
		if (ret != hrSuccess)
			return EXIT_FAILURE;
		for (unsigned int i = 0; i < 10000; ++i) {
			void *x = nullptr;
			ret = MAPIAllocateMore(sizeof(MAPIUID), base.get(), &x);
			if (ret != hrSuccess)
				return EXIT_FAILURE;
		}
		base.reset();
		dp.stop = clk::now();
		mpt_stat_record(dp);
	}
	return EXIT_SUCCESS;
}

static int mpt_main_bin2hex()
{
	struct mpt_stat_entry dp;
	static constexpr const size_t bufsize = 1048576;
	auto temp = std::make_unique<char[]>(bufsize);
	memset(temp.get(), 0, bufsize);
	while (mpt_repeat-- > 0) {
		dp.start = clk::now();
		bin2hex(bufsize, reinterpret_cast<const unsigned char *>(temp.get()));
		dp.stop = clk::now();
		mpt_stat_record(dp);
	}
	return EXIT_SUCCESS;
}

static void mpt_usage(void)
{
	fprintf(stderr, "mapitime [-p pass] [-s server] [-u username] [-z count] benchmark_choice\n");
	fprintf(stderr, "  -z count    Run this many iterations (default: finite but almost forever)\n");
	fprintf(stderr, "Benchmark choices:\n");
	fprintf(stderr, "  init        Just the library initialization\n");
	fprintf(stderr, "  open1       Measure: init, login, open store, open root container\n");
	fprintf(stderr, "  open2       Like open1, but use Save-Restore\n");
	fprintf(stderr, "  pagetime    Measure webpage retrieval time\n");
	fprintf(stderr, "  exectime    Measure process runtime\n");
	fprintf(stderr, "  qicast      Measure QueryInterface throughput\n");
	fprintf(stderr, "  dycast      Measure dynamic_cast<> throughput\n");
	fprintf(stderr, "  malloc      Measure MAPIAllocateMore throughput\n");
	fprintf(stderr, "  bin2hex     Measure bin2hex throughput\n");
}

static int mpt_option_parse(int argc, char **argv)
{
	int c;
	if (argc < 2) {
		mpt_usage();
		return EXIT_FAILURE;
	}
	while ((c = getopt(argc, argv, "p:s:u:vz:")) != -1) {
		if (c == 'p') {
			mpt_pass = optarg;
		} else if (c == 'u') {
			mpt_user = optarg;
		} else if (c == 's') {
			mpt_socket = optarg;
		} else if (c == 'v') {
			if (mpt_loglevel <= EC_LOGLEVEL_DEBUG)
			++mpt_loglevel;
		} else if (c == 'z') {
			mpt_repeat = strtoul(optarg, NULL, 0);
		} else {
			fprintf(stderr, "Error: unknown option -%c\n", c);
			mpt_usage();
		}
	}
	ec_log_get()->SetLoglevel(mpt_loglevel);
	if (mpt_user == nullptr) {
		mpt_user = "foo";
		fprintf(stderr, "Info: defaulting to username \"foo\"\n");
	}
	if (mpt_pass == nullptr) {
		mpt_pass = "xfoo";
		fprintf(stderr, "Info: defaulting to password \"xfoo\"\n");
	}
	if (mpt_socket == NULL) {
		mpt_socket = "http://localhost:236/";
		fprintf(stderr, "Info: defaulting to %s\n", mpt_socket);
	}
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	int ret = mpt_option_parse(argc, argv);
	if (ret != EXIT_SUCCESS)
		return ret;
	argc -= optind - 1;
	argv += optind - 1;
	if (argc < 2) {
		mpt_usage();
		return EXIT_FAILURE;
	}

	ret = pthread_create(&mpt_ticker, nullptr, mpt_stat_dump, nullptr);
	if (ret != 0) {
		perror("pthread_create");
		return EXIT_FAILURE;
	}
	ret = EXIT_FAILURE;
	if (strcmp(argv[1], "init") == 0 || strcmp(argv[1], "i") == 0)
		ret = mpt_main_init();
	else if (strcmp(argv[1], "open1") == 0)
		ret = mpt_runner(mpt_open1());
	else if (strcmp(argv[1], "open2") == 0)
		ret = mpt_runner(mpt_open2());
	else if (strcmp(argv[1], "exectime") == 0)
		ret = mpt_main_exectime(argc - 1, argv + 1);
	else if (strcmp(argv[1], "pagetime") == 0)
		ret = mpt_main_pagetime(argc - 1, argv + 1);
	else if (strcmp(argv[1], "qicast") == 0)
		ret = mpt_main_cast(0);
	else if (strcmp(argv[1], "dycast") == 0)
		ret = mpt_main_cast(1);
	else if (strcmp(argv[1], "malloc") == 0)
		ret = mpt_main_malloc();
	else if (strcmp(argv[1], "bin2hex") == 0)
		ret = mpt_main_bin2hex();
	else
		mpt_usage();
	pthread_cancel(mpt_ticker);
	pthread_join(mpt_ticker, nullptr);
	return ret;
}
