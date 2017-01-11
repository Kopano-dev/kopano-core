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

/* ArchiverImpl.h
 * Declaration of class ArchiverImpl
 */
#ifndef ARCHIVERIMPL_H_INCLUDED
#define ARCHIVERIMPL_H_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/automapi.hpp>
#include "Archiver.h"               // for declaration of class Archiver
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr

namespace KC {

class ArchiverImpl _kc_final : public Archiver {
public:
	ArchiverImpl(void) = default;
	ArchiverImpl(const ArchiverImpl &) = delete;
	~ArchiverImpl();
	eResult Init(const char *lpszAppName, const char *lpszConfig, const configsetting_t *lpExtraSettings, unsigned int ulFlags) _kc_override;
	eResult GetControl(ArchiveControlPtr *lpptrControl, bool bForceCleanup) _kc_override;
	eResult GetManage(const TCHAR *lpszUser, ArchiveManagePtr *lpptrManage) _kc_override;
	eResult AutoAttach(unsigned int ulFlags) _kc_override;
	ECConfig *GetConfig(void) const _kc_override { return m_lpsConfig; }
	ECLogger *GetLogger(eLogType which) const _kc_override; // Inherits default (which = DefaultLog) from Archiver::GetLogger

private:
	configsetting_t* ConcatSettings(const configsetting_t *lpSettings1, const configsetting_t *lpSettings2);
	unsigned CountSettings(const configsetting_t *lpSettings);

private:
	KCHL::AutoMAPI m_MAPI;
	ECConfig *m_lpsConfig = nullptr;
	ECLogger *m_lpLogger = nullptr;
	ECLogger *m_lpLogLogger = nullptr; // Logs only to the log specified in the config
	ArchiverSessionPtr 		m_ptrSession;
	configsetting_t	*m_lpDefaults = nullptr;
};

} /* namespace */

#endif // !defined ARCHIVERIMPL_H_INCLUDED
