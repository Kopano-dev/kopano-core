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
#ifdef HAVE_OFFLINE_SUPPORT
#include <kopano/ECGuid.h>
#include "ECSync.h"
#include "ECSyncUtil.h"
#include <kopano/ECDebug.h>
#include "../provider/include/kcore.hpp"
#include <algorithm>

#ifdef WIN32
#include <eventsys.h>
#include <Sensevts.h>
#include <sensapi.h>
#endif

#include <mapiutil.h>
#include <kopano/mapiguidext.h>
#include <edkguid.h>
#include <kopano/Util.h>
#include <kopano/CommonUtil.h>
#include <kopano/mapiext.h>
#include <kopano/stringutil.h>
#include <kopano/ECABEntryID.h>
#include "ECSyncLog.h"
#include <kopano/ECLogger.h>
#include <kopano/mapi_ptr.h>
#include <kopano/threadutil.h>

#include "IECExportAddressbookChanges.h"
#include "IECExportChanges.h"
#include "IECImportContentsChanges.h"
#include "ECOfflineABImporter.h"

#include <kopano/IECServiceAdmin.h>
#include "IECChangeAdvisor.h"
#include "ECResyncSet.h"

#include "ECSensNetwork.h"
#include <kopano/ECGetText.h>
#include <kopano/charset/convert.h>
#include "MAPINotifSink.h"

typedef mapi_object_ptr<IExchangeImportContentsChanges, IID_IExchangeImportContentsChanges> ExchangeImportContentsChangesPtr;
typedef mapi_object_ptr<ISensNetwork, IID_ISensNetwork> SensNetworkPtr;
typedef mapi_object_ptr<IEventSystem, IID_IEventSystem> EventSystemPtr;
typedef mapi_object_ptr<IEventSubscription, IID_IEventSubscription> EventSubscriptionPtr;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static const char THIS_FILE[] = __FILE__;
#endif

HRESULT CreateECSync(LPMSPROVIDER lpOfflineProvider, LPMSPROVIDER lpOnlineProvider, LPMAPISUP lpSupport, ULONG cbStoreID, LPENTRYID lpStoreID, IECSync **lppSync)
{
	HRESULT hr = hrSuccess;
	ECSync *lpECSync = NULL;

	hr = ECSync::Create(lpOfflineProvider, lpOnlineProvider, lpSupport, cbStoreID, lpStoreID, &lpECSync);
	if (hr != hrSuccess)
		goto exit;

	hr = lpECSync->QueryInterface(IID_IECSync, (void**)lppSync);

exit:
	if (lpECSync)
		lpECSync->Release();

	return hr;
}

HRESULT CreateECSync(LPMAPISESSION lpSession, IECSync **lppSync)
{
	HRESULT hr = hrSuccess;
	ECSync *lpECSync = NULL;

	hr = ECSync::Create(lpSession, &lpECSync);
	if (hr != hrSuccess)
		goto exit;

	hr = lpECSync->QueryInterface(IID_IECSync, (void**)lppSync);

exit:
	if (lpECSync)
		lpECSync->Release();

	return hr;
}

void ECSync::init(){
	BYTE szDir[MAX_PATH];
	std::string strPath;
	HKEY hKey = NULL;
	ULONG cbDir = 0;
	convert_context	converter;
	std::wstring strManufacturer = _T("Kopano");
	DWORD cbData = 0;
	DWORD dwData = 0;
	DWORD dwType = REG_DWORD;
	ULONG lRegRet = 0;

	ECSyncLog::GetLogger(&m_lpLogger);

	m_lpOfflineContext = NULL;
	m_lpOnlineContext = NULL;

	m_hrSyncError = hrSuccess;
	m_lpOfflineProvider = NULL;
	m_lpOnlineProvider = NULL;
	m_lpSupport = NULL;
	m_lpStoreID = NULL;
	m_cbStoreID = 0;
	m_localeOLK = 0;

	m_lpSession = NULL;

	m_lpOfflineSession = NULL;
	m_lpOnlineSession = NULL;

	m_lpSyncStatusCallBack = NULL;
	m_lpSyncStatusCallBackObject = NULL;
	m_lpSyncProgressCallBack = NULL;
	m_lpSyncProgressCallBackObject = NULL;
	m_lpSyncPopupErrorCallBack = NULL;
	m_lpSyncPopupErrorCallBackObject = NULL;

	m_ulOnlineAdviseConnection = 0;

	pthread_mutex_init(&m_hMutex, NULL);

	pthread_cond_init(&m_hExitSignal, NULL);

	m_ulWaitTime = 60*15; //default wait time between syncs (15 minutes)
	m_ulSyncOnNotify = fnevNewMail; //default sync also on newmail (0, fnevNewMail, fnevNewMail | fnevObjectCreated | fnevObjectDeleted | fnevObjectModified | fnevObjectMoved | fnevObjectCopied)
	m_ulSyncFlags = SYNC_NORMAL | SYNC_ASSOCIATED | SYNC_READ_STATE | SYNC_BEST_BODY;
	m_ulStatus = 0;
	m_bExit = false;
	m_bCancel = false;
	m_eResyncReason = ResyncReasonNone;
	m_bSupressErrorPopup = false;

	// Load global settings from registry
	if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("Software\\Kopano\\Client"), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		cbDir = MAX_PATH;
		if(RegQueryValueEx(hKey, _T("Manufacturer"), NULL, NULL, szDir, &cbDir) == ERROR_SUCCESS) {
			strManufacturer = (LPTSTR)szDir;
		}
		RegCloseKey(hKey);
	}
	// Load from extension, since the ECProps shows the popup.
	if(RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Kopano\\Extension"), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		cbData = sizeof(DWORD);
		lRegRet = RegQueryValueEx(hKey, _T("DisableErrorPopup"), NULL, &dwType, (LPBYTE)&dwData, &cbData);
		if(lRegRet == ERROR_SUCCESS && dwData != 0) {
			m_bSupressErrorPopup = true;
		}
		RegCloseKey(hKey);
	}
#ifndef NO_GETTEXT
	if(SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES_COMMON, NULL, SHGFP_TYPE_CURRENT, (LPSTR)szDir))) {
		strPath = (LPSTR)szDir;

		strPath += "\\" + converter.convert_to<std::string>(strManufacturer.c_str()) + "\\locale";
		bindtextdomain("kopano", strPath.c_str());
	}
#endif
}

ECSync::ECSync(LPMAPISESSION lpSession){
	init();
	m_lpSession = lpSession;
	m_lpSession->AddRef();
}

ECSync::ECSync(const std::string &strProfileName){
	init();
	m_strProfileName = strProfileName;
}

ECSync::ECSync(LPMSPROVIDER lpOfflineProvider, LPMSPROVIDER lpOnlineProvider, LPMAPISUP lpSupport, ULONG cbStoreID, LPENTRYID lpStoreID){
	init();
	m_lpOfflineProvider = lpOfflineProvider;
	m_lpOnlineProvider = lpOnlineProvider;
	m_lpSupport = lpSupport;

	m_cbStoreID = cbStoreID;

	MAPIAllocateBuffer(cbStoreID, (LPVOID*)&m_lpStoreID);
	memcpy(m_lpStoreID, lpStoreID, cbStoreID);

	m_lpOfflineProvider->AddRef();
	m_lpOnlineProvider->AddRef();
	m_lpSupport->AddRef();
}

ECSync::ECSync(const std::string &strOfflineProfile, const std::string &strOnlineProfile){
	init();
	m_strOfflineProfile = strOfflineProfile;
	m_strOnlineProfile = strOnlineProfile;
}

ECSync::~ECSync(){
	std::map<std::string, LPSTREAM>::const_iterator iSyncStatus;
	std::list<LPNOTIFICATION>::const_iterator iterNotif;
	LPMDB lpOnlineStore = NULL;

	StopSync();
	for (iterNotif = m_lstNewMailNotifs.begin();
	     iterNotif != m_lstNewMailNotifs.end(); ++iterNotif)
		MAPIFreeBuffer(*iterNotif);

	if (m_ulOnlineAdviseConnection && m_lpOnlineContext && m_lpOnlineContext->HrGetMsgStore(&lpOnlineStore) == hrSuccess) {
		lpOnlineStore->Unadvise(m_ulOnlineAdviseConnection);
		lpOnlineStore->Release();
	}

	if(m_lpOfflineSession)
		m_lpOfflineSession->Release();

	if(m_lpOnlineSession)
		m_lpOnlineSession->Release();

	if(m_lpOfflineProvider)
		m_lpOfflineProvider->Release();

	if(m_lpOnlineProvider)
		m_lpOnlineProvider->Release();

	if(m_lpSession)
		m_lpSession->Release();

	if(m_lpSupport)
		m_lpSupport->Release();
	MAPIFreeBuffer(m_lpStoreID);
	delete m_lpOfflineContext;
	delete m_lpOnlineContext;

	if (m_lpLogger)
		m_lpLogger->Release();

	pthread_cond_destroy(&m_hExitSignal);
	pthread_mutex_destroy(&m_hMutex);
}

HRESULT ECSync::Create(LPMAPISESSION lpSession, ECSync **lppECSync){
	*lppECSync = new ECSync(lpSession);
	(*lppECSync)->AddRef();

	return hrSuccess;
}

HRESULT ECSync::Create(const std::string &strProfileName, ECSync **lppECSync){

	*lppECSync = new ECSync(strProfileName);
	(*lppECSync)->AddRef();

	return hrSuccess;
}

HRESULT ECSync::Create(LPMSPROVIDER lpOfflineProvider, LPMSPROVIDER lpOnlineProvider, LPMAPISUP lpSupport, ULONG cbStoreID, LPENTRYID lpStoreID, ECSync **lppECSync){

	if(lpOfflineProvider == NULL || lpOnlineProvider == NULL || lpSupport == NULL)
		return MAPI_E_INVALID_PARAMETER;

	*lppECSync = new ECSync(lpOfflineProvider, lpOnlineProvider, lpSupport, cbStoreID, lpStoreID);
	(*lppECSync)->AddRef();

	return hrSuccess;
}

HRESULT ECSync::Create(const std::string &strOfflineProfile, const std::string &strOnlineProfile, ECSync **lppECSync){
	if(strOfflineProfile.empty() || strOnlineProfile.empty())
		return MAPI_E_INVALID_PARAMETER;

	*lppECSync = new ECSync(strOfflineProfile, strOnlineProfile);
	(*lppECSync)->AddRef();

	return hrSuccess;
}

HRESULT	ECSync::QueryInterface(REFIID refiid, void ** lppInterface)
{
	REGISTER_INTERFACE(IID_IECSync, &m_xECSync);

	return ECUnknown::QueryInterface(refiid, lppInterface);
}

//sync all folders and all associated messages
HRESULT ECSync::FirstFolderSync(){
	HRESULT hr = hrSuccess;
	ULONG ulStatus;
	std::list<SBinary> lstChanges;

	clock_t		clkStart = 0;
	clock_t		clkEnd = 0;
	struct z_tms	tmsStart = {0};
	struct z_tms	tmsEnd = {0};
	double		dblDuration = 0;
	char		szDuration[64] = {0};
	GUID		serverUid;

	WITH_LOCK(m_hMutex) {
		if(m_ulStatus & (EC_SYNC_STATUS_SYNCING | EC_SYNC_STATUS_RUNNING)){
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}
		m_ulStatus |= EC_SYNC_STATUS_SYNCING;
		ulStatus = m_ulStatus;
	}

	clkStart = z_times(&tmsStart);

	if(m_lpSyncStatusCallBack)
		m_lpSyncStatusCallBack(m_lpSyncStatusCallBackObject, ulStatus);

	if(m_lpSyncProgressCallBack)
		m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, 0, 0, 0.0, L"");

	WITH_LOCK(m_hMutex) {
		m_bExit = false;
		m_bCancel = false;
	}

	hr = Logon();
	if(hr != hrSuccess)
		goto exit;

	hr = LoadSyncStatusStreams();
	if(hr != hrSuccess)
		goto exit;

	hr = SyncStoreInfo();
	if(hr != hrSuccess)
		goto exit;
	if(m_lpSyncProgressCallBack)
		m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, 0, 0, 0.10, L"");

	hr = HrSyncReceiveFolders(m_lpOnlineContext, m_lpOfflineContext);
	if(hr != hrSuccess)
		goto exit;

	if(m_lpSyncProgressCallBack)
		m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, 0, 0, 0.15, L"");

	hr = HrCreateRemindersFolder();
	if(hr != hrSuccess)
		goto exit;

	if(m_lpSyncProgressCallBack)
		m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, 0, 0, 0.20, L"");

	//for the first sync we only want to sync all associated messages
	m_ulSyncFlags = SYNC_ASSOCIATED | SYNC_BEST_BODY;

	hr = CreateChangeList(&lstChanges, &m_ulTotalSteps);
	if(hr != hrSuccess)
		goto exit;

	hr = MessageSync(lstChanges);
	if (hr != hrSuccess)
		goto exit;

	hr = SaveSyncStatusStreams();
	if(hr != hrSuccess)
		goto exit;

	hr = m_lpOnlineContext->GetServerUid(&serverUid);
	if (hr != hrSuccess)
		goto exit;

	hr = m_lpOfflineContext->SetStoredServerUid(&serverUid);
	if (hr != hrSuccess)
		goto exit;

exit:
	// Calculate diff
	clkEnd = z_times(&tmsEnd);
	dblDuration = (double)(clkEnd - clkStart) / TICKS_PER_SEC;
	if (dblDuration >= 60)
		_snprintf(szDuration, sizeof(szDuration), "%u:%02u.%03u min.", (unsigned)(dblDuration / 60), (unsigned)dblDuration % 60, (unsigned)(dblDuration * 1000 + .5) % 1000);
	else
		_snprintf(szDuration, sizeof(szDuration), "%u.%03u s.", (unsigned)dblDuration % 60, (unsigned)(dblDuration * 1000 + .5) % 1000);

	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Performed initial synchronization in %s", szDuration);

	ReleaseChangeList(lstChanges);

	UpdateSyncStatus(0, EC_SYNC_STATUS_SYNCING);
	return hr;
}

HRESULT ECSync::HrSetupChangeAdvisors()
{
	HRESULT					hr = hrSuccess;
	IECChangeAdvisor *lpOnlineAdvisor = NULL;
	IECChangeAdviseSink *lpOnlineAdviseSink = NULL;
	LPSTREAM				lpOnlineStream = NULL;
	IECChangeAdvisor *lpOfflineAdvisor = NULL;
	LPSTREAM				lpOfflineStream = NULL;
	IECChangeAdviseSink *lpOfflineAdviseSink = NULL;


	// Open the advisors so we know whether they are supported.
	hr = m_lpOnlineContext->HrGetChangeAdvisor(&lpOnlineAdvisor);
	if (hr == MAPI_E_NO_SUPPORT)
		hr = hrSuccess;
	if (hr != hrSuccess)
		goto exit;


	hr = m_lpOfflineContext->HrGetChangeAdvisor(&lpOfflineAdvisor);
	if (hr == MAPI_E_NO_SUPPORT)
		hr = hrSuccess;
	if (hr != hrSuccess)
		goto exit;


	// Nothing to do?
	if (lpOnlineAdvisor == NULL && lpOfflineAdvisor == NULL)
		goto exit;


	hr = HrLoadChangeNotificationStatusStreams(lpOnlineAdvisor ? &lpOnlineStream : NULL, lpOfflineAdvisor ? &lpOfflineStream : NULL, true);
	if (hr != hrSuccess)
		goto exit;


	if (lpOnlineAdvisor) {
		hr = m_lpOnlineContext->HrGetChangeAdviseSink(&lpOnlineAdviseSink);
		if (hr != hrSuccess)
			goto exit;

		hr = lpOnlineAdvisor->Config(lpOnlineStream, NULL, lpOnlineAdviseSink, 0);
		if (hr != hrSuccess)
			goto exit;
	}

	if (lpOfflineAdvisor) {
		hr = m_lpOfflineContext->HrGetChangeAdviseSink(&lpOfflineAdviseSink);
		if (hr != hrSuccess)
			goto exit;

		hr = lpOfflineAdvisor->Config(lpOfflineStream, NULL, lpOfflineAdviseSink, 0);
		if (hr != hrSuccess)
			goto exit;
	}


exit:
	if (lpOfflineAdviseSink)
		lpOfflineAdviseSink->Release();

	if (lpOnlineAdviseSink)
		lpOnlineAdviseSink->Release();

	if (lpOfflineStream)
		lpOfflineStream->Release();

	if (lpOnlineStream)
		lpOnlineStream->Release();

	if (lpOfflineAdvisor)
		lpOfflineAdvisor->Release();

	if (lpOnlineAdvisor)
		lpOnlineAdvisor->Release();

	return hr;
}

