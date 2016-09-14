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
#include "kcore.hpp"

#include "ECMAPITable.h"
#include "Mem.h"

#include <kopano/ECGuid.h>
#include <kopano/ECDebug.h>

#include "ECDisplayTable.h"

#include <kopano/CommonUtil.h>
#include "ics.h"
#include <kopano/mapiext.h>

#include "ECABContainer.h"

#include <edkmdb.h>
#include <mapiutil.h>

#include <kopano/charset/convstring.h>
#include <kopano/ECGetText.h>

ECABContainer::ECABContainer(void *lpProvider, ULONG ulObjType, BOOL fModify,
    const char *szClassName) :
	ECABProp(lpProvider, ulObjType, fModify, szClassName)
{
	this->HrAddPropHandlers(PR_AB_PROVIDER_ID,	DefaultABContainerGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_CONTAINER_FLAGS,	DefaultABContainerGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_DISPLAY_TYPE,	DefaultABContainerGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_EMSMDB_SECTION_UID,	DefaultABContainerGetProp,		DefaultSetPropComputed, (void*) this);
	
	this->HrAddPropHandlers(PR_ACCOUNT,	DefaultABContainerGetProp, DefaultSetPropIgnore, (void*) this);
	this->HrAddPropHandlers(PR_NORMALIZED_SUBJECT,	DefaultABContainerGetProp, DefaultSetPropIgnore, (void*) this);
	this->HrAddPropHandlers(PR_DISPLAY_NAME,	DefaultABContainerGetProp, DefaultSetPropIgnore, (void*) this);
	this->HrAddPropHandlers(PR_TRANSMITABLE_DISPLAY_NAME,	DefaultABContainerGetProp, DefaultSetPropIgnore, (void*) this);

	m_lpImporter = NULL;
}

ECABContainer::~ECABContainer()
{
    if(m_lpImporter)
        m_lpImporter->Release();
}

HRESULT	ECABContainer::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECABContainer, this);
	REGISTER_INTERFACE(IID_ECABProp, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);
	REGISTER_INTERFACE(IID_IABContainer, &this->m_xABContainer);
	REGISTER_INTERFACE(IID_IMAPIContainer, &this->m_xABContainer);
	REGISTER_INTERFACE(IID_IMAPIProp, &this->m_xABContainer);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xABContainer);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT	ECABContainer::Create(void* lpProvider, ULONG ulObjType, BOOL fModify, ECABContainer **lppABContainer)
{
	ECABContainer *lpABContainer = new ECABContainer(lpProvider, ulObjType, fModify, "IABContainer");
	return lpABContainer->QueryInterface(IID_ECABContainer, reinterpret_cast<void **>(lppABContainer));
}

HRESULT	ECABContainer::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk)
{
	HRESULT			hr = hrSuccess;

	if (lpiid == NULL)
		return MAPI_E_INVALID_PARAMETER;

	switch (ulPropTag) {
	case PR_CONTAINER_CONTENTS:
		if(*lpiid == IID_IMAPITable)
			hr = GetContentsTable(ulInterfaceOptions, (LPMAPITABLE*)lppUnk);
		else
			hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
		if (hr != hrSuccess)
			return hr;
		break;
	case PR_CONTAINER_HIERARCHY:
		if (*lpiid == IID_IMAPITable)
			hr = GetHierarchyTable(ulInterfaceOptions, (LPMAPITABLE*)lppUnk);
		else
			hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
		if (hr != hrSuccess)
			return hr;
		break;
	default:
		hr = ECABProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
		if (hr != hrSuccess)
			return hr;
		break;
	}
	return hr;
}

