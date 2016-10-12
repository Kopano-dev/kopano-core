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

#include <kopano/zcdefs.h>
#include <mutex>
#include <kopano/platform.h>
#include <kopano/lockhelper.hpp>

#include "ECSyncContext.h"
#include "ECOfflineABImporter.h"
#include "ECSyncUtil.h"
#include "ECSyncSettings.h"
#include <IECExportAddressbookChanges.h>
#include <IECExportChanges.h>
#include <IECChangeAdvisor.h>

#include <kopano/ECUnknown.h>
#include <kopano/ECGuid.h>
#include <kopano/ECTags.h>
#include <kopano/ECLogger.h>
#include <kopano/stringutil.h>

#include <mapix.h>
#include <kopano/mapiext.h>
#include <mapiutil.h>
#include <edkguid.h>
#include <edkmdb.h>

#include <kopano/mapi_ptr.h>
typedef mapi_object_ptr<IECChangeAdvisor, IID_IECChangeAdvisor> ECChangeAdvisorPtr;
//DEFINEMAPIPTR(ECChangeAdvisor);
typedef mapi_object_ptr<IECChangeAdviseSink, IID_IECChangeAdviseSink> ECChangeAdviseSinkPtr;
//DEFINEMAPIPTR(ECChangeAdviseSink);

#define EC_SYNC_STATUS_VERSION			1

#define CALL_MEMBER_FN(object,ptrToMember)  ((object).*(ptrToMember))
class ECChangeAdviseSink _kc_final : public ECUnknown {
public:
	typedef ULONG(ECSyncContext::*NOTIFYCALLBACK)(ULONG,LPENTRYLIST);

	ECChangeAdviseSink(ECSyncContext *lpsSyncContext, NOTIFYCALLBACK fnCallback)
		: m_lpsSyncContext(lpsSyncContext)
		, m_fnCallback(fnCallback)
	{ }

	// IUnknown
	HRESULT QueryInterface(REFIID refiid, void **lpvoid) {
		if (refiid == IID_ECUnknown || refiid == IID_ECChangeAdviseSink) {
			AddRef();
			*lpvoid = (void *)this;
			return hrSuccess;
		}
		if (refiid == IID_IUnknown || refiid == IID_IECChangeAdviseSink) {
			AddRef();
			*lpvoid = (void *)&this->m_xECChangeAdviseSink;
			return hrSuccess;
		}

		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	}

	// IExchangeChangeAdviseSink
	ULONG OnNotify(ULONG ulFlags, LPENTRYLIST lpEntryList) {
		return CALL_MEMBER_FN(*m_lpsSyncContext, m_fnCallback)(ulFlags, lpEntryList);
	}

private:
	class xECChangeAdviseSink _zcp_final : public IECChangeAdviseSink {
	public:
		// IUnknown
		virtual ULONG __stdcall AddRef(void) _zcp_override
		{
			METHOD_PROLOGUE_(ECChangeAdviseSink, ECChangeAdviseSink);
			return pThis->AddRef();
		}

		virtual ULONG __stdcall Release(void) _zcp_override
		{
			METHOD_PROLOGUE_(ECChangeAdviseSink, ECChangeAdviseSink);
			return pThis->Release();
		}

		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **pInterface) _zcp_override
		{
			METHOD_PROLOGUE_(ECChangeAdviseSink, ECChangeAdviseSink);
			return pThis->QueryInterface(refiid, pInterface);
		}

		// IExchangeChangeAdviseSink
		virtual ULONG __stdcall OnNotify(ULONG ulFlags, LPENTRYLIST lpEntryList) {
			METHOD_PROLOGUE_(ECChangeAdviseSink, ECChangeAdviseSink);
			return pThis->OnNotify(ulFlags, lpEntryList);
		}
	} m_xECChangeAdviseSink;

private:
	ECSyncContext	*m_lpsSyncContext;
	NOTIFYCALLBACK	m_fnCallback;
};

