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

#include "ECMSProviderSwitch.h"
#include "ECMSProvider.h"
#include "ECOfflineState.h"

#include <kopano/ECGuid.h>

#include <kopano/Trace.h>
#include <kopano/ECDebug.h>

#include <edkguid.h>
#include "EntryPoint.h"
#include "DLLGlobal.h"
#include <edkmdb.h>
#include <kopano/mapiext.h>

#include "ClientUtil.h"
#include "ECMsgStore.h"
#include <kopano/stringutil.h>

#ifdef WIN32
#include <Sensapi.h>
#include "PasswordDlg.h"
#endif
#include <csignal>

#include "ProviderUtil.h"

#include <kopano/charset/convstring.h>

#ifdef swprintf
	#undef swprintf
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

ECMSProviderSwitch::ECMSProviderSwitch(ULONG ulFlags) : ECUnknown("ECMSProviderSwitch")
{
	m_ulFlags = ulFlags;
}

ECMSProviderSwitch::~ECMSProviderSwitch(void)
{
}

HRESULT ECMSProviderSwitch::Create(ULONG ulFlags, ECMSProviderSwitch **lppMSProvider)
{
	ECMSProviderSwitch *lpMSProvider = new ECMSProviderSwitch(ulFlags);

	return lpMSProvider->QueryInterface(IID_ECUnknown/*IID_ECMSProviderSwitch*/, (void **)lppMSProvider);
}

HRESULT ECMSProviderSwitch::QueryInterface(REFIID refiid, void **lppInterface)
{
	/*refiid == IID_ECMSProviderSwitch */
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IMSProvider, &this->m_xMSProvider);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xMSProvider);

	REGISTER_INTERFACE(IID_ISelectUnicode, &this->m_xUnknown);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

ULONG ECMSProviderSwitch::Release()
{
	return ECUnknown::Release();
}

HRESULT ECMSProviderSwitch::Shutdown(ULONG * lpulFlags) 
{
	HRESULT hr = hrSuccess;

	//FIXME
	return hr;
}

HRESULT ECMSProviderSwitch::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity, LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	HRESULT			hr = hrSuccess;
	ECMsgStore*		lpecMDB = NULL;

	sGlobalProfileProps	sProfileProps;
	LPPROFSECT		lpProfSect = NULL;
	LPSPropValue	lpsPropArray = NULL;
	LPSPropTagArray	lpsPropTagArray = NULL;
	ULONG			cValues = 0;

	char*			lpDisplayName = NULL;
	LPSPropValue	lpProp = NULL;
	bool			bIsDefaultStore = false;
	LPSPropValue	lpIdentityProps = NULL;

	LPMDB			lpMDB = NULL;
	LPMSLOGON		lpMSLogon = NULL;

	PROVIDER_INFO sProviderInfo;
	ULONG ulConnectType = CT_UNSPECIFIED;
	IMSProvider *lpOnline = NULL;
	convert_context converter;
	LPENTRYID		lpStoreID = NULL;
	ULONG			cbStoreID = 0;

	convstring			tstrProfileName(lpszProfileName, ulFlags);

#ifdef HAVE_OFFLINE_SUPPORT
	int				ulAction = 0;
	BOOL			bFirstSync = FALSE;

	LPMSLOGON		lpMSLogonOffline = NULL;
	LPMDB			lpMDBOffline = NULL;
	DWORD			dwNetworkFlag = 0;

	IMSProvider *lpOffline = NULL;
	bool bRetryLogon;
	IUnknown *lpTmpStream = NULL;
#endif

#if defined(WIN32) && !defined(WINCE)
	if (ulUIParam == 0)
		ulUIParam = (ULONG)GetWindow(GetDesktopWindow(), GW_CHILD);
