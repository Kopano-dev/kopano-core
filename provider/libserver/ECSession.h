/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

// ECSession.h: interface for the ECSession class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ECSESSION
#define ECSESSION

#include <kopano/zcdefs.h>
#include <atomic>
#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <ctime>
#include <pthread.h>
#include <sys/types.h>
#include "soapH.h"
#include <kopano/kcodes.h>
#include <kopano/timeutil.hpp>
#include "ECNotification.h"
#include "ECTableManager.h"
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/rqstat.hpp>
#include <kopano/timeutil.hpp>
#include "ECDatabaseFactory.h"
#include "ECPluginFactory.h"
#include "ECSessionGroup.h"
#include "ECLockManager.h"
#include "kcore.hpp"
#ifdef HAVE_GSSAPI
#include <gssapi/gssapi.h>
#endif

struct soap;

namespace KC {

class ECSecurity;
class ECUserManagement;
class SOURCEKEY;

void CreateSessionID(unsigned int ulCapabilities, ECSESSIONID *lpSessionId);

enum { SESSION_STATE_PROCESSING, SESSION_STATE_SENDING };

struct BUSYSTATE {
    const char *fname;
	const struct request_stat *rqstat;
	struct timespec wi_cpu_start;
	time_point wi_wall_start;
    pthread_t threadid;
    int state;
};

/*
  BaseType session
*/
class KC_EXPORT BTSession {
public:
	KC_HIDDEN BTSession(const char *addr, ECSESSIONID, ECDatabaseFactory *, ECSessionManager *, unsigned int caps);
	KC_HIDDEN virtual ~BTSession() = default;
	KC_HIDDEN virtual ECRESULT Shutdown(unsigned int timeout);
	KC_HIDDEN virtual ECRESULT ValidateOriginator(struct soap *);
	KC_HIDDEN virtual ECSESSIONID GetSessionId() const final { return m_sessionID; }
	KC_HIDDEN virtual time_t GetSessionTime() const final { return m_sessionTime + m_ulSessionTimeout; }
	KC_HIDDEN virtual void UpdateSessionTime();
	KC_HIDDEN virtual unsigned int GetCapabilities() const final { return m_ulClientCapabilities; }
	KC_HIDDEN virtual ECSessionManager *GetSessionManager() const final { return m_lpSessionManager; }
	KC_HIDDEN virtual ECUserManagement *GetUserManagement() const = 0;
	virtual ECRESULT GetDatabase(ECDatabase **);
	KC_HIDDEN virtual ECRESULT GetAdditionalDatabase(ECDatabase **);
	KC_HIDDEN ECRESULT GetServerGUID(GUID *);
	KC_HIDDEN ECRESULT GetNewSourceKey(SOURCEKEY *);
	KC_HIDDEN virtual void SetClientMeta(const char *cl_vers, const char *cl_misc);
	KC_HIDDEN virtual void GetClientApplicationVersion(std::string *);
	KC_HIDDEN virtual void GetClientApplicationMisc(std::string *);
	virtual void lock();
	virtual void unlock();
	KC_HIDDEN virtual bool IsLocked() const final { return m_ulRefCount > 0; }
	KC_HIDDEN virtual void RecordRequest(struct soap *);
	KC_HIDDEN virtual unsigned int GetRequests();
	KC_HIDDEN virtual std::string GetProxyHost();
	KC_HIDDEN size_t GetInternalObjectSize();
	KC_HIDDEN virtual size_t GetObjectSize() = 0;
	KC_HIDDEN time_t GetIdleTime() const;
	KC_HIDDEN const std::string &GetSourceAddr() const { return m_strSourceAddr; }

