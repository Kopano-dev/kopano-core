#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <getopt.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include "hx-time.h"

struct mpt_stat_entry {
	struct timespec start, stop;
};

static std::list<struct mpt_stat_entry> mpt_stat_list;
static const wchar_t *mpt_user = L"foo";
static const wchar_t *mpt_pass = L"xfoo";
static const char *mpt_socket = "http://localhost:236/";
static size_t mpt_repeat = ~0U;

static void mpt_stat_dump(int s = 0)
{
	size_t z = mpt_stat_list.size();
	if (z == 0)
		return;
	const struct mpt_stat_entry &first = *mpt_stat_list.begin();
	const struct mpt_stat_entry &last  = *mpt_stat_list.rbegin();
	struct timespec delta;
	HX_timespec_sub(&delta, &last.stop, &first.start);
	double dt = delta.tv_sec + delta.tv_nsec / 1000000000.0;
	if (dt == 0)
		return;
	printf("\r\e[2K%.1f per second", z / dt);
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
	if (sigaction(SIGALRM, &sa, NULL) < 0) {
		perror("sigaction");
		return -errno;
	}
	struct sigevent sev;
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

static int mpt_main_login(void)
{
	HRESULT ret = MAPIInitialize(NULL);
	if (ret != hrSuccess) {
		perror("MAPIInitialize");
		return EXIT_FAILURE;
	}

	int err = mpt_setup_tick();
	if (err < 0)
		return EXIT_FAILURE;

	IMAPISession *ses;
	ECLogger *log = ec_log_get();
	struct mpt_stat_entry dp;

	while (mpt_repeat-- > 0) {
		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		ret = HrOpenECSession(log, &ses, "mapitime", "", mpt_user, mpt_pass, mpt_socket, 0, NULL, NULL);
		clock_gettime(CLOCK_MONOTONIC, &dp.stop);
		if (ret != hrSuccess) {
			fprintf(stderr, "Logon failed: %s\n", GetMAPIErrorMessage(ret));
			sleep(1);
		} else {
			mpt_stat_record(dp);
			ses->Release();
		}
	}
	MAPIUninitialize();
	return EXIT_SUCCESS;
}

static int mpt_main_lilo(void)
{
	HRESULT ret = MAPIInitialize(NULL);
	if (ret != hrSuccess) {
		perror("MAPIInitialize");
		return EXIT_FAILURE;
	}

	int err = mpt_setup_tick();
	if (err < 0)
		return EXIT_FAILURE;

	IMAPISession *ses;
	ECLogger *log = ec_log_get();
	struct mpt_stat_entry dp;

	while (mpt_repeat-- > 0) {
		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		ret = HrOpenECSession(log, &ses, "mapitime", "", mpt_user, mpt_pass, mpt_socket, 0, NULL, NULL);
		if (ret != hrSuccess) {
			fprintf(stderr, "Logon failed: %s\n", GetMAPIErrorMessage(ret));
			sleep(1);
		} else {
			ses->Release();
			clock_gettime(CLOCK_MONOTONIC, &dp.stop);
			mpt_stat_record(dp);
		}
	}
	MAPIUninitialize();
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	int c;
	if (argc < 2) {
		fprintf(stderr, "Need to specify a benchmark to run (init, login)\n");
		return EXIT_FAILURE;
	}
	while ((c = getopt(argc, argv, "z:")) != -1) {
		if (c == 'z')
			mpt_repeat = strtoul(optarg, NULL, 0);
		else
			fprintf(stderr, "Error: unknown option -%c\n", c);
	}
	argv += optind - 1;
	if (strcmp(argv[1], "init") == 0 || strcmp(argv[1], "i") == 0)
		return mpt_main_init();
	else if (strcmp(argv[1], "li") == 0)
		return mpt_main_login();
	else if (strcmp(argv[1], "lilo") == 0)
		return mpt_main_lilo();

	fprintf(stderr, "Unknown benchmark\n");
	return EXIT_FAILURE;
}
