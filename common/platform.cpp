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

namespace KC {

void set_thread_name(pthread_t tid, const std::string & name)
{
#ifdef HAVE_PTHREAD_SETNAME_NP_2
	if (name.size() > 15)
		pthread_setname_np(tid, name.substr(0, 15).c_str());
	else
		pthread_setname_np(tid, name.c_str());
#endif
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
