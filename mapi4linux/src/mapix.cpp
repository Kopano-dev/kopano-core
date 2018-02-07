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
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <kopano/ECLogger.h>
#include <kopano/lockhelper.hpp>
#include <kopano/memory.hpp>
#include <kopano/Util.h>
#include "m4l.mapix.h"
#include "m4l.mapispi.h"
#include "m4l.mapisvc.h"

#include <mapi.h>
#include <mapiutil.h>
#include <cstring>

#include <kopano/Util.h>

#include <kopano/ECConfig.h>
#include <kopano/ECDebug.h>
#include <kopano/ECGuid.h>
#include <kopano/ECMemTable.h>
#include <kopano/CommonUtil.h>
#include <kopano/stringutil.h>
#include <kopano/mapiguidext.h>
#include <kopano/ECRestriction.h>
#include <kopano/MAPIErrors.h>

#include <string>
#include <map>
#include <kopano/charset/convert.h>
#define _MAPI_MEM_DEBUG 0
#define _MAPI_MEM_MORE_DEBUG 0

namespace KC {

class SessionRestorer _kc_final {
	public:
	HRESULT restore_profile(const std::string &, IMAPISession **);

	private:
	HRESULT restore_propvals(SPropValue **, ULONG &);
	HRESULT restore_services(IProfAdmin *);
	HRESULT restore_providers();

	std::string::const_iterator m_input;
	size_t m_left = 0;
	std::string m_profname;
	object_ptr<M4LMsgServiceAdmin> m_svcadm;
};

class SessionSaver _kc_final {
	public:
	static HRESULT save_profile(IMAPISession *, std::string &);
};

} /* namespace */

using namespace KC;

enum mapibuf_ident {
	/*
	 * Arbitrary values chosen. At least 62100 sticks out from
	 * offsets-of-NULL and normal Linux pointers in gdb.
	 */
	MAPIBUF_BASE = 62100,
	MAPIBUF_MORE,
};

struct alignas(::max_align_t) mapiext_head {
	struct mapiext_head *child;
	alignas(::max_align_t) char data[];
};

struct alignas(::max_align_t) mapibuf_head {
	std::mutex mtx;
	struct mapiext_head *child; /* singly-linked list */
#if _MAPI_MEM_MORE_DEBUG
	enum mapibuf_ident ident;
#endif
	alignas(::max_align_t) char data[];
};

/* Some required globals */
MAPISVC *m4l_lpMAPISVC = NULL;

// ---
// M4LProfAdmin
// ---
decltype(M4LProfAdmin::profiles)::iterator
M4LProfAdmin::findProfile(const TCHAR *name)
{
	decltype(profiles)::iterator i;

	for (i = profiles.begin(); i != profiles.end(); ++i)
		if ((*i)->profname == reinterpret_cast<const char *>(name))
			break;
	return i;
}

HRESULT M4LProfAdmin::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError) {
	*lppMAPIError = NULL;
    return hrSuccess;
}

/**
 * Returns IMAPITable object with all profiles available. Only has 2
 * properties per row: PR_DEFAULT_PROFILE (always false in Linux) and
 * PR_DISPLAY_NAME. This table does not have notifications, so changes
 * will not be present in the table.
 *
 * @param[in]	ulFlags		Unused.
 * @param[out]	lppTable	Pointer to IMAPITable object.
 * @return		HRESULT
 */
HRESULT M4LProfAdmin::GetProfileTable(ULONG ulFlags, LPMAPITABLE* lppTable) {
	HRESULT hr = hrSuccess;
	object_ptr<ECMemTable> lpTable;
	object_ptr<ECMemTableView> lpTableView;
	SPropValue sProps[3];
	int n = 0;
	std::wstring wDisplayName;
	SizedSPropTagArray(2, sptaProfileCols) = {2, {PR_DEFAULT_PROFILE, PR_DISPLAY_NAME}};

	if (ulFlags & MAPI_UNICODE)
		sptaProfileCols.aulPropTag[1] = CHANGE_PROP_TYPE(PR_DISPLAY_NAME_W, PT_UNICODE);
	else
		sptaProfileCols.aulPropTag[1] = CHANGE_PROP_TYPE(PR_DISPLAY_NAME_A, PT_STRING8);
		
	hr = ECMemTable::Create(sptaProfileCols, PR_ROWID, &~lpTable);
	if(hr != hrSuccess)
		return hr;

	ulock_rec l_prof(m_mutexProfiles);
	for (auto &prof : profiles) {
		sProps[0].ulPropTag = PR_DEFAULT_PROFILE;
		sProps[0].Value.b = false; //FIXME: support setDefaultProfile

		if (ulFlags & MAPI_UNICODE) {
			wDisplayName = convert_to<std::wstring>(prof->profname);
			sProps[1].ulPropTag = PR_DISPLAY_NAME_W;
			sProps[1].Value.lpszW = (WCHAR *) wDisplayName.c_str();
		} else {
			sProps[1].ulPropTag = PR_DISPLAY_NAME_A;
			sProps[1].Value.lpszA = const_cast<char *>(prof->profname.c_str());
		}
		
		sProps[2].ulPropTag = PR_ROWID;
		sProps[2].Value.ul = n++;

		//TODO: PR_INSTANCE_KEY

		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, sProps, 3);
		if (hr != hrSuccess)
			return kc_perrorf("HrModifyRow failed", hr);
	}

	hr = lpTable->HrGetView(createLocaleFromName(""), ulFlags, &~lpTableView);
	if (hr != hrSuccess)
		return kc_perrorf("HrGetView failed", hr);
	hr = lpTableView->QueryInterface(IID_IMAPITable, (void **)lppTable);
	if (hr != hrSuccess)
		kc_perrorf("QueryInterface failed", hr);
	return hr;
}

/**
 * Create new profile with unique name.
 *
 * @param[in]	lpszProfileName	Name of the profile to create, us-ascii charset. Actual type always char*.
 * @param[in]	lpszPassword	Password of the profile, us-ascii charset. Actual type always char*.
 * @param[in]	ulUIParam		Unused in Linux.
 * @param[in]	ulFlags			Unused in Linux.
 * @return		HRESULT
 * @retval		MAPI_E_NO_ACCESS	Profilename already exists.
 * @retval		MAPI_E_NOT_ENOUGH_MEMORY	Out of memory.
 */
HRESULT M4LProfAdmin::CreateProfile(const TCHAR *lpszProfileName,
    const TCHAR *lpszPassword, ULONG_PTR ulUIParam, ULONG ulFlags)
{
    HRESULT hr = hrSuccess;
	std::unique_ptr<profEntry> entry;
	object_ptr<M4LProfSect> profilesection;
	SPropValue sPropValue;
	ulock_rec l_prof(m_mutexProfiles);
    
    if(lpszProfileName == NULL) {
		ec_log_err("M4LProfAdmin::CreateProfile(): invalid parameters");
		return MAPI_E_INVALID_PARAMETER;
	}
	if (findProfile(lpszProfileName) != profiles.cend()) {
		ec_log_err("M4LProfAdmin::CreateProfile(): duplicate profile name");
		return MAPI_E_NO_ACCESS; /* duplicate profile name */
    }
	entry.reset(new(std::nothrow) profEntry);
    if (!entry) {
		ec_log_crit("M4LProfAdmin::CreateProfile(): ENOMEM");
		return MAPI_E_NOT_ENOUGH_MEMORY;
    }
    // This is the so-called global profile section.
	profilesection.reset(new M4LProfSect(TRUE));

	// Set the default profilename
	sPropValue.ulPropTag = PR_PROFILE_NAME_A;
	sPropValue.Value.lpszA = (char*)lpszProfileName;
	hr = profilesection->SetProps(1 ,&sPropValue, NULL);
	if (hr != hrSuccess)
		return kc_perrorf("SetProps failed", hr);
	entry->serviceadmin.reset(new(std::nothrow) M4LMsgServiceAdmin(profilesection));
    if (!entry->serviceadmin) {
		ec_log_err("M4LProfAdmin::CreateProfile(): M4LMsgServiceAdmin failed");
		return MAPI_E_NOT_ENOUGH_MEMORY;
    }

    // enter data
    entry->profname = (char*)lpszProfileName;
    if (lpszPassword)
		entry->password = (char*)lpszPassword;
	profiles.emplace_back(std::move(entry));
	return hrSuccess;
}

/**
 * Delete profile from list.
 *
 * @param[in]	lpszProfileName	Name of the profile to delete, us-ascii charset.
 * @param[in]	ulFlags			Unused in Linux.
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Profilename does not exist.
 */
