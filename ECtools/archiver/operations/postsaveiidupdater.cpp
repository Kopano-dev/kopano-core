/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <algorithm>
#include <list>
#include <kopano/platform.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "postsaveiidupdater.h"
#include "instanceidmapper.h"

namespace KC { namespace operations {

TaskBase::TaskBase(IAttach *sa, IMessage *dst, unsigned int dst_at_idx) :
	m_ptrSourceAttach(sa), m_ptrDestMsg(dst), m_ulDestAttachIdx(dst_at_idx)
{ }

HRESULT TaskBase::Execute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper) {
	memory_ptr<SPropValue> ptrSourceServerUID, ptrDestServerUID;
	memory_ptr<ENTRYID> ptrSourceInstanceID, ptrDestInstanceID;
	unsigned int cbSourceInstanceID = 0, cbDestInstanceID = 0;
	static constexpr const SizedSPropTagArray(1, sptaTableProps) = {1, {PR_ATTACH_NUM}};

	auto hr = GetUniqueIDs(m_ptrSourceAttach, &~ptrSourceServerUID, &cbSourceInstanceID, &~ptrSourceInstanceID);
	if (hr != hrSuccess)
		return hr;
	object_ptr<IMAPITable> ptrTable;
	hr = m_ptrDestMsg->GetAttachmentTable(MAPI_DEFERRED_ERRORS, &~ptrTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SetColumns(sptaTableProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SeekRow(BOOKMARK_BEGINNING, m_ulDestAttachIdx, NULL);
	if (hr != hrSuccess)
		return hr;
	rowset_ptr ptrRows;
	hr = ptrTable->QueryRows(1, 0, &~ptrRows);
	if (hr != hrSuccess)
		return hr;
	if (ptrRows.empty())
		return MAPI_E_NOT_FOUND;
	object_ptr<IAttach> ptrAttach;
	hr = m_ptrDestMsg->OpenAttach(ptrRows[0].lpProps[0].Value.ul, &iid_of(ptrAttach), 0, &~ptrAttach);
	if (hr != hrSuccess)
		return hr;
	hr = GetUniqueIDs(ptrAttach, &~ptrDestServerUID, &cbDestInstanceID, &~ptrDestInstanceID);
	if (hr != hrSuccess)
		return hr;
	return DoExecute(ulPropTag, ptrMapper, ptrSourceServerUID->Value.bin,
		cbSourceInstanceID, ptrSourceInstanceID,
		ptrDestServerUID->Value.bin, cbDestInstanceID,
		ptrDestInstanceID);
}

HRESULT TaskBase::GetUniqueIDs(IAttach *lpAttach, LPSPropValue *lppServerUID, ULONG *lpcbInstanceID, LPENTRYID *lppInstanceID)
{
	memory_ptr<SPropValue> ptrServerUID;
	object_ptr<IECSingleInstance> ptrInstance;
	ULONG cbInstanceID = 0;
	memory_ptr<ENTRYID> ptrInstanceID;

	auto hr = HrGetOneProp(lpAttach, PR_EC_SERVER_UID, &~ptrServerUID);
	if (hr != hrSuccess)
		return hr;
	hr = lpAttach->QueryInterface(iid_of(ptrInstance), &~ptrInstance);
	if (hr != hrSuccess)
		return hr;
	hr = ptrInstance->GetSingleInstanceId(&cbInstanceID, &~ptrInstanceID);
	if (hr != hrSuccess)
		return hr;

	*lppServerUID = ptrServerUID.release();
	*lpcbInstanceID = cbInstanceID;
	*lppInstanceID = ptrInstanceID.release();
	return hrSuccess;
}

TaskMapInstanceId::TaskMapInstanceId(IAttach *sa, IMessage *dst, unsigned int dst_at_num) :
	TaskBase(sa, dst, dst_at_num)
{ }

HRESULT TaskMapInstanceId::DoExecute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper, const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID) {
	return ptrMapper->SetMappedInstances(ulPropTag, sourceServerUID, cbSourceInstanceID, lpSourceInstanceID, destServerUID, cbDestInstanceID, lpDestInstanceID);
}

TaskVerifyAndUpdateInstanceId::TaskVerifyAndUpdateInstanceId(IAttach *sa,
    IMessage *dst, unsigned int dst_at_num, unsigned int di_size, ENTRYID *di_id) :
	TaskBase(sa, dst, dst_at_num), m_destInstanceID(di_size, di_id)
{ }

HRESULT TaskVerifyAndUpdateInstanceId::DoExecute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper, const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID) {
	SBinary lhs, rhs;
	lhs.cb = cbDestInstanceID;
	lhs.lpb = (LPBYTE)lpDestInstanceID;
	rhs.cb = m_destInstanceID.size();
	rhs.lpb = m_destInstanceID;

	if (Util::CompareSBinary(lhs, rhs) == 0)
		return hrSuccess;
	return ptrMapper->SetMappedInstances(ulPropTag, sourceServerUID,
	       cbSourceInstanceID, lpSourceInstanceID, destServerUID,
	       cbDestInstanceID, lpDestInstanceID);
}

PostSaveInstanceIdUpdater::PostSaveInstanceIdUpdater(unsigned int tag,
    const InstanceIdMapperPtr &m, const std::list<TaskPtr> &d) :
	m_ulPropTag(tag), m_ptrMapper(m), m_lstDeferred(d)
{ }

HRESULT PostSaveInstanceIdUpdater::Execute()
{
	return std::any_of(m_lstDeferred.begin(), m_lstDeferred.end(),
	       [&](const auto &i) { return i->Execute(m_ulPropTag, m_ptrMapper) != hrSuccess; }) ?
	       MAPI_W_ERRORS_RETURNED : hrSuccess;
}

}} /* namespace */
