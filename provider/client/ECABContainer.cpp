/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include "kcore.hpp"
#include "ECMAPITable.h"
#include "Mem.h"
#include <kopano/ECDefs.h>
#include <kopano/ECGuid.h>
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include "ics.h"
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include "ECABContainer.h"
#include <edkguid.h>
#include <edkmdb.h>
#include <mapi.h>
#include <mapispi.h>
#include <mapiutil.h>
#include <kopano/charset/convstring.h>
#include <kopano/ECGetText.h>
#include "ClientUtil.h"
#include "ECABContainer.h"
#include "ECMailUser.h"
#include "EntryPoint.h"
#include "ProviderUtil.h"
#include "WSTransport.h"
#include "pcutil.hpp"

using namespace KC;

static const ABEID_FIXED eidRoot(MAPI_ABCONT, MUIDECSAB, 0);

ECABContainer::ECABContainer(ECABLogon *prov, ULONG objtype, BOOL modify,
    const char *cls_name) :
	ECABProp(prov, objtype, modify, cls_name)
{
	HrAddPropHandlers(PR_AB_PROVIDER_ID, DefaultABContainerGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_CONTAINER_FLAGS, DefaultABContainerGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_DISPLAY_TYPE, DefaultABContainerGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_EMSMDB_SECTION_UID, DefaultABContainerGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_ACCOUNT, DefaultABContainerGetProp, DefaultSetPropIgnore, this);
	HrAddPropHandlers(PR_NORMALIZED_SUBJECT, DefaultABContainerGetProp, DefaultSetPropIgnore, this);
	HrAddPropHandlers(PR_DISPLAY_NAME, DefaultABContainerGetProp, DefaultSetPropIgnore, this);
	HrAddPropHandlers(PR_TRANSMITABLE_DISPLAY_NAME, DefaultABContainerGetProp, DefaultSetPropIgnore, this);
}

HRESULT	ECABContainer::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECABContainer, this);
	REGISTER_INTERFACE2(ECABProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABContainer, this);
	REGISTER_INTERFACE2(IMAPIContainer, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABContainer::Create(ECABLogon *lpProvider, ULONG ulObjType,
    BOOL fModify, ECABContainer **lppABContainer)
{
	return alloc_wrap<ECABContainer>(lpProvider, ulObjType, fModify, "IABContainer")
	       .put(lppABContainer);
}

HRESULT	ECABContainer::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	if (lpiid == NULL)
		return MAPI_E_INVALID_PARAMETER;

	switch (ulPropTag) {
	case PR_CONTAINER_CONTENTS:
		if (*lpiid != IID_IMAPITable)
			return MAPI_E_INTERFACE_NOT_SUPPORTED;
		return GetContentsTable(ulInterfaceOptions, reinterpret_cast<IMAPITable **>(lppUnk));
	case PR_CONTAINER_HIERARCHY:
		if (*lpiid != IID_IMAPITable)
			return MAPI_E_INTERFACE_NOT_SUPPORTED;
		return GetHierarchyTable(ulInterfaceOptions, reinterpret_cast<IMAPITable **>(lppUnk));
	default:
		return ECABProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	}
}

