/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <algorithm>
#include <list>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <kopano/memory.hpp>
#include "m4l.mapidefs.h"
#include "m4l.mapix.h"
#include <kopano/Util.h>
#include <kopano/ECMemTable.h>
#include <kopano/ECUnknown.h>
#include <kopano/charset/convert.h>
#include <kopano/ustringutil.h>
#include <mapi.h>
#include <mapicode.h>
#include <mapiguid.h>
#include <mapix.h>
#include <mapiutil.h>
#include <kopano/ECConfig.h>
#include <kopano/CommonUtil.h>
#include <set>

using namespace KC;

// ---
// IMAPIProp
// ---

M4LMAPIProp::~M4LMAPIProp() {
	for (auto pvp : properties)
		MAPIFreeBuffer(pvp);
	properties.clear();
}

HRESULT M4LMAPIProp::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError) {
	*lppMAPIError = NULL;
	return hrSuccess;
}

HRESULT M4LMAPIProp::SaveChanges(ULONG ulFlags) {
	// memory only.
	return hrSuccess;
}

HRESULT M4LMAPIProp::GetProps(const SPropTagArray *lpPropTagArray,
    ULONG ulFlags, ULONG *lpcValues, SPropValue **lppPropArray)
{
	std::list<SPropValue *>::const_iterator i;
	ULONG c;
	memory_ptr<SPropValue> props;
	SPropValue sConvert;
	convert_context converter;
	std::wstring unicode;
	std::string ansi;
	LPSPropValue lpCopy = NULL;

	if (!lpPropTagArray) {
		// all properties are requested
		auto hr = MAPIAllocateBuffer(sizeof(SPropValue) * properties.size(), &~props);
		if (hr != hrSuccess)
			return hr;

		for (c = 0, i = properties.begin(); i != properties.end(); ++i, ++c) {
			// perform unicode conversion if required
			if ((ulFlags & MAPI_UNICODE) && PROP_TYPE((*i)->ulPropTag) == PT_STRING8) {
				sConvert.ulPropTag = CHANGE_PROP_TYPE((*i)->ulPropTag, PT_UNICODE);
				unicode = converter.convert_to<std::wstring>((*i)->Value.lpszA);
				sConvert.Value.lpszW = const_cast<wchar_t *>(unicode.c_str());
				lpCopy = &sConvert;
			} else if ((ulFlags & MAPI_UNICODE) == 0 && PROP_TYPE((*i)->ulPropTag) == PT_UNICODE) {
				sConvert.ulPropTag = CHANGE_PROP_TYPE((*i)->ulPropTag, PT_STRING8);
				ansi = converter.convert_to<std::string>((*i)->Value.lpszW);
				sConvert.Value.lpszA = const_cast<char *>(ansi.c_str());

				lpCopy = &sConvert;
			} else {
				lpCopy = *i;
			}
			hr = Util::HrCopyProperty(&props[c], lpCopy, props);
			if (hr != hrSuccess)
				return hr;
		}
		*lpcValues = c;
		*lppPropArray = props.release();
		return hr;
	}

	auto hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpPropTagArray->cValues, &~props);
	if (hr != hrSuccess)
		return hr;

	for (c = 0; c < lpPropTagArray->cValues; ++c) {
		for (i = properties.begin(); i != properties.end(); ++i) {
			if (PROP_ID((*i)->ulPropTag) != PROP_ID(lpPropTagArray->aulPropTag[c]))
				continue;
			// perform unicode conversion if required
			if (PROP_TYPE((*i)->ulPropTag) == PT_STRING8 &&
			    (PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_UNICODE ||
			    ((ulFlags & MAPI_UNICODE) && PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_UNSPECIFIED)))
			{
				// string8 to unicode
				sConvert.ulPropTag = CHANGE_PROP_TYPE((*i)->ulPropTag, PT_UNICODE);
				unicode = converter.convert_to<std::wstring>((*i)->Value.lpszA);
				sConvert.Value.lpszW = const_cast<wchar_t *>(unicode.c_str());
				lpCopy = &sConvert;
			}
			else if (PROP_TYPE((*i)->ulPropTag) == PT_UNICODE &&
			    (PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_STRING8 ||
			    (((ulFlags & MAPI_UNICODE) == 0) && PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_UNSPECIFIED)))
			{
				// unicode to string8
				sConvert.ulPropTag = CHANGE_PROP_TYPE((*i)->ulPropTag, PT_STRING8);
				ansi = converter.convert_to<std::string>((*i)->Value.lpszW);
				sConvert.Value.lpszA = const_cast<char *>(ansi.c_str());
				lpCopy = &sConvert;
			}
			else if (PROP_TYPE((*i)->ulPropTag) == PT_MV_STRING8 &&
			    (PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_MV_UNICODE ||
			    ((ulFlags & MAPI_UNICODE) && PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_UNSPECIFIED)))
			{
				// mv string8 to mv unicode
				sConvert.ulPropTag = CHANGE_PROP_TYPE((*i)->ulPropTag, PT_MV_UNICODE);
				sConvert.Value.MVszW.cValues = (*i)->Value.MVszA.cValues;
				hr = MAPIAllocateMore((*i)->Value.MVszA.cValues * sizeof(wchar_t *),
				     props, reinterpret_cast<void **>(&sConvert.Value.MVszW.lppszW));
				if (hr != hrSuccess)
					return hr;
				for (ULONG d = 0; d < (*i)->Value.MVszA.cValues; ++d) {
					unicode = converter.convert_to<std::wstring>((*i)->Value.MVszA.lppszA[d]);
					hr = MAPIAllocateMore(unicode.length() * sizeof(wchar_t) + sizeof(wchar_t), props, reinterpret_cast<void **>(&sConvert.Value.MVszW.lppszW[d]));
					if (hr != hrSuccess)
						return hr;
					wcscpy(sConvert.Value.MVszW.lppszW[d], unicode.c_str());
				}
				lpCopy = &sConvert;
			}
			else if (PROP_TYPE((*i)->ulPropTag) == PT_MV_UNICODE &&
			    (PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_MV_STRING8 ||
			    (((ulFlags & MAPI_UNICODE) == 0) && PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_UNSPECIFIED)))
			{
				// mv string8 to mv unicode
				sConvert.ulPropTag = CHANGE_PROP_TYPE((*i)->ulPropTag, PT_MV_STRING8);
				sConvert.Value.MVszA.cValues = (*i)->Value.MVszW.cValues;
				hr = MAPIAllocateMore((*i)->Value.MVszW.cValues * sizeof(char *), props,
				     reinterpret_cast<void **>(&sConvert.Value.MVszA.lppszA));
				if (hr != hrSuccess)
					return hr;
				for (ULONG d = 0; d < (*i)->Value.MVszW.cValues; ++d) {
					ansi = converter.convert_to<std::string>((*i)->Value.MVszW.lppszW[d]);
					hr = MAPIAllocateMore(ansi.length() + 1, props, reinterpret_cast<void **>(&sConvert.Value.MVszA.lppszA[d]));
					if (hr != hrSuccess)
						return hr;
					strcpy(sConvert.Value.MVszA.lppszA[d], ansi.c_str());
				}
				lpCopy = &sConvert;
			} else {
				// memory property is requested property
				lpCopy = *i;
			}
			hr = Util::HrCopyProperty(&props[c], lpCopy, props);
			if (hr != hrSuccess)
				return hr;
			break;
		}

		if (i != properties.cend())
			continue;
		// Not found
		props[c].ulPropTag = CHANGE_PROP_TYPE(lpPropTagArray->aulPropTag[c], PT_ERROR);
		props[c].Value.err = MAPI_E_NOT_FOUND;
		hr = MAPI_W_ERRORS_RETURNED;
	}

	*lpcValues = c;
	*lppPropArray = props.release();
	return hr;
}

