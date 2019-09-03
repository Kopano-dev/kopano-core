/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <shared_mutex>
#include <utility>
#include <pthread.h>
#include <mapidefs.h>
#include <mapitags.h>
#include <kopano/MAPIErrors.h>
#include <kopano/hl.hpp>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include "ECMAPI.h"
#include "ECDatabase.h"
#include "ECSessionGroup.h"
#include "ECSessionManager.h"
#include "StatsClient.h"
#include "ECTPropsPurge.h"
#include "ECDatabaseUtils.h"
#include "ECSecurity.h"
#include "SSLUtil.h"
#include "kcore.hpp"
#include "ECICS.h"
#include <edkmdb.h>
#include "StorageUtil.h"

using namespace std::chrono_literals;
using namespace std::string_literals;

namespace KC {

void (*kopano_get_server_stats)(unsigned int *qlen, KC::time_duration *qage, unsigned int *nthr, unsigned int *idlthr) = [](unsigned int *, KC::time_duration *, unsigned int *, unsigned int *) {};

static inline const char *znul(const char *s)
{
	return s != nullptr ? s : "";
}

ECSessionManager::ECSessionManager(std::shared_ptr<ECConfig> cfg,
    std::shared_ptr<ECLogger> ad, std::shared_ptr<server_stats> sc,
    bool bHostedKopano, bool bDistributedKopano) :
	m_stats(std::move(sc)),
	m_lpConfig(std::move(cfg)), m_bHostedKopano(bHostedKopano),
	m_bDistributedKopano(bDistributedKopano), m_lpAudit(std::move(ad)),
	m_lpPluginFactory(new ECPluginFactory(m_lpConfig, m_stats, bHostedKopano, bDistributedKopano)),
	m_lpDatabaseFactory(new ECDatabaseFactory(m_lpConfig, m_stats)),
	m_lpSearchFolders(new ECSearchFolders(this, m_lpDatabaseFactory.get())),
	m_lpECCacheManager(new ECCacheManager(m_lpConfig, m_lpDatabaseFactory.get())),
	m_lpTPropsPurge(new ECTPropsPurge(m_lpConfig, m_lpDatabaseFactory.get())),
	m_ptrLockManager(ECLockManager::Create())
{
	// init SSL randomness for session IDs
	ssl_random_init();

	//Create session clean up thread
	auto err = pthread_create(&m_hSessionCleanerThread, nullptr, SessionCleaner, this);
	if (err != 0) {
		ec_log_crit("Could not create SessionCleaner thread: %s", strerror(err));
	} else {
		m_thread_active = true;
	        set_thread_name(m_hSessionCleanerThread, "ses_cleaner");
	}

	m_lpNotificationManager.reset(new ECNotificationManager());
}

ECSessionManager::~ECSessionManager()
{
	ulock_normal l_exit(m_hExitMutex);
	bExit = TRUE;
	m_hExitSignal.notify_one();
	l_exit.unlock();
	m_lpTPropsPurge.reset();
	m_lpDatabase.reset();
	m_lpDatabaseFactory.reset();

	if (m_thread_active) {
		auto err = pthread_join(m_hSessionCleanerThread, nullptr);
		if (err != 0)
			ec_log_crit("Unable to join session cleaner thread: %s", strerror(err));
	}
	/* Clean up all sessions */
	std::lock_guard<KC::shared_mutex> l_cache(m_hCacheRWLock);
	for (auto s = m_mapSessions.begin(); s != m_mapSessions.end();
	     s = m_mapSessions.erase(s))
		delete s->second;
	// Clearing the cache takes too long while shutting down
}

ECRESULT ECSessionManager::LoadSettings(){
	ECDatabase *	lpDatabase = NULL;
	DB_RESULT lpDBResult;

	if (m_sguid_set)
		return KCERR_BAD_VALUE;
	auto er = m_lpDatabaseFactory.get()->get_tls_db(&lpDatabase);
	if(er != erSuccess)
		return er;

	std::string strQuery = "SELECT `value` FROM settings WHERE `name` = 'server_guid' LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	auto lpDBRow = lpDBResult.fetch_row();
	auto lpDBLenths = lpDBResult.fetch_row_lengths();
	if (lpDBRow == nullptr || lpDBRow[0] == nullptr ||
	    lpDBLenths == nullptr || lpDBLenths[0] != sizeof(GUID))
		return KCERR_NOT_FOUND;

	memcpy(&m_server_guid, lpDBRow[0], sizeof(m_server_guid));
	/* ECStatsCollector may decide to send before the guid has been set. That's normal. */
	m_stats->set(SCN_SERVER_GUID, bin2hex(sizeof(m_server_guid), &m_server_guid));

	er = lpDatabase->DoSelect("SELECT `value` FROM `settings` WHERE `name`='charset' LIMIT 1", &lpDBResult);
	if (er != erSuccess)
		return er;
	lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == nullptr || lpDBRow[0] == nullptr || strcmp(lpDBRow[0], "utf8mb4") != 0) {
		m_lpDatabaseFactory->filter_bmp(true);
		ec_log_warn("K-1244: Your database does not support storing 4-byte UTF-8! The content of some mails may be truncated. The DB should be upgraded with `kopano-dbadm usmp` and kopano-server be restarted.");
	}

	strQuery = "SELECT `value` FROM settings WHERE `name` = 'source_key_auto_increment' LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	lpDBRow = lpDBResult.fetch_row();
	lpDBLenths = lpDBResult.fetch_row_lengths();
	if (lpDBRow == nullptr || lpDBRow[0] == nullptr ||
	    lpDBLenths == nullptr || lpDBLenths[0] != 8)
		return KCERR_NOT_FOUND;

	memcpy(&m_ullSourceKeyAutoIncrement, lpDBRow[0], sizeof(m_ullSourceKeyAutoIncrement));
	m_ullSourceKeyAutoIncrement = le64_to_cpu(m_ullSourceKeyAutoIncrement);
	m_sguid_set = true;

	er = ECAttachmentConfig::create(m_server_guid, m_lpConfig, &unique_tie(m_atxconfig));
	if (er != hrSuccess) {
		ec_log_crit("Could not initialize attachment store: %s", GetMAPIErrorMessage(kcerr_to_mapierr(er)));
		return er;
	}
	return erSuccess;
}

/*
 * This function is threadsafe since we hold the lock the the group list, and the session retrieved from the grouplist
 * is locked so it cannot be deleted by other sessions, while we hold the lock for the group list.
 *
 * Other sessions may release the session group, even if they are the last, while we are in this function since
 * deletion of the session group only occurs within DeleteIfOrphaned(), and this function guarantees that the caller
 * will receive a sessiongroup that is not an orphan unless the caller releases the session group.
 */
ECRESULT ECSessionManager::GetSessionGroup(ECSESSIONGROUPID sgid,
    ECSession *lpSession, ECSessionGroup **lppSessionGroup)
{
	ECSessionGroup *lpSessionGroup = NULL;
	std::shared_lock<KC::shared_mutex> lr_group(m_hGroupLock);
	std::unique_lock<KC::shared_mutex> lw_group(m_hGroupLock, std::defer_lock_t());

	/* Workaround for old clients, when sgid is 0 each session is its own group */
	if (sgid == 0) {
		lpSessionGroup = new ECSessionGroup(sgid, this);
		g_lpSessionManager->m_stats->inc(SCN_SESSIONGROUPS_CREATED);
	} else {
		auto iter = m_mapSessionGroups.find(sgid);
		/* Check if the SessionGroup already exists on the server */
		if (iter == m_mapSessionGroups.cend()) {
			// "upgrade" lock to insert new session
			lr_group.unlock();
			lw_group.lock();
			lpSessionGroup = new ECSessionGroup(sgid, this);
			m_mapSessionGroups.emplace(sgid, lpSessionGroup);
			g_lpSessionManager->m_stats->inc(SCN_SESSIONGROUPS_CREATED);
		} else
			lpSessionGroup = iter->second;
	}

	lpSessionGroup->AddSession(lpSession);
	*lppSessionGroup = lpSessionGroup;
	return erSuccess;
}

