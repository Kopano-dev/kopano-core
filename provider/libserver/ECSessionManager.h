/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

// ECSessionManager.h: interface for the ECSessionManager class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ECSESSIONMANAGER
#define ECSESSIONMANAGER

#include <kopano/zcdefs.h>
#include "ECSession.h"
#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <pthread.h>
#include <kopano/platform.h>
#include <kopano/timeutil.hpp>
#include "ECAttachmentStorage.h"
#include "ECUserManagement.h"
#include "ECSearchFolders.h"
#include "ECDatabaseFactory.h"
#include "ECCacheManager.h"
#include "ECPluginFactory.h"
#include "ECServerEntrypoint.h"
#include "ECSessionGroup.h"
#include "ECNotificationManager.h"
#include "ECLockManager.h"
#include "StatsClient.h"

struct soap;

namespace KC {

class ECConfig;
class ECLogger;
class ECTPropsPurge;

typedef std::unordered_map<ECSESSIONGROUPID, ECSessionGroup *> EC_SESSIONGROUPMAP;
typedef std::unordered_map<ECSESSIONID, BTSession *> SESSIONMAP;
typedef std::unordered_map<ECSESSIONID, unsigned int> PERSISTENTBYSESSION;
typedef std::unordered_map<unsigned int, ECSESSIONID> PERSISTENTBYCONNECTION;
typedef std::multimap<unsigned int, ECSESSIONGROUPID> OBJECTSUBSCRIPTIONSMULTIMAP;

struct TABLESUBSCRIPTION {
     TABLE_ENTRY::TABLE_TYPE ulType;
	unsigned int ulRootObjectId, ulObjectType, ulObjectFlags;
	bool operator==(const TABLESUBSCRIPTION &b) const noexcept { return memcmp(this, &b, sizeof(*this)) == 0; }
	bool operator<(const TABLESUBSCRIPTION &b) const noexcept { return memcmp(this, &b, sizeof(*this)) < 0; }
};

typedef std::multimap<TABLESUBSCRIPTION, ECSESSIONID> TABLESUBSCRIPTIONMULTIMAP;

struct sSessionManagerStats {
	struct {
		ULONG ulItems, ulLocked, ulOpenTables;
		ULONGLONG ullSize, ulTableSize;
	}session;

	struct {
		ULONG ulItems;
		ULONGLONG ullSize;
	} group;

	ULONG ulPersistentByConnection, ulPersistentByConnectionSize;
	ULONG ulPersistentBySession, ulPersistentBySessionSize;
	ULONG ulTableSubscriptions, ulTableSubscriptionSize;
	ULONG ulObjectSubscriptions, ulObjectSubscriptionSize;
};

class usercount_t final {
	public:
	enum ucIndex {
		ucActiveUser = 0,
		ucNonActiveUser,
		ucRoom,
		ucEquipment,
		ucContact,
		ucNonActiveTotal, /* Must be right before ucMAX */
		ucMAX = ucNonActiveTotal, /* Must be very last */
	};

	usercount_t() = default;

	usercount_t(unsigned int a, unsigned int n, unsigned int r, unsigned int e, unsigned int c) :
		m_valid(true)
	{
		m_counts[ucActiveUser] = a;
		m_counts[ucNonActiveUser] = n;
		m_counts[ucRoom] = r;
		m_counts[ucEquipment] = e;
		m_counts[ucContact] = c;
	}

	void assign(unsigned int a, unsigned int n, unsigned int r, unsigned int e, unsigned int c)
	{
		*this = usercount_t(a, n, r, e, c);
	}

	bool is_valid() const { return m_valid; }

	void set(ucIndex index, unsigned int value)
	{
		if (index == ucNonActiveTotal)
			return;
		assert(index >= 0 && index < ucMAX);
		m_counts[index] = value;
		m_valid = true;
	}

	unsigned int operator[](ucIndex index) const
	{
		if (index == ucNonActiveTotal)
			/* Contacts do not count for non-active stores. */
			return m_counts[ucNonActiveUser] + m_counts[ucRoom] + m_counts[ucEquipment];
		assert(index >= 0 && index < ucMAX);
		return m_counts[index];
	}

