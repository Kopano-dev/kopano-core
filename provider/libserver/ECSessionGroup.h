/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <atomic>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <kopano/ECKeyTable.h>
#include "ECNotification.h"
#include <kopano/kcodes.h>
#include <kopano/CommonUtil.h>

struct soap;

namespace KC {

class ECSession;
class ECSessionGroup;
class ECSessionManager;

struct sessionInfo {
	sessionInfo(ECSession *s) : lpSession(s) {}
	ECSession	 *lpSession;
};

struct subscribeItem {
	ECSESSIONID	 ulSession;		// Unique session identifier
	unsigned int ulConnection;	// Unique client identifier for notification
	unsigned int ulKey;			// database object id (also storeid) or a tableid
	unsigned int ulEventMask;
};

struct changeSubscribeItem {
	ECSESSIONID		ulSession;
	unsigned int	ulConnection;
	notifySyncState	sSyncState;
};

class ECSessionGroup final {
public:
	ECSessionGroup(ECSESSIONGROUPID sessionGroupId, ECSessionManager *lpSessionManager);
	virtual ~ECSessionGroup();

	/*
	 * Thread safety handlers
	 */
	virtual void lock();
	virtual void unlock();
	virtual bool IsLocked() const final { return m_ulRefCount > 0; }

	/*
	 * Returns the SessionGroupId
	 */
	virtual ECSESSIONGROUPID GetSessionGroupId() const final { return m_sessionGroupId; }

	/*
	 * Add/Remove Session from group
	 */
	virtual void AddSession(ECSession *lpSession);
	virtual void ShutdownSession(ECSession *lpSession);
	virtual void ReleaseSession(ECSession *lpSession);

	/*
	 * Update session time for all attached sessions
	 */
	virtual void UpdateSessionTime();

	/*
	 * Check is SessionGroup has lost all its children
	 */
	virtual bool isOrphan();

	/*
	 * Item subscription
	 */
	virtual ECRESULT AddAdvise(ECSESSIONID ulSessionId, unsigned int ulConnection, unsigned int ulKey, unsigned int ulEventMask);
	virtual ECRESULT AddChangeAdvise(ECSESSIONID ulSessionId, unsigned int ulConnection, notifySyncState *lpSyncState);
	virtual ECRESULT DelAdvise(ECSESSIONID ulSessionId, unsigned int ulConnection);

	/*
	 * Notifications
	 */
	virtual ECRESULT AddNotification(notification *notifyItem, unsigned int ulKey, unsigned int ulStore, ECSESSIONID ulSessionId = 0, bool isCounter = false);
	virtual ECRESULT AddNotificationTable(ECSESSIONID, unsigned int type, unsigned int objtype, unsigned int table_id, sObjectTableKey *child_row, sObjectTableKey *prev_row, struct propValArray *row);
	virtual ECRESULT AddChangeNotification(const std::set<unsigned int> &syncIds, unsigned int ulChangeId, unsigned int ulChangeType);
	virtual ECRESULT AddChangeNotification(ECSESSIONID ulSessionId, unsigned int ulConnection, unsigned int ulSyncId, unsigned long ulChangeId);
	virtual ECRESULT GetNotifyItems(struct soap *soap, ECSESSIONID ulSessionId, struct notifyResponse *notifications);

	size_t GetObjectSize(void);

private:
	ECRESULT releaseListeners();

	/* Personal SessionGroupId */
	ECSESSIONGROUPID	m_sessionGroupId;

	/* All Sessions attached to this group */
	std::map<ECSESSIONID, sessionInfo> m_mapSessions;

	/* List of all items the group is subscribed to */
	std::map<unsigned int, subscribeItem> m_mapSubscribe;
	std::multimap<unsigned int, changeSubscribeItem> m_mapChangeSubscribe; // SyncId -> changeSubscribeItem
	std::recursive_mutex m_hSessionMapLock;

	/* Notifications */
	std::list<ECNotification> m_listNotification;

	/* Notifications lock/event */
	std::mutex m_hNotificationLock;
	std::condition_variable m_hNewNotificationEvent;
	ECSESSIONID m_getNotifySession = 0;

	/* Thread safety mutex/event */
	std::atomic<unsigned int> m_ulRefCount{0};
	std::mutex m_hThreadReleasedMutex;
	std::condition_variable m_hThreadReleased;

	/* Set to TRUE if no more GetNextNotifyItems() should be done on this group since the main
	 * session has exited
	 */
	bool m_bExit = false;

	/* Reference to the session manager needed to notify changes in our queue */
	ECSessionManager *	m_lpSessionManager;

	/* Multimap of subscriptions that we have (key -> store id) */
	std::multimap<unsigned int, unsigned int> m_mapSubscribedStores;
	std::mutex m_mutexSubscribedStores;

private:
	// Make ECSessionGroup non-copyable
	ECSessionGroup(const ECSessionGroup &) = delete;
	ECSessionGroup &operator=(const ECSessionGroup &) = delete;
};

} /* namespace */
