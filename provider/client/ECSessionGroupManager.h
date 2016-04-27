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

#ifndef ECSESSIONGROUPMANAGER_H
#define ECSESSIONGROUPMANAGER_H

#include <algorithm>
#include <list>
#include <map>
#include <pthread.h>
#include <string>

#include "SessionGroupData.h"
#include "ClientUtil.h"

typedef std::map<ECSessionGroupInfo, ECSESSIONGROUPID> SESSIONGROUPIDMAP;
typedef std::map<ECSessionGroupInfo, SessionGroupData*> SESSIONGROUPMAP;

class ECSessionGroupManager
{
private:
	/*
	 * Both maps must be protected under the same mutx: m_hMutex
	 */
	SESSIONGROUPIDMAP		m_mapSessionGroupIds;
	SESSIONGROUPMAP			m_mapSessionGroups;

	pthread_mutex_t			m_hMutex;
	pthread_mutexattr_t		m_hMutexAttrib;

public:
	ECSessionGroupManager(void);
	~ECSessionGroupManager(void);

	/* Gets the session id by connect parameters */
	ECSESSIONGROUPID GetSessionGroupId(const sGlobalProfileProps &sProfileProps);

	/* Gets or creates a session group with the specified ID and connect parameters */
	HRESULT GetSessionGroupData(ECSESSIONGROUPID ecSessionGroupId, const sGlobalProfileProps &sProfileProps, SessionGroupData **lppData);

	/* Cleanup callback when SessionGroupData object is deleted (should only be called from SessionGroupData::~SessionGroupData() */
	HRESULT DeleteSessionGroupDataIfOrphan(ECSESSIONGROUPID ecSessionGroupId);
};

/* Global SessionManager for entire client */
extern ECSessionGroupManager g_ecSessionManager;

#endif /* ECSESSIONGROUPMANAGER_H */