HRESULT M4LProfAdmin::DeleteProfile(const TCHAR *lpszProfileName, ULONG ulFlags)
{
	scoped_rlock l_prof(m_mutexProfiles);
    
	auto i = findProfile(lpszProfileName);
	if (i != profiles.cend())
		profiles.erase(i);
	else
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

HRESULT M4LProfAdmin::ChangeProfilePassword(const TCHAR *lpszProfileName,
    const TCHAR *lpszOldPassword, const TCHAR *lpszNewPassword, ULONG ulFlags)
{
	ec_log_err("M4LProfAdmin::ChangeProfilePassword is not implemented");
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LProfAdmin::CopyProfile(const TCHAR *lpszOldProfileName,
    const TCHAR *lpszOldPassword, const TCHAR *lpszNewProfileName,
    ULONG_PTR ulUIParam, ULONG ulFlags)
{
	ec_log_err("M4LProfAdmin::CopyProfile is not implemented");
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LProfAdmin::RenameProfile(const TCHAR *lpszOldProfileName,
    const TCHAR *lpszOldPassword, const TCHAR *lpszNewProfileName,
    ULONG_PTR ulUIParam, ULONG ulFlags)
{
	ec_log_err("M4LProfAdmin::RenameProfile is not implemented");
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LProfAdmin::SetDefaultProfile(const TCHAR *lpszProfileName,
    ULONG ulFlags)
{
	ec_log_err("M4LProfAdmin::SetDefaultProfile is not implemented");
    return MAPI_E_NO_SUPPORT;
}

/**
 * Request IServiceAdmin object of profile. Linux does not check the password.
 *
 * @param[in]	lpszProfileName	Name of the profile to open, us-ascii charset.
 * @param[in]	lpszPassword	Password of the profile, us-ascii charset. Not used in Linux.
 * @param[in]	ulUIParam		Unused in Linux.
 * @param[in]	ulFlags			Unused in Linux.
 * @param[out]	lppServiceAdmin	IServiceAdmin object
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Profile is not found.
 */
HRESULT M4LProfAdmin::AdminServices(const TCHAR *lpszProfileName,
    const TCHAR *lpszPassword, ULONG_PTR ulUIParam, ULONG ulFlags,
    IMsgServiceAdmin **lppServiceAdmin)
{
    HRESULT hr = hrSuccess;									
	decltype(profiles)::const_iterator i;
	scoped_rlock l_prof(m_mutexProfiles);

    if(lpszProfileName == NULL) {
	ec_log_err("M4LProfAdmin::AdminServices invalid parameters");
		return MAPI_E_INVALID_PARAMETER;
	}

    i = findProfile(lpszProfileName);
    if (i == profiles.cend()) {
	ec_log_err("M4LProfAdmin::AdminServices profile not found");
		return MAPI_E_NOT_FOUND;
    }
    
	hr = (*i)->serviceadmin->QueryInterface(IID_IMsgServiceAdmin,(void**)lppServiceAdmin);
	if (hr != hrSuccess)
		kc_perrorf("QueryInterface failed", hr);
    return hr;
}

HRESULT M4LProfAdmin::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IProfAdmin) {
		AddRef();
		*lpvoid = static_cast<IProfAdmin *>(this);
	} else if (refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = static_cast<IUnknown *>(this);
	} else {
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	}
	return hrSuccess;
}

// ---
// IMsgServceAdmin
// ---
M4LMsgServiceAdmin::M4LMsgServiceAdmin(M4LProfSect *ps) :
	profilesection(ps)
{
}

M4LMsgServiceAdmin::~M4LMsgServiceAdmin()
{
	for (auto &i : services) {
		auto p = providers.begin();
		while (p != providers.end()) {
			if ((*p)->servicename != i->servicename) {
				++p;
				continue;
			}
			p = providers.erase(p);
		}
		try {
			i->service->MSGServiceEntry()(0, nullptr, nullptr,
				0, 0, MSG_SERVICE_DELETE, 0, nullptr,
				i->provideradmin, nullptr);
		} catch (...) {
		}
	}
}

serviceEntry *M4LMsgServiceAdmin::findServiceAdmin(const TCHAR *name)
{
	for (auto &serv : services)
		if (serv->servicename == reinterpret_cast<const char *>(name))
			return serv.get();
	return NULL;
}

serviceEntry *M4LMsgServiceAdmin::findServiceAdmin(const MAPIUID *lpMUID)
{
	for (auto &serv : services)
		if (memcmp(&serv->muid, lpMUID, sizeof(MAPIUID)) == 0)
			return serv.get();
	return NULL;
}

providerEntry *M4LMsgServiceAdmin::findProvider(const MAPIUID *lpUid)
{
	for (auto &prov : providers)
		if (memcmp(&prov->uid,lpUid,sizeof(MAPIUID)) == 0)
			return prov.get();
	return NULL;
}

HRESULT M4LMsgServiceAdmin::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError) {
    *lppMAPIError = NULL;
    return hrSuccess;
}

/**
 * Get all services in this profile in a IMAPITable object. This table
 * doesn't have updates through notifications. This table only has 3
 * properties: PR_SERVICE_UID, PR_SERVICE_NAME, PR_DISPLAY_NAME.
 *
 * @param[in]	ulFlags			Unused in Linux.
 * @param[out]	lppTable		IMAPITable return object
 * @return		HRESULT
 */
HRESULT M4LMsgServiceAdmin::GetMsgServiceTable(ULONG ulFlags, LPMAPITABLE* lppTable) {
	HRESULT hr = hrSuccess;
	object_ptr<ECMemTable> lpTable;
	object_ptr<ECMemTableView> lpTableView;
	SPropValue sProps[4];
	int n = 0;
	std::wstring wServiceName, wDisplayName;
	convert_context converter;
	SizedSPropTagArray(3, sptaProviderCols) =
		{3, {PR_SERVICE_UID, PR_SERVICE_NAME_W, PR_DISPLAY_NAME_W}};

	Util::proptag_change_unicode(ulFlags, sptaProviderCols);
	hr = ECMemTable::Create(sptaProviderCols, PR_ROWID, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("failed to create memtable", hr);
	
	// Loop through all providers, add each to the table
	ulock_rec l_srv(m_mutexserviceadmin);
	for (auto &serv : services) {
		sProps[0].ulPropTag = PR_SERVICE_UID;
		sProps[0].Value.bin.lpb = reinterpret_cast<BYTE *>(&serv->muid);
		sProps[0].Value.bin.cb = sizeof(GUID);

		if (ulFlags & MAPI_UNICODE) {
			wServiceName = converter.convert_to<std::wstring>(serv->servicename);
			sProps[1].ulPropTag = PR_SERVICE_NAME_W;
			sProps[1].Value.lpszW = (WCHAR *) wServiceName.c_str();
		} else {
			sProps[1].ulPropTag = PR_SERVICE_NAME_A;
			sProps[1].Value.lpszA = const_cast<char *>(serv->servicename.c_str());
		}			
		
		if (ulFlags & MAPI_UNICODE) {
			wDisplayName = converter.convert_to<std::wstring>(serv->displayname);
			sProps[1].ulPropTag = PR_DISPLAY_NAME_W;
			sProps[1].Value.lpszW = (WCHAR *) wDisplayName.c_str();
		} else {
			sProps[2].ulPropTag = PR_DISPLAY_NAME_A;
			sProps[2].Value.lpszA = const_cast<char *>(serv->displayname.c_str());
		}
		
		sProps[3].ulPropTag = PR_ROWID;
		sProps[3].Value.ul = n++;
		
		lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, sProps, 4);
	}
	
	hr = lpTable->HrGetView(createLocaleFromName(""), ulFlags, &~lpTableView);
	if (hr != hrSuccess)
		return kc_perrorf("failed to create memtable view", hr);
	hr = lpTableView->QueryInterface(IID_IMAPITable, (void **)lppTable);
	if (hr != hrSuccess)
		kc_perrorf("failed to query memtable interface", hr);
	return hr;
}

/**
 * Create new message service in this profile.
 *
 * @param[in]	lpszService		Name of the new service to add. In Linux, this is only the ZARAFA6 (libkcclient.so) service.
 * @param[in]	lpszDisplayName	Unused in Linux.
 * @param[in]	ulUIParam		Unused in Linux.
 * @param[in]	ulFlags			Unused in Linux.
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Service not available.
 * @retval		MAPI_E_NO_ACCESS	Service already in profile.
 * @retval		MAPI_E_NOT_ENOUGH_MEMORY	Out of memory.
 */
HRESULT M4LMsgServiceAdmin::CreateMsgService(const TCHAR *lpszService,
    const TCHAR *lpszDisplayName, ULONG_PTR ulUIParam, ULONG ulFlags)
{
	return CreateMsgServiceEx(reinterpret_cast<const char *>(lpszService),
	       reinterpret_cast<const char *>(lpszDisplayName), 0, ulFlags,
	       nullptr);
}

HRESULT M4LMsgServiceAdmin::CreateMsgServiceEx(const char *lpszService,
    const char *lpszDisplayName, ULONG_PTR ulUIParam, ULONG ulFlags,
    MAPIUID *uid)
{
	HRESULT hr = hrSuccess;
	std::unique_ptr<serviceEntry> entry;
	serviceEntry *rawent;
	SVCService* service = NULL;
	const SPropValue *lpProp = NULL;
	
	if(lpszService == NULL || lpszDisplayName == NULL) {
		ec_log_err("M4LMsgServiceAdmin::CreateMsgService(): invalid parameters");
		return MAPI_E_INVALID_PARAMETER;
	}

	scoped_rlock l_srv(m_mutexserviceadmin);
	hr = m4l_lpMAPISVC->GetService(reinterpret_cast<const TCHAR *>(lpszService), ulFlags, &service);
	if (hr == MAPI_E_NOT_FOUND) {
		ec_log_err("M4LMsgServiceAdmin::CreateMsgService(): get service \"%s\" failed: %s (%x). "
			"Does a config file exist for the service? (/usr/lib/mapi.d, /etc/mapi.d)",
			lpszService, GetMAPIErrorMessage(hr), hr);
		return hr;
	} else if (hr != hrSuccess) {
		ec_log_err("M4LMsgServiceAdmin::CreateMsgService(): get service \"%s\" failed: %s (%x)",
			lpszService, GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	// Create a Kopano message service
	if (findServiceAdmin(reinterpret_cast<const TCHAR *>(lpszService)) != nullptr) {
		kc_perrorf("service already exists", hr);
		return MAPI_E_NO_ACCESS; /* already exists */
	}
	entry.reset(new(std::nothrow) serviceEntry);
	if (!entry) {
		ec_log_crit("M4LMsgServiceAdmin::CreateMsgService(): ENOMEM");
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}
	entry->provideradmin.reset(new(std::nothrow) M4LProviderAdmin(this, lpszService));
	if (!entry->provideradmin) {
		ec_log_crit("M4LMsgServiceAdmin::CreateMsgService(): ENOMEM(2)");
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}
	entry->servicename = (char*)lpszService;
	lpProp = service->GetProp(PR_DISPLAY_NAME_A);
	entry->displayname = lpProp ? lpProp->Value.lpszA : (char*)lpszService;
	
	CoCreateGuid((LPGUID)&entry->muid);
	if (uid != nullptr)
		*uid = entry->muid;
	entry->service = service;
	rawent = entry.get();
	/* @entry needs to be in the list for CreateProviders() to find it */
	services.emplace_back(std::move(entry));
	// calls entry->provideradmin->CreateProvider for each provider read from mapisvc.inf
	hr = service->CreateProviders(rawent->provideradmin);
	rawent->bInitialize = false;
    return hr;
}

/**
 * Delete message service from this profile.
 *
 * @param[in]	lpUID		MAPIUID (guid) of the service to remove
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Service not in profile.
 */
HRESULT M4LMsgServiceAdmin::DeleteMsgService(const MAPIUID *lpUID)
{
	decltype(services)::iterator i;
	decltype(providers)::iterator p;
	scoped_rlock l_srv(m_mutexserviceadmin);

	for (i = services.begin(); i != services.end(); ++i)
		if (memcmp(&(*i)->muid, lpUID, sizeof(MAPIUID)) == 0)
			break;
	if (i == services.cend()) {
		ec_log_err("M4LMsgServiceAdmin::DeleteMsgService(): GUID not found");
		return MAPI_E_NOT_FOUND;
	}
    
    p = providers.begin();
    while (p != providers.end()) {
		if ((*p)->servicename != (*i)->servicename) {
			++p;
			continue;
		}
		p = providers.erase(p);
    }
	auto ret = (*i)->service->MSGServiceEntry()(0, nullptr, nullptr, 0, 0,
	           MSG_SERVICE_DELETE, 0, nullptr, (*i)->provideradmin, nullptr);
	if (ret != hrSuccess)
		/* ignore */;
	services.erase(i);
	return hrSuccess;
}

HRESULT M4LMsgServiceAdmin::CopyMsgService(const MAPIUID *lpUID,
    const TCHAR *lpszDisplayName, const IID *lpInterfaceToCopy,
    const IID *lpInterfaceDst, void *lpObjectDst, ULONG_PTR ulUIParam,
    ULONG ulFlags)
{
	ec_log_err("M4LMsgServiceAdmin::CopyMsgService() not implemented");
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMsgServiceAdmin::RenameMsgService(const MAPIUID *lpUID,
    ULONG ulFlags, const TCHAR *lpszDisplayName)
{
	ec_log_err("M4LMsgServiceAdmin::RenameMsgService() not implemented");
    return MAPI_E_NO_SUPPORT;
}

/**
 * Calls MSGServiceEntry of the given service in lpUID.
 *
 * @param[in]	lpUID		MAPIUID (guid) of the service to call MSGServiceEntry on.
 * @param[in]	ulUIParam	Passed to MSGServiceEntry
 * @param[in]	ulFlags		Passed to MSGServiceEntry. If MAPI_UNICODE is passed, lpProps should contain PT_UNICODE strings.
 * @param[in]	cValues		Passed to MSGServiceEntry
 * @param[in]	lpProps		Passed to MSGServiceEntry
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Service not available.
 */
HRESULT M4LMsgServiceAdmin::ConfigureMsgService(const MAPIUID *lpUID,
    ULONG_PTR ulUIParam, ULONG ulFlags, ULONG cValues,
    const SPropValue *lpProps)
{
    serviceEntry* entry;
	
	if (lpUID == NULL) {
		ec_log_err("M4LMsgServiceAdmin::ConfigureMsgService() invalid parameters");
		return MAPI_E_INVALID_PARAMETER;
	}
	ulock_rec l_srv(m_mutexserviceadmin);
	entry = findServiceAdmin(lpUID);
	if (!entry) {
		ec_log_err("M4LMsgServiceAdmin::ConfigureMsgService() service not found");
		return MAPI_E_NOT_FOUND;
	}

	// call kopano client Message Service Entry (provider/client/EntryPoint.cpp)
	auto hr = entry->service->MSGServiceEntry()(0, nullptr, nullptr,
	          ulUIParam, ulFlags, MSG_SERVICE_CONFIGURE, cValues, lpProps,
	          (IProviderAdmin *)entry->provideradmin, nullptr);
	if (hr != hrSuccess)
		return kc_perrorf("MSGServiceEntry failed", hr);
	entry->bInitialize = true;
	return hrSuccess;
}

/**
 * Get the IProfSect object for a service in the profile.
 *
 * @param[in]	lpUID		MAPIUID (guid) of the service.
 * @param[in]	lpInterface	IID request a specific interface on the profilesection. If NULL, IID_IProfSect is used.
 * @param[in]	ulFlags		Unused in Linux.
 * @param[out]	lppProfSect	IProfSect object of the service.
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Service not in profile.
 */
HRESULT M4LMsgServiceAdmin::OpenProfileSection(const MAPIUID *lpUID,
    const IID *lpInterface, ULONG ulFlags, IProfSect **lppProfSect)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpsPropVal;
	object_ptr<IMAPIProp> lpMapiProp;
	providerEntry* entry;
	ulock_rec l_srv(m_mutexserviceadmin);

	if(lpUID && memcmp(lpUID, &pbGlobalProfileSectionGuid, sizeof(MAPIUID)) == 0) {
		hr = this->profilesection->QueryInterface( (lpInterface)?*lpInterface:IID_IProfSect, (void**)lppProfSect);
	} else if (lpUID && memcmp(lpUID, &MUID_PROFILE_INSTANCE, sizeof(MAPIUID)) == 0) {
		// hack to support MUID_PROFILE_INSTANCE
		*lppProfSect = new M4LProfSect();
		(*lppProfSect)->AddRef();

		// @todo add PR_SEARCH_KEY should be a profile unique GUID

		// Set the default profilename
		hr = this->profilesection->QueryInterface(IID_IMAPIProp, &~lpMapiProp);
		if (hr != hrSuccess)
			return kc_perrorf("QueryInterface failed", hr);
		hr = HrGetOneProp(lpMapiProp, PR_PROFILE_NAME_A, &~lpsPropVal);
		if (hr != hrSuccess)
			return kc_perrorf("HrGetOneProp failed", hr);
		hr = (*lppProfSect)->SetProps(1, lpsPropVal, NULL);
		if (hr != hrSuccess)
			return kc_perrorf("SetProps failed", hr);
	} else if (lpUID == nullptr) {
		// Profile section NULL, create a temporary profile section that will be discarded
		*lppProfSect = new M4LProfSect();
		(*lppProfSect)->AddRef();
	} else {
		entry = findProvider(lpUID);
		if (entry == nullptr)
			return MAPI_E_NOT_FOUND;
		hr = entry->profilesection->QueryInterface((lpInterface) ? *lpInterface : IID_IProfSect, (void **)lppProfSect);
		if (hr != hrSuccess)
			kc_perrorf("QueryInterface failed(2)", hr);
	}
    return hr;
}

HRESULT M4LMsgServiceAdmin::MsgServiceTransportOrder(ULONG cUID,
    const MAPIUID *lpUIDList, ULONG ulFlags)
{
	ec_log_err("M4LMsgServiceAdmin::MsgServiceTransportOrder not implemented");
	return MAPI_E_NO_SUPPORT;
}

/**
 * Get the IProviderAdmin object for a service in the profile.
 *
 * @param[in]	lpUID		MAPIUID (guid) of the service.
 * @param[in]	ulFlags		Unused in Linux.
 * @param[out]	lppProviderAdmin	IProviderAdmin object of the service.
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Service not in profile.
 */
HRESULT M4LMsgServiceAdmin::AdminProviders(const MAPIUID *lpUID, ULONG ulFlags,
    IProviderAdmin **lppProviderAdmin)
{
	serviceEntry* entry = NULL;
	scoped_rlock l_srv(m_mutexserviceadmin);

	entry = findServiceAdmin(lpUID);
	if (!entry) {
		ec_log_err("M4LMsgServiceAdmin::AdminProviders(): service admin not found");
		return MAPI_E_NOT_FOUND;
	}

	auto hr = entry->provideradmin->QueryInterface(IID_IProviderAdmin,
	          reinterpret_cast<void **>(lppProviderAdmin));
	if (hr != hrSuccess)
		kc_perrorf("QueryInterface failed", hr);
	return hr;
}

HRESULT M4LMsgServiceAdmin::SetPrimaryIdentity(const MAPIUID *lpUID,
    ULONG ulFlags)
{
	ec_log_err("M4LMsgServiceAdmin::SetPrimaryIdentity not implemented");
	return MAPI_E_NO_SUPPORT;
}

/**
 * Get a list of all providers in the profile in a IMAPITable
 * object. No notifications for changes are sent.
 *
 * @param[in]	ulFlags		Unused in Linux.
 * @param[out]	lppTable	IMAPITable object
 * @return		HRESULT
 */
HRESULT M4LMsgServiceAdmin::GetProviderTable(ULONG ulFlags, LPMAPITABLE* lppTable) {
	HRESULT hr = hrSuccess;
	ULONG cValues = 0;
	object_ptr<ECMemTable> lpTable;
	object_ptr<ECMemTableView> lpTableView;
	ULONG cValuesDest = 0;
	SPropValue sPropID;
	int n = 0;
	memory_ptr<SPropTagArray> lpPropTagArray;
	SizedSPropTagArray(11, sptaProviderCols) = {11, {PR_MDB_PROVIDER, PR_AB_PROVIDER_ID, PR_INSTANCE_KEY, PR_RECORD_KEY, PR_ENTRYID,
												   PR_DISPLAY_NAME_A, PR_OBJECT_TYPE, PR_PROVIDER_UID, PR_RESOURCE_TYPE,
												   PR_PROVIDER_DISPLAY_A, PR_SERVICE_UID}};
	Util::proptag_change_unicode(ulFlags, sptaProviderCols);
	hr = ECMemTable::Create(sptaProviderCols, PR_ROWID, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("ECMemTable::Create failed", hr);

	ulock_rec l_srv(m_mutexserviceadmin);
	for (auto &serv : services) {
		if (serv->bInitialize)
			continue;
		hr = serv->service->MSGServiceEntry()(0, NULL, NULL, 0,
		     0, MSG_SERVICE_CREATE, 0, NULL,
		     static_cast<IProviderAdmin *>(serv->provideradmin),
		     NULL);
		if (hr != hrSuccess)
			return kc_perrorf("MSGServiceEntry failed", hr);
		serv->bInitialize = true;
	}

	// Loop through all providers, add each to the table
	for (auto &prov : providers) {
		memory_ptr<SPropValue> lpDest, lpsProps;

		hr = prov->profilesection->GetProps(lpPropTagArray, 0, &cValues, &~lpsProps);
		if (FAILED(hr))
			return kc_perrorf("GetProps failed", hr);
		
		sPropID.ulPropTag = PR_ROWID;
		sPropID.Value.ul = n++;
		
		hr = Util::HrAddToPropertyArray(lpsProps, cValues, &sPropID, &~lpDest, &cValuesDest);
		if (hr != hrSuccess)
			return kc_perrorf("Util::HrAddToPropertyArray failed", hr);
		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, lpDest, cValuesDest);
		if (hr != hrSuccess)
			return kc_perrorf("HrModifyRow failed", hr);
	}
	
	hr = lpTable->HrGetView(createLocaleFromName(""), ulFlags, &~lpTableView);
	if (hr != hrSuccess)
		return kc_perrorf("HrGetView failed", hr);
	hr = lpTableView->QueryInterface(IID_IMAPITable, (void **)lppTable);
	if (hr != hrSuccess)
		kc_perrorf("QueryInterface(2) failed", hr);
	return hr;
}

HRESULT M4LMsgServiceAdmin::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IMsgServiceAdmin2) {
		AddRef();
		*lpvoid = static_cast<IMsgServiceAdmin2 *>(this);
	} else if (refiid == IID_IMsgServiceAdmin) {
		AddRef();
		*lpvoid = static_cast<IMsgServiceAdmin *>(this);
	} else if (refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = static_cast<IUnknown *>(this);
	} else
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	return hrSuccess;
}

// ---
// M4LMAPISession
// ---
M4LMAPISession::M4LMAPISession(const TCHAR *pn, M4LMsgServiceAdmin *sa) :
	profileName(reinterpret_cast<const char *>(pn)), serviceAdmin(sa)
{
}

HRESULT M4LMAPISession::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError) {
    *lppMAPIError = NULL;
    return hrSuccess;
}

