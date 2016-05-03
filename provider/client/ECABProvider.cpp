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

// ECABProvider.cpp: implementation of the ECABProvider class.
//
//////////////////////////////////////////////////////////////////////

#include <kopano/platform.h>
#include <mapi.h>
#include <kopano/mapiext.h>
#include <mapispi.h>
#include <mapiutil.h>
#include "kcore.hpp"
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include "ECABProvider.h"
#include "ECABLogon.h"

#include <kopano/ECDebug.h>


#include <kopano/Util.h>

#include "WSTransport.h"
#include "ClientUtil.h"
#include "EntryPoint.h"
#include "pcutil.hpp"

#include <kopano/mapi_ptr/mapi_memory_ptr.h>
typedef mapi_memory_ptr<ECUSER>	ECUserPtr;

#include <kopano/ECGetText.h>

using namespace std;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

ECABProvider::ECABProvider(ULONG ulFlags, const char *szClassName) :
    ECUnknown(szClassName)
{
	m_ulFlags = ulFlags;
}

ECABProvider::~ECABProvider()
{

}

HRESULT ECABProvider::Create(ECABProvider **lppECABProvider)
{
	HRESULT hr = hrSuccess;

	ECABProvider *lpECABProvider = new ECABProvider(0, "ECABProvider");

	hr = lpECABProvider->QueryInterface(IID_ECABProvider, (void **)lppECABProvider);

	if(hr != hrSuccess)
		delete lpECABProvider;

	return hr;
}

HRESULT ECABProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECABProvider, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IABProvider, &this->m_xABProvider);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xABProvider);

	REGISTER_INTERFACE(IID_ISelectUnicode, &this->m_xUnknown);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABProvider::Shutdown(ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECABProvider::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG ulFlags, ULONG * lpulcbSecurity, LPBYTE * lppbSecurity, LPMAPIERROR * lppMAPIError, LPABLOGON * lppABLogon)
{
	HRESULT			hr = hrSuccess;
	ECABLogon*		lpABLogon = NULL;
	LPSPropValue lpProviderUid = NULL;
	LPSPropValue lpSectionUid = NULL;
	IProfSect *lpProfSect = NULL;
	IProfSect *lpProfSectSection = NULL;
#ifdef WIN32
	IProfSect *lpProfSectService = NULL;
#endif
	LPSPropValue lpUidService = NULL;
	sGlobalProfileProps	sProfileProps;
	LPMAPIUID	lpGuid = NULL;

	WSTransport*	lpTransport = NULL;

#ifdef HAVE_OFFLINE_SUPPORT
	ECUserPtr		ptrUser;
	unsigned int	ulUserId = 0;
	std::string		strLocalServerPath;
#endif

	if (!lpMAPISup || !lppABLogon) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Get the username and password from the profile settings
	hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if(hr != hrSuccess)
		goto exit;

	// Create a transport for this provider
	hr = WSTransport::Create(ulFlags, &lpTransport);
	if(hr != hrSuccess)
		goto exit;

#ifdef HAVE_OFFLINE_SUPPORT
	if ( (m_ulFlags&EC_PROVIDER_OFFLINE) == EC_PROVIDER_OFFLINE) { 
		if (!sProfileProps.strOfflinePath.empty())
			g_strUserLocalAppDataKopano = sProfileProps.strOfflinePath;

		hr = CheckStartServerAndGetServerURL(lpMAPISup, g_strUserLocalAppDataKopano.c_str(), g_strKopanoDirectory.c_str(), &strLocalServerPath);
		if(hr != hrSuccess)
			goto exit;

		sProfileProps.strServerPath = strLocalServerPath;
	}
#endif

	// Log on the transport to the server
	hr = lpTransport->HrLogon(sProfileProps);

#ifdef HAVE_OFFLINE_SUPPORT
	if ( (m_ulFlags&EC_PROVIDER_OFFLINE) == EC_PROVIDER_OFFLINE && hr != hrSuccess)
	{
		sGlobalProfileProps sLocalServerProfileProps;
		sGlobalProfileProps sOnlineProfileProps;

		hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sLocalServerProfileProps);
		if(hr != hrSuccess)
			goto exit;

		sOnlineProfileProps = sLocalServerProfileProps;

		sLocalServerProfileProps.strServerPath = strLocalServerPath;
		sLocalServerProfileProps.strUserName = KOPANO_SYSTEM_USER_W;
		sLocalServerProfileProps.strPassword = KOPANO_SYSTEM_USER_W;

		lpTransport->HrLogOff();

		// Log on online
		hr = lpTransport->HrLogon(sOnlineProfileProps);
		if(hr != hrSuccess) {
			hr = MAPI_E_UNCONFIGURED;
			goto exit;
		}

		hr = lpTransport->HrGetUser(0, NULL, MAPI_UNICODE, &ptrUser);
		if(hr != hrSuccess)
			goto exit;

		lpTransport->HrLogOff();

		// first time logon, you should be an administrator
		hr = lpTransport->HrLogon(sLocalServerProfileProps);
		if(hr != hrSuccess)
			goto exit; // Only when the offline server is killed on a bad moment

		// Add user to offline store
		ptrUser->lpszPassword = (LPTSTR)L"dummy";		
		hr = lpTransport->HrSetUser(ptrUser, MAPI_UNICODE);
		if(hr != hrSuccess)
			goto exit;

		// Log off the admin user
		lpTransport->HrLogOff();

		// Login as normal user
		hr = lpTransport->HrLogon(sProfileProps);

	}
