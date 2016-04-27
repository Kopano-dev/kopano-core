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
#include <pthread.h>
#include <boost/smart_ptr.hpp>

class ECLockManager;
class ECObjectLockImpl;

typedef boost::shared_ptr<ECLockManager> ECLockManagerPtr;

///////////////
// ECObjectLock
///////////////
class ECObjectLock _zcp_final {
public:
	ECObjectLock();
	ECObjectLock(ECLockManagerPtr ptrLockManager, unsigned int ulObjId, ECSESSIONID sessionId);
	ECObjectLock(const ECObjectLock &other);

	ECObjectLock& operator=(const ECObjectLock &other);
	void swap(ECObjectLock &other);

	ECRESULT Unlock();

private:
	typedef boost::shared_ptr<ECObjectLockImpl> ImplPtr;
	ImplPtr	m_ptrImpl;
};

///////////////////////
// ECObjectLock inlines
///////////////////////
inline ECObjectLock::ECObjectLock() {}

inline ECObjectLock::ECObjectLock(const ECObjectLock &other): m_ptrImpl(other.m_ptrImpl) {}

inline ECObjectLock& ECObjectLock::operator=(const ECObjectLock &other) {
	if (&other != this) {
		ECObjectLock tmp(other);
		swap(tmp);
	}
	return *this;
}

inline void ECObjectLock::swap(ECObjectLock &other) {
	m_ptrImpl.swap(other.m_ptrImpl);
}



////////////////
// ECLockManager
////////////////
class ECLockManager _zcp_final : public boost::enable_shared_from_this<ECLockManager> {
public:
	static ECLockManagerPtr Create();
	~ECLockManager();

	ECRESULT LockObject(unsigned int ulObjId, ECSESSIONID sessionId, ECObjectLock *lpOjbectLock);
	ECRESULT UnlockObject(unsigned int ulObjId, ECSESSIONID sessionId);
	bool IsLocked(unsigned int ulObjId, ECSESSIONID *lpSessionId);

private:
	ECLockManager();

private:
	// Map object ids to session IDs.
	typedef std::map<unsigned int, ECSESSIONID>	LockMap;

	pthread_rwlock_t	m_hRwLock;
	LockMap				m_mapLocks;
};

#endif // ndef ECLockManager_INCLUDED
