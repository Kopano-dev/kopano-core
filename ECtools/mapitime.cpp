#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <getopt.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/charset/convert.h>
#include <kopano/ECMemTable.h>

struct mpt_stat_entry {
	struct timespec start, stop;
};

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
	struct mpt_stat_entry dp;

	while (mpt_repeat-- > 0) {
		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		ret = HrOpenECSession(&ses, "mapitime", "", mpt_user, mpt_pass,
		      mpt_socket, 0, NULL, NULL);
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
	struct mpt_stat_entry dp;

	while (mpt_repeat-- > 0) {
		clock_gettime(CLOCK_MONOTONIC, &dp.start);
		ret = HrOpenECSession(&ses, "mapitime", "", mpt_user, mpt_pass,
		      mpt_socket, 0, NULL, NULL);
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

static int mpt_main_vft(void)
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
	SPropTagArray spta = {1, {PR_ENTRYID}};
	ECMemTable *mt;
	ret = ECMemTable::Create(&spta, PT_LONG, &mt);
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
	mt->Release();
	MAPIUninitialize();
	return EXIT_SUCCESS;
}

static void mpt_usage(void)
{
	fprintf(stderr, "mapitime [-p pass] [-s server] [-u username] [-z count] {init|lilo}\n");
	fprintf(stderr, "  -z count    Run this many iterations (default: finite but almost forever)\n");
	fprintf(stderr, "Benchmark choices:\n");
	fprintf(stderr, "  init        Just the library initialization\n");
	fprintf(stderr, "  lilo        Send login and logoff RPCs to the server\n");
	fprintf(stderr, "  vft         Measure C++ class dispatching\n");
}

static int mpt_option_parse(int argc, char **argv)
{
	char *user = NULL, *pass = NULL;
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
		return mpt_main_login();
	else if (strcmp(argv[1], "lilo") == 0)
		return mpt_main_lilo();
	else if (strcmp(argv[1], "vft") == 0)
		return mpt_main_vft();

	mpt_usage();
	return EXIT_FAILURE;
}
