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
#include <kopano/stringutil.h>
#include "StatsClient.h"
#include "fileutil.h"

namespace KC {

static ECStatsCollector main_collector;
ECStatsCollector *const g_lpStatsCollector = &main_collector;

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

void StatsClient::inc(enum SCName k, double n)
{
	auto kp = std::to_string(k);
	scoped_lock l_map(mapsLock);

	auto doubleIterator = countsMapDouble.find(kp);
	if (doubleIterator == countsMapDouble.cend())
		countsMapDouble.emplace(kp, n);
	else
		doubleIterator -> second += n;
}

void StatsClient::inc(enum SCName k, int64_t n)
{
	auto kp = std::to_string(k);
	scoped_lock l_map(mapsLock);

	auto int64Iterator = countsMapInt64.find(kp);
	if (int64Iterator == countsMapInt64.cend())
		countsMapInt64.emplace(kp, n);
	else
		int64Iterator -> second += n;
}

void ECStatsCollector::AddStat(SCName index, SCType type, const char *name,
    const char *description)
{
	ECStat &newStat = m_StatData[index];

	newStat.data.ll = 0;		// reset largest data var in union
	newStat.avginc = 1;
	newStat.type = type;
	newStat.name = name;
	newStat.description = description;
}

void ECStatsCollector::inc(SCName name, float inc)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_FLOAT);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.f += inc;
}

void ECStatsCollector::inc(SCName name, int v)
{
	inc(name, static_cast<LONGLONG>(v));
}

void ECStatsCollector::inc(SCName name, LONGLONG inc)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_LONGLONG);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ll += inc;
}

void ECStatsCollector::Set(SCName name, float set)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_FLOAT);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.f = set;
}

void ECStatsCollector::Set(SCName name, LONGLONG set)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_LONGLONG);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ll = set;
}

void ECStatsCollector::SetTime(SCName name, time_t set)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_TIMESTAMP);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ts = set;
}

void ECStatsCollector::Max(SCName name, LONGLONG max)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_LONGLONG);
	scoped_lock lk(iSD->second.lock);
	if (iSD->second.data.ll < max)
		iSD->second.data.ll = max;
}

void ECStatsCollector::Avg(SCName name, float add)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_FLOAT);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.f = ((add - iSD->second.data.f) / iSD->second.avginc) + iSD->second.data.f;
	++iSD->second.avginc;
	if (iSD->second.avginc == 0)
		iSD->second.avginc = 1;
}

void ECStatsCollector::Avg(SCName name, LONGLONG add)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_LONGLONG);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ll = ((add - iSD->second.data.ll) / iSD->second.avginc) + iSD->second.data.ll;
	++iSD->second.avginc;
	if (iSD->second.avginc == 0)
		iSD->second.avginc = 1;
}

std::string ECStatsCollector::GetValue(const SCMap::const_iterator::value_type &iSD)
{
	switch (iSD.second.type) {
	case SCDT_FLOAT:
		return stringify_float(iSD.second.data.f);
	case SCDT_LONGLONG:
		return stringify_int64(iSD.second.data.ll);
	case SCDT_TIMESTAMP: {
		if (iSD.second.data.ts <= 0)
			break;
		char timestamp[128] = { 0 };
		struct tm *tm = localtime(&iSD.second.data.ts);
		strftime(timestamp, sizeof timestamp, "%a %b %e %T %Y", tm);
		return timestamp;
	}
	}
	return "";
}

std::string ECStatsCollector::GetValue(const SCName &name)
{
	auto iSD = m_StatData.find(name);
	if (iSD != m_StatData.cend())
		return GetValue(*iSD);
	return {};
}

void ECStatsCollector::ForEachStat(void(callback)(const std::string &, const std::string &, const std::string &, void *), void *obj)
{
	for (auto &i : m_StatData) {
		scoped_lock lk(i.second.lock);
		callback(i.second.name, i.second.description, GetValue(i), obj);
	}
}

void ECStatsCollector::Reset()
{
	for (auto &i : m_StatData) {
		// reset largest var in union
		scoped_lock lk(i.second.lock);
		i.second.data.ll = 0;
	}
}

void ECStatsCollector::Reset(SCName name)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	/* reset largest var in union */
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ll = 0;
}

} /* namespace */
