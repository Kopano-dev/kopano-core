/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <string>
#include <utility>
#include <mapi.h>
#include <mapiutil.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/memory.hpp>
#include <kopano/userutil.h>
#include <kopano/ecversion.h>
#include <kopano/charset/utf8string.h>
#include <kopano/charset/convert.h>
#include <kopano/ECDefs.h>
#include <kopano/ECGuid.h>
#include <edkmdb.h>
#include <edkguid.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/ECRestriction.h>
#include <kopano/mapi_ptr.h>
#include <kopano/mapiguidext.h>
#include <kopano/Util.h>

namespace KC {

class servername final {
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

	bool operator<(const servername &other) const noexcept
	{
		return wcscasecmp(m_strName.c_str(), other.m_strName.c_str()) < 0;
	}

private:
	std::wstring m_strName;
};

static HRESULT GetMailboxDataPerServer(const char *lpszPath, const char *lpSSLKey, const char *lpSSLPass, DataCollector *lpCollector);
static HRESULT GetMailboxDataPerServer(IMAPISession *lpSession, const char *lpszPath, DataCollector *lpCollector);
static HRESULT UpdateServerList(IABContainer *lpContainer, std::set<servername> &listServers);

template <typename string_type, ULONG prAccount>
class UserListCollector final : public DataCollector {
public:
	UserListCollector(IMAPISession *lpSession);
	virtual HRESULT GetRequiredPropTags(IMAPIProp *, SPropTagArray **) const override;
	virtual HRESULT CollectData(IMAPITable *store_table) override;
	void move_result(std::list<string_type> *lplstUsers);

private:
	void push_back(LPSPropValue lpPropAccount);

	std::list<string_type> m_lstUsers;
	MAPISessionPtr m_ptrSession;
};

HRESULT	DataCollector::GetRequiredPropTags(LPMAPIPROP /*lpProp*/, LPSPropTagArray *lppPropTagArray) const {
	static constexpr const SizedSPropTagArray(1, sptaDefaultProps) = {1, {PR_DISPLAY_NAME}};
	return Util::HrCopyPropTagArray(sptaDefaultProps, lppPropTagArray);
}

HRESULT DataCollector::GetRestriction(LPMAPIPROP lpProp, LPSRestriction *lppRestriction) {
	SPropValue sPropOrphan;

	PROPMAP_START(1)
		PROPMAP_NAMED_ID(STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, "store-entryids")
	PROPMAP_INIT(lpProp);

	sPropOrphan.ulPropTag = PR_EC_DELETED_STORE;
	sPropOrphan.Value.b = TRUE;

	return ECAndRestriction(
		ECNotRestriction(
			ECAndRestriction(
				ECExistRestriction(PR_EC_DELETED_STORE) +
				ECPropertyRestriction(RELOP_EQ, PR_EC_DELETED_STORE, &sPropOrphan, ECRestriction::Cheap)
			)
		) +
		ECExistRestriction(CHANGE_PROP_TYPE(PROP_STORE_ENTRYIDS, PT_MV_BINARY))
	).CreateMAPIRestriction(lppRestriction, ECRestriction::Full);
}

template<typename string_type, ULONG prAccount>
UserListCollector<string_type, prAccount>::UserListCollector(IMAPISession *lpSession): m_ptrSession(lpSession, true) {}

template<typename string_type, ULONG prAccount>
HRESULT	UserListCollector<string_type, prAccount>::GetRequiredPropTags(LPMAPIPROP /*lpProp*/, LPSPropTagArray *lppPropTagArray) const {
	static constexpr const SizedSPropTagArray(1, sptaDefaultProps) =
		{1, {PR_MAILBOX_OWNER_ENTRYID}};
	return Util::HrCopyPropTagArray(sptaDefaultProps, lppPropTagArray);
}

template<typename string_type, ULONG prAccount>
HRESULT UserListCollector<string_type, prAccount>::CollectData(LPMAPITABLE lpStoreTable) {
	while (true) {
		SRowSetPtr ptrRows;

		HRESULT hr = lpStoreTable->QueryRows(50, 0, &~ptrRows);
		if (hr != hrSuccess)
			return hr;

		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[0].ulPropTag != PR_MAILBOX_OWNER_ENTRYID)
				continue;
			ULONG ulType;
			MAPIPropPtr ptrUser;
			SPropValuePtr ptrAccount;

			auto hrTmp = m_ptrSession->OpenEntry(ptrRows[i].lpProps[0].Value.bin.cb,
			        reinterpret_cast<ENTRYID *>(ptrRows[i].lpProps[0].Value.bin.lpb),
			        &iid_of(ptrUser), 0, &ulType, &~ptrUser);
			if (hrTmp != hrSuccess)
				continue;
			hrTmp = HrGetOneProp(ptrUser, prAccount, &~ptrAccount);
			if (hrTmp != hrSuccess)
				continue;
			push_back(std::move(ptrAccount));
		}

