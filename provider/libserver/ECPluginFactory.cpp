/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <utility>
#include <cstring>
#include <climits>
#include "ECPluginFactory.h"
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ecversion.h>

namespace KC {

ECPluginFactory::ECPluginFactory(std::shared_ptr<ECConfig> cfg,
    std::shared_ptr<ECStatsCollector> sc, bool bHosted, bool bDistributed) :
	m_config(std::move(cfg)), m_stats(sc)
{
	ECPluginSharedData::GetSingleton(&m_shareddata, m_config, std::move(sc), bHosted, bDistributed);
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
        const char *pluginname = m_config->GetSetting("user_plugin");
        char filename[PATH_MAX + 1];
        if (!pluginname || !strcmp(pluginname, "")) {
			ec_log_crit("No user plugin was declared in the config file.");
			return KCERR_NOT_FOUND;
        }
		if (strcmp(pluginname, "ldapms") == 0)
			pluginname = "ldap";
		snprintf(filename, sizeof(filename), "libkcserver-%s.%s",
		         pluginname, SHARED_OBJECT_EXTENSION);
        m_dl = dlopen(filename, RTLD_NOW | RTLD_GLOBAL);
	if (m_dl == nullptr) {
		snprintf(filename, sizeof(filename), "%s%clibkcserver-%s.%s",
		         PKGLIBDIR, PATH_SEPARATOR, pluginname, SHARED_OBJECT_EXTENSION);
		m_dl = dlopen(filename, RTLD_NOW | RTLD_GLOBAL);
	}
        if (!m_dl) {
			ec_log_crit("Failed to load \"%s\": %s", filename, dlerror());
			ec_log_crit("Please correct your configuration file and set the \"user_plugin\" option.");
			goto out;
        }
	auto sversion = reinterpret_cast<const char *>(dlsym(m_dl, "kcsrv_plugin_version"));
	if (sversion == nullptr) {
		auto fngetUserPluginInstance = reinterpret_cast<unsigned long (*)()>(dlsym(m_dl, "getUserPluginVersion"));
		if (fngetUserPluginInstance == NULL) {
			ec_log_crit("Failed to load getUserPluginVersion from plugin: %s", dlerror());
			goto out;
		}
		auto version = fngetUserPluginInstance();
		if (version != PROJECT_VERSION_REVISION) {
			ec_log_err("%s: Plugin version 0x%lx, but expected 0x%lx",
				filename, version, PROJECT_VERSION_REVISION);
			goto out;
		}
	} else if (strcmp(sversion, PROJECT_VERSION) != 0) {
		ec_log_err("%s: Plugin version \"%s\", but server is \"%s\"",
			filename, sversion, PROJECT_VERSION);
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
		lpPlugin->InitPlugin(m_stats);
	} catch (const std::exception &e) {
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
	auto lpPlugin = static_cast<UserPlugin *>(pthread_getspecific(plugin_key));
	if (lpPlugin == NULL) {
		auto er = lpPluginFactory->CreateUserPlugin(&lpPlugin);
		if (er != erSuccess) {
			lpPlugin = NULL;
			ec_log_crit("Unable to instantiate user plugin");
			return er;
		}
		pthread_setspecific(plugin_key, lpPlugin);
	}
	*lppPlugin = lpPlugin;
	return erSuccess;
}

} /* namespace */
