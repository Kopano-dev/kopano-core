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

	env = getenv("KOPANO_STREAM_BATCHSIZE");
	if (env && env[0] != '\0')
		m_ulStreamBatchSize = strtoul(env, NULL, 10);
}

ULONG ECSyncSettings::SyncLogLevel() const {
	return ContinuousLogging() ? EC_LOGLEVEL_DEBUG : m_ulSyncLogLevel;
}

/**
 * Enable/disable the synclog.
 * @param[in]	bEnable		Set to true to enable the synclog.
 * @retval		The previous value.
 */
bool ECSyncSettings::EnableSyncLog(bool bEnable) {
	bool bPrev = SyncLogEnabled();
	m_ulSyncLog = (bEnable ? 1 : 0);
	return bPrev;
}

/**
 * Set the synclog loglevel.
 * @param[in]	ulLogLevel	The new loglevel.
 * @retval		The previous loglevel.
 * @note		The loglevel returned by this function can differ
 *				from the level returned by SyncLogLevel() when
 *				continuous logging is enabled, in which case 
 *				SyncLogLevel() will always return EC_LOGLEVEL_DEBUG.
 */
ULONG ECSyncSettings::SetSyncLogLevel(ULONG ulLogLevel) {
	ULONG ulPrev = m_ulSyncLogLevel;
	if (ulLogLevel >= EC_LOGLEVEL_FATAL && ulLogLevel <= EC_LOGLEVEL_DEBUG)
		m_ulSyncLogLevel = ulLogLevel;
	return ulPrev;
}

/**
 * Set the sync options.
 * @param[in]	ulOptions	The options to enable
 * @retval		The previous value.
 */
ULONG ECSyncSettings::SetSyncOptions(ULONG ulOptions) {
	ULONG ulPrev = m_ulSyncOpts;
	m_ulSyncOpts = ulOptions;
	return ulPrev;
}

/**
 * Set the stream timeout.
 * This is the timeout used by the asynchronous components. This has nothing
 * to do with network timeouts.
 * @param[in]	ulTimeout	The new timeout in milliseconds
 * @retval		The previous value.
 * @note	No validation of the passed value is performed. So setting
 * 			this to an insane value will result in insane behavior.
 */
ULONG ECSyncSettings::SetStreamTimeout(ULONG ulTimeout) {
	ULONG ulPrev = m_ulStreamTimeout;
	m_ulStreamTimeout = ulTimeout;
	return ulPrev;
}

/**
 * Set the stream buffer size.
 * This is the size of the buffer that is used to transfer data from the
 * exporter to the importer.
 * @param[in]	ulBufferSize	The new buffer size in bytes.
 * @retval		The previous value.
 * @note	No validation of the passed value is performed. So setting
 * 			this to an insane value will result in insane behavior.
 */
ULONG ECSyncSettings::SetStreamBufferSize(ULONG ulBufferSize) {
	ULONG ulPrev = m_ulStreamBufferSize;
	m_ulStreamBufferSize = ulBufferSize;
	return ulPrev;
}

/**
 * Set the stream batch size.
 * This is the number of messages that is requested by the exporter from the
 * server in one batch.
 * @param[in]	ulTimeout	The new size in messages
 * @retval		The previous value.
 * @note	No validation of the passed value is performed. So setting
 * 			this to an insane value will result in insane behavior.
 */
ULONG ECSyncSettings::SetStreamBatchSize(ULONG ulBatchSize) {
	ULONG ulPrev = m_ulStreamBatchSize;
	m_ulStreamBatchSize = ulBatchSize;
	return ulPrev;
}

std::mutex ECSyncSettings::s_hMutex;
ECSyncSettings ECSyncSettings::instance;

} /* namespace */
