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

#include <mapi.h>
#include <mapispi.h>
#include <mapiutil.h>

#include <kopano/ECGetText.h>

#include <string>
#include <cassert>

#include "kcore.hpp"
#include "Mem.h"

#include "DLLGlobal.h"
#include "ECMSProviderSwitch.h"
#include "ECXPProvider.h"
#include "ECABProviderSwitch.h"
#ifdef LINUX
#include <iostream>
#endif

#include <kopano/ecversion.h>

#include <kopano/ECDebug.h>
#include <kopano/stringutil.h>

#include <kopano/ECLogger.h>

#include <kopano/ECGuid.h>
#include <edkmdb.h>
#include <edkguid.h>

#include <kopano/mapi_ptr.h>

#include "SSLUtil.h"
#include "ClientUtil.h"
#include "SymmetricCrypt.h"

#include "EntryPoint.h"

#include <kopano/charset/convstring.h>

using namespace std;

#ifdef _DEBUG
#define new DEBUG_NEW

#define DEBUG_WITH_MEMORY_DUMP 0 // Sure to dump memleaks before the dll is exit
#endif

// Client wide variable
tstring		g_strCommonFilesKopano;
tstring		g_strUserLocalAppDataKopano;
tstring		g_strKopanoDirectory;

tstring		g_strManufacturer;
tstring		g_strProductName;
tstring		g_strProductNameShort;
bool		g_isOEM;
ULONG		g_ulLoadsim;

// Map of msprovider with Profilename as key
ECMapProvider	g_mapProviders;

class CKopanoApp {
public:
    CKopanoApp() {
        ssl_threading_setup();

		g_strManufacturer = _T("Kopano");
		g_strProductName = _T("Kopano Core");
		g_isOEM = false;
		g_ulLoadsim = FALSE;

		// FIXME for offline
		// - g_strUserLocalAppDataKopano = ~/kopano ?
		// - g_strKopanoDirectory = /usr/bin/ ?
    }
    ~CKopanoApp() {
        ssl_threading_cleanup();

		RemoveAllProviders(&g_mapProviders);
    }
};

CKopanoApp theApp;

///////////////////////////////////////////////////////////////////
// entrypoints
//

