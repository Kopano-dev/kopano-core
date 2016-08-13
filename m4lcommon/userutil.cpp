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

#include <kopano/zcdefs.h>
#include <kopano/platform.h>

#include <mapi.h>
#include <mapiutil.h>
#include <kopano/ECLogger.h>

#include <kopano/userutil.h>

#include <kopano/charset/utf8string.h>
#include <kopano/charset/convert.h>
#include <kopano/ECDefs.h>
#include <kopano/ECGuid.h>
#include <kopano/IECServiceAdmin.h>
#include <edkmdb.h>
#include <edkguid.h>
#include <kopano/IECLicense.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECRestriction.h>
#include <kopano/mapi_ptr.h>
#include <kopano/mapiguidext.h>

using namespace std;

typedef mapi_object_ptr<IECLicense, IID_IECLicense>ECLicensePtr;

class servername _zcp_final {
public:
	servername(LPCTSTR lpszName): m_strName(lpszName) {}
	servername(const servername &other): m_strName(other.m_strName) {}

	servername& operator=(const servername &other) {
		if (&other != this)
			m_strName = other.m_strName;
		return *this;
	}

	LPTSTR c_str() const {
		return (LPTSTR)m_strName.c_str();
	}

	bool operator<(const servername &other) const {
		return wcscasecmp(m_strName.c_str(), other.m_strName.c_str()) < 0;
	}

private:
	wstring	m_strName;
};

static HRESULT GetMailboxDataPerServer(ECLogger *lpLogger, const char *lpszPath, const char *lpSSLKey, const char *lpSSLPass, DataCollector *lpCollector);
static HRESULT GetMailboxDataPerServer(ECLogger *lpLogger, IMAPISession *lpSession, const char *lpszPath, DataCollector *lpCollector);
static HRESULT UpdateServerList(ECLogger *lpLogger, IABContainer *lpContainer, std::set<servername> &listServers);

class UserCountCollector _zcp_final : public DataCollector
{
public:
	UserCountCollector();
	virtual HRESULT CollectData(LPMAPITABLE lpStoreTable) _zcp_override;
	unsigned int result() const;

private:
	unsigned int m_ulUserCount;
};

template <typename string_type, ULONG prAccount>
class UserListCollector _zcp_final : public DataCollector
{
public:
	UserListCollector(IMAPISession *lpSession);

	virtual HRESULT GetRequiredPropTags(LPMAPIPROP lpProp, LPSPropTagArray *lppPropTagArray) const _zcp_override;
	virtual HRESULT CollectData(LPMAPITABLE lpStoreTable) _zcp_override;
	void swap_result(std::list<string_type> *lplstUsers);

private:
	void push_back(LPSPropValue lpPropAccount);

private:
	std::list<string_type> m_lstUsers;
	MAPISessionPtr m_ptrSession;
};

HRESULT	DataCollector::GetRequiredPropTags(LPMAPIPROP /*lpProp*/, LPSPropTagArray *lppPropTagArray) const {
	static SizedSPropTagArray(1, sptaDefaultProps) = {1, {PR_DISPLAY_NAME}};
	return Util::HrCopyPropTagArray((LPSPropTagArray)&sptaDefaultProps, lppPropTagArray);
}

HRESULT DataCollector::GetRestriction(LPMAPIPROP lpProp, LPSRestriction *lppRestriction) {
	HRESULT hr = hrSuccess;
	SPropValue sPropOrphan;
	ECAndRestriction resMailBox;

	PROPMAP_START
		PROPMAP_NAMED_ID(STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, "store-entryids")
	PROPMAP_INIT(lpProp);

	sPropOrphan.ulPropTag = PR_EC_DELETED_STORE;
	sPropOrphan.Value.b = TRUE;

	resMailBox = ECAndRestriction (
					ECNotRestriction(
						ECAndRestriction(
								ECExistRestriction(PR_EC_DELETED_STORE) +
								ECPropertyRestriction(RELOP_EQ, PR_EC_DELETED_STORE, &sPropOrphan, ECRestriction::Cheap)
						)
					) + 
					ECExistRestriction(CHANGE_PROP_TYPE(PROP_STORE_ENTRYIDS, PT_MV_BINARY))
				);

	hr = resMailBox.CreateMAPIRestriction(lppRestriction);

exit:
	return hr;
}

UserCountCollector::UserCountCollector(): m_ulUserCount(0) {}

