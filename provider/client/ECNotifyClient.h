/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECNOTIFYCLIENT_H
#define ECNOTIFYCLIENT_H

#include <memory>
#include <mutex>
#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "ics_client.hpp"
#include "ECNotifyMaster.h"
#include <map>
#include <list>
#include <mapispi.h>

struct ECADVISE;
struct ECCHANGEADVISE;
typedef std::map<int, std::unique_ptr<ECADVISE>> ECMAPADVISE;
typedef std::map<int, std::unique_ptr<ECCHANGEADVISE>> ECMAPCHANGEADVISE;
typedef std::list<std::pair<syncid_t,connection_t> > ECLISTCONNECTION;

class SessionGroupData;

class ECNotifyClient final : public KC::ECUnknown {
protected:
	ECNotifyClient(ULONG ulProviderType, void *lpProvider, ULONG ulFlags, LPMAPISUP lpSupport);
	virtual ~ECNotifyClient();
public:
	static HRESULT Create(ULONG ulProviderType, void *lpProvider, ULONG ulFlags, LPMAPISUP lpSupport, ECNotifyClient**lppNotifyClient);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Advise(ULONG cbKey, LPBYTE lpKey, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection);
	virtual HRESULT Advise(const ECLISTSYNCSTATE &, KC::IECChangeAdviseSink *, ECLISTCONNECTION *);
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT Unadvise(const ECLISTCONNECTION &lstConnections);

	// Re-request the connection from the server. You may pass an updated key if required.
	virtual HRESULT Reregister(ULONG ulConnection, ULONG cbKey = 0, LPBYTE lpKey = NULL);
	virtual HRESULT ReleaseAll();
	virtual HRESULT Notify(ULONG ulConnection, const NOTIFYLIST &lNotifications);
	virtual HRESULT NotifyChange(ULONG ulConnection, const NOTIFYLIST &lNotifications);
	virtual HRESULT NotifyReload(); // Called when all tables should be notified of RELOAD

	// Only register an advise client side
	virtual HRESULT RegisterAdvise(ULONG cbKey, LPBYTE lpKey, ULONG ulEventMask, bool bSynchronous, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection);
	virtual HRESULT RegisterChangeAdvise(ULONG sync_id, ULONG change_id, KC::IECChangeAdviseSink *, ULONG *conn);
	virtual HRESULT UnRegisterAdvise(ULONG ulConnection);
	virtual HRESULT UpdateSyncStates(const ECLISTSYNCID &lstSyncID, ECLISTSYNCSTATE *lplstSyncState);

private:
	ECMAPADVISE				m_mapAdvise;		// Map of all advise request from the client (outlook)
	ECMAPCHANGEADVISE		m_mapChangeAdvise;	// ExchangeChangeAdvise(s)
	KC::object_ptr<SessionGroupData> m_lpSessionGroup;
	/* weak ptr: ECNotifyMaster is already owned by SessionGroupData */
	ECNotifyMaster*			m_lpNotifyMaster;
	KC::object_ptr<WSTransport> m_lpTransport;
	KC::object_ptr<IMAPISupport> m_lpSupport;
	void*					m_lpProvider;
	ULONG					m_ulProviderType;
	std::recursive_mutex m_hMutex;
	KC::ECSESSIONGROUPID m_ecSessionGroupId;
	ALLOC_WRAP_FRIEND;
};

#endif // #ifndef ECNOTIFYCLIENT_H
