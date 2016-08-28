/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef ECPLUGINSHAREDDATA_H
#define ECPLUGINSHAREDDATA_H

#include <kopano/zcdefs.h>
#include <kopano/ECConfig.h>
#include <pthread.h>

class ECStatsCollector;

/**
 * Shared plugin data
 *
 * Each Server thread owns its own UserPlugin object.
 * Each instance of the UserPlugin share the contents
 * of ECPluginSharedData.
 */
class ECPluginSharedData _zcp_final {
private:
	/**
	 * Singleton instance of ECPluginSharedData
	 */
	static ECPluginSharedData *m_lpSingleton;

	/**
	 * Lock for m_lpSingleton access
	 */
	static pthread_mutex_t m_SingletonLock;

	/**
	 * Lock for CreateConfig
	 */
	static pthread_mutex_t m_CreateConfigLock;

	/**
	 * Reference count, used to destroy object when no users are left.
	 */
	unsigned int m_ulRefCount;

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
	ECPluginSharedData(ECConfig *lpParent, ECStatsCollector *, bool bHosted, bool bDistributed);
	virtual ~ECPluginSharedData(void);

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
	static void GetSingleton(ECPluginSharedData **lppSingleton, ECConfig *lpParent, ECStatsCollector *, bool bHosted, bool bDistributed);

	/**
	 * Increase reference count
	 */
	virtual void AddRef();

	/**
	 * Decrease reference count, object might be destroyed before this function returns.
	 */
	virtual void Release();

	/**
	 * Load plugin configuration file
	 *
	 * @param[in]	lpDefaults
	 *					Default values for configuration options.
	 * @param[in]	lpszDirectives
	 *					Supported configuration file directives.
	 * @return The ECConfig pointer. NULL if configuration file could not be loaded.
	 */
	virtual ECConfig *CreateConfig(const configsetting_t *lpDefaults, const char *const *lpszDirectives = lpszDEFAULTDIRECTIVES);

	/**
	 * Obtain the Stats collector
	 *
	 * @return the ECStatsCollector pointer
	 */
	virtual ECStatsCollector *GetStatsCollector(void) const { return m_lpStatsCollector; }

	/**
	 * Check for multi-company support
	 *
	 * @return True if multi-company support is enabled.
	 */
	virtual bool IsHosted(void) const { return m_bHosted; }

	/**
	 * Check for multi-server support
	 * 
	 * @return True if multi-server support is enabled.
	 */
	virtual bool IsDistributed(void) const { return m_bDistributed; }

	/**
	 * Signal handler for userspace signals like SIGHUP
	 *
	 * @param[in]	signal
	 *					The signal ID to be handled
	 */
	virtual void Signal(int signal);

private:
	/**
	 * Plugin configuration file
	 */
	ECConfig *m_lpConfig;

	/**
	 * Server configuration file
	 */
	ECConfig *m_lpParentConfig;

	/**
	 * Statistics collector
	 */
	ECStatsCollector *m_lpStatsCollector;

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
	configsetting_t *m_lpDefaults;

	/**
	 * Copy of plugin directives, stored in the singleton
	 */
	char **m_lpszDirectives;
};

#endif