HRESULT UserCountCollector::CollectData(LPMAPITABLE lpStoreTable) {
	HRESULT hr = hrSuccess;
	ULONG ulCount = 0;

	hr = lpStoreTable->GetRowCount(0, &ulCount);
	if (hr != hrSuccess)
		goto exit;

	m_ulUserCount += ulCount;

exit:
	return hr;
}

inline unsigned int UserCountCollector::result() const {
	return m_ulUserCount;
}

template<typename string_type, ULONG prAccount>
UserListCollector<string_type, prAccount>::UserListCollector(IMAPISession *lpSession): m_ptrSession(lpSession, true) {}

template<typename string_type, ULONG prAccount>
HRESULT	UserListCollector<string_type, prAccount>::GetRequiredPropTags(LPMAPIPROP /*lpProp*/, LPSPropTagArray *lppPropTagArray) const {
	static SizedSPropTagArray(1, sptaDefaultProps) = {1, {PR_MAILBOX_OWNER_ENTRYID}};
	return Util::HrCopyPropTagArray((LPSPropTagArray)&sptaDefaultProps, lppPropTagArray);
}

template<typename string_type, ULONG prAccount>
HRESULT UserListCollector<string_type, prAccount>::CollectData(LPMAPITABLE lpStoreTable) {
	HRESULT hr = hrSuccess;

	while (true) {
		SRowSetPtr ptrRows;

		hr = lpStoreTable->QueryRows(50, 0, &ptrRows);
		if (hr != hrSuccess)
			goto exit;

		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[0].ulPropTag == PR_MAILBOX_OWNER_ENTRYID) {
				HRESULT hrTmp;
				ULONG ulType;
				MAPIPropPtr ptrUser;
				SPropValuePtr ptrAccount;

				hrTmp = m_ptrSession->OpenEntry(ptrRows[i].lpProps[0].Value.bin.cb, (LPENTRYID)ptrRows[i].lpProps[0].Value.bin.lpb, &ptrUser.iid, 0, &ulType, &ptrUser);
				if (hrTmp != hrSuccess)
					continue;

				hrTmp = HrGetOneProp(ptrUser, prAccount, &ptrAccount);
				if (hrTmp != hrSuccess)
					continue;

				push_back(ptrAccount);
			}
		}

		if (ptrRows.size() < 50)
			break;
	}
exit:
	return hr;
}

template<typename string_type, ULONG prAccount>
void UserListCollector<string_type, prAccount>::swap_result(std::list<string_type> *lplstUsers) {
	lplstUsers->swap(m_lstUsers);
}

template<>
void UserListCollector<std::string, PR_ACCOUNT_A>::push_back(LPSPropValue lpPropAccount) {
	m_lstUsers.push_back(lpPropAccount->Value.lpszA);
}

template<>
void UserListCollector<std::wstring, PR_ACCOUNT_W>::push_back(LPSPropValue lpPropAccount) {
	m_lstUsers.push_back(lpPropAccount->Value.lpszW);
}

HRESULT GetArchivedUserList(ECLogger *lpLogger, IMAPISession *lpMapiSession, const char *lpSSLKey, const char *lpSSLPass, std::list<std::string> *lplstUsers, bool bLocalOnly)
{
	HRESULT hr = hrSuccess;
	UserListCollector<std::string, PR_ACCOUNT_A> collector(lpMapiSession);

	hr = GetMailboxData(lpLogger, lpMapiSession, lpSSLKey, lpSSLPass, bLocalOnly, &collector);
	if (hr != hrSuccess)
		goto exit;

	collector.swap_result(lplstUsers);

exit:
	return hr;
}

HRESULT GetArchivedUserList(ECLogger *lpLogger, IMAPISession *lpMapiSession, const char *lpSSLKey, const char *lpSSLPass, std::list<std::wstring> *lplstUsers, bool bLocalOnly)
{
	HRESULT hr = hrSuccess;
	UserListCollector<std::wstring, PR_ACCOUNT_W> collector(lpMapiSession);

	hr = GetMailboxData(lpLogger, lpMapiSession, lpSSLKey, lpSSLPass, bLocalOnly, &collector);
	if (hr != hrSuccess)
		goto exit;

	collector.swap_result(lplstUsers);

exit:
	return hr;
}

