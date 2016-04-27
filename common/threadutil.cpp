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
#include <kopano/threadutil.h>

CPthreadMutex::CPthreadMutex(bool bRecurse) {
	pthread_mutexattr_t a;
	pthread_mutexattr_init(&a);
	if (bRecurse)
		pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_hMutex, &a);
	pthread_mutexattr_destroy(&a);
}

CPthreadMutex::~CPthreadMutex() {
	pthread_mutex_destroy(&m_hMutex);
}

void CPthreadMutex::Lock()
{
	pthread_mutex_lock(&m_hMutex);
}

void CPthreadMutex::Unlock()
{
	pthread_mutex_unlock(&m_hMutex);
}

