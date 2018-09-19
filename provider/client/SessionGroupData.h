/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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

class ECNotifyMaster;
class WSTransport;

class ECSessionGroupInfo final {
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

class SessionGroupData final {
private:
	/* SessionGroup ID to which this data belongs */
	KC::ECSESSIONGROUPID m_ecSessionGroupId;
	ECSessionGroupInfo	m_ecSessionGroupInfo;

	/* Notification information */
	KC::object_ptr<ECNotifyMaster> m_lpNotifyMaster;

	/* Mutex */
	std::recursive_mutex m_hMutex;
	sGlobalProfileProps m_sProfileProps;

	/* Refcounting */
	std::atomic<unsigned int> m_cRef{0};

public:
	SessionGroupData(KC::ECSESSIONGROUPID, ECSessionGroupInfo *, const sGlobalProfileProps &);
	static HRESULT Create(KC::ECSESSIONGROUPID, ECSessionGroupInfo *, const sGlobalProfileProps &, SessionGroupData **out);
	HRESULT GetOrCreateNotifyMaster(ECNotifyMaster **lppMaster);
	HRESULT GetTransport(WSTransport **lppTransport);
	ULONG AddRef();
	ULONG Release();
	BOOL IsOrphan();
	KC::ECSESSIONGROUPID GetSessionGroupId();
	ALLOC_WRAP_FRIEND;
};

#endif /* ECSESSIONGROUPDATA_H */
