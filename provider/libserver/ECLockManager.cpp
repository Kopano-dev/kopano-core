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
#include <kopano/platform.h>
#include "ECLockManager.h"
#include <kopano/threadutil.h>

using namespace std;

class ECObjectLockImpl {
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
	ECRESULT er = erSuccess;

	er = m_ptrImpl->Unlock();
	if (er == erSuccess)
		m_ptrImpl.reset();

	return er;
}

ECLockManagerPtr ECLockManager::Create() {
	return ECLockManagerPtr(new ECLockManager());
}

ECLockManager::ECLockManager() {
	pthread_rwlock_init(&m_hRwLock, NULL);
}

ECLockManager::~ECLockManager() {
	pthread_rwlock_destroy(&m_hRwLock);
}

ECRESULT ECLockManager::LockObject(unsigned int ulObjId, ECSESSIONID sessionId, ECObjectLock *lpObjectLock)
{
	ECRESULT er = erSuccess;
	pair<LockMap::const_iterator, bool> res;
	scoped_exclusive_rwlock lock(m_hRwLock);

	res = m_mapLocks.insert(LockMap::value_type(ulObjId, sessionId));
	if (res.second == false && res.first->second != sessionId)
		er = KCERR_NO_ACCESS;

	if (lpObjectLock)
		*lpObjectLock = ECObjectLock(shared_from_this(), ulObjId, sessionId);
	
	return er;
}

ECRESULT ECLockManager::UnlockObject(unsigned int ulObjId, ECSESSIONID sessionId)
{
	ECRESULT er = erSuccess;
	LockMap::iterator i;
	scoped_exclusive_rwlock lock(m_hRwLock);

	i = m_mapLocks.find(ulObjId);
	if (i == m_mapLocks.end())
		er = KCERR_NOT_FOUND;
	else if (i->second != sessionId)
		er = KCERR_NO_ACCESS;
	else
		m_mapLocks.erase(i);

	return er;
}

bool ECLockManager::IsLocked(unsigned int ulObjId, ECSESSIONID *lpSessionId)
{
	LockMap::const_iterator i;
	
	scoped_shared_rwlock lock(m_hRwLock);
	i = m_mapLocks.find(ulObjId);
	if (i != m_mapLocks.end() && lpSessionId)
		*lpSessionId = i->second;

	return i != m_mapLocks.end();
}
