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

#if defined(_WIN32) && !defined(WINCE)
#include "SettingsDlg.h"
#include "SettingsTabConnection.h"
#include "SettingsPropPageAdvance.h"
#include <kopano/CommonUtil.h>
#include "MapiClientReg.h"
#include "ProgressDlg.h"
#endif
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

#if defined(WINCE)
// Wince
#elif defined(WIN32)

class CKopanoApp : public CWinApp
{
public:
// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CKopanoApp)
	public:
    virtual BOOL InitInstance();
    virtual int ExitInstance();

	//}}AFX_VIRTUAL

	//{{AFX_MSG(CKopanoApp)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CKopanoApp, CWinApp)
	//{{AFX_MSG_MAP(CKopanoApp)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

CKopanoApp theApp;


BOOL CKopanoApp::InitInstance()
{
	BOOL bResult = FALSE;
	HKEY hKey = NULL;
	BYTE szDir[MAX_PATH];
	TCHAR szShort[MAX_PATH];
	ULONG cbDir = 0;
	ULONG ulLoadsim = 0;
	tstring strPath;

	InitCommonControls();

	g_strManufacturer = _T("Kopano");
	g_strProductName = _T("Kopano Core");
	g_isOEM = false;
	g_ulLoadsim = FALSE;

	if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("Software\\Kopano\\Client"), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		cbDir = sizeof(szDir);
		if(RegQueryValueEx(hKey, _T("Manufacturer"), NULL, NULL, szDir, &cbDir) == ERROR_SUCCESS) {
			g_strManufacturer = (LPTSTR)szDir;
			g_isOEM = true;
		}

		cbDir = sizeof(szDir);
		if(RegQueryValueEx(hKey, _T("ProductName"), NULL, NULL, szDir, &cbDir) == ERROR_SUCCESS) {
			g_strProductName = (LPTSTR)szDir;
			g_isOEM = true;
		}

		cbDir = sizeof(szDir);
		if(RegQueryValueEx(hKey, _T("InstallDir"), NULL, NULL, szDir, &cbDir) == ERROR_SUCCESS)
			g_strKopanoDirectory = (LPTSTR)szDir;

		cbDir = sizeof(szDir);
		if(RegQueryValueEx(hKey, _T("ProductNameShort"), NULL, NULL, szDir, &cbDir) == ERROR_SUCCESS)
			g_strProductNameShort = (LPTSTR)szDir;
		
		cbDir = sizeof(ULONG);
		if(RegQueryValueEx(hKey, _T("LoadSim"), NULL, NULL, (BYTE*)&ulLoadsim, &cbDir) == ERROR_SUCCESS)
			g_ulLoadsim = ulLoadsim;

		RegCloseKey(hKey);
	}

	// Get HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\CommonFilesDir
	if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion"), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return false;

	cbDir = MAX_PATH;
	if(RegQueryValueEx(hKey, _T("CommonFilesDir"), NULL, NULL, szDir, &cbDir) != ERROR_SUCCESS)
		return false;

	RegCloseKey(hKey);

	g_strCommonFilesKopano = (LPTSTR)szDir;
	g_strCommonFilesKopano += _T("\\") + g_strManufacturer;

	strPath = g_strCommonFilesKopano + _T("\\locale");

#ifdef UNICODE
	// bindtextdomain takes a non-unicode path argument, which we don't have if compiled with UNICODE. Our
	// best bet is to get the short form of the required directory, which will contain only ANSI characters.
	// However the 8.3 aliasses can be disabled. In that case we'll just convert the UNICODE path to ANSI
	// and pray for the best.
	if (GetShortPathName(strPath.c_str(), szShort, sizeof(szShort)) > 0)
		bindtextdomain("kopano", convert_to<std::string>(szShort).c_str());
	else
		bindtextdomain("kopano", convert_to<std::string>(strPath).c_str());
#else
	bindtextdomain("kopano", strPath.c_str());
#endif

