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
#include <memory>
#include <new>
#include <utility>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "ArchiverSession.h"
#include <kopano/ecversion.h>
#include <kopano/mapi_ptr.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ECRestriction.h>
#include <kopano/CommonUtil.h>
#include <kopano/MAPIErrors.h>
#include <kopano/mapiext.h>
#include <kopano/userutil.h>
#include "ECMsgStore.h"

namespace KC {

/**
 * Create a ArchiverSession object based on the passed configuration and a specific logger
 *
 * @param[in]	lpConfig
 *					The configuration used for creating the session. The values extracted from the
 *					configuration are server_path, sslkey_file and sslkey_path.
 * @param[in]	lpLogger
 *					The logger to which logging will occur.
 * @param[out]	lppSession
 *					Pointer to a Session pointer that will be assigned the address of the returned
 *					ArchiverSession object.
 */
HRESULT ArchiverSession::Create(ECConfig *lpConfig, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession)
{
	if (!lpConfig || !lpLogger)
		return MAPI_E_INVALID_PARAMETER;
	std::unique_ptr<ArchiverSession> lpSession(new(std::nothrow) ArchiverSession(lpLogger));
	if (lpSession == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto hr = lpSession->Init(lpConfig);
	if (FAILED(hr))
		return hr;
	*lpptrSession = std::move(lpSession);
	return hrSuccess;
}

/**
 * Create a Session object based on an existing MAPISession.
 *
 * @param[in]	ptrSession
 *					MAPISessionPtr that points to the MAPISession to contruct a
 *					Session object for.
 * @param[in]	lpLogger
 * 					An ECLogger instance.
 * @param[out]	lppSession
 *					Pointer to a Session pointer that will be assigned the address of the returned
 *					Session object.
 */
HRESULT ArchiverSession::Create(const MAPISessionPtr &ptrSession, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession)
{
	return ArchiverSession::Create(ptrSession, NULL, lpLogger, lpptrSession);
}

/**
 * Create a ArchiverSession object based on an existing MAPISession.
 *
 * @param[in]	ptrSession
 *					MAPISessionPtr that points to the MAPISession to contruct a
 *					ArchiverSession object for.
 * @param[in]	lpConfig
 * 					An ECConfig instance containing sslkey_file and sslkey_pass.
 * @param[in]	lpLogger
 * 					An ECLogger instance.
 * @param[out]	lppSession
 *					Pointer to a ArchiverSession pointer that will be assigned the address of the returned
 *					ArchiverSession object.
 */
HRESULT ArchiverSession::Create(const MAPISessionPtr &ptrSession, ECConfig *lpConfig, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession)
{
	object_ptr<ECLogger> lpLocalLogger;
	const char *lpszSslKeyFile = NULL;
	const char *lpszSslKeyPass = NULL;

	if (lpLogger != nullptr)
		lpLocalLogger.reset(lpLogger);
	else
		lpLocalLogger.reset(new ECLogger_Null(), false);

	if (lpConfig) {
		lpszSslKeyFile = lpConfig->GetSetting("sslkey_file", "", NULL);
		lpszSslKeyPass = lpConfig->GetSetting("sslkey_pass", "", NULL);
	}

	std::unique_ptr<ArchiverSession> lpSession(new(std::nothrow) ArchiverSession(lpLocalLogger));
	if (lpSession == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto hr = lpSession->Init(ptrSession, lpszSslKeyFile, lpszSslKeyPass);
	if (FAILED(hr))
		return hr;
	*lpptrSession = std::move(lpSession);
	return hrSuccess;
}

ArchiverSession::ArchiverSession(ECLogger *lpLogger): m_lpLogger(lpLogger)
{
}

/**
 * Initialize a ArchiverSession object based on the passed configuration.
 *
 * @param[in]	lpConfig
 *					The configuration used for creating the ArchiverSession. The values extracted from the
 *					configuration are server_path, sslkey_file and sslkey_path.
 */
HRESULT ArchiverSession::Init(ECConfig *lpConfig)
{
	return Init(GetServerUnixSocket(lpConfig->GetSetting("server_socket")),
				lpConfig->GetSetting("sslkey_file", "", NULL),
				lpConfig->GetSetting("sslkey_pass", "", NULL));
}

/**
 * Initialize a ArchiverSession object.
 *
 * @param[in]	lpszServerPath
 *					The path to use to connect with the server.
 * @param[in]	lpszSslPath
 * 					The path to the certificate.
 * @param[in]
 * 				lpszSslPass
 * 					The password for the certificate.
 */
HRESULT ArchiverSession::Init(const char *lpszServerPath, const char *lpszSslPath, const char *lpszSslPass)
{
	auto hr = HrOpenECAdminSession(&~m_ptrSession, PROJECT_VERSION,
	          "archiver:system", const_cast<char *>(lpszServerPath),
	          EC_PROFILE_FLAGS_NO_NOTIFICATIONS, const_cast<char *>(lpszSslPath),
	          const_cast<char *>(lpszSslPass));
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Unable to open Admin ArchiverSession.");
		switch (hr) {
		case MAPI_E_NETWORK_ERROR:
			m_lpLogger->logf(EC_LOGLEVEL_INFO, "The server is not running, or not accessible through \"%s\".", lpszServerPath);
			break;
		case MAPI_E_LOGON_FAILED:
		case MAPI_E_NO_ACCESS:
			m_lpLogger->logf(EC_LOGLEVEL_INFO, "Access was denied on \"%s\".", lpszServerPath);
			break;
		default:
			m_lpLogger->perr("Other cause", hr);
			break;
		};
		return hr;
	}

	hr = HrOpenDefaultStore(m_ptrSession, MDB_NO_MAIL | MDB_TEMPORARY, &~m_ptrAdminStore);
	if (hr != hrSuccess) {
		m_lpLogger->perr("Unable to open Admin store", hr);
		return hr;
	}

	if (lpszSslPath)
		m_strSslPath = lpszSslPath;

	if (lpszSslPass)
		m_strSslPass = lpszSslPass;

	return hrSuccess;
}

/**
 * Initialize a ArchiverSession object based on an existing MAPISession.
 *
 * @param[in]	ptrSession
 *					MAPISessionPtr that points to the MAPISession to contruct this
 *					ArchiverSession object for.
 */
HRESULT ArchiverSession::Init(const MAPISessionPtr &ptrSession, const char *lpszSslPath, const char *lpszSslPass)
{
	m_ptrSession = ptrSession;
	auto hr = HrOpenDefaultStore(m_ptrSession, &~m_ptrAdminStore);
	if (hr != hrSuccess)
		return hr;

	if (lpszSslPath)
		m_strSslPath = lpszSslPath;

	if (lpszSslPass)
		m_strSslPass = lpszSslPass;

	return hrSuccess;
}

/**
 * Open a message store based on a username.
 *
 * @param[in]	strUser
 *					The username of the user for which to open the store.
 * @param[out]	lppMsgStore
 *					Pointer to a IMsgStore pointer that will be assigned the
 *					address of the returned message store.
 */ 
HRESULT ArchiverSession::OpenStoreByName(const tstring &strUser, LPMDB *lppMsgStore)
{
	ExchangeManageStorePtr ptrEMS;
	MsgStorePtr ptrUserStore;
	ULONG cbEntryId = 0;
	EntryIdPtr ptrEntryId;
	
	auto hr = m_ptrAdminStore->QueryInterface(iid_of(ptrEMS), &~ptrEMS);
	if (hr != hrSuccess) {
		m_lpLogger->perr("Failed to get EMS interface", hr);
		return hr;
	}
	hr = ptrEMS->CreateStoreEntryID(nullptr, reinterpret_cast<const TCHAR *>(strUser.c_str()), fMapiUnicode, &cbEntryId, &~ptrEntryId);
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_INFO, "Failed to create store entryid for user \"" TSTRING_PRINTF "\": %s (%x)",
			strUser.c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	hr = m_ptrSession->OpenMsgStore(0, cbEntryId, ptrEntryId, &iid_of(ptrUserStore), MDB_WRITE | fMapiDeferredErrors | MDB_NO_MAIL | MDB_TEMPORARY, &~ptrUserStore);
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_INFO, "Failed to open store for user \"" TSTRING_PRINTF "\": %s (%x)",
			strUser.c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}
		
	return ptrUserStore->QueryInterface(IID_IMsgStore,
		reinterpret_cast<LPVOID *>(lppMsgStore));
}

/**
 * Open a message store with best access.
 *
 * @param[in]	sEntryId
 *					The entryid of the store to open.
 * @param[in]	ulFlags
 * 					Flags that are passed on to OpenMsgStore
 * @param[out]	lppMsgStore
 *					Pointer to a IMsgStore pointer that will be assigned the
 *					address of the returned message store.
 */ 
HRESULT ArchiverSession::OpenStore(const entryid_t &sEntryId, ULONG ulFlags, LPMDB *lppMsgStore)
{
	MsgStorePtr ptrUserStore;
	ArchiverSessionPtr ptrSession;
	
	if (!sEntryId.isWrapped()) {
		auto hr = m_ptrSession->OpenMsgStore(0, sEntryId.size(), sEntryId, &iid_of(ptrUserStore), ulFlags, &~ptrUserStore);
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_INFO, "Failed to open store by entryid \"%s\": %s (%x)",
				sEntryId.tostring().c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}
		return ptrUserStore->QueryInterface(IID_IMsgStore, reinterpret_cast<void **>(lppMsgStore));
	}

