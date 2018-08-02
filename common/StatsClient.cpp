/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include "StatsClient.h"
#include "fileutil.h"

namespace KC {

static void submitThreadDo(void *p)
{
	auto psc = static_cast<StatsClient *>(p);
	time_t now = time(NULL);

	scoped_lock l_map(psc->mapsLock);
	for (const auto &it : psc->countsMapDouble)
		psc->submit(it.first, now, it.second);
	psc->countsMapDouble.clear();
	for (const auto &it : psc->countsMapInt64)
		psc->submit(it.first, now, it.second);
	psc->countsMapInt64.clear();
}

static void *submitThread(void *p)
{
	kcsrv_blocksigs();
	auto psc = static_cast<StatsClient *>(p);
	ec_log_debug("Submit thread started");
	pthread_cleanup_push(submitThreadDo, p);

	while(!psc -> terminate) {
		sleep(300);

		submitThreadDo(p);
	}

	pthread_cleanup_pop(1);
	ec_log_debug("Submit thread stopping");
	return NULL;
}

int StatsClient::startup(const std::string &collectorSocket)
{
	int ret = -1;

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd == -1) {
		ec_log_err("StatsClient cannot create socket: %s", strerror(errno));
		return -errno; /* maybe log a bit */
	}

	rand_init();
	ec_log_debug("StatsClient binding socket");

	for (unsigned int retry = 3; retry > 0; --retry) {
		struct sockaddr_un laddr;
		memset(&laddr, 0, sizeof(laddr));
		laddr.sun_family = AF_UNIX;
		ret = snprintf(laddr.sun_path, sizeof(laddr.sun_path), "%s/.%x%x.sock", TmpPath::instance.getTempPath().c_str(), rand(), rand());
		if (ret >= 0 &&
		    static_cast<size_t>(ret) >= sizeof(laddr.sun_path)) {
			ec_log_err("%s: Random path too long (%s...) for AF_UNIX socket",
				__func__, laddr.sun_path);
			return -ENAMETOOLONG;
		}

		ret = bind(fd, reinterpret_cast<const struct sockaddr *>(&laddr),
		      sizeof(laddr));
		if (ret == 0) {
			ec_log_debug("StatsClient bound socket to %s", laddr.sun_path);
			unlink(laddr.sun_path);
			break;
		}
		ret = -errno;
		ec_log_err("StatsClient bind %s: %s", laddr.sun_path, strerror(errno));
		if (ret == -EADDRINUSE)
			return ret;
	}
	if (ret != 0)
		return ret;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	ret = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", collectorSocket.c_str());
	if (ret >= 0 && static_cast<size_t>(ret) >= sizeof(addr.sun_path)) {
		ec_log_err("%s: Path \"%s\" too long for AF_UNIX socket",
			__func__, collectorSocket.c_str());
		return -ENAMETOOLONG;
	}

	addr_len = sizeof(addr);
	ret = pthread_create(&countsSubmitThread, nullptr, submitThread, this);
	if (ret != 0) {
		ec_log_err("Could not create StatsClient submit thread: %s", strerror(ret));
		return -ret;
	}
	thread_running = true;
	set_thread_name(countsSubmitThread, "StatsClient");
	ec_log_debug("StatsClient thread started");
	return 0;
}

StatsClient::~StatsClient() {
	ec_log_debug("StatsClient terminating");
	terminate = true;
	if (thread_running) {
		// interrupt sleep()
		pthread_cancel(countsSubmitThread);
		void *dummy = NULL;
		pthread_join(countsSubmitThread, &dummy);
	}
	close(fd);
	ec_log_debug("StatsClient terminated");
}

void StatsClient::submit(const std::string & key, const time_t ts, const double value) {
	if (fd == -1)
		return;

	char msg[4096];
	int len = snprintf(msg, sizeof msg, "ADD float %s %ld %f", key.c_str(), ts, value);

	// in theory snprintf can return -1
	if (len <= 0)
		return;
	int rc = sendto(fd, msg, len, 0, (struct sockaddr *)&addr, addr_len);
	if (rc == -1)
		ec_log_debug("StatsClient submit float failed: %s", strerror(errno));
}

void StatsClient::submit(const std::string & key, const time_t ts, const int64_t value) {
	if (fd == -1)
		return;

	char msg[4096];
	int len = snprintf(msg, sizeof msg, "ADD int %s %ld %zd",
	          key.c_str(), static_cast<long>(ts),
	          static_cast<size_t>(value));

	// in theory snprintf can return -1
	if (len <= 0)
		return;
	int rc = sendto(fd, msg, len, 0, (struct sockaddr *)&addr, addr_len);
	if (rc == -1)
		ec_log_debug("StatsClient submit int failed: %s", strerror(errno));
}

void StatsClient::countInc(const std::string & key, const std::string & key_sub) {
	countAdd(key, key_sub, int64_t(1));
}

void StatsClient::countAdd(const std::string & key, const std::string & key_sub, const double n) {
	std::string kp = key + " " + key_sub;
	scoped_lock l_map(mapsLock);

	auto doubleIterator = countsMapDouble.find(kp);
	if (doubleIterator == countsMapDouble.cend())
		countsMapDouble.emplace(kp, n);
	else
		doubleIterator -> second += n;
}

void StatsClient::countAdd(const std::string & key, const std::string & key_sub, const int64_t n) {
	std::string kp = key + " " + key_sub;
	scoped_lock l_map(mapsLock);

	auto int64Iterator = countsMapInt64.find(kp);
	if (int64Iterator == countsMapInt64.cend())
		countsMapInt64.emplace(kp, n);
	else
		int64Iterator -> second += n;
}

} /* namespace */
