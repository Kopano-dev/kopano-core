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

#include <chrono>
#include <condition_variable>
#include <utility>
#include <kopano/platform.h>
#include <kopano/ECLogger.h>
#include <kopano/ECThreadPool.h>
#include <algorithm>

namespace KC {

/**
 * Construct an ECThreadPool instance.
 * @param[in]	ulThreadCount	The amount of worker hreads to create.
 */
ECThreadPool::ECThreadPool(unsigned ulThreadCount)
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
 * Dispatch a task object on the threadpool instance.
 * @param[in]	lpTask			The task object to dispatch.
 * @param[in]	bTakeOwnership	Boolean parameter specifying whether the threadpool
 *                              should take ownership of the task object, and thus
 *                              is responsible for deleting the object when done.
 * @returns true if the task was successfully queued, false otherwise.
 */
bool ECThreadPool::dispatch(ECTask *lpTask, bool bTakeOwnership)
{
	STaskInfo sTaskInfo = {lpTask, bTakeOwnership};
	ulock_normal locker(m_hMutex);
	m_listTasks.emplace_back(std::move(sTaskInfo));
	m_hCondition.notify_one();
	joinTerminated(locker);
	return true;
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
				pthread_t hThread;
		
				auto ret = pthread_create(&hThread, nullptr, &threadFunc, this);
				if (ret != 0) {
					ec_log_err("Could not create ECThreadPool worker thread: %s", strerror(ret));
					/* If there were no resources, stop trying. */
					break;
				}
				set_thread_name(hThread, "ECThreadPool");
				m_setThreads.emplace(hThread);
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
	while ((bTerminate = (m_ulTermReq > 0)) == false && m_listTasks.empty())
		m_hCondition.wait(locker);
		
	if (bTerminate) {
		pthread_t self = pthread_self();
		auto iThread = std::find_if(m_setThreads.cbegin(), m_setThreads.cend(),
			[self](pthread_t t) { return pthread_equal(t, self) != 0; });
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
	for (auto thr : m_setTerminated)
		pthread_join(thr, NULL);
	
	m_setTerminated.clear();
}

/**
 * The main loop of the worker threads.
 * @param[in]	lpVoid	Pointer to the owning ECThreadPool object cast to a void pointer.
 * @returns NULL
 */
void* ECThreadPool::threadFunc(void *lpVoid)
{
	auto lpPool = static_cast<ECThreadPool *>(lpVoid);
	
	while (true) {
		STaskInfo sTaskInfo = {NULL, false};
		bool bResult = false;
	
		ulock_normal locker(lpPool->m_hMutex);
		bResult = lpPool->getNextTask(&sTaskInfo, locker);
		locker.unlock();
		if (!bResult)
			break;
			
		assert(sTaskInfo.lpTask != NULL);
		sTaskInfo.lpTask->execute();
		if (sTaskInfo.bDelete)
			delete sTaskInfo.lpTask;
		lpPool->m_hCondTaskDone.notify_one();
	}
	
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

} /* namespace */
