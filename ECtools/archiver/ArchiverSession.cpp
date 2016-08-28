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

#include <kopano/platform.h>
#include "ArchiverSession.h"
#include <kopano/ecversion.h>
#include <kopano/mapi_ptr.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/CommonUtil.h>
#include <kopano/mapiext.h>
#include <kopano/userutil.h>
#include "ECMsgStore.h"

typedef mapi_memory_ptr<ECSERVERLIST> ECServerListPtr;

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
	HRESULT hr;
	ArchiverSession *lpSession = NULL;

	if (!lpConfig || !lpLogger)
		return MAPI_E_INVALID_PARAMETER;
	
	lpSession = new ArchiverSession(lpLogger);
	hr = lpSession->Init(lpConfig);
	if (FAILED(hr)) {
		delete lpSession;
		return hr;
	}
	
	lpptrSession->reset(lpSession);
	return hr;
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
	HRESULT hr = hrSuccess;
	ArchiverSession *lpSession = NULL;
	ECLogger *lpLocalLogger = lpLogger;
	const char *lpszSslKeyFile = NULL;
	const char *lpszSslKeyPass = NULL;

	if (lpLocalLogger != NULL)
		lpLocalLogger->AddRef();
	else
		lpLocalLogger = new ECLogger_Null();

	if (lpConfig) {
		lpszSslKeyFile = lpConfig->GetSetting("sslkey_file", "", NULL);
		lpszSslKeyPass = lpConfig->GetSetting("sslkey_pass", "", NULL);
	}

	lpSession = new ArchiverSession(lpLocalLogger);
	hr = lpSession->Init(ptrSession, lpszSslKeyFile, lpszSslKeyPass);
	if (FAILED(hr)) {
		delete lpSession;
		goto exit;
	}

	lpptrSession->reset(lpSession);

exit:
	lpLocalLogger->Release();
	return hr;
}

ArchiverSession::~ArchiverSession()
{
	m_lpLogger->Release();
}

