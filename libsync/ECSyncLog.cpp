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
#include <kopano/ecversion.h>

#include "ECSyncLog.h"
#include "ECSyncSettings.h"
#include <kopano/stringutil.h>
#include <kopano/ECLogger.h>

#include <cstdlib>
#include <mapidefs.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// for LOG_MAIL
#include <syslog.h>

HRESULT ECSyncLog::GetLogger(ECLogger **lppLogger)
{
	HRESULT		hr = hrSuccess;

	*lppLogger = NULL;

	pthread_mutex_lock(&s_hMutex);

	if (s_lpLogger == NULL) {
		ECSyncSettings *lpSettings = ECSyncSettings::GetInstance();

		if (lpSettings->SyncLogEnabled()) {
			char dummy[MAX_PATH + 1] = { 0 };

			if (GetTempPathA(sizeof dummy, dummy) >= sizeof dummy)
				dummy[0] = 0x00;

			std::string strPath = dummy + std::string("/");

			if (lpSettings->ContinuousLogging()) {
				time_t now = time(NULL);

				strPath += "synclog-";
				strPath += stringify(now);
				strPath += ".txt.gz";

				s_lpLogger = new ECLogger_File(lpSettings->SyncLogLevel(), 1, strPath.c_str(), true);
			} else {
				strPath += "synclog.txt";
				s_lpLogger = new ECLogger_File(lpSettings->SyncLogLevel(), 1, strPath.c_str(), false);
			}

			s_lpLogger->Log(EC_LOGLEVEL_FATAL, "********************");
			s_lpLogger->Log(EC_LOGLEVEL_FATAL, "New sync log session openend (Kopano-" PROJECT_VERSION_CLIENT_STR ")");
			s_lpLogger->Log(EC_LOGLEVEL_FATAL, " - Log level: %u", lpSettings->SyncLogLevel());
			s_lpLogger->Log(EC_LOGLEVEL_FATAL, " - Sync stream: %s", lpSettings->SyncStreamEnabled() ? "enabled" : "disabled");
			s_lpLogger->Log(EC_LOGLEVEL_FATAL, " - Change notifications: %s", lpSettings->ChangeNotificationsEnabled() ? "enabled" : "disabled");
			s_lpLogger->Log(EC_LOGLEVEL_FATAL, " - State collector: %s", lpSettings->StateCollectorEnabled() ? "enabled" : "disabled");
			s_lpLogger->Log(EC_LOGLEVEL_FATAL, "********************");
		}
		else {
			s_lpLogger = new ECLogger_Null();
		}
	}

	if (!s_lpLogger) {
		s_lpLogger = new ECLogger_Syslog(EC_LOGLEVEL_DEBUG, "kclibsync", LOG_MAIL);
	}

	*lppLogger = s_lpLogger;

	s_lpLogger->AddRef();

	pthread_mutex_unlock(&s_hMutex);

	return hr;
}

HRESULT ECSyncLog::SetLogger(ECLogger *lpLogger)
{
	pthread_mutex_lock(&s_hMutex);

	if (s_lpLogger)
		s_lpLogger->Release();

	s_lpLogger = lpLogger;
	if (s_lpLogger)
		s_lpLogger->AddRef();

	pthread_mutex_unlock(&s_hMutex);

	return hrSuccess;
}

pthread_mutex_t	ECSyncLog::s_hMutex;
ECLogger		*ECSyncLog::s_lpLogger = NULL;


ECSyncLog::__initializer::__initializer() {
	pthread_mutex_init(&ECSyncLog::s_hMutex, NULL);
}

ECSyncLog::__initializer::~__initializer() {
	if (ECSyncLog::s_lpLogger) {
		unsigned ulRef = ECSyncLog::s_lpLogger->Release();
		// Make sure all references are released so compressed logs don't get corrupted.
		while (ulRef)
			ulRef = ECSyncLog::s_lpLogger->Release();
	}

	pthread_mutex_destroy(&ECSyncLog::s_hMutex);
}

ECSyncLog::__initializer ECSyncLog::__i;
