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

#include <kopano/ECGetText.h>

#include <memory.h>
#include <mapi.h>
#include <mapiutil.h>
#include <mapispi.h>

#include "ClientUtil.h"
#include "Mem.h"
#include <kopano/stringutil.h>

#include <kopano/ECGuid.h>

#include "ECABProvider.h"
#include "ECMSProvider.h"
#include "ECABProviderOffline.h"
#include "ECMSProviderOffline.h"
#include "ECMsgStore.h"
#include "ECArchiveAwareMsgStore.h"
#include "ECMsgStorePublic.h"
#include <kopano/charset/convstring.h>


#ifdef WIN32
#include "ProgressDlg.h"
#include <ECSync.h>

#include <Sensapi.h>

#endif //#ifdef WIN32

#include "DLLGlobal.h"
#include "EntryPoint.h"
#include "ProviderUtil.h"

#include <kopano/charset/convert.h>

#include <boost/filesystem.hpp>
namespace bfs = boost::filesystem;

#ifdef UNICODE
typedef bfs::wpath path;
#else
typedef bfs::path path;
#endif

using namespace std;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static const char THIS_FILE[] = __FILE__;
#endif


HRESULT CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	HRESULT hr = hrSuccess;
	BOOL fTheSame = FALSE;
	PEID peid1 = (PEID)lpEntryID1;
	PEID peid2 = (PEID)lpEntryID2;

	if(lpEntryID1 == NULL || lpEntryID2 == NULL || lpulResult == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (cbEntryID1 < (sizeof(GUID) + 4 + 4) || cbEntryID2 < (sizeof(GUID) + 4 + 4)) {
		hr = MAPI_E_INVALID_ENTRYID;
		goto exit;
	}

	if(memcmp(&peid1->guid, &peid2->guid, sizeof(GUID)) != 0)
		goto exit;

	if(peid1->ulVersion != peid2->ulVersion)
		goto exit;

	if(peid1->usType != peid2->usType)
		goto exit;

	if(peid1->ulVersion == 0) {

		if(cbEntryID1 < sizeof(EID_V0))
			goto exit;

		if( ((EID_V0*)lpEntryID1)->ulId != ((EID_V0*)lpEntryID2)->ulId )
			goto exit;

	}else {
		if(cbEntryID1 < CbNewEID(""))
			goto exit;

		if(peid1->uniqueId != peid2->uniqueId) //comp. with the old ulId
			goto exit;
	}

	fTheSame = TRUE;

exit:
	if(lpulResult)
		*lpulResult = fTheSame;

	return hr;
}

#ifdef WIN32
HRESULT FirstFolderSync(HWND hWnd, IMSProvider *lpOffline, IMSProvider *lpOnline, ULONG cbEntryID, LPENTRYID lpEntryID, LPMAPISUP lpMAPISupport)
{
	HRESULT hr = hrSuccess;
	DWORD dwNetworkFlag = 0;

	IECSync* lpStoreSync = NULL;
	bool bRetryLogon;
	ULONG ulAction;
#if defined(WIN32) && !defined(WINCE)
	CProgressDlg	dlgProgress(hWnd);
#endif

	SyncProgressCallBack	lpFolderSyncCallBack = NULL;
	void*					lpCallObject = NULL;

	convert_context			converter;

	// Check is networkavailable
	do {
		bRetryLogon = false;
		
		if (IsNetworkAlive(&dwNetworkFlag) == FALSE) { // No network, can't sync
			ulAction = MessageBox(hWnd, _("Unable to work online! The first time you need a network connection. Retry if you have the network connection."), g_strProductName.c_str(), MB_RETRYCANCEL);
			if (ulAction == IDRETRY) {
				bRetryLogon = true;
			} else {
				hr = MAPI_E_NETWORK_ERROR;
				goto exit;
			}
		}
	} while (bRetryLogon);


	hr = CreateECSync(lpOffline, lpOnline, lpMAPISupport, cbEntryID, lpEntryID, &lpStoreSync);
	if(hr != hrSuccess)
		goto exit;

#if defined(WIN32) && !defined(WINCE)

	// Setup dialog
	dlgProgress.SetTitle(_("First Folder Sync..."));
	dlgProgress.SetCancelMsg(_("Cancel"));
	dlgProgress.SetCalculateTime(true);
	dlgProgress.SetAllowMinimize(false);
	dlgProgress.SetShowCancelButton(false);
	dlgProgress.SetLine(1, _("Sync folder items"));
	
	dlgProgress.ShowModal(true);

	// Callback
	lpCallObject = (void*)&dlgProgress;
	lpFolderSyncCallBack = (SyncProgressCallBack)CProgressDlg::WrapCallUpdateProgress;

#else
	lpCallObject = NULL;
	lpFolderSyncCallBack = NULL;
#endif

	// SYNC all folders
	hr = lpStoreSync->SetSyncProgressCallBack(lpCallObject, lpFolderSyncCallBack);
	if (hr != hrSuccess)
		goto exit;

	hr = lpStoreSync->FirstFolderSync();
	if(hr != hrSuccess) {
		// FIXME: give an error 
		goto exit;
	}

#if defined(WIN32) && !defined(WINCE)
	// Remove first sync dialog
	dlgProgress.EndDialog();
#endif

exit:
	if(lpStoreSync) {
	    /* 
		 * Make sure to Logoff lpStoreSync to make sure the internal Advice doesn't keep
		 * a reference (or actually three references) to the instance itself, which acts
		 * as the AdviseSink for the Advise. Failing to do so will cause lpStoreSync to
		 * not be destructed by the call to Release, which will cause it to call into
		 * dlgProgress (that is destructed) when new mail is delivered online.
		 */
		lpStoreSync->Logoff();
		lpStoreSync->Release();
	}

	return hr;
}

