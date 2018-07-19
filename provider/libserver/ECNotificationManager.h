/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECNOTIFICATIONMANAGER_H
#define ECNOTIFICATIONMANAGER_H

#include <kopano/zcdefs.h>
#include <condition_variable>
#include <mutex>
#include <pthread.h>
#include "ECSession.h"
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>

#include <map>
#include <set>

struct soap;

namespace KC {

/*
 * The notification manager runs in a single thread, servicing notifications to ALL clients
 * that are waiting for a notification. We simply store all waiting soap connection objects together
 * with their getNextNotify() request, and once we are signalled that something has changed for one
 * of those queues, we send the reply, and requeue the soap connection for the next request.
 *
 * So, basically we only handle the SOAP-reply part of the soap request.
 */
 
struct NOTIFREQUEST {
    struct soap *soap;
    time_t ulRequestTime;
};

class ECNotificationManager _kc_final {
public:
	ECNotificationManager();
	~ECNotificationManager();
    
    // Called by the SOAP handler
    HRESULT AddRequest(ECSESSIONID ecSessionId, struct soap *soap);

    // Called by a session when it has a notification to send
    HRESULT NotifyChange(ECSESSIONID ecSessionId);
    
private:
    // Just a wrapper to Work()
    static void * Thread(void *lpParam);
    void * Work();
    
	bool m_thread_active = false, m_bExit = false;
    pthread_t 	m_thread;
	unsigned int m_ulTimeout = 60; /* Currently hardcoded at 60s, see comment in Work() */

    // A map of all sessions that are waiting for a SOAP response to be sent (an item can be in here for up to 60 seconds)
    std::map<ECSESSIONID, NOTIFREQUEST> 	m_mapRequests;
    // A set of all sessions that have reported notification activity, but are yet to be processed.
    // (a session is in here for only very short periods of time, and contains only a few sessions even if the load is high)
    std::set<ECSESSIONID> 					m_setActiveSessions;
    
	std::mutex m_mutexRequests;
	std::mutex m_mutexSessions;
	std::condition_variable m_condSessions;
};

extern ECSessionManager *g_lpSessionManager;
extern _kc_export void (*kopano_notify_done)(struct soap *);

} /* namespace */

#endif
