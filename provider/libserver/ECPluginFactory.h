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

// -*- Mode: c++ -*-
#ifndef ECPLUGINFACTORY_H
#define ECPLUGINFACTORY_H

#include <kopano/zcdefs.h>
#include <mutex>
#include <kopano/kcodes.h>
#include "plugin.h"

class ECConfig;
class ECPluginSharedData;
class ECStatsCollector;

class ECPluginFactory _zcp_final {
public:
	ECPluginFactory(ECConfig *config, ECStatsCollector *lpStatsCollector, bool bHosted, bool bDistributed);
	~ECPluginFactory();

	ECRESULT	CreateUserPlugin(UserPlugin **lppPlugin);
	void		SignalPlugins(int signal);

private:
	UserPlugin* (*m_getUserPluginInstance)(std::mutex &, ECPluginSharedData*);
	void (*m_deleteUserPluginInstance)(UserPlugin*);

	ECPluginSharedData *m_shareddata;
	ECConfig *m_config;
	std::mutex m_plugin_lock;
	DLIB m_dl;
};

extern ECRESULT GetThreadLocalPlugin(ECPluginFactory *lpPluginFactory, UserPlugin **lppPlugin);
#endif