HRESULT ECABContainer::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	return Util::DoCopyTo(&IID_IABContainer, &this->m_xABContainer, ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECABContainer::CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	return Util::DoCopyProps(&IID_IABContainer, &this->m_xABContainer, lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT	ECABContainer::DefaultABContainerGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT		hr = hrSuccess;
	ECABProp*	lpProp = (ECABProp *)lpParam;

	LPSPropValue lpSectionUid = NULL;
	IProfSect *lpProfSect = NULL;

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_EMSMDB_SECTION_UID): {
		ECABLogon *lpLogon = (ECABLogon *)lpProvider;
		hr = lpLogon->m_lpMAPISup->OpenProfileSection(NULL, 0, &lpProfSect);
		if(hr != hrSuccess)
			goto exit;

		hr = HrGetOneProp(lpProfSect, PR_EMSMDB_SECTION_UID, &lpSectionUid);
		if(hr != hrSuccess)
			goto exit;

		lpsPropValue->ulPropTag = PR_EMSMDB_SECTION_UID;
		if ((hr = MAPIAllocateMore(sizeof(GUID), lpBase, (void **) &lpsPropValue->Value.bin.lpb)) != hrSuccess)
			goto exit;
		memcpy(lpsPropValue->Value.bin.lpb, lpSectionUid->Value.bin.lpb, sizeof(GUID));
		lpsPropValue->Value.bin.cb = sizeof(GUID);
		break;
		}
	case PROP_ID(PR_AB_PROVIDER_ID):
		lpsPropValue->ulPropTag = PR_AB_PROVIDER_ID;

		lpsPropValue->Value.bin.cb = sizeof(GUID);
		ECAllocateMore(sizeof(GUID), lpBase, (void**)&lpsPropValue->Value.bin.lpb);

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
			goto exit;

		if (PROP_TYPE(lpsPropValue->ulPropTag) == PT_UNICODE)
			strValue = convert_to<std::wstring>(lpsPropValue->Value.lpszW);
		else if (PROP_TYPE(lpsPropValue->ulPropTag) == PT_STRING8)
			strValue = convert_to<std::wstring>(lpsPropValue->Value.lpszA);
		else
			goto exit;
		
		if(strValue.compare( L"Global Address Book" ) == 0)
			lpszName = _("Global Address Book");
		else if(strValue.compare( L"Global Address Lists" ) == 0)
			lpszName = _("Global Address Lists");
		else if (strValue.compare( L"All Address Lists" ) == 0)
			lpszName = _("All Address Lists");

		if(lpszName) {
			if (PROP_TYPE(ulPropTag) == PT_UNICODE) {
				const std::wstring strTmp = convert_to<std::wstring>(lpszName);

				hr = MAPIAllocateMore((strTmp.size() + 1) * sizeof(WCHAR), lpBase, (void**)&lpsPropValue->Value.lpszW);
				if (hr != hrSuccess) 
					goto exit;

				wcscpy(lpsPropValue->Value.lpszW, strTmp.c_str());
			} else {
				const std::string strTmp = convert_to<std::string>(lpszName);

				hr = MAPIAllocateMore(strTmp.size() + 1, lpBase, (void**)&lpsPropValue->Value.lpszA);
				if (hr != hrSuccess) 
					goto exit;

				strcpy(lpsPropValue->Value.lpszA, strTmp.c_str());
			}
			lpsPropValue->ulPropTag = ulPropTag;
		}
		}
		break;
	default:
		hr = lpProp->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
		break;
	}

exit:
	if(lpProfSect)
		lpProfSect->Release();
	MAPIFreeBuffer(lpSectionUid);
	return hr;
}

HRESULT ECABContainer::TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType)
{
	HRESULT hr = hrSuccess;
	ULONG size = 0;

	switch(lpsPropValSrc->ulPropTag) {
		case PR_ACCOUNT_W:
		case PR_NORMALIZED_SUBJECT_W:
		case PR_DISPLAY_NAME_W:
		case PR_TRANSMITABLE_DISPLAY_NAME_W:
			{
				LPWSTR lpszW = NULL;
				if (strcmp(lpsPropValSrc->Value.lpszA, "Global Address Book" ) == 0)
					lpszW = _W("Global Address Book");
				else if (strcmp(lpsPropValSrc->Value.lpszA, "Global Address Lists" ) == 0)
					lpszW = _W("Global Address Lists");
				else if (strcmp(lpsPropValSrc->Value.lpszA, "All Address Lists" ) == 0)
					lpszW = _W("All Address Lists");
				else
					return MAPI_E_NOT_FOUND;
				size = (wcslen(lpszW) + 1) * sizeof(WCHAR);
				hr = MAPIAllocateMore(size, lpBase, (void **)&lpsPropValDst->Value.lpszW);
				if (hr != hrSuccess)
					return hr;

				memcpy(lpsPropValDst->Value.lpszW, lpszW, size);
				lpsPropValDst->ulPropTag = lpsPropValSrc->ulPropTag;
			}
			break;
		case PR_ACCOUNT_A:
		case PR_NORMALIZED_SUBJECT_A:
		case PR_DISPLAY_NAME_A:
		case PR_TRANSMITABLE_DISPLAY_NAME_A:
			{
				LPSTR lpszA = NULL;
				if (strcmp(lpsPropValSrc->Value.lpszA, "Global Address Book" ) == 0)
					lpszA = _A("Global Address Book");
				else if (strcmp(lpsPropValSrc->Value.lpszA, "Global Address Lists" ) == 0)
					lpszA = _A("Global Address Lists");
				else if (strcmp(lpsPropValSrc->Value.lpszA, "All Address Lists" ) == 0)
					lpszA = _A("All Address Lists");
				else
					return MAPI_E_NOT_FOUND;
				
				size = (strlen(lpszA) + 1) * sizeof(CHAR);
				hr = MAPIAllocateMore(size, lpBase, (void **)&lpsPropValDst->Value.lpszA);
				if (hr != hrSuccess)
					return hr;

				memcpy(lpsPropValDst->Value.lpszA, lpszA, size);
				lpsPropValDst->ulPropTag = lpsPropValSrc->ulPropTag;
			}
			break;
		default:
			hr = MAPI_E_NOT_FOUND;
			break;
	}
	return hr;
}