HRESULT ECSync::HrLoadChangeNotificationStatusStreams(LPSTREAM *lppOnlineStream, LPSTREAM *lppOfflineStream, bool bAllowCreate)
{
	HRESULT		hr = hrSuccess;
	LPMDB		lpOnlineStore = NULL;
	LPMDB		lpOfflineStore = NULL;
	LPSTREAM	lpOnlineStream = NULL;
	LPSTREAM	lpOfflineStream = NULL;

	hr = m_lpOnlineContext->HrGetMsgStore(&lpOnlineStore);
	if (hr != hrSuccess)
		goto exit;

	hr = m_lpOfflineContext->HrGetMsgStore(&lpOfflineStore);
	if (hr != hrSuccess)
		goto exit;

	ZLOG_DEBUG(m_lpLogger, "Loading change notification status streams");

	if (lppOnlineStream)
		hr = lpOfflineStore->OpenProperty(PR_EC_CHANGE_ONL_STATE, &IID_IStream, STGM_TRANSACTED, 0, (LPUNKNOWN*)&lpOnlineStream);
	if (hr == hrSuccess && lppOfflineStream)
		hr = lpOfflineStore->OpenProperty(PR_EC_CHANGE_OFFL_STATE, &IID_IStream, STGM_TRANSACTED, 0, (LPUNKNOWN*)&lpOfflineStream);

	// If any of the requested is not found we'll recreate all.
	if (hr == MAPI_E_NOT_FOUND) {
		if (!bAllowCreate || (hr = HrCreateChangeNotificationStatusStreams(lppOnlineStream, lppOfflineStream)) != hrSuccess)
			goto exit;
	} else {
		if (lppOnlineStream) {
			hr = lpOnlineStream->QueryInterface(IID_IStream, (void**)lppOnlineStream);
			if (hr != hrSuccess)
				goto exit;
		}

		if (lppOfflineStream) {
			hr = lpOfflineStream->QueryInterface(IID_IStream, (void**)lppOfflineStream);
			if (hr != hrSuccess)
				goto exit;
		}
	}

exit:
	if (lpOfflineStream)
		lpOfflineStream->Release();

	if (lpOnlineStream)
		lpOnlineStream->Release();

	if (lpOfflineStore)
		lpOfflineStore->Release();

	if (lpOnlineStore)
		lpOnlineStore->Release();

	return hr;
}

HRESULT ECSync::HrSaveChangeNotificationStatusStreams()
{
	ASSERT(m_lpOfflineContext != NULL);

	ZLOG_DEBUG(m_lpLogger, "Saving change notification status streams");

	if (m_lpOnlineContext)
		HrSaveChangeNotificationStatusStream(m_lpOnlineContext, PR_EC_CHANGE_ONL_STATE);

	HrSaveChangeNotificationStatusStream(m_lpOfflineContext, PR_EC_CHANGE_OFFL_STATE);

	return hrSuccess;
}

HRESULT ECSync::HrSaveChangeNotificationStatusStream(ECSyncContext *lpContext, ULONG ulStatePropTag)
{
	HRESULT					hr = hrSuccess;
	IECChangeAdvisor *lpAdvisor = NULL;
	LPMDB					lpStore = NULL;
	LPSTREAM				lpStream = NULL;

	hr = lpContext->HrGetChangeAdvisor(&lpAdvisor);
	if (hr == MAPI_E_NO_SUPPORT) {
		hr = hrSuccess;
		goto exit;
	} else if (hr != hrSuccess)
		goto exit;

	hr = m_lpOfflineContext->HrGetMsgStore(&lpStore);
	if (hr != hrSuccess)
		goto exit;

	hr = lpStore->OpenProperty(ulStatePropTag, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, (LPUNKNOWN*)&lpStream);
	if (hr != hrSuccess)
		goto exit;

	hr = lpAdvisor->UpdateState(lpStream);
	if (hr != hrSuccess)
		goto exit;

	lpStream->Commit(0);

exit:
	if (lpStream)
		lpStream->Release();

	if (lpStore)
		lpStore->Release();

	if (lpAdvisor)
		lpAdvisor->Release();

	return hr;
}

HRESULT ECSync::HrAddFolderSyncStateToEntryList(ECSyncContext *lpContext, SBinary *lpsEntryID, LPENTRYLIST lpEntryList)
{
	HRESULT		hr = hrSuccess;
	LPSTREAM	lpStream = NULL;
	SSyncState	*lpsSyncState = NULL;

	hr = lpContext->HrGetSyncStatusStream(lpsEntryID, &lpStream);
	if (FAILED(hr))
		goto exit;

	hr = MAPIAllocateMore(sizeof *lpsSyncState, lpEntryList->lpbin, (void**)&lpsSyncState);
	if (hr != hrSuccess)
		goto exit;

	hr = HrDecodeSyncStateStream(lpStream, &lpsSyncState->ulSyncId, &lpsSyncState->ulChangeId);
	if (hr != hrSuccess)
		goto exit;

	ZLOG_DEBUG(m_lpLogger, "%p: entryid=%s, syncid=%u, changeid=%u", lpContext, bin2hex(lpsEntryID->cb, lpsEntryID->lpb).c_str(), lpsSyncState->ulSyncId, lpsSyncState->ulChangeId);

	if (lpsSyncState->ulSyncId != 0) {
		lpEntryList->lpbin[lpEntryList->cValues].cb = sizeof *lpsSyncState;
		lpEntryList->lpbin[lpEntryList->cValues++].lpb = (LPBYTE)lpsSyncState;
	}

exit:
	if (lpStream)
		lpStream->Release();

	return hr;
}

HRESULT ECSync::HrEntryListToChangeNotificationStream(IECChangeAdvisor *lpChangeAdvisor,
    LPMDB lpStore, ULONG ulPropTag, LPENTRYLIST lpEntryList,
    LPSTREAM *lppStream)
{
	HRESULT		hr = hrSuccess;
	LPSTREAM	lpStream = NULL;

	ASSERT(lpChangeAdvisor != NULL);
	ASSERT(lpStore != NULL);
	ASSERT(lpEntryList != NULL);
	ASSERT(lppStream != NULL);

	hr = lpChangeAdvisor->AddKeys(lpEntryList);
	if (hr != hrSuccess)
		goto exit;

	hr = lpStore->OpenProperty(ulPropTag, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE|MAPI_MODIFY, (LPUNKNOWN*)&lpStream);
	if (hr != hrSuccess)
		goto exit;

	hr = lpChangeAdvisor->UpdateState(lpStream);
	if (hr != hrSuccess)
		goto exit;

	hr = lpStream->Commit(0);
	if (hr != hrSuccess)
		goto exit;

	hr = lpStream->QueryInterface(IID_IStream, (void**)lppStream);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (lpStream)
		lpStream->Release();

	return hr;
}

HRESULT ECSync::HrCreateChangeNotificationStatusStreams(LPSTREAM *lppOnlineStream, LPSTREAM *lppOfflineStream)
{
	HRESULT					hr = hrSuccess;
	IECChangeAdvisor *lpOnlineChangeAdvisor = NULL;
	IECChangeAdvisor *lpOfflineChangeAdvisor = NULL;
	LPSRowSet				lpRows = NULL;
	ENTRYLIST				sOnlineEntryList = {0};
	ENTRYLIST				sOfflineEntryList = {0};
	LPMDB					lpStore = NULL;

	SizedSPropTagArray(2, sptFolder) = {2, {PR_SOURCE_KEY, PR_FOLDER_TYPE}};

	if (lppOnlineStream) {
		ZLOG_DEBUG(m_lpLogger, "Creating new online change notification status stream");

		hr = m_lpOnlineContext->HrGetChangeAdvisor(&lpOnlineChangeAdvisor);
		if (hr == hrSuccess)
			hr = lpOnlineChangeAdvisor->Config(NULL, NULL, NULL, SYNC_CATCHUP);
		else if (hr == MAPI_E_NO_SUPPORT)
			hr = hrSuccess;

		if (hr != hrSuccess)
			goto exit;
	}


	if (lppOfflineStream) {
		ZLOG_DEBUG(m_lpLogger, "Creating new offline change notification status stream");

		hr = m_lpOnlineContext->HrGetChangeAdvisor(&lpOfflineChangeAdvisor);
		if (hr == hrSuccess)
			hr = lpOfflineChangeAdvisor->Config(NULL, NULL, NULL, SYNC_CATCHUP);
		else if (hr == MAPI_E_NO_SUPPORT)
			hr = hrSuccess;

		if (hr != hrSuccess)
			goto exit;
	}

	// Nothing to do?
	if (lpOnlineChangeAdvisor == NULL && lpOfflineChangeAdvisor == NULL)
		goto exit;



	hr = m_lpOfflineContext->HrQueryHierarchyTable((LPSPropTagArray)&sptFolder, &lpRows);
	if (hr != hrSuccess)
		goto exit;


	if (lpOnlineChangeAdvisor) {
		hr = MAPIAllocateBuffer(lpRows->cRows * sizeof *sOnlineEntryList.lpbin, (void**)&sOnlineEntryList.lpbin);
		if (hr != hrSuccess)
			goto exit;
	}

	if (lpOfflineChangeAdvisor) {
		hr = MAPIAllocateBuffer(lpRows->cRows * sizeof *sOfflineEntryList.lpbin, (void**)&sOfflineEntryList.lpbin);
		if (hr != hrSuccess)
			goto exit;
	}

	for (ULONG ulCount = 0; ulCount < lpRows->cRows; ++ulCount) {
		if (lpRows->aRow[ulCount].lpProps[0].ulPropTag != PR_SOURCE_KEY)
			continue;

		if (lpRows->aRow[ulCount].lpProps[1].ulPropTag == PR_FOLDER_TYPE &&  lpRows->aRow[ulCount].lpProps[1].Value.ul == FOLDER_SEARCH)
			continue;

		if (lpOnlineChangeAdvisor) {
			hr = HrAddFolderSyncStateToEntryList(m_lpOnlineContext, &lpRows->aRow[ulCount].lpProps[0].Value.bin, &sOnlineEntryList);
			if (hr != hrSuccess)
				goto exit;
		}

		if (lpOfflineChangeAdvisor) {
			hr = HrAddFolderSyncStateToEntryList(m_lpOfflineContext, &lpRows->aRow[ulCount].lpProps[0].Value.bin, &sOfflineEntryList);
			if (hr != hrSuccess)
				goto exit;
		}
	}


	hr = m_lpOfflineContext->HrGetMsgStore(&lpStore);
	if (hr != hrSuccess)
		goto exit;


	if (lpOnlineChangeAdvisor) {
		hr = HrEntryListToChangeNotificationStream(lpOnlineChangeAdvisor, lpStore, PR_EC_CHANGE_ONL_STATE, &sOnlineEntryList, lppOnlineStream);
		if (hr != hrSuccess)
			goto exit;
	}

	if (lpOfflineChangeAdvisor) {
		hr = HrEntryListToChangeNotificationStream(lpOfflineChangeAdvisor, lpStore, PR_EC_CHANGE_OFFL_STATE, &sOfflineEntryList, lppOfflineStream);
		if (hr != hrSuccess)
			goto exit;
	}


exit:
	if (lpStore)
		lpStore->Release();
	MAPIFreeBuffer(sOfflineEntryList.lpbin);
	MAPIFreeBuffer(sOnlineEntryList.lpbin);
	if (lpRows)
		FreeProws(lpRows);

	if (lpOfflineChangeAdvisor)
		lpOfflineChangeAdvisor->Release();

	if (lpOnlineChangeAdvisor)
		lpOnlineChangeAdvisor->Release();

	return hr;
}

//start sync thread if not running and cancel wait
HRESULT ECSync::StartSync(){
	HRESULT hr = hrSuccess;
	ULONG ulStatus = 0;

	//check running
	WITH_LOCK(m_hMutex) {
		if(m_ulStatus & EC_SYNC_STATUS_RUNNING){
			pthread_cond_signal(&m_hExitSignal);
			goto exit;
		}else if(m_ulStatus & EC_SYNC_STATUS_SYNCING){
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}
		m_ulStatus |= EC_SYNC_STATUS_RUNNING | EC_SYNC_STATUS_SYNCING;
		ulStatus = m_ulStatus;
	}

	if(m_lpSyncStatusCallBack)
		m_lpSyncStatusCallBack(m_lpSyncStatusCallBackObject, ulStatus);

	if(m_lpSyncProgressCallBack)
		m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, 0, 0, 0, L"");

	WITH_LOCK(m_hMutex) {
		m_bExit = false;
		m_bCancel = false;
	}

	m_ulSyncFlags = SYNC_NORMAL | SYNC_ASSOCIATED | SYNC_READ_STATE | SYNC_BEST_BODY;

	pthread_create(&m_hSyncThread, NULL, ECSync::SyncThread, (void*)this);
	set_thread_name(m_hSyncThread, "SyncThread");

exit:
	return hr;
}

HRESULT ECSync::StopSync(){
	bool bJoinThread = false;

	WITH_LOCK(m_hMutex) {
		if(m_ulStatus & EC_SYNC_STATUS_RUNNING){
			m_bExit = true;
			pthread_cond_signal(&m_hExitSignal);
			bJoinThread = true;
		}else if(m_ulStatus & EC_SYNC_STATUS_SYNCING){
			m_bExit = true;
		}
	}

	// Join the thread with m_hMutex unlocked or the thread won't stop because
	// it's waiting for m_hMutex.
	if (bJoinThread)
		pthread_join(m_hSyncThread, NULL);

	UpdateSyncStatus(0, EC_SYNC_STATUS_RUNNING);

	return hrSuccess;
}

HRESULT ECSync::CancelSync(){
	HRESULT hr = hrSuccess;
	scoped_lock lock(m_hMutex);
	m_bCancel = true;
	return hr;
}

HRESULT ECSync::SyncAB() {
	HRESULT hr = hrSuccess;

	hr = DoSyncAB();
	if (hr != hrSuccess)
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to sync addressbook. (hr=0x%08x)", hr);
	if (hr == hrSuccess || (hr != MAPI_E_NOT_FOUND && hr != MAPI_E_COLLISION))
		goto exit;

	m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Resetting addressbook.");
	hr = ResetAB();
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to reset addressbook. (hr=0x%08x)", hr);
		goto exit;
	}

	m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Resyncing addressbook.");
	hr = DoSyncAB();
	if (hr != hrSuccess)
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to resync addressbook. (hr=0x%08x)", hr);

exit:
	return hr;
}

HRESULT ECSync::DoSyncAB(){
	HRESULT hr = hrSuccess;
	HRESULT hrSync = hrSuccess;

	LPMDB lpOnlineStore = NULL;
	LPMDB lpOfflineStore = NULL;
	IECExportAddressbookChanges *lpEEAC = NULL;
	ULONG ulSteps = 0;
	ULONG ulProgress = 0;
	OfflineABImporter *lpCollector = NULL;
	IECServiceAdmin *lpOfflineServiceAdmin = NULL;
	IECServiceAdmin *lpOnlineServiceAdmin = NULL;
	LPSTREAM lpABSyncStatus = NULL;

	hr = Logon();
	if(hr != hrSuccess)
		goto exit;

	hr = m_lpOnlineContext->HrGetMsgStore(&lpOnlineStore);
	if (hr != hrSuccess)
		goto exit;

	hr = m_lpOfflineContext->HrGetMsgStore(&lpOfflineStore);
	if (hr != hrSuccess)
		goto exit;

	hr = lpOnlineStore->QueryInterface(IID_IECServiceAdmin, (void **)&lpOnlineServiceAdmin);
	if(hr != hrSuccess)
		goto exit;

	hr = lpOfflineStore->QueryInterface(IID_IECServiceAdmin, (void **)&lpOfflineServiceAdmin);
	if(hr != hrSuccess)
		goto exit;

	hr = lpOnlineStore->OpenProperty(PR_CONTENTS_SYNCHRONIZER, &IID_IECExportAddressbookChanges, 0, 0, (IUnknown **)&lpEEAC);
	if(hr != hrSuccess)
		goto exit;

	lpCollector = new OfflineABImporter(lpOfflineServiceAdmin, lpOnlineServiceAdmin);;

	// Open or create the state stream
	if(lpOfflineStore->OpenProperty(PR_EC_AB_SYNC_STATUS, &IID_IStream, STGM_TRANSACTED, MAPI_MODIFY, (LPUNKNOWN *)&lpABSyncStatus) != hrSuccess) {
		hr = lpOfflineStore->OpenProperty(PR_EC_AB_SYNC_STATUS, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, (LPUNKNOWN *)&lpABSyncStatus);
		if(hr != hrSuccess)
			goto exit;
	}

	hr = lpEEAC->Config(lpABSyncStatus, 0, lpCollector);
	if(hr != hrSuccess)
		goto exit;

	do{
		hrSync = CheckExit();
		if(hrSync != hrSuccess)
			break;

		hrSync = lpEEAC->Synchronize(&ulSteps, &ulProgress);
		if(m_lpSyncProgressCallBack && hrSync == SYNC_W_PROGRESS)
			m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, ulProgress, ulSteps, ((double)ulProgress/ulSteps) * 0.10, L"Address Book");
	}while(hrSync == SYNC_W_PROGRESS);

	hr = lpEEAC->UpdateState(lpABSyncStatus);
 	if(hr != hrSuccess)
		goto exit;

	hr = lpABSyncStatus->Commit(0);
	if(hr != hrSuccess)
		goto exit;

	// We want to return the error from the sync after saving the state
	hr = hrSync;

exit:
	if(lpABSyncStatus)
		lpABSyncStatus->Release();

	if(lpEEAC)
		lpEEAC->Release();

	if(lpOfflineServiceAdmin)
		lpOfflineServiceAdmin->Release();

	if(lpOnlineServiceAdmin)
		lpOnlineServiceAdmin->Release();

	if (lpOfflineStore)
		lpOfflineStore->Release();

	if (lpOnlineStore)
		lpOnlineStore->Release();

	delete lpCollector;
	return hr;
}

/**
 * Reset the GAB sync by resetting the state and removing all users and groups except
 * System, Everyone and the store owner.
 */
