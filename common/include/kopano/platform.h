/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <kopano/zcdefs.h>

enum {
	KC_DESIRED_FILEDES = 8192,
};

  #ifdef HAVE_CONFIG_H
  #include "config.h"
  #endif
  #include <kopano/platform.linux.h>
#include <chrono>
#include <mutex>
#if __cplusplus >= 201400L
#	include <shared_mutex>
#endif
#include <string>
#include <type_traits>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <endian.h>
#include <pthread.h>

namespace KC {

#define KOPANO_SYSTEM_USER		"SYSTEM"
#define KOPANO_SYSTEM_USER_W	L"SYSTEM"

/* This should match what is used in proto.h for __size */
typedef int gsoap_size_t;

/*
 * Platform independent functions
 */
extern _kc_export FILETIME UnixTimeToFileTime(time_t);
extern _kc_export time_t FileTimeToUnixTime(const FILETIME &);
extern _kc_export void UnixTimeToFileTime(time_t, int *hi, unsigned int *lo);
extern _kc_export LONG FileTimeToRTime(const FILETIME &);
extern _kc_export int FileTimeToTimestamp(const FILETIME &, time_t &, char *, size_t);
extern _kc_export LONG UnixTimeToRTime(time_t);
extern _kc_export time_t RTimeToUnixTime(LONG rtime);
extern _kc_export struct tm *gmtime_safe(time_t, struct tm *);
extern _kc_export double timespec2dbl(const struct timespec &);
extern bool operator==(const FILETIME &, const FILETIME &) noexcept;
extern _kc_export bool operator >(const FILETIME &, const FILETIME &) noexcept;
extern bool operator>=(const FILETIME &, const FILETIME &) noexcept;
extern _kc_export bool operator <(const FILETIME &, const FILETIME &) noexcept;
extern bool operator<=(const FILETIME &, const FILETIME &) noexcept;

/* convert struct tm to time_t in timezone UTC0 (GM time) */
#ifndef HAVE_TIMEGM
time_t timegm(struct tm *t);
#endif

// mkdir -p
extern _kc_export int CreatePath(std::string, unsigned int = 0770);

// Random-number generators
extern _kc_export void rand_init(void);
extern _kc_export int rand_mt(void);
extern _kc_export void rand_get(char *p, int n);
extern _kc_export char *get_password(const char *prompt);

/**
 * Memory usage calculation macros
 */
#define MEMALIGN(x) (((x) + alignof(void *) - 1) & ~(alignof(void *) - 1))

#define MEMORY_USAGE_MAP(items, map)		(items * (sizeof(map) + sizeof(map::value_type)))
#define MEMORY_USAGE_LIST(items, list)		(items * (MEMALIGN(sizeof(list) + sizeof(list::value_type))))
#define MEMORY_USAGE_HASHMAP(items, map)	MEMORY_USAGE_MAP(items, map)
#define MEMORY_USAGE_STRING(str)			(str.capacity() + 1)
#define MEMORY_USAGE_MULTIMAP(items, map)	MEMORY_USAGE_MAP(items, map)

extern _kc_export ssize_t read_retry(int, void *, size_t);
extern _kc_export ssize_t write_retry(int, const void *, size_t);
extern _kc_export void set_thread_name(pthread_t, const std::string &);
extern _kc_export bool force_buffers_to_disk(int fd);
extern _kc_export int ec_relocate_fd(int);
extern _kc_export void kcsrv_blocksigs(void);
extern _kc_export unsigned long kc_threadid(void);

/* Determine the size of an array */
template<typename T, size_t N> constexpr inline size_t ARRAY_SIZE(T (&)[N]) { return N; }

/* Get the one-past-end item of an array */
template<typename T, size_t N> constexpr inline T *ARRAY_END(T (&a)[N]) { return a + N; }

template<typename T> constexpr const IID &iid_of();
template<typename T> static inline constexpr const IID &iid_of(const T &)
{
	return iid_of<typename std::remove_cv<typename std::remove_pointer<T>::type>::type>();
}

using time_point = std::chrono::time_point<std::chrono::steady_clock>;

template<typename T> static constexpr inline double dur2dbl(const T &t)
{
	return std::chrono::duration_cast<std::chrono::duration<double>>(t).count();
}

#if (defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN) || \
    (defined(_BYTE_ORDER) && _BYTE_ORDER == _BIG_ENDIAN)
	/* We need to use constexpr functions, and htole16 unfortunately is not. */
#	define cpu_to_le16(x) __builtin_bswap16(x)
#	define cpu_to_le32(x) __builtin_bswap32(x)
#	define cpu_to_be64(x) (x)
#	define le16_to_cpu(x) __builtin_bswap16(x)
#	define le32_to_cpu(x) __builtin_bswap32(x)
#	define be64_to_cpu(x) (x)
#	define KC_BIGENDIAN 1
#else
#	define cpu_to_le16(x) (x)
#	define cpu_to_le32(x) (x)
#	define cpu_to_be64(x) __builtin_bswap64(x)
#	define le16_to_cpu(x) (x)
#	define le32_to_cpu(x) (x)
#	define be64_to_cpu(x) __builtin_bswap64(x)
#	undef KC_BIGENDIAN
#endif

#if __cplusplus >= 201700L
using shared_mutex = std::shared_mutex;
#elif __cplusplus >= 201400L
using shared_mutex = std::shared_timed_mutex;
#else
class shared_mutex {
	public:
	~shared_mutex(void) { pthread_rwlock_destroy(&mtx); }
	void lock(void) { pthread_rwlock_wrlock(&mtx); }
	void unlock(void) { pthread_rwlock_unlock(&mtx); }
	void lock_shared(void) { pthread_rwlock_rdlock(&mtx); }
	void unlock_shared(void) { pthread_rwlock_unlock(&mtx); }