HRESULT GetMailboxData(ECLogger *lpLogger, IMAPISession *lpMapiSession, const char *lpSSLKey, const char *lpSSLPass, bool bLocalOnly, DataCollector *lpCollector)
{
	HRESULT			hr = S_OK;

	AddrBookPtr		ptrAdrBook;
	EntryIdPtr		ptrDDEntryID;
	ABContainerPtr	ptrDefaultDir;
	ABContainerPtr	ptrCompanyDir;
	MAPITablePtr	ptrHierarchyTable;
	SRowSetPtr		ptrRows;
	MsgStorePtr		ptrStore;
	ECServiceAdminPtr	ptrServiceAdmin;

	ULONG ulObj = 0;
	ULONG cbDDEntryID = 0;
	ULONG ulCompanyCount = 0;

	std::set<servername>	listServers;
	convert_context		converter;
	
	ECSVRNAMELIST	*lpSrvNameList = NULL;
	ECSERVERLIST *lpSrvList = NULL;

	SizedSPropTagArray(1, sCols) = {1, { PR_ENTRYID } };

	if (!lpLogger || !lpMapiSession || !lpCollector) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpMapiSession->OpenAddressBook(0, &IID_IAddrBook, 0, &ptrAdrBook);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open addressbook: 0x%08X", hr);
		goto exit;
	}

	hr = ptrAdrBook->GetDefaultDir(&cbDDEntryID, &ptrDDEntryID);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open default addressbook: 0x%08X", hr);
		goto exit;
	}

	hr = ptrAdrBook->OpenEntry(cbDDEntryID, ptrDDEntryID, NULL, 0, &ulObj, (LPUNKNOWN*)&ptrDefaultDir);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open GAB: 0x%08X", hr);
		goto exit;
	}

	/* Open Hierarchy Table to see if we are running in multi-tenancy mode or not */
	hr = ptrDefaultDir->GetHierarchyTable(0, &ptrHierarchyTable);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open hierarchy table: 0x%08X", hr);
		goto exit;
	}

	hr = ptrHierarchyTable->GetRowCount(0, &ulCompanyCount);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get hierarchy row count: 0x%08X", hr);
		goto exit;
	}

	if( ulCompanyCount > 0) {

		hr = ptrHierarchyTable->SetColumns((LPSPropTagArray)&sCols, TBL_BATCH);
		if(hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to set set columns on user table: 0x%08X", hr);
			goto exit;
		}

		/* multi-tenancy, loop through all subcontainers to find all users */
		hr = ptrHierarchyTable->QueryRows(ulCompanyCount, 0, &ptrRows);
		if (hr != hrSuccess)
			goto exit;
		
		for (unsigned int i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[0].ulPropTag != PR_ENTRYID) {
				lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get entryid to open tenancy Address Book");
				hr = MAPI_E_INVALID_PARAMETER;
				goto exit;
			}
			
			hr = ptrAdrBook->OpenEntry(ptrRows[i].lpProps[0].Value.bin.cb, (LPENTRYID)ptrRows[i].lpProps[0].Value.bin.lpb, NULL, 0, &ulObj, (LPUNKNOWN*)&ptrCompanyDir);
			if (hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open tenancy Address Book: 0x%08X", hr);
				goto exit;
			}

			hr = UpdateServerList(lpLogger, ptrCompanyDir, listServers);
			if(hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to create tenancy server list");
				goto exit;
			}
		}
	} else {
		hr = UpdateServerList(lpLogger, ptrDefaultDir, listServers);
		if(hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to create server list");
			goto exit;
		}
	}

	hr = HrOpenDefaultStore(lpMapiSession, &ptrStore);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open default store: 0x%08X", hr);
		goto exit;
	}

	//@todo use PT_OBJECT to queryinterface
	hr = ptrStore->QueryInterface(IID_IECServiceAdmin, &ptrServiceAdmin);
	if (hr != hrSuccess) {
		goto exit;
	}

	hr = MAPIAllocateBuffer(sizeof(ECSVRNAMELIST), (LPVOID *)&lpSrvNameList);
	if (hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateMore(sizeof(WCHAR *) * listServers.size(), lpSrvNameList, (LPVOID *)&lpSrvNameList->lpszaServer);
	if (hr != hrSuccess)
		goto exit;

	lpSrvNameList->cServers = 0;
	for (std::set<servername>::const_iterator iServer = listServers.begin();
	     iServer != listServers.end(); ++iServer)
		lpSrvNameList->lpszaServer[lpSrvNameList->cServers++] = iServer->c_str();

	hr = ptrServiceAdmin->GetServerDetails(lpSrvNameList, MAPI_UNICODE, &lpSrvList);
	if (hr == MAPI_E_NETWORK_ERROR) {
		//support single server
		hr = GetMailboxDataPerServer(lpLogger, lpMapiSession, "", lpCollector);
		if (hr != hrSuccess)
			goto exit;

	} else if (FAILED(hr)) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get server details: 0x%08X", hr);
		if (hr == MAPI_E_NOT_FOUND) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Details for one or more requested servers was not found.");
			lpLogger->Log(EC_LOGLEVEL_ERROR, "This usually indicates a misconfigured home server for a user.");
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Requested servers:");
			for (std::set<servername>::const_iterator iServer = listServers.begin();
			     iServer != listServers.end(); ++iServer)
				lpLogger->Log(EC_LOGLEVEL_ERROR, "* %ls", iServer->c_str());
		}
		goto exit;
	} else {

		for (ULONG i = 0; i < lpSrvList->cServers; ++i) {
			wchar_t *wszPath = NULL;

			lpLogger->Log(EC_LOGLEVEL_INFO, "Check server: '%ls' ssl='%ls' flag=%08x", 
				(lpSrvList->lpsaServer[i].lpszName)?lpSrvList->lpsaServer[i].lpszName : L"<UNKNOWN>", 
				(lpSrvList->lpsaServer[i].lpszSslPath)?lpSrvList->lpsaServer[i].lpszSslPath : L"<UNKNOWN>", 
				lpSrvList->lpsaServer[i].ulFlags);

			if (bLocalOnly && (lpSrvList->lpsaServer[i].ulFlags & EC_SDFLAG_IS_PEER) == 0) {
				lpLogger->Log(EC_LOGLEVEL_INFO, "Skipping remote server: '%ls'.", 
					(lpSrvList->lpsaServer[i].lpszName)?lpSrvList->lpsaServer[i].lpszName : L"<UNKNOWN>");
				continue;
			}

			if(lpSrvList->lpsaServer[i].ulFlags & EC_SDFLAG_IS_PEER) {
				if(lpSrvList->lpsaServer[i].lpszFilePath)
					wszPath = lpSrvList->lpsaServer[i].lpszFilePath;
			}
			if (wszPath == NULL) {
				if(lpSrvList->lpsaServer[i].lpszSslPath == NULL) {
					lpLogger->Log(EC_LOGLEVEL_ERROR, "No SSL or File path found for server: '%ls', please fix your configuration.", lpSrvList->lpsaServer[i].lpszName);
					goto exit;
				} else {
					wszPath = lpSrvList->lpsaServer[i].lpszSslPath;
				}
			}

			hr = GetMailboxDataPerServer(lpLogger, converter.convert_to<char *>(wszPath), lpSSLKey, lpSSLPass, lpCollector);
			if(FAILED(hr)) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to collect data from server: '%ls', hr: 0x%08x", wszPath, hr);
				goto exit;
			}
		}

		hr = hrSuccess;
	}

