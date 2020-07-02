/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <memory>
#include <string>
#include <kopano/zcdefs.h>
#include "ArchiverSessionPtr.h"
#include <kopano/mapi_ptr.h>
#include <kopano/archiver-common.h>
#include <kopano/memory.hpp>

namespace KC {

class ECConfig;
class ECLogger;

/**
 * The ArchiverSession class wraps the MAPISession and provides commonly used operations. It also
 * checks the license. This way the license doesn't need to be checked all over the place.
 */
class KC_EXPORT ArchiverSession final {
public:
	static HRESULT Create(ECConfig *lpConfig, std::shared_ptr<ECLogger>, ArchiverSessionPtr *lpptrSession);
	static HRESULT Create(const object_ptr<IMAPISession> &, std::shared_ptr<ECLogger>, ArchiverSessionPtr *);
	KC_HIDDEN static HRESULT Create(const object_ptr<IMAPISession> &, ECConfig *, std::shared_ptr<ECLogger>, ArchiverSessionPtr *);
	HRESULT OpenStoreByName(const tstring &strUser, LPMDB *lppMsgStore);
	KC_HIDDEN HRESULT OpenStore(const entryid_t &, unsigned int flags, IMsgStore **);
	HRESULT OpenStore(const entryid_t &eid, LPMDB *ret);
	KC_HIDDEN HRESULT OpenReadOnlyStore(const entryid_t &, IMsgStore **);
	KC_HIDDEN HRESULT GetUserInfo(const tstring &user, abentryid_t *eid, tstring *fullname, bool *acl_capable);
	KC_HIDDEN HRESULT GetUserInfo(const abentryid_t &eid, tstring *user, tstring *fullname);
	KC_HIDDEN HRESULT GetGAL(IABContainer **);
	KC_HIDDEN HRESULT CompareStoreIds(IMsgStore *user_store, IMsgStore *arc_store, bool *res);
	KC_HIDDEN HRESULT CompareStoreIds(const entryid_t &, const entryid_t &, bool *res);
	KC_HIDDEN HRESULT CreateRemote(const char *server_path, std::shared_ptr<ECLogger>, ArchiverSessionPtr *);
	KC_HIDDEN HRESULT OpenMAPIProp(unsigned int eid_size, ENTRYID *eid, IMAPIProp **prop);
	KC_HIDDEN HRESULT OpenOrCreateArchiveStore(const tstring &user, const tstring &server, IMsgStore **arc_store);
	KC_HIDDEN HRESULT GetArchiveStoreEntryId(const tstring &user, const tstring &server, entryid_t *arc_id);
	KC_HIDDEN IMAPISession *GetMAPISession() const { return m_ptrSession; }
	const char *GetSSLPath() const;
	const char *GetSSLPass() const;

private:
	KC_HIDDEN ArchiverSession(std::shared_ptr<ECLogger>);
	KC_HIDDEN HRESULT Init(ECConfig *);
	KC_HIDDEN HRESULT Init(const char *server_path, const char *ssl_path, const char *ssl_pass);
	KC_HIDDEN HRESULT Init(const object_ptr<IMAPISession> &, const char *ssl_path, const char *ssl_pass);
	KC_HIDDEN HRESULT CreateArchiveStore(const tstring &user, const tstring &server, IMsgStore **arc_store);

	object_ptr<IMAPISession> m_ptrSession;
	object_ptr<IMsgStore> m_ptrAdminStore;
	std::shared_ptr<ECLogger> m_lpLogger;
	std::string m_strSslPath, m_strSslPass;
};

} /* namespace */