	private:
	bool m_valid = false;
	unsigned int m_counts[ucMAX]{};
};

class KC_EXPORT server_stats final : public ECStatsCollector {
	public:
	KC_HIDDEN server_stats(std::shared_ptr<ECConfig>);
	void fill_odm() override;

	private:
	KC_HIDDEN void update_tcmalloc_stats();
};

class SOURCEKEY;

class KC_EXPORT ECSessionManager final {
public:
	KC_HIDDEN ECSessionManager(std::shared_ptr<ECConfig>, std::shared_ptr<ECLogger> audit, std::shared_ptr<server_stats>, bool hosted, bool distributed);
	KC_HIDDEN virtual ~ECSessionManager();
	KC_HIDDEN virtual ECRESULT CreateAuthSession(struct soap *, unsigned int caps, ECSESSIONID *, ECAuthSession **, bool register_ses, bool lock_ses);
	// Creates a session based on passed credentials
	KC_HIDDEN virtual ECRESULT CreateSession(struct soap *, const char *name, const char *pass, const char *imp_user, const char *cl_vers, const char *cl_app, const char *cl_app_ver, const char *cl_app_misc, unsigned int caps, ECSESSIONGROUPID, ECSESSIONID *, ECSession **, bool lock_ses, bool allow_uid_auth, bool register_session);
	// Creates a session without credential checking (caller must check credentials)
	KC_HIDDEN virtual ECRESULT RegisterSession(ECAuthSession *, ECSESSIONGROUPID, const char *cl_ver, const char *cl_app, const char *cl_app_ver, const char *cl_app_misc, ECSESSIONID *, ECSession **, bool lock_ses);
	virtual ECRESULT CreateSessionInternal(ECSession **, unsigned int user_id = KOPANO_UID_SYSTEM);
	KC_HIDDEN virtual ECRESULT RemoveSession(ECSESSIONID);
	virtual void RemoveSessionInternal(ECSession *);

	KC_HIDDEN virtual ECRESULT GetSessionGroup(ECSESSIONGROUPID, ECSession *, ECSessionGroup **);
	KC_HIDDEN virtual ECRESULT DeleteIfOrphaned(ECSessionGroup *);
	ECRESULT RemoveAllSessions();
	KC_HIDDEN ECRESULT CancelAllSessions(ECSESSIONID except = 0);
	KC_HIDDEN ECRESULT ForEachSession(void (*cb)(ECSession *, void *), void *obj);
	KC_HIDDEN ECRESULT LoadSettings();
	KC_HIDDEN ECRESULT UpdateTables(ECKeyTable::UpdateType, unsigned int flags, unsigned int obj_id, unsigned int child_id, unsigned int obj_type);
	KC_HIDDEN ECRESULT UpdateTables(ECKeyTable::UpdateType, unsigned int flags, unsigned int obj_id, std::list<unsigned int> &objects, unsigned int obj_type);
	KC_HIDDEN ECRESULT UpdateOutgoingTables(ECKeyTable::UpdateType, unsigned int store_id, unsigned int obj_id, unsigned int flags, unsigned int obj_type);
	KC_HIDDEN ECRESULT NotificationModified(unsigned int obj_type, unsigned int obj_id, unsigned int parent_id = 0, bool isCounter=false);
	KC_HIDDEN ECRESULT NotificationCreated(unsigned int obj_type, unsigned int obj_id, unsigned int parent_id);
	KC_HIDDEN ECRESULT NotificationMoved(unsigned int obj_type, unsigned int obj_id, unsigned int parent_id, unsigned int old_parent_id, entryId *old_eid = nullptr);
	KC_HIDDEN ECRESULT NotificationCopied(unsigned int obj_type, unsigned int obj_id, unsigned int parent_id, unsigned int old_obj_id, unsigned int old_parent_id);
	KC_HIDDEN ECRESULT NotificationDeleted(unsigned int obj_type, unsigned int obj_id, unsigned int store_id, entryId *eid, unsigned int folder_id, unsigned int flags);
	KC_HIDDEN ECRESULT NotificationSearchComplete(unsigned int obj_id, unsigned int store_id);
	KC_HIDDEN ECRESULT NotificationChange(const std::set<unsigned int> &sync_ids, unsigned int change_id, unsigned int change_type);
	KC_HIDDEN ECRESULT ValidateSession(struct soap *, ECSESSIONID, ECAuthSession **);
	KC_HIDDEN ECRESULT ValidateSession(struct soap *, ECSESSIONID, ECSession **);
	KC_HIDDEN ECRESULT AddSessionClocks(ECSESSIONID, double user, double system, double real);
	ECRESULT RemoveBusyState(ECSESSIONID ecSessionID, pthread_t thread);
	KC_HIDDEN static void *SessionCleaner(void *tmp_ses_mgr);
	KC_HIDDEN ECRESULT AddNotification(notification *item, unsigned int key, unsigned int store_id = 0, unsigned int folder_id = 0, unsigned int flags = 0, bool isCounter = false);
	KC_HIDDEN ECRESULT DeferNotificationProcessing(ECSESSIONID, struct soap *);
	KC_HIDDEN ECRESULT NotifyNotificationReady(ECSESSIONID);
	KC_HIDDEN void update_extra_stats();
	KC_HIDDEN sSessionManagerStats get_stats();
	KC_HIDDEN bool IsHostedSupported() const { return m_bHostedKopano; }
	KC_HIDDEN bool IsDistributedSupported() const { return m_bDistributedKopano; }
	KC_HIDDEN ECRESULT GetServerGUID(GUID *);
	KC_HIDDEN ECRESULT GetNewSourceKey(SOURCEKEY *);