static HRESULT HrCreateECChangeAdviseSink(ECSyncContext *lpsSyncContext,
    ECChangeAdviseSink::NOTIFYCALLBACK fnCallback,
    IECChangeAdviseSink **lppAdviseSink)
{
	ECChangeAdviseSink *lpAdviseSink =
		new(std::nothrow) ECChangeAdviseSink(lpsSyncContext, fnCallback);
	if (lpAdviseSink == NULL)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	HRESULT hr = lpAdviseSink->QueryInterface(IID_IECChangeAdviseSink,
		reinterpret_cast<void **>(lppAdviseSink));
	if (hr == hrSuccess)
		lpAdviseSink = NULL;
	if (lpAdviseSink)
		lpAdviseSink->Release();

	return hr;
}

ECSyncContext::ECSyncContext(LPMDB lpStore, ECLogger *lpLogger)
	: m_lpStore(lpStore)
	, m_lpLogger(lpLogger)
	, m_lpSettings(ECSyncSettings::GetInstance())
	, m_lpChangeAdvisor(NULL)
	, m_lpChangeAdviseSink(NULL)
{
	m_lpLogger->AddRef();
	m_lpStore->AddRef();

	if (m_lpSettings->ChangeNotificationsEnabled())
		HrCreateECChangeAdviseSink(this, &ECSyncContext::OnChange, &m_lpChangeAdviseSink);
}

ECSyncContext::~ECSyncContext()
{
	if (m_lpChangeAdvisor)
		m_lpChangeAdvisor->Release();

	if (m_lpChangeAdviseSink)
		m_lpChangeAdviseSink->Release();

	if (m_lpStore)
		m_lpStore->Release();

	m_lpLogger->Release();
}

