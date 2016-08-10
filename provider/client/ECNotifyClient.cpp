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
 */
#include <kopano/platform.h>
#include <mapispi.h>
#include <mapix.h>
#include <kopano/ECDebug.h>
#include "ECMsgStore.h"
#include "ECNotifyClient.h"
#include "ECSessionGroupManager.h"
#include <kopano/ECGuid.h>
#include "SOAPUtils.h"
#include "WSUtil.h"
#include <kopano/Util.h>
#include <kopano/stringutil.h>
#include <kopano/mapiext.h>

#define MAX_NOTIFS_PER_CALL 64


static inline std::pair<ULONG,ULONG> SyncAdviseToConnection(const SSyncAdvise &sSyncAdvise) {
	return std::make_pair(sSyncAdvise.sSyncState.ulSyncId,sSyncAdvise.ulConnection);
}

ECNotifyClient::ECNotifyClient(ULONG ulProviderType, void *lpProvider, ULONG ulFlags, LPMAPISUP lpSupport) : ECUnknown("ECNotifyClient")
{
	TRACE_MAPI(TRACE_ENTRY, "ECNotifyClient::ECNotifyClient","");

	ECSESSIONID ecSessionId;

	/* Create a recursive mutex */
	pthread_mutexattr_init(&m_hMutexAttrib);
	pthread_mutexattr_settype(&m_hMutexAttrib, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_hMutex, &m_hMutexAttrib);

	m_lpProvider		= lpProvider;
	m_ulProviderType	= ulProviderType;
	m_lpSupport			= lpSupport;

	if(m_ulProviderType == MAPI_STORE)
		m_lpTransport = ((ECMsgStore*)m_lpProvider)->lpTransport;
	else if(m_ulProviderType == MAPI_ADDRBOOK)
		m_lpTransport = ((ECABLogon*)m_lpProvider)->m_lpTransport;
	else
		ASSERT(FALSE);

    /* Get the sessiongroup ID of the provider that we will be handling notifications for */
	if (m_lpTransport->HrGetSessionId(&ecSessionId, &m_ecSessionGroupId) != hrSuccess)
		ASSERT(FALSE);

    /* Get the session group that this session belongs to */
	if (g_ecSessionManager.GetSessionGroupData(m_ecSessionGroupId, m_lpTransport->GetProfileProps(), &m_lpSessionGroup) != hrSuccess)
		ASSERT(FALSE);

	if (m_lpSessionGroup->GetOrCreateNotifyMaster(&m_lpNotifyMaster) != hrSuccess)
		ASSERT(FALSE);

	m_lpNotifyMaster->AddSession(this);
}

ECNotifyClient::~ECNotifyClient()
{
	TRACE_MAPI(TRACE_ENTRY, "ECNotifyClient::~ECNotifyClient","");

	if (m_lpNotifyMaster)
		m_lpNotifyMaster->ReleaseSession(this);

	if (m_lpSessionGroup)
		m_lpSessionGroup->Release();

    /*
     * We MAY have been the last person using the session group. Tell the session group manager
     * to look at the session group and delete it if necessary
     */
	g_ecSessionManager.DeleteSessionGroupDataIfOrphan(m_ecSessionGroupId);

	pthread_mutex_lock(&m_hMutex);

	/*
	 * Clean up, this map should actually be empty if all advised were correctly unadvised.
	 * This is however not always the case, but ECNotifyMaster and Server will remove all
	 * advises when the session is removed.
	 */
	for (ECMAPADVISE::const_iterator i = m_mapAdvise.begin();
	     i != m_mapAdvise.end(); ++i) {
		if (i->second->lpAdviseSink)
			i->second->lpAdviseSink->Release();

		MAPIFreeBuffer(i->second);
	}
	m_mapAdvise.clear();

	for (ECMAPCHANGEADVISE::const_iterator i = m_mapChangeAdvise.begin();
	     i != m_mapChangeAdvise.end(); ++i) {
		if (i->second->lpAdviseSink != NULL)
			i->second->lpAdviseSink->Release();

		MAPIFreeBuffer(i->second);
	}

	m_mapChangeAdvise.clear();

	pthread_mutex_unlock(&m_hMutex);

	/* Cleanup mutexes */
	pthread_mutex_destroy(&m_hMutex);
	pthread_mutexattr_destroy(&m_hMutexAttrib);

	TRACE_MAPI(TRACE_RETURN, "ECNotifyClient::~ECNotifyClient","");
}

