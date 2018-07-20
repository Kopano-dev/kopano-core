/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#define _GNU_SOURCE 1
#include <kopano/platform.h>
#include <memory>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <climits>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include "fileutil.h"

namespace KC {

FILETIME UnixTimeToFileTime(time_t t)
{
	auto l = static_cast<int64_t>(t) * 10000000 + NANOSECS_BETWEEN_EPOCHS;
	return {static_cast<DWORD>(l), static_cast<DWORD>(l >> 32)};
}

time_t FileTimeToUnixTime(const FILETIME &ft)
{
	__int64 l = (static_cast<__int64>(ft.dwHighDateTime) << 32) + ft.dwLowDateTime;
	l -= NANOSECS_BETWEEN_EPOCHS;
	l /= 10000000;
	
	if(sizeof(time_t) < 8) {
		// On 32-bit systems, we cap the values at MAXINT and MININT
		if (l < static_cast<__int64>(INT_MIN))
			l = INT_MIN;
		if (l > static_cast<__int64>(INT_MAX))
			l = INT_MAX;
	}
	return l;
}

void UnixTimeToFileTime(time_t t, int *hi, unsigned int *lo)
{
	__int64 ll = static_cast<__int64>(t) * 10000000 + NANOSECS_BETWEEN_EPOCHS;
	*lo = (unsigned int)ll;
	*hi = (unsigned int)(ll >> 32);
}

/* Convert from FILETIME to time_t *and* string repr */
int FileTimeToTimestamp(const FILETIME &ft, time_t &ts, char *buf, size_t size)
{
	ts = FileTimeToUnixTime(ft);
	auto tm = localtime(&ts);
	if (tm == nullptr)
		return -1;
	strftime(buf, size, "%F %T", tm);
	return 0;
}

static const LONGLONG UnitsPerMinute = 600000000;
static const LONGLONG UnitsPerHalfMinute = 300000000;

static FILETIME RTimeToFileTime(LONG rtime)
{
	auto q = static_cast<ULONGLONG>(rtime) * UnitsPerMinute;
	return {static_cast<DWORD>(q & 0xFFFFFFFF), static_cast<DWORD>(q >> 32)};
}
 
LONG FileTimeToRTime(const FILETIME &pft)
{
	ULONGLONG q = pft.dwHighDateTime;
	q <<= 32;
	q |= pft.dwLowDateTime;
	q += UnitsPerHalfMinute;
	q /= UnitsPerMinute;
	return q & 0x7FFFFFFF;
}

time_t RTimeToUnixTime(LONG rtime)
{
	return FileTimeToUnixTime(RTimeToFileTime(rtime));
}

LONG UnixTimeToRTime(time_t unixtime)
{
	return FileTimeToRTime(UnixTimeToFileTime(unixtime));
}

/* The 'IntDate' and 'IntTime' date and time encoding are used for some CDO calculations. They
 * are basically a date or time encoded in a bitshifted way, packed so that it uses the least amount
 * of bits. Eg. a date (day,month,year) is encoded as 5 bits for the day (1-31), 4 bits for the month (1-12),
 * and the rest of the bits (32-4-5 = 23) for the year. The same goes for time, with seconds and minutes
 * each using 6 bits and 32-6-6=20 bits for the hours.
 *
 * For dates, everything is 1-index (1st January is 1-1) and year is full (2008)
 */
bool operator==(const FILETIME &a, const FILETIME &b) noexcept
{
	return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

bool operator>(const FILETIME &a, const FILETIME &b) noexcept
{
	return a.dwHighDateTime > b.dwHighDateTime ||
		(a.dwHighDateTime == b.dwHighDateTime &&
		 a.dwLowDateTime > b.dwLowDateTime);
}

bool operator>=(const FILETIME &a, const FILETIME &b) noexcept
{
	return a > b || a == b;
}

bool operator<(const FILETIME &a, const FILETIME &b) noexcept
{
	return a.dwHighDateTime < b.dwHighDateTime ||
		(a.dwHighDateTime == b.dwHighDateTime &&
		 a.dwLowDateTime < b.dwLowDateTime);
}

bool operator<=(const FILETIME &a, const FILETIME &b) noexcept
{
	return a < b || a == b;
}

#ifndef HAVE_TIMEGM
time_t timegm(struct tm *t) {
	char *s_tz = nullptr, *tz = getenv("TZ");
	if(tz)
		s_tz = strdup(tz);

	// SuSE 9.1 segfaults when putenv() is used in a detached thread on the next getenv() call.
	// so use setenv() on linux, putenv() on others.
	setenv("TZ", "UTC0", 1);
	tzset();
	auto convert = mktime(t);
	if (s_tz) {
		setenv("TZ", s_tz, 1);
		tzset();
	} else {
		unsetenv("TZ");
		tzset();
	}
	free(s_tz);
	return convert;
}
#endif

struct tm *gmtime_safe(time_t t, struct tm *result)
{
	auto tmp = gmtime_r(&t, result);
	if(tmp == NULL)
		memset(result, 0, sizeof(struct tm));

	return tmp;
}

double timespec2dbl(const struct timespec &t)
{
    return (double)t.tv_sec + t.tv_nsec/1000000000.0;
}

// Does mkdir -p <path>
int CreatePath(const char *createpath)
{
	struct stat s;
	std::unique_ptr<char[], cstdlib_deleter> path(strdup(createpath));

	// Remove trailing slashes
	size_t len = strlen(path.get());
	while (len > 0 && (path[len-1] == '/' || path[len-1] == '\\'))
		path[--len] = 0;

	if (stat(path.get(), &s) == 0) {
		if (s.st_mode & S_IFDIR)
			return 0; // Directory is already there
		return -1; // Item is not a directory
	}
	// We need to create the directory
	// First, create parent directories
	char *trail = strrchr(path.get(), '/') > strrchr(path.get(), '\\') ?
	              strrchr(path.get(), '/') : strrchr(path.get(), '\\');
	if (trail == NULL)
		// Should only happen if you are trying to create /path/to/dir
		// in win32 or \path\to\dir in linux
		return -1;
	*trail = '\0';
	if (CreatePath(path.get()) != 0)
		return -1;
	// Create the actual directory
	return mkdir(createpath, 0700);
}

void set_thread_name(pthread_t tid, const std::string & name)
{
#ifdef HAVE_PTHREAD_SETNAME_NP_2
	if (name.size() > 15)
		pthread_setname_np(tid, name.substr(0, 15).c_str());
	else
		pthread_setname_np(tid, name.c_str());
#endif
}

ssize_t read_retry(int fd, void *data, size_t len)
{
	auto buf = static_cast<char *>(data);
	size_t tread = 0;

	while (len > 0) {
		ssize_t ret = read(fd, buf, len);
		if (ret < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;
		len -= ret;
		buf += ret;
		tread += ret;
	}
	return tread;
}

ssize_t write_retry(int fd, const void *data, size_t len)
{
	auto buf = static_cast<const char *>(data);
	size_t twrote = 0;

	while (len > 0) {
		ssize_t ret = write(fd, buf, len);
		if (ret < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;
		len -= ret;
		buf += ret;
		twrote += ret;
	}
	return twrote;
}

bool force_buffers_to_disk(const int fd)
{
	return fsync(fd) != -1;
}

void kcsrv_blocksigs(void)
{
	sigset_t m;
	sigemptyset(&m);
	sigaddset(&m, SIGINT);
	sigaddset(&m, SIGHUP);
	sigaddset(&m, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &m, nullptr);
}

/*
 * Used for logging only. Can return anything as long as it is unique
 * per thread.
 */
unsigned long kc_threadid(void)
{
#if defined(LINUX)
	return syscall(SYS_gettid);
#elif defined(OPENBSD)
	return getthrid();
#else
	return pthread_self();
#endif
}

KAlternateStack::KAlternateStack()
{
	memset(&st, 0, sizeof(st));
	st.ss_flags = 0;
	st.ss_size = 65536;
	st.ss_sp = malloc(st.ss_size);
	if (st.ss_sp != nullptr && sigaltstack(&st, nullptr) < 0)
		ec_log_err("sigaltstack: %s", strerror(errno));
}

KAlternateStack::~KAlternateStack()
{
	if (st.ss_sp == nullptr)
		return;
	sigaltstack(nullptr, nullptr);
	free(st.ss_sp);
}

} /* namespace */