HRESULT ECSyncContext::HrGetMsgStore(LPMDB *lppMsgStore)
{
	if (lppMsgStore == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (m_lpStore == NULL)
		return MAPI_E_NOT_FOUND;
	return m_lpStore->QueryInterface(IID_IMsgStore,
	       reinterpret_cast<void **>(lppMsgStore));
}

HRESULT ECSyncContext::HrGetReceiveFolder(LPMAPIFOLDER *lppInboxFolder)
{
	HRESULT			hr = hrSuccess;
	ULONG			cbEntryID = 0;
	LPENTRYID		lpEntryID = NULL;
	ULONG			ulObjType = 0;
	LPMAPIFOLDER	lpInboxFolder = NULL;

	hr = m_lpStore->GetReceiveFolder((LPTSTR)"IPM", 0, &cbEntryID, &lpEntryID, NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = m_lpStore->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, (LPUNKNOWN*)&lpInboxFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = lpInboxFolder->QueryInterface(IID_IMAPIFolder, (void**)lppInboxFolder);

exit:
	if (lpInboxFolder)
		lpInboxFolder->Release();
	MAPIFreeBuffer(lpEntryID);
	return hr;
}

HRESULT ECSyncContext::HrGetChangeAdvisor(IECChangeAdvisor **lppChangeAdvisor)
{
	std::unique_lock<std::mutex> lk(m_hMutex);

	if (!m_lpSettings->ChangeNotificationsEnabled())
		return MAPI_E_NO_SUPPORT;
	if (m_lpChangeAdvisor == NULL) {
		HRESULT hr = m_lpStore->OpenProperty(PR_EC_CHANGE_ADVISOR,
			&IID_IECChangeAdvisor, 0, 0,
			reinterpret_cast<LPUNKNOWN *>(&m_lpChangeAdvisor));
		if (hr != hrSuccess)
			return hr;
	}
	lk.unlock();
	return m_lpChangeAdvisor->QueryInterface(IID_IECChangeAdvisor,
	       reinterpret_cast<void **>(lppChangeAdvisor));
}

HRESULT ECSyncContext::HrReleaseChangeAdvisor()
{
	ECChangeAdvisorPtr ptrReleaseMe;

	// WARNING:
	// This must come after the declaration of ptrReleaseMe to
	// ensure the mutex is unlocked before the change advisor
	// is released.
	scoped_lock lock(m_hMutex);

	if (!m_lpSettings->ChangeNotificationsEnabled())
		return MAPI_E_NO_SUPPORT;

	if (m_lpChangeAdvisor) {
		// Don't release while holding the lock as that might
		// cause a deadlock if a notification is being delivered.
		ptrReleaseMe.reset(m_lpChangeAdvisor);
		m_lpChangeAdvisor = NULL;
	}

	m_mapNotifiedSyncIds.clear();
	return hrSuccess;
}

HRESULT ECSyncContext::HrResetChangeAdvisor()
{
	ECChangeAdvisorPtr ptrChangeAdvisor;
	ECChangeAdviseSinkPtr ptrChangeAdviseSink;

	HRESULT hr = HrReleaseChangeAdvisor();
	if (hr != hrSuccess)
		return hr;
	hr = HrGetChangeAdvisor(&ptrChangeAdvisor);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetChangeAdviseSink(&ptrChangeAdviseSink);
	if (hr != hrSuccess)
		return hr;
	return ptrChangeAdvisor->Config(NULL, NULL, ptrChangeAdviseSink, 0);
}

HRESULT ECSyncContext::HrGetChangeAdviseSink(IECChangeAdviseSink **lppChangeAdviseSink)
{
	assert(m_lpChangeAdviseSink != NULL);
	return m_lpChangeAdviseSink->QueryInterface(IID_IECChangeAdviseSink, (void**)lppChangeAdviseSink);
}

HRESULT ECSyncContext::HrQueryHierarchyTable(LPSPropTagArray lpsPropTags, LPSRowSet *lppRows)
{
	HRESULT			hr = hrSuccess;
	LPMAPIFOLDER	lpRootFolder = NULL;
	ULONG			ulType = 0;
	LPMAPITABLE		lpTable = NULL;

	assert(lppRows != NULL);
	hr = m_lpStore->OpenEntry(0, NULL, &IID_IMAPIFolder, MAPI_DEFERRED_ERRORS, &ulType, (LPUNKNOWN*)&lpRootFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = lpRootFolder->GetHierarchyTable(CONVENIENT_DEPTH, &lpTable);
	if (hr != hrSuccess)
		goto exit;

	hr = HrQueryAllRows(lpTable, lpsPropTags, NULL, NULL, 0, lppRows);
	if(hr != hrSuccess)
		goto exit;

exit:
	if (lpTable)
		lpTable->Release();

	if (lpRootFolder)
		lpRootFolder->Release();

	return hr;
}

HRESULT ECSyncContext::HrOpenRootFolder(LPMAPIFOLDER *lppRootFolder, LPMDB *lppMsgStore)
{
	HRESULT			hr = hrSuccess;
	LPMAPIFOLDER	lpRootFolder = NULL;
	SBinary			sEntryID = {0};

	assert(lppRootFolder != NULL);
	hr = HrOpenFolder(&sEntryID, &lpRootFolder);
	if (hr != hrSuccess)
		goto exit;

	if (lppMsgStore) {
		hr = HrGetMsgStore(lppMsgStore);
		if (hr != hrSuccess)
			goto exit;
	}

	*lppRootFolder = lpRootFolder;
	lpRootFolder = NULL;

exit:
	if (lpRootFolder)
		lpRootFolder->Release();

	return hr;
}

HRESULT ECSyncContext::HrOpenFolder(SBinary *lpsEntryID, LPMAPIFOLDER *lppFolder)
{
	HRESULT			hr = hrSuccess;
	LPMAPIFOLDER	lpFolder = NULL;
	ULONG			ulType = 0;

	assert(lpsEntryID != NULL);
	assert(lppFolder != NULL);
	hr = m_lpStore->OpenEntry(lpsEntryID->cb, (LPENTRYID)lpsEntryID->lpb, &IID_IMAPIFolder, MAPI_DEFERRED_ERRORS|MAPI_MODIFY, &ulType, (LPUNKNOWN*)&lpFolder);
	if (hr != hrSuccess)
		goto exit;

	*lppFolder = lpFolder;
	lpFolder = NULL;

exit:
	if (lpFolder)
		lpFolder->Release();

	return hr;
}

HRESULT ECSyncContext::HrNotifyNewMail(LPNOTIFICATION lpNotification)
{
	return m_lpStore->NotifyNewMail(lpNotification);
}

HRESULT ECSyncContext::HrGetSteps(SBinary *lpEntryID, SBinary *lpSourceKey, ULONG ulSyncFlags, ULONG *lpulSteps)
{
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpFolder = NULL;
	LPSPropValue lpPropVal = NULL;
	LPSTREAM lpStream = NULL;
	IExchangeExportChanges *lpIEEC = NULL;
	IECExportChanges *lpECEC = NULL;
	ULONG ulChangeCount = 0;
	ULONG ulChangeId = 0;
	ULONG ulType = 0;
	SSyncState sSyncState = {0};
	IECChangeAdvisor *lpECA = NULL;
	bool bNotified = false;

	assert(lpulSteps != NULL);

	// First see if the changeadvisor is monitoring the requested folder.
	if (m_lpChangeAdvisor == NULL)
		goto fallback;

	hr = HrGetSyncStateFromSourceKey(lpSourceKey, &sSyncState);
	if (hr == MAPI_E_NOT_FOUND)
		goto fallback;
	else if (hr != hrSuccess)
		goto exit;

	hr = m_lpChangeAdvisor->QueryInterface(IID_IECChangeAdvisor, (void**)&lpECA);
	if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED)
		goto fallback;
	else if (hr != hrSuccess)
		goto exit;

	hr = lpECA->IsMonitoringSyncId(sSyncState.ulSyncId);
	if (hr == hrSuccess) {
		std::unique_lock<std::mutex> lk(m_hMutex);

		auto iterNotifiedSyncId = m_mapNotifiedSyncIds.find(sSyncState.ulSyncId);
		if (iterNotifiedSyncId == m_mapNotifiedSyncIds.cend()) {
			*lpulSteps = 0;
			m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "GetSteps: sourcekey=%s, syncid=%u, notified=yes, steps=0 (unsignalled)", bin2hex(lpSourceKey->cb, lpSourceKey->lpb).c_str(), sSyncState.ulSyncId);
			lk.unlock();
			goto exit;
		}

		ulChangeId = iterNotifiedSyncId->second;	// Remember for later.
		bNotified = true;
	} else if (hr == MAPI_E_NOT_FOUND) {
		SBinary	sEntry = { sizeof(sSyncState), (LPBYTE)&sSyncState };
		SBinaryArray sEntryList = { 1, &sEntry };

		hr = m_lpChangeAdvisor->AddKeys(&sEntryList);
		if (hr != hrSuccess)
			goto exit;
	} else
		goto exit;

fallback:
	// The current folder is not being monitored, so get steps the old fashioned way.
	hr = m_lpStore->OpenEntry(lpEntryID->cb, (LPENTRYID)lpEntryID->lpb, 0, MAPI_DEFERRED_ERRORS, &ulType, (IUnknown **)&lpFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = HrGetSyncStatusStream(lpSourceKey, &lpStream);
	if (FAILED(hr))
		goto exit;

	hr = lpFolder->OpenProperty(PR_CONTENTS_SYNCHRONIZER, &IID_IExchangeExportChanges, 0, 0, (LPUNKNOWN *)&lpIEEC);
	if (hr != hrSuccess)
		goto exit;

	hr = lpIEEC->Config(lpStream, SYNC_CATCHUP | ulSyncFlags, NULL, NULL, NULL, NULL, 1);
	if (hr != hrSuccess)
		goto exit;

	hr = lpIEEC->QueryInterface(IID_IECExportChanges, (void **)&lpECEC);
	if (hr != hrSuccess)
		goto exit;

	hr = lpECEC->GetChangeCount(&ulChangeCount);
	if (hr != hrSuccess)
		goto exit;

	// If the change notification system was signalled for this syncid, but the server returns no results, we need
	// to remove it from the list. However there could have been a change in the mean time, so we need to check if
	// m_mapNotifiedSyncIds was updated for this syncid.
	if (bNotified && ulChangeCount == 0) {
		std::lock_guard<std::mutex> lock(m_hMutex);
		if (m_mapNotifiedSyncIds[sSyncState.ulSyncId] <= ulChangeId)
			m_mapNotifiedSyncIds.erase(sSyncState.ulSyncId);
	}

	*lpulSteps = ulChangeCount;
	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "GetSteps: sourcekey=%s, syncid=%u, notified=%s, steps=%u", bin2hex(lpSourceKey->cb, lpSourceKey->lpb).c_str(), sSyncState.ulSyncId, (bNotified ? "yes" : "no"), *lpulSteps);

exit:
	if (lpECA)
		lpECA->Release();

	if (lpECEC)
		lpECEC->Release();

	if (lpIEEC)
		lpIEEC->Release();

	if (lpStream)
		lpStream->Release();
	MAPIFreeBuffer(lpPropVal);
	if (lpFolder)
		lpFolder->Release();

	return hr;
}

HRESULT ECSyncContext::HrUpdateChangeId(LPSTREAM lpStream)
{
	syncid_t	ulSyncId = 0;
	changeid_t	ulChangeId = 0;
	ECChangeAdvisorPtr	ptrECA;

	assert(lpStream != NULL);
	HRESULT hr = HrDecodeSyncStateStream(lpStream, &ulSyncId, &ulChangeId);
	if (hr != hrSuccess)
		return hr;

	{
		std::lock_guard<std::mutex> lock(m_hMutex);
		if (m_mapNotifiedSyncIds[ulSyncId] <= ulChangeId)
			m_mapNotifiedSyncIds.erase(ulSyncId);
	}

	if(m_lpChangeAdvisor) {
		// Now inform the change advisor of our accomplishment
		hr = m_lpChangeAdvisor->QueryInterface(ptrECA.iid, &ptrECA);
		if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED)
			return hr;
		hr = ptrECA->UpdateSyncState(ulSyncId, ulChangeId);
		if (hr == MAPI_E_INVALID_PARAMETER)
			// We're apparently not tracking this syncid.
			return hrSuccess;
	}
	return hrSuccess;
}

