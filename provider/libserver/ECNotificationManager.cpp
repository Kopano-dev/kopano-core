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

#include <kopano/platform.h>
#include "ECNotificationManager.h"
#include "ECSession.h"
#include "ECSessionManager.h"
#include "ECStringCompat.h"

#include "soapH.h"

// Copied from generated soapServer.cpp
static int soapresponse(struct notifyResponse notifications, struct soap *soap)
{
    soap_serializeheader(soap);
#if GSOAP_VERSION > 20816
    soap_serialize_notifyResponse(soap, &notifications);
#else
    soap_serialize_ns_notifyResponse(soap, &notifications);
#endif
    if (soap_begin_count(soap))
        return soap->error;
    if (soap->mode & SOAP_IO_LENGTH)
    {	if (soap_envelope_begin_out(soap)
         || soap_putheader(soap)
         || soap_body_begin_out(soap)
#if GSOAP_VERSION > 20816
         || soap_put_notifyResponse(soap, &notifications, "ns:notifyResponse", NULL)
#else
         || soap_put_ns_notifyResponse(soap, &notifications, "ns:notifyResponse", NULL)
#endif
         || soap_body_end_out(soap)
         || soap_envelope_end_out(soap))
            return soap->error;
    };
    if (soap_end_count(soap)
     || soap_response(soap, SOAP_OK)
     || soap_envelope_begin_out(soap)
     || soap_putheader(soap)
     || soap_body_begin_out(soap)
#if GSOAP_VERSION > 20816
     || soap_put_notifyResponse(soap, &notifications, "ns:notifyResponse", NULL)
#else
     || soap_put_ns_notifyResponse(soap, &notifications, "ns:notifyResponse", NULL)
#endif
     || soap_body_end_out(soap)
     || soap_envelope_end_out(soap)
     || soap_end_send(soap))
            return soap->error;
    return soap_closesock(soap);
}

ECNotificationManager::ECNotificationManager(void)
{
    m_bExit = false;
    pthread_mutex_init(&m_mutexSessions, NULL);
    pthread_mutex_init(&m_mutexRequests, NULL);
    pthread_cond_init(&m_condSessions, NULL);
    pthread_create(&m_thread, NULL, Thread, this);
    set_thread_name(m_thread, "NotificationManager");

    m_ulTimeout = 60; // Currently hardcoded at 60s, see comment in Work()    
}

ECNotificationManager::~ECNotificationManager()
{
    pthread_mutex_lock(&m_mutexSessions);
    m_bExit = true;
    pthread_cond_broadcast(&m_condSessions);
    pthread_mutex_unlock(&m_mutexSessions);

	ec_log_info("Shutdown notification manager");
    pthread_join(m_thread, NULL);

    // Close and free any pending requests (clients will receive EOF)
    std::map<ECSESSIONID, NOTIFREQUEST>::const_iterator iterRequest;
    for (iterRequest = m_mapRequests.begin();
         iterRequest != m_mapRequests.end(); ++iterRequest) {
		// we can't call kopano_notify_done here, race condition on shutdown in ECSessionManager vs ECDispatcher
		kopano_end_soap_connection(iterRequest->second.soap);
		soap_destroy(iterRequest->second.soap);
		soap_end(iterRequest->second.soap);
		soap_free(iterRequest->second.soap);
    }
    pthread_mutex_destroy(&m_mutexSessions);
    pthread_mutex_destroy(&m_mutexRequests);
    pthread_cond_destroy(&m_condSessions);
}

// Called by the SOAP handler
HRESULT ECNotificationManager::AddRequest(ECSESSIONID ecSessionId, struct soap *soap)
{
    std::map<ECSESSIONID, NOTIFREQUEST>::const_iterator iterRequest;
    struct soap *lpItem = NULL;
    
    pthread_mutex_lock(&m_mutexRequests);
    iterRequest = m_mapRequests.find(ecSessionId);
    if(iterRequest != m_mapRequests.end()) {
        // Hm. There is already a SOAP request waiting for this session id. Apparently a second SOAP connection has now
        // requested notifications. Since this should only happen if the client thinks it has lost its connection and has
        // restarted the request, we will replace the existing request with this one.

		ec_log_warn("Replacing notification request for ID %llu",
			static_cast<unsigned long long>(ecSessionId));
        
        // Return the previous request as an error
        struct notifyResponse notifications;
#if GSOAP_VERSION > 20816
        soap_default_notifyResponse(iterRequest->second.soap, &notifications);
#else
        soap_default_ns_notifyResponse(iterRequest->second.soap, &notifications);
#endif
        notifications.er = KCERR_NOT_FOUND; // Should be something like 'INTERRUPTED' or something
        if(soapresponse(notifications, iterRequest->second.soap)) {
            // Handle error on the response
            soap_send_fault(iterRequest->second.soap);
        }
		soap_destroy(iterRequest->second.soap);
		soap_end(iterRequest->second.soap);
        lpItem = iterRequest->second.soap;
    
        // Pass the socket back to the socket manager (which will probably close it since the client should not be holding two notification sockets)
        kopano_notify_done(lpItem);
    }
    
    NOTIFREQUEST req;
    req.soap = soap;
    time(&req.ulRequestTime);
    
    m_mapRequests[ecSessionId] = req;
    
    pthread_mutex_unlock(&m_mutexRequests);
    
    // There may already be notifications waiting for this session, so post a change on this session so that the
    // thread will attempt to get notifications on this session
    NotifyChange(ecSessionId);
    
    return hrSuccess;
}