	// CSIDL_APPDATA		=> C:\Documents and Settings\username\Application Data
	// CSIDL_LOCAL_APPDATA	=> C:\Documents and Settings\username\Local Settings\Application Data
	// CSIDL_PROGRAM_FILES_COMMON => C:\Program Files\Common files
	if(SUCCEEDED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, (LPTSTR)szDir))) {
		g_strUserLocalAppDataKopano = (LPTSTR)szDir;
		g_strUserLocalAppDataKopano += _T("\\");
		g_strUserLocalAppDataKopano += g_strManufacturer.c_str();
	}

	/*g_dwSecondsToWaitStartOfflineServer = 5; //
	if(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Kopano\\Client", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {

	//	lRegRet = RegQueryValueExA(hKey, "Timeout", NULL, &dwType, (LPBYTE)&dwData, &cbData);
	
		RegCloseKey(hKey);
	}*/

	ssl_threading_setup();
	return CWinApp::InitInstance();
}

int CKopanoApp::ExitInstance()
{

    ssl_threading_cleanup();

	RemoveAllProviders(&g_mapProviders);

#if DEBUG_WITH_MEMORY_DUMP
	// Dump the memleak objects
	_CrtMemDumpAllObjectsSince(0);
#endif

	return CWinApp::ExitInstance();
}
#else

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

#endif // #ifdef Windows32

///////////////////////////////////////////////////////////////////
// entrypoints
//

