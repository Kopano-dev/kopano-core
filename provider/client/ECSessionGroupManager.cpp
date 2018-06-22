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
#include <mapicode.h>
#include <mapix.h>

#include "ECNotifyMaster.h"
#include "ECSessionGroupManager.h"
#include "SessionGroupData.h"
#include "SSLUtil.h"

/* Global SessionManager for entire client */
ECSessionGroupManager g_ecSessionManager;

ECSESSIONGROUPID ECSessionGroupManager::GetSessionGroupId(const sGlobalProfileProps &sProfileProps)
{
	ECSESSIONGROUPID ecSessionGroupId;
	scoped_rlock lock(m_hMutex);
	ECSessionGroupInfo ecSessionGroup(sProfileProps.strServerPath, sProfileProps.strProfileName);
	auto result = m_mapSessionGroupIds.emplace(ecSessionGroup, 0);
	if (result.second == true) {
        // Not found, generate one now
    	ssl_random((sizeof(ecSessionGroupId) == 8), &ecSessionGroupId);
		// Register the new SessionGroupId, this is needed because we are not creating a SessionGroupData
		// object yet, and thus we are not putting anything in the m_mapSessionGroups yet. To prevent 2
		// threads to obtain 2 different SessionGroup IDs for the same server & profile combination we
		// use this separate map containing SessionGroup IDs.
		result.first->second = ecSessionGroupId;
	}
	else {
		ecSessionGroupId = result.first->second;
	}
	return ecSessionGroupId;
}

/*
 * Threading comment:
 *
 * This function is safe since we hold the sessiongroup list lock, and we AddRef() the object before releasing the sessiongroup list
 * lock. This means that any Release can run at any time; if a release on the sessiongroup happens during the running of this function,
 * we are simply upping the refcount by 1, possibly leaving the refcount at 1 (the release in the other thread leaving the refcount at 0, does nothing)
 *
 * The object cannot be destroyed during the function since the deletion is done in DeleteSessionGroupDataIfOrphan requires the same lock
 * and that is the only place the object can be deleted.
 */
HRESULT ECSessionGroupManager::GetSessionGroupData(ECSESSIONGROUPID ecSessionGroupId, const sGlobalProfileProps &sProfileProps, SessionGroupData **lppData)
{
	HRESULT hr = hrSuccess;
	ECSessionGroupInfo ecSessionGroup(sProfileProps.strServerPath, sProfileProps.strProfileName);
	SessionGroupData *lpData = NULL;
	scoped_rlock lock(m_hMutex);

	auto result = m_mapSessionGroups.emplace(ecSessionGroup, nullptr);
	if (result.second == true) {
        hr = SessionGroupData::Create(ecSessionGroupId, &ecSessionGroup, sProfileProps, &lpData);
        if (hr == hrSuccess)
			result.first->second = lpData;
		else
			m_mapSessionGroups.erase(result.first);
	} else {
		lpData = result.first->second;
		lpData->AddRef();
	}
	*lppData = lpData;
	return hr;
}

HRESULT ECSessionGroupManager::DeleteSessionGroupDataIfOrphan(ECSESSIONGROUPID ecSessionGroupId)
{
	SessionGroupData *lpSessionGroupData = NULL;
	ulock_rec biglock(m_hMutex);
	auto iter = std::find_if(m_mapSessionGroups.cbegin(), m_mapSessionGroups.cend(),
		[&](const SESSIONGROUPMAP::value_type &e) {
			return e.second->GetSessionGroupId() == ecSessionGroupId;
		});
	if (iter != m_mapSessionGroups.cend()) {
        if(iter->second->IsOrphan()) {
            // If the group is an orphan now, we can delete it safely since the only way
            // a new session would connect to the sessiongroup would be through us, and we 
            // hold the mutex.
            lpSessionGroupData = iter->second;
            m_mapSessionGroups.erase(iter);
        }
    }
	biglock.unlock();
	// Delete the object outside the lock; we can do this because nobody can access this group
	// now (since it is not in the map anymore), and the delete() will cause a pthread_join(), 
	// which could be blocked by the m_hMutex.
	delete lpSessionGroupData;
	return hrSuccess;
}
