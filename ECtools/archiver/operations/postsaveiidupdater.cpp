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
#include "postsaveiidupdater.h"
#include "instanceidmapper.h"

namespace za { namespace operations {

TaskBase::TaskBase(const AttachPtr &ptrSourceAttach, const MessagePtr &ptrDestMsg, ULONG ulDestAttachIdx)
: m_ptrSourceAttach(ptrSourceAttach)
, m_ptrDestMsg(ptrDestMsg)
, m_ulDestAttachIdx(ulDestAttachIdx)
{ }

HRESULT TaskBase::Execute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper) {
	HRESULT hr;
	SPropValuePtr ptrSourceServerUID;
	ULONG cbSourceInstanceID = 0;
	EntryIdPtr ptrSourceInstanceID;
	MAPITablePtr ptrTable;
	SRowSetPtr ptrRows;
	AttachPtr ptrAttach;
	SPropValuePtr ptrDestServerUID;
	ULONG cbDestInstanceID = 0;
	EntryIdPtr ptrDestInstanceID;

	SizedSPropTagArray(1, sptaTableProps) = {1, {PR_ATTACH_NUM}};
	
	hr = GetUniqueIDs(m_ptrSourceAttach, &ptrSourceServerUID, &cbSourceInstanceID, &ptrSourceInstanceID);
	if (hr != hrSuccess)
		return hr;
	hr = m_ptrDestMsg->GetAttachmentTable(MAPI_DEFERRED_ERRORS, &ptrTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SetColumns((LPSPropTagArray)&sptaTableProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SeekRow(BOOKMARK_BEGINNING, m_ulDestAttachIdx, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->QueryRows(1, 0, &ptrRows);
	if (hr != hrSuccess)
		return hr;
	if (ptrRows.empty())
		return MAPI_E_NOT_FOUND;
	hr = m_ptrDestMsg->OpenAttach(ptrRows[0].lpProps[0].Value.ul, &ptrAttach.iid, 0, &ptrAttach);
	if (hr != hrSuccess)
		return hr;
	hr = GetUniqueIDs(ptrAttach, &ptrDestServerUID, &cbDestInstanceID, &ptrDestInstanceID);
	if (hr != hrSuccess)
		return hr;
	return DoExecute(ulPropTag, ptrMapper, ptrSourceServerUID->Value.bin,
		cbSourceInstanceID, ptrSourceInstanceID,
		ptrDestServerUID->Value.bin, cbDestInstanceID,
		ptrDestInstanceID);
}

HRESULT TaskBase::GetUniqueIDs(IAttach *lpAttach, LPSPropValue *lppServerUID, ULONG *lpcbInstanceID, LPENTRYID *lppInstanceID)
{
	HRESULT hr;
	SPropValuePtr ptrServerUID;
	ECSingleInstancePtr ptrInstance;
	ULONG cbInstanceID = 0;
	EntryIdPtr ptrInstanceID;

	hr = HrGetOneProp(lpAttach, PR_EC_SERVER_UID, &ptrServerUID);
	if (hr != hrSuccess)
		return hr;
	hr = lpAttach->QueryInterface(ptrInstance.iid, &ptrInstance);
	if (hr != hrSuccess)
		return hr;
	hr = ptrInstance->GetSingleInstanceId(&cbInstanceID, &ptrInstanceID);
	if (hr != hrSuccess)
		return hr;

	*lppServerUID = ptrServerUID.release();
	*lpcbInstanceID = cbInstanceID;
	*lppInstanceID = ptrInstanceID.release();
	return hrSuccess;
}

TaskMapInstanceId::TaskMapInstanceId(const AttachPtr &ptrSourceAttach, const MessagePtr &ptrDestMsg, ULONG ulDestAttachNum)
: TaskBase(ptrSourceAttach, ptrDestMsg, ulDestAttachNum)
{ }

HRESULT TaskMapInstanceId::DoExecute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper, const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID) {
	return ptrMapper->SetMappedInstances(ulPropTag, sourceServerUID, cbSourceInstanceID, lpSourceInstanceID, destServerUID, cbDestInstanceID, lpDestInstanceID);
}

TaskVerifyAndUpdateInstanceId::TaskVerifyAndUpdateInstanceId(const AttachPtr &ptrSourceAttach, const MessagePtr &ptrDestMsg, ULONG ulDestAttachNum, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID)
: TaskBase(ptrSourceAttach, ptrDestMsg, ulDestAttachNum)
, m_destInstanceID(cbDestInstanceID, lpDestInstanceID)
{ }

HRESULT TaskVerifyAndUpdateInstanceId::DoExecute(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper, const SBinary &sourceServerUID, ULONG cbSourceInstanceID, LPENTRYID lpSourceInstanceID, const SBinary &destServerUID, ULONG cbDestInstanceID, LPENTRYID lpDestInstanceID) {
	HRESULT hr = hrSuccess;
	SBinary lhs, rhs;
	lhs.cb = cbDestInstanceID;
	lhs.lpb = (LPBYTE)lpDestInstanceID;
	rhs.cb = m_destInstanceID.size();
	rhs.lpb = m_destInstanceID;

	if (Util::CompareSBinary(lhs, rhs) != 0)
		hr = ptrMapper->SetMappedInstances(ulPropTag, sourceServerUID, cbSourceInstanceID, lpSourceInstanceID, destServerUID, cbDestInstanceID, lpDestInstanceID);

	return hr;
}

PostSaveInstanceIdUpdater::PostSaveInstanceIdUpdater(ULONG ulPropTag, const InstanceIdMapperPtr &ptrMapper, const TaskList &lstDeferred)
: m_ulPropTag(ulPropTag)
, m_ptrMapper(ptrMapper)
, m_lstDeferred(lstDeferred)
{ }

HRESULT PostSaveInstanceIdUpdater::Execute()
{
	HRESULT hr = hrSuccess;
	bool bFailure = false;

	for (const auto &i : m_lstDeferred) {
		hr = i->Execute(m_ulPropTag, m_ptrMapper);
		if (hr != hrSuccess)
			bFailure = true;
	}

	return bFailure ? MAPI_W_ERRORS_RETURNED : hrSuccess;
}

}} // namespace operations, za