#endif

	// Get the username and password from the profile settings
	hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if (hr != hrSuccess)
		goto exit;

	// Open profile settings
	hr = lpMAPISup->OpenProfileSection(NULL, MAPI_MODIFY, &lpProfSect);
	if (hr != hrSuccess)
		goto exit;

	if (lpEntryID == NULL) {

		// Try to initialize the provider
		if (InitializeProvider(NULL, lpProfSect, sProfileProps, &cbStoreID, &lpStoreID) != hrSuccess) {
			hr = MAPI_E_UNCONFIGURED;
			goto exit;
		}

		lpEntryID = lpStoreID;
		cbEntryID = cbStoreID;
	}

	cValues = 1;
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(cValues), (void **)&lpsPropTagArray);
	if (hr != hrSuccess)
		goto exit;

	lpsPropTagArray->cValues = 1;
	lpsPropTagArray->aulPropTag[0] = PR_MDB_PROVIDER;
	
	hr = lpProfSect->GetProps(lpsPropTagArray, 0, &cValues, &lpsPropArray);
	if (hr == hrSuccess)
	{
		if (lpsPropArray[0].ulPropTag == PR_MDB_PROVIDER){
			if (CompareMDBProvider(lpsPropArray[0].Value.bin.lpb, &KOPANO_SERVICE_GUID) ||
				CompareMDBProvider(lpsPropArray[0].Value.bin.lpb, &MSEMS_SERVICE_GUID)) {
				bIsDefaultStore = true;
			}
		}
	}
	hr = hrSuccess;

	hr = GetProviders(&g_mapProviders, lpMAPISup, tstrProfileName.c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		goto exit;

	hr = sProviderInfo.lpMSProviderOnline->QueryInterface(IID_IMSProvider, (void **)&lpOnline);
	if (hr != hrSuccess)
		goto exit;

#ifdef HAVE_OFFLINE_SUPPORT
	hr = sProviderInfo.lpMSProviderOffline->QueryInterface(IID_IMSProvider, (void **)&lpOffline);
	if (hr != hrSuccess)
		goto exit;
#endif

#if defined(WIN32) && !defined(WINCE)
relogin:
#endif
	// Default error
	hr = MAPI_E_LOGON_FAILED; //or MAPI_E_FAILONEPROVIDER

	// Connect online if any of the following is true:
	// - Online store was specifically requested (MDB_ONLINE)
	// - Profile is not offline capable
	// - Store being opened is not the user's default store

	if ((ulFlags & MDB_ONLINE) == MDB_ONLINE || (sProviderInfo.ulProfileFlags&EC_PROFILE_FLAGS_OFFLINE) != EC_PROFILE_FLAGS_OFFLINE || bIsDefaultStore == false)
	{
#ifdef HAVE_OFFLINE_SUPPORT
		ECOfflineState::OFFLINESTATE state;

		if(sProviderInfo.ulProfileFlags & EC_PROFILE_FLAGS_OFFLINE) {
			// If the profile is offline-capable, check offline state
			if(ECOfflineState::GetOfflineState(tstrProfileName, &state) == hrSuccess && state == ECOfflineState::OFFLINESTATE_OFFLINE) {
				// Deny logon to online store if 'working offline'
				hr = MAPI_E_FAILONEPROVIDER;
				goto exit;
			}
		}
#endif
		bool fDone = false;

		while(!fDone) {
			hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags, lpInterface, NULL, NULL, NULL, &lpMSLogon, &lpMDB);
			ulConnectType = CT_ONLINE;
#ifdef HAVE_OFFLINE_SUPPORT
			if(hr == MAPI_E_NETWORK_ERROR && sProviderInfo.ulProfileFlags&EC_PROFILE_FLAGS_OFFLINE) {
#ifdef WIN32
				if(ulFlags & MDB_NO_DIALOG) {
#endif
					// If no dialog is allowed, do the same as when pressing 'continue', ie work offline
					ECOfflineState::SetOfflineState(tstrProfileName, ECOfflineState::OFFLINESTATE_OFFLINE);
					fDone = true;
#ifdef WIN32
				} else {
					// An offline profile is in use, and we were unable to connect to an online store. Prompt the user for what to do
					// The user can choose 'Retry', which will retry the connect, 'Cancel' which will cancel the current request, and 'Continue'
					// in which case all subsequent online requests should fail (work offline).
					switch(MessageBox((HWND)ulUIParam, _("Unable to connect to the online server. If you continue, no new connections will be attempted until you disable the 'Work offline' option in the 'File' menu."), g_strProductName.c_str(), MB_ICONQUESTION | MB_CANCELTRYCONTINUE)) {
						case IDCANCEL:
							fDone = true;
							// break loop with error
							break;
						case IDTRYAGAIN:
							// just loop around again
							break;
						case IDCONTINUE:
							ECOfflineState::SetOfflineState(tstrProfileName, ECOfflineState::OFFLINESTATE_OFFLINE);
							fDone = true;
							// break loop with error
							break;
					}
				}
#endif
			} else
#endif //offline
			{
				fDone = true;
			}
		}
	}
