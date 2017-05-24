#include "config.h"
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <getopt.h>
#include <spawn.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/charset/convert.h>
#include <kopano/ECMemTable.h>
#include <kopano/automapi.hpp>
#include <kopano/memory.hpp>
#include <m4lcommon/IECTestProtocol.h>
#ifdef HAVE_CURL_CURL_H
#	include <curl/curl.h>
#endif

struct mpt_stat_entry {
	struct timespec start, stop;
};

using namespace KCHL;

static std::list<struct mpt_stat_entry> mpt_stat_list;
static std::wstring mpt_userw, mpt_passw;
static const wchar_t *mpt_user, *mpt_pass;
static const char *mpt_socket;
static size_t mpt_repeat = ~0U;

static void mpt_stat_dump(int s = 0)
{
	size_t z = mpt_stat_list.size();
	if (z == 0)
		return;
	const struct mpt_stat_entry &first = *mpt_stat_list.begin();
	const struct mpt_stat_entry &last  = *mpt_stat_list.rbegin();
	typedef std::chrono::seconds sec;
	typedef std::chrono::nanoseconds nsec;
	auto dt = std::chrono::duration<double>(
	          sec(last.stop.tv_sec) + nsec(last.stop.tv_nsec) -
	          (sec(first.start.tv_sec) + nsec(first.start.tv_nsec)));
	if (dt.count() == 0)
		return;
	printf("\r\x1b\x5b""2K%.1f per second", z / dt.count());
	fflush(stdout);
}

static void mpt_stat_record(const struct mpt_stat_entry &dp, size_t limit = 50)
{
	mpt_stat_list.push_back(dp);
	if (mpt_stat_list.size() > limit)
		mpt_stat_list.pop_front();
}

static int mpt_setup_tick(void)
{
	struct sigaction sa;
	sa.sa_handler = mpt_stat_dump;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGALRM, &sa, NULL) < 0) {
		perror("sigaction");
		return -errno;
	}
	struct sigevent sev;
	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo  = SIGALRM;
	timer_t timerid;
	if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) < 0) {
		perror("timer_create");
		return -errno;
	}
	struct itimerspec it;
	it.it_interval.tv_sec = it.it_value.tv_sec = 1;
	it.it_interval.tv_nsec = it.it_value.tv_nsec = 0;
	if (timer_settime(timerid, 0, &it, NULL) < 0) {
		perror("timer_settime");
		return -errno;
	}
	return 1;
}

static int mpt_main_init(void)
{
	if (mpt_setup_tick() < 0)
		return EXIT_FAILURE;
	struct mpt_stat_entry dp;
	while (mpt_repeat-- > 0) {
		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		HRESULT ret = MAPIInitialize(NULL);
		if (ret == erSuccess)
			MAPIUninitialize();
		clock_gettime(CLOCK_MONOTONIC, &dp.stop);
		mpt_stat_record(dp);
	}
	return EXIT_SUCCESS;
}

static void mpt_ping(IMAPISession *ses)
{
	object_ptr<IMsgStore> store;
	auto ret = HrOpenDefaultStore(ses, &~store);
	if (ret != hrSuccess)
		return;
	object_ptr<IECTestProtocol> tp;
	ret = store->QueryInterface(IID_IECTestProtocol, &~tp);
	if (ret != hrSuccess)
		return;
	memory_ptr<char> out;
	tp->TestGet("ping", &~out);
}

/**
 * @with_lo:	whether to include the logoff RPC in the time
 * 		measurement (it is executed in any case)
 * @with_ping:	whether to issue some more RPCs
 */
static int mpt_main_login(bool with_lo, bool with_ping)
{
	HRESULT ret = MAPIInitialize(NULL);
	if (ret != hrSuccess) {
		perror("MAPIInitialize");
		return EXIT_FAILURE;
	}

	int err = mpt_setup_tick();
	if (err < 0)
		return EXIT_FAILURE;

	struct mpt_stat_entry dp;

	while (mpt_repeat-- > 0) {
		object_ptr<IMAPISession> ses;
		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		ret = HrOpenECSession(&~ses, "mapitime", "", mpt_user, mpt_pass,
		      mpt_socket, 0, NULL, NULL);
		if (!with_lo)
			clock_gettime(CLOCK_MONOTONIC, &dp.stop);
		if (ret != hrSuccess) {
			fprintf(stderr, "Logon failed: %s\n", GetMAPIErrorMessage(ret));
			sleep(1);
			continue;
		}
		if (with_lo) {
			if (with_ping)
				mpt_ping(ses);
			ses.reset();
			clock_gettime(CLOCK_MONOTONIC, &dp.stop);
		}
		mpt_stat_record(dp);
	}
	MAPIUninitialize();
	return EXIT_SUCCESS;
}