ECRESULT ECSessionManager::DeleteIfOrphaned(ECSessionGroup *lpGroup)
{
	ECSessionGroup *lpSessionGroup = NULL;
	ECSESSIONGROUPID id = lpGroup->GetSessionGroupId();

	if (id != 0) {
		std::lock_guard<KC::shared_mutex> l_group(m_hGroupLock);

    	/* Check if the SessionGroup actually exists, if it doesn't just return without error */
	auto i = m_mapSessionGroups.find(id);
		if (i == m_mapSessionGroups.cend())
			return erSuccess;
    	/* If this was the last Session, delete the SessionGroup */
    	if (i->second->isOrphan()) {
    	    lpSessionGroup = i->second;
    	    m_mapSessionGroups.erase(i);
    	}
	} else
		lpSessionGroup = lpGroup;

	if (lpSessionGroup) {
		delete lpSessionGroup;
		g_lpSessionManager->m_stats->inc(SCN_SESSIONGROUPS_DELETED);
	}
	return erSuccess;
}

BTSession* ECSessionManager::GetSession(ECSESSIONID sessionID, bool fLockSession) {
	BTSession *lpSession = NULL;

	auto iIterator = m_mapSessions.find(sessionID);
	if (iIterator != m_mapSessions.cend()) {
		lpSession = iIterator->second;
		lpSession->UpdateSessionTime();
		if(fLockSession)
			lpSession->lock();
	}else{
		//EC_SESSION_LOST
	}
	return lpSession;
}

// Clean up all current sessions
ECRESULT ECSessionManager::RemoveAllSessions()
{
	std::list<BTSession *> lstSessions;

	// Lock the session map since we're going to remove all the sessions.
	std::unique_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
	ec_log_info("Shutdown all current sessions");
	for (auto s = m_mapSessions.cbegin(); s != m_mapSessions.cend();
	     s = m_mapSessions.erase(s))
		lstSessions.emplace_back(s->second);
	l_cache.unlock();
	// Do the actual session deletes, while the session map is not locked (!)
	for (auto sesp : lstSessions)
		delete sesp;
	return erSuccess;
}

ECRESULT ECSessionManager::CancelAllSessions(ECSESSIONID sessionIDException)
{
	BTSession		*lpSession = NULL;
	std::list<BTSession *> lstSessions;

	// Lock the session map since we're going to remove all the sessions.
	std::unique_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
	ec_log_info("Shutdown all current sessions");

	for (auto iIterSession = m_mapSessions.begin();
	     iIterSession != m_mapSessions.cend(); ) {
		if (iIterSession->first == sessionIDException) {
			++iIterSession;
			continue;
		}
		lpSession = iIterSession->second;
		// Tell the notification manager to wake up anyone waiting for this session
		m_lpNotificationManager->NotifyChange(iIterSession->first);
		iIterSession = m_mapSessions.erase(iIterSession);
		lstSessions.emplace_back(lpSession);
	}

	l_cache.unlock();
	// Do the actual session deletes, while the session map is not locked (!)
	for (auto sesp : lstSessions)
		delete sesp;
	return erSuccess;
}

// call a function for all sessions available
// used by ECStatsTable
ECRESULT ECSessionManager::ForEachSession(void(*callback)(ECSession*, void*), void *obj)
{
	std::shared_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
	for (const auto &p : m_mapSessions)
		callback(dynamic_cast<ECSession *>(p.second), obj);
	return erSuccess;
}

// Locking of sessions works as follows:
//
// - A session is requested by the caller thread through ValidateSession. ValidateSession
//   Locks the complete session table, then acquires a lock on the session, and then
//   frees the lock on the session table. This makes sure that when a session is returned,
//   it is guaranteed not to be deleted by another thread (due to a shutdown or logoff).
//   The caller of 'ValidateSession' is therefore responsible for unlocking the session
//   when it is finished.
//
// - When a session is terminated, a lock is opened on the session table, making sure no
//   new session can be opened, or session can be deleted. Then, the session is searched
//   in the table, and directly deleted from the table, making sure that no new threads can
//   open the session in question after this point. Then, the session is deleted, but the
//   session itself waits in the destructor until all threads holding a lock on the session
//   through Lock or ValidateSession have released their lock, before actually deleting the
//   session object.
//
// This means that exiting the server must wait until all client requests have exited. For
// most operations, this is not a problem, but for some long requests (ie large deletes or
// copies, or GetNextNotifyItem) may take quite a while to exit. This is compensated for, by
// having the session call a 'cancel' request to long-running calls, which makes the calls
// exit prematurely.
//
ECRESULT ECSessionManager::ValidateSession(struct soap *soap,
    ECSESSIONID sessionID, ECAuthSession **lppSession)
{
	BTSession *lpSession = NULL;
	auto er = ValidateBTSession(soap, sessionID, &lpSession);
	if (er != erSuccess)
		return er;
	*lppSession = dynamic_cast<ECAuthSession*>(lpSession);
	return erSuccess;
}

ECRESULT ECSessionManager::ValidateSession(struct soap *soap,
    ECSESSIONID sessionID, ECSession **lppSession)
{
	BTSession *lpSession = NULL;
	auto er = ValidateBTSession(soap, sessionID, &lpSession);
	if (er != erSuccess)
		return er;
	*lppSession = dynamic_cast<ECSession*>(lpSession);
	return erSuccess;
}

ECRESULT ECSessionManager::ValidateBTSession(struct soap *soap,
    ECSESSIONID sessionID, BTSession **lppSession)
{
	// Read lock
	std::shared_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
	auto lpSession = GetSession(sessionID, true);
	l_cache.unlock();

	if (lpSession == NULL)
		return KCERR_END_OF_SESSION;
	lpSession->RecordRequest(soap);
	auto er = lpSession->ValidateOriginator(soap);
	if (er != erSuccess) {
		lpSession->unlock();
		lpSession = NULL;
		return er;
	}

#ifdef WITH_ZLIB
	/* Enable compression if client desired and granted */
	if (lpSession->GetCapabilities() & KOPANO_CAP_COMPRESSION) {
		soap_set_imode(soap, SOAP_ENC_ZLIB);
		soap_set_omode(soap, SOAP_ENC_ZLIB | SOAP_IO_CHUNK);
	}
#endif
	// Enable streaming support if client is capable
	if (lpSession->GetCapabilities() & KOPANO_CAP_ENHANCED_ICS) {
		soap_set_omode(soap, SOAP_ENC_MTOM | SOAP_IO_CHUNK);
		soap_set_imode(soap, SOAP_ENC_MTOM);
		soap_post_check_mime_attachments(soap);
	}
	*lppSession = lpSession;
	return erSuccess;
}

