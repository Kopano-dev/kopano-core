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
#include "ECSyncSettings.h"

#include <pthread.h>
#include <mapix.h>

#include <kopano/ECLogger.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


ECSyncSettings* ECSyncSettings::GetInstance()
{
	pthread_mutex_lock(&s_hMutex);

	if (s_lpInstance == NULL)
		s_lpInstance = new ECSyncSettings;

	pthread_mutex_unlock(&s_hMutex);

	return s_lpInstance;
}

ECSyncSettings::ECSyncSettings()
: m_ulSyncLog(0)
, m_ulSyncLogLevel(EC_LOGLEVEL_INFO)
, m_ulSyncOpts(EC_SYNC_OPT_ALL)
, m_ulStreamTimeout(30000)
, m_ulStreamBufferSize(131072)
, m_ulStreamBatchSize(256)
{
#ifdef WIN32
	ULONG ulSize = sizeof(ULONG);
	HKEY hKey = NULL;

	/**
	 * Only load the registry setting once
	 * to enable SyncLog set 1 (REG_DWORD) on to HKEY_CURRENT_USER\Software\Kopano\Client\SyncLog
	 * to set SyncLogLevel set 1-6 (REG_DWORD) on HKEY_CURRENT_USER\Software\Kopano\Client\SyncLogLevel
	 *
	 * The SyncOptions (REG_DWORD) takes 3 options that are orred together:
	 * bit 0: Enable Streaming ICS (unless disabled on server)
	 * bit 1: Enable Change Notifications (unless disabled on server)
	 * bit 2: Enable State Collector (not yet implemented in 6.40.x)
	 *
	 * If the registry key is absent all three options are enabled.
	 **/
	if (RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Kopano\\Client"), 0, KEY_READ, &hKey) == ERROR_SUCCESS){
		ulSize = sizeof(ULONG);
		RegQueryValueEx(hKey, _T("SyncLog"), NULL, NULL, (BYTE*)&m_ulSyncLog, &ulSize);

		ulSize = sizeof(ULONG);
		RegQueryValueEx(hKey, _T("SyncLogLevel"), NULL, NULL, (BYTE*)&m_ulSyncLogLevel, &ulSize);

		ulSize = sizeof(ULONG);
		RegQueryValueEx(hKey, _T("SyncOptions"), NULL, NULL, (BYTE*)&m_ulSyncOpts, &ulSize);

		ulSize = sizeof(ULONG);
		RegQueryValueEx(hKey, _T("StreamTimeout"), NULL, NULL, (BYTE*)&m_ulStreamTimeout, &ulSize);

		ulSize = sizeof(ULONG);
		RegQueryValueEx(hKey, _T("StreamBufferSize"), NULL, NULL, (BYTE*)&m_ulStreamBufferSize, &ulSize);

		ulSize = sizeof(ULONG);
		RegQueryValueEx(hKey, _T("StreamBatchSize"), NULL, NULL, (BYTE*)&m_ulStreamBatchSize, &ulSize);

		RegCloseKey(hKey);
	}
#else
	char *env = getenv("KOPANO_SYNC_LOGLEVEL");
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
#endif
}

bool ECSyncSettings::SyncLogEnabled() const {
	return ContinuousLogging() ? true : m_ulSyncLog != 0;
}

ULONG ECSyncSettings::SyncLogLevel() const {
	return ContinuousLogging() ? EC_LOGLEVEL_DEBUG : m_ulSyncLogLevel;
}

bool ECSyncSettings::ContinuousLogging() const {
	return (m_ulSyncOpts & EC_SYNC_OPT_CONTINUOUS) == EC_SYNC_OPT_CONTINUOUS;
}

bool ECSyncSettings::SyncStreamEnabled() const {
	return (m_ulSyncOpts & EC_SYNC_OPT_STREAM) == EC_SYNC_OPT_STREAM;
}

bool ECSyncSettings::ChangeNotificationsEnabled() const {
	return (m_ulSyncOpts & EC_SYNC_OPT_CHANGENOTIF) == EC_SYNC_OPT_CHANGENOTIF;
}

bool ECSyncSettings::StateCollectorEnabled() const {
	return (m_ulSyncOpts & EC_SYNC_OPT_STATECOLLECT) == EC_SYNC_OPT_STATECOLLECT;
}

ULONG ECSyncSettings::StreamTimeout() const {
	return m_ulStreamTimeout;
}

ULONG ECSyncSettings::StreamBufferSize() const {
	return m_ulStreamBufferSize;
}

ULONG ECSyncSettings::StreamBatchSize() const {
	return m_ulStreamBatchSize;
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



pthread_mutex_t ECSyncSettings::s_hMutex;
ECSyncSettings* ECSyncSettings::s_lpInstance = NULL;


ECSyncSettings::__initializer::__initializer() {
	pthread_mutex_init(&ECSyncSettings::s_hMutex, NULL);
}

ECSyncSettings::__initializer::~__initializer() {
	delete ECSyncSettings::s_lpInstance;
	pthread_mutex_destroy(&ECSyncSettings::s_hMutex);
}

ECSyncSettings::__initializer ECSyncSettings::__i;
