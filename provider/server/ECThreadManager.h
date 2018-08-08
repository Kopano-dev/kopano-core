/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECTHREADMANAGER_H
#define ECTHREADMANAGER_H

#include <kopano/zcdefs.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <set>
#include <pthread.h>
#include <kopano/ECConfig.h>
#include <kopano/kcodes.h>
#include "SOAPUtils.h"
#include "soapH.h"

using KC::ECRESULT;

/*
 * A single work item - it doesn't contain much since we defer all processing, including XML
 * parsing until a worker thread starts processing
 */
struct WORKITEM {
    struct soap *soap;			// socket and state associated with the connection
	KC::time_point dblReceiveStamp; /* time at which activity was detected on the socket */
};

struct ACTIVESOCKET _kc_final {
    struct soap *soap;
    time_t ulLastActivity;

	bool operator<(const ACTIVESOCKET &a) const noexcept { return a.soap->socket < this->soap->socket; };
};

class FindSocket _kc_final {
public:
	FindSocket(SOAP_SOCKET sk) : s(sk) {}
	bool operator()(const ACTIVESOCKET &a) const noexcept { return a.soap->socket == s; }
private:
	SOAP_SOCKET s;
};

class FindListenSocket _kc_final {
public:
	FindListenSocket(SOAP_SOCKET sk) : s(sk) {}
	bool operator()(struct soap *soap) const noexcept { return soap->socket == s; }
private:
	SOAP_SOCKET s;
};

class ECThreadManager;
class ECDispatcher;

/*
 * Each instance of ECWorkerThread represents a single worker thread; the thread is started
 * when constructed, and exits when it is deleted. It needs access to the thread manager to notify
 * it of the thread state, possibly exiting if needed, and needs access to the dispatcher to retrieve
 * the next work item.
 */
class ECWorkerThread {
public:
	ECWorkerThread(ECThreadManager *, ECDispatcher *, bool nostart = false);

protected:
    // The destructor is protected since we self-cleanup; you cannot delete this object externally.
	virtual ~ECWorkerThread(void) = default;
    static void *Work(void *param);

    pthread_t m_thread;
    ECThreadManager *m_lpManager;
    ECDispatcher *m_lpDispatcher;
	bool m_thread_active = false;
};

class _kc_export_dycast ECPriorityWorkerThread _kc_final :
    public ECWorkerThread {
	public:
	_kc_hidden ECPriorityWorkerThread(ECThreadManager *, ECDispatcher *);
	// The destructor is public since this thread isn't detached, we wait for the thread and clean it
	_kc_hidden ~ECPriorityWorkerThread(void);
};

/*
 * It is the thread manager's job to keep track of processing threads, and adding or removing threads
 * when requested.
 */
class ECThreadManager _kc_final {
public:
    // ulThreads is the normal number of threads that are started; These threads are pre-started and will be in an idle state.
	ECThreadManager(ECDispatcher *, unsigned int threads);
    ~ECThreadManager();

    // Adds n threads above the standard thread count. Threads are removed back to the normal thread count whenever the message
    // queue hits size 0 and there is an idle thread.
    ECRESULT ForceAddThread(int nThreads);
    // Some statistics
    ECRESULT GetThreadCount(unsigned int *lpulThreads);
    // This is the same parameter as passed in the constructor
    ECRESULT SetThreadCount(unsigned int ulThreads);
    // Called by the worker thread when it is idle. *lpfStop is set to TRUE then the thread will terminate and delete itself.
    ECRESULT NotifyIdle(ECWorkerThread *, bool *lpfStop);

private:
	std::mutex m_mutexThreads;
    std::list<ECWorkerThread *> m_lstThreads;
	ECPriorityWorkerThread *	m_lpPrioWorker;
    ECDispatcher *				m_lpDispatcher;
    unsigned int				m_ulThreads;
};

/*
 * Represents the watchdog thread. This monitors the dispatcher and acts when needed.
 *
 * We check the age of the first item in the queue dblMaxFreq times per second. If it is higher
 * than dblMaxAge, a new thread is added
 *
 * Thread deletion is done by the Thread Manager.
 */
