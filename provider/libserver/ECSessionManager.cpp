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
#include <algorithm>
#include <new>
#include <typeinfo>
#include <sys/time.h>
#include <mapidefs.h>
#include <mapitags.h>
#include "ECMAPI.h"
#include "ECDatabase.h"
#include "ECSessionGroup.h"
#include "ECSessionManager.h"
#include "ECStatsCollector.h"
#include "ECTPropsPurge.h"
#include "ECLicenseClient.h"
#include "ECDatabaseUtils.h"
#include "ECSecurity.h"
#include "SSLUtil.h"
#include <kopano/Trace.h>
#include "kcore.hpp"
#include "ECICS.h"
#include <edkmdb.h>
#include "logontime.hpp"

ECSessionManager::ECSessionManager(ECConfig *lpConfig, ECLogger *lpAudit,
    bool bHostedKopano, bool bDistributedKopano)
{
	int err = 0;

	bExit					= FALSE;
	m_lpConfig				= lpConfig;
	m_lpAudit				= lpAudit;
	if (m_lpAudit)
		m_lpAudit->AddRef();
	m_bHostedKopano			= bHostedKopano;
	m_bDistributedKopano	= bDistributedKopano;
	m_lpDatabase			= NULL;

	// Create a rwlock with no initial owner.
	pthread_rwlock_init(&m_hCacheRWLock, NULL);
	pthread_rwlock_init(&m_hGroupLock, NULL);
	pthread_mutex_init(&m_hExitMutex, NULL);
	pthread_mutex_init(&m_mutexPersistent, NULL);
	pthread_mutex_init(&m_mutexTableSubscriptions, NULL);
	pthread_mutex_init(&m_mutexObjectSubscriptions, NULL);

	//Terminate Event
	pthread_cond_init(&m_hExitSignal, NULL);

	m_lpDatabaseFactory = new ECDatabaseFactory(lpConfig);
	m_lpPluginFactory = new ECPluginFactory(lpConfig, g_lpStatsCollector, bHostedKopano, bDistributedKopano);
	m_lpECCacheManager = new ECCacheManager(lpConfig, m_lpDatabaseFactory);
	m_lpSearchFolders = new ECSearchFolders(this, m_lpDatabaseFactory);
	m_lpTPropsPurge = new ECTPropsPurge(lpConfig, m_lpDatabaseFactory);
	m_ptrLockManager = ECLockManager::Create();
	
	m_lpServerGuid = NULL;
	m_ullSourceKeyAutoIncrement = 0;
	m_ulSourceKeyQueue = 0;

	m_ulSeqIMAP = 0;
	m_ulSeqIMAPQueue = 0;

	pthread_mutex_init(&m_hSourceKeyAutoIncrementMutex, NULL);
	pthread_mutex_init(&m_hSeqMutex, NULL);

	// init ssl randomness for session id's
	ssl_random_init();

	//Create session clean up thread
	err = pthread_create(&m_hSessionCleanerThread, NULL, SessionCleaner, (void*)this);
        set_thread_name(m_hSessionCleanerThread, "SessionCleanUp");
	
	if (err != 0)
		ec_log_crit("Unable to spawn thread for session cleaner! Sessions will live forever!: %s", strerror(err));

	m_lpNotificationManager = new ECNotificationManager();
}

ECSessionManager::~ECSessionManager()
{
	int err = 0;
	SESSIONMAP::const_iterator iSession, iSessionNext;

	pthread_mutex_lock(&m_hExitMutex);
	bExit = TRUE;
	pthread_cond_signal(&m_hExitSignal);

	pthread_mutex_unlock(&m_hExitMutex);
	delete m_lpTPropsPurge;
	delete m_lpDatabase;
	delete m_lpDatabaseFactory;
		
	err = pthread_join(m_hSessionCleanerThread, NULL);
	
	if (err != 0)
		ec_log_crit("Unable to join session cleaner thread: %s", strerror(err));

	pthread_rwlock_wrlock(&m_hCacheRWLock);
			
	/* Clean up all sessions */
	iSession = m_mapSessions.begin();
	while(iSession != m_mapSessions.end()) {
		delete iSession->second;
		iSessionNext = iSession;
		++iSessionNext;
		ec_log_info("End of session (shutdown) %llu",
			static_cast<unsigned long long>(iSession->first));

		m_mapSessions.erase(iSession);

		iSession = iSessionNext;
	}
	
	delete m_lpNotificationManager;
//#ifdef DEBUG
	// Clearing the cache takes too long while shutting down
	delete m_lpECCacheManager;
//#endif
	delete m_lpSearchFolders;
	delete m_lpPluginFactory;
	delete m_lpServerGuid;
	if (m_lpAudit != NULL)
		m_lpAudit->Release();

	// Release ownership of the rwlock object.
	pthread_rwlock_unlock(&m_hCacheRWLock);

	pthread_rwlock_destroy(&m_hCacheRWLock);
	pthread_rwlock_destroy(&m_hGroupLock);

	pthread_mutex_destroy(&m_hSourceKeyAutoIncrementMutex);
	pthread_mutex_destroy(&m_hSeqMutex);

	pthread_mutex_destroy(&m_hExitMutex);
	pthread_mutex_destroy(&m_mutexPersistent);
	
	pthread_mutex_destroy(&m_mutexTableSubscriptions);
	pthread_mutex_destroy(&m_mutexObjectSubscriptions);	

	pthread_cond_destroy(&m_hExitSignal);
}

ECRESULT ECSessionManager::LoadSettings(){
	ECRESULT		er = erSuccess;
		
	ECDatabase *	lpDatabase = NULL;
	DB_RESULT		lpDBResult = NULL;
	DB_ROW			lpDBRow = NULL;
	DB_LENGTHS		lpDBLenths = NULL;
	std::string		strQuery;

	if(m_lpServerGuid != NULL){
		er = KCERR_BAD_VALUE;
		goto exit;
	}

	er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if(er != erSuccess)
		goto exit;
	
	strQuery = "SELECT `value` FROM settings WHERE `name` = 'server_guid'";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	lpDBRow = lpDatabase->FetchRow(lpDBResult);
	lpDBLenths = lpDatabase->FetchRowLengths(lpDBResult);
	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBLenths == NULL || lpDBLenths[0] != sizeof(GUID)) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	m_lpServerGuid = new GUID;

	memcpy(m_lpServerGuid, lpDBRow[0], sizeof(GUID));

	if (lpDBResult)
		lpDatabase->FreeResult(lpDBResult);
	lpDBResult = NULL;

	strQuery = "SELECT `value` FROM settings WHERE `name` = 'source_key_auto_increment'";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	lpDBRow = lpDatabase->FetchRow(lpDBResult);
	lpDBLenths = lpDatabase->FetchRowLengths(lpDBResult);
	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBLenths == NULL || lpDBLenths[0] != 8) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	memcpy(&m_ullSourceKeyAutoIncrement, lpDBRow[0], sizeof(m_ullSourceKeyAutoIncrement));

exit:
	if (lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	return er;
}

ECRESULT ECSessionManager::CheckUserLicense()
{
	ECRESULT er = erSuccess;
	ECSession *lpecSession = NULL;
	unsigned int ulLicense = 0;

	er = this->CreateSessionInternal(&lpecSession);
	if (er != erSuccess)
		goto exit;

	lpecSession->Lock();

	er = lpecSession->GetUserManagement()->CheckUserLicense(&ulLicense);
	if (er != erSuccess)
		goto exit;

	if (ulLicense & USERMANAGEMENT_USER_LICENSE_EXCEEDED) {
		ec_log_err("Failed to start server: Your license does not permit this amount of users.");
		er = KCERR_NO_ACCESS;
		goto exit;
	}

exit:
	if(lpecSession) {
		lpecSession->Unlock(); // Lock the session
		this->RemoveSessionInternal(lpecSession);
	}

	return er;
}

