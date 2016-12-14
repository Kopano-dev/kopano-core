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
#include <kopano/memory.hpp>
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

namespace KC {

typedef mapi_object_ptr<IECLicense, IID_IECLicense>ECLicensePtr;

class servername _kc_final {
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

static HRESULT GetMailboxDataPerServer(const char *lpszPath, const char *lpSSLKey, const char *lpSSLPass, DataCollector *lpCollector);
static HRESULT GetMailboxDataPerServer(IMAPISession *lpSession, const char *lpszPath, DataCollector *lpCollector);
static HRESULT UpdateServerList(IABContainer *lpContainer, std::set<servername> &listServers);

class UserCountCollector _kc_final : public DataCollector {
public:
	UserCountCollector();
	virtual HRESULT CollectData(LPMAPITABLE store_table) _kc_override;
	unsigned int result() const;

private:
	unsigned int m_ulUserCount;
};

template <typename string_type, ULONG prAccount>
class UserListCollector _kc_final : public DataCollector {
public:
	UserListCollector(IMAPISession *lpSession);
	virtual HRESULT GetRequiredPropTags(LPMAPIPROP prop, LPSPropTagArray *) const _kc_override;
	virtual HRESULT CollectData(LPMAPITABLE store_table) _kc_override;
	void swap_result(std::list<string_type> *lplstUsers);

private:
	void push_back(LPSPropValue lpPropAccount);

private:
	std::list<string_type> m_lstUsers;
	MAPISessionPtr m_ptrSession;
};

HRESULT	DataCollector::GetRequiredPropTags(LPMAPIPROP /*lpProp*/, LPSPropTagArray *lppPropTagArray) const {
	static SizedSPropTagArray(1, sptaDefaultProps) = {1, {PR_DISPLAY_NAME}};
	return Util::HrCopyPropTagArray(sptaDefaultProps, lppPropTagArray);
}

HRESULT DataCollector::GetRestriction(LPMAPIPROP lpProp, LPSRestriction *lppRestriction) {
	HRESULT hr = hrSuccess;
	SPropValue sPropOrphan;
	ECAndRestriction resMailBox;

	PROPMAP_START(1)
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
 exitpm:
	return hr;
}

UserCountCollector::UserCountCollector(): m_ulUserCount(0) {}

HRESULT UserCountCollector::CollectData(LPMAPITABLE lpStoreTable) {
	ULONG ulCount = 0;
	HRESULT hr = lpStoreTable->GetRowCount(0, &ulCount);
	if (hr != hrSuccess)
		return hr;

	m_ulUserCount += ulCount;
	return hrSuccess;
}

inline unsigned int UserCountCollector::result() const {
	return m_ulUserCount;
}

template<typename string_type, ULONG prAccount>
UserListCollector<string_type, prAccount>::UserListCollector(IMAPISession *lpSession): m_ptrSession(lpSession, true) {}

template<typename string_type, ULONG prAccount>
HRESULT	UserListCollector<string_type, prAccount>::GetRequiredPropTags(LPMAPIPROP /*lpProp*/, LPSPropTagArray *lppPropTagArray) const {
	static SizedSPropTagArray(1, sptaDefaultProps) = {1, {PR_MAILBOX_OWNER_ENTRYID}};
	return Util::HrCopyPropTagArray(sptaDefaultProps, lppPropTagArray);
}

template<typename string_type, ULONG prAccount>
HRESULT UserListCollector<string_type, prAccount>::CollectData(LPMAPITABLE lpStoreTable) {
	while (true) {
		SRowSetPtr ptrRows;

		HRESULT hr = lpStoreTable->QueryRows(50, 0, &ptrRows);
		if (hr != hrSuccess)
			return hr;

		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[0].ulPropTag == PR_MAILBOX_OWNER_ENTRYID) {
				HRESULT hrTmp;
				ULONG ulType;
				MAPIPropPtr ptrUser;
				SPropValuePtr ptrAccount;

				hrTmp = m_ptrSession->OpenEntry(ptrRows[i].lpProps[0].Value.bin.cb, (LPENTRYID)ptrRows[i].lpProps[0].Value.bin.lpb, &ptrUser.iid, 0, &ulType, &ptrUser);
				if (hrTmp != hrSuccess)
					continue;
				hrTmp = HrGetOneProp(ptrUser, prAccount, &~ptrAccount);
				if (hrTmp != hrSuccess)
					continue;

				push_back(ptrAccount);
			}
		}

