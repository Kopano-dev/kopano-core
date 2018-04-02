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

#include <kopano/zcdefs.h>
#include <atomic>
#include <mutex>
#include <mapispi.h>

#include <kopano/kcodes.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "ClientUtil.h"

using namespace KC;
class ECNotifyMaster;
class WSTransport;

class ECSessionGroupInfo _kc_final {
public:
	std::string strServer, strProfile;

	ECSessionGroupInfo()
		: strServer(), strProfile()
	{
	}

	ECSessionGroupInfo(const std::string &server, const std::string &profile) :
		strServer(server), strProfile(profile)
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

class SessionGroupData _kc_final {
private:
	/* SessionGroup ID to which this data belongs */
	ECSESSIONGROUPID	m_ecSessionGroupId;
	ECSessionGroupInfo	m_ecSessionGroupInfo;

	/* Notification information */
	KC::object_ptr<ECNotifyMaster> m_lpNotifyMaster;

	/* Mutex */
	std::recursive_mutex m_hMutex;
	sGlobalProfileProps m_sProfileProps;

	/* Refcounting */
	std::atomic<unsigned int> m_cRef{0};

public:
	SessionGroupData(ECSESSIONGROUPID ecSessionGroupId, ECSessionGroupInfo *lpInfo, const sGlobalProfileProps &sProfileProps);
	static HRESULT Create(ECSESSIONGROUPID ecSessionGroupId, ECSessionGroupInfo *lpInfo, const sGlobalProfileProps &sProfileProps, SessionGroupData **lppData);

	HRESULT GetOrCreateNotifyMaster(ECNotifyMaster **lppMaster);
	HRESULT GetTransport(WSTransport **lppTransport);
	
	ULONG AddRef();
	ULONG Release();
	
	BOOL IsOrphan();

	ECSESSIONGROUPID GetSessionGroupId();
	ALLOC_WRAP_FRIEND;
};

#endif /* ECSESSIONGROUPDATA_H */
