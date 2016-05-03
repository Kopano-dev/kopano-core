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

#ifndef ECThreadPool_INCLUDED
#define ECThreadPool_INCLUDED

#include <pthread.h>
#include <set>
#include <list>
#include <kopano/zcdefs.h>

class ECTask;

/**
 * This class represents a thread pool with a fixed amount of worker threads.
 * The amount of workers can be modified at run time, but is not automatically
 * adjusted based on the task queue length or age.
 */
class ECThreadPool _zcp_final {
private:	// types
	struct STaskInfo {
		ECTask			*lpTask;
		bool			bDelete;
		struct timeval	tvQueueTime;
	};

	typedef std::set<pthread_t> ThreadSet;
	typedef std::list<STaskInfo> TaskList;
	
public:
	ECThreadPool(unsigned ulThreadCount);
	virtual ~ECThreadPool(void);
	
	virtual bool dispatch(ECTask *lpTask, bool bTakeOwnership = false);
	unsigned threadCount() const;
	void setThreadCount(unsigned ulThreadCount, bool bWait = false);
	
	struct timeval queueAge() const;

	bool waitForAllTasks(time_t timeout) const;
	
private:	// methods
	virtual bool getNextTask(STaskInfo *lpsTaskInfo);
	void joinTerminated();
	
private:	// static methods
	static void *threadFunc(void *lpVoid);
	static bool isCurrentThread(const pthread_t &hThread);
	
private:	// members
	ThreadSet	m_setThreads;
	ThreadSet	m_setTerminated;
	TaskList	m_listTasks;
	
	mutable pthread_mutex_t	m_hMutex;
	pthread_cond_t			m_hCondition;
	pthread_cond_t			m_hCondTerminated;
	mutable pthread_cond_t	m_hCondTaskDone;

private:
	ECThreadPool(const ECThreadPool &);
	ECThreadPool& operator=(const ECThreadPool &);
	
	unsigned	m_ulTermReq;
};

/**
 * Get the number of worker threads.
 * @retval The number of available worker threads.
 */
inline unsigned ECThreadPool::threadCount() const {
	return m_setThreads.size() - m_ulTermReq;
}

/**
 * This class represents a task that can be dispatched on an ECThreadPool or
 * derived object.
 * Once dispatched, the objects run method will be executed once the threadpool
 * has a free worker and all previously queued tasks have been processed. There's
 * no way of knowing when the task is done.
 */
class ECTask
{
public:
	virtual ~ECTask(void) {};
	virtual void execute();
	
	bool dispatchOn(ECThreadPool *lpThreadPool, bool bTransferOwnership = false);
	
protected:
	virtual void run() = 0;
	ECTask(void) {};
	
private:
	// Make the object non-copyable
	ECTask(const ECTask &);
	ECTask &operator=(const ECTask &);
};

/**
 * Dispatch a task object on a particular threadpool.
 *
 * @param[in]	lpThreadPool		The threadpool on which to dispatch the task.
 * @param[in]	bTransferOwnership	Boolean parameter specifying wether the threadpool
 *                                  should take ownership of the task object, and thus
 *                                  is responsible for deleting the object when done.
 * @retval true if the task was successfully queued, false otherwise.
 */
inline bool ECTask::dispatchOn(ECThreadPool *lpThreadPool, bool bTransferOwnership) {
	return lpThreadPool ? lpThreadPool->dispatch(this, bTransferOwnership) : false;
}



/**
 * This class represents a task that can be executed on an ECThreadPool or
 * derived object. It's similar to an ECTask, but one can wait for the task
 * to be finished.
 */
class ECWaitableTask : public ECTask
{
public:
	static const unsigned WAIT_INFINITE = (unsigned)-1;
	
	enum State {
		Idle = 1,
		Running = 2,
		Done = 4
	};
	
public:
	virtual ~ECWaitableTask();
	virtual void execute(void) _zcp_override;
	
	bool done() const;
	bool wait(unsigned timeout = WAIT_INFINITE, unsigned waitMask = Done) const;
	
protected:
	ECWaitableTask();

private:
	mutable pthread_mutex_t	m_hMutex;
	mutable pthread_cond_t	m_hCondition;
	State					m_state;
};

/**
 * Check if the task has been executed.
 * @retval true when executed, false otherwise.
 */
inline bool ECWaitableTask::done() const {
	return m_state == Done;
}


/**
 * This class can be used to run a function with one argument asynchronously on
 * an ECThreadPool or derived class.
 * To call a function with more than one argument boost::bind can be used.
 */
template<typename _Rt, typename _Fn, typename _At>
class ECDeferredFunc _zcp_final : public ECWaitableTask
{
public:
	/**
	 * Construct an ECDeferredFunc instance.
	 * @param[in]	fn		The function to execute
	 * @param[in]	arg		The argument to pass to fn.
	 */
	ECDeferredFunc(_Fn fn, const _At &arg) : m_fn(fn), m_arg(arg)
	{ }
	
	virtual void run(void) _zcp_override
	{
		m_result = m_fn(m_arg);
	}
	
	/**
	 * Get the result of the asynchronous function. This method will
	 * block until the method has been executed.
	 */
	_Rt result() const {
		wait();
		return m_result;
	}
	
private:
	_Rt	m_result;
	_Fn m_fn;
	_At m_arg;
};

#endif // ndef ECThreadPool_INCLUDED