HRESULT ECABContainer::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyTo(&IID_IABContainer, static_cast<IABContainer *>(this), ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECABContainer::CopyProps(const SPropTagArray *lpIncludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyProps(&IID_IABContainer, static_cast<IABContainer *>(this), lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECABContainer::DefaultABContainerGetProp(unsigned int ulPropTag,
    void *lpProvider, unsigned int ulFlags, SPropValue *lpsPropValue,
    ECGenericProp *lpParam, void *lpBase)
{
	HRESULT		hr = hrSuccess;
	auto lpProp = static_cast<ECABContainer *>(lpParam);
	memory_ptr<SPropValue> lpSectionUid;
	object_ptr<IProfSect> lpProfSect;

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_EMSMDB_SECTION_UID): {
		auto lpLogon = static_cast<ECABLogon *>(lpProvider);
		if (lpLogon->m_lpMAPISup == nullptr)
			return MAPI_E_NOT_FOUND;
		hr = lpLogon->m_lpMAPISup->OpenProfileSection(nullptr, 0, &~lpProfSect);
		if(hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(lpProfSect, PR_EMSMDB_SECTION_UID, &~lpSectionUid);
		if(hr != hrSuccess)
			return hr;
		lpsPropValue->ulPropTag = PR_EMSMDB_SECTION_UID;
		hr = KAllocCopy(lpSectionUid->Value.bin.lpb, sizeof(GUID), reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb), lpBase);
		if (hr != hrSuccess)
			return hr;
		lpsPropValue->Value.bin.cb = sizeof(GUID);
		break;
		}
	case PROP_ID(PR_AB_PROVIDER_ID):
		lpsPropValue->ulPropTag = PR_AB_PROVIDER_ID;

		lpsPropValue->Value.bin.cb = sizeof(GUID);
		hr = ECAllocateMore(sizeof(GUID), lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
		if (hr != hrSuccess)
			break;
		memcpy(lpsPropValue->Value.bin.lpb, &MUIDECSAB, sizeof(GUID));
		break;
	case PROP_ID(PR_ACCOUNT):
	case PROP_ID(PR_NORMALIZED_SUBJECT):
	case PROP_ID(PR_DISPLAY_NAME):
	case PROP_ID(PR_TRANSMITABLE_DISPLAY_NAME):
		{
		LPCTSTR lpszName = NULL;
		std::wstring strValue;

		hr = lpProp->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
		if(hr != hrSuccess)
			return hr;

		if (PROP_TYPE(lpsPropValue->ulPropTag) == PT_UNICODE)
			strValue = convert_to<std::wstring>(lpsPropValue->Value.lpszW);
		else if (PROP_TYPE(lpsPropValue->ulPropTag) == PT_STRING8)
			strValue = convert_to<std::wstring>(lpsPropValue->Value.lpszA);
		else
			return hr;

		if(strValue.compare( L"Global Address Book" ) == 0)
			lpszName = KC_TX("Global Address Book");
		else if(strValue.compare( L"Global Address Lists" ) == 0)
			lpszName = KC_TX("Global Address Lists");
		else if (strValue.compare( L"All Address Lists" ) == 0)
			lpszName = KC_TX("All Address Lists");

		if (lpszName == nullptr)
			break;
		if (PROP_TYPE(ulPropTag) == PT_UNICODE) {
			const std::wstring strTmp = convert_to<std::wstring>(lpszName);
			hr = MAPIAllocateMore((strTmp.size() + 1) * sizeof(WCHAR), lpBase, (void**)&lpsPropValue->Value.lpszW);
			if (hr != hrSuccess)
				return hr;
			wcscpy(lpsPropValue->Value.lpszW, strTmp.c_str());
		} else {
			const std::string strTmp = convert_to<std::string>(lpszName);
			hr = MAPIAllocateMore(strTmp.size() + 1, lpBase, (void**)&lpsPropValue->Value.lpszA);
			if (hr != hrSuccess)
				return hr;
			strcpy(lpsPropValue->Value.lpszA, strTmp.c_str());
		}
		lpsPropValue->ulPropTag = ulPropTag;
		break;
	}
	default:
		return lpProp->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
	}
	return hrSuccess;
}

HRESULT ECABContainer::TableRowGetProp(void *lpProvider,
    const struct propVal *lpsPropValSrc, SPropValue *lpsPropValDst,
    void **lpBase, ULONG ulType)
{
	ULONG size = 0;

	switch(lpsPropValSrc->ulPropTag) {
	case PR_ACCOUNT_W:
	case PR_NORMALIZED_SUBJECT_W:
	case PR_DISPLAY_NAME_W:
	case PR_TRANSMITABLE_DISPLAY_NAME_W: {
		LPWSTR lpszW = NULL;
		if (strcmp(lpsPropValSrc->Value.lpszA, "Global Address Book" ) == 0)
			lpszW = KC_W("Global Address Book");
		else if (strcmp(lpsPropValSrc->Value.lpszA, "Global Address Lists" ) == 0)
			lpszW = KC_W("Global Address Lists");
		else if (strcmp(lpsPropValSrc->Value.lpszA, "All Address Lists" ) == 0)
			lpszW = KC_W("All Address Lists");
		else
			return MAPI_E_NOT_FOUND;
		size = (wcslen(lpszW) + 1) * sizeof(WCHAR);
		lpsPropValDst->ulPropTag = lpsPropValSrc->ulPropTag;
		return KAllocCopy(lpszW, size, reinterpret_cast<void **>(&lpsPropValDst->Value.lpszW), lpBase);
	}
	case PR_ACCOUNT_A:
	case PR_NORMALIZED_SUBJECT_A:
	case PR_DISPLAY_NAME_A:
	case PR_TRANSMITABLE_DISPLAY_NAME_A: {
		LPSTR lpszA = NULL;
		if (strcmp(lpsPropValSrc->Value.lpszA, "Global Address Book" ) == 0)
			lpszA = KC_A("Global Address Book");
		else if (strcmp(lpsPropValSrc->Value.lpszA, "Global Address Lists" ) == 0)
			lpszA = KC_A("Global Address Lists");
		else if (strcmp(lpsPropValSrc->Value.lpszA, "All Address Lists" ) == 0)
			lpszA = KC_A("All Address Lists");
		else
			return MAPI_E_NOT_FOUND;
		size = (strlen(lpszA) + 1) * sizeof(CHAR);
		lpsPropValDst->ulPropTag = lpsPropValSrc->ulPropTag;
		return KAllocCopy(lpszA, size, reinterpret_cast<void **>(&lpsPropValDst->Value.lpszA), lpBase);
	}
	default:
		return MAPI_E_NOT_FOUND;
	}
	return hrSuccess;
}

// IMAPIContainer
HRESULT ECABContainer::GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableView> lpTableOps;
	static constexpr const SizedSSortOrderSet(1, sSortByDisplayName) =
		{1, 0, 0, {{PR_DISPLAY_NAME, TABLE_SORT_ASCEND}}};

	auto hr = ECMAPITable::Create("AB Contents", nullptr, 0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = GetABStore()->m_lpTransport->HrOpenABTableOps(MAPI_MAILUSER,
	     ulFlags, m_cbEntryId, m_lpEntryId,
	     static_cast<ECABLogon *>(lpProvider), &~lpTableOps); // also MAPI_DISTLIST
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));
	if(hr != hrSuccess)
		return hr;
	hr = lpTableOps->HrSortTable(sSortByDisplayName);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);
	AddChild(lpTable);
	return hr;
}

