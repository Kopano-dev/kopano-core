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
#include <kopano/lockhelper.hpp>

using namespace std;

namespace KC {

class ECObjectLockImpl _kc_final {
public:
	ECObjectLockImpl(ECLockManagerPtr ptrLockManager, unsigned int ulObjId, ECSESSIONID sessionId);
	ECObjectLockImpl(const ECObjectLockImpl &) = delete;
	~ECObjectLockImpl();
	void operator=(const ECObjectLockImpl &) = delete;
	ECRESULT Unlock();

private:
	std::weak_ptr<ECLockManager> m_ptrLockManager;
	unsigned int m_ulObjId;
	ECSESSIONID m_sessionId;
};

ECObjectLockImpl::ECObjectLockImpl(ECLockManagerPtr ptrLockManager, unsigned int ulObjId, ECSESSIONID sessionId)
: m_ptrLockManager(ptrLockManager)
, m_ulObjId(ulObjId)
, m_sessionId(sessionId)
{ }

ECObjectLockImpl::~ECObjectLockImpl() {
	Unlock();
}

ECRESULT ECObjectLockImpl::Unlock() {
	ECRESULT er = erSuccess;

	ECLockManagerPtr ptrLockManager = m_ptrLockManager.lock();
	if (ptrLockManager) {
		er = ptrLockManager->UnlockObject(m_ulObjId, m_sessionId);
		if (er == erSuccess)
			m_ptrLockManager.reset();
	}

	return er;
}

ECObjectLock::ECObjectLock(ECLockManagerPtr ptrLockManager, unsigned int ulObjId, ECSESSIONID sessionId)
: m_ptrImpl(new ECObjectLockImpl(ptrLockManager, ulObjId, sessionId))
{ }

ECRESULT ECObjectLock::Unlock() {
	auto er = m_ptrImpl->Unlock();
	if (er == erSuccess)
		m_ptrImpl.reset();

	return er;
}

ECLockManagerPtr ECLockManager::Create() {
	return ECLockManagerPtr(new ECLockManager());
}

ECRESULT ECLockManager::LockObject(unsigned int ulObjId, ECSESSIONID sessionId, ECObjectLock *lpObjectLock)
{
	ECRESULT er = erSuccess;
	std::lock_guard<KC::shared_mutex> lock(m_hRwLock);

	auto res = m_mapLocks.insert({ulObjId, sessionId});
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
