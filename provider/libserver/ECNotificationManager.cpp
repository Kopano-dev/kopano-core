/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <chrono>
#include <pthread.h>
#include "ECMAPI.h"
#include "ECNotification.h"
#include "ECNotificationManager.h"
#include "ECSession.h"
#include "ECSessionManager.h"
#include "ECStringCompat.h"
#include "SOAPUtils.h"
#include "soapH.h"

using namespace KC::chrono_literals;

namespace KC {

ECNotification::ECNotification()
{
	Init();
}

ECNotification::~ECNotification()
{
	FreeNotificationStruct(m_lpsNotification, true);
}

ECNotification::ECNotification(const ECNotification &x)
{
	Init();
	*this = x;
}

ECNotification::ECNotification(const notification &notification)
{
	Init();
	*this = notification;
}

void ECNotification::Init()
{
	m_lpsNotification = s_alloc<notification>(nullptr);
	memset(m_lpsNotification, 0, sizeof(notification));
}

ECNotification& ECNotification::operator=(const ECNotification &x)
{
	if (this != &x)
		CopyNotificationStruct(nullptr, x.m_lpsNotification, *m_lpsNotification);
	return *this;
}

ECNotification &ECNotification::operator=(const notification &srcNotification)
{
	CopyNotificationStruct(nullptr, &srcNotification, *m_lpsNotification);
	return *this;
}

void ECNotification::SetConnection(unsigned int ulConnection)
{
	m_lpsNotification->ulConnection = ulConnection;
}

void ECNotification::GetCopy(struct soap *soap, notification &notification) const
{
	CopyNotificationStruct(soap, m_lpsNotification, notification);
}

/**
 * Get object size
 *
 * @return Object size in bytes
 */
size_t ECNotification::GetObjectSize(void) const
{
	return NotificationStructSize(m_lpsNotification);
}

// Copied from generated soapServer.cpp
static int soapresponse(struct notifyResponse notifications, struct soap *soap)
{
    soap_serializeheader(soap);
    soap_serialize_notifyResponse(soap, &notifications);
    if (soap_begin_count(soap))
        return soap->error;
    if (soap->mode & SOAP_IO_LENGTH)
    {	if (soap_envelope_begin_out(soap)
         || soap_putheader(soap)
         || soap_body_begin_out(soap)
         || soap_put_notifyResponse(soap, &notifications, "ns:notifyResponse", NULL)
         || soap_body_end_out(soap)
         || soap_envelope_end_out(soap))
            return soap->error;
    };
    if (soap_end_count(soap)
     || soap_response(soap, SOAP_OK)
     || soap_envelope_begin_out(soap)
     || soap_putheader(soap)
     || soap_body_begin_out(soap)
     || soap_put_notifyResponse(soap, &notifications, "ns:notifyResponse", NULL)
     || soap_body_end_out(soap)
     || soap_envelope_end_out(soap)
     || soap_end_send(soap))
            return soap->error;
    return soap_closesock(soap);
}

void (*kopano_notify_done)(struct soap *) = [](struct soap *) {};

ECNotificationManager::ECNotificationManager(void)
{
	auto ret = pthread_create(&m_thread, nullptr, Thread, this);
	if (ret != 0) {
		ec_log_err("Could not create ECNotificationManager thread: %s", strerror(ret));
		return;
	}
	m_thread_active = true;
    set_thread_name(m_thread, "NotificationManager");
}

ECNotificationManager::~ECNotificationManager()
{
	ulock_normal l_ses(m_mutexSessions);
	m_bExit = true;
	m_condSessions.notify_all();
	l_ses.unlock();

	ec_log_info("Shutdown notification manager");
	if (m_thread_active)
		pthread_join(m_thread, nullptr);

    // Close and free any pending requests (clients will receive EOF)
	for (const auto &p : m_mapRequests) {
		// we can't call kopano_notify_done here, race condition on shutdown in ECSessionManager vs ECDispatcher
		kopano_end_soap_connection(p.second.soap);
		soap_destroy(p.second.soap);
		soap_end(p.second.soap);
		soap_free(p.second.soap);
    }
}

// Called by the SOAP handler
HRESULT ECNotificationManager::AddRequest(ECSESSIONID ecSessionId, struct soap *soap)
{
    struct soap *lpItem = NULL;
	ulock_normal l_req(m_mutexRequests);
	auto iterRequest = m_mapRequests.find(ecSessionId);
	if (iterRequest != m_mapRequests.cend()) {
        // Hm. There is already a SOAP request waiting for this session id. Apparently a second SOAP connection has now
        // requested notifications. Since this should only happen if the client thinks it has lost its connection and has
        // restarted the request, we will replace the existing request with this one.

		ec_log_warn("Replacing notification request for ID %llu",
			static_cast<unsigned long long>(ecSessionId));

        // Return the previous request as an error
        struct notifyResponse notifications;
        soap_default_notifyResponse(iterRequest->second.soap, &notifications);
        notifications.er = KCERR_NOT_FOUND; // Should be something like 'INTERRUPTED' or something
		if (soapresponse(notifications, iterRequest->second.soap))
			// Handle error on the response
			soap_send_fault(iterRequest->second.soap);
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
	l_req.unlock();
    // There may already be notifications waiting for this session, so post a change on this session so that the
    // thread will attempt to get notifications on this session
    NotifyChange(ecSessionId);
    return hrSuccess;
}

// Called by a session when it has a notification to send
HRESULT ECNotificationManager::NotifyChange(ECSESSIONID ecSessionId)
{
    // Simply mark the session in our set of active sessions
	scoped_lock l_ses(m_mutexSessions);
	m_setActiveSessions.emplace(ecSessionId);
	m_condSessions.notify_all(); /* Wake up thread due to activity */
	return hrSuccess;
}

void * ECNotificationManager::Thread(void *lpParam)
{
	kcsrv_blocksigs();
	return static_cast<ECNotificationManager *>(lpParam)->Work();
}

void *ECNotificationManager::Work() {
    ECSession *lpecSession = NULL;
    struct notifyResponse notifications;
    std::set<ECSESSIONID> setActiveSessions;
    struct soap *lpItem;
    time_t ulNow = 0;

    // Keep looping until we should exit
    while(1) {
		ulock_normal l_ses(m_mutexSessions);
		if (m_bExit)
			break;
		if (m_setActiveSessions.size() == 0)
			m_condSessions.wait_for(l_ses, 1s);

        // Make a copy of the session list so we can release the lock ASAP
        setActiveSessions = m_setActiveSessions;
        m_setActiveSessions.clear();
		l_ses.unlock();

        // Look at all the sessions that have signalled a change
        for (const auto &ses : setActiveSessions) {
            lpItem = NULL;
			ulock_normal l_req(m_mutexRequests);

            // Find the request for the session that had something to say
            auto iterRequest = m_mapRequests.find(ses);
            if (iterRequest != m_mapRequests.cend()) {
                // Reset notification response to default values
                soap_default_notifyResponse(iterRequest->second.soap, &notifications);
                if (g_lpSessionManager->ValidateSession(iterRequest->second.soap, ses, &lpecSession) == erSuccess) {
                    // Get the notifications from the session
					auto er = lpecSession->GetNotifyItems(iterRequest->second.soap, &notifications);

                    if(er == KCERR_NOT_FOUND) {
                        if(time(NULL) - iterRequest->second.ulRequestTime < m_ulTimeout) {
                            // No notifications - this means we have to wait. This can happen if the session was marked active since
                            // the request was just made, and there may have been notifications still waiting for us
							l_req.unlock();
							lpecSession->unlock();
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
					lpecSession->unlock();
                } else {
                    // The session is dead
                    notifications.er = KCERR_END_OF_SESSION;
                }

                // Send the SOAP data
				if (soapresponse(notifications, iterRequest->second.soap))
					// Handle error on the response
					soap_send_fault(iterRequest->second.soap);
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
			l_req.unlock();
            if(lpItem)
                kopano_notify_done(lpItem);
        }

        /* Find all notification requests which have not received any data for m_ulTimeout seconds. This makes sure
         * that the client get a response, even if there are no notifications. Since the client has a hard-coded
         * TCP timeout of 70 seconds, we need to respond well within those 70 seconds. We therefore use a timeout
         * value of 60 seconds here.
         */
		ulock_normal l_req(m_mutexRequests);
        time(&ulNow);
        for (const auto &req : m_mapRequests)
            if (ulNow - req.second.ulRequestTime > m_ulTimeout)
                // Mark the session as active so it will be processed in the next loop
                NotifyChange(req.first);
    }

    return NULL;
}

} /* namespace */
