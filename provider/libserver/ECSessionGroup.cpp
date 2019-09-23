/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <mapidefs.h>
#include <mapitags.h>
#include <algorithm>
#include <mutex>
#include "ECSession.h"
#include "ECSessionGroup.h"
#include "ECSessionManager.h"
#include "SOAPUtils.h"

namespace KC {

ECSessionGroup::ECSessionGroup(ECSESSIONGROUPID sessionGroupId,
    ECSessionManager *lpSessionManager) :
	m_sessionGroupId(sessionGroupId), m_lpSessionManager(lpSessionManager)
{
}

ECSessionGroup::~ECSessionGroup()
{
	/* Unsubscribe any subscribed stores */
	for (const auto &p : m_mapSubscribedStores)
		m_lpSessionManager->UnsubscribeObjectEvents(p.second, m_sessionGroupId);
}

void ECSessionGroup::lock()
{
	/* Increase our refcount by one */
	scoped_lock lock(m_hThreadReleasedMutex);
	++m_ulRefCount;
}

void ECSessionGroup::unlock()
{
	// Decrease our refcount by one, signal ThreadReleased if RefCount == 0
	scoped_lock lock(m_hThreadReleasedMutex);
	--m_ulRefCount;
	if (!IsLocked())
		m_hThreadReleased.notify_one();
}

void ECSessionGroup::AddSession(ECSession *lpSession)
{
	scoped_rlock lock(m_hSessionMapLock);
	m_mapSessions.emplace(lpSession->GetSessionId(), sessionInfo(lpSession));
}

void ECSessionGroup::ReleaseSession(ECSession *lpSession)
{
	ulock_rec l_map(m_hSessionMapLock);
	m_mapSessions.erase(lpSession->GetSessionId());
	l_map.unlock();

	scoped_lock l_note(m_hNotificationLock);
	for (auto i = m_mapSubscribe.cbegin(); i != m_mapSubscribe.cend(); )
		if (i->second.ulSession != lpSession->GetSessionId())
			++i;
		else
			i = m_mapSubscribe.erase(i);
}

void ECSessionGroup::ShutdownSession(ECSession *lpSession)
{
    /* This session is used to get the notifications, stop GetNotifyItems() */
    if (m_getNotifySession == lpSession->GetSessionId())
        releaseListeners();
}

bool ECSessionGroup::isOrphan()
{
	scoped_rlock lock(m_hSessionMapLock);
	return m_mapSessions.empty();
}

void ECSessionGroup::UpdateSessionTime()
{
	scoped_rlock lock(m_hSessionMapLock);
	for (const auto &i : m_mapSessions)
		i.second.lpSession->UpdateSessionTime();
}

ECRESULT ECSessionGroup::AddAdvise(ECSESSIONID ulSessionId, unsigned int ulConnection, unsigned int ulKey, unsigned int ulEventMask)
{
	ECRESULT		hr = erSuccess;
	subscribeItem	sSubscribeItem;

	sSubscribeItem.ulSession	= ulSessionId;
	sSubscribeItem.ulConnection	= ulConnection;
	sSubscribeItem.ulKey		= ulKey;
	sSubscribeItem.ulEventMask	= ulEventMask;

	{
		scoped_lock lock(m_hNotificationLock);
		m_mapSubscribe.emplace(ulConnection, sSubscribeItem);
	}

	if(ulEventMask & (fnevNewMail | fnevObjectModified | fnevObjectCreated | fnevObjectCopied | fnevObjectDeleted | fnevObjectMoved)) {
		// Object and new mail notifications should be subscribed at the session manager
		unsigned int ulStore = 0;

		m_lpSessionManager->GetCacheManager()->GetStore(ulKey, &ulStore, NULL);
		m_lpSessionManager->SubscribeObjectEvents(ulStore, m_sessionGroupId);
		scoped_lock lock(m_mutexSubscribedStores);
		m_mapSubscribedStores.emplace(ulKey, ulStore);
	}

	return hr;
}

ECRESULT ECSessionGroup::AddChangeAdvise(ECSESSIONID ulSessionId, unsigned int ulConnection, notifySyncState *lpSyncState)
{
	changeSubscribeItem sSubscribeItem = {ulSessionId, ulConnection};

	if (lpSyncState == NULL)
		return KCERR_INVALID_PARAMETER;
	sSubscribeItem.sSyncState = *lpSyncState;
	scoped_lock lock(m_hNotificationLock);
	m_mapChangeSubscribe.emplace(lpSyncState->ulSyncId, sSubscribeItem);
	return erSuccess;
}

ECRESULT ECSessionGroup::DelAdvise(ECSESSIONID ulSessionId, unsigned int ulConnection)
{
	scoped_lock lock(m_hNotificationLock);
	auto iterSubscription = m_mapSubscribe.find(ulConnection);
	if (iterSubscription == m_mapSubscribe.cend()) {
		// Apparently the connection was used for change notifications.
		auto iterItem = find_if(m_mapChangeSubscribe.cbegin(),
			m_mapChangeSubscribe.cend(), [&](const auto &r) {
				return r.second.ulSession == ulSessionId &&
				       r.second.ulConnection == ulConnection;
			});
		if (iterItem != m_mapChangeSubscribe.cend())
			m_mapChangeSubscribe.erase(iterItem);
		return hrSuccess;
	}
	if (iterSubscription->second.ulEventMask & (fnevObjectModified | fnevObjectCreated | fnevObjectCopied | fnevObjectDeleted | fnevObjectMoved)) {
		// Object notification - remove our subscription to the store
		scoped_lock slock(m_mutexSubscribedStores);
		// Find the store that the key was subscribed for
		auto iterSubscribed = m_mapSubscribedStores.find(iterSubscription->second.ulKey);
		if (iterSubscribed != m_mapSubscribedStores.cend()) {
			// Unsubscribe the store
			m_lpSessionManager->UnsubscribeObjectEvents(iterSubscribed->second, m_sessionGroupId);
			// Remove from our list
			m_mapSubscribedStores.erase(iterSubscribed);
		} else
			assert(false); // Unsubscribe for something that was not subscribed
	}
	m_mapSubscribe.erase(iterSubscription);
	return hrSuccess;
}

ECRESULT ECSessionGroup::AddNotification(notification *notifyItem, unsigned int ulKey, unsigned int ulStore, ECSESSIONID ulSessionId, bool isCounter)
{
	ulock_normal l_note(m_hNotificationLock);
	ECNotification notify(*notifyItem);
	unsigned int ulParent = 0, ulOldParent = 0;
	bool check_parent = false, check_old_parent = false;
	ECRESULT hr = erSuccess;

	if (notifyItem->obj != nullptr) {
		if (notifyItem->obj->pParentId) {
			hr = m_lpSessionManager->GetCacheManager()->GetObjectFromEntryId(notifyItem->obj->pParentId, &ulParent);
			if (hr != hrSuccess)
				ulParent = 0; // to be sure
		}
		if (notifyItem->obj->pOldParentId) {
			hr = m_lpSessionManager->GetCacheManager()->GetObjectFromEntryId(notifyItem->obj->pOldParentId, &ulOldParent);
			if (hr != hrSuccess)
				ulOldParent = 0; // to be sure
		}
	}

	for (const auto &i : m_mapSubscribe) {
		auto eventmask = i.second.ulEventMask;

		// session mismatch (?)
		if (ulSessionId != 0 && ulSessionId != i.second.ulSession)
			continue;

		// not subscribed to respective (parent) folder or store
		check_parent = ulParent && (eventmask & fnevObjTypeMessage) &&
				(notifyItem->obj != nullptr) &&
				notifyItem->obj->ulObjType == MAPI_MESSAGE;
		check_old_parent = ulOldParent && (eventmask & fnevObjTypeMessage) &&
				(notifyItem->obj != nullptr) &&
				notifyItem->obj->ulObjType == MAPI_MESSAGE;

		if (i.second.ulKey != ulKey &&
		    i.second.ulKey != ulStore &&
		    (!check_parent || i.second.ulKey != ulParent) &&
		    (!check_old_parent || i.second.ulKey != ulOldParent))
			continue;

		// not subscribed to specified event type(s)
		if (!(notifyItem->ulEventType & eventmask))
			continue;

		// not subscribed to specified object type(s)
		if ((notifyItem->obj != nullptr) && (eventmask & (fnevObjTypeMessage | fnevObjTypeFolder))) {
			unsigned int objtype = 0;
			if (notifyItem->obj->ulObjType == MAPI_MESSAGE)
				objtype = fnevObjTypeMessage;
			else if (notifyItem->obj->ulObjType == MAPI_FOLDER)
				objtype = fnevObjTypeFolder;
			if (!(objtype & eventmask))
				continue;
		}

		// not subscribed to counters
		if (isCounter && (eventmask & fnevIgnoreCounters))
			continue;

		// send notification
		notify.SetConnection(i.second.ulConnection);
		m_listNotification.emplace_back(notify);
	}
	l_note.unlock();

	// Since we now have a notification ready to send, tell the session manager that we have something to send. Since
	// a notification can be read from any session in the session group, we have to notify all of the sessions
	scoped_rlock l_ses(m_hSessionMapLock);
	for (const auto &p : m_mapSessions)
		m_lpSessionManager->NotifyNotificationReady(p.second.lpSession->GetSessionId());
	return erSuccess;
}

ECRESULT ECSessionGroup::AddNotificationTable(ECSESSIONID ulSessionId, unsigned int ulType, unsigned int ulObjType, unsigned int ulTableId,
											  sObjectTableKey* lpsChildRow, sObjectTableKey* lpsPrevRow, struct propValArray *lpRow)
{
	std::lock_guard<ECSessionGroup> holder(*this);
	auto lpNotify = soap_new_notification(nullptr);
	lpNotify->tab = soap_new_notificationTable(nullptr);
	lpNotify->ulEventType			= fnevTableModified;
	lpNotify->tab->ulTableEvent		= ulType;

	if(lpsChildRow && (lpsChildRow->ulObjId > 0 || lpsChildRow->ulOrderId > 0)) {
		auto &p = lpNotify->tab->propIndex;
		p.ulPropTag = PR_INSTANCE_KEY;
		p.__union = SOAP_UNION_propValData_bin;
		p.Value.bin = soap_new_xsd__base64Binary(nullptr);
		p.Value.bin->__ptr  = soap_new_unsignedByte(nullptr, sizeof(uint32_t) * 2);
		p.Value.bin->__size = sizeof(ULONG) * 2;
		memcpy(p.Value.bin->__ptr, &lpsChildRow->ulObjId, sizeof(ULONG));
		memcpy(p.Value.bin->__ptr + sizeof(ULONG), &lpsChildRow->ulOrderId, sizeof(ULONG));
	}else {
		lpNotify->tab->propIndex.ulPropTag = PR_NULL;
		lpNotify->tab->propIndex.__union = SOAP_UNION_propValData_ul;
	}

	if(lpsPrevRow && (lpsPrevRow->ulObjId > 0 || lpsPrevRow->ulOrderId > 0))
	{
		auto &p = lpNotify->tab->propPrior;
		p.ulPropTag = PR_INSTANCE_KEY;
		p.__union = SOAP_UNION_propValData_bin;
		p.Value.bin = soap_new_xsd__base64Binary(nullptr);
		p.Value.bin->__ptr  = soap_new_unsignedByte(nullptr, sizeof(uint32_t) * 2);
		p.Value.bin->__size = sizeof(ULONG) * 2;
		memcpy(p.Value.bin->__ptr, &lpsPrevRow->ulObjId, sizeof(ULONG));
		memcpy(p.Value.bin->__ptr + sizeof(ULONG), &lpsPrevRow->ulOrderId, sizeof(ULONG));
	}else {
		lpNotify->tab->propPrior.__union = SOAP_UNION_propValData_ul;
		lpNotify->tab->propPrior.ulPropTag = PR_NULL;
	}

	lpNotify->tab->ulObjType = ulObjType;
	if(lpRow) {
		lpNotify->tab->pRow = soap_new_propValArray(nullptr);
		lpNotify->tab->pRow->__ptr = lpRow->__ptr;
		lpNotify->tab->pRow->__size = lpRow->__size;
	}

	AddNotification(lpNotify, ulTableId, 0, ulSessionId);
	//Free by lpRow
	if(lpNotify->tab->pRow){
		lpNotify->tab->pRow->__ptr = NULL;
		lpNotify->tab->pRow->__size = 0;
	}

	//Free struct
	soap_del_PointerTonotification(&lpNotify);
	return erSuccess;
}

ECRESULT ECSessionGroup::AddChangeNotification(const std::set<unsigned int> &syncIds, unsigned int ulChangeId, unsigned int ulChangeType)
{
	notification notifyItem;
	notificationICS ics;
	entryId syncStateBin;
	notifySyncState	syncState;
	std::map<ECSESSIONID,unsigned int> mapInserted;

	syncState.ulChangeId = ulChangeId;
	notifyItem.ulEventType = fnevKopanoIcsChange;
	notifyItem.ics = &ics;
	notifyItem.ics->pSyncState = &syncStateBin;
	notifyItem.ics->pSyncState->__size = sizeof(syncState);
	notifyItem.ics->pSyncState->__ptr = (unsigned char*)&syncState;
	notifyItem.ics->ulChangeType = ulChangeType;

	std::lock_guard<ECSessionGroup> holder(*this);
	ulock_normal l_note(m_hNotificationLock);
	// Iterate through all sync ids
	for (auto sync_id : syncIds) {
		// Iterate through all subscribed clients for the current sync id
		auto iterRange = m_mapChangeSubscribe.equal_range(sync_id);
		for (auto iterItem = iterRange.first;
		     iterItem != iterRange.second; ++iterItem) {
			// update sync state
			syncState.ulSyncId = sync_id;
			// create ECNotification
			ECNotification notify(notifyItem);
			notify.SetConnection(iterItem->second.ulConnection);
			m_listNotification.emplace_back(std::move(notify));
			mapInserted[iterItem->second.ulSession]++;
		}
	}
	l_note.unlock();

	// Since we now have a notification ready to send, tell the session manager that we have something to send. Since
	// a notifications can be read from any session in the session group, we have to notify all of the sessions
	ulock_rec l_ses(m_hSessionMapLock);
	for (const auto &p : m_mapSessions)
		m_lpSessionManager->NotifyNotificationReady(p.second.lpSession->GetSessionId());
	l_ses.unlock();
	return erSuccess;
}

ECRESULT ECSessionGroup::AddChangeNotification(ECSESSIONID ulSessionId, unsigned int ulConnection, unsigned int ulSyncId, unsigned long ulChangeId)
{
	notification notifyItem;
	notificationICS ics;
	entryId syncStateBin;
	notifySyncState	syncState;

	syncState.ulSyncId = ulSyncId;
	syncState.ulChangeId = ulChangeId;
	notifyItem.ulEventType = fnevKopanoIcsChange;
	notifyItem.ics = &ics;
	notifyItem.ics->pSyncState = &syncStateBin;
	notifyItem.ics->pSyncState->__size = sizeof(syncState);
	notifyItem.ics->pSyncState->__ptr = (unsigned char*)&syncState;

	std::lock_guard<ECSessionGroup> holder(*this);
	ulock_normal l_note(m_hNotificationLock);
	// create ECNotification
	ECNotification notify(notifyItem);
	notify.SetConnection(ulConnection);
	m_listNotification.emplace_back(std::move(notify));
	l_note.unlock();

	// Since we now have a notification ready to send, tell the session manager that we have something to send. Since
	// a notifications can be read from any session in the session group, we have to notify all of the sessions
	ulock_rec l_ses(m_hSessionMapLock);
	for (const auto &p : m_mapSessions)
		m_lpSessionManager->NotifyNotificationReady(p.second.lpSession->GetSessionId());
	l_ses.unlock();
	return erSuccess;
}

ECRESULT ECSessionGroup::GetNotifyItems(struct soap *soap, ECSESSIONID ulSessionId, struct notifyResponse *notifications)
{
	ECRESULT		er = erSuccess;
	std::lock_guard<ECSessionGroup> holder(*this);

	/* Start waiting for notifications */
	/*
	 * Store the session which requested the notifications.
	 * We need this in case the session is removed and the
	 * session must release all calls into ECSessionGroup.
	 */
	m_getNotifySession = ulSessionId;
	/*
	 * Update Session times for all sessions attached to this group.
	 * This prevents any of the sessions to timeout while it was waiting
	 * for notifications for the group.
	 */
	UpdateSessionTime();
	soap_default_notifyResponse(soap, notifications);
	ulock_normal l_note(m_hNotificationLock);

	/* May still be nothing in there, as the signal is also fired when we should exit */
	if (!m_listNotification.empty()) {
		ULONG ulSize = (ULONG)m_listNotification.size();

		notifications->pNotificationArray = soap_new_notificationArray(soap);
		notifications->pNotificationArray->__ptr  = soap_new_notification(soap, ulSize);
		notifications->pNotificationArray->__size = ulSize;

		size_t nPos = 0;
		for (const auto i : m_listNotification)
			i.GetCopy(soap, notifications->pNotificationArray->__ptr[nPos++]);
		m_listNotification.clear();
	} else {
	    er = KCERR_NOT_FOUND;
    }
	l_note.unlock();

	/* Reset GetNotifySession */
	m_getNotifySession = 0;
	return er;
}

ECRESULT ECSessionGroup::releaseListeners()
{
	scoped_lock lock(m_hNotificationLock);
	m_bExit = true;
	m_hNewNotificationEvent.notify_all();
	return erSuccess;
}

/**
 * Get object size
 *
 * @return Object size in bytes
 */
size_t ECSessionGroup::GetObjectSize(void)
{
	size_t ulSize = 0;
	ulock_normal l_note(m_hNotificationLock);

	ulSize += MEMORY_USAGE_MAP(m_mapSubscribe.size(), SUBSCRIBEMAP);
	ulSize += MEMORY_USAGE_MAP(m_mapChangeSubscribe.size(), CHANGESUBSCRIBEMAP);

	for (const auto &n : m_listNotification)
		ulSize += n.GetObjectSize();
	ulSize += MEMORY_USAGE_LIST(m_listNotification.size(), ECNOTIFICATIONLIST);
	l_note.unlock();

	ulSize += sizeof(*this);

	ulock_rec l_ses(m_hSessionMapLock);
	ulSize += MEMORY_USAGE_MAP(m_mapSessions.size(), SESSIONINFOMAP);
	l_ses.unlock();

	ulock_normal l_sub(m_mutexSubscribedStores);
	ulSize += MEMORY_USAGE_MULTIMAP(m_mapSubscribedStores.size(), SUBSCRIBESTOREMULTIMAP);
	l_sub.unlock();
	return ulSize;
}

} /* namespace */
