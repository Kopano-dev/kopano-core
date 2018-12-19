/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <new>
#include <stdexcept>
#include <utility>
#include <kopano/memory.hpp>
#include <mapispi.h>
#include <mapix.h>
#include <kopano/ECLogger.h>
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

using namespace KC;

struct ECADVISE {
	unsigned int cbKey = 0, ulEventMask = 0;
	unsigned int ulConnection = 0, ulSupportConnection = 0;
	memory_ptr<BYTE> lpKey;
	object_ptr<IMAPIAdviseSink> lpAdviseSink;
	GUID guid{};
};

struct ECCHANGEADVISE {
	unsigned int ulSyncId = 0, ulChangeId = 0;
	unsigned int ulEventMask = 0, ulConnection = 0;
	object_ptr<IECChangeAdviseSink> lpAdviseSink;
	GUID guid{};
};

static inline std::pair<ULONG,ULONG> SyncAdviseToConnection(const SSyncAdvise &sSyncAdvise) {
	return {sSyncAdvise.sSyncState.ulSyncId, sSyncAdvise.ulConnection};
}

ECNotifyClient::ECNotifyClient(ULONG ulProviderType, void *lpProvider,
    ULONG ulFlags, LPMAPISUP lpSupport) :
	ECUnknown("ECNotifyClient"), m_lpSupport(lpSupport),
	m_lpProvider(lpProvider), m_ulProviderType(ulProviderType)
{
	ECSESSIONID ecSessionId;

	if(m_ulProviderType == MAPI_STORE)
		m_lpTransport.reset(static_cast<ECMsgStore *>(m_lpProvider)->lpTransport);
	else if(m_ulProviderType == MAPI_ADDRBOOK)
		m_lpTransport.reset(static_cast<ECABLogon *>(m_lpProvider)->m_lpTransport);
	else
		throw std::runtime_error("Unknown m_ulProviderType");

    /* Get the sessiongroup ID of the provider that we will be handling notifications for */
	if (m_lpTransport->HrGetSessionId(&ecSessionId, &m_ecSessionGroupId) != hrSuccess)
		throw std::runtime_error("ECNotifyClient/HrGetSessionId failed");
    /* Get the session group that this session belongs to */
	if (g_ecSessionManager.GetSessionGroupData(m_ecSessionGroupId, m_lpTransport->GetProfileProps(), &~m_lpSessionGroup) != hrSuccess)
		throw std::runtime_error("ECNotifyClient/GetSessionGroupData failed");
	if (m_lpSessionGroup->GetOrCreateNotifyMaster(&m_lpNotifyMaster) != hrSuccess)
		throw std::runtime_error("ECNotifyClient/GetOrCreateNotifyMaster failed");
	m_lpNotifyMaster->AddSession(this);
}

ECNotifyClient::~ECNotifyClient()
{
	if (m_lpNotifyMaster)
		m_lpNotifyMaster->ReleaseSession(this);
	m_lpSessionGroup.reset();
    /*
     * We MAY have been the last person using the session group. Tell the session group manager
     * to look at the session group and delete it if necessary
     */
	g_ecSessionManager.DeleteSessionGroupDataIfOrphan(m_ecSessionGroupId);
	/*
	 * Clean up, this map should actually be empty if all advised were correctly unadvised.
	 * This is however not always the case, but ECNotifyMaster and Server will remove all
	 * advises when the session is removed.
	 */
	ulock_rec biglock(m_hMutex);
	m_mapAdvise.clear();
	m_mapChangeAdvise.clear();
}

HRESULT ECNotifyClient::Create(ULONG ulProviderType, void *lpProvider, ULONG ulFlags, LPMAPISUP lpSupport, ECNotifyClient**lppNotifyClient)
{
	return alloc_wrap<ECNotifyClient>(ulProviderType, lpProvider, ulFlags,
	       lpSupport).put(lppNotifyClient);
}

