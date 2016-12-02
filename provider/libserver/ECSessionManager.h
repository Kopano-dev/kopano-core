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
#include <mutex>
#include <set>
#include <pthread.h>

#include "ECUserManagement.h"
#include "ECSearchFolders.h"
#include "ECDatabaseFactory.h"
#include "ECCacheManager.h"
#include "ECPluginFactory.h"
#include "ECServerEntrypoint.h"
#include "ECSessionGroup.h"
#include "ECNotificationManager.h"
#include "ECLockManager.h"

class ECLogger;
class ECTPropsPurge;

using namespace std;

typedef hash_map<ECSESSIONGROUPID, ECSessionGroup*>::Type EC_SESSIONGROUPMAP;
typedef hash_map<ECSESSIONID, BTSession*>::Type SESSIONMAP;
typedef hash_map<ECSESSIONID, unsigned int>::Type PERSISTENTBYSESSION;
typedef hash_map<unsigned int, ECSESSIONID>::Type PERSISTENTBYCONNECTION;
typedef std::multimap<unsigned int, ECSESSIONGROUPID> OBJECTSUBSCRIPTIONSMULTIMAP;

struct TABLESUBSCRIPTION {
     TABLE_ENTRY::TABLE_TYPE ulType;
     unsigned int ulRootObjectId;
     unsigned int ulObjectType;
     unsigned int ulObjectFlags;
     
     bool operator==(const TABLESUBSCRIPTION &b) const { return memcmp(this, &b, sizeof(*this)) == 0; }
     bool operator<(const TABLESUBSCRIPTION &b) const { return memcmp(this, &b, sizeof(*this)) < 0; }
};

typedef std::multimap<TABLESUBSCRIPTION, ECSESSIONID> TABLESUBSCRIPTIONMULTIMAP;

typedef struct tagSessionManagerStats {
	struct {
		ULONG ulItems;
		ULONG ulLocked;
		ULONG ulOpenTables;
		ULONGLONG ullSize;
		ULONGLONG ulTableSize;
	}session;

	struct {
		ULONG ulItems;
		ULONGLONG ullSize;
	} group;

	ULONG ulPersistentByConnection;
	ULONG ulPersistentByConnectionSize;
	ULONG ulPersistentBySession;
	ULONG ulPersistentBySessionSize;
	ULONG ulTableSubscriptions;
	ULONG ulObjectSubscriptions;
	ULONG ulTableSubscriptionSize;
	ULONG ulObjectSubscriptionSize;
}sSessionManagerStats;

class SOURCEKEY;

class _kc_export ECSessionManager {
public:
	_kc_hidden ECSessionManager(ECConfig *, ECLogger *audit, bool hosted, bool distributed);
	_kc_hidden virtual ~ECSessionManager(void);
	_kc_hidden virtual ECRESULT CreateAuthSession(struct soap *, unsigned int caps, ECSESSIONID *, ECAuthSession **, bool register_ses, bool lock_ses);
	// Creates a session based on passed credentials
	_kc_hidden virtual ECRESULT CreateSession(struct soap *, char *name, char *pass, char *imp_user, char *cl_vers, char *cl_app, const char *cl_app_ver, const char *cl_app_misc, unsigned int caps, ECSESSIONGROUPID, ECSESSIONID *, ECSession **, bool lock_ses, bool allow_uid_auth);
	// Creates a session without credential checking (caller must check credentials)
	_kc_hidden virtual ECRESULT RegisterSession(ECAuthSession *, ECSESSIONGROUPID, char *cl_ver, char *cl_app, const char *cl_app_ver, const char *cl_app_misc, ECSESSIONID *, ECSession **, bool lock_ses);
	_kc_hidden virtual ECRESULT CreateSessionInternal(ECSession **, unsigned int user_id = KOPANO_UID_SYSTEM);
	_kc_hidden virtual ECRESULT RemoveSession(ECSESSIONID);
	_kc_hidden virtual void RemoveSessionInternal(ECSession *);

