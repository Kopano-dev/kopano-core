/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include <mutex>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include <sys/utsname.h>
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
#include <kopano/ecversion.h>
#include <kopano/stringutil.h>
#include <kopano/timeutil.hpp>
#include "StatsClient.h"
#include <kopano/fileutil.hpp>

using namespace std::string_literals;

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

template<typename T> static void setleaf(Json::Value &leaf, const T &elem)
{
	switch (elem.type) {
	case SCT_REAL:
		leaf["value"] = elem.data.f;
		leaf["type"] = "real";
		leaf["mode"] = "counter";
		break;
	case SCT_REALGAUGE:
		leaf["value"] = elem.data.f;
		leaf["type"] = "real";
		leaf["mode"] = "gauge";
		break;
	case SCT_INTEGER:
		leaf["value"] = static_cast<Json::Value::Int64>(elem.data.ll);
		leaf["type"] = "int";
		leaf["mode"] = "counter";
		break;
	case SCT_INTGAUGE:
		leaf["value"] = static_cast<Json::Value::Int64>(elem.data.ll);
		leaf["type"] = "int";
		leaf["mode"] = "gauge";
		break;
	case SCT_TIME:
		leaf["value"] = static_cast<Json::Value::Int64>(elem.data.ts);
		leaf["type"] = "unixtime";
		leaf["mode"] = "counter";
		break;
	case SCT_STRING:
		leaf["value"] = elem.strdata;
		leaf["type"] = "string";
		break;
	default:
		break;
	}
}
#endif

std::string ECStatsCollector::stats_as_text()
{
	Json::Value root;
	root["version"] = 2;

	for (auto &i : m_StatData) {
		scoped_lock lk(i.second.lock);
		Json::Value leaf;
		leaf["desc"] = i.second.description;
		setleaf(leaf, i.second);
		root["stats"][i.second.name] = leaf;
	}
	std::unique_lock<std::mutex> lk(m_odm_lock);
	for (const auto &i : m_ondemand) {
		Json::Value leaf;
		leaf["desc"] = i.second.desc;
		setleaf(leaf, i.second);
		root["stats"][i.first] = leaf;
	}
	lk.unlock();
	return Json::writeString(Json::StreamWriterBuilder(), std::move(root));
}

std::string ECStatsCollector::survey_as_text()
{
	Json::Value root, leaf;
	root["version"] = 2;

	for (const auto &key : {SCN_MACHINE_ID, SCN_PROGRAM_NAME, SCN_PROGRAM_VERSION,
	    SCN_SERVER_GUID, SCN_UTSNAME, SCN_OSRELEASE}) {
		auto i = m_StatData.find(key);
		if (i == m_StatData.cend())
			continue;
		scoped_lock lk(i->second.lock);
		Json::Value leaf;
		leaf["desc"] = i->second.description;
		setleaf(leaf, i->second);
		root["stats"][i->second.name] = leaf;
	}
	std::unique_lock<std::mutex> lk(m_odm_lock);
	for (const auto &key : {"userplugin", "usercnt_active", "usercnt_contact",
	    "usercnt_equipment", "usercnt_na_user", "usercnt_nonactive",
	    "usercnt_room", "attachment_storage"}) {
		auto i = m_ondemand.find(key);
		if (i == m_ondemand.cend())
			continue;
		Json::Value leaf;
		leaf["desc"] = i->second.desc;
		setleaf(leaf, i->second);
		root["stats"][i->first] = leaf;
	}
	lk.unlock();
	return Json::writeString(Json::StreamWriterBuilder(), std::move(root));
}

