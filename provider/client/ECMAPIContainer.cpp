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
 */
#include <kopano/platform.h>
#include <kopano/ECInterfaceDefs.h>
#include "kcore.hpp"
#include "ECMAPIContainer.h"
#include "ECMAPITable.h"
#include "Mem.h"

#include <kopano/ECGuid.h>
#include <kopano/ECDebug.h>

//#include <edkmdb.h>
#include <kopano/mapiext.h>
#include <mapiutil.h>

ECMAPIContainer::ECMAPIContainer(ECMsgStore *lpMsgStore, ULONG ulObjType,
    BOOL fModify, const char *szClassName) :
	ECMAPIProp(lpMsgStore, ulObjType, fModify, NULL, szClassName)
{

}

HRESULT	ECMAPIContainer::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMAPIContainer, this);
	REGISTER_INTERFACE2(ECMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMAPIContainer, &this->m_xMAPIContainer);
	REGISTER_INTERFACE2(IMAPIProp, &this->m_xMAPIContainer);
	REGISTER_INTERFACE2(IUnknown, &this->m_xMAPIContainer);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMAPIContainer::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems)
{
	return Util::DoCopyTo(&IID_IMAPIContainer, &this->m_xMAPIContainer, ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECMAPIContainer::CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems)
{
	return Util::DoCopyProps(&IID_IMAPIContainer, &this->m_xMAPIContainer, lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECMAPIContainer::SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMAPIContainer::GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMAPIContainer::GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	HRESULT			hr = hrSuccess;
	ECMAPITable*	lpTable = NULL;
	WSTableView*	lpTableOps = NULL;
	std::string		strName = "Contents table";

#ifdef DEBUG
	{
		LPSPropValue lpDisplay;
		HrGetOneProp(&this->m_xMAPIProp, PR_DISPLAY_NAME_A, &lpDisplay);
		if (lpDisplay != nullptr)
			strName = lpDisplay->Value.lpszA;
	}
#endif

	hr = ECMAPITable::Create(strName.c_str(), this->GetMsgStore()->m_lpNotifyClient, 0, &lpTable);

	if(hr != hrSuccess)
		goto exit;

	hr = this->GetMsgStore()->lpTransport->HrOpenTableOps(MAPI_MESSAGE, (ulFlags&(MAPI_UNICODE|SHOW_SOFT_DELETES|MAPI_ASSOCIATED|EC_TABLE_NOCAP)), m_cbEntryId, m_lpEntryId, this->GetMsgStore(), &lpTableOps);

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

HRESULT ECMAPIContainer::GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	HRESULT			hr = hrSuccess;
	ECMAPITable*	lpTable = NULL;
	WSTableView*	lpTableOps = NULL;
	SPropTagArray	sPropTagArray;
	ULONG			cValues = 0;
	LPSPropValue	lpPropArray = NULL; 
	std::string		strName = "Hierarchy table";
	
#ifdef DEBUG
	{
		LPSPropValue lpDisplay;
		HrGetOneProp(&this->m_xMAPIProp, PR_DISPLAY_NAME_A, &lpDisplay);
		if (lpDisplay != nullptr)
			strName = lpDisplay->Value.lpszA;
	}
#endif

	sPropTagArray.aulPropTag[0] = PR_FOLDER_TYPE;
	sPropTagArray.cValues = 1;

	hr = GetProps(&sPropTagArray, 0, &cValues, &lpPropArray);
	if(FAILED(hr))
		goto exit;
	
	// block for searchfolders
	if(lpPropArray && lpPropArray[0].ulPropTag == PR_FOLDER_TYPE && lpPropArray[0].Value.l == FOLDER_SEARCH)
	{		
		hr= MAPI_E_NO_SUPPORT;
		goto exit;
	}

	hr = ECMAPITable::Create(strName.c_str(), this->GetMsgStore()->m_lpNotifyClient, 0, &lpTable);

	if(hr != hrSuccess)
		goto exit;

	hr = this->GetMsgStore()->lpTransport->HrOpenTableOps(MAPI_FOLDER, ulFlags & (MAPI_UNICODE | SHOW_SOFT_DELETES | CONVENIENT_DEPTH), m_cbEntryId, m_lpEntryId, this->GetMsgStore(), &lpTableOps);

	if(hr != hrSuccess)
		goto exit;

	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));

	if(hr != hrSuccess)
		goto exit;

	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);

	AddChild(lpTable);

exit:
	if(lpPropArray)
		ECFreeBuffer(lpPropArray);

	if(lpTable)
		lpTable->Release();

	if(lpTableOps)
		lpTableOps->Release();

	return hr;
}

HRESULT ECMAPIContainer::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
{
	return this->GetMsgStore()->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
}

// From IMAPIContainer
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, GetContentsTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, GetHierarchyTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, OpenEntry, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, SetSearchCriteria, (LPSRestriction, lpRestriction), (LPENTRYLIST, lpContainerList), (ULONG, ulSearchFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, GetSearchCriteria, (ULONG, ulFlags), (LPSRestriction *, lppRestriction), (LPENTRYLIST *, lppContainerList), (ULONG *, lpulSearchState))

// From IUnknown
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, Release, (void))

// From IMAPIProp
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, GetLastError, (HRESULT, hError), (ULONG, ulFlags), (LPMAPIERROR *, lppMapiError))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, SaveChanges, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, GetProps, (LPSPropTagArray, lpPropTagArray), (ULONG, ulFlags), (ULONG *, lpcValues, LPSPropValue *, lppPropArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, GetPropList, (ULONG, ulFlags), (LPSPropTagArray *, lppPropTagArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, OpenProperty, (ULONG, ulPropTag), (LPCIID, lpiid), (ULONG, ulInterfaceOptions), (ULONG, ulFlags), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, SetProps, (ULONG, cValues), (LPSPropValue, lpPropArray), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, DeleteProps, (LPSPropTagArray, lpPropTagArray), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, CopyTo, (ULONG, ciidExclude), (LPCIID, rgiidExclude), (LPSPropTagArray, lpExcludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, CopyProps, (LPSPropTagArray, lpIncludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, GetNamesFromIDs, (LPSPropTagArray *, pptaga), (LPGUID, lpguid), (ULONG, ulFlags), (ULONG *, pcNames, LPMAPINAMEID **, pppNames))
DEF_HRMETHOD1(TRACE_MAPI, ECMAPIContainer, MAPIContainer, GetIDsFromNames, (ULONG, cNames), (LPMAPINAMEID *, ppNames), (ULONG, ulFlags), (LPSPropTagArray *, pptaga))
