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

#include <kopano/platform.h>

#include <cstring>
#include <climits>

#include "ECPluginFactory.h"
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ecversion.h>

namespace KC {

ECPluginFactory::ECPluginFactory(ECConfig *config, ECStatsCollector *lpStatsCollector,
    bool bHosted, bool bDistributed) :
	m_config(config)
{
	ECPluginSharedData::GetSingleton(&m_shareddata, m_config, lpStatsCollector, bHosted, bDistributed);
}

ECPluginFactory::~ECPluginFactory() {
#ifndef VALGRIND
	if(m_dl)
		dlclose(m_dl);
#endif
	if (m_shareddata)
		m_shareddata->Release();
}

ECRESULT ECPluginFactory::CreateUserPlugin(UserPlugin **lppPlugin) {
    UserPlugin *lpPlugin = NULL;

    if(m_dl == NULL) {    
        const char *pluginpath = m_config->GetSetting("plugin_path");
        const char *pluginname = m_config->GetSetting("user_plugin");
        char filename[PATH_MAX + 1];

        if (pluginpath == nullptr || strcmp(pluginpath, "") == 0)
            pluginpath = "";
        if (!pluginname || !strcmp(pluginname, "")) {
			ec_log_crit("User plugin is unavailable.");
			ec_log_crit("Please correct your configuration file and set the \"plugin_path\" and \"user_plugin\" options.");
			return KCERR_NOT_FOUND;
        }

        snprintf(filename, PATH_MAX + 1, "%s%c%splugin.%s", 
                 pluginpath, PATH_SEPARATOR, pluginname, SHARED_OBJECT_EXTENSION);
        
        m_dl = dlopen(filename, RTLD_NOW | RTLD_GLOBAL);

        if (!m_dl) {
			ec_log_crit("Failed to load \"%s\": %s", filename, dlerror());
			ec_log_crit("Please correct your configuration file and set the \"plugin_path\" and \"user_plugin\" options.");
			goto out;
        }

        int (*fngetUserPluginInstance)() = (int (*)()) dlsym(m_dl, "getUserPluginVersion");
        if (fngetUserPluginInstance == NULL) {
			ec_log_crit("Failed to load getUserPluginVersion from plugin: %s", dlerror());
			goto out;
        }
        int version = fngetUserPluginInstance(); 
        if (version != PROJECT_VERSION_REVISION) {
			ec_log_crit("Version of the plugin \"%s\" is not the same for the server. Expected %d, plugin %d", filename, PROJECT_VERSION_REVISION, version);
			goto out;
	}
    
        m_getUserPluginInstance = (UserPlugin* (*)(std::mutex &, ECPluginSharedData *)) dlsym(m_dl, "getUserPluginInstance");
        if (m_getUserPluginInstance == NULL) {
			ec_log_crit("Failed to load getUserPluginInstance from plugin: %s", dlerror());
			goto out;
        }

        m_deleteUserPluginInstance = (void (*)(UserPlugin *)) dlsym(m_dl, "deleteUserPluginInstance");
        if (m_deleteUserPluginInstance == NULL) {
			ec_log_crit("Failed to load deleteUserPluginInstance from plugin: %s", dlerror());
			goto out;
        }
    }
	try {
		lpPlugin = m_getUserPluginInstance(m_plugin_lock, m_shareddata);
		lpPlugin->InitPlugin();
	} catch (std::exception &e) {
		ec_log_crit("Cannot instantiate user plugin: %s", e.what());
		return KCERR_NOT_FOUND;
	}
	
	*lppPlugin = lpPlugin;

	return erSuccess;

 out:
	if (m_dl)
		dlclose(m_dl);
	m_dl = NULL;
	return KCERR_NOT_FOUND;
}

void ECPluginFactory::SignalPlugins(int signal)
{
	m_shareddata->Signal(signal);
}

// Returns a plugin local to this thread. Works the same as GetThreadLocalDatabase
extern pthread_key_t plugin_key;
ECRESULT GetThreadLocalPlugin(ECPluginFactory *lpPluginFactory,
    UserPlugin **lppPlugin)
{
	ECRESULT er;
	UserPlugin *lpPlugin = NULL;

	lpPlugin = (UserPlugin *)pthread_getspecific(plugin_key);

	if (lpPlugin == NULL) {
		er = lpPluginFactory->CreateUserPlugin(&lpPlugin);

		if (er != erSuccess) {
			lpPlugin = NULL;
			ec_log_crit("Unable to instantiate user plugin");
			return er;
		}

		pthread_setspecific(plugin_key, (void *)lpPlugin);
	}

	*lppPlugin = lpPlugin;
	return erSuccess;
}

} /* namespace */