exit:
	MAPIFreeBuffer(lpSrvNameList);
	MAPIFreeBuffer(lpSrvList);
	return hr;
}

HRESULT GetMailboxDataPerServer(ECLogger *lpLogger, const char *lpszPath,
    const char *lpSSLKey, const char *lpSSLPass, DataCollector *lpCollector)
{
	HRESULT hr = hrSuccess;
	MAPISessionPtr  ptrSessionServer;

	hr = HrOpenECAdminSession(lpLogger, &ptrSessionServer, "userutil.cpp", "GetMailboxDataPerServer", lpszPath, 0, lpSSLKey, lpSSLPass);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open admin session on server '%s': 0x%08X", lpszPath, hr);
		goto exit;
	}

	hr = GetMailboxDataPerServer(lpLogger, ptrSessionServer, lpszPath, lpCollector);
exit:
	return hr;
}

/**
 * Get archived user count per server
 *
 * @param[in] lpszPath	Path to a server
 * @param[out] lpulArchivedUsers The amount of archived user on the give server
 *
 * @return Mapi errors
 */
HRESULT GetMailboxDataPerServer(ECLogger *lpLogger, IMAPISession *lpSession,
    const char *lpszPath, DataCollector *lpCollector)
{
	HRESULT hr = hrSuccess;

	MsgStorePtr		ptrStoreAdmin;
	MAPITablePtr	ptrStoreTable;
	SPropTagArrayPtr ptrPropTagArray;
	SRestrictionPtr ptrRestriction;

	ExchangeManageStorePtr	ptrEMS;

	hr = HrOpenDefaultStore(lpSession, &ptrStoreAdmin);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open default store on server '%s': 0x%08X", lpszPath, hr);
		goto exit;
	}

	//@todo use PT_OBJECT to queryinterface
	hr = ptrStoreAdmin->QueryInterface(IID_IExchangeManageStore, (void**)&ptrEMS);
	if (hr != hrSuccess) {
		goto exit;
	}

	hr = ptrEMS->GetMailboxTable(NULL, &ptrStoreTable, MAPI_DEFERRED_ERRORS);
	if (hr != hrSuccess)
		goto exit;

	hr = lpCollector->GetRequiredPropTags(ptrStoreAdmin, &ptrPropTagArray);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrStoreTable->SetColumns(ptrPropTagArray, TBL_BATCH);
	if (hr != hrSuccess)
		goto exit;

	hr = lpCollector->GetRestriction(ptrStoreAdmin, &ptrRestriction);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrStoreTable->Restrict(ptrRestriction, TBL_BATCH);
	if (hr != hrSuccess)
		goto exit;

	hr = lpCollector->CollectData(ptrStoreTable);
	if (hr != hrSuccess)
		goto exit;