HRESULT ECSync::ResetAB()
{
	HRESULT hr = hrSuccess;
	LPMAPISESSION lpSession = NULL;
	ULONG cbOwnerId;
	EntryIdPtr ptrOwnerId;
	MsgStorePtr ptrOfflineStore;
	ECServiceAdminPtr ptrOfflineServiceAdmin;

	SizedSPropTagArray(1, sptaDelProps) = {1, {PR_EC_AB_SYNC_STATUS}};

	if (m_lpSession)
		lpSession = m_lpSession;
	else if (m_lpOfflineSession)
		lpSession = m_lpOfflineSession;
	else if (m_lpOnlineSession)
		lpSession = m_lpOnlineSession;
	else {
		hr = MAPI_E_UNCONFIGURED;
		goto exit;
	}

	hr = lpSession->QueryIdentity(&cbOwnerId, &ptrOwnerId);
	if (hr != hrSuccess)
		goto exit;

	hr = m_lpOfflineContext->HrGetMsgStore(&ptrOfflineStore);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrOfflineStore->QueryInterface(IID_IECServiceAdmin, &ptrOfflineServiceAdmin);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrOfflineServiceAdmin->RemoveAllObjects(cbOwnerId, ptrOwnerId);
	if (hr != hrSuccess && hr != MAPI_E_NOT_FOUND)
		goto exit;

	hr = ptrOfflineStore->DeleteProps((LPSPropTagArray)&sptaDelProps, NULL);
	if (hr != hrSuccess)
		goto exit;

exit:
	return hr;
}

HRESULT ECSync::SetSyncStatusCallBack(LPVOID lpSyncStatusCallBackObject, SyncStatusCallBack lpSyncStatusCallBack){
	m_lpSyncStatusCallBack = lpSyncStatusCallBack;
	m_lpSyncStatusCallBackObject = lpSyncStatusCallBackObject;
	return hrSuccess;
}

HRESULT ECSync::SetSyncProgressCallBack(LPVOID lpSyncProgressCallBackObject, SyncProgressCallBack lpSyncProgressCallBack){
	m_lpSyncProgressCallBack = lpSyncProgressCallBack;
	m_lpSyncProgressCallBackObject = lpSyncProgressCallBackObject;
	return hrSuccess;
}

HRESULT ECSync::SetSyncPopupErrorCallBack(LPVOID lpSyncPopupErrorCallBackObject, SyncPopupErrorCallBack lpSyncPopupErrorCallBack) {
	m_lpSyncPopupErrorCallBack = lpSyncPopupErrorCallBack;
	m_lpSyncPopupErrorCallBackObject = lpSyncPopupErrorCallBackObject;
	return hrSuccess;
}


HRESULT ECSync::GetSyncStatus(ULONG * lpulStatus){
	scoped_lock lock(m_hMutex);
	*lpulStatus = m_ulStatus;
	return hrSuccess;
}

HRESULT ECSync::SetWaitTime(ULONG ulWaitTime){
	m_ulWaitTime = ulWaitTime;
	return hrSuccess;
}

HRESULT ECSync::GetWaitTime(ULONG* lpulWaitTime){
	*lpulWaitTime = m_ulWaitTime;
	return hrSuccess;
}

HRESULT ECSync::SetSyncOnNotify(ULONG ulEventMask) {
	bool bResync = false;

	// We'll force a sync if ulEventMask is not a subset of m_ulSyncOnNotify.
	// But only if the syncthread is running, otherwise we're in startup code.
	if ((m_ulSyncOnNotify | ulEventMask) != m_ulSyncOnNotify) {
		scoped_lock lock(m_hMutex);
		bResync = ((m_ulStatus & EC_SYNC_STATUS_RUNNING) == EC_SYNC_STATUS_RUNNING);
	}

	m_ulSyncOnNotify = ulEventMask;

	if (bResync)
		StartSync();

	return hrSuccess;
}

HRESULT ECSync::GetSyncOnNotify(ULONG* lpulEventMask) {
	*lpulEventMask = m_ulSyncOnNotify;
	return hrSuccess;
}

HRESULT ECSync::GetSyncError(){
	return m_hrSyncError;
}

HRESULT ECSync::SetThreadLocale(LCID locale) {
	m_localeOLK = locale;
	return hrSuccess;
}

LPVOID ECSync::SyncThread(void *lpTmpECSync){
	ECSync			*lpSync = (ECSync*)lpTmpECSync;

	ASSERT(lpSync != NULL);
	return lpSync->SyncThread();
}

LPVOID ECSync::SyncThread() {
	int				lResult;
	struct timeval	now;
	struct timespec	timeout;
	HRESULT			hr = hrSuccess;
	ULONG			ulRetry = 0;
	std::list<LPNOTIFICATION>::const_iterator iterNewMail;
	LPSPropValue lpSyncTimeProp = NULL;
	LPSPropValue lpSyncOnDemand = NULL;
	std::list<SBinary> lstChanges;

	clock_t		clkStart = 0;
	clock_t		clkEnd = 0;
	struct z_tms	tmsStart = {0};
	struct z_tms	tmsEnd = {0};
	double		dblDuration = 0;
	char		szDuration[64] = {0};

#ifdef WIN32
	SensNetworkPtr			ptrSensNetwork;
	EventSystemPtr			ptrEventSystem;
	EventSubscriptionPtr	ptrEventSubscription;

	hr = ECSensNetwork::Create(&this->m_xECSync, &ptrSensNetwork);
	if (hr != hrSuccess)
		goto loop;

	hr = CoCreateInstance(CLSID_CEventSystem, 0, CLSCTX_SERVER, ptrEventSystem.iid, &ptrEventSystem);
	if(hr != hrSuccess)
		goto loop;

	hr = CoCreateInstance(CLSID_CEventSubscription, 0, CLSCTX_SERVER, ptrEventSubscription.iid, &ptrEventSubscription);
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_EventClassID(OLESTR("{D5978620-5B9F-11D1-8DD2-00AA004ABD5E}"));
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_SubscriberInterface(ptrSensNetwork);
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_MethodName(OLESTR("ConnectionMade"));
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_SubscriptionName(OLESTR("Connection Made"));
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_SubscriptionID(OLESTR("{cd1dcbd6-a14d-4823-a0d2-8473afde360f}"));
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSystem->Store(PROGID_EventSubscription, ptrEventSubscription);
	if(hr != hrSuccess)
		goto loop;

	hr = CoCreateInstance(CLSID_CEventSubscription, 0, CLSCTX_SERVER, IID_IEventSubscription, &ptrEventSubscription);
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_EventClassID(OLESTR("{D5978620-5B9F-11D1-8DD2-00AA004ABD5E}"));
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_SubscriberInterface(ptrSensNetwork);
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_MethodName(OLESTR("ConnectionLost"));
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_SubscriptionName(OLESTR("Connection Lost"));
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSubscription->put_SubscriptionID(OLESTR("{45233130-b6c3-44fb-a6af-487c47cee611}"));
	if(hr != hrSuccess)
		goto loop;
	hr = ptrEventSystem->Store(PROGID_EventSubscription, ptrEventSubscription);
	if(hr != hrSuccess)
		goto loop;

loop:
	ptrEventSubscription.reset();
	ptrSensNetwork.reset();

	if (m_localeOLK)
		::SetThreadLocale(this->m_localeOLK);
#endif

	while(true){

		clkStart = z_times(&tmsStart);
		UpdateSyncStatus(EC_SYNC_STATUS_RUNNING | EC_SYNC_STATUS_SYNCING, EC_SYNC_CONNECTION_ERROR);

		// Try max 5 times to log in (5 seconds)
		ulRetry = 5;
		while(ulRetry && (!m_lpOfflineContext || !m_lpOnlineContext || hr != hrSuccess)) {
			hr = ReLogin();
			--ulRetry;
			if(CheckExit() != hrSuccess)
				goto wait;

			if(hr != hrSuccess && ulRetry)
				Sleep(1000);
		}
		if(hr != hrSuccess)
			goto wait;

		if (!m_lpOfflineContext->SyncStatusLoaded()) {
			hr = LoadSyncStatusStreams();
			if(hr != hrSuccess)
				goto wait;
		}

		if(m_lpSyncProgressCallBack)
			m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, 0, 0, 0.0, L"");

		// See if any form of resynchronization is required
		hr = GetResyncReason(&m_eResyncReason);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Failed to get resync reason. (hr=0x%08x)", hr);
			goto wait;
		}

		if (m_eResyncReason == ResyncReasonRelocated) {
			hr = m_lpOnlineContext->HrClearSyncStatus();
			if (hr != hrSuccess) {
				m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Failed to clear sync status. (hr=0x%08x)", hr);
				goto wait;
			}

			hr = m_lpOnlineContext->HrResetChangeAdvisor();
			if (hr == MAPI_E_NO_SUPPORT)
				hr = hrSuccess;
			else if (hr != hrSuccess) {
				m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Failed to reset change advisor. (hr=0x%08x)", hr);
				goto wait;
			}
		}

		if (m_eResyncReason != ResyncReasonNone) {
			hr = ResetAB();
			if (hr != hrSuccess)
				goto wait;
		}

		hr = SyncAB();
		if (hr != hrSuccess)
			m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Failed to sync and resync addressbook. (hr=0x%08x)", hr);

		hr = CheckExit();
		if (hr!= hrSuccess)
			goto wait;

		if(m_lpSyncProgressCallBack)
			m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, 0, 0, 0.10, L"");

		hr = FolderSync();
		if(hr != hrSuccess)
			goto wait;

		hr = CheckExit();
		if(hr!= hrSuccess)
			goto wait;

		if(m_lpSyncProgressCallBack)
			m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, 0, 0, 0.20, L"");

		hr = CreateChangeList(&lstChanges, &m_ulTotalSteps);
		if(hr != hrSuccess)
			goto wait;

		if (m_eResyncReason != ResyncReasonNone || m_ulTotalSteps > 0) {
			ULONG ulResyncID = 0;

			hr = MessageSync(lstChanges);
			if(hr == MAPI_E_END_OF_SESSION){
				hr = ReLogin();
				if(hr != hrSuccess)
					goto wait;
				hr = MessageSync(lstChanges);
			}
			if(hr == MAPI_E_NETWORK_ERROR || hr == MAPI_E_END_OF_SESSION)
				goto wait;

			// Update the offline resync id. Do we only want to do this on success?
			if (m_lpOnlineContext->GetResyncID(&ulResyncID) == hrSuccess)
				m_lpOfflineContext->SetResyncID(ulResyncID);

			if (m_eResyncReason == ResyncReasonRelocated) {
				GUID serverUid;
				if (m_lpOnlineContext->GetServerUid(&serverUid) == hrSuccess)
					m_lpOfflineContext->SetStoredServerUid(&serverUid);
			}
		}

wait:
		// Although the sync may have not succeeded, send the new mail notifications anyway. This may cause you
		// to get new mail notifications for e-mails that you haven't had yet. But on the other hand, it makes sure
		// that you don't get new mail notifications for e-mails that you received quite some time ago.
		for (iterNewMail = m_lstNewMailNotifs.begin();
		     iterNewMail != m_lstNewMailNotifs.end(); ++iterNewMail) {
			m_lpOfflineContext->HrNotifyNewMail(*iterNewMail);
			MAPIFreeBuffer(*iterNewMail);
		}
		m_lstNewMailNotifs.clear();

		// Treat cancel as OK
		if(hr == MAPI_E_USER_CANCEL || hr == MAPI_E_CANCEL) {
			hr = hrSuccess;
		}

		// Signal 100%
		if(m_lpSyncProgressCallBack)
			m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, 0, 0, 1.00, L"");

        // Save status streams if they were loaded
		if (m_lpOfflineContext && m_lpOfflineContext->SyncStatusLoaded())
   			SaveSyncStatusStreams();
		// Ignore error, not much we can do ... besides, we want the error from the above
    	// sync, not from SaveSyncStatusStreams()

		if(hr != hrSuccess){
			m_hrSyncError = hr;
			UpdateSyncStatus(EC_SYNC_CONNECTION_ERROR, EC_SYNC_STATUS_SYNCING);
		}else{
			UpdateSyncStatus(0, EC_SYNC_CONNECTION_ERROR | EC_SYNC_STATUS_SYNCING);
		}

		// Calculate diff
		clkEnd = z_times(&tmsEnd);
		dblDuration = (double)(clkEnd - clkStart) / TICKS_PER_SEC;
		if (dblDuration >= 60)
			_snprintf(szDuration, sizeof(szDuration), "%u:%02u.%03u min.", (unsigned)(dblDuration / 60), (unsigned)dblDuration % 60, (unsigned)(dblDuration * 1000 + .5) % 1000);
		else
			_snprintf(szDuration, sizeof(szDuration), "%u.%03u s.", (unsigned)dblDuration % 60, (unsigned)(dblDuration * 1000 + .5) % 1000);

		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Performed full synchronization in %s", szDuration);

		if(m_bExit)
			goto exit;

		// Just before we go to sleep, we'll readvise fo notifications in case the user has changed the setting.
		if (m_lpOnlineContext) {
			MsgStorePtr ptrStore;
			if (m_lpOnlineContext->HrGetMsgStore(&ptrStore) == hrSuccess) {
				HRESULT hrTmp;
				const int ulOldAdviseConnection = m_ulOnlineAdviseConnection;

				hrTmp = ptrStore->Advise(0, NULL, fnevNewMail | m_ulSyncOnNotify, &this->m_xMAPIAdviseSink, &this->m_ulOnlineAdviseConnection);
				if (hrTmp == hrSuccess)
					ptrStore->Unadvise(ulOldAdviseConnection);
			}
		}

		gettimeofday(&now,NULL); // null==timezone
		timeout.tv_sec = now.tv_sec + m_ulWaitTime;
		timeout.tv_nsec = now.tv_usec * 1000;

		WITH_LOCK(m_hMutex) {
			lResult = pthread_cond_timedwait(&m_hExitSignal, &m_hMutex, &timeout);

			if (m_bExit)
				goto exit;

			m_bCancel = false;
		}

    	ReleaseChangeList(lstChanges);
	}

exit:
	MAPIFreeBuffer(lpSyncTimeProp);
	MAPIFreeBuffer(lpSyncOnDemand);
	ReleaseChangeList(lstChanges);

	if (m_lpOfflineContext && m_lpOfflineContext->SyncStatusLoaded())
   		SaveSyncStatusStreams();

	UpdateSyncStatus(0, EC_SYNC_STATUS_RUNNING | EC_SYNC_STATUS_SYNCING);

	Logoff();

	return NULL;
}

HRESULT ECSync::Logon(){
	HRESULT hr = hrSuccess;
	LPMDB	lpOfflineStore = NULL;
	LPMDB	lpOnlineStore = NULL;
	SPropValuePtr ptrStoreName;

	UpdateSyncStatus(EC_SYNC_STATUS_LOGGING_IN, 0);

	if(!m_lpOfflineContext || FAILED(m_lpOfflineContext->HrGetMsgStore(&lpOfflineStore))){
		if(!m_strProfileName.empty()){
			if(m_lpSession == NULL){
				hr = MAPILogonEx(0, (LPTSTR)m_strProfileName.c_str(), (LPTSTR)"", MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL, &m_lpSession);
				if(hr != hrSuccess)
					goto exit;
			}
			hr = HrOpenDefaultStoreOffline(m_lpSession, &lpOfflineStore);
		}else if(!m_strOfflineProfile.empty()){
			if(m_lpOfflineSession == NULL){
				hr = MAPILogonEx(0, (LPTSTR)m_strOfflineProfile.c_str(), (LPTSTR)"", MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL, &m_lpOfflineSession);
				if(hr != hrSuccess)
					goto exit;
			}
			hr = HrOpenDefaultStore(m_lpOfflineSession, &lpOfflineStore);
		}else if(m_lpOfflineProvider != NULL){
			hr = m_lpOfflineProvider->Logon(m_lpSupport, 0, NULL, m_cbStoreID, m_lpStoreID, MDB_WRITE | MDB_NO_DIALOG, NULL, NULL, NULL, NULL, NULL, &lpOfflineStore);
		}else if(m_lpSession != NULL){
			hr = HrOpenDefaultStoreOffline(m_lpSession, &lpOfflineStore);
		}
		if(hr != hrSuccess)
			goto exit;

		delete m_lpOfflineContext;
		m_lpOfflineContext = new ECSyncContext(lpOfflineStore, m_lpLogger);
	}

	{
		// This is a bit of a hack; apparently Outlook 2007 will crash when opening items and you have some kind of plugin installed
		// like NOD32 or GPGol and you don't have the following named property registered:
		// 00020386-0000-0000-c000-0000000000000046:content-type
		// 00020386-0000-0000-c000-0000000000000046:Content-Class
		// 00020386-0000-0000-c000-0000000000000046:content-class
		// To avoid that problem, we register it now. This ensures you have the named property registered, even when you upgrade from
		// an old kopano client to a new one.

		MAPINAMEID sHardcodedNames[3];
		LPMAPINAMEID lpHardcodedNames[3] = {&sHardcodedNames[0], &sHardcodedNames[1], &sHardcodedNames[2]};
		LPSPropTagArray lpPropTags = NULL;

		sHardcodedNames[0].lpguid = (LPGUID)&PS_INTERNET_HEADERS;
		sHardcodedNames[0].ulKind = MNID_STRING;
		sHardcodedNames[0].Kind.lpwstrName = L"content-type";

		sHardcodedNames[1].lpguid = (LPGUID)&PS_INTERNET_HEADERS;
		sHardcodedNames[1].ulKind = MNID_STRING;
		sHardcodedNames[1].Kind.lpwstrName = L"Content-Class";

		sHardcodedNames[2].lpguid = (LPGUID)&PS_INTERNET_HEADERS;
		sHardcodedNames[2].ulKind = MNID_STRING;
		sHardcodedNames[2].Kind.lpwstrName = L"content-class";

		ASSERT(lpOfflineStore != NULL);
		if(lpOfflineStore->GetIDsFromNames(3, lpHardcodedNames, MAPI_CREATE, &lpPropTags) == hrSuccess)
			MAPIFreeBuffer(lpPropTags); // discard output
	}

	hr = CheckExit();
	if(hr!= hrSuccess)
		goto exit;

	if(!m_lpOnlineContext){
		if(!m_strProfileName.empty()){
			if(m_lpSession == NULL){
				hr = MAPILogonEx(0, (LPTSTR)m_strProfileName.c_str(), (LPTSTR)"", MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL, &m_lpSession);
				if(hr != hrSuccess)
					goto exit;
			}
			hr = HrOpenDefaultStoreOnline(m_lpSession, &lpOnlineStore);
		}else if(!m_strOnlineProfile.empty()){
			if(m_lpOnlineSession == NULL){
				hr = MAPILogonEx(0, (LPTSTR)m_strOnlineProfile.c_str(), (LPTSTR)"", MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL, &m_lpOnlineSession);
				if(hr != hrSuccess)
					goto exit;
			}
			hr = HrOpenDefaultStore(m_lpOnlineSession, &lpOnlineStore);
		}else if(m_lpOnlineProvider != NULL){
			hr = m_lpOnlineProvider->Logon(m_lpSupport, 0, NULL, m_cbStoreID, m_lpStoreID, MDB_WRITE | MDB_NO_DIALOG, NULL, NULL, NULL, NULL, NULL, &lpOnlineStore);
		}else if(m_lpSession != NULL){
			hr = HrOpenDefaultStoreOnline(m_lpSession, &lpOnlineStore);
		}

		if(hr != hrSuccess)
			goto exit;

		// When the online store was relocated, any call to loadObject will result in MAPI_E_UNCONFIGURED.
		// Since HrOpenDefaultStoreOnline on an offline store does not load the actual store from the online
		// server, this error is never seen.
		// So here we'll get one arbitrary property and see if we get the error. If so, we'll need to find the
		// correct server, update the profile and login again.
		hr = HrGetOneProp(lpOnlineStore, PR_DISPLAY_NAME, &ptrStoreName);
		if (hr == MAPI_E_UNCONFIGURED) {
			hr = hrSuccess;
			hr = UpdateProfileAndOpenOnlineStore(m_lpSession, &lpOnlineStore);
			if (hr != hrSuccess)
				goto exit;
		}

		delete m_lpOnlineContext;
		m_lpOnlineContext = new ECSyncContext(lpOnlineStore, m_lpLogger);

		hr = lpOnlineStore->Advise(0, NULL, fnevNewMail | m_ulSyncOnNotify, &this->m_xMAPIAdviseSink, &this->m_ulOnlineAdviseConnection);
		if(hr != hrSuccess)
			goto exit;
	}
	if(hr != hrSuccess)
		goto exit;

exit:
	if (lpOfflineStore)
		lpOfflineStore->Release();

	if (lpOnlineStore)
		lpOnlineStore->Release();

	UpdateSyncStatus(0, EC_SYNC_STATUS_LOGGING_IN);
	return hr;
}