HRESULT ECABContainer::GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableView> lpTableOps;

	auto hr = ECMAPITable::Create("AB hierarchy", GetABStore()->m_lpNotifyClient, ulFlags, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = GetABStore()->m_lpTransport->HrOpenABTableOps(MAPI_ABCONT, ulFlags,
	     m_cbEntryId, m_lpEntryId, static_cast<ECABLogon *>(lpProvider),
	     &~lpTableOps);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);
	AddChild(lpTable);
	return hr;
}

HRESULT ECABContainer::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	return GetABStore()->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
}

HRESULT ECABContainer::SetSearchCriteria(const SRestriction *,
    const ENTRYLIST *container, ULONG flags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABContainer::GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState)
{
	return MAPI_E_NO_SUPPORT;
}

// IABContainer
HRESULT ECABContainer::CreateEntry(ULONG eid_size, const ENTRYID *eid,
    ULONG flags, IMAPIProp **)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABContainer::CopyEntries(const ENTRYLIST *, ULONG ui_param,
    IMAPIProgress *, ULONG flags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABContainer::DeleteEntries(const ENTRYLIST *, ULONG flags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABContainer::ResolveNames(const SPropTagArray *lpPropTagArray,
    ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList)
{
	static constexpr const SizedSPropTagArray(11, sptaDefault) =
		{11, {PR_ADDRTYPE_A, PR_DISPLAY_NAME_A, PR_DISPLAY_TYPE,
		PR_EMAIL_ADDRESS_A, PR_SMTP_ADDRESS_A, PR_ENTRYID,
		PR_INSTANCE_KEY, PR_OBJECT_TYPE, PR_RECORD_KEY, PR_SEARCH_KEY,
		PR_EC_SENDAS_USER_ENTRYIDS}};
	static constexpr const SizedSPropTagArray(11, sptaDefaultUnicode) =
		{11, {PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_DISPLAY_TYPE,
		PR_EMAIL_ADDRESS_W, PR_SMTP_ADDRESS_W, PR_ENTRYID,
		PR_INSTANCE_KEY, PR_OBJECT_TYPE, PR_RECORD_KEY, PR_SEARCH_KEY,
		PR_EC_SENDAS_USER_ENTRYIDS}};
	if (lpPropTagArray == NULL)
		lpPropTagArray = (ulFlags & MAPI_UNICODE) ?
		                 sptaDefaultUnicode : sptaDefault;
	return ((ECABLogon*)lpProvider)->m_lpTransport->HrResolveNames(lpPropTagArray, ulFlags, lpAdrList, lpFlagList);
}

ECABLogon::ECABLogon(LPMAPISUP lpMAPISup, WSTransport *lpTransport,
    ULONG ulProfileFlags, const GUID *lpGUID) :
	ECUnknown("IABLogon"), m_lpMAPISup(lpMAPISup),
	m_lpTransport(lpTransport),
	/* The "legacy" guid used normally (all AB entryIDs have this GUID) */
	m_guid(MUIDECSAB),
	/* The specific GUID for *this* addressbook provider, if available */
	m_ABPGuid((lpGUID != nullptr) ? *lpGUID : GUID_NULL)
{
	if (! (ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS))
		ECNotifyClient::Create(MAPI_ADDRBOOK, this, ulProfileFlags, lpMAPISup, &~m_lpNotifyClient);
}

ECABLogon::~ECABLogon()
{
	if(m_lpTransport)
		m_lpTransport->HrLogOff();
	// Disable all advises
	if(m_lpNotifyClient)
		m_lpNotifyClient->ReleaseAll();
}

HRESULT ECABLogon::Create(IMAPISupport *lpMAPISup, WSTransport *lpTransport,
    ULONG ulProfileFlags, const GUID *lpGuid, ECABLogon **lppECABLogon)
{
	return alloc_wrap<ECABLogon>(lpMAPISup, lpTransport, ulProfileFlags,
	       lpGuid).put(lppECABLogon);
}

HRESULT ECABLogon::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECABLogon, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABLogon, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABLogon::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_CALL_FAILED;
}

HRESULT ECABLogon::Logoff(ULONG ulFlags)
{
	//FIXME: Release all Other open objects ?
	//Releases all open objects, such as any subobjects or the status object.
	//Releases the provider's support object.
	m_lpMAPISup.reset();
	return hrSuccess;
}

HRESULT ECABLogon::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	if (lpulObjType == nullptr || lppUnk == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT			hr = hrSuccess;
	object_ptr<ECABContainer> lpABContainer;
	BOOL			fModifyObject = FALSE;
	ABEID_FIXED lpABeid;
	object_ptr<IECPropStorage> lpPropStorage;
	object_ptr<ECMailUser> lpMailUser;
	object_ptr<ECDistList> 	lpDistList;
	memory_ptr<ENTRYID> lpEntryIDServer;

	/*if(ulFlags & MAPI_MODIFY) {
		if (!fModify)
			return MAPI_E_NO_ACCESS;
		else
			fModifyObject = TRUE;
	}
	if(ulFlags & MAPI_BEST_ACCESS)
		fModifyObject = fModify;
	*/

	if(cbEntryID == 0 && lpEntryID == NULL) {
		memcpy(&lpABeid, &eidRoot, sizeof(lpABeid));
		cbEntryID = sizeof(lpABeid);
		lpEntryID = reinterpret_cast<ENTRYID *>(&lpABeid);
	} else {
		if (cbEntryID == 0 || lpEntryID == nullptr || cbEntryID < sizeof(ABEID))
			return MAPI_E_UNKNOWN_ENTRYID;
		hr = KAllocCopy(lpEntryID, cbEntryID, &~lpEntryIDServer);
		if(hr != hrSuccess)
			return hr;
		lpEntryID = lpEntryIDServer;
		memcpy(&lpABeid, lpEntryID, sizeof(ABEID));

		// Check sane entryid
		if (lpABeid.ulType != MAPI_ABCONT &&
		    lpABeid.ulType != MAPI_MAILUSER &&
		    lpABeid.ulType != MAPI_DISTLIST)
			return MAPI_E_UNKNOWN_ENTRYID;

		// Check entryid GUID, must be either MUIDECSAB or m_ABPGuid
		if (memcmp(&lpABeid.guid, &MUIDECSAB, sizeof(MAPIUID)) != 0 &&
		    memcmp(&lpABeid.guid, &m_ABPGuid, sizeof(MAPIUID)) != 0)
			return MAPI_E_UNKNOWN_ENTRYID;
		memcpy(&lpABeid.guid, &MUIDECSAB, sizeof(MAPIUID));
	}

	//TODO: check entryid serverside?
	switch (lpABeid.ulType) {
	case MAPI_ABCONT:
		hr = ECABContainer::Create(this, MAPI_ABCONT, fModifyObject, &~lpABContainer);
		if (hr != hrSuccess)
			return hr;
		hr = lpABContainer->SetEntryId(cbEntryID, lpEntryID);
		if (hr != hrSuccess)
			return hr;
		AddChild(lpABContainer);
		hr = m_lpTransport->HrOpenABPropStorage(cbEntryID, lpEntryID, &~lpPropStorage);
		if (hr != hrSuccess)
			return hr;
		hr = lpABContainer->HrSetPropStorage(lpPropStorage, TRUE);
		if (hr != hrSuccess)
			return hr;
		hr = lpABContainer->QueryInterface(lpInterface != nullptr ? *lpInterface : IID_IABContainer, reinterpret_cast<void **>(lppUnk));
		if (hr != hrSuccess)
			return hr;
		break;
	case MAPI_MAILUSER:
		hr = ECMailUser::Create(this, fModifyObject, &~lpMailUser);
		if (hr != hrSuccess)
			return hr;
		hr = lpMailUser->SetEntryId(cbEntryID, lpEntryID);
		if (hr != hrSuccess)
			return hr;
		AddChild(lpMailUser);
		hr = m_lpTransport->HrOpenABPropStorage(cbEntryID, lpEntryID, &~lpPropStorage);
		if (hr != hrSuccess)
			return hr;
		hr = lpMailUser->HrSetPropStorage(lpPropStorage, TRUE);
		if (hr != hrSuccess)
			return hr;
		hr = lpMailUser->QueryInterface(lpInterface != nullptr ? *lpInterface : IID_IMailUser, reinterpret_cast<void **>(lppUnk));
		if (hr != hrSuccess)
			return hr;
		break;
	case MAPI_DISTLIST:
		hr = ECDistList::Create(this, fModifyObject, &~lpDistList);
		if (hr != hrSuccess)
			return hr;
		hr = lpDistList->SetEntryId(cbEntryID, lpEntryID);
		if (hr != hrSuccess)
			return hr;
		AddChild(lpDistList);
		hr = m_lpTransport->HrOpenABPropStorage(cbEntryID, lpEntryID, &~lpPropStorage);
		if (hr != hrSuccess)
			return hr;
		hr = lpDistList->HrSetPropStorage(lpPropStorage, TRUE);
		if (hr != hrSuccess)
			return hr;
		hr = lpDistList->QueryInterface(lpInterface != nullptr ? *lpInterface : IID_IDistList, reinterpret_cast<void **>(lppUnk));
		if (hr != hrSuccess)
			return hr;
		break;
	default:
		return MAPI_E_NOT_FOUND;
	}

	if(lpulObjType)
		*lpulObjType = lpABeid.ulType;
	return hrSuccess;
}

HRESULT ECABLogon::CompareEntryIDs(ULONG cbEntryID1, const ENTRYID *lpEntryID1,
    ULONG cbEntryID2, const ENTRYID *lpEntryID2, ULONG ulFlags,
    ULONG *lpulResult)
{
	if(lpulResult)
		*lpulResult = CompareABEID(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2);
	return hrSuccess;
}

HRESULT ECABLogon::Advise(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulEventMask, IMAPIAdviseSink *lpAdviseSink, ULONG *lpulConnection)
{
	if (lpAdviseSink == NULL || lpulConnection == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpEntryID == NULL)
		//NOTE: Normal you must give the entryid of the addressbook toplevel
		return MAPI_E_INVALID_PARAMETER;
	assert(m_lpNotifyClient != NULL && (lpEntryID != NULL || true));
	if(m_lpNotifyClient->Advise(cbEntryID, (LPBYTE)lpEntryID, ulEventMask, lpAdviseSink, lpulConnection) != S_OK)
		return MAPI_E_NO_SUPPORT;
	return hrSuccess;
}

HRESULT ECABLogon::Unadvise(ULONG ulConnection)
{
	assert(m_lpNotifyClient != NULL);
	m_lpNotifyClient->Unadvise(ulConnection);
	return hrSuccess;
}

HRESULT ECABLogon::OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPMAPISTATUS * lppMAPIStatus)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABLogon::OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid,
    ULONG tpl_flags, IMAPIProp *propdata, const IID *iface, IMAPIProp **propnew,
    IMAPIProp *sibling)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABLogon::GetOneOffTable(ULONG ulFlags, LPMAPITABLE * lppTable)
{
	//return m_lpMAPISup->GetOneOffTable(ulFlags, lppTable);
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABLogon::PrepareRecips(ULONG ulFlags,
    const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList)
{
	if (lpPropTagArray == nullptr || lpPropTagArray->cValues == 0)
		return hrSuccess;

	ULONG cValues, ulObjType;
	ecmem_ptr<SPropValue> lpPropArray, lpNewPropArray;

	for (unsigned int i = 0; i < lpRecipList->cEntries; ++i) {
		auto rgpropvalsRecip = lpRecipList->aEntries[i].rgPropVals;
		unsigned int cPropsRecip = lpRecipList->aEntries[i].cValues;

		// For each recipient, find its entryid
		auto lpPropVal = PCpropFindProp(rgpropvalsRecip, cPropsRecip, PR_ENTRYID);
		if(!lpPropVal)
			continue; // no

		auto lpABeid = reinterpret_cast<ABEID *>(lpPropVal->Value.bin.lpb);
		auto cbABeid = lpPropVal->Value.bin.cb;
		/* Is it one of ours? */
		if ( cbABeid  < CbNewABEID("") || lpABeid == NULL)
			continue;	// no
		if (memcmp(&lpABeid->guid, &m_guid, sizeof(MAPIUID)) != 0)
			continue;	// no

		object_ptr<IMailUser> lpIMailUser;
		auto hr = OpenEntry(cbABeid, reinterpret_cast<ENTRYID *>(lpABeid), nullptr, 0, &ulObjType, &~lpIMailUser);
		if(hr != hrSuccess)
			continue;	// no
		hr = lpIMailUser->GetProps(lpPropTagArray, 0, &cValues, &~lpPropArray);
		if(FAILED(hr) != hrSuccess)
			continue;	// no
		// merge the properties
		hr = ECAllocateBuffer((cValues + cPropsRecip) * sizeof(SPropValue), &~lpNewPropArray);
		if (hr != hrSuccess)
			return hr;

		for (unsigned int j = 0; j < cValues; ++j) {
			lpPropVal = NULL;

			if(PROP_TYPE(lpPropArray[j].ulPropTag) == PT_ERROR)
				lpPropVal = PCpropFindProp(rgpropvalsRecip, cPropsRecip, lpPropTagArray->aulPropTag[j]);
			if(lpPropVal == NULL)
				lpPropVal = &lpPropArray[j];
			hr = Util::HrCopyProperty(lpNewPropArray + j, lpPropVal, lpNewPropArray);
			if(hr != hrSuccess)
				return hr;
		}

		for (unsigned int j = 0; j < cPropsRecip; ++j) {
			if (PCpropFindProp(lpNewPropArray, cValues, rgpropvalsRecip[j].ulPropTag) ||
				PROP_TYPE( rgpropvalsRecip[j].ulPropTag ) == PT_ERROR )
				continue;
			hr = Util::HrCopyProperty(lpNewPropArray + cValues, &rgpropvalsRecip[j], lpNewPropArray);
			if(hr != hrSuccess)
				return hr;
			++cValues;
		}

		lpRecipList->aEntries[i].rgPropVals	= lpNewPropArray.release();
		lpRecipList->aEntries[i].cValues	= cValues;
		if(rgpropvalsRecip) {
			ECFreeBuffer(rgpropvalsRecip);
			rgpropvalsRecip = NULL;
		}
	}

	// Always succeeded on this point
	return hrSuccess;
}

ECABProp::ECABProp(ECABLogon *prov, ULONG objtype, BOOL modify,
    const char *cls_name) :
	ECGenericProp(prov, objtype, modify, cls_name)
{
	HrAddPropHandlers(PR_RECORD_KEY, DefaultABGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_STORE_SUPPORT_MASK, DefaultABGetProp, DefaultSetPropComputed, this);
}

HRESULT ECABProp::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECABProp, this);
	return ECGenericProp::QueryInterface(refiid, lppInterface);
}