		if (ptrRows.size() < 50)
			break;
	}
	return hrSuccess;
}

template<typename string_type, ULONG prAccount>
void UserListCollector<string_type, prAccount>::move_result(std::list<string_type> *lplstUsers) {
	*lplstUsers = std::move(m_lstUsers);
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
	collector.move_result(lplstUsers);
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
	collector.move_result(lplstUsers);
	return hrSuccess;
}

HRESULT GetMailboxData(IMAPISession *lpMapiSession, const char *lpSSLKey,
    const char *lpSSLPass, bool bLocalOnly, DataCollector *lpCollector)
{
	AddrBookPtr		ptrAdrBook;
	EntryIdPtr		ptrDDEntryID;
	ABContainerPtr ptrDefaultDir, ptrCompanyDir;
	MAPITablePtr	ptrHierarchyTable;
	SRowSetPtr		ptrRows;
	MsgStorePtr		ptrStore;
	ECServiceAdminPtr	ptrServiceAdmin;
	unsigned int ulObj = 0, cbDDEntryID = 0, ulCompanyCount = 0;
	std::set<servername>	listServers;
	convert_context		converter;
	memory_ptr<ECSVRNAMELIST> lpSrvNameList;
	memory_ptr<ECSERVERLIST> lpSrvList;
	static constexpr const SizedSPropTagArray(1, sCols) = {1, {PR_ENTRYID}};

	if (lpMapiSession == nullptr || lpCollector == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpMapiSession->OpenAddressBook(0, &IID_IAddrBook, 0, &~ptrAdrBook);
	if (hr != hrSuccess)
		return kc_perror("Unable to open addressbook", hr);
	hr = ptrAdrBook->GetDefaultDir(&cbDDEntryID, &~ptrDDEntryID);
	if (hr != hrSuccess)
		return kc_perror("Unable to open default addressbook", hr);
	hr = ptrAdrBook->OpenEntry(cbDDEntryID, ptrDDEntryID,
	     &iid_of(ptrDefaultDir), 0, &ulObj, &~ptrDefaultDir);
	if (hr != hrSuccess)
		return kc_perror("Unable to open GAB", hr);
	/* Open Hierarchy Table to see if we are running in multi-tenancy mode or not */
	hr = ptrDefaultDir->GetHierarchyTable(0, &~ptrHierarchyTable);
	if (hr != hrSuccess)
		return kc_perror("Unable to open hierarchy table", hr);
	hr = ptrHierarchyTable->GetRowCount(0, &ulCompanyCount);
	if (hr != hrSuccess)
		return kc_perror("Unable to get hierarchy row count", hr);

	if( ulCompanyCount > 0) {
		hr = ptrHierarchyTable->SetColumns(sCols, TBL_BATCH);
		if (hr != hrSuccess)
			return kc_perror("Unable to set set columns on user table", hr);
		/* multi-tenancy, loop through all subcontainers to find all users */
		hr = ptrHierarchyTable->QueryRows(ulCompanyCount, 0, &~ptrRows);
		if (hr != hrSuccess)
			return hr;
		
		for (unsigned int i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[0].ulPropTag != PR_ENTRYID) {
				ec_log_crit("Unable to get entryid to open tenancy Address Book");
				return MAPI_E_INVALID_PARAMETER;
			}
			hr = ptrAdrBook->OpenEntry(ptrRows[i].lpProps[0].Value.bin.cb,
			     reinterpret_cast<ENTRYID *>(ptrRows[i].lpProps[0].Value.bin.lpb),
			     &iid_of(ptrCompanyDir), 0, &ulObj, &~ptrCompanyDir);
			if (hr != hrSuccess)
				return kc_perror("Unable to open tenancy address book", hr);
			hr = UpdateServerList(ptrCompanyDir, listServers);
			if(hr != hrSuccess) {
				ec_log_crit("Unable to create tenancy server list");
				return hr;
			}
		}
	} else {
		hr = UpdateServerList(ptrDefaultDir, listServers);
		if(hr != hrSuccess) {
			ec_log_crit("Unable to create server list");
			return hr;
		}
	}

	hr = HrOpenDefaultStore(lpMapiSession, &~ptrStore);
	if (hr != hrSuccess)
		return kc_perror("Unable to open default store", hr);
	//@todo use PT_OBJECT to queryinterface
	hr = ptrStore->QueryInterface(IID_IECServiceAdmin, &~ptrServiceAdmin);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(ECSVRNAMELIST), &~lpSrvNameList);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(sizeof(wchar_t *) * listServers.size(), lpSrvNameList, reinterpret_cast<void **>(&lpSrvNameList->lpszaServer));
	if (hr != hrSuccess)
		return hr;

	lpSrvNameList->cServers = 0;
	for (const auto &i : listServers)
		lpSrvNameList->lpszaServer[lpSrvNameList->cServers++] = i.c_str();

	hr = ptrServiceAdmin->GetServerDetails(lpSrvNameList, MAPI_UNICODE, &~lpSrvList);
	if (hr == MAPI_E_NETWORK_ERROR) {
		//support single server
		return GetMailboxDataPerServer(lpMapiSession, "", lpCollector);
	} else if (FAILED(hr)) {
		kc_perror("Unable to get server details", hr);
		if (hr == MAPI_E_NOT_FOUND) {
			ec_log_err("Details for one or more requested servers was not found.");
			ec_log_err("This usually indicates a misconfigured home server for a user.");
			ec_log_err("Requested servers:");
			for (const auto &i : listServers)
				ec_log_err("* %ls", i.c_str());
		}
		return hr;
	}
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
				return hr;
			}
			wszPath = lpSrvList->lpsaServer[i].lpszSslPath;
		}
		hr = GetMailboxDataPerServer(converter.convert_to<std::string>(wszPath).c_str(), lpSSLKey, lpSSLPass, lpCollector);
		if(FAILED(hr)) {
			ec_log_err("Failed to collect data from server: \"%ls\": %s (%x)",
				wszPath, GetMAPIErrorMessage(hr), hr);
			return hr;
		}
	}
	return hrSuccess;
}

