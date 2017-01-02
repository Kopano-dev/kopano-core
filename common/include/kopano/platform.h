/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <kopano/zcdefs.h>
#if defined(__GNUC__) && !defined(__cplusplus)
	/*
	 * If typeof @a stays the same through a demotion to pointer,
	 * @a cannot be an array.
	 */
#	define __array_size_check(a) BUILD_BUG_ON_EXPR(\
		__builtin_types_compatible_p(__typeof__(a), \
		__typeof__(DEMOTE_TO_PTR(a))))
#else
#	define __array_size_check(a) 0
#endif
#ifndef ARRAY_SIZE
#	define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)) + __array_size_check(x))
#endif

/* About _FORTIFY_SOURCE a.k.a. _BREAKIFY_SOURCE_IN_INSANE_WAYS
 *
 * This has the insane feature that it will assert() when you attempt
 * to FD_SET(n, &set) with n > 1024, irrespective of your compile-time FD_SETSIZE
 * Even stranger, this should be easily fixed but nobody is interested apparently.
 *
 * Although you could just not use select() anywhere, we depend on some libs that
 * are doing select(), so we can't just remove them. 
 *
 * This will also disable a few other valid buffer-overflow checks, but we'll have
 * to live with that for now.
 */

#ifdef _FORTIFY_SOURCE
#undef _FORTIFY_SOURCE
#endif

  // We have to include this now in case select.h is included too soon.
  // Increase our maximum amount of file descriptors to 8192
  #include <bits/types.h>
  #undef __FD_SETSIZE
  #define __FD_SETSIZE 8192

  // Log the pthreads locks
  #define DEBUG_PTHREADS 0

  #ifdef HAVE_CONFIG_H
  #include "config.h"
  #endif
  #include <kopano/platform.linux.h>
#include <string>
#include <pthread.h>

namespace KC {

#define KOPANO_SYSTEM_USER		"SYSTEM"
#define KOPANO_SYSTEM_USER_W	L"SYSTEM"

/* This should match what is used in proto.h for __size */
typedef int gsoap_size_t;

/*
 * Platform independent functions
 */
extern _kc_export HRESULT UnixTimeToFileTime(time_t, FILETIME *);
extern _kc_export HRESULT FileTimeToUnixTime(const FILETIME &, time_t *);
extern _kc_export void UnixTimeToFileTime(time_t, int *hi, unsigned int *lo);
extern _kc_export time_t FileTimeToUnixTime(unsigned int hi, unsigned int lo);

void	RTimeToFileTime(LONG rtime, FILETIME *pft);
extern _kc_export void FileTimeToRTime(const FILETIME *, LONG *rtime);
extern _kc_export HRESULT UnixTimeToRTime(time_t unixtime, LONG *rtime);
extern _kc_export HRESULT RTimeToUnixTime(LONG rtime, time_t *unixtime);
extern _kc_export double GetTimeOfDay(void);

inline double difftimeval(struct timeval *ptstart, struct timeval *ptend) {
	return 1000000 * (ptend->tv_sec - ptstart->tv_sec) + (ptend->tv_usec - ptstart->tv_usec);
}

extern _kc_export struct tm *gmtime_safe(const time_t *timer, struct tm *result);

/**
 * Creates the deadline timespec, which is the current time plus the specified
 * amount of milliseconds.
 *
 * @param[in]	ulTimeoutMs		The timeout in ms.
 * @return		The required timespec.
 */
struct timespec GetDeadline(unsigned int ulTimeoutMs);

extern _kc_export double timespec2dbl(const struct timespec &);

bool operator ==(const FILETIME &, const FILETIME &);
extern _kc_export bool operator >(const FILETIME &, const FILETIME &);
bool operator >=(const FILETIME &, const FILETIME &);
extern _kc_export bool operator <(const FILETIME &, const FILETIME &);
bool operator <=(const FILETIME &, const FILETIME &);
extern _kc_export time_t operator -(const FILETIME &, const FILETIME &);

/* convert struct tm to time_t in timezone UTC0 (GM time) */
#ifndef __linux__
time_t timegm(struct tm *t);
#endif

// mkdir -p
extern _kc_export int CreatePath(const char *);

// Random-number generators
extern _kc_export void rand_init(void);
extern _kc_export int rand_mt(void);
extern _kc_export void rand_free(void);
extern _kc_export void rand_get(char *p, int n);
extern _kc_export char *get_password(const char *prompt);

 #define KDLLAPI

/**
 * Memory usage calculation macros
 */
#define MEMALIGN(x) (((x) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))

#define MEMORY_USAGE_MAP(items, map)		(items * (sizeof(map) + sizeof(map::value_type)))
#define MEMORY_USAGE_LIST(items, list)		(items * (MEMALIGN(sizeof(list) + sizeof(list::value_type))))
#define MEMORY_USAGE_HASHMAP(items, map)	MEMORY_USAGE_MAP(items, map)
#define MEMORY_USAGE_STRING(str)			(str.capacity() + 1)
#define MEMORY_USAGE_MULTIMAP(items, map)	MEMORY_USAGE_MAP(items, map)

extern _kc_export ssize_t read_retry(int, void *, size_t);
extern _kc_export ssize_t write_retry(int, const void *, size_t);

extern _kc_export void set_thread_name(pthread_t, const std::string &);
extern _kc_export void my_readahead(int fd);
extern _kc_export void give_filesize_hint(int fd, off_t len);

extern _kc_export bool force_buffers_to_disk(int fd);
extern _kc_export int ec_relocate_fd(int);

} /* namespace */

#endif // PLATFORM_H
