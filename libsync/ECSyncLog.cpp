/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
	if (s_lpLogger == nullptr)
		s_lpLogger.reset(new ECLogger_Null);
	if (!s_lpLogger)
		s_lpLogger.reset(new ECLogger_Syslog(EC_LOGLEVEL_DEBUG, "kclibsync", LOG_MAIL));
	*lppLogger = s_lpLogger;

	s_lpLogger->AddRef();
	return hr;
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
