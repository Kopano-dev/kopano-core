/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef PLATFORM_H
#define PLATFORM_H

  #ifdef HAVE_CONFIG_H
  #include "config.h"
  #endif
  #include <kopano/platform.linux.h>
#include <kopano/zcdefs.h>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <endian.h>
#include <pthread.h>

enum {
	KC_DESIRED_FILEDES = 8192,
};

namespace KC {

#define S_IRWUG (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)
#define S_IRWXUG (S_IRWXU | S_IRWXG)
#define S_IRWUGO (S_IRWUG | S_IROTH | S_IWOTH)

#define KOPANO_SYSTEM_USER		"SYSTEM"
#define KOPANO_SYSTEM_USER_W	L"SYSTEM"

/* This should match what is used in proto.h for __size */
typedef int gsoap_size_t;

/*
 * Platform independent functions
 */
// Random-number generators
extern KC_EXPORT void rand_init();
extern KC_EXPORT int rand_mt();
extern KC_EXPORT void rand_get(char *p, int n);
extern KC_EXPORT char *get_password(const char *prompt);

/**
 * Memory usage calculation macros
 */
#define MEMALIGN(x) (((x) + alignof(void *) - 1) & ~(alignof(void *) - 1))

#define MEMORY_USAGE_MAP(items, map)		(items * (sizeof(map) + sizeof(map::value_type)))
#define MEMORY_USAGE_LIST(items, list)		(items * (MEMALIGN(sizeof(list) + sizeof(list::value_type))))
#define MEMORY_USAGE_HASHMAP(items, map)	MEMORY_USAGE_MAP(items, map)
#define MEMORY_USAGE_STRING(str)			(str.capacity() + 1)
#define MEMORY_USAGE_MULTIMAP(items, map)	MEMORY_USAGE_MAP(items, map)

extern KC_EXPORT void set_thread_name(pthread_t, const std::string &);
extern KC_EXPORT int ec_relocate_fd(int);
extern KC_EXPORT void kcsrv_blocksigs();
extern KC_EXPORT unsigned long kc_threadid();

/* Determine the size of an array */
template<typename T, size_t N> constexpr inline size_t ARRAY_SIZE(T (&)[N]) { return N; }

/* Get the one-past-end item of an array */
template<typename T, size_t N> constexpr inline T *ARRAY_END(T (&a)[N]) { return a + N; }

template<typename T> constexpr const IID &iid_of();
template<typename T> static inline constexpr const IID &iid_of(const T &)
{
	return iid_of<typename std::remove_cv<typename std::remove_pointer<T>::type>::type>();
}

#if (defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN) || \
    (defined(_BYTE_ORDER) && _BYTE_ORDER == _BIG_ENDIAN)
	/* We need to use constexpr functions, and htole16 unfortunately is not. */
#	define cpu_to_le16(x) __builtin_bswap16(x)
#	define cpu_to_le32(x) __builtin_bswap32(x)
#	define cpu_to_le64(x) __builtin_bswap64(x)
#	define cpu_to_be64(x) (x)
#	define le16_to_cpu(x) __builtin_bswap16(x)
#	define le32_to_cpu(x) __builtin_bswap32(x)
#	define le64_to_cpu(x) __builtin_bswap64(x)
#	define be64_to_cpu(x) (x)
#	define KC_BIGENDIAN 1
#else
#	define cpu_to_le16(x) (x)
#	define cpu_to_le32(x) (x)
#	define cpu_to_le64(x) (x)
#	define cpu_to_be64(x) __builtin_bswap64(x)
#	define le16_to_cpu(x) (x)
#	define le32_to_cpu(x) (x)
#	define le64_to_cpu(x) (x)
#	define be64_to_cpu(x) __builtin_bswap64(x)
#	undef KC_BIGENDIAN
#endif

static inline uint16_t get_unaligned_le16(const uint16_t *p)
{
	uint16_t v;
	memcpy(&v, p, sizeof(v));
	return le16_to_cpu(v);
}

static inline uint32_t get_unaligned_le32(const uint32_t *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return le32_to_cpu(v);
}

#if __cplusplus >= 201700L
using shared_mutex = std::shared_mutex;
#else
using shared_mutex = std::shared_timed_mutex;
#endif

template<class Mutex> using shared_lock = std::shared_lock<Mutex>;
typedef std::lock_guard<std::mutex> scoped_lock;
typedef std::lock_guard<std::recursive_mutex> scoped_rlock;
typedef std::unique_lock<std::mutex> ulock_normal;
typedef std::unique_lock<std::recursive_mutex> ulock_rec;

} /* namespace */

#define IID_OF(T) namespace KC { template<> inline constexpr const IID &iid_of<T>() { return IID_ ## T; } }
#define IID_OF2(T, U) namespace KC { template<> inline constexpr const IID &iid_of<T>() { return IID_ ## U; } }

#endif // PLATFORM_H