HRESULT ECSync::FolderSync(){
	HRESULT hr = hrSuccess;

	if(!m_lpOfflineContext || !m_lpOnlineContext){
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	UpdateSyncStatus(EC_SYNC_OFFLINE_STORE, EC_SYNC_ONLINE_STORE);

	hr = FolderSyncOneWay(m_lpOfflineContext, m_lpOnlineContext, 0.10);
	if(hr != hrSuccess)
		goto exit;

	hr = CheckExit();
	if(hr!= hrSuccess)
		goto exit;

	UpdateSyncStatus(EC_SYNC_ONLINE_STORE, EC_SYNC_OFFLINE_STORE);

	hr = FolderSyncOneWay(m_lpOnlineContext, m_lpOfflineContext, 0.15);
	if(hr != hrSuccess)
		goto exit;

exit:
	UpdateSyncStatus(0, EC_SYNC_STATUS_FOLDER | EC_SYNC_OFFLINE_STORE | EC_SYNC_ONLINE_STORE);

	return hr;
}

HRESULT ECSync::FolderSyncOneWay(ECSyncContext *lpFromContext, ECSyncContext *lpToContext, double dProgressStart){
	HRESULT hr = hrSuccess;
	HRESULT hrSync = hrSuccess;
	LPMAPIFOLDER lpFromFolder = NULL;
	LPMAPIFOLDER lpToFolder = NULL;
	LPSTREAM lpFromStream = NULL;
	LPSTREAM lpToStream = NULL;
	IExchangeExportChanges* lpEHChanges = NULL;
	IExchangeImportHierarchyChanges* lpIHChanges = NULL;
	ULONG ulStep = 0;
	ULONG ulSteps = 0;
	SizedSPropTagArray(17, sptEntryID) = { 17, {
		PR_ENTRYID,
		PR_CONTAINER_CLASS,
		PR_DISPLAY_NAME,
		PR_COMMENT,
		PR_IPM_APPOINTMENT_ENTRYID,
		PR_IPM_CONTACT_ENTRYID,
		PR_IPM_DRAFTS_ENTRYID,
		PR_IPM_JOURNAL_ENTRYID,
		PR_IPM_NOTE_ENTRYID,
		PR_IPM_TASK_ENTRYID,
		PR_REM_ONLINE_ENTRYID,
		PR_ADDITIONAL_REN_ENTRYIDS,
		PR_FREEBUSY_ENTRYIDS,
		PR_RULES_DATA,
		PR_FOLDER_XVIEWINFO_E,
		PR_FOLDER_DISPLAY_FLAGS,
		PR_IPM_OL2007_ENTRYIDS, // Property used by Outlook 2007 to store the entryid of RSS feeds and two searchfolders
	} };

	hr = lpFromContext->HrOpenRootFolder(&lpFromFolder);
	if (FAILED(hr))
		goto exit;

	hr = lpFromContext->HrGetSyncStatusStream(lpFromFolder, &lpFromStream);
	if (FAILED(hr))
		goto exit;

	hr = lpToContext->HrOpenRootFolder(&lpToFolder);
	if (FAILED(hr))
		goto exit;

	hr = lpToContext->HrGetSyncStatusStream(lpToFolder, &lpToStream);
	if (FAILED(hr))
		goto exit;

	hr = lpFromFolder->OpenProperty(PR_HIERARCHY_SYNCHRONIZER, &IID_IExchangeExportChanges, 0, 0, (LPUNKNOWN FAR *)&lpEHChanges);
	if(hr != hrSuccess)
		goto exit;

	hr = CheckExit();
	if(hr!= hrSuccess)
		goto exit;

	hr = lpToFolder->OpenProperty(PR_COLLECTOR, &IID_IExchangeImportHierarchyChanges, 0, 0, (LPUNKNOWN FAR *)&lpIHChanges);
	if(hr != hrSuccess)
		goto exit;

	hr = CheckExit();
	if(hr!= hrSuccess)
		goto exit;

	hr = lpIHChanges->Config(lpToStream, 0);
	if (hr == MAPI_E_NOT_FOUND){
		hr = ResetStream(lpToStream);
		if (hr != hrSuccess)
			goto exit;
		hr = lpIHChanges->Config(lpToStream, 0);
	}
	if(hr != hrSuccess)
		goto exit;

	hr = CheckExit();
	if(hr!= hrSuccess)
		goto exit;

	hr = lpEHChanges->Config(lpFromStream, SYNC_UNICODE, lpIHChanges, NULL, (LPSPropTagArray)&sptEntryID, NULL, 1);
	if (hr == MAPI_E_NOT_FOUND){
		hr = ResetStream(lpFromStream);
		if (hr != hrSuccess)
			goto exit;
		hr = lpEHChanges->Config(lpFromStream, SYNC_UNICODE, lpIHChanges, NULL, (LPSPropTagArray)&sptEntryID, NULL, 1);
	}
	if(hr != hrSuccess)
		goto exit;

	do{
		hrSync = CheckExit();
		if(hrSync != hrSuccess)
			break;

		hrSync = lpEHChanges->Synchronize(&ulSteps, &ulStep);
		if(ulStep % 10 == 0){
			hr = lpEHChanges->UpdateState(lpFromStream);
			if(hr != hrSuccess)
				goto exit;
		}
		if(m_lpSyncProgressCallBack && hrSync == SYNC_W_PROGRESS){
			m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, ulStep, ulSteps, dProgressStart + ((double)ulStep / ulSteps) * 0.05, L"Folders");
		}
	}while(hrSync == SYNC_W_PROGRESS);

	// Remember hrSync status for later, but always save the sync state

	hr = lpIHChanges->UpdateState(lpToStream);
	if(hr != hrSuccess)
		goto exit;

	hr = lpEHChanges->UpdateState(lpFromStream);
	if(hr != hrSuccess)
		goto exit;

	// Update the change tracking in ECSyncContext.
	hr = lpFromContext->HrUpdateChangeId(lpFromStream);
	if (hr != hrSuccess)
		goto exit;

	hr = hrSync;

exit:
	SaveSyncStatusStreams();	// Ignore errors

	if (lpFromFolder)
		lpFromFolder->Release();

	if (lpToFolder)
		lpToFolder->Release();

	if(lpEHChanges)
		lpEHChanges->Release();

	if(lpIHChanges)
		lpIHChanges->Release();

	return hr;
}

HRESULT ECSync::MessageSync(std::list<SBinary> &lstChanges){
	HRESULT hr = hrSuccess;
	LPMAPIFOLDER lpInboxFolder = NULL;
	ULONG ulCount = 0;
	LPSPropValue lpPropValue = NULL;
	std::list<SBinary>::const_iterator iChanges;

	//order of syncing folders: Calendar, Inbox, Contacts, Tasks, Notes, Drafts
	SizedSPropTagArray(6, sptInbox) = { 6, {PR_IPM_APPOINTMENT_ENTRYID, PR_ENTRYID, PR_IPM_CONTACT_ENTRYID, PR_IPM_TASK_ENTRYID, PR_IPM_NOTE_ENTRYID, PR_IPM_DRAFTS_ENTRYID} };

	m_ulTotalStep = 0;

	UpdateSyncStatus(EC_SYNC_STATUS_MESSAGE, 0);

	if (m_lpOfflineContext->HrGetReceiveFolder(&lpInboxFolder) == hrSuccess) {
		for (ulCount = 0; ulCount < sptInbox.cValues; ++ulCount) {
			hr = HrGetOneProp(lpInboxFolder, sptInbox.aulPropTag[ulCount], &lpPropValue);
			if(hr != hrSuccess) {
				hr = hrSuccess;
				goto nextdefaultfolder;
			}

			iChanges = find(lstChanges.begin(), lstChanges.end(), lpPropValue->Value.bin);
			if (iChanges != lstChanges.end()) {
				hr = MessageSyncFolder(lpPropValue->Value.bin);
				if(hr == MAPI_E_CANCEL || hr == MAPI_E_USER_CANCEL){
					goto exit;
				}
				delete [] iChanges->lpb;
				lstChanges.erase(iChanges);
			}

nextdefaultfolder:
			MAPIFreeBuffer(lpPropValue);
			lpPropValue = NULL;
		}
	}

	for (iChanges = lstChanges.begin(); iChanges != lstChanges.end(); ++iChanges) {
		hr = CheckExit();
		if(hr!= hrSuccess)
			goto exit;

		hr = MessageSyncFolder(*iChanges);
		if(hr == MAPI_E_CANCEL || hr == MAPI_E_USER_CANCEL || hr == MAPI_E_NETWORK_ERROR)
			break;

		hr = hrSuccess;
	}

exit:
	UpdateSyncStatus(0, EC_SYNC_STATUS_MESSAGE);
	MAPIFreeBuffer(lpPropValue);
	if(lpInboxFolder)
		lpInboxFolder->Release();

	return hr;
}

HRESULT ECSync::ReleaseChangeList(std::list<SBinary> &lstChanges) {
	std::list<SBinary>::const_iterator i;
	for (i = lstChanges.begin(); i != lstChanges.end(); ++i)
		delete [] i->lpb;
	lstChanges.clear();
	return hrSuccess;
}