HRESULT GetMailboxDataPerServer(const char *lpszPath, const char *lpSSLKey,
    const char *lpSSLPass, DataCollector *lpCollector)
{
	MAPISessionPtr  ptrSessionServer;
	auto hr = HrOpenECAdminSession(&~ptrSessionServer, PROJECT_VERSION,
	          "userutil.cpp:GetMailboxDataPerServer", lpszPath, 0, lpSSLKey,
	          lpSSLPass);
	if(hr != hrSuccess) {
		ec_log_err("Unable to open admin session on server \"%s\": %s (%x)",
			lpszPath, GetMAPIErrorMessage(hr), hr);
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

	HRESULT hr = HrOpenDefaultStore(lpSession, &~ptrStoreAdmin);
	if(hr != hrSuccess) {
		ec_log_err("Unable to open default store on server \"%s\": %s (%x)",
			lpszPath, GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	//@todo use PT_OBJECT to queryinterface
	hr = ptrStoreAdmin->QueryInterface(IID_IExchangeManageStore, &~ptrEMS);
	if (hr != hrSuccess)
		return hr;
	hr = ptrEMS->GetMailboxTable(nullptr, &~ptrStoreTable, MAPI_DEFERRED_ERRORS);
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
	SPropValue sPropUser, sPropDisplayType;
	static constexpr const SizedSPropTagArray(2, sCols) =
		{2, {PR_EC_HOMESERVER_NAME_W, PR_DISPLAY_NAME_W}};

	sPropDisplayType.ulPropTag = PR_DISPLAY_TYPE;
	sPropDisplayType.Value.ul = DT_REMOTE_MAILUSER;

	sPropUser.ulPropTag = PR_OBJECT_TYPE;
	sPropUser.Value.ul = MAPI_MAILUSER;

	HRESULT hr = lpContainer->GetContentsTable(MAPI_DEFERRED_ERRORS, &~ptrTable);
	if (hr != hrSuccess)
		return kc_perror("Unable to open contents table", hr);
	hr = ptrTable->SetColumns(sCols, TBL_BATCH);
	if (hr != hrSuccess)
		return kc_perror("Unable to set set columns on user table", hr);
	// Restrict to users (not groups) 
	hr = ECAndRestriction(
		ECPropertyRestriction(RELOP_NE, PR_DISPLAY_TYPE, &sPropDisplayType, ECRestriction::Cheap) +
		ECPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, &sPropUser, ECRestriction::Cheap))
	.RestrictTable(ptrTable, TBL_BATCH);
	if (hr != hrSuccess)
		return kc_perror("Unable to get total user count", hr);

	while (true) {
		hr = ptrTable->QueryRows(50, 0, &~ptrRows);
		if (hr != hrSuccess)
			return hr;
		if (ptrRows.empty())
			break;

		for (unsigned int i = 0; i < ptrRows.size(); ++i) {
			if (ptrRows[i].lpProps[0].ulPropTag != PR_EC_HOMESERVER_NAME_W)
				continue;
			listServers.emplace(ptrRows[i].lpProps[0].Value.lpszW);
			if (ptrRows[i].lpProps[1].ulPropTag == PR_DISPLAY_NAME_W)
				ec_log_info("User: %ls on server \"%ls\"", ptrRows[i].lpProps[1].Value.lpszW, ptrRows[i].lpProps[0].Value.lpszW);
		}
	}
	return hrSuccess;
}

} /* namespace */
