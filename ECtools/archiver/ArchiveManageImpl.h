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
class _kc_export_dycast ArchiveManageImpl _kc_final : public ArchiveManage {
public:
	static HRESULT Create(ArchiverSessionPtr ptrSession, ECConfig *lpConfig, const TCHAR *lpszUser, std::shared_ptr<ECLogger>, ArchiveManagePtr *lpptrArchiveManage);
	_kc_hidden eResult AttachTo(const char *server, const TCHAR *archive, const TCHAR *folder, unsigned int flags) _kc_override;
	_kc_hidden eResult DetachFrom(const char *server, const TCHAR *archive, const TCHAR *folder) _kc_override;
	_kc_hidden eResult DetachFrom(unsigned int archive) _kc_override;
	_kc_hidden eResult ListArchives(std::ostream &) _kc_override;
	_kc_hidden eResult ListArchives(ArchiveList *, const char *ipm_subtree_subst) _kc_override;
	_kc_hidden eResult ListAttachedUsers(std::ostream &) _kc_override;
	_kc_hidden eResult ListAttachedUsers(UserList *) _kc_override;
	_kc_hidden eResult AutoAttach(unsigned int flags) _kc_override;
	_kc_hidden HRESULT AttachTo(const char *server, const TCHAR *archive, const TCHAR *folder, unsigned int flags, helpers::AttachType);
	_kc_hidden HRESULT AttachTo(LPMDB store, const tstring &folder, const char *server, const abentryid_t &user_eid, unsigned int flags, helpers::AttachType);

private:
	_kc_hidden ArchiveManageImpl(ArchiverSessionPtr, ECConfig *, const tstring &user, std::shared_ptr<ECLogger>);
	_kc_hidden HRESULT Init(void);
	_kc_hidden static UserEntry MakeUserEntry(const std::string &user);
	_kc_hidden HRESULT GetRights(LPMAPIFOLDER folder, unsigned int *right);

	ArchiverSessionPtr	m_ptrSession;
	ECConfig	*m_lpConfig;
	tstring	m_strUser;
	std::shared_ptr<ECArchiverLogger> m_lpLogger;
	MsgStorePtr	m_ptrUserStore;
};

} /* namespace */

#endif // !defined ARCHIVEMANAGEIMPL_H_INCLUDED