	entryid_t sTempEntryId = sEntryId;
	std::string strPath;
	sTempEntryId.unwrap(&strPath);
	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Archive store entryid is wrapped.");
	auto hr = CreateRemote(strPath.c_str(), m_lpLogger, &ptrSession);
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_INFO, "Failed to create ArchiverSession on \"%s\": %s (%x)",
			strPath.c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	return ptrSession->OpenStore(sTempEntryId, ulFlags, lppMsgStore);
}

/**
 * Open a message store with best access.
 *
 * @param[in]	sEntryId
 *					The entryid of the store to open.
 * @param[out]	lppMsgStore
 *					Pointer to a IMsgStore pointer that will be assigned the
 *					address of the returned message store.
 */ 
HRESULT ArchiverSession::OpenStore(const entryid_t &sEntryId, LPMDB *lppMsgStore)
{
	return OpenStore(sEntryId, MDB_WRITE|fMapiDeferredErrors|MDB_NO_MAIL|MDB_TEMPORARY, lppMsgStore);
}

/**
 * Open a message store with read-only access.
 *
 * @param[in]	sEntryId
 *					The entryid of the store to open.
 * @param[out]	lppMsgStore
 *					Pointer to a IMsgStore pointer that will be assigned the
 *					address of the returned message store.
 */ 
