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

#include "ECSession.h"
#include <list>
#include <map>
#include <set>

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

typedef struct TABLESUBSCRIPTION {
     TABLE_ENTRY::TABLE_TYPE ulType;
     unsigned int ulRootObjectId;
     unsigned int ulObjectType;
     unsigned int ulObjectFlags;
     
     bool operator==(const TABLESUBSCRIPTION &b) const { return memcmp(this, &b, sizeof(*this)) == 0; }
     bool operator<(const TABLESUBSCRIPTION &b) const { return memcmp(this, &b, sizeof(*this)) < 0; }
} TABLESUBSCRIPTION;

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

class ECSessionManager
{
public:
	ECSessionManager(ECConfig *lpConfig, ECLogger *audit, bool bHostedKopano, bool bDistributedKopano);
	virtual ~ECSessionManager();

	virtual ECRESULT CreateAuthSession(struct soap *soap, unsigned int ulCapabilities, ECSESSIONID *sessionID, ECAuthSession **lppAuthSession, bool bRegisterSession, bool bLockSession);
	// Creates a session based on passed credentials
	virtual ECRESULT CreateSession(struct soap *soap, char *szName, char *szPassword, char *szImpersonateUser, char *szClientVersion, char *szClientApp, const char *szClientAppVersion, const char *szClientAppMisc, unsigned int ulCapabilities, ECSESSIONGROUPID sessionGroupID, ECSESSIONID *lpSessionID, ECSession **lppSession, bool fLockSession, bool fAllowUidAuth);
	// Creates a session without credential checking (caller must check credentials)
	virtual ECRESULT RegisterSession(ECAuthSession *lpAuthSession, ECSESSIONGROUPID sessionGroupID, char *szClientVersion, char *szClientApp, const char *szClientApplicationVersion, const char *szClientApplicationMisc, ECSESSIONID *lpSessionID, ECSession **lppSession, bool fLockSession);
	virtual ECRESULT CreateSessionInternal(ECSession **lppSession, unsigned int ulUserId = KOPANO_UID_SYSTEM);
	virtual ECRESULT RemoveSession(ECSESSIONID sessionID);
	virtual void RemoveSessionInternal(ECSession *lpSession);

	// Persistent connections: sessions with persistent connections (named pipes) are guaranteed not to timeout
	// between calls to SetSessionPersistentConnection() and RemoveSessionPersistentConnection. The persistent connection ID
	// is implementation-specific, but must be unique for each session.
	virtual ECRESULT SetSessionPersistentConnection(ECSESSIONID sessionID, unsigned int ulPersistentConnectionId);
	virtual ECRESULT RemoveSessionPersistentConnection(unsigned int ulPersistentConnectionId);

	virtual ECRESULT GetSessionGroup(ECSESSIONGROUPID sessionGroupID, ECSession *lpSession, ECSessionGroup **lppSessionGroup);
	virtual ECRESULT DeleteIfOrphaned(ECSessionGroup *lpGroup);

	ECRESULT RemoveAllSessions();
	ECRESULT CancelAllSessions(ECSESSIONID except = 0);
	ECRESULT ForEachSession(void(*callback)(ECSession*, void*), void *obj);

	ECRESULT LoadSettings();
	ECRESULT CheckUserLicense();

	ECRESULT UpdateTables(ECKeyTable::UpdateType ulType, unsigned int ulFlags, unsigned int ulObjId, unsigned int ulChildId, unsigned int ulObjType);
	ECRESULT UpdateTables(ECKeyTable::UpdateType ulType, unsigned int ulFlags, unsigned int ulObjId, std::list<unsigned int> &lstObjects, unsigned int ulObjType);
	ECRESULT UpdateOutgoingTables(ECKeyTable::UpdateType ulType, unsigned int ulStoreId, unsigned int ulObjId, unsigned int ulFlags, unsigned int ulObjType);

	ECRESULT NotificationModified(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId = 0);
	ECRESULT NotificationCreated(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId);
	ECRESULT NotificationMoved(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId, unsigned int ulOldParentId, entryId *lpOldEntryId = NULL);
	ECRESULT NotificationCopied(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId, unsigned int ulOldObjId, unsigned int ulOldParentId);
	ECRESULT NotificationDeleted(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulStoreId, entryId* lpEntryId, unsigned int ulFolderId, unsigned int ulFlags);
	ECRESULT NotificationSearchComplete(unsigned int ulObjId, unsigned int ulStoreId);
	ECRESULT NotificationChange(const set<unsigned int> &syncIds, unsigned int ulChangeId, unsigned int ulChangeType);
	
	ECRESULT ValidateSession(struct soap *soap, ECSESSIONID sessionID, ECAuthSession **lppSession, bool fLockSession = false);
	ECRESULT ValidateSession(struct soap *soap, ECSESSIONID sessionID, ECSession **lppSession, bool fLockSession = false);
	
	ECRESULT AddSessionClocks(ECSESSIONID ecSessionID, double dblUSer, double dblSystem, double dblReal);
	ECRESULT RemoveBusyState(ECSESSIONID ecSessionID, pthread_t thread);

	static	void*  SessionCleaner(void *lpTmpSessionManager);