HRESULT ECABProp::DefaultABGetProp(unsigned int ulPropTag, void *lpProvider,
    unsigned int ulFlags, SPropValue *lpsPropValue, ECGenericProp *lpParam,
    void *lpBase)
{
	HRESULT		hr = hrSuccess;
	auto lpProp = static_cast<ECABProp *>(lpParam);

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_RECORD_KEY):
		lpsPropValue->ulPropTag = PR_RECORD_KEY;

		if(lpProp->m_lpEntryId && lpProp->m_cbEntryId > 0) {
			lpsPropValue->Value.bin.cb = lpProp->m_cbEntryId;
			hr = ECAllocateMore(lpsPropValue->Value.bin.cb, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
			if (hr != hrSuccess)
				break;
			memcpy(lpsPropValue->Value.bin.lpb, lpProp->m_lpEntryId, lpsPropValue->Value.bin.cb);
		} else {
			hr = MAPI_E_NOT_FOUND;
		}
		break;
	case PROP_ID(PR_STORE_SUPPORT_MASK):
	{
		unsigned int ulClientVersion = -1;
		KC::GetClientVersion(&ulClientVersion);

		// No real unicode support in outlook 2000 and xp
		if (ulClientVersion > CLIENT_VERSION_OLK2002) {
			lpsPropValue->Value.l = STORE_UNICODE_OK;
			lpsPropValue->ulPropTag = PR_STORE_SUPPORT_MASK;
		} else {
			hr = MAPI_E_NOT_FOUND;
		}
		break;
	}
	default:
		hr = lpProp->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
		break;
	}

	return hr;
}

