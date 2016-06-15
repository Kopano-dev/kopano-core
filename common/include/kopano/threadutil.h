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

#ifndef ECTHREADUTIL_H
#define ECTHREADUTIL_H

#include <kopano/zcdefs.h>
#include <pthread.h>

class CPthreadMutex _zcp_final {
public:
	CPthreadMutex(bool bRecurse = false);
	~CPthreadMutex();

	void Lock();
	void Unlock();

private:
	pthread_mutex_t m_hMutex;
};


class scoped_lock _zcp_final {
public:
	scoped_lock(pthread_mutex_t &mutex) : m_mutex(mutex) {
		pthread_mutex_lock(&m_mutex);
	}

	~scoped_lock() { 
		pthread_mutex_unlock(&m_mutex);
	}

private:
	// Make sure an object is not accidentally copied
	scoped_lock(const scoped_lock &) = delete;
	scoped_lock &operator=(const scoped_lock &) = delete;

	pthread_mutex_t &m_mutex;
};

#define WITH_LOCK(_lock)	\
	for (scoped_lock __lock(_lock), *ptr = NULL; ptr == NULL; ptr = &__lock)


template<int(*fnlock)(pthread_rwlock_t*)>
class scoped_rwlock _zcp_final {
public:
	scoped_rwlock(pthread_rwlock_t &rwlock) : m_rwlock(rwlock) {
		fnlock(&m_rwlock);
	}

	~scoped_rwlock() { 
		pthread_rwlock_unlock(&m_rwlock);
	}

private:
	// Make sure an object is not accidentally copied
	scoped_rwlock(const scoped_rwlock &) = delete;
	scoped_rwlock &operator=(const scoped_rwlock &) = delete;

	pthread_rwlock_t &m_rwlock;
};

typedef scoped_rwlock<pthread_rwlock_wrlock> scoped_exclusive_rwlock;
typedef scoped_rwlock<pthread_rwlock_rdlock> scoped_shared_rwlock;

#endif //#ifndef ECTHREADUTIL_H