	enum AUTHMETHOD {
	    METHOD_NONE, METHOD_USERPASSWORD, METHOD_SOCKET, METHOD_SSO, METHOD_SSL_CERT
	};

protected:
	std::atomic<unsigned int> m_ulRefCount{0};
	std::string		m_strSourceAddr;
	ECSESSIONID		m_sessionID;
	bool m_bCheckIP = true;
	time_t			m_sessionTime;
	unsigned int m_ulSessionTimeout = 300, m_ulClientCapabilities;
	unsigned int m_ulRequests = 0, m_ulLastRequestPort = 0;
	ECDatabaseFactory	*m_lpDatabaseFactory;
	ECSessionManager	*m_lpSessionManager;
	/*
	 * Protects the object from deleting while a thread is running on a
	 * method in this object.
	 */
	std::condition_variable m_hThreadReleased;
	std::mutex m_hThreadReleasedMutex, m_hRequestStats;
	std::string m_strProxyHost;
	std::string		m_strClientApplicationVersion, m_strClientApplicationMisc;
};

/*
  Normal session
*/
class KC_EXPORT_DYCAST ECSession final : public BTSession {
public:
	KC_HIDDEN ECSession(const char *addr, ECSESSIONID, ECSESSIONGROUPID, ECDatabaseFactory *, ECSessionManager *, unsigned int caps, AUTHMETHOD, const std::string &cl_vers, const std::string &cl_app, const std::string &cl_app_ver, const std::string &cl_app_misc);
	KC_HIDDEN virtual ECSESSIONGROUPID GetSessionGroupId() const final { return m_ecSessionGroupId; }
	KC_HIDDEN virtual ~ECSession();
	KC_HIDDEN virtual ECRESULT Shutdown(unsigned int timeout) override;
	KC_HIDDEN virtual ECUserManagement *GetUserManagement() const override final { return m_lpUserManagement.get(); }

	/* Notification functions all wrap directly to SessionGroup */
	KC_HIDDEN ECRESULT AddAdvise(unsigned int conn, unsigned int key, unsigned int event_mask);
	KC_HIDDEN ECRESULT AddChangeAdvise(unsigned int conn, notifySyncState *);
	KC_HIDDEN ECRESULT DelAdvise(unsigned int conn);
	KC_HIDDEN ECRESULT AddNotificationTable(unsigned int type, unsigned int obj_type, unsigned int table, sObjectTableKey *child_row, sObjectTableKey *prev_row, struct propValArray *row);
	KC_HIDDEN ECRESULT GetNotifyItems(struct soap *, struct notifyResponse *notifications);
	KC_HIDDEN ECTableManager *GetTableManager() const { return m_lpTableManager.get(); }
	KC_HIDDEN ECSecurity *GetSecurity() const { return m_lpEcSecurity.get(); }
	KC_HIDDEN ECRESULT GetObjectFromEntryId(const entryId *, unsigned int *obj_id, unsigned int *eid_flags = nullptr);
	KC_HIDDEN ECRESULT LockObject(unsigned int obj_id);
	KC_HIDDEN ECRESULT UnlockObject(unsigned int obj_id);

	/* for ECStatsSessionTable */
	KC_HIDDEN void AddBusyState(pthread_t, const char *state, const request_stat &);
	KC_HIDDEN void UpdateBusyState(pthread_t, int state);
	KC_HIDDEN void RemoveBusyState(pthread_t);
	KC_HIDDEN void GetBusyStates(std::list<BUSYSTATE> *);
	KC_HIDDEN void AddClocks(double user, double system, double real);
	KC_HIDDEN void GetClocks(double *user, double *system, double *real);
	KC_HIDDEN void GetClientVersion(std::string *version);
	KC_HIDDEN void GetClientApp(std::string *client_app);
	KC_HIDDEN size_t GetObjectSize() override;
	KC_HIDDEN unsigned int ClientVersion() const { return m_ulClientVersion; }
	KC_HIDDEN AUTHMETHOD GetAuthMethod() const { return m_ulAuthMethod; }

private:
	ECSessionGroup		*m_lpSessionGroup;
	std::mutex m_hStateLock;
	typedef std::map<pthread_t, BUSYSTATE> BusyStateMap;
	BusyStateMap		m_mapBusyStates; /* which thread does what function */
	double m_dblUser = 0, m_dblSystem = 0, m_dblReal = 0;
	AUTHMETHOD		m_ulAuthMethod;
	ECSESSIONGROUPID m_ecSessionGroupId;
	std::string m_strClientVersion, m_strClientApp, m_strUsername;
	unsigned int		m_ulClientVersion;

