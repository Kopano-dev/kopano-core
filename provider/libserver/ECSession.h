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

// ECSession.h: interface for the ECSession class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ECSESSION
#define ECSESSION

#include <kopano/zcdefs.h>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <pthread.h>

#include "soapH.h"
#include <kopano/kcodes.h>
#include "ECNotification.h"
#include "ECTableManager.h"

#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include "ECDatabaseFactory.h"
#include "ECPluginFactory.h"
#include "ECSessionGroup.h"
#include "ECLockManager.h"
#include "kcore.hpp"

#ifdef HAVE_GSSAPI
#include <gssapi/gssapi.h>
#endif

class ECSecurity;
class ECUserManagement;
class SOURCEKEY;

void CreateSessionID(unsigned int ulCapabilities, ECSESSIONID *lpSessionId);

enum { SESSION_STATE_PROCESSING, SESSION_STATE_SENDING };

typedef struct {
    const char *fname;
    struct timespec threadstart;
    double start;
    pthread_t threadid;
    int state;
} BUSYSTATE;

/*
  BaseType session
*/
class BTSession {
public:
	BTSession(const char *addr, ECSESSIONID sessionID, ECDatabaseFactory *lpDatabaseFactory, ECSessionManager *lpSessionManager, unsigned int ulCapabilities);
	virtual ~BTSession(void) {}

	virtual ECRESULT Shutdown(unsigned int ulTimeout);

	virtual ECRESULT ValidateOriginator(struct soap *soap);
	virtual ECSESSIONID GetSessionId(void) const { return m_sessionID; }

	virtual time_t GetSessionTime(void) const { return m_sessionTime + m_ulSessionTimeout; }
	virtual void UpdateSessionTime();
	virtual unsigned int GetCapabilities(void) const { return m_ulClientCapabilities; }
	virtual ECSessionManager *GetSessionManager(void) const { return m_lpSessionManager; }
	virtual ECUserManagement *GetUserManagement(void) const { return m_lpUserManagement; }
	virtual ECRESULT GetDatabase(ECDatabase **lppDatabase);
	virtual ECRESULT GetAdditionalDatabase(ECDatabase **lppDatabase);
	ECRESULT GetServerGUID(GUID* lpServerGuid);
	ECRESULT GetNewSourceKey(SOURCEKEY* lpSourceKey);

	virtual void SetClientMeta(const char *const lpstrClientVersion, const char *const lpstrClientMisc);
	virtual void GetClientApplicationVersion(std::string *lpstrClientApplicationVersion);
	virtual void GetClientApplicationMisc(std::string *lpstrClientApplicationMisc);

	virtual void Lock();
	virtual void Unlock();
	virtual bool IsLocked(void) const { return m_ulRefCount > 0; }
	
	virtual void RecordRequest(struct soap *soap);
	virtual unsigned int GetRequests();

	virtual void GetClientPort(unsigned int *lpulPort);
	virtual void GetRequestURL(std::string *lpstrURL);
	virtual void GetProxyHost(std::string *lpstrProxyHost);

	size_t GetInternalObjectSize();
	virtual size_t GetObjectSize() = 0;

	time_t GetIdleTime();
	const std::string &GetSourceAddr(void) const { return m_strSourceAddr; }

	typedef enum {
	    METHOD_NONE, METHOD_USERPASSWORD, METHOD_SOCKET, METHOD_SSO, METHOD_SSL_CERT
    } AUTHMETHOD;

protected:
	unsigned int		m_ulRefCount;

	std::string		m_strSourceAddr;
	ECSESSIONID		m_sessionID;
	bool			m_bCheckIP;

	time_t			m_sessionTime;
	unsigned int		m_ulSessionTimeout;

	ECDatabaseFactory	*m_lpDatabaseFactory;
	ECSessionManager	*m_lpSessionManager;
	ECUserManagement	*m_lpUserManagement;

	unsigned int		m_ulClientCapabilities;

