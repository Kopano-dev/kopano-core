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

#include <mapicode.h>
#include <mapix.h>

#include "ClientUtil.h"
#include "ECNotifyMaster.h"
#include "ECSessionGroupManager.h"
#include "SessionGroupData.h"
#include "WSTransport.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


SessionGroupData::SessionGroupData(ECSESSIONGROUPID ecSessionGroupId, ECSessionGroupInfo *lpInfo, const sGlobalProfileProps &sProfileProps)
{
	m_ecSessionGroupId = ecSessionGroupId;

	if (lpInfo) {
		m_ecSessionGroupInfo.strServer = lpInfo->strServer;
		m_ecSessionGroupInfo.strProfile = lpInfo->strProfile;
	}

	m_lpNotifyMaster = NULL;
	m_sProfileProps = sProfileProps;
	m_cRef = 0;

	pthread_mutexattr_init(&m_hMutexAttrib);
	pthread_mutexattr_settype(&m_hMutexAttrib, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_hMutex, &m_hMutexAttrib);
	pthread_mutex_init(&m_hRefMutex, &m_hMutexAttrib);
}

SessionGroupData::~SessionGroupData(void)
{
	if (m_lpNotifyMaster)
		m_lpNotifyMaster->Release();

	pthread_mutex_destroy(&m_hMutex);
	pthread_mutex_destroy(&m_hRefMutex);
	pthread_mutexattr_destroy(&m_hMutexAttrib);
}

HRESULT SessionGroupData::Create(ECSESSIONGROUPID ecSessionGroupId, ECSessionGroupInfo *lpInfo, const sGlobalProfileProps &sProfileProps, SessionGroupData **lppData)
{
	HRESULT hr = hrSuccess;
	SessionGroupData *lpData = NULL;

	lpData = new SessionGroupData(ecSessionGroupId, lpInfo, sProfileProps);
	lpData->AddRef();

	*lppData = lpData;

	return hr;
}


HRESULT SessionGroupData::GetOrCreateNotifyMaster(ECNotifyMaster **lppMaster)
{
	HRESULT hr = hrSuccess;

	pthread_mutex_lock(&m_hMutex);

	if (!m_lpNotifyMaster)
		hr = ECNotifyMaster::Create(this, &m_lpNotifyMaster);

	pthread_mutex_unlock(&m_hMutex);

	*lppMaster = m_lpNotifyMaster;

	return hr;
}

HRESULT SessionGroupData::GetTransport(WSTransport **lppTransport)
{
	HRESULT hr;
	WSTransport *lpTransport = NULL;

	hr = WSTransport::Create(MDB_NO_DIALOG, &lpTransport);
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
	ULONG cRef;
    
	pthread_mutex_lock(&m_hRefMutex);
	cRef = ++m_cRef;
	pthread_mutex_unlock(&m_hRefMutex);
	return cRef;
}

ULONG SessionGroupData::Release()
{
	ULONG cRef;
    
	pthread_mutex_lock(&m_hRefMutex);
	cRef = --m_cRef;
	pthread_mutex_unlock(&m_hRefMutex);
	return cRef;
}

BOOL SessionGroupData::IsOrphan()
{
    return m_cRef == 0;
}
