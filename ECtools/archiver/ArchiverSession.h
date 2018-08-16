/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ARCHIVERSESSION_H_INCLUDED
#define ARCHIVERSESSION_H_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>
#include "ArchiverSessionPtr.h"
#include <kopano/mapi_ptr.h>
#include <kopano/archiver-common.h>

namespace KC {

class ECConfig;
class ECLogger;

/**
 * The ArchiverSession class wraps the MAPISession and provides commonly used operations. It also
 * checks the license. This way the license doesn't need to be checked all over the place.
 */
class _kc_export ArchiverSession _kc_final {
public:
	static HRESULT Create(ECConfig *lpConfig, std::shared_ptr<ECLogger>, ArchiverSessionPtr *lpptrSession);
	static HRESULT Create(const MAPISessionPtr &ptrSession, std::shared_ptr<ECLogger>, ArchiverSessionPtr *lpptrSession);
	_kc_hidden static HRESULT Create(const MAPISessionPtr &, ECConfig *, std::shared_ptr<ECLogger>, ArchiverSessionPtr *);
	HRESULT OpenStoreByName(const tstring &strUser, LPMDB *lppMsgStore);
	_kc_hidden HRESULT OpenStore(const entryid_t &eid, ULONG flags, LPMDB *ret);
	HRESULT OpenStore(const entryid_t &eid, LPMDB *ret);
	_kc_hidden HRESULT OpenReadOnlyStore(const entryid_t &eid, LPMDB *ret);
	_kc_hidden HRESULT GetUserInfo(const tstring &user, abentryid_t *eid, tstring *fullname, bool *acl_capable);
	_kc_hidden HRESULT GetUserInfo(const abentryid_t &eid, tstring *user, tstring *fullname);
	_kc_hidden HRESULT GetGAL(LPABCONT *container);
	_kc_hidden HRESULT CompareStoreIds(LPMDB user_store, LPMDB arc_store, bool *res);
	_kc_hidden HRESULT CompareStoreIds(const entryid_t &, const entryid_t &, bool *res);
	_kc_hidden HRESULT CreateRemote(const char *server_path, std::shared_ptr<ECLogger>, ArchiverSessionPtr *);
	_kc_hidden HRESULT OpenMAPIProp(ULONG eid_size, LPENTRYID eid, LPMAPIPROP *prop);
	_kc_hidden HRESULT OpenOrCreateArchiveStore(const tstring &user, const tstring &server, LPMDB *arc_store);
	_kc_hidden HRESULT GetArchiveStoreEntryId(const tstring &user, const tstring &server, entryid_t *arc_id);
	_kc_hidden IMAPISession *GetMAPISession(void) const { return m_ptrSession; }
	const char *GetSSLPath() const;
	const char *GetSSLPass() const;

private:
	_kc_hidden ArchiverSession(std::shared_ptr<ECLogger>);
	_kc_hidden HRESULT Init(ECConfig *);
	_kc_hidden HRESULT Init(const char *server_path, const char *ssl_path, const char *ssl_pass);
	_kc_hidden HRESULT Init(const MAPISessionPtr &, const char *ssl_path, const char *ssl_pass);
	_kc_hidden HRESULT CreateArchiveStore(const tstring &user, const tstring &server, LPMDB *arc_store);

	MAPISessionPtr	m_ptrSession;
	MsgStorePtr		m_ptrAdminStore;
	std::shared_ptr<ECLogger> m_lpLogger;
	std::string		m_strSslPath;
	std::string		m_strSslPass;
};

} /* namespace */

#endif // !defined ARCHIVERSESSION_H_INCLUDED
