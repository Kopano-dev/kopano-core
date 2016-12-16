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

#include <kopano/platform.h>
#include <kopano/lockhelper.hpp>
#include <kopano/memory.hpp>
#include <kopano/ECGuid.h>
#include <kopano/ECInterfaceDefs.h>
#include <ECSyncLog.h>
#include <kopano/ECDebug.h>
#include <kopano/ECLogger.h>

#include "ECChangeAdvisor.h"
#include "ECMsgStore.h"

ULONG ECChangeAdvisor::GetSyncId(const ConnectionMap::value_type &sConnection)
{
	return sConnection.first;
}

ECChangeAdvisor::SyncStateMap::value_type ECChangeAdvisor::ConvertSyncState(const SSyncState &sSyncState)
{
	return SyncStateMap::value_type(sSyncState.ulSyncId, sSyncState.ulChangeId);
}

SSyncState ECChangeAdvisor::ConvertSyncStateMapEntry(const SyncStateMap::value_type &sMapEntry)
{
	SSyncState tmp = {sMapEntry.first, sMapEntry.second};
	return tmp;
}

bool ECChangeAdvisor::CompareSyncId(const ConnectionMap::value_type &sConnection, const SyncStateMap::value_type &sSyncState)
{
	return sConnection.first < sSyncState.first;
}

ECChangeAdvisor::ECChangeAdvisor(ECMsgStore *lpMsgStore)
	: m_lpMsgStore(lpMsgStore)
	, m_lpChangeAdviseSink(NULL)
	, m_ulFlags(0)
	, m_ulReloadId(0)
{ 
	ECSyncLog::GetLogger(&m_lpLogger);

	m_lpMsgStore->AddRef();

	// Need MUTEX RECURSIVE because with a reconnection the funtion PurgeStates called indirect the function Reload again:
	// ECChangeAdvisor::Reload(....)
 	// WSTransport::HrReLogon()
 	// WSTransport::HrGetSyncStates(....)
 	// ECChangeAdvisor::PurgeStates()
 	// ECChangeAdvisor::UpdateState(IStream * lpStream)
}

ECChangeAdvisor::~ECChangeAdvisor()
{
	if (m_ulReloadId)
		m_lpMsgStore->lpTransport->RemoveSessionReloadCallback(m_ulReloadId);

	// Unregister notifications
	if (!(m_ulFlags & SYNC_CATCHUP))
		m_lpMsgStore->m_lpNotifyClient->Unadvise(ECLISTCONNECTION(m_mapConnections.begin(), m_mapConnections.end()));

	if (m_lpChangeAdviseSink)
		m_lpChangeAdviseSink->Release();
		
	if (m_lpLogger)
		m_lpLogger->Release();
	m_lpMsgStore->Release();
}

