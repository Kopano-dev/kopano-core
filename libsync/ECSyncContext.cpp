/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <memory>
#include <mutex>
#include <utility>
#include <cstdint>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "ECSyncContext.h"
#include "ECSyncUtil.h"
#include "ECSyncSettings.h"
#include <kopano/ECUnknown.h>
#include <kopano/ECGuid.h>
#include <kopano/ECTags.h>
#include <kopano/ECLogger.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/stringutil.h>
#include <kopano/Util.h>
#include <mapix.h>
#include <kopano/mapiext.h>
#include <mapiutil.h>
#include <edkguid.h>
#include <edkmdb.h>
#include <kopano/mapi_ptr.h>

using namespace KC;
typedef object_ptr<IECChangeAdvisor> ECChangeAdvisorPtr;
//DEFINEMAPIPTR(ECChangeAdvisor);

#define EC_SYNC_STATUS_VERSION			1

#define CALL_MEMBER_FN(object,ptrToMember)  ((object).*(ptrToMember))
class ECChangeAdviseSink final :
    public ECUnknown, public IECChangeAdviseSink {
public:
	typedef ULONG(ECSyncContext::*NOTIFYCALLBACK)(ULONG,LPENTRYLIST);

	ECChangeAdviseSink(ECSyncContext *lpsSyncContext, NOTIFYCALLBACK fnCallback)
		: m_lpsSyncContext(lpsSyncContext)
		, m_fnCallback(fnCallback)
	{ }

	// IUnknown
	HRESULT QueryInterface(const IID &refiid, void **lppInterface) override
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

ECSyncContext::ECSyncContext(IMsgStore *lpStore, std::shared_ptr<ECLogger> lpLogger) :
	m_lpLogger(std::move(lpLogger)), m_lpStore(lpStore),
	m_lpSettings(&ECSyncSettings::instance)
{
	if (m_lpSettings->ChangeNotificationsEnabled())
		HrCreateECChangeAdviseSink(this, &ECSyncContext::OnChange, &~m_lpChangeAdviseSink);
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
	ULONG			cbEntryID = 0;
	memory_ptr<ENTRYID> lpEntryID;
	ULONG			ulObjType = 0;
	object_ptr<IMAPIFolder> lpInboxFolder;

	auto hr = m_lpStore->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &cbEntryID, &~lpEntryID, nullptr);
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
			&IID_IECChangeAdvisor, 0, 0, &~m_lpChangeAdvisor);
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

	if (m_lpChangeAdvisor)
		// Don't release while holding the lock as that might
		// cause a deadlock if a notification is being delivered.
		ptrReleaseMe.reset(m_lpChangeAdvisor.release());
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
	object_ptr<IMAPIFolder> lpRootFolder;
	ULONG			ulType = 0;
	object_ptr<IMAPITable> lpTable;

	assert(lppRows != NULL);
	auto hr = m_lpStore->OpenEntry(0, nullptr, &IID_IMAPIFolder, MAPI_DEFERRED_ERRORS, &ulType, &~lpRootFolder);
	if (hr != hrSuccess)
		return hr;
	hr = lpRootFolder->GetHierarchyTable(CONVENIENT_DEPTH, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	return HrQueryAllRows(lpTable, lpsPropTags, nullptr, nullptr, 0, lppRows);
}