/*
 * This function is threadsafe since we hold the lock the the group list, and the session retrieved from the grouplist
 * is locked so it cannot be deleted by other sessions, while we hold the lock for the group list.
 *
 * Other sessions may release the session group, even if they are the last, while we are in this function since
 * deletion of the session group only occurs within DeleteIfOrphaned(), and this function guarantees that the caller
 * will receive a sessiongroup that is not an orphan unless the caller releases the session group.
 */
ECRESULT ECSessionManager::GetSessionGroup(ECSESSIONGROUPID sessionGroupID, ECSession *lpSession, ECSessionGroup **lppSessionGroup)
{
	ECRESULT er = erSuccess;
	ECSessionGroup *lpSessionGroup = NULL;

	pthread_rwlock_rdlock(&m_hGroupLock);

	/* Workaround for old clients, when sessionGroupID is 0 each session is its own group */
	if (sessionGroupID == 0) {
		lpSessionGroup = new ECSessionGroup(sessionGroupID, this);
		g_lpStatsCollector->Increment(SCN_SESSIONGROUPS_CREATED);
	} else {
		EC_SESSIONGROUPMAP::const_iterator iter = m_mapSessionGroups.find(sessionGroupID);
		/* Check if the SessionGroup already exists on the server */
		if (iter == m_mapSessionGroups.end()) {
			// "upgrade" lock to insert new session
			pthread_rwlock_unlock(&m_hGroupLock);
			pthread_rwlock_wrlock(&m_hGroupLock);
			lpSessionGroup = new ECSessionGroup(sessionGroupID, this);
			m_mapSessionGroups.insert(EC_SESSIONGROUPMAP::value_type(sessionGroupID, lpSessionGroup));
			g_lpStatsCollector->Increment(SCN_SESSIONGROUPS_CREATED);
		} else
			lpSessionGroup = iter->second;
	}
	
	lpSessionGroup->AddSession(lpSession);

	pthread_rwlock_unlock(&m_hGroupLock);

	*lppSessionGroup = lpSessionGroup;

	return er;
}

ECRESULT ECSessionManager::DeleteIfOrphaned(ECSessionGroup *lpGroup)
{
	ECSessionGroup *lpSessionGroup = NULL;
	ECSESSIONGROUPID id = lpGroup->GetSessionGroupId();

	if (id != 0) {
		pthread_rwlock_wrlock(&m_hGroupLock);

    	/* Check if the SessionGroup actually exists, if it doesn't just return without error */
	EC_SESSIONGROUPMAP::const_iterator i = m_mapSessionGroups.find(id);
    	if (i == m_mapSessionGroups.end()) {
			pthread_rwlock_unlock(&m_hGroupLock);
			return erSuccess;
    	}

    	/* If this was the last Session, delete the SessionGroup */
    	if (i->second->isOrphan()) {
    	    lpSessionGroup = i->second;
    	    m_mapSessionGroups.erase(i);
    	}

		pthread_rwlock_unlock(&m_hGroupLock);
	} else
		lpSessionGroup = lpGroup;

	if (lpSessionGroup) {
		delete lpSessionGroup;
		g_lpStatsCollector->Increment(SCN_SESSIONGROUPS_DELETED);
	}
	return erSuccess;
}

BTSession* ECSessionManager::GetSession(ECSESSIONID sessionID, bool fLockSession) {

	SESSIONMAP::const_iterator iIterator;
	BTSession *lpSession = NULL;
		
	//TRACE_INTERNAL(TRACE_ENTRY, "ECSessionManager", "GetSession", "%lu", sessionID);

	iIterator = m_mapSessions.find(sessionID);

	if(iIterator != m_mapSessions.end()){
		lpSession = iIterator->second;
		lpSession->UpdateSessionTime();
		
		if(fLockSession)
			lpSession->Lock();
	}else{
		//EC_SESSION_LOST
	}
	
	//TRACE_INTERNAL(TRACE_RETURN, "ECSessionManager", "GetSession", "%lu", sessionID);
	return lpSession;
}

// Clean up all current sessions
ECRESULT ECSessionManager::RemoveAllSessions()
{
	ECRESULT		er = erSuccess;
	BTSession		*lpSession = NULL;
	SESSIONMAP::const_iterator iIterSession, iSessionNext;
	std::list<BTSession *> lstSessions;
	std::list<BTSession *>::const_iterator iterSessionList;
	
	// Lock the session map since we're going to remove all the sessions.
	pthread_rwlock_wrlock(&m_hCacheRWLock);

	ec_log_info("Shutdown all current sessions");

	iIterSession = m_mapSessions.begin();
	while(iIterSession != m_mapSessions.end())
	{
		lpSession = iIterSession->second;
		iSessionNext = iIterSession;
		++iSessionNext;
		m_mapSessions.erase(iIterSession);

		iIterSession = iSessionNext;

		lstSessions.push_back(lpSession);
	}

	// Release ownership of the mutex object.
	pthread_rwlock_unlock(&m_hCacheRWLock);
	
	// Do the actual session deletes, while the session map is not locked (!)
	for (iterSessionList = lstSessions.begin();
	     iterSessionList != lstSessions.end(); ++iterSessionList)
	    delete *iterSessionList;
	
	return er;
}

ECRESULT ECSessionManager::CancelAllSessions(ECSESSIONID sessionIDException)
{
	ECRESULT		er = erSuccess;
	BTSession		*lpSession = NULL;
	SESSIONMAP::const_iterator iIterSession, iSessionNext;
	std::list<BTSession *> lstSessions;
	std::list<BTSession *>::const_iterator iterSessionList;
	
	// Lock the session map since we're going to remove all the sessions.
	pthread_rwlock_wrlock(&m_hCacheRWLock);

	ec_log_info("Shutdown all current sessions");

	iIterSession = m_mapSessions.begin();
	while(iIterSession != m_mapSessions.end())
	{
		if(iIterSession->first != sessionIDException) {
			lpSession = iIterSession->second;
			iSessionNext = iIterSession;
			++iSessionNext;
			// Tell the notification manager to wake up anyone waiting for this session
			m_lpNotificationManager->NotifyChange(iIterSession->first);
			
			m_mapSessions.erase(iIterSession);

			iIterSession = iSessionNext;

			lstSessions.push_back(lpSession);
		} else {
			++iIterSession;
		}
	}

	// Release ownership of the mutex object.
	pthread_rwlock_unlock(&m_hCacheRWLock);
	
	// Do the actual session deletes, while the session map is not locked (!)
	for (iterSessionList = lstSessions.begin();
	     iterSessionList != lstSessions.end(); ++iterSessionList)
	    delete *iterSessionList;
	
	return er;
}