	// Persistent connections: sessions with persistent connections (named pipes) are guaranteed not to timeout
	// between calls to SetSessionPersistentConnection() and RemoveSessionPersistentConnection. The persistent connection ID
	// is implementation-specific, but must be unique for each session.
	_kc_hidden virtual ECRESULT SetSessionPersistentConnection(ECSESSIONID, unsigned int conn_id);
	_kc_hidden virtual ECRESULT RemoveSessionPersistentConnection(unsigned int conn_id);
	_kc_hidden virtual ECRESULT GetSessionGroup(ECSESSIONGROUPID, ECSession *, ECSessionGroup **);
	_kc_hidden virtual ECRESULT DeleteIfOrphaned(ECSessionGroup *);
	_kc_hidden ECRESULT RemoveAllSessions(void);
	_kc_hidden ECRESULT CancelAllSessions(ECSESSIONID except = 0);
	_kc_hidden ECRESULT ForEachSession(void (*cb)(ECSession *, void *), void *obj);
	_kc_hidden ECRESULT LoadSettings(void);
	_kc_hidden ECRESULT CheckUserLicense(void);
	_kc_hidden ECRESULT UpdateTables(ECKeyTable::UpdateType, unsigned int flags, unsigned int obj_id, unsigned int child_id, unsigned int obj_type);
	_kc_hidden ECRESULT UpdateTables(ECKeyTable::UpdateType, unsigned int flags, unsigned int obj_id, std::list<unsigned int> &objects, unsigned int obj_type);
	_kc_hidden ECRESULT UpdateOutgoingTables(ECKeyTable::UpdateType, unsigned int store_id, unsigned int obj_id, unsigned int flags, unsigned int obj_type);
	_kc_hidden ECRESULT NotificationModified(unsigned int obj_type, unsigned int obj_id, unsigned int parent_id = 0);
	_kc_hidden ECRESULT NotificationCreated(unsigned int obj_type, unsigned int obj_id, unsigned int parent_id);
	_kc_hidden ECRESULT NotificationMoved(unsigned int obj_type, unsigned int obj_id, unsigned int parent_id, unsigned int old_parent_id, entryId *old_eid = nullptr);
	_kc_hidden ECRESULT NotificationCopied(unsigned int obj_type, unsigned int obj_id, unsigned int parent_id, unsigned int old_obj_id, unsigned int old_parent_id);
	_kc_hidden ECRESULT NotificationDeleted(unsigned int obj_type, unsigned int obj_id, unsigned int store_id, entryId *eid, unsigned int folder_id, unsigned int flags);
	_kc_hidden ECRESULT NotificationSearchComplete(unsigned int obj_id, unsigned int store_id);
	_kc_hidden ECRESULT NotificationChange(const std::set<unsigned int> &sync_ids, unsigned int change_id, unsigned int change_type);
	_kc_hidden ECRESULT ValidateSession(struct soap *, ECSESSIONID, ECAuthSession **, bool lock_ses = false);
	_kc_hidden ECRESULT ValidateSession(struct soap *, ECSESSIONID, ECSession **, bool lock_ses = false);
	_kc_hidden ECRESULT AddSessionClocks(ECSESSIONID, double user, double system, double real);
	ECRESULT RemoveBusyState(ECSESSIONID ecSessionID, pthread_t thread);
	_kc_hidden static void *SessionCleaner(void *tmp_ses_mgr);
	_kc_hidden ECRESULT AddNotification(notification *item, unsigned int key, unsigned int store_id = 0, unsigned int folder_id = 0, unsigned int flags = 0);
	_kc_hidden ECRESULT DeferNotificationProcessing(ECSESSIONID, struct soap *);
	_kc_hidden ECRESULT NotifyNotificationReady(ECSESSIONID);
	_kc_hidden void GetStats(void (*cb)(const std::string &, const std::string &, const std::string &, void *), void *obj);
	_kc_hidden void GetStats(sSessionManagerStats &);
	_kc_hidden ECRESULT DumpStats(void);
	_kc_hidden bool IsHostedSupported(void) { return m_bHostedKopano; }
	_kc_hidden bool IsDistributedSupported(void) { return m_bDistributedKopano; }
	_kc_hidden ECRESULT GetLicensedUsers(unsigned int svc_type, unsigned int *licusers);
	_kc_hidden ECRESULT GetServerGUID(GUID *);
	_kc_hidden ECRESULT GetNewSourceKey(SOURCEKEY *);

