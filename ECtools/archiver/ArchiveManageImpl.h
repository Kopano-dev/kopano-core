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

#ifndef ARCHIVEMANAGEIMPL_H_INCLUDED
#define ARCHIVEMANAGEIMPL_H_INCLUDED

#include <kopano/zcdefs.h>
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr
#include "helpers/ArchiveHelper.h"
#include "ECArchiverLogger.h"
#include "Archiver.h"

/**
 * The ArchiveManager is used to attach, detach and list archives for users.
 */
class ArchiveManageImpl _kc_final : public ArchiveManage {
public:
	static HRESULT Create(ArchiverSessionPtr ptrSession, ECConfig *lpConfig, const TCHAR *lpszUser, ECLogger *lpLogger, ArchiveManagePtr *lpptrArchiveManage);

	eResult AttachTo(const char *lpszArchiveServer, const TCHAR *lpszArchive, const TCHAR *lpszFolder, unsigned ulFlags);
	eResult DetachFrom(const char *lpszArchiveServer, const TCHAR *lpszArchive, const TCHAR *lpszFolder);
	eResult DetachFrom(unsigned int ulArchive);
	eResult ListArchives(std::ostream &ostr);
	eResult ListArchives(ArchiveList *lplstArchives, const char *lpszIpmSubtreeSubstitude);
	eResult ListAttachedUsers(std::ostream &ostr);
	eResult ListAttachedUsers(UserList *lplstUsers);
	eResult AutoAttach(unsigned int ulFlags);

	HRESULT AttachTo(const char *lpszArchiveServer, const TCHAR *lpszArchive, const TCHAR *lpszFolder, unsigned ulFlags, za::helpers::AttachType attachType);
	HRESULT AttachTo(LPMDB lpArchiveStore, const tstring &strFoldername, const char *lpszArchiveServer, const abentryid_t &sUserEntryId, unsigned ulFlags, za::helpers::AttachType attachType);

	~ArchiveManageImpl();

private:
	ArchiveManageImpl(ArchiverSessionPtr ptrSession, ECConfig *lpConfig, const tstring &strUser, ECLogger *lpLogger);
	HRESULT Init();

	static UserEntry MakeUserEntry(const std::string &strUser);

	HRESULT GetRights(LPMAPIFOLDER lpFolder, unsigned *lpulRights);

private:
	ArchiverSessionPtr	m_ptrSession;
	ECConfig	*m_lpConfig;
	tstring	m_strUser;
	ECArchiverLogger *m_lpLogger;
	MsgStorePtr	m_ptrUserStore;
};

#endif // !defined ARCHIVEMANAGEIMPL_H_INCLUDED