exit:
	return hr;
}

/**
 * Build a server list from a countainer with users
 *
 * @param[in] lpContainer A container to get users, groups and other objects
 * @param[in,out] A set with server names. The new servers will be added
 *
 * @return MAPI error codes
 */
HRESULT UpdateServerList(ECLogger *lpLogger, IABContainer *lpContainer, std::set<servername> &listServers)
{
	HRESULT hr = S_OK;
	SRowSetPtr ptrRows;
	MAPITablePtr ptrTable;
	SRestriction sResAllUsers;
	SPropValue sPropUser;
	SPropValue sPropDisplayType;
	SRestriction sResSub[2];

	SizedSPropTagArray(2, sCols) = {2, { PR_EC_HOMESERVER_NAME_W, PR_DISPLAY_NAME_W } };

	sPropDisplayType.ulPropTag = PR_DISPLAY_TYPE;
	sPropDisplayType.Value.ul = DT_REMOTE_MAILUSER;

	sPropUser.ulPropTag = PR_OBJECT_TYPE;
	sPropUser.Value.ul = MAPI_MAILUSER;

	sResSub[0].rt = RES_PROPERTY;
	sResSub[0].res.resProperty.relop = RELOP_NE;
	sResSub[0].res.resProperty.ulPropTag = PR_DISPLAY_TYPE;
	sResSub[0].res.resProperty.lpProp = &sPropDisplayType;

	sResSub[1].rt = RES_PROPERTY;
	sResSub[1].res.resProperty.relop = RELOP_EQ;
	sResSub[1].res.resProperty.ulPropTag = PR_OBJECT_TYPE;
	sResSub[1].res.resProperty.lpProp = &sPropUser;

	sResAllUsers.rt = RES_AND;
	sResAllUsers.res.resAnd.cRes = 2;
	sResAllUsers.res.resAnd.lpRes = sResSub;

	hr = lpContainer->GetContentsTable(MAPI_DEFERRED_ERRORS, &ptrTable);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open contents table: 0x%08X", hr);
		goto exit;
	}

	hr = ptrTable->SetColumns((LPSPropTagArray)&sCols, TBL_BATCH);
	if(hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to set set columns on user table: 0x%08X", hr);
		goto exit;
	}

	// Restrict to users (not groups) 
	hr = ptrTable->Restrict(&sResAllUsers, TBL_BATCH);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get total user count: 0x%08X", hr);
		goto exit;
	}

	while (true) {
		hr = ptrTable->QueryRows(50, 0, &ptrRows);
		if (hr != hrSuccess)
			goto exit;

		if (ptrRows.empty())
			break;

		for (unsigned int i = 0; i < ptrRows.size(); ++i) {
			if(ptrRows[i].lpProps[0].ulPropTag == PR_EC_HOMESERVER_NAME_W) {
				listServers.insert(ptrRows[i].lpProps[0].Value.lpszW);

				if(ptrRows[i].lpProps[1].ulPropTag == PR_DISPLAY_NAME_W)
					lpLogger->Log(EC_LOGLEVEL_INFO, "User: %ls on server '%ls'", ptrRows[i].lpProps[1].Value.lpszW, ptrRows[i].lpProps[0].Value.lpszW);
			}
		}
	}

exit:

	return hr;
}