#ifdef HAVE_OFFLINE_SUPPORT
	// Offline provider
	else {
#ifdef WIN32
		//Logon on the offline store
		hr = lpOffline->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags, lpInterface, NULL, NULL, NULL, &lpMSLogonOffline, &lpMDBOffline);
		if (hr == MAPI_E_BUSY) {
			bool bTryOnline = (sProviderInfo.ulConnectType != CT_OFFLINE);

			// The offline server is performing an upgrade that takes too long for a
			// user to comfortably wait for.
			// We'll just work online this session if the user wants to
			if (sProviderInfo.ulConnectType == CT_UNSPECIFIED && (ulFlags & MDB_NO_DIALOG) == 0) {
				MessageBox((HWND)ulUIParam, _("Your offline cache database is being upgraded. Outlook will start in online mode for this session."), g_strProductName.c_str(), MB_ICONEXCLAMATION | MB_OK);
				bTryOnline = true;
			}

			if (bTryOnline)
				hr = Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags | MDB_ONLINE, lpInterface, lpcbSpoolSecurity, lppbSpoolSecurity, lppMAPIError, lppMSLogon, lppMDB);
			else
				hr = MAPI_E_LOGON_FAILED;

			goto exit;	// In any case we're done
		}
		else if (hr != hrSuccess)
			goto exit;

		// Check if this is the first time to logon
		hr = lpMDBOffline->OpenProperty(PR_EC_OFFLINE_SYNC_STATUS, &IID_IStream, 0, 0, (IUnknown**)&lpTmpStream);
		if (hr == MAPI_E_NOT_FOUND)
			bFirstSync = true;

		if (lpTmpStream) { lpTmpStream->Release(); lpTmpStream = NULL; }

		hr = hrSuccess; // Reset hr

		
		if (bFirstSync)
		{
			hr = FirstFolderSync((HWND)ulUIParam, lpOffline, lpOnline, cbEntryID, lpEntryID, lpMAPISup);
			if (hr != hrSuccess)
				goto exit;
		}

		// Login with offline cached mode
		if ( (sProviderInfo.ulProfileFlags&(EC_PROFILE_FLAGS_OFFLINE|EC_PROFILE_FLAGS_CACHE_PRIVATE)) == (EC_PROFILE_FLAGS_OFFLINE|EC_PROFILE_FLAGS_CACHE_PRIVATE))
		{

			if (bFirstSync)
			{
				// Ok, reload the store, to get the last properties (a bug in the store)
				lpMSLogonOffline->Release(); 
				lpMDBOffline->Release(); 
				lpMSLogonOffline = NULL;
				lpMDBOffline = NULL;

				hr = lpOffline->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags, lpInterface, NULL, NULL, NULL, &lpMSLogonOffline, &lpMDBOffline);
				if (hr != hrSuccess)
					goto exit;
			}

			lpMSLogon = lpMSLogonOffline;
			lpMDB = lpMDBOffline;

			lpMSLogonOffline = NULL;
			lpMDBOffline = NULL;
			ulConnectType = CT_OFFLINE;

		} else if ( (sProviderInfo.ulProfileFlags&EC_PROFILE_FLAGS_OFFLINE) == EC_PROFILE_FLAGS_OFFLINE ) {

			// login with online/offline mode
			if (bFirstSync)
			{
				// Logoff offline store, not needed
				lpMSLogonOffline->Release();
				lpMDBOffline->Release();
				lpMSLogonOffline = NULL;
				lpMDBOffline = NULL;
			}

			if (ulFlags & MDB_NO_DIALOG && sProviderInfo.ulConnectType == CT_UNSPECIFIED) {
				// This is a non-interactive logon with autodetect mode. Our behaviour is to follow
				// whatever the user has last chosen in an interactive login session (pressing the button 'continue'
				// for example, selecting offline mode. This causes MAPISP32.exe to follow whatever the user has chosen.

				if(GetLastConnectionType(lpMAPISup, &ulConnectType) == erSuccess) {
					sProviderInfo.ulConnectType = ulConnectType;
				}
			}

			if (bFirstSync == FALSE && sProviderInfo.ulConnectType == CT_OFFLINE) {
				// Autodetect mode with connection type == CT_OFFLINE
				lpMSLogon = lpMSLogonOffline;
				lpMDB = lpMDBOffline;

				lpMSLogonOffline = NULL;
				lpMDBOffline = NULL;
				ulConnectType = CT_OFFLINE;
			}else if (bFirstSync == FALSE && sProviderInfo.ulConnectType == CT_ONLINE) {
				// Autodetect mode with connection type == CT_ONLINE
				hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags, lpInterface, NULL, NULL, NULL, &lpMSLogon, &lpMDB);
				ulConnectType = CT_ONLINE;
			}else {
				// Autodetect mode with connection type == CT_UNSPECIFIED
				do {
					bRetryLogon = false;
					ulConnectType = CT_ONLINE;

					if (IsNetworkAlive(&dwNetworkFlag) == TRUE) {
						hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags, lpInterface, NULL, NULL, NULL, &lpMSLogon, &lpMDB);
					}else {
						hr = MAPI_E_NETWORK_ERROR;
					}
					
					if(hr != hrSuccess && ulFlags & MDB_NO_DIALOG) {
						goto exit;
					}else if (hr == MAPI_E_NETWORK_ERROR) {
						hr = hrSuccess;

						// Question work online / offline
						ulAction = MessageBox((HWND)ulUIParam, _("Unable to work online! Do you want to switch to offline mode?"), g_strProductName.c_str(), MB_CANCELTRYCONTINUE);
						switch(ulAction)
						{
							case IDCONTINUE: // Use offline store
								lpMSLogon = lpMSLogonOffline;
								lpMDB = lpMDBOffline;

								lpMSLogonOffline = NULL;
								lpMDBOffline = NULL;

								ulConnectType = CT_OFFLINE;
								
								// Set 'work offline' mode, since we know that the online server is not available now
								ECOfflineState::SetOfflineState(tstrProfileName, ECOfflineState::OFFLINESTATE_OFFLINE);

								break;
							case IDTRYAGAIN:
								bRetryLogon = true;
								break;
							case IDCANCEL:
							default:
								hr = MAPI_E_NETWORK_ERROR;
								break;
						}// switch(ulAction)
					} // if (hr == MAPI_E_NETWORK_ERROR)
					
					// INFO: hr can be an error, check on a other place
				}while (bRetryLogon);

			} //if (bFirstSync == FALSE && sProviderInfo.bOnline == FALSE) {
		}
