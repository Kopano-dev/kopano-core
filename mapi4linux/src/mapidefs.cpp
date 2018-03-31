/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
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
				sConvert.Value.lpszW = (WCHAR*)unicode.c_str();

				lpCopy = &sConvert;
			} else if ((ulFlags & MAPI_UNICODE) == 0 && PROP_TYPE((*i)->ulPropTag) == PT_UNICODE) {
				sConvert.ulPropTag = CHANGE_PROP_TYPE((*i)->ulPropTag, PT_STRING8);
				ansi = converter.convert_to<std::string>((*i)->Value.lpszW);
				sConvert.Value.lpszA = (char*)ansi.c_str();

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
				sConvert.Value.lpszW = (WCHAR*)unicode.c_str();
				lpCopy = &sConvert;
			}
			else if (PROP_TYPE((*i)->ulPropTag) == PT_UNICODE &&
			    (PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_STRING8 ||
			    (((ulFlags & MAPI_UNICODE) == 0) && PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_UNSPECIFIED)))
			{
				// unicode to string8
				sConvert.ulPropTag = CHANGE_PROP_TYPE((*i)->ulPropTag, PT_STRING8);
				ansi = converter.convert_to<std::string>((*i)->Value.lpszW);
				sConvert.Value.lpszA = (char *)ansi.c_str();
				lpCopy = &sConvert;
			}
			else if (PROP_TYPE((*i)->ulPropTag) == PT_MV_STRING8 &&
			    (PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_MV_UNICODE ||
			    ((ulFlags & MAPI_UNICODE) && PROP_TYPE(lpPropTagArray->aulPropTag[c]) == PT_UNSPECIFIED)))
			{
				// mv string8 to mv unicode
				sConvert.ulPropTag = CHANGE_PROP_TYPE((*i)->ulPropTag, PT_MV_UNICODE);
				sConvert.Value.MVszW.cValues = (*i)->Value.MVszA.cValues;
				hr = MAPIAllocateMore((*i)->Value.MVszA.cValues * sizeof(WCHAR*), props, (void**)&sConvert.Value.MVszW.lppszW);
				if (hr != hrSuccess)
					return hr;
				for (ULONG d = 0; d < (*i)->Value.MVszA.cValues; ++d) {
					unicode = converter.convert_to<std::wstring>((*i)->Value.MVszA.lppszA[d]);
					hr = MAPIAllocateMore(unicode.length() * sizeof(wchar_t) + sizeof(WCHAR), props, reinterpret_cast<void **>(&sConvert.Value.MVszW.lppszW[d]));
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
				hr = MAPIAllocateMore((*i)->Value.MVszW.cValues * sizeof(char*), props, (void**)&sConvert.Value.MVszA.lppszA);
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

HRESULT M4LMAPIProp::GetPropList(ULONG ulFlags, LPSPropTagArray* lppPropTagArray) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIProp::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN* lppUnk) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIProp::SetProps(ULONG cValues, const SPropValue *lpPropArray,
    SPropProblemArray **lppProblems)
{
	LPSPropValue pv = NULL;

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
		auto hr = MAPIAllocateBuffer(sizeof(SPropValue), reinterpret_cast<void **>(&pv));
		if (hr != hrSuccess)
			return hr;
		memset(pv, 0, sizeof(SPropValue));
		hr = Util::HrCopyProperty(pv, &lpPropArray[c], pv);
		if (hr != hrSuccess) {
			MAPIFreeBuffer(pv);
			return hr;
		}
		properties.emplace_back(pv);
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

HRESULT M4LMAPIProp::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIProp::CopyProps(const SPropTagArray *lpIncludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIProp::GetNamesFromIDs(SPropTagArray **tags, const GUID *propset,
    ULONG flags, ULONG *nvals, MAPINAMEID ***names)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIProp::GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID* lppPropNames, ULONG ulFlags, LPSPropTagArray* lppPropTags) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIProp::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IMailUser) {
		AddRef();
		*lpvoid = static_cast<IMailUser *>(this);
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

// ---
// IProfSect
// ---
HRESULT M4LProfSect::ValidateState(ULONG ulUIParam, ULONG ulFlags) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LProfSect::SettingsDialog(ULONG ulUIParam, ULONG ulFlags) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LProfSect::ChangePassword(const TCHAR *oldp, const TCHAR *newp,
    ULONG flags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LProfSect::FlushQueues(ULONG ulUIParam, ULONG cbTargetTransport, LPENTRYID lpTargetTransport, ULONG ulFlags) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LProfSect::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IProfSect) {
		AddRef();
		*lpvoid = static_cast<IProfSect *>(this);
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

// ---
// IMAPITable
// ---
HRESULT M4LMAPITable::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) {
	*lppMAPIError = NULL;
	return hrSuccess;
}

HRESULT M4LMAPITable::Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG * lpulConnection) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::Unadvise(ULONG ulConnection) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::SetColumns(const SPropTagArray *lpPropTagArray,
    ULONG ulFlags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::QueryColumns(ULONG ulFlags, LPSPropTagArray *lpPropTagArray) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::GetRowCount(ULONG ulFlags, ULONG *lpulCount) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::FindRow(const SRestriction *, BOOKMARK origin, ULONG fl)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::Restrict(const SRestriction *, ULONG flags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::CreateBookmark(BOOKMARK* lpbkPosition) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::FreeBookmark(BOOKMARK bkPosition) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::SortTable(const SSortOrderSet *, ULONG flags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::QuerySortOrder(LPSSortOrderSet *lppSortCriteria) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet *lppRows) {
    // TODO
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::Abort() {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount,
								ULONG ulFlags, LPSRowSet * lppRows, ULONG *lpulMoreRows) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::WaitForCompletion(ULONG ulFlags, ULONG ulTimeout, ULONG *lpulTableStatus) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::GetCollapseState(ULONG ulFlags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState,
			 LPBYTE *lppbCollapseState) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::SetCollapseState(ULONG ulFlags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPITable::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IMAPITable) {
		AddRef();
		*lpvoid = static_cast<IMAPITable *>(this);
	} else if (refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = static_cast<IUnknown *>(this);
	} else
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	return hrSuccess;
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
		lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, lpDest, cValuesDest);
	}
	
	hr = lpTable->HrGetView(createLocaleFromName(""), ulFlags, &~lpTableView);
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
	LPSPropValue lpProviderProps = NULL;
	HRESULT hr = hrSuccess;
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
	hr = HrGetOneProp((IProfSect*)msa->profilesection, PR_PROFILE_NAME_A, &~lpsPropValProfileName);
	if (hr != hrSuccess)
		return hr;
	hr = entry->profilesection->SetProps(1, lpsPropValProfileName, NULL);
	if (hr != hrSuccess)
		return hr;
	CoCreateGuid((LPGUID)&entry->uid);

	// no need to free this, not a copy!
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
		if(memcmp(&(*i)->uid, lpUID, sizeof(MAPIUID)) == 0) {
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
	if (memcmp(lpUID, &pbGlobalProfileSectionGuid, sizeof(*lpUID)) == 0)
		return msa->OpenProfileSection(lpUID, lpInterface, ulFlags, lppProfSect);
	auto provider = msa->findProvider(lpUID);
	if (provider == nullptr)
		return MAPI_E_NOT_FOUND;
	return provider->profilesection->QueryInterface(lpInterface != nullptr ?
	       *lpInterface : IID_IProfSect, reinterpret_cast<void **>(lppProfSect));
}

HRESULT M4LProviderAdmin::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IProviderAdmin) {
		AddRef();
		*lpvoid = static_cast<IProviderAdmin *>(this);
	} else if (refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = static_cast<IUnknown *>(this);
	} else
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	return hrSuccess;
}

