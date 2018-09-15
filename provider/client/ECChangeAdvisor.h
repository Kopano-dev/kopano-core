/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECCHANGEADVISOR_H
#define ECCHANGEADVISOR_H

#include <kopano/zcdefs.h>
#include <memory>
#include <mutex>
#include <mapidefs.h>
#include <mapispi.h>
#include <kopano/ECUnknown.h>
#include <kopano/IECInterfaces.hpp>
#include "ics_client.hpp"
#include <kopano/kcodes.h>
#include <kopano/memory.hpp>
#include <map>
#include "ECMsgStore.h"

namespace KC {
class ECLogger;
}

class ECMsgStore;

/**
 * ECChangeAdvisor: Implementation IECChangeAdvisor, which allows one to register for 
 *                  change notifications on folders.
 */
class ECChangeAdvisor _kc_final :
    public KC::ECUnknown, public KC::IECChangeAdvisor {
protected:
	/**
	 * @param[in]	lpMsgStore
	 *					The message store that contains the folder to be registered for
	 *					change notifications.
	 */
	ECChangeAdvisor(ECMsgStore *lpMsgStore);

	virtual ~ECChangeAdvisor();

public:
	/**
	 * @param[in]	lpMsgStore
	 *					The message store that contains the folder to be registered for
	 *					change notifications.
	 * @param[out]	lppChangeAdvisor
	 *					The new change advisor.
	 */
	static	HRESULT Create(ECMsgStore *lpMsgStore, ECChangeAdvisor **lppChangeAdvisor);

	/**
	 * Get an alternate interface to the same object.
	 *
	 * @param[in]	refiid
	 *					A reference to the IID of the requested interface.
	 *					Supported interfaces:
	 *						- IID_ECChangeAdvisor
	 *						- IID_IECChangeAdvisor
	 * @param[out]	lpvoid
	 *					Pointer to a pointer of the requested type, casted to a void pointer.
	 */
	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

	// IECChangeAdvisor
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Config(IStream *, GUID *guid, KC::IECChangeAdviseSink *, ULONG flags);
	virtual HRESULT UpdateState(LPSTREAM lpStream);
	virtual HRESULT AddKeys(LPENTRYLIST lpEntryList);
	virtual HRESULT RemoveKeys(LPENTRYLIST lpEntryList);
	virtual HRESULT IsMonitoringSyncId(syncid_t ulSyncId);
	virtual HRESULT UpdateSyncState(syncid_t ulSyncId, changeid_t ulChangeId);

private:
	typedef std::map<syncid_t, connection_t> ConnectionMap;
	typedef std::map<syncid_t, changeid_t> SyncStateMap;

	/**
	 * Create a SyncStateMap entry from an SSyncState structure.
	 *
	 * @param[in]	sSyncState
	 *					The SSyncState structure to be converted to a SyncStateMap entry.
	 * @return A new SyncStateMap entry.
	 */
	static SyncStateMap::value_type	ConvertSyncState(const SSyncState &sSyncState);

	static SSyncState				ConvertSyncStateMapEntry(const SyncStateMap::value_type &sMapEntry);

	/**
	 * Compare the sync ids from a ConnectionMap entry and a SyncStateMap entry.
	 *
	 * @param[in]	sConnection
	 *					The ConnectionMap entry to compare the sync id with.
	 * @param[in]	sSyncState
	 *					The SyncStateMap entry to compare the sync id with.
	 * @return true if the sync ids are equal, false otherwise.
	 */
	static bool						CompareSyncId(const ConnectionMap::value_type &sConnection, const SyncStateMap::value_type &sSyncState);

	/**
	 * Reload the change notifications.
	 *
	 * @param[in]	lpParam
	 *					The parameter passed to AddSessionReloadCallback.
	 * @param[in]	newSessionId
	 *					The sessionid of the new session.
	 */
	static HRESULT Reload(void *parm, KC::ECSESSIONID new_id);

	/**
	 * Purge all unused connections from advisor.
	 */
	HRESULT							PurgeStates();

	ULONG m_ulFlags = 0, m_ulReloadId = 0;
	std::recursive_mutex m_hConnectionLock;
	ConnectionMap			m_mapConnections;
	SyncStateMap			m_mapSyncStates;
	KC::object_ptr<ECMsgStore> m_lpMsgStore;
	std::shared_ptr<KC::ECLogger> m_lpLogger;
	KC::object_ptr<KC::IECChangeAdviseSink> m_lpChangeAdviseSink;
};

#endif // ndef ECCHANGEADVISOR_H