void ECStatsCollector::submit(std::string &&url, std::string &&text, bool sslverify)
{
#ifdef HAVE_CURL_CURL_H
	struct slfree {
		void operator()(CURL *s) { curl_easy_cleanup(s); }
		void operator()(curl_slist *s) { curl_slist_free_all(s); }
	};
	std::unique_ptr<CURL, slfree> chp(curl_easy_init());
	std::unique_ptr<curl_slist, slfree> hl(curl_slist_append(nullptr, "Content-Type: application/json"));
	curl_slist_append(hl.get(), "X-Kopano-Stats-Request: 1");
	CURL *ch = chp.get();
	if (!sc_proxy_from_env(ch, url.c_str()))
		sc_proxy_from_sysconfig(ch, url.c_str());
	curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(ch, CURLOPT_TCP_NODELAY, 0L);
	curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, sslverify);
	curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, sslverify);
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
	KC::time_point next_sc, next_sv;
	std::mutex mtx;
	do {
		auto zsc_url = m_config->GetSetting("statsclient_url");
		auto zsc_int = m_config->GetSetting("statsclient_interval");
		auto zsv_url = m_config->GetSetting("surveyclient_url");
		auto zsv_int = m_config->GetSetting("surveyclient_interval");
		auto sc_int = zsc_int != nullptr ? atoui(zsc_int) : 0;
		auto sv_int = zsv_int != nullptr ? atoui(zsv_int) : 0;
		auto now = decltype(next_sc)::clock::now();
		auto do_sc = zsc_url != nullptr && sc_int > 0 && now > next_sc;
		auto do_sv = zsv_url != nullptr && sv_int > 0 && now > next_sv;
		auto next_wk = now + std::chrono::seconds(60); /* basic config reeval interval */

		if (do_sc || do_sv)
			fill_odm();
		if (do_sc) {
			submit(zsc_url, stats_as_text(), parseBool(m_config->GetSetting("statsclient_ssl_verify")));
			next_sc = now + std::chrono::seconds(sc_int);
			next_wk = std::min(next_wk, next_sc);
		}
		if (do_sv) {
			submit(zsv_url, survey_as_text(), parseBool(m_config->GetSetting("surveyclient_ssl_verify")));
			next_sv = now + std::chrono::seconds(sv_int);
			next_wk = std::min(next_wk, next_sv);
		}

		ulock_normal blah(mtx);
		if (m_exitsig.wait_until(blah, next_wk) != std::cv_status::timeout)
			break;
	} while (!terminate);
#endif
}

ECStatsCollector::ECStatsCollector(std::shared_ptr<ECConfig> config) :
	m_config(std::move(config))
{
	AddStat(SCN_MACHINE_ID, SCT_STRING, "machine_id");
	AddStat(SCN_UTSNAME, SCT_STRING, "utsname", "Pretty platform name"); /* not for parsing */
	AddStat(SCN_OSRELEASE, SCT_STRING, "osrelease", "Pretty operating system name"); /* not for parsing either */
	AddStat(SCN_PROGRAM_NAME, SCT_STRING, "program_name", "Program name");
	AddStat(SCN_PROGRAM_VERSION, SCT_STRING, "program_version", "Program version");
	set(SCN_PROGRAM_VERSION, PROJECT_VERSION);
	std::unique_ptr<FILE, file_deleter> fp(fopen("/etc/machine-id", "r"));
	if (fp != nullptr) {
		std::string mid;
		HrMapFileToString(fp.get(), &mid);
		auto pos = mid.find('\n');
		if (pos != std::string::npos)
			mid.erase(pos);
		set(SCN_MACHINE_ID, mid);
	}
	struct utsname uts;
	if (uname(&uts) == 0)
		set(SCN_UTSNAME, uts.sysname + " "s + uts.machine + " " + uts.release);
	set(SCN_OSRELEASE, ec_os_pretty_name());
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
	assert(iSD->second.type == SCT_REAL || iSD->second.type == SCT_REALGAUGE);
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
	assert(iSD->second.type == SCT_INTEGER || iSD->second.type == SCT_INTGAUGE);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ll += inc;
}

void ECStatsCollector::set_dbl(enum SCName name, double set)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCT_REAL || iSD->second.type == SCT_REALGAUGE);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.f = set;
}

void ECStatsCollector::set(enum SCName name, LONGLONG set)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCT_INTEGER || iSD->second.type == SCT_INTGAUGE);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ll = set;
}

void ECStatsCollector::SetTime(enum SCName name, time_t set)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCT_TIME);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ts = set;
}