// 
// IMAPIAdviseSink
// 
ULONG M4LMAPIAdviseSink::OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications) {
	return lpFn(lpContext, cNotif, lpNotifications);
}

HRESULT M4LMAPIAdviseSink::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IMAPIAdviseSink) {
		AddRef();
		*lpvoid = static_cast<IMAPIAdviseSink *>(this);
	} else if (refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = static_cast<IUnknown *>(this);
	} else
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	return hrSuccess;
}

// 
// IMAPIContainer
// 
HRESULT M4LMAPIContainer::GetContentsTable(ULONG ulFlags, LPMAPITABLE* lppTable) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIContainer::GetHierarchyTable(ULONG ulFlags, LPMAPITABLE* lppTable) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIContainer::OpenEntry(ULONG eid_size, const ENTRYID *eid,
    const IID *intf, ULONG flags, ULONG *objtype, IUnknown **res)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIContainer::SetSearchCriteria(const SRestriction *,
    const ENTRYLIST *container, ULONG flags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIContainer::GetSearchCriteria(ULONG ulFlags, LPSRestriction* lppRestriction, LPENTRYLIST* lppContainerList, ULONG* lpulSearchState) {
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPIContainer::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IMAPIContainer) {
		AddRef();
		*lpvoid = static_cast<IMAPIContainer *>(this);
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

// 
// IABContainer
// 

M4LABContainer::M4LABContainer(const std::list<abEntry> &lABEntries) : m_lABEntries(lABEntries) {
}

HRESULT M4LABContainer::CreateEntry(ULONG eid_size, const ENTRYID *eid,
    ULONG flags, IMAPIProp **)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LABContainer::CopyEntries(const ENTRYLIST *, ULONG ui_param,
    IMAPIProgress *, ULONG flags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LABContainer::DeleteEntries(const ENTRYLIST *, ULONG flags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT M4LABContainer::ResolveNames(const SPropTagArray *, ULONG flags,
    LPADRLIST, LPFlagList)
{
	return MAPI_E_NO_SUPPORT;
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

	hr = lpTable->HrGetView(createLocaleFromName(""), ulFlags, &~lpTableView);
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

HRESULT M4LABContainer::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IABContainer) {
		AddRef();
		*lpvoid = static_cast<IABContainer *>(this);
	} else if (refiid == IID_IMAPIContainer) {
		AddRef();
		*lpvoid = static_cast<IMAPIContainer *>(this);
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