	ECRESULT AddNotification(notification *notifyItem, unsigned int ulKey, unsigned int ulStoreId = 0, unsigned int ulFolderId = 0, unsigned int ulFlags = 0);
	ECRESULT DeferNotificationProcessing(ECSESSIONID ecSessionID, struct soap *soap);
	ECRESULT NotifyNotificationReady(ECSESSIONID ecSessionID);
	
	void GetStats(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj);
	void GetStats(sSessionManagerStats &sStats);
	ECRESULT DumpStats();

	bool IsHostedSupported(void) { return m_bHostedKopano; }
	bool IsDistributedSupported(void) { return m_bDistributedKopano; }
	ECRESULT GetLicensedUsers(unsigned int ulServiceType, unsigned int* lpulLicensedUsers);
	ECRESULT GetServerGUID(GUID* lpServerGuid);

	ECRESULT GetNewSourceKey(SOURCEKEY* lpSourceKey);

    // Requests that table change events of a specific table are sent to a
    // session. Events are published to the 'UpdateTables()' function or
    // 'UpdateOutgoingTables()' function of the session.
	ECRESULT SubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE, unsigned int ulTableRootObjectId, unsigned int ulObjectType, unsigned int ulObjectFlags, ECSESSIONID sessionID);
	ECRESULT UnsubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE, unsigned int ulTableRootObjectId, unsigned int ulObjectType, unsigned int ulObjectFlags, ECSESSIONID sessionID);

	// Requests that object notifications for a certain store are dispatched to a sessiongroup. Events
	// are published to the 'AddNotification()' function for the session's sessiongroup.
	ECRESULT SubscribeObjectEvents(unsigned int ulStoreId, ECSESSIONGROUPID sessionID);
	ECRESULT UnsubscribeObjectEvents(unsigned int ulStoreId, ECSESSIONGROUPID sessionID);
	
	enum SEQUENCE { SEQ_IMAP };
	ECRESULT GetNewSequence(SEQUENCE seq, unsigned long long *lpllSeqId);

	ECRESULT CreateDatabaseConnection();

	ECRESULT GetStoreSortLCID(ULONG ulStoreId, ULONG *lpLcid);
	LPCSTR GetDefaultSortLocaleID();
	ULONG GetSortLCID(ULONG ulStoreId);
	ECLocale GetSortLocale(ULONG ulStoreId);

	ECCacheManager *GetCacheManager(void) { return m_lpECCacheManager; }
	ECSearchFolders *GetSearchFolders(void) { return m_lpSearchFolders; }
	ECConfig *GetConfig(void) { return m_lpConfig; }
	ECLogger *GetAudit(void) { return m_lpAudit; }
	ECPluginFactory *GetPluginFactory(void) { return m_lpPluginFactory; }
	ECLockManager *GetLockManager(void) { return m_ptrLockManager.get(); }

protected:
	BTSession* 			GetSession(ECSESSIONID sessionID, bool fLockSession = false);
	ECRESULT 			ValidateBTSession(struct soap *soap, ECSESSIONID sessionID, BTSession **lppSession, bool fLockSession = false);
	BOOL 				IsSessionPersistent(ECSESSIONID sessionID);
	ECRESULT			UpdateSubscribedTables(ECKeyTable::UpdateType ulType, TABLESUBSCRIPTION sSubscription, std::list<unsigned int> &lstChildId);
	ECRESULT			SaveSourceKeyAutoIncrement(unsigned long long ullNewSourceKeyAutoIncrement);

	EC_SESSIONGROUPMAP m_mapSessionGroups; ///< map of all the session groups
	SESSIONMAP			m_mapSessions;			///< map of all the sessions
	
	pthread_rwlock_t	m_hCacheRWLock;			///< locking of the sessionMap
	pthread_rwlock_t	m_hGroupLock;			///< locking of session group map and lonely list
	pthread_mutex_t		m_hExitMutex;			///< Mutex needed for the release signal
	pthread_cond_t		m_hExitSignal;			///< Signal that should be send to the sessionncleaner when to exit 
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
	pthread_mutex_t		m_hSourceKeyAutoIncrementMutex;
	ECDatabase *		m_lpDatabase;

	pthread_mutex_t		m_mutexPersistent;
	PERSISTENTBYSESSION m_mapPersistentBySession; ///< map of all persistent sessions mapped to their connection id
	PERSISTENTBYCONNECTION m_mapPersistentByConnection; ///< map of all persistent connections mapped to their sessions

	pthread_mutex_t		m_mutexTableSubscriptions;
	unsigned int		m_ulTableSubscriptionId;
	TABLESUBSCRIPTIONMULTIMAP m_mapTableSubscriptions;	///< Maps a table subscription to the subscriber

	pthread_mutex_t		m_mutexObjectSubscriptions;
	OBJECTSUBSCRIPTIONSMULTIMAP	m_mapObjectSubscriptions;	///< Maps an object notification subscription (store id) to the subscriber

	ECNotificationManager *m_lpNotificationManager;
	ECTPropsPurge		*m_lpTPropsPurge;
	ECLockManagerPtr	m_ptrLockManager;

	// Sequences
	pthread_mutex_t		m_hSeqMutex;
	unsigned long long 	m_ulSeqIMAP;
	unsigned int		m_ulSeqIMAPQueue;
};

extern ECSessionManager *g_lpSessionManager;

#endif // #ifndef ECSESSIONMANAGER
