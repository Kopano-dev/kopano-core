/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSCHEDULER_H
#define ECSCHEDULER_H

#include <kopano/zcdefs.h>
#include <condition_variable>
#include <mutex>
#include <kopano/memory.hpp>
#include <pthread.h>
#include <list>

namespace KC {

enum eSchedulerType{
	SCHEDULE_NONE,
	SCHEDULE_SECONDS,
	SCHEDULE_MINUTES,
	SCHEDULE_HOUR,
	SCHEDULE_DAY,
	SCHEDULE_MONTH
};

struct ECSCHEDULE {
	eSchedulerType	eType;
	unsigned int	ulBeginCycle;
	time_t			tLastRunTime;
	void*			(*lpFunction)(void*);
	void*			lpData;
};

typedef std::list<ECSCHEDULE> ECScheduleList;

class _kc_export ECScheduler _kc_final {
public:
	ECScheduler();
	~ECScheduler(void);

	HRESULT AddSchedule(eSchedulerType eType, unsigned int ulBeginCycle, void* (*lpFunction)(void*), void* lpData = NULL);

private:
	_kc_hidden static bool hasExpired(time_t ttime, ECSCHEDULE *);
	_kc_hidden static void *ScheduleThread(void *tmp_scheduler);

	ECScheduleList		m_listScheduler;
	bool m_thread_active = false, m_bExit = false;
	std::mutex m_hExitMutex; /* Mutex needed for the release signal */
	std::condition_variable m_hExitSignal; /* Signal that should be sent to the Scheduler when to exit */
	std::recursive_mutex m_hSchedulerMutex; /* Mutex for the locking of the scheduler */
	pthread_t			m_hMainThread;			// Thread that is used for the Scheduler
};

} /* namespace */

#endif
