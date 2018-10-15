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
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include "fileutil.h"

namespace KC {

// Does mkdir -p <path>
int CreatePath(std::string s, unsigned int mode)
{
	if (s.size() == 0)
		return -ENOENT;
	size_t p = 0;
	while (s[p] == '/')
		/* No need to create the root directory; it always exists. */
		++p;
	do {
		p = s.find('/', p);
		if (p == std::string::npos)
			break;
		s[p] = '\0';
		auto ret = mkdir(s.c_str(), mode);
		if (ret != 0 && errno != EEXIST)
			return -errno;
		s[p++] = '/';
		while (s[p] == '/')
			++p;
	} while (true);
	auto ret = mkdir(s.c_str(), mode);
	if (ret != 0 && errno != EEXIST)
		return -errno;
	return 0;
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
