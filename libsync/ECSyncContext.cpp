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
#include <memory>
#include <mutex>
#include <utility>
#include <kopano/platform.h>
#include <kopano/lockhelper.hpp>
#include <kopano/memory.hpp>
#include "ECSyncContext.h"
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
#include <kopano/Util.h>
#include <mapix.h>
#include <kopano/mapiext.h>
#include <mapiutil.h>
#include <edkguid.h>
#include <edkmdb.h>

#include <kopano/mapi_ptr.h>

using namespace KCHL;

typedef object_ptr<IECChangeAdvisor> ECChangeAdvisorPtr;
//DEFINEMAPIPTR(ECChangeAdvisor);

#define EC_SYNC_STATUS_VERSION			1

#define CALL_MEMBER_FN(object,ptrToMember)  ((object).*(ptrToMember))
class ECChangeAdviseSink _kc_final :
    public ECUnknown, public IECChangeAdviseSink {
public:
	typedef ULONG(ECSyncContext::*NOTIFYCALLBACK)(ULONG,LPENTRYLIST);

	ECChangeAdviseSink(ECSyncContext *lpsSyncContext, NOTIFYCALLBACK fnCallback)
		: m_lpsSyncContext(lpsSyncContext)
		, m_fnCallback(fnCallback)
	{ }

	// IUnknown
	HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override
	{
		REGISTER_INTERFACE2(ECChangeAdviseSink, this);
		REGISTER_INTERFACE2(ECUnknown, this);
		REGISTER_INTERFACE2(IECChangeAdviseSink, this);
		REGISTER_INTERFACE2(IUnknown, this);
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	}

	// IExchangeChangeAdviseSink
	ULONG OnNotify(ULONG ulFlags, LPENTRYLIST lpEntryList) {
		return CALL_MEMBER_FN(*m_lpsSyncContext, m_fnCallback)(ulFlags, lpEntryList);
	}

private:
	ECSyncContext	*m_lpsSyncContext;
	NOTIFYCALLBACK	m_fnCallback;
};

static HRESULT HrCreateECChangeAdviseSink(ECSyncContext *lpsSyncContext,
    ECChangeAdviseSink::NOTIFYCALLBACK fnCallback,
    IECChangeAdviseSink **lppAdviseSink)
{
	return alloc_wrap<ECChangeAdviseSink>(lpsSyncContext, fnCallback)
	       .as(IID_IECChangeAdviseSink, lppAdviseSink);
}

ECSyncContext::ECSyncContext(LPMDB lpStore, ECLogger *lpLogger)
	: m_lpStore(lpStore)
	, m_lpLogger(lpLogger)
	, m_lpSettings(ECSyncSettings::GetInstance())
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
	memory_ptr<ENTRYID> lpEntryID;
	ULONG			ulObjType = 0;
	object_ptr<IMAPIFolder> lpInboxFolder;

	hr = m_lpStore->GetReceiveFolder((LPTSTR)"IPM", 0, &cbEntryID, &~lpEntryID, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = m_lpStore->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpInboxFolder);
	if (hr != hrSuccess)
		return hr;
	return lpInboxFolder->QueryInterface(IID_IMAPIFolder, (void**)lppInboxFolder);
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
	object_ptr<IECChangeAdviseSink> ptrChangeAdviseSink;

	HRESULT hr = HrReleaseChangeAdvisor();
	if (hr != hrSuccess)
		return hr;
	hr = HrGetChangeAdvisor(&~ptrChangeAdvisor);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetChangeAdviseSink(&~ptrChangeAdviseSink);
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
	object_ptr<IMAPIFolder> lpRootFolder;
	ULONG			ulType = 0;
	object_ptr<IMAPITable> lpTable;

	assert(lppRows != NULL);
	hr = m_lpStore->OpenEntry(0, nullptr, &IID_IMAPIFolder, MAPI_DEFERRED_ERRORS, &ulType, &~lpRootFolder);
	if (hr != hrSuccess)
		return hr;
	hr = lpRootFolder->GetHierarchyTable(CONVENIENT_DEPTH, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	return HrQueryAllRows(lpTable, lpsPropTags, nullptr, nullptr, 0, lppRows);
}