HRESULT ECSyncContext::HrGetSyncStateFromSourceKey(SBinary *lpSourceKey, SSyncState *lpsSyncState)
{
	HRESULT							hr = hrSuccess;
	std::string						strSourceKey((char*)lpSourceKey->lpb, lpSourceKey->cb);
	LPSTREAM						lpStream = NULL;
	SSyncState						sSyncState = {0};

	// First check the sourcekey to syncid map.
	auto iterSyncState = m_mapStates.find(strSourceKey);
	if (iterSyncState != m_mapStates.cend()) {
		assert(iterSyncState->second.ulSyncId != 0);
		*lpsSyncState = iterSyncState->second;
		goto exit;
	}

	// Try to get the information from the status stream.
	hr = HrGetSyncStatusStream(lpSourceKey, &lpStream);
	if (FAILED(hr))
		goto exit;

	hr = HrDecodeSyncStateStream(lpStream, &sSyncState.ulSyncId, &sSyncState.ulChangeId, NULL);
	if (hr != hrSuccess)
		goto exit;

	if (sSyncState.ulSyncId == 0) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// update the sourcekey to syncid map.
	m_mapStates.insert(SyncStateMap::value_type(strSourceKey, sSyncState));
	*lpsSyncState = sSyncState;

exit:
	if (lpStream)
		lpStream->Release();

	return hr;
}

