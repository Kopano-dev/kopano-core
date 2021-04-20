/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <cstdint>
#include <mapi.h>
#include <mapispi.h>
#include <mapiutil.h>
#include <kopano/ECGetText.h>
#include <kopano/memory.hpp>
#include <string>
#include <utility>
#include <cassert>
#include "kcore.hpp"
#include "ECMSProvider.h"
#include "ECABContainer.h"
#include <kopano/stringutil.h>
#include <kopano/ECLogger.h>
#include <kopano/ECGuid.h>
#include <kopano/MAPIErrors.h>
#include <kopano/charset/convert.h>
#include <kopano/charset/utf8string.h>
#include <edkmdb.h>
#include <edkguid.h>
#include "SSLUtil.h"
#include "ClientUtil.h"
#include "EntryPoint.h"

using namespace KC;

struct initprov {
	IProviderAdmin *provadm;
	MAPIUID *provuid;
	IProfSect *profsect;
	object_ptr<WSTransport> transport;
	unsigned int count, eid_size, wrap_eid_size;
	SPropValue prop[6];
	memory_ptr<ENTRYID> eid, wrap_eid;
	/* referenced from prop[n] */
	memory_ptr<wchar_t> store_name;
	memory_ptr<ABEID> abe_id;
};

static const uint32_t MAPI_S_SPECIAL_OK = MAKE_MAPI_S(0x900);

// Client wide variable
tstring		g_strProductName;

// Map of msprovider with Profilename as key
ECMapProvider	g_mapProviders;