/* Login-Logout with Save–Restore */
static int mpt_main_lsr(bool with_ping)
{
	AutoMAPI automapi;
	auto ret = automapi.Initialize();
	if (ret != hrSuccess) {
		perror("MAPIInitialize");
		return EXIT_FAILURE;
	}

	int err = mpt_setup_tick();
	if (err < 0)
		return EXIT_FAILURE;

	object_ptr<IMAPISession> ses;
	ret = HrOpenECSession(&~ses, "mapitime", "", mpt_user, mpt_pass,
	      mpt_socket, 0, nullptr, nullptr);
	if (ret != hrSuccess) {
		fprintf(stderr, "Logon failed: %s\n", GetMAPIErrorMessage(ret));
		return EXIT_FAILURE;
	}

	struct mpt_stat_entry dp;
	while (mpt_repeat-- > 0) {
		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		std::string data;
		ret = kc_session_save(ses, data);
		if (ret != hrSuccess) {
			fprintf(stderr, "save failed: %s\n", GetMAPIErrorMessage(ret));
			return EXIT_FAILURE;
		}
		ret = kc_session_restore(data, &~ses);
		if (ret != hrSuccess) {
			fprintf(stderr, "restore failed: %s\n", GetMAPIErrorMessage(ret));
			return EXIT_FAILURE;
		}
		if (with_ping)
			mpt_ping(ses);
		clock_gettime(CLOCK_MONOTONIC, &dp.stop);
		mpt_stat_record(dp);
	}
	return EXIT_SUCCESS;
}

static int mpt_main_vft(void)
{
	AutoMAPI mapiinit;
	HRESULT ret = mapiinit.Initialize();
	if (ret != hrSuccess) {
		perror("MAPIInitialize");
		return EXIT_FAILURE;
	}

	int err = mpt_setup_tick();
	if (err < 0)
		return EXIT_FAILURE;

	struct mpt_stat_entry dp;
	static constexpr const SizedSPropTagArray(1, spta) = {1, {PR_ENTRYID}};
	object_ptr<ECMemTable> mt;
	ret = ECMemTable::Create(spta, PT_LONG, &~mt);
	if (ret != hrSuccess) {
		ec_log_err("ECMemTable::Create died");
		return EXIT_FAILURE;
	}
	ECMemTableView *view;
	ret = mt->HrGetView(createLocaleFromName(""), 0, &view);
	if (ret != hrSuccess) {
		ec_log_err("HrGetView died");
		return EXIT_FAILURE;
	}

	while (mpt_repeat-- > 0) {
		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		IMAPITable *imt = NULL;
		IUnknown *iunk = NULL;
		view->QueryInterface(IID_IMAPITable, reinterpret_cast<void **>(&imt));
		view->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&iunk));
		imt->QueryInterface(IID_IMAPITable, reinterpret_cast<void **>(&imt));
		imt->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&iunk));
		iunk->QueryInterface(IID_IMAPITable, reinterpret_cast<void **>(&imt));
		iunk->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&iunk));
		clock_gettime(CLOCK_MONOTONIC, &dp.stop);
		mpt_stat_record(dp);
	}
	return EXIT_SUCCESS;
}