// Called by MAPI to return a MSProvider object when a user opens a store based on our service
extern "C" HRESULT __cdecl MSProviderInit(HINSTANCE hInstance, LPMALLOC pmalloc, LPALLOCATEBUFFER pfnAllocBuf, LPALLOCATEMORE pfnAllocMore, LPFREEBUFFER pfnFreeBuf, ULONG ulFlags, ULONG ulMAPIver, ULONG * lpulProviderVer, LPMSPROVIDER * ppmsp)
{
#if defined(WIN32) && !defined(WINCE)
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
#endif
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
#ifdef WIN32
	HRESULT hr = hrSuccess;
	IProfSect *lpProfSect = NULL;
	LPSPropValue lpProp =  NULL;

	hr = lpProviderAdmin->OpenProfileSection(NULL, NULL, 0, &lpProfSect);
	if(hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(lpProfSect, PR_SERVICE_NAME_A, &lpProp);
	if(hr != hrSuccess)
		goto exit;

	lpServiceName->assign(lpProp->Value.lpszA);

exit:
	if(lpProfSect)
		lpProfSect->Release();
	MAPIFreeBuffer(lpProp);
	return hr;
#else
	lpServiceName->assign("ZARAFA6");
	return hrSuccess;
#endif
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
HRESULT InitializeProvider(LPPROVIDERADMIN lpAdminProvider, IProfSect *lpProfSect, sGlobalProfileProps sProfileProps, ULONG *lpcStoreID, LPENTRYID *lppStoreID)
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
#ifdef WIN32
	unsigned int	ulClientVersion = (unsigned int)-1;
	std::string		strLStoreName;
#endif

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

	hr = WSTransport::Create(0, &lpTransport);
	if(hr != hrSuccess)
		goto exit;

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

#ifdef WIN32
		GetClientVersion(&ulClientVersion);
		if(ulClientVersion <= CLIENT_VERSION_OLK2000)
		{
			// outlook 2000 doesn't do unicode
			strLStoreName = convert_to<string>(ptrStoreName.get());
			sPropVals[cPropValue].ulPropTag = PR_DISPLAY_NAME_A;
			sPropVals[cPropValue++].Value.lpszA = (char*)strLStoreName.c_str();
		}
		else
#endif
		{
			sPropVals[cPropValue].ulPropTag = PR_DISPLAY_NAME_W;
			sPropVals[cPropValue++].Value.lpszW = ptrStoreName.get();
		}

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
	if (lpTransport)
		lpTransport->Release();

	return hr;
}

static HRESULT UpdateProviders(LPPROVIDERADMIN lpAdminProviders,
    const sGlobalProfileProps &sProfileProps)
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

		hr = InitializeProvider(lpAdminProviders, ptrProfSect, sProfileProps, NULL, NULL);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

// Called by MAPI to configure, or create a service
extern "C" HRESULT __stdcall MSGServiceEntry(HINSTANCE hInst, LPMALLOC lpMalloc, LPMAPISUP psup, ULONG ulUIParam, ULONG ulFlags, ULONG ulContext, ULONG cvals, LPSPropValue pvals, LPPROVIDERADMIN lpAdminProviders, MAPIERROR **lppMapiError)
{
#if defined(WIN32) && !defined(WINCE)
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
#endif
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
	LPBYTE			lpDeligateStores = NULL;
	ULONG			cDeligateStores = 0;
	LPSPropValue	lpsPropValueFind = NULL;
	ULONG 			cValueIndex = 0;
	convert_context	converter;

	bool bGlobalProfileUpdate = false;
	bool bUpdatedPageConnection = false;
	bool bInitStores = true;

#ifdef _WIN32
	unsigned int	d = 0;
	LPCTSTR			lpszDisplayName = NULL;
	ECUSER *lpECUser = NULL;
	ULONG			UIFlags = 0;
	CProgressDlg	*lpProgressDlg = NULL;
#endif


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

#if defined(_WIN32) && !defined(WINCE)
	//Init defaults
	if(ulUIParam == 0)
		ulUIParam = (ULONG)GetWindow(GetDesktopWindow(), GW_CHILD);
#endif

	// Logon defaults
	strType = "http";
	strServerName = "";
	strServerPort ="236";

#if defined(_WIN32) && !defined(WINCE)
	if(ulFlags & MSG_SERVICE_UI_READ_ONLY)
		UIFlags = DISABLE_PATH | DISABLE_USER;
#endif


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

			hr = InitializeProvider(lpAdminProviders, ptrProfSect, sProfileProps, NULL, NULL);
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
		ClientUtil::GetGlobalProfileDeligateStoresProp(ptrGlobalProfSect, &cDeligateStores, &lpDeligateStores);

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
#if defined(_WIN32) && !defined(WINCE)
				// Show a setting dialog
				CWnd *lpParentWnd = CWnd::FromHandle((HWND)ulUIParam); 
				
				CPropertySheet				dlgSettingsPropSheet(g_strProductName.c_str(), lpParentWnd);
				CSettingsPropPageAdvance	pageAdvanced;
				CSettingsTabConnection		pageConnection;
				CSettingsDlg				pageDefault;

				pageDefault.InitDefaults(converter.convert_to<LPTSTR>(strServerName), 
										 converter.convert_to<LPTSTR>(sProfileProps.strUserName),
										 _T(""), 
										 converter.convert_to<LPTSTR>(strServerPort), 
										 strType == "https", UIFlags);

				pageDefault.SetConnectionFlags(sProfileProps.ulProfileFlags & (EC_PROFILE_FLAGS_OFFLINE|EC_PROFILE_FLAGS_CACHE_PRIVATE|EC_PROFILE_FLAGS_CACHE_PUBLIC) );

				dlgSettingsPropSheet.AddPage(&pageDefault);

				pageConnection.InitDefaults(!(sProfileProps.ulProfileFlags & EC_PROFILE_FLAGS_NO_COMPRESSION),
											sProfileProps.ulConnectionTimeOut,
											sProfileProps.ulProxyFlags, converter.convert_to<LPTSTR>(sProfileProps.strProxyHost),
											sProfileProps.ulProxyPort, converter.convert_to<LPTSTR>(sProfileProps.strProxyUserName),
											converter.convert_to<LPTSTR>(sProfileProps.strProxyPassword));

				dlgSettingsPropSheet.AddPage(&pageConnection);				
				
				if(bShowAllSettingsPages)
				{
					hr = lpTransport->HrLogon(sProfileProps);
					if(hr != hrSuccess) {
						bShowAllSettingsPages = false; //Skip other settingpages
					} else {
						// Open a session to get an addressbook
						ECLogger_Null ecLogger;
						hr = HrOpenECSession(&ecLogger, &ptrSession, "provider/client", PROJECT_SVN_REV_STR, sProfileProps.strUserName.c_str(), sProfileProps.strPassword.c_str(), sProfileProps.strServerPath.c_str(), 0, NULL, NULL);
						if(hr != hrSuccess)
							bShowAllSettingsPages = false; //Skip other settingpages
					}
				}

				if(bShowAllSettingsPages)
				{
					////////////////////////////////////////////
					// Add pages
					dlgSettingsPropSheet.AddPage(&pageAdvanced);
					
					////////////////////////////////////////////
					// Set default settings
					pageAdvanced.InitDefaults(ptrSession);

					// Add deligate stores
					if(cDeligateStores >= sizeof(MAPIUID))
					{
						SPropArrayPtr	ptrProfSectProps;

						for (d = 0; d < cDeligateStores / sizeof(MAPIUID); ++d) {
							SizedSPropTagArray(2, sptaDelegateProps) = {2, {PR_DISPLAY_NAME, PR_MDB_PROVIDER}};

							hr = lpAdminProviders->OpenProfileSection((LPMAPIUID)(lpDeligateStores+(sizeof(MAPIUID)*d)), NULL, MAPI_MODIFY, &ptrProfSect);
							if(hr != hrSuccess)
								goto exit;

							hr = ptrProfSect->GetProps((LPSPropTagArray)&sptaDelegateProps, 0, &cValues, &ptrProfSectProps);
							if (HR_FAILED(hr))
								goto exit;
							if (PROP_TYPE(ptrProfSectProps[1].ulPropTag) == PT_ERROR) {
								hr = ptrProfSectProps[1].Value.err;
								goto exit;
							}
							if (ptrProfSectProps[1].Value.bin.cb != sizeof(MAPIUID) || memcmp(ptrProfSectProps[1].Value.bin.lpb, &KOPANO_STORE_DELEGATE_GUID, sizeof(MAPIUID)) != 0)
								continue;

							if (PROP_TYPE(ptrProfSectProps[0].ulPropTag) == PT_ERROR)
								lpszDisplayName = _("Unknown username");
							else
								lpszDisplayName = ptrProfSectProps[0].Value.LPSZ;
							
							//@FIXME: do something with this error
							hr = pageAdvanced.AddMailBox(lpszDisplayName, 0, NULL, (LPMAPIUID)(lpDeligateStores+(sizeof(MAPIUID)*d)), MAILBOXITEM_STATE_NORMAL);
						}
					}

				}// if(bShowAllSettingsPages)

				if(dlgSettingsPropSheet.DoModal() == IDOK)
				{
					//////////////////////////////////////////
					// Set the new settings

					if (pageDefault.IsChanged())
					{
						strServerName	= converter.convert_to<std::string>(pageDefault.GetServerName());
						strServerPort	= converter.convert_to<std::string>(pageDefault.GetServerPort());
						strType			= pageDefault.GetSSL() ? "https" : "http";

						sProfileProps.strUserName	= converter.convert_to<std::wstring>(pageDefault.GetUserName());
						if(_tcslen(pageDefault.GetUserPassword()) > 0)
							sProfileProps.strPassword	= SymmetricCryptW(converter.convert_to<std::wstring>(pageDefault.GetUserPassword()));
						sProfileProps.strServerPath	= ServerNamePortToURL(strType.c_str(), strServerName.c_str() , strServerPort.c_str());

						sProfileProps.ulProfileFlags &=~(EC_PROFILE_FLAGS_OFFLINE|EC_PROFILE_FLAGS_CACHE_PRIVATE|EC_PROFILE_FLAGS_CACHE_PUBLIC);
						sProfileProps.ulProfileFlags |= pageDefault.GetConnectionFlags();
						
						bGlobalProfileUpdate = true;
					}

					if (pageConnection.IsChanged())
					{
						sProfileProps.ulProfileFlags &=~(EC_PROFILE_FLAGS_NO_COMPRESSION);
						sProfileProps.ulProfileFlags |= (pageConnection.UseCompression())? 0 : EC_PROFILE_FLAGS_NO_COMPRESSION;
						sProfileProps.ulConnectionTimeOut = pageConnection.GetConnectionTimeOut();
						
						sProfileProps.ulProxyFlags = pageConnection.GetProxyFlags();
						sProfileProps.strProxyHost = converter.convert_to<std::string>(pageConnection.GetProxyHost());
						sProfileProps.ulProxyPort = pageConnection.GetProxyPort();
						sProfileProps.strProxyUserName = converter.convert_to<std::string>(pageConnection.GetProxyUserName());
						sProfileProps.strProxyPassword = converter.convert_to<std::string>(pageConnection.GetProxyPassword());

						bUpdatedPageConnection = true;
						bGlobalProfileUpdate = true;
					}

					if(bShowAllSettingsPages)
					{
						// Check for new and removed deligatestores
						for (d = 0; d < pageAdvanced.GetMailBoxCount(); ++d) {
							if(pageAdvanced.GetMailBoxItemPtr(d)!= NULL)
							{
								if(pageAdvanced.GetMailBoxItemPtr(d)->ulState == MAILBOXITEM_STATE_NEW) {
								
									LPSECMailBoxItem lpSECMailBoxItem = pageAdvanced.GetMailBoxItemPtr(d);

									hr = lpTransport->HrGetUser(lpSECMailBoxItem->sUserId.cb, (LPENTRYID)lpSECMailBoxItem->sUserId.lpb, MAPI_UNICODE, &lpECUser);
									if(hr != hrSuccess)
										goto exit;
									
									hr = HrAddECMailBox(lpAdminProviders, (LPWSTR)lpECUser->lpszUsername);
									if(hr != hrSuccess)
										goto exit;
								}else if(pageAdvanced.GetMailBoxItemPtr(d)->ulState == MAILBOXITEM_STATE_REMOVE) {
									hr = HrRemoveECMailBox(lpAdminProviders, &pageAdvanced.GetMailBoxItemPtr(d)->uidProvider);
									if(hr != hrSuccess)
										goto exit;
								}
								
							}
						}

						bShowAllSettingsPages = false;
					} // if(bShowAllSettingsPages)
					
					lpTransport->HrLogOff();

				} else {
					hr = MAPI_E_USER_CANCEL;
					goto exit;
				}// if(dlgSettingsPropSheet.DoModal() == IDOK)

#else
				hr = MAPI_E_USER_CANCEL;
#endif // #if defined(_WIN32) && !defined(WINCE)

			}// if(bShowDialog...)

						
			if(!(ulFlags & SERVICE_UI_ALLOWED || ulFlags & SERVICE_UI_ALWAYS) && (strServerName.empty() || sProfileProps.strUserName.empty())){
				hr = MAPI_E_UNCONFIGURED;
				goto exit;
			}else if(!strServerName.empty() && !sProfileProps.strUserName.empty()) {
#if defined(_WIN32)
				if (ulFlags & SERVICE_UI_ALLOWED || ulFlags & SERVICE_UI_ALWAYS ) {
					lpProgressDlg  = new CProgressDlg((HWND)ulUIParam);
					lpProgressDlg->SetTitle(g_strProductName.c_str());
					lpProgressDlg->SetShowCancelButton(false);
					lpProgressDlg->SetLine(1, converter.convert_to<LPTSTR>(_("Connecting to the server...")));
					lpProgressDlg->SetCalculateTime();
					lpProgressDlg->ShowModal(true);
					
					lpProgressDlg->UpdateProgress((DWORD)1, (DWORD)sProfileProps.ulConnectionTimeOut);
				}
#endif // #if defined(_WIN32)

				//Logon the server
				hr = lpTransport->HrLogon(sProfileProps);
#if defined(_WIN32)
				if (lpProgressDlg) {
					lpProgressDlg->EndDialog();
					delete lpProgressDlg;
					lpProgressDlg = NULL;
				}

				if (hr == MAPI_E_NETWORK_ERROR &&
					(ulFlags & SERVICE_UI_ALLOWED || ulFlags & SERVICE_UI_ALWAYS) )
				{
					if (MessageBox((HWND)ulUIParam, _("Unable to reach the server.\nDo you want to save the settings (OK) or (Cancel) and re-enter the settings?"), g_strProductName.c_str(), MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
					{
						bInitStores = false; // To besure the stores does not connect to the server
						hr = hrSuccess;
					} else {
						// We want to re-enter the settings so show the dialog again
						bShowDialog = true;
						continue;
					}
				}else if(hr == MAPI_E_SESSION_LIMIT && (ulFlags & SERVICE_UI_ALLOWED || ulFlags & SERVICE_UI_ALWAYS)) {
					if(!(ulFlags & MDB_NO_DIALOG)) {
						MessageBox((HWND)ulUIParam, _("Cannot use the profile because you are over the license limit. To continue, you must purchase additional client licenses."), g_strProductName.c_str(), MB_ICONEXCLAMATION | MB_OK);
					}
					bShowDialog = true;
					continue;
				}else if(hr == MAPI_E_NO_ACCESS && (ulFlags & SERVICE_UI_ALLOWED || ulFlags & SERVICE_UI_ALWAYS)) {
					if(!(ulFlags & MDB_NO_DIALOG)) {
						MessageBox((HWND)ulUIParam, _("Cannot use the profile because the server was unable to contact the license server. Please consult your system administrator."), g_strProductName.c_str(), MB_ICONEXCLAMATION | MB_OK);
					}
					bShowDialog = true;
					continue;
				}
#endif
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
#ifdef WINCE
				hr = MAPI_E_UNCONFIGURED;
				goto exit;
#elif defined(_WIN32)
				if (hr == MAPI_E_LOGON_FAILED)
					MessageBox((HWND)ulUIParam, _("Incorrect username and/or password."), g_strProductName.c_str(), MB_OK | MB_ICONEXCLAMATION);
				else if (hr == MAPI_E_NETWORK_ERROR) // NOTE: Not possible to get this error on this point
					MessageBox((HWND)ulUIParam, _("Incorrect servername and/or port\nCheck if the server is running."), g_strProductName.c_str(), MB_OK | MB_ICONEXCLAMATION);
				else if (hr == MAPI_E_VERSION) 
					MessageBox((HWND)ulUIParam, _("Incorrect version."), g_strProductName.c_str(), MB_OK | MB_ICONEXCLAMATION);
				else
					MessageBox((HWND)ulUIParam, _("An unknown failure has occurred.\nPlease contact your administrator."), g_strProductName.c_str(), MB_OK | MB_ICONEXCLAMATION);
#else
				// what do we do on linux?
				cout << "Access Denied: Incorrect username and/or password." << endl;
				hr = MAPI_E_UNCONFIGURED;
				goto exit;
#endif
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
			hr = UpdateProviders(lpAdminProviders, sProfileProps);
			if(hr != hrSuccess)
				goto exit;
		}

#ifdef WIN32
		if (ulContext == MSG_SERVICE_CONFIGURE && bGlobalProfileUpdate == true && (ulFlags & SERVICE_UI_ALLOWED || ulFlags & SERVICE_UI_ALWAYS)) {
			MessageBox((HWND)ulUIParam, _("You should restart Outlook for the changes to take effect."), g_strProductName.c_str(), MB_OK | MB_ICONEXCLAMATION);
		}
#endif

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

	MAPIFreeBuffer(lpDeligateStores);
	if(lpTransport)
		lpTransport->Release();
	MAPIFreeBuffer(lpsPropValue);

#ifdef _WIN32
	if(lpECUser)
		ECFreeBuffer(lpECUser);

	if (lpProgressDlg) {
		lpProgressDlg->EndDialog();
		delete lpProgressDlg;
	}
#endif

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

#ifdef WIN32
	{
		// Use version 2 flags, with DDLWRAP_FLAG_ANSI and DDLWRAP_FLAG_UNICODE. This enables unicode internally.
		// but only for OLK > 2000
		unsigned int ulClientVersion = CLIENT_VERSION_OLK2000;
		GetClientVersion(&ulClientVersion);
		
		if(ulClientVersion > CLIENT_VERSION_OLK2000)
			*lpulProviderVer = 0x20003;
		else
			*lpulProviderVer = CURRENT_SPI_VERSION;
	}
#else
	*lpulProviderVer = CURRENT_SPI_VERSION;
#endif

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

#ifdef WIN32
// Add kopano profile in MAPISVC.ini
extern "C" HRESULT __stdcall MergeWithMAPISVC()
{
	return HrSetProfileParameters(aInstall_ZarafaServicesIni, (g_isOEM)? g_strProductNameShort.c_str(): NULL);
}

// Remove kopano profile from MAPISVC.ini
extern "C" HRESULT __stdcall RemoveFromMAPISVC()
{
	return HrSetProfileParameters(aRemove_ZarafaServicesIni, (g_isOEM)? g_strProductNameShort.c_str(): NULL);
}

#endif