#endif

	if(hr != hrSuccess)
		goto exit;

#ifdef WIN32
	/**
	 * Do some profile management:
	 * 
	 * Add PR_EMSMDB_SECTION_UID to the addressbook's profile section AND the service entry if it is not there yet
	 * Create the EMSMDB section if it does not exist yet
	 * Add PR_EMSABP_UID to the EMSMDB profile section if it doesn't exist yet
	 */
	hr = lpMAPISup->OpenProfileSection(NULL, MAPI_MODIFY, &lpProfSect);
	if(hr != hrSuccess)
		goto exit;

	if(HrGetOneProp(lpProfSect, PR_EMSMDB_SECTION_UID, &lpSectionUid) != hrSuccess) {
		/* No EMSMDB property found; create the EMSMDB uid and save it in both our profile section
		 * and the message service's profile section */

		/* Open our service's profile section */
		hr = HrGetOneProp(lpProfSect, PR_SERVICE_UID, &lpUidService);
		if(hr != hrSuccess)
			goto exit;

		hr = lpMAPISup->OpenProfileSection((LPMAPIUID)lpUidService->Value.bin.lpb, MAPI_MODIFY, &lpProfSectService);
		if(hr != hrSuccess)
			goto exit;

		/* Create the new PR_EMSMDB_SECTION_UID property */
		hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpSectionUid);
		if(hr != hrSuccess)
			goto exit;

		hr = MAPIAllocateMore(sizeof(GUID), lpSectionUid, (void **)&lpSectionUid->Value.bin.lpb);
		if(hr != hrSuccess)
			goto exit;

		lpSectionUid->ulPropTag = PR_EMSMDB_SECTION_UID;
		lpSectionUid->Value.bin.cb = sizeof(GUID);
		CoCreateGuid((GUID *)lpSectionUid->Value.bin.lpb);

		/* Set the property on the message service profile section */
		hr = HrSetOneProp(lpProfSectService, lpSectionUid);
		if(hr != hrSuccess)
			goto exit;

		/* Set the property on the addressbook provider's profile section */
		hr = HrSetOneProp(lpProfSect, lpSectionUid);
		if(hr != hrSuccess)
			goto exit;
	}

	/* from the emsmdb section, get the emsabp UID (creates the section if it was not there before) */
	hr = lpMAPISup->OpenProfileSection((LPMAPIUID)lpSectionUid->Value.bin.lpb, MAPI_MODIFY, &lpProfSectSection);
	if(hr != hrSuccess)
		goto exit;

	if(HrGetOneProp(lpProfSectSection, 0x3d1a0102 /* PR_EMSABP_UID */, &lpProviderUid) != hrSuccess) {
		/* Unsure if this is the right place, but can't find a more logical place to do it. Use case
		 * is to have a 2007-style profile (without 0x3d1a) and then 'upgrade' by starting OLK2010 with
		 * global profile redirection enabled. This automatically creates a new EMSMDB_SECTION_UID and
		 * profile section, but it does not contain the EMSABP UID. Tactic is now to just create it here
		 * if there was none yet.
		 */
		hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpProviderUid);
		if (hr != hrSuccess)
			goto exit;

		hr = MAPIAllocateMore(sizeof(GUID), lpProviderUid, (void **)&lpProviderUid->Value.bin.lpb);
		if (hr != hrSuccess)
			goto exit;

		hr = lpMAPISup->NewUID((LPMAPIUID)lpProviderUid->Value.bin.lpb);
		if (hr != hrSuccess)
			goto exit;

		lpProviderUid->ulPropTag = 0x3d1a0102;
		lpProviderUid->Value.bin.cb = sizeof(GUID);
		hr = lpProfSectSection->SetProps(1, lpProviderUid, NULL);
		if (hr != hrSuccess)
			goto exit;

		hr = lpProfSectSection->SaveChanges(0);
		if (hr != hrSuccess)
			goto exit;
	}

	lpGuid = (LPMAPIUID)lpProviderUid->Value.bin.lpb;

	/* Register ourselves as handling the EMSABP UID too
	 *
	 * This is needed because in OLK2010, multi-exchange-account-mode, there can be multiple GAB
	 * providers. Normally these would all use UID MUIDECSAB, which would make entryIDs indistinguishable. To
	 * distinguish between addressbooks, various *WithExchangeContext*() functions have been introduced, which
	 * take an EMSMDB parameter. The EMSMDB parameter is then used to lookup the EMSABP GUID from the profile
	 * section, which is then patched directly into the EntryID of that addressbook item. This is then passed
	 * to MAPI. We therefore have to register ourselves for THAT uid, apart from registering ourselves for
	 * the 'normal' UID (which was done dus before the EMSMDB_SECTION_UID call above.
	 */
	lpMAPISup->SetProviderUID(lpGuid, 0);