HRESULT ArchiverSession::OpenReadOnlyStore(const entryid_t &sEntryId, LPMDB *lppMsgStore)
{
	return OpenStore(sEntryId, fMapiDeferredErrors|MDB_NO_MAIL|MDB_TEMPORARY, lppMsgStore);
}

/**
 * Resolve a user and return its username and entryid.
 *
 * @param[in]	strUser
 *					The user to resolve.
 * @param[out]	lpsEntryId
 *					Pointer to a entryid_t that will be populated with the entryid
 *					of the resovled user. This argument can be set to NULL if the entryid
 *					is not required.
 * @param[out]	lpstrFullname
 *					Pointer to a std::string that will be populated with the full name
 *					of the resolved user. This argument can be set to NULL if the full name
 *					is not required.
 * @param[out]	lpbAclCapable
 * 					Pointer to a boolean that will be set to true if the user is ACL capable.
 * 					This argument can be set to NULL if the active/non-active
 * 					information is not required.
 */
HRESULT ArchiverSession::GetUserInfo(const tstring &strUser, abentryid_t *lpsEntryId, tstring *lpstrFullname, bool *lpbAclCapable)
{
	MsgStorePtr ptrStore;
	ECServiceAdminPtr ptrServiceAdmin;
	ULONG cbEntryId = 0;
	EntryIdPtr ptrEntryId;

	auto hr = HrOpenDefaultStore(m_ptrSession, &~ptrStore);
	if (hr != hrSuccess) {
		m_lpLogger->perr("Failed to open default store", hr);
		return hr;
	}

	hr = ptrStore.QueryInterface(ptrServiceAdmin);
	if (hr != hrSuccess) {
		m_lpLogger->perr("Failed to obtain the serviceadmin interface", hr);
		return hr;
	}
	hr = ptrServiceAdmin->ResolveUserName((LPCTSTR)strUser.c_str(), fMapiUnicode, &cbEntryId, &~ptrEntryId);
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_INFO, "Failed to resolve user \"" TSTRING_PRINTF "\": %s (%x)",
			strUser.c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	if (lpstrFullname || lpbAclCapable) {
		ULONG ulType = 0;
		MailUserPtr ptrUser;
		ULONG cValues = 0;
		SPropArrayPtr ptrUserProps;
		static constexpr const SizedSPropTagArray(2, sptaUserProps) =
			{2, {PR_DISPLAY_NAME, PR_DISPLAY_TYPE_EX}};
		enum {IDX_DISPLAY_NAME, IDX_DISPLAY_TYPE_EX};

		hr = m_ptrSession->OpenEntry(cbEntryId, ptrEntryId, &IID_IMailUser, 0, &ulType, &~ptrUser);
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_INFO, "Failed to open user object for user \"" TSTRING_PRINTF "\": %s (%x)",
				strUser.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}
		hr = ptrUser->GetProps(sptaUserProps, 0, &cValues, &~ptrUserProps);
		if (FAILED(hr)) {
			m_lpLogger->logf(EC_LOGLEVEL_INFO, "Failed to obtain properties from user \"" TSTRING_PRINTF "\": %s (%x)",
				strUser.c_str(), GetMAPIErrorMessage(hr), hr);
			return hr;
		}

		if (lpstrFullname) {
			if (ptrUserProps[IDX_DISPLAY_NAME].ulPropTag != PR_DISPLAY_NAME) {
				hr = ptrUserProps[IDX_DISPLAY_NAME].Value.err;
				m_lpLogger->logf(EC_LOGLEVEL_INFO, "Failed to obtain the display name for user \"" TSTRING_PRINTF "\": %s (%x)",
					strUser.c_str(), GetMAPIErrorMessage(hr), hr);
				return hr;
			}

			lpstrFullname->assign(ptrUserProps[IDX_DISPLAY_NAME].Value.LPSZ);
		}
		
		if (lpbAclCapable) {
			if (ptrUserProps[IDX_DISPLAY_TYPE_EX].ulPropTag != PR_DISPLAY_TYPE_EX) {
				hr = ptrUserProps[IDX_DISPLAY_TYPE_EX].Value.err;
				m_lpLogger->logf(EC_LOGLEVEL_INFO, "Failed to obtain the type for user \"" TSTRING_PRINTF "\": %s (%x)",
					strUser.c_str(), GetMAPIErrorMessage(hr), hr);
				return hr;
			}

			*lpbAclCapable = (ptrUserProps[IDX_DISPLAY_TYPE_EX].Value.ul & DTE_FLAG_ACL_CAPABLE);
		}
	}

	if (lpsEntryId)
		lpsEntryId->assign(cbEntryId, ptrEntryId);
	return hr;
}