bool ECSyncContext::SyncStatusLoaded() const
{
	return !m_mapSyncStatus.empty();
}

HRESULT ECSyncContext::HrClearSyncStatus()
{
	m_mapSyncStatus.clear();
	return hrSuccess;
}

HRESULT ECSyncContext::HrLoadSyncStatus(SBinary *lpsSyncState)
{
	ULONG ulStatusCount = 0;
	ULONG ulStatusNumber = 0;
	ULONG ulVersion = 0;
	ULONG ulSize = 0;
	ULONG ulPos = 0;
	std::string strSourceKey;
	LPSTREAM lpStream = NULL;

	assert(lpsSyncState != NULL);
	if (lpsSyncState->cb < 8)
		return MAPI_E_CORRUPT_DATA;
	HrClearSyncStatus();

	ulVersion = *((ULONG*)(lpsSyncState->lpb));
	if (ulVersion != EC_SYNC_STATUS_VERSION)
		return hrSuccess;

	ulStatusCount = *((ULONG*)(lpsSyncState->lpb+4));

	ZLOG_DEBUG(m_lpLogger, "Loading sync status stream: version=%u, items=%u", ulVersion, ulStatusCount);

	ulPos = 8;
	for (ulStatusNumber = 0; ulStatusNumber < ulStatusCount; ++ulStatusNumber) {
		ulSize = *((ULONG*)(lpsSyncState->lpb + ulPos));
		ulPos += 4;

		if (ulSize <= 16 || ulPos + ulSize + 4 > lpsSyncState->cb)
			return MAPI_E_CORRUPT_DATA;

		strSourceKey.assign((char*)(lpsSyncState->lpb + ulPos), ulSize);
		ulPos += ulSize;
		ulSize = *((ULONG*)(lpsSyncState->lpb + ulPos));
		ulPos += 4;

		if (ulSize < 8 || ulPos + ulSize > lpsSyncState->cb)
			return MAPI_E_CORRUPT_DATA;

		ZLOG_DEBUG(m_lpLogger, "  Stream %u: size=%u, sourcekey=%s", ulStatusNumber, ulSize, bin2hex(strSourceKey.size(), (unsigned char*)strSourceKey.data()).c_str());

		HRESULT hr = CreateStreamOnHGlobal(GlobalAlloc(GPTR, ulSize), true, &lpStream);
		if (hr != hrSuccess)
			return hr;
		hr = lpStream->Write(lpsSyncState->lpb + ulPos, ulSize, &ulSize);
		if (hr != hrSuccess)
			return hr;
		m_mapSyncStatus[strSourceKey] = lpStream;
		lpStream = NULL;

		ulPos += ulSize;
	}
	return hrSuccess;
}