/**
 * Get a list of all message stores in this session. With Kopano in
 * Linux, this is always at least your own and the public where
 * available.
 *
 * @param[in]	ulFlags		Unused in Linux.
 * @param[out]	lppTable	IMAPITable object
 * @return		HRESULT
 */
HRESULT M4LMAPISession::GetMsgStoresTable(ULONG ulFlags, LPMAPITABLE* lppTable) {
	HRESULT hr = hrSuccess;
	ULONG cValues = 0;
	object_ptr<ECMemTable> lpTable;
	object_ptr<ECMemTableView> lpTableView;
	ULONG cValuesDest = 0;
	SPropValue sPropID;
	int n = 0;
	memory_ptr<SPropTagArray> lpPropTagArray;
	SizedSPropTagArray(11, sptaProviderCols) = {11, {PR_MDB_PROVIDER, PR_INSTANCE_KEY, PR_RECORD_KEY, PR_ENTRYID,
												   PR_DISPLAY_NAME_A, PR_OBJECT_TYPE, PR_RESOURCE_TYPE, PR_PROVIDER_UID,
												   PR_RESOURCE_FLAGS, PR_DEFAULT_STORE, PR_PROVIDER_DISPLAY_A}};
	Util::proptag_change_unicode(ulFlags, sptaProviderCols);
	hr = ECMemTable::Create(sptaProviderCols, PR_ROWID, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("ECMemTable::Create failed", hr);
	
	// Loop through all providers, add each to the table
	ulock_rec l_srv(serviceAdmin->m_mutexserviceadmin);
	for (auto &prov : serviceAdmin->providers) {
		memory_ptr<SPropValue> lpDest, lpsProps;

		hr = prov->profilesection->GetProps(lpPropTagArray, 0, &cValues, &~lpsProps);
		if (FAILED(hr))
			return kc_perrorf("GetProps failed", hr);

		auto lpType = PCpropFindProp(lpsProps, cValues, PR_RESOURCE_TYPE);
		if(lpType == NULL || lpType->Value.ul != MAPI_STORE_PROVIDER)
			continue;

		sPropID.ulPropTag = PR_ROWID;
		sPropID.Value.ul = n++;
		
		hr = Util::HrAddToPropertyArray(lpsProps, cValues, &sPropID, &~lpDest, &cValuesDest);
		if (hr != hrSuccess)
			return kc_perrorf("Util::HrAddToPropertyArray failed", hr);
		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, lpDest, cValuesDest);
		if (hr != hrSuccess)
			return kc_perrorf("HrModifyRow failed", hr);
	}
	
	hr = lpTable->HrGetView(createLocaleFromName(""), ulFlags, &~lpTableView);
	if (hr != hrSuccess)
		return kc_perrorf("HrGetView failed", hr);
	hr = lpTableView->QueryInterface(IID_IMAPITable, (void **)lppTable);
	if (hr != hrSuccess)
		kc_perrorf("QueryInterface failed", hr);
	return hr;
}

/**
 * Open a Message Store on the server.
 *
 * @param[in]	ulUIParam	Unused.
 * @param[in]	cbEntryID	Size of lpEntryID
 * @param[in]	lpEntryID	EntryID identifier of store.
 * @param[in]	lpInterface	Requested interface on lppMDB return value.
 * @param[in]	ulFlags		Passed to MSProviderInit function of provider of the store. In Linux always libkcclient.so.
 * @param[out]	lppMDB		Pointer to IMsgStore object
 * @return		HRESULT
 */
HRESULT M4LMAPISession::OpenMsgStore(ULONG_PTR ulUIParam, ULONG cbEntryID,
    const ENTRYID *lpEntryID, LPCIID lpInterface, ULONG ulFlags,
    IMsgStore **lppMDB)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMSProvider> msp;
	object_ptr<IMAPISupport> lpISupport;
	object_ptr<IMsgStore> mdb;
	ULONG mdbver;
	// I don't want these ...
	ULONG sizeSpoolSec;
	memory_ptr<BYTE> pSpoolSec;
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpsRows;
	MAPIUID sProviderUID;
	ULONG cbStoreEntryID = 0;
	memory_ptr<ENTRYID> lpStoreEntryID;
	SVCService *service = NULL;

	SizedSPropTagArray(2, sptaProviders) = { 2, {PR_RECORD_KEY, PR_PROVIDER_UID} };

	if (lpEntryID == NULL || lppMDB == NULL) {
		ec_log_err("M4LMAPISession::OpenMsgStore() invalid parameters");
		return MAPI_E_INVALID_PARAMETER;
	}

	// unwrap mapi store entry
	hr = UnWrapStoreEntryID(cbEntryID, lpEntryID, &cbStoreEntryID, &~lpStoreEntryID);
	if (hr != hrSuccess)
		return kc_perrorf("UnWrapStoreEntryID failed", hr);

	// padding in entryid solves string ending
	hr = m4l_lpMAPISVC->GetService((char*)lpEntryID+4+sizeof(GUID)+2, &service);
	if (hr != hrSuccess)
		return kc_perrorf("GetService failed", hr);
	
	// Find the profile section associated with this entryID
	hr = serviceAdmin->GetProviderTable(0, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("GetProviderTable failed", hr);
	hr = lpTable->SetColumns(sptaProviders, 0);
	if (hr != hrSuccess)
		return kc_perrorf("SetColumns failed", hr);
		
	while(TRUE) {
		hr = lpTable->QueryRows(1, 0, &~lpsRows);
		if (hr != hrSuccess)
			return kc_perrorf("QueryRows failed", hr);
		if(lpsRows->cRows != 1)
			break;
		if (lpsRows[0].lpProps[0].ulPropTag == PR_RECORD_KEY &&
		    lpsRows[0].lpProps[0].Value.bin.cb == sizeof(GUID) &&
		    memcmp(lpsRows[0].lpProps[0].Value.bin.lpb, reinterpret_cast<char *>(lpStoreEntryID.get()) + 4, sizeof(GUID)) == 0) {
			// Found it
			memcpy(&sProviderUID, lpsRows[0].lpProps[1].Value.bin.lpb, sizeof(MAPIUID));
			break;
			
		}
	}
	
	if (lpsRows->cRows != 1)
		// No provider for the store, use a temporary profile section
		lpISupport.reset(new M4LMAPISupport(this, NULL, service));
	else
		lpISupport.reset(new M4LMAPISupport(this, &sProviderUID, service));

	// call kopano client for the Message Store Provider (provider/client/EntryPoint.cpp)
	hr = service->MSProviderInit()(0, nullptr, MAPIAllocateBuffer, MAPIAllocateMore, MAPIFreeBuffer, ulFlags, CURRENT_SPI_VERSION, &mdbver, &~msp);
	if (hr != hrSuccess)
		return kc_perrorf("MSProviderInit failed", hr);
	hr = msp->Logon(lpISupport, 0, (LPTSTR)profileName.c_str(), cbStoreEntryID, lpStoreEntryID, ulFlags, nullptr, &sizeSpoolSec, &~pSpoolSec, nullptr, nullptr, &~mdb);
	if (hr != hrSuccess)
		return kc_perrorf("msp->Logon failed", hr);
	hr = mdb->QueryInterface(lpInterface ? (*lpInterface) : IID_IMsgStore, (void**)lppMDB);
	if (hr != hrSuccess)
		kc_perrorf("QueryInterface failed", hr);
	return hr;
}

/**
 * Opens the Global Addressbook from a provider of the profile (service admin).
 *
 * @param[in]	ulUIParam	Unused in Linux.
 * @param[in]	lpInterface	Requested interface on the addressbook. If NULL, IID_IAddrBook is used.
 * @param[in]	ulFlags		Passed to ABProviderInit of the provider.
 * @param[out]	lppAdrBook	Pointer to an IAddrBook object
 * @return		HRESULT
 * @retval		MAPI_E_CALL_FAILED				Provider not available
 * @retval		MAPI_E_NOT_ENOUGH_MEMORY		Out of memory
 * @retval		MAPI_E_INTERFACE_NOT_SUPPORTED	Invalid lpInterface parameter
 */
HRESULT M4LMAPISession::OpenAddressBook(ULONG_PTR ulUIParam, LPCIID lpInterface,
    ULONG ulFlags, LPADRBOOK *lppAdrBook)
{
	HRESULT hr = hrSuccess;
	object_ptr<M4LAddrBook> myAddrBook;
	ULONG abver;
	object_ptr<IMAPISupport> lpMAPISup;
	SPropValue sProp;

	lpMAPISup.reset(new(std::nothrow) M4LMAPISupport(this, nullptr, nullptr));
	if (!lpMAPISup) {
		ec_log_crit("M4LMAPISession::OpenAddressBook(): ENOMEM");
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}
	myAddrBook.reset(new(std::nothrow) M4LAddrBook(serviceAdmin, lpMAPISup));
	if (myAddrBook == nullptr) {
		ec_log_crit("M4LMAPISession::OpenAddressBook(): ENOMEM(2)");
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}

	// Set default properties
	sProp.ulPropTag = PR_OBJECT_TYPE;
	sProp.Value.ul = MAPI_ADDRBOOK;
	hr = myAddrBook->SetProps(1, &sProp, nullptr);
	if (hr != hrSuccess)
		return kc_perrorf("SetProps failed", hr);
	hr = myAddrBook->QueryInterface(lpInterface ? *lpInterface : IID_IAddrBook, reinterpret_cast<void **>(lppAdrBook));
	if (hr != hrSuccess)
		return kc_perrorf("QueryInterface failed", hr);

	for (auto &serv : serviceAdmin->services) {
		if (serv->service->ABProviderInit() == NULL)
			continue;

		object_ptr<IABProvider> lpABProvider;
		if (serv->service->ABProviderInit()(0, nullptr,
		    MAPIAllocateBuffer, MAPIAllocateMore, MAPIFreeBuffer,
		    ulFlags, CURRENT_SPI_VERSION, &abver,
		    &~lpABProvider) != hrSuccess) {
			hr = MAPI_W_ERRORS_RETURNED;
			continue;
		}
		std::vector<SVCProvider *> vABProviders = serv->service->GetProviders();
		LPSPropValue lpProps;
		ULONG cValues;
		for (const auto prov : vABProviders) {
			LPSPropValue lpUID;
			LPSPropValue lpProp;
			std::string strDisplayName = "<unknown>";
			prov->GetProps(&cValues, &lpProps);

			lpProp = PpropFindProp(lpProps, cValues, PR_RESOURCE_TYPE);
			lpUID = PpropFindProp(lpProps, cValues, PR_AB_PROVIDER_ID);
			if (!lpUID || !lpProp || lpProp->Value.ul != MAPI_AB_PROVIDER)
				continue;

			lpProp = PpropFindProp(lpProps, cValues, PR_DISPLAY_NAME_A);
			if (lpProp)
				strDisplayName = lpProp->Value.lpszA;

			if (myAddrBook->addProvider(profileName, strDisplayName, (LPMAPIUID)lpUID->Value.bin.lpb, lpABProvider) != hrSuccess)
				hr = MAPI_W_ERRORS_RETURNED;
		}
	}
	// If returning S_OK or MAPI_W_ERRORS_RETURNED, lppAdrBook must be set
	return hr;
}

HRESULT M4LMAPISession::OpenProfileSection(const MAPIUID *lpUID,
    const IID *lpInterface, ULONG ulFlags, IProfSect **lppProfSect)
{
	return serviceAdmin->OpenProfileSection(lpUID, lpInterface, ulFlags, lppProfSect);
}