HRESULT M4LMAPIProp::SetProps(ULONG cValues, const SPropValue *lpPropArray,
    SPropProblemArray **lppProblems)
{
	// Validate input
	if (lpPropArray == nullptr || cValues == 0)
		return MAPI_E_INVALID_PARAMETER;

		// TODO: return MAPI_E_INVALID_PARAMETER, if multivalued property in 
		//       the array and its cValues member is set to zero.		

    // remove possible old properties
	for (unsigned int c = 0; c < cValues; ++c) {
		for (auto i = properties.begin(); i != properties.end(); ) {
			if ( PROP_ID((*i)->ulPropTag) == PROP_ID(lpPropArray[c].ulPropTag) && 
				(*i)->ulPropTag != PR_NULL && 
				PROP_TYPE((*i)->ulPropTag) != PT_ERROR)
			{
				auto del = i++;
				MAPIFreeBuffer(*del);
				properties.erase(del);
				break;
			} else {
				++i;
			}
		}
	}

    // set new properties
	for (unsigned int c = 0; c < cValues; ++c) {
		// Ignore PR_NULL property tag and all properties with a type of PT_ERROR
		if(PROP_TYPE(lpPropArray[c].ulPropTag) == PT_ERROR || 
			lpPropArray[c].ulPropTag == PR_NULL)
			continue;
		memory_ptr<SPropValue> pv;
		auto hr = MAPIAllocateBuffer(sizeof(SPropValue), &~pv);
		if (hr != hrSuccess)
			return hr;
		memset(pv, 0, sizeof(SPropValue));
		hr = Util::HrCopyProperty(pv, &lpPropArray[c], pv);
		if (hr != hrSuccess)
			return hr;
		properties.emplace_back(pv);
		pv.release();
	}
	return hrSuccess;
}