		if (ptrRows.size() < 50)
			break;
	}
	return hrSuccess;
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

HRESULT GetArchivedUserList(IMAPISession *lpMapiSession, const char *lpSSLKey,
    const char *lpSSLPass, std::list<std::string> *lplstUsers, bool bLocalOnly)
{
	UserListCollector<std::string, PR_ACCOUNT_A> collector(lpMapiSession);
	HRESULT hr = GetMailboxData(lpMapiSession, lpSSLKey, lpSSLPass,
	             bLocalOnly, &collector);
	if (hr != hrSuccess)
		return hr;
	collector.swap_result(lplstUsers);
	return hrSuccess;
}

HRESULT GetArchivedUserList(IMAPISession *lpMapiSession, const char *lpSSLKey,
    const char *lpSSLPass, std::list<std::wstring> *lplstUsers, bool bLocalOnly)
{
	UserListCollector<std::wstring, PR_ACCOUNT_W> collector(lpMapiSession);
	HRESULT hr = GetMailboxData(lpMapiSession, lpSSLKey, lpSSLPass,
	             bLocalOnly, &collector);
	if (hr != hrSuccess)
		return hr;
	collector.swap_result(lplstUsers);
	return hrSuccess;
}

HRESULT GetMailboxData(IMAPISession *lpMapiSession, const char *lpSSLKey,
    const char *lpSSLPass, bool bLocalOnly, DataCollector *lpCollector)
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
	KCHL::memory_ptr<ECSVRNAMELIST> lpSrvNameList;
	KCHL::memory_ptr<ECSERVERLIST> lpSrvList;
	SizedSPropTagArray(1, sCols) = {1, { PR_ENTRYID } };

	if (lpMapiSession == NULL || lpCollector == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpMapiSession->OpenAddressBook(0, &IID_IAddrBook, 0, &ptrAdrBook);
	if(hr != hrSuccess) {
		ec_log_crit("Unable to open addressbook: 0x%08X", hr);
		goto exit;
	}
	hr = ptrAdrBook->GetDefaultDir(&cbDDEntryID, &~ptrDDEntryID);
	if(hr != hrSuccess) {
		ec_log_crit("Unable to open default addressbook: 0x%08X", hr);
		goto exit;
	}

	hr = ptrAdrBook->OpenEntry(cbDDEntryID, ptrDDEntryID, NULL, 0, &ulObj, (LPUNKNOWN*)&ptrDefaultDir);
	if(hr != hrSuccess) {
		ec_log_crit("Unable to open GAB: 0x%08X", hr);
		goto exit;
	}

	/* Open Hierarchy Table to see if we are running in multi-tenancy mode or not */
	hr = ptrDefaultDir->GetHierarchyTable(0, &ptrHierarchyTable);
	if (hr != hrSuccess) {
		ec_log_crit("Unable to open hierarchy table: 0x%08X", hr);
		goto exit;
	}

	hr = ptrHierarchyTable->GetRowCount(0, &ulCompanyCount);
	if(hr != hrSuccess) {
		ec_log_crit("Unable to get hierarchy row count: 0x%08X", hr);
		goto exit;
	}

	if( ulCompanyCount > 0) {
		hr = ptrHierarchyTable->SetColumns(sCols, TBL_BATCH);
		if(hr != hrSuccess) {
			ec_log_crit("Unable to set set columns on user table: 0x%08X", hr);
			goto exit;
		}

		/* multi-tenancy, loop through all subcontainers to find all users */
		hr = ptrHierarchyTable->QueryRows(ulCompanyCount, 0, &ptrRows);
		if (hr != hrSuccess)
			goto exit;
		
		for (unsigned int i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[0].ulPropTag != PR_ENTRYID) {
				ec_log_crit("Unable to get entryid to open tenancy Address Book");
				hr = MAPI_E_INVALID_PARAMETER;
				goto exit;
			}
			
			hr = ptrAdrBook->OpenEntry(ptrRows[i].lpProps[0].Value.bin.cb, (LPENTRYID)ptrRows[i].lpProps[0].Value.bin.lpb, NULL, 0, &ulObj, (LPUNKNOWN*)&ptrCompanyDir);
			if (hr != hrSuccess) {
				ec_log_crit("Unable to open tenancy Address Book: 0x%08X", hr);
				goto exit;
			}

			hr = UpdateServerList(ptrCompanyDir, listServers);
			if(hr != hrSuccess) {
				ec_log_crit("Unable to create tenancy server list");
				goto exit;
			}
		}
	} else {
		hr = UpdateServerList(ptrDefaultDir, listServers);
		if(hr != hrSuccess) {
			ec_log_crit("Unable to create server list");
			goto exit;
		}
	}