HRESULT M4LMAPISession::GetStatusTable(ULONG ulFlags, LPMAPITABLE* lppTable) {
	ec_log_err("M4LMAPISession::GetStatusTable not implemented");
	return MAPI_E_NO_SUPPORT;
}

/**
 * Opens any object identified by lpEntryID from a provider in the profile.
 *
 * @param[in]	cbEntryID	Size of lpEntryID.
 * @param[in]	lpEntryID	Unique identifier of an object.
 * @param[in]	lpInterface	Requested interface on the object. If NULL, default interface of objecttype is used.
 * @param[in]	ulFlags		Passed to OpenEntry of the provider of the EntryID.
 * @param[out]	lpulObjType	Type of the object returned. Eg. MAPI_MESSAGE, MAPI_STORE, etc.
 * @param[out]	lppUnk		IUnknown interface pointer of the object returned. Can be cast to the object returned in lpulObjType.
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND				lpEntryID is NULL, or not found to be an entryid for any provider of your profile.
 * @retval		MAPI_E_NOT_ENOUGH_MEMORY		Out of memory
 * @retval		MAPI_E_INVALID_ENTRYID			lpEntryID does not point to an entryid
 * @retval		MAPI_E_INTERFACE_NOT_SUPPORTED	Invalid lpInterface parameter for found object
 */
HRESULT M4LMAPISession::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
    HRESULT hr = hrSuccess;
	object_ptr<IMAPITable> lpTable;
	object_ptr<IAddrBook> lpAddrBook;
    IMsgStore *lpMDB = NULL;
    ULONG cbUnWrappedID = 0;
	memory_ptr<ENTRYID> lpUnWrappedID;
	SizedSPropTagArray(3, sptaProviders) = { 3, {PR_ENTRYID, PR_RECORD_KEY, PR_RESOURCE_TYPE} };
	GUID guidProvider;
	bool bStoreEntryID = false;
	MAPIUID muidOneOff = {MAPI_ONE_OFF_UID};

    if (lpEntryID == NULL) {
	ec_log_err("M4LMAPISession::OpenEntry() invalid parameters");
		return MAPI_E_NOT_FOUND;
    }

	if (cbEntryID <= (4 + sizeof(GUID)) ) {
		ec_log_err("M4LMAPISession::OpenEntry() cbEntryId too small");
		return MAPI_E_INVALID_ENTRYID;
	}
   
	// If this a wrapped entryid, just unwrap them.
	if (memcmp(&muidStoreWrap, &lpEntryID->ab, sizeof(GUID)) == 0) {
		hr = UnWrapStoreEntryID(cbEntryID, lpEntryID, &cbUnWrappedID, &~lpUnWrappedID);
		if (hr != hrSuccess) {
			ec_log_err("M4LMAPISession::OpenEntry() UnWrapStoreEntryID failed");
			return hr;
		}

		cbEntryID = cbUnWrappedID;
		lpEntryID = lpUnWrappedID;
		bStoreEntryID = true;
 	}

	// first 16 bytes are the store/addrbook GUID
	memcpy(&guidProvider, &lpEntryID->ab, sizeof(GUID));
        
	// See if we already have the store open
	decltype(mapStores)::const_iterator iterStores = mapStores.find(guidProvider);
	if (iterStores != mapStores.cend()) {
		if (bStoreEntryID == true) {
			hr = iterStores->second->QueryInterface(IID_IMsgStore, (void**)lppUnk);
			if (hr == hrSuccess)
				*lpulObjType = MAPI_STORE;
		}
		else {
			hr = iterStores->second->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
		}

		if (hr != hrSuccess)
			kc_perrorf("store open check failed", hr);
		return hr;
	}

	// If this is an addressbook EntryID or a one-off entryid, use the addressbook to open the item
	if (memcmp(&guidProvider, &muidOneOff, sizeof(GUID)) == 0) {
		hr = OpenAddressBook(0, NULL, AB_NO_DIALOG, &~lpAddrBook);
		if (hr != hrSuccess)
			return kc_perrorf("OpenAddressBook failed", hr);
		hr = lpAddrBook->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
		if (hr != hrSuccess)
			return kc_perrorf("OpenEntry failed", hr);
		return hr;
    }
            
    // If not, it must be a provider entryid, so we have to find the provider

	// Find the profile section associated with this entryID
	hr = serviceAdmin->GetProviderTable(0, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("GetProviderTable failed", hr);
	hr = lpTable->SetColumns(sptaProviders, 0);
	if (hr != hrSuccess)
		return kc_perrorf("SetColumns failed", hr);
		
	while(TRUE) {
		rowset_ptr lpsRows;
		hr = lpTable->QueryRows(1, 0, &~lpsRows);
		if (hr != hrSuccess)
			return kc_perrorf("QueryRows failed", hr);
		if (lpsRows->cRows != 1)
			return MAPI_E_NOT_FOUND;
		if (lpsRows[0].lpProps[0].ulPropTag != PR_ENTRYID ||
		    lpsRows[0].lpProps[1].ulPropTag != PR_RECORD_KEY ||
		    lpsRows[0].lpProps[1].Value.bin.cb != sizeof(GUID) ||
		    memcmp(lpsRows[0].lpProps[1].Value.bin.lpb, &guidProvider, sizeof(GUID)) != 0)
			continue;

		if (lpsRows[0].lpProps[2].ulPropTag == PR_RESOURCE_TYPE &&
		    lpsRows[0].lpProps[2].Value.ul == MAPI_AB_PROVIDER) {
			hr = OpenAddressBook(0, NULL, AB_NO_DIALOG, &~lpAddrBook);
			if (hr != hrSuccess)
				return kc_perrorf("OpenAddressBook(2) failed", hr);
			hr = lpAddrBook->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
			if(hr != hrSuccess)
				kc_perrorf("OpenEntry(2) failed", hr);
			return hr;
		}

		hr = OpenMsgStore(0, lpsRows[0].lpProps[0].Value.bin.cb, reinterpret_cast<const ENTRYID *>(lpsRows[0].lpProps[0].Value.bin.lpb),
		     &IID_IMsgStore, MDB_WRITE | MDB_NO_DIALOG | MDB_TEMPORARY, &lpMDB);
		if (hr != hrSuccess)
			return kc_perrorf("OpenMsgStore failed", hr);

		// Keep the store open in case somebody else needs it later (only via this function)
		mapStores.emplace(guidProvider, object_ptr<IMsgStore>(lpMDB, false));
		if (bStoreEntryID == true) {
			hr = lpMDB->QueryInterface(IID_IMsgStore, (void **)lppUnk);
			if (hr == hrSuccess)
				*lpulObjType = MAPI_STORE;
		}
		else {
			hr = lpMDB->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
			if (hr != hrSuccess)
				kc_perrorf("OpenEntry(3) failed", hr);
		}
		return hr;
	}
	return hr;
}

/**
 * Compare two EntryIDs.
 * 
 * @param[in]	cbEntryID1		Length of the first entryid
 * @param[in]	lpEntryID1		First entryid.
 * @param[in]	cbEntryID2		Length of the first entryid
 * @param[in]	lpEntryID2		First entryid.
 * @param[in]	ulFlags			Unused.
 * @param[out]	lpulResult		TRUE if EntryIDs are the same, otherwise FALSE.
 *
 * @return		HRESULT
 * @retval		MAPI_E_INVALID_ENTRYID	either lpEntryID1 or lpEntryID2 is NULL
 */
HRESULT M4LMAPISession::CompareEntryIDs(ULONG cbEntryID1,
    const ENTRYID *lpEntryID1, ULONG cbEntryID2, const ENTRYID *lpEntryID2,
    ULONG ulFlags, ULONG *lpulResult)
{
	if (cbEntryID1 != cbEntryID2)
		*lpulResult = FALSE;
	else if (!lpEntryID1 || !lpEntryID2)
		return MAPI_E_INVALID_ENTRYID;
	else if (memcmp(lpEntryID1, lpEntryID2, cbEntryID1) != 0)
		*lpulResult = FALSE;
	else
		*lpulResult = TRUE;

	return hrSuccess;
}

/**
 * Request notifications on an object identified by lpEntryID. EntryID
 * must be on the default store in Linux.
 * 
 * @param[in]	cbEntryID		Length of lpEntryID
 * @param[in]	lpEntryID		EntryID of object to Advise on.
 * @param[in]	ulEventMask		Bitmask of events to receive notifications on.
 * @param[in]	lpAdviseSink	Callback function for notifications.
 * @param[out]	lpulConnection	Connection identifier for this notification, for use with IMAPISession::Unadvise()
 *
 * @return		HRESULT
 * @retval		MAPI_E_INVALID_ENTRYID	either lpEntryID1 or lpEntryID2 is NULL
 */
HRESULT M4LMAPISession::Advise(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulEventMask, IMAPIAdviseSink *lpAdviseSink, ULONG *lpulConnection)
{
	object_ptr<IMsgStore> lpMsgStore;

	//FIXME: Advise should handle one or more stores/addressbooks not only the default store,
	//       the entry identifier can be an address book, message store object or
	//       NULL which means an advise on the MAPISession.
	//       MAPISessions should hold his own ulConnection list because it should work 
	//       with one or more different objects.
	auto hr = HrOpenDefaultStore(this, &~lpMsgStore);
	if (hr != hrSuccess)
		return kc_perrorf("HrOpenDefaultStore failed", hr);
	hr = lpMsgStore->Advise(cbEntryID, lpEntryID, ulEventMask, lpAdviseSink, lpulConnection);
	if (hr != hrSuccess)
		kc_perrorf("Advise failed", hr);
	return hr;
}

/**
 * Remove request for notifications for a specific ID.
 * 
 * @param[in]	ulConnection	Connection identifier of Adivse call.
 *
 * @return		HRESULT
 */
HRESULT M4LMAPISession::Unadvise(ULONG ulConnection) {
	object_ptr<IMsgStore> lpMsgStore;

	// FIXME: should work with an internal list of connections ids, see M4LMAPISession::Advise for more information.
	auto hr = HrOpenDefaultStore(this, &~lpMsgStore);
	if (hr != hrSuccess)
		return kc_perrorf("HrOpenDefaultStore failed", hr);
	hr = lpMsgStore->Unadvise(ulConnection);
	if (hr != hrSuccess)
		kc_perrorf("Unadvise failed", hr);
	return hr;
}

