/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include <map>
#include <memory>
#include <pthread.h>

namespace KC {

class ECLockManager;

class ECObjectLock final {
public:
	ECObjectLock() = default;
	ECObjectLock(std::shared_ptr<ECLockManager>, unsigned int obj_id, ECSESSIONID);
	ECObjectLock(ECObjectLock &&);
	~ECObjectLock() { Unlock(); }
	ECObjectLock &operator=(ECObjectLock &&);
	ECRESULT Unlock();

private:
	std::weak_ptr<ECLockManager> m_ptrLockManager;
	unsigned int m_ulObjId = 0;
	ECSESSIONID m_sessionId = 0;
};

////////////////
// ECLockManager
////////////////
class ECLockManager final : public std::enable_shared_from_this<ECLockManager> {
public:
	ECRESULT LockObject(unsigned int ulObjId, ECSESSIONID sessionId, ECObjectLock *lpOjbectLock);
	ECRESULT UnlockObject(unsigned int ulObjId, ECSESSIONID sessionId);
	bool IsLocked(unsigned int ulObjId, ECSESSIONID *lpSessionId);

private:
	// Map object ids to session IDs.
	typedef std::map<unsigned int, ECSESSIONID>	LockMap;
	KC::shared_mutex m_hRwLock;
	LockMap				m_mapLocks;
};

} /* namespace */
