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

#ifndef ECTHREADMANAGER_H
#define ECTHREADMANAGER_H

#include <kopano/zcdefs.h>
#include <queue>
#include <set>

#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include <kopano/kcodes.h>
#include "SOAPUtils.h"
#include "soapH.h"

/*
 * A single work item - it doesn't contain much since we defer all processing, including XML
 * parsing until a worker thread starts processing
 */
typedef struct {
    struct soap *soap;			// socket and state associated with the connection
    double dblReceiveStamp;		// time at which activity was detected on the socket
} WORKITEM;

typedef struct ACTIVESOCKET _zcp_final {
    struct soap *soap;
    time_t ulLastActivity;
    
    bool operator < (const ACTIVESOCKET &a) const { return a.soap->socket < this->soap->socket; };
} ACTIVESOCKET;

class FindSocket _zcp_final {
public:
	FindSocket(SOAP_SOCKET s) { this->s = s; };

	bool operator()(const ACTIVESOCKET &a) const { return a.soap->socket == s; }
private:
	SOAP_SOCKET s;
};

class FindListenSocket _zcp_final {
public:
	FindListenSocket(SOAP_SOCKET s) { this->s = s; };

	bool operator()(struct soap *soap) const { return soap->socket == s; }
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
    ECWorkerThread(ECLogger *lpLogger, ECThreadManager *lpManager, ECDispatcher *lpDispatcher, bool bDoNotStart = false);

protected:
    // The destructor is protected since we self-cleanup; you cannot delete this object externally.
    virtual ~ECWorkerThread();

    static void *Work(void *param);

    pthread_t m_thread;
    ECLogger *m_lpLogger;
    ECThreadManager *m_lpManager;
    ECDispatcher *m_lpDispatcher;
};

class ECPriorityWorkerThread _zcp_final : public ECWorkerThread {
public:
	ECPriorityWorkerThread(ECLogger *lpLogger, ECThreadManager *lpManager, ECDispatcher *lpDispatcher);
	// The destructor is public since this thread isn't detached, we wait for the thread and clean it
	~ECPriorityWorkerThread();
};

/*
 * It is the thread manager's job to keep track of processing threads, and adding or removing threads
 * when requested. 
 */
class ECThreadManager _zcp_final {
public:
    // ulThreads is the normal number of threads that are started; These threads are pre-started and will be in an idle state.
    ECThreadManager(ECLogger *lpLogger, ECDispatcher *lpDispatcher, unsigned int ulThreads);
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
    pthread_mutex_t 			m_mutexThreads;
    std::list<ECWorkerThread *> m_lstThreads;
	ECPriorityWorkerThread *	m_lpPrioWorker;
    ECLogger *					m_lpLogger;
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
class ECWatchDog _zcp_final {
public:
    ECWatchDog(ECConfig *lpConfig, ECLogger *lpLogger, ECDispatcher *lpDispatcher, ECThreadManager *lpThreadManager);
    ~ECWatchDog();
private:
    // Main watch thread
    static void *Watch(void *);
    
    ECLogger *			m_lpLogger;
    ECConfig *			m_lpConfig;
    ECDispatcher *		m_lpDispatcher;
    ECThreadManager*	m_lpThreadManager;
    pthread_t			m_thread;
    bool				m_bExit;
    pthread_mutex_t		m_mutexExit;
    pthread_cond_t		m_condExit;
};

/*
 * The main dispatcher; The dispatcher monitors open connections for activity and queues processing on the
 * work item queue. It is the owner of the thread manager and watchdog. Workers will query the dispatcher for
 * work items and will inform the dispatcher when a work item is done.
 */
typedef SOAP_SOCKET (*CREATEPIPESOCKETCALLBACK)(void *lpParam);

class ECDispatcher {
public:

    ECDispatcher(ECLogger *lpLogger, ECConfig *lpConfig, CREATEPIPESOCKETCALLBACK lpCallback, void *lpCallbackParam);
    virtual ~ECDispatcher();
    
    // Statistics
    ECRESULT GetIdle(unsigned int *lpulIdle); 				// Idle threads
    ECRESULT GetThreadCount(unsigned int *lpulThreads, unsigned int *lpulIdleThreads);		// Total threads + idle threads
    ECRESULT GetFrontItemAge(double *lpdblAge);		// Age of the front queue item (time since the item was queued and now)
    ECRESULT GetQueueLength(unsigned int *lpulQueueLength);	// Number of requests in the queue

    ECRESULT SetThreadCount(unsigned int ulThreads);
    
    // Add a listen socket
    ECRESULT AddListenSocket(struct soap *soap);

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
    ECLogger *				m_lpLogger;
    ECConfig *				m_lpConfig;
    ECThreadManager *		m_lpThreadManager;

    pthread_mutex_t 		m_mutexItems;
    std::queue<WORKITEM *> 	m_queueItems;
    pthread_cond_t			m_condItems;
    std::queue<WORKITEM *> 	m_queuePrioItems;
    pthread_cond_t			m_condPrioItems;

    std::map<int, ACTIVESOCKET>	m_setSockets;
    std::map<int, struct soap *>	m_setListenSockets;
    pthread_mutex_t			m_mutexSockets;
    bool					m_bExit;
    pthread_mutex_t			m_mutexIdle;
    unsigned int			m_ulIdle;
	CREATEPIPESOCKETCALLBACK m_lpCreatePipeSocketCallback;
	void *					m_lpCreatePipeSocketParam;

	// Socket settings (TCP + SSL)
	int			m_nMaxKeepAlive;
	int			m_nRecvTimeout;
	int			m_nReadTimeout;
	int			m_nSendTimeout;
};

class ECDispatcherSelect _zcp_final : public ECDispatcher {
private:
    int			m_fdRescanRead;
    int			m_fdRescanWrite;

public:
    ECDispatcherSelect(ECLogger *lpLogger, ECConfig *lpConfig, CREATEPIPESOCKETCALLBACK lpCallback, void *lpCallbackParam);
    virtual ECRESULT MainLoop();

    virtual ECRESULT ShutDown();

    virtual ECRESULT NotifyRestart(SOAP_SOCKET s);
};

#ifdef HAVE_EPOLL_CREATE
class ECDispatcherEPoll _zcp_final : public ECDispatcher {
private:
	int m_fdMax;
	int m_epFD;

public:
    ECDispatcherEPoll(ECLogger *lpLogger, ECConfig *lpConfig, CREATEPIPESOCKETCALLBACK lpCallback, void *lpCallbackParam);
    virtual ~ECDispatcherEPoll();

    virtual ECRESULT MainLoop();

    //virtual ECRESULT ShutDown();

    virtual ECRESULT NotifyRestart(SOAP_SOCKET s);
};
#endif

#endif