HRESULT M4LMAPIProp::DeleteProps(const SPropTagArray *lpPropTagArray,
    SPropProblemArray **lppProblems)
{
	for (ULONG c = 0; c < lpPropTagArray->cValues; ++c) {
		for (auto i = properties.begin(); i != properties.end(); ++i) {
			// @todo check PT_STRING8 vs PT_UNICODE
			if ((*i)->ulPropTag == lpPropTagArray->aulPropTag[c] ||
				(PROP_TYPE((*i)->ulPropTag) == PT_UNSPECIFIED && PROP_ID((*i)->ulPropTag) == PROP_ID(lpPropTagArray->aulPropTag[c])) )
			{
				MAPIFreeBuffer(*i);
				properties.erase(i);
				break;
			}
		}
	}
	return hrSuccess;
}

HRESULT M4LMAPIProp::QueryInterface(const IID &refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT M4LMailUser::QueryInterface(const IID &refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(IMailUser, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

// ---
// IProfSect
// ---
HRESULT M4LProfSect::QueryInterface(const IID &refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(IProfSect, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

// ---
// IProviderAdmin
// ---
M4LProviderAdmin::M4LProviderAdmin(M4LMsgServiceAdmin *new_msa,
    const char *serv) :
	msa(new_msa), szService(serv != nullptr ? strdup(serv) : nullptr)
{}

M4LProviderAdmin::~M4LProviderAdmin() {
	free(szService);
}

HRESULT M4LProviderAdmin::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError) {
    *lppMAPIError = NULL;
    return hrSuccess;
}

HRESULT M4LProviderAdmin::GetProviderTable(ULONG ulFlags, LPMAPITABLE* lppTable) {
	ULONG cValues = 0;
	object_ptr<ECMemTable> lpTable;
	object_ptr<ECMemTableView> lpTableView;
	ULONG cValuesDest = 0;
	SPropValue sPropID;
	int n = 0;
	memory_ptr<SPropTagArray> lpPropTagArray;
	SizedSPropTagArray(8, sptaProviderCols) =
		{8, {PR_MDB_PROVIDER, PR_INSTANCE_KEY, PR_RECORD_KEY,
		PR_ENTRYID, PR_DISPLAY_NAME_A, PR_OBJECT_TYPE,
		PR_RESOURCE_TYPE, PR_PROVIDER_UID}};

	Util::proptag_change_unicode(ulFlags, sptaProviderCols);
	auto hr = ECMemTable::Create(sptaProviderCols, PR_ROWID, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	
	// Loop through all providers, add each to the table
	ulock_rec l_srv(msa->m_mutexserviceadmin);
	for (auto &prov : msa->providers) {
		memory_ptr<SPropValue> lpsProps, lpDest;

		if (szService != NULL &&
		    strcmp(szService, prov->servicename.c_str()) != 0)
			continue;
		
		hr = prov->profilesection->GetProps(sptaProviderCols, 0,
		     &cValues, &~lpsProps);
		if (FAILED(hr))
			return hr;
		sPropID.ulPropTag = PR_ROWID;
		sPropID.Value.ul = n++;
		hr = Util::HrAddToPropertyArray(lpsProps, cValues, &sPropID, &~lpDest, &cValuesDest);
		if(hr != hrSuccess)
			return hr;
		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, nullptr, lpDest, cValuesDest);
		if (hr != hrSuccess)
			return hr;
	}
	
	hr = lpTable->HrGetView(createLocaleFromName(nullptr), ulFlags, &~lpTableView);
	if(hr != hrSuccess)
		return hr;
	return lpTableView->QueryInterface(IID_IMAPITable, reinterpret_cast<void **>(lppTable));
}

/** 
 * Add a provider to a MAPI service
 * 
 * @param[in] lpszProvider name of the provider to add, must be known through mapisvc.inf
 * @param[in] cValues number of properties in lpProps
 * @param[in] lpProps properties to set on the provider context (properties from mapisvc.inf)
 * @param[in] ulUIParam Unused in linux
 * @param[in] ulFlags must be 0
 * @param[out] lpUID a uid which will identify this added provider in the service context
 * 
 * @return MAPI Error code
 */
HRESULT M4LProviderAdmin::CreateProvider(const TCHAR *lpszProvider,
    ULONG cValues, const SPropValue *lpProps, ULONG ulUIParam, ULONG ulFlags,
    MAPIUID *lpUID)
{
	SPropValue sProps[10];
	ULONG nProps = 0;
	memory_ptr<SPropValue> lpsPropValProfileName;
	std::unique_ptr<providerEntry> entry;
	ULONG cProviderProps = 0;
	ulock_rec l_srv(msa->m_mutexserviceadmin);

	if (szService == nullptr)
		return MAPI_E_NO_ACCESS;
	auto lpService = msa->findServiceAdmin(reinterpret_cast<TCHAR *>(szService));
	if (lpService == nullptr)
		return MAPI_E_NO_ACCESS;
	auto lpProvider = lpService->service->GetProvider(lpszProvider, ulFlags);
	if (lpProvider == nullptr)
		return MAPI_E_NO_ACCESS;
	entry.reset(new(std::nothrow) providerEntry);
	if (entry == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	entry->profilesection.reset(new(std::nothrow) M4LProfSect);
	if (entry->profilesection == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	
	// Set the default profilename
	auto hr = HrGetOneProp(msa->profilesection, PR_PROFILE_NAME_A, &~lpsPropValProfileName);
	if (hr != hrSuccess)
		return hr;
	hr = entry->profilesection->SetProps(1, lpsPropValProfileName, NULL);
	if (hr != hrSuccess)
		return hr;
	CoCreateGuid(reinterpret_cast<GUID *>(&entry->uid));

	// no need to free this, not a copy!
	const SPropValue *lpProviderProps;
	lpProvider->GetProps(&cProviderProps, &lpProviderProps);
	hr = entry->profilesection->SetProps(cProviderProps, lpProviderProps, NULL);
	if (hr != hrSuccess)
		return hr;
	if (cValues && lpProps) {
		hr = entry->profilesection->SetProps(cValues, lpProps, NULL);
		if (hr != hrSuccess)
			return hr;
	}

	sProps[nProps].ulPropTag = PR_INSTANCE_KEY;
	sProps[nProps].Value.bin.lpb = (BYTE *)&entry->uid;
	sProps[nProps].Value.bin.cb = sizeof(GUID);
	++nProps;

	sProps[nProps].ulPropTag = PR_PROVIDER_UID;
	sProps[nProps].Value.bin.lpb = (BYTE *)&entry->uid;
	sProps[nProps].Value.bin.cb = sizeof(GUID);
	++nProps;

	auto lpResource = PCpropFindProp(lpProviderProps, cProviderProps, PR_RESOURCE_TYPE);
	if (!lpResource || lpResource->Value.ul == MAPI_STORE_PROVIDER) {
		sProps[nProps].ulPropTag = PR_OBJECT_TYPE;
		sProps[nProps].Value.ul = MAPI_STORE;
		++nProps;
		lpResource = PCpropFindProp(lpProviderProps, cProviderProps, PR_RESOURCE_FLAGS);
		sProps[nProps].ulPropTag = PR_DEFAULT_STORE;
		sProps[nProps].Value.b = lpResource && lpResource->Value.ul & STATUS_DEFAULT_STORE;
		++nProps;
	} else if (lpResource->Value.ul == MAPI_AB_PROVIDER) {
		sProps[nProps].ulPropTag = PR_OBJECT_TYPE;
		sProps[nProps].Value.ul = MAPI_ADDRBOOK;
		++nProps;
	}

	sProps[nProps].ulPropTag = PR_SERVICE_UID;
	sProps[nProps].Value.bin.lpb = (BYTE *)&lpService->muid;
	sProps[nProps].Value.bin.cb = sizeof(GUID);
	++nProps;

	hr = entry->profilesection->SetProps(nProps, sProps, NULL);
	if (hr != hrSuccess)
		return hr;
	entry->servicename = szService;
	if(lpUID)
		*lpUID = entry->uid;
	msa->providers.emplace_back(std::move(entry));
	// We should really call the MSGServiceEntry with MSG_SERVICE_PROVIDER_CREATE, but there
	// isn't much use at the moment. (since we don't store the profile data on disk? or why not?)
	// another rumor is that that is only called once per service, not once per created provider. huh?
	return hrSuccess;
}

HRESULT M4LProviderAdmin::DeleteProvider(const MAPIUID *lpUID)
{
	for (auto i = msa->providers.begin(); i != msa->providers.end(); ++i) {
		if ((*i)->uid == *lpUID) {
			msa->providers.erase(i);
			return hrSuccess;
		}
	}
	return MAPI_E_NOT_FOUND;
}

HRESULT M4LProviderAdmin::OpenProfileSection(const MAPIUID *lpUID,
    const IID *lpInterface, ULONG ulFlags, IProfSect **lppProfSect)
{
	scoped_rlock l_srv(msa->m_mutexserviceadmin);

	// Special ID: the global guid opens the profile's global profile section instead of a local profile
	if (*lpUID == pbGlobalProfileSectionGuid)
		return msa->OpenProfileSection(lpUID, lpInterface, ulFlags, lppProfSect);
	auto provider = msa->findProvider(lpUID);
	if (provider == nullptr)
		return MAPI_E_NOT_FOUND;
	return provider->profilesection->QueryInterface(lpInterface != nullptr ?
	       *lpInterface : IID_IProfSect, reinterpret_cast<void **>(lppProfSect));
}

HRESULT M4LProviderAdmin::QueryInterface(const IID &refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(IProviderAdmin, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

// 
// IMAPIAdviseSink
// 
ULONG M4LMAPIAdviseSink::OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications) {
	return lpFn(lpContext, cNotif, lpNotifications);
}

HRESULT M4LMAPIAdviseSink::QueryInterface(const IID &refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(IMAPIAdviseSink, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return hrSuccess;
}

M4LABContainer::M4LABContainer(const std::list<abEntry> &lABEntries) : m_lABEntries(lABEntries) {
}

/** 
 * Merges all HierarchyTables from the providers passed in the constructor.
 * 
 * @param[in] ulFlags MAPI_UNICODE
 * @param[out] lppTable ECMemTable with combined contents from all providers
 * 
 * @return 
 */
HRESULT M4LABContainer::GetHierarchyTable(ULONG ulFlags, LPMAPITABLE* lppTable) {
	object_ptr<ECMemTable> lpTable;
	object_ptr<ECMemTableView> lpTableView;

	// make a list of all hierarchy tables, and create the combined column list
	std::list<object_ptr<IMAPITable>> lHierarchies;
	std::set<ULONG> stProps;
	memory_ptr<SPropTagArray> lpColumns;

	for (const auto &abe : m_lABEntries) {
		ULONG ulObjType;
		object_ptr<IABContainer> lpABContainer;
		object_ptr<IMAPITable> lpABHierarchy;
		memory_ptr<SPropTagArray> lpPropArray;

		auto hr = abe.lpABLogon->OpenEntry(0, nullptr, &IID_IABContainer, 0,
		     &ulObjType, &~lpABContainer);
		if (hr != hrSuccess)
			continue;
		hr = lpABContainer->GetHierarchyTable(ulFlags, &~lpABHierarchy);
		if (hr != hrSuccess)
			continue;
		hr = lpABHierarchy->QueryColumns(TBL_ALL_COLUMNS, &~lpPropArray);
		if (hr != hrSuccess)
			continue;

		std::copy(lpPropArray->aulPropTag, lpPropArray->aulPropTag + lpPropArray->cValues, std::inserter(stProps, stProps.begin()));
		lHierarchies.emplace_back(std::move(lpABHierarchy));
	}

	// remove key row
	stProps.erase(PR_ROWID);
	auto hr = MAPIAllocateBuffer(CbNewSPropTagArray(stProps.size() + 1), &~lpColumns);
	if (hr != hrSuccess)
		return hr;
	lpColumns->cValues = stProps.size();
	std::copy(stProps.begin(), stProps.end(), lpColumns->aulPropTag);
	lpColumns->aulPropTag[lpColumns->cValues] = PR_NULL; // will be used for PR_ROWID
	hr = ECMemTable::Create(lpColumns, PR_ROWID, &~lpTable);
	if(hr != hrSuccess)
		return hr;

	// get enough columns from queryrows to add the PR_ROWID
	++lpColumns->cValues;

	unsigned int n = 0;
	for (const auto &mt : lHierarchies) {
		hr = mt->SetColumns(lpColumns, 0);
		if (hr != hrSuccess)
			return hr;

		while (true) {
			rowset_ptr lpsRows;
			hr = mt->QueryRows(1, 0, &~lpsRows);
			if (hr != hrSuccess)
				return hr;
			if (lpsRows->cRows == 0)
				break;
			lpsRows[0].lpProps[stProps.size()].ulPropTag = PR_ROWID;
			lpsRows[0].lpProps[stProps.size()].Value.ul  = n++;
			hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, nullptr, lpsRows[0].lpProps, lpsRows[0].cValues);
			if(hr != hrSuccess)
				return hr;
		}
	}

	hr = lpTable->HrGetView(createLocaleFromName(nullptr), ulFlags, &~lpTableView);
	if(hr != hrSuccess)
		return hr;
	return lpTableView->QueryInterface(IID_IMAPITable, reinterpret_cast<void **>(lppTable));
}

HRESULT M4LABContainer::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	LPABLOGON lpABLogon = NULL;
	MAPIUID muidEntry;

	if (cbEntryID < sizeof(MAPIUID) + 4 || lpEntryID == NULL)
		return MAPI_E_INVALID_PARAMETER;

	// get the provider muid
	memcpy(&muidEntry, (LPBYTE)lpEntryID + 4, sizeof(MAPIUID));

	// locate provider
	for (const auto &abe : m_lABEntries)
		if (memcmp(&muidEntry, &abe.muid, sizeof(MAPIUID)) == 0) {
			lpABLogon = abe.lpABLogon;
			break;
		}
	if (lpABLogon == NULL)
		return MAPI_E_UNKNOWN_ENTRYID;
	// open root container of provider
	return lpABLogon->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
}

HRESULT M4LABContainer::QueryInterface(const IID &refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(IABContainer, this);
	REGISTER_INTERFACE2(IMAPIContainer, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}