void ECStatsCollector::set(SCName name, const std::string &s)
{
	auto i = m_StatData.find(name);
	if (i == m_StatData.cend())
		return;
	assert(i->second.type == SCT_STRING);
	scoped_lock lk(i->second.lock);
	i->second.strdata = s;
}

void ECStatsCollector::Max(SCName name, LONGLONG max)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCT_INTEGER || iSD->second.type == SCT_INTGAUGE);
	scoped_lock lk(iSD->second.lock);
	if (iSD->second.data.ll < max)
		iSD->second.data.ll = max;
}

void ECStatsCollector::avg_dbl(SCName name, double add)
{
	auto iSD = m_StatData.find(name);
	if (iSD == m_StatData.cend())
		return;
	assert(iSD->second.type == SCT_REALGAUGE);
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
	assert(iSD->second.type == SCT_INTGAUGE);
	scoped_lock lk(iSD->second.lock);
	iSD->second.data.ll = ((add - iSD->second.data.ll) / iSD->second.avginc) + iSD->second.data.ll;
	++iSD->second.avginc;
	if (iSD->second.avginc == 0)
		iSD->second.avginc = 1;
}

std::string ECStatsCollector::GetValue(const SCMap::const_iterator::value_type &iSD)
{
	switch (iSD.second.type) {
	case SCT_REAL:
	case SCT_REALGAUGE:
		return stringify_double(iSD.second.data.f);
	case SCT_INTEGER:
	case SCT_INTGAUGE:
		return stringify_int64(iSD.second.data.ll);
	case SCT_TIME: {
		if (iSD.second.data.ts <= 0)
			break;
		char timestamp[128] = { 0 };
		struct tm *tm = localtime(&iSD.second.data.ts);
		strftime(timestamp, sizeof timestamp, "%a %b %e %T %Y", tm);
		return timestamp;
	}
	case SCT_STRING:
		return iSD.second.strdata;
	}
	return "";
}

std::string ECStatsCollector::GetValue(const ECStat2 &i)
{
	switch (i.type) {
	case SCT_REAL:
	case SCT_REALGAUGE:
		return stringify_double(i.data.f);
	case SCT_INTEGER:
	case SCT_INTGAUGE:
		return stringify_int64(i.data.ll);
	case SCT_TIME: {
		if (i.data.ts <= 0)
			break;
		char timestamp[128] = { 0 };
		struct tm *tm = localtime(&i.data.ts);
		strftime(timestamp, sizeof(timestamp), "%a %b %e %T %Y", tm);
		return timestamp;
	}
	case SCT_STRING:
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
		assert(i->second.type == SCT_INTEGER);
		i->second.data.ll = v;
		return;
	}
	ECStat2 st{desc, {}, SCT_INTEGER};
	st.data.ll = v;
	m_ondemand.emplace(name, std::move(st));
}

void ECStatsCollector::setg(const std::string &name, const std::string &desc, int64_t v)
{
	scoped_lock lk(m_odm_lock);
	auto i = m_ondemand.find(name);
	if (i != m_ondemand.cend()) {
		assert(i->second.type == SCT_INTGAUGE);
		i->second.data.ll = v;
		return;
	}
	ECStat2 st{desc, {}, SCT_INTGAUGE};
	st.data.ll = v;
	m_ondemand.emplace(name, std::move(st));
}

void ECStatsCollector::setg_dbl(const std::string &name, const std::string &desc, double v)
{
	scoped_lock lk(m_odm_lock);
	auto i = m_ondemand.find(name);
	if (i != m_ondemand.cend()) {
		assert(i->second.type == SCT_REALGAUGE);
		i->second.data.f = v;
		return;
	}
	ECStat2 st{desc, {}, SCT_REALGAUGE};
	st.data.f = v;
	m_ondemand.emplace(name, std::move(st));
}

void ECStatsCollector::set(const std::string &name, const std::string &desc,
    const std::string &v)
{
	scoped_lock lk(m_odm_lock);
	auto i = m_ondemand.find(name);
	if (i == m_ondemand.cend()) {
		m_ondemand.emplace(name, ECStat2{desc, v, SCT_STRING});
		return;
	}
	assert(i->second.type == SCT_STRING);
	i->second.strdata = v;
}

} /* namespace */
