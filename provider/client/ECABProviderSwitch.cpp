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

#include <memory.h>
#include <mapi.h>
#include <mapiutil.h>
#include <mapispi.h>
#include "ECABProvider.h"

#include <kopano/ECGetText.h>

#include <kopano/ECGuid.h>
#include <edkguid.h>

#include <kopano/Trace.h>
#include <kopano/ECDebug.h>

#include "EntryPoint.h"
#include <kopano/mapiext.h>

#include "ECABProviderSwitch.h"
#include "ECOfflineState.h"

#ifdef WIN32
#include <Sensapi.h>
#include "PasswordDlg.h"
#endif

#include "ProviderUtil.h"

#include <kopano/charset/convstring.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

ECABProviderSwitch::ECABProviderSwitch(void) : ECUnknown("ECABProviderSwitch")
{
}

ECABProviderSwitch::~ECABProviderSwitch(void)
{
}

HRESULT ECABProviderSwitch::Create(ECABProviderSwitch **lppECABProvider)
{
	HRESULT hr = hrSuccess;

	ECABProviderSwitch *lpECABProvider = new ECABProviderSwitch();

	hr = lpECABProvider->QueryInterface(IID_ECABProvider, (void **)lppECABProvider);

	if(hr != hrSuccess)
		delete lpECABProvider;

	return hr;
}

HRESULT ECABProviderSwitch::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECABProvider, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IABProvider, &this->m_xABProvider);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xABProvider);

	REGISTER_INTERFACE(IID_ISelectUnicode, &this->m_xUnknown);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABProviderSwitch::Shutdown(ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECABProviderSwitch::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG ulFlags, ULONG * lpulcbSecurity, LPBYTE * lppbSecurity, LPMAPIERROR * lppMAPIError, LPABLOGON * lppABLogon)
{
	HRESULT hr = hrSuccess;
	PROVIDER_INFO sProviderInfo;
	ULONG ulConnectType = CT_UNSPECIFIED;

	IABLogon *lpABLogon = NULL;
	IABProvider *lpOnline = NULL;
#ifdef HAVE_OFFLINE_SUPPORT
	IABLogon *lpABLogonOffline = NULL;
	IABProvider *lpOffline = NULL;

	ULONG ulAction;
	bool bRetryLogon;
	bool bFirstSync = false;
	DWORD dwNetworkFlag = 0;
#endif

	convstring tstrProfileName(lpszProfileName, ulFlags);

#if defined(WIN32) && !defined(WINCE)
	if (ulUIParam == 0)
		ulUIParam = (ULONG)GetWindow(GetDesktopWindow(), GW_CHILD);
#endif

	hr = GetProviders(&g_mapProviders, lpMAPISup, convstring(lpszProfileName, ulFlags).c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		goto exit;

	hr = sProviderInfo.lpABProviderOnline->QueryInterface(IID_IABProvider, (void **)&lpOnline);
	if (hr != hrSuccess)
		goto exit;

#ifdef HAVE_OFFLINE_SUPPORT
	hr = sProviderInfo.lpABProviderOffline->QueryInterface(IID_IABProvider, (void **)&lpOffline);
	if (hr != hrSuccess)
		goto exit;

relogin:

	if ((ulFlags & MDB_ONLINE) == MDB_ONLINE || (sProviderInfo.ulProfileFlags&EC_PROFILE_FLAGS_OFFLINE) != EC_PROFILE_FLAGS_OFFLINE) {
#endif
		// Online
		hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags, NULL, NULL, NULL, &lpABLogon);
		ulConnectType = CT_ONLINE;
#ifdef HAVE_OFFLINE_SUPPORT
	} else {
		// Offline
		hr = lpOffline->Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags | AB_NO_DIALOG, NULL, NULL, NULL, &lpABLogonOffline);
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
				hr = Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags | MDB_ONLINE, lpulcbSecurity, lppbSecurity, lppMAPIError, lppABLogon);
			else
				hr = MAPI_E_LOGON_FAILED;

			goto exit;	// In any case we're done
		} 
		else if (hr != hrSuccess)
			goto exit;

		//TODO: check is this the first time?
#ifdef WIN32
		if (bFirstSync) {
			hr = FirstAddressBookSync((HWND)ulUIParam, lpOffline, lpOnline, lpMAPISup);
			if (hr != hrSuccess)
				goto exit;
		}
#endif

		if ( (sProviderInfo.ulProfileFlags&(EC_PROFILE_FLAGS_OFFLINE|EC_PROFILE_FLAGS_CACHE_PRIVATE)) == (EC_PROFILE_FLAGS_OFFLINE|EC_PROFILE_FLAGS_CACHE_PRIVATE) )
		{ 
			// Cached mode
			lpABLogon = lpABLogonOffline;
			lpABLogonOffline = NULL;
			ulConnectType = CT_OFFLINE;
		}
