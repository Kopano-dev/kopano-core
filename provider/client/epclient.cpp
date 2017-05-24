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

#include <cstdint>
#include <mapi.h>
#include <mapispi.h>
#include <mapiutil.h>

#include <kopano/ECGetText.h>
#include <kopano/memory.hpp>
#include <memory>
#include <string>
#include <cassert>

#include "kcore.hpp"
#include "Mem.h"

#include "DLLGlobal.h"
#include "ECMSProviderSwitch.h"
#include "ECABProviderSwitch.h"
#include <iostream>
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
using namespace KCHL;

struct initprov {
	IProviderAdmin *provadm;
	MAPIUID *provuid;
	IProfSect *profsect;
	WSTransport *transport;
	unsigned int count, eid_size, wrap_eid_size;
	SPropValue prop[6];
	EntryIdPtr eid;
	/* referenced from prop[n] */
	WStringPtr store_name;
	EntryIdPtr wrap_eid;
	memory_ptr<ABEID> abe_id;
};

static const uint32_t MAPI_S_SPECIAL_OK = MAKE_MAPI_S(0x900);

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

static HRESULT RemoveAllProviders(ECMapProvider *);
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

static HRESULT RemoveAllProviders(ECMapProvider *mp)
{
	if (mp == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	for (const auto &p : *mp) {
		if (p.second.lpMSProviderOnline)
			p.second.lpMSProviderOnline->Release();
		if (p.second.lpABProviderOnline)
			p.second.lpABProviderOnline->Release();
	}
	return hrSuccess;
}

// entrypoints

// Called by MAPI to return a MSProvider object when a user opens a store based on our service
HRESULT __cdecl MSProviderInit(HINSTANCE hInstance, LPMALLOC pmalloc,
    LPALLOCATEBUFFER pfnAllocBuf, LPALLOCATEMORE pfnAllocMore,
    LPFREEBUFFER pfnFreeBuf, ULONG ulFlags, ULONG ulMAPIver,
    ULONG *lpulProviderVer, LPMSPROVIDER *ppmsp)
{
	HRESULT hr = hrSuccess;
	object_ptr<ECMSProviderSwitch> lpMSProvider;

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
	hr = ECMSProviderSwitch::Create(ulFlags, &~lpMSProvider);
	if(hr != hrSuccess)
		goto exit;

	hr = lpMSProvider->QueryInterface(IID_IMSProvider, (void **)ppmsp); 

exit:
	return hr;
}

/**
 * Get the service name from the provider admin
 *
 * The service name is the string normally passed to CreateMsgService, like "ZARAFA6" or "MSEMS".
 *
 * @param lpProviderAdmin[in] The ProviderAdmin object passed to MSGServiceEntry
 * @param lpServiceName[out] The name of the message service
 */
static HRESULT GetServiceName(IProviderAdmin *lpProviderAdmin,
    std::string *lpServiceName)
{
	lpServiceName->assign("ZARAFA6");
	return hrSuccess;
}

static HRESULT
initprov_storepub(struct initprov &d, const sGlobalProfileProps &profprop)
{
	/* Get the public store */
	std::string redir_srv;
	HRESULT ret;

	if (profprop.ulProfileFlags & EC_PROFILE_FLAGS_NO_PUBLIC_STORE)
		/* skip over to the DeleteProvider part */
		ret = MAPI_E_INVALID_PARAMETER;
	else
		ret = d.transport->HrGetPublicStore(0, &d.eid_size, &~d.eid, &redir_srv);

	if (ret == MAPI_E_UNABLE_TO_COMPLETE) {
		d.transport->HrLogOff();
		auto new_props = profprop;
		new_props.strServerPath = redir_srv;
		ret = d.transport->HrLogon(new_props);
		if (ret == hrSuccess)
			ret = d.transport->HrGetPublicStore(0, &d.eid_size, &~d.eid);
	}
	if (ret == hrSuccess)
		return hrSuccess;
	if (d.provadm != NULL && d.provuid != NULL)
		d.provadm->DeleteProvider(d.provuid);
	/* Profile without public store */
	return MAPI_S_SPECIAL_OK;
}

static HRESULT
initprov_service(struct initprov &d, const sGlobalProfileProps &profprop)
{
	/* Get the default store for this user */
	std::string redir_srv;
	HRESULT ret = d.transport->HrGetStore(0, NULL, &d.eid_size, &~d.eid,
	              0, NULL, &redir_srv);
	if (ret == MAPI_E_NOT_FOUND) {
		ec_log_err("HrGetStore failed: No store present.");
		return ret;
	} else if (ret != MAPI_E_UNABLE_TO_COMPLETE) {
		return ret;
	}

	/* MAPI_E_UNABLE_TO_COMPLETE */
	d.transport->HrLogOff();
	auto new_props = profprop;
	new_props.strServerPath = redir_srv;
	ret = d.transport->HrLogon(new_props);
	if (ret != hrSuccess)
		return ret;
	ret = d.transport->HrGetStore(0, NULL, &d.eid_size, &~d.eid, 0, NULL);
	if (ret != hrSuccess)
		return ret;

	/* This should be a real URL */
	assert(redir_srv.compare(0, 9, "pseudo://") != 0);

	if (d.provadm == NULL || redir_srv.empty())
		return hrSuccess;

	/* Set/update the default store home server. */
	auto guid = reinterpret_cast<MAPIUID *>(const_cast<char *>(pbGlobalProfileSectionGuid));
	ProfSectPtr globprofsect;
	ret = d.provadm->OpenProfileSection(guid, nullptr, MAPI_MODIFY, &~globprofsect);
	if (ret != hrSuccess)
		return ret;

	SPropValue spv;
	spv.ulPropTag = PR_EC_PATH;
	spv.Value.lpszA = const_cast<char *>(redir_srv.c_str());
	return HrSetOneProp(globprofsect, &spv);
}

static HRESULT
initprov_storedl(struct initprov &d, const sGlobalProfileProps &profprop)
{
	/* PR_EC_USERNAME is the user we want to add ... */
	SPropValuePtr name;
	HRESULT ret = HrGetOneProp(d.profsect, PR_EC_USERNAME_W, &~name);
	if (ret != hrSuccess)
		ret = HrGetOneProp(d.profsect, PR_EC_USERNAME_A, &~name);
	if (ret != hrSuccess) {
		/*
		 * This should probably be done in UpdateProviders. But
		 * UpdateProviders does not know the type of the provider and it
		 * should not just delete the provider for all types of
		 * providers.
		 */
		if (d.provadm != NULL && d.provuid != NULL)
			d.provadm->DeleteProvider(d.provuid);
		/* Invalid or empty delegate store */
		return MAPI_S_SPECIAL_OK;
	}

	std::string redir_srv;
	ret = d.transport->HrResolveUserStore(convstring::from_SPropValue(name),
	      0, NULL, &d.eid_size, &~d.eid, &redir_srv);
	if (ret != MAPI_E_UNABLE_TO_COMPLETE)
		return ret;

	d.transport->HrLogOff();
	auto new_props = profprop;
	new_props.strServerPath = redir_srv;
	ret = d.transport->HrLogon(new_props);
	if (ret != hrSuccess)
		return ret;
	return d.transport->HrResolveUserStore(convstring::from_SPropValue(name),
	       0, NULL, &d.eid_size, &~d.eid);
}

static HRESULT initprov_storearc(struct initprov &d)
{
	// We need to get the username and the server name or url from the profsect.
	// That's enough information to get the entryid from the correct server. There's no redirect
	// available when resolving archive stores.
	SPropValuePtr name, server;
	HRESULT ret = HrGetOneProp(d.profsect, PR_EC_USERNAME_W, &~name);
	if (ret != hrSuccess)
		ret = HrGetOneProp(d.profsect, PR_EC_USERNAME_A, &~name);
	if (ret == hrSuccess) {
		ret = HrGetOneProp(d.profsect, PR_EC_SERVERNAME_W, &~server);
		if (ret != hrSuccess)
			ret = HrGetOneProp(d.profsect, PR_EC_SERVERNAME_A, &~server);
		if (ret != hrSuccess)
			return MAPI_E_UNCONFIGURED;
	}
	if (ret != hrSuccess) {
		/*
		 * This should probably be done in UpdateProviders. But
		 * UpdateProviders does not know the type of the provider and
		 * it should not just delete the provider for all types of
		 * providers.
		 */
		if (d.provadm != NULL && d.provuid != NULL)
			d.provadm->DeleteProvider(d.provuid);
		/* Invalid or empty archive store */
		return MAPI_S_SPECIAL_OK;
	}

	WSTransport *alt_transport;
	ret = GetTransportToNamedServer(d.transport, server->Value.LPSZ,
	      (PROP_TYPE(name->ulPropTag) == PT_STRING8 ? 0 : MAPI_UNICODE),
	      &alt_transport);
	if (ret != hrSuccess)
		return ret;

	std::swap(d.transport, alt_transport);
	alt_transport->Release();
	alt_transport = NULL;
	return d.transport->HrResolveTypedStore(convstring::from_SPropValue(name),
	       ECSTORE_TYPE_ARCHIVE, &d.eid_size, &~d.eid);
}

static HRESULT
initprov_mapi_store(struct initprov &d, const sGlobalProfileProps &profprop)
{
	SPropValuePtr mdb;
	HRESULT ret = HrGetOneProp(d.profsect, PR_MDB_PROVIDER, &~mdb);
	if (ret != hrSuccess)
		return ret;

	if (CompareMDBProvider(mdb->Value.bin.lpb, &KOPANO_STORE_PUBLIC_GUID)) {
		ret = initprov_storepub(d, profprop);
		if (ret != hrSuccess)
			return ret;
	} else if (CompareMDBProvider(mdb->Value.bin.lpb, &KOPANO_SERVICE_GUID)) {
		ret = initprov_service(d, profprop);
		if (ret != hrSuccess)
			return ret;
	} else if(CompareMDBProvider(mdb->Value.bin.lpb, &KOPANO_STORE_DELEGATE_GUID)) {
		ret = initprov_storedl(d, profprop);
		if (ret != hrSuccess)
			return ret;
	} else if(CompareMDBProvider(mdb->Value.bin.lpb, &KOPANO_STORE_ARCHIVE_GUID)) {
		ret = initprov_storearc(d);
		if (ret != hrSuccess)
			return ret;
	} else {
		assert(false); // unknown GUID?
		return hrSuccess;
	}

	ret = d.transport->HrGetStoreName(d.eid_size, d.eid, MAPI_UNICODE,
	      static_cast<LPTSTR *>(&~d.store_name));
	if (ret != hrSuccess)
		return ret;
	ret = WrapStoreEntryID(0, reinterpret_cast<const TCHAR *>(WCLIENT_DLL_NAME),
	      d.eid_size, d.eid, &d.wrap_eid_size, &~d.wrap_eid);
	if (ret != hrSuccess)
		return ret;

	d.prop[d.count].ulPropTag = PR_ENTRYID;
	d.prop[d.count].Value.bin.cb = d.wrap_eid_size;
	d.prop[d.count++].Value.bin.lpb = reinterpret_cast<BYTE *>(d.wrap_eid.get());
	d.prop[d.count].ulPropTag = PR_RECORD_KEY;
	d.prop[d.count].Value.bin.cb = sizeof(MAPIUID);
	d.prop[d.count++].Value.bin.lpb = (LPBYTE)&reinterpret_cast<PEID>(d.eid.get())->guid; //@FIXME validate guid
	d.prop[d.count].ulPropTag = PR_DISPLAY_NAME_W;
	d.prop[d.count++].Value.lpszW = reinterpret_cast<wchar_t *>(d.store_name.get());
	d.prop[d.count].ulPropTag = PR_EC_PATH;
	d.prop[d.count++].Value.lpszA = const_cast<char *>("Server");
	d.prop[d.count].ulPropTag = PR_PROVIDER_DLL_NAME_A;
	d.prop[d.count++].Value.lpszA = const_cast<char *>(WCLIENT_DLL_NAME);
	return hrSuccess;
}

static HRESULT initprov_addrbook(struct initprov &d)
{
	ABEID *eidptr;
	size_t abe_size = CbNewABEID("");
	HRESULT ret = MAPIAllocateBuffer(abe_size, reinterpret_cast<void **>(&eidptr));
	if (ret != hrSuccess)
		return ret;

	d.abe_id.reset(eidptr);
	memset(eidptr, 0, abe_size);
	memcpy(&d.abe_id->guid, &MUIDECSAB, sizeof(GUID));
	d.abe_id->ulType = MAPI_ABCONT;

	d.prop[d.count].ulPropTag = PR_ENTRYID;
	d.prop[d.count].Value.bin.cb = abe_size;
	d.prop[d.count++].Value.bin.lpb = reinterpret_cast<BYTE *>(eidptr);
	d.prop[d.count].ulPropTag = PR_RECORD_KEY;
	d.prop[d.count].Value.bin.cb = sizeof(MAPIUID);
	d.prop[d.count++].Value.bin.lpb = reinterpret_cast<BYTE *>(const_cast<GUID *>(&MUIDECSAB));
	d.prop[d.count].ulPropTag = PR_DISPLAY_NAME_A;
	d.prop[d.count++].Value.lpszA = const_cast<char *>("Kopano Addressbook");
	d.prop[d.count].ulPropTag = PR_EC_PATH;
	d.prop[d.count++].Value.lpszA = const_cast<char *>("Kopano Addressbook");
	d.prop[d.count].ulPropTag = PR_PROVIDER_DLL_NAME_A;
	d.prop[d.count++].Value.lpszA = const_cast<char *>(WCLIENT_DLL_NAME);
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
    IProfSect *lpProfSect, const sGlobalProfileProps &sProfileProps,
    ULONG *lpcStoreID, LPENTRYID *lppStoreID, WSTransport *transport)
{
	HRESULT hr = hrSuccess;
	SPropValuePtr	ptrPropValueResourceType;
	SPropValuePtr	ptrPropValueProviderUid;
	std::string		strServiceName;
	ULONG			ulResourceType=0;
	struct initprov d;
	d.provadm = lpAdminProvider;
	d.profsect = lpProfSect;
	d.count = d.eid_size = 0;
	d.transport = NULL;

	if (d.provadm != NULL) {
		hr = GetServiceName(d.provadm, &strServiceName);
		if (hr != hrSuccess)
			goto exit;
	} else {
		SPropValuePtr psn;
		hr = HrGetOneProp(d.profsect, PR_SERVICE_NAME_A, &~psn);
		if(hr == hrSuccess)
			strServiceName = psn->Value.lpszA;
		hr = hrSuccess;
	}
	hr = HrGetOneProp(d.profsect, PR_RESOURCE_TYPE, &~ptrPropValueResourceType);
	if(hr != hrSuccess) {
		// Ignore this provider; apparently it has no resource type, so just skip it
		hr = hrSuccess;
		goto exit;
	}
	if (HrGetOneProp(d.profsect, PR_PROVIDER_UID, &~ptrPropValueProviderUid) == hrSuccess &&
	    ptrPropValueProviderUid != nullptr)
		d.provuid = reinterpret_cast<MAPIUID *>(ptrPropValueProviderUid.get()->Value.bin.lpb);
	else
		d.provuid = nullptr;
	ulResourceType = ptrPropValueResourceType->Value.l;

	if (transport != NULL) {
		d.transport = transport;
	} else {
		hr = WSTransport::Create(0, &d.transport);
		if (hr != hrSuccess)
			goto exit;
		hr = d.transport->HrLogon(sProfileProps);
		if (hr != hrSuccess)
			goto exit;
	}

	if(ulResourceType == MAPI_STORE_PROVIDER)
	{
		hr = initprov_mapi_store(d, sProfileProps);
		if (hr != hrSuccess)
			goto exit;
	} else if(ulResourceType == MAPI_AB_PROVIDER) {
		hr = initprov_addrbook(d);
		if (hr != hrSuccess)
			goto exit;
	} else {
		assert(false);
	}

	hr = d.profsect->SetProps(d.count, d.prop, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = d.profsect->SaveChanges(0);
	if(hr != hrSuccess)
		goto exit;

	if (lpcStoreID && lppStoreID) {
		*lpcStoreID = d.eid_size;
		hr = MAPIAllocateBuffer(d.eid_size, reinterpret_cast<void **>(lppStoreID));
		if(hr != hrSuccess)
			goto exit;
		
		memcpy(*lppStoreID, d.eid, d.eid_size);
	}
exit:
	//Free allocated memory
	if (d.transport != NULL && d.transport != transport)
		d.transport->Release(); /* implies logoff */
	if (hr == MAPI_S_SPECIAL_OK)
		return hrSuccess;
	return hr;
}

static HRESULT UpdateProviders(LPPROVIDERADMIN lpAdminProviders,
    const sGlobalProfileProps &sProfileProps, WSTransport *transport)
{
	HRESULT hr;

	ProfSectPtr		ptrProfSect;
	MAPITablePtr	ptrTable;
	SRowSetPtr		ptrRows;

	// Get the provider table
	hr = lpAdminProviders->GetProviderTable(0, &~ptrTable);
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
		auto lpsProviderUID = PCpropFindProp(ptrRows[curRow].lpProps, ptrRows[curRow].cValues, PR_PROVIDER_UID);
		if(lpsProviderUID == NULL || lpsProviderUID->Value.bin.cb == 0) {
			// Provider without a provider uid,  just move to the next
			assert(false);
			continue;
		}
		hr = lpAdminProviders->OpenProfileSection((MAPIUID *)lpsProviderUID->Value.bin.lpb, nullptr, MAPI_MODIFY, &~ptrProfSect);
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
    ULONG ulContext, ULONG cvals, const SPropValue *pvals,
    LPPROVIDERADMIN lpAdminProviders, MAPIERROR **lppMapiError)
{
	HRESULT			hr = erSuccess;
	std::string		strServerName;
	std::wstring	strUserName;
	std::wstring	strUserPassword;
	std::string		strServerPort;
	std::string		strDefaultOfflinePath;
	std::string		strType;
	std::string		strDefStoreServer;
	sGlobalProfileProps	sProfileProps;
	std::basic_string<TCHAR> strError;

	ProfSectPtr		ptrGlobalProfSect;
	ProfSectPtr		ptrProfSect;
	MAPISessionPtr	ptrSession;
	object_ptr<WSTransport> lpTransport;
	memory_ptr<SPropValue> lpsPropValue;
	bool			bShowDialog = false;

	MAPIERROR		*lpMapiError = NULL;
	memory_ptr<BYTE> lpDelegateStores;
	ULONG			cDelegateStores = 0;
	convert_context	converter;
	bool bInitStores = true;

	_hInstance = hInst;

	if (psup) {
		hr = psup->GetMemAllocRoutines(&_pfnAllocBuf, &_pfnAllocMore, &_pfnFreeBuf);
		if (hr != hrSuccess)
			assert(false);
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
		break;
	case MSG_SERVICE_PROVIDER_DELETE:
		hr = hrSuccess;

		//FIXME: delete Offline database

		break;
	case MSG_SERVICE_CONFIGURE:
		//bShowAllSettingsPages = true;
		// Do not break here
	case MSG_SERVICE_CREATE:
		/* Open global {profile section}, add the store. (for show list, delete etc.). */
		hr = lpAdminProviders->OpenProfileSection((LPMAPIUID)pbGlobalProfileSectionGuid, nullptr, MAPI_MODIFY , &~ptrGlobalProfSect);
		if(hr != hrSuccess)
			goto exit;

		if(cvals) {
			hr = ptrGlobalProfSect->SetProps(cvals, pvals, NULL);

			if(hr != hrSuccess)
				goto exit;
		}

		hr = ClientUtil::GetGlobalProfileProperties(ptrGlobalProfSect, &sProfileProps);
		if (sProfileProps.strServerPath.empty() ||
		    sProfileProps.strUserName.empty() ||
		    (sProfileProps.strPassword.empty() &&
		    sProfileProps.strSSLKeyFile.empty()))
			bShowDialog = true;
		//FIXME: check here offline path with the flags
		if(!sProfileProps.strServerPath.empty()) {
			strServerName = GetServerNameFromPath(sProfileProps.strServerPath.c_str());
			strServerPort = GetServerPortFromPath(sProfileProps.strServerPath.c_str());
			strType = GetServerTypeFromPath(sProfileProps.strServerPath.c_str());
		}

		/* Get delegate stores, ignore error. */
		ClientUtil::GetGlobalProfileDelegateStoresProp(ptrGlobalProfSect, &cDelegateStores, &~lpDelegateStores);

		// init defaults
		hr = WSTransport::Create(ulFlags & SERVICE_UI_ALLOWED ? 0 : MDB_NO_DIALOG, &~lpTransport);
		if(hr != hrSuccess)
			goto exit;

		// Check the path, username and password
		while(1)
		{
			if ((bShowDialog && ulFlags & SERVICE_UI_ALLOWED) || ulFlags & SERVICE_UI_ALWAYS)
				hr = MAPI_E_USER_CANCEL;
						
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
				assert(false);
			}else {
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
			memory_ptr<TCHAR> lpszErrorMsg;

			if (Util::HrMAPIErrorToText(hr, &~lpszErrorMsg) == hrSuccess) {
				// Set Error
				strError = _T("EntryPoint: ");
				strError += lpszErrorMsg;

				// Some outlook 2007 clients can't allocate memory so check it
				if(MAPIAllocateBuffer(sizeof(MAPIERROR), (void**)&lpMapiError) == hrSuccess) { 

					memset(lpMapiError, 0, sizeof(MAPIERROR));				
					if (ulFlags & MAPI_UNICODE) {
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
	return hr;
}

HRESULT  __cdecl ABProviderInit(HINSTANCE hInstance, LPMALLOC lpMalloc,
    LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore,
    LPFREEBUFFER lpFreeBuffer, ULONG ulFlags, ULONG ulMAPIVer,
    ULONG *lpulProviderVer, LPABPROVIDER *lppABProvider)
{
	if (ulMAPIVer < CURRENT_SPI_VERSION)
		return MAPI_E_VERSION;
	*lpulProviderVer = CURRENT_SPI_VERSION;
	// Save the pointer to the allocation routines in global variables
	_pmalloc = lpMalloc;
	_pfnAllocBuf = lpAllocateBuffer;
	_pfnAllocMore = lpAllocateMore;
	_pfnFreeBuf = lpFreeBuffer;
	_hInstance = hInstance;

	object_ptr<ECABProviderSwitch> lpABProvider;
	HRESULT hr = ECABProviderSwitch::Create(&~lpABProvider);
	if (hr == hrSuccess)
		hr = lpABProvider->QueryInterface(IID_IABProvider,
		     reinterpret_cast<void **>(lppABProvider));
	return hr;
}
