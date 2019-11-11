/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ARCHIVEMANAGEIMPL_H_INCLUDED
#define ARCHIVEMANAGEIMPL_H_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr
#include "helpers/ArchiveHelper.h"
#include "ECArchiverLogger.h"
#include "Archiver.h"

namespace KC {

/**
 * The ArchiveManager is used to attach, detach and list archives for users.
 */
class KC_EXPORT_DYCAST ArchiveManageImpl final : public ArchiveManage {
public:
	static HRESULT Create(ArchiverSessionPtr ptrSession, ECConfig *lpConfig, const TCHAR *lpszUser, std::shared_ptr<ECLogger>, ArchiveManagePtr *lpptrArchiveManage);
	KC_HIDDEN eResult AttachTo(const char *server, const TCHAR *archive, const TCHAR *folder, unsigned int flags) override;
	KC_HIDDEN eResult DetachFrom(const char *server, const TCHAR *archive, const TCHAR *folder) override;
	KC_HIDDEN eResult DetachFrom(unsigned int archive) override;
	KC_HIDDEN eResult ListArchives(std::ostream &) override;
	KC_HIDDEN eResult ListArchives(ArchiveList *, const char *ipm_subtree_subst) override;
	KC_HIDDEN eResult ListAttachedUsers(std::ostream &) override;
	KC_HIDDEN eResult ListAttachedUsers(UserList *) override;
	KC_HIDDEN eResult AutoAttach(unsigned int flags) override;
	KC_HIDDEN HRESULT AttachTo(const char *server, const TCHAR *archive, const TCHAR *folder, unsigned int flags, helpers::AttachType);
	KC_HIDDEN HRESULT AttachTo(IMsgStore *store, const tstring &folder, const char *server, const abentryid_t &user_eid, unsigned int flags, helpers::AttachType);

private:
	KC_HIDDEN ArchiveManageImpl(ArchiverSessionPtr, ECConfig *, const tstring &user, std::shared_ptr<ECLogger>);
	KC_HIDDEN HRESULT Init();
	KC_HIDDEN static UserEntry MakeUserEntry(const std::string &user);
	KC_HIDDEN HRESULT GetRights(IMAPIFolder *folder, unsigned int *right);

	ArchiverSessionPtr	m_ptrSession;
	ECConfig	*m_lpConfig;
	tstring	m_strUser;
	std::shared_ptr<ECArchiverLogger> m_lpLogger;
	MsgStorePtr	m_ptrUserStore;
};

} /* namespace */

#endif // !defined ARCHIVEMANAGEIMPL_H_INCLUDED