#else
		// TODO: Linux support
#endif
	}
#endif	// HAVE_OFFLINE_SUPPORT

	// Set the provider in the right connection type
	if (bIsDefaultStore) {
		if (SetProviderMode(lpMAPISup, &g_mapProviders, tstrProfileName.c_str(), ulConnectType) != hrSuccess) {
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
	}

	if(hr != hrSuccess) {
		if(ulFlags & MDB_NO_DIALOG) {
			hr = MAPI_E_FAILONEPROVIDER;
			goto exit;
		} else if(hr == MAPI_E_NETWORK_ERROR) {
			hr = MAPI_E_FAILONEPROVIDER; //for disable public folders, so you can work offline
			goto exit;
		} else if (hr == MAPI_E_LOGON_FAILED) {
#if defined(WIN32) && !defined(WINCE)
			//MessageBox((HWND)ulUIParam, _("Incorrect username and/or password."), g_strProductName.c_str(), MB_OK | MB_ICONEXCLAMATION );

			// Use this dll for resource stuff
			AFX_MANAGE_STATE(AfxGetStaticModuleState());

			CWnd *lpParentWnd = CWnd::FromHandle((HWND)ulUIParam); 
			CPasswordDlg pwdDlg(lpParentWnd, lpMAPISup);

			if (pwdDlg.DoModal() == IDOK && pwdDlg.Save() == hrSuccess) {
				goto relogin;
			}
			else {
				hr = MAPI_E_LOGON_FAILED;
				goto exit;
			}
#else
			hr = MAPI_E_UNCONFIGURED; // Linux error ??//
			//hr = MAPI_E_LOGON_FAILED;
			goto exit;
#endif
		}else{
			hr = MAPI_E_LOGON_FAILED;
			goto exit;
		}
	}
	

	hr = lpMDB->QueryInterface(IID_ECMsgStore, (void **)&lpecMDB);
	if (hr != hrSuccess)
		goto exit;

	// Register ourselves with mapisupport
	hr = lpMAPISup->SetProviderUID((MAPIUID *)&lpecMDB->GetStoreGuid(), 0); 
	if (hr != hrSuccess)
		goto exit;

	// Set profile identity
	hr = ClientUtil::HrSetIdentity(lpecMDB->lpTransport, lpMAPISup, &lpIdentityProps);
	if (hr != hrSuccess)
		goto exit;

	// Get store name
	// The server will return MAPI_E_UNCONFIGURED when an attempt is made to open a store
	// that does not exist on that server. However the store is only opened the first time
	// when it's actually needed.
	// Since this is the first call that actually needs information from the store, we need
	// to be prepared to handle the MAPI_E_UNCONFIGURED error as we want to propagate this
	// up to the caller so this 'error' can be resolved by reconfiguring the profile.
	hr = HrGetOneProp(lpMDB, PR_DISPLAY_NAME_A, &lpProp);
	if (hr == MAPI_E_UNCONFIGURED)
		goto exit;
	if (hr != hrSuccess || lpProp->ulPropTag != PR_DISPLAY_NAME_A) {
		lpDisplayName = _A("Unknown");
		hr = hrSuccess;
	} else {
		lpDisplayName = lpProp->Value.lpszA;
	}

	if (CompareMDBProvider(&lpecMDB->m_guidMDB_Provider, &KOPANO_SERVICE_GUID) ||
		CompareMDBProvider(&lpecMDB->m_guidMDB_Provider, &KOPANO_STORE_DELEGATE_GUID))
	{
		hr = ClientUtil::HrInitializeStatusRow(lpDisplayName, MAPI_STORE_PROVIDER, lpMAPISup, lpIdentityProps, 0);
		if (hr != hrSuccess)
			goto exit;
	}

	if (lppMSLogon) {
		hr = lpMSLogon->QueryInterface(IID_IMSLogon, (void **)lppMSLogon);
		if (hr != hrSuccess)
			goto exit;
	}
	
	if (lppMDB) {
		hr = lpMDB->QueryInterface(IID_IMsgStore, (void **)lppMDB);
		if (hr != hrSuccess)
			goto exit;
	}

	// Store username and password so SpoolerLogon can log on to the profile
	if(lppbSpoolSecurity)
	{
		ULONG cbSpoolSecurity = sizeof(wchar_t) * (sProfileProps.strUserName.length() + sProfileProps.strPassword.length() + 1 + 1);

		hr = MAPIAllocateBuffer(cbSpoolSecurity, (void **)lppbSpoolSecurity);
		if(hr != hrSuccess)
			goto exit;

		swprintf((wchar_t*)*lppbSpoolSecurity, cbSpoolSecurity, L"%s%c%s", sProfileProps.strUserName.c_str(), 0, sProfileProps.strPassword.c_str());
		*lpcbSpoolSecurity = cbSpoolSecurity;
	}


exit:
	if (lppMAPIError)
		*lppMAPIError = NULL;
	MAPIFreeBuffer(lpsPropTagArray);
	MAPIFreeBuffer(lpsPropArray);
	MAPIFreeBuffer(lpProp);
	if (lpProfSect)
		lpProfSect->Release();

#ifdef HAVE_OFFLINE_SUPPORT
	if (lpMSLogonOffline)
		lpMSLogonOffline->Release();

	if (lpMDBOffline)
		lpMDBOffline->Release();
#endif
	
	if (lpMSLogon)
		lpMSLogon->Release();
	
	if (lpMDB)
		lpMDB->Release();

	if (lpecMDB)
		lpecMDB->Release();
    
	if (lpOnline)
		lpOnline->Release();

#ifdef HAVE_OFFLINE_SUPPORT
	if (lpOffline)
		lpOffline->Release();

	if (lpTmpStream)
		lpTmpStream->Release();
#endif
	MAPIFreeBuffer(lpIdentityProps);
	MAPIFreeBuffer(lpStoreID);
	return hr;
}