    // Requests that table change events of a specific table are sent to a
    // session. Events are published to the 'UpdateTables()' function or
    // 'UpdateOutgoingTables()' function of the session.
	KC_HIDDEN ECRESULT SubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE, unsigned int tbl_root_obj_id, unsigned int obj_type, unsigned int obj_flags, ECSESSIONID);
	KC_HIDDEN ECRESULT UnsubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE, unsigned int tbl_root_obj_id, unsigned int obj_type, unsigned int obj_flags, ECSESSIONID);

	// Requests that object notifications for a certain store are dispatched to a sessiongroup. Events
	// are published to the 'AddNotification()' function for the session's sessiongroup.
	KC_HIDDEN ECRESULT SubscribeObjectEvents(unsigned int store_id, ECSESSIONGROUPID);
	KC_HIDDEN ECRESULT UnsubscribeObjectEvents(unsigned int store_id, ECSESSIONGROUPID);

	enum SEQUENCE { SEQ_IMAP };
	KC_HIDDEN ECRESULT GetNewSequence(SEQUENCE, unsigned long long *seq_id);
	KC_HIDDEN ECRESULT CreateDatabaseConnection();
	KC_HIDDEN ECRESULT GetStoreSortLCID(unsigned int store_id, unsigned int *id);
	KC_HIDDEN const char *GetDefaultSortLocaleID();
	KC_HIDDEN unsigned int GetSortLCID(unsigned int store_id);
	KC_HIDDEN ECLocale GetSortLocale(unsigned int store_id);
	KC_HIDDEN ECCacheManager *GetCacheManager() const { return m_lpECCacheManager.get(); }
	KC_HIDDEN ECSearchFolders *GetSearchFolders() const { return m_lpSearchFolders.get(); }
	KC_HIDDEN std::shared_ptr<ECConfig> GetConfig() const { return m_lpConfig; }
	KC_HIDDEN std::shared_ptr<ECLogger> GetAudit() const { return m_lpAudit; }
	KC_HIDDEN ECPluginFactory *GetPluginFactory() const { return m_lpPluginFactory.get(); }
	KC_HIDDEN ECLockManager *GetLockManager() const { return m_ptrLockManager.get(); }
	KC_HIDDEN ECDatabaseFactory *get_db_factory() const { return m_lpDatabaseFactory.get(); }
	KC_HIDDEN ECAttachmentConfig *get_atxconfig() const { return m_atxconfig.get(); }
	KC_HIDDEN ECRESULT get_user_count(usercount_t *);
	KC_HIDDEN ECRESULT get_user_count_cached(usercount_t *);

	std::shared_ptr<server_stats> m_stats;