ECRESULT ECSessionManager::CreateAuthSession(struct soap *soap, unsigned int ulCapabilities, ECSESSIONID *sessionID, ECAuthSession **lppAuthSession, bool bRegisterSession, bool bLockSession)
{
	ECSESSIONID newSessionID;

	CreateSessionID(ulCapabilities, &newSessionID);

	auto lpAuthSession = new(std::nothrow) ECAuthSession(GetSourceAddr(soap),
		newSessionID, m_lpDatabaseFactory.get(), this, ulCapabilities);
	if (lpAuthSession == NULL)
		return KCERR_NOT_ENOUGH_MEMORY;
	if (bLockSession)
	        lpAuthSession->lock();
	if (bRegisterSession) {
		std::unique_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
		m_mapSessions.emplace(newSessionID, lpAuthSession);
		l_cache.unlock();
		g_lpSessionManager->m_stats->inc(SCN_SESSIONS_CREATED);
	}

	*sessionID = newSessionID;
	*lppAuthSession = lpAuthSession;
	return erSuccess;
}

ECRESULT ECSessionManager::CreateSession(struct soap *soap, const char *szName,
    const char *szPassword, const char *szImpersonateUser,
    const char *cl_ver, const char *cl_app,
    const char *cl_app_ver, const char *cl_app_misc,
    unsigned int ulCapabilities, ECSESSIONGROUPID sgid,
    ECSESSIONID *lpSessionID, ECSession **lppSession, bool fLockSession,
    bool fAllowUidAuth, bool bRegisterSession)
{
	std::unique_ptr<ECAuthSession> lpAuthSession;
	const char		*method = "error";
	std::string		from;
	CONNECTION_TYPE ulType = SOAP_CONNECTION_TYPE(soap);

	if (ulType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY)
		from = std::string("file://") + m_lpConfig->GetSetting("server_pipe_priority");
	else if (ulType == CONNECTION_TYPE_NAMED_PIPE)
		// connected through Unix socket
		from = std::string("file://") + m_lpConfig->GetSetting("server_pipe_name");
	else
		// connected over network
		from = soap->host;

	auto er = CreateAuthSession(soap, ulCapabilities, lpSessionID, &unique_tie(lpAuthSession), false, false);
	if (er != erSuccess)
		return er;
	if (cl_app == nullptr)
		cl_app = "<unknown>";

	// If we've connected with SSL, check if there is a certificate, and check if we accept that certificate for that user
	if (soap->ssl && lpAuthSession->ValidateUserCertificate(soap, szName, szImpersonateUser) == erSuccess) {
		g_lpSessionManager->m_stats->inc(SCN_LOGIN_SSL);
		method = "SSL Certificate";
		goto authenticated;
	}
	// First, try socket authentication (dagent, won't print error)
	if(fAllowUidAuth && lpAuthSession->ValidateUserSocket(soap->socket, szName, szImpersonateUser) == erSuccess) {
		g_lpSessionManager->m_stats->inc(SCN_LOGIN_SOCKET);
		method = "Pipe socket";
		goto authenticated;
	}
	// If that fails, try logon with supplied username/password (clients, may print logon error)
	if(lpAuthSession->ValidateUserLogon(szName, szPassword, szImpersonateUser) == erSuccess) {
		g_lpSessionManager->m_stats->inc(SCN_LOGIN_PASSWORD);
		method = "User supplied password";
		goto authenticated;
	}

	// whoops, out of auth options.
	ZLOG_AUDIT(m_lpAudit, "authenticate failed user='%s' from='%s' program='%s'",
		szName, from.c_str(), cl_app);
	g_lpSessionManager->m_stats->inc(SCN_LOGIN_DENIED);
	return KCERR_LOGON_FAILED;

authenticated:
	if (!bRegisterSession)
		return erSuccess;
	er = RegisterSession(lpAuthSession.get(), sgid, cl_ver, cl_app,
	     cl_app_ver, cl_app_misc, lpSessionID, lppSession, fLockSession);
	if (er != erSuccess) {
		if (er == KCERR_NO_ACCESS && szImpersonateUser != NULL && *szImpersonateUser != '\0') {
			ec_log_err("Failed attempt to impersonate user \"%s\" by user \"%s\": %s (0x%x)", szImpersonateUser, szName, GetMAPIErrorMessage(er), er);
			ZLOG_AUDIT(m_lpAudit, "authenticate ok, impersonate failed: from=\"%s\" user=\"%s\" impersonator=\"%s\" method=\"%s\" program=\"%s\"",
				from.c_str(), szImpersonateUser, szName, method, cl_app);
		} else {
			ec_log_err("User \"%s\" authenticated, but failed to create session: %s (0x%x)", szName, GetMAPIErrorMessage(er), er);
			ZLOG_AUDIT(m_lpAudit, "authenticate ok, session failed: from=\"%s\" user=\"%s\" impersonator=\"%s\" method=\"%s\" program=\"%s\"",
				from.c_str(), szImpersonateUser, szName, method, cl_app);
		}
		return er;
	}
	if (!szImpersonateUser || *szImpersonateUser == '\0')
		ZLOG_AUDIT(m_lpAudit, "authenticate ok: from=\"%s\" user=\"%s\" method=\"%s\" program=\"%s\" sid=0x%llx",
			from.c_str(), szName, method, cl_app, static_cast<unsigned long long>(*lpSessionID));
	else
		ZLOG_AUDIT(m_lpAudit, "authenticate ok, impersonate ok: from=\"%s\" user=\"%s\" impersonator=\"%s\" method=\"%s\" program=\"%s\" sid=0x%llx",
			from.c_str(), szImpersonateUser, szName, method, cl_app, static_cast<unsigned long long>(*lpSessionID));
	return erSuccess;
}

ECRESULT ECSessionManager::RegisterSession(ECAuthSession *lpAuthSession,
    ECSESSIONGROUPID sgid, const char *cl_ver, const char *cl_app,
    const char *cl_app_ver, const char *cl_app_misc, ECSESSIONID *lpSessionID,
    ECSession **lppSession, bool fLockSession)
{
	ECSession	*lpSession = NULL;
	ECSESSIONID	newSID = 0;

	auto er = lpAuthSession->CreateECSession(sgid, znul(cl_ver), znul(cl_app),
	          znul(cl_app_ver), znul(cl_app_misc), &newSID, &lpSession);
	if (er != erSuccess)
		return er;

	if (fLockSession)
		lpSession->lock();
	std::unique_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
	m_mapSessions.emplace(newSID, lpSession);
	l_cache.unlock();
	*lpSessionID = std::move(newSID);
	*lppSession = lpSession;
	g_lpSessionManager->m_stats->inc(SCN_SESSIONS_CREATED);
	return er;
}

ECRESULT ECSessionManager::CreateSessionInternal(ECSession **lppSession, unsigned int ulUserId)
{
	ECSESSIONID	newSID;

	CreateSessionID(KOPANO_CAP_LARGE_SESSIONID, &newSID);

	auto lpSession = make_unique_nt<ECSession>("<internal>",
		newSID, 0, m_lpDatabaseFactory.get(), this, 0,
		ECSession::METHOD_NONE, 0, "internal", "kopano-server", "", "");
	if (lpSession == NULL)
		return KCERR_LOGON_FAILED;
	auto er = lpSession->GetSecurity()->SetUserContext(ulUserId, EC_NO_IMPERSONATOR);
	if (er != erSuccess)
		return er;
	m_stats->inc(SCN_SESSIONS_INTERNAL_CREATED);
	*lppSession = lpSession.release();
	return erSuccess;
}

