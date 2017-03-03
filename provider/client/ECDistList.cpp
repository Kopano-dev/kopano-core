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
#include <kopano/ECInterfaceDefs.h>
#include "kcore.hpp"
#include "ECDistList.h"

#include "Mem.h"

#include <kopano/ECGuid.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECDebug.h>

#include "ECMAPITable.h"

ECDistList::ECDistList(void* lpProvider, BOOL fModify) : ECABContainer(lpProvider, MAPI_DISTLIST, fModify, "IDistList")
{
	// since we have no OpenProperty / abLoadProp, remove the 8k prop limit
	this->m_ulMaxPropSize = 0;
}

HRESULT	ECDistList::QueryInterface(REFIID refiid, void **lppInterface) 
{
	REGISTER_INTERFACE2(ECDistList, this);
	REGISTER_INTERFACE2(ECABContainer, this);
	REGISTER_INTERFACE2(ECABProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IDistList, &this->m_xDistList);
	REGISTER_INTERFACE(IID_IABContainer, &this->m_xDistList);
	REGISTER_INTERFACE2(IMAPIProp, &this->m_xDistList);
	REGISTER_INTERFACE2(IUnknown, &this->m_xDistList);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECDistList::Create(void* lpProvider, BOOL fModify, ECDistList** lppDistList)
{

	HRESULT hr = hrSuccess;
	auto lpDistList = new(std::nothrow) ECDistList(lpProvider, fModify);
	if (lpDistList == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	hr = lpDistList->QueryInterface(IID_ECDistList, (void **)lppDistList);

	if(hr != hrSuccess)
		delete lpDistList;

	return hr;
}

HRESULT ECDistList::TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType)
{
	return MAPI_E_NOT_FOUND;
}

HRESULT ECDistList::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	if (lpiid == NULL)
		return MAPI_E_INVALID_PARAMETER;
	return ECABProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
}

HRESULT ECDistList::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return this->GetABStore()->m_lpMAPISup->DoCopyTo(&IID_IDistList, &this->m_xDistList, ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECDistList::CopyProps(const SPropTagArray *lpIncludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return this->GetABStore()->m_lpMAPISup->DoCopyProps(&IID_IDistList, &this->m_xDistList, lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

// Interface IUnknown
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, QueryInterface, (REFIID, refiid), (void**, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECDistList, DistList, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECDistList, DistList, Release, (void))

// Interface IABContainer
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, CreateEntry, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG, ulCreateFlags), (LPMAPIPROP*, lppMAPIPropEntry))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, CopyEntries, (LPENTRYLIST, lpEntries), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, DeleteEntries, (LPENTRYLIST, lpEntries), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, ResolveNames, (const SPropTagArray *, lpPropTagArray), (ULONG, ulFlags), (LPADRLIST, lpAdrList), (LPFlagList, lpFlagList))

// Interface IMAPIContainer
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, GetContentsTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, GetHierarchyTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, OpenEntry, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, SetSearchCriteria, (LPSRestriction, lpRestriction), (LPENTRYLIST, lpContainerList), (ULONG, ulSearchFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, GetSearchCriteria, (ULONG, ulFlags), (LPSRestriction *, lppRestriction), (LPENTRYLIST *, lppContainerList), (ULONG *, lpulSearchState))

// Interface IMAPIProp
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, GetLastError, (HRESULT, hError), (ULONG, ulFlags), (LPMAPIERROR *, lppMapiError))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, SaveChanges, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, GetProps, (const SPropTagArray *, lpPropTagArray), (ULONG, ulFlags), (ULONG *, lpcValues), (SPropValue **, lppPropArray))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, GetPropList, (ULONG, ulFlags), (LPSPropTagArray *, lppPropTagArray))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, OpenProperty, (ULONG, ulPropTag), (LPCIID, lpiid), (ULONG, ulInterfaceOptions), (ULONG, ulFlags), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, SetProps, (ULONG, cValues), (const SPropValue *, lpPropArray), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, DeleteProps, (const SPropTagArray *, lpPropTagArray), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, CopyTo, (ULONG, ciidExclude), (LPCIID, rgiidExclude), (const SPropTagArray *, lpExcludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (void *, lpDestObj), (ULONG, ulFlags), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, CopyProps, (const SPropTagArray *, lpIncludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (void *, lpDestObj), (ULONG, ulFlags), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, GetNamesFromIDs, (LPSPropTagArray *, pptaga), (LPGUID, lpguid), (ULONG, ulFlags), (ULONG *, pcNames), (LPMAPINAMEID **, pppNames))
DEF_HRMETHOD1(TRACE_MAPI, ECDistList, DistList, GetIDsFromNames, (ULONG, cNames), (LPMAPINAMEID *, ppNames), (ULONG, ulFlags), (LPSPropTagArray *, pptaga))