HRESULT ECNotifyClient::Create(ULONG ulProviderType, void *lpProvider, ULONG ulFlags, LPMAPISUP lpSupport, ECNotifyClient**lppNotifyClient)
{
	HRESULT hr			= hrSuccess;

	ECNotifyClient *lpNotifyClient = new ECNotifyClient(ulProviderType, lpProvider, ulFlags, lpSupport);

	hr = lpNotifyClient->QueryInterface(IID_ECNotifyClient, (void **)lppNotifyClient);
	if (hr != hrSuccess)
		delete lpNotifyClient;

	return hr;
}

HRESULT ECNotifyClient::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECNotifyClient, this);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

/**
 * Register an advise connection
 *
 * In windows, registers using the IMAPISupport subscription model, so that threading is handled correctly
 * concerning the multithreading model selected by the client when doing MAPIInitialize(). However, when
 * bSynchronous is TRUE, that notification is always handled internal synchronously while the notifications
 * are being received.
 *
 * @param[in] cbKey Bytes in lpKey
 * @param[in] lpKey Key to subscribe for
 * @param[in] ulEventMask Mask for events to receive
 * @param[in] TRUE if the notification should be handled synchronously, FALSE otherwise. In linux, handled as if
 *                 it were always TRUE
 * @param[in] lpAdviseSink Sink to send notifications to
 * @param[out] lpulConnection Connection ID for the subscription
 * @return result
 */