void ECSessionManager::RemoveSessionInternal(ECSession *lpSession)
{
	if (lpSession == nullptr)
		return;
	m_stats->inc(SCN_SESSIONS_INTERNAL_DELETED);
	delete lpSession;
}

ECRESULT ECSessionManager::RemoveSession(ECSESSIONID sessionID){
	m_stats->inc(SCN_SESSIONS_DELETED);

	// Make sure no other thread can read or write the sessions list
	std::unique_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
	// Get a session, don't lock it ourselves
	auto lpSession = GetSession(sessionID, false);
	// Remove the session from the list. No other threads can start new
	// requests on the session after this point
	m_mapSessions.erase(sessionID);
	l_cache.unlock();

	// We know for sure that no other thread is attempting to remove the session
	// at this time because it would not have been in the m_mapSessions map
	// Delete the session. This will block until all requesters on the session
	// have released their lock on the session
	if(lpSession != NULL) {
		if(lpSession->Shutdown(5 * 60 * 1000) == erSuccess)
			delete lpSession;
		else
			ec_log_err("Session failed to shut down: skipping logoff");
	}
    // Tell the notification manager to wake up anyone waiting for this session
    m_lpNotificationManager->NotifyChange(sessionID);
	return erSuccess;
}

/**
 * Add notification to a session group.
 * @note This function can't handle table notifications!
 *
 * @param[in] notifyItem The notification data to send
 * @param[in] ulKey The object (hierarchyid) the notification acts on
 * @param[in] ulStore The store the ulKey object resides in. 0 for unknown (default).
 * @param[in] ulFolderId Parent folder object for ulKey. 0 for unknown or not required (default).
 * @param[in] ulFlags Hierarchy flags for ulKey. 0 for unknown (default).
 *
 * @return Kopano error code
 */
ECRESULT ECSessionManager::AddNotification(notification *notifyItem, unsigned int ulKey, unsigned int ulStore, unsigned int ulFolderId, unsigned int ulFlags, bool isCounter) {
	std::set<ECSESSIONGROUPID> setGroups;

	if(ulStore == 0) {
		auto hr = m_lpECCacheManager->GetStore(ulKey, &ulStore, nullptr);
		if(hr != erSuccess)
			return hr;
	}

	// Send notification to subscribed sessions
	ulock_normal l_sub(m_mutexObjectSubscriptions);
	for (auto sub = m_mapObjectSubscriptions.lower_bound(ulStore);
	     sub != m_mapObjectSubscriptions.cend() && sub->first == ulStore;
	     ++sub)
		// Send a notification only once to a session group, even if it has subscribed multiple times
		setGroups.emplace(sub->second);
	l_sub.unlock();

	// Send each subscribed session group one notification
	for (const auto &grp : setGroups) {
		std::shared_lock<KC::shared_mutex> grplk(m_hGroupLock);
		auto iIterator = m_mapSessionGroups.find(grp);
		if (iIterator != m_mapSessionGroups.cend())
			iIterator->second->AddNotification(notifyItem, ulKey, ulStore, 0, isCounter);
	}

	// Next, do an internal notification to update searchfolder views for message updates.
	if (notifyItem->obj == nullptr || notifyItem->obj->ulObjType != MAPI_MESSAGE)
		return hrSuccess;
	if (ulFolderId == 0 && ulFlags == 0 &&
	    GetCacheManager()->GetObject(ulKey, &ulFolderId, NULL, &ulFlags, NULL) != erSuccess) {
		assert(false);
		return hrSuccess;
	}
	// Skip changes on associated messages, and changes on deleted item. (but include DELETE of deleted items)
	if ((ulFlags & MAPI_ASSOCIATED) || (notifyItem->ulEventType != fnevObjectDeleted && (ulFlags & MSGFLAG_DELETED)))
		return hrSuccess;

	switch (notifyItem->ulEventType) {
	case fnevObjectMoved:
		// Only update the item in the new folder. The system will automatically delete the item from folders that were not in the search path
		m_lpSearchFolders->UpdateSearchFolders(ulStore, ulFolderId, ulKey, ECKeyTable::TABLE_ROW_MODIFY);
		break;
	case fnevObjectDeleted:
		m_lpSearchFolders->UpdateSearchFolders(ulStore, ulFolderId, ulKey, ECKeyTable::TABLE_ROW_DELETE);
		break;
	case fnevObjectCreated:
		m_lpSearchFolders->UpdateSearchFolders(ulStore, ulFolderId, ulKey, ECKeyTable::TABLE_ROW_ADD);
		break;
	case fnevObjectCopied:
		m_lpSearchFolders->UpdateSearchFolders(ulStore, ulFolderId, ulKey, ECKeyTable::TABLE_ROW_ADD);
		break;
	case fnevObjectModified:
		m_lpSearchFolders->UpdateSearchFolders(ulStore, ulFolderId, ulKey, ECKeyTable::TABLE_ROW_MODIFY);
		break;
	}
	return hrSuccess;
}

void* ECSessionManager::SessionCleaner(void *lpTmpSessionManager)
{
	kcsrv_blocksigs();
	auto lpSessionManager = static_cast<ECSessionManager *>(lpTmpSessionManager);
	std::list<BTSession *> lstSessions;

	if (lpSessionManager == NULL)
		return 0;

	ECDatabase *db = NULL;
	if (lpSessionManager->m_lpDatabaseFactory.get()->get_tls_db(&db) != erSuccess)
		ec_log_err("GTLD failed in SessionCleaner");

	while(true){
		std::unique_lock<KC::shared_mutex> l_cache(lpSessionManager->m_hCacheRWLock);
		auto lCurTime = GetProcessTime();

		// Find a session that has timed out
		for (auto iIterator = lpSessionManager->m_mapSessions.begin();
		     iIterator != lpSessionManager->m_mapSessions.cend(); ) {
			bool del = iIterator->second->GetSessionTime() < lCurTime &&
			           !lpSessionManager->IsSessionPersistent(iIterator->first);
			if (!del) {
				++iIterator;
				continue;
			}
			// Remember all the session to be deleted
			lstSessions.emplace_back(iIterator->second);
			// Remove the session from the list, no new threads can start on this session after this point.
			g_lpSessionManager->m_stats->inc(SCN_SESSIONS_TIMEOUT);
			iIterator = lpSessionManager->m_mapSessions.erase(iIterator);
		}

		// Release ownership of the rwlock object. This makes sure all threads are free to run (and exit).
		l_cache.unlock();

		// Now, remove all the session. It will wait until all running threads for that session have exited.
		for (const auto ses : lstSessions) {
			if (ses->Shutdown(5 * 60 * 1000) == erSuccess)
				delete ses;
			else
				// The session failed to shut down within our timeout period. This means we probably hit a bug; this
				// should only happen if some bit of code has locked the session and failed to unlock it. There are now
				// two options: delete the session anyway and hope we don't segfault, or leak the session. We choose
				// the latter.
				ec_log_err("Session failed to shut down: skipping clean");
		}

		lstSessions.clear();
		KC::sync_logon_times(lpSessionManager->GetCacheManager(), db);

		// Wait for a terminate signal or return after a few minutes
		ulock_normal l_exit(lpSessionManager->m_hExitMutex);
		if(lpSessionManager->bExit) {
			l_exit.unlock();
			break;
		}
		if (lpSessionManager->m_hExitSignal.wait_for(l_exit, 5s) != std::cv_status::timeout)
			break;
	}
	return NULL;
}

