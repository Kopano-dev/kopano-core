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

#ifndef ECSYNCSETTINGS_INCLUDED
#define ECSYNCSETTINGS_INCLUDED

#include <memory>
#include <mutex>
#include <kopano/zcdefs.h>

namespace KC {

#define EC_SYNC_OPT_STREAM			1
#define EC_SYNC_OPT_CHANGENOTIF		2
#define EC_SYNC_OPT_STATECOLLECT	4
#define EC_SYNC_OPT_CONTINUOUS		8	// Not included in EC_SYNC_OPT_ALL
#define EC_SYNC_OPT_ALL				(EC_SYNC_OPT_STREAM | EC_SYNC_OPT_CHANGENOTIF | EC_SYNC_OPT_STATECOLLECT)

class ECSyncSettings final {
public:
	// Synclog settings
	bool SyncLogEnabled() const { return ContinuousLogging() ? true : m_ulSyncLog != 0; }
	ULONG SyncLogLevel() const;
	bool ContinuousLogging() const { return m_ulSyncOpts & EC_SYNC_OPT_CONTINUOUS; }

	// Sync options
	bool SyncStreamEnabled() const { return m_ulSyncOpts & EC_SYNC_OPT_STREAM; }
	bool ChangeNotificationsEnabled() const { return m_ulSyncOpts & EC_SYNC_OPT_CHANGENOTIF; }
	bool StateCollectorEnabled() const { return m_ulSyncOpts & EC_SYNC_OPT_STATECOLLECT; }

	// Stream settings
	ULONG StreamTimeout() const { return m_ulStreamTimeout; }
	ULONG StreamBufferSize() const { return m_ulStreamBufferSize; }
	ULONG StreamBatchSize() const { return m_ulStreamBatchSize; }

	// Update settings
	bool	EnableSyncLog(bool bEnable);
	ULONG	SetSyncLogLevel(ULONG ulLogLevel);
	ULONG	SetSyncOptions(ULONG ulOptions);
	ULONG	SetStreamTimeout(ULONG ulTimeout);
	ULONG	SetStreamBufferSize(ULONG ulBufferSize);
	ULONG	SetStreamBatchSize(ULONG ulBatchSize);

	static ECSyncSettings instance;

private:
	_kc_hidden ECSyncSettings(void);

	ULONG m_ulSyncLog = 0, m_ulSyncLogLevel;
	ULONG m_ulSyncOpts = EC_SYNC_OPT_ALL, m_ulStreamTimeout = 30000;
	ULONG m_ulStreamBufferSize = 131072, m_ulStreamBatchSize = 256;

	static std::mutex s_hMutex;
};

} /* namespace */

#endif // ndef ECSYNCSETTINGS_INCLUDED