	typedef std::map<unsigned int, ECObjectLock>	LockMap;
	std::mutex m_hLocksLock;
	LockMap			m_mapLocks;
	std::unique_ptr<ECSecurity> m_lpEcSecurity;
	std::unique_ptr<ECUserManagement> m_lpUserManagement;
	std::unique_ptr<ECTableManager> m_lpTableManager;
};

/*
  Authentication session
*/
class KC_EXPORT_DYCAST ECAuthSession final : public BTSession {
public:
	KC_HIDDEN ECAuthSession(const char *addr, ECSESSIONID, ECDatabaseFactory *, ECSessionManager *, unsigned int caps);
	KC_HIDDEN virtual ~ECAuthSession();
	KC_HIDDEN ECRESULT ValidateUserLogon(const char *name, const char *pass, const char *imp_user);
	KC_HIDDEN ECRESULT ValidateUserSocket(int socket, const char *name, const char *imp_user);
	KC_HIDDEN ECRESULT ValidateUserCertificate(struct soap *, const char *name, const char *imp_user);
	KC_HIDDEN ECRESULT ValidateSSOData(struct soap *, const char *name, const char *imp_user, const char *cl_ver, const char *cl_app, const char *cl_app_ver, const char *cl_app_misc, const struct xsd__base64Binary *input, struct xsd__base64Binary **output);
	KC_HIDDEN virtual ECRESULT CreateECSession(ECSESSIONGROUPID, const std::string &cl_ver, const std::string &cl_app, const std::string &cl_app_ver, const std::string &cl_app_misc, ECSESSIONID *retid, ECSession **ret);
	KC_HIDDEN size_t GetObjectSize() override;
	KC_HIDDEN virtual ECUserManagement *GetUserManagement() const override final { return m_lpUserManagement.get(); }

protected:
	unsigned int m_ulUserID = 0;
	unsigned int m_ulImpersonatorID = 0; // The ID of the user whose credentials were used to login when using impersonation
	bool m_bValidated = false;
	AUTHMETHOD m_ulValidationMethod = METHOD_NONE;

private:
	/* SSO */
	KC_HIDDEN ECRESULT ValidateSSOData_NTLM(struct soap *, const char *name, const char *cl_ver, const char *cl_app, const char *cl_app_ver, const char *cl_app_misc, const struct xsd__base64Binary *input, struct xsd__base64Binary **out);
	KC_HIDDEN ECRESULT ValidateSSOData_KRB5(struct soap *, const char *name, const char *cl_ver, const char *cl_app, const char *cl_app_ver, const char *cl_app_misc, const struct xsd__base64Binary *input, struct xsd__base64Binary **out);
	KC_HIDDEN ECRESULT ValidateSSOData_KCOIDC(struct soap *, const char *name, const char *cl_ver, const char *cl_app, const char *cl_app_ver, const char *cl_app_misc, const struct xsd__base64Binary *input, struct xsd__base64Binary **output);
#ifdef HAVE_GSSAPI
	KC_HIDDEN ECRESULT LogKRB5Error(const char *msg, OM_uint32 major, OM_uint32 minor);
#endif
	KC_HIDDEN ECRESULT ProcessImpersonation(const char *imp_user);

	/* NTLM */
	pid_t m_NTLM_pid = -1;
	int m_stdin = -1, m_stdout = -1, m_stderr = -1;

#ifdef HAVE_GSSAPI
	/* KRB5 */
	gss_cred_id_t m_gssServerCreds;
	gss_ctx_id_t m_gssContext;
#endif
	std::unique_ptr<ECUserManagement> m_lpUserManagement;
};

} /* namespace */

#endif // #ifndef ECSESSION