ECRESULT ECSessionManager::UpdateOutgoingTables(ECKeyTable::UpdateType ulType, unsigned int ulStoreId, unsigned int ulObjId, unsigned int ulFlags, unsigned int ulObjType)
{
	TABLESUBSCRIPTION sSubscription;
	std::list<unsigned int> lstObjId;

	lstObjId.emplace_back(ulObjId);
	sSubscription.ulType = TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE;
	sSubscription.ulRootObjectId = ulFlags & EC_SUBMIT_MASTER ? 0 : ulStoreId; // in the master queue, use 0 as root object id
	sSubscription.ulObjectType = ulObjType;
	sSubscription.ulObjectFlags = ulFlags & EC_SUBMIT_MASTER; // Only use MASTER flag as differentiator
	return UpdateSubscribedTables(ulType, sSubscription, lstObjId);
}

ECRESULT ECSessionManager::UpdateTables(ECKeyTable::UpdateType ulType, unsigned int ulFlags, unsigned ulObjId, unsigned ulChildId, unsigned int ulObjType)
{
	std::list<unsigned int> lstChildId = {ulChildId};
	return UpdateTables(ulType, ulFlags, ulObjId, lstChildId, ulObjType);
}

ECRESULT ECSessionManager::UpdateTables(ECKeyTable::UpdateType ulType, unsigned int ulFlags, unsigned ulObjId, std::list<unsigned int>& lstChildId, unsigned int ulObjType)
{
	TABLESUBSCRIPTION sSubscription;

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER)
		return erSuccess;
	sSubscription.ulType = TABLE_ENTRY::TABLE_TYPE_GENERIC;
	sSubscription.ulRootObjectId = ulObjId;
	sSubscription.ulObjectType = ulObjType;
	sSubscription.ulObjectFlags = ulFlags;
	return UpdateSubscribedTables(ulType, sSubscription, lstChildId);
}

ECRESULT ECSessionManager::UpdateSubscribedTables(ECKeyTable::UpdateType ulType, TABLESUBSCRIPTION sSubscription, std::list<unsigned int> &lstChildId)
{
	std::set<ECSESSIONID> setSessions;

    // Find out which sessions our interested in this event by looking at our subscriptions
	ulock_normal l_sub(m_mutexTableSubscriptions);
	for (auto sub = m_mapTableSubscriptions.find(sSubscription);
	     sub != m_mapTableSubscriptions.cend() && sub->first == sSubscription; ++sub)
		setSessions.emplace(sub->second);
	l_sub.unlock();

    // We now have a set of sessions that are interested in the notification. This list is normally quite small since not that many
    // sessions have the same table opened at one time.

    // For each of the sessions that are interested, send the table change
	for (const auto &ses : setSessions) {
		// Get session
		std::shared_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
		auto lpBTSession = GetSession(ses, true);
		l_cache.unlock();

	    // Send the change notification
		if (lpBTSession == nullptr)
			continue;
		auto lpSession = dynamic_cast<ECSession *>(lpBTSession);
		if (lpSession == NULL) {
			lpBTSession->unlock();
			continue;
		}
		if (sSubscription.ulType == TABLE_ENTRY::TABLE_TYPE_GENERIC)
			lpSession->GetTableManager()->UpdateTables(ulType, sSubscription.ulObjectFlags, sSubscription.ulRootObjectId, lstChildId, sSubscription.ulObjectType);
		else if (sSubscription.ulType == TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE)
			lpSession->GetTableManager()->UpdateOutgoingTables(ulType, sSubscription.ulRootObjectId, lstChildId, sSubscription.ulObjectFlags, sSubscription.ulObjectType);
		lpBTSession->unlock();
	}
	return erSuccess;
}

// FIXME: ulFolderId should be an entryid, because the parent is already deleted!
// You must specify which store the object was deleted from, 'cause we can't find out afterwards
ECRESULT ECSessionManager::NotificationDeleted(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulStoreId, entryId* lpEntryId, unsigned int ulFolderId, unsigned int ulFlags)
{
	ECRESULT er = erSuccess;
	struct notification notify;

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		goto exit;
	notify.obj = s_alloc<notificationObject>(nullptr);
	notify.ulEventType			= fnevObjectDeleted;
	notify.obj->ulObjType		= ulObjType;
	notify.obj->pEntryId		= lpEntryId;
	if(ulFolderId > 0) {
		er = GetCacheManager()->GetEntryIdFromObject(ulFolderId, NULL, 0, &notify.obj->pParentId);
		if(er != erSuccess)
			goto exit;
	}
	AddNotification(&notify, ulObjId, ulStoreId, ulFolderId, ulFlags);
exit:
	notify.obj->pEntryId = NULL;
	FreeNotificationStruct(&notify, false);
	return er;
}

ECRESULT ECSessionManager::NotificationModified(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId, bool isCounter)
{
	struct notification notify;

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		return erSuccess;
	notify.obj = s_alloc<notificationObject>(nullptr);
	notify.ulEventType			= fnevObjectModified;
	notify.obj->ulObjType		= ulObjType;
	auto er = GetCacheManager()->GetEntryIdFromObject(ulObjId, nullptr, 0, &notify.obj->pEntryId);
	if(er != erSuccess)
		goto exit;
	if(ulParentId > 0) {
		er = GetCacheManager()->GetEntryIdFromObject(ulParentId, NULL, 0, &notify.obj->pParentId);
		if(er != erSuccess)
			goto exit;
	}
	AddNotification(&notify, ulObjId, 0, 0, 0, isCounter);
exit:
	FreeNotificationStruct(&notify, false);
	return er;
}

ECRESULT ECSessionManager::NotificationCreated(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId)
{
	struct notification notify;

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		return erSuccess;
	notify.obj = s_alloc<notificationObject>(nullptr);
	notify.ulEventType			= fnevObjectCreated;
	notify.obj->ulObjType		= ulObjType;

	auto er = GetCacheManager()->GetEntryIdFromObject(ulObjId, nullptr, 0, &notify.obj->pEntryId);
	if(er != erSuccess)
		goto exit;
	er = GetCacheManager()->GetEntryIdFromObject(ulParentId, NULL, 0, &notify.obj->pParentId);
	if(er != erSuccess)
		goto exit;
	AddNotification(&notify, ulObjId);
exit:
	FreeNotificationStruct(&notify, false);
	return er;
}

ECRESULT ECSessionManager::NotificationMoved(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId, unsigned int ulOldParentId, entryId *lpOldEntryId)
{
	struct notification notify;

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		return erSuccess;
	notify.obj = s_alloc<notificationObject>(nullptr);
	notify.ulEventType				= fnevObjectMoved;
	notify.obj->ulObjType			= ulObjType;

	auto er = GetCacheManager()->GetEntryIdFromObject(ulObjId, nullptr, 0, &notify.obj->pEntryId);
	if(er != erSuccess)
		goto exit;
	er = GetCacheManager()->GetEntryIdFromObject(ulParentId, NULL, 0, &notify.obj->pParentId);
	if(er != erSuccess)
		goto exit;
	er = GetCacheManager()->GetEntryIdFromObject(ulOldParentId, NULL, 0, &notify.obj->pOldParentId);
	if(er != erSuccess)
		goto exit;
	notify.obj->pOldId = lpOldEntryId;
	AddNotification(&notify, ulObjId);
	notify.obj->pOldId = NULL;
exit:
	FreeNotificationStruct(&notify, false);
	return er;
}