HRESULT ECNotifyClient::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECNotifyClient, this);
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
	if (lpKey == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ULONG		ulConnection = 0;
	auto pEcAdvise = make_unique_nt<ECADVISE>();
	if (pEcAdvise == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	*lpulConnection = 0;
	pEcAdvise->lpKey = NULL;
	pEcAdvise->cbKey = cbKey;
	auto hr = KAllocCopy(lpKey, cbKey, &~pEcAdvise->lpKey);
	if (hr != hrSuccess)
		return hr;
	pEcAdvise->lpAdviseSink.reset(lpAdviseSink);
	pEcAdvise->ulEventMask = ulEventMask;
	/*
	 * Request unique connection id from Master.
	 */
	hr = m_lpNotifyMaster->ReserveConnection(&ulConnection);
	if(hr != hrSuccess)
		return hr;

#ifdef NOTIFY_THROUGH_SUPPORT_OBJECT
	memory_ptr<NOTIFKEY> lpKeySupport;

	if(!bSynchronous) {
		hr = MAPIAllocateBuffer(CbNewNOTIFKEY(sizeof(GUID)), &~lpKeySupport);
		if(hr != hrSuccess)
			return hr;
		lpKeySupport->cb = sizeof(GUID);
		hr = CoCreateGuid(reinterpret_cast<GUID *>(lpKeySupport->ab));
		if(hr != hrSuccess)
			return hr;
		// Get support object connection id
		hr = m_lpSupport->Subscribe(lpKeySupport, (ulEventMask&~fnevLongTermEntryIDs), 0, lpAdviseSink, &pEcAdvise->ulSupportConnection);
		if(hr != hrSuccess)
			return hr;
		memcpy(&pEcAdvise->guid, lpKeySupport->ab, sizeof(GUID));
	}
#endif

	{
		scoped_rlock biglock(m_hMutex);
		m_mapAdvise.emplace(ulConnection, std::move(pEcAdvise));
	}

	// Since we're ready to receive notifications now, register ourselves with the master
	hr = m_lpNotifyMaster->ClaimConnection(this, &ECNotifyClient::Notify, ulConnection);
	if(hr != hrSuccess)
		return hr;
	// Set out value
	*lpulConnection = ulConnection;
	return hrSuccess;
}

HRESULT ECNotifyClient::RegisterChangeAdvise(ULONG ulSyncId, ULONG ulChangeId,
    IECChangeAdviseSink *lpChangeAdviseSink, ULONG *lpulConnection)
{
	ULONG			ulConnection = 0;
	auto pEcAdvise = make_unique_nt<ECCHANGEADVISE>();
	if (pEcAdvise == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	*lpulConnection = 0;
	pEcAdvise->ulSyncId = ulSyncId;
	pEcAdvise->ulChangeId = ulChangeId;
	pEcAdvise->lpAdviseSink.reset(lpChangeAdviseSink);
	pEcAdvise->ulEventMask = fnevKopanoIcsChange;

	/*
	 * Request unique connection id from Master.
	 */
	auto hr = m_lpNotifyMaster->ReserveConnection(&ulConnection);
	if(hr != hrSuccess)
		return hr;
	/*
	 * Setup our maps to receive the notifications
	 */
	{
		scoped_rlock biglock(m_hMutex);
		m_mapChangeAdvise.emplace(ulConnection, std::move(pEcAdvise));
	}

	// Since we're ready to receive notifications now, register ourselves with the master
	hr = m_lpNotifyMaster->ClaimConnection(this, &ECNotifyClient::NotifyChange, ulConnection);
	if(hr != hrSuccess)
		return hr;
	// Set out value
	*lpulConnection = ulConnection;
	return hrSuccess;
}

HRESULT ECNotifyClient::UnRegisterAdvise(ULONG ulConnection)
{
	/*
	 * Release connection from Master
	 */
	HRESULT hr = m_lpNotifyMaster->DropConnection(ulConnection);
	if (hr != hrSuccess)
		return hr;

	// Remove notify from list
	scoped_rlock lock(m_hMutex);
	auto iIterAdvise = m_mapAdvise.find(ulConnection);
	if (iIterAdvise != m_mapAdvise.cend()) {
		if(iIterAdvise->second->ulSupportConnection)
			m_lpSupport->Unsubscribe(iIterAdvise->second->ulSupportConnection);
		m_mapAdvise.erase(iIterAdvise);
		return hr;
	}
	auto iIterChangeAdvise = m_mapChangeAdvise.find(ulConnection);
	if (iIterChangeAdvise == m_mapChangeAdvise.cend())
		return hr;
	m_mapChangeAdvise.erase(iIterChangeAdvise);
	return hr;
}

HRESULT ECNotifyClient::Advise(ULONG cbKey, LPBYTE lpKey, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection){
	ULONG		ulConnection = 0;
	auto hr = RegisterAdvise(cbKey, lpKey, ulEventMask, false, lpAdviseSink, &ulConnection);
	if (hr != hrSuccess)
		return hr;

	//Request the advice
	hr = m_lpTransport->HrSubscribe(cbKey, lpKey, ulConnection, ulEventMask);
	if(hr != hrSuccess) {
		UnRegisterAdvise(ulConnection);
		return MAPI_E_NO_SUPPORT;
	}
	// Set out value
	*lpulConnection = ulConnection;
	return hrSuccess;
}

HRESULT ECNotifyClient::Advise(const ECLISTSYNCSTATE &lstSyncStates,
    IECChangeAdviseSink *lpChangeAdviseSink, ECLISTCONNECTION *lplstConnections)
{
	HRESULT				hr = MAPI_E_NO_SUPPORT;
	ECLISTSYNCADVISE	lstAdvises;

	for (const auto &state : lstSyncStates) {
		SSyncAdvise sSyncAdvise = {{0}};

		hr = RegisterChangeAdvise(state.ulSyncId, state.ulChangeId, lpChangeAdviseSink, &sSyncAdvise.ulConnection);
		if (hr != hrSuccess)
			goto exit;
		sSyncAdvise.sSyncState = state;
		lstAdvises.emplace_back(std::move(sSyncAdvise));
	}

	hr = m_lpTransport->HrSubscribeMulti(lstAdvises, fnevKopanoIcsChange);
	if (hr != hrSuccess) {
		// On failure we'll try the one-at-a-time approach.
		for (auto iSyncAdvise = lstAdvises.cbegin();
		     iSyncAdvise != lstAdvises.cend(); ++iSyncAdvise) {
			hr = m_lpTransport->HrSubscribe(iSyncAdvise->sSyncState.ulSyncId, iSyncAdvise->sSyncState.ulChangeId, iSyncAdvise->ulConnection, fnevKopanoIcsChange);
			if (hr != hrSuccess) {
				// Unadvise all advised connections
				// No point in attempting the multi version as SubscribeMulti also didn't work
				for (auto iSyncUnadvise = lstAdvises.cbegin();
				     iSyncUnadvise != iSyncAdvise; ++iSyncUnadvise)
					m_lpTransport->HrUnSubscribe(iSyncUnadvise->ulConnection);

				hr = MAPI_E_NO_SUPPORT;
				goto exit;
			}
		}
	}

	std::transform(lstAdvises.begin(), lstAdvises.end(), std::back_inserter(*lplstConnections), &SyncAdviseToConnection);

exit:
	if (hr != hrSuccess)
		// Unregister all advises.
		for (auto iSyncAdvise = lstAdvises.cbegin();
		     iSyncAdvise != lstAdvises.cend(); ++iSyncAdvise)
			UnRegisterAdvise(iSyncAdvise->ulConnection);
	return hr;
}

HRESULT ECNotifyClient::Unadvise(ULONG ulConnection)
{
	// Logoff the advisor
	auto hr = m_lpTransport->HrUnSubscribe(ulConnection);
	if (hr != hrSuccess)
		return hr;
	return UnRegisterAdvise(ulConnection);
}

HRESULT ECNotifyClient::Unadvise(const ECLISTCONNECTION &lstConnections)
{
	bool bWithErrors = false;

	// Logoff the advisors
	auto hr = m_lpTransport->HrUnSubscribeMulti(lstConnections);
	if (hr != hrSuccess) {
		hr = hrSuccess;

		for (const auto &p : lstConnections) {
			auto hrTmp = m_lpTransport->HrUnSubscribe(p.second);
			if (FAILED(hrTmp))
				bWithErrors = true;
		}
	}
	for (const auto &p : lstConnections) {
		auto hrTmp = UnRegisterAdvise(p.second);
		if (FAILED(hrTmp))
			bWithErrors = true;
	}

	if (SUCCEEDED(hr) && bWithErrors)
		hr = MAPI_W_ERRORS_RETURNED;
	return hr;
}

// Re-registers a notification on the server. Normally only called if the server
// session has been reset.
HRESULT ECNotifyClient::Reregister(ULONG ulConnection, ULONG cbKey, LPBYTE lpKey)
{
	scoped_rlock biglock(m_hMutex);

	ECMAPADVISE::const_iterator iter = m_mapAdvise.find(ulConnection);
	if (iter == m_mapAdvise.cend())
		return MAPI_E_NOT_FOUND;

	if(cbKey) {
		/*
		 * Update key if required. When the new key is equal or smaller
		 * than the previous key, we do not need to allocate anything.
		 */
		if (cbKey > iter->second->cbKey) {
			memory_ptr<BYTE> newkey;
			auto hr = MAPIAllocateBuffer(cbKey, &~newkey);
			if (hr != hrSuccess)
				return hr;
			iter->second->lpKey.reset(newkey);
		}

		memcpy(iter->second->lpKey, lpKey, cbKey);
		iter->second->cbKey = cbKey;
	}
	return m_lpTransport->HrSubscribe(iter->second->cbKey,
	       iter->second->lpKey, ulConnection, iter->second->ulEventMask);
}

HRESULT ECNotifyClient::ReleaseAll()
{
	scoped_rlock biglock(m_hMutex);
	for (auto &p : m_mapAdvise)
		p.second->lpAdviseSink.reset();
	return hrSuccess;
}

typedef std::list<NOTIFICATION *> NOTIFICATIONLIST;
typedef std::list<SBinary *> BINARYLIST;

HRESULT ECNotifyClient::NotifyReload()
{
	struct notification notif;
	struct notificationTable table;
	NOTIFYLIST notifications;

	memset(&table, 0, sizeof(table));
	notif.ulEventType = fnevTableModified;
	notif.tab = &table;
	notif.tab->ulTableEvent = TABLE_RELOAD;
	notifications.emplace_back(&notif);

	// The transport used for this notifyclient *may* have a broken session. Inform the
	// transport that the session may be broken and it should verify that all is well.

	// Disabled because deadlock, research needed
	//m_lpTransport->HrEnsureSession();

	// Don't send the notification while we are locked
	scoped_rlock biglock(m_hMutex);
	for (const auto &p : m_mapAdvise)
		if (p.second->cbKey == 4)
			Notify(p.first, notifications);
	return hrSuccess;
}

HRESULT ECNotifyClient::Notify(ULONG ulConnection, const NOTIFYLIST &lNotifications)
{
	HRESULT						hr = hrSuccess;
	NOTIFICATIONLIST			notifications;

	for (auto notp : lNotifications) {
		LPNOTIFICATION tmp = NULL;
		auto ret = CopySOAPNotificationToMAPINotification(m_lpProvider, notp, &tmp);
		if (ret != hrSuccess)
			continue;
		notifications.emplace_back(tmp);
	}

	ulock_rec biglock(m_hMutex);

	/* Search for the right connection */
	auto iterAdvise = m_mapAdvise.find(ulConnection);
	if (iterAdvise == m_mapAdvise.cend() ||
	    iterAdvise->second->lpAdviseSink == nullptr)
		goto exit;

	if (!notifications.empty()) {
		/* Send notifications in batches of MAX_NOTIFS_PER_CALL notifications */
		auto iterNotification = notifications.cbegin();
		while (iterNotification != notifications.cend()) {
			memory_ptr<NOTIFICATION> lpNotifs;
			/* Create a straight array of all the notifications */
			auto ret = MAPIAllocateBuffer(sizeof(NOTIFICATION) * MAX_NOTIFS_PER_CALL, &~lpNotifs);
			if (ret != hrSuccess)
				continue;

			ULONG i = 0;
			while (iterNotification != notifications.cend() && i < MAX_NOTIFS_PER_CALL) {
				/* We can do a straight memcpy here because pointers are still intact */
				memcpy(&lpNotifs[i++], *iterNotification, sizeof(NOTIFICATION));
				++iterNotification;
			}

			/* Send notification to the listener */
			if (!iterAdvise->second->ulSupportConnection) {
				if (iterAdvise->second->lpAdviseSink->OnNotify(i, lpNotifs) != 0)
					ec_log_debug("ECNotifyClient::Notify: Error by notify a client");
				continue;
			}

			memory_ptr<NOTIFKEY> lpKey;
			ULONG ulResult = 0;
			hr = MAPIAllocateBuffer(CbNewNOTIFKEY(sizeof(GUID)), &~lpKey);
			if (hr != hrSuccess)
				goto exit;
			lpKey->cb = sizeof(GUID);
			memcpy(lpKey->ab, &iterAdvise->second->guid, sizeof(GUID));
			// FIXME log errors
			m_lpSupport->Notify(lpKey, i, lpNotifs, &ulResult);
		}
	}
exit:
	biglock.unlock();
	/* Release all notifications */
	for (auto notp : notifications)
		MAPIFreeBuffer(notp);
	return hr;
}

HRESULT ECNotifyClient::NotifyChange(ULONG ulConnection, const NOTIFYLIST &lNotifications)
{
	memory_ptr<ENTRYLIST> lpSyncStates;
	BINARYLIST					syncStates;
	ulock_rec biglock(m_hMutex, std::defer_lock_t());

	/* Create a straight array of MAX_NOTIFS_PER_CALL sync states */
	auto hr = MAPIAllocateBuffer(sizeof(*lpSyncStates), &~lpSyncStates);
	if (hr != hrSuccess)
		return hr;
	memset(lpSyncStates, 0, sizeof *lpSyncStates);

	hr = MAPIAllocateMore(sizeof *lpSyncStates->lpbin * MAX_NOTIFS_PER_CALL, lpSyncStates, (void**)&lpSyncStates->lpbin);
	if (hr != hrSuccess)
		return hr;
	memset(lpSyncStates->lpbin, 0, sizeof *lpSyncStates->lpbin * MAX_NOTIFS_PER_CALL);

	for (auto notp : lNotifications) {
		LPSBinary	tmp = NULL;

		hr = CopySOAPChangeNotificationToSyncState(notp, &tmp, lpSyncStates);
		if (hr != hrSuccess)
			continue;
		syncStates.emplace_back(tmp);
	}

	/* Search for the right connection */
	biglock.lock();
	auto iterAdvise = m_mapChangeAdvise.find(ulConnection);
	if (iterAdvise == m_mapChangeAdvise.cend() ||
	    iterAdvise->second->lpAdviseSink == nullptr)
		return hr;

	if (syncStates.empty())
		return hrSuccess;
	/* Send notifications in batches of MAX_NOTIFS_PER_CALL notifications */
	auto iterSyncStates = syncStates.cbegin();
	while (iterSyncStates != syncStates.cend()) {
		lpSyncStates->cValues = 0;
		while (iterSyncStates != syncStates.cend() &&
		       lpSyncStates->cValues < MAX_NOTIFS_PER_CALL) {
			/* We can do a straight memcpy here because pointers are still intact */
			memcpy(&lpSyncStates->lpbin[lpSyncStates->cValues++], *iterSyncStates, sizeof *lpSyncStates->lpbin);
			++iterSyncStates;
		}

		/* Send notification to the listener */
		if (iterAdvise->second->lpAdviseSink->OnNotify(0, lpSyncStates) != 0)
			ec_log_debug("ECNotifyClient::NotifyChange: Error by notify a client");
	}
	return hrSuccess;
}

HRESULT ECNotifyClient::UpdateSyncStates(const ECLISTSYNCID &lstSyncId, ECLISTSYNCSTATE *lplstSyncState)
{
	return m_lpTransport->HrGetSyncStates(lstSyncId, lplstSyncState);
}