HRESULT FirstAddressBookSync(HWND hWnd, IABProvider *lpOffline, IABProvider *lpOnline, LPMAPISUP lpMAPISupport)
{
	// Check is networkavailable

	return MAPI_E_NO_SUPPORT;
}

#endif

#ifdef WIN32
BOOL StartServer(LPCTSTR lpKopanoDir, LPCTSTR lpDbConfigName, LPCTSTR lpDatabasePath, LPCTSTR lpUniqueId, LPPROCESS_INFORMATION lpProcInfo)
{
	BOOL bRunning = TRUE;
	STARTUPINFO siStartupInfo;
	DWORD dwCreationFlags;
	DWORD dwWait = 0;
	DWORD dwExitCode = 0;
	LPCTSTR lpEnvironment = NULL;
	LPCTSTR lpTmp;
	LPTSTR lpCmdLine = NULL;

	tstring strNewEnv, strCmdLine;

	ASSERT(lpProcInfo != NULL);

	memset(&siStartupInfo, 0, sizeof(siStartupInfo));
	memset(lpProcInfo, 0, sizeof *lpProcInfo);

	siStartupInfo.cb = sizeof(siStartupInfo);

	dwCreationFlags = CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS;
#ifdef UNICODE
	dwCreationFlags |= CREATE_UNICODE_ENVIRONMENT;
#endif
	
	// Get all current environment variables
	lpEnvironment = GetEnvironmentStrings();

	if (lpEnvironment) {
		lpTmp = lpEnvironment;
		while(true)
		{
			if (*lpTmp == '\0')
				break;

			int size = _tcslen(lpTmp);

			strNewEnv += lpTmp;
			strNewEnv.append(1, '\0');

			lpTmp += size + 1;
		}
	}

	// Add kopano environment variable
	strNewEnv += _T("KOPANO_DB_CONFIG_NAME=");
	strNewEnv += lpDbConfigName;
	strNewEnv.append(1, '\0');

	strNewEnv += _T("KOPANO_DB_PATH=");
	strNewEnv += lpDatabasePath;
	strNewEnv.append(1, '\0');

	strNewEnv += _T("KOPANO_UNIQUEID=");
	strNewEnv += lpUniqueId;
	strNewEnv.append(1, '\0');

	strCmdLine = lpKopanoDir;
	strCmdLine += _T("\\kopano-offline.exe");

	// CreateProcess can modify the commandline parameter, so create a temporary buffer.
	lpCmdLine = new TCHAR[strCmdLine.size() + 1];
	_tcscpy(lpCmdLine, strCmdLine.c_str());

	// Start kopano server process
	if (CreateProcess(NULL, lpCmdLine, NULL, NULL, FALSE, 
			dwCreationFlags, (LPVOID)strNewEnv.c_str(), lpKopanoDir, &siStartupInfo, lpProcInfo) == FALSE)
	{
		bRunning = FALSE;
		goto exit;
	}

exit:
	delete[] lpCmdLine;	// delete[] on NULL is allowed

	return bRunning;
}
#endif //#ifdef WIN32

