/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <tuple>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <kopano/timeutil.hpp>

namespace KC {

/**
 * Get a timestamp for the given date/time point
 *
 * Gets a timestamp for 'Nth [weekday] in month X, year Y at HH:00:00 GMT'
 *
 * @param year Full year (e.g. 2008, 2010)
 * @param month Month 1-12
 * @param week Week 1-5 (1 == first, 2 == second, 5 == last)
 * @param day Day 0-6 (0 == sunday, 1 == monday, ...)
 * @param hour Hour 0-23 (0 == 00:00, 1 == 01:00, ...)
 */
static time_t getDateByYearMonthWeekDayHour(WORD year, WORD month, WORD week,
    WORD day, WORD hour)
{
	struct tm tm = {0};

	// get first day of month
	tm.tm_year = year;
	tm.tm_mon = month-1;
	tm.tm_mday = 1;
	auto date = timegm(&tm);

	// convert back to struct to get wday info
	gmtime_safe(date, &tm);
	date -= tm.tm_wday * 24 * 60 * 60; // back up to start of week
	date += week * 7 * 24 * 60 * 60;   // go to correct week nr
	date += day * 24 * 60 * 60;
	date += hour * 60 * 60;

	// if we are in the next month, then back up a week, because week '5' means
	// 'last week of month'
	gmtime_safe(date, &tm);
	if (tm.tm_mon != month-1)
		date -= 7 * 24 * 60 * 60;
	return date;
}

static LONG getTZOffset(time_t date, const TIMEZONE_STRUCT &sTimeZone)
{
	struct tm tm;
	bool dst = false;

	if (sTimeZone.lStdBias == sTimeZone.lDstBias || sTimeZone.stDstDate.wMonth == 0 || sTimeZone.stStdDate.wMonth == 0)
		return -(sTimeZone.lBias) * 60;
	gmtime_safe(date, &tm);
	auto dststart = getDateByYearMonthWeekDayHour(tm.tm_year, sTimeZone.stDstDate.wMonth, sTimeZone.stDstDate.wDay, sTimeZone.stDstDate.wDayOfWeek, sTimeZone.stDstDate.wHour);
	auto dstend = getDateByYearMonthWeekDayHour(tm.tm_year, sTimeZone.stStdDate.wMonth, sTimeZone.stStdDate.wDay, sTimeZone.stStdDate.wDayOfWeek, sTimeZone.stStdDate.wHour);

	if (dststart <= dstend) {
		// Northern hemisphere, eg DST is during Mar-Oct
		if (date > dststart && date < dstend)
			dst = true;
	} else {
		// Southern hemisphere, eg DST is during Oct-Mar
		if (date < dstend || date > dststart)
			dst = true;
	}
	if (dst)
		return -(sTimeZone.lBias + sTimeZone.lDstBias) * 60;
	return -(sTimeZone.lBias + sTimeZone.lStdBias) * 60;
}

time_t UTCToLocal(time_t utc, const TIMEZONE_STRUCT &sTimeZone)
{
	return utc + getTZOffset(utc, sTimeZone);
}

time_t LocalToUTC(time_t local, const TIMEZONE_STRUCT &sTimeZone)
{
	return local - getTZOffset(local, sTimeZone);
}

FILETIME UnixTimeToFileTime(time_t t)
{
	auto l = static_cast<int64_t>(t) * 10000000 + NANOSECS_BETWEEN_EPOCHS;
	return {static_cast<DWORD>(l), static_cast<DWORD>(l >> 32)};
}

time_t FileTimeToUnixTime(const FILETIME &ft)
{
	int64_t l = (static_cast<int64_t>(ft.dwHighDateTime) << 32) + ft.dwLowDateTime;
	l -= NANOSECS_BETWEEN_EPOCHS;
	l /= 10000000;

	if (sizeof(time_t) < 8) {
		/* On 32-bit systems, we cap the values at MAXINT and MININT */
		if (l < static_cast<__int64>(INT_MIN))
			l = INT_MIN;
		if (l > static_cast<__int64>(INT_MAX))
			l = INT_MAX;
	}
	return l;
}

void UnixTimeToFileTime(time_t t, int *hi, unsigned int *lo)
{
	int64_t ll = static_cast<int64_t>(t) * 10000000 + NANOSECS_BETWEEN_EPOCHS;
	*lo = static_cast<unsigned int>(ll);
	*hi = static_cast<unsigned int>(ll >> 32);
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

/*
 * The "IntDate" and "IntTime" date and time encoding are used for some CDO
 * calculations. They are basically a date or time encoded in a bitshifted way,
 * packed so that it uses the least amount of bits. Eg. a date (day,month,year)
 * is encoded as 5 bits for the day (1-31), 4 bits for the month (1-12), and
 * the rest of the bits (32-4-5 = 23) for the year. The same goes for time,
 * with seconds and minutes each using 6 bits and 32-6-6=20 bits for the hours.
 *
 * For dates, everything is 1-index (1st January is 1-1) and year is full
 * (2008)
 */
bool operator==(const FILETIME &a, const FILETIME &b) noexcept
{
	return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

bool operator>(const FILETIME &a, const FILETIME &b) noexcept
{
	return std::tie(a.dwHighDateTime, a.dwLowDateTime) >
	       std::tie(b.dwHighDateTime, b.dwLowDateTime);
}

bool operator>=(const FILETIME &a, const FILETIME &b) noexcept
{
	return a > b || a == b;
}

bool operator<(const FILETIME &a, const FILETIME &b) noexcept
{
	return std::tie(a.dwHighDateTime, a.dwLowDateTime) <
	       std::tie(b.dwHighDateTime, b.dwLowDateTime);
}

bool operator<=(const FILETIME &a, const FILETIME &b) noexcept
{
	return a < b || a == b;
}

#ifndef KC_USES_TIMEGM
time_t timegm(struct tm *t)
{
	char *s_tz = nullptr, *tz = getenv("TZ");
	if (tz != nullptr)
		s_tz = strdup(tz);

	/*
	 * SuSE 9.1 segfaults when putenv() is used in a detached thread on the
	 * next getenv() call. so use setenv() on Linux, putenv() on others.
	 */
	setenv("TZ", "UTC0", 1);
	tzset();
	auto convert = mktime(t);
	if (s_tz != nullptr) {
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
	if (tmp == nullptr)
		memset(result, 0, sizeof(struct tm));
	return tmp;
}

double timespec2dbl(const struct timespec &t)
{
	return static_cast<double>(t.tv_sec) + t.tv_nsec / 1000000000.0;
}

} /* namespace */
