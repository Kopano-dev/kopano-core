/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <string>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "kcore.hpp"
#include "ECMAPIContainer.h"
#include "ECMAPITable.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include <kopano/mapiext.h>
#include <mapiutil.h>

using namespace KC;

ECMAPIContainer::ECMAPIContainer(ECMsgStore *lpMsgStore, ULONG obj_type,
    BOOL modify) :
	ECMAPIProp(lpMsgStore, obj_type, modify, nullptr)
{}

HRESULT	ECMAPIContainer::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMAPIContainer, this);
	REGISTER_INTERFACE2(ECMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMAPIContainer, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMAPIContainer::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    IMAPIProgress *lpProgress, const IID *lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyTo(&IID_IMAPIContainer, static_cast<IMAPIContainer *>(this), ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECMAPIContainer::CopyProps(const SPropTagArray *lpIncludeProps,
    unsigned int ulUIParam, IMAPIProgress *lpProgress, const IID *lpInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyProps(&IID_IMAPIContainer, static_cast<IMAPIContainer *>(this), lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECMAPIContainer::GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableView> lpTableOps;

	auto hr = ECMAPITable::Create("Contents table", GetMsgStore()->m_lpNotifyClient, 0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = GetMsgStore()->lpTransport->HrOpenTableOps(MAPI_MESSAGE,
	     ulFlags & (MAPI_UNICODE | SHOW_SOFT_DELETES | MAPI_ASSOCIATED | EC_TABLE_NOCAP),
	     m_cbEntryId, m_lpEntryId, GetMsgStore(), &~lpTableOps);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, reinterpret_cast<void **>(lppTable));
	AddChild(lpTable);
	return hr;
}

HRESULT ECMAPIContainer::GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableView> lpTableOps;

	auto hr = ECMAPITable::Create("Hierarchy table", GetMsgStore()->m_lpNotifyClient, 0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = GetMsgStore()->lpTransport->HrOpenTableOps(MAPI_FOLDER,
	     ulFlags & (MAPI_UNICODE | SHOW_SOFT_DELETES | CONVENIENT_DEPTH),
	     m_cbEntryId, m_lpEntryId, GetMsgStore(), &~lpTableOps);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, reinterpret_cast<void **>(lppTable));
	AddChild(lpTable);
	return hr;
}

HRESULT ECMAPIContainer::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	return GetMsgStore()->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
}