#endif

	hr = ECABLogon::Create(lpMAPISup, lpTransport, sProfileProps.ulProfileFlags, (GUID *)lpGuid, &lpABLogon);
	if(hr != hrSuccess)
		goto exit;

	AddChild(lpABLogon);

	hr = lpABLogon->QueryInterface(IID_IABLogon, (void **)lppABLogon);
	if(hr != hrSuccess)
		goto exit;


	if (lpulcbSecurity)
		*lpulcbSecurity = 0;

	if (lppbSecurity)
		*lppbSecurity = NULL;

	if (lppMAPIError)
		*lppMAPIError = NULL;

exit:
#ifdef WIN32
	if (lpProfSectService)
		lpProfSectService->Release();
#endif
	MAPIFreeBuffer(lpUidService);
	MAPIFreeBuffer(lpProviderUid);
	MAPIFreeBuffer(lpSectionUid);
	if (lpProfSect)
		lpProfSect->Release();

	if (lpProfSectSection)
		lpProfSectSection->Release();

	if(lpABLogon)
		lpABLogon->Release();

	if(lpTransport)
		lpTransport->Release();

	return hr;
}


HRESULT __stdcall ECABProvider::xABProvider::QueryInterface(REFIID refiid, void ** lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "IABProvider::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECABProvider , ABProvider);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IABProvider::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

ULONG __stdcall ECABProvider::xABProvider::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "IABProvider::AddRef", "");
	METHOD_PROLOGUE_(ECABProvider , ABProvider);
	return pThis->AddRef();
}

ULONG __stdcall ECABProvider::xABProvider::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IABProvider::Release", "");
	METHOD_PROLOGUE_(ECABProvider , ABProvider);
	return pThis->Release();
}

HRESULT ECABProvider::xABProvider::Shutdown(ULONG *lpulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IABProvider::Shutdown", "");
	METHOD_PROLOGUE_(ECABProvider , ABProvider);
	return pThis->Shutdown(lpulFlags);
}

HRESULT ECABProvider::xABProvider::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG ulFlags, ULONG * lpulcbSecurity, LPBYTE * lppbSecurity, LPMAPIERROR * lppMAPIError, LPABLOGON * lppABLogon)
{
	TRACE_MAPI(TRACE_ENTRY, "IABProvider::Logon", "");
	METHOD_PROLOGUE_(ECABProvider , ABProvider);
	HRESULT hr = pThis->Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags, lpulcbSecurity, lppbSecurity, lppMAPIError, lppABLogon);
	TRACE_MAPI(TRACE_RETURN, "IABProvider::Logon", "%s", GetMAPIErrorDescription(hr).c_str());
	return  hr;
}