protected:
	KC_HIDDEN BTSession *GetSession(ECSESSIONID, bool lock_ses = false);
	KC_HIDDEN ECRESULT ValidateBTSession(struct soap *, ECSESSIONID, BTSession **);
	KC_HIDDEN BOOL IsSessionPersistent(ECSESSIONID);
	KC_HIDDEN ECRESULT UpdateSubscribedTables(ECKeyTable::UpdateType, const TABLESUBSCRIPTION &, std::list<unsigned int> &child_id);
	KC_HIDDEN ECRESULT SaveSourceKeyAutoIncrement(unsigned long long new_src_key_autoincr);

	EC_SESSIONGROUPMAP m_mapSessionGroups; ///< map of all the session groups
	SESSIONMAP			m_mapSessions;			///< map of all the sessions
	KC::shared_mutex m_hCacheRWLock; ///< locking of the sessionMap
	KC::shared_mutex m_hGroupLock; ///< locking of session group map and lonely list
	std::mutex m_hExitMutex; /* Mutex needed for the release signal */
	std::condition_variable m_hExitSignal; /* Signal that should be sent to the sessionncleaner when to exit */
	pthread_t			m_hSessionCleanerThread;///< Thread that is used for the sessioncleaner
	std::shared_ptr<ECConfig> m_lpConfig;
	bool bExit = false, m_bTerminateThread, m_thread_active = false;
	bool m_bHostedKopano, m_bDistributedKopano;
	unsigned long long m_ullSourceKeyAutoIncrement = 0;
	unsigned int m_ulSourceKeyQueue = 0;
	std::mutex m_hSourceKeyAutoIncrementMutex;
	std::mutex m_mutexPersistent;
	PERSISTENTBYSESSION m_mapPersistentBySession; ///< map of all persistent sessions mapped to their connection id
	PERSISTENTBYCONNECTION m_mapPersistentByConnection; ///< map of all persistent connections mapped to their sessions
	std::mutex m_mutexTableSubscriptions;
	unsigned int		m_ulTableSubscriptionId;
	TABLESUBSCRIPTIONMULTIMAP m_mapTableSubscriptions;	///< Maps a table subscription to the subscriber
	std::mutex m_mutexObjectSubscriptions;
	OBJECTSUBSCRIPTIONSMULTIMAP	m_mapObjectSubscriptions;	///< Maps an object notification subscription (store id) to the subscriber

	// Sequences
	std::mutex m_hSeqMutex;
	unsigned long long m_ulSeqIMAP = 0;
	unsigned int m_ulSeqIMAPQueue = 0;

	bool m_sguid_set = false;
	GUID m_server_guid{};
	std::shared_ptr<ECLogger> m_lpAudit;
	std::unique_ptr<ECPluginFactory> m_lpPluginFactory;
	std::unique_ptr<ECDatabaseFactory> m_lpDatabaseFactory;
	std::unique_ptr<ECSearchFolders> m_lpSearchFolders;
	std::unique_ptr<ECCacheManager> m_lpECCacheManager;
	std::unique_ptr<ECTPropsPurge> m_lpTPropsPurge;
	ECLockManagerPtr m_ptrLockManager;
	std::unique_ptr<ECNotificationManager> m_lpNotificationManager;
	std::unique_ptr<ECDatabase> m_lpDatabase;
	std::unique_ptr<ECAttachmentConfig> m_atxconfig;

	std::recursive_mutex m_usercount_mtx;
	KC::time_point m_usercount_ts;
	usercount_t m_usercount;
};

extern KC_EXPORT void (*kopano_get_server_stats)(unsigned int *qlen, KC::time_duration *qage, unsigned int *nthr, unsigned int *nidlethr);
extern KC_EXPORT std::unique_ptr<ECSessionManager> g_lpSessionManager;

} /* namespace */

#endif // #ifndef ECSESSIONMANAGER