ECRESULT ECSessionManager::NotificationCopied(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId, unsigned int ulOldObjId, unsigned int ulOldParentId)
{
	struct notification notify;

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		return erSuccess;
	notify.obj = s_alloc<notificationObject>(nullptr);
	notify.ulEventType				= fnevObjectCopied;
	notify.obj->ulObjType			= ulObjType;

	auto er = GetCacheManager()->GetEntryIdFromObject(ulObjId, nullptr, 0, &notify.obj->pEntryId);
	if(er != erSuccess)
		goto exit;
	er = GetCacheManager()->GetEntryIdFromObject(ulParentId, NULL, 0, &notify.obj->pParentId);
	if(er != erSuccess)
		goto exit;
	if(ulOldObjId > 0) {
		er = GetCacheManager()->GetEntryIdFromObject(ulOldObjId, NULL, 0, &notify.obj->pOldId);
		if(er != erSuccess)
			goto exit;
	}
	if(ulOldParentId > 0) {
		er = GetCacheManager()->GetEntryIdFromObject(ulOldParentId, NULL, 0, &notify.obj->pOldParentId);
		if(er != erSuccess)
			goto exit;
	}
	AddNotification(&notify, ulObjId);
exit:
	FreeNotificationStruct(&notify, false);
	return er;
}

/**
 * Send "Search complete" notification to the client.
 *
 * @param ulObjId object id of the search folder
 *
 * @return Kopano error code
 */
ECRESULT ECSessionManager::NotificationSearchComplete(unsigned int ulObjId, unsigned int ulStoreId)
{
	struct notification notify;

	notify.obj = s_alloc<notificationObject>(nullptr);
	notify.ulEventType				= fnevSearchComplete;
	notify.obj->ulObjType			= MAPI_FOLDER;
	auto er = GetCacheManager()->GetEntryIdFromObject(ulObjId, nullptr, 0, &notify.obj->pEntryId);
	if(er != erSuccess)
		goto exit;
	AddNotification(&notify, ulObjId, ulStoreId);
exit:
	FreeNotificationStruct(&notify, false);
	return er;
}

ECRESULT ECSessionManager::NotificationChange(const std::set<unsigned int> &syncIds,
    unsigned int ulChangeId, unsigned int ulChangeType)
{
	std::shared_lock<KC::shared_mutex> grplk(m_hGroupLock);
	// Send the notification to all sessionsgroups so that any client listening for these
	// notifications can receive them
	for (const auto &p : m_mapSessionGroups)
		p.second->AddChangeNotification(syncIds, ulChangeId, ulChangeType);
	return erSuccess;
}

/**
 * Get the sessionmanager statistics
 *
 * @param[in] callback	Callback to the statistics collector
 * @param[in] obj pointer to the statistics collector
 */
void ECSessionManager::update_extra_stats()
{
	auto &s = *m_stats;
	auto sSessionStats = get_stats();
	s.setg("sessions", "Number of sessions", sSessionStats.session.ulItems);
	s.setg("sessions_size", "Memory usage of sessions", sSessionStats.session.ullSize);
	s.setg("sessiongroups", "Number of session groups", sSessionStats.group.ulItems);
	s.setg("sessiongroups_size", "Memory usage of session groups", sSessionStats.group.ullSize);
	s.setg("persist_conn", "Persistent connections", sSessionStats.ulPersistentByConnection);
	s.setg("persist_conn_size", "Memory usage of persistent connections", sSessionStats.ulPersistentByConnectionSize);
	s.setg("persist_sess", "Persistent sessions", sSessionStats.ulPersistentBySession);
	s.setg("persist_sess_size", "Memory usage of persistent sessions", sSessionStats.ulPersistentBySessionSize);
	s.setg("tables_subscr", "Tables subscribed", sSessionStats.ulTableSubscriptions);
	s.setg("tables_subscr_size", "Memory usage of subscribed tables", sSessionStats.ulTableSubscriptionSize);
	s.setg("object_subscr", "Objects subscribed", sSessionStats.ulObjectSubscriptions);
	s.setg("object_subscr_size", "Memory usage of subscribed objects", sSessionStats.ulObjectSubscriptionSize);

	auto sSearchStats = m_lpSearchFolders->get_stats();
	s.setg("searchfld_stores", "Number of stores in use by search folders", sSearchStats.ulStores);
	s.setg("searchfld_folders", "Number of folders in use by search folders", sSearchStats.ulFolders);
	s.setg("searchfld_events", "Number of events waiting for searchfolder updates", sSearchStats.ulEvents);
	s.setg("searchfld_size", "Memory usage of search folders", sSearchStats.ullSize);

	auto cm = GetCacheManager();
	if (cm != nullptr)
		cm->update_extra_stats(s);

	/* It's not the same as the AUTO_INCREMENT value, but good enough. */
	ECDatabase *db = nullptr;
	DB_RESULT result;
	if (m_lpDatabaseFactory.get()->get_tls_db(&db) == erSuccess &&
	    db->DoSelect("SELECT MAX(id) FROM hierarchy", &result) == erSuccess) {
		auto row = result.fetch_row();
		if (row != nullptr && row[0] != nullptr)
			s.set(SCN_DATABASE_MAX_OBJECTID, atoui(row[0]));
	}
}

/**
 * Collect session statistics
 *
 * @param[out] sStats	The statistics
 *
 */
sSessionManagerStats ECSessionManager::get_stats()
{
	sSessionManagerStats sStats;

	// Get session data
	std::shared_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
	sStats.session.ulItems = m_mapSessions.size();
	sStats.session.ullSize = MEMORY_USAGE_MAP(sStats.session.ulItems, SESSIONMAP);
	l_cache.unlock();
	sStats.session.ulLocked = 0;
	sStats.session.ulOpenTables = 0;

	// Get group data
	std::shared_lock<KC::shared_mutex> l_group(m_hGroupLock);
	sStats.group.ulItems = m_mapSessionGroups.size();
	sStats.group.ullSize = MEMORY_USAGE_HASHMAP(sStats.group.ulItems, EC_SESSIONGROUPMAP);

	for (const auto &psg : m_mapSessionGroups)
		sStats.group.ullSize += psg.second->GetObjectSize();
	l_group.unlock();

	// persistent connections/sessions
	ulock_normal l_per(m_mutexPersistent);
	sStats.ulPersistentByConnection = m_mapPersistentByConnection.size();
	sStats.ulPersistentByConnectionSize = MEMORY_USAGE_HASHMAP(sStats.ulPersistentByConnection, PERSISTENTBYCONNECTION);
	sStats.ulPersistentBySession = m_mapPersistentBySession.size();
	sStats.ulPersistentBySessionSize = MEMORY_USAGE_HASHMAP(sStats.ulPersistentBySession, PERSISTENTBYSESSION);
	l_per.unlock();

	// Table subscriptions
	ulock_normal l_tblsub(m_mutexTableSubscriptions);
	sStats.ulTableSubscriptions = m_mapTableSubscriptions.size();
	sStats.ulTableSubscriptionSize = MEMORY_USAGE_MULTIMAP(sStats.ulTableSubscriptions, TABLESUBSCRIPTIONMULTIMAP);
	l_tblsub.unlock();

	// Object subscriptions
	ulock_normal l_objsub(m_mutexObjectSubscriptions);
	sStats.ulObjectSubscriptions = m_mapObjectSubscriptions.size();
	sStats.ulObjectSubscriptionSize = MEMORY_USAGE_MULTIMAP(sStats.ulObjectSubscriptions, OBJECTSUBSCRIPTIONSMULTIMAP);
	l_objsub.unlock();

	return sStats;
}

