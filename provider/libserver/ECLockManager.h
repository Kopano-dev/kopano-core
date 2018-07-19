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

#ifndef ECLockManager_INCLUDED
#define ECLockManager_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include <map>
#include <memory>
#include <pthread.h>

namespace KC {

class ECLockManager;

typedef std::shared_ptr<ECLockManager> ECLockManagerPtr;

class ECObjectLock _kc_final {
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
class ECLockManager _kc_final : public std::enable_shared_from_this<ECLockManager> {
public:
	static ECLockManagerPtr Create();
	ECRESULT LockObject(unsigned int ulObjId, ECSESSIONID sessionId, ECObjectLock *lpOjbectLock);
	ECRESULT UnlockObject(unsigned int ulObjId, ECSESSIONID sessionId);
	bool IsLocked(unsigned int ulObjId, ECSESSIONID *lpSessionId);

private:
	ECLockManager(void) = default;
	// Map object ids to session IDs.
	typedef std::map<unsigned int, ECSESSIONID>	LockMap;
	KC::shared_mutex m_hRwLock;
	LockMap				m_mapLocks;
};

} /* namespace */

#endif // ndef ECLockManager_INCLUDED