HRESULT ECMSProviderSwitch::SpoolerLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG cbSpoolSecurity, LPBYTE lpbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	HRESULT hr = hrSuccess;
	IMSProvider *lpProvider = NULL; // Do not release
	PROVIDER_INFO sProviderInfo;
	LPMDB lpMDB = NULL;
	LPMSLOGON lpMSLogon = NULL;
	ECMsgStore *lpecMDB = NULL;

	if (lpEntryID == NULL) {
		hr = MAPI_E_UNCONFIGURED;
		goto exit;
	}
	
	if (cbSpoolSecurity == 0 || lpbSpoolSecurity == NULL) {
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	hr = GetProviders(&g_mapProviders, lpMAPISup, convstring(lpszProfileName, ulFlags).c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		goto exit;

#ifdef HAVE_OFFLINE_SUPPORT
	ASSERT(sProviderInfo.ulConnectType != CT_UNSPECIFIED);
	if (sProviderInfo.ulConnectType == CT_OFFLINE)
		lpProvider = sProviderInfo.lpMSProviderOffline;
	else // all other types
#endif
		lpProvider = sProviderInfo.lpMSProviderOnline;

	hr = lpProvider->SpoolerLogon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID, ulFlags, lpInterface, cbSpoolSecurity, lpbSpoolSecurity, NULL, &lpMSLogon, &lpMDB);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMDB->QueryInterface(IID_ECMsgStore, (void **)&lpecMDB);
	if (hr != hrSuccess)
		goto exit;

	// Register ourselves with mapisupport
	hr = lpMAPISup->SetProviderUID((MAPIUID *)&lpecMDB->GetStoreGuid(), 0); 
	if (hr != hrSuccess)
		goto exit;


	if (lppMSLogon) {
		hr = lpMSLogon->QueryInterface(IID_IMSLogon, (void **)lppMSLogon);
		if (hr != hrSuccess)
			goto exit;
	}
	
	if (lppMDB) {
		hr = lpMDB->QueryInterface(IID_IMsgStore, (void **)lppMDB);
		if (hr != hrSuccess)
			goto exit;
	}


exit:
	if (lppMAPIError)
		*lppMAPIError = NULL;

	if (lpecMDB)
		lpecMDB->Release();

	if (lpMSLogon)
		lpMSLogon->Release();
	
	if (lpMDB)
		lpMDB->Release();

	return hr;
}
	
