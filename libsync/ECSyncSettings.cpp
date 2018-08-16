/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <mutex>
#include "ECSyncSettings.h"
#include <mapix.h>
#include <kopano/ECLogger.h>

namespace KC {

ECSyncSettings::ECSyncSettings(void) :
	m_ulSyncLogLevel(EC_LOGLEVEL_INFO)
{
	const char *env = getenv("KOPANO_SYNC_LOGLEVEL");
	if (env && env[0] != '\0') {
		unsigned loglevel = strtoul(env, NULL, 10);
		if (loglevel > 0) {
			m_ulSyncLog = 1;
			m_ulSyncLogLevel = loglevel;
		}
	}

	env = getenv("KOPANO_STREAM_TIMEOUT");
	if (env && env[0] != '\0')
		m_ulStreamTimeout = strtoul(env, NULL, 10);

	env = getenv("KOPANO_STREAM_BUFFERSIZE");
	if (env && env[0] != '\0')
		m_ulStreamBufferSize = strtoul(env, NULL, 10);
}

ULONG ECSyncSettings::SyncLogLevel() const {
	return ContinuousLogging() ? EC_LOGLEVEL_DEBUG : m_ulSyncLogLevel;
}

ECSyncSettings ECSyncSettings::instance;

} /* namespace */