ArchiverSession::ArchiverSession(ECLogger *lpLogger): m_lpLogger(lpLogger)
{
	m_lpLogger->AddRef();
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
	HRESULT hr;
	
	hr = HrOpenECAdminSession(m_lpLogger, &m_ptrSession, "kopano-archiver:system", PROJECT_SVN_REV_STR, (char*)lpszServerPath, EC_PROFILE_FLAGS_NO_NOTIFICATIONS, (char*)lpszSslPath, (char*)lpszSslPass);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Unable to open Admin ArchiverSession.");
		switch (hr) {
		case MAPI_E_NETWORK_ERROR:
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "The server is not running, or not accessible through %s.", lpszServerPath);
			break;
		case MAPI_E_LOGON_FAILED:
		case MAPI_E_NO_ACCESS:
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "Access was denied on %s.", lpszServerPath);
			break;
		default:
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "Unknown cause (hr=%s).", stringify(hr,true).c_str());
			break;
		};
		return hr;
	}

	hr = HrOpenDefaultStore(m_ptrSession, MDB_NO_MAIL | MDB_TEMPORARY, &m_ptrAdminStore);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Unable to open Admin store (hr=%s).", stringify(hr,true).c_str());
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
	HRESULT hr;

	m_ptrSession = ptrSession;

	hr = HrOpenDefaultStore(m_ptrSession, &m_ptrAdminStore);
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
	HRESULT hr;
	ExchangeManageStorePtr ptrEMS;
	MsgStorePtr ptrUserStore;
	ULONG cbEntryId = 0;
	EntryIdPtr ptrEntryId;
	
	hr = m_ptrAdminStore->QueryInterface(ptrEMS.iid, &ptrEMS);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to get EMS interface (hr=%s).", stringify(hr, true).c_str());
		return hr;
	}
	
	hr = ptrEMS->CreateStoreEntryID(NULL, (LPTSTR)strUser.c_str(), fMapiUnicode, &cbEntryId, &ptrEntryId);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to create store entryid for user '" TSTRING_PRINTF "' (hr=%s).", strUser.c_str(), stringify(hr, true).c_str());
		return hr;
	}
		
	hr = m_ptrSession->OpenMsgStore(0, cbEntryId, ptrEntryId, &ptrUserStore.iid, MDB_WRITE|fMapiDeferredErrors|MDB_NO_MAIL|MDB_TEMPORARY, &ptrUserStore);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to open store for user '" TSTRING_PRINTF "' (hr=%s).", strUser.c_str(), stringify(hr, true).c_str());
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
	HRESULT hr;
	MsgStorePtr ptrUserStore;
	ArchiverSessionPtr ptrSession;
	
	if (sEntryId.isWrapped()) {
		entryid_t sTempEntryId = sEntryId;
		std::string	strPath;

		sTempEntryId.unwrap(&strPath);

		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Archive store entryid is wrapped.");
		
		hr = CreateRemote(strPath.c_str(), m_lpLogger, &ptrSession);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to create ArchiverSession on '%s' (hr=%s)", strPath.c_str(), stringify(hr, true).c_str());
			return hr;
		}
		
		hr = ptrSession->OpenStore(sTempEntryId, ulFlags, lppMsgStore);		
	} else {	
		hr = m_ptrSession->OpenMsgStore(0, sEntryId.size(), sEntryId, &ptrUserStore.iid, ulFlags, &ptrUserStore);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to open store. (entryid=%s, hr=%s)", sEntryId.tostring().c_str(), stringify(hr, true).c_str());
			return hr;
		}
			
		hr = ptrUserStore->QueryInterface(IID_IMsgStore, (LPVOID*)lppMsgStore);
	}
	return hr;
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
	HRESULT hr;
	MsgStorePtr ptrStore;
	ECServiceAdminPtr ptrServiceAdmin;
	ULONG cbEntryId = 0;
	EntryIdPtr ptrEntryId;

	hr = HrOpenDefaultStore(m_ptrSession, &ptrStore);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to open default store (hr=%s)", stringify(hr, true).c_str());
		return hr;
	}

	hr = ptrStore.QueryInterface(ptrServiceAdmin);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to obtain the serviceadmin interface (hr=%s)", stringify(hr, true).c_str());
		return hr;
	}

	hr = ptrServiceAdmin->ResolveUserName((LPCTSTR)strUser.c_str(), fMapiUnicode, &cbEntryId, &ptrEntryId);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to resolve user '" TSTRING_PRINTF "' (hr=%s)", strUser.c_str(), stringify(hr, true).c_str());
		return hr;
	}

	if (lpstrFullname || lpbAclCapable) {
		ULONG ulType = 0;
		MailUserPtr ptrUser;
		ULONG cValues = 0;
		SPropArrayPtr ptrUserProps;

		SizedSPropTagArray(2, sptaUserProps) = {2, {PR_DISPLAY_NAME, PR_DISPLAY_TYPE_EX}};
		enum {IDX_DISPLAY_NAME, IDX_DISPLAY_TYPE_EX};

		hr = m_ptrSession->OpenEntry(cbEntryId, ptrEntryId, &IID_IMailUser, 0, &ulType, &ptrUser);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to open user object for user '" TSTRING_PRINTF "' (hr=%s)", strUser.c_str(), stringify(hr, true).c_str());
			return hr;
		}

		hr = ptrUser->GetProps((LPSPropTagArray)&sptaUserProps, 0, &cValues, &ptrUserProps);
		if (FAILED(hr)) {
			m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to obtain properties from user '" TSTRING_PRINTF "' (hr=0x%08x)", strUser.c_str(), hr);
			return hr;
		}

		if (lpstrFullname) {
			if (ptrUserProps[IDX_DISPLAY_NAME].ulPropTag != PR_DISPLAY_NAME) {
				hr = ptrUserProps[IDX_DISPLAY_NAME].Value.err;
				m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to obtain the display name for user '" TSTRING_PRINTF "' (hr=%s)", strUser.c_str(), stringify(hr, true).c_str());
				return hr;
			}

			lpstrFullname->assign(ptrUserProps[IDX_DISPLAY_NAME].Value.LPSZ);
		}
		
		if (lpbAclCapable) {
			if (ptrUserProps[IDX_DISPLAY_TYPE_EX].ulPropTag != PR_DISPLAY_TYPE_EX) {
				hr = ptrUserProps[IDX_DISPLAY_TYPE_EX].Value.err;
				m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to obtain the type for user '" TSTRING_PRINTF "' (hr=%s)", strUser.c_str(), stringify(hr, true).c_str());
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
	HRESULT hr;
	ULONG ulType = 0;
	MAPIPropPtr ptrUser;
	ULONG cUserProps = 0;
	SPropArrayPtr ptrUserProps;

	SizedSPropTagArray(2, sptaUserProps) = {2, {PR_ACCOUNT, PR_DISPLAY_NAME}};
	enum {IDX_ACCOUNT, IDX_DISPLAY_NAME};

	hr = m_ptrSession->OpenEntry(sEntryId.size(), sEntryId, NULL, MAPI_DEFERRED_ERRORS, &ulType, &ptrUser);
	if (hr != hrSuccess)
		return hr;
	hr = ptrUser->GetProps((LPSPropTagArray)&sptaUserProps, 0, &cUserProps, &ptrUserProps);
	if (FAILED(hr))
		return hr;

	if (lpstrUser) {
		if (PROP_TYPE(ptrUserProps[IDX_ACCOUNT].ulPropTag) != PT_ERROR)
			lpstrUser->assign(ptrUserProps[IDX_ACCOUNT].Value.LPSZ);
		else
			lpstrUser->assign(_T("<Unknown>"));
	}

	if (lpstrFullname) {
		if (PROP_TYPE(ptrUserProps[IDX_DISPLAY_NAME].ulPropTag) != PT_ERROR)
			lpstrFullname->assign(ptrUserProps[IDX_DISPLAY_NAME].Value.LPSZ);
		else
			lpstrFullname->assign(_T("<Unknown>"));
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
	HRESULT hr;
	AddrBookPtr		ptrAdrBook;
	ABContainerPtr	ptrABRootContainer;
	ABContainerPtr	ptrGAL;
	MAPITablePtr	ptrABRCTable;
	SRowSetPtr		ptrRows;
	ULONG			ulType = 0;

	SizedSPropTagArray(1, sGALProps) = {1, {PR_ENTRYID}};
	SPropValue			  sGALPropVal = {0};
	SRestriction		  sGALRestrict = {0};

	hr = m_ptrSession->OpenAddressBook(0, &ptrAdrBook.iid, AB_NO_DIALOG, &ptrAdrBook);
	if (hr != hrSuccess)
		return hr;
	hr = ptrAdrBook->OpenEntry(0, NULL, &ptrABRootContainer.iid, MAPI_BEST_ACCESS, &ulType, (LPUNKNOWN*)&ptrABRootContainer);
	if (hr != hrSuccess)
		return hr;
	hr = ptrABRootContainer->GetHierarchyTable(0, &ptrABRCTable);
	if (hr != hrSuccess)
		return hr;

	sGALRestrict.rt = RES_PROPERTY;
	sGALRestrict.res.resProperty.relop = RELOP_EQ;
	sGALRestrict.res.resProperty.ulPropTag = PR_AB_PROVIDER_ID;
	sGALRestrict.res.resProperty.lpProp = &sGALPropVal;

	sGALPropVal.ulPropTag = PR_AB_PROVIDER_ID;
	sGALPropVal.Value.bin.cb = sizeof(GUID);
	sGALPropVal.Value.bin.lpb = (LPBYTE)&MUIDECSAB;

	hr = ptrABRCTable->SetColumns((LPSPropTagArray)&sGALProps, TBL_BATCH); 
	if (hr != hrSuccess)
		return hr;
	hr = ptrABRCTable->Restrict(&sGALRestrict, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ptrABRCTable->QueryRows(1, 0, &ptrRows);
	if (hr != hrSuccess)
		return hr;
	if (ptrRows.size() != 1 || ptrRows[0].lpProps[0].ulPropTag != PR_ENTRYID)
		return MAPI_E_NOT_FOUND;

	hr = ptrAdrBook->OpenEntry(ptrRows[0].lpProps[0].Value.bin.cb, (LPENTRYID)ptrRows[0].lpProps[0].Value.bin.lpb,
							&ptrGAL.iid, MAPI_BEST_ACCESS, &ulType, (LPUNKNOWN*)&ptrGAL);
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
	HRESULT hr;
	SPropValuePtr ptrUserStoreEntryId;
	SPropValuePtr ptrArchiveStoreEntryId;
	ULONG ulResult = 0;

	hr = HrGetOneProp(lpUserStore, PR_ENTRYID, &ptrUserStoreEntryId);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(lpArchiveStore, PR_ENTRYID, &ptrArchiveStoreEntryId);
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
	HRESULT hr;
	ULONG ulResult = 0;

	hr = m_ptrSession->CompareEntryIDs(sEntryId1.size(), sEntryId1,
					   sEntryId2.size(), sEntryId2,
					   0, &ulResult);
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
	ArchiverSession *lpSession = new ArchiverSession(lpLogger);
	HRESULT hr = lpSession->Init(lpszServerPath, m_strSslPath.c_str(), m_strSslPass.c_str());
	if (FAILED(hr)) {
		delete lpSession;
		return hr;
	}
	
	lpptrSession->reset(lpSession);
	return hr;
}

HRESULT ArchiverSession::OpenMAPIProp(ULONG cbEntryID, LPENTRYID lpEntryID, LPMAPIPROP *lppProp)
{
	HRESULT hr;
	ULONG ulType = 0;
	MAPIPropPtr ptrMapiProp;

	hr = m_ptrSession->OpenEntry(cbEntryID, lpEntryID, &ptrMapiProp.iid, MAPI_BEST_ACCESS|fMapiDeferredErrors, &ulType, &ptrMapiProp);
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
	HRESULT hr;
	ECServiceAdminPtr ptrServiceAdmin;
	ULONG cbStoreId;
	EntryIdPtr ptrStoreId;
	MsgStorePtr ptrArchiveStore;

	hr = m_ptrAdminStore.QueryInterface(ptrServiceAdmin);
	if (hr != hrSuccess)
		return hr;
	hr = ptrServiceAdmin->GetArchiveStoreEntryID(strUserName.c_str(), strServerName.c_str(), fMapiUnicode, &cbStoreId, &ptrStoreId);
	if (hr == hrSuccess)
		hr = m_ptrSession->OpenMsgStore(0, cbStoreId, ptrStoreId, &ptrArchiveStore.iid, MDB_WRITE, &ptrArchiveStore);
	else if (hr == MAPI_E_NOT_FOUND)
		hr = CreateArchiveStore(strUserName, strServerName, &ptrArchiveStore);
	if (hr != hrSuccess)
		return hr;

	return ptrArchiveStore->QueryInterface(IID_IMsgStore,
		reinterpret_cast<LPVOID *>(lppArchiveStore));
}

HRESULT ArchiverSession::GetArchiveStoreEntryId(const tstring& strUserName, const tstring& strServerName, entryid_t *lpArchiveId)
{
	HRESULT hr;
	ECServiceAdminPtr ptrServiceAdmin;
	ULONG cbStoreId;
	EntryIdPtr ptrStoreId;

	hr = m_ptrAdminStore.QueryInterface(ptrServiceAdmin);
	if (hr != hrSuccess)
		return hr;

	hr = ptrServiceAdmin->GetArchiveStoreEntryID(strUserName.c_str(), strServerName.c_str(), fMapiUnicode, &cbStoreId, &ptrStoreId);
	if (hr != hrSuccess)
		return hr;

	lpArchiveId->assign(cbStoreId, ptrStoreId);
	return hrSuccess;
}

HRESULT ArchiverSession::CreateArchiveStore(const tstring& strUserName, const tstring& strServerName, LPMDB *lppArchiveStore)
{
	HRESULT hr;
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

	hr = GetUserInfo(strUserName, &userId, NULL, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetRemoteAdminStore(m_ptrSession, m_ptrAdminStore, strServerName.c_str(), fMapiUnicode, &ptrRemoteAdminStore);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRemoteAdminStore.QueryInterface(ptrRemoteServiceAdmin);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRemoteServiceAdmin->CreateEmptyStore(ECSTORE_TYPE_ARCHIVE, userId.size(), userId, EC_OVERRIDE_HOMESERVER, &cbStoreId, &ptrStoreId, &cbRootId, &ptrRootId);
	if (hr != hrSuccess)
		return hr;

	// The entryids returned from CreateEmptyStore are unwrapped and unusable from external client code. So
	// we'll resolve the correct entryids through GetArchiveStoreEntryID.
	hr = ptrRemoteServiceAdmin->GetArchiveStoreEntryID(strUserName.c_str(), strServerName.c_str(), fMapiUnicode, &cbStoreId, &ptrStoreId);
	if (hr != hrSuccess)
		return hr;
	hr = m_ptrSession->OpenMsgStore(0, cbStoreId, ptrStoreId, &ptrArchiveStore.iid, MDB_WRITE, &ptrArchiveStore);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveStore->OpenEntry(0, NULL, &ptrRoot.iid, MAPI_MODIFY, &ulType, &ptrRoot);
	if (hr != hrSuccess)
		return hr;

	hr = ptrRoot->CreateFolder(FOLDER_GENERIC,
	     const_cast<TCHAR *>(_T("IPM_SUBTREE")),
	     const_cast<TCHAR *>(_T("")), &IID_IMAPIFolder, fMapiUnicode,
	     &ptrIpmSubtree);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrIpmSubtree, PR_ENTRYID, &ptrIpmSubtreeId);
	if (hr != hrSuccess)
		return hr;

	ptrIpmSubtreeId->ulPropTag = PR_IPM_SUBTREE_ENTRYID;
	
	hr = ptrArchiveStore->SetProps(1, ptrIpmSubtreeId, NULL);
	if (hr != hrSuccess)
		return hr;

	return ptrArchiveStore->QueryInterface(IID_IMsgStore,
		reinterpret_cast<LPVOID *>(lppArchiveStore));
}
