/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECNOTIFYMASTER_H
#define ECNOTIFYMASTER_H

#include <atomic>
#include <list>
#include <map>
#include <mutex>
#include <pthread.h>
#include <kopano/zcdefs.h>
#include "ECSessionGroupManager.h"
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/kcodes.h>
#include <kopano/memory.hpp>

class ECNotifyClient;
class ECNotifyMaster;
class WSTransport;
struct notification;

typedef std::list<notification*> NOTIFYLIST;
typedef HRESULT(ECNotifyClient::*NOTIFYCALLBACK)(ULONG,const NOTIFYLIST &);
class ECNotifySink _kc_final {
public:
	ECNotifySink(ECNotifyClient *lpClient, NOTIFYCALLBACK fnCallback);
	HRESULT Notify(ULONG ulConnection, const NOTIFYLIST &lNotifications) const;
	bool IsClient(const ECNotifyClient *) const;

private:
	ECNotifyClient	*m_lpClient;
	NOTIFYCALLBACK	m_fnCallback;
};

/*
 * ECNotifyClient is owned by ECABLogon/ECMsgStore, so the list basically
 * consists of weak pointers (and must not be changed to object_ptr).
 */
typedef std::list<ECNotifyClient*> NOTIFYCLIENTLIST;
typedef std::map<ULONG, ECNotifySink> NOTIFYCONNECTIONCLIENTMAP;
typedef std::map<ULONG, NOTIFYLIST> NOTIFYCONNECTIONMAP;

class ECNotifyMaster _kc_final : public KC::ECUnknown {
protected:
	ECNotifyMaster(SessionGroupData *lpData);
	virtual ~ECNotifyMaster(void);

public:
	static HRESULT Create(SessionGroupData *lpData, ECNotifyMaster **lppMaster);
	virtual HRESULT ConnectToSession();
	virtual HRESULT AddSession(ECNotifyClient* lpClient);
	virtual HRESULT ReleaseSession(ECNotifyClient* lpClient);
	virtual HRESULT ReserveConnection(ULONG *lpulConnection);
	virtual HRESULT ClaimConnection(ECNotifyClient* lpClient, NOTIFYCALLBACK fnCallback, ULONG ulConnection);
	virtual HRESULT DropConnection(ULONG ulConnection);

private:
	/* Threading functions */
	virtual HRESULT StartNotifyWatch();
	virtual HRESULT StopNotifyWatch();
	static void* NotifyWatch(void *pTmpNotifyClient);

	/* List of Clients attached to this master. */
	NOTIFYCLIENTLIST			m_listNotifyClients;

	/* List of all connections, mapped to client. */
	NOTIFYCONNECTIONCLIENTMAP	m_mapConnections;

	/* Connection settings */
	/* weak ptr: ECNotifyMaster is owned by SessionGroupData */
	SessionGroupData *m_lpSessionGroupData;
	KC::object_ptr<WSTransport> m_lpTransport;
	std::atomic<unsigned int> m_ulConnection{1};

	/* Threading information */
	std::recursive_mutex m_hMutex;
	pthread_t					m_hThread;
	BOOL m_bThreadRunning = false, m_bThreadExit = false;
	ALLOC_WRAP_FRIEND;
};

#endif /* ECNOTIFYMASTER_H */