// IMAPIContainer
HRESULT ECABContainer::GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	HRESULT			hr = hrSuccess;
	ECMAPITable*	lpTable = NULL;
	WSTableView*	lpTableOps = NULL;
	SSortOrderSet	sSortByDisplayName;

	sSortByDisplayName.cSorts = 1;
	sSortByDisplayName.cCategories = 0;
	sSortByDisplayName.cExpanded = 0;
	sSortByDisplayName.aSort[0].ulPropTag = PR_DISPLAY_NAME;
	sSortByDisplayName.aSort[0].ulOrder = TABLE_SORT_ASCEND;

	hr = ECMAPITable::Create("AB Contents", NULL, 0, &lpTable);
	if(hr != hrSuccess)
		goto exit;

	hr = GetABStore()->m_lpTransport->HrOpenABTableOps(MAPI_MAILUSER, ulFlags, m_cbEntryId, m_lpEntryId, (ECABLogon*)this->lpProvider, &lpTableOps); // also MAPI_DISTLIST
	if(hr != hrSuccess)
		goto exit;

	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));
	if(hr != hrSuccess)
		goto exit;

	hr = lpTableOps->HrSortTable(&sSortByDisplayName);
	if(hr != hrSuccess)
		goto exit;

	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);

	AddChild(lpTable);

exit:
	if(lpTable)
		lpTable->Release();

	if(lpTableOps)
		lpTableOps->Release();

	return hr;
}

HRESULT ECABContainer::GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	HRESULT			hr = hrSuccess;
	ECMAPITable*	lpTable = NULL;
	WSTableView*	lpTableOps = NULL;

	hr = ECMAPITable::Create("AB hierarchy", GetABStore()->m_lpNotifyClient, ulFlags, &lpTable);

	if(hr != hrSuccess)
		goto exit;

	hr = GetABStore()->m_lpTransport->HrOpenABTableOps(MAPI_ABCONT, ulFlags, m_cbEntryId, m_lpEntryId, (ECABLogon*)this->lpProvider, &lpTableOps);

	if(hr != hrSuccess)
		goto exit;

	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));

	if(hr != hrSuccess)
		goto exit;

	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);

	AddChild(lpTable);

exit:
	if(lpTable)
		lpTable->Release();

	if(lpTableOps)
		lpTableOps->Release();

	return hr;
}

HRESULT ECABContainer::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
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
HRESULT ECABContainer::CreateEntry(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulCreateFlags, LPMAPIPROP* lppMAPIPropEntry)
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

HRESULT ECABContainer::ResolveNames(LPSPropTagArray lpPropTagArray, ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList)
{
	SizedSPropTagArray(11, sptaDefault) = {11, {PR_ADDRTYPE_A, PR_DISPLAY_NAME_A, PR_DISPLAY_TYPE, PR_EMAIL_ADDRESS_A, PR_SMTP_ADDRESS_A, PR_ENTRYID,
												PR_INSTANCE_KEY, PR_OBJECT_TYPE, PR_RECORD_KEY, PR_SEARCH_KEY, PR_EC_SENDAS_USER_ENTRYIDS}};

	SizedSPropTagArray(11, sptaDefaultUnicode) = {11, {PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_DISPLAY_TYPE, PR_EMAIL_ADDRESS_W, PR_SMTP_ADDRESS_W, PR_ENTRYID,
												       PR_INSTANCE_KEY, PR_OBJECT_TYPE, PR_RECORD_KEY, PR_SEARCH_KEY, PR_EC_SENDAS_USER_ENTRYIDS}};

	if (lpPropTagArray == NULL) {
		if(ulFlags & MAPI_UNICODE)
			lpPropTagArray = (LPSPropTagArray)&sptaDefaultUnicode;
		else
			lpPropTagArray = (LPSPropTagArray)&sptaDefault;
	}
	return ((ECABLogon*)lpProvider)->m_lpTransport->HrResolveNames(lpPropTagArray, ulFlags, lpAdrList, lpFlagList);
}

// Interface IUnknown
HRESULT ECABContainer::xABContainer::QueryInterface(REFIID refiid , void** lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->QueryInterface(refiid,lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

ULONG ECABContainer::xABContainer::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::AddRef", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	return pThis->AddRef();
}

ULONG ECABContainer::xABContainer::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::Release", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	ULONG ulRef = pThis->Release();
	TRACE_MAPI(TRACE_RETURN, "IABContainer::Release", "%d", ulRef);
	return ulRef;
}