HRESULT ECNotifyClient::RegisterAdvise(ULONG cbKey, LPBYTE lpKey, ULONG ulEventMask, bool bSynchronous, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection)
{
	HRESULT		hr = MAPI_E_NO_SUPPORT;
	ECADVISE*	pEcAdvise = NULL;
	ULONG		ulConnection = 0;

	if(lpKey == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = MAPIAllocateBuffer(sizeof(ECADVISE), (LPVOID*)&pEcAdvise);
	if (hr != hrSuccess)
		goto exit;

	*lpulConnection = 0;

	memset(pEcAdvise, 0, sizeof(ECADVISE));
	
	pEcAdvise->lpKey = NULL;
	pEcAdvise->cbKey = cbKey;

	hr = MAPIAllocateMore(cbKey, pEcAdvise, (LPVOID*)&pEcAdvise->lpKey);
	if (hr != hrSuccess)
		goto exit;

	memcpy(pEcAdvise->lpKey, lpKey, cbKey);
	
	pEcAdvise->lpAdviseSink	= lpAdviseSink;
	pEcAdvise->ulEventMask	= ulEventMask;
	pEcAdvise->ulSupportConnection = 0;

	/*
	 * Request unique connection id from Master.
	 */
	hr = m_lpNotifyMaster->ReserveConnection(&ulConnection);
	if(hr != hrSuccess)
		goto exit;

	// Add reference on the notify sink
	lpAdviseSink->AddRef();

#ifdef NOTIFY_THROUGH_SUPPORT_OBJECT
	LPNOTIFKEY	lpKeySupport = NULL;

	if(!bSynchronous) {
		hr = MAPIAllocateBuffer(CbNewNOTIFKEY(sizeof(GUID)), (void **)&lpKeySupport);
		if(hr != hrSuccess)
			goto exit;

		lpKeySupport->cb = sizeof(GUID);
		hr = CoCreateGuid((GUID *)lpKeySupport->ab);
		if(hr != hrSuccess)
			goto exit;

		// Get support object connection id
		hr = m_lpSupport->Subscribe(lpKeySupport, (ulEventMask&~fnevLongTermEntryIDs), 0, lpAdviseSink, &pEcAdvise->ulSupportConnection);
		if(hr != hrSuccess)
			goto exit;

		memcpy(&pEcAdvise->guid, lpKeySupport->ab, sizeof(GUID));
	}
#endif

	/* Setup our maps to receive the notifications */
	pthread_mutex_lock(&m_hMutex);

	/* Add notification to map */
	m_mapAdvise.insert( ECMAPADVISE::value_type( ulConnection, pEcAdvise) );
	
	// Release ownership of the mutex object.
	pthread_mutex_unlock(&m_hMutex);

	// Since we're ready to receive notifications now, register ourselves with the master
	hr = m_lpNotifyMaster->ClaimConnection(this, &ECNotifyClient::Notify, ulConnection);
	if(hr != hrSuccess)
		goto exit;

	// Set out value
	*lpulConnection = ulConnection;
exit:
#ifdef NOTIFY_THROUGH_SUPPORT_OBJECT
	MAPIFreeBuffer(lpKeySupport);
#endif
	if (hr != hrSuccess)
		MAPIFreeBuffer(pEcAdvise);

	return hr;
}

HRESULT ECNotifyClient::RegisterChangeAdvise(ULONG ulSyncId, ULONG ulChangeId,
    IECChangeAdviseSink *lpChangeAdviseSink, ULONG *lpulConnection)
{
	HRESULT			hr = MAPI_E_NO_SUPPORT;
	ECCHANGEADVISE*	pEcAdvise = NULL;
	ULONG			ulConnection = 0;

	hr = MAPIAllocateBuffer(sizeof(ECCHANGEADVISE), (LPVOID*)&pEcAdvise);
	if (hr != hrSuccess)
		goto exit;

	*lpulConnection = 0;

	memset(pEcAdvise, 0, sizeof(ECCHANGEADVISE));
	
	pEcAdvise->ulSyncId = ulSyncId;
	pEcAdvise->ulChangeId = ulChangeId;
	pEcAdvise->lpAdviseSink = lpChangeAdviseSink;
	pEcAdvise->ulEventMask = fnevKopanoIcsChange;

	/*
	 * Request unique connection id from Master.
	 */
	hr = m_lpNotifyMaster->ReserveConnection(&ulConnection);
	if(hr != hrSuccess)
		goto exit;

	/*
	 * Setup our maps to receive the notifications
	 */
	pthread_mutex_lock(&m_hMutex);

	// Add reference on the notify sink
	lpChangeAdviseSink->AddRef();

	/* Add notification to map */
	m_mapChangeAdvise.insert( ECMAPCHANGEADVISE::value_type( ulConnection, pEcAdvise) );
	
	// Release ownership of the mutex object.
	pthread_mutex_unlock(&m_hMutex);

	// Since we're ready to receive notifications now, register ourselves with the master
	hr = m_lpNotifyMaster->ClaimConnection(this, &ECNotifyClient::NotifyChange, ulConnection);
	if(hr != hrSuccess)
		goto exit;

	// Set out value
	*lpulConnection = ulConnection;

exit:
	if (hr != hrSuccess)
		MAPIFreeBuffer(pEcAdvise);

	return hr;
}

HRESULT ECNotifyClient::UnRegisterAdvise(ULONG ulConnection)
{
	HRESULT hr;
	ECMAPADVISE::iterator iIterAdvise;
	ECMAPCHANGEADVISE::iterator iIterChangeAdvise;

	/*
	 * Release connection from Master
	 */
	hr = m_lpNotifyMaster->DropConnection(ulConnection);
	if (hr != hrSuccess)
		return hr;

	pthread_mutex_lock(&m_hMutex);

	// Remove notify from list
	iIterAdvise = m_mapAdvise.find(ulConnection);
	if (iIterAdvise != m_mapAdvise.end()) {
		if(iIterAdvise->second->ulSupportConnection)
			m_lpSupport->Unsubscribe(iIterAdvise->second->ulSupportConnection);

		if (iIterAdvise->second->lpAdviseSink != NULL)
			iIterAdvise->second->lpAdviseSink->Release();

		MAPIFreeBuffer(iIterAdvise->second);
		m_mapAdvise.erase(iIterAdvise);	
	} else {
		iIterChangeAdvise = m_mapChangeAdvise.find(ulConnection);
		if (iIterChangeAdvise != m_mapChangeAdvise.end()) {
			if (iIterChangeAdvise->second->lpAdviseSink != NULL)
				iIterChangeAdvise->second->lpAdviseSink->Release();

			MAPIFreeBuffer(iIterChangeAdvise->second);
			m_mapChangeAdvise.erase(iIterChangeAdvise);
		} else {
			//ASSERT(FALSE);	
		}
	}
		
	// Release ownership of the mutex object.
	pthread_mutex_unlock(&m_hMutex);
	return hr;
}

HRESULT ECNotifyClient::Advise(ULONG cbKey, LPBYTE lpKey, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection){

	TRACE_NOTIFY(TRACE_ENTRY, "ECNotifyClient::Advise", "");

	HRESULT		hr = MAPI_E_NO_SUPPORT;
	ULONG		ulConnection = 0;

	
	hr = RegisterAdvise(cbKey, lpKey, ulEventMask, false, lpAdviseSink, &ulConnection);
	if (hr != hrSuccess)
		goto exit;

	//Request the advice
	hr = m_lpTransport->HrSubscribe(cbKey, lpKey, ulConnection, ulEventMask);
	if(hr != hrSuccess) {
		UnRegisterAdvise(ulConnection);
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	} 
	
	// Set out value
	*lpulConnection = ulConnection;
	hr = hrSuccess;

exit:

	TRACE_NOTIFY(TRACE_RETURN, "ECNotifyClient::Advise", "hr=0x%08X connection=%d", hr, *lpulConnection);

	return hr;
}

HRESULT ECNotifyClient::Advise(const ECLISTSYNCSTATE &lstSyncStates,
    IECChangeAdviseSink *lpChangeAdviseSink, ECLISTCONNECTION *lplstConnections)
{
	TRACE_NOTIFY(TRACE_ENTRY, "ECNotifyClient::AdviseICS", "");

	HRESULT				hr = MAPI_E_NO_SUPPORT;
	ECLISTSYNCADVISE	lstAdvises;

	ECLISTSYNCSTATE::const_iterator iSyncState;
	ECLISTSYNCADVISE::const_iterator iSyncAdvise;
	ECLISTSYNCADVISE::const_iterator iSyncUnadvise;

	for (iSyncState = lstSyncStates.begin(); iSyncState != lstSyncStates.end(); ++iSyncState) {
		SSyncAdvise sSyncAdvise = {{0}};

		hr = RegisterChangeAdvise(iSyncState->ulSyncId, iSyncState->ulChangeId, lpChangeAdviseSink, &sSyncAdvise.ulConnection);
		if (hr != hrSuccess)
			goto exit;

		sSyncAdvise.sSyncState = *iSyncState;
		lstAdvises.push_back(sSyncAdvise);
	}

	hr = m_lpTransport->HrSubscribeMulti(lstAdvises, fnevKopanoIcsChange);
	if (hr != hrSuccess) {
		// On failure we'll try the one-at-a-time approach.
		for (iSyncAdvise = lstAdvises.begin(); iSyncAdvise != lstAdvises.end(); ++iSyncAdvise) {
			hr = m_lpTransport->HrSubscribe(iSyncAdvise->sSyncState.ulSyncId, iSyncAdvise->sSyncState.ulChangeId, iSyncAdvise->ulConnection, fnevKopanoIcsChange);
			if (hr != hrSuccess) {
				// Unadvise all advised connections
				// No point in attempting the multi version as SubscribeMulti also didn't work
				for (iSyncUnadvise = lstAdvises.begin(); iSyncUnadvise != iSyncAdvise; ++iSyncUnadvise)
					m_lpTransport->HrUnSubscribe(iSyncUnadvise->ulConnection);
				
				hr = MAPI_E_NO_SUPPORT;
				goto exit;
			} 
		}
	}

	std::transform(lstAdvises.begin(), lstAdvises.end(), std::back_inserter(*lplstConnections), &SyncAdviseToConnection);

exit:
	if (hr != hrSuccess) {
		// Unregister all advises.
		for (iSyncAdvise = lstAdvises.begin(); iSyncAdvise != lstAdvises.end(); ++iSyncAdvise)
			UnRegisterAdvise(iSyncAdvise->ulConnection);
	}

	TRACE_NOTIFY(TRACE_RETURN, "ECNotifyClient::AdviseICS", "hr=0x%08X", hr);
	return hr;
}

HRESULT ECNotifyClient::Unadvise(ULONG ulConnection)
{
	TRACE_NOTIFY(TRACE_ENTRY, "ECNotifyClient::Unadvise", "%d", ulConnection);

	HRESULT hr	= MAPI_E_NO_SUPPORT;

	// Logoff the advisor
	hr = m_lpTransport->HrUnSubscribe(ulConnection);
	if (hr != hrSuccess)
		goto exit;

	hr = UnRegisterAdvise(ulConnection);
	if (hr != hrSuccess)
		goto exit;

exit:
	TRACE_NOTIFY(TRACE_RETURN, "ECNotifyClient::Unadvise", "hr=0x%08X", hr);

	return hr;
}

HRESULT ECNotifyClient::Unadvise(const ECLISTCONNECTION &lstConnections)
{
	TRACE_NOTIFY(TRACE_ENTRY, "ECNotifyClient::Unadvise", "");

	HRESULT hr	= MAPI_E_NO_SUPPORT;
	HRESULT hrTmp;
	ECLISTCONNECTION::const_iterator iConnection;
	bool bWithErrors = false;

	// Logoff the advisors
	hr = m_lpTransport->HrUnSubscribeMulti(lstConnections);
	if (hr != hrSuccess) {
		hr = hrSuccess;

		for (iConnection = lstConnections.begin(); iConnection != lstConnections.end(); ++iConnection) {
			hrTmp = m_lpTransport->HrUnSubscribe(iConnection->second);
			if (FAILED(hrTmp))
				bWithErrors = true;
		}
	}

	for (iConnection = lstConnections.begin(); iConnection != lstConnections.end(); ++iConnection) {
		hrTmp = UnRegisterAdvise(iConnection->second);
		if (FAILED(hrTmp))
			bWithErrors = true;
	}

	if (SUCCEEDED(hr) && bWithErrors)
		hr = MAPI_W_ERRORS_RETURNED;

	TRACE_NOTIFY(TRACE_RETURN, "ECNotifyClient::Unadvise", "hr=0x%08X", hr);

	return hr;
}

// Re-registers a notification on the server. Normally only called if the server
// session has been reset.
HRESULT ECNotifyClient::Reregister(ULONG ulConnection, ULONG cbKey, LPBYTE lpKey)
{
	HRESULT hr = hrSuccess;
	ECMAPADVISE::const_iterator iter;

	pthread_mutex_lock(&m_hMutex);

	iter = m_mapAdvise.find(ulConnection);
	if(iter == m_mapAdvise.end()) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	if(cbKey) {
		// Update key if required, when the new key is equal or smaller
		// then the previous key we don't need to allocate anything.
		// Note that we cannot do MAPIFreeBuffer() since iter->second->lpKey
		// was allocated with MAPIAllocateMore().
		if (cbKey > iter->second->cbKey) {
			hr = MAPIAllocateMore(cbKey, iter->second, (void **)&iter->second->lpKey);
			if (hr != hrSuccess)
				goto exit;
		}

		memcpy(iter->second->lpKey, lpKey, cbKey);
		iter->second->cbKey = cbKey;
	}

	hr = m_lpTransport->HrSubscribe(iter->second->cbKey, iter->second->lpKey, ulConnection, iter->second->ulEventMask);

exit:
	pthread_mutex_unlock(&m_hMutex);

	return hr;
}


HRESULT ECNotifyClient::ReleaseAll()
{
	HRESULT hr			= hrSuccess;
	ECMAPADVISE::const_iterator iIterAdvise;

	pthread_mutex_lock(&m_hMutex);

	for (iIterAdvise = m_mapAdvise.begin(); iIterAdvise != m_mapAdvise.end(); ++iIterAdvise) {
		iIterAdvise->second->lpAdviseSink->Release();
		iIterAdvise->second->lpAdviseSink = NULL;
	}

	pthread_mutex_unlock(&m_hMutex);

	return hr;
}

typedef std::list<LPNOTIFICATION> NOTIFICATIONLIST;
typedef std::list<LPSBinary> BINARYLIST;

HRESULT ECNotifyClient::NotifyReload()
{
	HRESULT hr = hrSuccess;
	ECMAPADVISE::const_iterator iterAdvise;
	struct notification notif;
	struct notificationTable table;
	NOTIFYLIST notifications;

	memset(&notif, 0, sizeof(notif));
	memset(&table, 0, sizeof(table));

	notif.ulEventType = fnevTableModified;
	notif.tab = &table;
	notif.tab->ulTableEvent = TABLE_RELOAD;
	
	notifications.push_back(&notif);


	// The transport used for this notifyclient *may* have a broken session. Inform the
	// transport that the session may be broken and it should verify that all is well.

	// Disabled because deadlock, research needed
	//m_lpTransport->HrEnsureSession();

	// Don't send the notification while we are locked
	pthread_mutex_lock(&m_hMutex);
	for (iterAdvise = m_mapAdvise.begin(); iterAdvise != m_mapAdvise.end(); ++iterAdvise)
		if (iterAdvise->second->cbKey == 4)
			Notify(iterAdvise->first, notifications);
	pthread_mutex_unlock(&m_hMutex);

	return hr;
}


HRESULT ECNotifyClient::Notify(ULONG ulConnection, const NOTIFYLIST &lNotifications)
{
	HRESULT						hr = hrSuccess;
	LPNOTIFICATION				lpNotifs = NULL;
	NOTIFYLIST::const_iterator	iterNotify;
	ECMAPADVISE::const_iterator iterAdvise;
	NOTIFICATIONLIST			notifications;
	NOTIFICATIONLIST::const_iterator iterNotification;

	for (iterNotify = lNotifications.begin();
	     iterNotify != lNotifications.end(); ++iterNotify) {
		LPNOTIFICATION tmp = NULL;

		hr = CopySOAPNotificationToMAPINotification(m_lpProvider, *iterNotify, &tmp);
		if (hr != hrSuccess)
			continue;

		TRACE_NOTIFY(TRACE_ENTRY, "ECNotifyClient::Notify", "id=%d\n%s", (*iterNotify)->ulConnection, NotificationToString(1, tmp).c_str());
		notifications.push_back(tmp);
	}

	pthread_mutex_lock(&m_hMutex);

	/* Search for the right connection */
	iterAdvise = m_mapAdvise.find(ulConnection);

	if (iterAdvise == m_mapAdvise.end() || iterAdvise->second->lpAdviseSink == NULL)	
	{
		TRACE_NOTIFY(TRACE_WARNING, "ECNotifyClient::Notify", "Unknown Notification id %d", ulConnection);
		pthread_mutex_unlock(&m_hMutex);
		goto exit;
	}

	if (!notifications.empty()) {
		/* Send notifications in batches of MAX_NOTIFS_PER_CALL notifications */
		iterNotification = notifications.begin();
		while (iterNotification != notifications.end()) {
			/* Create a straight array of all the notifications */
			hr = MAPIAllocateBuffer(sizeof(NOTIFICATION) * MAX_NOTIFS_PER_CALL, (void **)&lpNotifs);
			if (hr != hrSuccess)
				continue;

			ULONG i = 0;
			while (iterNotification != notifications.end() && i < MAX_NOTIFS_PER_CALL) {
				/* We can do a straight memcpy here because pointers are still intact */
				memcpy(&lpNotifs[i++], *iterNotification, sizeof(NOTIFICATION));
				++iterNotification;
			}

			/* Send notification to the listener */
			if (!iterAdvise->second->ulSupportConnection) {
				if (iterAdvise->second->lpAdviseSink->OnNotify(i, lpNotifs) != 0)
					TRACE_NOTIFY(TRACE_WARNING, "ECNotifyClient::Notify", "Error by notify a client");
			} else {
				LPNOTIFKEY	lpKey = NULL;
				ULONG		ulResult = 0;

				hr = MAPIAllocateBuffer(CbNewNOTIFKEY(sizeof(GUID)), (void **)&lpKey);
				if (hr != hrSuccess) {
					pthread_mutex_unlock(&m_hMutex);
					goto exit;
				}

				lpKey->cb = sizeof(GUID);
				memcpy(lpKey->ab, &iterAdvise->second->guid, sizeof(GUID));

				// FIXME log errors
				m_lpSupport->Notify(lpKey, i, lpNotifs, &ulResult);

				MAPIFreeBuffer(lpKey);
				lpKey = NULL;
			}

			MAPIFreeBuffer(lpNotifs);
			lpNotifs = NULL;
		}
	}

	pthread_mutex_unlock(&m_hMutex);

exit:
	MAPIFreeBuffer(lpNotifs);

	/* Release all notifications */
	for (iterNotification = notifications.begin();
	     iterNotification != notifications.end(); ++iterNotification)
		MAPIFreeBuffer(*iterNotification);

	return hr;
}

HRESULT ECNotifyClient::NotifyChange(ULONG ulConnection, const NOTIFYLIST &lNotifications)
{
	HRESULT						hr = hrSuccess;
	LPENTRYLIST					lpSyncStates = NULL;
	NOTIFYLIST::const_iterator	iterNotify;
	ECMAPCHANGEADVISE::const_iterator iterAdvise;
	BINARYLIST					syncStates;
	BINARYLIST::const_iterator iterSyncStates;

	/* Create a straight array of MAX_NOTIFS_PER_CALL sync states */
	hr = MAPIAllocateBuffer(sizeof *lpSyncStates, (void**)&lpSyncStates);
	if (hr != hrSuccess)
		goto exit;
	memset(lpSyncStates, 0, sizeof *lpSyncStates);

	hr = MAPIAllocateMore(sizeof *lpSyncStates->lpbin * MAX_NOTIFS_PER_CALL, lpSyncStates, (void**)&lpSyncStates->lpbin);
	if (hr != hrSuccess)
		goto exit;
	memset(lpSyncStates->lpbin, 0, sizeof *lpSyncStates->lpbin * MAX_NOTIFS_PER_CALL);

	for (iterNotify = lNotifications.begin();
	     iterNotify != lNotifications.end(); ++iterNotify) {
		LPSBinary	tmp = NULL;

		hr = CopySOAPChangeNotificationToSyncState(*iterNotify, &tmp, lpSyncStates);
		if (hr != hrSuccess)
			continue;

		TRACE_NOTIFY(TRACE_ENTRY, "ECNotifyClient::NotifyChange", "id=%d\n%s", (*iterNotify)->ulConnection, bin2hex(tmp->cb, tmp->lpb).c_str());
		syncStates.push_back(tmp);
	}

	pthread_mutex_lock(&m_hMutex);

	/* Search for the right connection */
	iterAdvise = m_mapChangeAdvise.find(ulConnection);

	if (iterAdvise == m_mapChangeAdvise.end() || iterAdvise->second->lpAdviseSink == NULL)	
	{
		TRACE_NOTIFY(TRACE_WARNING, "ECNotifyClient::NotifyChange", "Unknown Notification id %d", ulConnection);
		pthread_mutex_unlock(&m_hMutex);
		goto exit;
	}

	if (!syncStates.empty()) {
		/* Send notifications in batches of MAX_NOTIFS_PER_CALL notifications */
		iterSyncStates = syncStates.begin();
		while (iterSyncStates != syncStates.end()) {

			lpSyncStates->cValues = 0;
			while (iterSyncStates != syncStates.end() && lpSyncStates->cValues < MAX_NOTIFS_PER_CALL) {
				/* We can do a straight memcpy here because pointers are still intact */
				memcpy(&lpSyncStates->lpbin[lpSyncStates->cValues++], *iterSyncStates, sizeof *lpSyncStates->lpbin);
				++iterSyncStates;
			}

			/* Send notification to the listener */
			if (iterAdvise->second->lpAdviseSink->OnNotify(0, lpSyncStates) != 0)
				TRACE_NOTIFY(TRACE_WARNING, "ECNotifyClient::NotifyChange", "Error by notify a client");

		}
	}

	pthread_mutex_unlock(&m_hMutex);

exit:
	MAPIFreeBuffer(lpSyncStates);
	return hrSuccess;
}

HRESULT ECNotifyClient::UpdateSyncStates(const ECLISTSYNCID &lstSyncId, ECLISTSYNCSTATE *lplstSyncState)
{
	return m_lpTransport->HrGetSyncStates(lstSyncId, lplstSyncState);
}
