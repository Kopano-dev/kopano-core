/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/* ArchiverImpl.cpp
 * Definition of class ArchiverImpl
 */
#include <memory>
#include <kopano/platform.h>
#include <kopano/automapi.hpp>
#include <kopano/memory.hpp>
#include "ArchiverImpl.h"
#include "ArchiveControlImpl.h"
#include "ArchiveManageImpl.h"
#include "ArchiveStateCollector.h"
#include "ArchiveStateUpdater.h"
#include "ArchiverSession.h"
#include <kopano/ECConfig.h>

namespace KC {

eResult ArchiverImpl::Init(const char *lpszAppName, const char *lpszConfig, const configsetting_t *lpExtraSettings, unsigned int ulFlags)
{
	MAPIINIT_0 sMapiInit = {MAPI_INIT_VERSION, MAPI_MULTITHREAD_NOTIFICATIONS};

	if (lpExtraSettings == nullptr) {
		m_lpsConfig.reset(ECConfig::Create(Archiver::GetConfigDefaults()));
	} else {
		m_lpDefaults.reset(ConcatSettings(Archiver::GetConfigDefaults(), lpExtraSettings));
		m_lpsConfig.reset(ECConfig::Create(m_lpDefaults.get()));
	}

	if (!m_lpsConfig->LoadSettings(lpszConfig) && (ulFlags & RequireConfig))
		return FileNotFound;
	if (!m_lpsConfig->LoadSettings(lpszConfig)) {
		if ((ulFlags & RequireConfig))
			return FileNotFound;
	} else if (m_lpsConfig->HasErrors()) {
		if (!(ulFlags & InhibitErrorLogging)) {
			std::shared_ptr<ECLogger> lpLogger(new ECLogger_File(EC_LOGLEVEL_CRIT, 0, "-", false));
			ec_log_set(lpLogger);
			LogConfigErrors(m_lpsConfig.get());
		}

		return InvalidConfig;
	}

	m_lpLogLogger = CreateLogger(m_lpsConfig.get(), const_cast<char *>(lpszAppName));
	if (ulFlags & InhibitErrorLogging) {
		// We need to check if we're logging to stderr. If so we'll replace
		// the logger with a NULL logger.
		auto lpFileLogger = dynamic_cast<ECLogger_File *>(m_lpLogLogger.get());
		if (lpFileLogger && lpFileLogger->IsStdErr())
			m_lpLogLogger = std::make_shared<ECLogger_Null>();
		m_lpLogger = m_lpLogLogger;
	} else if (ulFlags & AttachStdErr) {
		// We need to check if the current logger isn't logging to the console
		// as that would give duplicate messages
		auto lpFileLogger = dynamic_cast<ECLogger_File *>(m_lpLogLogger.get());
		if (lpFileLogger == NULL || !lpFileLogger->IsStdErr()) {
			std::shared_ptr<ECLogger_Tee> lpTeeLogger(new ECLogger_Tee);
			lpTeeLogger->AddLogger(m_lpLogLogger);
			std::shared_ptr<ECLogger_File> lpConsoleLogger(new ECLogger_File(EC_LOGLEVEL_ERROR, 0, "-", false));
			lpTeeLogger->AddLogger(lpConsoleLogger);
			m_lpLogger = lpTeeLogger;
		} else {
			m_lpLogger = m_lpLogLogger;
		}
	} else {
		m_lpLogger = m_lpLogLogger;
	}

	ec_log_set(m_lpLogger);
	if (m_lpsConfig->HasWarnings())
		LogConfigErrors(m_lpsConfig.get());
	if (ulFlags & DumpConfig)
		return m_lpsConfig->dump_config(stdout) == 0 ? Success : Failure;
	if (m_MAPI.Initialize(&sMapiInit) != hrSuccess)
		return Failure;
	if (ArchiverSession::Create(m_lpsConfig.get(), m_lpLogger, &m_ptrSession) != hrSuccess)
		return Failure;

	return Success;
}

eResult ArchiverImpl::GetControl(ArchiveControlPtr *lpptrControl, bool bForceCleanup)
{
    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiverImpl::GetControl() function entry");
	if (!m_MAPI.IsInitialized())
		return Uninitialized;
    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiverImpl::GetControl(): about to create an ArchiveControlImpl object");
	return MAPIErrorToArchiveError(ArchiveControlImpl::Create(m_ptrSession, m_lpsConfig.get(), m_lpLogger, bForceCleanup, lpptrControl));
}

eResult ArchiverImpl::GetManage(const TCHAR *lpszUser, ArchiveManagePtr *lpptrManage)
{
	if (!m_MAPI.IsInitialized())
		return Uninitialized;
	return MAPIErrorToArchiveError(ArchiveManageImpl::Create(m_ptrSession, m_lpsConfig.get(), lpszUser, m_lpLogger, lpptrManage));
}

eResult ArchiverImpl::AutoAttach(unsigned int ulFlags)
{
    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiverImpl::AutoAttach() function entry");
	ArchiveStateCollectorPtr ptrArchiveStateCollector;
	ArchiveStateUpdaterPtr ptrArchiveStateUpdater;

	if (ulFlags != ArchiveManage::Writable && ulFlags != ArchiveManage::ReadOnly && ulFlags != 0)
		return MAPIErrorToArchiveError(MAPI_E_INVALID_PARAMETER);

    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiverImpl::AutoAttach() about to create collector");
	auto hr = ArchiveStateCollector::Create(m_ptrSession, m_lpLogger, &ptrArchiveStateCollector);
	if (hr != hrSuccess)
		return MAPIErrorToArchiveError(hr);

    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiverImpl::AutoAttach() about to get state updater");
	hr = ptrArchiveStateCollector->GetArchiveStateUpdater(&ptrArchiveStateUpdater);
	if (hr != hrSuccess)
		return MAPIErrorToArchiveError(hr);

	if (ulFlags == 0) {
		if (parseBool(m_lpsConfig->GetSetting("auto_attach_writable")))
			ulFlags = ArchiveManage::Writable;
		else
			ulFlags = ArchiveManage::ReadOnly;
	}

    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiverImpl::AutoAttach() about to call update all");
	return MAPIErrorToArchiveError(ptrArchiveStateUpdater->UpdateAll(ulFlags));
}

ECLogger* ArchiverImpl::GetLogger(eLogType which) const
{
	switch (which) {
		case DefaultLog:
			return ec_log_get();
		case LogOnly:
			return m_lpLogLogger.get();
		default:
			return nullptr;
	}
}

configsetting_t* ArchiverImpl::ConcatSettings(const configsetting_t *lpSettings1, const configsetting_t *lpSettings2)
{
	unsigned ulIndex = 0;
	auto ulSettings = CountSettings(lpSettings1) + CountSettings(lpSettings2);
	auto lpMergedSettings = new configsetting_t[ulSettings + 1];

	while (lpSettings1->szName != NULL)
		lpMergedSettings[ulIndex++] = *lpSettings1++;
	while (lpSettings2->szName != NULL)
		lpMergedSettings[ulIndex++] = *lpSettings2++;
	memset(&lpMergedSettings[ulIndex], 0, sizeof(lpMergedSettings[ulIndex]));

	return lpMergedSettings;
}

unsigned ArchiverImpl::CountSettings(const configsetting_t *lpSettings)
{
	unsigned ulSettings = 0;

	while ((lpSettings++)->szName != NULL)
		++ulSettings;

	return ulSettings;
}

} /* namespace */
