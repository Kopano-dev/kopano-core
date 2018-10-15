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
#include <kopano/automapi.hpp>
#include <kopano/ECLogger.h>
#include "Archiver.h"               // for declaration of class Archiver
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr

namespace KC {

class ArchiverImpl final : public Archiver {
public:
	eResult Init(const char *progname, const char *config, const configsetting_t *extra, unsigned int flags) override;
	eResult GetControl(ArchiveControlPtr *, bool force_cleanup) override;
	eResult GetManage(const TCHAR *user, ArchiveManagePtr *) override;
	eResult AutoAttach(unsigned int flags) override;
	ECConfig *GetConfig() const override { return m_lpsConfig.get(); }
	ECLogger *GetLogger(eLogType which) const override; /* Inherits default (which = DefaultLog) from Archiver::GetLogger */

private:
	configsetting_t* ConcatSettings(const configsetting_t *lpSettings1, const configsetting_t *lpSettings2);
	unsigned CountSettings(const configsetting_t *lpSettings);

	AutoMAPI m_MAPI;
	std::unique_ptr<ECConfig> m_lpsConfig;
	std::shared_ptr<ECLogger> m_lpLogger;
	std::shared_ptr<ECLogger> m_lpLogLogger; // Logs only to the log specified in the config
	ArchiverSessionPtr 		m_ptrSession;
	std::unique_ptr<configsetting_t[]> m_lpDefaults;
};

} /* namespace */

#endif // !defined ARCHIVERIMPL_H_INCLUDED