	/*
	 * Protects the object from deleting while a thread is running on a
	 * method in this object.
	 */
	std::condition_variable m_hThreadReleased;
	std::mutex m_hThreadReleasedMutex;	
	
	std::mutex m_hRequestStats;
	unsigned int		m_ulRequests;
	std::string		m_strLastRequestURL;
	std::string		m_strProxyHost;
	unsigned int		m_ulLastRequestPort;

	std::string		m_strClientApplicationVersion, m_strClientApplicationMisc;
};

/*
  Normal session
*/
class _kc_export_dycast ECSession _kc_final : public BTSession {
public:
	ECSession(const char *addr, ECSESSIONID sessionID, ECSESSIONGROUPID ecSessionGroupId, ECDatabaseFactory *lpDatabaseFactory, ECSessionManager *lpSessionManager, unsigned int ulCapabilities, bool bIsOffline, AUTHMETHOD ulAuthMethod, int pid, const std::string &cl_vers, const std::string &cl_app, const std::string &cl_app_ver, const std::string &cl_app_misc);

	virtual ECSESSIONGROUPID GetSessionGroupId(void) const { return m_ecSessionGroupId; }
	virtual int GetConnectingPid(void) const { return m_ulConnectingPid; }

	virtual ~ECSession();

	virtual ECRESULT Shutdown(unsigned int ulTimeout);

	/* Notification functions all wrap directly to SessionGroup */
	ECRESULT AddAdvise(unsigned int ulConnection, unsigned int ulKey, unsigned int ulEventMask);
	ECRESULT AddChangeAdvise(unsigned int ulConnection, notifySyncState *lpSyncState);
	ECRESULT DelAdvise(unsigned int ulConnection);
	ECRESULT AddNotificationTable(unsigned int ulType, unsigned int ulObjType, unsigned int ulTableId, sObjectTableKey *lpsChildRow, sObjectTableKey *lpsPrevRow, struct propValArray *lpRow);
	ECRESULT GetNotifyItems(struct soap *soap, struct notifyResponse *notifications);

	ECTableManager *GetTableManager(void) const { return m_lpTableManager; }
	ECSecurity *GetSecurity(void) const { return m_lpEcSecurity; }
	
	ECRESULT GetObjectFromEntryId(const entryId *lpEntryId, unsigned int *lpulObjId, unsigned int *lpulEidFlags = NULL);
	ECRESULT LockObject(unsigned int ulObjId);
	ECRESULT UnlockObject(unsigned int ulObjId);
	
	/* for ECStatsSessionTable */
	void AddBusyState(pthread_t threadId, const char *lpszState, struct timespec threadstart, double start);
	void UpdateBusyState(pthread_t threadId, int state);
	void RemoveBusyState(pthread_t threadId);
	void GetBusyStates(std::list<BUSYSTATE> *lpLstStates);
	
	void AddClocks(double dblUser, double dblSystem, double dblReal);
	void GetClocks(double *lpdblUser, double *lpdblSystem, double *lpdblReal);
	void GetClientVersion(std::string *lpstrVersion);
	void GetClientApp(std::string *lpstrClientApp);

	size_t GetObjectSize();

	unsigned int ClientVersion() const { return m_ulClientVersion; }

	AUTHMETHOD GetAuthMethod(void) const { return m_ulAuthMethod; }

private:
	ECTableManager		*m_lpTableManager;
	ECSessionGroup		*m_lpSessionGroup;
	ECSecurity		*m_lpEcSecurity;

	std::mutex m_hStateLock;
	typedef std::map<pthread_t, BUSYSTATE> BusyStateMap;
	BusyStateMap		m_mapBusyStates; /* which thread does what function */
	double			m_dblUser;
	double			m_dblSystem;
	double			m_dblReal;
	AUTHMETHOD		m_ulAuthMethod;
	int			m_ulConnectingPid;
	ECSESSIONGROUPID m_ecSessionGroupId;
	std::string		m_strClientVersion;
	unsigned int		m_ulClientVersion;
	std::string		m_strClientApp;
	std::string		m_strUsername;

