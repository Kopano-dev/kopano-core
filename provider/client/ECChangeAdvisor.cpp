/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <iterator>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include <kopano/ECGuid.h>
#include <kopano/ECLogger.h>
#include "ECChangeAdvisor.h"
#include "ECMsgStore.h"

using namespace KC;

ECChangeAdvisor::SyncStateMap::value_type ECChangeAdvisor::ConvertSyncState(const SSyncState &sSyncState)
{
	return SyncStateMap::value_type(sSyncState.ulSyncId, sSyncState.ulChangeId);
}

bool ECChangeAdvisor::CompareSyncId(const ConnectionMap::value_type &sConnection, const SyncStateMap::value_type &sSyncState)
{
	return sConnection.first < sSyncState.first;
}

ECChangeAdvisor::ECChangeAdvisor(ECMsgStore *lpMsgStore) :
	m_lpMsgStore(lpMsgStore), m_lpLogger(new ECLogger_Null)
	// Need MUTEX RECURSIVE because with a reconnection the funtion PurgeStates called indirect the function Reload again:
	// ECChangeAdvisor::Reload(....)
 	// WSTransport::HrReLogon()
 	// WSTransport::HrGetSyncStates(....)
 	// ECChangeAdvisor::PurgeStates()
 	// ECChangeAdvisor::UpdateState(IStream * lpStream)
{}

ECChangeAdvisor::~ECChangeAdvisor()
{
	if (m_ulReloadId)
		m_lpMsgStore->lpTransport->RemoveSessionReloadCallback(m_ulReloadId);
	// Unregister notifications
	if (!(m_ulFlags & SYNC_CATCHUP))
		m_lpMsgStore->m_lpNotifyClient->Unadvise(ECLISTCONNECTION(m_mapConnections.begin(), m_mapConnections.end()));
}

HRESULT ECChangeAdvisor::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECChangeAdvisor, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IECChangeAdvisor, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

/**
 * Create a ECChangeAdvisor instance.
 *
 * @param[in]	lpMsgStore			The store to register the change notifications on
 * @param[out]	lppChangeAdvisor	The newly create change advisor
 *
 * @retval	MAPI_E_INVALID_PARAMETER	lpMsgStore or lppChangeAdvisor are NULL pointers
 * @retval	MAPI_E_NO_SUPPORT			The profile was create with notification disabled or
 * 										enhanced ICS is not enabled.
 */
