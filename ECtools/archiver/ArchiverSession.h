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
class ArchiverSession
{
public:
	static HRESULT Create(ECConfig *lpConfig, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession);
	static HRESULT Create(const MAPISessionPtr &ptrSession, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession);
	static HRESULT Create(const MAPISessionPtr &ptrSession, ECConfig *lpConfig, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession);
	~ArchiverSession();
	
	HRESULT OpenStoreByName(const tstring &strUser, LPMDB *lppMsgStore);
	HRESULT OpenStore(const entryid_t &sEntryId, ULONG ulFlags, LPMDB *lppMsgStore);
	HRESULT OpenStore(const entryid_t &sEntryId, LPMDB *lppMsgStore);
	HRESULT OpenReadOnlyStore(const entryid_t &sEntryId, LPMDB *lppMsgStore);
	HRESULT GetUserInfo(const tstring &strUser, abentryid_t *lpsEntryId, tstring *lpstrFullname, bool *bAclCapable);
	HRESULT GetUserInfo(const abentryid_t &sEntryId, tstring *lpstrUser, tstring *lpstrFullname);
	HRESULT GetGAL(LPABCONT *lppAbContainer);
	HRESULT CompareStoreIds(LPMDB lpUserStore, LPMDB lpArchiveStore, bool *lpbResult);
	HRESULT CompareStoreIds(const entryid_t &sEntryId1, const entryid_t &sEntryId2, bool *lpbResult);
	
	HRESULT CreateRemote(const char *lpszServerPath, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession);

	HRESULT OpenMAPIProp(ULONG cbEntryID, LPENTRYID lpEntryID, LPMAPIPROP *lppProp);

	HRESULT OpenOrCreateArchiveStore(const tstring& strUserName, const tstring& strServerName, LPMDB *lppArchiveStore);
	HRESULT GetArchiveStoreEntryId(const tstring& strUserName, const tstring& strServerName, entryid_t *lpArchiveId);

	IMAPISession *GetMAPISession(void) const { return m_ptrSession; }
	const char *GetSSLPath() const;
	const char *GetSSLPass() const;

private:
	ArchiverSession(ECLogger *lpLogger);
	HRESULT Init(ECConfig *lpConfig);
	HRESULT Init(const char *lpszServerPath, const char *lpszSslPath, const char *lpszSslPass);
	HRESULT Init(const MAPISessionPtr &ptrSession, const char *lpszSslPath, const char *lpszSslPass);

	HRESULT CreateArchiveStore(const tstring& strUserName, const tstring& strServerName, LPMDB *lppArchiveStore);

private:
	MAPISessionPtr	m_ptrSession;
	MsgStorePtr		m_ptrAdminStore;
	ECLogger		*m_lpLogger;
	
	std::string		m_strSslPath;
	std::string		m_strSslPass;
};

#endif // !defined ARCHIVERSESSION_H_INCLUDED