// Interface IABContainer
HRESULT ECABContainer::xABContainer::CreateEntry(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulCreateFlags, LPMAPIPROP* lppMAPIPropEntry)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::CreateEntry", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->CreateEntry(cbEntryID, lpEntryID, ulCreateFlags, lppMAPIPropEntry);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::CreateEntry", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::CopyEntries(LPENTRYLIST lpEntries, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::CopyEntries", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->CopyEntries(lpEntries, ulUIParam, lpProgress, ulFlags);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::CopyEntries", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::DeleteEntries(LPENTRYLIST lpEntries, ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::DeleteEntries", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->DeleteEntries(lpEntries, ulFlags);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::DeleteEntries", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::ResolveNames(LPSPropTagArray lpPropTagArray, ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::ResolveNames", "\nlpPropTagArray:\t%s\nlpAdrList:\t%s", PropNameFromPropTagArray(lpPropTagArray).c_str(), AdrRowSetToString(lpAdrList, lpFlagList).c_str() );
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->ResolveNames(lpPropTagArray, ulFlags, lpAdrList, lpFlagList);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::ResolveNames", "%s, lpadrlist=\n%s", GetMAPIErrorDescription(hr).c_str(), AdrRowSetToString(lpAdrList, lpFlagList).c_str() );
	return hr;
}

// Interface IMAPIContainer
HRESULT ECABContainer::xABContainer::GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::GetContentsTable", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->GetContentsTable(ulFlags, lppTable);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::GetContentsTable", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::GetHierarchyTable", ""); 
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->GetHierarchyTable(ulFlags, lppTable);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::GetHierarchyTable", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::OpenEntry", "interface=%s", (lpInterface)?DBGGUIDToString(*lpInterface).c_str():"NULL");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::OpenEntry", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::SetSearchCriteria", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->SetSearchCriteria(lpRestriction, lpContainerList, ulSearchFlags);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::SetSearchCriteria", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::GetSearchCriteria", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr =pThis->GetSearchCriteria(ulFlags, lppRestriction, lppContainerList, lpulSearchState);;
	TRACE_MAPI(TRACE_RETURN, "IABContainer::GetSearchCriteria", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

// Interface IMAPIProp
HRESULT ECABContainer::xABContainer::GetLastError(HRESULT hError, ULONG ulFlags, LPMAPIERROR * lppMapiError)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::GetLastError", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->GetLastError(hError, ulFlags, lppMapiError);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::GetLastError", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::SaveChanges(ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::SaveChanges", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->SaveChanges(ulFlags);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::SaveChanges", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG FAR * lpcValues, LPSPropValue FAR * lppPropArray)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::GetProps", "%s", PropNameFromPropTagArray(lpPropTagArray).c_str());
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->GetProps(lpPropTagArray, ulFlags, lpcValues, lppPropArray);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::GetProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::GetPropList(ULONG ulFlags, LPSPropTagArray FAR * lppPropTagArray)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::GetPropList", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->GetPropList(ulFlags, lppPropTagArray);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::GetPropList", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::OpenProperty", "PropTag=%s, lpiid=%s", PropNameFromPropTag(ulPropTag).c_str(), DBGGUIDToString(*lpiid).c_str());
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::OpenProperty", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::SetProps", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->SetProps(cValues, lpPropArray, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::SetProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::DeleteProps", "%s", PropNameFromPropTagArray(lpPropTagArray).c_str());
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->DeleteProps(lpPropTagArray, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::DeleteProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::CopyTo", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->CopyTo(ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);;
	TRACE_MAPI(TRACE_RETURN, "IABContainer::CopyTo", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::CopyProps", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->CopyProps(lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::CopyProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::GetNamesFromIDs(LPSPropTagArray * pptaga, LPGUID lpguid, ULONG ulFlags, ULONG * pcNames, LPMAPINAMEID ** pppNames)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::GetNamesFromIDs", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->GetNamesFromIDs(pptaga, lpguid, ulFlags, pcNames, pppNames);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::GetIDsFromNames", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECABContainer::xABContainer::GetIDsFromNames(ULONG cNames, LPMAPINAMEID * ppNames, ULONG ulFlags, LPSPropTagArray * pptaga)
{
	TRACE_MAPI(TRACE_ENTRY, "IABContainer::GetIDsFromNames", "");
	METHOD_PROLOGUE_(ECABContainer, ABContainer);
	HRESULT hr = pThis->GetIDsFromNames(cNames, ppNames, ulFlags, pptaga);
	TRACE_MAPI(TRACE_RETURN, "IABContainer::GetIDsFromNames", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

