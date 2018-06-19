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

#include <condition_variable>
#include <mutex>
#include <pthread.h>
#include <set>
#include <list>
#include <kopano/zcdefs.h>

namespace KC {

class ECTask;

/**
 * This class represents a thread pool with a fixed amount of worker threads.
 * The amount of workers can be modified at run time, but is not automatically
 * adjusted based on the task queue length or age.
 */
class _kc_export ECThreadPool _kc_final {
private:	// types
	struct STaskInfo {
		ECTask			*lpTask;
		bool			bDelete;
	};

	typedef std::set<pthread_t> ThreadSet;
	typedef std::list<STaskInfo> TaskList;
	
public:
	ECThreadPool(unsigned ulThreadCount);
	virtual ~ECThreadPool(void);
	
	virtual bool dispatch(ECTask *lpTask, bool bTakeOwnership = false);
	_kc_hidden unsigned int threadCount(void) const;
	_kc_hidden void setThreadCount(unsigned int cuont, bool wait = false);
	
private:	// methods
	_kc_hidden virtual bool getNextTask(STaskInfo *, std::unique_lock<std::mutex> &);
	_kc_hidden void joinTerminated(std::unique_lock<std::mutex> &);
	_kc_hidden static void *threadFunc(void *);
	
	ThreadSet	m_setThreads;
	ThreadSet	m_setTerminated;
	TaskList	m_listTasks;
	
	mutable std::mutex m_hMutex;
	std::condition_variable m_hCondition;
	std::condition_variable m_hCondTerminated;
	mutable std::condition_variable m_hCondTaskDone;

	ECThreadPool(const ECThreadPool &) = delete;
	ECThreadPool &operator=(const ECThreadPool &) = delete;
	
	unsigned int m_ulTermReq = 0;
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
class _kc_export ECTask {
public:
	_kc_hidden virtual ~ECTask(void) = default;
	_kc_hidden virtual void execute(void);
	_kc_hidden bool dispatchOn(ECThreadPool *, bool transfer_ownership = false);
	
protected:
	_kc_hidden virtual void run(void) = 0;
	_kc_hidden ECTask(void) {};
	
private:
	// Make the object non-copyable
	ECTask(const ECTask &) = delete;
	ECTask &operator=(const ECTask &) = delete;
};

/**
 * Dispatch a task object on a particular threadpool.
 *
 * @param[in]	lpThreadPool		The threadpool on which to dispatch the task.
 * @param[in]	bTransferOwnership	Boolean parameter specifying whether the threadpool
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
class _kc_export ECWaitableTask : public ECTask {
public:
	static const unsigned WAIT_INFINITE = (unsigned)-1;
	
	enum State {
		Idle = 1,
		Running = 2,
		Done = 4
	};
	
	virtual ~ECWaitableTask();
	virtual void execute(void) _kc_override;
	_kc_hidden bool done() const { return m_state == Done; }
	bool wait(unsigned timeout = WAIT_INFINITE, unsigned waitMask = Done) const;
	
protected:
	ECWaitableTask();

private:
	mutable std::mutex m_hMutex;
	mutable std::condition_variable m_hCondition;
	State					m_state;
};

/**
 * This class can be used to run a function with one argument asynchronously on
 * an ECThreadPool or derived class.
 * To call a function with more than one argument boost::bind can be used.
 */
template<typename Rt, typename Fn, typename At>
class ECDeferredFunc _kc_final : public ECWaitableTask {
public:
	/**
	 * Construct an ECDeferredFunc instance.
	 * @param[in]	fn		The function to execute
	 * @param[in]	arg		The argument to pass to fn.
	 */
	ECDeferredFunc(Fn fn, const At &arg) : m_fn(fn), m_arg(arg)
	{ }
	
	virtual void run(void) _kc_override
	{
		m_result = m_fn(m_arg);
	}
	
	/**
	 * Get the result of the asynchronous function. This method will
	 * block until the method has been executed.
	 */
	Rt result() const
	{
		wait();
		return m_result;
	}
	
private:
	Rt m_result = 0;
	Fn m_fn;
	At m_arg;
};

} /* namespace */

#endif // ndef ECThreadPool_INCLUDED