HRESULT M4LMAPISession::MessageOptions(ULONG_PTR ui_param, ULONG flags,
    const TCHAR *addrtype, IMessage *)
{
	ec_log_err("M4LMAPISessionM4LMAPISession::MessageOptions not implemented");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISession::QueryDefaultMessageOpt(const TCHAR *addrtype,
    ULONG flags, ULONG *nvals, SPropValue **opts)
{
	ec_log_err("M4LMAPISession::QueryDefaultMessageOpt not implemented");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISession::EnumAdrTypes(ULONG flags, ULONG *ntypes, TCHAR ***types)
{
	ec_log_err("M4LMAPISession::EnumAdrTypes not implemented");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISession::QueryIdentity(ULONG* lpcbEntryID, LPENTRYID* lppEntryID) {
	HRESULT hr = hrSuccess;
	LPENTRYID lpEntryID = NULL;
	scoped_lock l_srv(m_mutexStatusRow);

	auto lpProp = PCpropFindProp(this->m_lpPropsStatus, this->m_cValuesStatus, PR_IDENTITY_ENTRYID);
	if(lpProp == NULL) {
		ec_log_err("M4LMAPISession::QueryIdentity(): PCpropFindProp failed");
		return MAPI_E_NOT_FOUND;
	}

	if ((hr = MAPIAllocateBuffer(lpProp->Value.bin.cb, (void **)&lpEntryID)) != hrSuccess)
		return hr;
	memcpy(lpEntryID, lpProp->Value.bin.lpb, lpProp->Value.bin.cb);

	*lppEntryID = lpEntryID;
	*lpcbEntryID = lpProp->Value.bin.cb;
	return hrSuccess;
}

HRESULT M4LMAPISession::Logoff(ULONG_PTR ulUIParam, ULONG ulFlags,
    ULONG ulReserved)
{
	return hrSuccess;
}

HRESULT M4LMAPISession::SetDefaultStore(ULONG flags, ULONG eid_size, const ENTRYID *)
{
	ec_log_err("M4LMAPISession::SetDefaultStore(): not implemented");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISession::AdminServices(ULONG ulFlags, LPSERVICEADMIN* lppServiceAdmin) {
	HRESULT hr = hrSuccess;
	serviceAdmin->QueryInterface(IID_IMsgServiceAdmin,(void**)lppServiceAdmin);
	return hr;
}

HRESULT M4LMAPISession::ShowForm(ULONG_PTR ulUIParam, LPMDB lpMsgStore,
    LPMAPIFOLDER lpParentFolder, LPCIID lpInterface, ULONG ulMessageToken,
    LPMESSAGE lpMessageSent, ULONG ulFlags, ULONG ulMessageStatus,
    ULONG ulMessageFlags, ULONG ulAccess, LPSTR lpszMessageClass)
{
	ec_log_err("M4LMAPISession::ShowForm(): not implemented");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISession::PrepareForm(LPCIID lpInterface, LPMESSAGE lpMessage, ULONG* lpulMessageToken) {
	ec_log_err("M4LMAPISession::PrepareForm(): not implemented");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISession::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IMAPISession) {
		AddRef();
		*lpvoid = static_cast<IMAPISession *>(this);
	} else if (refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = static_cast<IUnknown *>(this);
	} else {
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	}
	return hrSuccess;
}

HRESULT M4LMAPISession::setStatusRow(ULONG cValues, LPSPropValue lpProps)
{
	scoped_lock l_status(m_mutexStatusRow);
	m_cValuesStatus = 0;
	HRESULT hr = Util::HrCopyPropertyArray(lpProps, cValues, &~m_lpPropsStatus, &m_cValuesStatus, true);
	if (hr != hrSuccess)
		kc_perrorf("Util::HrCopyPropertyArray failed", hr);
	return hr;
}

// ---
// M4LAddrBook
// ---
M4LAddrBook::M4LAddrBook(M4LMsgServiceAdmin *new_serviceAdmin,
    LPMAPISUP newlpMAPISup) :
	m_lpMAPISup(newlpMAPISup)
{}

M4LAddrBook::~M4LAddrBook() {
	for (const auto &i : m_lABProviders)
		i.lpABLogon->Logoff(0);
	if (m_lpSavedSearchPath)
		FreeProws(m_lpSavedSearchPath);
}

HRESULT M4LAddrBook::addProvider(const std::string &profilename, const std::string &displayname, LPMAPIUID lpUID, LPABPROVIDER newProvider) {
	HRESULT hr = hrSuccess;
	ULONG cbSecurity;
	memory_ptr<BYTE> lpSecurity;
	memory_ptr<MAPIERROR> lpMAPIError;
	object_ptr<IABLogon> lpABLogon;
	abEntry entry;

	hr = newProvider->Logon(m_lpMAPISup, 0, reinterpret_cast<const TCHAR *>(profilename.c_str()),
	     0, &cbSecurity, &~lpSecurity, &~lpMAPIError, &~lpABLogon);
	if (hr != hrSuccess)
		return kc_perrorf("logon failed", hr);

	// @todo?, call lpABLogon->OpenEntry(0,NULL) to get the root folder, and save that entryid that we can use for the GetHierarchyTable of our root container?
	memcpy(&entry.muid, lpUID, sizeof(MAPIUID));
	entry.displayname = displayname;
	entry.lpABProvider.reset(newProvider);
	entry.lpABLogon = std::move(lpABLogon);
	m_lABProviders.emplace_back(std::move(entry));
	return hrSuccess;
}

HRESULT M4LAddrBook::getDefaultSearchPath(ULONG ulFlags, LPSRowSet* lppSearchPath)
{
	HRESULT hr = hrSuccess;
	object_ptr<IABContainer> lpRoot;
	object_ptr<IMAPITable> lpTable;
	ULONG ulObjType;
	SPropValue sProp;
	ECAndRestriction cRes;

	hr = this->OpenEntry(0, nullptr, &IID_IABContainer, 0, &ulObjType, &~lpRoot);
	if (hr != hrSuccess)
		return kc_perrorf("OpenEntry failed", hr);
	hr = lpRoot->GetHierarchyTable((ulFlags & MAPI_UNICODE) | CONVENIENT_DEPTH, &~lpTable);
	if (hr != hrSuccess)
		return kc_perrorf("GetHierarchyTable failed", hr);

	// We add this restriction to filter out All Address Lists
	sProp.ulPropTag = 0xFFFD0003; //PR_EMS_AB_CONTAINERID;
	sProp.Value.ul = 7000;
	cRes += ECOrRestriction(
			ECPropertyRestriction(RELOP_NE, sProp.ulPropTag, &sProp, ECRestriction::Shallow) +
			ECNotRestriction(ECExistRestriction(sProp.ulPropTag)));
	// only folders, not groups
	sProp.ulPropTag = PR_DISPLAY_TYPE;
	sProp.Value.ul = DT_NOT_SPECIFIC;
	cRes += ECPropertyRestriction(RELOP_EQ, sProp.ulPropTag, &sProp, ECRestriction::Cheap);
	// only end folders, not root container folders
	cRes += ECBitMaskRestriction(BMR_EQZ, PR_CONTAINER_FLAGS, AB_SUBCONTAINERS);
	hr = cRes.RestrictTable(lpTable, 0);
	if (hr != hrSuccess)
		return kc_perrorf("Restrict failed", hr);
	hr = lpTable->QueryRows(-1, 0, lppSearchPath);
	if (hr != hrSuccess)
		kc_perrorf("QueryRows failed", hr);
	return hr;
}

// 
// How it works:
//   1. program calls M4LMAPISession::OpenAddressBook()
//      OpenAddressBook calls ABProviderInit() for all AB providers in the profile, and adds the returned
//      IABProvider/IABLogon interface to the Addressbook
//   2.1a. program calls M4LAddrBook::OpenEntry()
//         - lpEntryID == NULL, open root container.
//           this is a IABContainer. On this interface, use GetHierarchyTable() to get the list of all the providers'
//           entry IDs. (e.g. Global Address Book, Outlook addressbook)
//           This IABContainer version should be implemented as M4LABContainer
//         - lpEntryID != NULL, pass to correct IABLogon::OpenEntry()
//   2.1b. program calls M4LAddrBook::ResolveName()
//         - for every IABLogon object (in the searchpath), use OpenEntry() to get the IABContainer, and call ResolveNames()
// 

/**
 * Remove request for notifications for a specific ID.
 * 
 * @param[in]	cbEntryID	Length of lpEntryID.
 * @param[in]	lpEntryID	Unique entryid of a mapi object in the addressbook.
 * @param[in]	lpInterface	MAPI Interface to query on the object.
 * @param[in]	ulFlags		Passed to OpenEntry of the addressbook.
 * @param[out]	lpulObjType	The type of the return object
 * @param[out]	lppUnk		IUnknown pointer to the opened object. Cast to correct object.
 *
 * @return		HRESULT
 */
HRESULT M4LAddrBook::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	HRESULT hr = hrSuccess;
	std::wstring name, type, email;
	SPropValue sProps[5];

	if ((lpInterface == NULL || *lpInterface == IID_IMailUser || *lpInterface == IID_IMAPIProp || *lpInterface == IID_IUnknown) && lpEntryID != NULL) {
		hr = ECParseOneOff(lpEntryID, cbEntryID, name, type, email);
		if (hr == hrSuccess) {
			// yes, it was an one off, create IMailUser
			auto lpMailUser = new(std::nothrow) M4LMAPIProp;
			if (lpMailUser == nullptr)
				return MAPI_E_NOT_ENOUGH_MEMORY;
			lpMailUser->AddRef();

			sProps[0].ulPropTag = PR_DISPLAY_NAME_W;
			sProps[0].Value.lpszW = (WCHAR*)name.c_str();

			sProps[1].ulPropTag = PR_ADDRTYPE_W;
			sProps[1].Value.lpszW = (WCHAR*)type.c_str();

			sProps[2].ulPropTag = PR_EMAIL_ADDRESS_W;
			sProps[2].Value.lpszW = (WCHAR*)email.c_str();

			sProps[3].ulPropTag = PR_ENTRYID;
			sProps[3].Value.bin.cb = cbEntryID;
			sProps[3].Value.bin.lpb = (BYTE *)lpEntryID;

			sProps[4].ulPropTag = PR_OBJECT_TYPE;
			sProps[4].Value.ul = MAPI_MAILUSER;

			// also missing, but still not important:
			// PR_ENTRYID, PR_RECORD_KEY, PR_SEARCH_KEY, PR_SEND_INTERNET_ENCODING, PR_SEND_RICH_INFO

			lpMailUser->SetProps(5, sProps, NULL);
			if (lpInterface == nullptr || *lpInterface == IID_IMailUser)
				*lppUnk = reinterpret_cast<IUnknown *>(static_cast<IMailUser *>(lpMailUser));
			else if (*lpInterface == IID_IMAPIProp)
				*lppUnk = reinterpret_cast<IUnknown *>(static_cast<IMAPIProp *>(lpMailUser));
			else if (*lpInterface == IID_IUnknown)
				*lppUnk = static_cast<IUnknown *>(lpMailUser);
			*lpulObjType = MAPI_MAILUSER;
			return hr;
		}
	}

	if (lpEntryID == NULL && (lpInterface == NULL || *lpInterface == IID_IABContainer || *lpInterface == IID_IMAPIContainer || *lpInterface == IID_IMAPIProp || *lpInterface == IID_IUnknown)) {
		// 2.1a1: open root container, make a M4LABContainer which have the ABContainers of all providers as hierarchy entries.
		M4LABContainer *lpCont = NULL;
		SPropValue sPropObjectType;
		lpCont = new M4LABContainer(m_lABProviders);

		hr = lpCont->QueryInterface(IID_IABContainer, (void**)lppUnk);
		if (hr != hrSuccess) {
			delete lpCont;
			return kc_perrorf("QueryInterface failed", hr);
		}

		sPropObjectType.ulPropTag = PR_OBJECT_TYPE;
		sPropObjectType.Value.ul = MAPI_ABCONT;
		lpCont->SetProps(1, &sPropObjectType, NULL);

		*lpulObjType = MAPI_ABCONT;
	} else if (lpEntryID == nullptr) {
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	} else if (cbEntryID < 4 + sizeof(MAPIUID)) {
		return MAPI_E_UNKNOWN_ENTRYID;
	} else {
		std::list<abEntry>::const_iterator i;

		hr = MAPI_E_UNKNOWN_ENTRYID;
		for (i = m_lABProviders.cbegin(); i != m_lABProviders.cend(); ++i)
			if (memcmp((BYTE*)lpEntryID +4, &i->muid, sizeof(MAPIUID)) == 0)
				break;
		if (i != m_lABProviders.cend())
			hr = i->lpABLogon->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
	}
	return hr;
}

/** 
 * @todo Should use the GetSearchPath items, not just all providers.
 * 
 * @param cbEntryID1 
 * @param lpEntryID1 
 * @param cbEntryID2 
 * @param lpEntryID2 
 * @param ulFlags 
 * @param lpulResult 
 * 
 * @return 
 */
HRESULT M4LAddrBook::CompareEntryIDs(ULONG cbEntryID1,
    const ENTRYID *lpEntryID1, ULONG cbEntryID2, const ENTRYID *lpEntryID2,
    ULONG ulFlags, ULONG *lpulResult)
{
	HRESULT hr = hrSuccess;

	// m_lABProviders[0] probably always is Kopano
	for (const auto &i : m_lABProviders) {
		hr = i.lpABLogon->CompareEntryIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
		if (hr == hrSuccess || hr != MAPI_E_NO_SUPPORT)
			break;
	}
	return hr;
}

HRESULT M4LAddrBook::Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG* lpulConnection) {
	ec_log_err("M4LAddrBook::Advise not implemented");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LAddrBook::Unadvise(ULONG ulConnection) {
	ec_log_err("M4LAddrBook::Unadvise not implemented");
	return MAPI_E_NO_SUPPORT;
}

/**
 * Create a OneOff EntryID.
 *
 * @param[in]	lpszName		Displayname of object
 * @param[in]	lpszAdrType		Addresstype of EntryID. Mostly SMTP or ZARAFA.
 * @param[in]	lpszAddress		Address of EntryID, according to type.
 * @param[in]	ulFlags			Enable MAPI_UNICODE flag if input strings are WCHAR strings. Output will be unicode too.
 * @param[out]	lpcbEntryID		Length of lppEntryID
 * @param[out]	lpplpEntryID	OneOff EntryID for object.
 *
 * @return	HRESULT
 */
HRESULT M4LAddrBook::CreateOneOff(const TCHAR *lpszName,
    const TCHAR *lpszAdrType, const TCHAR *lpszAddress, ULONG ulFlags,
    ULONG *lpcbEntryID, ENTRYID **lppEntryID)
{
	return ECCreateOneOff(lpszName, lpszAdrType, lpszAddress, ulFlags, lpcbEntryID, lppEntryID);
}

HRESULT M4LAddrBook::NewEntry(ULONG_PTR ulUIParam, ULONG ulFlags,
    ULONG cbEIDContainer, LPENTRYID lpEIDContainer, ULONG cbEIDNewEntryTpl,
    LPENTRYID lpEIDNewEntryTpl, ULONG *lpcbEIDNewEntry,
    LPENTRYID *lppEIDNewEntry)
{
	ec_log_err("M4LAddrBook::NewEntry not implemented");
	return MAPI_E_NO_SUPPORT;
}

/**
 * Resolve a list of names in the addressbook.
 * @todo use GetSearchPath, and not to loop over all providers
 *
 * @param[in]	ulUIParam		Unused in Linux.
 * @param[in]	ulFlags			Passed to ResolveNames in Addressbook.
								MAPI_UNICODE, strings in lpAdrList are WCHAR strings.
 * @param[in]	lpszNewEntryTitle	Unused in Linux
 * @param[in,out]	lpAdrList	A list of names to resolve. Resolved users are present in this list (although you can't tell of an error is returned which).
 *
 * @return	HRESULT
 * @retval	MAPI_E_AMBIGUOUS_RECIP	One or more recipients in the list are ambiguous.
 * @retval	MAPI_E_UNRESOLVED		One or more recipients in the list are not resolved.
 */
// should use PR_AB_SEARCH_PATH
HRESULT M4LAddrBook::ResolveName(ULONG_PTR ulUIParam, ULONG ulFlags,
    LPTSTR lpszNewEntryTitle, LPADRLIST lpAdrList)
{
	HRESULT hr = hrSuccess;
	ULONG objType;
	memory_ptr<FlagList> lpFlagList;
	ULONG cNewRow = 0;
	rowset_ptr lpSearchRows;
	bool bContinue = true;

	if (lpAdrList == NULL) {
		ec_log_err("M4LAddrBook::ResolveName() invalid parameters");
		return MAPI_E_INVALID_PARAMETER;
	}

	hr = MAPIAllocateBuffer(CbNewFlagList(lpAdrList->cEntries), &~lpFlagList);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffer failed", hr);
	memset(lpFlagList, 0, CbNewFlagList(lpAdrList->cEntries));
	lpFlagList->cFlags = lpAdrList->cEntries;

	// Resolve local items
	for (unsigned int i = 0; i < lpAdrList->cEntries; ++i) {
		auto lpDisplay  = lpAdrList->aEntries[i].cfind(PR_DISPLAY_NAME_A);
		auto lpDisplayW = lpAdrList->aEntries[i].cfind(PR_DISPLAY_NAME_W);
		auto lpEntryID  = lpAdrList->aEntries[i].cfind(PR_ENTRYID);
		std::wstring strwDisplay, strwType, strwAddress;

		if(lpEntryID != NULL) {
			// Item is already resolved, leave it untouched
			lpFlagList->ulFlag[i] = MAPI_RESOLVED;
			continue;
		}

		if(lpDisplay == NULL && lpDisplayW == NULL) // Can't do much without the PR_DISPLAY_NAME
			continue;

		// Use PT_UNICODE display string if available, otherwise fallback to PT_STRING8
		if(lpDisplayW)
			strwDisplay = lpDisplayW->Value.lpszW;
		else
			strwDisplay = convert_to<std::wstring>(lpDisplay->Value.lpszA);

		// Handle 'DISPLAYNAME [ADDRTYPE:EMAILADDR]' strings
		size_t lbracketpos = strwDisplay.find('[');
		size_t rbracketpos = strwDisplay.find(']');
		size_t colonpos = strwDisplay.find(':');
		if (colonpos == std::string::npos || lbracketpos == std::string::npos || rbracketpos == std::string::npos)
			continue;

		ULONG cbOneEntryID = 0;
		memory_ptr<ENTRYID> lpOneEntryID;
		memory_ptr<SPropValue> lpNewProps, lpNewRow;

		strwType = strwDisplay.substr(lbracketpos+1, colonpos - lbracketpos - 1); // Everything from '[' up to ':'
		strwAddress = strwDisplay.substr(colonpos+1, rbracketpos - colonpos - 1); // Everything after ':' up to ']'
		strwDisplay = strwDisplay.substr(0, lbracketpos); // Everything before '['
		lpFlagList->ulFlag[i] = MAPI_RESOLVED;
		if ((hr = MAPIAllocateBuffer(sizeof(SPropValue) * 4, &~lpNewProps)) != hrSuccess)
			return hr;

		lpNewProps[0].ulPropTag = PR_ENTRYID;
		hr = CreateOneOff((LPTSTR)strwDisplay.c_str(), (LPTSTR)strwType.c_str(), (LPTSTR)strwAddress.c_str(), MAPI_UNICODE, &cbOneEntryID, &~lpOneEntryID);
		if (hr != hrSuccess)
			return kc_perrorf("CreateOneOff failed", hr);
		if ((hr = MAPIAllocateMore(cbOneEntryID, lpNewProps, (void **)&lpNewProps[0].Value.bin.lpb)) != hrSuccess)
			return hr;

		memcpy(lpNewProps[0].Value.bin.lpb, lpOneEntryID, cbOneEntryID);
		lpNewProps[0].Value.bin.cb = cbOneEntryID;
		if (ulFlags & MAPI_UNICODE) {
			lpNewProps[1].ulPropTag = PR_DISPLAY_NAME_W;
			if ((hr = MAPIAllocateMore(sizeof(WCHAR) * (strwDisplay.length() + 1), lpNewProps, (void **)&lpNewProps[1].Value.lpszW)) != hrSuccess)
				return hr;
			wcscpy(lpNewProps[1].Value.lpszW, strwDisplay.c_str());
			lpNewProps[2].ulPropTag = PR_ADDRTYPE_W;
			if ((hr = MAPIAllocateMore(sizeof(WCHAR) * (strwType.length() + 1), lpNewProps, (void **)&lpNewProps[2].Value.lpszW)) != hrSuccess)
				return hr;
			wcscpy(lpNewProps[2].Value.lpszW, strwType.c_str());
			lpNewProps[3].ulPropTag = PR_EMAIL_ADDRESS_W;
			if ((hr = MAPIAllocateMore(sizeof(WCHAR) * (strwAddress.length() + 1), lpNewProps, (void **)&lpNewProps[3].Value.lpszW)) != hrSuccess)
				return hr;
			wcscpy(lpNewProps[3].Value.lpszW, strwAddress.c_str());
		} else {
			std::string conv;

			conv = convert_to<std::string>(strwDisplay);
			lpNewProps[1].ulPropTag = PR_DISPLAY_NAME_A;
			if ((hr = MAPIAllocateMore(conv.length() + 1, lpNewProps, (void **)&lpNewProps[1].Value.lpszA)) != hrSuccess)
				return hr;
			strcpy(lpNewProps[1].Value.lpszA, conv.c_str());
			conv = convert_to<std::string>(strwType);
			lpNewProps[2].ulPropTag = PR_ADDRTYPE_A;
			if ((hr = MAPIAllocateMore(conv.length() + 1, lpNewProps, (void **)&lpNewProps[2].Value.lpszA)) != hrSuccess)
				return hr;
			strcpy(lpNewProps[2].Value.lpszA, conv.c_str());
			conv = convert_to<std::string>(strwAddress);
			lpNewProps[3].ulPropTag = PR_EMAIL_ADDRESS_A;
			if ((hr = MAPIAllocateMore(conv.length() + 1, lpNewProps, (void **)&lpNewProps[3].Value.lpszA)) != hrSuccess)
				return hr;
			strcpy(lpNewProps[3].Value.lpszA, conv.c_str());
		}

		lpOneEntryID.reset();

		// Copy old properties + lpNewProps into row
		hr = Util::HrMergePropertyArrays(lpAdrList->aEntries[i].rgPropVals, lpAdrList->aEntries[i].cValues, lpNewProps, 4, &~lpNewRow, &cNewRow);
		if (hr != hrSuccess)
			return kc_perrorf("Util::HrMergePropertyArrays failed", hr);
		MAPIFreeBuffer(lpAdrList->aEntries[i].rgPropVals);
		lpAdrList->aEntries[i].rgPropVals = lpNewRow.release();
		lpAdrList->aEntries[i].cValues = cNewRow;
	}

	hr = this->GetSearchPath(MAPI_UNICODE, &~lpSearchRows);
	if (hr != hrSuccess)
		return kc_perrorf("GetSearchPath failed", hr);

	for (ULONG c = 0; bContinue && c < lpSearchRows->cRows; ++c) {
		auto lpEntryID = lpSearchRows[c].cfind(PR_ENTRYID);
		if (!lpEntryID)
			continue;

		object_ptr<IABContainer> lpABContainer;
		hr = this->OpenEntry(lpEntryID->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpEntryID->Value.bin.lpb), &IID_IABContainer, 0, &objType, &~lpABContainer);
		if (hr != hrSuccess) {
			kc_perrorf("OpenEntry failed", hr);
			continue;
		}

		hr = lpABContainer->ResolveNames(NULL, ulFlags, lpAdrList, lpFlagList);
		if (FAILED(hr)) {
			kc_perrorf("ResolveNames failed", hr);
			continue;
		}

		hr = hrSuccess;

		bContinue = false;
		// may have warnings .. let's find out
		for (ULONG i = 0; i < lpFlagList->cFlags; ++i) {
			if (lpFlagList->ulFlag[i] == MAPI_AMBIGUOUS)
				return MAPI_E_AMBIGUOUS_RECIP;
			else if (lpFlagList->ulFlag[i] == MAPI_UNRESOLVED)
				bContinue = true;
		}
	}

	
	// check for still unresolved addresses
	for (ULONG i = 0; bContinue && i < lpFlagList->cFlags; ++i)
		if (lpFlagList->ulFlag[i] == MAPI_UNRESOLVED)
			return MAPI_E_NOT_FOUND;
	return hr;
}

HRESULT M4LAddrBook::Address(ULONG_PTR *lpulUIParam, LPADRPARM lpAdrParms,
    LPADRLIST *lppAdrList)
{
	ec_log_err("not implemented: M4LAddrBook::Address");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LAddrBook::Details(ULONG* lpulUIParam, LPFNDISMISS lpfnDismiss, LPVOID lpvDismissContext, ULONG cbEntryID, LPENTRYID lpEntryID, LPFNBUTTON lpfButtonCallback, LPVOID lpvButtonContext, LPTSTR lpszButtonText, ULONG ulFlags) {
	ec_log_err("not implemented: M4LAddrBook::Details");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LAddrBook::RecipOptions(ULONG_PTR ulUIParam, ULONG ulFlags,
    LPADRENTRY lpRecip)
{
	ec_log_err("not implemented: M4LAddrBook::RecipOptions");
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LAddrBook::QueryDefaultRecipOpt(const TCHAR *addrtype, ULONG flags,
    ULONG *nvals, SPropValue **opts)
{
	ec_log_err("not implemented: M4LAddrBook::QueryDefaultRecipOpt");
	return MAPI_E_NO_SUPPORT;
}

// Get Personal AddressBook
HRESULT M4LAddrBook::GetPAB(ULONG* lpcbEntryID, LPENTRYID* lppEntryID) {
	ec_log_err("not implemented: M4LAddrBook::GetPAB");
	return MAPI_E_NO_SUPPORT;
}

// Set Personal AddressBook
HRESULT M4LAddrBook::SetPAB(ULONG cbEntryID, LPENTRYID lpEntryID) {
	ec_log_err("not implemented: M4LAddrBook::SetPAB");
	return MAPI_E_NO_SUPPORT;
}

/**
 * Returns the EntryID of the Global Address Book container.
 *
 * @param[out]	lpcbEntryID	Length of lppEntryID.
 * @param[out]	lppEntryID	Unique identifier of the GAB container.
 *
 * @return	HRESULT
 * @retval	MAPI_E_INVALID_PARAMETER	Invalid input
 * @retval	MAPI_E_NOT_FOUND			Addressbook	is not available.
 */
HRESULT M4LAddrBook::GetDefaultDir(ULONG* lpcbEntryID, LPENTRYID* lppEntryID) {
	HRESULT hr = MAPI_E_INVALID_PARAMETER;
	ULONG objType;
	object_ptr<IABContainer> lpABContainer;
	memory_ptr<SPropValue> propEntryID;
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpRowSet;
	ULONG cbEntryID;
	LPENTRYID lpEntryID = NULL;
	const SPropValue *lpProp = NULL;

	if (lpcbEntryID == NULL || lppEntryID == NULL) {
		ec_log_err("M4LAddrBook::GetDefaultDir(): invalid parameters");
		return hr;
	} else if (m_lABProviders.size() == 0) {
		ec_log_err("M4LAddrBook::GetDefaultDir(): no ABs to search");
		return hr;
	}

	// m_lABProviders[0] probably always is Kopano
	for (const auto &i : m_lABProviders) {
		// find a working open root container
		hr = i.lpABLogon->OpenEntry(0, NULL, &IID_IABContainer, 0,
		     &objType, &~lpABContainer);
		if (hr == hrSuccess)
			break;
	}
	if (hr != hrSuccess)
		return kc_perrorf("OpenEntry failed", hr);

	// more steps with gethierarchy() -> get entryid -> OpenEntry() ?
	hr = lpABContainer->GetHierarchyTable(0, &~lpTable);
	if (hr != hrSuccess) {
		kc_perrorf("GetHierarchyTable failed", hr);
		goto no_hierarchy;
	}
	hr = lpTable->QueryRows(1, 0, &~lpRowSet); // can only return 1 row, as there is only 1
	if (hr != hrSuccess) {
		kc_perrorf("QueryRows failed", hr);
		goto no_hierarchy;
	}

	// get entry id from table, use it.
	lpProp = lpRowSet[0].cfind(PR_ENTRYID);
no_hierarchy:

	if (!lpProp) {
		// fallback to getprops on lpABContainer, actually a level too high, but it should work too.
		hr = HrGetOneProp(lpABContainer, 0, &~propEntryID);
		if (hr != hrSuccess)
			return kc_perrorf("HrGetOneProp failed", hr);
		lpProp = propEntryID;
	}

	// make copy and return
	cbEntryID = lpProp->Value.bin.cb;
	if ((hr = MAPIAllocateBuffer(cbEntryID, (void**)&lpEntryID)) != hrSuccess)
		return hr;
	memcpy(lpEntryID, lpProp->Value.bin.lpb, cbEntryID);

	*lpcbEntryID = cbEntryID;
	*lppEntryID = lpEntryID;
	return hrSuccess;
}

HRESULT M4LAddrBook::SetDefaultDir(ULONG eid_size, const ENTRYID *)
{
	ec_log_err("not implemented M4LAddrBook::SetDefaultDir");
	return MAPI_E_NO_SUPPORT;
}

/** 
 * Returns all the hierarchy entries of the AB Root Container, see ResolveName()
 * Should return what's set with SetSearchPath().
 * Also should not return AB_SUBFOLDERS from the root container, but the subfolders.
 * 
 * @param[in] ulFlags MAPI_UNICODE
 * @param[out] lppSearchPath IABContainers EntryIDs of all known providers
 * 
 * @return MAPI Error code
 */
HRESULT M4LAddrBook::GetSearchPath(ULONG ulFlags, LPSRowSet* lppSearchPath) {
	HRESULT hr = hrSuccess;
	rowset_ptr lpSearchPath;

	if (!m_lpSavedSearchPath) {
		hr = this->getDefaultSearchPath(ulFlags, &m_lpSavedSearchPath);
		if (hr != hrSuccess)
			return kc_perrorf("getDefaultSearchPath failed", hr);
	}

	hr = MAPIAllocateBuffer(CbNewSRowSet(m_lpSavedSearchPath->cRows), &~lpSearchPath);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffer failed", hr);
	hr = Util::HrCopySRowSet(lpSearchPath, m_lpSavedSearchPath, NULL);
	if (hr != hrSuccess)
		return kc_perrorf("Util::HrCopySRowSet failed", hr);
	*lppSearchPath = lpSearchPath.release();
	return hrSuccess;
}

HRESULT M4LAddrBook::SetSearchPath(ULONG ulFlags, LPSRowSet lpSearchPath) {
	HRESULT hr = hrSuccess;

	if (m_lpSavedSearchPath) {
		FreeProws(m_lpSavedSearchPath);
		m_lpSavedSearchPath = NULL;
	}

	hr = MAPIAllocateBuffer(CbNewSRowSet(lpSearchPath->cRows), (void**)&m_lpSavedSearchPath);
	if (hr != hrSuccess) {
		kc_perrorf("MAPIAllocateBuffer failed", hr);
		goto exit;
	}

	hr = Util::HrCopySRowSet(m_lpSavedSearchPath, lpSearchPath, NULL);
	if (hr != hrSuccess) {
		kc_perrorf("Util::HrCopySRowSet failed", hr);
		goto exit;
	}

exit:
	if (hr != hrSuccess && m_lpSavedSearchPath) {
		FreeProws(m_lpSavedSearchPath);
		m_lpSavedSearchPath = NULL;
	}
	return hr;
}

/**
 * Returns the EntryID of the Global Address Book container.
 *
 * @param[in]		ulFlags			Unused. (MAPI_UNICODE new?)
 * @param[in]		lpPropTagArray	List of properties to add in lpRecipList per recipient.
 * @param[in,out]	lpRecipList		List will be edited with requested properties from addressbook.
 *
 * @return	HRESULT
 * @retval	MAPI_E_INVALID_PARAMETER	Invalid input
 * @retval	MAPI_E_NOT_FOUND			Addressbook	is not available.
 */
HRESULT M4LAddrBook::PrepareRecips(ULONG ulFlags,
    const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList)
{
	HRESULT hr = hrSuccess;
	ULONG cValues = 0;
	ULONG ulType = 0;

	//FIXME: lpPropTagArray can be NULL, this means that doesn't have extra properties to update only the 
	//       properties in the lpRecipList array.
	//       This function should merge properties which are in lpRecipList and lpPropTagArray, the requested
	//       properties are ordered first, followed by any additional properties that were already present for the entry.

	if (lpRecipList == NULL) {
		ec_log_err("M4LAddrBook::PrepareRecips(): invalid parameters");
		return MAPI_E_INVALID_PARAMETER;
	}

	for (unsigned int i = 0; i < lpRecipList->cEntries; ++i) {
		object_ptr<IMailUser> lpMailUser;
		memory_ptr<SPropValue> lpProps;
		auto lpEntryId = lpRecipList->aEntries[i].cfind(PR_ENTRYID);
		if(lpEntryId == NULL)
			continue;
		hr = OpenEntry(lpEntryId->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpEntryId->Value.bin.lpb), &IID_IMailUser, 0, &ulType, &~lpMailUser);
		if (hr != hrSuccess)
			return kc_perrorf("OpenEntry failed", hr);
		hr = lpMailUser->GetProps(lpPropTagArray, 0, &cValues, &~lpProps);
		if (FAILED(hr))
			return kc_perrorf("GetProps failed", hr);
		hr = hrSuccess;
		MAPIFreeBuffer(lpRecipList->aEntries[i].rgPropVals);

		if ((hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpPropTagArray->cValues, (void **)&lpRecipList->aEntries[i].rgPropVals)) != hrSuccess)
			return hr;
		memset(lpRecipList->aEntries[i].rgPropVals, 0, sizeof(SPropValue) * lpPropTagArray->cValues);
		lpRecipList->aEntries[i].cValues = lpPropTagArray->cValues;

		for (unsigned int j = 0; j < lpPropTagArray->cValues; ++j) {
			auto lpProp = PCpropFindProp(lpProps, cValues,
				lpPropTagArray->aulPropTag[j]);
			if(lpProp == NULL) {
				lpRecipList->aEntries[i].rgPropVals[j].ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(lpPropTagArray->aulPropTag[j]));
				lpRecipList->aEntries[i].rgPropVals[j].Value.err = MAPI_E_NOT_FOUND;
				continue;
			}
			hr = Util::HrCopyProperty(&lpRecipList->aEntries[i].rgPropVals[j], lpProp, lpRecipList->aEntries[i].rgPropVals);
			if (hr != hrSuccess)
				return kc_perrorf("Util::HrCopyProperty failed", hr);
		}
	}
	return hr;
}

HRESULT M4LAddrBook::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IAddrBook) {
		AddRef();
		*lpvoid = (IAddrBook *)this;
	} else if (refiid == IID_IMAPIProp) {
		AddRef();
		*lpvoid = static_cast<IMAPIProp *>(this);
	} else if (refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = static_cast<IUnknown *>(this);
	} else
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	return hrSuccess;
}

