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

#ifndef ECSESSIONGROUPDATA_H
#define ECSESSIONGROUPDATA_H

#include <pthread.h>

#include <mapispi.h>

#include <kopano/kcodes.h>
#include "ClientUtil.h"

class ECNotifyMaster;
class WSTransport;

class ECSessionGroupInfo {
public:
	std::string strServer;
	std::string strProfile;

	ECSessionGroupInfo()
		: strServer(), strProfile()
	{
	}

	ECSessionGroupInfo(const std::string &strServer, const std::string &strProfile)
		: strServer(strServer), strProfile(strProfile)
	{
	}
};

static inline bool operator==(const ECSessionGroupInfo &a, const ECSessionGroupInfo &b)
{
	return	(a.strServer.compare(b.strServer) == 0) &&
			(a.strProfile.compare(b.strProfile) == 0);
}

static inline bool operator<(const ECSessionGroupInfo &a, const ECSessionGroupInfo &b)
{
	return	(a.strServer.compare(b.strServer) < 0) ||
			((a.strServer.compare(b.strServer) == 0) && (a.strProfile.compare(b.strProfile) < 0));
}

class SessionGroupData
{
private:
	/* SessionGroup ID to which this data belongs */
	ECSESSIONGROUPID	m_ecSessionGroupId;
	ECSessionGroupInfo	m_ecSessionGroupInfo;

	/* Notification information */
	ECNotifyMaster*		m_lpNotifyMaster;

	/* Mutex */
	pthread_mutex_t		m_hMutex;
	pthread_mutexattr_t	m_hMutexAttrib;
	sGlobalProfileProps m_sProfileProps;

	/* Refcounting */
	pthread_mutex_t		m_hRefMutex;
	ULONG				m_cRef;

public:
	SessionGroupData(ECSESSIONGROUPID ecSessionGroupId, ECSessionGroupInfo *lpInfo, const sGlobalProfileProps &sProfileProps);
	~SessionGroupData(void);

	static HRESULT Create(ECSESSIONGROUPID ecSessionGroupId, ECSessionGroupInfo *lpInfo, const sGlobalProfileProps &sProfileProps, SessionGroupData **lppData);

	HRESULT GetOrCreateNotifyMaster(ECNotifyMaster **lppMaster);
	HRESULT GetTransport(WSTransport **lppTransport);
	
	ULONG AddRef();
	ULONG Release();
	
	BOOL IsOrphan();

	ECSESSIONGROUPID GetSessionGroupId();
};

#endif /* ECSESSIONGROUPDATA_H */