// Get MAPI unique guid, guaranteed by MAPI to be unique for all profiles.
HRESULT GetMAPIUniqueProfileId(LPMAPISUP lpMAPISup, tstring* lpstrUniqueId)
{
	HRESULT			hr = hrSuccess;
	LPPROFSECT		lpProfSect = NULL;
	LPSPropValue	lpsPropValue = NULL;

	hr = lpMAPISup->OpenProfileSection((LPMAPIUID)&MUID_PROFILE_INSTANCE, 0, &lpProfSect);
	if(hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(lpProfSect, PR_SEARCH_KEY, &lpsPropValue);
	if(hr != hrSuccess)
		goto exit;

	*lpstrUniqueId = bin2hext(lpsPropValue->Value.bin.cb, lpsPropValue->Value.bin.lpb);
exit:
	MAPIFreeBuffer(lpsPropValue);
	if(lpProfSect)
		lpProfSect->Release();

	return hr;
}

HRESULT RemoveAllProviders(ECMapProvider* lpmapProvider)
{
	ECMapProvider::const_iterator iterProvider;
	
	if (lpmapProvider == NULL)
		return MAPI_E_INVALID_PARAMETER;

	for (iterProvider = lpmapProvider->begin();
	     iterProvider != lpmapProvider->end(); ++iterProvider) {
#ifdef HAVE_OFFLINE_SUPPORT
		if (iterProvider->second.lpMSProviderOffline)
			iterProvider->second.lpMSProviderOffline->Release();
#endif
		if (iterProvider->second.lpMSProviderOnline)
			iterProvider->second.lpMSProviderOnline->Release();

		if (iterProvider->second.lpABProviderOnline)
			iterProvider->second.lpABProviderOnline->Release();

#ifdef HAVE_OFFLINE_SUPPORT
		if (iterProvider->second.lpABProviderOffline)
			iterProvider->second.lpABProviderOffline->Release();
#endif
	}
	return hrSuccess;
}

HRESULT SetProviderMode(IMAPISupport *lpMAPISup, ECMapProvider* lpmapProvider, LPCSTR lpszProfileName, ULONG ulConnectType)
{
	HRESULT hr = hrSuccess;
#ifdef HAVE_OFFLINE_SUPPORT
	ECMapProvider::const_iterator iterProvider;
	SPropValue sProps;
	LPPROFSECT lpProfSect = NULL;

	if (lpmapProvider == NULL || lpszProfileName == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if ( (ulConnectType &~(CT_UNSPECIFIED|CT_ONLINE|CT_OFFLINE)) != 0 ) {
		hr = MAPI_E_UNKNOWN_FLAGS;
		goto exit;
	}

	
	iterProvider = lpmapProvider->find(lpszProfileName);
	if (iterProvider == lpmapProvider->end()) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	iterProvider->second.ulConnectType = ulConnectType;

	// We also save the connection type in the global profile section. This information can be used later
	// by other processes which are not interactive (ie they use MDB_NO_DIALOG) when they log on in autodetect
	// mode. This means that you can only switch between using the online or offline mode when in 'autodetect' mode
	// by using an interactive logon.

	hr = lpMAPISup->OpenProfileSection((LPMAPIUID)pbGlobalProfileSectionGuid, MAPI_MODIFY, &lpProfSect);
	if(hr != hrSuccess)
		goto exit;

	sProps.ulPropTag = PR_EC_LAST_CONNECTIONTYPE;
	sProps.Value.ul = ulConnectType;

	hr = lpProfSect->SetProps(1, &sProps, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpProfSect->SaveChanges(0);
	if(hr != hrSuccess)
		goto exit;

exit:
	if(lpProfSect)
		lpProfSect->Release();
#endif

	return hr;
}

HRESULT GetLastConnectionType(IMAPISupport *lpMAPISup, ULONG *lpulType) {
	HRESULT hr = hrSuccess;
#ifdef HAVE_OFFLINE_SUPPORT
	LPPROFSECT lpProfSect = NULL;
	LPSPropValue lpProp = NULL;

	hr = lpMAPISup->OpenProfileSection((LPMAPIUID)&pbGlobalProfileSectionGuid, MAPI_MODIFY, &lpProfSect);
	if(hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(lpProfSect, PR_EC_LAST_CONNECTIONTYPE, &lpProp);
	if(hr != hrSuccess)
		goto exit;

	if(lpulType)
		*lpulType = lpProp->Value.ul;

exit:
	MAPIFreeBuffer(lpProp);
	if(lpProfSect)
		lpProfSect->Release();
#else
	*lpulType = CT_ONLINE;
#endif

	return hr;
}

HRESULT GetProviders(ECMapProvider* lpmapProvider, IMAPISupport *lpMAPISup, const char *lpszProfileName, ULONG ulFlags, PROVIDER_INFO* lpsProviderInfo)
{
	HRESULT hr = hrSuccess;
	ECMapProvider::const_iterator iterProvider;
	PROVIDER_INFO sProviderInfo;
	ECMSProvider *lpECMSProvider = NULL;
	ECABProvider *lpECABProvider = NULL;
#ifdef HAVE_OFFLINE_SUPPORT
	ECMSProviderOffline *lpECMSProviderOffline = NULL;
	ECABProviderOffline *lpECABProviderOffline = NULL;
#endif
	sGlobalProfileProps	sProfileProps;

	if (lpmapProvider == NULL || lpMAPISup == NULL || lpszProfileName == NULL || lpsProviderInfo == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	iterProvider = lpmapProvider->find(lpszProfileName);
	if (iterProvider != lpmapProvider->end())
	{
		*lpsProviderInfo = iterProvider->second;
		goto exit;
	}
		
	// Get the username and password from the profile settings
	hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if(hr != hrSuccess)
		goto exit;

	//////////////////////////////////////////////////////
	// Init providers

	// Message store online
	hr = ECMSProvider::Create(ulFlags, &lpECMSProvider);
	if(hr != hrSuccess)
		goto exit;

	// Addressbook online
	hr = ECABProvider::Create(&lpECABProvider);
	if(hr != hrSuccess)
		goto exit;

#ifdef HAVE_OFFLINE_SUPPORT
	// Message store offline
	hr = ECMSProviderOffline::Create(ulFlags, &lpECMSProviderOffline);
	if(hr != hrSuccess)
		goto exit;

	// Addressbook offline
	hr = ECABProviderOffline::Create(&lpECABProviderOffline);
	if(hr != hrSuccess)
		goto exit;
#endif

	//////////////////////////////////////////////////////
	// Fill in the Provider info struct
	
	//Init only the firsttime the flags
	sProviderInfo.ulProfileFlags = sProfileProps.ulProfileFlags;

#ifdef HAVE_OFFLINE_SUPPORT
	sProviderInfo.ulConnectType = CT_UNSPECIFIED; //Default start with CT_UNSPECIFIED this will change
#else
	sProviderInfo.ulConnectType = CT_ONLINE;
#endif

	hr = lpECMSProvider->QueryInterface(IID_IMSProvider, (void **)&sProviderInfo.lpMSProviderOnline);
	if(hr != hrSuccess)
		goto exit;

	hr = lpECABProvider->QueryInterface(IID_IABProvider, (void **)&sProviderInfo.lpABProviderOnline);
	if(hr != hrSuccess)
		goto exit;

#ifdef HAVE_OFFLINE_SUPPORT
	hr = lpECMSProviderOffline->QueryInterface(IID_IMSProvider, (void **)&sProviderInfo.lpMSProviderOffline);
	if(hr != hrSuccess)
		goto exit;

	hr = lpECABProviderOffline->QueryInterface(IID_IABProvider, (void **)&sProviderInfo.lpABProviderOffline);
	if(hr != hrSuccess)
		goto exit;
#endif
	

	//Add provider in map
	lpmapProvider->insert(std::map<string, PROVIDER_INFO>::value_type(lpszProfileName, sProviderInfo));

	*lpsProviderInfo = sProviderInfo;

exit:
	if (lpECMSProvider)
		lpECMSProvider->Release();

	if (lpECABProvider)
		lpECABProvider->Release();

#ifdef HAVE_OFFLINE_SUPPORT
	if (lpECMSProviderOffline)
		lpECMSProviderOffline->Release();

	if (lpECABProviderOffline)
		lpECABProviderOffline->Release();
#endif

	return hr;
}

// Create an anonymous message store, linked to transport and support object
//
// NOTE
//  Outlook will stay 'alive' when the user shuts down until the support
//  object is released, so we have to make sure that when the users has released
//  all the msgstore objects, we also release the support object.
//
HRESULT CreateMsgStoreObject(char * lpszProfname, LPMAPISUP lpMAPISup, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulMsgFlags, ULONG ulProfileFlags, WSTransport* lpTransport,
							MAPIUID* lpguidMDBProvider, BOOL bSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore,
							ECMsgStore** lppECMsgStore)
{
	HRESULT	hr = hrSuccess;
	
	BOOL fModify = FALSE;

	ECMsgStore *lpMsgStore = NULL;
	IECPropStorage *lpStorage = NULL;
		

	fModify = ulMsgFlags & MDB_WRITE || ulMsgFlags & MAPI_BEST_ACCESS; // FIXME check access at server

	if (CompareMDBProvider(lpguidMDBProvider, &KOPANO_STORE_PUBLIC_GUID) == TRUE)
		hr = ECMsgStorePublic::Create(lpszProfname, lpMAPISup, lpTransport, fModify, ulProfileFlags, bSpooler, bOfflineStore, &lpMsgStore);
	else if (CompareMDBProvider(lpguidMDBProvider, &KOPANO_STORE_ARCHIVE_GUID) == TRUE)
		hr = ECMsgStore::Create(lpszProfname, lpMAPISup, lpTransport, fModify, ulProfileFlags, bSpooler, FALSE, bOfflineStore, &lpMsgStore);
	else
		hr = ECArchiveAwareMsgStore::Create(lpszProfname, lpMAPISup, lpTransport, fModify, ulProfileFlags, bSpooler, fIsDefaultStore, bOfflineStore, &lpMsgStore);

	if (hr != hrSuccess)
		goto exit;

	memcpy(&lpMsgStore->m_guidMDB_Provider, lpguidMDBProvider,sizeof(MAPIUID));

	// Get a propstorage for the message store
	hr = lpTransport->HrOpenPropStorage(0, NULL, cbEntryID, lpEntryID, 0, &lpStorage);
	if (hr != hrSuccess)
		goto exit;

	// Set up the message store to use this storage
	hr = lpMsgStore->HrSetPropStorage(lpStorage, FALSE);
	if (hr != hrSuccess)
		goto exit;

	// Setup callback for session change
	hr = lpTransport->AddSessionReloadCallback(lpMsgStore, ECMsgStore::Reload, NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMsgStore->SetEntryId(cbEntryID, lpEntryID);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMsgStore->QueryInterface(IID_ECMsgStore, (void **)lppECMsgStore);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (lpMsgStore)
		lpMsgStore->Release();

	if(lpStorage)
		lpStorage->Release();

	return hr;
}

#ifdef HAVE_OFFLINE_SUPPORT
HRESULT GetOfflineServerURL(IMAPISupport *lpMAPISup, std::string *lpstrServerURL, tstring *lpstrUniqueId)
{
	HRESULT hr;
	tstring strServerPath;
	tstring strUniqueId;

	if (lpMAPISup == NULL || lpstrServerURL == NULL)
		return MAPI_E_INVALID_PARAMETER;

	//for windows: file://\\.\pipe\kopano-ID
	//for linux: file:///tmp/kopano-ID
#ifdef WIN32
	strServerPath = _T("file://\\\\.\\pipe\\kopano-");
#else
	strServerPath = _T("file:///tmp/kopano-");
#endif

	hr = GetMAPIUniqueProfileId(lpMAPISup, &strUniqueId);
	if(hr != hrSuccess)
		return hr;

	*lpstrServerURL = convert_to<std::string>(strServerPath + strUniqueId);

	if (lpstrUniqueId)
		*lpstrUniqueId = strUniqueId;
	return hrSuccess;
}
#endif

#ifdef HAVE_OFFLINE_SUPPORT
HRESULT CheckStartServerAndGetServerURL(IMAPISupport *lpMAPISup, LPCTSTR lpszUserLocalAppDataKopano, LPCTSTR lpszKopanoDirectory, std::string *lpstrServerURL)
{
	HRESULT hr = hrSuccess;
	string strServerPath;
	tstring strUniqueId;
	tstring strDBDirectory;
	tstring strDBConfigFile;

#ifdef WIN32
	PROCESS_INFORMATION piProcInfo = {0};
#endif

	if (lpMAPISup == NULL || lpstrServerURL == NULL || lpszUserLocalAppDataKopano == NULL || lpszKopanoDirectory == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = GetOfflineServerURL(lpMAPISup, &strServerPath, &strUniqueId);
	if(hr != hrSuccess)
		goto exit;

#ifdef WIN32
	// check if kopano server is running
	if (WaitNamedPipeA(strServerPath.c_str() + 7, NMPWAIT_USE_DEFAULT_WAIT) == FALSE && GetLastError() != ERROR_PIPE_BUSY) {

		strDBDirectory = lpszUserLocalAppDataKopano;
		strDBDirectory+= _T("\\offlinedata\\");

		strDBConfigFile = lpszKopanoDirectory;
		strDBConfigFile+= _T("\\MySQL\\My.ini");

		const path pathUpgradeFile = path(strDBDirectory) / path(strUniqueId + _T(".upgrading"));
	
		// Check if the offline server isn't upgrading the database.
		if (bfs::exists(pathUpgradeFile) && ::_unlink(pathUpgradeFile.string().c_str()) == -1) {
			hr = MAPI_E_BUSY;
			goto exit;
		}

		if (StartServer(lpszKopanoDirectory, strDBConfigFile.c_str(), strDBDirectory.c_str(), strUniqueId.c_str(), &piProcInfo) == FALSE) {
			hr = MAPI_E_FAILONEPROVIDER;
			goto exit;
		}

        // Check 25 times with a 1 second interval if the named pipe is availble or the process has exit.
		hr = MAPI_E_FAILONEPROVIDER;
		for (int wait = 0; wait < 25; ++wait) {
			DWORD dwExitCode = 0;
			if (GetExitCodeProcess(piProcInfo.hProcess, &dwExitCode) == FALSE || dwExitCode != STILL_ACTIVE)
				goto exit;

			// Check if the offline server isn't upgrading the database.
			if (bfs::exists(pathUpgradeFile)) {
				hr = MAPI_E_BUSY;
				goto exit;
			}

			// The named pipe is available whenever WaitNamedPipe returns TRUE (meaning it's ready to be connected) or 
			// when GetLastError() returns ERROR_PIPE_BUSY (meaning that it exists but is busy).
			if (WaitNamedPipeA(strServerPath.c_str() + 7, NMPWAIT_USE_DEFAULT_WAIT) == TRUE || GetLastError() == ERROR_PIPE_BUSY) {
				hr = hrSuccess;
				break;
			}

			// WaitNamedPipe doesn't block if the named pipe doesn't exist. So it will only block if the pipe does exist, which
			// is all we're interested in. So in our loop it will never block. Therefore we'll just Sleep for a second.
			Sleep(1000);
		}
	}

#else
	hr = MAPI_E_FAILONEPROVIDER;
	// TODO: Linux support
#endif

	if(hr != hrSuccess)
		goto exit;

	*lpstrServerURL = strServerPath;
exit:

#ifdef WIN32
	if (piProcInfo.hProcess)
		CloseHandle(piProcInfo.hProcess);
	if (piProcInfo.hThread)
		CloseHandle(piProcInfo.hThread);
#endif

	return hr;
}
#endif

HRESULT GetTransportToNamedServer(WSTransport *lpTransport, LPCTSTR lpszServerName, ULONG ulFlags, WSTransport **lppTransport)
{
	HRESULT hr;
	utf8string strServerName;
	utf8string strPseudoUrl = utf8string::from_string("pseudo://");
	char *lpszServerPath = NULL;
	bool bIsPeer = false;
	WSTransport *lpNewTransport = NULL;

	if (lpszServerName == NULL || lpTransport == NULL || lppTransport == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if ((ulFlags & ~MAPI_UNICODE) != 0)
		return MAPI_E_UNKNOWN_FLAGS;

	strServerName = convstring(lpszServerName, ulFlags);
	strPseudoUrl.append(strServerName);
	hr = lpTransport->HrResolvePseudoUrl(strPseudoUrl.c_str(), &lpszServerPath, &bIsPeer);
	if (hr != hrSuccess)
		return hr;

	if (bIsPeer) {
		lpNewTransport = lpTransport;
		lpNewTransport->AddRef();
	} else {
		hr = lpTransport->CreateAndLogonAlternate(lpszServerPath, &lpNewTransport);
		if (hr != hrSuccess)
			return hr;
	}

	*lppTransport = lpNewTransport;
	return hrSuccess;
}
