/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include <chrono>
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <kopano/ECLogger.h>
#include <sys/select.h>
#include <iconv.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <ctime>
#include <dirent.h>
#include <mapicode.h>			// return codes
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <string>
#include <map>
#include <vector>
#ifdef HAVE_SYS_RANDOM_H
#	include <sys/random.h>
#endif
#ifndef HAVE_UUID_CREATE
#	include <uuid/uuid.h>
#else
#	include <uuid.h>
#endif
#if defined(__GLIBC__)
#	include <execinfo.h>
#	define WITH_BACKTRACE 1
#endif
#include <kopano/fileutil.hpp>

static bool rand_init_done = false;

HRESULT CoCreateGuid(LPGUID pNewGUID) {
	if (!pNewGUID)
		return MAPI_E_INVALID_PARAMETER;

	static_assert(sizeof(GUID) == sizeof(uuid_t), "UUID type sizes mismatch");
#ifdef HAVE_UUID_CREATE
	uuid_t g;
	uint32_t uid_ret;
	uuid_create(&g, &uid_ret);
	memcpy(pNewGUID, &g, sizeof(g));
#else
	uuid_t g;
	uuid_generate(g);
	memcpy(pNewGUID, g, sizeof(g));
#endif

	return S_OK;
}

void GetSystemTimeAsFileTime(FILETIME *ft) {
	using namespace std::chrono;
	using ft_ns = duration<nanoseconds::rep, std::ratio_multiply<std::hecto, std::nano>>;
	auto now = duration_cast<ft_ns>(system_clock::now().time_since_epoch()).count() + NANOSECS_BETWEEN_EPOCHS;
	ft->dwLowDateTime  = now & 0xffffffff;
	ft->dwHighDateTime = now >> 32;
}

void Sleep(unsigned int msec) {
	struct timespec ts;
	ts.tv_sec = msec/1000;
	unsigned int rsec = msec - ts.tv_sec * 1000;
	ts.tv_nsec = rsec*1000*1000;
	nanosleep(&ts, NULL);
}

namespace KC {

#if defined(HAVE_SYS_RANDOM_H) && defined(HAVE_GETRANDOM)
void rand_get(char *p, int n)
{
	getrandom(p, n, 0);
}
#elif defined(HAVE_ARC4RANDOM_BUF)
void rand_get(char *p, int n)
{
	arc4random_buf(p, n);
}
#else
static void rand_fail(void)
{
	fprintf(stderr, "Cannot access/use /dev/urandom, this is fatal (%s)\n", strerror(errno));
	kill(0, SIGTERM);
	exit(1);
}

void rand_get(char *p, int n)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd == -1)
		rand_fail();

	// handle EINTR
	while(n > 0)
	{
		int rc = read(fd, p, n);
		if (rc == 0)
			rand_fail();
		if (rc == -1)
		{
			if (errno == EINTR)
				continue;
			rand_fail();
		}
		p += rc;
		n -= rc;
	}
	close(fd);
}
#endif

void rand_init() {
	if (rand_init_done)
		return;
	unsigned int seed = 0;
	rand_get(reinterpret_cast<char *>(&seed), sizeof(seed));
	srand(seed);

	rand_init_done = true;
}

int rand_mt() {
	int dummy = 0;
	rand_get(reinterpret_cast<char *>(&dummy), sizeof dummy);
	if (dummy == INT_MIN)
		dummy = INT_MAX;
	else
		dummy = abs(dummy);

	// this gives a slightly bias to the value 0
	// also RAND_MAX is never returned which the
	// regular rand() does do
	return dummy % RAND_MAX;
}

char * get_password(const char *prompt) {
	return getpass(prompt);
}

} /* namespace */

void * GlobalAlloc(UINT uFlags, ULONG ulSize)
{
	// always returns NULL, as required by CreateStreamOnHGlobal implementation in mapi4linux/src/mapiutil.cpp
	return NULL;
}