#ifdef WIN32
		else 
		{
			// Autodetect
			if ((ulFlags & MDB_NO_DIALOG) && sProviderInfo.ulConnectType == CT_UNSPECIFIED) {

				// This is a non-interactive logon with autodetect mode. Our behaviour is to follow
				// whatever the user has last chosen in an interactive login session (pressing the button 'continue'
				// for example, selecting offline mode. This causes MAPISP32.exe to follow whatever the user has chosen.

				if(GetLastConnectionType(lpMAPISup, &ulConnectType) == erSuccess) {
					sProviderInfo.ulConnectType = ulConnectType;
				}
			}

			if (bFirstSync == FALSE && sProviderInfo.ulConnectType == CT_OFFLINE) {
				// Autodetect offline
				lpABLogon = lpABLogonOffline;
				lpABLogonOffline = NULL;
				ulConnectType = CT_OFFLINE;
			} else if (bFirstSync == FALSE && sProviderInfo.ulConnectType == CT_ONLINE) {
				// Autodetect online
				hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags, NULL, NULL, NULL, &lpABLogon);
				ulConnectType = CT_ONLINE;
			} else {
				// Autodetect unspecified

				do {
					bRetryLogon = false;
					ulConnectType = CT_ONLINE;

					if (IsNetworkAlive(&dwNetworkFlag) == TRUE) {
						hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags, NULL, NULL, NULL, &lpABLogon);
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
								lpABLogon = lpABLogonOffline;
								lpABLogonOffline = NULL;

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
				
			} // if (bFirstSync == FALSE && sProviderInfo.bOnline == FALSE)

		}
#endif	// WIN32
	}
#endif	// HAVE_OFFLINE_SUPPORT

	// Set the provider in the right connection type
	if (SetProviderMode(lpMAPISup, &g_mapProviders, convstring(lpszProfileName, ulFlags).c_str(), ulConnectType) != hrSuccess) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
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
			//MessageBox((HWND)ulUIParam, _("Incorrect username and/or password."), g_strProductName, MB_OK | MB_ICONEXCLAMATION );

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
#ifdef WIN32
		}else if(hr == MAPI_E_SESSION_LIMIT) {
			if(!(ulFlags & MDB_NO_DIALOG)) {
				MessageBox((HWND)ulUIParam, _("Cannot use the profile because you are over the license limit. To continue, you must purchase additional client licenses."), g_strProductName.c_str(), MB_ICONEXCLAMATION | MB_OK);
			}
			goto exit;
		}else if(hr == MAPI_E_NO_ACCESS) {
			if(!(ulFlags & MDB_NO_DIALOG)) {
				MessageBox((HWND)ulUIParam, _("Cannot use the profile because the server was unable to contact the license server. Please consult your system administrator."), g_strProductName.c_str(), MB_ICONEXCLAMATION | MB_OK);
			}
			goto exit;
#endif
		}else{
			hr = MAPI_E_LOGON_FAILED;
			goto exit;
		}
	}

	hr = lpMAPISup->SetProviderUID((LPMAPIUID)&MUIDECSAB, 0);
	if(hr != hrSuccess)
		goto exit;
	
	hr = lpABLogon->QueryInterface(IID_IABLogon, (void **)lppABLogon);
	if(hr != hrSuccess)
		goto exit;

	if(lpulcbSecurity)
		*lpulcbSecurity = 0;

	if(lppbSecurity)
		*lppbSecurity = NULL;

	if (lppMAPIError)
		*lppMAPIError = NULL;

exit:
	if (lpABLogon)
		lpABLogon->Release();

	if (lpOnline)
		lpOnline->Release();

#ifdef HAVE_OFFLINE_SUPPORT
	if (lpABLogonOffline)
		lpABLogonOffline->Release();

	if (lpOffline)
		lpOffline->Release();
#endif

	return hr;
}

HRESULT __stdcall ECABProviderSwitch::xABProvider::QueryInterface(REFIID refiid, void ** lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "ECABProviderSwitch::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECABProviderSwitch , ABProvider);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "ECABProviderSwitch::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

ULONG __stdcall ECABProviderSwitch::xABProvider::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "ECABProviderSwitch::AddRef", "");
	METHOD_PROLOGUE_(ECABProviderSwitch , ABProvider);
	return pThis->AddRef();
}

ULONG __stdcall ECABProviderSwitch::xABProvider::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "ECABProviderSwitch::Release", "");
	METHOD_PROLOGUE_(ECABProviderSwitch , ABProvider);
	ULONG ulRef = pThis->Release();
	TRACE_MAPI(TRACE_RETURN, "ECABProviderSwitch::Release", "%d", ulRef);
	return ulRef;
}

HRESULT ECABProviderSwitch::xABProvider::Shutdown(ULONG *lpulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "ECABProviderSwitch::Shutdown", "");
	METHOD_PROLOGUE_(ECABProviderSwitch , ABProvider);
	return pThis->Shutdown(lpulFlags);
}

HRESULT ECABProviderSwitch::xABProvider::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG ulFlags, ULONG * lpulcbSecurity, LPBYTE * lppbSecurity, LPMAPIERROR * lppMAPIError, LPABLOGON * lppABLogon)
{
	TRACE_MAPI(TRACE_ENTRY, "ECABProviderSwitch::Logon", "");
	METHOD_PROLOGUE_(ECABProviderSwitch , ABProvider);
	HRESULT hr = pThis->Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags, lpulcbSecurity, lppbSecurity, lppMAPIError, lppABLogon);
	TRACE_MAPI(TRACE_RETURN, "ECABProviderSwitch::Logon", "%s", GetMAPIErrorDescription(hr).c_str());
	return  hr;
}