HRESULT ArchiverSession::GetUserInfo(const abentryid_t &sEntryId, tstring *lpstrUser, tstring *lpstrFullname)
{
	ULONG ulType = 0;
	MAPIPropPtr ptrUser;
	ULONG cUserProps = 0;
	SPropArrayPtr ptrUserProps;
	static constexpr const SizedSPropTagArray(2, sptaUserProps) =
		{2, {PR_ACCOUNT, PR_DISPLAY_NAME}};
	enum {IDX_ACCOUNT, IDX_DISPLAY_NAME};

	auto hr = m_ptrSession->OpenEntry(sEntryId.size(), sEntryId, &iid_of(ptrUser), MAPI_DEFERRED_ERRORS, &ulType, &~ptrUser);
	if (hr != hrSuccess)
		return hr;
	hr = ptrUser->GetProps(sptaUserProps, 0, &cUserProps, &~ptrUserProps);
	if (FAILED(hr))
		return hr;

	if (lpstrUser) {
		if (PROP_TYPE(ptrUserProps[IDX_ACCOUNT].ulPropTag) != PT_ERROR)
			lpstrUser->assign(ptrUserProps[IDX_ACCOUNT].Value.LPSZ);
		else
			lpstrUser->assign(KC_T("<Unknown>"));
	}

	if (lpstrFullname) {
		if (PROP_TYPE(ptrUserProps[IDX_DISPLAY_NAME].ulPropTag) != PT_ERROR)
			lpstrFullname->assign(ptrUserProps[IDX_DISPLAY_NAME].Value.LPSZ);
		else
			lpstrFullname->assign(KC_T("<Unknown>"));
	}
	return hrSuccess;
}

