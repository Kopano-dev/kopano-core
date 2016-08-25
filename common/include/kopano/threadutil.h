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

#endif //#ifndef ECTHREADUTIL_H