HRESULT ECSyncContext::HrOpenRootFolder(LPMAPIFOLDER *lppRootFolder, LPMDB *lppMsgStore)
{
	HRESULT			hr = hrSuccess;
	object_ptr<IMAPIFolder> lpRootFolder;
	SBinary			sEntryID = {0};

	assert(lppRootFolder != NULL);
	hr = HrOpenFolder(&sEntryID, &~lpRootFolder);
	if (hr != hrSuccess)
		return hr;

	if (lppMsgStore) {
		hr = HrGetMsgStore(lppMsgStore);
		if (hr != hrSuccess)
			return hr;
	}
	*lppRootFolder = lpRootFolder.release();
	return hrSuccess;
}

HRESULT ECSyncContext::HrOpenFolder(SBinary *lpsEntryID, LPMAPIFOLDER *lppFolder)
{
	HRESULT			hr = hrSuccess;
	object_ptr<IMAPIFolder> lpFolder;
	ULONG			ulType = 0;

	assert(lpsEntryID != NULL);
	assert(lppFolder != NULL);
	hr = m_lpStore->OpenEntry(lpsEntryID->cb, reinterpret_cast<ENTRYID *>(lpsEntryID->lpb), &IID_IMAPIFolder, MAPI_DEFERRED_ERRORS | MAPI_MODIFY, &ulType, &~lpFolder);
	if (hr != hrSuccess)
		return hr;
	*lppFolder = lpFolder.release();
	return hrSuccess;
}

HRESULT ECSyncContext::HrNotifyNewMail(LPNOTIFICATION lpNotification)
{
	return m_lpStore->NotifyNewMail(lpNotification);
}

HRESULT ECSyncContext::HrGetSteps(SBinary *lpEntryID, SBinary *lpSourceKey, ULONG ulSyncFlags, ULONG *lpulSteps)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPIFolder> lpFolder;
	object_ptr<IStream> lpStream;
	object_ptr<IExchangeExportChanges> lpIEEC;
	object_ptr<IECExportChanges> lpECEC;
	ULONG ulChangeCount = 0;
	ULONG ulChangeId = 0;
	ULONG ulType = 0;
	SSyncState sSyncState = {0};
	object_ptr<IECChangeAdvisor> lpECA;
	bool bNotified = false;

	assert(lpulSteps != NULL);

	// First see if the changeadvisor is monitoring the requested folder.
	if (m_lpChangeAdvisor == NULL)
		goto fallback;

	hr = HrGetSyncStateFromSourceKey(lpSourceKey, &sSyncState);
	if (hr == MAPI_E_NOT_FOUND)
		goto fallback;
	else if (hr != hrSuccess)
		return hr;
	hr = m_lpChangeAdvisor->QueryInterface(IID_IECChangeAdvisor, &~lpECA);
	if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED)
		goto fallback;
	else if (hr != hrSuccess)
		return hr;

	hr = lpECA->IsMonitoringSyncId(sSyncState.ulSyncId);
	if (hr == hrSuccess) {
		std::unique_lock<std::mutex> lk(m_hMutex);

		auto iterNotifiedSyncId = m_mapNotifiedSyncIds.find(sSyncState.ulSyncId);
		if (iterNotifiedSyncId == m_mapNotifiedSyncIds.cend()) {
			*lpulSteps = 0;
			m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "GetSteps: sourcekey=%s, syncid=%u, notified=yes, steps=0 (unsignalled)", bin2hex(lpSourceKey->cb, lpSourceKey->lpb).c_str(), sSyncState.ulSyncId);
			lk.unlock();
			return hr;
		}

		ulChangeId = iterNotifiedSyncId->second;	// Remember for later.
		bNotified = true;
	} else if (hr == MAPI_E_NOT_FOUND) {
		SBinary	sEntry = { sizeof(sSyncState), (LPBYTE)&sSyncState };
		SBinaryArray sEntryList = { 1, &sEntry };

		hr = m_lpChangeAdvisor->AddKeys(&sEntryList);
		if (hr != hrSuccess)
			return hr;
	} else
		return hr;

