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

#include <algorithm>
#include <new>
#include <kopano/memory.hpp>
#include <kopano/ECLogger.h>
#include <mapidefs.h>

#include "ECNotifyClient.h"
#include "ECNotifyMaster.h"
#include "ECSessionGroupManager.h"
#include <kopano/stringutil.h>
#include "SOAPUtils.h"
#include "WSTransport.h"
#include <sys/signal.h>
#include <sys/types.h>

#define CALL_MEMBER_FN(object,ptrToMember)  ((object).*(ptrToMember)) 

inline ECNotifySink::ECNotifySink(ECNotifyClient *lpClient, NOTIFYCALLBACK fnCallback)
	: m_lpClient(lpClient)
	, m_fnCallback(fnCallback)
{ }

inline HRESULT ECNotifySink::Notify(ULONG ulConnection,
    const NOTIFYLIST &lNotifications) const
{
	return CALL_MEMBER_FN(*m_lpClient, m_fnCallback)(ulConnection, lNotifications);
}

inline bool ECNotifySink::IsClient(const ECNotifyClient *lpClient) const
{
	return lpClient == m_lpClient;
}

ECNotifyMaster::ECNotifyMaster(SessionGroupData *lpData) :
	m_lpSessionGroupData(lpData /* no addref */)
{
	memset(&m_hThread, 0, sizeof(m_hThread));
}

ECNotifyMaster::~ECNotifyMaster(void)
{
	assert(m_listNotifyClients.empty());
	/* Disable Notifications */
	StopNotifyWatch();
}

HRESULT ECNotifyMaster::Create(SessionGroupData *lpData, ECNotifyMaster **lppMaster)
{
	return alloc_wrap<ECNotifyMaster>(lpData).put(lppMaster);
}

HRESULT ECNotifyMaster::ConnectToSession()
{
	scoped_rlock biglock(m_hMutex);

	/* This function can be called from NotifyWatch, and could race against StopNotifyWatch */
	if (m_bThreadExit)
		return MAPI_E_END_OF_SESSION;

	/*
	 * Cancel connection IO operations before switching Transport.
	 */
	if (m_lpTransport) {
		auto hr = m_lpTransport->HrCancelIO();
		if (hr != hrSuccess)
			return hr;
		m_lpTransport.reset();
	}

	/* Open notification transport */
	return m_lpSessionGroupData->GetTransport(&~m_lpTransport);
}

HRESULT ECNotifyMaster::AddSession(ECNotifyClient* lpClient)
{
	scoped_rlock biglock(m_hMutex);

	m_listNotifyClients.emplace_back(lpClient);
	/* Enable Notifications */
	if (StartNotifyWatch() != hrSuccess)
		assert(false);
	return hrSuccess;
}

HRESULT ECNotifyMaster::ReleaseSession(ECNotifyClient* lpClient)
{
	scoped_rlock biglock(m_hMutex);

	/* Remove all connections attached to client */
	auto iter = m_mapConnections.cbegin();
	while (true) {
		iter = find_if(iter, m_mapConnections.cend(), [lpClient](const NOTIFYCONNECTIONCLIENTMAP::value_type &entry) { return entry.second.IsClient(lpClient); });
		if (iter == m_mapConnections.cend())
			break;
		iter = m_mapConnections.erase(iter);
	}

	/* Remove client from list */
	m_listNotifyClients.remove(lpClient);
	return hrSuccess;
}

HRESULT ECNotifyMaster::ReserveConnection(ULONG *lpulConnection)
{
	*lpulConnection = m_ulConnection++;
	return hrSuccess;
}

HRESULT ECNotifyMaster::ClaimConnection(ECNotifyClient* lpClient, NOTIFYCALLBACK fnCallback, ULONG ulConnection)
{
	scoped_rlock lock(m_hMutex);
	m_mapConnections.emplace(ulConnection, ECNotifySink(lpClient, fnCallback));
	return hrSuccess;
}

HRESULT ECNotifyMaster::DropConnection(ULONG ulConnection)
{
	scoped_rlock lock(m_hMutex);
	m_mapConnections.erase(ulConnection);
	return hrSuccess;
}

HRESULT ECNotifyMaster::StartNotifyWatch()
{
	/* Thread is already running */
	if (m_bThreadRunning)
		return hrSuccess;
	auto hr = ConnectToSession();
	if (hr != hrSuccess)
		return hr;

	/* Make thread joinable which we need during shutdown */
	pthread_attr_t m_hAttrib;
	pthread_attr_init(&m_hAttrib);
	pthread_attr_setdetachstate(&m_hAttrib, PTHREAD_CREATE_JOINABLE);
	/* 1Mb of stack space per thread */
	if (pthread_attr_setstacksize(&m_hAttrib, 1024 * 1024))
		return MAPI_E_CALL_FAILED;
	auto ret = pthread_create(&m_hThread, &m_hAttrib, NotifyWatch, static_cast<void *>(this));
	if (ret != 0) {
		ec_log_err("Could not create ECNotifyMaster watch thread: %s", strerror(ret));
		return MAPI_E_CALL_FAILED;
	}
	pthread_attr_destroy(&m_hAttrib);
	set_thread_name(m_hThread, "NotifyThread");

	m_bThreadRunning = TRUE;
	return hrSuccess;
}