HRESULT ECMSProviderSwitch::CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	return ::CompareStoreIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
}

ULONG ECMSProviderSwitch::xMSProvider::AddRef() 
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderSwitch::AddRef", "");
	METHOD_PROLOGUE_(ECMSProviderSwitch, MSProvider);
	return pThis->AddRef();
}

ULONG ECMSProviderSwitch::xMSProvider::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderSwitch::Release", "");
	METHOD_PROLOGUE_(ECMSProviderSwitch, MSProvider);
	return pThis->Release();
}

HRESULT ECMSProviderSwitch::xMSProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderSwitch::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECMSProviderSwitch, MSProvider);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderSwitch::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMSProviderSwitch::xMSProvider::Shutdown(ULONG *lpulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderSwitch::Shutdown", "");
	METHOD_PROLOGUE_(ECMSProviderSwitch, MSProvider);
	return pThis->Shutdown(lpulFlags);
}

HRESULT ECMSProviderSwitch::xMSProvider::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity, LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderSwitch::Logon", "flags=%x, cbEntryID=%d", ulFlags, cbEntryID);
	METHOD_PROLOGUE_(ECMSProviderSwitch, MSProvider);
	HRESULT hr = pThis->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID,ulFlags, lpInterface, lpcbSpoolSecurity, lppbSpoolSecurity, lppMAPIError, lppMSLogon, lppMDB);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderSwitch::Logon", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMSProviderSwitch::xMSProvider::SpoolerLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG lpcbSpoolSecurity, LPBYTE lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderSwitch::SpoolerLogon", "flags=%x", ulFlags);
	METHOD_PROLOGUE_(ECMSProviderSwitch, MSProvider);
	HRESULT hr = pThis->SpoolerLogon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID,ulFlags, lpInterface, lpcbSpoolSecurity, lppbSpoolSecurity, lppMAPIError, lppMSLogon, lppMDB);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderSwitch::SpoolerLogon", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMSProviderSwitch::xMSProvider::CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderSwitch::CompareStoreIDs", "flags: %d\ncb=%d  entryid1: %s\n cb=%d entryid2: %s", ulFlags, cbEntryID1, bin2hex(cbEntryID1, (BYTE*)lpEntryID1).c_str(), cbEntryID2, bin2hex(cbEntryID2, (BYTE*)lpEntryID2).c_str());
	METHOD_PROLOGUE_(ECMSProviderSwitch, MSProvider);
	HRESULT hr = pThis->CompareStoreIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderSwitch::CompareStoreIDs", "%s %s", GetMAPIErrorDescription(hr).c_str(), (*lpulResult == TRUE)?"TRUE": "FALSE");
	return hr;
}

