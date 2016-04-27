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

#include "ECLibSync.h"
#include <pthread.h>


#define EC_SYNC_OPT_STREAM			1
#define EC_SYNC_OPT_CHANGENOTIF		2
#define EC_SYNC_OPT_STATECOLLECT	4
#define EC_SYNC_OPT_CONTINUOUS		8	// Not included in EC_SYNC_OPT_ALL
#define EC_SYNC_OPT_ALL				(EC_SYNC_OPT_STREAM | EC_SYNC_OPT_CHANGENOTIF | EC_SYNC_OPT_STATECOLLECT)


class ECLIBSYNC_API ECSyncSettings
{
public:
	static ECSyncSettings* GetInstance();

	// Synclog settings
	bool	SyncLogEnabled() const;
	ULONG	SyncLogLevel() const;
	bool	ContinuousLogging() const;

	// Sync options
	bool	SyncStreamEnabled() const;
	bool	ChangeNotificationsEnabled() const;
	bool	StateCollectorEnabled() const;

	// Stream settings
	ULONG	StreamTimeout() const;
	ULONG	StreamBufferSize() const;
	ULONG	StreamBatchSize() const;

	// Update settings
	bool	EnableSyncLog(bool bEnable);
	ULONG	SetSyncLogLevel(ULONG ulLogLevel);
	ULONG	SetSyncOptions(ULONG ulOptions);
	ULONG	SetStreamTimeout(ULONG ulTimeout);
	ULONG	SetStreamBufferSize(ULONG ulBufferSize);
	ULONG	SetStreamBatchSize(ULONG ulBatchSize);

private:
	ECSyncSettings();

private:
	ULONG	m_ulSyncLog;
	ULONG	m_ulSyncLogLevel;
	ULONG	m_ulSyncOpts;
	ULONG	m_ulStreamTimeout;
	ULONG	m_ulStreamBufferSize;
	ULONG	m_ulStreamBatchSize;

	static pthread_mutex_t s_hMutex;
	static ECSyncSettings *s_lpInstance;

	struct __initializer {
		__initializer();
		~__initializer();
	};
	static __initializer __i;
};

#endif // ndef ECSYNCSETTINGS_INCLUDED