HRESULT ECChangeAdvisor::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECChangeAdvisor, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IECChangeAdvisor, &this->m_xECChangeAdvisor);
	REGISTER_INTERFACE2(IUnknown, &this->m_xECChangeAdvisor);
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
	HRESULT			hr = hrSuccess;
	ECChangeAdvisor	*lpChangeAdvisor = NULL;
	BOOL			fEnhancedICS = false;

	if (lpMsgStore == NULL || lppChangeAdvisor == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (lpMsgStore->m_lpNotifyClient == NULL) {
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	hr = lpMsgStore->lpTransport->HrCheckCapabilityFlags(KOPANO_CAP_ENHANCED_ICS, &fEnhancedICS);
	if (hr != hrSuccess)
		goto exit;
	if (!fEnhancedICS) {
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	lpChangeAdvisor = new ECChangeAdvisor(lpMsgStore);
	hr = lpChangeAdvisor->QueryInterface(IID_ECChangeAdvisor, (void**)lppChangeAdvisor);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMsgStore->lpTransport->AddSessionReloadCallback(lpChangeAdvisor, &Reload, &lpChangeAdvisor->m_ulReloadId);
	if (hr != hrSuccess)
		goto exit;

	lpChangeAdvisor = NULL;

exit:
	if (lpChangeAdvisor)
		lpChangeAdvisor->Release();

	return hr;
}

HRESULT ECChangeAdvisor::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECChangeAdvisor::Config(LPSTREAM lpStream, LPGUID /*lpGUID*/,
    IECChangeAdviseSink *lpAdviseSink, ULONG ulFlags)
{
	HRESULT					hr = hrSuccess;
	ULONG					ulVal = 0;
	KCHL::memory_ptr<ENTRYLIST> lpEntryList;
	ULONG					ulRead = {0};
	LARGE_INTEGER			liSeekStart = {{0}};

	if (lpAdviseSink == nullptr && !(ulFlags & SYNC_CATCHUP))
		return MAPI_E_INVALID_PARAMETER;

	// Unregister notifications
	if (!(m_ulFlags & SYNC_CATCHUP))
		m_lpMsgStore->m_lpNotifyClient->Unadvise(ECLISTCONNECTION(m_mapConnections.begin(), m_mapConnections.end()));
	m_mapConnections.clear();

	if (m_lpChangeAdviseSink) {
		m_lpChangeAdviseSink->Release();
		m_lpChangeAdviseSink = NULL;
	}

	m_ulFlags = ulFlags;

	if (lpAdviseSink) {
		m_lpChangeAdviseSink = lpAdviseSink;
		m_lpChangeAdviseSink->AddRef();
	}

	if (lpStream == NULL)
		return hr;
	hr = lpStream->Seek(liSeekStart, SEEK_SET, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = lpStream->Read(&ulVal, sizeof(ulVal), &ulRead);
	if (hr != hrSuccess)
		return hr;
	if (ulRead != sizeof(ulVal))
		return MAPI_E_CALL_FAILED;

	if (ulVal > 0) {
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

		hr = AddKeys(lpEntryList);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
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
	std::transform(m_mapConnections.begin(), m_mapConnections.end(), std::back_inserter(lstSyncId), &GetSyncId);
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
	HRESULT					hr = hrSuccess;
	LARGE_INTEGER			liPos = {{0}};
	ULARGE_INTEGER			uliSize = {{0}};
	ULONG					ulVal = 0;
	SyncStateMap			mapChangeId;
	scoped_rlock lock(m_hConnectionLock);

	if (m_lpChangeAdviseSink == NULL && !(m_ulFlags & SYNC_CATCHUP))
		return MAPI_E_UNCONFIGURED;
	if (lpStream == NULL)
		return MAPI_E_INVALID_PARAMETER;
	hr = PurgeStates();
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
	HRESULT						hr = hrSuccess;
	SSyncState					*lpsSyncState = NULL;
	ECLISTCONNECTION			listConnections;
	ECLISTSYNCSTATE				listSyncStates;

	if (m_lpChangeAdviseSink == NULL && !(m_ulFlags & SYNC_CATCHUP))
		return MAPI_E_UNCONFIGURED;
	if (lpEntryList == NULL)
		return MAPI_E_INVALID_PARAMETER;

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
				listSyncStates.push_back(*lpsSyncState);
			else
				listConnections.push_back(ConnectionMap::value_type(lpsSyncState->ulSyncId, 0));
		} else {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, " - Key %u: Invalid size=%u", i, lpEntryList->lpbin[i].cb);
			hr = MAPI_E_INVALID_PARAMETER;
		}
	}

	if (!(m_ulFlags & SYNC_CATCHUP))
		hr = m_lpMsgStore->m_lpNotifyClient->Advise(listSyncStates, m_lpChangeAdviseSink, &listConnections);

	if (hr == hrSuccess) {
		m_mapConnections.insert(listConnections.begin(), listConnections.end());
		std::transform(listSyncStates.begin(), listSyncStates.end(), std::inserter(m_mapSyncStates, m_mapSyncStates.begin()), &ConvertSyncState);
	}
	return hr;
}

HRESULT ECChangeAdvisor::RemoveKeys(LPENTRYLIST lpEntryList)
{
	HRESULT					hr = hrSuccess;
	SSyncState				*lpsSyncState = NULL;
	ECLISTCONNECTION		listConnections;

	if (m_lpChangeAdviseSink == NULL && !(m_ulFlags & SYNC_CATCHUP))
		return MAPI_E_UNCONFIGURED;
	if (lpEntryList == NULL)
		return MAPI_E_INVALID_PARAMETER;

	scoped_rlock lock(m_hConnectionLock);
	
	for (ULONG i = 0; hr == hrSuccess && i < lpEntryList->cValues; ++i) {
		if (lpEntryList->lpbin[i].cb >= sizeof(SSyncState)) {
			lpsSyncState = (SSyncState*)lpEntryList->lpbin[i].lpb;

			// Try to delete the sync state from state map anyway
			m_mapSyncStates.erase(lpsSyncState->ulSyncId);

			// Check if we even have the sync state
			auto iterConnection = m_mapConnections.find(lpsSyncState->ulSyncId);
			if (iterConnection == m_mapConnections.cend())
				continue;

			// Unregister the sync state.
			if (!(m_ulFlags & SYNC_CATCHUP))
				listConnections.push_back(*iterConnection);

			// Remove from map
			m_mapConnections.erase(iterConnection);
		}
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
	HRESULT				hr = hrSuccess;
	ECChangeAdvisor		*lpChangeAdvisor = (ECChangeAdvisor*)lpParam;
	ECLISTSYNCSTATE		listSyncStates;
	ECLISTCONNECTION	listConnections;

	if (lpParam == NULL)
		return MAPI_E_INVALID_PARAMETER;

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
	std::transform(lpChangeAdvisor->m_mapSyncStates.begin(), lpChangeAdvisor->m_mapSyncStates.end(), std::back_inserter(listSyncStates), &ConvertSyncStateMapEntry);
	hr = lpChangeAdvisor->m_lpMsgStore->m_lpNotifyClient->Advise(listSyncStates, lpChangeAdvisor->m_lpChangeAdviseSink, &listConnections);
	if (hr == hrSuccess)
		lpChangeAdvisor->m_mapConnections.insert(listConnections.begin(), listConnections.end());
	return hr;
}

// IECChangeAdvisor interface
DEF_ULONGMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, GetLastError, (HRESULT, hResult), (ULONG, ulFlags), (LPMAPIERROR *, lppMAPIError))
DEF_HRMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, Config, (LPSTREAM, lpStream), (LPGUID, lpGUID), (IECChangeAdviseSink *, lpAdviseSink), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, UpdateState, (LPSTREAM, lpStream))
DEF_HRMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, AddKeys, (LPENTRYLIST, lpEntryList))
DEF_HRMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, RemoveKeys, (LPENTRYLIST, lpEntryList))
DEF_HRMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, IsMonitoringSyncId, (syncid_t, ulSyncId))
DEF_HRMETHOD1(TRACE_MAPI, ECChangeAdvisor, ECChangeAdvisor, UpdateSyncState, (syncid_t, ulSyncId), (changeid_t, ulChangeId))
