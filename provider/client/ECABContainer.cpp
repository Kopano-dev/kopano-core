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
#include <new>
#include <kopano/platform.h>
#include "kcore.hpp"

#include "ECMAPITable.h"
#include "Mem.h"

#include <kopano/ECGuid.h>
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include "ics.h"
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include "ECABContainer.h"

#include <edkmdb.h>
#include <mapiutil.h>

#include <kopano/charset/convstring.h>
#include <kopano/ECGetText.h>

using namespace KC;

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

HRESULT	ECABContainer::DefaultABContainerGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT		hr = hrSuccess;
	auto lpProp = static_cast<ECABProp *>(lpParam);
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
			lpszName = _("Global Address Book");
		else if(strValue.compare( L"Global Address Lists" ) == 0)
			lpszName = _("Global Address Lists");
		else if (strValue.compare( L"All Address Lists" ) == 0)
			lpszName = _("All Address Lists");

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

HRESULT ECABContainer::SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags)
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

HRESULT ECABContainer::CopyEntries(LPENTRYLIST lpEntries, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABContainer::DeleteEntries(LPENTRYLIST lpEntries, ULONG ulFlags)
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
