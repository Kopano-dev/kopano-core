/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSESSIONGROUPMANAGER_H
#define ECSESSIONGROUPMANAGER_H

#include <kopano/zcdefs.h>
#include <algorithm>
#include <list>
#include <map>
#include <mutex>
#include <string>

#include "SessionGroupData.h"
#include "ClientUtil.h"

using namespace KC;
typedef std::map<ECSessionGroupInfo, ECSESSIONGROUPID> SESSIONGROUPIDMAP;
typedef std::map<ECSessionGroupInfo, SessionGroupData*> SESSIONGROUPMAP;

class ECSessionGroupManager _kc_final {
private:
	/*
	 * Both maps must be protected under the same mutx: m_hMutex
	 */
	SESSIONGROUPIDMAP		m_mapSessionGroupIds;
	SESSIONGROUPMAP			m_mapSessionGroups;
	std::recursive_mutex m_hMutex;

public:
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