fallback:
	// The current folder is not being monitored, so get steps the old fashioned way.
	hr = m_lpStore->OpenEntry(lpEntryID->cb, reinterpret_cast<ENTRYID *>(lpEntryID->lpb),
	     &iid_of(lpFolder), MAPI_DEFERRED_ERRORS, &ulType, &~lpFolder);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetSyncStatusStream(lpSourceKey, &~lpStream);
	if (FAILED(hr))
		return hr;
	hr = lpFolder->OpenProperty(PR_CONTENTS_SYNCHRONIZER, &IID_IExchangeExportChanges, 0, 0, &~lpIEEC);
	if (hr != hrSuccess)
		return hr;
	hr = lpIEEC->Config(lpStream, SYNC_CATCHUP | ulSyncFlags, NULL, NULL, NULL, NULL, 1);
	if (hr != hrSuccess)
		return hr;
	hr = lpIEEC->QueryInterface(IID_IECExportChanges, &~lpECEC);
	if (hr != hrSuccess)
		return hr;
	hr = lpECEC->GetChangeCount(&ulChangeCount);
	if (hr != hrSuccess)
		return hr;

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
	return hrSuccess;
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
		hr = m_lpChangeAdvisor->QueryInterface(iid_of(ptrECA), &~ptrECA);
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
	object_ptr<IStream> lpStream;
	SSyncState						sSyncState = {0};

	// First check the sourcekey to syncid map.
	auto iterSyncState = m_mapStates.find(strSourceKey);
	if (iterSyncState != m_mapStates.cend()) {
		assert(iterSyncState->second.ulSyncId != 0);
		*lpsSyncState = iterSyncState->second;
		return hr;
	}

	// Try to get the information from the status stream.
	hr = HrGetSyncStatusStream(lpSourceKey, &~lpStream);
	if (FAILED(hr))
		return hr;
	hr = HrDecodeSyncStateStream(lpStream, &sSyncState.ulSyncId, &sSyncState.ulChangeId, NULL);
	if (hr != hrSuccess)
		return hr;
	if (sSyncState.ulSyncId == 0)
		return MAPI_E_NOT_FOUND;

	// update the sourcekey to syncid map.
	m_mapStates.insert({strSourceKey, sSyncState});
	*lpsSyncState = std::move(sSyncState);
	return hrSuccess;
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

		ZLOG_DEBUG(m_lpLogger, "  Stream %u: size=%u, sourcekey=%s", ulStatusNumber, ulSize, bin2hex(strSourceKey.size(), strSourceKey.data()).c_str());

		HRESULT hr = CreateStreamOnHGlobal(GlobalAlloc(GPTR, ulSize), true, &lpStream);
		if (hr != hrSuccess)
			return hr;
		hr = lpStream->Write(lpsSyncState->lpb + ulPos, ulSize, &ulSize);
		if (hr != hrSuccess)
			return hr;
		m_mapSyncStatus[std::move(strSourceKey)] = lpStream;
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
	LARGE_INTEGER liPos = {{0, 0}};
	STATSTG sStat;
	memory_ptr<SPropValue> lpSyncStatusProp;

	assert(lppSyncStatusProp != NULL);
	strSyncStatus.assign((char*)&ulVersion, 4);
	ulSize = m_mapSyncStatus.size();
	strSyncStatus.append((char*)&ulSize, 4);

	ZLOG_DEBUG(m_lpLogger, "Saving sync status stream: items=%u", ulSize);

	for (const auto &ssp : m_mapSyncStatus) {
		std::unique_ptr<char[]> lpszStream;

		ulSize = ssp.first.size();
		strSyncStatus.append((char*)&ulSize, 4);
		strSyncStatus.append(ssp.first);

		hr = ssp.second->Stat(&sStat, STATFLAG_NONAME);
		if (hr != hrSuccess)
			return hr;
		ulSize = sStat.cbSize.LowPart;
		strSyncStatus.append((char*)&ulSize, 4);
		ZLOG_DEBUG(m_lpLogger, "  Stream: size=%u, sourcekey=%s", ulSize,
			bin2hex(ssp.first.size(), ssp.first.data()).c_str());
		hr = ssp.second->Seek(liPos, STREAM_SEEK_SET, NULL);
		if (hr != hrSuccess)
			return hr;
		lpszStream.reset(new char[sStat.cbSize.LowPart]);
		hr = ssp.second->Read(lpszStream.get(), sStat.cbSize.LowPart, &ulSize);
		if (hr != hrSuccess)
			return hr;
		strSyncStatus.append(lpszStream.get(), sStat.cbSize.LowPart);
	}

	hr = MAPIAllocateBuffer(sizeof *lpSyncStatusProp, &~lpSyncStatusProp);
	if (hr != hrSuccess)
		return hr;
	memset(lpSyncStatusProp, 0, sizeof *lpSyncStatusProp);

	lpSyncStatusProp->Value.bin.cb = strSyncStatus.size();
	hr = MAPIAllocateMore(strSyncStatus.size(), lpSyncStatusProp, (void**)&lpSyncStatusProp->Value.bin.lpb);
	if (hr != hrSuccess)
		return hr;
	memcpy(lpSyncStatusProp->Value.bin.lpb, strSyncStatus.data(), strSyncStatus.size());
	*lppSyncStatusProp = lpSyncStatusProp.release();
	return hrSuccess;
}

