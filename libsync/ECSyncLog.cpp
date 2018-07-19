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
#include <kopano/ecversion.h>
#include "ECSyncLog.h"
#include "ECSyncSettings.h"
#include <kopano/stringutil.h>
#include <kopano/ECLogger.h>
#include <cstdlib>
#include <mapidefs.h>
#include <syslog.h> /* LOG_MAIL */

namespace KC {

HRESULT ECSyncLog::GetLogger(ECLogger **lppLogger)
{
	HRESULT		hr = hrSuccess;

	*lppLogger = NULL;

	scoped_lock lock(s_hMutex);

	if (s_lpLogger == NULL) {
		auto lpSettings = &ECSyncSettings::instance;

		if (lpSettings->SyncLogEnabled()) {
			char dummy[MAX_PATH + 1] = { 0 };

			if (GetTempPath(sizeof dummy, dummy) >= sizeof dummy)
				dummy[0] = 0x00;

			std::string strPath = dummy + std::string("/");

			if (lpSettings->ContinuousLogging()) {
				time_t now = time(NULL);

				strPath += "synclog-";
				strPath += stringify(now);
				strPath += ".txt.gz";
				s_lpLogger.reset(new ECLogger_File(lpSettings->SyncLogLevel(), 1, strPath.c_str(), true));
			} else {
				strPath += "synclog.txt";
				s_lpLogger.reset(new ECLogger_File(lpSettings->SyncLogLevel(), 1, strPath.c_str(), false));
			}

			s_lpLogger->Log(EC_LOGLEVEL_FATAL, "********************");
			s_lpLogger->Log(EC_LOGLEVEL_FATAL, "New sync log session openend (Kopano " PROJECT_VERSION ")");
			s_lpLogger->logf(EC_LOGLEVEL_FATAL, " - Log level: %u", lpSettings->SyncLogLevel());
			s_lpLogger->logf(EC_LOGLEVEL_FATAL, " - Sync stream: %s", lpSettings->SyncStreamEnabled() ? "enabled" : "disabled");
			s_lpLogger->logf(EC_LOGLEVEL_FATAL, " - Change notifications: %s", lpSettings->ChangeNotificationsEnabled() ? "enabled" : "disabled");
			s_lpLogger->logf(EC_LOGLEVEL_FATAL, " - State collector: %s", lpSettings->StateCollectorEnabled() ? "enabled" : "disabled");
			s_lpLogger->Log(EC_LOGLEVEL_FATAL, "********************");
		}
		else {
			s_lpLogger.reset(new ECLogger_Null);
		}
	}

	if (!s_lpLogger)
		s_lpLogger.reset(new ECLogger_Syslog(EC_LOGLEVEL_DEBUG, "kclibsync", LOG_MAIL));
	*lppLogger = s_lpLogger;

	s_lpLogger->AddRef();
	return hr;
}

HRESULT ECSyncLog::SetLogger(ECLogger *lpLogger)
{
	scoped_lock lock(s_hMutex);
	s_lpLogger.reset(lpLogger);
	return hrSuccess;
}

std::mutex ECSyncLog::s_hMutex;
object_ptr<ECLogger> ECSyncLog::s_lpLogger;

ECSyncLog::initializer::~initializer()
{
	if (ECSyncLog::s_lpLogger == nullptr)
		return;
	auto ulRef = ECSyncLog::s_lpLogger->AddRef();
	/*
	 * Forcibly drop all references so the bytestream of a compressed log
	 * gets finalized.
	 */
	while (ulRef > 1)
		ulRef = ECSyncLog::s_lpLogger->Release();
	ECSyncLog::s_lpLogger.reset();
}

ECSyncLog::initializer ECSyncLog::xinit;

} /* namespace */