// call a function for all sessions available
// used by ECStatsTable
ECRESULT ECSessionManager::ForEachSession(void(*callback)(ECSession*, void*), void *obj)
{
	ECRESULT er = erSuccess;
	SESSIONMAP::const_iterator iIterSession;

	pthread_rwlock_rdlock(&m_hCacheRWLock);

	for (iIterSession = m_mapSessions.begin();
	     iIterSession != m_mapSessions.end(); ++iIterSession)
		callback(dynamic_cast<ECSession*>(iIterSession->second), obj);

	pthread_rwlock_unlock(&m_hCacheRWLock);

	return er;
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
ECRESULT ECSessionManager::ValidateSession(struct soap *soap, ECSESSIONID sessionID, ECAuthSession **lppSession, bool fLockSession)
{
	ECRESULT er;
	BTSession *lpSession = NULL;

	er = this->ValidateBTSession(soap, sessionID, &lpSession, fLockSession);
	if (er != erSuccess)
		return er;
	*lppSession = dynamic_cast<ECAuthSession*>(lpSession);
	return erSuccess;
}

ECRESULT ECSessionManager::ValidateSession(struct soap *soap, ECSESSIONID sessionID, ECSession **lppSession, bool fLockSession)
{
	ECRESULT er;
	BTSession *lpSession = NULL;

	er = this->ValidateBTSession(soap, sessionID, &lpSession, fLockSession);
	if (er != erSuccess)
		return er;
	*lppSession = dynamic_cast<ECSession*>(lpSession);
	return erSuccess;
}

ECRESULT ECSessionManager::ValidateBTSession(struct soap *soap, ECSESSIONID sessionID, BTSession **lppSession, bool fLockSession)
{
	ECRESULT er;
	BTSession*		lpSession	= NULL;
	
	// Read lock
	pthread_rwlock_rdlock(&m_hCacheRWLock);
	
	lpSession = GetSession(sessionID, fLockSession);
	
	pthread_rwlock_unlock(&m_hCacheRWLock);

	if (lpSession == NULL)
		return KCERR_END_OF_SESSION;
	lpSession->RecordRequest(soap);
	
	er = lpSession->ValidateOriginator(soap);
	if (er != erSuccess) {
		if (fLockSession)
			lpSession->Unlock();
		lpSession = NULL;
		return er;
	}

	/* Enable compression if client desired and granted */
	if (lpSession->GetCapabilities() & KOPANO_CAP_COMPRESSION) {
		soap_set_imode(soap, SOAP_ENC_ZLIB);
		soap_set_omode(soap, SOAP_ENC_ZLIB | SOAP_IO_CHUNK);
	}

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
	ECAuthSession *lpAuthSession = NULL;
	ECSESSIONID newSessionID;

	CreateSessionID(ulCapabilities, &newSessionID);

	lpAuthSession = new(std::nothrow) ECAuthSession(GetSourceAddr(soap), newSessionID, m_lpDatabaseFactory, this, ulCapabilities);
	if (lpAuthSession == NULL)
		return KCERR_NOT_ENOUGH_MEMORY;
	if (bLockSession)
	        lpAuthSession->Lock();
	if (bRegisterSession) {
		pthread_rwlock_wrlock(&m_hCacheRWLock);
		m_mapSessions.insert( SESSIONMAP::value_type(newSessionID, lpAuthSession) );
		pthread_rwlock_unlock(&m_hCacheRWLock);
		g_lpStatsCollector->Increment(SCN_SESSIONS_CREATED);
	}

	*sessionID = newSessionID;
	*lppAuthSession = lpAuthSession;
	return erSuccess;
}

ECRESULT ECSessionManager::CreateSession(struct soap *soap, char *szName, char *szPassword, char *szImpersonateUser, char *szClientVersion, char *szClientApp, const char *szClientAppVersion, const char *szClientAppMisc, unsigned int ulCapabilities, ECSESSIONGROUPID sessionGroupID, ECSESSIONID *lpSessionID, ECSession **lppSession, bool fLockSession, bool fAllowUidAuth)
{
	ECRESULT		er			= erSuccess;
	ECAuthSession	*lpAuthSession	= NULL;
	ECSession		*lpSession	= NULL;
	const char		*method = "error";
	std::string		from;
	CONNECTION_TYPE ulType;

	ulType = SOAP_CONNECTION_TYPE(soap);
	if (ulType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY) {
		from = string("file://") + m_lpConfig->GetSetting("server_pipe_priority");
	} else if (ulType == CONNECTION_TYPE_NAMED_PIPE) {
		// connected through unix socket
		from = string("file://") + m_lpConfig->GetSetting("server_pipe_name");
	} else {
		// connected over network
		from = soap->host;
	}

	er = this->CreateAuthSession(soap, ulCapabilities, lpSessionID, &lpAuthSession, false, false);
	if (er != erSuccess)
		goto exit;

	// If we've connected with SSL, check if there is a certificate, and check if we accept that certificate for that user
	if (soap->ssl && lpAuthSession->ValidateUserCertificate(soap, szName, szImpersonateUser) == erSuccess) {
		g_lpStatsCollector->Increment(SCN_LOGIN_SSL);
		method = "SSL Certificate";
		goto authenticated;
	}

	// First, try socket authentication (dagent, won't print error)
	if(fAllowUidAuth && lpAuthSession->ValidateUserSocket(soap->socket, szName, szImpersonateUser) == erSuccess) {
		g_lpStatsCollector->Increment(SCN_LOGIN_SOCKET);
		method = "Pipe socket";
		goto authenticated;
	}

	// If that fails, try logon with supplied username/password (clients, may print logon error)
	if(lpAuthSession->ValidateUserLogon(szName, szPassword, szImpersonateUser) == erSuccess) {
		g_lpStatsCollector->Increment(SCN_LOGIN_PASSWORD);
		method = "User supplied password";
		goto authenticated;
	}

	// whoops, out of auth options.
	ec_log_warn("Failed to authenticate user \"%s\" from \"%s\" using program \"%s\"",
					szName, from.c_str(), szClientApp ? szClientApp : "<unknown>");

	ZLOG_AUDIT(m_lpAudit, "authenticate failed user='%s' from='%s' program='%s'",
			  szName, from.c_str(), szClientApp ? szClientApp : "<unknown>");

	er = KCERR_LOGON_FAILED;			
	g_lpStatsCollector->Increment(SCN_LOGIN_DENIED);
	goto exit;

authenticated:
	ec_log_debug("User \"%s\" from \"%s\" authenticated through \"%s\" using program %s", szName, from.c_str(), method, szClientApp ? szClientApp : "<unknown>");
	if (strcmp(KOPANO_SYSTEM_USER, szName) != 0) {
		/* Do not log successful SYSTEM logins */
		ZLOG_AUDIT(m_lpAudit, "authenticate ok user='%s' from='%s' method='%s' program='%s'",
				  szName, from.c_str(), method, szClientApp ? szClientApp : "<unknown>");
	}

	er = RegisterSession(lpAuthSession, sessionGroupID, szClientVersion, szClientApp, szClientAppVersion, szClientAppMisc, lpSessionID, &lpSession, fLockSession);
	if (er != erSuccess) {
		if (er == KCERR_NO_ACCESS && szImpersonateUser != NULL && *szImpersonateUser != '\0') {
			ec_log_err("Failed attempt to impersonate user \"%s\" by user \"%s\"", szImpersonateUser, szName);
			ZLOG_AUDIT(m_lpAudit, "impersonate failed user='%s', from='%s' program='%s' impersonator='%s'",
					  szImpersonateUser, from.c_str(), szClientApp ? szClientApp : "<unknown>", szName);
		} else
			ec_log_err("User \"%s\" authenticated, but failed to create session. Error 0x%08X", szName, er);
		goto exit;
	}
	if (!szImpersonateUser || *szImpersonateUser == '\0')
		ec_log_debug("User \"%s\" receives session %llu",
			szName, static_cast<unsigned long long>(*lpSessionID));
	else {
		ec_log_debug("User \"%s\" impersonated by \"%s\" receives session %llu",
			szImpersonateUser, szName,
			static_cast<unsigned long long>(*lpSessionID));
		ZLOG_AUDIT(m_lpAudit, "impersonate ok user='%s', from='%s' program='%s' impersonator='%s'",
				  szImpersonateUser, from.c_str(), szClientApp ? szClientApp : "<unknown>", szName);
	}

exit:
	delete lpAuthSession;
	*lppSession = lpSession;

	return er;
}

ECRESULT ECSessionManager::RegisterSession(ECAuthSession *lpAuthSession, ECSESSIONGROUPID sessionGroupID, char *szClientVersion, char *szClientApp, const char *szClientApplicationVersion, const char *szClientApplicationMisc, ECSESSIONID *lpSessionID, ECSession **lppSession, bool fLockSession)
{
	ECRESULT	er = erSuccess;
	ECSession	*lpSession = NULL;
	ECSESSIONID	newSID = 0;

	er = lpAuthSession->CreateECSession(sessionGroupID, szClientVersion ? szClientVersion : "", szClientApp ? szClientApp : "", szClientApplicationVersion ? szClientApplicationVersion : "", szClientApplicationMisc ? szClientApplicationMisc : "", &newSID, &lpSession);
	if (er != erSuccess)
		return er;

	if (fLockSession)
		lpSession->Lock();

	pthread_rwlock_wrlock(&m_hCacheRWLock);
	m_mapSessions.insert( SESSIONMAP::value_type(newSID, lpSession) );
	pthread_rwlock_unlock(&m_hCacheRWLock);

	*lpSessionID = newSID;
	*lppSession = lpSession;

	g_lpStatsCollector->Increment(SCN_SESSIONS_CREATED);

	return er;
}

ECRESULT ECSessionManager::CreateSessionInternal(ECSession **lppSession, unsigned int ulUserId)
{
	ECRESULT er;
	ECSession	*lpSession	= NULL;
	ECSESSIONID	newSID;

	CreateSessionID(KOPANO_CAP_LARGE_SESSIONID, &newSID);

	lpSession = new(std::nothrow) ECSession("<internal>", newSID, 0, m_lpDatabaseFactory, this, 0, false, ECSession::METHOD_NONE, 0, "internal", "kopano-server", "", "");
	if (lpSession == NULL)
		return KCERR_LOGON_FAILED;
	er = lpSession->GetSecurity()->SetUserContext(ulUserId, EC_NO_IMPERSONATOR);
	if (er != erSuccess) {
		delete lpSession;
		return er;
	}

	ec_log_debug("New internal session (%llu)",
		static_cast<unsigned long long>(newSID));

	g_lpStatsCollector->Increment(SCN_SESSIONS_INTERNAL_CREATED);

	*lppSession = lpSession;
	return erSuccess;
}

void ECSessionManager::RemoveSessionInternal(ECSession *lpSession)
{
	if (lpSession != NULL) {
		g_lpStatsCollector->Increment(SCN_SESSIONS_INTERNAL_DELETED);
		delete lpSession;
	}
}

ECRESULT ECSessionManager::RemoveSession(ECSESSIONID sessionID){

	ECRESULT	hr			= erSuccess;
	BTSession	*lpSession	= NULL;
	
	ec_log_debug("End of session (logoff) %llu",
		static_cast<unsigned long long>(sessionID));
	g_lpStatsCollector->Increment(SCN_SESSIONS_DELETED);

	// Make sure no other thread can read or write the sessions list
	pthread_rwlock_wrlock(&m_hCacheRWLock);

	// Get a session, don't lock it ourselves
	lpSession = GetSession(sessionID, false);

	// Remove the session from the list. No other threads can start new
	// requests on the session after this point
	m_mapSessions.erase(sessionID);

	// Release the mutex, other threads can now access the (updated) sessions list
	pthread_rwlock_unlock(&m_hCacheRWLock);

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

	return hr;
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
ECRESULT ECSessionManager::AddNotification(notification *notifyItem, unsigned int ulKey, unsigned int ulStore, unsigned int ulFolderId, unsigned int ulFlags) {
	
	EC_SESSIONGROUPMAP::const_iterator iIterator;
	std::multimap<unsigned int, ECSESSIONGROUPID>::const_iterator iterObjectSubscription;
	std::set<ECSESSIONGROUPID> setGroups;
	std::set<ECSESSIONGROUPID>::const_iterator iterGroups;
	
	ECRESULT				hr = erSuccess;
	
	if(ulStore == 0) {
		hr = m_lpECCacheManager->GetStore(ulKey, &ulStore, NULL);
		if(hr != erSuccess)
			return hr;
	}

	pthread_mutex_lock(&m_mutexObjectSubscriptions);

	// Send notification to subscribed sessions
	iterObjectSubscription = m_mapObjectSubscriptions.lower_bound(ulStore);
	while(iterObjectSubscription != m_mapObjectSubscriptions.end() && iterObjectSubscription->first == ulStore) {
		// Send a notification only once to a session group, even if it has subscribed multiple times
		setGroups.insert(iterObjectSubscription->second);
		++iterObjectSubscription;
	}
	
	pthread_mutex_unlock(&m_mutexObjectSubscriptions);

	// Send each subscribed session group one notification
	for (iterGroups = setGroups.begin(); iterGroups != setGroups.end(); ++iterGroups) {
		pthread_rwlock_rdlock(&m_hGroupLock);
		iIterator = m_mapSessionGroups.find(*iterGroups);
		if(iIterator != m_mapSessionGroups.end())
			iIterator->second->AddNotification(notifyItem, ulKey, ulStore);
		pthread_rwlock_unlock(&m_hGroupLock);
	}
	
	// Next, do an internal notification to update searchfolder views for message updates.
	if(notifyItem->obj && notifyItem->obj->ulObjType == MAPI_MESSAGE) {
		if (ulFolderId == 0 && ulFlags == 0 ) {
			if(GetCacheManager()->GetObject(ulKey, &ulFolderId, NULL, &ulFlags, NULL) != erSuccess) {
				ASSERT(FALSE);
				return hr;
			}
		}
            
        // Skip changes on associated messages, and changes on deleted item. (but include DELETE of deleted items)
        if((ulFlags & MAPI_ASSOCIATED) || (notifyItem->ulEventType != fnevObjectDeleted && (ulFlags & MSGFLAG_DELETED)))
			return hr;

		switch(notifyItem->ulEventType) {
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
	}
	return hr;
}

void* ECSessionManager::SessionCleaner(void *lpTmpSessionManager)
{
	SESSIONMAP::const_iterator iIterator, iRemove;
	time_t					lCurTime;
	ECSessionManager*		lpSessionManager = (ECSessionManager *)lpTmpSessionManager;
	int						lResult;
	struct timeval			now;
	struct timespec			timeout;
	list<BTSession*>		lstSessions;

	if(lpSessionManager == NULL) {
		return 0;
	}

	ECDatabase *db = NULL;
	if (GetThreadLocalDatabase(lpSessionManager->m_lpDatabaseFactory, &db) != erSuccess)
		ec_log_err("GTLD failed in SessionCleaner");

	while(true){
		pthread_rwlock_wrlock(&lpSessionManager->m_hCacheRWLock);

		lCurTime = GetProcessTime();
		
		// Find a session that has timed out
		iIterator = lpSessionManager->m_mapSessions.begin();
		while( iIterator != lpSessionManager->m_mapSessions.end() ) {
			if((iIterator->second->GetSessionTime()) < lCurTime && !lpSessionManager->IsSessionPersistent(iIterator->first)){
				// Remember all the session to be deleted
				lstSessions.push_back(iIterator->second);

				iRemove = iIterator++;
				// Remove the session from the list, no new threads can start on this session after this point.
				g_lpStatsCollector->Increment(SCN_SESSIONS_TIMEOUT);
				ec_log_info("End of session (timeout) %llu",
					static_cast<unsigned long long>(iRemove->first));
				lpSessionManager->m_mapSessions.erase(iRemove);
			} else {
				++iIterator;
			}
		}

		// Release ownership of the rwlock object. This makes sure all threads are free to run (and exit).
		pthread_rwlock_unlock(&lpSessionManager->m_hCacheRWLock);

		// Now, remove all the session. It will wait until all running threads for that session have exited.
		for (std::list<BTSession *>::const_iterator iSessions = lstSessions.begin();
		     iSessions != lstSessions.end(); ++iSessions)
		{
			if((*iSessions)->Shutdown(5 * 60 * 1000) == erSuccess) 
				delete *iSessions;
			else {
				// The session failed to shut down within our timeout period. This means we probably hit a bug; this
				// should only happen if some bit of code has locked the session and failed to unlock it. There are now
				// two options: delete the session anyway and hope we don't segfault, or leak the session. We choose
				// the latter.
				ec_log_err("Session failed to shut down: skipping clean");
			}
		}

		lstSessions.clear();
		kcsrv::sync_logon_times(db);

		// Wait for a terminate signal or return after a few minutes
		pthread_mutex_lock(&lpSessionManager->m_hExitMutex);
		if(lpSessionManager->bExit) {
			pthread_mutex_unlock(&lpSessionManager->m_hExitMutex);
			break;
		}

		gettimeofday(&now,NULL); // null==timezone
		timeout.tv_sec = now.tv_sec + 5;
		timeout.tv_nsec = now.tv_usec * 1000;

		lResult = pthread_cond_timedwait(&lpSessionManager->m_hExitSignal, &lpSessionManager->m_hExitMutex, &timeout);

		if (lResult != ETIMEDOUT) {
			pthread_mutex_unlock(&lpSessionManager->m_hExitMutex);
			break;
		}
		pthread_mutex_unlock(&lpSessionManager->m_hExitMutex);
	}

	return NULL;
}

ECRESULT ECSessionManager::UpdateOutgoingTables(ECKeyTable::UpdateType ulType, unsigned int ulStoreId, unsigned int ulObjId, unsigned int ulFlags, unsigned int ulObjType)
{
	TABLESUBSCRIPTION sSubscription;
	std::list<unsigned int> lstObjId;
	
	lstObjId.push_back(ulObjId);

	sSubscription.ulType = TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE;
	sSubscription.ulRootObjectId = ulFlags & EC_SUBMIT_MASTER ? 0 : ulStoreId; // in the master queue, use 0 as root object id
	sSubscription.ulObjectType = ulObjType;
	sSubscription.ulObjectFlags = ulFlags & EC_SUBMIT_MASTER; // Only use MASTER flag as differentiator
	return UpdateSubscribedTables(ulType, sSubscription, lstObjId);
}

ECRESULT ECSessionManager::UpdateTables(ECKeyTable::UpdateType ulType, unsigned int ulFlags, unsigned ulObjId, unsigned ulChildId, unsigned int ulObjType)
{
	std::list<unsigned int> lstChildId;
	
	lstChildId.push_back(ulChildId);
	
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
	SESSIONMAP::const_iterator iterSession;
	ECRESULT		er = erSuccess;
	std::set<ECSESSIONID> setSessions;
	std::set<ECSESSIONID>::const_iterator iterSubscribedSession;
	std::multimap<TABLESUBSCRIPTION, ECSESSIONID>::const_iterator iterSubscriptions;

	BTSession	*lpBTSession = NULL;
		
    // Find out which sessions our interested in this event by looking at our subscriptions
    pthread_mutex_lock(&m_mutexTableSubscriptions);
    
    iterSubscriptions = m_mapTableSubscriptions.find(sSubscription);
    while(iterSubscriptions != m_mapTableSubscriptions.end() && iterSubscriptions->first == sSubscription) {
        setSessions.insert(iterSubscriptions->second);
        ++iterSubscriptions;
    }
    
    pthread_mutex_unlock(&m_mutexTableSubscriptions);

    // We now have a set of sessions that are interested in the notification. This list is normally quite small since not that many
    // sessions have the same table opened at one time.

    // For each of the sessions that are interested, send the table change
	for (iterSubscribedSession = setSessions.begin();
	     iterSubscribedSession != setSessions.end();
	     ++iterSubscribedSession) {
		// Get session
		pthread_rwlock_rdlock(&m_hCacheRWLock);
		lpBTSession = GetSession(*iterSubscribedSession, true);
		pthread_rwlock_unlock(&m_hCacheRWLock);
	    
	    // Send the change notification
	    if(lpBTSession != NULL) {
			ECSession *lpSession = dynamic_cast<ECSession*>(lpBTSession);
	    	if (lpSession == NULL) {
				lpBTSession->Unlock();
	    	    continue;
			}
	    	
			if (sSubscription.ulType == TABLE_ENTRY::TABLE_TYPE_GENERIC)
				lpSession->GetTableManager()->UpdateTables(ulType, sSubscription.ulObjectFlags, sSubscription.ulRootObjectId, lstChildId, sSubscription.ulObjectType);
			else if (sSubscription.ulType == TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE)
				lpSession->GetTableManager()->UpdateOutgoingTables(ulType, sSubscription.ulRootObjectId, lstChildId, sSubscription.ulObjectFlags, sSubscription.ulObjectType);

			lpBTSession->Unlock();
		}
	}

	return er;
}

// FIXME: ulFolderId should be an entryid, because the parent is already deleted!
// You must specify which store the object was deleted from, 'cause we can't find out afterwards
ECRESULT ECSessionManager::NotificationDeleted(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulStoreId, entryId* lpEntryId, unsigned int ulFolderId, unsigned int ulFlags)
{
	ECRESULT er = erSuccess;
	struct notification notify;

	memset(&notify, 0, sizeof(notification));

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		goto exit;

	notify.obj = new notificationObject;
	memset(notify.obj, 0, sizeof(notificationObject));	
	
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

ECRESULT ECSessionManager::NotificationModified(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId)
{
	ECRESULT er = erSuccess;
	struct notification notify;

	memset(&notify, 0, sizeof(notification));

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		goto exit;

	notify.obj = new notificationObject;
	memset(notify.obj, 0, sizeof(notificationObject));
	
	notify.ulEventType			= fnevObjectModified;
	notify.obj->ulObjType		= ulObjType;

	er = GetCacheManager()->GetEntryIdFromObject(ulObjId, NULL, 0, &notify.obj->pEntryId);
	if(er != erSuccess)
		goto exit;

	if(ulParentId > 0) {
		er = GetCacheManager()->GetEntryIdFromObject(ulParentId, NULL, 0, &notify.obj->pParentId);
		if(er != erSuccess)
			goto exit;
	}

	AddNotification(&notify, ulObjId);

exit:
	FreeNotificationStruct(&notify, false);

	return er;
}

ECRESULT ECSessionManager::NotificationCreated(unsigned int ulObjType, unsigned int ulObjId, unsigned int ulParentId)
{
	ECRESULT er = erSuccess;
	struct notification notify;

	memset(&notify, 0, sizeof(notification));

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		goto exit;

	notify.obj = new notificationObject;
	memset(notify.obj, 0, sizeof(notificationObject));

	notify.ulEventType			= fnevObjectCreated;
	notify.obj->ulObjType		= ulObjType;

	er = GetCacheManager()->GetEntryIdFromObject(ulObjId, NULL, 0, &notify.obj->pEntryId);
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
	ECRESULT er = erSuccess;
	struct notification notify;

	memset(&notify, 0, sizeof(notification));

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		goto exit;

	notify.obj = new notificationObject;
	memset(notify.obj, 0, sizeof(notificationObject));

	notify.ulEventType				= fnevObjectMoved;	
	notify.obj->ulObjType			= ulObjType;
	
	er = GetCacheManager()->GetEntryIdFromObject(ulObjId, NULL, 0, &notify.obj->pEntryId);
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
	ECRESULT er = erSuccess;
	struct notification notify;

	memset(&notify, 0, sizeof(notification));

	if(ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER && ulObjType != MAPI_STORE)
		goto exit;

	notify.obj = new notificationObject;
	memset(notify.obj, 0, sizeof(notificationObject));

	notify.ulEventType				= fnevObjectCopied;
	
	notify.obj->ulObjType			= ulObjType;

	er = GetCacheManager()->GetEntryIdFromObject(ulObjId, NULL, 0, &notify.obj->pEntryId);
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
	ECRESULT er = erSuccess;
	struct notification notify;

	memset(&notify, 0, sizeof(notification));

	notify.obj = new notificationObject;
	memset(notify.obj, 0, sizeof(notificationObject));

	notify.ulEventType				= fnevSearchComplete;
	notify.obj->ulObjType			= MAPI_FOLDER;

	er = GetCacheManager()->GetEntryIdFromObject(ulObjId, NULL, 0, &notify.obj->pEntryId);
	if(er != erSuccess)
		goto exit;

	AddNotification(&notify, ulObjId, ulStoreId);

exit:
	FreeNotificationStruct(&notify, false);

	return er;
}

ECRESULT ECSessionManager::NotificationChange(const set<unsigned int> &syncIds, unsigned int ulChangeId, unsigned int ulChangeType)
{
	ECRESULT					er = erSuccess;
	EC_SESSIONGROUPMAP::const_iterator iIterator;
	
	pthread_rwlock_rdlock(&m_hGroupLock);

	// Send the notification to all sessionsgroups so that any client listening for these
	// notifications can receive them
	for (iIterator = m_mapSessionGroups.begin();
	     iIterator != m_mapSessionGroups.end(); ++iIterator)
		iIterator->second->AddChangeNotification(syncIds, ulChangeId, ulChangeType);
	
	pthread_rwlock_unlock(&m_hGroupLock);

	return er;
}

/**
 * Get the sessionmanager statistics
 * 
 * @param[in] callback	Callback to the statistics collector
 * @param[in] obj pointer to the statistics collector
 */
void ECSessionManager::GetStats(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj)
{
	sSessionManagerStats sSessionStats;
	sSearchFolderStats sSearchStats;

	GetStats(sSessionStats);

	callback("sessions", "Number of sessions", stringify(sSessionStats.session.ulItems), obj);
	callback("sessions_size", "Memory usage of sessions", stringify_int64(sSessionStats.session.ullSize), obj);
	callback("sessiongroups", "Number of session groups", stringify(sSessionStats.group.ulItems), obj);
	callback("sessiongroups_size", "Memory usage of session groups", stringify_int64(sSessionStats.group.ullSize), obj);

	callback("persist_conn", "Persistent connections", stringify(sSessionStats.ulPersistentByConnection), obj);
	callback("persist_conn_size", "Memory usage of persistent connections", stringify(sSessionStats.ulPersistentByConnectionSize), obj);
	callback("persist_sess", "Persistent sessions", stringify(sSessionStats.ulPersistentBySession), obj);
	callback("persist_sess_size", "Memory usage of persistent sessions", stringify(sSessionStats.ulPersistentBySessionSize), obj);

	callback("tables_subscr", "Tables subscribed", stringify(sSessionStats.ulTableSubscriptions), obj);
	callback("tables_subscr_size", "Memory usage of subscribed tables", stringify(sSessionStats.ulTableSubscriptionSize), obj);
	callback("object_subscr", "Objects subscribed", stringify(sSessionStats.ulObjectSubscriptions), obj);
	callback("object_subscr_size", "Memory usage of subscribed objects", stringify(sSessionStats.ulObjectSubscriptionSize), obj);

	m_lpSearchFolders->GetStats(sSearchStats);

	callback("searchfld_stores", "Number of stores in use by search folders", stringify(sSearchStats.ulStores), obj);
	callback("searchfld_folders", "Number of folders in use by search folders", stringify(sSearchStats.ulFolders), obj);
	callback("searchfld_events", "Number of events waiting for searchfolder updates", stringify(sSearchStats.ulEvents), obj);
	callback("searchfld_size", "Memory usage of search folders", stringify_int64(sSearchStats.ullSize), obj);
}

/**
 * Collect session statistics
 *
 * @param[out] sStats	The statistics
 *
 */
void ECSessionManager::GetStats(sSessionManagerStats &sStats)
{
	SESSIONMAP::const_iterator iIterator;
	EC_SESSIONGROUPMAP::const_iterator itersg;
	list<ECSession*> vSessions;

	memset(&sStats, 0, sizeof(sSessionManagerStats));

	// Get session data
	pthread_rwlock_rdlock(&m_hCacheRWLock);

	sStats.session.ulItems = m_mapSessions.size();
	sStats.session.ullSize = MEMORY_USAGE_MAP(sStats.session.ulItems, SESSIONMAP);

	pthread_rwlock_unlock(&m_hCacheRWLock);

	// Get group data
	pthread_rwlock_rdlock(&m_hGroupLock);
	
	sStats.group.ulItems = m_mapSessionGroups.size();
	sStats.group.ullSize = MEMORY_USAGE_HASHMAP(sStats.group.ulItems, EC_SESSIONGROUPMAP);

	for (itersg = m_mapSessionGroups.begin();
	     itersg != m_mapSessionGroups.end(); ++itersg)
		sStats.group.ullSize += itersg->second->GetObjectSize();

	pthread_rwlock_unlock(&m_hGroupLock);

	// persistent connections/sessions
	pthread_mutex_lock(&m_mutexPersistent);

	sStats.ulPersistentByConnection = m_mapPersistentByConnection.size();
	sStats.ulPersistentByConnectionSize = MEMORY_USAGE_HASHMAP(sStats.ulPersistentByConnection, PERSISTENTBYCONNECTION);

	sStats.ulPersistentBySession = m_mapPersistentBySession.size();
	sStats.ulPersistentBySessionSize = MEMORY_USAGE_HASHMAP(sStats.ulPersistentBySession, PERSISTENTBYSESSION);

	pthread_mutex_unlock(&m_mutexPersistent);

	// Table subscriptions
	pthread_mutex_lock(&m_mutexTableSubscriptions);
	
	sStats.ulTableSubscriptions = m_mapTableSubscriptions.size();
	sStats.ulTableSubscriptionSize = MEMORY_USAGE_MULTIMAP(sStats.ulTableSubscriptions, TABLESUBSCRIPTIONMULTIMAP);

	pthread_mutex_unlock(&m_mutexTableSubscriptions);

	// Object subscriptions
	pthread_mutex_lock(&m_mutexObjectSubscriptions);

	sStats.ulObjectSubscriptions = m_mapObjectSubscriptions.size();
	sStats.ulObjectSubscriptionSize = MEMORY_USAGE_MULTIMAP(sStats.ulObjectSubscriptions, OBJECTSUBSCRIPTIONSMULTIMAP);

	pthread_mutex_unlock(&m_mutexObjectSubscriptions);

}

/**
 * Dump statistics
 */
ECRESULT ECSessionManager::DumpStats()
{
	sSessionManagerStats sSessionStats;
	sSearchFolderStats sSearchStats;

	GetStats(sSessionStats);
	ec_log_info("Session stats:");
	ec_log_info("  Sessions : %u (%llu bytes)", sSessionStats.session.ulItems, static_cast<unsigned long long>(sSessionStats.session.ullSize));
	ec_log_info("  Locked   : %d", sSessionStats.session.ulLocked);
	ec_log_info("  Groups   : %u (%llu bytes)" ,sSessionStats.group.ulItems, static_cast<unsigned long long>(sSessionStats.group.ullSize));
	ec_log_info("  PersistentByConnection : %u (%u bytes)" ,sSessionStats.ulPersistentByConnection, sSessionStats.ulPersistentByConnectionSize);
	ec_log_info("  PersistentBySession    : %u (%u bytes)" , sSessionStats.ulPersistentBySession, sSessionStats.ulPersistentBySessionSize);
	ec_log_info("Subscription stats:");
	ec_log_info("  Table : %u (%u bytes)", sSessionStats.ulTableSubscriptions,  sSessionStats.ulTableSubscriptionSize);
	ec_log_info("  Object: %u (%u bytes)", sSessionStats.ulObjectSubscriptions, sSessionStats.ulObjectSubscriptionSize);
	ec_log_info("Table stats:");
	ec_log_info("  Open tables: %u (%llu bytes)", sSessionStats.session.ulOpenTables, static_cast<unsigned long long>(sSessionStats.session.ulTableSize));
	m_lpSearchFolders->GetStats(sSearchStats);
	ec_log_info("SearchFolders:");
	ec_log_info("  Stores    : %u", sSearchStats.ulStores);
	ec_log_info("  Folders   : %u", sSearchStats.ulFolders);
	ec_log_info("  Queue     : %u", sSearchStats.ulEvents);
	ec_log_info("  Mem usage : %llu Bytes", static_cast<unsigned long long>(sSearchStats.ullSize));
	return this->m_lpECCacheManager->DumpStats();
}

ECRESULT ECSessionManager::GetLicensedUsers(unsigned int ulServiceType, unsigned int* lpulLicensedUsers)
{
	ECRESULT er = erSuccess;
	unsigned int ulLicensedUsers = 0;
    ECLicenseClient *lpLicenseClient = NULL;
	lpLicenseClient = new ECLicenseClient();
	
	er = lpLicenseClient->GetInfo(ulServiceType, &ulLicensedUsers);
	
	if(er != erSuccess) {
	    ulLicensedUsers = 0;
	    er = erSuccess;
	}
	
	delete lpLicenseClient;
	*lpulLicensedUsers = ulLicensedUsers;

	return er;
}

ECRESULT ECSessionManager::GetServerGUID(GUID* lpServerGuid){
	if (lpServerGuid == NULL)
		return KCERR_INVALID_PARAMETER;
	memcpy(lpServerGuid, m_lpServerGuid, sizeof(GUID));
	return erSuccess;
}

ECRESULT ECSessionManager::GetNewSourceKey(SOURCEKEY* lpSourceKey){
	ECRESULT er;
	
	if (lpSourceKey == NULL)
		return KCERR_INVALID_PARAMETER;
	
	pthread_mutex_lock(&m_hSourceKeyAutoIncrementMutex);
	
	if (m_ulSourceKeyQueue == 0) {
		er = SaveSourceKeyAutoIncrement(m_ullSourceKeyAutoIncrement + 50);
		if(er != erSuccess) {
			pthread_mutex_unlock(&m_hSourceKeyAutoIncrementMutex);
			return er;
		}
		m_ulSourceKeyQueue = 50;
	}

	*lpSourceKey = SOURCEKEY(*m_lpServerGuid, m_ullSourceKeyAutoIncrement + 1);
	++m_ullSourceKeyAutoIncrement;
	--m_ulSourceKeyQueue;
	pthread_mutex_unlock(&m_hSourceKeyAutoIncrementMutex);
	return erSuccess;
}

ECRESULT ECSessionManager::SaveSourceKeyAutoIncrement(unsigned long long ullNewSourceKeyAutoIncrement){
	ECRESULT er;
	std::string		strQuery;
	
	er = CreateDatabaseConnection();
	if(er != erSuccess)
		return er;

	strQuery = "UPDATE `settings` SET `value` = "+ m_lpDatabase->EscapeBinary((unsigned char*)&ullNewSourceKeyAutoIncrement, 8) + " WHERE `name` = 'source_key_auto_increment'";
	return m_lpDatabase->DoUpdate(strQuery);
	// @TODO if this failed we want to retry this
}

ECRESULT ECSessionManager::SetSessionPersistentConnection(ECSESSIONID sessionID, unsigned int ulPersistentConnectionId)
{
	ECRESULT er = erSuccess;

	pthread_mutex_lock(&m_mutexPersistent);

	// maintain a bi-map of connection <-> session here
	m_mapPersistentByConnection[ulPersistentConnectionId] = sessionID;
	m_mapPersistentBySession[sessionID] = ulPersistentConnectionId;

	pthread_mutex_unlock(&m_mutexPersistent);

	return er;
}

ECRESULT ECSessionManager::RemoveSessionPersistentConnection(unsigned int ulPersistentConnectionId)
{
	ECRESULT er = erSuccess;
	PERSISTENTBYCONNECTION::const_iterator iterConnection;
	PERSISTENTBYSESSION::const_iterator iterSession;

	pthread_mutex_lock(&m_mutexPersistent);

	iterConnection = m_mapPersistentByConnection.find(ulPersistentConnectionId);
	if(iterConnection == m_mapPersistentByConnection.end()) {
		er = KCERR_NOT_FOUND; // shouldn't really happen
		goto exit;
	}

	iterSession = m_mapPersistentBySession.find(iterConnection->second);
	if(iterSession == m_mapPersistentBySession.end()) {
		er = KCERR_NOT_FOUND; // really really shouldn't happen
		goto exit;
	}

	m_mapPersistentBySession.erase(iterSession);
	m_mapPersistentByConnection.erase(iterConnection);

exit:
	pthread_mutex_unlock(&m_mutexPersistent);

	return er;
}

BOOL ECSessionManager::IsSessionPersistent(ECSESSIONID sessionID)
{
	PERSISTENTBYSESSION::const_iterator iterSession;

	pthread_mutex_lock(&m_mutexPersistent);
	iterSession = m_mapPersistentBySession.find(sessionID);
	pthread_mutex_unlock(&m_mutexPersistent);

	if(iterSession == m_mapPersistentBySession.end()) {
		return FALSE;
	} else {
		return TRUE;
	}
}

// @todo make this function with a map of seq ids
ECRESULT ECSessionManager::GetNewSequence(SEQUENCE seq, unsigned long long *lpllSeqId)
{
	ECRESULT er;
	std::string strSeqName;

	er = CreateDatabaseConnection();
	if(er != erSuccess)
		return er;

	if (seq == SEQ_IMAP)
		strSeqName = "imapseq";
	else
		return KCERR_INVALID_PARAMETER;

	pthread_mutex_lock(&m_hSeqMutex);
	if (m_ulSeqIMAPQueue == 0)
	{
		er = m_lpDatabase->DoSequence(strSeqName, 50, &m_ulSeqIMAP);
		if (er != erSuccess) {
			pthread_mutex_unlock(&m_hSeqMutex);
			return er;
		}
		m_ulSeqIMAPQueue = 50;
	}
	--m_ulSeqIMAPQueue;
	*lpllSeqId = m_ulSeqIMAP++;

	pthread_mutex_unlock(&m_hSeqMutex);
	return erSuccess;
}

ECRESULT ECSessionManager::CreateDatabaseConnection()
{
	ECRESULT er;
	std::string strError;
    
	if(m_lpDatabase == NULL) {
		er = m_lpDatabaseFactory->CreateDatabaseObject(&m_lpDatabase, strError);
		if(er != erSuccess) {
			ec_log_crit("Unable to open connection to database: %s", strError.c_str());
			return er;
		}
	}
	return erSuccess;
}

ECRESULT ECSessionManager::SubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE ulType, unsigned int ulTableRootObjectId, unsigned int ulObjectType, unsigned int ulObjectFlags, ECSESSIONID sessionID)
{
    ECRESULT er = erSuccess;
    TABLESUBSCRIPTION sSubscription;
    
    pthread_mutex_lock(&m_mutexTableSubscriptions);
    
    sSubscription.ulType = ulType;
    sSubscription.ulRootObjectId = ulTableRootObjectId;
    sSubscription.ulObjectType = ulObjectType;
    sSubscription.ulObjectFlags = ulObjectFlags;
    
    m_mapTableSubscriptions.insert(std::pair<TABLESUBSCRIPTION, ECSESSIONID>(sSubscription, sessionID));
    
    pthread_mutex_unlock(&m_mutexTableSubscriptions);
    
    return er;
}

ECRESULT ECSessionManager::UnsubscribeTableEvents(TABLE_ENTRY::TABLE_TYPE ulType, unsigned int ulTableRootObjectId, unsigned int ulObjectType, unsigned int ulObjectFlags, ECSESSIONID sessionID)
{
    ECRESULT er = erSuccess;
    TABLESUBSCRIPTION sSubscription;
    std::multimap<TABLESUBSCRIPTION, ECSESSIONID>::iterator iter;
    
    pthread_mutex_lock(&m_mutexTableSubscriptions);
    
    sSubscription.ulType = ulType;
    sSubscription.ulRootObjectId = ulTableRootObjectId;
    sSubscription.ulObjectType = ulObjectType;
    sSubscription.ulObjectFlags = ulObjectFlags;
    
    iter = m_mapTableSubscriptions.find(sSubscription);
    while(iter != m_mapTableSubscriptions.end() && iter->first == sSubscription) {
        if(iter->second == sessionID)
            break;
        ++iter;
    }
    
    if(iter != m_mapTableSubscriptions.end()) {
        m_mapTableSubscriptions.erase(iter);
    } else {
        er = KCERR_NOT_FOUND;
    }
    
    pthread_mutex_unlock(&m_mutexTableSubscriptions);
    
    return er;
}

// Subscribes for all object notifications in store ulStoreID for session group sessionID
ECRESULT ECSessionManager::SubscribeObjectEvents(unsigned int ulStoreId, ECSESSIONGROUPID sessionID)
{
    ECRESULT er = erSuccess;
    
    pthread_mutex_lock(&m_mutexObjectSubscriptions);
    m_mapObjectSubscriptions.insert(std::pair<unsigned int, ECSESSIONGROUPID>(ulStoreId, sessionID));
    pthread_mutex_unlock(&m_mutexObjectSubscriptions);
    
    return er;
}

ECRESULT ECSessionManager::UnsubscribeObjectEvents(unsigned int ulStoreId, ECSESSIONGROUPID sessionID)
{
    ECRESULT er = erSuccess;
    std::multimap<unsigned int, ECSESSIONGROUPID>::iterator i;
    
    pthread_mutex_lock(&m_mutexObjectSubscriptions);
    i = m_mapObjectSubscriptions.find(ulStoreId);
    
	while (i != m_mapObjectSubscriptions.end() && i->first == ulStoreId &&
	    i->second != sessionID)
		++i;
    
    if(i != m_mapObjectSubscriptions.end()) {
        m_mapObjectSubscriptions.erase(i);
    }

    pthread_mutex_unlock(&m_mutexObjectSubscriptions);
    
    return er;
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
	ECRESULT		er = erSuccess;
	ECDatabase		*lpDatabase = NULL;
	DB_RESULT		lpDBResult = NULL;
	DB_ROW			lpDBRow = NULL;
	std::string		strQuery;

	if (lpLcid == NULL) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if(er != erSuccess)
		goto exit;

	strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid=" + stringify(ulStoreId) +
				" AND tag=" + stringify(PROP_ID(PR_SORT_LOCALE_ID)) + " AND type=" + stringify(PROP_TYPE(PR_SORT_LOCALE_ID));
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	lpDBRow = lpDatabase->FetchRow(lpDBResult);
	if (lpDBRow == NULL || lpDBRow[0] == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	*lpLcid = strtoul(lpDBRow[0], NULL, 10);

exit:
	if (lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	return er;
}

LPCSTR ECSessionManager::GetDefaultSortLocaleID()
{
	return GetConfig()->GetSetting("default_sort_locale_id");
}

ULONG ECSessionManager::GetSortLCID(ULONG ulStoreId)
{
	ECRESULT er = erSuccess;
	ULONG ulLcid = 0;
	LPCSTR lpszLocaleId = NULL;

	er = GetStoreSortLCID(ulStoreId, &ulLcid);
	if (er == erSuccess)
		return ulLcid;

	lpszLocaleId = GetDefaultSortLocaleID();
	if (lpszLocaleId == NULL || *lpszLocaleId == '\0')
		return 0; // Select default LCID

	er = LocaleIdToLCID(lpszLocaleId, &ulLcid);
	if (er != erSuccess)
		return 0; // Select default LCID

	return ulLcid;
}

ECLocale ECSessionManager::GetSortLocale(ULONG ulStoreId)
{
	ECRESULT er;
	ULONG			ulLcid = 0;
	LPCSTR			lpszLocaleId = NULL;

	er = GetStoreSortLCID(ulStoreId, &ulLcid);
	if (er == erSuccess) {
		er = LCIDToLocaleId(ulLcid, &lpszLocaleId);
	}
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
	BTSession *lpSession = NULL;
	ECSession *lpECSession = NULL;
	
	pthread_rwlock_rdlock(&m_hCacheRWLock);
	
	lpSession = GetSession(ecSessionId, true);

	pthread_rwlock_unlock(&m_hCacheRWLock);

	if(!lpSession)
		goto exit;
		
	lpECSession = dynamic_cast<ECSession *>(lpSession);
	
	if(!lpECSession) {
		ASSERT(lpECSession != NULL);
		goto exit;
	}
	
	lpECSession->RemoveBusyState(thread);
	
exit:
	if(lpSession)
		lpSession->Unlock();
		
	return er;
}