ECRESULT ECSessionManager::GetServerGUID(GUID* lpServerGuid){
	if (lpServerGuid == NULL)
		return KCERR_INVALID_PARAMETER;
	*lpServerGuid = m_server_guid;
	return erSuccess;
}

ECRESULT ECSessionManager::GetNewSourceKey(SOURCEKEY* lpSourceKey){
	if (lpSourceKey == NULL)
		return KCERR_INVALID_PARAMETER;

	scoped_lock l_inc(m_hSourceKeyAutoIncrementMutex);
	if (m_ulSourceKeyQueue == 0) {
		auto er = SaveSourceKeyAutoIncrement(m_ullSourceKeyAutoIncrement + 50);
		if (er != erSuccess)
			return er;
		m_ulSourceKeyQueue = 50;
	}
	*lpSourceKey = SOURCEKEY(m_server_guid, m_ullSourceKeyAutoIncrement + 1);
	++m_ullSourceKeyAutoIncrement;
	--m_ulSourceKeyQueue;
	return erSuccess;
}

ECRESULT ECSessionManager::SaveSourceKeyAutoIncrement(unsigned long long ullNewSourceKeyAutoIncrement){
	auto er = CreateDatabaseConnection();
	if(er != erSuccess)
		return er;
	ullNewSourceKeyAutoIncrement = cpu_to_le64(ullNewSourceKeyAutoIncrement);
	std::string strQuery = "UPDATE `settings` SET `value` = " + m_lpDatabase->EscapeBinary(reinterpret_cast<unsigned char *>(&ullNewSourceKeyAutoIncrement), 8) + " WHERE `name` = 'source_key_auto_increment'";
	return m_lpDatabase->DoUpdate(strQuery);
	// @TODO if this failed we want to retry this
}

BOOL ECSessionManager::IsSessionPersistent(ECSESSIONID sessionID)
{
	scoped_lock lock(m_mutexPersistent);
	auto iterSession = m_mapPersistentBySession.find(sessionID);
	return iterSession != m_mapPersistentBySession.cend();
}

// @todo make this function with a map of seq ids
ECRESULT ECSessionManager::GetNewSequence(SEQUENCE seq, unsigned long long *lpllSeqId)
{
	std::string strSeqName;

	auto er = CreateDatabaseConnection();
	if(er != erSuccess)
		return er;
	if (seq == SEQ_IMAP)
		strSeqName = "imapseq";
	else
		return KCERR_INVALID_PARAMETER;

	scoped_lock lock(m_hSeqMutex);
	if (m_ulSeqIMAPQueue == 0)
	{
		er = m_lpDatabase->DoSequence(strSeqName, 50, &m_ulSeqIMAP);
		if (er != erSuccess)
			return er;
		m_ulSeqIMAPQueue = 50;
	}
	--m_ulSeqIMAPQueue;
	*lpllSeqId = m_ulSeqIMAP++;
	return erSuccess;
}

ECRESULT ECSessionManager::CreateDatabaseConnection()
{
	std::string strError;

	if (m_lpDatabase != nullptr)
		return erSuccess;
	auto er = m_lpDatabaseFactory->CreateDatabaseObject(&unique_tie(m_lpDatabase), strError);
	if (er != erSuccess)
		ec_log_crit("Unable to open connection to database: %s", strError.c_str());
	return er;
}

ECRESULT ECSessionManager::SubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE ulType, unsigned int ulTableRootObjectId, unsigned int ulObjectType, unsigned int ulObjectFlags, ECSESSIONID sessionID)
{
    TABLESUBSCRIPTION sSubscription;
	scoped_lock lock(m_mutexTableSubscriptions);

    sSubscription.ulType = ulType;
    sSubscription.ulRootObjectId = ulTableRootObjectId;
    sSubscription.ulObjectType = ulObjectType;
    sSubscription.ulObjectFlags = ulObjectFlags;
	m_mapTableSubscriptions.emplace(sSubscription, sessionID);
    return erSuccess;
}

ECRESULT ECSessionManager::UnsubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE ulType, unsigned int ulTableRootObjectId, unsigned int ulObjectType, unsigned int ulObjectFlags, ECSESSIONID sessionID)
{
    TABLESUBSCRIPTION sSubscription;
	scoped_lock lock(m_mutexTableSubscriptions);

    sSubscription.ulType = ulType;
    sSubscription.ulRootObjectId = ulTableRootObjectId;
    sSubscription.ulObjectType = ulObjectType;
    sSubscription.ulObjectFlags = ulObjectFlags;

    auto iter = m_mapTableSubscriptions.find(sSubscription);
    while (iter != m_mapTableSubscriptions.cend() &&
           iter->first == sSubscription) {
        if(iter->second == sessionID)
            break;
        ++iter;
    }

	if (iter == m_mapTableSubscriptions.cend())
		return KCERR_NOT_FOUND;
	m_mapTableSubscriptions.erase(iter);
    return erSuccess;
}

// Subscribes for all object notifications in store ulStoreID for session group sessionID
ECRESULT ECSessionManager::SubscribeObjectEvents(unsigned int ulStoreId, ECSESSIONGROUPID sessionID)
{
	scoped_lock lock(m_mutexObjectSubscriptions);
	m_mapObjectSubscriptions.emplace(ulStoreId, sessionID);
    return erSuccess;
}

ECRESULT ECSessionManager::UnsubscribeObjectEvents(unsigned int ulStoreId, ECSESSIONGROUPID sessionID)
{
	scoped_lock lock(m_mutexObjectSubscriptions);
	auto i = m_mapObjectSubscriptions.find(ulStoreId);
	while (i != m_mapObjectSubscriptions.cend() && i->first == ulStoreId &&
	       i->second != sessionID)
		++i;
	if (i != m_mapObjectSubscriptions.cend())
		m_mapObjectSubscriptions.erase(i);
    return erSuccess;
}

ECRESULT ECSessionManager::DeferNotificationProcessing(ECSESSIONID ecSessionId, struct soap *soap)
{
    // Let the notification  manager handle this request. We don't do anything more with the notification
    // request since the notification manager will handle it all

    return m_lpNotificationManager->AddRequest(ecSessionId, soap);
}

// Called when a notification is ready for a session group
ECRESULT ECSessionManager::NotifyNotificationReady(ECSESSIONID ecSessionId)
{
    return m_lpNotificationManager->NotifyChange(ecSessionId);
}