// Called by MAPI to return a MSProvider object when a user opens a store based on our service
extern "C" HRESULT __cdecl MSProviderInit(HINSTANCE hInstance, LPMALLOC pmalloc, LPALLOCATEBUFFER pfnAllocBuf, LPALLOCATEMORE pfnAllocMore, LPFREEBUFFER pfnFreeBuf, ULONG ulFlags, ULONG ulMAPIver, ULONG * lpulProviderVer, LPMSPROVIDER * ppmsp)
{
	TRACE_MAPI(TRACE_ENTRY, "MSProviderInit", "flags=%08X", ulFlags);

	HRESULT hr = hrSuccess;
	ECMSProviderSwitch *lpMSProvider = NULL;

	// Check the interface version is ok
	if(ulMAPIver != CURRENT_SPI_VERSION) {
		hr = MAPI_E_VERSION;
		goto exit;
	}

	*lpulProviderVer = CURRENT_SPI_VERSION;
	
	// Save the pointers for later use
	_pmalloc = pmalloc;
	_pfnAllocBuf = pfnAllocBuf;
	_pfnAllocMore = pfnAllocMore;
	_pfnFreeBuf = pfnFreeBuf;
	_hInstance = hInstance;

	// This object is created for the lifetime of the DLL and destroyed when the
	// DLL is closed (same on linux, but then for the shared library);
	hr = ECMSProviderSwitch::Create(ulFlags, &lpMSProvider);

	if(hr != hrSuccess)
		goto exit;

	hr = lpMSProvider->QueryInterface(IID_IMSProvider, (void **)ppmsp); 

exit:
	if (lpMSProvider)
		lpMSProvider->Release();

	TRACE_MAPI(TRACE_RETURN, "MSProviderInit", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

/**
 * Get the service name from the provider admin
 *
 * The service name is the string normally passed to CreateMsgService, like "ZARAFA6" or "MSEMS".
 *
 * @param lpProviderAdmin[in] The ProviderAdmin object passed to MSGServiceEntry
 * @param lpServiceName[out] The name of the message service
 * @return HRESULT Result status
 */
static HRESULT GetServiceName(IProviderAdmin *lpProviderAdmin,
    std::string *lpServiceName)
{
	lpServiceName->assign("ZARAFA6");
	return hrSuccess;
}

/**
 * Initialize one service provider
 *
 * @param lpAdminProvider[in]	Pointer to provider admin.
 * @param lpProfSect[in]		Pointer to provider profile section.
 * @param sProfileProps[in]		Global profile properties
 * @param lpcStoreID[out]		Size of lppStoreID
 * @param lppStoreID[out]		Entryid of the store of the provider
 *
 * @return MAPI error codes
 */
HRESULT InitializeProvider(LPPROVIDERADMIN lpAdminProvider,
    IProfSect *lpProfSect, sGlobalProfileProps sProfileProps,
    ULONG *lpcStoreID, LPENTRYID *lppStoreID, WSTransport *transport)
{
	HRESULT hr = hrSuccess;

	WSTransport		*lpTransport = NULL;
	WSTransport		*lpAltTransport = NULL;
	
	PABEID			pABeid = NULL;
	
	ULONG			cbEntryId = 0;
	ULONG			cbWrappedEntryId = 0;
	EntryIdPtr		ptrEntryId;
	EntryIdPtr		ptrWrappedEntryId;
	ProfSectPtr		ptrGlobalProfSect;
	SPropValuePtr	ptrPropValueName;
	SPropValuePtr	ptrPropValueMDB;
	SPropValuePtr	ptrPropValueResourceType;
	SPropValuePtr	ptrPropValueServiceName;
	SPropValuePtr	ptrPropValueProviderUid;
	SPropValuePtr	ptrPropValueServerName;
	WStringPtr		ptrStoreName;
	std::string		strRedirServer;
	std::string		strDefStoreServer;
	std::string		strServiceName;

	SPropValue		sPropValue;
	SPropValue		sPropVals[6]; 
	ULONG			cPropValue = 0;
	ULONG			ulResourceType=0;

	if (lpAdminProvider) {
		hr = GetServiceName(lpAdminProvider, &strServiceName);
		if (hr != hrSuccess)
			goto exit;
	} else {

		hr = HrGetOneProp(lpProfSect, PR_SERVICE_NAME_A, &ptrPropValueServiceName);
		if(hr == hrSuccess)
			strServiceName = ptrPropValueServiceName->Value.lpszA;
		
		hr = hrSuccess;
	}
	
	hr = HrGetOneProp(lpProfSect, PR_RESOURCE_TYPE, &ptrPropValueResourceType);
	if(hr != hrSuccess) {
		// Ignore this provider; apparently it has no resource type, so just skip it
		hr = hrSuccess;
		goto exit;
	}
	
	HrGetOneProp(lpProfSect, PR_PROVIDER_UID, &ptrPropValueProviderUid);

	ulResourceType = ptrPropValueResourceType->Value.l;

	TRACE_MAPI(TRACE_INFO, "InitializeProvider", "Resource type=%s", ResourceTypeToString(ulResourceType) );

	if (transport != NULL) {
		lpTransport = transport;
	} else {
		hr = WSTransport::Create(0, &lpTransport);
		if (hr != hrSuccess)
			goto exit;
	}

	hr = lpTransport->HrLogon(sProfileProps);
	if(hr != hrSuccess)
		goto exit;

	if(ulResourceType == MAPI_STORE_PROVIDER)
	{
		hr = HrGetOneProp(lpProfSect, PR_MDB_PROVIDER, &ptrPropValueMDB);
		if(hr != hrSuccess)
			goto exit;

		if(CompareMDBProvider(ptrPropValueMDB->Value.bin.lpb, &KOPANO_STORE_PUBLIC_GUID)) {
			// Get the public store
			if (sProfileProps.ulProfileFlags & EC_PROFILE_FLAGS_NO_PUBLIC_STORE)
				hr = MAPI_E_INVALID_PARAMETER; // just to get to the DeleteProvider part
			else
				hr = lpTransport->HrGetPublicStore(0, &cbEntryId, &ptrEntryId, &strRedirServer);

			if (hr == MAPI_E_UNABLE_TO_COMPLETE)
			{
				lpTransport->HrLogOff();
				sProfileProps.strServerPath = strRedirServer;
				hr = lpTransport->HrLogon(sProfileProps);
									
				if (hr == hrSuccess)
					hr = lpTransport->HrGetPublicStore(0, &cbEntryId, &ptrEntryId);
			}
			if(hr != hrSuccess) {
				if(lpAdminProvider && ptrPropValueProviderUid.get())
					lpAdminProvider->DeleteProvider((MAPIUID *)ptrPropValueProviderUid->Value.bin.lpb);

				// Profile without public store
				hr = hrSuccess;
				goto exit;
			}
		} else if(CompareMDBProvider(ptrPropValueMDB->Value.bin.lpb, &KOPANO_SERVICE_GUID)) {
			// Get the default store for this user
			hr = lpTransport->HrGetStore(0, NULL, &cbEntryId, &ptrEntryId, 0, NULL, &strRedirServer);
			if (hr == MAPI_E_NOT_FOUND) {
				ec_log_err("HrGetStore failed: No store present.");
			} else if (hr == MAPI_E_UNABLE_TO_COMPLETE) {
				lpTransport->HrLogOff();
				sProfileProps.strServerPath = strRedirServer;
				hr = lpTransport->HrLogon(sProfileProps);
				if (hr != hrSuccess)
					goto exit;

				hr = lpTransport->HrGetStore(0, NULL, &cbEntryId, &ptrEntryId, 0, NULL);
				if (hr == hrSuccess)
				{
					// This should be a real URL
					assert(strRedirServer.compare(0, 9, "pseudo://") != 0);

					// Set the default store home server.
					if (lpAdminProvider && !strRedirServer.empty())
					{
						hr = lpAdminProvider->OpenProfileSection((LPMAPIUID)pbGlobalProfileSectionGuid, NULL, MAPI_MODIFY , &ptrGlobalProfSect);
						if(hr != hrSuccess)
							goto exit;

						sPropValue.ulPropTag = PR_EC_PATH;
						sPropValue.Value.lpszA = (char *)strRedirServer.c_str();
						hr = HrSetOneProp(ptrGlobalProfSect, &sPropValue);
						if(hr != hrSuccess)
							goto exit;
					}
				}
			}
			if(hr != hrSuccess) 
				goto exit;
			
		} else if(CompareMDBProvider(ptrPropValueMDB->Value.bin.lpb, &KOPANO_STORE_DELEGATE_GUID)) {
			// PR_EC_USERNAME is the user we're want to add ...
			hr = HrGetOneProp(lpProfSect, PR_EC_USERNAME_W, &ptrPropValueName);
			if(hr != hrSuccess) {
				hr = HrGetOneProp(lpProfSect, PR_EC_USERNAME_A, &ptrPropValueName);
			}
			if(hr != hrSuccess) {
				// This should probably be done in UpdateProviders. But UpdateProviders doesn't
				// know the type of the provider and it shouldn't just delete the provider for
				// all types of providers.
				if(lpAdminProvider && ptrPropValueProviderUid.get())
					lpAdminProvider->DeleteProvider((MAPIUID *)ptrPropValueProviderUid->Value.bin.lpb);

				// Invalid or empty delegate store
				hr = hrSuccess;
				goto exit;
			}

			hr = lpTransport->HrResolveUserStore(convstring::from_SPropValue(ptrPropValueName), 0, NULL, &cbEntryId, &ptrEntryId, &strRedirServer);
			if (hr == MAPI_E_UNABLE_TO_COMPLETE)
			{
				lpTransport->HrLogOff();
				sProfileProps.strServerPath = strRedirServer;
				hr = lpTransport->HrLogon(sProfileProps);
				if (hr != hrSuccess)
					goto exit;
				
				hr = lpTransport->HrResolveUserStore(convstring::from_SPropValue(ptrPropValueName), 0, NULL, &cbEntryId, &ptrEntryId);
			}
			if(hr != hrSuccess)
				goto exit;
		} else if(CompareMDBProvider(ptrPropValueMDB->Value.bin.lpb, &KOPANO_STORE_ARCHIVE_GUID)) {
			// We need to get the username and the server name or url from the profsect.
			// That's enough information to get the entryid from the correct server. There's no redirect
			// available when resolving archive stores.
			hr = HrGetOneProp(lpProfSect, PR_EC_USERNAME_W, &ptrPropValueName);
			if (hr != hrSuccess)
				hr = HrGetOneProp(lpProfSect, PR_EC_USERNAME_A, &ptrPropValueName);
			if (hr == hrSuccess) {
				hr = HrGetOneProp(lpProfSect, PR_EC_SERVERNAME_W, &ptrPropValueServerName);
				if (hr != hrSuccess)
					hr = HrGetOneProp(lpProfSect, PR_EC_SERVERNAME_A, &ptrPropValueServerName);
				if (hr != hrSuccess) {
					hr = MAPI_E_UNCONFIGURED;
					goto exit;
				}
			}
			if (hr != hrSuccess) {
				// This should probably be done in UpdateProviders. But UpdateProviders doesn't
				// know the type of the provider and it shouldn't just delete the provider for
				// all types of providers.
				if(lpAdminProvider && ptrPropValueProviderUid.get())
					lpAdminProvider->DeleteProvider((MAPIUID *)ptrPropValueProviderUid->Value.bin.lpb);

				// Invalid or empty archive store
				hr = hrSuccess;
				goto exit;
			}

			hr = GetTransportToNamedServer(lpTransport, ptrPropValueServerName->Value.LPSZ, (PROP_TYPE(ptrPropValueServerName->ulPropTag) == PT_STRING8 ? 0 : MAPI_UNICODE), &lpAltTransport);
			if (hr != hrSuccess)
				goto exit;

			std::swap(lpTransport, lpAltTransport);
			lpAltTransport->Release();
			lpAltTransport = NULL;

			hr = lpTransport->HrResolveTypedStore(convstring::from_SPropValue(ptrPropValueName), ECSTORE_TYPE_ARCHIVE, &cbEntryId, &ptrEntryId);
			if (hr != hrSuccess)
				goto exit;
		} else {
			ASSERT(FALSE); // unknown GUID?
			goto exit;
		}

		hr = lpTransport->HrGetStoreName(cbEntryId, ptrEntryId, MAPI_UNICODE, (LPTSTR*)&ptrStoreName);
		if(hr != hrSuccess) 
			goto exit;

		hr = WrapStoreEntryID(0, (LPTSTR)WCLIENT_DLL_NAME, cbEntryId, ptrEntryId, &cbWrappedEntryId, &ptrWrappedEntryId);
		if(hr != hrSuccess) 
			goto exit;

		sPropVals[cPropValue].ulPropTag = PR_ENTRYID;
		sPropVals[cPropValue].Value.bin.cb = cbWrappedEntryId;
		sPropVals[cPropValue++].Value.bin.lpb = (LPBYTE) ptrWrappedEntryId.get();

		sPropVals[cPropValue].ulPropTag = PR_RECORD_KEY;
		sPropVals[cPropValue].Value.bin.cb = sizeof(MAPIUID);
		sPropVals[cPropValue++].Value.bin.lpb = (LPBYTE) &((PEID)ptrEntryId.get())->guid; //@FIXME validate guid

			sPropVals[cPropValue].ulPropTag = PR_DISPLAY_NAME_W;
			sPropVals[cPropValue++].Value.lpszW = ptrStoreName.get();

		sPropVals[cPropValue].ulPropTag = PR_EC_PATH;
		sPropVals[cPropValue++].Value.lpszA = const_cast<char *>("Server");

		sPropVals[cPropValue].ulPropTag = PR_PROVIDER_DLL_NAME_A;
		sPropVals[cPropValue++].Value.lpszA = const_cast<char *>(WCLIENT_DLL_NAME);
						
	} else if(ulResourceType == MAPI_AB_PROVIDER) {
		hr = MAPIAllocateBuffer(CbNewABEID(""), (void**)&pABeid);
		if(hr != hrSuccess)
			goto exit;

		memset(pABeid, 0, CbNewABEID(""));

		memcpy(&pABeid->guid, &MUIDECSAB, sizeof(GUID));
		pABeid->ulType = MAPI_ABCONT;

		sPropVals[cPropValue].ulPropTag = PR_ENTRYID;
		sPropVals[cPropValue].Value.bin.cb = CbNewABEID("");
		sPropVals[cPropValue++].Value.bin.lpb = (LPBYTE) pABeid;

		sPropVals[cPropValue].ulPropTag = PR_RECORD_KEY;
		sPropVals[cPropValue].Value.bin.cb = sizeof(MAPIUID);
		sPropVals[cPropValue++].Value.bin.lpb = (LPBYTE)&MUIDECSAB;

		sPropVals[cPropValue].ulPropTag = PR_DISPLAY_NAME_A;
		sPropVals[cPropValue++].Value.lpszA = const_cast<char *>("Kopano Addressbook");

		sPropVals[cPropValue].ulPropTag = PR_EC_PATH;
		sPropVals[cPropValue++].Value.lpszA = const_cast<char *>("Kopano Addressbook");

		sPropVals[cPropValue].ulPropTag = PR_PROVIDER_DLL_NAME_A;
		sPropVals[cPropValue++].Value.lpszA = const_cast<char *>(WCLIENT_DLL_NAME);

	} else {
		if(ulResourceType != MAPI_TRANSPORT_PROVIDER) {
			ASSERT(FALSE);
		}
		goto exit;
	}

	hr = lpProfSect->SetProps(cPropValue, sPropVals, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpProfSect->SaveChanges(0);
	if(hr != hrSuccess)
		goto exit;

	if (lpcStoreID && lppStoreID) {
		*lpcStoreID = cbEntryId;

		hr = MAPIAllocateBuffer(cbEntryId, (void**)lppStoreID);
		if(hr != hrSuccess)
			goto exit;
		
		memcpy(*lppStoreID, ptrEntryId, cbEntryId);
	}
exit:
	//Free allocated memory
	MAPIFreeBuffer(pABeid);
	pABeid = NULL;
	if (lpTransport != NULL && lpTransport != transport)
		lpTransport->Release(); /* implies logoff */
	else
		lpTransport->logoff_nd();

	return hr;
}

static HRESULT UpdateProviders(LPPROVIDERADMIN lpAdminProviders,
    const sGlobalProfileProps &sProfileProps, WSTransport *transport)
{
	HRESULT hr;

	ProfSectPtr		ptrProfSect;
	MAPITablePtr	ptrTable;
	SRowSetPtr		ptrRows;
	LPSPropValue	lpsProviderUID;

	// Get the provider table
	hr = lpAdminProviders->GetProviderTable(0, &ptrTable);
	if(hr != hrSuccess)
		return hr;

	// Get the rows
	hr = ptrTable->QueryRows(0xFF, 0, &ptrRows);
	if(hr != hrSuccess)
		return hr;

	//Check if exist one or more rows
	if (ptrRows.size() == 0)
		return MAPI_E_NOT_FOUND;

	// Scan the rows for message stores
	for (ULONG curRow = 0; curRow < ptrRows.size(); ++curRow) {
		//Get de UID of the provider to open the profile section
		lpsProviderUID = PpropFindProp(ptrRows[curRow].lpProps, ptrRows[curRow].cValues, PR_PROVIDER_UID);
		if(lpsProviderUID == NULL || lpsProviderUID->Value.bin.cb == 0) {
			// Provider without a provider uid,  just move to the next
			ASSERT(FALSE);
			continue;
		}

		hr = lpAdminProviders->OpenProfileSection((MAPIUID *)lpsProviderUID->Value.bin.lpb, NULL, MAPI_MODIFY, &ptrProfSect);
		if(hr != hrSuccess)
			return hr;

		// Set already PR_PROVIDER_UID, ignore error
		HrSetOneProp(ptrProfSect, lpsProviderUID);

		hr = InitializeProvider(lpAdminProviders, ptrProfSect, sProfileProps, NULL, NULL, transport);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

static std::string GetServerTypeFromPath(const char *szPath)
{
	std::string path = szPath;
	size_t pos;

	pos = path.find("://");
	if (pos != std::string::npos)
		return path.substr(0, pos);
	return std::string();
}

// Called by MAPI to configure, or create a service
extern "C" HRESULT __stdcall MSGServiceEntry(HINSTANCE hInst,
    LPMALLOC lpMalloc, LPMAPISUP psup, ULONG ulUIParam, ULONG ulFlags,
    ULONG ulContext, ULONG cvals, LPSPropValue pvals,
    LPPROVIDERADMIN lpAdminProviders, MAPIERROR **lppMapiError)
{
	TRACE_MAPI(TRACE_ENTRY, "MSGServiceEntry", "flags=0x%08X, context=%s", ulFlags, MsgServiceContextToString(ulContext));

	HRESULT			hr = erSuccess;
	std::string		strServerName;
	std::wstring	strUserName;
	std::wstring	strUserPassword;
	std::string		strServerPort;
	std::string		strDefaultOfflinePath;
	std::string		strType;
	std::string		strRedirServer;
	std::string		strDefStoreServer;
	sGlobalProfileProps	sProfileProps;
	std::basic_string<TCHAR> strError;

	ProfSectPtr		ptrGlobalProfSect;
	ProfSectPtr		ptrProfSect;
	MAPISessionPtr	ptrSession;

	WSTransport		*lpTransport = NULL;
	LPSPropValue	lpsPropValue = NULL;
	ULONG			cValues = 0;
	bool			bShowDialog = false;

	MAPIERROR		*lpMapiError = NULL;

	bool			bShowAllSettingsPages = false;
	LPBYTE			lpDelegateStores = NULL;
	ULONG			cDelegateStores = 0;
	LPSPropValue	lpsPropValueFind = NULL;
	ULONG 			cValueIndex = 0;
	convert_context	converter;

	bool bGlobalProfileUpdate = false;
	bool bUpdatedPageConnection = false;
	bool bInitStores = true;

	_hInstance = hInst;

	if (psup) {
		hr = psup->GetMemAllocRoutines(&_pfnAllocBuf, &_pfnAllocMore, &_pfnFreeBuf);
		if(hr != hrSuccess) {
			ASSERT(FALSE);
		}
	} else {
		// Support object not available on linux at this time... TODO: fix mapi4linux?
		_pfnAllocBuf = MAPIAllocateBuffer;
		_pfnAllocMore = MAPIAllocateMore;
		_pfnFreeBuf = MAPIFreeBuffer;
	}

	// Logon defaults
	strType = "http";
	strServerName = "";
	strServerPort ="236";

	switch(ulContext) {
	case MSG_SERVICE_INSTALL:
		hr = hrSuccess;
		break;
	case MSG_SERVICE_UNINSTALL:
		hr = hrSuccess;
		break;
	case MSG_SERVICE_DELETE:
		hr = hrSuccess;
		break;
	case MSG_SERVICE_PROVIDER_CREATE:
		if(cvals && pvals) {

			LPSPropValue lpsPropName = NULL;
			
			lpsPropValueFind = PpropFindProp(pvals, cvals, PR_PROVIDER_UID);
			if(lpsPropValueFind == NULL || lpsPropValueFind->Value.bin.cb == 0)
			{
				//FIXME: give the right error?
				hr = MAPI_E_UNCONFIGURED;
				goto exit;
			}

			// PR_EC_USERNAME is the user we're adding ...
			lpsPropName = PpropFindProp(pvals, cvals, CHANGE_PROP_TYPE(PR_EC_USERNAME_A, PT_UNSPECIFIED));
			if(lpsPropName == NULL || lpsPropName->Value.bin.cb == 0)
			{
				hr = MAPI_E_UNCONFIGURED;
				goto exit;
			}

			//Open profile section
			hr = lpAdminProviders->OpenProfileSection((MAPIUID *)lpsPropValueFind->Value.bin.lpb, NULL, MAPI_MODIFY, &ptrProfSect);
			if(hr != hrSuccess)
				goto exit;

			hr = HrSetOneProp(ptrProfSect, lpsPropName);
			if(hr != hrSuccess)
				goto exit;
		
			hr = lpAdminProviders->OpenProfileSection((LPMAPIUID)pbGlobalProfileSectionGuid, NULL, MAPI_MODIFY , &ptrGlobalProfSect);
			if(hr != hrSuccess)
				goto exit;

			// Get username/pass settings
			hr = ClientUtil::GetGlobalProfileProperties(ptrGlobalProfSect, &sProfileProps);
			if(hr != hrSuccess)
				goto exit;

			if(sProfileProps.strUserName.empty() || sProfileProps.strServerPath.empty()) {
				hr = MAPI_E_UNCONFIGURED; // @todo: check if this is the right error
				goto exit;
			}

			hr = InitializeProvider(lpAdminProviders, ptrProfSect, sProfileProps, NULL, NULL, NULL);
			if (hr != hrSuccess)
				goto exit;
		
		}

		break;
	case MSG_SERVICE_PROVIDER_DELETE:
		hr = hrSuccess;

		//FIXME: delete Offline database

		break;
	case MSG_SERVICE_CONFIGURE:
		bShowAllSettingsPages = true;
		// Do not break here
	case MSG_SERVICE_CREATE:
		
		//Open global profile, add the store.(for show list, delete etc)
		hr = lpAdminProviders->OpenProfileSection((LPMAPIUID)pbGlobalProfileSectionGuid, NULL, MAPI_MODIFY , &ptrGlobalProfSect);
		if(hr != hrSuccess)
			goto exit;

		if(cvals) {
			hr = ptrGlobalProfSect->SetProps(cvals, pvals, NULL);

			if(hr != hrSuccess)
				goto exit;
		}

		hr = ClientUtil::GetGlobalProfileProperties(ptrGlobalProfSect, &sProfileProps);

		if(sProfileProps.strServerPath.empty() || sProfileProps.strUserName.empty() || (sProfileProps.strPassword.empty() && sProfileProps.strSSLKeyFile.empty())) {
			bShowDialog = true;
		}
		//FIXME: check here offline path with the flags
		if(!sProfileProps.strServerPath.empty()) {
			strServerName = GetServerNameFromPath(sProfileProps.strServerPath.c_str());
			strServerPort = GetServerPortFromPath(sProfileProps.strServerPath.c_str());
			strType = GetServerTypeFromPath(sProfileProps.strServerPath.c_str());
		}

		// Get deligate stores, Ignore error
		ClientUtil::GetGlobalProfileDelegateStoresProp(ptrGlobalProfSect, &cDelegateStores, &lpDelegateStores);

		// init defaults
		hr = WSTransport::Create(ulFlags & SERVICE_UI_ALLOWED ? 0 : MDB_NO_DIALOG, &lpTransport);
		if(hr != hrSuccess)
			goto exit;

		// Check the path, username and password
		while(1)
		{
			bGlobalProfileUpdate = false;
			bUpdatedPageConnection = false;

			if((bShowDialog && ulFlags & SERVICE_UI_ALLOWED) || ulFlags & SERVICE_UI_ALWAYS )
			{
				hr = MAPI_E_USER_CANCEL;
			}// if(bShowDialog...)

						
			if(!(ulFlags & SERVICE_UI_ALLOWED || ulFlags & SERVICE_UI_ALWAYS) && (strServerName.empty() || sProfileProps.strUserName.empty())){
				hr = MAPI_E_UNCONFIGURED;
				goto exit;
			}else if(!strServerName.empty() && !sProfileProps.strUserName.empty()) {
				//Logon the server
				hr = lpTransport->HrLogon(sProfileProps);
			}else{
				hr = MAPI_E_LOGON_FAILED;
			}

			if(hr == MAPI_E_LOGON_FAILED || hr == MAPI_E_NETWORK_ERROR || hr == MAPI_E_VERSION || hr == MAPI_E_INVALID_PARAMETER) {
				bShowDialog = true;
			} else if(hr != erSuccess){ // Big error?
				bShowDialog = true;
				ASSERT(FALSE);
			}else {
				//Update global profile
				if( bGlobalProfileUpdate == true) {

					cValues = 12;
					cValueIndex = 0;
					hr = MAPIAllocateBuffer(sizeof(SPropValue) * cValues, (void**)&lpsPropValue);
					if(hr != hrSuccess)
						goto exit;

					lpsPropValue[cValueIndex].ulPropTag	= PR_EC_PATH;
					lpsPropValue[cValueIndex++].Value.lpszA = (char *)sProfileProps.strServerPath.c_str();

					lpsPropValue[cValueIndex].ulPropTag	= PR_EC_USERNAME_W;
					lpsPropValue[cValueIndex++].Value.lpszW = (wchar_t *)sProfileProps.strUserName.c_str();

					lpsPropValue[cValueIndex].ulPropTag = PR_EC_USERPASSWORD_W;
					lpsPropValue[cValueIndex++].Value.lpszW = (wchar_t *)sProfileProps.strPassword.c_str();

					lpsPropValue[cValueIndex].ulPropTag = PR_EC_FLAGS;
					lpsPropValue[cValueIndex++].Value.ul = sProfileProps.ulProfileFlags;

					lpsPropValue[cValueIndex].ulPropTag = PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION;
					lpsPropValue[cValueIndex++].Value.lpszA = (char *)sProfileProps.strClientAppVersion.c_str();

					lpsPropValue[cValueIndex].ulPropTag = PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC;
					lpsPropValue[cValueIndex++].Value.lpszA = (char *)sProfileProps.strClientAppMisc.c_str();

					if (bUpdatedPageConnection == true)
					{
						lpsPropValue[cValueIndex].ulPropTag = PR_EC_CONNECTION_TIMEOUT;
						lpsPropValue[cValueIndex++].Value.ul = sProfileProps.ulConnectionTimeOut;

						// Proxy settings
						lpsPropValue[cValueIndex].ulPropTag = PR_EC_PROXY_FLAGS;
						lpsPropValue[cValueIndex++].Value.ul = sProfileProps.ulProxyFlags;

						lpsPropValue[cValueIndex].ulPropTag = PR_EC_PROXY_PORT;
						lpsPropValue[cValueIndex++].Value.ul = sProfileProps.ulProxyPort;

						lpsPropValue[cValueIndex].ulPropTag = PR_EC_PROXY_HOST;
						lpsPropValue[cValueIndex++].Value.lpszA = (char *)sProfileProps.strProxyHost.c_str();
							
						lpsPropValue[cValueIndex].ulPropTag = PR_EC_PROXY_USERNAME;
						lpsPropValue[cValueIndex++].Value.lpszA = (char *)sProfileProps.strProxyUserName.c_str();

						lpsPropValue[cValueIndex].ulPropTag = PR_EC_PROXY_PASSWORD;
						lpsPropValue[cValueIndex++].Value.lpszA = (char *)sProfileProps.strProxyPassword.c_str();
					}

					hr = ptrGlobalProfSect->SetProps(cValueIndex, lpsPropValue, NULL);
					if(hr != hrSuccess)
						goto exit;
					
					//Free allocated memory
					MAPIFreeBuffer(lpsPropValue);
					lpsPropValue = NULL;

				}
				break; // Everything is oke
			}
			

			// On incorrect password, and UI allowed, show incorrect password error
			if((ulFlags & SERVICE_UI_ALLOWED || ulFlags & SERVICE_UI_ALWAYS)) {
				// what do we do on linux?
				cout << "Access Denied: Incorrect username and/or password." << endl;
				hr = MAPI_E_UNCONFIGURED;
				goto exit;
			}else if(!(ulFlags & SERVICE_UI_ALLOWED || ulFlags & SERVICE_UI_ALWAYS)){
				// Do not reset the logon error from HrLogon()
				// The DAgent uses this value to determain if the delivery is fatal or not
				// 
				// Although this error is not in the online spec from MS, it should not really matter .... right?
				// hr = MAPI_E_UNCONFIGURED;
				goto exit;
			}

		}// while(1)

		if(bInitStores) {
			hr = UpdateProviders(lpAdminProviders, sProfileProps, lpTransport);
			if(hr != hrSuccess)
				goto exit;
		}
		break;
	} // switch(ulContext)


exit:
	if (lppMapiError) {
		
		*lppMapiError = NULL;

		if(hr != hrSuccess) {
			LPTSTR lpszErrorMsg;

			if (Util::HrMAPIErrorToText(hr, &lpszErrorMsg) == hrSuccess) {
				// Set Error
				strError = _T("EntryPoint: ");
				strError += lpszErrorMsg;
				MAPIFreeBuffer(lpszErrorMsg);

				// Some outlook 2007 clients can't allocate memory so check it
				if(MAPIAllocateBuffer(sizeof(MAPIERROR), (void**)&lpMapiError) == hrSuccess) { 

					memset(lpMapiError, 0, sizeof(MAPIERROR));				

					if ((ulFlags & MAPI_UNICODE) == MAPI_UNICODE) {
						std::wstring wstrErrorMsg = convert_to<std::wstring>(strError);
						std::wstring wstrCompName = convert_to<std::wstring>(g_strProductName.c_str());
							
						if ((hr = MAPIAllocateMore(sizeof(std::wstring::value_type) * (wstrErrorMsg.size() + 1), lpMapiError, (void**)&lpMapiError->lpszError)) != hrSuccess)
							goto exit;
						wcscpy((wchar_t*)lpMapiError->lpszError, wstrErrorMsg.c_str());
						
						if ((hr = MAPIAllocateMore(sizeof(std::wstring::value_type) * (wstrCompName.size() + 1), lpMapiError, (void**)&lpMapiError->lpszComponent)) != hrSuccess)
							goto exit;
						wcscpy((wchar_t*)lpMapiError->lpszComponent, wstrCompName.c_str()); 
					} else {
						std::string strErrorMsg = convert_to<std::string>(strError);
						std::string strCompName = convert_to<std::string>(g_strProductName.c_str());

						if ((hr = MAPIAllocateMore(strErrorMsg.size() + 1, lpMapiError, (void**)&lpMapiError->lpszError)) != hrSuccess)
							goto exit;
						strcpy((char*)lpMapiError->lpszError, strErrorMsg.c_str());
						
						if ((hr = MAPIAllocateMore(strCompName.size() + 1, lpMapiError, (void**)&lpMapiError->lpszComponent)) != hrSuccess)
							goto exit;
						strcpy((char*)lpMapiError->lpszComponent, strCompName.c_str());  
					}
				
					lpMapiError->ulVersion = 0;
					lpMapiError->ulLowLevelError = 0;
					lpMapiError->ulContext = 0;

					*lppMapiError = lpMapiError;
				}
			}
		}
	}

	MAPIFreeBuffer(lpDelegateStores);
	if(lpTransport)
		lpTransport->Release();
	MAPIFreeBuffer(lpsPropValue);
	TRACE_MAPI(TRACE_RETURN, "MSGServiceEntry", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}


extern "C" HRESULT __cdecl XPProviderInit(HINSTANCE hInstance, LPMALLOC lpMalloc, LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore, LPFREEBUFFER lpFreeBuffer, ULONG ulFlags, ULONG ulMAPIVer, ULONG * lpulProviderVer, LPXPPROVIDER * lppXPProvider)
{
	TRACE_MAPI(TRACE_ENTRY, "XPProviderInit", "");

	HRESULT hr = hrSuccess;
	ECXPProvider	*pXPProvider = NULL;

    if (ulMAPIVer < CURRENT_SPI_VERSION)
    {
        hr = MAPI_E_VERSION;
		goto exit;
    }

	*lpulProviderVer = CURRENT_SPI_VERSION;

	// Save the pointer to the allocation routines in global variables
	_pmalloc = lpMalloc;
	_pfnAllocBuf = lpAllocateBuffer;
	_pfnAllocMore = lpAllocateMore;
	_pfnFreeBuf = lpFreeBuffer;
	_hInstance = hInstance;

	hr = ECXPProvider::Create(&pXPProvider);
	if(hr != hrSuccess)
		goto exit;

	hr = pXPProvider->QueryInterface(IID_IXPProvider, (void **)lppXPProvider);

exit:
	if(pXPProvider)
		pXPProvider->Release();


	return hr;
}


extern "C" HRESULT  __cdecl ABProviderInit(HINSTANCE hInstance, LPMALLOC lpMalloc, LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore, LPFREEBUFFER lpFreeBuffer, ULONG ulFlags, ULONG ulMAPIVer, ULONG * lpulProviderVer, LPABPROVIDER * lppABProvider)
{
	TRACE_MAPI(TRACE_ENTRY, "ABProviderInit", "");

	HRESULT hr = hrSuccess;
	ECABProviderSwitch	*lpABProvider = NULL;

	if (ulMAPIVer < CURRENT_SPI_VERSION)
	{
		hr = MAPI_E_VERSION;
		goto exit;
	}

	*lpulProviderVer = CURRENT_SPI_VERSION;
	// Save the pointer to the allocation routines in global variables
	_pmalloc = lpMalloc;
	_pfnAllocBuf = lpAllocateBuffer;
	_pfnAllocMore = lpAllocateMore;
	_pfnFreeBuf = lpFreeBuffer;
	_hInstance = hInstance;

	hr = ECABProviderSwitch::Create(&lpABProvider);
	if(hr != hrSuccess)
		goto exit;

	hr = lpABProvider->QueryInterface(IID_IABProvider, (void **)lppABProvider);

exit:
	if(lpABProvider)
		lpABProvider->Release();


	return hr;
}