    // Requests that table change events of a specific table are sent to a
    // session. Events are published to the 'UpdateTables()' function or
    // 'UpdateOutgoingTables()' function of the session.
	_kc_hidden ECRESULT SubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE, unsigned int tbl_root_obj_id, unsigned int obj_type, unsigned int obj_flags, ECSESSIONID);
	_kc_hidden ECRESULT UnsubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE, unsigned int tbl_root_obj_id, unsigned int obj_type, unsigned int obj_flags, ECSESSIONID);

	// Requests that object notifications for a certain store are dispatched to a sessiongroup. Events
	// are published to the 'AddNotification()' function for the session's sessiongroup.
	_kc_hidden ECRESULT SubscribeObjectEvents(unsigned int store_id, ECSESSIONGROUPID);
	_kc_hidden ECRESULT UnsubscribeObjectEvents(unsigned int store_id, ECSESSIONGROUPID);
	
	enum SEQUENCE { SEQ_IMAP };
	_kc_hidden ECRESULT GetNewSequence(SEQUENCE, unsigned long long *seq_id);
	_kc_hidden ECRESULT CreateDatabaseConnection(void);
	_kc_hidden ECRESULT GetStoreSortLCID(ULONG store_id, ULONG *id);
	_kc_hidden LPCSTR GetDefaultSortLocaleID(void);
	_kc_hidden ULONG GetSortLCID(ULONG store_id);
	_kc_hidden ECLocale GetSortLocale(ULONG store_id);
	_kc_hidden ECCacheManager *GetCacheManager(void) { return m_lpECCacheManager; }
	_kc_hidden ECSearchFolders *GetSearchFolders(void) { return m_lpSearchFolders; }
	_kc_hidden ECConfig *GetConfig(void) { return m_lpConfig; }
	_kc_hidden ECLogger *GetAudit(void) { return m_lpAudit; }
	_kc_hidden ECPluginFactory *GetPluginFactory(void) { return m_lpPluginFactory; }
	_kc_hidden ECLockManager *GetLockManager(void) { return m_ptrLockManager.get(); }

protected:
	_kc_hidden BTSession *GetSession(ECSESSIONID, bool lock_ses = false);
	_kc_hidden ECRESULT ValidateBTSession(struct soap *, ECSESSIONID, BTSession **, bool lock_ses = false);
	_kc_hidden BOOL IsSessionPersistent(ECSESSIONID );
	_kc_hidden ECRESULT UpdateSubscribedTables(ECKeyTable::UpdateType, TABLESUBSCRIPTION, std::list<unsigned int> &child_id);
	_kc_hidden ECRESULT SaveSourceKeyAutoIncrement(unsigned long long new_src_key_autoincr);

	EC_SESSIONGROUPMAP m_mapSessionGroups; ///< map of all the session groups
	SESSIONMAP			m_mapSessions;			///< map of all the sessions
	
	pthread_rwlock_t	m_hCacheRWLock;			///< locking of the sessionMap
	pthread_rwlock_t	m_hGroupLock;			///< locking of session group map and lonely list
	std::mutex m_hExitMutex; /* Mutex needed for the release signal */
	std::condition_variable m_hExitSignal; /* Signal that should be send to the sessionncleaner when to exit */
	pthread_t			m_hSessionCleanerThread;///< Thread that is used for the sessioncleaner
	bool				m_bTerminateThread;
	ECConfig*			m_lpConfig;
	bool				bExit;
	ECCacheManager*		m_lpECCacheManager;
	ECLogger*			m_lpAudit;
	ECDatabaseFactory*	m_lpDatabaseFactory;
	ECPluginFactory*	m_lpPluginFactory;
	ECSearchFolders*	m_lpSearchFolders;
	bool				m_bHostedKopano;
	bool				m_bDistributedKopano;
	GUID*				m_lpServerGuid;
	unsigned long long	m_ullSourceKeyAutoIncrement;
	unsigned int		m_ulSourceKeyQueue;
	std::mutex m_hSourceKeyAutoIncrementMutex;
	ECDatabase *		m_lpDatabase;

	std::mutex m_mutexPersistent;
	PERSISTENTBYSESSION m_mapPersistentBySession; ///< map of all persistent sessions mapped to their connection id
	PERSISTENTBYCONNECTION m_mapPersistentByConnection; ///< map of all persistent connections mapped to their sessions

	std::mutex m_mutexTableSubscriptions;
	unsigned int		m_ulTableSubscriptionId;
	TABLESUBSCRIPTIONMULTIMAP m_mapTableSubscriptions;	///< Maps a table subscription to the subscriber

	std::mutex m_mutexObjectSubscriptions;
	OBJECTSUBSCRIPTIONSMULTIMAP	m_mapObjectSubscriptions;	///< Maps an object notification subscription (store id) to the subscriber

	ECNotificationManager *m_lpNotificationManager;
	ECTPropsPurge		*m_lpTPropsPurge;
	ECLockManagerPtr	m_ptrLockManager;

	// Sequences
	std::mutex m_hSeqMutex;
	unsigned long long 	m_ulSeqIMAP;
	unsigned int		m_ulSeqIMAPQueue;
};

extern _kc_export ECSessionManager *g_lpSessionManager;

#endif // #ifndef ECSESSIONMANAGER