/**
 * Get the global address list.
 *
 * @param[out]	lppAbContainer
 *					Pointer to a IABContainer pointer that will be assigned the
 *					address of the returned addressbook container.
 */
HRESULT ArchiverSession::GetGAL(LPABCONT *lppAbContainer)
{
	AddrBookPtr		ptrAdrBook;
	ABContainerPtr	ptrABRootContainer;
	ABContainerPtr	ptrGAL;
	MAPITablePtr	ptrABRCTable;
	SRowSetPtr		ptrRows;
	ULONG			ulType = 0;
	static constexpr const SizedSPropTagArray(1, sGALProps) = {1, {PR_ENTRYID}};
	SPropValue			  sGALPropVal = {0};

	auto hr = m_ptrSession->OpenAddressBook(0, &iid_of(ptrAdrBook), AB_NO_DIALOG, &~ptrAdrBook);
	if (hr != hrSuccess)
		return hr;
	hr = ptrAdrBook->OpenEntry(0, nullptr, &iid_of(ptrABRootContainer), MAPI_BEST_ACCESS, &ulType, &~ptrABRootContainer);
	if (hr != hrSuccess)
		return hr;
	hr = ptrABRootContainer->GetHierarchyTable(0, &~ptrABRCTable);
	if (hr != hrSuccess)
		return hr;

	sGALPropVal.ulPropTag = PR_AB_PROVIDER_ID;
	sGALPropVal.Value.bin.cb = sizeof(GUID);
	sGALPropVal.Value.bin.lpb = (LPBYTE)&MUIDECSAB;

	hr = ptrABRCTable->SetColumns(sGALProps, TBL_BATCH); 
	if (hr != hrSuccess)
		return hr;
	hr = ECPropertyRestriction(RELOP_EQ, PR_AB_PROVIDER_ID, &sGALPropVal, ECRestriction::Cheap)
	     .RestrictTable(ptrABRCTable, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ptrABRCTable->QueryRows(1, 0, &~ptrRows);
	if (hr != hrSuccess)
		return hr;
	if (ptrRows.size() != 1 || ptrRows[0].lpProps[0].ulPropTag != PR_ENTRYID)
		return MAPI_E_NOT_FOUND;

	hr = ptrAdrBook->OpenEntry(ptrRows[0].lpProps[0].Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(ptrRows[0].lpProps[0].Value.bin.lpb), &iid_of(ptrGAL),
	     MAPI_BEST_ACCESS, &ulType, &~ptrGAL);
	if (hr != hrSuccess)
		return hr;

	return ptrGAL->QueryInterface(IID_IABContainer,
		reinterpret_cast<void **>(lppAbContainer));
}

/**
 * Check if two MsgStorePtr point to the same store by comparing their entryids.
 * This function is used to make sure a store is not attached to itself as archive (hence the argument names)
 *
 * @param[in]	lpUserStore
 *					MsgStorePtr that points to the user store.
 * @param[in]	lpArchiveStore
 *					MsgStorePtr that points to the archive store.
 * @param[out]	lpbResult
 *					Pointer to a boolean that will be set to true if the two stores
 *					reference the same store.
 */					
HRESULT ArchiverSession::CompareStoreIds(LPMDB lpUserStore, LPMDB lpArchiveStore, bool *lpbResult)
{
	SPropValuePtr ptrUserStoreEntryId;
	SPropValuePtr ptrArchiveStoreEntryId;
	ULONG ulResult = 0;

	auto hr = HrGetOneProp(lpUserStore, PR_ENTRYID, &~ptrUserStoreEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(lpArchiveStore, PR_ENTRYID, &~ptrArchiveStoreEntryId);
	if (hr != hrSuccess)
		return hr;
	
	hr = m_ptrSession->CompareEntryIDs(ptrUserStoreEntryId->Value.bin.cb, (LPENTRYID)ptrUserStoreEntryId->Value.bin.lpb,
					   ptrArchiveStoreEntryId->Value.bin.cb, (LPENTRYID)ptrArchiveStoreEntryId->Value.bin.lpb,
					   0, &ulResult);
	if (hr != hrSuccess)
		return hr;
		
	*lpbResult = (ulResult == TRUE);
	return hrSuccess;
}

/**
 * Compare two store entryids to see if they reference the same store.
 *
 * @param[in]	sEntryId1
 *					Entryid 1.
 * @param[in]	sEntryId2
 *					Entryid 2.
 * @param[out]	lpbResult
 *					Pointer to a boolean that will be set to true if the two ids
 *					reference the same store.
 */					
HRESULT ArchiverSession::CompareStoreIds(const entryid_t &sEntryId1, const entryid_t &sEntryId2, bool *lpbResult)
{
	ULONG ulResult = 0;
	auto hr = m_ptrSession->CompareEntryIDs(sEntryId1.size(), sEntryId1,
	          sEntryId2.size(), sEntryId2, 0, &ulResult);
	if (hr != hrSuccess)
		return hr;
		
	*lpbResult = (ulResult == TRUE);
	return hrSuccess;
}

/**
 * Create a ArchiverSession on another server, with the same credentials (SSL) as the current ArchiverSession.
 * 
 * @param[in]	lpszServerPath	The path of the server to connect with.
 * @param[in]	lpLogger		THe logger to log to.
 * @param[ou]t	lppSession		The returned ArchiverSession.
 * 
 * @retval	hrSuccess	The new ArchiverSession was successfully created.
 */
HRESULT ArchiverSession::CreateRemote(const char *lpszServerPath, ECLogger *lpLogger, ArchiverSessionPtr *lpptrSession)
{
	std::unique_ptr<ArchiverSession> lpSession(new(std::nothrow) ArchiverSession(lpLogger));
	if (lpSession == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	HRESULT hr = lpSession->Init(lpszServerPath, m_strSslPath.c_str(), m_strSslPass.c_str());
	if (FAILED(hr))
		return hr;
	*lpptrSession = std::move(lpSession);
	return hrSuccess;
}

HRESULT ArchiverSession::OpenMAPIProp(ULONG cbEntryID, LPENTRYID lpEntryID, LPMAPIPROP *lppProp)
{
	ULONG ulType = 0;
	MAPIPropPtr ptrMapiProp;

	auto hr = m_ptrSession->OpenEntry(cbEntryID, lpEntryID, &iid_of(ptrMapiProp),
	          MAPI_BEST_ACCESS|fMapiDeferredErrors, &ulType, &~ptrMapiProp);
	if (hr != hrSuccess)
		return hr;

	return ptrMapiProp->QueryInterface(IID_IMAPIProp,
		reinterpret_cast<LPVOID *>(lppProp));
}

const char *ArchiverSession::GetSSLPath() const {
	return m_strSslPath.c_str();
}

const char *ArchiverSession::GetSSLPass() const {
	return m_strSslPass.c_str();
}

HRESULT ArchiverSession::OpenOrCreateArchiveStore(const tstring& strUserName, const tstring& strServerName, LPMDB *lppArchiveStore)
{
	ECServiceAdminPtr ptrServiceAdmin;
	ULONG cbStoreId;
	EntryIdPtr ptrStoreId;
	MsgStorePtr ptrArchiveStore;

	auto hr = m_ptrAdminStore.QueryInterface(ptrServiceAdmin);
	if (hr != hrSuccess)
		return hr;
	hr = ptrServiceAdmin->GetArchiveStoreEntryID(strUserName.c_str(), strServerName.c_str(), fMapiUnicode, &cbStoreId, &~ptrStoreId);
	if (hr == hrSuccess)
		hr = m_ptrSession->OpenMsgStore(0, cbStoreId, ptrStoreId, &iid_of(ptrArchiveStore), MDB_WRITE, &~ptrArchiveStore);
	else if (hr == MAPI_E_NOT_FOUND)
		hr = CreateArchiveStore(strUserName, strServerName, &~ptrArchiveStore);
	if (hr != hrSuccess)
		return hr;

	return ptrArchiveStore->QueryInterface(IID_IMsgStore,
		reinterpret_cast<LPVOID *>(lppArchiveStore));
}

HRESULT ArchiverSession::GetArchiveStoreEntryId(const tstring& strUserName, const tstring& strServerName, entryid_t *lpArchiveId)
{
	ECServiceAdminPtr ptrServiceAdmin;
	ULONG cbStoreId;
	EntryIdPtr ptrStoreId;

	auto hr = m_ptrAdminStore.QueryInterface(ptrServiceAdmin);
	if (hr != hrSuccess)
		return hr;
	hr = ptrServiceAdmin->GetArchiveStoreEntryID(strUserName.c_str(), strServerName.c_str(), fMapiUnicode, &cbStoreId, &~ptrStoreId);
	if (hr != hrSuccess)
		return hr;

	lpArchiveId->assign(cbStoreId, ptrStoreId);
	return hrSuccess;
}

HRESULT ArchiverSession::CreateArchiveStore(const tstring& strUserName, const tstring& strServerName, LPMDB *lppArchiveStore)
{
	MsgStorePtr ptrRemoteAdminStore;
	ECServiceAdminPtr ptrRemoteServiceAdmin;
	abentryid_t userId;
	ULONG cbStoreId = 0;
	EntryIdPtr ptrStoreId;
	ULONG cbRootId = 0;
	EntryIdPtr ptrRootId;
	MsgStorePtr ptrArchiveStore;
	MAPIFolderPtr ptrRoot;
	ULONG ulType;
	MAPIFolderPtr ptrIpmSubtree;
	SPropValuePtr ptrIpmSubtreeId;

	auto hr = GetUserInfo(strUserName, &userId, nullptr, nullptr);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetRemoteAdminStore(m_ptrSession, m_ptrAdminStore, strServerName.c_str(), fMapiUnicode, &~ptrRemoteAdminStore);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRemoteAdminStore.QueryInterface(ptrRemoteServiceAdmin);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRemoteServiceAdmin->CreateEmptyStore(ECSTORE_TYPE_ARCHIVE, userId.size(), userId, EC_OVERRIDE_HOMESERVER, &cbStoreId, &~ptrStoreId, &cbRootId, &~ptrRootId);
	if (hr != hrSuccess)
		return hr;

	// The entryids returned from CreateEmptyStore are unwrapped and unusable from external client code. So
	// we'll resolve the correct entryids through GetArchiveStoreEntryID.
	hr = ptrRemoteServiceAdmin->GetArchiveStoreEntryID(strUserName.c_str(), strServerName.c_str(), fMapiUnicode, &cbStoreId, &~ptrStoreId);
	if (hr != hrSuccess)
		return hr;
	hr = m_ptrSession->OpenMsgStore(0, cbStoreId, ptrStoreId, &iid_of(ptrArchiveStore), MDB_WRITE, &~ptrArchiveStore);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveStore->OpenEntry(0, nullptr, &iid_of(ptrRoot), MAPI_MODIFY, &ulType, &~ptrRoot);
	if (hr != hrSuccess)
		return hr;

	hr = ptrRoot->CreateFolder(FOLDER_GENERIC,
	     const_cast<TCHAR *>(KC_T("IPM_SUBTREE")),
	     const_cast<TCHAR *>(KC_T("")), &IID_IMAPIFolder, fMapiUnicode,
	     &~ptrIpmSubtree);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrIpmSubtree, PR_ENTRYID, &~ptrIpmSubtreeId);
	if (hr != hrSuccess)
		return hr;

	ptrIpmSubtreeId->ulPropTag = PR_IPM_SUBTREE_ENTRYID;
	
	hr = ptrArchiveStore->SetProps(1, ptrIpmSubtreeId, NULL);
	if (hr != hrSuccess)
		return hr;

	return ptrArchiveStore->QueryInterface(IID_IMsgStore,
		reinterpret_cast<LPVOID *>(lppArchiveStore));
}

} /* namespace */