HRESULT ECSyncContext::HrOpenRootFolder(LPMAPIFOLDER *lppRootFolder, LPMDB *lppMsgStore)
{
	object_ptr<IMAPIFolder> lpRootFolder;
	SBinary			sEntryID = {0};

	assert(lppRootFolder != NULL);
	auto hr = HrOpenFolder(&sEntryID, &~lpRootFolder);
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
	object_ptr<IMAPIFolder> lpFolder;
	ULONG			ulType = 0;

	assert(lpsEntryID != NULL);
	assert(lppFolder != NULL);
	auto hr = m_lpStore->OpenEntry(lpsEntryID->cb, reinterpret_cast<ENTRYID *>(lpsEntryID->lpb), &IID_IMAPIFolder, MAPI_DEFERRED_ERRORS | MAPI_MODIFY, &ulType, &~lpFolder);
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
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "GetSteps: sourcekey=%s, syncid=%u, notified=yes, steps=0 (unsignalled)", bin2hex(*lpSourceKey).c_str(), sSyncState.ulSyncId);
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
	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "GetSteps: sourcekey=%s, syncid=%u, notified=%s, steps=%u",
		bin2hex(*lpSourceKey).c_str(), sSyncState.ulSyncId,
		(bNotified ? "yes" : "no"), *lpulSteps);
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

	if (m_lpChangeAdvisor == nullptr)
		return hrSuccess;
	// Now inform the change advisor of our accomplishment
	hr = m_lpChangeAdvisor->QueryInterface(iid_of(ptrECA), &~ptrECA);
	if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED)
		return hr;
	hr = ptrECA->UpdateSyncState(ulSyncId, ulChangeId);
	if (hr == MAPI_E_INVALID_PARAMETER)
		// We're apparently not tracking this syncid.
		return hrSuccess;
	return hrSuccess;
}

HRESULT ECSyncContext::HrGetSyncStateFromSourceKey(SBinary *lpSourceKey, SSyncState *lpsSyncState)
{
	std::string						strSourceKey((char*)lpSourceKey->lpb, lpSourceKey->cb);
	object_ptr<IStream> lpStream;
	SSyncState						sSyncState = {0};

	// First check the sourcekey to syncid map.
	auto iterSyncState = m_mapStates.find(strSourceKey);
	if (iterSyncState != m_mapStates.cend()) {
		assert(iterSyncState->second.ulSyncId != 0);
		*lpsSyncState = iterSyncState->second;
		return hrSuccess;
	}

	// Try to get the information from the status stream.
	auto hr = HrGetSyncStatusStream(lpSourceKey, &~lpStream);
	if (FAILED(hr))
		return hr;
	hr = HrDecodeSyncStateStream(lpStream, &sSyncState.ulSyncId, &sSyncState.ulChangeId, NULL);
	if (hr != hrSuccess)
		return hr;
	if (sSyncState.ulSyncId == 0)
		return MAPI_E_NOT_FOUND;

	// update the sourcekey to syncid map.
	m_mapStates.emplace(strSourceKey, sSyncState);
	*lpsSyncState = std::move(sSyncState);
	return hrSuccess;
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
	std::string strSourceKey;

	assert(lpsSyncState != NULL);
	if (lpsSyncState->cb < 8)
		return MAPI_E_CORRUPT_DATA;
	HrClearSyncStatus();

	memcpy(&ulVersion, lpsSyncState->lpb, sizeof(ulVersion));
	ulVersion = le32_to_cpu(ulVersion);
	if (ulVersion != EC_SYNC_STATUS_VERSION)
		return hrSuccess;

	memcpy(&ulStatusCount, lpsSyncState->lpb + 4, sizeof(ulStatusCount));
	ulStatusCount = le32_to_cpu(ulStatusCount);
	ZLOG_DEBUG(m_lpLogger, "Loading sync status stream: version=%u, items=%u", ulVersion, ulStatusCount);

	ULONG ulPos = 8;
	for (ulStatusNumber = 0; ulStatusNumber < ulStatusCount; ++ulStatusNumber) {
		uint32_t ulSize;
		memcpy(&ulSize, lpsSyncState->lpb + ulPos, sizeof(ulSize));
		ulSize = le32_to_cpu(ulSize);
		ulPos += 4;

		if (ulSize <= 16 || ulPos + ulSize + 4 > lpsSyncState->cb)
			return MAPI_E_CORRUPT_DATA;

		strSourceKey.assign((char*)(lpsSyncState->lpb + ulPos), ulSize);
		ulPos += ulSize;
		memcpy(&ulSize, lpsSyncState->lpb + ulPos, sizeof(ulSize));
		ulSize = le32_to_cpu(ulSize);
		ulPos += 4;

		if (ulSize < 8 || ulPos + ulSize > lpsSyncState->cb)
			return MAPI_E_CORRUPT_DATA;

		ZLOG_DEBUG(m_lpLogger, "  Stream %u: size=%u, sourcekey=%s",
			ulStatusNumber, ulSize, bin2hex(strSourceKey).c_str());
		object_ptr<IStream> lpStream;
		auto hr = CreateStreamOnHGlobal(GlobalAlloc(GPTR, ulSize), true, &~lpStream);
		if (hr != hrSuccess)
			return hr;
		hr = lpStream->Write(lpsSyncState->lpb + ulPos, ulSize, &ulSize);
		if (hr != hrSuccess)
			return hr;
		m_mapSyncStatus[std::move(strSourceKey)].reset(lpStream);
		ulPos += ulSize;
	}
	return hrSuccess;
}