// Called by a session when it has a notification to send
HRESULT ECNotificationManager::NotifyChange(ECSESSIONID ecSessionId)
{
    // Simply mark the session in our set of active sessions
    pthread_mutex_lock(&m_mutexSessions);
    m_setActiveSessions.insert(ecSessionId);
    pthread_cond_signal(&m_condSessions);		// Wake up thread due to activity
    pthread_mutex_unlock(&m_mutexSessions);    
    
    return hrSuccess;
}

void * ECNotificationManager::Thread(void *lpParam)
{
    ECNotificationManager *lpThis = (ECNotificationManager *)lpParam;
    
    return lpThis->Work();
}

void *ECNotificationManager::Work() {
    ECRESULT er = erSuccess;
    ECSession *lpecSession = NULL;
    struct notifyResponse notifications;

    std::set<ECSESSIONID>::const_iterator iterSessions;
    std::set<ECSESSIONID> setActiveSessions;
    std::map<ECSESSIONID, NOTIFREQUEST>::iterator iterRequest;
    struct soap *lpItem;
    time_t ulNow = 0;
    
    // Keep looping until we should exit
    while(1) {
        pthread_mutex_lock(&m_mutexSessions);
        
        if(m_bExit) {
            pthread_mutex_unlock(&m_mutexSessions);
            break;
        }
            
        if(m_setActiveSessions.size() == 0) {
            // Wait for events for maximum of 1 sec
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&m_condSessions, &m_mutexSessions, &ts);
        }
        
        // Make a copy of the session list so we can release the lock ASAP
        setActiveSessions = m_setActiveSessions;
        m_setActiveSessions.clear();
        
        pthread_mutex_unlock(&m_mutexSessions);
        
        // Look at all the sessions that have signalled a change
        for (iterSessions = setActiveSessions.begin();
             iterSessions != setActiveSessions.end(); ++iterSessions)
        {
            lpItem = NULL;
        
            pthread_mutex_lock(&m_mutexRequests);
            
            // Find the request for the session that had something to say
            iterRequest = m_mapRequests.find(*iterSessions);
            
            if(iterRequest != m_mapRequests.end()) {
                // Reset notification response to default values
#if GSOAP_VERSION > 20816
                soap_default_notifyResponse(iterRequest->second.soap, &notifications);
#else
                soap_default_ns_notifyResponse(iterRequest->second.soap, &notifications);
#endif
                if(g_lpSessionManager->ValidateSession(iterRequest->second.soap, *iterSessions, &lpecSession, true) == erSuccess) {
                    // Get the notifications from the session
                    er = lpecSession->GetNotifyItems(iterRequest->second.soap, &notifications);
                    
                    if(er == KCERR_NOT_FOUND) {
                        if(time(NULL) - iterRequest->second.ulRequestTime < m_ulTimeout) {
                            // No notifications - this means we have to wait. This can happen if the session was marked active since
                            // the request was just made, and there may have been notifications still waiting for us
                            pthread_mutex_unlock(&m_mutexRequests);
                            lpecSession->Unlock();
                            continue; // Totally ignore this item == wait
                        } else {
                            // No notifications and we're out of time, just respond OK with 0 notifications
                            er = erSuccess;
                            notifications.pNotificationArray = (struct notificationArray *)soap_malloc(iterRequest->second.soap, sizeof(notificationArray));
                            soap_default_notificationArray(iterRequest->second.soap, notifications.pNotificationArray);
                        }
                    }

					ULONG ulCapabilities = lpecSession->GetCapabilities();
					if (er == erSuccess && (ulCapabilities & KOPANO_CAP_UNICODE) == 0) {
						ECStringCompat stringCompat(false);
						er = FixNotificationsEncoding(iterRequest->second.soap, stringCompat, notifications.pNotificationArray);
					}
						
                    notifications.er = er;
                    
                    lpecSession->Unlock();
                } else {
                    // The session is dead
                    notifications.er = KCERR_END_OF_SESSION;
                }

                // Send the SOAP data
				if(soapresponse(notifications, iterRequest->second.soap)) {
					// Handle error on the response
					soap_send_fault(iterRequest->second.soap);
				}
				// Free allocated SOAP data (in GetNotifyItems())
				soap_destroy(iterRequest->second.soap);
				soap_end(iterRequest->second.soap);

                // Since we have responded, remove the item from our request list and pass it back to the active socket list so
                // that the next SOAP call can be handled (probably another notification request)
                lpItem = iterRequest->second.soap;
                
                m_mapRequests.erase(iterRequest);
                
            } else {
                // Nobody was listening to this session, just ignore it
            }

            pthread_mutex_unlock(&m_mutexRequests);
            
            if(lpItem)
                kopano_notify_done(lpItem);
            
        }
        
        /* Find all notification requests which have not received any data for m_ulTimeout seconds. This makes sure
         * that the client get a response, even if there are no notifications. Since the client has a hard-coded
         * TCP timeout of 70 seconds, we need to respond well within those 70 seconds. We therefore use a timeout
         * value of 60 seconds here.
         */
        pthread_mutex_lock(&m_mutexRequests);
        time(&ulNow);
        iterRequest = m_mapRequests.begin();
        while(iterRequest != m_mapRequests.end()) {
            if(ulNow - iterRequest->second.ulRequestTime > m_ulTimeout) {
                // Mark the session as active so it will be processed in the next loop
                NotifyChange(iterRequest->first);
            }
            ++iterRequest;
        } 
        pthread_mutex_unlock(&m_mutexRequests);
        
    }
    
    return NULL;
}