	private:
	pthread_rwlock_t mtx = PTHREAD_RWLOCK_INITIALIZER;
};
#endif

#if __cplusplus >= 201400L
template<class Mutex> using shared_lock = std::shared_lock<Mutex>;
#else
template<class Mutex> class shared_lock {
	public:
	shared_lock(Mutex &m) : mtx(m), locked(true)
	{
		mtx.lock_shared();
	}
	~shared_lock(void)
	{
		if (locked)
			mtx.unlock_shared();
	}
	void lock(void)
	{
		assert(!locked);
		mtx.lock_shared();
		locked = true;
	}
	void unlock(void)
	{
		assert(locked);
		mtx.unlock_shared();
		locked = false;
	}
	private:
	Mutex &mtx;
	bool locked = false;
};
#endif

typedef std::lock_guard<std::mutex> scoped_lock;
typedef std::lock_guard<std::recursive_mutex> scoped_rlock;
typedef std::unique_lock<std::mutex> ulock_normal;
typedef std::unique_lock<std::recursive_mutex> ulock_rec;

namespace chrono_literals {

#if __cplusplus >= 201400L
using namespace std::chrono_literals;
#else
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wliteral-suffix"
static constexpr inline std::chrono::seconds operator"" s(unsigned long long x) { return std::chrono::seconds{x}; }
#	pragma GCC diagnostic pop
#endif

}

namespace string_literals {

#if __cplusplus >= 201400L
using namespace std::string_literals;
#else
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wliteral-suffix"
static inline std::string operator"" s(const char *str, std::size_t len) { return std::string(str, len); }
static inline std::wstring operator"" s(const wchar_t *str, std::size_t len) { return std::wstring(str, len); }
#	pragma GCC diagnostic pop
#endif

}

class _kc_export KAlternateStack {
	public:
	KAlternateStack();
	~KAlternateStack();
	protected:
	stack_t st;
};

} /* namespace */

#define IID_OF(T) namespace KC { template<> inline constexpr const IID &iid_of<T>() { return IID_ ## T; } }
#define IID_OF2(T, U) namespace KC { template<> inline constexpr const IID &iid_of<T>() { return IID_ ## U; } }

#endif // PLATFORM_H
