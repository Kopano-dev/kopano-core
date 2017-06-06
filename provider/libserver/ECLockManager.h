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
#include <kopano/lockhelper.hpp>

namespace KC {

class ECLockManager;
class ECObjectLockImpl;

typedef std::shared_ptr<ECLockManager> ECLockManagerPtr;

///////////////
// ECObjectLock
///////////////
class ECObjectLock _kc_final {
public:
	ECObjectLock(void) = default;
	ECObjectLock(ECLockManagerPtr ptrLockManager, unsigned int ulObjId, ECSESSIONID sessionId);
	ECObjectLock(const ECObjectLock &other);
	ECObjectLock(ECObjectLock &&);

	ECObjectLock& operator=(const ECObjectLock &other);
	void swap(ECObjectLock &other) noexcept;

	ECRESULT Unlock();

private:
	std::shared_ptr<ECObjectLockImpl> m_ptrImpl;
};

///////////////////////
// ECObjectLock inlines
///////////////////////
inline ECObjectLock::ECObjectLock(const ECObjectLock &other): m_ptrImpl(other.m_ptrImpl) {}

inline ECObjectLock::ECObjectLock(ECObjectLock &&other) : m_ptrImpl(std::move(other.m_ptrImpl)) {}

inline ECObjectLock& ECObjectLock::operator=(const ECObjectLock &other) {
	if (&other != this) {
		ECObjectLock tmp(other);
		swap(tmp);
	}
	return *this;
}

inline void ECObjectLock::swap(ECObjectLock &other) noexcept
{
	m_ptrImpl.swap(other.m_ptrImpl);
}



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
