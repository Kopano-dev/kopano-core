/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECTHREADMANAGER_H
#define ECTHREADMANAGER_H

#include <kopano/zcdefs.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <pthread.h>
#include <kopano/ECConfig.h>
#include <kopano/ECThreadPool.h>
#include <kopano/kcodes.h>
#include "SOAPUtils.h"
#include "soapH.h"

using KC::ECRESULT;

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

class ECDispatcher;

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
	ECWatchDog(KC::ECConfig *, ECDispatcher *);
    ~ECWatchDog();
private:
    // Main watch thread
    static void *Watch(void *);

	KC::ECConfig *m_lpConfig;
    ECDispatcher *		m_lpDispatcher;
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
	ECDispatcher(std::shared_ptr<KC::ECConfig>);
	virtual ~ECDispatcher();

    // Statistics
    ECRESULT GetIdle(unsigned int *lpulIdle); 				// Idle threads
    ECRESULT GetThreadCount(unsigned int *lpulThreads, unsigned int *lpulIdleThreads);		// Total threads + idle threads
    ECRESULT GetFrontItemAge(double *lpdblAge);		// Age of the front queue item (time since the item was queued and now)
    ECRESULT GetQueueLength(unsigned int *lpulQueueLength);	// Number of requests in the queue
    ECRESULT SetThreadCount(unsigned int ulThreads);
	void force_add_threads(size_t);
    // Add a listen socket
	ECRESULT AddListenSocket(std::unique_ptr<struct soap, KC::ec_soap_deleter> &&);

	// Add soap socket in the work queue
	ECRESULT QueueItem(struct soap *soap);

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
	std::shared_ptr<KC::ECConfig> m_lpConfig;
	KC::ECThreadPool m_pool{0}, m_prio{0};
	std::map<int, ACTIVESOCKET> m_setSockets;
	std::map<int, std::unique_ptr<struct soap, KC::ec_soap_deleter>> m_setListenSockets;
	std::mutex m_poolcount, m_mutexSockets;
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
	ECDispatcherSelect(std::shared_ptr<KC::ECConfig>);
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
	ECDispatcherEPoll(std::shared_ptr<KC::ECConfig>);
    virtual ~ECDispatcherEPoll();
    virtual ECRESULT MainLoop();
    virtual ECRESULT NotifyRestart(SOAP_SOCKET s);
};
#endif

#endif
