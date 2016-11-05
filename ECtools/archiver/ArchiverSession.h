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

#ifndef ARCHIVERSESSION_H_INCLUDED
#define ARCHIVERSESSION_H_INCLUDED

#include <kopano/zcdefs.h>
#include "ArchiverSessionPtr.h"
#include <kopano/mapi_ptr.h>
#include <kopano/tstring.h>
#include <kopano/archiver-common.h>

// Forward declarations
class ECConfig;
class ECLogger;

/**
 * The ArchiverSession class wraps the MAPISession an provides commonly used operations. It also
 * checks the license. This way the license doesn't need to be checked all over the place.
 */
class _kc_export ArchiverSession _kc_final {
public:
	static HRESULT Create(ECConfig *lpConfig, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession);
	static HRESULT Create(const MAPISessionPtr &ptrSession, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession);
	_kc_hidden static HRESULT Create(const MAPISessionPtr &, ECConfig *, ECLogger *, ArchiverSessionPtr *);
	_kc_hidden ~ArchiverSession(void);
	HRESULT OpenStoreByName(const tstring &strUser, LPMDB *lppMsgStore);
	_kc_hidden HRESULT OpenStore(const entryid_t &eid, ULONG flags, LPMDB *ret);
	HRESULT OpenStore(const entryid_t &eid, LPMDB *ret);
	_kc_hidden HRESULT OpenReadOnlyStore(const entryid_t &eid, LPMDB *ret);
	_kc_hidden HRESULT GetUserInfo(const tstring &user, abentryid_t *eid, tstring *fullname, bool *acl_capable);
	_kc_hidden HRESULT GetUserInfo(const abentryid_t &eid, tstring *user, tstring *fullname);
	_kc_hidden HRESULT GetGAL(LPABCONT *container);
	_kc_hidden HRESULT CompareStoreIds(LPMDB user_store, LPMDB arc_store, bool *res);
	_kc_hidden HRESULT CompareStoreIds(const entryid_t &, const entryid_t &, bool *res);
	_kc_hidden HRESULT CreateRemote(const char *server_path, ECLogger *, ArchiverSessionPtr *);
	_kc_hidden HRESULT OpenMAPIProp(ULONG eid_size, LPENTRYID eid, LPMAPIPROP *prop);
	_kc_hidden HRESULT OpenOrCreateArchiveStore(const tstring &user, const tstring &server, LPMDB *arc_store);
	_kc_hidden HRESULT GetArchiveStoreEntryId(const tstring &user, const tstring &server, entryid_t *arc_id);
	_kc_hidden IMAPISession *GetMAPISession(void) const { return m_ptrSession; }
	const char *GetSSLPath() const;
	const char *GetSSLPass() const;

private:
	_kc_hidden ArchiverSession(ECLogger *);
	_kc_hidden HRESULT Init(ECConfig *);
	_kc_hidden HRESULT Init(const char *server_path, const char *ssl_path, const char *ssl_pass);
	_kc_hidden HRESULT Init(const MAPISessionPtr &, const char *ssl_path, const char *ssl_pass);
	_kc_hidden HRESULT CreateArchiveStore(const tstring &user, const tstring &server, LPMDB *arc_store);

private:
	MAPISessionPtr	m_ptrSession;
	MsgStorePtr		m_ptrAdminStore;
	ECLogger		*m_lpLogger;
	
	std::string		m_strSslPath;
	std::string		m_strSslPass;
};

#endif // !defined ARCHIVERSESSION_H_INCLUDED