static int mpt_main_pagetime(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Need URL to test\n");
		return EXIT_FAILURE;
	}
	int err = mpt_setup_tick();
	if (err < 0)
		return EXIT_FAILURE;

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
		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		curl_easy_perform(curl);
		clock_gettime(CLOCK_MONOTONIC, &dp.stop);
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
	int err = mpt_setup_tick();
	if (err < 0)
		return EXIT_FAILURE;

	struct mpt_stat_entry dp;
	while (mpt_repeat-- > 0) {
		pid_t pid;
		int st;

		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		if (posix_spawn(&pid, argv[0], nullptr, nullptr, const_cast<char **>(argv), nullptr) == 0)
			wait(&st);
		clock_gettime(CLOCK_MONOTONIC, &dp.stop);
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
	int err = mpt_setup_tick();
	if (err < 0)
		return EXIT_FAILURE;
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
			clock_gettime(CLOCK_MONOTONIC, &dp.start);
			while (rep-- > 0)
				unk->QueryInterface(IID_IProfAdmin, &~profadm);
			clock_gettime(CLOCK_MONOTONIC, &dp.stop);
			mpt_stat_record(dp);
		}
	} else if (which == 1) { /* dycast */
		while (mpt_repeat-- > 0) {
			struct mpt_stat_entry dp;
			unsigned int rep = 100000;
			clock_gettime(CLOCK_MONOTONIC, &dp.start);
			while (rep-- > 0)
				profadm.reset(dynamic_cast<IProfAdmin *>(unk.get()));
			clock_gettime(CLOCK_MONOTONIC, &dp.stop);
			mpt_stat_record(dp);
		}
	}
	return EXIT_SUCCESS;
}

static void mpt_usage(void)
{
	fprintf(stderr, "mapitime [-p pass] [-s server] [-u username] [-z count] benchmark_choice\n");
	fprintf(stderr, "  -z count    Run this many iterations (default: finite but almost forever)\n");
	fprintf(stderr, "Benchmark choices:\n");
	fprintf(stderr, "  init        Just the library initialization\n");
	fprintf(stderr, "  li          Issue login/logoff RPCs, but measure only login\n");
	fprintf(stderr, "  lilo        Issue login/logoff RPCs, and measure both\n");
	fprintf(stderr, "  ping        Issue login/logoff/PING RPCs, and measure all\n");
	fprintf(stderr, "  lsr         Measure profile save-restore cycle\n");
	fprintf(stderr, "  lsr+ping    lsr with forced network access (PING RPC)\n");
	fprintf(stderr, "  vft         Measure C++ class dispatching\n");
	fprintf(stderr, "  pagetime    Measure webpage retrieval time\n");
	fprintf(stderr, "  exectime    Measure process runtime\n");
}

static int mpt_option_parse(int argc, char **argv)
{
	const char *user = NULL, *pass = NULL;
	int c;
	if (argc < 2) {
		mpt_usage();
		return EXIT_FAILURE;
	}
	while ((c = getopt(argc, argv, "p:s:u:z:")) != -1) {
		if (c == 'p')
			pass = optarg;
		else if (c == 'u')
			user = optarg;
		else if (c == 's')
			mpt_socket = optarg;
		else if (c == 'z')
			mpt_repeat = strtoul(optarg, NULL, 0);
		else {
			fprintf(stderr, "Error: unknown option -%c\n", c);
			mpt_usage();
		}
	}
	if (user == NULL) {
		user = "foo";
		fprintf(stderr, "Info: defaulting to username \"foo\"\n");
	}
	mpt_userw = convert_to<std::wstring>(user);
	mpt_user = mpt_userw.c_str();
	if (pass == NULL) {
		pass = "xfoo";
		fprintf(stderr, "Info: defaulting to password \"xfoo\"\n");
	}
	mpt_passw = convert_to<std::wstring>(pass);
	mpt_pass = mpt_passw.c_str();
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
	argv += optind - 1;
	if (strcmp(argv[1], "init") == 0 || strcmp(argv[1], "i") == 0)
		return mpt_main_init();
	else if (strcmp(argv[1], "li") == 0)
		return mpt_main_login(false, false);
	else if (strcmp(argv[1], "lilo") == 0)
		return mpt_main_login(true, false);
	else if (strcmp(argv[1], "ping") == 0)
		return mpt_main_login(true, true);
	else if (strcmp(argv[1], "lsr") == 0)
		return mpt_main_lsr(false);
	else if (strcmp(argv[1], "lsr+ping") == 0)
		return mpt_main_lsr(true);
	else if (strcmp(argv[1], "vft") == 0)
		return mpt_main_vft();
	else if (strcmp(argv[1], "exectime") == 0)
		return mpt_main_exectime(argc - 1, argv + 1);
	else if (strcmp(argv[1], "pagetime") == 0)
		return mpt_main_pagetime(argc - 1, argv + 1);
	else if (strcmp(argv[1], "qicast") == 0)
		return mpt_main_cast(0);
	else if (strcmp(argv[1], "dycast") == 0)
		return mpt_main_cast(1);

	mpt_usage();
	return EXIT_FAILURE;
}