HRESULT ECSync::CreateChangeList(std::list<SBinary> *lplstChanges, ULONG *lpulSteps){
	HRESULT hr = hrSuccess;
	LPSRowSet		lpRows = NULL;
	ULONG ulCount = 0;
	ULONG ulSteps = 0;
	ULONG ulOfflineSteps = 0;
	ULONG ulOnlineSteps = 0;
	SBinary sChangedFolder;

	SizedSPropTagArray(3, sptFolder) = { 3, {PR_ENTRYID, PR_FOLDER_TYPE, PR_SOURCE_KEY} };

	if(!m_lpOfflineContext || !m_lpOnlineContext){
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	UpdateSyncStatus(EC_SYNC_STATUS_COUNTING, EC_SYNC_STATUS_MESSAGE | EC_SYNC_STATUS_FOLDER | EC_SYNC_STATUS_LOGGING_IN);

	hr = m_lpOfflineContext->HrQueryHierarchyTable((LPSPropTagArray)&sptFolder, &lpRows);
	if(hr != hrSuccess)
		goto exit;

	for (ulCount = 0; ulCount < lpRows->cRows; ++ulCount) {
		hr = CheckExit();
		if(hr!= hrSuccess)
			goto exit;

		if(lpRows->aRow[ulCount].lpProps[0].ulPropTag != PR_ENTRYID)
			continue;

		if(lpRows->aRow[ulCount].lpProps[1].ulPropTag == PR_FOLDER_TYPE && lpRows->aRow[ulCount].lpProps[1].Value.ul == FOLDER_SEARCH)
			continue;

		if (m_eResyncReason != ResyncReasonRelocated) {
			hr = m_lpOnlineContext->HrGetSteps(&lpRows->aRow[ulCount].lpProps[0].Value.bin, &lpRows->aRow[ulCount].lpProps[2].Value.bin, m_ulSyncFlags, &ulOnlineSteps);
			if(hr != hrSuccess) {
				hr = hrSuccess;
				ulOnlineSteps = 0;
			}
		}

		hr =m_lpOfflineContext->HrGetSteps(&lpRows->aRow[ulCount].lpProps[0].Value.bin, &lpRows->aRow[ulCount].lpProps[2].Value.bin, m_ulSyncFlags, &ulOfflineSteps);
		if(hr != hrSuccess) {
			hr = hrSuccess;
			ulOfflineSteps = 0;
		}

		if (m_eResyncReason != ResyncReasonNone || ulOnlineSteps + ulOfflineSteps > 0) {
			sChangedFolder.cb = lpRows->aRow[ulCount].lpProps[0].Value.bin.cb;
			sChangedFolder.lpb = new BYTE[sChangedFolder.cb];
			memcpy(sChangedFolder.lpb, lpRows->aRow[ulCount].lpProps[0].Value.bin.lpb, sChangedFolder.cb);
			lplstChanges->push_back(sChangedFolder);

			ulSteps += ulOnlineSteps;
			ulSteps += ulOfflineSteps;
			ulSteps += 2; // Each folder is two steps (online -> offline and offline -> online)
		}
	}

	*lpulSteps = ulSteps;

exit:
	UpdateSyncStatus(0, EC_SYNC_STATUS_COUNTING);

	if (lpRows)
		FreeProws(lpRows);

	return hr;
}

HRESULT ECSync::MessageSyncFolder(SBinary sEntryID){
	HRESULT hr = hrSuccess;
	LPMAPIFOLDER lpOfflineFolder = NULL;
	LPMAPIFOLDER lpOnlineFolder = NULL;
	LPSTREAM lpOfflineStream = NULL;
	LPSTREAM lpOnlineStream = NULL;
	ULONG ulObjType = 0;
	ULONG ulSyncOfflineFlags = SYNC_NORMAL | SYNC_ASSOCIATED | SYNC_READ_STATE;
	ULONG ulSyncOnlineFlags = ulSyncOfflineFlags;
	HRESULT hrSync1 = hrSuccess;
	HRESULT hrSync2 = hrSuccess;

	hr = Logon();
	if (hr != hrSuccess) {
		ec_log_debug("Unable to login for folder sync: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	hr = m_lpOfflineContext->HrOpenFolder(&sEntryID, &lpOfflineFolder);
	if(hr == MAPI_E_END_OF_SESSION){
		hr = ReLogin();
		if (hr != hrSuccess) {
			ec_log_info("Failed (re-)connecting: %s (%x)",
				GetMAPIErrorMessage(hr), hr);
			goto exit;
		}

		hr = m_lpOfflineContext->HrOpenFolder(&sEntryID, &lpOfflineFolder);
	}
	if (hr != hrSuccess) {
		ec_log_debug("Unable to open offline folder for folder sync: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	hr = CheckExit();
	if(hr != hrSuccess)
		goto exit;

	hr = m_lpOnlineContext->HrOpenFolder(&sEntryID, &lpOnlineFolder);
	if(hr == MAPI_E_END_OF_SESSION){
		hr = ReLogin();
		if (hr != hrSuccess) {
			ec_log_info("Failed (re-)connecting: %s (%x)",
				GetMAPIErrorMessage(hr), hr);
			goto exit;
		}

		hr = m_lpOnlineContext->HrOpenFolder(&sEntryID, &lpOnlineFolder);
	}

	if (hr != hrSuccess) {
		ec_log_debug("Unable to open online folder for folder sync: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	hr = CheckExit();
	if (hr != hrSuccess)
		goto exit;

	hr = m_lpOfflineContext->HrGetSyncStatusStream(lpOfflineFolder, &lpOfflineStream);
	if(hr == MAPI_W_POSITION_CHANGED){
		ulSyncOfflineFlags |= SYNC_NO_SOFT_DELETIONS;
		hr = hrSuccess;
	}

	if (hr == MAPI_E_NOT_FOUND) {
		ec_log_debug("Unable to open offline stream for folder sync: Probably the folder is already deleted");
		hr = hrSuccess;
		goto exit;
	} else if (hr != hrSuccess) {
		ec_log_debug("Unable to open offline stream for folder sync: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	hr = CheckExit();
	if(hr!= hrSuccess)
		goto exit;

	hr = m_lpOnlineContext->HrGetSyncStatusStream(lpOnlineFolder, &lpOnlineStream);
	if (hr == MAPI_W_POSITION_CHANGED){
		ulSyncOnlineFlags |= SYNC_NO_SOFT_DELETIONS;
		hr = hrSuccess;
	}

	if (hr == MAPI_E_NOT_FOUND) {
		ec_log_debug("Unable to open online stream for folder sync: Probably the folder is already deleted");
		hr = hrSuccess;
		goto exit;
	} else if (hr != hrSuccess) {
		ec_log_debug("Unable to open online stream for folder sync: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	hr = CheckExit();
	if (hr != hrSuccess)
		goto exit;

	UpdateSyncStatus(EC_SYNC_OFFLINE_STORE, EC_SYNC_ONLINE_STORE);

	hr = MessageSyncFolderOneWay(lpOfflineFolder, lpOnlineFolder, lpOfflineStream, lpOnlineStream);
	if(hr == MAPI_E_CANCEL || hr == MAPI_E_USER_CANCEL)
		goto exit;

	hrSync1 = hr;
	hr = hrSuccess;

	// Update the change tracking in ECSyncContext.
	hr = m_lpOfflineContext->HrUpdateChangeId(lpOfflineStream);
	if (hr != hrSuccess) {
		ec_log_debug("Unable to update offline change id: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	hr = CheckExit();
	if (hr != hrSuccess)
		goto exit;

	UpdateSyncStatus(EC_SYNC_ONLINE_STORE, EC_SYNC_OFFLINE_STORE);

	hr = MessageSyncFolderOneWay(lpOnlineFolder, lpOfflineFolder, lpOnlineStream, lpOfflineStream, (m_eResyncReason == ResyncReasonRelocated ? ECSync::resyncFrom : 0));
	if(hr == MAPI_E_CANCEL || hr == MAPI_E_USER_CANCEL)
		goto exit;

	hrSync2 = hr;
	hr = hrSuccess;

	// Update the change tracking in ECSyncContext.
	hr = m_lpOnlineContext->HrUpdateChangeId(lpOnlineStream);
	if (hr != hrSuccess) {
		ec_log_debug("Unable to update online change id: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	if (m_eResyncReason == ResyncReasonRequested) {
		ec_log_info("Server requested resync");

		hr = ResyncFoldersBothWays(lpOnlineFolder, lpOfflineFolder);
		if (hr != hrSuccess) {
			ec_log_info("ResyncFoldersBothWays failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}
	}
	else if (m_eResyncReason == ResyncReasonRelocated) {
		ec_log_info("Resync required because of relocation");

		hr = ResyncFoldersOneWay(lpOnlineFolder, lpOfflineFolder);
		if (hr != hrSuccess) {
			ec_log_info("ResyncFoldersOneWay failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}
	}

exit:
	UpdateSyncStatus(0, EC_SYNC_ONLINE_STORE | EC_SYNC_OFFLINE_STORE);

	if (hr != MAPI_E_NETWORK_ERROR && hr != MAPI_E_LOGON_FAILED && hr != MAPI_E_CANCEL && hr != MAPI_E_USER_CANCEL &&
		hrSync1 != MAPI_E_NETWORK_ERROR && hrSync1 != MAPI_E_LOGON_FAILED &&
		hrSync2 != MAPI_E_NETWORK_ERROR && hrSync2 != MAPI_E_LOGON_FAILED &&
		(FAILED(hr) || FAILED(hrSync1) || FAILED(hrSync2)) && !m_bSupressErrorPopup)
	{
		std::wstring strReportText = _("The synchronization of a folder failed. Press retry to continue or ignore to supress all errors.\r\n\r\nPlease contact the system administrator if this problem persists.");
		strReportText += _T("\r\n");
		strReportText += _("Error codes: ") + tstringify(hr, true) + _T(", ") + tstringify(hrSync1, true) + _T(", ") + tstringify(hrSync2, true) + _T(".\r\n");

		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Prompting user for sync error, %08x, %08x, %08x.", hr, hrSync1, hrSync2);

		hr = ReportError(strReportText.c_str());
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "User choice: %08x", hr);

		if (hr == MAPI_W_PARTIAL_COMPLETION) {
			m_bSupressErrorPopup = true;
			hr = hrSuccess;
		}
	}

	if(lpOfflineFolder)
		lpOfflineFolder->Release();

	if(lpOnlineFolder)
		lpOnlineFolder->Release();

	if(lpOfflineStream)
		lpOfflineStream->Release();

	if(lpOnlineStream)
		lpOnlineStream->Release();

	return hr;
}

HRESULT ECSync::MessageSyncFolderOneWay(LPMAPIFOLDER lpFromFolder, LPMAPIFOLDER lpToFolder, LPSTREAM lpFromStream, LPSTREAM lpToStream, ULONG ulFlags)
{
	HRESULT	hr = hrSuccess, hrSync = hrSuccess;
	std::wstring strFolderName = L"<Unknown>";
	LPSPropValue lpDisplayName = NULL;;

	IExchangeExportChanges *lpECChanges = NULL;
	IExchangeImportContentsChanges *lpICChanges = NULL;

	ULONG ulStep = 0, ulSteps = 0;

	if ((ulFlags & ~(ECSync::resyncFrom)) != 0) {
		hr = MAPI_E_UNKNOWN_FLAGS;
		ec_log_debug("ECSync::MessageSyncFolderOneWay unknown flags: %d", ulFlags);
		goto exit;
	}

	if (hrSuccess == HrGetOneProp(lpFromFolder, PR_DISPLAY_NAME_W, &lpDisplayName))
		strFolderName = lpDisplayName->Value.lpszW;

	hr = lpFromFolder->OpenProperty(PR_CONTENTS_SYNCHRONIZER, &IID_IExchangeExportChanges, 0, 0, (LPUNKNOWN FAR *)&lpECChanges);
	if (hr != hrSuccess) {
		ec_log_info("ECSync::MessageSyncFolderOneWay OpenProperty(from-folder) failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	hr = CheckExit();
	if (hr!= hrSuccess)
		goto exit;

	hr = lpToFolder->OpenProperty(PR_COLLECTOR, &IID_IExchangeImportContentsChanges, 0, 0, (LPUNKNOWN FAR *)&lpICChanges);
	if (hr != hrSuccess) {
		ec_log_info("ECSync::MessageSyncFolderOneWay OpenProperty(to-folder) failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	hr = CheckExit();
	if (hr!= hrSuccess)
		goto exit;

	hr = HrConfigureImporter(lpICChanges, lpToStream);
	if (hr != hrSuccess) {
		ec_log_info("ECSync::MessageSyncFolderOneWay HrConfigureImporter failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	hr = CheckExit();
	if (hr!= hrSuccess)
		goto exit;

	hr = HrConfigureExporter(lpECChanges, lpFromStream, lpICChanges, (ulFlags == ECSync::resyncFrom ? SYNC_CATCHUP : 0));
	if (hr != hrSuccess) {
		ec_log_info("ECSync::MessageSyncFolderOneWay HrConfigureExporter failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	do {
		hrSync = CheckExit();
		if (hrSync != hrSuccess)
			break;

		hrSync = lpECChanges->Synchronize(&ulSteps, &ulStep);
		if (ulStep % 42 == 0 && (m_ulSyncFlags & SYNC_ASSOCIATED) == 0) {	// Don't update and save when syncing associated messages.
			hr = lpECChanges->UpdateState(lpFromStream);
			if (hr != hrSuccess) {
				ec_log_info("ECSync::MessageSyncFolderOneWay UpdateState(from) during sync failed: %s (%x)",
					GetMAPIErrorDescription(hr), hr);
				goto exit;
			}

			SaveSyncStatusStreams();	// Ignore errors
		}

		if (m_lpSyncProgressCallBack && (hrSync == hrSuccess || hrSync == SYNC_W_PROGRESS))
			m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, ulStep, ulSteps, 0.20 + ((double)(m_ulTotalStep + ulStep) / m_ulTotalSteps) * 0.80, strFolderName.c_str());
	} while(hrSync == SYNC_W_PROGRESS);

	m_ulTotalStep += ulStep;
	++m_ulTotalStep; // Folder itself counts as one step

	if (m_lpSyncProgressCallBack && (hrSync == hrSuccess || hrSync == SYNC_W_PROGRESS))
		m_lpSyncProgressCallBack(m_lpSyncProgressCallBackObject, ulStep, ulSteps, 0.20 + ((double)m_ulTotalStep / m_ulTotalSteps) * 0.80, strFolderName.c_str());

	if ((m_ulSyncFlags & ~SYNC_BEST_BODY) != SYNC_ASSOCIATED) {
		hr = lpICChanges->UpdateState(lpToStream);
		if (hr != hrSuccess) {
			ec_log_info("ECSync::MessageSyncFolderOneWay UpdateState(to) failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}

		hr = lpECChanges->UpdateState(lpFromStream);
		if (hr != hrSuccess) {
			ec_log_info("ECSync::MessageSyncFolderOneWay UpdateState(from) failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}
	}

	// Return the error from Synchronize()
	hr = hrSync;

exit:
	SaveSyncStatusStreams();	// Ignore errors

	MAPIFreeBuffer(lpDisplayName);

	if (lpECChanges)
		lpECChanges->Release();

	if (lpICChanges)
		lpICChanges->Release();

	return hr;
}

HRESULT ECSync::SyncStoreInfo(){
	HRESULT hr = hrSuccess;
	LPMDB lpOfflineStore = NULL;
	LPMDB lpOnlineStore = NULL;
	LPMAPIFOLDER lpOfflineRootFolder = NULL;
	LPMAPIFOLDER lpOnlineRootFolder = NULL;
	LPSPropValue lpPropVals = NULL;
	ULONG ulValues;

	SizedSPropTagArray(11, sptStore) = { 11, {
		PR_FINDER_ENTRYID,
		PR_COMMON_VIEWS_ENTRYID,
		PR_VIEWS_ENTRYID,
		PR_IPM_SENTMAIL_ENTRYID,
		PR_IPM_OUTBOX_ENTRYID,
		PR_IPM_SUBTREE_ENTRYID,
		PR_IPM_DAF_ENTRYID,
		PR_IPM_FAVORITES_ENTRYID,
		PR_IPM_WASTEBASKET_ENTRYID,
		PR_IPM_FAVORITES_ENTRYID,
		PR_SCHEDULE_FOLDER_ENTRYID
	} };

	SizedSPropTagArray(11, sptRootFolder) = { 11, {
		PR_IPM_APPOINTMENT_ENTRYID,
		PR_IPM_CONTACT_ENTRYID,
		PR_IPM_DRAFTS_ENTRYID,
		PR_IPM_JOURNAL_ENTRYID,
		PR_IPM_NOTE_ENTRYID,
		PR_IPM_TASK_ENTRYID,
		PR_REM_ONLINE_ENTRYID,
		PR_ADDITIONAL_REN_ENTRYIDS,
		PR_FREEBUSY_ENTRYIDS,
		PR_IPM_OL2007_ENTRYIDS,
		PR_EC_RESYNC_ID
	} };

	hr = m_lpOnlineContext->HrOpenRootFolder(&lpOnlineRootFolder, &lpOnlineStore);
	if(FAILED(hr))
		goto exit;

	hr = m_lpOfflineContext->HrOpenRootFolder(&lpOfflineRootFolder, &lpOfflineStore);
	if(FAILED(hr))
		goto exit;

	hr = lpOnlineStore->GetProps((LPSPropTagArray)&sptStore, 0, &ulValues, &lpPropVals);
	if(FAILED(hr))
		goto exit;

	hr = lpOfflineStore->SetProps(ulValues, lpPropVals, NULL);
	if(FAILED(hr))
		goto exit;

	hr = lpOfflineStore->SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		goto exit;

	MAPIFreeBuffer(lpPropVals);
	lpPropVals = NULL;

	hr = lpOnlineRootFolder->GetProps((LPSPropTagArray)&sptRootFolder, 0, &ulValues, &lpPropVals);
	if(FAILED(hr))
		goto exit;

	hr = lpOfflineRootFolder->SetProps(ulValues, lpPropVals, NULL);
	if(FAILED(hr))
		goto exit;

	hr = lpOfflineRootFolder->SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		goto exit;

	hr = FolderSyncOneWay(m_lpOnlineContext, m_lpOfflineContext, 0.05);
	if(hr != hrSuccess)
		goto exit;

exit:
	MAPIFreeBuffer(lpPropVals);
	if(lpOfflineRootFolder)
		lpOfflineRootFolder->Release();

	if (lpOfflineStore)
		lpOfflineStore->Release();

	if(lpOnlineRootFolder)
		lpOnlineRootFolder->Release();

	if (lpOnlineStore)
		lpOnlineStore->Release();

	return hr;
}

HRESULT ECSync::HrCreateRemindersFolder(){
	HRESULT hr = hrSuccess;
	LPMDB lpOnlineStore = NULL;
	LPMDB lpOfflineStore = NULL;
	LPMAPIFOLDER lpRootFolder = NULL;
	LPMAPIFOLDER lpInboxFolder = NULL;
	LPMAPIFOLDER lpReminderFolder = NULL;
	LPSRestriction lpRootRestriction = NULL;
	LPSRestriction lpRestriction = NULL;
	LPENTRYLIST lpContainerList = NULL;
	LPENTRYID lpEntryID = NULL;
	ULONG cbEntryID = 0;
	ULONG ulObjType;
	ULONG ulValues = 0;
	LPSPropValue lpPropVal = NULL;

	LPSPropValue lpPropWasteBasket = NULL;
	LPSPropValue lpPropDrafts = NULL;
	LPSPropValue lpPropOutBox = NULL;
	LPSPropValue lpPropAdditionalREN = NULL;

	LPSPropValue lpPropContainerClass = NULL;
	LPSPropValue lpPropIPM = NULL;

	//delete items			store	PR_IPM_WASTEBASKET_ENTRYID 4
	//junk mail				Inbox	PR_ADDITIONAL_REN_ENTRYIDS
	//drafts				Inbox	PR_IPM_DRAFTS_ENTRYID
	//outbox				Store	PR_IPM_OUTBOX_ENTRYID
	//conflicts				Inbox	PR_ADDITIONAL_REN_ENTRYIDS 0
	//local failures		Inbox	PR_ADDITIONAL_REN_ENTRYIDS 2
	//sync issues			Inbox	PR_ADDITIONAL_REN_ENTRYIDS 1

	hr = m_lpOfflineContext->HrOpenRootFolder(&lpRootFolder, &lpOfflineStore);
	if(hr != hrSuccess)
		goto exit;

	hr = m_lpOnlineContext->HrGetMsgStore(&lpOnlineStore);
	if (hr != hrSuccess)
		goto exit;

	hr = lpOnlineStore->GetReceiveFolder((LPTSTR)"IPM", 0, &cbEntryID, &lpEntryID, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpOfflineStore->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, (LPUNKNOWN *) &lpInboxFolder);
	if(hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(sizeof(SRestriction), (LPVOID*)&lpRootRestriction);
	if(hr != hrSuccess)
		goto exit;
	lpRootRestriction->rt = RES_AND;
	lpRootRestriction->res.resAnd.cRes = 2;
	hr = MAPIAllocateMore(sizeof(SRestriction)*2, lpRootRestriction, (LPVOID*)&lpRootRestriction->res.resAnd.lpRes);
	if(hr != hrSuccess)
		goto exit;
	lpRootRestriction->res.resAnd.lpRes[0].rt = RES_AND;

	hr = MAPIAllocateMore(sizeof(SRestriction)*7, lpRootRestriction, (LPVOID*)&lpRestriction);
	if(hr != hrSuccess)
		goto exit;
	lpRootRestriction->res.resAnd.lpRes[0].res.resAnd.lpRes = lpRestriction;
	lpRootRestriction->res.resAnd.lpRes[0].res.resAnd.cRes = 0;
	ulValues = 0;
	if(HrGetOneProp(lpOfflineStore, PR_IPM_WASTEBASKET_ENTRYID, &lpPropWasteBasket) == hrSuccess){
		lpRestriction[ulValues].rt = RES_PROPERTY;
		lpPropWasteBasket->ulPropTag = PR_PARENT_ENTRYID;
		lpRestriction[ulValues].res.resProperty.ulPropTag = PR_PARENT_ENTRYID;
		lpRestriction[ulValues].res.resProperty.lpProp = lpPropWasteBasket;
		lpRestriction[ulValues].res.resProperty.relop = RELOP_NE;
		++ulValues;
	}

	if(HrGetOneProp(lpOfflineStore, PR_IPM_OUTBOX_ENTRYID, &lpPropOutBox) == hrSuccess){
		lpRestriction[ulValues].rt = RES_PROPERTY;
		lpPropOutBox->ulPropTag = PR_PARENT_ENTRYID;
		lpRestriction[ulValues].res.resProperty.ulPropTag = PR_PARENT_ENTRYID;
		lpRestriction[ulValues].res.resProperty.lpProp = lpPropOutBox;
		lpRestriction[ulValues].res.resProperty.relop = RELOP_NE;
		++ulValues;
	}

	if(HrGetOneProp(lpInboxFolder, PR_IPM_DRAFTS_ENTRYID, &lpPropDrafts) == hrSuccess){
		lpRestriction[ulValues].rt = RES_PROPERTY;
		lpPropDrafts->ulPropTag = PR_PARENT_ENTRYID;
		lpRestriction[ulValues].res.resProperty.ulPropTag = PR_PARENT_ENTRYID;
		lpRestriction[ulValues].res.resProperty.lpProp = lpPropDrafts;
		lpRestriction[ulValues].res.resProperty.relop = RELOP_NE;
		++ulValues;
	}

	if(HrGetOneProp(lpInboxFolder, PR_ADDITIONAL_REN_ENTRYIDS, &lpPropAdditionalREN) == hrSuccess){
		if(lpPropAdditionalREN->Value.MVbin.cValues > 0 && lpPropAdditionalREN->Value.MVbin.lpbin[0].cb > 0){
			lpRestriction[ulValues].rt = RES_PROPERTY;
			lpRestriction[ulValues].res.resProperty.ulPropTag = PR_PARENT_ENTRYID;
			lpRestriction[ulValues].res.resProperty.relop = RELOP_NE;
			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestriction, (LPVOID*)&lpRestriction[ulValues].res.resProperty.lpProp);
			if(hr != hrSuccess)
				goto exit;
			lpRestriction[ulValues].res.resProperty.lpProp->ulPropTag = PR_PARENT_ENTRYID;
			lpRestriction[ulValues].res.resProperty.lpProp->Value.bin.cb = lpPropAdditionalREN->Value.MVbin.lpbin[0].cb;
			lpRestriction[ulValues].res.resProperty.lpProp->Value.bin.lpb = lpPropAdditionalREN->Value.MVbin.lpbin[0].lpb;
			++ulValues;
		}
		if(lpPropAdditionalREN->Value.MVbin.cValues > 1 && lpPropAdditionalREN->Value.MVbin.lpbin[1].cb > 0){
			lpRestriction[ulValues].rt = RES_PROPERTY;
			lpRestriction[ulValues].res.resProperty.ulPropTag = PR_PARENT_ENTRYID;
			lpRestriction[ulValues].res.resProperty.relop = RELOP_NE;
			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestriction, (LPVOID*)&lpRestriction[ulValues].res.resProperty.lpProp);
			if(hr != hrSuccess)
				goto exit;
			lpRestriction[ulValues].res.resProperty.lpProp->ulPropTag = PR_PARENT_ENTRYID;
			lpRestriction[ulValues].res.resProperty.lpProp->Value.bin.cb = lpPropAdditionalREN->Value.MVbin.lpbin[1].cb;
			lpRestriction[ulValues].res.resProperty.lpProp->Value.bin.lpb = lpPropAdditionalREN->Value.MVbin.lpbin[1].lpb;
			++ulValues;
		}
		if(lpPropAdditionalREN->Value.MVbin.cValues > 2 && lpPropAdditionalREN->Value.MVbin.lpbin[2].cb > 0){
			lpRestriction[ulValues].rt = RES_PROPERTY;
			lpRestriction[ulValues].res.resProperty.ulPropTag = PR_PARENT_ENTRYID;
			lpRestriction[ulValues].res.resProperty.relop = RELOP_NE;
			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestriction, (LPVOID*)&lpRestriction[ulValues].res.resProperty.lpProp);
			if(hr != hrSuccess)
				goto exit;
			lpRestriction[ulValues].res.resProperty.lpProp->ulPropTag = PR_PARENT_ENTRYID;
			lpRestriction[ulValues].res.resProperty.lpProp->Value.bin.cb = lpPropAdditionalREN->Value.MVbin.lpbin[2].cb;
			lpRestriction[ulValues].res.resProperty.lpProp->Value.bin.lpb = lpPropAdditionalREN->Value.MVbin.lpbin[2].lpb;
			++ulValues;
		}
		if(lpPropAdditionalREN->Value.MVbin.cValues > 4 && lpPropAdditionalREN->Value.MVbin.lpbin[4].cb > 0){
			lpRestriction[ulValues].rt = RES_PROPERTY;
			lpRestriction[ulValues].res.resProperty.ulPropTag = PR_PARENT_ENTRYID;
			lpRestriction[ulValues].res.resProperty.relop = RELOP_NE;
			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestriction, (LPVOID*)&lpRestriction[ulValues].res.resProperty.lpProp);
			if(hr != hrSuccess)
				goto exit;
			lpRestriction[ulValues].res.resProperty.lpProp->ulPropTag = PR_PARENT_ENTRYID;
			lpRestriction[ulValues].res.resProperty.lpProp->Value.bin.cb = lpPropAdditionalREN->Value.MVbin.lpbin[4].cb;
			lpRestriction[ulValues].res.resProperty.lpProp->Value.bin.lpb = lpPropAdditionalREN->Value.MVbin.lpbin[4].lpb;
			++ulValues;
		}
	}
	lpRootRestriction->res.resAnd.lpRes[0].res.resAnd.cRes = ulValues;

	lpRootRestriction->res.resAnd.lpRes[1].rt = RES_AND;
	lpRootRestriction->res.resAnd.lpRes[1].res.resAnd.cRes = 3;
	hr = MAPIAllocateMore(sizeof(SRestriction)*3, lpRootRestriction, (LPVOID*)&lpRestriction);
	if(hr != hrSuccess)
		goto exit;
	lpRootRestriction->res.resAnd.lpRes[1].res.resAnd.lpRes = lpRestriction;

	lpRestriction[0].rt = RES_NOT;
	hr = MAPIAllocateMore(sizeof(SRestriction), lpRootRestriction, (LPVOID*)&lpRestriction[0].res.resNot.lpRes);
	if(hr != hrSuccess)
		goto exit;
	lpRestriction[0].res.resNot.lpRes->rt = RES_AND;
	lpRestriction[0].res.resNot.lpRes->res.resAnd.cRes = 2;
	hr = MAPIAllocateMore(sizeof(SRestriction)*2, lpRootRestriction, (LPVOID*)&lpRestriction[0].res.resNot.lpRes->res.resAnd.lpRes);
	if(hr != hrSuccess)
		goto exit;
	lpRestriction[0].res.resNot.lpRes->res.resAnd.lpRes[0].rt = RES_EXIST;
	lpRestriction[0].res.resNot.lpRes->res.resAnd.lpRes[0].res.resExist.ulPropTag = PR_MESSAGE_CLASS;
	lpRestriction[0].res.resNot.lpRes->res.resAnd.lpRes[1].rt = RES_CONTENT;
	lpRestriction[0].res.resNot.lpRes->res.resAnd.lpRes[1].res.resContent.ulPropTag = PR_MESSAGE_CLASS;
	lpRestriction[0].res.resNot.lpRes->res.resAnd.lpRes[1].res.resContent.ulFuzzyLevel = FL_PREFIX;
	hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestriction, (LPVOID*)&lpRestriction[0].res.resNot.lpRes->res.resAnd.lpRes[1].res.resContent.lpProp);
	if(hr != hrSuccess)
		goto exit;
	lpRestriction[0].res.resNot.lpRes->res.resAnd.lpRes[1].res.resContent.lpProp->ulPropTag = PR_MESSAGE_CLASS;
	lpRestriction[0].res.resNot.lpRes->res.resAnd.lpRes[1].res.resContent.lpProp->Value.lpszA = "IPM.Schedule";

	lpRestriction[1].rt = RES_BITMASK;
	lpRestriction[1].res.resBitMask.relBMR = BMR_EQZ;
	lpRestriction[1].res.resBitMask.ulMask = 0x00000004;
	lpRestriction[1].res.resBitMask.ulPropTag = PR_MESSAGE_FLAGS;

	lpRestriction[2].rt = RES_OR;
	lpRestriction[2].res.resOr.cRes = 2;
	hr = MAPIAllocateMore(sizeof(SRestriction)*2, lpRootRestriction, (LPVOID*)&lpRestriction[2].res.resOr.lpRes);
	if(hr != hrSuccess)
		goto exit;
	lpRestriction[2].res.resOr.lpRes[0].rt = RES_PROPERTY;
	lpRestriction[2].res.resOr.lpRes[0].res.resProperty.relop = RELOP_EQ;
	//named property reminder set, doesn't need getidsfromnames in kopano
	lpRestriction[2].res.resOr.lpRes[0].res.resProperty.ulPropTag = 0x81A3000B;
	hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestriction, (LPVOID*)&lpRestriction[2].res.resOr.lpRes[0].res.resProperty.lpProp);
	if(hr != hrSuccess)
		goto exit;
	lpRestriction[2].res.resOr.lpRes[0].res.resProperty.lpProp->ulPropTag = 0x81A3000B;
	lpRestriction[2].res.resOr.lpRes[0].res.resProperty.lpProp->Value.b = true;

	lpRestriction[2].res.resOr.lpRes[1].rt = RES_AND;
	lpRestriction[2].res.resOr.lpRes[1].res.resAnd.cRes = 2;
	hr = MAPIAllocateMore(sizeof(SRestriction)*2, lpRootRestriction, (LPVOID*)&lpRestriction[2].res.resOr.lpRes[1].res.resAnd.lpRes);
	if(hr != hrSuccess)
		goto exit;
	lpRestriction[2].res.resOr.lpRes[1].res.resAnd.lpRes[0].rt = RES_EXIST;
	//named property is recurring, doesn't need getidsfromnames in kopano
	lpRestriction[2].res.resOr.lpRes[1].res.resAnd.lpRes[0].res.resExist.ulPropTag = 0x8023000B;
	lpRestriction[2].res.resOr.lpRes[1].res.resAnd.lpRes[1].rt = RES_PROPERTY;
	lpRestriction[2].res.resOr.lpRes[1].res.resAnd.lpRes[1].res.resProperty.relop = RELOP_EQ;
	lpRestriction[2].res.resOr.lpRes[1].res.resAnd.lpRes[1].res.resProperty.ulPropTag = 0x8023000B;
	hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestriction, (LPVOID*)&lpRestriction[2].res.resOr.lpRes[1].res.resAnd.lpRes[1].res.resProperty.lpProp);
	if(hr != hrSuccess)
		goto exit;
	lpRestriction[2].res.resOr.lpRes[1].res.resAnd.lpRes[1].res.resProperty.lpProp->ulPropTag = 0x8023000B;
	lpRestriction[2].res.resOr.lpRes[1].res.resAnd.lpRes[1].res.resProperty.lpProp->Value.b = true;

	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), (LPVOID*)&lpContainerList);
	if(hr != hrSuccess)
		goto exit;

	lpContainerList->cValues = 1;
	hr = MAPIAllocateMore(sizeof(SBinary),lpContainerList, (LPVOID*)&lpContainerList->lpbin);
	if(hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(lpOfflineStore, PR_IPM_SUBTREE_ENTRYID, &lpPropIPM);
	if(hr != hrSuccess)
		goto exit;
	lpContainerList->lpbin[0].cb = lpPropIPM->Value.bin.cb;
	lpContainerList->lpbin[0].lpb = lpPropIPM->Value.bin.lpb;

	hr = lpRootFolder->CreateFolder(FOLDER_SEARCH, (LPTSTR)"Reminders", NULL, &IID_IMAPIFolder, OPEN_IF_EXISTS, &lpReminderFolder);
	if(hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID*)&lpPropContainerClass);
	lpPropContainerClass->ulPropTag = PR_CONTAINER_CLASS;
	lpPropContainerClass->Value.lpszA = "Outlook.Reminder";
	hr = HrSetOneProp(lpReminderFolder, lpPropContainerClass);
	if(hr != hrSuccess)
		goto exit;

	hr = lpReminderFolder->SetSearchCriteria(lpRootRestriction, lpContainerList, RECURSIVE_SEARCH);
	if(hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(lpReminderFolder, PR_ENTRYID, &lpPropVal);
	if(hr != hrSuccess)
		goto exit;

	lpPropVal->ulPropTag = PR_REM_OFFLINE_ENTRYID;

	hr = HrSetOneProp(lpRootFolder, lpPropVal);
	if(hr != hrSuccess)
		goto exit;

	hr = lpRootFolder->SaveChanges(0);
	if(hr != hrSuccess)
		goto exit;

	hr = HrSetOneProp(lpInboxFolder, lpPropVal);
	if(hr != hrSuccess)
		goto exit;

	hr = lpInboxFolder->SaveChanges(0);
	if(hr != hrSuccess)
		goto exit;

exit:
	if (lpOnlineStore)
		lpOnlineStore->Release();

	if (lpOfflineStore)
		lpOfflineStore->Release();

	if(lpRootFolder)
		lpRootFolder->Release();

	if(lpInboxFolder)
		lpInboxFolder->Release();

	if(lpReminderFolder)
		lpReminderFolder->Release();
	MAPIFreeBuffer(lpRootRestriction);
	MAPIFreeBuffer(lpContainerList);
	MAPIFreeBuffer(lpEntryID);
	MAPIFreeBuffer(lpPropWasteBasket);
	MAPIFreeBuffer(lpPropDrafts);
	MAPIFreeBuffer(lpPropOutBox);
	MAPIFreeBuffer(lpPropAdditionalREN);
	MAPIFreeBuffer(lpPropVal);
	MAPIFreeBuffer(lpPropContainerClass);
	MAPIFreeBuffer(lpPropIPM);
	return hr;
}

HRESULT ECSync::HrSyncReceiveFolders(ECSyncContext *lpFromContext, ECSyncContext *lpToContext){
	HRESULT hr = hrSuccess;
	LPMDB lpFromStore = NULL;
	LPMDB lpToStore = NULL;
	LPMAPITABLE lpTable = NULL;
	LPSRowSet lpRows = NULL;
	LPEXCHANGEMANAGESTORE lpEMS = NULL;

	SizedSPropTagArray(2, spt) = { 2, {PR_ENTRYID, PR_MESSAGE_CLASS_A} };

	hr = lpToContext->HrGetMsgStore(&lpToStore);
	if (hr != hrSuccess)
		goto exit;

	hr = lpFromContext->HrGetMsgStore(&lpFromStore);
	if (hr != hrSuccess)
		goto exit;

	hr = lpToStore->QueryInterface(IID_IExchangeManageStore, (LPVOID*)&lpEMS);
	if(hr != hrSuccess)
		goto exit;

	hr = lpFromStore->GetReceiveFolderTable(0, &lpTable);
	if(hr != hrSuccess)
		goto exit;

	hr = lpTable->SetColumns((LPSPropTagArray) &spt, 0);
	if(hr != hrSuccess)
		goto exit;

	//FIXME: only syncing 100 recieve folders
	hr = lpTable->QueryRows(100, 0, &lpRows);
	if(hr != hrSuccess)
		goto exit;

	for (ULONG ulCount = 0; ulCount < lpRows->cRows; ++ulCount) {
		if(lpRows->aRow[ulCount].lpProps[0].ulPropTag != PR_ENTRYID || lpRows->aRow[ulCount].lpProps[1].ulPropTag != PR_MESSAGE_CLASS_A ||strcmp(lpRows->aRow[ulCount].lpProps[1].Value.lpszA, "IPC")==0)
			continue;
		hr = lpToStore->SetReceiveFolder((LPTSTR)lpRows->aRow[ulCount].lpProps[1].Value.lpszA, 0, lpRows->aRow[ulCount].lpProps[0].Value.bin.cb, (LPENTRYID)lpRows->aRow[ulCount].lpProps[0].Value.bin.lpb);
		if(hr != hrSuccess){
			hr = hrSuccess;
			continue;
		}
	}

exit:
	if(lpEMS)
		lpEMS->Release();

	if (lpRows)
		FreeProws(lpRows);

	if (lpTable)
		lpTable->Release();

	if (lpFromStore)
		lpFromStore->Release();

	if (lpToStore)
		lpToStore->Release();

	return hr;
}
HRESULT ECSync::SaveSyncStatusStreams(){
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpSyncStatusProp = NULL;
	LPMDB			lpStore = NULL;

	if (!m_lpOfflineContext)
		goto exit;

	hr = m_lpOfflineContext->HrGetMsgStore(&lpStore);
	if (hr != hrSuccess)
		goto exit;



	// offline
	hr = m_lpOfflineContext->HrSaveSyncStatus(&lpSyncStatusProp);
	if (hr != hrSuccess)
		goto exit;

	lpSyncStatusProp->ulPropTag = PR_EC_OFFLINE_SYNC_STATUS;
	hr = HrSetOneProp(lpStore, lpSyncStatusProp);
	if(hr != hrSuccess)
		goto exit;

	hr = lpStore->SaveChanges(KEEP_OPEN_READWRITE | FORCE_SAVE);
	if(hr != hrSuccess)
		goto exit;

	MAPIFreeBuffer(lpSyncStatusProp);
	lpSyncStatusProp = NULL;



	// online
	if (m_lpOnlineContext) {
		hr = m_lpOnlineContext->HrSaveSyncStatus(&lpSyncStatusProp);
		if (hr != hrSuccess)
			goto exit;

		lpSyncStatusProp->ulPropTag = PR_EC_ONLINE_SYNC_STATUS;
		hr = HrSetOneProp(lpStore, lpSyncStatusProp);
		if(hr != hrSuccess)
			goto exit;

		hr = lpStore->SaveChanges(KEEP_OPEN_READWRITE | FORCE_SAVE);
		if(hr != hrSuccess)
			goto exit;
	}



	// change notifications
	hr = HrSaveChangeNotificationStatusStreams();

exit:
	MAPIFreeBuffer(lpSyncStatusProp);
	if (lpStore)
		lpStore->Release();

	return hr;
}

HRESULT ECSync::HrLoadSyncStatusStream(ECSyncContext *lpContext, LPMDB lpStore, ULONG ulPropTag)
{
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpSyncStatusProp = NULL;

	ASSERT(lpContext);
	ASSERT(lpStore != NULL);
	ASSERT(PROP_TYPE(ulPropTag) == PT_BINARY);

	ZLOG_DEBUG(m_lpLogger, "Loading sync status stream from %s", PropNameFromPropTag(ulPropTag).c_str());

	// FIXME read the stream directly
	hr = HrGetOneBinProp(lpStore, ulPropTag, &lpSyncStatusProp);
	if (hr == MAPI_E_NOT_FOUND){
		ZLOG_DEBUG(m_lpLogger, "Status stream not found, creating new one");

		lpContext->HrClearSyncStatus();
		hr = hrSuccess;
		goto exit;
	} else if(hr != hrSuccess)
		goto exit;

	hr = lpContext->HrLoadSyncStatus(&lpSyncStatusProp->Value.bin);
	if (hr != hrSuccess)
		goto exit;

exit:
	MAPIFreeBuffer(lpSyncStatusProp);
	return hr;
}

HRESULT ECSync::LoadSyncStatusStreams(){
	HRESULT			hr = hrSuccess;
	LPMDB			lpStore = NULL;

	if(!m_lpOfflineContext || !m_lpOnlineContext){
		hr = MAPI_E_NETWORK_ERROR;
		goto exit;
	}

	hr = m_lpOfflineContext->HrGetMsgStore(&lpStore);
	if (hr != hrSuccess)
		goto exit;

	hr = HrLoadSyncStatusStream(m_lpOfflineContext, lpStore, PR_EC_OFFLINE_SYNC_STATUS);
	if (hr != hrSuccess)
		goto exit;

	hr = HrLoadSyncStatusStream(m_lpOnlineContext, lpStore, PR_EC_ONLINE_SYNC_STATUS);
	if (hr != hrSuccess)
		goto exit;


	hr = HrSetupChangeAdvisors();

exit:
	if (lpStore)
		lpStore->Release();

	return hr;
}

HRESULT ECSync::HrConfigureImporter(LPEXCHANGEIMPORTCONTENTSCHANGES lpImporter, LPSTREAM lpStream)
{
	HRESULT hr = hrSuccess;
	mapi_object_ptr<IECImportContentsChanges, IID_IECImportContentsChanges> ptrEcImporter;

	if (lpImporter == NULL || lpStream == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpImporter->Config(lpStream, 0);
	if (hr == MAPI_E_NOT_FOUND){
		hr = ResetStream(lpStream);
		if (hr != hrSuccess)
			goto exit;
		hr = lpImporter->Config(lpStream, 0);
	}
	if (hr != hrSuccess)
		goto exit;


	hr = lpImporter->QueryInterface(ptrEcImporter.iid, &ptrEcImporter);
	if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED) {
		hr = hrSuccess;
		goto exit;
	}
	if (hr != hrSuccess)
		goto exit;

	// Select IID_IECMessageRaw, so the destubbing code will be bypassed.
	hr = ptrEcImporter->SetMessageInterface(IID_IECMessageRaw);

exit:
	return hr;
}

HRESULT ECSync::HrConfigureExporter(LPEXCHANGEEXPORTCHANGES lpExporter, LPSTREAM lpStream, LPEXCHANGEIMPORTCONTENTSCHANGES lpImporter, ULONG ulExtraFlags)
{
	HRESULT hr = hrSuccess;
	mapi_object_ptr<IECExportChanges, IID_IECExportChanges> ptrEcExporter;

	SizedSPropTagArray(1, sptEntryID) = { 1, {PR_ENTRYID} };

	if (lpExporter == NULL || lpStream == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if ((ulExtraFlags & ~SYNC_CATCHUP) != 0) {
		hr = MAPI_E_UNKNOWN_FLAGS;
		goto exit;
	}

	hr = lpExporter->Config(lpStream, m_ulSyncFlags | ulExtraFlags, lpImporter, NULL, (LPSPropTagArray)&sptEntryID, NULL, 1);
	if (hr == MAPI_E_NOT_FOUND){
		hr = ResetStream(lpStream);
		if (hr != hrSuccess)
			goto exit;
		hr = lpExporter->Config(lpStream, m_ulSyncFlags | ulExtraFlags, lpImporter, NULL, (LPSPropTagArray)&sptEntryID, NULL, 1);
	}
	if (hr != hrSuccess)
		goto exit;


	hr = lpExporter->QueryInterface(ptrEcExporter.iid, &ptrEcExporter);
	if (hr == MAPI_E_INTERFACE_NOT_SUPPORTED) {
		hr = hrSuccess;
		goto exit;
	}
	if (hr != hrSuccess)
		goto exit;

	// Select IID_IECMessageRaw, so the destubbing code will be bypassed.
	hr = ptrEcExporter->SetMessageInterface(IID_IECMessageRaw);

exit:
	return hr;
}

HRESULT ECSync::ReportError(LPCTSTR lpszReportText)
{
	if (m_lpSyncPopupErrorCallBack)
		return m_lpSyncPopupErrorCallBack(m_lpSyncPopupErrorCallBackObject, lpszReportText);

	return hrSuccess;
}

HRESULT ECSync::UpdateSyncStatus(ULONG ulAddStatus, ULONG ulRemoveStatus){
	ULONG ulStatus = 0;
	bool bChanged = false;

	WITH_LOCK(m_hMutex) {
		ULONG ulOldStatus = m_ulStatus;
		m_ulStatus &= ~ulRemoveStatus;
		m_ulStatus |= ulAddStatus;
		bChanged = (ulOldStatus != m_ulStatus);
		ulStatus = m_ulStatus;
	}

	// Perform the callback with m_hMutex unlocked to avoid deadlocks.
	if (bChanged && m_lpSyncStatusCallBack)
		m_lpSyncStatusCallBack(m_lpSyncStatusCallBackObject, ulStatus);

	return hrSuccess;
}

HRESULT ECSync::CheckExit(){
	HRESULT hr = hrSuccess;
	scoped_lock lock(m_hMutex);
	if(m_bExit){
		hr = MAPI_E_CANCEL;
	}else if(m_bCancel){
		hr = MAPI_E_USER_CANCEL;
	}
	return hr;
}

HRESULT ECSync::Logoff(){
	std::list<LPNOTIFICATION>::const_iterator iterNotif;
	LPMDB lpStore = NULL;

	for (iterNotif = m_lstNewMailNotifs.begin();
	     iterNotif != m_lstNewMailNotifs.end(); ++iterNotif)
		MAPIFreeBuffer(*iterNotif);
	m_lstNewMailNotifs.clear();

	if(m_ulOnlineAdviseConnection && m_lpOnlineContext && m_lpOnlineContext->HrGetMsgStore(&lpStore) == hrSuccess) {
		lpStore->Unadvise(m_ulOnlineAdviseConnection);
		lpStore->Release();
		m_ulOnlineAdviseConnection = 0;
	}

	delete m_lpOnlineContext;
	m_lpOnlineContext = NULL;
	delete m_lpOfflineContext;
	m_lpOfflineContext = NULL;
	return hrSuccess;
}

HRESULT ECSync::ReLogin(){
	Logoff();
	return Logon();
}

ULONG ECSync::OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifs) {
	LPNOTIFICATION lpNewMail = NULL;
	bool bStartSync = false;

	// Process all new mail notifications and save them for later
	for (unsigned int i = 0; i < cNotif; ++i) {
		if(lpNotifs[i].ulEventType & m_ulSyncOnNotify)
			bStartSync = true;

		if(lpNotifs[i].ulEventType == fnevNewMail) {
			if (MAPIAllocateBuffer(sizeof(NOTIFICATION), (void **)&lpNewMail) != hrSuccess)
				return 1;

			lpNewMail->ulEventType = fnevNewMail;

			lpNewMail->info.newmail.cbEntryID = lpNotifs[i].info.newmail.cbEntryID;
			if (MAPIAllocateMore(lpNotifs[i].info.newmail.cbEntryID, lpNewMail, (void **)&lpNewMail->info.newmail.lpEntryID) != hrSuccess)
				return 1;
			memcpy(lpNewMail->info.newmail.lpEntryID, lpNotifs[i].info.newmail.lpEntryID, lpNotifs[i].info.newmail.cbEntryID);

			lpNewMail->info.newmail.cbParentID = lpNotifs[i].info.newmail.cbParentID;
			if (MAPIAllocateMore(lpNotifs[i].info.newmail.cbParentID, lpNewMail, (void **)&lpNewMail->info.newmail.lpParentID) != hrSuccess)
				return 1;
			memcpy(lpNewMail->info.newmail.lpParentID, lpNotifs[i].info.newmail.lpParentID, lpNotifs[i].info.newmail.cbParentID);

			if (lpNotifs[i].info.newmail.ulFlags&MAPI_UNICODE)
				MAPICopyUnicode((LPWSTR)lpNotifs[i].info.newmail.lpszMessageClass, lpNewMail, (LPWSTR*)&lpNewMail->info.newmail.lpszMessageClass);
			else
				MAPICopyString((char*)lpNotifs[i].info.newmail.lpszMessageClass, lpNewMail, (char**)&lpNewMail->info.newmail.lpszMessageClass);

			lpNewMail->info.newmail.ulFlags = lpNotifs[i].info.newmail.ulFlags;
			lpNewMail->info.newmail.ulMessageFlags = lpNotifs[i].info.newmail.ulMessageFlags;

			m_lstNewMailNotifs.push_back(lpNewMail);
		}
	}

	// Any change we want triggers a sync
	if (bStartSync)
		this->StartSync();

	return 0;
}


HRESULT ECSync::GetResyncReason(eResyncReason *lpResyncState)
{
	HRESULT hr = hrSuccess;
	GUID	onlineServerUid;
	GUID	storedServerUid;
	ULONG	ulOnlineResyncId;
	ULONG	ulOfflineResyncId;

	if (m_lpOnlineContext == NULL || m_lpOfflineContext == NULL) {
		hr = MAPI_E_UNCONFIGURED;
		goto exit;
	}


	// First check whether the online store was relocated
	hr = m_lpOnlineContext->GetServerUid(&onlineServerUid);
	if (hr != hrSuccess)
		goto exit;

	hr = m_lpOfflineContext->GetStoredServerUid(&storedServerUid);
	if (hr == MAPI_E_NOT_FOUND) {
		hr = m_lpOfflineContext->SetStoredServerUid(&onlineServerUid);
		if (hr != hrSuccess)
			goto exit;

		*lpResyncState = ResyncReasonNone;
		goto exit;
	} else if (hr != hrSuccess)
		goto exit;

	if (memcmp(&onlineServerUid, &storedServerUid, sizeof(onlineServerUid)) != 0) {
		*lpResyncState = ResyncReasonRelocated;
		goto exit;
	}


	// Now check if a resync was requested.
	hr = m_lpOnlineContext->GetResyncID(&ulOnlineResyncId);
	if (hr == hrSuccess)
		hr = m_lpOfflineContext->GetResyncID(&ulOfflineResyncId);
	if (hr != hrSuccess)
		goto exit;

	*lpResyncState = (ulOnlineResyncId > ulOfflineResyncId ? ResyncReasonRequested : ResyncReasonNone);

exit:
	return hr;
}


HRESULT ECSync::ResyncFoldersBothWays(LPMAPIFOLDER lpOnlineFolder, LPMAPIFOLDER lpOfflineFolder)
{
	HRESULT hr = hrSuccess;
	ECResyncSet	onlineResyncSet;
	ECResyncSet	offlineResyncSet;
	StreamPtr ptrStateStream;

	if (lpOnlineFolder == NULL || lpOfflineFolder == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CreateMessageList(lpOnlineFolder, &onlineResyncSet);
	if (hr != hrSuccess) {
		ec_log_info("ECSync::ResyncFoldersBothWays: CreateMessageList failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	hr = FilterAndCreateMessageLists(lpOfflineFolder, &onlineResyncSet, &offlineResyncSet, false);
	if (hr != hrSuccess) {
		ec_log_info("ECSync::ResyncFoldersBothWays: FilterAndCreateMessageLists failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	ec_log_info("Found %u messages online, that are absent offline", onlineResyncSet.Size());
	if (!onlineResyncSet.IsEmpty()) {
		hr = m_lpOfflineContext->HrGetSyncStatusStream(lpOfflineFolder, &ptrStateStream);
		if (hr != hrSuccess) {
			ec_log_info("ECSync::ResyncFoldersBothWays: m_lpOfflineContext->HrGetSyncStatusStream failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}

		hr = ResyncFromSet(lpOnlineFolder, lpOfflineFolder, ptrStateStream, onlineResyncSet);
		if (hr != hrSuccess) {
			ec_log_info("ECSync::ResyncFoldersBothWays: ResyncFromSet failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}
	}

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Found %u messages offline, that are absent online", offlineResyncSet.Size());
	if (!offlineResyncSet.IsEmpty()) {
		hr = m_lpOnlineContext->HrGetSyncStatusStream(lpOnlineFolder, &ptrStateStream);
		if (hr != hrSuccess) {
			ec_log_info("ECSync::ResyncFoldersBothWays: m_lpOnlineContext->HrGetSyncStatusStream(2) failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}

		hr = ResyncFromSet(lpOfflineFolder, lpOnlineFolder, ptrStateStream, offlineResyncSet);
		if (hr != hrSuccess) {
			ec_log_info("ECSync::ResyncFoldersBothWays: ResyncFromSet failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}
	}

exit:
	return hr;
}

HRESULT ECSync::ResyncFoldersOneWay(LPMAPIFOLDER lpOnlineFolder, LPMAPIFOLDER lpOfflineFolder)
{
	HRESULT hr = hrSuccess;
	ECResyncSet	addSet;
	EntryListPtr ptrDelList;
	StreamPtr ptrStateStream;

	if (lpOnlineFolder == NULL || lpOfflineFolder == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CreateMessageList(lpOnlineFolder, &addSet);
	if (hr != hrSuccess) {
		ec_log_info("ECSync::ResyncFoldersBothWays: CreateMessageList failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	hr = FilterAddsAndCreateDeleteLists(lpOfflineFolder, &addSet, &ptrDelList);
	if (hr != hrSuccess) {
		ec_log_info("ECSync::ResyncFoldersBothWays: FilterAddsAndCreateDeleteLists failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Found %u messages online, that are absent offline", addSet.Size());
	if (!addSet.IsEmpty()) {
		hr = m_lpOfflineContext->HrGetSyncStatusStream(lpOfflineFolder, &ptrStateStream);
		if (hr != hrSuccess) {
			ec_log_info("ECSync::ResyncFoldersBothWays: m_lpOfflineContext->HrGetSyncStatusStream failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}

		hr = ResyncFromSet(lpOnlineFolder, lpOfflineFolder, ptrStateStream, addSet);
		if (hr != hrSuccess) {
			ec_log_info("ECSync::ResyncFoldersBothWays: ResyncFromSet failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}
	}

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Found %u messages offline, that are absent online", ptrDelList->cValues);

	if (ptrDelList->cValues) {
		hr = lpOfflineFolder->DeleteMessages(ptrDelList, 0, NULL, DELETE_HARD_DELETE);
		if (hr != hrSuccess) {
			ec_log_info("ECSync::ResyncFoldersBothWays: lpOfflineFolder->DeleteMessages failed: %s (%x)",
				GetMAPIErrorDescription(hr), hr);
			goto exit;
		}
	}

exit:
	return hr;
}

/**
 * Create a set of sourcekey, entryid, last modification time and flags tuples of all
 * messages in a folder. The flags will always be set to SYNC_NEW_MESSAGE.
 *
 * @param[in]	lpFolder	The folder to get the messages from.
 * @param[out]	lpResyncSet	The set of tuples for the messages.
 */
HRESULT ECSync::CreateMessageList(LPMAPIFOLDER lpFolder, ECResyncSet *lpResyncSet)
{
	HRESULT hr = hrSuccess;
	MAPITablePtr ptrContents;
	SRowSetPtr ptrRows;

	SizedSPropTagArray(3, sptaTableProps) = {3, {PR_SOURCE_KEY, PR_ENTRYID, PR_LAST_MODIFICATION_TIME}};
	enum {IDX_SOURCE_KEY, IDX_ENTRYID, IDX_LAST_MODIFICATION_TIME};

	if (lpFolder == NULL || lpResyncSet == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpFolder->GetContentsTable(0, &ptrContents);
	if (hr != hrSuccess) {
		ec_log_info("ECSync::CreateMessageList GetContentsTable failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	hr = ptrContents->SetColumns((LPSPropTagArray)&sptaTableProps, TBL_BATCH);
	if (hr != hrSuccess)
		goto exit;

	for(;;) {
		hr = ptrContents->QueryRows(64, 0, &ptrRows);
		if (hr != hrSuccess)
			goto exit;

		if (ptrRows.size() == 0)
			break;

		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[IDX_SOURCE_KEY].ulPropTag != PR_SOURCE_KEY) {
				m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Server returned object without a sourcekey. Skipping entry.");
				continue;
			}

			if (ptrRows[i].lpProps[IDX_ENTRYID].ulPropTag != PR_ENTRYID) {
				m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Server returned object without an entryid. Skipping entry.");
				continue;
			}

			if (ptrRows[i].lpProps[IDX_LAST_MODIFICATION_TIME].ulPropTag != PR_LAST_MODIFICATION_TIME) {
				m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Server returned object without a last modification time. Skipping entry.");
				continue;
			}

			lpResyncSet->Append(ptrRows[i].lpProps[IDX_SOURCE_KEY].Value.bin, ptrRows[i].lpProps[IDX_ENTRYID].Value.bin, ptrRows[i].lpProps[IDX_LAST_MODIFICATION_TIME].Value.ft);
		}
	}

exit:
	return hr;
}

/**
 * Filter one set of messages and create another one, based on the content of a folder.
 *
 * The purpose of this method is two end up with two sets of messages. One set will contain
 * the messages that exist in one folder and not the other. The other set will contain the
 * messages that exist in the other folder but not in the first.
 * When last modification times are taken into consideration, the first set will also contain
 * items that exist in both folder but have a different last modification time.
 *
 * This method gets all messages from the folder and determines if the message should be
 * removed from the original set or added to the new set. When modification times should
 * be taken into consideration, it can also decide to set the flag of an item in the original
 * set to 0.
 *
 * How this works without consideration of last modification times:
 * - If the sourcekey of a message from the current folder exists in the original set, the
 *   item is removed from that set.
 * - If the sourcekey of a message from the current folder does not exist in the original set,
 *   a new item for that message is added to the new set.
 *
 * How this works with consideration of last modification times:
 * - If the sourcekey of a message from the current folder exists in the original set, the
 *   last modification time is compared to that of the item in the original set:
 *   - If the last modification times are equal, the item is removed from the original set.
 *   - If the last modification times are unequal, the flags of the item in the original set
 *     is set to 0, allowing the importer to update the message later on.
 * - If the sourcekey of a message from the current folder does not exist in the original set,
 *   a new item for that message is added to the new set.
 *
 * @param[in]		lpFolder			The folder to get the messages from.
 * @param[in,out]	lpOtherResyncSet	The original set of messages. This set will be filtered.
 * @param[out]		lpResyncSet			The new set of messages.
 * @param[in]		bCompareLastModTime	If set to true, the last modification times will be taken
 *										into consideration.
 */
HRESULT ECSync::FilterAndCreateMessageLists(LPMAPIFOLDER lpFolder, ECResyncSet *lpOtherResyncSet, ECResyncSet *lpResyncSet, bool bCompareLastModTime)
{
	HRESULT hr = hrSuccess;
	MAPITablePtr ptrContents;
	SRowSetPtr ptrRows;

	SizedSPropTagArray(3, sptaTableProps) = {3, {PR_SOURCE_KEY, PR_ENTRYID, PR_LAST_MODIFICATION_TIME}};
	enum {IDX_SOURCE_KEY, IDX_ENTRYID, IDX_LAST_MODIFICATION_TIME};

	if (lpFolder == NULL || lpOtherResyncSet == NULL || lpResyncSet == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpFolder->GetContentsTable(0, &ptrContents);
	if (hr != hrSuccess) {
		ec_log_info("ECSync::FilterAndCreateMessageLists GetContentsTable failed: %s (%x)",
			GetMAPIErrorDescription(hr), hr);
		goto exit;
	}

	hr = ptrContents->SetColumns((LPSPropTagArray)&sptaTableProps, TBL_BATCH);
	if (hr != hrSuccess)
		goto exit;

	for(;;) {
		hr = ptrContents->QueryRows(64, 0, &ptrRows);
		if (hr != hrSuccess)
			goto exit;

		if (ptrRows.size() == 0)
			break;

		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[IDX_SOURCE_KEY].ulPropTag != PR_SOURCE_KEY) {
				m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Server returned object without a sourcekey. Skipping entry.");
				continue;
			}
			if (ptrRows[i].lpProps[IDX_ENTRYID].ulPropTag != PR_ENTRYID) {
				m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Server returned object without an entryid. Skipping entry.");
				continue;
			}
			if (ptrRows[i].lpProps[IDX_LAST_MODIFICATION_TIME].ulPropTag != PR_LAST_MODIFICATION_TIME) {
				m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Server returned object without a last modification time. Skipping entry.");
				continue;
			}

			if (!bCompareLastModTime) {
				if (!lpOtherResyncSet->Remove(ptrRows[i].lpProps[IDX_SOURCE_KEY].Value.bin))
					lpResyncSet->Append(ptrRows[i].lpProps[IDX_SOURCE_KEY].Value.bin, ptrRows[i].lpProps[IDX_ENTRYID].Value.bin, ptrRows[i].lpProps[IDX_LAST_MODIFICATION_TIME].Value.ft);
			} else {
				ECResyncSetIterator iter(*lpOtherResyncSet, ptrRows[i].lpProps[IDX_SOURCE_KEY].Value.bin);
				if (iter.IsValid()) {
					// If the sourcekey was found in the other set, we'll only remove it if the last modification time
					// equals the modification time of the item we're currently working on.
					// Otherwise we'll leave it in the other set, causing the message to be resynced.
					if (ptrRows[i].lpProps[IDX_LAST_MODIFICATION_TIME].Value.ft == iter.GetLastModTime())
						lpOtherResyncSet->Remove(ptrRows[i].lpProps[IDX_SOURCE_KEY].Value.bin);
					else
						iter.SetFlags(0);
				} else {
					// If the sourcekey was not found in the other set, we'll add it to the new set
					lpResyncSet->Append(ptrRows[i].lpProps[IDX_SOURCE_KEY].Value.bin, ptrRows[i].lpProps[IDX_ENTRYID].Value.bin, ptrRows[i].lpProps[IDX_LAST_MODIFICATION_TIME].Value.ft);
				}
			}
		}
	}

exit:
	return hr;
}

/**
 * Resync a folder based on a resync set.
 * This will cause all messages that are missing or out of date in the destination folder to be imported from the
 * source folder. The missing and out of date messages are defined by the resyncSet.
 *
 * @param[in]	lpFromFolder	The source folder.
 * @param[in]	lpToFolder		The destination folder.
 * @param[in]	lpToStream		The state stream for the destination folder.
 * @param[in]	resyncSet		The set of messages to resync.
 */
HRESULT ECSync::ResyncFromSet(LPMAPIFOLDER lpFromFolder, LPMAPIFOLDER lpToFolder, LPSTREAM lpToStream, ECResyncSet &resyncSet)
{
	HRESULT hr = hrSuccess;
	HRESULT hrTmp = hrSuccess;
	BOOL bFailed = FALSE;
	StreamPtr ptrStateStream;
	ExchangeImportContentsChangesPtr ptrICChanges;
	MessagePtr ptrFromMessage;
	MessagePtr ptrToMessage;
	ULONG ulType = 0;
	SPropValuePtr ptrImporterProps;
	ULONG cImporterProps = 0;

	SizedSPropTagArray(6, sptaImporterProps) = {6, {PR_SOURCE_KEY, PR_MESSAGE_FLAGS, PROP_TAG(PT_BOOLEAN, 0x67AA) /*PR_ASSOCIATED*/, PR_PREDECESSOR_CHANGE_LIST, PR_CHANGE_KEY, PR_ENTRYID}};
	SizedSPropTagArray(5, sptaExcludeProps) = {5, {PR_MESSAGE_SIZE, PR_MESSAGE_RECIPIENTS, PR_MESSAGE_ATTACHMENTS, PR_ATTACH_SIZE, PR_PARENT_SOURCE_KEY}};

	if (lpFromFolder == NULL || lpToFolder == NULL || lpToStream == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpToFolder->OpenProperty(PR_COLLECTOR, &ptrICChanges.iid, 0, 0, &ptrICChanges);
	if(hr != hrSuccess)
		goto exit;

	hr = HrConfigureImporter(ptrICChanges, lpToStream);
	if (hr != hrSuccess)
		goto exit;

	for (ECResyncSetIterator iterator(resyncSet); iterator.IsValid(); iterator.Next()) {
		hrTmp = lpFromFolder->OpenEntry(iterator.GetEntryIDSize(), iterator.GetEntryID(), &ptrFromMessage.iid, 0, &ulType, &ptrFromMessage);
		if (FAILED(hrTmp)) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to open source message for resync. hr=0x%08x", hrTmp);
			bFailed = TRUE;
			continue;
		}

		// We could get these from the contents table earlier. But that creates probably more traffic than
		// only getting them for the messages that need to be synced anyway.
		hrTmp = ptrFromMessage->GetProps((LPSPropTagArray)&sptaImporterProps, 0, &cImporterProps, &ptrImporterProps);
		if (FAILED(hrTmp)) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to get required properties for resync. hr=0x%08x", hrTmp);
			bFailed = TRUE;
			continue;
		}

		hrTmp = ptrICChanges->ImportMessageChange(cImporterProps, ptrImporterProps, iterator.GetFlags(), &ptrToMessage);
		if (FAILED(hrTmp)) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to create destination message for resync. hr=0x%08x", hrTmp);
			bFailed = TRUE;
			continue;
		}

		hrTmp = ptrFromMessage->CopyTo(0, NULL, (LPSPropTagArray)&sptaExcludeProps, 0, NULL, &ptrToMessage.iid, (LPMESSAGE)ptrToMessage, 0, NULL);
		if (FAILED(hrTmp)) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to copy source message to destination for resync. hr=0x%08x", hrTmp);
			bFailed = TRUE;
			continue;
		}

		hrTmp = ptrToMessage->SaveChanges(0);
		if (FAILED(hrTmp)) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to save destination message for resync. hr=0x%08x", hrTmp);
			bFailed = TRUE;
		}
	}

	if (bFailed)
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "One or more messages failed but resync continued.");

exit:
	return hr;
}

/**
 * Filter the set of messages to add, and create an ENTRYLIST that can be used in DeleteMessages of message
 * that should be deleted from the folder.
 *
 * This method works the same as ECSync::FilterAndCreateMessageLists, but instead of creating a set of messages
 * that should be imported to the other folder, it creates a set of messages (in the ENTRYLIST) that are to be
 * deleted from this folder.
 * This is used when performing a resync after a relocation where nothing can be changed offline. So the online
 * folder is leading.
 *
 * @param[in]		lpFolder	The folder to get the messages from.
 * @param[in,out]	lpAddSet	The set of messages that will be added to this folder. This set is filtered.
 * @param[out]		lppDelList	The ENTRYLIST that contains all messages that should be deleted.
 */
HRESULT ECSync::FilterAddsAndCreateDeleteLists(LPMAPIFOLDER lpFolder, ECResyncSet *lpAddSet, LPENTRYLIST *lppDelList)
{
	HRESULT hr = hrSuccess;
	ECResyncSet delSet;
	EntryListPtr ptrEntryList;
	ULONG i = 0;

	hr = FilterAndCreateMessageLists(lpFolder, lpAddSet, &delSet, true);
	if (hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &ptrEntryList);
	if (hr != hrSuccess)
		goto exit;

	ptrEntryList->cValues = delSet.Size();
	hr = MAPIAllocateMore(ptrEntryList->cValues * sizeof *ptrEntryList->lpbin, ptrEntryList, (LPVOID*)&ptrEntryList->lpbin);
	if (hr != hrSuccess)
		goto exit;

	for (ECResyncSetIterator iterator(delSet); iterator.IsValid(); iterator.Next(), ++i) {
		ptrEntryList->lpbin[i].cb = iterator.GetEntryIDSize();
		hr = MAPIAllocateMore(ptrEntryList->lpbin[i].cb, ptrEntryList, (LPVOID*)&ptrEntryList->lpbin[i].lpb);
		if (hr != hrSuccess)
			goto exit;
		memcpy(ptrEntryList->lpbin[i].lpb, iterator.GetEntryID(), ptrEntryList->lpbin[i].cb);
	}

	*lppDelList = ptrEntryList.release();

exit:
	return hr;
}

/**
 * Update the profile to point to the correct server and open the store on it.
 *
 * @param[in]		lpSession	The current MAPI session.
 * @param[in,out]	lppStore	Initially the store on the wrong server is expected.
 *								On success this will be set to the store on the
 *								correct server.
 *
 * @note	This method doesn't really update the profile because currently the
 *			profile isn't updated on a redirect during profile creation either.
 *			This is actually a bug, but some customers like it that way because
 *			otherwise it would show 'internal' server names in the profile
 *			instead of the nice human rememberable names.
 */
HRESULT ECSync::UpdateProfileAndOpenOnlineStore(LPMAPISESSION lpSession, LPMDB *lppStore)
{
	HRESULT hr = hrSuccess;
	SPropValuePtr ptrUserName;
	ProfSectPtr ptrProfSect;
	ExchangeManageStorePtr ptrEMS;
	ULONG cbStoreId;
	EntryIdPtr ptrStoreId;
	MsgStorePtr ptrNewStore;

	if (lpSession == NULL || lppStore == NULL || *lppStore == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpSession->OpenProfileSection((LPMAPIUID)pbGlobalProfileSectionGuid, &ptrProfSect.iid, 0, &ptrProfSect);
	if (hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(ptrProfSect, PR_EC_USERNAME, &ptrUserName);
	if (hr != hrSuccess)
		goto exit;

	hr = (*lppStore)->QueryInterface(ptrEMS.iid, &ptrEMS);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrEMS->CreateStoreEntryID(NULL, ptrUserName->Value.LPSZ, fMapiUnicode, &cbStoreId, &ptrStoreId);
	if (hr != hrSuccess)
		goto exit;

	hr = HrOpenStoreOnline(lpSession, cbStoreId, ptrStoreId, &ptrNewStore);
	if (hr != hrSuccess)
		goto exit;

	// This is where we would get the store entryid, extract the url from it,
	// resolve the real url if it's a pseudo url and update the profile.

	hr = ptrNewStore->QueryInterface(IID_IMsgStore, (LPVOID*)lppStore);

exit:
	return hr;
}



ULONG __stdcall ECSync::xMAPIAdviseSink::OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifs) {
	METHOD_PROLOGUE_(ECSync, MAPIAdviseSink);
	return pThis->OnNotify(cNotif, lpNotifs);
}

HRESULT __stdcall ECSync::xMAPIAdviseSink::QueryInterface(REFIID refiid, void ** lppInterface){
	METHOD_PROLOGUE_(ECSync, MAPIAdviseSink);
	return pThis->QueryInterface(refiid, lppInterface);
}

ULONG __stdcall ECSync::xMAPIAdviseSink::AddRef(){
	METHOD_PROLOGUE_(ECSync, MAPIAdviseSink);
	return pThis->AddRef();
}

ULONG __stdcall ECSync::xMAPIAdviseSink::Release(){
	METHOD_PROLOGUE_(ECSync, MAPIAdviseSink);
	return pThis->Release();
}


ULONG ECSync::xECSync::AddRef() {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->AddRef();
}

ULONG ECSync::xECSync::Release() {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->Release();
}

HRESULT ECSync::xECSync::QueryInterface(REFIID refiid, void **lpvoid) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->QueryInterface(refiid, lpvoid);
}

HRESULT ECSync::xECSync::FirstFolderSync() {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->FirstFolderSync();
}

HRESULT ECSync::xECSync::StartSync() {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->StartSync();
}

HRESULT ECSync::xECSync::StopSync() {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->StopSync();
}

HRESULT ECSync::xECSync::CancelSync() {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->CancelSync();
}

HRESULT ECSync::xECSync::SetSyncStatusCallBack(LPVOID lpSyncStatusCallBackObject, SyncStatusCallBack lpSyncStatusCallBack) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->SetSyncStatusCallBack(lpSyncStatusCallBackObject, lpSyncStatusCallBack);
}

HRESULT ECSync::xECSync::SetSyncProgressCallBack(LPVOID lpSyncProgressCallBackObject, SyncProgressCallBack lpSyncProgressCallBack) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->SetSyncProgressCallBack(lpSyncProgressCallBackObject, lpSyncProgressCallBack);
}

HRESULT ECSync::xECSync::SetSyncPopupErrorCallBack(LPVOID lpSyncPopupErrorCallBackObject, SyncPopupErrorCallBack lpSyncPopupErrorCallBack) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->SetSyncPopupErrorCallBack(lpSyncPopupErrorCallBackObject, lpSyncPopupErrorCallBack);
}

HRESULT ECSync::xECSync::GetSyncStatus(ULONG * lpulStatus) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->GetSyncStatus(lpulStatus);
}

HRESULT ECSync::xECSync::SetWaitTime(ULONG ulWaitTime) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->SetWaitTime(ulWaitTime);
}

HRESULT ECSync::xECSync::GetWaitTime(ULONG* lpulWaitTime) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->GetWaitTime(lpulWaitTime);
}

HRESULT ECSync::xECSync::SetSyncOnNotify(ULONG ulEventMask) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->SetSyncOnNotify(ulEventMask);
}

HRESULT ECSync::xECSync::GetSyncOnNotify(ULONG* lpulEventMask) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->GetSyncOnNotify(lpulEventMask);
}

HRESULT ECSync::xECSync::GetSyncError() {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->GetSyncError();
}

HRESULT ECSync::xECSync::SetThreadLocale(LCID locale) {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->SetThreadLocale(locale);
}

HRESULT ECSync::xECSync::Logoff() {
	METHOD_PROLOGUE_(ECSync, ECSync);
	return pThis->Logoff();
}


#endif
