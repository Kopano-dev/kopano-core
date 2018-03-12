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

#include <memory>
#include <mutex>
#include <kopano/platform.h>
#include <kopano/zcdefs.h>
#include "ECLockManager.h"

namespace KC {

ECObjectLock::ECObjectLock(ECLockManagerPtr ptrLockManager,
    unsigned int ulObjId, ECSESSIONID sessionId)
: m_ptrLockManager(ptrLockManager)
, m_ulObjId(ulObjId)
, m_sessionId(sessionId)
{ }

ECObjectLock::ECObjectLock(ECObjectLock &&o) :
	m_ptrLockManager(std::move(o.m_ptrLockManager)),
	m_ulObjId(o.m_ulObjId), m_sessionId(o.m_sessionId)
{
	/*
	 * Our Unlock routine depends on m_ptrLockManager being reset, but due
	 * to LWG DR 2315, weak_ptr(weak_ptr&&) is not implemented in some
	 * compiler versions and thus did not do that reset.
	 */
	o.m_ptrLockManager.reset();
}

ECObjectLock &ECObjectLock::operator=(ECObjectLock &&o)
{
	m_ptrLockManager = std::move(o.m_ptrLockManager);
	o.m_ptrLockManager.reset();
	m_ulObjId = o.m_ulObjId;
	m_sessionId = o.m_sessionId;
	return *this;
}

ECRESULT ECObjectLock::Unlock()
{
	ECRESULT er = erSuccess;

	ECLockManagerPtr ptrLockManager = m_ptrLockManager.lock();
	if (ptrLockManager) {
		er = ptrLockManager->UnlockObject(m_ulObjId, m_sessionId);
		if (er == erSuccess)
			m_ptrLockManager.reset();
	}

	return er;
}

ECLockManagerPtr ECLockManager::Create() {
	return ECLockManagerPtr(new ECLockManager());
}

ECRESULT ECLockManager::LockObject(unsigned int ulObjId, ECSESSIONID sessionId, ECObjectLock *lpObjectLock)
{
	ECRESULT er = erSuccess;
	std::lock_guard<KC::shared_mutex> lock(m_hRwLock);
	auto res = m_mapLocks.emplace(ulObjId, sessionId);
	if (res.second == false && res.first->second != sessionId)
		er = KCERR_NO_ACCESS;

	if (lpObjectLock)
		*lpObjectLock = ECObjectLock(shared_from_this(), ulObjId, sessionId);
	
	return er;
}

ECRESULT ECLockManager::UnlockObject(unsigned int ulObjId, ECSESSIONID sessionId)
{
	std::lock_guard<KC::shared_mutex> lock(m_hRwLock);

	auto i = m_mapLocks.find(ulObjId);
	if (i == m_mapLocks.cend())
		return KCERR_NOT_FOUND;
	else if (i->second != sessionId)
		return KCERR_NO_ACCESS;
	else
		m_mapLocks.erase(i);
	return erSuccess;
}

bool ECLockManager::IsLocked(unsigned int ulObjId, ECSESSIONID *lpSessionId)
{
	KC::shared_lock<KC::shared_mutex> lock(m_hRwLock);
	auto i = m_mapLocks.find(ulObjId);
	if (i != m_mapLocks.cend() && lpSessionId != NULL)
		*lpSessionId = i->second;

	return i != m_mapLocks.end();
}

} /* namespace */