	hr = HrOpenDefaultStore(lpMapiSession, &ptrStore);
	if(hr != hrSuccess) {
		ec_log_crit("Unable to open default store: 0x%08X", hr);
		goto exit;
	}

	//@todo use PT_OBJECT to queryinterface
	hr = ptrStore->QueryInterface(IID_IECServiceAdmin, &ptrServiceAdmin);
	if (hr != hrSuccess)
		goto exit;
	hr = MAPIAllocateBuffer(sizeof(ECSVRNAMELIST), &~lpSrvNameList);
	if (hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateMore(sizeof(WCHAR *) * listServers.size(), lpSrvNameList, (LPVOID *)&lpSrvNameList->lpszaServer);
	if (hr != hrSuccess)
		goto exit;

	lpSrvNameList->cServers = 0;
	for (const auto &i : listServers)
		lpSrvNameList->lpszaServer[lpSrvNameList->cServers++] = i.c_str();

	hr = ptrServiceAdmin->GetServerDetails(lpSrvNameList, MAPI_UNICODE, &~lpSrvList);
	if (hr == MAPI_E_NETWORK_ERROR) {
		//support single server
		hr = GetMailboxDataPerServer(lpMapiSession, "", lpCollector);
		if (hr != hrSuccess)
			goto exit;

	} else if (FAILED(hr)) {
		ec_log_err("Unable to get server details: 0x%08X", hr);
		if (hr == MAPI_E_NOT_FOUND) {
			ec_log_err("Details for one or more requested servers was not found.");
			ec_log_err("This usually indicates a misconfigured home server for a user.");
			ec_log_err("Requested servers:");
			for (const auto &i : listServers)
				ec_log_err("* %ls", i.c_str());
		}
		goto exit;
	} else {

		for (ULONG i = 0; i < lpSrvList->cServers; ++i) {
			wchar_t *wszPath = NULL;

			ec_log_info("Check server: \"%ls\" ssl=\"%ls\" flag=%08x",
				(lpSrvList->lpsaServer[i].lpszName)?lpSrvList->lpsaServer[i].lpszName : L"<UNKNOWN>", 
				(lpSrvList->lpsaServer[i].lpszSslPath)?lpSrvList->lpsaServer[i].lpszSslPath : L"<UNKNOWN>", 
				lpSrvList->lpsaServer[i].ulFlags);

			if (bLocalOnly && (lpSrvList->lpsaServer[i].ulFlags & EC_SDFLAG_IS_PEER) == 0) {
				ec_log_info("Skipping remote server: \"%ls\".",
					(lpSrvList->lpsaServer[i].lpszName)?lpSrvList->lpsaServer[i].lpszName : L"<UNKNOWN>");
				continue;
			}

			if (lpSrvList->lpsaServer[i].ulFlags & EC_SDFLAG_IS_PEER &&
			    lpSrvList->lpsaServer[i].lpszFilePath != nullptr)
				wszPath = lpSrvList->lpsaServer[i].lpszFilePath;
			if (wszPath == NULL) {
				if(lpSrvList->lpsaServer[i].lpszSslPath == NULL) {
					ec_log_err("No SSL or File path found for server: \"%ls\", please fix your configuration.", lpSrvList->lpsaServer[i].lpszName);
					goto exit;
				} else {
					wszPath = lpSrvList->lpsaServer[i].lpszSslPath;
				}
			}

			hr = GetMailboxDataPerServer(converter.convert_to<char *>(wszPath), lpSSLKey, lpSSLPass, lpCollector);
			if(FAILED(hr)) {
				ec_log_err("Failed to collect data from server: \"%ls\", hr: 0x%08x", wszPath, hr);
				goto exit;
			}
		}

		hr = hrSuccess;
	}

exit:
	return hr;
}

HRESULT GetMailboxDataPerServer(const char *lpszPath, const char *lpSSLKey,
    const char *lpSSLPass, DataCollector *lpCollector)
{
	MAPISessionPtr  ptrSessionServer;
	HRESULT hr = HrOpenECAdminSession(&ptrSessionServer, "userutil.cpp",
	             "GetMailboxDataPerServer", lpszPath, 0, lpSSLKey,
	             lpSSLPass);
	if(hr != hrSuccess) {
		ec_log_crit("Unable to open admin session on server \"%s\": 0x%08X", lpszPath, hr);
		return hr;
	}
	return GetMailboxDataPerServer(ptrSessionServer, lpszPath, lpCollector);
}

/**
 * Get archived user count per server
 *
 * @param[in] lpszPath	Path to a server
 * @param[out] lpulArchivedUsers The amount of archived user on the give server
 *
 * @return Mapi errors
 */
HRESULT GetMailboxDataPerServer(IMAPISession *lpSession, const char *lpszPath,
    DataCollector *lpCollector)
{
	MsgStorePtr		ptrStoreAdmin;
	MAPITablePtr	ptrStoreTable;
	SPropTagArrayPtr ptrPropTagArray;
	SRestrictionPtr ptrRestriction;

	ExchangeManageStorePtr	ptrEMS;

	HRESULT hr = HrOpenDefaultStore(lpSession, &ptrStoreAdmin);
	if(hr != hrSuccess) {
		ec_log_crit("Unable to open default store on server \"%s\": 0x%08X", lpszPath, hr);
		return hr;
	}

	//@todo use PT_OBJECT to queryinterface
	hr = ptrStoreAdmin->QueryInterface(IID_IExchangeManageStore, (void**)&ptrEMS);
	if (hr != hrSuccess)
		return hr;
	hr = ptrEMS->GetMailboxTable(NULL, &ptrStoreTable, MAPI_DEFERRED_ERRORS);
	if (hr != hrSuccess)
		return hr;
	hr = lpCollector->GetRequiredPropTags(ptrStoreAdmin, &~ptrPropTagArray);
	if (hr != hrSuccess)
		return hr;
	hr = ptrStoreTable->SetColumns(ptrPropTagArray, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = lpCollector->GetRestriction(ptrStoreAdmin, &~ptrRestriction);
	if (hr != hrSuccess)
		return hr;
	hr = ptrStoreTable->Restrict(ptrRestriction, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;

	return lpCollector->CollectData(ptrStoreTable);
}

/**
 * Build a server list from a countainer with users
 *
 * @param[in] lpContainer A container to get users, groups and other objects
 * @param[in,out] A set with server names. The new servers will be added
 *
 * @return MAPI error codes
 */
HRESULT UpdateServerList(IABContainer *lpContainer,
    std::set<servername> &listServers)
{
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

	HRESULT hr = lpContainer->GetContentsTable(MAPI_DEFERRED_ERRORS, &ptrTable);
	if(hr != hrSuccess) {
		ec_log_crit("Unable to open contents table: 0x%08X", hr);
		return hr;
	}
	hr = ptrTable->SetColumns(sCols, TBL_BATCH);
	if(hr != hrSuccess) {
		ec_log_crit("Unable to set set columns on user table: 0x%08X", hr);
		return hr;
	}

	// Restrict to users (not groups) 
	hr = ptrTable->Restrict(&sResAllUsers, TBL_BATCH);
	if (hr != hrSuccess) {
		ec_log_crit("Unable to get total user count: 0x%08X", hr);
		return hr;
	}

	while (true) {
		hr = ptrTable->QueryRows(50, 0, &ptrRows);
		if (hr != hrSuccess)
			return hr;
		if (ptrRows.empty())
			break;

		for (unsigned int i = 0; i < ptrRows.size(); ++i) {
			if(ptrRows[i].lpProps[0].ulPropTag == PR_EC_HOMESERVER_NAME_W) {
				listServers.insert(ptrRows[i].lpProps[0].Value.lpszW);

				if(ptrRows[i].lpProps[1].ulPropTag == PR_DISPLAY_NAME_W)
					ec_log_info("User: %ls on server \"%ls\"", ptrRows[i].lpProps[1].Value.lpszW, ptrRows[i].lpProps[0].Value.lpszW);
			}
		}
	}
	return hrSuccess;
}

} /* namespace */
