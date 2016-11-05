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

#ifndef ECSCHEDULER_H
#define ECSCHEDULER_H

#include <kopano/zcdefs.h>
#include <condition_variable>
#include <mutex>
#include <kopano/ECLogger.h>

#include <pthread.h>
#include <list>

enum eSchedulerType{
	SCHEDULE_NONE,
	SCHEDULE_SECONDS,
	SCHEDULE_MINUTES,
	SCHEDULE_HOUR,
	SCHEDULE_DAY,
	SCHEDULE_MONTH
};

typedef struct tagSchedule {
	eSchedulerType	eType;
	unsigned int	ulBeginCycle;
	time_t			tLastRunTime;
	void*			(*lpFunction)(void*);
	void*			lpData;
} ECSCHEDULE;

typedef std::list<ECSCHEDULE> ECScheduleList;

class _kc_export ECScheduler _kc_final {
public:
	ECScheduler(ECLogger *lpLogger);
	~ECScheduler(void);

	HRESULT AddSchedule(eSchedulerType eType, unsigned int ulBeginCycle, void* (*lpFunction)(void*), void* lpData = NULL);

private:
	_kc_hidden static bool hasExpired(time_t ttime, ECSCHEDULE *);
	_kc_hidden static void *ScheduleThread(void *tmp_scheduler);

private:
	ECScheduleList		m_listScheduler;
	ECLogger *			m_lpLogger;

	bool				m_bExit;
	std::mutex m_hExitMutex; /* Mutex needed for the release signal */
	std::condition_variable m_hExitSignal; /* Signal that should be send to the Scheduler when to exit */
	std::recursive_mutex m_hSchedulerMutex; /* Mutex for the locking of the scheduler */
	pthread_t			m_hMainThread;			// Thread that is used for the Scheduler
};

#endif