/**
 * Allocate a new buffer. Must be freed with MAPIFreeBuffer.
 *
 * @param[in]	cbSize		Size of buffer to allocate
 * @param[out]	lppBuffer	Allocated buffer.
 * @return		SCODE
 * @retval		MAPI_E_INVALID_PARAMETER	Invalid input parameters
 * @retval		MAPI_E_NOT_ENOUGH_MEMORY
 * @fixme		Why is the return value not the mapi error MAPI_E_NOT_ENOUGH_MEMORY?
 *			I don't think Mapi programs like an error 0x80040001.
 */
#ifdef DUMB_MAPIALLOC
/*
 * Coverity SCAN does not understand the pointer arithmetic strategy in
 * MAPIAllocateBuffer, so provide something different for their scanner.
 * (The mutex makes this really slow in practice.)
 */
static std::unordered_map<void *, std::vector<void *> > mapi_allocmap;
static std::mutex mapi_allocmap_lock;

SCODE MAPIAllocateBuffer(ULONG size, void **buf)
{
	if (buf == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	*buf = malloc(size);
	if (*buf == nullptr)
		return MAKE_MAPI_E(1);
	return hrSuccess;
}

SCODE MAPIAllocateMore(ULONG size, void *obj, void **buf)
{
	if (obj == nullptr)
		return MAPIAllocateBuffer(size, buf);
	*buf = malloc(size);
	if (*buf == nullptr)
		return MAKE_MAPI_E(1);
	scoped_lock lock(mapi_allocmap_lock);
	mapi_allocmap[obj].emplace_back(*buf);
	return hrSuccess;
}

ULONG MAPIFreeBuffer(void *buf)
{
	if (buf == nullptr)
		return S_OK;
	scoped_lock lk(mapi_allocmap_lock);
	for (auto i : mapi_allocmap[buf])
		free(i);
	mapi_allocmap.erase(buf);
	return S_OK;
}

#else /* DUMB_MAPIALLOC */

SCODE MAPIAllocateBuffer(ULONG cbSize, LPVOID *lppBuffer)
{
	if (lppBuffer == NULL)
		return MAPI_E_INVALID_PARAMETER;
	cbSize += sizeof(struct mapibuf_head);
	auto bfr = static_cast<struct mapibuf_head *>(malloc(cbSize));
	if (bfr == nullptr)
		return MAKE_MAPI_E(1);
	try {
		new(bfr) struct mapibuf_head; /* init mutex */
		bfr->child = nullptr;
	} catch (const std::exception &e) {
		fprintf(stderr, "MAPIAllocateBuffer: %s\n", e.what());
		free(bfr);
		return MAKE_MAPI_E(1);
	}
	*lppBuffer = bfr->data;
	return hrSuccess;
}

/**
 * Allocate a new buffer and associate it with lpObject.
 *
 * @param[in]	cbSize		Size of buffer to allocate
 * @param[in]	lpObject	Pointer from MAPIAllocate to associate this allocation with.
 * @param[out]	lppBuffer	Allocated buffer.
 * @return		SCODE
 * @retval		MAPI_E_INVALID_PARAMETER	Invalid input parameters
 * @retval		MAPI_E_NOT_ENOUGH_MEMORY
 */
SCODE MAPIAllocateMore(ULONG cbSize, LPVOID lpObject, LPVOID *lppBuffer)
{
	if (lppBuffer == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (!lpObject)
		return MAPIAllocateBuffer(cbSize, lppBuffer);
	cbSize += sizeof(struct mapiext_head);
	auto bfr = static_cast<struct mapiext_head *>(malloc(cbSize));
	if (bfr == nullptr) {
		ec_log_crit("MAPIAllocateMore(): %s", strerror(errno));
		return MAKE_MAPI_E(1);
	}

	auto head = container_of(lpObject, struct mapibuf_head, data);
#if _MAPI_MEM_MORE_DEBUG
	if (head->ident != MAPIBUF_BASE)
		assert("AllocateMore on something that was not allocated with MAPIAllocateBuffer!\n" == nullptr);
#endif
	scoped_lock lock(head->mtx);
	bfr->child = head->child;
	head->child = bfr;
	*lppBuffer = bfr->data;
#if _MAPI_MEM_DEBUG
	fprintf(stderr, "Extra buffer: %p on %p\n", *lppBuffer, lpObject);
#endif
	return hrSuccess;
}

/**
 * Free a buffer allocated with MAPIAllocate. All buffers associated
 * with lpBuffer will be freed too.
 *
 * @param[in]	lpBuffer	Pointer to be freed.
 * @return		ULONG		Always 0 in Linux.
 */
ULONG MAPIFreeBuffer(LPVOID lpBuffer)
{
	/* Well it could happen, especially according to the MSDN.. */
	if (!lpBuffer)
		return S_OK;
#if _MAPI_MEM_DEBUG
	fprintf(stderr, "Freeing: %p\n", lpBuffer);
#endif
	auto head = container_of(lpBuffer, struct mapibuf_head, data);
#if _MAPI_MEM_MORE_DEBUG
	assert(head->ident == MAPIBUF_BASE);
#endif
	auto p = head->child;
	while (p != nullptr) {
		auto q = p->child;
#if _MAPI_MEM_DEBUG
		fprintf(stderr, "  Freeing: %p\n", i);
#endif
		free(p);
		p = q;
	}
	head->~mapibuf_head();
	free(head);
	return 0;
 }

#endif /* DUMB_MAPIALLOC */

// ---
// Entry
// ---

IProfAdmin *localProfileAdmin = NULL;

/**
 * Returns a pointer to the IProfAdmin interface. MAPIInitialize must been called previously.
 *
 * @param[in]	ulFlags			Unused in Linux.
 * @param[out]	lppProfAdmin	Return value of function
 * @return		HRESULT
 * @retval		MAPI_E_CALL_FAILED	MAPIInitialize not called previously.
 * @retval		MAPI_E_INVALID_PARAMETER	No lppProfAdmin return parameter given.
 */
HRESULT MAPIAdminProfiles(ULONG ulFlags, IProfAdmin **lppProfAdmin)
{
	if (!localProfileAdmin) {
		ec_log_err("MAPIAdminProfiles(): localProfileAdmin not set");
		return MAPI_E_CALL_FAILED;
	}

	if (!lppProfAdmin) {
		ec_log_err("MAPIAdminProfiles(): invalid parameter");
		return MAPI_E_INVALID_PARAMETER;
	}
	return localProfileAdmin->QueryInterface(IID_IProfAdmin,
	       reinterpret_cast<void **>(lppProfAdmin));
}

/**
 * Login on a previously created profile.
 *
 * @param[in]	ulUIParam	Unused in Linux.
 * @param[in]	lpszProfileName	us-ascii profilename. No limit to this string in Linux (Win32 is 64 characters)
 * @param[in]	lpszPassword	us-ascii password of the profile. Not the password of the user defined in the profile. Mostly unused.
 * @param[in]	ulFlags	the following flags are used in Linux:
 *				MAPI_UNICODE : treat lpszProfileName and lpszPassword (unused) as wchar_t strings
 * @param[out]	lppSession	returns the MAPISession object on hrSuccess
 * @return		HRESULT
 * @retval		MAPI_E_CALL_FAILED	MAPIInitialize not previously called
 * @retval		MAPI_E_INVALID_PARAMETER	No profilename or lppSession return pointer given.
 * @retval		MAPI_E_NOT_ENOUGH_MEMORY
 */
HRESULT MAPILogonEx(ULONG_PTR ulUIParam, const TCHAR *lpszProfileName,
    const TCHAR *lpszPassword, ULONG ulFlags, IMAPISession **lppSession)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMsgServiceAdmin> sa;
	std::string strProfname;

	if (!lpszProfileName || !lppSession) {
		ec_log_err("MAPILogonEx(): invalid parameter");
		return MAPI_E_INVALID_PARAMETER;
	}

	if (!localProfileAdmin) {
		ec_log_err("MAPILogonEx(): localProfileAdmin not set");
		return MAPI_E_CALL_FAILED;
	}

	if (ulFlags & MAPI_UNICODE) {
		// since a profilename can only be us-ascii, convert
		try {
			strProfname = convert_to<std::string>(reinterpret_cast<const wchar_t *>(lpszProfileName));
		} catch (const illegal_sequence_exception &) {
			return MAPI_E_INVALID_PARAMETER;
		} catch (const unknown_charset_exception &) {
			return MAPI_E_CALL_FAILED;
		}
	} else {
		strProfname = (char*)lpszProfileName;
	}

	hr = localProfileAdmin->AdminServices((LPTSTR)strProfname.c_str(), lpszPassword, ulUIParam, ulFlags & ~MAPI_UNICODE, &~sa);
	if (hr != hrSuccess)
		return kc_perrorf("AdminServices failed", hr);
	hr = alloc_wrap<M4LMAPISession>(lpszProfileName, static_cast<M4LMsgServiceAdmin *>(sa.get()))
	     .as(IID_IMAPISession, lppSession);
	if (hr != hrSuccess)
		kc_perrorf("M4LMAPISession failed", hr);
	return hr;
}

/*
 * Small (non-threadsafe protection against multiple MAPIInitialize/MAPIUnitialize calls.
 * Some (bad behaving) MAPI clients (i.e. CalHelper.exe) might call MAPIInitialize/MAPIUnitialize
 * multiple times, obviously this is very bad behavior, but it shouldn't hurt to at least
 * builtin some protection against this.
 * _MAPIInitializeCount simply counts the number of times MAPIInitialize is called, and will
 * not cleanup anything in MAPIUnitialize until the counter is back to 0.
 */
static int _MAPIInitializeCount = 0;
static std::mutex g_MAPILock;

/**
 * MAPIInitialize is the first function called.
 *
 * In Linux, this will already open the libkcclient.so file, and will retrieve
 * the entry point function pointers. If these are not present, the function will fail.
 *
 * @param[in] lpMapiInit
 *			Optional pointer to MAPIINIT struct. Unused in Linux.
 * @return	HRESULT
 * @retval	hrSuccess
 * @retval	MAPI_E_CALL_FAILED	Unable to use libkcclient.so
 * @retval	MAPI_E_NOT_ENOUGH_MEMORY Memory allocation failed
 */
HRESULT MAPIInitialize(LPVOID lpMapiInit)
{
	scoped_lock l_mapi(g_MAPILock);

	if (_MAPIInitializeCount++) {
		assert(localProfileAdmin);
		localProfileAdmin->AddRef();
		return hrSuccess;
	}

	// Loads the mapisvc.inf, and finds all providers and entry point functions
	m4l_lpMAPISVC = new MAPISVC();
	auto hr = m4l_lpMAPISVC->Init();
	if (hr != hrSuccess) {
		ec_log_crit("MAPIInitialize(): MAPISVC::Init fail %x: %s", hr, GetMAPIErrorMessage(hr));
		return hr;
	}

	if (!localProfileAdmin) {
		localProfileAdmin = new M4LProfAdmin;
		if (localProfileAdmin == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		localProfileAdmin->AddRef();
	}
	return hrSuccess;
}

/**
 * Last function of your MAPI program.  
 *
 * In Linux, this will unload the libkcclient.so library. Any
 * object from that library you still * have will be unusable,
 * and will make your program crash when used.
 */
void MAPIUninitialize(void)
{
	scoped_lock l_mapi(g_MAPILock);

	if (_MAPIInitializeCount == 0)
		abort();

	/* MAPIInitialize always AddRefs localProfileAdmin */
	if (localProfileAdmin)
		localProfileAdmin->Release();

	/* Only clean everything up when this is the last MAPIUnitialize call. */
	if ((--_MAPIInitializeCount) == 0) {
		delete m4l_lpMAPISVC;

		localProfileAdmin = NULL;
	}
}

namespace KC {

static HRESULT kc_sesave_propvals(const SPropValue *pv, unsigned int n, std::string &serout)
{
#define RC(p) reinterpret_cast<const char *>(p)
	serout.append(RC(&n), sizeof(n));
	for (unsigned int i = 0; i < n; ++i) {
		const SPropValue &p = pv[i];
		uint32_t z;
		serout.append(RC(&p.ulPropTag), sizeof(p.ulPropTag));
		switch (PROP_TYPE(p.ulPropTag)) {
		case PT_BOOLEAN:
			serout.append(RC(&p.Value.b), sizeof(p.Value.b));
			break;
		case PT_LONG:
			serout.append(RC(&p.Value.ul), sizeof(p.Value.ul));
			break;
		case PT_STRING8:
			z = strlen(p.Value.lpszA);
			serout.append(RC(&z), sizeof(z));
			serout.append(p.Value.lpszA, z);
			break;
		case PT_UNICODE:
			z = wcslen(p.Value.lpszW) * sizeof(wchar_t);
			serout.append(RC(&z), sizeof(z));
			serout.append(RC(p.Value.lpszW), z);
			break;
		case PT_BINARY:
			serout.append(RC(&p.Value.bin.cb), sizeof(p.Value.bin.cb));
			serout.append(RC(p.Value.bin.lpb), p.Value.bin.cb);
			break;
		case PT_ERROR:
			break;
		default:
			/* Some datatype not anticipated yet */
			return MAPI_E_NO_SUPPORT;
		}
	}
	return hrSuccess;
#undef S
}

static HRESULT kc_sesave_profsect(IProfSect *sect, std::string &serout)
{
	memory_ptr<SPropValue> props;
	ULONG nprops = 0;
	auto ret = sect->GetProps(nullptr, 0, &nprops, &~props);
	if (ret != hrSuccess)
		return ret;
	return kc_sesave_propvals(props, nprops, serout);
}

/**
 * Serialize the profile configuration of a session into a blob for
 * later restoration. The blob is specific to the host that generated
 * it and can only be reused there.
 */
HRESULT kc_session_save(IMAPISession *ses, std::string &serout)
{
	serout.reserve(1536);
	object_ptr<IMsgServiceAdmin> svcadm;
	auto ret = ses->AdminServices(0, &~svcadm);
	if (ret != hrSuccess)
		return ret;
	object_ptr<IMAPITable> table;
	ret = svcadm->GetMsgServiceTable(0, &~table);
	if (ret != hrSuccess)
		return ret;
	rowset_ptr rows;
	object_ptr<IProfSect> sect;
	while (true) {
		ret = table->QueryRows(1, 0, &~rows);
		if (ret != hrSuccess || rows->cRows == 0)
			break;
		serout += "S"; /* service start marker */
		ret = kc_sesave_propvals(rows[0].lpProps, rows[0].cValues, serout);
		if (ret != hrSuccess)
			return ret;
		ret = svcadm->OpenProfileSection(reinterpret_cast<const MAPIUID *>(&pbGlobalProfileSectionGuid), nullptr, 0, &~sect);
		if (ret != hrSuccess)
			return ret;
		ret = kc_sesave_profsect(sect, serout);
		if (ret != hrSuccess)
			return ret;
	}
	ret = svcadm->GetProviderTable(0, &~table);
	if (ret != hrSuccess)
		return ret;
	while (true) {
		ret = table->QueryRows(1, 0, &~rows);
		if (ret != hrSuccess)
			return ret;
		if (rows->cRows == 0)
			break;
		auto provuid_prop = rows[0].cfind(PR_PROVIDER_UID);
		if (provuid_prop == nullptr)
			continue;
		serout += "P"; /* provider start marker */
		ret = kc_sesave_propvals(rows[0].lpProps, rows[0].cValues, serout);
		if (ret != hrSuccess)
			return ret;
		ret = svcadm->OpenProfileSection(reinterpret_cast<const MAPIUID *>(provuid_prop->Value.bin.lpb), nullptr, 0, &~sect);
		if (ret != hrSuccess)
			return ret;
		ret = kc_sesave_profsect(sect, serout);
		if (ret != hrSuccess)
			return ret;
	}
	return hrSuccess;
}
#undef RC

HRESULT SessionRestorer::restore_propvals(SPropValue **prop_ret, ULONG &nprops)
{
	if (m_left < sizeof(nprops))
		return MAPI_E_CORRUPT_DATA;
	memcpy(&nprops, &*m_input, sizeof(nprops));
	m_input += sizeof(nprops);
	m_left -= sizeof(nprops);
	auto ret = MAPIAllocateBuffer(sizeof(**prop_ret) * nprops, reinterpret_cast<void **>(prop_ret));
	if (ret != hrSuccess)
		return ret;
	for (unsigned int i = 0; i < nprops; ++i) {
		auto pv = (*prop_ret) + i;
		uint32_t z;
		if (m_left < sizeof(pv->ulPropTag))
			return MAPI_E_CORRUPT_DATA;
		memcpy(&pv->ulPropTag, &*m_input, sizeof(pv->ulPropTag));
		m_input += sizeof(pv->ulPropTag);
		m_left -= sizeof(pv->ulPropTag);
		switch (PROP_TYPE(pv->ulPropTag)) {
		case PT_BOOLEAN:
			if (m_left < sizeof(pv->Value.b))
				return MAPI_E_CORRUPT_DATA;
			memcpy(&pv->Value.b, &*m_input, sizeof(pv->Value.b));
			m_input += sizeof(pv->Value.b);
			m_left -= sizeof(pv->Value.b);
			break;
		case PT_LONG:
			if (m_left < sizeof(pv->Value.ul))
				return MAPI_E_CORRUPT_DATA;
			memcpy(&pv->Value.ul, &*m_input, sizeof(pv->Value.ul));
			m_input += sizeof(pv->Value.ul);
			m_left -= sizeof(pv->Value.ul);
			break;
		case PT_STRING8: /* little overallocate, nevermind */
		case PT_UNICODE:
			if (m_left < sizeof(z))
				return MAPI_E_CORRUPT_DATA;
			memcpy(&z, &*m_input, sizeof(z));
			m_input += sizeof(z);
			m_left -= sizeof(z);
			if (m_left < z)
				return MAPI_E_CORRUPT_DATA;
			ret = MAPIAllocateMore(z + sizeof(wchar_t), *prop_ret, reinterpret_cast<void **>(&pv->Value.lpszA));
			if (ret != hrSuccess)
				return ret;
			memcpy(pv->Value.lpszA, &*m_input, z);
			memcpy(&pv->Value.lpszA[z], L"", sizeof(wchar_t));
			m_input += z;
			m_left -= z;
			break;
		case PT_BINARY:
			if (m_left < sizeof(pv->Value.bin.cb))
				return MAPI_E_CORRUPT_DATA;
			memcpy(&pv->Value.bin.cb, &*m_input, sizeof(pv->Value.bin.cb));
			m_input += sizeof(pv->Value.bin.cb);
			m_left -= sizeof(pv->Value.bin.cb);
			if (m_left < pv->Value.bin.cb)
				return MAPI_E_CORRUPT_DATA;
			ret = MAPIAllocateMore(pv->Value.bin.cb, *prop_ret, reinterpret_cast<void **>(&pv->Value.bin.lpb));
			if (ret != hrSuccess)
				return ret;
			memcpy(pv->Value.bin.lpb, &*m_input, pv->Value.bin.cb);
			m_input += pv->Value.bin.cb;
			m_left -= pv->Value.bin.cb;
			break;
		default:
			--i;
			--nprops;
			break;
		}
	}
	return hrSuccess;
}

HRESULT SessionRestorer::restore_services(IProfAdmin *profadm)
{
	memory_ptr<SPropValue> tbl_props, ps_props;
	ULONG tbl_nprops = 0, ps_nprops = 0;

	while (m_left > 0 && *m_input == 'S') {
		++m_input;
		--m_left;

		auto ret = restore_propvals(&~tbl_props, tbl_nprops);
		if (ret != hrSuccess)
			return ret;
		ret = restore_propvals(&~ps_props, ps_nprops);
		if (ret != hrSuccess)
			return ret;
		auto profname_prop = PCpropFindProp(ps_props, ps_nprops, PR_PROFILE_NAME_A);
		auto dispname_prop = PCpropFindProp(tbl_props, tbl_nprops, PR_DISPLAY_NAME_A);
		auto svcname_prop = PCpropFindProp(tbl_props, tbl_nprops, PR_SERVICE_NAME_A);
		auto svcuid_prop = PCpropFindProp(tbl_props, tbl_nprops, PR_SERVICE_UID);
		if (profname_prop == nullptr || dispname_prop == nullptr ||
		    svcname_prop == nullptr || svcuid_prop == nullptr ||
		    svcuid_prop->Value.bin.cb != sizeof(MAPIUID))
			return MAPI_E_CORRUPT_DATA;

		if (m_svcadm == nullptr) {
			auto profname = profname_prop->Value.lpszA;
			ret = profadm->CreateProfile(reinterpret_cast<const TCHAR *>(profname), reinterpret_cast<const TCHAR *>(""), 0, 0);
			if (ret != hrSuccess)
				return ret;
			object_ptr<IMsgServiceAdmin> svcadm1;
			ret = profadm->AdminServices(reinterpret_cast<const TCHAR *>(profname), reinterpret_cast<const TCHAR *>(""), 0, 0, &~svcadm1);
			if (ret != hrSuccess)
				return ret;
			m_svcadm.reset(static_cast<M4LMsgServiceAdmin *>(svcadm1.get()));
			if (m_svcadm == nullptr)
				return MAPI_E_CALL_FAILED;
			m_profname = profname;
		}
		if (m_svcadm == nullptr)
			return MAPI_E_CALL_FAILED;
		/*
		 * Since this is a new profile, there are no services yet
		 * and we need not check for their existence like in CreateMsgServiceEx.
		 */
		std::unique_ptr<serviceEntry> entry(new(std::nothrow) serviceEntry);
		if (entry == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		auto svcname = svcname_prop->Value.lpszA;
		ret = m4l_lpMAPISVC->GetService(reinterpret_cast<const TCHAR *>(svcname), 0, &entry->service);
		if (ret != hrSuccess)
			return MAPI_E_CALL_FAILED;
		entry->provideradmin.reset(new(std::nothrow) M4LProviderAdmin(m_svcadm, m_profname.c_str()));
		if (entry->provideradmin == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		entry->servicename = svcname;
		entry->displayname = dispname_prop->Value.lpszA;
		entry->bInitialize = true;
		if (svcuid_prop->Value.bin.cb != sizeof(entry->muid))
			return MAPI_E_CORRUPT_DATA;
		memcpy(&entry->muid, svcuid_prop->Value.bin.lpb, sizeof(entry->muid));
		ulock_rec svclk(m_svcadm->m_mutexserviceadmin);
		m_svcadm->services.emplace_back(std::move(entry));
		svclk.unlock();

		object_ptr<IProfSect> psect;
		ret = m_svcadm->OpenProfileSection(reinterpret_cast<const MAPIUID *>(&pbGlobalProfileSectionGuid), nullptr, 0, &~psect);
		if (ret != hrSuccess)
			return ret;
		ret = psect->SetProps(ps_nprops, ps_props, nullptr);
		if (ret != hrSuccess)
			return ret;
	}
	return hrSuccess;
}

HRESULT SessionRestorer::restore_providers()
{
	memory_ptr<SPropValue> tbl_props, ps_props;
	ULONG tbl_nprops = 0, ps_nprops = 0;

	while (m_left > 0 && *m_input == 'P') {
		++m_input;
		--m_left;

		auto ret = restore_propvals(&~tbl_props, tbl_nprops);
		if (ret != hrSuccess)
			return ret;
		ret = restore_propvals(&~ps_props, ps_nprops);
		if (ret != hrSuccess)
			return ret;
		auto svcuid_prop = PCpropFindProp(tbl_props, tbl_nprops, PR_SERVICE_UID);
		auto provuid_prop = PCpropFindProp(tbl_props, tbl_nprops, PR_PROVIDER_UID);
		if (svcuid_prop == nullptr || provuid_prop == nullptr ||
		    svcuid_prop->Value.bin.cb != sizeof(MAPIUID) ||
		    provuid_prop->Value.bin.cb != sizeof(MAPIUID))
			return MAPI_E_CORRUPT_DATA;
		auto svc = m_svcadm->findServiceAdmin(reinterpret_cast<const MAPIUID *>(svcuid_prop->Value.bin.lpb));
		if (svc == nullptr)
			return MAPI_E_CORRUPT_DATA;

		std::unique_ptr<providerEntry> entry(new(std::nothrow) providerEntry);
		if (entry == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		memcpy(&entry->uid, provuid_prop->Value.bin.lpb, sizeof(entry->uid));
		entry->servicename = svc->servicename;
		entry->profilesection.reset(new(std::nothrow) M4LProfSect);
		if (entry->profilesection == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		ret = entry->profilesection->SetProps(ps_nprops, ps_props, nullptr);
		if (ret != hrSuccess)
			return ret;
		scoped_rlock svclk(m_svcadm->m_mutexserviceadmin);
		m_svcadm->providers.emplace_back(std::move(entry));
	}
	return hrSuccess;
}

HRESULT SessionRestorer::restore_profile(const std::string &serin,
    IMAPISession **sesp)
{
	object_ptr<IProfAdmin> profadm;
	auto ret = MAPIAdminProfiles(0, &~profadm);
	if (ret != hrSuccess)
		return ret;
	m_left = serin.size();
	m_input = serin.cbegin();
	ret = restore_services(profadm);
	if (ret != hrSuccess)
		return ret;
	ret = restore_providers();
	if (ret != hrSuccess)
		return ret;
	ret = MAPILogonEx(0, reinterpret_cast<const TCHAR *>(m_profname.c_str()),
	      reinterpret_cast<const TCHAR *>(""),
	      MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL, sesp);
	profadm->DeleteProfile(reinterpret_cast<const TCHAR *>(m_profname.c_str()), 0);
	return ret;
}

/**
 * Restore a MAPI profile (and a IMAPISession object using it) from a blob
 * previously generated with kc_session_save. If this function returns an error
 * code, the "normal" way of logging on needs to be carried out instead.
 *
 * Due to the implementation of M4L and libkcclient, a TCP connection may not
 * immediately be recreated here, but be deferred until the next RPC.
 */
HRESULT kc_session_restore(const std::string &a, IMAPISession **s)
{
	return SessionRestorer().restore_profile(a, s);
}

SCODE KAllocCopy(const void *src, size_t z, void **dst, void *base)
{
	auto ret = MAPIAllocateMore(z, base, dst);
	if (ret != hrSuccess)
		return ret;
	if (src != nullptr)
		memcpy(*dst, src, z);
	return hrSuccess;
}

} /* namespace */