static HRESULT RemoveAllProviders(ECMapProvider *);
class CKopanoApp {
public:
    CKopanoApp() {
        ssl_threading_setup();
		g_strProductName = KC_T("Kopano Core");
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
	for (auto &p : *mp) {
		p.second.lpMSProviderOnline.reset();
		p.second.lpABProviderOnline.reset();
	}
	return hrSuccess;
}

// entrypoints

// Called by MAPI to return a MSProvider object when a user opens a store based on our service
HRESULT MSProviderInit(HINSTANCE hInstance, IMalloc *,
    ALLOCATEBUFFER *, ALLOCATEMORE *,
    FREEBUFFER *, ULONG ulFlags, ULONG ulMAPIver,
    unsigned int *lpulProviderVer, IMSProvider **ppmsp)
{
	object_ptr<ECMSProviderSwitch> lpMSProvider;

	// Check the interface version is ok
	if (ulMAPIver != CURRENT_SPI_VERSION)
		return MAPI_E_VERSION;
	*lpulProviderVer = CURRENT_SPI_VERSION;
	// This object is created for the lifetime of the DLL and destroyed when the
	// DLL is closed (same on linux, but then for the shared library);
	auto hr = ECMSProviderSwitch::Create(&~lpMSProvider);
	if(hr != hrSuccess)
		return hr;
	return lpMSProvider->QueryInterface(IID_IMSProvider, reinterpret_cast<void **>(ppmsp));
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
		ec_log_debug("Received a redirect from %s to %s for public store",
			profprop.strServerPath.c_str(), redir_srv.c_str());
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
	auto ret = d.transport->HrGetStore(0, nullptr, &d.eid_size, &~d.eid,
	           0, nullptr, &redir_srv);
	if (ret == MAPI_E_NOT_FOUND) {
		ec_log_err("HrGetStore failed: No store present.");
		return ret;
	} else if (ret != MAPI_E_UNABLE_TO_COMPLETE) {
		return ret;
	}

	/* MAPI_E_UNABLE_TO_COMPLETE */
	ec_log_debug("Received a redirect from %s to %s for store",
		profprop.strServerPath.c_str(), redir_srv.c_str());
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
	object_ptr<IProfSect> globprofsect;
	ret = d.provadm->OpenProfileSection(reinterpret_cast<const MAPIUID *>(&pbGlobalProfileSectionGuid), nullptr, MAPI_MODIFY, &~globprofsect);
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
	memory_ptr<SPropValue> name;
	auto ret = HrGetOneProp(d.profsect, PR_EC_USERNAME_W, &~name);
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
	auto u8name = PROP_TYPE(name->ulPropTag) == PT_UNICODE ?
	              convert_to<utf8string>(name->Value.lpszW) :
	              convert_to<utf8string>(name->Value.lpszA);
	ret = d.transport->HrResolveUserStore(u8name, 0, nullptr, &d.eid_size, &~d.eid, &redir_srv);
	if (ret != MAPI_E_UNABLE_TO_COMPLETE)
		return ret;

	ec_log_debug("Received a redirect from %s to %s for delegate store",
		profprop.strServerPath.c_str(), redir_srv.c_str());
	d.transport->HrLogOff();
	auto new_props = profprop;
	new_props.strServerPath = redir_srv;
	ret = d.transport->HrLogon(new_props);
	if (ret != hrSuccess)
		return ret;
	return d.transport->HrResolveUserStore(u8name, 0, nullptr, &d.eid_size, &~d.eid);
}

static HRESULT initprov_storearc(struct initprov &d)
{
	// We need to get the username and the server name or url from the profsect.
	// That's enough information to get the entryid from the correct server. There's no redirect
	// available when resolving archive stores.
	memory_ptr<SPropValue> name, server;
	auto ret = HrGetOneProp(d.profsect, PR_EC_USERNAME_W, &~name);
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

	object_ptr<WSTransport> alt_transport;
	ret = GetTransportToNamedServer(d.transport, server->Value.LPSZ,
	      (PROP_TYPE(name->ulPropTag) == PT_STRING8 ? 0 : MAPI_UNICODE),
	      &~alt_transport);
	if (ret != hrSuccess)
		return ret;
	d.transport = std::move(alt_transport);
	auto u8name = PROP_TYPE(name->ulPropTag) == PT_UNICODE ?
	              convert_to<utf8string>(name->Value.lpszW) :
	              convert_to<utf8string>(name->Value.lpszA);
	return d.transport->HrResolveTypedStore(u8name, ECSTORE_TYPE_ARCHIVE, &d.eid_size, &~d.eid);
}

static HRESULT
initprov_mapi_store(struct initprov &d, const sGlobalProfileProps &profprop)
{
	memory_ptr<SPropValue> mdb;
	auto ret = HrGetOneProp(d.profsect, PR_MDB_PROVIDER, &~mdb);
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
	d.prop[d.count++].Value.bin.lpb = reinterpret_cast<BYTE *>(&reinterpret_cast<EID *>(d.eid.get())->guid); //@FIXME validate guid
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
	size_t abe_size = CbNewABEID("");
	auto ret = MAPIAllocateBuffer(abe_size, &~d.abe_id);
	if (ret != hrSuccess)
		return ret;

	memset(d.abe_id, 0, abe_size);
	memcpy(&d.abe_id->guid, &MUIDECSAB, sizeof(GUID));
	d.abe_id->ulType = MAPI_ABCONT;

	d.prop[d.count].ulPropTag = PR_ENTRYID;
	d.prop[d.count].Value.bin.cb = abe_size;
	d.prop[d.count++].Value.bin.lpb = reinterpret_cast<BYTE *>(d.abe_id.get());
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
HRESULT InitializeProvider(IProviderAdmin *lpAdminProvider,
    IProfSect *lpProfSect, const sGlobalProfileProps &sProfileProps,
    ULONG *lpcStoreID, ENTRYID **lppStoreID)
{
	memory_ptr<SPropValue> ptrPropValueResourceType, dspname, tpprop;
	memory_ptr<SPropValue> ptrPropValueProviderUid;
	std::string		strServiceName;
	struct initprov d;
	d.provadm = lpAdminProvider;
	d.profsect = lpProfSect;
	d.count = d.eid_size = 0;

	if (d.provadm != NULL) {
		auto hr = GetServiceName(d.provadm, &strServiceName);
		if (hr != hrSuccess)
			return hr;
	} else {
		memory_ptr<SPropValue> psn;
		auto hr = HrGetOneProp(d.profsect, PR_SERVICE_NAME_A, &~psn);
		if(hr == hrSuccess)
			strServiceName = psn->Value.lpszA;
	}
	auto hr = HrGetOneProp(d.profsect, PR_RESOURCE_TYPE, &~ptrPropValueResourceType);
	if (hr != hrSuccess)
		// Ignore this provider; apparently it has no resource type, so just skip it
		return hrSuccess;
	if (HrGetOneProp(d.profsect, PR_PROVIDER_UID, &~ptrPropValueProviderUid) == hrSuccess &&
	    ptrPropValueProviderUid != nullptr)
		d.provuid = reinterpret_cast<MAPIUID *>(ptrPropValueProviderUid.get()->Value.bin.lpb);
	else
		d.provuid = nullptr;

	unsigned int ulResourceType = ptrPropValueResourceType->Value.l;
	hr = HrGetOneProp(d.profsect, PR_DISPLAY_NAME_A, &~dspname);
	ec_log_debug("Initializing provider \"%s\"",
		hr == hrSuccess && dspname != nullptr ? dspname->Value.lpszA : "(unnamed)");

	object_ptr<IProfSect> globprof;
	hr = lpAdminProvider->OpenProfileSection(reinterpret_cast<const MAPIUID *>(&pbGlobalProfileSectionGuid), nullptr, MAPI_MODIFY, &~globprof);
	if (hr == hrSuccess && globprof != nullptr) {
		hr = HrGetOneProp(globprof, PR_EC_TRANSPORTOBJECT, &~tpprop);
		if (hr == hrSuccess && tpprop != nullptr)
			d.transport.reset(reinterpret_cast<WSTransport *>(tpprop->Value.lpszA));
	}
	if (d.transport == nullptr) {
		hr = WSTransport::Create(&~d.transport);
		if (hr != hrSuccess)
			return hr;
		hr = d.transport->HrLogon(sProfileProps);
		if (hr != hrSuccess)
			return hr;
	}

	if(ulResourceType == MAPI_STORE_PROVIDER)
	{
		hr = initprov_mapi_store(d, sProfileProps);
		if (hr == MAPI_S_SPECIAL_OK)
			return hrSuccess;
		else if (hr != hrSuccess)
			return hr;
	} else if(ulResourceType == MAPI_AB_PROVIDER) {
		hr = initprov_addrbook(d);
		if (hr != hrSuccess)
			return hr;
	} else {
		assert(false);
	}

	hr = d.profsect->SetProps(d.count, d.prop, NULL);
	if(hr != hrSuccess)
		return hr;
	hr = d.profsect->SaveChanges(0);
	if(hr != hrSuccess)
		return hr;

	if (lpcStoreID && lppStoreID) {
		*lpcStoreID = d.eid_size;
		hr = KAllocCopy(d.eid, d.eid_size, reinterpret_cast<void **>(lppStoreID));
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

static HRESULT UpdateProviders(IProviderAdmin *lpAdminProviders,
    const sGlobalProfileProps &sProfileProps)
{
	object_ptr<IMAPITable> ptrTable;
	rowset_ptr ptrRows;

	// Get the provider table
	auto hr = lpAdminProviders->GetProviderTable(0, &~ptrTable);
	if(hr != hrSuccess)
		return hr;
	// Get the rows
	hr = ptrTable->QueryRows(0xFF, 0, &~ptrRows);
	if(hr != hrSuccess)
		return hr;
	//Check if exist one or more rows
	if (ptrRows.size() == 0)
		return MAPI_E_NOT_FOUND;

	// Scan the rows for message stores
	for (ULONG curRow = 0; curRow < ptrRows.size(); ++curRow) {
		//Get de UID of the provider to open the profile section
		auto lpsProviderUID = ptrRows[curRow].cfind(PR_PROVIDER_UID);
		if(lpsProviderUID == NULL || lpsProviderUID->Value.bin.cb == 0) {
			// Provider without a provider uid,  just move to the next
			assert(false);
			continue;
		}
		object_ptr<IProfSect> ptrProfSect;
		hr = lpAdminProviders->OpenProfileSection((MAPIUID *)lpsProviderUID->Value.bin.lpb, nullptr, MAPI_MODIFY, &~ptrProfSect);
		if(hr != hrSuccess)
			return hr;
		// Set already PR_PROVIDER_UID, ignore error
		HrSetOneProp(ptrProfSect, lpsProviderUID);
		hr = InitializeProvider(lpAdminProviders, ptrProfSect, sProfileProps, nullptr, nullptr);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

static std::string GetServerTypeFromPath(const char *szPath)
{
	std::string path = szPath;
	auto pos = path.find("://");
	if (pos != std::string::npos)
		return path.substr(0, pos);
	return std::string();
}

// Called by MAPI to configure, or create a service
extern "C" HRESULT MSGServiceEntry(HINSTANCE hInst,
    IMalloc *, IMAPISupport *psup, unsigned int ulUIParam, unsigned int ulFlags,
    ULONG ulContext, ULONG cvals, const SPropValue *pvals,
    IProviderAdmin *lpAdminProviders, MAPIERROR **lppMapiError)
{
	HRESULT hr = hrSuccess;
	std::string strServerName, strDefStoreServer;
	std::wstring strUserName, strUserPassword;
	sGlobalProfileProps	sProfileProps;
	std::basic_string<TCHAR> strError;
	object_ptr<WSTransport> lpTransport;
	memory_ptr<SPropValue> lpsPropValue;
	memory_ptr<BYTE> lpDelegateStores;
	ULONG			cDelegateStores = 0;
	SPropValue spv;

	// Logon defaults
	std::string strType = "http", strServerPort = "236";
	object_ptr<IProfSect> ptrGlobalProfSect, ptrProfSect;
	switch(ulContext) {
	case MSG_SERVICE_CONFIGURE:
		/* Open global {profile section}, add the store. (for show list, delete etc.). */
		hr = lpAdminProviders->OpenProfileSection(reinterpret_cast<const MAPIUID *>(&pbGlobalProfileSectionGuid), nullptr, MAPI_MODIFY, &~ptrGlobalProfSect);
		if(hr != hrSuccess)
			return hr;
		if(cvals) {
			hr = ptrGlobalProfSect->SetProps(cvals, pvals, NULL);
			if(hr != hrSuccess)
				return hr;
		}

		ClientUtil::GetGlobalProfileProperties(ptrGlobalProfSect, &sProfileProps);
		//FIXME: check here offline path with the flags
		if(!sProfileProps.strServerPath.empty()) {
			strServerName = GetServerNameFromPath(sProfileProps.strServerPath.c_str());
			strServerPort = GetServerPortFromPath(sProfileProps.strServerPath.c_str());
			strType = GetServerTypeFromPath(sProfileProps.strServerPath.c_str());
		}

		/* Get delegate stores, ignore error. */
		ClientUtil::GetGlobalProfileDelegateStoresProp(ptrGlobalProfSect, &cDelegateStores, &~lpDelegateStores);

		// init defaults
		hr = HrGetOneProp(ptrGlobalProfSect, PR_EC_TRANSPORTOBJECT, &~lpsPropValue);
		if (hr == hrSuccess && lpsPropValue != nullptr && lpsPropValue->Value.lpszA != nullptr)
			reinterpret_cast<WSTransport *>(lpsPropValue->Value.lpszA)->Release();
		hr = WSTransport::Create(&~lpTransport);
		if(hr != hrSuccess)
			return hr;
		spv.ulPropTag = PR_EC_TRANSPORTOBJECT;
		spv.Value.lpszA = reinterpret_cast<char *>(lpTransport.get());
		hr = HrSetOneProp(ptrGlobalProfSect, &spv);
		if (hr != hrSuccess)
			return hr;
		lpTransport.get()->AddRef(); /* for EC_TRANSPORTOBJECT */

		// Check the path, username and password
		if (strServerName.empty() || sProfileProps.strUserName.empty()) {
			hr = MAPI_E_UNCONFIGURED;
			goto exit2;
		} else if(!strServerName.empty() && !sProfileProps.strUserName.empty()) {
			// Logon the server
			hr = lpTransport->HrLogon(sProfileProps);
			if (hr != hrSuccess)
				ec_log_err("HrLogon server \"%s\" user \"%ls\": %s",
					sProfileProps.strServerPath.c_str(),
					sProfileProps.strUserName.c_str(),
					GetMAPIErrorMessage(hr));
		} else {
			hr = MAPI_E_LOGON_FAILED;
		}

		// On incorrect password, and UI allowed, show incorrect password error
		// Do not reset the logon error from HrLogon()
		// The DAgent uses this value to determine if the delivery is fatal or not
		//
		// Although this error is not in the online spec from MS, it should not really matter .... right?
		// hr = MAPI_E_UNCONFIGURED;
		if (hr == MAPI_E_LOGON_FAILED || hr == MAPI_E_NETWORK_ERROR ||
		    hr == MAPI_E_VERSION || hr == MAPI_E_INVALID_PARAMETER ||
		    hr == MAPI_E_NO_ACCESS) {
			goto exit2;
		} else if (hr != hrSuccess) { // Big error?
			assert(false);
			goto exit2;
		}

		hr = UpdateProviders(lpAdminProviders, sProfileProps);
 exit2:
		static constexpr SizedSPropTagArray(1, tags) = {1, {PR_EC_TRANSPORTOBJECT}};
		ptrGlobalProfSect->DeleteProps(tags, nullptr);
		lpTransport.get()->Release();
		break;
	} // switch(ulContext)
	return hr;
}

HRESULT ABProviderInit(HINSTANCE hInstance, IMalloc *,
    ALLOCATEBUFFER *, ALLOCATEMORE *,
    FREEBUFFER *, ULONG ulFlags, ULONG ulMAPIVer,
    unsigned int *lpulProviderVer, IABProvider **lppABProvider)
{
	if (ulMAPIVer < CURRENT_SPI_VERSION)
		return MAPI_E_VERSION;
	*lpulProviderVer = CURRENT_SPI_VERSION;
	object_ptr<ECABProviderSwitch> lpABProvider;
	auto hr = ECABProviderSwitch::Create(&~lpABProvider);
	if (hr == hrSuccess)
		hr = lpABProvider->QueryInterface(IID_IABProvider,
		     reinterpret_cast<void **>(lppABProvider));
	return hr;
}