HRESULT ECSyncContext::HrGetSyncStatusStream(LPMAPIFOLDER lpFolder, LPSTREAM *lppStream)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpPropVal;

	hr = HrGetOneProp(lpFolder, PR_SOURCE_KEY, &~lpPropVal);
	if(hr != hrSuccess)
		return hr;
	return HrGetSyncStatusStream(&lpPropVal->Value.bin, lppStream);
}

HRESULT ECSyncContext::HrGetSyncStatusStream(SBinary *lpsSourceKey, LPSTREAM *lppStream)
{
	HRESULT hr = hrSuccess;
	object_ptr<IStream> lpStream;
	std::string strSourceKey;

	strSourceKey.assign((char*)lpsSourceKey->lpb, lpsSourceKey->cb);
	auto iStatusStream = m_mapSyncStatus.find(strSourceKey);
	if (iStatusStream != m_mapSyncStatus.cend()) {
		*lppStream = iStatusStream->second;
	} else {
		hr = CreateNullStatusStream(&~lpStream);
		if (hr != hrSuccess)
			return hr;
		hr = MAPI_W_POSITION_CHANGED;
		m_mapSyncStatus[std::move(strSourceKey)] = lpStream;
		lpStream->AddRef();
		*lppStream = lpStream;
	}
	(*lppStream)->AddRef();
	return hrSuccess;
}

HRESULT ECSyncContext::GetResyncID(ULONG *lpulResyncID)
{
	MAPIFolderPtr ptrRoot;
	SPropValuePtr ptrResyncID;

	if (lpulResyncID == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = HrOpenRootFolder(&~ptrRoot, nullptr);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrRoot, PR_EC_RESYNC_ID, &~ptrResyncID);
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

	HRESULT hr = HrOpenRootFolder(&~ptrRoot, nullptr);
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
	HRESULT hr = HrOpenRootFolder(&~ptrRoot, nullptr);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrRoot, PR_EC_STORED_SERVER_UID, &~ptrServerUid);
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

	HRESULT hr = HrOpenRootFolder(&~ptrRoot, nullptr);
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
	HRESULT hr = HrGetMsgStore(&~ptrStore);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrStore, PR_EC_SERVER_UID, &~ptrServerUid);
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
