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

#ifndef PTHREADUTIL_H
#define PTHREADUTIL_H

#include <sys/time.h>

inline int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, int millis)
{
    struct timespec ts;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + millis/1000;
    ts.tv_nsec = tv.tv_usec * 1000 + (millis % 1000)*1000;

    // Normalize nsec
    ts.tv_sec += ts.tv_nsec / 1000000000;
    ts.tv_nsec = ts.tv_nsec % 1000000000;

    if(millis)
        return pthread_cond_timedwait(cond, mutex, &ts);
    else
        return pthread_cond_wait(cond, mutex);
}

#endif