HRESULT ECSyncContext::HrSaveSyncStatus(LPSPropValue *lppSyncStatusProp)
{
	ULONG ulVersion = EC_SYNC_STATUS_VERSION;
	LARGE_INTEGER liPos = {{0, 0}};
	STATSTG sStat;
	memory_ptr<SPropValue> lpSyncStatusProp;

	assert(lppSyncStatusProp != NULL);
	uint32_t tmp4 = cpu_to_le32(ulVersion);
	std::string strSyncStatus{reinterpret_cast<const char *>(&tmp4), 4};
	ULONG ulSize = m_mapSyncStatus.size();
	tmp4 = cpu_to_le32(ulSize);
	strSyncStatus.append(reinterpret_cast<const char *>(&tmp4), 4);

	ZLOG_DEBUG(m_lpLogger, "Saving sync status stream: items=%u", ulSize);

	for (const auto &ssp : m_mapSyncStatus) {
		std::unique_ptr<char[]> lpszStream;

		ulSize = ssp.first.size();
		tmp4 = cpu_to_le32(ulSize);
		strSyncStatus.append(reinterpret_cast<const char *>(&tmp4), 4);
		strSyncStatus.append(ssp.first);

		auto hr = ssp.second->Stat(&sStat, STATFLAG_NONAME);
		if (hr != hrSuccess)
			return hr;
		ulSize = sStat.cbSize.LowPart;
		tmp4 = cpu_to_le32(ulSize);
		strSyncStatus.append(reinterpret_cast<const char *>(&tmp4), 4);
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

	auto hr = MAPIAllocateBuffer(sizeof *lpSyncStatusProp, &~lpSyncStatusProp);
	if (hr != hrSuccess)
		return hr;
	memset(lpSyncStatusProp, 0, sizeof *lpSyncStatusProp);

	lpSyncStatusProp->Value.bin.cb = strSyncStatus.size();
	hr = KAllocCopy(strSyncStatus.data(), strSyncStatus.size(), reinterpret_cast<void **>(&lpSyncStatusProp->Value.bin.lpb), lpSyncStatusProp);
	if (hr != hrSuccess)
		return hr;
	*lppSyncStatusProp = lpSyncStatusProp.release();
	return hrSuccess;
}

HRESULT ECSyncContext::HrGetSyncStatusStream(LPMAPIFOLDER lpFolder, LPSTREAM *lppStream)
{
	memory_ptr<SPropValue> lpPropVal;
	auto hr = HrGetOneProp(lpFolder, PR_SOURCE_KEY, &~lpPropVal);
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
		m_mapSyncStatus[std::move(strSourceKey)].reset(lpStream);
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
	std::lock_guard<std::mutex> lock(m_hMutex);

	for (unsigned i = 0; i < lpEntryList->cValues; ++i) {
		if (lpEntryList->lpbin[i].cb < 8) {
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "change notification: [Invalid]");
			continue;
		}
		ULONG ulSyncId = SYNCID(lpEntryList->lpbin[i].lpb);
		ULONG ulChangeId = CHANGEID(lpEntryList->lpbin[i].lpb);
		m_mapNotifiedSyncIds[ulSyncId] = ulChangeId;

		m_lpLogger->logf(EC_LOGLEVEL_INFO, "change notification: syncid=%u, changeid=%u", ulSyncId, ulChangeId);
	}
	return 0;
}
