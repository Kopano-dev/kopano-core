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
#include <mapicode.h>
#include <mapix.h>

#include "ClientUtil.h"
#include "ECNotifyMaster.h"
#include "ECSessionGroupManager.h"
#include "SessionGroupData.h"
#include "WSTransport.h"

SessionGroupData::SessionGroupData(ECSESSIONGROUPID ecSessionGroupId,
    ECSessionGroupInfo *lpInfo, const sGlobalProfileProps &sProfileProps) :
	m_ecSessionGroupId(ecSessionGroupId), m_sProfileProps(sProfileProps)
{
	if (lpInfo == nullptr)
		return;
	m_ecSessionGroupInfo.strServer = lpInfo->strServer;
	m_ecSessionGroupInfo.strProfile = lpInfo->strProfile;
}

HRESULT SessionGroupData::Create(ECSESSIONGROUPID ecSessionGroupId, ECSessionGroupInfo *lpInfo, const sGlobalProfileProps &sProfileProps, SessionGroupData **lppData)
{
	return alloc_wrap<SessionGroupData>(ecSessionGroupId, lpInfo,
	       sProfileProps).put(lppData);
}

HRESULT SessionGroupData::GetOrCreateNotifyMaster(ECNotifyMaster **lppMaster)
{
	HRESULT hr = hrSuccess;
	scoped_rlock lock(m_hMutex);

	if (!m_lpNotifyMaster)
		hr = ECNotifyMaster::Create(this, &~m_lpNotifyMaster);
	*lppMaster = m_lpNotifyMaster.get();
	return hr;
}

HRESULT SessionGroupData::GetTransport(WSTransport **lppTransport)
{
	WSTransport *lpTransport = NULL;
	auto hr = WSTransport::Create(MDB_NO_DIALOG, &lpTransport);
	if (hr != hrSuccess) 
		return hr;

	hr = lpTransport->HrLogon(m_sProfileProps);
	if (hr != hrSuccess) 
		return hr;

	// Since we are doing request that take max EC_SESSION_KEEPALIVE_TIME, set timeout to that plus 10 seconds
	lpTransport->HrSetRecvTimeout(EC_SESSION_KEEPALIVE_TIME + 10);

	*lppTransport = lpTransport;
	return hrSuccess;
}

ECSESSIONGROUPID SessionGroupData::GetSessionGroupId()
{
	return m_ecSessionGroupId;
}

ULONG SessionGroupData::AddRef()
{
	return ++m_cRef;
}

ULONG SessionGroupData::Release()
{
	return --m_cRef;
}

BOOL SessionGroupData::IsOrphan()
{
    return m_cRef == 0;
}
