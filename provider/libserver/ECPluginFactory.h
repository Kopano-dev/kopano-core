/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
// -*- Mode: c++ -*-
#pragma once
#include <kopano/zcdefs.h>
#include <memory>
#include <mutex>
#include <kopano/kcodes.h>
#include "plugin.h"

namespace KC {

class Config;
class ECPluginSharedData;
class ECStatsCollector;

class KC_EXPORT ECPluginFactory final {
public:
	KC_HIDDEN ECPluginFactory(std::shared_ptr<Config>, std::shared_ptr<ECStatsCollector>, bool hosted, bool distributed);
	KC_HIDDEN ~ECPluginFactory();
	KC_HIDDEN ECRESULT CreateUserPlugin(UserPlugin **ret);
	void		SignalPlugins(int signal);

private:
	UserPlugin *(*m_getUserPluginInstance)(std::mutex &, ECPluginSharedData *) = nullptr;
	void (*m_deleteUserPluginInstance)(UserPlugin *) = nullptr;
	ECPluginSharedData *m_shareddata;
	std::shared_ptr<Config> m_config;
	std::shared_ptr<ECStatsCollector> m_stats;
	std::mutex m_plugin_lock;
	DLIB m_dl = nullptr;
};

extern ECRESULT GetThreadLocalPlugin(ECPluginFactory *lpPluginFactory, UserPlugin **lppPlugin);

} /* namespace */