HRESULT ECABProp::TableRowGetProp(void *lpProvider,
    const struct propVal *lpsPropValSrc, SPropValue *lpsPropValDst,
    void **lpBase, ULONG ulType)
{
	HRESULT hr = hrSuccess;

	switch(lpsPropValSrc->ulPropTag) {
	case CHANGE_PROP_TYPE(PR_AB_PROVIDER_ID, PT_ERROR):
		lpsPropValDst->ulPropTag = PR_AB_PROVIDER_ID;
		lpsPropValDst->Value.bin.cb = sizeof(GUID);
		hr = ECAllocateMore(sizeof(GUID), lpBase, reinterpret_cast<void **>(&lpsPropValDst->Value.bin.lpb));
		if (hr != hrSuccess)
			break;
		memcpy(lpsPropValDst->Value.bin.lpb, &MUIDECSAB, sizeof(GUID));
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}

	return hr;
}

ECABProvider::ECABProvider(ULONG ulFlags, const char *cls_name) :
	ECUnknown(cls_name), m_ulFlags(ulFlags)
{}

HRESULT ECABProvider::Create(ECABProvider **lppECABProvider)
{
	return alloc_wrap<ECABProvider>(0, "ECABProvider").put(lppECABProvider);
}

HRESULT ECABProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABProvider, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABProvider::Shutdown(ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECABProvider::Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam,
    const TCHAR *lpszProfileName, ULONG ulFlags, ULONG *lpulcbSecurity,
    LPBYTE *lppbSecurity, LPMAPIERROR *lppMAPIError, LPABLOGON *lppABLogon)
{
	if (lpMAPISup == nullptr || lppABLogon == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<ECABLogon> lpABLogon;
	sGlobalProfileProps	sProfileProps;
	object_ptr<WSTransport> lpTransport;

	// Get the username and password from the profile settings
	auto hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if(hr != hrSuccess)
		return hr;
	// Create a transport for this provider
	hr = WSTransport::Create(ulFlags, &~lpTransport);
	if(hr != hrSuccess)
		return hr;
	// Log on the transport to the server
	hr = lpTransport->HrLogon(sProfileProps);
	if(hr != hrSuccess)
		return hr;
	hr = ECABLogon::Create(lpMAPISup, lpTransport, sProfileProps.ulProfileFlags, nullptr, &~lpABLogon);
	if(hr != hrSuccess)
		return hr;
	AddChild(lpABLogon);
	hr = lpABLogon->QueryInterface(IID_IABLogon, (void **)lppABLogon);
	if(hr != hrSuccess)
		return hr;
	if (lpulcbSecurity)
		*lpulcbSecurity = 0;
	if (lppbSecurity)
		*lppbSecurity = NULL;
	if (lppMAPIError)
		*lppMAPIError = NULL;
	return hrSuccess;
}

ECABProviderSwitch::ECABProviderSwitch(void) : ECUnknown("ECABProviderSwitch")
{
}

HRESULT ECABProviderSwitch::Create(ECABProviderSwitch **lppECABProvider)
{
	return alloc_wrap<ECABProviderSwitch>().put(lppECABProvider);
}

HRESULT ECABProviderSwitch::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABProvider, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABProviderSwitch::Shutdown(ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECABProviderSwitch::Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam,
    const TCHAR *lpszProfileName, ULONG ulFlags, ULONG *lpulcbSecurity,
    LPBYTE *lppbSecurity, LPMAPIERROR *lppMAPIError, LPABLOGON *lppABLogon)
{
	PROVIDER_INFO sProviderInfo;
	object_ptr<IABLogon> lpABLogon;
	object_ptr<IABProvider> lpOnline;
	convstring tstrProfileName(lpszProfileName, ulFlags);

	auto hr = GetProviders(&g_mapProviders, lpMAPISup, convstring(lpszProfileName, ulFlags).c_str(), ulFlags, &sProviderInfo);
	if (hr != hrSuccess)
		return hr;
	hr = sProviderInfo.lpABProviderOnline->QueryInterface(IID_IABProvider, &~lpOnline);
	if (hr != hrSuccess)
		return hr;

	// Online
	hr = lpOnline->Logon(lpMAPISup, ulUIParam, lpszProfileName, ulFlags, nullptr, nullptr, nullptr, &~lpABLogon);
	// Set the provider in the right connection type
	if (SetProviderMode(lpMAPISup, &g_mapProviders,
	    convstring(lpszProfileName, ulFlags).c_str(), CT_ONLINE) != hrSuccess)
		return MAPI_E_INVALID_PARAMETER;

	if(hr != hrSuccess) {
		if (hr == MAPI_E_NETWORK_ERROR)
			/* for disable public folders, so you can work offline */
			return MAPI_E_FAILONEPROVIDER;
		else if (hr == MAPI_E_LOGON_FAILED)
			return MAPI_E_UNCONFIGURED; /* Linux error ?? */
			//hr = MAPI_E_LOGON_FAILED;
		else
			return MAPI_E_LOGON_FAILED;
	}

	hr = lpMAPISup->SetProviderUID((LPMAPIUID)&MUIDECSAB, 0);
	if(hr != hrSuccess)
		return hr;
	hr = lpABLogon->QueryInterface(IID_IABLogon, (void **)lppABLogon);
	if(hr != hrSuccess)
		return hr;
	if(lpulcbSecurity)
		*lpulcbSecurity = 0;
	if(lppbSecurity)
		*lppbSecurity = NULL;
	if (lppMAPIError)
		*lppMAPIError = NULL;
	return hrSuccess;
}