ECRESULT ECSessionManager::GetStoreSortLCID(ULONG ulStoreId, ULONG *lpLcid)
{
	ECDatabase		*lpDatabase = NULL;
	DB_RESULT lpDBResult;

	if (lpLcid == nullptr)
		return KCERR_INVALID_PARAMETER;

	auto cache = GetCacheManager();
	sObjectTableKey key(ulStoreId, 0);
	struct propVal prop;
	if (cache->GetCell(&key, PR_SORT_LOCALE_ID, &prop, nullptr) == erSuccess) {
		if (prop.ulPropTag == CHANGE_PROP_TYPE(PR_SORT_LOCALE_ID, PT_ERROR))
			return prop.Value.ul;
		*lpLcid = prop.Value.ul;
		return erSuccess;
	}

	auto er = m_lpDatabaseFactory.get()->get_tls_db(&lpDatabase);
	if(er != erSuccess)
		return er;

	std::string strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid=" + stringify(ulStoreId) +
				" AND tag=" + stringify(PROP_ID(PR_SORT_LOCALE_ID)) + " AND type=" + stringify(PROP_TYPE(PR_SORT_LOCALE_ID)) + " LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	auto lpDBRow = lpDBResult.fetch_row();

	struct propVal new_prop;
	if (lpDBRow == nullptr || lpDBRow[0] == nullptr) {
		new_prop.ulPropTag = CHANGE_PROP_TYPE(PR_SORT_LOCALE_ID, PT_ERROR);
		new_prop.Value.ul = KCERR_NOT_FOUND;
		new_prop.__union = SOAP_UNION_propValData_ul;
		er = cache->SetCell(&key, PR_SORT_LOCALE_ID, &new_prop);
		if (er != erSuccess)
			return er;
		return KCERR_NOT_FOUND;
	}

	new_prop.ulPropTag = PR_SORT_LOCALE_ID;
	new_prop.Value.ul = *lpLcid = strtoul(lpDBRow[0], nullptr, 10);
	new_prop.__union = SOAP_UNION_propValData_ul;
	return cache->SetCell(&key, PR_SORT_LOCALE_ID, &new_prop);
}

LPCSTR ECSessionManager::GetDefaultSortLocaleID()
{
	return GetConfig()->GetSetting("default_sort_locale_id");
}

ULONG ECSessionManager::GetSortLCID(ULONG ulStoreId)
{
	ULONG ulLcid = 0;

	auto er = GetStoreSortLCID(ulStoreId, &ulLcid);
	if (er == erSuccess)
		return ulLcid;
	auto lpszLocaleId = GetDefaultSortLocaleID();
	if (lpszLocaleId == NULL || *lpszLocaleId == '\0')
		return 0; // Select default LCID
	er = LocaleIdToLCID(lpszLocaleId, &ulLcid);
	if (er != erSuccess)
		return 0; // Select default LCID
	return ulLcid;
}

ECLocale ECSessionManager::GetSortLocale(ULONG ulStoreId)
{
	ULONG			ulLcid = 0;
	LPCSTR			lpszLocaleId = NULL;

	auto er = GetStoreSortLCID(ulStoreId, &ulLcid);
	if (er == erSuccess)
		er = LCIDToLocaleId(ulLcid, &lpszLocaleId);
	if (er != erSuccess) {
		lpszLocaleId = GetDefaultSortLocaleID();
		if (lpszLocaleId == NULL || *lpszLocaleId == '\0')
			lpszLocaleId = "";	// Select default localeid
	}
	return createLocaleFromName(lpszLocaleId);
}

/**
 * Remove busy state for session
 *
 * Finds a session and calls RemoveBusyState on it if it is found
 *
 * @param[in] ecSessionId Session ID of session to remove busy state from
 * @param[in] thread Thread ID to remove busy state of
 * @return result
 */
ECRESULT ECSessionManager::RemoveBusyState(ECSESSIONID ecSessionId, pthread_t thread)
{
	ECRESULT er = erSuccess;
	ECSession *lpECSession = NULL;
	std::shared_lock<KC::shared_mutex> l_cache(m_hCacheRWLock);
	auto lpSession = GetSession(ecSessionId, true);
	l_cache.unlock();
	if(!lpSession)
		goto exit;

	lpECSession = dynamic_cast<ECSession *>(lpSession);
	if(!lpECSession) {
		assert(lpECSession != NULL);
		goto exit;
	}
	lpECSession->RemoveBusyState(thread);
exit:
	if(lpSession)
		lpSession->unlock();
	return er;
}

ECLockManagerPtr ECLockManager::Create()
{
	return ECLockManagerPtr(new ECLockManager);
}

ECRESULT ECLockManager::LockObject(unsigned int objid, ECSESSIONID sid,
    ECObjectLock *objlock)
{
	ECRESULT er = erSuccess;
	std::lock_guard<KC::shared_mutex> lock(m_hRwLock);
	auto res = m_mapLocks.emplace(objid, sid);
	if (!res.second && res.first->second != sid)
		er = KCERR_NO_ACCESS;
	if (objlock != nullptr)
		*objlock = ECObjectLock(shared_from_this(), objid, sid);
	return er;
}

ECRESULT ECLockManager::UnlockObject(unsigned int objid, ECSESSIONID sid)
{
	std::lock_guard<KC::shared_mutex> lock(m_hRwLock);
	auto i = m_mapLocks.find(objid);
	if (i == m_mapLocks.cend())
		return KCERR_NOT_FOUND;
	else if (i->second != sid)
		return KCERR_NO_ACCESS;
	else
		m_mapLocks.erase(i);
	return erSuccess;
}

bool ECLockManager::IsLocked(unsigned int objid, ECSESSIONID *sid)
{
	std::shared_lock<KC::shared_mutex> lock(m_hRwLock);
	auto i = m_mapLocks.find(objid);
	if (i != m_mapLocks.cend() && sid != nullptr)
		*sid = i->second;
	return i != m_mapLocks.end();
}

ECRESULT ECSessionManager::get_user_count(usercount_t *uc)
{
	ECDatabase *db = nullptr;
	DB_RESULT result;
	DB_ROW row;
	auto er = m_lpDatabaseFactory.get()->get_tls_db(&db);
	if (er != erSuccess)
		return er;
	auto query =
		"SELECT COUNT(*), objectclass FROM users "
		"WHERE externid IS NOT NULL " /* Keep local entries outside of COUNT() */
		"AND "s + OBJECTCLASS_COMPARE_SQL("objectclass", OBJECTCLASS_USER) + " "
		"GROUP BY objectclass";
	er = db->DoSelect(query, &result);
	if (er != erSuccess)
		return er;

	unsigned int act = 0, nonact = 0, room = 0, eqp = 0, contact = 0;
	while ((row = result.fetch_row()) != nullptr) {
		if (row[0] == nullptr || row[1] == nullptr)
			continue;
		switch (atoi(row[1])) {
		case ACTIVE_USER: act = atoi(row[0]); break;
		case NONACTIVE_USER: nonact = atoi(row[0]); break;
		case NONACTIVE_ROOM: room = atoi(row[0]); break;
		case NONACTIVE_EQUIPMENT: eqp = atoi(row[0]); break;
		case NONACTIVE_CONTACT: contact = atoi(row[0]); break;
		}
	}

	if (uc != nullptr)
		uc->assign(act, nonact, room, eqp, contact);
	std::lock_guard<std::recursive_mutex> lock(m_usercount_mtx);
	m_usercount.assign(act, nonact, room, eqp, contact);
	m_usercount_ts = decltype(m_usercount_ts)::clock::now();
	return erSuccess;
}

ECRESULT ECSessionManager::get_user_count_cached(usercount_t *uc)
{
	std::lock_guard<std::recursive_mutex> lock(m_usercount_mtx);
	if (!m_usercount.is_valid() || m_usercount_ts - decltype(m_usercount_ts)::clock::now() > std::chrono::seconds(5))
		return get_user_count(uc);
	if (uc != nullptr)
		*uc = m_usercount;
	return erSuccess;
}

} /* namespace */