HRESULT ECChangeAdvisor::Create(ECMsgStore *lpMsgStore, ECChangeAdvisor **lppChangeAdvisor)
{
	if (lpMsgStore == nullptr || lppChangeAdvisor == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (lpMsgStore->m_lpNotifyClient == nullptr)
		return MAPI_E_NO_SUPPORT;

	object_ptr<ECChangeAdvisor> lpChangeAdvisor;
	BOOL			fEnhancedICS = false;
	auto hr = lpMsgStore->lpTransport->HrCheckCapabilityFlags(KOPANO_CAP_ENHANCED_ICS, &fEnhancedICS);
	if (hr != hrSuccess)
		return hr;
	if (!fEnhancedICS)
		return MAPI_E_NO_SUPPORT;
	lpChangeAdvisor.reset(new ECChangeAdvisor(lpMsgStore));
	hr = lpMsgStore->lpTransport->AddSessionReloadCallback(lpChangeAdvisor, &Reload, &lpChangeAdvisor->m_ulReloadId);
	if (hr != hrSuccess)
		return hr;
	*lppChangeAdvisor = lpChangeAdvisor.release();
	return hr;
}

HRESULT ECChangeAdvisor::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECChangeAdvisor::Config(LPSTREAM lpStream, LPGUID /*lpGUID*/,
    IECChangeAdviseSink *lpAdviseSink, ULONG ulFlags)
{
	if (lpAdviseSink == nullptr && !(ulFlags & SYNC_CATCHUP))
		return MAPI_E_INVALID_PARAMETER;

	ULONG ulVal = 0, ulRead = 0;
	memory_ptr<ENTRYLIST> lpEntryList;
	LARGE_INTEGER			liSeekStart = {{0}};

	// Unregister notifications
	if (!(m_ulFlags & SYNC_CATCHUP))
		m_lpMsgStore->m_lpNotifyClient->Unadvise(ECLISTCONNECTION(m_mapConnections.begin(), m_mapConnections.end()));
	m_mapConnections.clear();
	m_ulFlags = ulFlags;
	m_lpChangeAdviseSink.reset(lpAdviseSink);
	if (lpStream == NULL)
		return hrSuccess;
	auto hr = lpStream->Seek(liSeekStart, SEEK_SET, nullptr);
	if (hr != hrSuccess)
		return hr;
	hr = lpStream->Read(&ulVal, sizeof(ulVal), &ulRead);
	if (hr != hrSuccess)
		return hr;
	if (ulRead != sizeof(ulVal))
		return MAPI_E_CALL_FAILED;
	if (ulVal <= 0)
		return hrSuccess;
	hr = MAPIAllocateBuffer(sizeof *lpEntryList, &~lpEntryList);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(ulVal * sizeof *lpEntryList->lpbin, lpEntryList, (void**)&lpEntryList->lpbin);
	if (hr != hrSuccess)
		return hr;

	lpEntryList->cValues = ulVal;
	for (ULONG i = 0; i < lpEntryList->cValues; ++i) {
		hr = lpStream->Read(&ulVal, sizeof(ulVal), &ulRead);
		if (hr != hrSuccess)
			return hr;
		if (ulRead != sizeof(ulVal))
			return MAPI_E_CALL_FAILED;
		hr = MAPIAllocateMore(ulVal, lpEntryList, (void**)&lpEntryList->lpbin[i].lpb);
		if (hr != hrSuccess)
			return hr;
		lpEntryList->lpbin[i].cb = ulVal;
		hr = lpStream->Read(lpEntryList->lpbin[i].lpb, ulVal, &ulRead);
		if (hr != hrSuccess)
			return hr;
		if (ulRead != ulVal)
			return MAPI_E_CALL_FAILED;
	}
	return AddKeys(lpEntryList);
}

/**
 * Purge states
 *
 * @note m_hConnectionLock must be locked
 */
HRESULT ECChangeAdvisor::PurgeStates()
{
	HRESULT hr;
	ECLISTSYNCID		lstSyncId;
	ECLISTSYNCSTATE		lstSyncState;
	SyncStateMap		mapChangeId;
	std::list<ConnectionMap::value_type>			lstObsolete;
	std::list<ConnectionMap::value_type>::const_iterator iterObsolete;

	// First get the most up to date change ids for all registered sync ids (we will ignore the changeids since we don't know if we actually got that far)
	std::transform(m_mapConnections.begin(), m_mapConnections.end(), std::back_inserter(lstSyncId),
		[](const ConnectionMap::value_type &s) { return s.first; });
	hr = m_lpMsgStore->m_lpNotifyClient->UpdateSyncStates(lstSyncId, &lstSyncState);
	if (hr != hrSuccess)
		return hr;

	// Create a map based on the returned sync states
	std::transform(lstSyncState.begin(), lstSyncState.end(), std::inserter(mapChangeId, mapChangeId.begin()), &ConvertSyncState);
	
	// Find all connections that are not used for the returned set of sync states and remove them
	std::set_difference(m_mapConnections.begin(), m_mapConnections.end(), mapChangeId.begin(), mapChangeId.end(), std::back_inserter(lstObsolete), &CompareSyncId);

	// Get rid of the obsolete connections (suboptimal)
	for (iterObsolete = lstObsolete.begin(); iterObsolete != lstObsolete.end(); ++iterObsolete) {
		m_lpMsgStore->m_lpNotifyClient->Unadvise(iterObsolete->second);
		m_mapConnections.erase(iterObsolete->first);
		m_mapSyncStates.erase(iterObsolete->first);
	}
	return hrSuccess;
}

HRESULT ECChangeAdvisor::UpdateState(LPSTREAM lpStream)
{
	if (lpStream == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	LARGE_INTEGER			liPos = {{0}};
	ULARGE_INTEGER			uliSize = {{0}};
	ULONG					ulVal = 0;
	scoped_rlock lock(m_hConnectionLock);

	if (m_lpChangeAdviseSink == NULL && !(m_ulFlags & SYNC_CATCHUP))
		return MAPI_E_UNCONFIGURED;
	auto hr = PurgeStates();
	if (hr != hrSuccess)
		return hr;

	// Since m_mapSyncStates are related m_mapConnection the maps should
	// be equal in size.
	assert(m_mapConnections.size() == m_mapSyncStates.size());
	// Create the status stream
	lpStream->Seek(liPos, STREAM_SEEK_SET, NULL);
	lpStream->SetSize(uliSize);
	// First the amount of items in the stream
	ulVal = (ULONG)m_mapConnections.size();
	lpStream->Write(&ulVal, sizeof(ulVal), NULL);

	for (const auto &p : m_mapConnections) {
		// The size of the sync state
		ulVal = 2 * sizeof(ULONG);		// syncid, changeid
		lpStream->Write(&ulVal, sizeof(ulVal), NULL);
		// syncid
		lpStream->Write(&p.first, sizeof(p.first), NULL);
		// changeid
		lpStream->Write(&m_mapSyncStates[p.first], sizeof(SyncStateMap::key_type), NULL);
	}
	return hrSuccess;
}

HRESULT ECChangeAdvisor::AddKeys(LPENTRYLIST lpEntryList)
{
	if (lpEntryList == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (m_lpChangeAdviseSink == NULL && !(m_ulFlags & SYNC_CATCHUP))
		return MAPI_E_UNCONFIGURED;

	HRESULT						hr = hrSuccess;
	SSyncState					*lpsSyncState = NULL;
	ECLISTCONNECTION			listConnections;
	ECLISTSYNCSTATE				listSyncStates;
	scoped_rlock lock(m_hConnectionLock);
	ZLOG_DEBUG(m_lpLogger, "Adding %u keys", lpEntryList->cValues);
	
	for (ULONG i = 0; hr == hrSuccess && i < lpEntryList->cValues; ++i) {
		if (lpEntryList->lpbin[i].cb >= sizeof(SSyncState)) {
			lpsSyncState = (SSyncState*)lpEntryList->lpbin[i].lpb;

			ZLOG_DEBUG(m_lpLogger, " - Key %u: syncid=%u, changeid=%u", i, lpsSyncState->ulSyncId, lpsSyncState->ulChangeId);
			// Check if we don't have this sync state already
			if (m_mapConnections.find(lpsSyncState->ulSyncId) != m_mapConnections.end()) {
				ZLOG_DEBUG(m_lpLogger, " - Key %u: duplicate!", lpsSyncState->ulSyncId);
				continue;
			}
			if (!(m_ulFlags & SYNC_CATCHUP))
				listSyncStates.emplace_back(*lpsSyncState);
			else
				listConnections.emplace_back(lpsSyncState->ulSyncId, 0);
		} else {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, " - Key %u: Invalid size=%u", i, lpEntryList->lpbin[i].cb);
			hr = MAPI_E_INVALID_PARAMETER;
		}
	}

	if (!(m_ulFlags & SYNC_CATCHUP))
		hr = m_lpMsgStore->m_lpNotifyClient->Advise(listSyncStates, m_lpChangeAdviseSink, &listConnections);
	if (hr == hrSuccess) {
		m_mapConnections.insert(std::make_move_iterator(listConnections.begin()), std::make_move_iterator(listConnections.end()));
		std::transform(listSyncStates.begin(), listSyncStates.end(), std::inserter(m_mapSyncStates, m_mapSyncStates.begin()), &ConvertSyncState);
	}
	return hr;
}

HRESULT ECChangeAdvisor::RemoveKeys(LPENTRYLIST lpEntryList)
{
	if (lpEntryList == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (m_lpChangeAdviseSink == nullptr && !(m_ulFlags & SYNC_CATCHUP))
		return MAPI_E_UNCONFIGURED;

	SSyncState				*lpsSyncState = NULL;
	ECLISTCONNECTION		listConnections;
	scoped_rlock lock(m_hConnectionLock);

	for (ULONG i = 0; i < lpEntryList->cValues; ++i) {
		if (lpEntryList->lpbin[i].cb < sizeof(SSyncState))
			continue;
		lpsSyncState = (SSyncState*)lpEntryList->lpbin[i].lpb;
		// Try to delete the sync state from state map anyway
		m_mapSyncStates.erase(lpsSyncState->ulSyncId);
		// Check if we even have the sync state
		auto iterConnection = m_mapConnections.find(lpsSyncState->ulSyncId);
		if (iterConnection == m_mapConnections.cend())
			continue;
		// Unregister the sync state.
		if (!(m_ulFlags & SYNC_CATCHUP))
			listConnections.emplace_back(*iterConnection);
		// Remove from map
		m_mapConnections.erase(iterConnection);
	}
	return m_lpMsgStore->m_lpNotifyClient->Unadvise(listConnections);
}

HRESULT ECChangeAdvisor::IsMonitoringSyncId(syncid_t ulSyncId)
{
	if (m_mapConnections.find(ulSyncId) == m_mapConnections.end())
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

HRESULT ECChangeAdvisor::UpdateSyncState(syncid_t ulSyncId, changeid_t ulChangeId)
{
	scoped_rlock lock(m_hConnectionLock);
	auto iSyncState = m_mapSyncStates.find(ulSyncId);
	if (iSyncState == m_mapSyncStates.cend())
		return MAPI_E_INVALID_PARAMETER;
	iSyncState->second = ulChangeId;
	return hrSuccess;
}

HRESULT ECChangeAdvisor::Reload(void *lpParam, ECSESSIONID /*newSessionId*/)
{
	if (lpParam == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT				hr = hrSuccess;
	auto lpChangeAdvisor = static_cast<ECChangeAdvisor *>(lpParam);
	ECLISTSYNCSTATE		listSyncStates;
	ECLISTCONNECTION	listConnections;
	scoped_rlock lock(lpChangeAdvisor->m_hConnectionLock);
	if ((lpChangeAdvisor->m_ulFlags & SYNC_CATCHUP))
		return hrSuccess;
	/**
	 * Here we will reregister all change notifications.
	 **/

	// Unregister notifications first
	lpChangeAdvisor->m_lpMsgStore->m_lpNotifyClient->Unadvise(ECLISTCONNECTION(lpChangeAdvisor->m_mapConnections.begin(), lpChangeAdvisor->m_mapConnections.end()));
	lpChangeAdvisor->m_mapConnections.clear();
	// Now re-register the notifications
	std::transform(lpChangeAdvisor->m_mapSyncStates.begin(), lpChangeAdvisor->m_mapSyncStates.end(), std::back_inserter(listSyncStates),
		[](const SyncStateMap::value_type &e) -> SSyncState { return {e.first, e.second}; });
	hr = lpChangeAdvisor->m_lpMsgStore->m_lpNotifyClient->Advise(listSyncStates, lpChangeAdvisor->m_lpChangeAdviseSink, &listConnections);
	if (hr == hrSuccess)
		lpChangeAdvisor->m_mapConnections.insert(std::make_move_iterator(listConnections.begin()), std::make_move_iterator(listConnections.end()));
	return hr;
}
