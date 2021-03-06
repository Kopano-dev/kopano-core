/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <kopano/ECConfig.h>

namespace KC {

class ECStatsCollector;

/**
 * Shared plugin data
 *
 * Each Server thread owns its own UserPlugin object.
 * Each instance of the UserPlugin share the contents
 * of ECPluginSharedData.
 */
class KC_EXPORT ECPluginSharedData final {
private:
	/**
	 * Singleton instance of ECPluginSharedData
	 */
	KC_HIDDEN static ECPluginSharedData *m_lpSingleton;

	/**
	 * Lock for m_lpSingleton access
	 */
	KC_HIDDEN static std::mutex m_SingletonLock;

	/**
	 * Lock for CreateConfig
	 */
	KC_HIDDEN static std::mutex m_CreateConfigLock;

	/**
	 * Reference count, used to destroy object when no users are left.
	 */
	std::atomic<unsigned int> m_ulRefCount{0};

	/**
	 * @param[in]	lpParent
	 *					Pointer to ECConfig to read configuration option from the server
	 * @param[in]	lpStatsCollector
	 *					Pointer to ECStatsCollector to collect statistics about
	 *					plugin specific tasks (i.e. the number of SQL or LDAP queries)
	 * @param[in]   bHosted
	 *					Boolean to indicate if multi-company support should be enabled.
	 *					Plugins are allowed to throw an exception when bHosted is true
	 *					while the plugin doesn't support multi-company.
	 * @param[in]	bDistributed
	 *					Boolean to indicate if multi-server support should be enabled.
	 * 					Plugins are allowed to throw an exception when bDistributed is true
	 *					while the plugin doesn't support multi-server.
	 */
	KC_HIDDEN ECPluginSharedData(std::shared_ptr<ECConfig> parent, std::shared_ptr<ECStatsCollector>, bool hosted, bool distributed);
	KC_HIDDEN virtual ~ECPluginSharedData();

public:
	/**
	 * Obtain singleton pointer to ECPluginSharedData.
	 *
	 * @param[out]	lppSingleton
	 *					The singleton ECPluginSharedData pointer
	 * @param[in]	lpParent
	 *					Server configuration file
	 * @param[in]	lpStatsCollector
	 *					Statistics collector
	 * @param[in]   bHosted
	 *					Boolean to indicate if multi-company support should be enabled.
	 *					Plugins are allowed to throw an exception when bHosted is true
	 *					while the plugin doesn't support multi-company.
	 * @param[in]	bDistributed
	 *					Boolean to indicate if multi-server support should be enabled.
	 * 					Plugins are allowed to throw an exception when bDistributed is true
	 *					while the plugin doesn't support multi-server.
	 */
	KC_HIDDEN static void GetSingleton(ECPluginSharedData **out, std::shared_ptr<ECConfig> parent, std::shared_ptr<ECStatsCollector>, bool hosted, bool distributed);

	/**
	 * Increase reference count
	 */
	KC_HIDDEN virtual void AddRef();

	/**
	 * Decrease reference count, object might be destroyed before this function returns.
	 */
	KC_HIDDEN virtual void Release();

	/**
	 * Load plugin configuration file
	 *
	 * @param[in]	lpDefaults
	 *					Default values for configuration options.
	 * @param[in]	lpszDirectives
	 *					Supported configuration file directives.
	 * @return The ECConfig pointer. NULL if configuration file could not be loaded.
	 */
	virtual ECConfig *CreateConfig(const configsetting_t *dfl, const char *const *directives = lpszDEFAULTDIRECTIVES);

	/**
	 * Obtain the Stats collector
	 *
	 * @return the ECStatsCollector pointer
	 */
	KC_HIDDEN virtual std::shared_ptr<ECStatsCollector> GetStatsCollector() const { return m_lpStatsCollector; }

	/**
	 * Check for multi-company support
	 *
	 * @return True if multi-company support is enabled.
	 */
	KC_HIDDEN virtual bool IsHosted() const { return m_bHosted; }

	/**
	 * Check for multi-server support
	 *
	 * @return True if multi-server support is enabled.
	 */
	KC_HIDDEN virtual bool IsDistributed() const { return m_bDistributed; }

	/**
	 * Signal handler for userspace signals like SIGHUP
	 *
	 * @param[in]	signal
	 *					The signal ID to be handled
	 */
	KC_HIDDEN virtual void Signal(int s);

private:
	/**
	 * Plugin configuration file
	 */
	ECConfig *m_lpConfig = nullptr;

	/**
	 * Server configuration file
	 */
	std::shared_ptr<ECConfig> m_lpParentConfig;

	/**
	 * Statistics collector
	 */
	std::shared_ptr<ECStatsCollector> m_lpStatsCollector;

	/**
	 * True if multi-company support is enabled.
	 */
	bool m_bHosted;

	/**
	 * True if multi-server support is enabled.
	 */
	bool m_bDistributed;

	/**
	 * Copy of plugin defaults, stored in the singleton
	 */
	configsetting_t *m_lpDefaults = nullptr;

	/**
	 * Copy of plugin directives, stored in the singleton
	 */
	char **m_lpszDirectives = nullptr;
};

} /* namespace */
