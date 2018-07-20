/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/ECLogger.h>
#include <kopano/ECPluginSharedData.h>

namespace KC {

ECPluginSharedData *ECPluginSharedData::m_lpSingleton = NULL;
std::mutex ECPluginSharedData::m_SingletonLock;
std::mutex ECPluginSharedData::m_CreateConfigLock;

ECPluginSharedData::ECPluginSharedData(ECConfig *lpParent,
    ECStatsCollector *lpStatsCollector, bool bHosted, bool bDistributed) :
	m_lpParentConfig(lpParent), m_lpStatsCollector(lpStatsCollector),
	m_bHosted(bHosted), m_bDistributed(bDistributed)
{
}

ECPluginSharedData::~ECPluginSharedData()
{
	delete m_lpConfig;
	if (m_lpDefaults) {
		for (int n = 0; m_lpDefaults[n].szName; ++n) {
			free(const_cast<char *>(m_lpDefaults[n].szName));
			free(const_cast<char *>(m_lpDefaults[n].szValue));
		}
		delete [] m_lpDefaults;
	}
	if (m_lpszDirectives) {
		for (int n = 0; m_lpszDirectives[n]; ++n)
			free(m_lpszDirectives[n]);
		delete [] m_lpszDirectives;
	}
}

void ECPluginSharedData::GetSingleton(ECPluginSharedData **lppSingleton,
    ECConfig *lpParent, ECStatsCollector *lpStatsCollector, bool bHosted,
    bool bDistributed)
{
	scoped_lock lock(m_SingletonLock);

	if (!m_lpSingleton)
		m_lpSingleton = new ECPluginSharedData(lpParent, lpStatsCollector, bHosted, bDistributed);
	++m_lpSingleton->m_ulRefCount;
	*lppSingleton = m_lpSingleton;
}

void ECPluginSharedData::AddRef()
{
	scoped_lock lock(m_SingletonLock);
	++m_ulRefCount;
}

void ECPluginSharedData::Release()
{
	scoped_lock lock(m_SingletonLock);
	if (!--m_ulRefCount) {
		delete m_lpSingleton;
		m_lpSingleton = NULL;
	}
}

ECConfig *ECPluginSharedData::CreateConfig(const configsetting_t *lpDefaults,
    const char *const *lpszDirectives)
{
	scoped_lock lock(m_CreateConfigLock);

	if (m_lpConfig != nullptr)
		return m_lpConfig;

	int n;
	/*
	 * Store all the defaults and directives in the singleton,
	 * so it isn't removed from memory when the plugin unloads.
	 */
	if (lpDefaults) {
		for (n = 0; lpDefaults[n].szName; ++n)
			;
		m_lpDefaults = new configsetting_t[n+1];
		for (n = 0; lpDefaults[n].szName; ++n) {
			m_lpDefaults[n].szName = strdup(lpDefaults[n].szName);
			m_lpDefaults[n].szValue = strdup(lpDefaults[n].szValue);
			m_lpDefaults[n].cs_flags = lpDefaults[n].cs_flags;
			m_lpDefaults[n].ulGroup = lpDefaults[n].ulGroup;
		}
		m_lpDefaults[n].szName = NULL;
		m_lpDefaults[n].szValue = NULL;
	}

	if (lpszDirectives) {
		for (n = 0; lpszDirectives[n]; ++n)
			;
		m_lpszDirectives = new char*[n+1];
		for (n = 0; lpszDirectives[n]; ++n)
			m_lpszDirectives[n] = strdup(lpszDirectives[n]);
		m_lpszDirectives[n] = NULL;
	}

	m_lpConfig = ECConfig::Create(m_lpDefaults, m_lpszDirectives);
	if (!m_lpConfig->LoadSettings(m_lpParentConfig->GetSetting("user_plugin_config")))
		ec_log_err("Failed to open plugin configuration file, using defaults.");
	if (m_lpConfig->HasErrors() || m_lpConfig->HasWarnings()) {
		LogConfigErrors(m_lpConfig);
		if (m_lpConfig->HasErrors()) {
			delete m_lpConfig;
			m_lpConfig = NULL;
		}
	}
	return m_lpConfig;
}

void ECPluginSharedData::Signal(int signal)
{
	if (!m_lpConfig)
		return;

	switch (signal) {
	case SIGHUP:
		if (!m_lpConfig->ReloadSettings())
			ec_log_crit("Unable to reload plugin configuration file, continuing with current settings.");
			
		if (m_lpConfig->HasErrors()) {
			ec_log_err("Unable to reload plugin configuration file.");
			LogConfigErrors(m_lpConfig);
		}
		break;
	default:
		break;
	}
}

} /* namespace */
