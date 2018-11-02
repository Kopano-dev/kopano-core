/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSYNCSETTINGS_INCLUDED
#define ECSYNCSETTINGS_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>

namespace KC {

#define EC_SYNC_OPT_STREAM			1
#define EC_SYNC_OPT_CHANGENOTIF		2
#define EC_SYNC_OPT_STATECOLLECT	4
#define EC_SYNC_OPT_ALL				(EC_SYNC_OPT_STREAM | EC_SYNC_OPT_CHANGENOTIF | EC_SYNC_OPT_STATECOLLECT)

class ECSyncSettings final {
public:
	// Sync options
	bool SyncStreamEnabled() const { return m_ulSyncOpts & EC_SYNC_OPT_STREAM; }

	// Stream settings
	ULONG StreamTimeout() const { return m_ulStreamTimeout; }
	ULONG StreamBufferSize() const { return m_ulStreamBufferSize; }

	static ECSyncSettings instance;

private:
	_kc_hidden ECSyncSettings(void);

	ULONG m_ulSyncLog = 0, m_ulSyncLogLevel;
	static const unsigned int m_ulSyncOpts = EC_SYNC_OPT_ALL;
	unsigned int m_ulStreamTimeout = 30000, m_ulStreamBufferSize = 131072;
};

} /* namespace */

#endif // ndef ECSYNCSETTINGS_INCLUDED