class ECWatchDog _kc_final {
public:
	ECWatchDog(KC::ECConfig *, ECDispatcher *, ECThreadManager *);
    ~ECWatchDog();
private:
    // Main watch thread
    static void *Watch(void *);

	KC::ECConfig *m_lpConfig;
    ECDispatcher *		m_lpDispatcher;
    ECThreadManager*	m_lpThreadManager;
    pthread_t			m_thread;
	bool m_thread_active = false, m_bExit = false;
	std::mutex m_mutexExit;
	std::condition_variable m_condExit;
};

/*
 * The main dispatcher; The dispatcher monitors open connections for activity and queues processing on the
 * work item queue. It is the owner of the thread manager and watchdog. Workers will query the dispatcher for
 * work items and will inform the dispatcher when a work item is done.
 */
class ECDispatcher {
public:
	ECDispatcher(KC::ECConfig *);
	virtual ~ECDispatcher();

    // Statistics
    ECRESULT GetIdle(unsigned int *lpulIdle); 				// Idle threads
    ECRESULT GetThreadCount(unsigned int *lpulThreads, unsigned int *lpulIdleThreads);		// Total threads + idle threads
    ECRESULT GetFrontItemAge(double *lpdblAge);		// Age of the front queue item (time since the item was queued and now)
    ECRESULT GetQueueLength(unsigned int *lpulQueueLength);	// Number of requests in the queue
    ECRESULT SetThreadCount(unsigned int ulThreads);
    // Add a listen socket
	ECRESULT AddListenSocket(std::unique_ptr<struct soap, KC::ec_soap_deleter> &&);

	// Add soap socket in the work queue
	ECRESULT QueueItem(struct soap *soap);

    // Get the next work item on the queue, if bWait is TRUE, will block until a work item is available. The returned
    // workitem should not be freed, but returned to the class via NotifyDone(), at which point it will be cleaned up
    ECRESULT GetNextWorkItem(WORKITEM **item, bool bWait, bool bPrio);

    // Reload variables from config
    ECRESULT DoHUP();

    // Called asynchronously during MainLoop() to shutdown the server
    virtual ECRESULT ShutDown();

    // Inform that a soap request was processed and is finished. This will cause the dispatcher to start listening
    // on that socket for activity again
    ECRESULT NotifyDone(struct soap *soap);
    virtual ECRESULT NotifyRestart(SOAP_SOCKET s) = 0;

    // Goes into main listen loop, accepting sockets and monitoring existing accepted sockets for activity. Also closes
    // sockets which are idle for more than ulSocketTimeout
    virtual ECRESULT MainLoop() = 0;

protected:
	KC::ECConfig *m_lpConfig;
	ECThreadManager *m_lpThreadManager = nullptr;
	std::mutex m_mutexItems;
	std::queue<WORKITEM *> m_queueItems;
	std::condition_variable m_condItems;
	std::queue<WORKITEM *> m_queuePrioItems;
	std::condition_variable m_condPrioItems;
	std::map<int, ACTIVESOCKET> m_setSockets;
	std::map<int, std::unique_ptr<struct soap, KC::ec_soap_deleter>> m_setListenSockets;
	std::mutex m_mutexSockets;
	bool m_bExit = false;
	std::atomic<unsigned int> m_ulIdle{0};
	// Socket settings (TCP + SSL)
	int			m_nRecvTimeout;
	int			m_nReadTimeout;
	int			m_nSendTimeout;
};

class ECDispatcherSelect _kc_final : public ECDispatcher {
private:
    int			m_fdRescanRead;
    int			m_fdRescanWrite;

public:
	ECDispatcherSelect(KC::ECConfig *);
    virtual ECRESULT MainLoop();
    virtual ECRESULT ShutDown();
    virtual ECRESULT NotifyRestart(SOAP_SOCKET s);
};

#ifdef HAVE_EPOLL_CREATE
class ECDispatcherEPoll _kc_final : public ECDispatcher {
private:
	int m_fdMax;
	int m_epFD;

public:
	ECDispatcherEPoll(KC::ECConfig *);
    virtual ~ECDispatcherEPoll();
    virtual ECRESULT MainLoop();
    virtual ECRESULT NotifyRestart(SOAP_SOCKET s);
};
#endif

#endif
