/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#define _GNU_SOURCE 1 /* pthread_setname_np */
#include <chrono>
#include <condition_variable>
#include <string>
#include <utility>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#ifdef LINUX
#	include <sys/syscall.h>
#endif
#include <kopano/platform.h>
#include <kopano/ECLogger.h>
#include <kopano/ECScheduler.h>
#include <kopano/ECThreadPool.h>
#include <algorithm>
#define SCHEDULER_POLL_FREQUENCY	5

namespace KC {

/**
 * Construct an ECThreadPool instance.
 * @param[in]	ulThreadCount	The amount of worker hreads to create.
 */
ECThreadPool::ECThreadPool(const std::string &pname, unsigned int ulThreadCount) :
	m_poolname(pname)
{
	setThreadCount(ulThreadCount);
}

/**
 * Destruct an ECThreadPool instance. This blocks until all worker
 * threads have exited.
 */
ECThreadPool::~ECThreadPool()
{
	setThreadCount(0, true);
}

/**
 * Queue a task object on the threadpool instance.
 * @param[in]	lpTask			The task object to queue.
 * @param[in]	bTakeOwnership	Boolean parameter specifying whether the threadpool
 *                              should take ownership of the task object, and thus
 *                              is responsible for deleting the object when done.
 * @returns true if the task was successfully queued, false otherwise.
 */
bool ECThreadPool::enqueue(ECTask *lpTask, bool bTakeOwnership)
{
	STaskInfo sTaskInfo = {lpTask, decltype(STaskInfo::enq_stamp)::clock::now(), bTakeOwnership};
	ulock_normal locker(m_hMutex);
	m_listTasks.emplace_back(std::move(sTaskInfo));
	m_hCondition.notify_one();
	joinTerminated(locker);
	return true;
}

time_duration ECThreadPool::front_item_age() const
{
	auto now = std::chrono::steady_clock::now();
	scoped_lock lock(m_hMutex);
	return m_listTasks.empty() ? time_duration(0) : now - m_listTasks.front().enq_stamp;
}

size_t ECThreadPool::queue_length() const
{
	scoped_lock lk(m_hMutex);
	return m_listTasks.size();
}

void ECThreadPool::thread_counts(size_t *active, size_t *idle) const
{
	scoped_lock lk(m_hMutex);
	*active = m_active;
	*idle   = m_setThreads.size() - *active;
}

size_t ECThreadPool::threadCount() const
{
	return m_setThreads.size() - m_ulTermReq;
}

/**
 * Set the amount of worker threads for the threadpool.
 * @param[in]	ulThreadCount	The amount of required worker threads.
 * @param[in]	bWait			If the requested amount of worker threads is less
 *                              than the current amount, this method will wait until
 *                              the extra threads have exited if this argument is true.
 */
void ECThreadPool::setThreadCount(unsigned ulThreadCount, bool bWait)
{
	ulock_normal locker(m_hMutex);

	if (ulThreadCount == threadCount() - 1) {
		++m_ulTermReq;
		m_hCondition.notify_one();
	}
	else if (ulThreadCount < threadCount()) {
		m_ulTermReq += (threadCount() - ulThreadCount);
		m_hCondition.notify_all();
	}
	else {
		unsigned ulThreadsToAdd = ulThreadCount - threadCount();

		if (ulThreadsToAdd <= m_ulTermReq)
			m_ulTermReq -= ulThreadsToAdd;
		else {
			ulThreadsToAdd -= m_ulTermReq;
			m_ulTermReq = 0;

			for (unsigned i = 0; i < ulThreadsToAdd; ++i) {
				auto wk = make_worker();
				pthread_t hThread;

				auto ret = pthread_create(&hThread, nullptr, &threadFunc, wk.get());
				if (ret != 0) {
					ec_log_err("Could not create ECThreadPool worker thread: %s", strerror(ret));
					/* If there were no resources, stop trying. */
					break;
				}
				m_setThreads.emplace(hThread, std::move(wk));
			}
		}
	}

	while (bWait && m_setThreads.size() > ulThreadCount) {
		m_hCondTerminated.wait(locker);
		joinTerminated(locker);
	}

	assert(threadCount() == ulThreadCount);
	joinTerminated(locker);
}

/**
 * Get the next task from the queue (or terminate thread).
 * This method normally pops the next task object from the queue. However when
 * the number of worker threads needs to be decreased this method will remove the
 * calling thread from the set of available worker threads and return false to
 * inform the caller that it should exit.
 *
 * @param[out]	lpsTaskInfo		A STaskInfo struct containing the task to be executed.
 * @retval	true	The next task was successfully obtained.
 * @retval	false	The thread was requested to exit.
 */
bool ECThreadPool::getNextTask(STaskInfo *lpsTaskInfo, ulock_normal &locker)
{
	assert(locker.owns_lock());
	assert(lpsTaskInfo != NULL);
	bool bTerminate = false;
	while (!(bTerminate = m_ulTermReq > 0) && m_listTasks.empty())
		m_hCondition.wait(locker);

	if (bTerminate) {
		pthread_t self = pthread_self();
		auto iThread = std::find_if(m_setThreads.cbegin(), m_setThreads.cend(),
			[=](const auto &pair) { return pthread_equal(pair.first, self) != 0; });
		assert(iThread != m_setThreads.cend());
		m_setTerminated.emplace(*iThread);
		m_setThreads.erase(iThread);
		--m_ulTermReq;
		m_hCondTerminated.notify_one();
		return false;
	}

	*lpsTaskInfo = m_listTasks.front();
	m_listTasks.pop_front();
	return true;
}

/**
 * Call pthread_join on all terminated threads for cleanup.
 */
void ECThreadPool::joinTerminated(ulock_normal &locker)
{
	assert(locker.owns_lock());
	for (const auto &pair : m_setTerminated)
		pthread_join(pair.first, nullptr);
	m_setTerminated.clear();
}

ECThreadWorker::ECThreadWorker(ECThreadPool *p) :
	m_pool(p)
{}

std::unique_ptr<ECThreadWorker> ECThreadPool::make_worker()
{
	return std::make_unique<ECThreadWorker>(this);
}

/**
 * The main loop of the worker threads.
 * @param[in]	lpVoid	Pointer to the owning ECThreadPool object cast to a void pointer.
 * @returns NULL
 */
void* ECThreadPool::threadFunc(void *lpVoid)
{
	auto worker = static_cast<ECThreadWorker *>(lpVoid);
	auto lpPool = worker->m_pool;
	set_thread_name(pthread_self(), (lpPool->m_poolname + "/idle").c_str());
	if (!worker->init())
		return nullptr;

	while (true) {
		STaskInfo sTaskInfo{};
		bool bResult = false;

		ulock_normal locker(lpPool->m_hMutex);
		bResult = lpPool->getNextTask(&sTaskInfo, locker);
		if (bResult)
			++lpPool->m_active;
		locker.unlock();
		if (!bResult)
			break;

		assert(sTaskInfo.lpTask != NULL);
		sTaskInfo.lpTask->m_worker = worker;
		sTaskInfo.lpTask->execute();
		if (sTaskInfo.bDelete)
			delete sTaskInfo.lpTask;
		--lpPool->m_active;
		lpPool->m_hCondTaskDone.notify_one();
	}

	worker->exit();
	return NULL;
}

/**
 * Execute an ECTask instance, just calls the run() method of the derived class.
 */
void ECTask::execute()
{
	run();
}

/**
 * Construct an ECWaitableTask object.
 */
ECWaitableTask::ECWaitableTask()
: m_state(Idle)
{
}

/**
 * Destruct an ECWaitableTask object.
 */
ECWaitableTask::~ECWaitableTask()
{
	wait(WAIT_INFINITE, Idle|Done);
}

/**
 * Execute an ECWaitableTask object.
 * This calls ECTask::execute and makes sure any blocking threads will be notified when done.
 */
void ECWaitableTask::execute()
{
	ulock_normal big(m_hMutex);
	m_state = Running;
	m_hCondition.notify_all();
	big.unlock();

	ECTask::execute();

	big.lock();
	m_state = Done;
	m_hCondition.notify_all();
	big.unlock();
}

/**
 * Wait for an ECWaitableTask instance to finish.
 * @param[in]	timeout		Timeout in ms to wait for the task to finish. Pass 0 don't block at all or WAIT_INFINITE to block indefinitely.
 * @param[in]	waitMask	Mask of the combined states for which this function will wait. Default is Done, causing this function to wait
 *                          until the task is executed. The destructor for instance used Idle|Done, causing this function to only wait when
 *                          the task is currently running.
 * @retval	true if the task state matches any state in waitMask, false otherwise.
 */
bool ECWaitableTask::wait(unsigned timeout, unsigned waitMask) const
{
	ulock_normal locker(m_hMutex);

	switch (timeout) {
	case 0:
		return (m_state & waitMask) != 0;
	case WAIT_INFINITE:
		m_hCondition.wait(locker, [&](void) { return m_state & waitMask; });
		return true;
	default:
		while (!(m_state & waitMask))
			if (m_hCondition.wait_for(locker, std::chrono::milliseconds(timeout)) ==
			    std::cv_status::timeout)
				break;
		return (m_state & waitMask) != 0;
	}
}

ECScheduler::ECScheduler()
{
	auto ret = pthread_create(&m_hMainThread, nullptr, ScheduleThread, this);
	if (ret != 0) {
		ec_log_err("Could not create ECScheduler thread: %s", strerror(ret));
		return;
	}
	m_thread_active = true;
	set_thread_name(m_hMainThread, "sch");
}

ECScheduler::~ECScheduler()
{
	ulock_normal l_exit(m_hExitMutex);
	m_bExit = TRUE;
	m_hExitSignal.notify_one();
	l_exit.unlock();
	if (m_thread_active)
		pthread_join(m_hMainThread, nullptr);
}

HRESULT ECScheduler::AddSchedule(eSchedulerType eType, unsigned int ulBeginCycle,
    void *(*lpFunction)(void *), void *lpData)
{
	if (lpFunction == nullptr)
		return E_INVALIDARG;

	ECSCHEDULE sECSchedule;
	sECSchedule.eType = eType;
	sECSchedule.ulBeginCycle = ulBeginCycle;
	sECSchedule.lpFunction = lpFunction;
	sECSchedule.lpData = lpData;
	sECSchedule.tLastRunTime = 0;
	scoped_rlock l_sched(m_hSchedulerMutex);
	m_listScheduler.emplace_back(std::move(sECSchedule));
	return S_OK;
}

bool ECScheduler::hasExpired(time_t ttime, ECSCHEDULE *lpSchedule)
{
	struct tm tmLastRunTime, tmtime;

	localtime_r(&ttime, &tmtime);
	if (lpSchedule->tLastRunTime > 0)
		localtime_r(&lpSchedule->tLastRunTime, &tmLastRunTime);
	else
		memset(&tmLastRunTime, 0, sizeof(tmLastRunTime));

	switch (lpSchedule->eType) {
	case SCHEDULE_SECONDS:
		return ((tmLastRunTime.tm_min != tmtime.tm_min ||
		       (tmLastRunTime.tm_min == tmtime.tm_min &&
		       tmLastRunTime.tm_sec != tmtime.tm_sec)) &&
		       ((tmtime.tm_sec == static_cast<int>(lpSchedule->ulBeginCycle)) ||
		       (lpSchedule->ulBeginCycle > 0 &&
		       tmtime.tm_sec % static_cast<int>(lpSchedule->ulBeginCycle) < SCHEDULER_POLL_FREQUENCY)));
	case SCHEDULE_MINUTES:
		return ((tmLastRunTime.tm_hour != tmtime.tm_hour ||
		       (tmLastRunTime.tm_hour == tmtime.tm_hour &&
		       tmLastRunTime.tm_min != tmtime.tm_min)) &&
		       (tmtime.tm_min == static_cast<int>(lpSchedule->ulBeginCycle) ||
		       (lpSchedule->ulBeginCycle > 0 &&
		       tmtime.tm_min % static_cast<int>(lpSchedule->ulBeginCycle) == 0)));
	case SCHEDULE_HOUR:
		return tmLastRunTime.tm_hour != tmtime.tm_hour &&
		       static_cast<int>(lpSchedule->ulBeginCycle) >= tmtime.tm_min &&
		       static_cast<int>(lpSchedule->ulBeginCycle) <= tmtime.tm_min + 2;
	case SCHEDULE_DAY:
		return tmLastRunTime.tm_mday != tmtime.tm_mday &&
		       static_cast<int>(lpSchedule->ulBeginCycle) == tmtime.tm_hour;
	case SCHEDULE_MONTH:
		return tmLastRunTime.tm_mon != tmtime.tm_mon &&
		       static_cast<int>(lpSchedule->ulBeginCycle) == tmtime.tm_mday;
	case SCHEDULE_NONE:
		return false;
	}
	return false;
}

void *ECScheduler::ScheduleThread(void *lpTmpScheduler)
{
	kcsrv_blocksigs();
	auto lpScheduler = static_cast<ECScheduler *>(lpTmpScheduler);
	HRESULT *lperThread = nullptr;
	pthread_t hThread;
	time_t ttime;

	if (lpScheduler == nullptr)
		return nullptr;

	while (true) {
		/* Wait for a terminate signal or return after a few minutes */
		ulock_normal l_exit(lpScheduler->m_hExitMutex);
		if (lpScheduler->m_bExit)
			break;
		if (lpScheduler->m_hExitSignal.wait_for(l_exit, std::chrono::seconds(SCHEDULER_POLL_FREQUENCY)) !=
		    std::cv_status::timeout)
			break;
		l_exit.unlock();

		for (auto &sl : lpScheduler->m_listScheduler) {
			ulock_rec l_sched(lpScheduler->m_hSchedulerMutex);

			/* TODO If load on server high, check only items with a high priority */
			time(&ttime);
			if (hasExpired(ttime, &sl)) {
				auto err = pthread_create(&hThread, nullptr, sl.lpFunction, static_cast<void *>(sl.lpData));
				if (err != 0) {
					ec_log_err("Could not create ECScheduler worker thread: %s", strerror(err));
					goto task_fail;
				}
				set_thread_name(hThread, "scw");
				sl.tLastRunTime = ttime;
				err = pthread_join(hThread, reinterpret_cast<void **>(&lperThread));
				if (err != 0) {
					ec_log_err("Could not join ECScheduler work thread: %s", strerror(err));
					goto task_fail;
				}
				delete lperThread;
				lperThread = nullptr;
			}

 task_fail:
			l_sched.unlock();
			// check for a exit signal
			l_exit.lock();
			if (lpScheduler->m_bExit) {
				l_exit.unlock();
				break;
			}
			l_exit.unlock();
		}
	}

	return nullptr;
}

void set_thread_name(pthread_t tid, const std::string &name)
{
#if defined(HAVE_PTHREAD_SETNAME_NP_2)
	if (name.size() > 15)
		pthread_setname_np(tid, name.substr(0, 15).c_str());
	else
		pthread_setname_np(tid, name.c_str());
#elif defined(HAVE_PTHREAD_SET_NAME_NP_2)
	pthread_set_name_np(tid, name.c_str());
#elif defined(HAVE_PTHREAD_SETNAME_NP_3)
	if (name.size() < PTHREAD_MAX_NAMELEN_NP)
		pthread_setname_np(tid, "%s", name.c_str());
	else
		pthread_setname_np(tid, "%s", name.substr(0, PTHREAD_MAX_NAMELEN_NP).c_str());
#elif defined(HAVE_PTHREAD_SETNAME_NP_1)
	if (tid == pthread_self())
		pthread_setname_np(name.c_str());
#endif
}

void kcsrv_blocksigs()
{
	sigset_t m;
	sigemptyset(&m);
	sigaddset(&m, SIGINT);
	sigaddset(&m, SIGHUP);
	sigaddset(&m, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &m, nullptr);
}

/*
 * Used for logging only. Can return anything as long as it is unique
 * per thread.
 */
unsigned long kc_threadid()
{
#if defined(LINUX)
	return syscall(SYS_gettid);
#elif defined(OPENBSD)
	return getthrid();
#else
	return pthread_self();
#endif
}

} /* namespace */
