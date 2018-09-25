/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include "config.h"
#include <mutex>
#include <string>
#include <pthread.h>
#include <unistd.h>
#ifdef HAVE_CURL_CURL_H
#	include <condition_variable>
#	include <thread>
#	include <curl/curl.h>
#	include <json/writer.h>
#endif
#include <libHX/map.h>
#include <libHX/option.h>
#include <kopano/platform.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/stringutil.h>
#include "StatsClient.h"
#include "fileutil.h"

namespace KC {

static void *submitThread(void *p)
{
	kcsrv_blocksigs();
	ec_log_debug("Submit thread started");
	static_cast<StatsClient *>(p)->mainloop();
	ec_log_debug("Submit thread stopping");
	return NULL;
}

StatsClient::~StatsClient() {
	ec_log_debug("StatsClient terminating");
	terminate = true;
	m_exitsig.notify_one();
	if (thread_running) {
		// interrupt sleep()
		pthread_cancel(countsSubmitThread);
		void *dummy = NULL;
		pthread_join(countsSubmitThread, &dummy);
	}
	ec_log_debug("StatsClient terminated");
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

#ifdef HAVE_CURL_CURL_H
static size_t curl_dummy_write(char *, size_t z, size_t n, void *)
{
	return z * n;
}
#endif

static bool sc_proxy_from_env(CURL *ch, const char *url)
{
	auto ssl = url != nullptr && strncmp(url, "https:", 6) == 0;
	const char *v = getenv(ssl ? "https_proxy" : "http_proxy");
	if (v == nullptr)
		return false;
	curl_easy_setopt(ch, CURLOPT_PROXY, v);
	v = getenv("no_proxy");
	if (v != nullptr)
		curl_easy_setopt(ch, CURLOPT_NOPROXY, v);
	return true;
}

#ifdef HAVE_CURL_CURL_H
static void sc_proxy_from_sysconfig(CURL *ch, const char *url)
{
	struct mapfree { void operator()(struct HXmap *m) { HXmap_free(m); } };
	std::unique_ptr<HXmap, mapfree> map(HX_shconfig_map("/etc/sysconfig/proxy"));
	if (map == nullptr)
		return;
	auto v = HXmap_get<const char *>(map.get(), "PROXY_ENABLED");
	if (v == nullptr || strcasecmp(v, "yes") != 0)
		return;
	auto ssl = url != nullptr && strncmp(url, "https:", 6) == 0;
	v = HXmap_get<const char *>(map.get(), ssl ? "HTTPS_PROXY" : "HTTP_PROXY");
	if (v != nullptr)
		curl_easy_setopt(ch, CURLOPT_PROXY, v);
	v = HXmap_get<const char *>(map.get(), "NO_PROXY");
	if (v != nullptr)
		curl_easy_setopt(ch, CURLOPT_NOPROXY, v);
}
#endif

void StatsClient::submit(std::string &&url)
{
#ifdef HAVE_CURL_CURL_H
	struct slfree { void operator()(curl_slist *s) { curl_slist_free_all(s); } };
	Json::Value root;
	root["version"] = 1;

	std::unique_lock<std::mutex> lk(mapsLock);
	for (const auto &p : countsMapInt64)
		root[p.first] = static_cast<Json::Value::Int64>(p.second);
	for (const auto &p : countsMapDouble)
		root[p.first] = p.second;
	lk.unlock();

	auto text = Json::writeString(Json::StreamWriterBuilder(), std::move(root));
	auto ch = curl_easy_init();
	std::unique_ptr<curl_slist, slfree> hl(curl_slist_append(nullptr, "Content-Type: application/json"));
	if (!sc_proxy_from_env(ch, url.c_str()))
		sc_proxy_from_sysconfig(ch, url.c_str());
	curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(ch, CURLOPT_TCP_NODELAY, 0L);
	curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);
	if (strncmp(url.c_str(), "unix:", 5) == 0) {
#if LIBCURL_VERSION_MAJOR >= 7 || (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 40)
		curl_easy_setopt(ch, CURLOPT_UNIX_SOCKET_PATH, url.c_str() + 5);
		curl_easy_setopt(ch, CURLOPT_URL, "http://localhost/");
#else
		return;
#endif
	} else {
		curl_easy_setopt(ch, CURLOPT_URL, url.c_str());
	}
	curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(ch, CURLOPT_HTTPHEADER, hl.get());
	curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(text.size()));
	curl_easy_setopt(ch, CURLOPT_POSTFIELDS, text.c_str());
	curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curl_dummy_write);
	curl_easy_perform(ch);
#endif
}

void StatsClient::mainloop()
{
#ifdef HAVE_CURL_CURL_H
	std::mutex mtx;
	do {
		auto url1 = m_config->GetSetting("statsclient_url");
		auto interval1 = m_config->GetSetting("statsclient_interval");
		if (url1 == nullptr || interval1 == nullptr)
			return;
		auto interval = atoui(interval1);
		if (interval > 0)
			submit(url1);
		else
			interval = 60;
		ulock_normal blah(mtx);
		if (m_exitsig.wait_for(blah, std::chrono::seconds(interval)) != std::cv_status::timeout)
			break;
	} while (!terminate);
#endif
}

StatsClient::StatsClient(std::shared_ptr<ECConfig> config) :
	m_config(std::move(config))
{
	if (m_config == nullptr)
		return;
	auto ret = pthread_create(&countsSubmitThread, nullptr, submitThread, this);
	if (ret == 0)
		thread_running = true;
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

void ECStatsCollector::inc(SCName name, double inc)
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

void ECStatsCollector::Set(SCName name, double set)
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

void ECStatsCollector::Avg(SCName name, double add)
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
		return stringify_double(iSD.second.data.f);
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
