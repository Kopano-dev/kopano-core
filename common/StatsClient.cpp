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
	static_cast<StatsClient *>(p)->mainloop();
	return NULL;
}

void ECStatsCollector::stop()
{
	if (!thread_running)
		return;
	terminate = true;
	m_exitsig.notify_one();
	void *dummy = nullptr;
	pthread_join(countsSubmitThread, &dummy);
	thread_running = false;
	terminate = false;
}

ECStatsCollector::~ECStatsCollector()
{
	stop();
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

void ECStatsCollector::submit(std::string &&url)
{
#ifdef HAVE_CURL_CURL_H
	struct slfree {
		void operator()(CURL *p) { curl_easy_cleanup(p); }
		void operator()(curl_slist *s) { curl_slist_free_all(s); }
	};
	Json::Value root;
	root["version"] = 2;
	fill_odm();

	for (auto &i : m_StatData) {
		scoped_lock lk(i.second.lock);
		Json::Value leaf;
		leaf["desc"] = i.second.description;
		switch (i.second.type) {
		case SCDT_FLOAT:
			leaf["value"] = i.second.data.f;
			break;
		case SCDT_LONGLONG:
			leaf["value"] = static_cast<Json::Value::Int64>(i.second.data.ll);
			break;
		case SCDT_TIMESTAMP:
			leaf["value"] = static_cast<Json::Value::Int64>(i.second.data.ts);
			break;
		case SCDT_STRING:
			leaf["value"] = i.second.strdata;
			break;
		}
		root["stats"][i.second.name] = leaf;
	}
	std::unique_lock<std::mutex> lk(m_odm_lock);
	for (auto &i : m_ondemand) {
		Json::Value leaf;
		leaf["desc"] = i.second.desc;
		switch (i.second.type) {
		case SCDT_FLOAT:
			leaf["value"] = i.second.data.f;
			break;
		case SCDT_LONGLONG:
			leaf["value"] = static_cast<Json::Value::Int64>(i.second.data.ll);
			break;
		case SCDT_TIMESTAMP:
			leaf["value"] = static_cast<Json::Value::Int64>(i.second.data.ts);
			break;
		case SCDT_STRING:
			leaf["value"] = i.second.strdata;
			break;
		}
		root["stats"][i.first] = leaf;
	}
	lk.unlock();

	auto text = Json::writeString(Json::StreamWriterBuilder(), std::move(root));
	std::unique_ptr<CURL, slfree> chp(curl_easy_init());
	std::unique_ptr<curl_slist, slfree> hl(curl_slist_append(nullptr, "Content-Type: application/json"));
	CURL *ch = chp.get();
	if (!sc_proxy_from_env(ch, url.c_str()))
		sc_proxy_from_sysconfig(ch, url.c_str());
	curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(ch, CURLOPT_TCP_NODELAY, 0L);
	curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);
	if (strncmp(url.c_str(), "unix:", 5) == 0) {
#if LIBCURL_VERSION_MAJOR > 7 || (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 40)
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

void ECStatsCollector::mainloop()
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

ECStatsCollector::ECStatsCollector(std::shared_ptr<ECConfig> config) :
	m_config(std::move(config))
{
	AddStat(SCN_MACHINE_ID, SCDT_STRING, "machine_id");
	std::unique_ptr<FILE, file_deleter> fp(fopen("/etc/machine-id", "r"));
	if (fp != nullptr) {
		std::string mid;
		HrMapFileToString(fp.get(), &mid);
		auto pos = mid.find('\n');
		if (pos != std::string::npos)
			mid.erase(pos);
		set(SCN_MACHINE_ID, mid);
	}
	if (m_config == nullptr)
		return;
}

void ECStatsCollector::start()
{
	if (thread_running)
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

void ECStatsCollector::set_dbl(enum SCName name, double set)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_FLOAT);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.f = set;
}

void ECStatsCollector::set(enum SCName name, LONGLONG set)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_LONGLONG);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ll = set;
}

void ECStatsCollector::SetTime(enum SCName name, time_t set)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCDT_TIMESTAMP);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ts = set;
}

void ECStatsCollector::set(SCName name, const std::string &s)
{
	auto i = m_StatData.find(name);
	if (i == m_StatData.cend())
		return;
	assert(i->second.type == SCDT_STRING);
	scoped_lock lk(i->second.lock);
	i->second.strdata = s;
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

void ECStatsCollector::avg_dbl(SCName name, double add)
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

void ECStatsCollector::avg(SCName name, LONGLONG add)
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
	case SCDT_STRING:
		return iSD.second.strdata;
	}
	return "";
}

std::string ECStatsCollector::GetValue(const ECStat2 &i)
{
	switch (i.type) {
	case SCDT_FLOAT:
		return stringify_double(i.data.f);
	case SCDT_LONGLONG:
		return stringify_int64(i.data.ll);
	case SCDT_TIMESTAMP: {
		if (i.data.ts <= 0)
			break;
		char timestamp[128] = { 0 };
		struct tm *tm = localtime(&i.data.ts);
		strftime(timestamp, sizeof(timestamp), "%a %b %e %T %Y", tm);
		return timestamp;
	}
	case SCDT_STRING:
		return i.strdata;
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
	std::lock_guard<std::mutex> lk(m_odm_lock);
	for (const auto &i : m_ondemand)
		callback(i.first, i.second.desc, GetValue(i.second), obj);
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

void ECStatsCollector::set(const std::string &name, const std::string &desc, int64_t v)
{
	scoped_lock lk(m_odm_lock);
	auto i = m_ondemand.find(name);
	if (i != m_ondemand.cend()) {
		assert(i->second.type == SCDT_LONGLONG);
		i->second.data.ll = v;
		return;
	}
	ECStat2 st{desc, {}, SCDT_LONGLONG};
	st.data.ll = v;
	m_ondemand.emplace(name, std::move(st));
}

void ECStatsCollector::set_dbl(const std::string &name, const std::string &desc, double v)
{
	scoped_lock lk(m_odm_lock);
	auto i = m_ondemand.find(name);
	if (i != m_ondemand.cend()) {
		assert(i->second.type == SCDT_FLOAT);
		i->second.data.f = v;
		return;
	}
	ECStat2 st{desc, {}, SCDT_FLOAT};
	st.data.f = v;
	m_ondemand.emplace(name, std::move(st));
}

void ECStatsCollector::set(const std::string &name, const std::string &desc,
    const std::string &v)
{
	scoped_lock lk(m_odm_lock);
	auto i = m_ondemand.find(name);
	if (i == m_ondemand.cend()) {
		m_ondemand.emplace(name, ECStat2{desc, v, SCDT_STRING});
		return;
	}
	assert(i->second.type == SCDT_STRING);
	i->second.strdata = v;
}

} /* namespace */