HRESULT ECNotifyMaster::StopNotifyWatch()
{
	KC::object_ptr<WSTransport> lpTransport;
	ulock_rec biglock(m_hMutex, std::defer_lock_t());

	/* Thread was already halted, or connection is broken */
	if (!m_bThreadRunning)
		return hrSuccess;

	/* Let the thread exit during its busy looping */
	biglock.lock();
	m_bThreadExit = TRUE;

	if (m_lpTransport) {
		/* Get another transport so we can tell the server to end the session. We 
		 * can't use our own m_lpTransport since it is probably in a blocking getNextNotify()
		 * call. Seems like a bit of a shame to open a new connection, but there's no
		 * other option */
		auto hr = m_lpTransport->HrClone(&~lpTransport);
		if (hr != hrSuccess) {
			biglock.unlock();
			return hr;
		}
    
		lpTransport->HrLogOff();

		/* Cancel any pending IO if the network transport is down, causing the logoff to fail */
		m_lpTransport->HrCancelIO();
	}
	biglock.unlock();
	if (pthread_join(m_hThread, NULL) != 0)
		ec_log_debug("ECNotifyMaster::StopNotifyWatch: Invalid thread join");

	m_bThreadRunning = FALSE;
	return hrSuccess;
}

void* ECNotifyMaster::NotifyWatch(void *pTmpNotifyMaster)
{
	kcsrv_blocksigs();
	auto pNotifyMaster = static_cast<ECNotifyMaster *>(pTmpNotifyMaster);
	assert(pNotifyMaster != NULL);
	NOTIFYCONNECTIONMAP				mapNotifications;
	notifyResponse					notifications;
	bool							bReconnect = false;

	/* Ignore SIGPIPE which may be caused by HrGetNotify writing to the closed socket */
	signal(SIGPIPE, SIG_IGN);

	while (!pNotifyMaster->m_bThreadExit) {
		notifications = notifyResponse();
		if (pNotifyMaster->m_bThreadExit)
			return nullptr;

		/* 'exitable' sleep before reconnect */
		if (bReconnect) {
			for (ULONG i = 10; i > 0; --i) {
				Sleep(100);
				if (pNotifyMaster->m_bThreadExit)
					return nullptr;
			}
		}

		/*
		 * Request notification (Blocking Call)
		 */
		notificationArray *pNotifyArray = NULL;

		auto hr = pNotifyMaster->m_lpTransport->HrGetNotify(&pNotifyArray);
		if (static_cast<unsigned int>(hr) == KCWARN_CALL_KEEPALIVE) {
			if (bReconnect)
				bReconnect = false;
			continue;
		} else if (hr == MAPI_E_NETWORK_ERROR) {
			bReconnect = true;
			continue;
		} else if (hr != hrSuccess) {
			/*
			 * Session was killed by server, try to start a new login.
			 * This is not a foolproof recovery because 3 things might have happened:
			 *  1) WSTransport has been logged off during StopNotifyWatch().
			 *	2) Notification Session on server has died
			 *  3) SessionGroup on server has died
			 * If (1) m_bThreadExit will be set to TRUE, which means this thread is no longer desired. No
			 * need to make a big deal out of it.
			 * If (2) it is not a disaster (but it is a bad situation), the simple logon should do the trick
			 * of restoring the notification retreival for all sessions for this group. Some notifications
			 * might have arrived later then we might want, but that shouldn't be a total loss (the notificataions
			 * themselves will not have disappeared since they have been queued on the server).
			 * If (3) the problem is that _all_ sessions attached to the server has died and we have lost some
			 * notifications. The main issue however is that a new login for the notification session will not
			 * reanimate the other sessions belonging to this group and neither can we inform all ECNotifyClients
			 * that they now belong to a dead session. A new login is important however, when new sessions are
			 * attached to this group we must ensure that they will get notifications as expected.
			 */
			if (!pNotifyMaster->m_bThreadExit)
				while (pNotifyMaster->ConnectToSession() != hrSuccess &&
				    !pNotifyMaster->m_bThreadExit)
					// On Windows, the ConnectToSession() takes a while .. the windows kernel does 3 connect tries
					// But on linux, this immediately returns a connection error when the server socket is closed
					// so we wait before we try again
					Sleep(1000);

			if (pNotifyMaster->m_bThreadExit)
				return nullptr;
			// We have a new session ID, notify reload
			scoped_rlock lock(pNotifyMaster->m_hMutex);
			for (auto ptr : pNotifyMaster->m_listNotifyClients)
				ptr->NotifyReload();
			continue;
		}

		if (bReconnect)
			bReconnect = false;

		/* This is when the connection is interupted */
		if (pNotifyArray == NULL)
			continue;

		/*
		 * Loop through all notifications and sort them by connection number
		 * with these mappings we can later send all notifications per connection to the appropriate client.
		 */
		for (gsoap_size_t item = 0; item < pNotifyArray->__size; ++item) {
			ULONG ulConnection = pNotifyArray->__ptr[item].ulConnection;

			// No need to do a find before an insert with a default object.
			auto iterNotifications = mapNotifications.emplace(ulConnection, NOTIFYLIST()).first;
			iterNotifications->second.emplace_back(&pNotifyArray->__ptr[item]);
		}

		for (const auto &p : mapNotifications) {
			/*
			 * Check if we have a client registered for this connection
			 * Be careful when locking this, Client->m_hMutex has priority over Master->m_hMutex
			 * which means we should NEVER call a Client function while holding the Master->m_hMutex!
			 */
			scoped_rlock lock(pNotifyMaster->m_hMutex);
			auto iterClient = pNotifyMaster->m_mapConnections.find(p.first);
			if (iterClient == pNotifyMaster->m_mapConnections.cend())
				continue;
			iterClient->second.Notify(p.first, p.second);
			/*
			 * All access to map completed, mutex is unlocked (end
			 * of scope), and send notification to client.
			 */
		}

		/* We're done, clean the map for next round */
		mapNotifications.clear();

		/* Cleanup */
		if (pNotifyArray != NULL) {
			FreeNotificationArrayStruct(pNotifyArray, true);
			pNotifyArray = NULL;
		}
	}
	return NULL;
}