HRESULT ECSyncContext::HrSaveSyncStatus(LPSPropValue *lppSyncStatusProp)
{
	HRESULT hr = hrSuccess;
	std::string strSyncStatus;
	ULONG ulSize = 0;
	ULONG ulVersion = EC_SYNC_STATUS_VERSION;
	char* lpszStream = NULL;
	LARGE_INTEGER liPos = {{0, 0}};
	STATSTG sStat;
	LPSPropValue lpSyncStatusProp = NULL;

	assert(lppSyncStatusProp != NULL);
	strSyncStatus.assign((char*)&ulVersion, 4);
	ulSize = m_mapSyncStatus.size();
	strSyncStatus.append((char*)&ulSize, 4);

	ZLOG_DEBUG(m_lpLogger, "Saving sync status stream: items=%u", ulSize);

	for (const auto &ssp : m_mapSyncStatus) {
		ulSize = ssp.first.size();
		strSyncStatus.append((char*)&ulSize, 4);
		strSyncStatus.append(ssp.first);

		hr = ssp.second->Stat(&sStat, STATFLAG_NONAME);
		if (hr != hrSuccess)
			goto exit;

		ulSize = sStat.cbSize.LowPart;
		strSyncStatus.append((char*)&ulSize, 4);
		ZLOG_DEBUG(m_lpLogger, "  Stream: size=%u, sourcekey=%s", ulSize,
			bin2hex(ssp.first.size(), reinterpret_cast<const unsigned char *>(ssp.first.data())).c_str());
		hr = ssp.second->Seek(liPos, STREAM_SEEK_SET, NULL);
		if (hr != hrSuccess)
			goto exit;

		lpszStream = new char[sStat.cbSize.LowPart];
		hr = ssp.second->Read(lpszStream, sStat.cbSize.LowPart, &ulSize);
		if (hr != hrSuccess)
			goto exit;

		strSyncStatus.append(lpszStream, sStat.cbSize.LowPart);
		delete[] lpszStream;
		lpszStream = NULL;
	}

	hr = MAPIAllocateBuffer(sizeof *lpSyncStatusProp, (void**)&lpSyncStatusProp);
	if (hr != hrSuccess)
		goto exit;
	memset(lpSyncStatusProp, 0, sizeof *lpSyncStatusProp);

	lpSyncStatusProp->Value.bin.cb = strSyncStatus.size();
	hr = MAPIAllocateMore(strSyncStatus.size(), lpSyncStatusProp, (void**)&lpSyncStatusProp->Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;
	memcpy(lpSyncStatusProp->Value.bin.lpb, strSyncStatus.data(), strSyncStatus.size());

	*lppSyncStatusProp = lpSyncStatusProp;
	lpSyncStatusProp = NULL;

exit:
	MAPIFreeBuffer(lpSyncStatusProp);
	delete[] lpszStream;
	return hr;
}

HRESULT ECSyncContext::HrGetSyncStatusStream(LPMAPIFOLDER lpFolder, LPSTREAM *lppStream)
{
	HRESULT hr = hrSuccess;
	LPSPropValue lpPropVal = NULL;

	hr = HrGetOneProp(lpFolder, PR_SOURCE_KEY, &lpPropVal);
	if(hr != hrSuccess)
		goto exit;

	hr = HrGetSyncStatusStream(&lpPropVal->Value.bin, lppStream);
	if (hr != hrSuccess)
		goto exit;

exit:
	MAPIFreeBuffer(lpPropVal);
	return hr;
}

HRESULT ECSyncContext::HrGetSyncStatusStream(SBinary *lpsSourceKey, LPSTREAM *lppStream)
{
	HRESULT hr = hrSuccess;
	LPSTREAM lpStream = NULL;
	std::string strSourceKey;

	strSourceKey.assign((char*)lpsSourceKey->lpb, lpsSourceKey->cb);
	auto iStatusStream = m_mapSyncStatus.find(strSourceKey);
	if (iStatusStream != m_mapSyncStatus.cend()) {
		*lppStream = iStatusStream->second;
	} else {
		hr = CreateNullStatusStream(&lpStream);
		if (hr != hrSuccess)
			goto exit;

		hr = MAPI_W_POSITION_CHANGED;
		m_mapSyncStatus[strSourceKey] = lpStream;
		lpStream->AddRef();
		*lppStream = lpStream;
	}
	(*lppStream)->AddRef();

exit:
	if(lpStream)
		lpStream->Release();

	return hr;
}

HRESULT ECSyncContext::GetResyncID(ULONG *lpulResyncID)
{
	MAPIFolderPtr ptrRoot;
	SPropValuePtr ptrResyncID;

	if (lpulResyncID == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = HrOpenRootFolder(&ptrRoot, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrRoot, PR_EC_RESYNC_ID, &ptrResyncID);
	if (hr == hrSuccess)
		*lpulResyncID = ptrResyncID->Value.ul;
	else if (hr == MAPI_E_NOT_FOUND) {
		*lpulResyncID = 0;
		hr = hrSuccess;
	}
	return hr;
}

HRESULT ECSyncContext::SetResyncID(ULONG ulResyncID)
{
	MAPIFolderPtr ptrRoot;
	SPropValue sPropResyncID;

	HRESULT hr = HrOpenRootFolder(&ptrRoot, NULL);
	if (hr != hrSuccess)
		return hr;
	sPropResyncID.ulPropTag = PR_EC_RESYNC_ID;
	sPropResyncID.Value.ul = ulResyncID;
	return HrSetOneProp(ptrRoot, &sPropResyncID);
}

HRESULT ECSyncContext::GetStoredServerUid(LPGUID lpServerUid)
{
	MAPIFolderPtr ptrRoot;
	SPropValuePtr ptrServerUid;

	if (lpServerUid == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = HrOpenRootFolder(&ptrRoot, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrRoot, PR_EC_STORED_SERVER_UID, &ptrServerUid);
	if (hr != hrSuccess)
		return hr;
	if (ptrServerUid->Value.bin.lpb == NULL ||
	    ptrServerUid->Value.bin.cb != sizeof(*lpServerUid))
		return MAPI_E_CORRUPT_DATA;
	memcpy(lpServerUid, ptrServerUid->Value.bin.lpb, sizeof *lpServerUid);
	return hrSuccess;
}

HRESULT ECSyncContext::SetStoredServerUid(LPGUID lpServerUid)
{
	MAPIFolderPtr ptrRoot;
	SPropValue sPropServerUid;

	HRESULT hr = HrOpenRootFolder(&ptrRoot, NULL);
	if (hr != hrSuccess)
		return hr;

	sPropServerUid.ulPropTag = PR_EC_STORED_SERVER_UID;
	sPropServerUid.Value.bin.cb = sizeof *lpServerUid;
	sPropServerUid.Value.bin.lpb = (LPBYTE)lpServerUid;
	return HrSetOneProp(ptrRoot, &sPropServerUid);
}

HRESULT ECSyncContext::GetServerUid(LPGUID lpServerUid)
{
	MsgStorePtr ptrStore;
	SPropValuePtr ptrServerUid;

	if (lpServerUid == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = HrGetMsgStore(&ptrStore);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrStore, PR_EC_SERVER_UID, &ptrServerUid);
	if (hr != hrSuccess)
		return hr;
	if (ptrServerUid->Value.bin.lpb == NULL ||
	    ptrServerUid->Value.bin.cb != sizeof(*lpServerUid))
		return MAPI_E_CORRUPT_DATA;

	memcpy(lpServerUid, ptrServerUid->Value.bin.lpb, sizeof *lpServerUid);
	return hrSuccess;
}

ULONG ECSyncContext::OnChange(ULONG ulFlags, LPENTRYLIST lpEntryList)
{
	ULONG ulSyncId = 0;
	ULONG ulChangeId = 0;
	std::lock_guard<std::mutex> lock(m_hMutex);

	for (unsigned i = 0; i < lpEntryList->cValues; ++i) {
		if (lpEntryList->lpbin[i].cb < 8) {
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "change notification: [Invalid]");
			continue;
		}

		ulSyncId = SYNCID(lpEntryList->lpbin[i].lpb);
		ulChangeId = CHANGEID(lpEntryList->lpbin[i].lpb);
		m_mapNotifiedSyncIds[ulSyncId] = ulChangeId;

		m_lpLogger->Log(EC_LOGLEVEL_INFO, "change notification: syncid=%u, changeid=%u", ulSyncId, ulChangeId);
	}
	return 0;
}
