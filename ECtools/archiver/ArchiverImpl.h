/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/* ArchiverImpl.h
 * Declaration of class ArchiverImpl
 */
#ifndef ARCHIVERIMPL_H_INCLUDED
#define ARCHIVERIMPL_H_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/automapi.hpp>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include "Archiver.h"               // for declaration of class Archiver
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr

namespace KC {

class ArchiverImpl _kc_final : public Archiver {
public:
	eResult Init(const char *lpszAppName, const char *lpszConfig, const configsetting_t *lpExtraSettings, unsigned int ulFlags) _kc_override;
	eResult GetControl(ArchiveControlPtr *lpptrControl, bool bForceCleanup) _kc_override;
	eResult GetManage(const TCHAR *lpszUser, ArchiveManagePtr *lpptrManage) _kc_override;
	eResult AutoAttach(unsigned int ulFlags) _kc_override;
	ECConfig *GetConfig(void) const _kc_override { return m_lpsConfig.get(); }
	ECLogger *GetLogger(eLogType which) const _kc_override; // Inherits default (which = DefaultLog) from Archiver::GetLogger

private:
	configsetting_t* ConcatSettings(const configsetting_t *lpSettings1, const configsetting_t *lpSettings2);
	unsigned CountSettings(const configsetting_t *lpSettings);

	AutoMAPI m_MAPI;
	std::unique_ptr<ECConfig> m_lpsConfig;
	object_ptr<ECLogger> m_lpLogger;
	object_ptr<ECLogger> m_lpLogLogger; // Logs only to the log specified in the config
	ArchiverSessionPtr 		m_ptrSession;
	std::unique_ptr<configsetting_t[]> m_lpDefaults;
};

} /* namespace */

#endif // !defined ARCHIVERIMPL_H_INCLUDED