namespace KC {

time_t GetProcessTime()
{
	time_t t;
	time(&t);
	return t;
}

std::vector<std::string> get_backtrace(void)
{
#define BT_MAX 256
	std::vector<std::string> result;
#ifdef WITH_BACKTRACE
	void *addrlist[BT_MAX];
	int addrlen = backtrace(addrlist, BT_MAX);
	if (addrlen == 0)
		return result;
	char **symbollist = backtrace_symbols(addrlist, addrlen);
	for (int i = 0; i < addrlen; ++i)
		result.emplace_back(symbollist[i]);
	free(symbollist);
#endif
	return result;
#undef BT_MAX
}

static void dump_fdtable_summary(pid_t pid)
{
	char procdir[64];
	snprintf(procdir, sizeof(procdir), "/proc/%ld/fd", static_cast<long>(pid));
	DIR *dh = opendir(procdir);
	if (dh == NULL)
		return;
	std::string msg;
	struct dirent *de;
	while ((de = readdir(dh)) != nullptr) {
		if (de->d_type != DT_LNK)
			continue;
		std::string de_name(std::string(procdir) + "/" + de->d_name);
		struct stat sb;
		if (stat(de_name.c_str(), &sb) < 0) {
			msg += " ?";
		} else switch (sb.st_mode & S_IFMT) {
			case S_IFREG:  msg += " ."; break;
			case S_IFSOCK: msg += " s"; break;
			case S_IFDIR:  msg += " d"; break;
			case S_IFIFO:  msg += " p"; break;
			case S_IFCHR:  msg += " c"; break;
			default:       msg += " O"; break;
		}
		msg += de->d_name;
	}
	closedir(dh);
	ec_log_debug("FD map:%s", msg.c_str());
}

/* ALERT! Big hack!
 *
 * This function relocates an open file descriptor to a new file descriptor above 1024. The
 * reason we do this is because, although we support many open FDs up to FD_SETSIZE, libraries
 * that we use may not (most notably libldap). This means that if a new socket is allocated within
 * libldap as socket 1025, libldap will fail because it was compiled with FD_SETSIZE=1024. To fix
 * this problem, we make sure that most FDs under 1024 are free for use by external libraries, while
 * we use the range 1024 -> \infty.
 */
int ec_relocate_fd(int fd)
{
	static constexpr const int typical_limit = 1024;

	if (fd >= typical_limit)
		/* No action needed */
		return fd;
	int relocated = fcntl(fd, F_DUPFD, typical_limit);
	if (relocated >= 0) {
		close(fd);
		return relocated;
	}
	if (errno == EINVAL) {
		/*
		 * The range start (typical_limit) was already >=RLIMIT_NOFILE.
		 * Just stay silent.
		 */
		static bool warned_once;
		if (warned_once)
			return fd;
		warned_once = true;
		ec_log_warn("F_DUPFD yielded EINVAL");
		return fd;
	}
	static time_t warned_last;
	time_t now = time(NULL);
	if (warned_last + 60 > now)
		return fd;
	ec_log_notice(
		"Relocation of FD %d into high range (%d+) could not be completed: "
		"%s. Keeping old number.", fd, typical_limit, strerror(errno));
	dump_fdtable_summary(getpid());
	return fd;
}

void le_to_cpu(SYSTEMTIME &s)
{
	s.wYear = le16_to_cpu(s.wYear);
	s.wMonth = le16_to_cpu(s.wMonth);
	s.wDayOfWeek = le16_to_cpu(s.wDayOfWeek);
	s.wDay = le16_to_cpu(s.wDay);
	s.wHour = le16_to_cpu(s.wHour);
	s.wMinute = le16_to_cpu(s.wMinute);
	s.wSecond = le16_to_cpu(s.wSecond);
	s.wMilliseconds = le16_to_cpu(s.wMilliseconds);
}

} /* namespace */
