/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

// -*- Mode: c++ -*-
#ifndef ECPLUGINFACTORY_H
#define ECPLUGINFACTORY_H

#include <kopano/zcdefs.h>
#include <mutex>
#include <kopano/kcodes.h>
#include "plugin.h"

namespace KC {

class ECConfig;
class ECPluginSharedData;
class ECStatsCollector;

class _kc_export ECPluginFactory _kc_final {
public:
	_kc_hidden ECPluginFactory(ECConfig *, ECStatsCollector *, bool hosted, bool distributed);
	_kc_hidden ~ECPluginFactory(void);
	_kc_hidden ECRESULT CreateUserPlugin(UserPlugin **ret);
	void		SignalPlugins(int signal);

private:
	UserPlugin *(*m_getUserPluginInstance)(std::mutex &, ECPluginSharedData *) = nullptr;
	void (*m_deleteUserPluginInstance)(UserPlugin *) = nullptr;
	ECPluginSharedData *m_shareddata;
	ECConfig *m_config;
	std::mutex m_plugin_lock;
	DLIB m_dl = nullptr;
};

extern ECRESULT GetThreadLocalPlugin(ECPluginFactory *lpPluginFactory, UserPlugin **lppPlugin);

} /* namespace */

#endif
