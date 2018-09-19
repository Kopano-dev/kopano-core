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

typedef std::map<ECSessionGroupInfo, KC::ECSESSIONGROUPID> SESSIONGROUPIDMAP;
typedef std::map<ECSessionGroupInfo, SessionGroupData*> SESSIONGROUPMAP;

class ECSessionGroupManager final {
private:
	/*
	 * Both maps must be protected under the same mutx: m_hMutex
	 */
	SESSIONGROUPIDMAP		m_mapSessionGroupIds;
	SESSIONGROUPMAP			m_mapSessionGroups;
	std::recursive_mutex m_hMutex;

public:
	/* Gets the session id by connect parameters */
	KC::ECSESSIONGROUPID GetSessionGroupId(const sGlobalProfileProps &);
	/* Gets or creates a session group with the specified ID and connect parameters */
	HRESULT GetSessionGroupData(KC::ECSESSIONGROUPID, const sGlobalProfileProps &, SessionGroupData **out);
	/* Cleanup callback when SessionGroupData object is deleted (should only be called from SessionGroupData::~SessionGroupData() */
	HRESULT DeleteSessionGroupDataIfOrphan(KC::ECSESSIONGROUPID);
};

/* Global SessionManager for entire client */
extern ECSessionGroupManager g_ecSessionManager;

#endif /* ECSESSIONGROUPMANAGER_H */