	typedef std::map<unsigned int, ECObjectLock>	LockMap;
	std::mutex m_hLocksLock;
	LockMap			m_mapLocks;
};


/*
  Authentication session
*/
class _kc_export_dycast ECAuthSession : public BTSession {
public:
	ECAuthSession(const char *addr, ECSESSIONID sessionID, ECDatabaseFactory *lpDatabaseFactory, ECSessionManager *lpSessionManager, unsigned int ulCapabilities);
	virtual ~ECAuthSession();

	ECRESULT ValidateUserLogon(const char* lpszName, const char* lpszPassword, const char* lpszImpersonateUser);
	ECRESULT ValidateUserSocket(int socket, const char* lpszName, const char* lpszImpersonateUser);
	ECRESULT ValidateUserCertificate(soap* soap, const char* lpszName, const char* lpszImpersonateUser);
	ECRESULT ValidateSSOData(struct soap* soap, const char* lpszName, const char* lpszImpersonateUser, const char* szClientVersion, const char* szClientApp, const char *szClientAppVersion, const char *szClientAppMisc, const struct xsd__base64Binary* lpInput, struct xsd__base64Binary** lppOutput);

	virtual ECRESULT CreateECSession(ECSESSIONGROUPID ecSessionGroupId, const std::string &cl_ver, const std::string &cl_app, const std::string &cl_app_ver, const std::string &cl_app_misc, ECSESSIONID *sessionID, ECSession **lppNewSession);

	size_t GetObjectSize();

protected:
	unsigned int m_ulUserID = 0;
	unsigned int m_ulImpersonatorID = 0; // The ID of the user who's credentials were used to login when using impersonation
	bool m_bValidated = false;
	AUTHMETHOD m_ulValidationMethod = METHOD_NONE;
	int m_ulConnectingPid = 0;

private:
	/* SSO */
	ECRESULT ValidateSSOData_NTLM(struct soap* soap, const char* lpszName, const char* szClientVersion, const char* szClientApp, const char *szClientAppVersion, const char *szClientAppMisc, const struct xsd__base64Binary* lpInput, struct xsd__base64Binary** lppOutput);
	ECRESULT ValidateSSOData_KRB5(struct soap* soap, const char* lpszName, const char* szClientVersion, const char* szClientApp, const char *szClientAppVersion, const char *szClientAppMisc, const struct xsd__base64Binary* lpInput, struct xsd__base64Binary** lppOutput);
#ifdef HAVE_GSSAPI
	ECRESULT LogKRB5Error(const char *msg, OM_uint32 major, OM_uint32 minor);
#endif

	ECRESULT ProcessImpersonation(const char* lpszImpersonateUser);

	/* NTLM */
	pid_t m_NTLM_pid = -1;
	int m_NTLM_stdin[2], m_NTLM_stdout[2], m_NTLM_stderr[2];
	int m_stdin = -1, m_stdout = -1, m_stderr = -1; /* shortcuts to the above */

#ifdef HAVE_GSSAPI
	/* KRB5 */
	gss_cred_id_t m_gssServerCreds;
	gss_ctx_id_t m_gssContext;
#endif
};

/*
  Authentication for offline session
*/
class ECAuthSessionOffline _kc_final : public ECAuthSession {
public:
	ECAuthSessionOffline(const char *addr, ECSESSIONID sessionID, ECDatabaseFactory *lpDatabaseFactory, ECSessionManager *lpSessionManager, unsigned int ulCapabilities);

	ECRESULT CreateECSession(ECSESSIONGROUPID ecSessionGroupId, const std::string &cl_ver, const std::string &cl_app, const std::string &cl_app_ver, const std::string &cl_app_misc, ECSESSIONID *sessionID, ECSession **lppNewSession);
};

#endif // #ifndef ECSESSION
