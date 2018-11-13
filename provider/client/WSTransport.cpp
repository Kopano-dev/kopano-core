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
#include <mapidefs.h>
#include <mapicode.h>
#include <mapitags.h>
#include <mapiutil.h>

#include <fstream>

#include <kopano/ECIConv.h>
#include <kopano/ECLogger.h>
#include "WSTransport.h"
#include "ProviderUtil.h"
#include "SymmetricCrypt.h"
#include "soapH.h"
#include "pcutil.hpp"

// The header files we use for communication with the server
#include <kopano/kcodes.h>
#include "soapKCmdProxy.h"
#include "KCmd.nsmap"
#include "Mem.h"
#include <kopano/ECGuid.h>

#include "SOAPUtils.h"
#include "WSUtil.h"
#include <kopano/mapiext.h>

#include "WSABTableView.h"
#include "WSABPropStorage.h"
#include <kopano/ecversion.h>
#include "ClientUtil.h"
#include "ECSessionGroupManager.h"
#include <kopano/stringutil.h>
#include "versions.h"

#include <kopano/charset/convert.h>
#include <kopano/charset/utf8string.h>
#include <kopano/charset/convstring.h>

#include "SOAPSock.h"
#include <kopano/mapi_ptr.h>
#include "WSMessageStreamExporter.h"
#include "WSMessageStreamImporter.h"

using namespace std;

/*
 *
 * This is the main WebServices transport object. All communications with the
 * web service server is done through this object. Also, this file is the 
 * coupling point between MAPI and our internal (network) formats, and
 * codes. This means that any classes communicating with this one either
 * use MAPI syntax (ie. MAPI_E_NOT_ENOUGH_MEMORY) OR use the EC syntax
 * (ie. EC_E_NOT_FOUND), but never BOTH.
 *
 */

#define START_SOAP_CALL retry: \
    if(m_lpCmd == NULL) { \
        hr = MAPI_E_NETWORK_ERROR; \
        goto exit; \
    }
#define END_SOAP_CALL 	\
	if(er == KCERR_END_OF_SESSION) { if(HrReLogon() == hrSuccess) goto retry; } \
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND); \
	if(hr != hrSuccess) \
		goto exit;

WSTransport::WSTransport(ULONG ulUIFlags)  
: ECUnknown("WSTransport")
, m_ResolveResultCache("ResolveResult", 4096, 300), m_has_session(false)
{
    pthread_mutexattr_t attr;
    
	m_lpCmd = NULL;
	m_ecSessionGroupId = 0;
	m_ecSessionId = 0;
	m_ulReloadId = 1;
	m_ulServerCapabilities = 0;
	m_llFlags = 0;
	m_ulUIFlags = ulUIFlags;
	memset(&m_sServerGuid, 0, sizeof(m_sServerGuid));

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
	pthread_mutex_init(&m_hDataLock, &attr);
	pthread_mutex_init(&m_mutexSessionReload, &attr);
	pthread_mutex_init(&m_ResolveResultCacheMutex, &attr);
}

WSTransport::~WSTransport()
{
	if(m_lpCmd) {
		this->HrLogOff();
	}

	pthread_mutex_destroy(&m_hDataLock);
	pthread_mutex_destroy(&m_mutexSessionReload);
	pthread_mutex_destroy(&m_ResolveResultCacheMutex);
}

HRESULT WSTransport::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECTransport, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSTransport::Create(ULONG ulUIFlags, WSTransport **lppTransport)
{
	HRESULT hr = hrSuccess;
	WSTransport *lpTransport = NULL;

	lpTransport = new WSTransport(ulUIFlags);

	hr = lpTransport->QueryInterface(IID_ECTransport, (void **) lppTransport);

	if(hr != hrSuccess)
		delete lpTransport;

	return hr;
}

/* Creates a transport working on the same session and session group as this transport */
HRESULT WSTransport::HrClone(WSTransport **lppTransport)
{
	HRESULT hr;
	WSTransport *lpTransport = NULL;

	hr = WSTransport::Create(m_ulUIFlags, &lpTransport);
	if(hr != hrSuccess)
		return hr;

	hr = CreateSoapTransport(m_ulUIFlags, m_sProfileProps, &lpTransport->m_lpCmd);
	if(hr != hrSuccess)
		return hr;
	
	lpTransport->m_ecSessionId = this->m_ecSessionId;
	lpTransport->m_ecSessionGroupId = this->m_ecSessionGroupId;

	*lppTransport = lpTransport;
	return hrSuccess;
}

HRESULT WSTransport::HrOpenTransport(LPMAPISUP lpMAPISup, WSTransport **lppTransport, BOOL bOffline)
{
	HRESULT			hr = hrSuccess;
	WSTransport		*lpTransport = NULL;
	std::string		strServerPath;
	sGlobalProfileProps	sProfileProps;

		// Get the username and password from the profile settings
	hr = ClientUtil::GetGlobalProfileProperties(lpMAPISup, &sProfileProps);
	if(hr != hrSuccess)
		goto exit;

	// TODO: check usernameand serverpath
	
	// Create a transport for this user
	hr = WSTransport::Create(MDB_NO_DIALOG, &lpTransport);
	if(hr != hrSuccess)
		goto exit;

	// Log on the transport to the server
	hr = lpTransport->HrLogon(sProfileProps);
	if(hr != hrSuccess) 
		goto exit;

	*lppTransport = lpTransport;

exit:

	if (hr != hrSuccess && lpTransport)
		lpTransport->Release();

	return hr;

}

HRESULT WSTransport::LockSoap()
{
	pthread_mutex_lock(&m_hDataLock);
	return erSuccess;
}

HRESULT WSTransport::UnLockSoap()
{
	//Clean up data create with soap_malloc
	if (m_lpCmd && m_lpCmd->soap) {
		soap_destroy(m_lpCmd->soap);
		soap_end(m_lpCmd->soap);
	}

	pthread_mutex_unlock(&m_hDataLock);
	return erSuccess;
}

HRESULT WSTransport::HrLogon2(const struct sGlobalProfileProps &sProfileProps)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	unsigned int	ulCapabilities = 0;
	unsigned int	ulLogonFlags = 0;
	unsigned int	ulServerCapabilities = 0;
	ECSESSIONID	ecSessionId = 0;
	KCmd*	lpCmd = NULL;
	bool		bPipeConnection = false;
	unsigned int	ulServerVersion = 0;
	struct logonResponse sResponse;
	struct xsd__base64Binary sLicenseRequest = {0,0};
	
	convert_context	converter;
	utf8string	strUserName = converter.convert_to<utf8string>(sProfileProps.strUserName);
	utf8string	strPassword = converter.convert_to<utf8string>(sProfileProps.strPassword);
	utf8string	strImpersonateUser = converter.convert_to<utf8string>(sProfileProps.strImpersonateUser);
	
	LockSoap();

	if (strncmp("file:", sProfileProps.strServerPath.c_str(), 5) == 0)
		bPipeConnection = true;
	else
		bPipeConnection = false;

	if(m_lpCmd == NULL) {
		if (CreateSoapTransport(m_ulUIFlags, sProfileProps, &lpCmd) != hrSuccess)
		{
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
	}
	else {
		lpCmd = m_lpCmd;
	}

	ASSERT(!sProfileProps.strProfileName.empty());

	// Attach session to sessiongroup
	m_ecSessionGroupId = g_ecSessionManager.GetSessionGroupId(sProfileProps);

	ulCapabilities |= KOPANO_CAP_MAILBOX_OWNER | KOPANO_CAP_MULTI_SERVER | KOPANO_CAP_ENHANCED_ICS | KOPANO_CAP_UNICODE | KOPANO_CAP_MSGLOCK | KOPANO_CAP_MAX_ABCHANGEID | KOPANO_CAP_EXTENDED_ANON;

	if (sizeof(ECSESSIONID) == 8)
		ulCapabilities |= KOPANO_CAP_LARGE_SESSIONID;

	if (bPipeConnection == false) {
		/*
		 * All connections except pipes request compression. The server
		 * can still reject the request.
		 */
		if(! (sProfileProps.ulProfileFlags & EC_PROFILE_FLAGS_NO_COMPRESSION))
			ulCapabilities |= KOPANO_CAP_COMPRESSION; // only to remote server .. windows?

		// try single signon logon
		er = TrySSOLogon(lpCmd, GetServerNameFromPath(sProfileProps.strServerPath.c_str()).c_str(), strUserName, strImpersonateUser, ulCapabilities, m_ecSessionGroupId, (char *)GetAppName().c_str(), &ecSessionId, &ulServerCapabilities, &m_llFlags, &m_sServerGuid, sProfileProps.strClientAppVersion, sProfileProps.strClientAppMisc);
		if (er == erSuccess)
			goto auth;
	} else {
		if (sProfileProps.ulProfileFlags & EC_PROFILE_FLAGS_NO_UID_AUTH)
			ulLogonFlags |= KOPANO_LOGON_NO_UID_AUTH;
	}
	
	// Login with username and password
	if (SOAP_OK != lpCmd->ns__logon(const_cast<char *>(strUserName.c_str()),
	    const_cast<char *>(strPassword.c_str()),
	    const_cast<char *>(strImpersonateUser.c_str()),
	    const_cast<char *>(PROJECT_VERSION_CLIENT_STR), ulCapabilities,
	    ulLogonFlags, sLicenseRequest, m_ecSessionGroupId,
	    const_cast<char *>(GetAppName().c_str()),
	    const_cast<char *>(sProfileProps.strClientAppVersion.c_str()),
	    const_cast<char *>(sProfileProps.strClientAppMisc.c_str()),
	    &sResponse)) {
#if GSOAP_VERSION >= 20871
		auto d = soap_fault_detail(lpCmd->soap);
#else
		const char *d = soap_check_faultdetail(lpCmd->soap);
#endif
		ec_log_err("gsoap connect: %s", d == nullptr ? "()" : d);
		er = KCERR_SERVER_NOT_RESPONDING;
	} else {
		er = sResponse.er;
	}

	// If the user was denied, and the server did not support encryption, and the password was encrypted, decrypt it now
	// so that we support older servers. If the password was not encrypted, then it was just wrong, and if the server supported encryption
	// then the password was also simply wrong.
	if (er == KCERR_LOGON_FAILED &&
	    SymmetricIsCrypted(sProfileProps.strPassword.c_str()) &&
	    !(sResponse.ulCapabilities & KOPANO_CAP_CRYPT)) {
		// Login with username and password
		if (SOAP_OK != lpCmd->ns__logon(const_cast<char *>(strUserName.c_str()),
		    const_cast<char *>(SymmetricDecrypt(sProfileProps.strPassword.c_str()).c_str()),
		    const_cast<char *>(strImpersonateUser.c_str()),
		    const_cast<char *>(PROJECT_VERSION_CLIENT_STR),
		    ulCapabilities, ulLogonFlags, sLicenseRequest,
		    m_ecSessionGroupId,
		    const_cast<char *>(GetAppName().c_str()),
		    const_cast<char *>(sProfileProps.strClientAppVersion.c_str()),
		    const_cast<char *>(sProfileProps.strClientAppMisc.c_str()),
		    &sResponse))
			er = KCERR_SERVER_NOT_RESPONDING;
		else
			er = sResponse.er;
	}

	hr = kcerr_to_mapierr(er, MAPI_E_LOGON_FAILED);
	if (hr != hrSuccess)
		goto exit;

	/*
	 * Version is retrieved but not analyzed because we want to be able to
	 * connect to old servers for development.
	 */
	er = ParseKopanoVersion(sResponse.lpszVersion, &ulServerVersion);
	if (er != erSuccess) {
		hr = MAPI_E_VERSION;
		goto exit;
	}

	ecSessionId = sResponse.ulSessionId;
	ulServerCapabilities = sResponse.ulCapabilities;

	if (sResponse.sServerGuid.__ptr != NULL && sResponse.sServerGuid.__size == sizeof(m_sServerGuid))
		memcpy(&m_sServerGuid, sResponse.sServerGuid.__ptr, sizeof(m_sServerGuid));

	// From here the login is ok

auth: // User have a logon
	// See if the server supports impersonation. If it doesn't but imporsonation was attempted,
	// we should fail now because the client won't expect his own store to be returned.
	if (!strImpersonateUser.empty() && (sResponse.ulCapabilities & KOPANO_CAP_IMPERSONATION) == 0) {
		hr = MAPI_E_NO_SUPPORT;	// or just MAPI_E_LOGON_FAILED?
		goto exit;
	}

	if (ulServerCapabilities & KOPANO_CAP_COMPRESSION) {
		/*
		 * GSOAP autodetects incoming compression, so even if not
		 * explicitly enabled, it will be accepted.
		 */
		soap_set_imode(lpCmd->soap, SOAP_ENC_ZLIB);
		soap_set_omode(lpCmd->soap, SOAP_ENC_ZLIB | SOAP_IO_CHUNK);
	}

	m_sProfileProps = sProfileProps;
	m_ulServerCapabilities = ulServerCapabilities;
	m_ecSessionId = ecSessionId;
	m_has_session = true;
	m_lpCmd = lpCmd;

exit:

	UnLockSoap();

	if(hr != hrSuccess) {
	    // UGLY FIX: due to the ugly code above that does lpCmd = m_lpCmd
	    // we need to check that we're not deleting our m_lpCmd. We also cannot
	    // set m_lpCmd to NULL since various functions in WSTransport rely on the
	    // fact that m_lpCmd is good after a successful HrLogon() call.
		if(lpCmd && lpCmd != m_lpCmd)
			DestroySoapTransport(lpCmd);
	}

	return hr;
}

HRESULT WSTransport::HrLogon(const struct sGlobalProfileProps &in_props)
{
	if (m_has_session)
		logoff_nd();
	if (in_props.strServerPath.compare("default:") != 0)
		return HrLogon2(in_props);
	struct sGlobalProfileProps p = in_props;
	p.strServerPath = "file:///var/run/kopano/server.sock";
	return HrLogon2(p);
}

HRESULT WSTransport::HrSetRecvTimeout(unsigned int ulSeconds)
{
	if (this->m_lpCmd == NULL)
		return MAPI_E_NOT_INITIALIZED;

	this->m_lpCmd->soap->recv_timeout = ulSeconds;
	return hrSuccess;
}

HRESULT WSTransport::CreateAndLogonAlternate(LPCSTR szServer, WSTransport **lppTransport) const
{
	HRESULT				hr = hrSuccess;
	WSTransport			*lpTransport = NULL;
	sGlobalProfileProps	sProfileProps = m_sProfileProps;

	if (!lppTransport)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = WSTransport::Create(m_ulUIFlags, &lpTransport);
	if (hr != hrSuccess)
		goto exit;

	sProfileProps.strServerPath = szServer;

	hr = lpTransport->HrLogon(sProfileProps);
	if (hr != hrSuccess)
		goto exit;

	*lppTransport = lpTransport;
	lpTransport = NULL;

exit:
	if (lpTransport)
		lpTransport->Release();

	return hr;
}

/**
 * Create a new transport based on the current configuration and
 * logon to the server.
 *
 * @param[out]	lppTransport	The new transport
 * @return		HRESULT
 */
HRESULT WSTransport::CloneAndRelogon(WSTransport **lppTransport) const
{
	HRESULT				hr = hrSuccess;
	WSTransport			*lpTransport = NULL;

	if (!lppTransport)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = WSTransport::Create(m_ulUIFlags, &lpTransport);
	if (hr != hrSuccess)
		goto exit;

	hr = lpTransport->HrLogon(m_sProfileProps);
	if (hr != hrSuccess)
		goto exit;

	*lppTransport = lpTransport;
	lpTransport = NULL;

exit:
	if (lpTransport)
		lpTransport->Release();

	return hr;
}

HRESULT WSTransport::HrReLogon()
{
	HRESULT hr;
	SESSIONRELOADLIST::const_iterator iter;

	hr = HrLogon(m_sProfileProps);
	if(hr != hrSuccess)
		return hr;

	// Notify new session to listeners
	pthread_mutex_lock(&m_mutexSessionReload);
	for (iter = m_mapSessionReload.begin();
	     iter != m_mapSessionReload.end(); ++iter)
		iter->second.second(iter->second.first, this->m_ecSessionId);
	pthread_mutex_unlock(&m_mutexSessionReload);
	return hrSuccess;
}

ECRESULT WSTransport::TrySSOLogon(KCmd* lpCmd, LPCSTR szServer, utf8string strUsername, utf8string strImpersonateUser, unsigned int ulCapabilities, ECSESSIONGROUPID ecSessionGroupId, char *szAppName, ECSESSIONID* lpSessionId, unsigned int* lpulServerCapabilities, unsigned long long *lpllFlags, LPGUID lpsServerGuid, const std::string appVersion, const std::string appMisc)
{
	ECRESULT		er = KCERR_LOGON_FAILED;
	return er;
}

HRESULT WSTransport::HrGetPublicStore(ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID, string *lpstrRedirServer)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct getStoreResponse sResponse;

	LockSoap();

	if ((ulFlags & ~EC_OVERRIDE_HOMESERVER) != 0) {
		hr = MAPI_E_UNKNOWN_FLAGS;
		goto exit;
	}

	if(lppStoreID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getPublicStore(m_ecSessionId, ulFlags, &sResponse))
			er = KCERR_SERVER_NOT_RESPONDING;
		else
			er = sResponse.er;
	}
	//END_SOAP_CALL
	if(er == KCERR_END_OF_SESSION) { if(HrReLogon() == hrSuccess) goto retry; }
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND);
	if (hr == MAPI_E_UNABLE_TO_COMPLETE)
	{
		if (lpstrRedirServer)
			*lpstrRedirServer = sResponse.lpszServerPath;
		else
			hr = MAPI_E_NOT_FOUND;
	}
	if(hr != hrSuccess)
		goto exit;

	// Create a client store entry, add the servername
	hr = WrapServerClientStoreEntry(sResponse.lpszServerPath ? sResponse.lpszServerPath : m_sProfileProps.strServerPath.c_str(), &sResponse.sStoreId, lpcbStoreID, lppStoreID);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetStore(ULONG cbMasterID, LPENTRYID lpMasterID, ULONG* lpcbStoreID, LPENTRYID* lppStoreID, ULONG* lpcbRootID, LPENTRYID* lppRootID, string *lpstrRedirServer)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	
	entryId		sEntryId = {0,0}; // Do not free
	struct getStoreResponse sResponse;
	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	if(lpMasterID) {

		hr = UnWrapServerClientStoreEntry(cbMasterID, lpMasterID, &cbUnWrapStoreID, &lpUnWrapStoreID);
		if(hr != hrSuccess)
			goto exit;

		sEntryId.__ptr = (unsigned char*)lpUnWrapStoreID;
		sEntryId.__size = cbUnWrapStoreID;
	}

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getStore(m_ecSessionId, (lpMasterID)?&sEntryId:NULL, &sResponse))
			er = KCERR_SERVER_NOT_RESPONDING;
		else
			er = sResponse.er;
	}
	//END_SOAP_CALL
	if(er == KCERR_END_OF_SESSION) { if(HrReLogon() == hrSuccess) goto retry; }
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND);
	if (hr == MAPI_E_UNABLE_TO_COMPLETE)
	{
		if (lpstrRedirServer)
			*lpstrRedirServer = sResponse.lpszServerPath;
		else
			hr = MAPI_E_NOT_FOUND;
	}
	if(hr != hrSuccess)
		goto exit;

	if(lppRootID && lpcbRootID) {
		hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sRootId, lpcbRootID, lppRootID);
		if(hr != hrSuccess)
			goto exit;
	}

	if(lppStoreID && lpcbStoreID) {
		// Create a client store entry, add the servername
		hr = WrapServerClientStoreEntry(sResponse.lpszServerPath ? sResponse.lpszServerPath : m_sProfileProps.strServerPath.c_str(), &sResponse.sStoreId, lpcbStoreID, lppStoreID);
		if(hr != hrSuccess)
			goto exit;
	}

exit:
	UnLockSoap();

	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrGetStoreName(ULONG cbStoreID, LPENTRYID lpStoreID, ULONG ulFlags, LPTSTR *lppszStoreName)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId		sEntryId; // Do not free
	struct getStoreNameResponse sResponse;
	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	if(lpStoreID == NULL || lppszStoreName == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Remove the servername
	hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	sEntryId.__ptr = (unsigned char*)lpUnWrapStoreID;
	sEntryId.__size = cbUnWrapStoreID;
	
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getStoreName(m_ecSessionId, sEntryId, &sResponse))
			er = KCERR_SERVER_NOT_RESPONDING;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = Utf8ToTString(sResponse.lpszStoreName, ulFlags, NULL, NULL, lppszStoreName);

exit:
	UnLockSoap();

	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrGetStoreType(ULONG cbStoreID, LPENTRYID lpStoreID, ULONG *lpulStoreType)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId		sEntryId; // Do not free
	struct getStoreTypeResponse sResponse;
	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	if(lpStoreID == NULL || lpulStoreType == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Remove the servername
	hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	sEntryId.__ptr = (unsigned char*)lpUnWrapStoreID;
	sEntryId.__size = cbUnWrapStoreID;
	
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getStoreType(m_ecSessionId, sEntryId, &sResponse))
			er = KCERR_SERVER_NOT_RESPONDING;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpulStoreType = sResponse.ulStoreType;

exit:
	UnLockSoap();

	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrLogOff()
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;

	LockSoap();

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__logoff(m_ecSessionId, &er) )
			er = KCERR_NETWORK_ERROR;
		else
			m_has_session = false;

		DestroySoapTransport(m_lpCmd);
		m_lpCmd = NULL;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hrSuccess; // NOTE hrSuccess, never fails since we don't really mind that it failed.
}

HRESULT WSTransport::logoff_nd(void)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;

	LockSoap();
	START_SOAP_CALL
	{
		if (m_lpCmd->ns__logoff(m_ecSessionId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			m_has_session = false;
	}
	END_SOAP_CALL
 exit:
	UnLockSoap();
	return er;
}

HRESULT WSTransport::HrCheckExistObject(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId		sEntryId = {0}; // Do not free

	LockSoap();

	if(cbEntryID == 0 || lpEntryID == NULL) {
		hr = MAPI_E_INVALID_ENTRYID;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__checkExistObject(m_ecSessionId, sEntryId, ulFlags, &er))
			er = KCERR_SERVER_NOT_RESPONDING;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrOpenPropStorage(ULONG cbParentEntryID, LPENTRYID lpParentEntryID, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, IECPropStorage **lppPropStorage)
{
	HRESULT hr = hrSuccess;
	WSMAPIPropStorage *lpPropStorage = NULL;
	LPENTRYID	lpUnWrapParentID = NULL;
	ULONG		cbUnWrapParentID = 0;
	LPENTRYID	lpUnWrapEntryID = NULL;
	ULONG		cbUnWrapEntryID = 0;

	if (lpParentEntryID) {
		hr = UnWrapServerClientStoreEntry(cbParentEntryID, lpParentEntryID, &cbUnWrapParentID, &lpUnWrapParentID);
		if(hr != hrSuccess)
			goto exit;
	}

	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapEntryID, &lpUnWrapEntryID);
	if(hr != hrSuccess)
		goto exit;

	hr = WSMAPIPropStorage::Create(cbUnWrapParentID, lpUnWrapParentID, cbUnWrapEntryID, lpUnWrapEntryID, ulFlags, m_lpCmd, &m_hDataLock, m_ecSessionId, this->m_ulServerCapabilities, this, &lpPropStorage);
	if(hr != hrSuccess)
		goto exit;

	hr = lpPropStorage->QueryInterface(IID_IECPropStorage, (void **)lppPropStorage);

exit:
	if(lpPropStorage)
		lpPropStorage->Release();

	if(lpUnWrapEntryID)
		ECFreeBuffer(lpUnWrapEntryID);

	if(lpUnWrapParentID)
		ECFreeBuffer(lpUnWrapParentID);

	return hr;
}

HRESULT WSTransport::HrOpenParentStorage(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage, IECPropStorage **lppPropStorage)
{
	HRESULT hr = hrSuccess;
	ECParentStorage *lpPropStorage = NULL;

	hr = ECParentStorage::Create(lpParentObject, ulUniqueId, ulObjId, lpServerStorage, &lpPropStorage);
	if(hr != hrSuccess)
		goto exit;

	hr = lpPropStorage->QueryInterface(IID_IECPropStorage, (void **)lppPropStorage);

exit:
	if(lpPropStorage)
		lpPropStorage->Release();

	return hr;
}

HRESULT WSTransport::HrOpenABPropStorage(ULONG cbEntryID, LPENTRYID lpEntryID, IECPropStorage **lppPropStorage)
{
	HRESULT			hr = hrSuccess;
	WSABPropStorage *lpPropStorage = NULL;

	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	hr = UnWrapServerClientABEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	hr = WSABPropStorage::Create(cbUnWrapStoreID, lpUnWrapStoreID, m_lpCmd, &m_hDataLock, m_ecSessionId, this, &lpPropStorage);

	if(hr != hrSuccess)
		goto exit;

	hr = lpPropStorage->QueryInterface(IID_IECPropStorage, (void **)lppPropStorage);

exit:
	if(lpPropStorage)
		lpPropStorage->Release();

	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrOpenFolderOps(ULONG cbEntryID, LPENTRYID lpEntryID, WSMAPIFolderOps **lppFolderOps)
{
	HRESULT hr = hrSuccess;

	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

//FIXME: create this function
//	hr = CheckEntryIDType(cbEntryID, lpEntryID, MAPI_FOLDER);
//	if( hr != hrSuccess)
		//goto exit;

	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	hr = WSMAPIFolderOps::Create(m_lpCmd, &m_hDataLock, m_ecSessionId, cbUnWrapStoreID, lpUnWrapStoreID, this, lppFolderOps);

exit:
	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;

}

HRESULT WSTransport::HrOpenTableOps(ULONG ulType, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID, ECMsgStore *lpMsgStore, WSTableView **lppTableOps)
{
	/*
	FIXME: Do a check ?
	if (peid->ulType != MAPI_FOLDER && peid->ulType != MAPI_MESSAGE)
		return MAPI_E_INVALID_ENTRYID;
	*/
	return WSStoreTableView::Create(ulType, ulFlags, m_lpCmd, &m_hDataLock, m_ecSessionId, cbEntryID, lpEntryID, lpMsgStore, this, lppTableOps);
}

HRESULT WSTransport::HrOpenABTableOps(ULONG ulType, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID, ECABLogon* lpABLogon, WSTableView **lppTableOps)
{
	/*if (peid->ulType != MAPI_FOLDER && peid->ulType != MAPI_MESSAGE)
		return MAPI_E_INVALID_ENTRYID;
	*/
	return WSABTableView::Create(ulType, ulFlags, m_lpCmd, &m_hDataLock, m_ecSessionId, cbEntryID, lpEntryID, lpABLogon, this, lppTableOps);
}

HRESULT WSTransport::HrOpenMailBoxTableOps(ULONG ulFlags, ECMsgStore *lpMsgStore, WSTableView **lppTableView)
{
	HRESULT hr = hrSuccess;
	WSTableMailBox *lpWSTable = NULL;
	

	hr = WSTableMailBox::Create(ulFlags, m_lpCmd, &m_hDataLock, m_ecSessionId, lpMsgStore, this, &lpWSTable);
	if(hr != hrSuccess)
		goto exit;

	hr = lpWSTable->QueryInterface(IID_ECTableView, (void **)lppTableView);

exit:
	if (lpWSTable)
		lpWSTable->Release();

	return hr;
}

HRESULT WSTransport::HrOpenTableOutGoingQueueOps(ULONG cbStoreEntryID, LPENTRYID lpStoreEntryID, ECMsgStore *lpMsgStore, WSTableOutGoingQueue **lppTableOutGoingQueueOps)
{
	HRESULT hr = hrSuccess;
	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	// lpStoreEntryID == null for master queue
	if(lpStoreEntryID) {
		hr = UnWrapServerClientStoreEntry(cbStoreEntryID, lpStoreEntryID, &cbUnWrapStoreID, &lpUnWrapStoreID);
		if(hr != hrSuccess)
			goto exit;
	}

	hr = WSTableOutGoingQueue::Create(m_lpCmd, &m_hDataLock, m_ecSessionId, cbUnWrapStoreID, lpUnWrapStoreID, lpMsgStore, this, lppTableOutGoingQueueOps);

exit:
	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrDeleteObjects(ULONG ulFlags, LPENTRYLIST lpMsgList, ULONG ulSyncId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct entryList sEntryList;

	LockSoap();
	memset(&sEntryList, 0, sizeof(struct entryList));

	if(lpMsgList->cValues == 0)
		goto exit;

	hr = CopyMAPIEntryListToSOAPEntryList(lpMsgList, &sEntryList);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__deleteObjects(m_ecSessionId, ulFlags, &sEntryList, ulSyncId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	FreeEntryList(&sEntryList, false);

	return hr;
}

HRESULT WSTransport::HrNotify(LPNOTIFICATION lpNotification)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct notification	sNotification; 
	int ulSize = 0;

	memset(&sNotification, 0, sizeof(struct notification));

	LockSoap();

	//FIMXE: also notify other types ?
	if(lpNotification == NULL || lpNotification->ulEventType != fnevNewMail)
	{
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	sNotification.ulConnection = 0;// The connection id should be calculate on the server side

	sNotification.ulEventType = lpNotification->ulEventType;
	sNotification.newmail = new notificationNewMail;
	memset(sNotification.newmail, 0, sizeof(notificationNewMail));

	hr = CopyMAPIEntryIdToSOAPEntryId(lpNotification->info.newmail.cbEntryID, (LPENTRYID)lpNotification->info.newmail.lpEntryID, &sNotification.newmail->pEntryId);
	if(hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(lpNotification->info.newmail.cbParentID, (LPENTRYID)lpNotification->info.newmail.lpParentID, &sNotification.newmail->pParentId);
	if(hr != hrSuccess)
		goto exit;
	
	if(lpNotification->info.newmail.lpszMessageClass){
		utf8string strMessageClass = convstring(lpNotification->info.newmail.lpszMessageClass, lpNotification->info.newmail.ulFlags);
		ulSize = strMessageClass.size() + 1;
		sNotification.newmail->lpszMessageClass = new char[ulSize];
		memcpy(sNotification.newmail->lpszMessageClass, strMessageClass.c_str(), ulSize);
	}
	sNotification.newmail->ulMessageFlags = lpNotification->info.newmail.ulMessageFlags;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__notify(m_ecSessionId, sNotification, &er)) {
			er = KCERR_NETWORK_ERROR;
		}
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	FreeNotificationStruct(&sNotification, false);

	return hr;
}

HRESULT WSTransport::HrSubscribe(ULONG cbKey, LPBYTE lpKey, ULONG ulConnection, ULONG ulEventMask)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	notifySubscribe notSubscribe{__gszeroinit};

	LockSoap();

	notSubscribe.ulConnection = ulConnection;
	notSubscribe.sKey.__size = cbKey;
	notSubscribe.sKey.__ptr = lpKey;
	notSubscribe.ulEventMask = ulEventMask;			

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__notifySubscribe(m_ecSessionId, &notSubscribe, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrSubscribe(ULONG ulSyncId, ULONG ulChangeId, ULONG ulConnection, ULONG ulEventMask)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	notifySubscribe notSubscribe{__gszeroinit};

	LockSoap();

	notSubscribe.ulConnection = ulConnection;
	notSubscribe.sSyncState.ulSyncId = ulSyncId;
	notSubscribe.sSyncState.ulChangeId = ulChangeId;
	notSubscribe.ulEventMask = ulEventMask;			

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__notifySubscribe(m_ecSessionId, &notSubscribe, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrSubscribeMulti(const ECLISTSYNCADVISE &lstSyncAdvises, ULONG ulEventMask)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	notifySubscribeArray notSubscribeArray{__gszeroinit};
	ECLISTSYNCADVISE::const_iterator iSyncAdvise;
	unsigned	i = 0;
	
	LockSoap();

	notSubscribeArray.__size = lstSyncAdvises.size();
	hr = MAPIAllocateBuffer(notSubscribeArray.__size * sizeof *notSubscribeArray.__ptr, (void**)&notSubscribeArray.__ptr);
	if (hr != hrSuccess)
		goto exit;
	memset(notSubscribeArray.__ptr, 0, notSubscribeArray.__size * sizeof *notSubscribeArray.__ptr);
	
	for (iSyncAdvise = lstSyncAdvises.begin(); iSyncAdvise != lstSyncAdvises.end(); ++i, ++iSyncAdvise) {
		notSubscribeArray.__ptr[i].ulConnection = iSyncAdvise->ulConnection;
		notSubscribeArray.__ptr[i].sSyncState.ulSyncId = iSyncAdvise->sSyncState.ulSyncId;
		notSubscribeArray.__ptr[i].sSyncState.ulChangeId = iSyncAdvise->sSyncState.ulChangeId;
		notSubscribeArray.__ptr[i].ulEventMask = ulEventMask;
	}

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__notifySubscribeMulti(m_ecSessionId, &notSubscribeArray, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL

exit:
	MAPIFreeBuffer(notSubscribeArray.__ptr);
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrUnSubscribe(ULONG ulConnection)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;

	LockSoap();

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__notifyUnSubscribe(m_ecSessionId, ulConnection, &er) )
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrUnSubscribeMulti(const ECLISTCONNECTION &lstConnections)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	mv_long ulConnArray = {0};
	ECLISTCONNECTION::const_iterator iConnection;
	unsigned i = 0;

	ulConnArray.__size = lstConnections.size();
	ulConnArray.__ptr = new unsigned int[ulConnArray.__size];
	
	LockSoap();

	for (iConnection = lstConnections.begin(); iConnection != lstConnections.end(); ++i, ++iConnection)
		ulConnArray.__ptr[i] = iConnection->second;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__notifyUnSubscribeMulti(m_ecSessionId, &ulConnArray, &er) )
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();
	delete[] ulConnArray.__ptr;
	return hr;
}

/**
 * Export a set of messages as stream.
 * This method MUST be called on a WSTransport that's dedicated for exporting because no locking is performed.
 *
 * @param[in]	ulFlags		Flags used to determine which messages and what data is to be exported.
 * @param[in]	ulPropTag	Either PR_ENTRYID or PR_SOURCE_KEY. Indicates which identifier is used in lpChanges[x].sSourceKey
 * @param[in]	lpChanges	The complete set of changes available.
 * @param[in]	ulStart		The index in sChanges that specifies the first message to export.
 * @param[in]	ulChanges	The number of messages to export, starting at ulStart. ulStart and ulCount must not me larger than the amount of available changes.
 * @param[in]	lpsProps	The set of proptags that will be returned as regular properties outside the stream.
 * @param[out]	lppsStreamExporter	The streamexporter that must be used to get the individual streams.
 *
 * @retval	MAPI_E_INVALID_PARAMETER	lpChanges or lpsProps == NULL
 * @retval	MAPI_E_NETWORK_ERROR		The actual call to the server failed or no streams are returned
 */
HRESULT WSTransport::HrExportMessageChangesAsStream(ULONG ulFlags, ULONG ulPropTag, ICSCHANGE *lpChanges, ULONG ulStart, ULONG ulChanges, LPSPropTagArray lpsProps, WSMessageStreamExporter **lppsStreamExporter)
{
	typedef mapi_memory_ptr<sourceKeyPairArray> sourceKeyPairArrayPtr;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	sourceKeyPairArrayPtr ptrsSourceKeyPairs;
	WSMessageStreamExporterPtr ptrStreamExporter;
	propTagArray sPropTags = {0, 0};
	exportMessageChangesAsStreamResponse sResponse{__gszeroinit};

	if (lpChanges == NULL || lpsProps == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if ((m_ulServerCapabilities & KOPANO_CAP_ENHANCED_ICS) == 0) {
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	hr = CopyICSChangeToSOAPSourceKeys(ulChanges, lpChanges + ulStart, &ptrsSourceKeyPairs);
	if (hr != hrSuccess)
		goto exit;

	sPropTags.__size = lpsProps->cValues;
	sPropTags.__ptr = (unsigned int*)lpsProps->aulPropTag;

	// Make sure to get the mime attachments ourselves
	soap_post_check_mime_attachments(m_lpCmd->soap);

	START_SOAP_CALL
	{
		if (m_lpCmd->ns__exportMessageChangesAsStream(m_ecSessionId, ulFlags, sPropTags, *ptrsSourceKeyPairs, ulPropTag, &sResponse) != SOAP_OK)
			er = MAPI_E_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if (sResponse.sMsgStreams.__size > 0 && !soap_check_mime_attachments(m_lpCmd->soap)) {
		hr = MAPI_E_NETWORK_ERROR;
		goto exit;
	}

	hr = WSMessageStreamExporter::Create(ulStart, ulChanges, sResponse.sMsgStreams, this, &ptrStreamExporter);
	if (hr != hrSuccess) {
		goto exit;
	}

	*lppsStreamExporter = ptrStreamExporter.release();

exit:
	return hr;
}

HRESULT WSTransport::HrGetMessageStreamImporter(ULONG ulFlags, ULONG ulSyncId, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG cbFolderEntryID, LPENTRYID lpFolderEntryID, bool bNewMessage, LPSPropValue lpConflictItems, WSMessageStreamImporter **lppStreamImporter)
{
	HRESULT hr;
	WSMessageStreamImporterPtr ptrStreamImporter;

	if ((m_ulServerCapabilities & KOPANO_CAP_ENHANCED_ICS) == 0)
		return MAPI_E_NO_SUPPORT;

	hr = WSMessageStreamImporter::Create(ulFlags, ulSyncId, cbEntryID, lpEntryID, cbFolderEntryID, lpFolderEntryID, bNewMessage, lpConflictItems, this, &ptrStreamImporter);
	if (hr != hrSuccess)
		return hr;

	*lppStreamImporter = ptrStreamImporter.release();
	return hrSuccess;
}

HRESULT WSTransport::HrGetIDsFromNames(LPMAPINAMEID *lppPropNames, ULONG cNames, ULONG ulFlags, ULONG **lpServerIDs)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct namedPropArray sNamedProps;
	struct getIDsFromNamesResponse sResponse;
	unsigned int i=0;
	convert_context convertContext;

	LockSoap();

	// Convert our data into a structure that the server can take
	sNamedProps.__size = cNames;
	ECAllocateBuffer(sizeof(struct namedProp) * cNames, (void **)&sNamedProps.__ptr);
	memset(sNamedProps.__ptr, 0 , sizeof(struct namedProp) * cNames);

	for (i = 0; i < cNames; ++i) {	
		switch(lppPropNames[i]->ulKind) {
		case MNID_ID:
			ECAllocateMore(sizeof(unsigned int), sNamedProps.__ptr,(void **)&sNamedProps.__ptr[i].lpId);
			*sNamedProps.__ptr[i].lpId = lppPropNames[i]->Kind.lID;
			break;
		case MNID_STRING: {
			// The string is actually utf-8, not windows-1252. This enables full support for wide char strings.
			utf8string strNameUTF8 = convertContext.convert_to<utf8string>(lppPropNames[i]->Kind.lpwstrName);

			ECAllocateMore(strNameUTF8.length()+1, sNamedProps.__ptr,(void **)&sNamedProps.__ptr[i].lpString);
			strcpy(sNamedProps.__ptr[i].lpString, strNameUTF8.c_str());
			break;
		}
		default:
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}

		if(lppPropNames[i]->lpguid) {
			ECAllocateMore(sizeof( xsd__base64Binary) , sNamedProps.__ptr, (void **) &sNamedProps.__ptr[i].lpguid);
			sNamedProps.__ptr[i].lpguid->__ptr = (unsigned char *)lppPropNames[i]->lpguid;
			sNamedProps.__ptr[i].lpguid->__size = sizeof(GUID);
		} else {
			sNamedProps.__ptr[i].lpguid = NULL;
		}
	}

	// Send the call off the the server
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getIDsFromNames(m_ecSessionId, &sNamedProps, ulFlags, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	// Make sure we response with the same amount of data that we requested
	if((ULONG)sResponse.lpsPropTags.__size != cNames) {
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	ECAllocateBuffer(sizeof(ULONG) * sResponse.lpsPropTags.__size, (void**)lpServerIDs);

	memcpy(*lpServerIDs, sResponse.lpsPropTags.__ptr, sizeof(ULONG) * sResponse.lpsPropTags.__size);

exit:
	UnLockSoap();

	if(sNamedProps.__ptr)
		ECFreeBuffer(sNamedProps.__ptr);

	return hr;
}

HRESULT WSTransport::HrGetNamesFromIDs(LPSPropTagArray lpsPropTags, LPMAPINAMEID **lpppNames, ULONG *lpcResolved)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct getNamesFromIDsResponse sResponse;
	struct propTagArray sPropTags;
	LPMAPINAMEID *lppNames = NULL;
	convert_context convertContext;

	sPropTags.__size = lpsPropTags->cValues;
	sPropTags.__ptr = (unsigned int *)&lpsPropTags->aulPropTag[0];

	LockSoap();

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getNamesFromIDs(m_ecSessionId, &sPropTags, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	ECAllocateBuffer(sizeof(LPMAPINAMEID) * sResponse.lpsNames.__size, (void **) &lppNames);

	// Loop through all the returned names, and put it into the return value
	for (gsoap_size_t i = 0; i < sResponse.lpsNames.__size; ++i) {
		// Each MAPINAMEID must be allocated
		ECAllocateMore(sizeof(MAPINAMEID), lppNames, (void **) &lppNames[i]);

		if(sResponse.lpsNames.__ptr[i].lpguid && sResponse.lpsNames.__ptr[i].lpguid->__ptr) {
			ECAllocateMore(sizeof(GUID), lppNames, (void **) &lppNames[i]->lpguid);
			memcpy(lppNames[i]->lpguid, sResponse.lpsNames.__ptr[i].lpguid->__ptr, sizeof(GUID));
		}
		if(sResponse.lpsNames.__ptr[i].lpId) {
			lppNames[i]->Kind.lID = *sResponse.lpsNames.__ptr[i].lpId;
			lppNames[i]->ulKind = MNID_ID;
		} else if(sResponse.lpsNames.__ptr[i].lpString) {
			std::wstring strNameW = convertContext.convert_to<std::wstring>(sResponse.lpsNames.__ptr[i].lpString, rawsize(sResponse.lpsNames.__ptr[i].lpString), "UTF-8");

			ECAllocateMore((strNameW.size() + 1) * sizeof(WCHAR), lppNames, (void **)&lppNames[i]->Kind.lpwstrName);
			memcpy(lppNames[i]->Kind.lpwstrName, strNameW.c_str(), (strNameW.size() + 1) * sizeof(WCHAR));	// Also copy the trailing '\0'
			lppNames[i]->ulKind = MNID_STRING;
		} else {
			// not found by server, we have actually allocated memory but it doesn't really matter
			lppNames[i] = NULL;
		}
	}

	*lpcResolved = sResponse.lpsNames.__size;
	*lpppNames = lppNames;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetReceiveFolderTable(ULONG ulFlags, ULONG cbStoreEntryID, LPENTRYID lpStoreEntryID, LPSRowSet* lppsRowSet)
{
	struct receiveFolderTableResponse sReceiveFolders;
	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;
	LPSRowSet	lpsRowSet = NULL;
	ULONG		ulRowId = 0;
	int			nLen = 0;
	entryId		sEntryId = {0}; // Do not free

	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;
	std::wstring unicode;
	convert_context converter;

	LockSoap();

	hr = UnWrapServerClientStoreEntry(cbStoreEntryID, lpStoreEntryID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	sEntryId.__ptr = (unsigned char*)lpUnWrapStoreID;
	sEntryId.__size = cbUnWrapStoreID;

	// Get ReceiveFolder information from the server
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getReceiveFolderTable(m_ecSessionId, sEntryId, &sReceiveFolders))
			er = KCERR_NETWORK_ERROR;
		else
			er = sReceiveFolders.er;

	}
	END_SOAP_CALL

	ECAllocateBuffer(CbNewSRowSet(sReceiveFolders.sFolderArray.__size), (void**)&lpsRowSet);
	memset(lpsRowSet, 0, CbNewSRowSet(sReceiveFolders.sFolderArray.__size));
	lpsRowSet->cRows = sReceiveFolders.sFolderArray.__size;

	for (gsoap_size_t i = 0; i < sReceiveFolders.sFolderArray.__size; ++i) {
		ulRowId = i+1;

		lpsRowSet->aRow[i].cValues = NUM_RFT_PROPS;
		ECAllocateBuffer(sizeof(SPropValue) * NUM_RFT_PROPS, (void**)&lpsRowSet->aRow[i].lpProps);
		memset(lpsRowSet->aRow[i].lpProps, 0, sizeof(SPropValue)*NUM_RFT_PROPS);
		
		lpsRowSet->aRow[i].lpProps[RFT_ROWID].ulPropTag = PR_ROWID;
		lpsRowSet->aRow[i].lpProps[RFT_ROWID].Value.ul = ulRowId;

		lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].ulPropTag = PR_INSTANCE_KEY;
		lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.cb = 4; //fixme: maybe fix, normal 8 now
		ECAllocateMore(lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.cb, lpsRowSet->aRow[i].lpProps, (void**)&lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.lpb);
		memset(lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.lpb, 0, lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.cb);
		memcpy(lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.lpb, &ulRowId, sizeof(ulRowId));
		
		lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].ulPropTag = PR_ENTRYID;
		lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.cb = sReceiveFolders.sFolderArray.__ptr[i].sEntryId.__size;
		ECAllocateMore(lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.cb, lpsRowSet->aRow[i].lpProps, (void**)&lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.lpb);
		memcpy(lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.lpb, sReceiveFolders.sFolderArray.__ptr[i].sEntryId.__ptr, lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.cb);

		// Use the entryid for record key
		lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].ulPropTag = PR_RECORD_KEY;
		lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.cb = sReceiveFolders.sFolderArray.__ptr[i].sEntryId.__size;
		ECAllocateMore(lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.cb, lpsRowSet->aRow[i].lpProps, (void**)&lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.lpb);
		memcpy(lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.lpb, sReceiveFolders.sFolderArray.__ptr[i].sEntryId.__ptr, lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.cb);

		if (ulFlags & MAPI_UNICODE) {
			lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].ulPropTag = PR_MESSAGE_CLASS_W;
			unicode = converter.convert_to<std::wstring>(sReceiveFolders.sFolderArray.__ptr[i].lpszAExplicitClass);
			ECAllocateMore((unicode.length()+1)*sizeof(WCHAR), lpsRowSet->aRow[i].lpProps, (void**)&lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].Value.lpszW);
			memcpy(lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].Value.lpszW, unicode.c_str(), (unicode.length()+1)*sizeof(WCHAR));
		} else {
			lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].ulPropTag = PR_MESSAGE_CLASS_A;
			nLen = strlen(sReceiveFolders.sFolderArray.__ptr[i].lpszAExplicitClass)+1;
			ECAllocateMore(nLen, lpsRowSet->aRow[i].lpProps, (void**)&lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].Value.lpszA);
			memcpy(lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].Value.lpszA, sReceiveFolders.sFolderArray.__ptr[i].lpszAExplicitClass, nLen);
		}	
	}

	*lppsRowSet = lpsRowSet;

exit:
	UnLockSoap();
	
	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrGetReceiveFolder(ULONG cbStoreEntryID, LPENTRYID lpStoreEntryID, const utf8string &strMessageClass, ULONG* lpcbEntryID, LPENTRYID* lppEntryID, utf8string *lpstrExplicitClass)
{
	struct receiveFolderResponse sReceiveFolderTable;

	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;
	entryId		sEntryId = {0}; // Do not free
	ULONG		cbEntryID = 0;
	LPENTRYID	lpEntryID = NULL;

	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	hr = UnWrapServerClientStoreEntry(cbStoreEntryID, lpStoreEntryID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	sEntryId.__ptr = (unsigned char*)lpUnWrapStoreID;
	sEntryId.__size = cbUnWrapStoreID;

	if(lpstrExplicitClass)
		lpstrExplicitClass->clear();

	// Get ReceiveFolder information from the server
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getReceiveFolder(m_ecSessionId, sEntryId, (char*)strMessageClass.c_str(), &sReceiveFolderTable))
			er = KCERR_NETWORK_ERROR;
		else
			er = sReceiveFolderTable.er;
	}
	END_SOAP_CALL

	if(er == KCERR_NOT_FOUND && lpstrExplicitClass)
	{
		// This is only by an empty message store ??
		*lpcbEntryID = 0;
		*lppEntryID = NULL;

		hr = hrSuccess;
		goto exit;
	}

	if(hr != hrSuccess)
		goto exit;
	
	hr = CopySOAPEntryIdToMAPIEntryId(&sReceiveFolderTable.sReceiveFolder.sEntryId, &cbEntryID, &lpEntryID, NULL);
	if(hr != hrSuccess)
		goto exit;

	if(er != KCERR_NOT_FOUND && lpstrExplicitClass != NULL)
		*lpstrExplicitClass = utf8string::from_string(sReceiveFolderTable.sReceiveFolder.lpszAExplicitClass);

	*lppEntryID = lpEntryID;
	*lpcbEntryID = cbEntryID;

exit:
	if(hr != hrSuccess){
		if(lpEntryID)
			ECFreeBuffer(lpEntryID);
	}

	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrSetReceiveFolder(ULONG cbStoreID, LPENTRYID lpStoreID, const utf8string &strMessageClass, ULONG cbEntryID, LPENTRYID lpEntryID)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	unsigned int result;
	entryId		sStoreId = {0}; // Do not free
	entryId		sEntryId = {0}; // Do not free
	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	sStoreId.__ptr = (unsigned char*)lpUnWrapStoreID;
	sStoreId.__size = cbUnWrapStoreID;

	// Ignore error
	CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__setReceiveFolder(m_ecSessionId, sStoreId, (lpEntryID)?&sEntryId : NULL, (char*)strMessageClass.c_str(), &result))
			er = KCERR_NETWORK_ERROR;
		else
			er = result;
	}
	END_SOAP_CALL

exit:
    UnLockSoap();
    
	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrSetReadFlag(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, ULONG ulSyncId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;

	struct entryList sEntryList;
	entryId			 sEntryId;

	sEntryId.__ptr = (unsigned char*)lpEntryID;
	sEntryId.__size = cbEntryID;

	sEntryList.__size = 1;
	sEntryList.__ptr = &sEntryId;

	LockSoap();

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__setReadFlags(m_ecSessionId, ulFlags, NULL, &sEntryList, ulSyncId, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;

}

HRESULT WSTransport::HrSubmitMessage(ULONG cbMessageID, LPENTRYID lpMessageID, ULONG ulFlags)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId		sEntryId = {0}; // Do not free

	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbMessageID, lpMessageID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__submitMessage(m_ecSessionId, sEntryId, ulFlags, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrFinishedMessage(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	entryId		sEntryId = {0}; // Do not free

	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exit;
	
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__finishedMessage(m_ecSessionId, sEntryId, ulFlags, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAbortSubmit(ULONG cbEntryID, LPENTRYID lpEntryID)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	entryId		sEntryId = {0}; // Do not free

	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__abortSubmit(m_ecSessionId, sEntryId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrIsMessageInQueue(ULONG cbEntryID, LPENTRYID lpEntryID)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	entryId		sEntryId = {0}; // Do not free

	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__isMessageInQueue(m_ecSessionId, sEntryId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrResolveStore(LPGUID lpGuid, ULONG *lpulUserID, ULONG* lpcbStoreID, LPENTRYID* lppStoreID)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct resolveUserStoreResponse sResponse;
	struct xsd__base64Binary sStoreGuid = {0,0};

	LockSoap();

	if (!lpGuid){
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	sStoreGuid.__ptr = (unsigned char*)lpGuid;
	sStoreGuid.__size = sizeof(GUID);

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__resolveStore(m_ecSessionId, sStoreGuid, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	if(lpulUserID)
		*lpulUserID = sResponse.ulUserId;

	if(lpcbStoreID && lppStoreID) {
		// Create a client store entry, add the servername
		hr = WrapServerClientStoreEntry(sResponse.lpszServerPath ? sResponse.lpszServerPath : m_sProfileProps.strServerPath.c_str(), &sResponse.sStoreId, lpcbStoreID, lppStoreID);
		if(hr != hrSuccess)
			goto exit;
	}

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrResolveUserStore(const utf8string &strUserName, ULONG ulFlags, ULONG *lpulUserID, ULONG* lpcbStoreID, LPENTRYID* lppStoreID, std::string *lpstrRedirServer)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct resolveUserStoreResponse sResponse;

	LockSoap();

	if(strUserName.empty()){
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__resolveUserStore(m_ecSessionId, (char*)strUserName.c_str(), ECSTORE_TYPE_MASK_PRIVATE | ECSTORE_TYPE_MASK_PUBLIC, ulFlags, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	//END_SOAP_CALL
	if(er == KCERR_END_OF_SESSION) { if(HrReLogon() == hrSuccess) goto retry; }
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND);
	if (hr == MAPI_E_UNABLE_TO_COMPLETE)
	{
		if (lpstrRedirServer)
			*lpstrRedirServer = sResponse.lpszServerPath;
		else
			hr = MAPI_E_NOT_FOUND;
	}
	if(hr != hrSuccess)
		goto exit;

	if(lpulUserID) {
		*lpulUserID = sResponse.ulUserId;
	}

	if(lpcbStoreID && lppStoreID) {

		// Create a client store entry, add the servername
		hr = WrapServerClientStoreEntry(sResponse.lpszServerPath ? sResponse.lpszServerPath : m_sProfileProps.strServerPath.c_str(), &sResponse.sStoreId, lpcbStoreID, lppStoreID);
		if(hr != hrSuccess)
			goto exit;

	}

exit:
	UnLockSoap();

	return hr;
}

/**
 * Resolve a specific store type for a user.
 *
 * @param[in]	strUserName		The name of the user for whom to resolve the store. If left
 *								empty, the store for the current user will be resolved.
 * @param[in]	ulStoreType		The type of the store to resolve.
 * @param[out]	lpcbStoreID		The length of the returned entry id.
 * @param[out]	lppStoreID		The returned store entry id.
 *
 * @note	This method should be called on a transport that's already connected to the
 *			right server as redirection is not supported.
 */
HRESULT WSTransport::HrResolveTypedStore(const utf8string &strUserName, ULONG ulStoreType, ULONG* lpcbStoreID, LPENTRYID* lppStoreID)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct resolveUserStoreResponse sResponse;

	LockSoap();

	// Currently only archive stores are supported.
	if (ulStoreType != ECSTORE_TYPE_ARCHIVE || lpcbStoreID == NULL || lppStoreID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__resolveUserStore(m_ecSessionId, (char*)strUserName.c_str(), (1 << ulStoreType), 0, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if(lpcbStoreID && lppStoreID) {
		// Create a client store entry, add the servername
		hr = WrapServerClientStoreEntry(sResponse.lpszServerPath ? sResponse.lpszServerPath : m_sProfileProps.strServerPath.c_str(), &sResponse.sStoreId, lpcbStoreID, lppStoreID);
		if(hr != hrSuccess)
			goto exit;
	}

exit:
	UnLockSoap();

	return hr;
}

/**
 * Create a new user.
 * 
 * @param[in]	lpECUser	Pointer to an ECUSER object that contains the details of the user
 * @param[in]	ulFlags		MAPI_UNICODE, values in user struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcbUserId	The size in bytes of the entryid
 * @param[out]	lppUserId	The entry id of the new user
 * @return		HRESULT		MAPI error code.
 */
HRESULT WSTransport::HrCreateUser(ECUSER *lpECUser, ULONG ulFlags,
    ULONG *lpcbUserId, LPENTRYID *lppUserId)
{
	HRESULT	hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct user sUser{__gszeroinit};
	struct setUserResponse sResponse;
	convert_context converter;

	LockSoap();

	if(lpECUser == NULL || lpcbUserId == NULL || lppUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	sUser.lpszUsername		= TO_UTF8_DEF((char *)lpECUser->lpszUsername);
	sUser.lpszPassword		= TO_UTF8_DEF((char *)lpECUser->lpszPassword);
	sUser.lpszMailAddress	= TO_UTF8_DEF((char *)lpECUser->lpszMailAddress);
	sUser.ulUserId			= 0;
	sUser.ulObjClass		= lpECUser->ulObjClass;
	sUser.ulIsNonActive		= lpECUser->ulObjClass;		// Keep 6.40.0 servers happy
	sUser.ulIsAdmin			= lpECUser->ulIsAdmin;
	sUser.lpszFullName		= TO_UTF8_DEF((char *)lpECUser->lpszFullName);
	sUser.ulIsABHidden		= lpECUser->ulIsABHidden;
	sUser.ulCapacity		= lpECUser->ulCapacity;
	sUser.lpsPropmap		= NULL;
	sUser.lpsMVPropmap		= NULL;

	hr = CopyABPropsToSoap(&lpECUser->sPropmap, &lpECUser->sMVPropmap, ulFlags,
						   &sUser.lpsPropmap, &sUser.lpsMVPropmap);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__createUser(m_ecSessionId, &sUser, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sUserId, sResponse.ulUserId, lpcbUserId, lppUserId);

exit:
	UnLockSoap();

	FreeABProps(sUser.lpsPropmap, sUser.lpsMVPropmap);

	return hr;
}

/**
 * Get user struct on a specific user, or the user you're connected as.
 * 
 * @param[in]	cbUserID	Length as lpUserID
 * @param[in]	lpUserID	EntryID of a user, use NULL to retrieve 'yourself'
 * @param[in]	ulFlags		MAPI_UNICODE, return values in user struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lppECUser	Pointer to an ECUSER object that contains the user details
 * @return		HRESULT		MAPI error code.
 */
HRESULT WSTransport::HrGetUser(ULONG cbUserID, LPENTRYID lpUserID,
    ULONG ulFlags, ECUSER **lppECUser)
{
	HRESULT	hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct getUserResponse	sResponse;
	ECUSER *lpECUser = NULL;
	entryId	sUserId = {0};
	ULONG ulUserId = 0;

	LockSoap();

	if (lppECUser == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (lpUserID)
		ulUserId = ABEID_ID(lpUserID);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserID, lpUserID, &sUserId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getUser(m_ecSessionId, ulUserId, sUserId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserToUser(sResponse.lpsUser, ulFlags, &lpECUser);
	if(hr != hrSuccess)
		goto exit;

	*lppECUser = lpECUser;
	lpECUser = NULL;

exit:
	UnLockSoap();

	if (lpECUser != NULL)
		ECFreeBuffer(lpECUser);

	return hr;
}

/**
 * Update an existing user.
 *
 * This function can create a new user on an offline server.
 * 
 * @param[in]	lpECUser	Pointer to an ECUSER object that contains the details of the user
 * @param[in]	ulFlags		MAPI_UNICODE, values in user struct will be PT_UNICODE, otherwise in PT_STRING8
 * @return		HRESULT		MAPI error code.
 */
HRESULT WSTransport::HrSetUser(ECUSER *lpECUser, ULONG ulFlags)
{
	HRESULT	hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct user sUser{__gszeroinit};
	unsigned int result = 0;
	convert_context	converter;

	LockSoap();

	if(lpECUser == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	sUser.lpszUsername		= TO_UTF8_DEF(lpECUser->lpszUsername);
	sUser.lpszPassword		= TO_UTF8_DEF(lpECUser->lpszPassword);
	sUser.lpszMailAddress	= TO_UTF8_DEF(lpECUser->lpszMailAddress);
	sUser.ulUserId			= ABEID_ID(lpECUser->sUserId.lpb);
	sUser.ulObjClass		= lpECUser->ulObjClass;
	sUser.ulIsNonActive		= lpECUser->ulObjClass;		// Keep 6.40.0 servers happy
	sUser.ulIsAdmin			= lpECUser->ulIsAdmin;
	sUser.lpszFullName		= TO_UTF8_DEF(lpECUser->lpszFullName);
	sUser.sUserId.__ptr		= lpECUser->sUserId.lpb;
	sUser.sUserId.__size	= lpECUser->sUserId.cb;
	sUser.ulIsABHidden		= lpECUser->ulIsABHidden;
	sUser.ulCapacity		= lpECUser->ulCapacity;
	sUser.lpsPropmap		= NULL;
	sUser.lpsMVPropmap		= NULL;

	hr = CopyABPropsToSoap(&lpECUser->sPropmap, &lpECUser->sMVPropmap, ulFlags,
						   &sUser.lpsPropmap, &sUser.lpsMVPropmap);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__setUser(m_ecSessionId, &sUser, &result))
			er = KCERR_NETWORK_ERROR;
		else
			er = result;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	FreeABProps(sUser.lpsPropmap, sUser.lpsMVPropmap);

	return hr;
}

/** 
 * Creates a new store. This can be a store for a user or the public
 * store (for a company or everyone).
 * 
 * @param ulStoreType ECSTORE_TYPE_PRIVATE or ECSTORE_TYPE_PUBLIC
 * @param cbUserID Number of bytes in lpUserID
 * @param lpUserID EntryID of a user, everyone (public) or a company (public)
 * @param cbStoreID Number of bytes in lpStoreID
 * @param lpStoreID Store entryid for the new store
 * @param cbRootID Number of bytes in lpRootID
 * @param lpRootID Root folder entryid for the new store
 * @param ulFlags Flags
 *        	@arg @c EC_OVERRIDE_HOMESERVER   Allow the store to be created on
 *                                               another server than the users
 *                                               homeserver.
 * 
 * @return MAPI error code
 * @retval MAPI_E_NOT_FOUND User described in lpUserID does not exist
 * @retval MAPI_E_COLLISION Store already exists
 */
HRESULT WSTransport::HrCreateStore(ULONG ulStoreType, ULONG cbUserID, LPENTRYID lpUserID, ULONG cbStoreID, LPENTRYID lpStoreID, ULONG cbRootID, LPENTRYID lpRootID, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;

	entryId sUserId = {0};
	entryId	sStoreId = {0};
	entryId	sRootId = {0};

	LockSoap();

	if(lpUserID == NULL || lpStoreID == NULL || lpRootID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserID, lpUserID, &sUserId, true);
	if(hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbStoreID, lpStoreID, &sStoreId, true);
	if(hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbRootID, lpRootID, &sRootId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
	  if(SOAP_OK != m_lpCmd->ns__createStore(m_ecSessionId, ulStoreType, ABEID_ID(lpUserID), sUserId, sStoreId, sRootId, ulFlags, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrHookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, LPGUID lpGuid, ULONG ulSyncId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId		sUserId = {0};
	struct xsd__base64Binary sStoreGuid = {0,0};

	LockSoap();

	if (cbUserId == 0 || lpUserId == NULL || lpGuid == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if(hr != hrSuccess)
		goto exit;

	sStoreGuid.__ptr = (unsigned char*)lpGuid;
	sStoreGuid.__size = sizeof(GUID);
	
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__hookStore(m_ecSessionId, ulStoreType, sUserId, sStoreGuid, ulSyncId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrUnhookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG ulSyncId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId		sUserId = {0};

	LockSoap();

	if (cbUserId == 0 || lpUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__unhookStore(m_ecSessionId, ulStoreType, sUserId, ulSyncId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrRemoveStore(LPGUID lpGuid, ULONG ulSyncId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	struct xsd__base64Binary sStoreGuid = {0,0};

	LockSoap();

	if (lpGuid == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	sStoreGuid.__ptr = (unsigned char*)lpGuid;
	sStoreGuid.__size = sizeof(GUID);
	
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__removeStore(m_ecSessionId, sStoreGuid, ulSyncId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDeleteUser(ULONG cbUserId, LPENTRYID lpUserId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId = {0};

	LockSoap();
	
	if(cbUserId < CbNewABEID("") || lpUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__deleteUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &er))
			er = KCERR_NETWORK_ERROR;
	
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

/**
 * Get the list of users for a specific company.
 * 
 * @param[in]	cbCompanyId		The size in bytes of the entryid of the company
 * @param[in]	lpCompanyId		Pointer to the entryid of the company
 * @param[in]	ulFlags			MAPI_UNICODE, return values in user struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcUsers		Number of users returned.
 * @param[out]	lppsUsers		Array of ECUSER objects.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrGetUserList(ULONG cbCompanyId, LPENTRYID lpCompanyId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sCompanyId = {0};

	struct userListResponse sResponse;

	LockSoap();

	if(lpcUsers == NULL || lppsUsers == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (cbCompanyId > 0 && lpCompanyId != NULL)
	{
		hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
		if (hr != hrSuccess)
			goto exit;
	}

	*lpcUsers = 0;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getUserList(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcUsers, lppsUsers);
	if(hr != hrSuccess)
		goto exit;

exit:	
	UnLockSoap();

	
	return hr;
}

// IECServiceAdmin group functions
/**
 * Create a new group.
 * 
 * @param[in]	lpECGroup	Pointer to an ECGROUP object that contains the details of the group
 * @param[in]	ulFlags		MAPI_UNICODE, values in group struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcbUserId	The size in bytes of the entryid
 * @param[out]	lppUserId	The entry id of the new group
 * @return		HRESULT		MAPI error code.
 */
HRESULT WSTransport::HrCreateGroup(ECGROUP *lpECGroup, ULONG ulFlags,
    ULONG *lpcbGroupId, LPENTRYID *lppGroupId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct group sGroup{__gszeroinit};
	struct setGroupResponse sResponse;
	convert_context converter;

	LockSoap();

	if(lpECGroup == NULL || lpcbGroupId == NULL || lppGroupId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	sGroup.ulGroupId = 0;
	sGroup.lpszGroupname = TO_UTF8_DEF(lpECGroup->lpszGroupname);
	sGroup.lpszFullname = TO_UTF8_DEF(lpECGroup->lpszFullname);
	sGroup.lpszFullEmail = TO_UTF8_DEF(lpECGroup->lpszFullEmail);
	sGroup.ulIsABHidden = lpECGroup->ulIsABHidden;
	sGroup.lpsPropmap = NULL;
	sGroup.lpsMVPropmap = NULL;

	hr = CopyABPropsToSoap(&lpECGroup->sPropmap, &lpECGroup->sMVPropmap, ulFlags,
						   &sGroup.lpsPropmap, &sGroup.lpsMVPropmap);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__createGroup(m_ecSessionId, &sGroup, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sGroupId, sResponse.ulGroupId, lpcbGroupId, lppGroupId);

exit:
	UnLockSoap();

	FreeABProps(sGroup.lpsPropmap, sGroup.lpsMVPropmap);

	return hr;
}

/**
 * Update an existing group.
 *
 * This function can create a new group on an offline server.
 * 
 * @param[in]	lpECGroup	Pointer to an ECGROUP object that contains the details of the group
 * @param[in]	ulFlags		MAPI_UNICODE, values in group struct will be PT_UNICODE, otherwise in PT_STRING8
 * @return		HRESULT		MAPI error code.
 */
HRESULT WSTransport::HrSetGroup(ECGROUP *lpECGroup, ULONG ulFlags)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	convert_context converter;
	struct group sGroup{__gszeroinit};

	LockSoap();

	if(lpECGroup == NULL || lpECGroup->lpszGroupname == NULL || lpECGroup->lpszFullname == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	sGroup.lpszFullname = TO_UTF8_DEF(lpECGroup->lpszFullname);
	sGroup.lpszGroupname = TO_UTF8_DEF(lpECGroup->lpszGroupname);
	sGroup.lpszFullEmail = TO_UTF8_DEF(lpECGroup->lpszFullEmail);
	sGroup.sGroupId.__size = lpECGroup->sGroupId.cb;
	sGroup.sGroupId.__ptr = lpECGroup->sGroupId.lpb;
	sGroup.ulGroupId = ABEID_ID(lpECGroup->sGroupId.lpb);
	sGroup.ulIsABHidden = lpECGroup->ulIsABHidden;
	sGroup.lpsPropmap = NULL;
	sGroup.lpsMVPropmap = NULL;

	hr = CopyABPropsToSoap(&lpECGroup->sPropmap, &lpECGroup->sMVPropmap, ulFlags,
						   &sGroup.lpsPropmap, &sGroup.lpsMVPropmap);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__setGroup(m_ecSessionId, &sGroup, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	FreeABProps(sGroup.lpsPropmap, sGroup.lpsMVPropmap);

	return hr;
}

/**
 * Get group struct on a specific group.
 * 
 * @param[in]	cbGroupID	Length as lpGroupID
 * @param[in]	lpGroupID	EntryID of a group
 * @param[in]	ulFlags		MAPI_UNICODE, return values in group struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lppECGroup	Pointer to an ECGROUP object that contains the group details
 * @return		HRESULT		MAPI error code.
 */
HRESULT WSTransport::HrGetGroup(ULONG cbGroupID, LPENTRYID lpGroupID,
    ULONG ulFlags, ECGROUP **lppECGroup)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	ECGROUP *lpGroup = NULL;
	entryId sGroupId = {0};

	struct getGroupResponse sResponse;
	
	LockSoap();

	if (lpGroupID == NULL || lppECGroup == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupID, lpGroupID, &sGroupId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getGroup(m_ecSessionId, ABEID_ID(lpGroupID), sGroupId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapGroupToGroup(sResponse.lpsGroup, ulFlags, &lpGroup);
	if (hr != hrSuccess)
		goto exit;

	*lppECGroup = lpGroup;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDeleteGroup(ULONG cbGroupId, LPENTRYID lpGroupId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sGroupId = {0};

	LockSoap();
	
	if(cbGroupId < CbNewABEID("") || lpGroupId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__groupDelete(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

/**
 * Get the send-as-list of a specific user.
 *
 * @param[in]	cbUserId	Size in bytes of the user entryid.
 * @param[in]	lpUserId	Entryid of the user.
 * @param[in]	ulFlags		MAPI_UNICODE, return values in user structs will be PT_UNICODE, otherwise PT_STRING8
 * @param[out]	lpcSenders	The number of results.
 * @param[out]	lppSenders	Array of ECUSER objects.
 * @return		HRESULT		MAPI error code.
 */
HRESULT WSTransport::HrGetSendAsList(ULONG cbUserId, LPENTRYID lpUserId,
    ULONG ulFlags, ULONG *lpcSenders, ECUSER **lppSenders)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct userListResponse sResponse;
	entryId sUserId = {0};

	LockSoap();
	
	if(cbUserId < CbNewABEID("") || lpUserId == NULL || lpcSenders == NULL || lppSenders == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getSendAsList(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcSenders, lppSenders);
	if(hr != hrSuccess)
		goto exit;

exit:	
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAddSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId = {0};
	entryId sSenderId = {0};

	LockSoap();

	if (cbUserId < CbNewABEID("") || lpUserId == NULL || cbSenderId < CbNewABEID("") || lpSenderId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbSenderId, lpSenderId, &sSenderId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__addSendAsUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpSenderId), sSenderId, &er))
			er = KCERR_NETWORK_ERROR;
	
	}
	END_SOAP_CALL

exit:	
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDelSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId = {0};
	entryId sSenderId = {0};

	LockSoap();

	if (cbUserId < CbNewABEID("") || lpUserId == NULL || cbSenderId < CbNewABEID("") || lpSenderId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbSenderId, lpSenderId, &sSenderId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__delSendAsUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpSenderId), sSenderId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:	
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetUserClientUpdateStatus(ULONG cbUserId,
    LPENTRYID lpUserId, ULONG ulFlags, ECUSERCLIENTUPDATESTATUS **lppECUCUS)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId = {0};
	struct userClientUpdateStatusResponse sResponse;

    LockSoap();

    if (cbUserId < CbNewABEID("") || lpUserId == NULL) {
        hr = MAPI_E_INVALID_PARAMETER;
        goto exit;
    }

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
    if (hr != hrSuccess)
        goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getUserClientUpdateStatus(m_ecSessionId, sUserId, &sResponse) )
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

	hr = CopyUserClientUpdateStatusFromSOAP(sResponse, ulFlags, lppECUCUS);
	if (hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrRemoveAllObjects(ULONG cbUserId, LPENTRYID lpUserId)
{
    ECRESULT er = erSuccess;
    HRESULT hr = hrSuccess;
    entryId sUserId = {0};
    
    LockSoap();
    
	if (cbUserId < CbNewABEID("") || lpUserId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__removeAllObjects(m_ecSessionId, sUserId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:	
	UnLockSoap();

	return hr;
}

/**
 * Resolve a users entryid by name.
 *
 * @param[in]	lpszUserName	The username to resolve.
 * @param[in]	ulFlags			MAPI_UNICODE, lpszUserName is in PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcbUserId		The size in bytes of the entryid.
 * @param[out]	lppUserId		The entryid of the resolved user.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrResolveUserName(LPCTSTR lpszUserName, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct resolveUserResponse sResponse;

	LockSoap();

	if(lpszUserName == NULL || lpcbUserId == NULL || lppUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	//Resolve userid from username
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__resolveUsername(m_ecSessionId, (char*)convstring(lpszUserName, ulFlags).u8_str(), &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sUserId, sResponse.ulUserId, lpcbUserId, lppUserId);

exit:
	UnLockSoap();

	return hr;
}

/**
 * Resolve a group entryid by name.
 *
 * @param[in]	lpszGroupName	The groupname to resolve.
 * @param[in]	ulFlags			MAPI_UNICODE, lpszGroupName is in PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcbUserId		The size in bytes of the entryid.
 * @param[out]	lppUserId		The entryid of the resolved group.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrResolveGroupName(LPCTSTR lpszGroupName, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct resolveGroupResponse sResponse;

	LockSoap();

	if(lpszGroupName == NULL || lpcbGroupId == NULL || lppGroupId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	//Resolve groupid from groupname
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__resolveGroupname(m_ecSessionId, (char*)convstring(lpszGroupName, ulFlags).u8_str(), &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sGroupId, sResponse.ulGroupId, lpcbGroupId, lppGroupId);

exit:
	UnLockSoap();

	return hr;
}

/**
 * Get the list of groups for a specific company.
 * 
 * @param[in]	cbCompanyId		The size in bytes of the entryid of the company
 * @param[in]	lpCompanyId		Pointer to the entryid of the company
 * @param[in]	ulFlags			MAPI_UNICODE, return values in group struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcGroups		Number of groups returned.
 * @param[out]	lppsGroups		Array of ECGROUP objects.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrGetGroupList(ULONG cbCompanyId, LPENTRYID lpCompanyId,
    ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups)
{
	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;

 	struct groupListResponse sResponse;
	entryId					sCompanyId = {0};

	LockSoap();

	if(lpcGroups == NULL || lppsGroups == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;
	
	*lpcGroups = 0;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__getGroupList(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapGroupArrayToGroupArray(&sResponse.sGroupArray, ulFlags, lpcGroups, lppsGroups);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDeleteGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sGroupId = {0};
	entryId sUserId = {0};

	LockSoap();

	if (!lpGroupId || !lpUserId) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	// Remove group
	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__deleteGroupUser(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, ABEID_ID(lpUserId), sUserId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAddGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;

	entryId sGroupId = {0};
	entryId sUserId = {0};

	LockSoap();

	if (!lpGroupId || !lpUserId) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	// Remove group
	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__addGroupUser(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, ABEID_ID(lpUserId), sUserId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

/**
 * Get the list of users in a specific group.
 * 
 * @param[in]	cbGroupId		The size in bytes of the entryid of the group
 * @param[in]	lpGroupId		Pointer to the entryid of the group
 * @param[in]	ulFlags			MAPI_UNICODE, return values in user struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcUsers		Number of users returned.
 * @param[out]	lppsUsers		Array of ECUSER objects.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrGetUserListOfGroup(ULONG cbGroupId, LPENTRYID lpGroupId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct userListResponse sResponse;
	entryId sGroupId = {0};

	LockSoap();

	if(lpGroupId == NULL || lpcUsers == NULL || lppsUsers == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exit;

	// Get an userlist of a group
	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__getUserListOfGroup(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcUsers, lppsUsers);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	return hr;
}

/**
 * Get the list of groups of which a specifi user is a member.
 * 
 * @param[in]	cbUserId		The size in bytes of the entryid of the user
 * @param[in]	cbUserId		Pointer to the entryid of the user
 * @param[in]	ulFlags			MAPI_UNICODE, return values in group struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcGroup		Number of groups returned.
 * @param[out]	lppsGroups		Array of ECGROUP objects.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrGetGroupListOfUser(ULONG cbUserId, LPENTRYID lpUserId,
    ULONG ulFlags, ULONG *lpcGroup, ECGROUP **lppsGroups)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct groupListResponse sResponse;
	entryId sUserId = {0};

	LockSoap();

	if(lpcGroup == NULL || lpUserId == NULL || lppsGroups == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	// Get a grouplist of an user
	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__getGroupListOfUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapGroupArrayToGroupArray(&sResponse.sGroupArray, ulFlags, lpcGroup, lppsGroups);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	return hr;
}

/**
 * Create a new company.
 * 
 * @param[in]	lpECCompany		Pointer to an ECCOMPANY object that contains the details of the company
 * @param[in]	ulFlags			MAPI_UNICODE, values in company struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcbCompanyId	The size in bytes of the entryid
 * @param[out]	lppCompanyId	The entry id of the new company
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrCreateCompany(ECCOMPANY *lpECCompany, ULONG ulFlags,
    ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct company sCompany{__gszeroinit};
	struct setCompanyResponse sResponse;
	convert_context	converter;

	LockSoap();

	if(lpECCompany == NULL || lpcbCompanyId == NULL || lppCompanyId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	sCompany.ulAdministrator = 0;
	sCompany.lpszCompanyname = TO_UTF8_DEF(lpECCompany->lpszCompanyname);
	sCompany.ulIsABHidden = lpECCompany->ulIsABHidden;
	sCompany.lpsPropmap = NULL;
	sCompany.lpsMVPropmap = NULL;

	hr = CopyABPropsToSoap(&lpECCompany->sPropmap, &lpECCompany->sMVPropmap, ulFlags,
						   &sCompany.lpsPropmap, &sCompany.lpsMVPropmap);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__createCompany(m_ecSessionId, &sCompany, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sCompanyId, sResponse.ulCompanyId, MAPI_ABCONT, lpcbCompanyId, lppCompanyId);

exit:
	UnLockSoap();

	FreeABProps(sCompany.lpsPropmap, sCompany.lpsMVPropmap);

	return hr;
}

HRESULT WSTransport::HrDeleteCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sCompanyId = {0};

	LockSoap();
	
	if(cbCompanyId < CbNewABEID("") || lpCompanyId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__deleteCompany(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

/**
 * Update an existing company.
 *
 * This function can create a new comapyn on an offline server.
 * 
 * @param[in]	lpECCompany	Pointer to an ECCOMPANY object that contains the details of the company
 * @param[in]	ulFlags		MAPI_UNICODE, values in company struct will be PT_UNICODE, otherwise in PT_STRING8
 * @return		HRESULT		MAPI error code.
 */
HRESULT WSTransport::HrSetCompany(ECCOMPANY *lpECCompany, ULONG ulFlags)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct company sCompany{__gszeroinit};
	convert_context converter;

	LockSoap();

	if(lpECCompany == NULL || lpECCompany->lpszCompanyname == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	sCompany.lpszCompanyname = TO_UTF8_DEF(lpECCompany->lpszCompanyname);

	sCompany.ulCompanyId = ABEID_ID(lpECCompany->sCompanyId.lpb);
	sCompany.sCompanyId.__size = lpECCompany->sCompanyId.cb;
	sCompany.sCompanyId.__ptr = lpECCompany->sCompanyId.lpb;

	sCompany.ulAdministrator = ABEID_ID(lpECCompany->sAdministrator.lpb);
	sCompany.sAdministrator.__size = lpECCompany->sAdministrator.cb;
	sCompany.sAdministrator.__ptr = lpECCompany->sAdministrator.lpb;

	sCompany.ulIsABHidden = lpECCompany->ulIsABHidden;

	sCompany.lpsPropmap = NULL;
	sCompany.lpsMVPropmap = NULL;

	hr = CopyABPropsToSoap(&lpECCompany->sPropmap, &lpECCompany->sMVPropmap, ulFlags,
						   &sCompany.lpsPropmap, &sCompany.lpsMVPropmap);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__setCompany(m_ecSessionId, &sCompany, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	FreeABProps(sCompany.lpsPropmap, sCompany.lpsMVPropmap);

	return hr;
}

/**
 * Get company struct on a specific company.
 * 
 * @param[in]	cbCompanyId		Length as lpCompanyId
 * @param[in]	lpCompanyId		EntryID of a company
 * @param[in]	ulFlags			MAPI_UNICODE, return values in company struct will be PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lppECCompany	Pointer to an ECOMPANY object that contains the company details
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrGetCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId,
    ULONG ulFlags, ECCOMPANY **lppECCompany)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	ECCOMPANY *lpCompany = NULL;
	struct getCompanyResponse sResponse;
	entryId sCompanyId = {0};

	LockSoap();

	if (lpCompanyId == NULL || lppECCompany == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__getCompany(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapCompanyToCompany(sResponse.lpsCompany, ulFlags, &lpCompany);
	if (hr != hrSuccess)
		goto exit;
	
	*lppECCompany = lpCompany;

exit:
	UnLockSoap();

	return hr;
}

/**
 * Resolve a company's entryid by name.
 *
 * @param[in]	lpszCompanyName	The companyname to resolve.
 * @param[in]	ulFlags			MAPI_UNICODE, lpszCompanyName is in PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcbCompanyId	The size in bytes of the entryid.
 * @param[out]	lppCompanyId	The entryid of the resolved company.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrResolveCompanyName(LPCTSTR lpszCompanyName, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct resolveCompanyResponse sResponse;

	LockSoap();

	if(lpszCompanyName == NULL || lpcbCompanyId == NULL || lppCompanyId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	//Resolve companyid from companyname
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__resolveCompanyname(m_ecSessionId, (char*)convstring(lpszCompanyName, ulFlags).u8_str(), &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sCompanyId, sResponse.ulCompanyId, MAPI_ABCONT, lpcbCompanyId, lppCompanyId);

exit:
	UnLockSoap();

	return hr;
}

/**
 * Get the list of available companies.
 *
 * @param[in]	ulFlags			MAPI_UNICODE, return values in company structs are in PT_UNICODE, otherwise in PT_STRING8
 * @param[out]	lpcCompanies	The number of companies.
 * @param[out]	lppsCompanies	Pointer to an array of ECCOMPANY objects.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrGetCompanyList(ULONG ulFlags, ULONG *lpcCompanies,
    ECCOMPANY **lppsCompanies)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct companyListResponse sResponse;

	LockSoap();

	if(lpcCompanies == NULL || lppsCompanies == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	*lpcCompanies = 0;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getCompanyList(m_ecSessionId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = SoapCompanyArrayToCompanyArray(&sResponse.sCompanyArray, ulFlags, lpcCompanies, lppsCompanies);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAddCompanyToRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;

	entryId sSetCompanyId = {0};
	entryId	sCompanyId = {0};

	LockSoap();

	if (lpSetCompanyId == NULL || lpCompanyId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbSetCompanyId, lpSetCompanyId, &sSetCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__addCompanyToRemoteViewList(m_ecSessionId, ABEID_ID(lpSetCompanyId), sSetCompanyId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDelCompanyFromRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sSetCompanyId = {0};
	entryId sCompanyId = {0};

	LockSoap();

	if (lpSetCompanyId == NULL || lpCompanyId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbSetCompanyId, lpSetCompanyId, &sSetCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__delCompanyFromRemoteViewList(m_ecSessionId, ABEID_ID(lpSetCompanyId), sSetCompanyId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

/**
 * Get the remote-view-list of a specific company.
 *
 * @param[in]	cbCompanyId		Size in bytes of the company entryid.
 * @param[in]	lpCompanyId		Entryid of the company.
 * @param[in]	ulFlags			MAPI_UNICODE, return values in company structs will be PT_UNICODE, otherwise PT_STRING8
 * @param[out]	lpcCompanies	The number of results.
 * @param[out]	lppsCompanies	Array of ECCOMPANY objects.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrGetRemoteViewList(ULONG cbCompanyId,
    LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcCompanies,
    ECCOMPANY **lppsCompanies)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct companyListResponse sResponse;
	entryId sCompanyId = {0};

	LockSoap();

	if(lpcCompanies == NULL || lpCompanyId == NULL || lppsCompanies == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	*lpcCompanies = 0;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__getRemoteViewList(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapCompanyArrayToCompanyArray(&sResponse.sCompanyArray, ulFlags, lpcCompanies, lppsCompanies);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAddUserToRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sUserId = {0};
	entryId sCompanyId = {0};

	LockSoap();

	if (lpUserId == NULL || lpCompanyId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__addUserToRemoteAdminList(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDelUserFromRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sUserId = {0};
	entryId sCompanyId = {0};

	LockSoap();

	if (lpUserId == NULL || lpCompanyId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__delUserFromRemoteAdminList(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

/**
 * Get the remote-admin-list of a specific company.
 *
 * @param[in]	cbCompanyId		Size in bytes of the company entryid.
 * @param[in]	lpCompanyId		Entryid of the company.
 * @param[in]	ulFlags			MAPI_UNICODE, return values in user structs will be PT_UNICODE, otherwise PT_STRING8
 * @param[out]	lpcUsers		The number of results.
 * @param[out]	lppsUsers		Array of ECUSER objects.
 * @return		HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrGetRemoteAdminList(ULONG cbCompanyId,
    LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct userListResponse sResponse;
	entryId sCompanyId = {0};

	LockSoap();

	if(lpcUsers == NULL || lpCompanyId == NULL || lppsUsers == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	*lpcUsers = 0;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__getRemoteAdminList(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcUsers, lppsUsers);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetPermissionRules(int ulType, ULONG cbEntryID,
    LPENTRYID lpEntryID, ULONG *lpcPermissions,
    ECPERMISSION **lppECPermissions)
{
	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;
	entryId			sEntryId = {0}; // Do not free
	ECPERMISSION *lpECPermissions = NULL;
	
	LPENTRYID		lpUnWrapStoreID = NULL;
	ULONG			cbUnWrapStoreID = 0;

	struct rightsResponse sRightResponse;

	LockSoap();

	if(lpcPermissions == NULL || lppECPermissions == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Remove servername, always
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	sEntryId.__ptr = (unsigned char*)lpUnWrapStoreID;
	sEntryId.__size = cbUnWrapStoreID;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getRights(m_ecSessionId, sEntryId, ulType, &sRightResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sRightResponse.er;

	}
	END_SOAP_CALL

	ECAllocateBuffer(sizeof(ECPERMISSION) * sRightResponse.pRightsArray->__size, (void**)&lpECPermissions);
	for (gsoap_size_t i = 0; i < sRightResponse.pRightsArray->__size; ++i) {
		lpECPermissions[i].ulRights	= sRightResponse.pRightsArray->__ptr[i].ulRights;
		lpECPermissions[i].ulState	= sRightResponse.pRightsArray->__ptr[i].ulState;
		lpECPermissions[i].ulType	= sRightResponse.pRightsArray->__ptr[i].ulType;

		hr = CopySOAPEntryIdToMAPIEntryId(&sRightResponse.pRightsArray->__ptr[i].sUserId, sRightResponse.pRightsArray->__ptr[i].ulUserid, MAPI_MAILUSER, (ULONG*)&lpECPermissions[i].sUserId.cb, (LPENTRYID*)&lpECPermissions[i].sUserId.lpb, lpECPermissions);
		if (hr != hrSuccess)
			goto exit;
	}

	*lppECPermissions = lpECPermissions;
	*lpcPermissions = sRightResponse.pRightsArray->__size;
	lpECPermissions = NULL;

exit:
	if (lpECPermissions != NULL)
		ECFreeBuffer(lpECPermissions);

	UnLockSoap();

	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrSetPermissionRules(ULONG cbEntryID, LPENTRYID lpEntryID,
    ULONG cPermissions, ECPERMISSION *lpECPermissions)
{
	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;
	entryId			sEntryId = {0}; // Do not free
	int				nChangedItems = 0;
	unsigned int	i,
					nItem;
	LPENTRYID		lpUnWrapStoreID = NULL;
	ULONG			cbUnWrapStoreID = 0;
	
	struct rightsArray rArray;
	
	LockSoap();

	if(cPermissions == 0 || lpECPermissions == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Remove servername, always
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	sEntryId.__ptr = (unsigned char*)lpUnWrapStoreID;
	sEntryId.__size = cbUnWrapStoreID;

	// Count the updated items
	for (i = 0; i < cPermissions; ++i)
		if(lpECPermissions[i].ulState != RIGHT_NORMAL)
			++nChangedItems;

	rArray.__ptr = s_alloc<rights>(m_lpCmd->soap, nChangedItems);
	rArray.__size = nChangedItems;

	nItem = 0;
	for (i = 0 ; i < cPermissions; ++i) {
		if(lpECPermissions[i].ulState != RIGHT_NORMAL){
			rArray.__ptr[nItem].ulRights = lpECPermissions[i].ulRights;
			rArray.__ptr[nItem].ulState	 = lpECPermissions[i].ulState;
			rArray.__ptr[nItem].ulType	 = lpECPermissions[i].ulType;
			rArray.__ptr[nItem].ulUserid = ABEID_ID(lpECPermissions[i].sUserId.lpb);

			hr = CopyMAPIEntryIdToSOAPEntryId(lpECPermissions[i].sUserId.cb, (LPENTRYID)lpECPermissions[i].sUserId.lpb, &rArray.__ptr[nItem].sUserId, true);
			if (hr != hrSuccess)
				goto exit;
			++nItem;
		}
	}

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__setRights(m_ecSessionId, sEntryId, &rArray, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrGetOwner(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG *lpcbOwnerId, LPENTRYID *lppOwnerId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sEntryId = {0}; // Do not free
	struct getOwnerResponse sResponse;
	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	if (lpcbOwnerId == NULL || lppOwnerId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Remove servername, always
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	sEntryId.__ptr = (unsigned char*)lpUnWrapStoreID;
	sEntryId.__size = cbUnWrapStoreID;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getOwner(m_ecSessionId, sEntryId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sOwner, sResponse.ulOwner, lpcbOwnerId, lppOwnerId);

exit:
	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	UnLockSoap();

	return hr;
}

/**
 * Calls ns__abResolveNames.
 *
 * Converts client input to SOAP structs, and calls ns__abResolveNames
 * on server. Server soap results are converted back into lpAdrList
 * and lpFlagList.
 *
 * @param[in]		lpPropTagArray	Requested properties from server in rows of lpAdrList. May not be NULL.
 * @param[in]		ulFlags			Client flags passed to server.
 * @param[in/out]	lpAdrList		Contains one search request per row, using PR_DISPLAY_NAME.
 * @param[in/out]	lpFlagList		Contains current status of matching row in lpAdrList, eg MAPI_(UN)RESOLVED.
 * @return			HRESULT			MAPI error code.
 */
HRESULT WSTransport::HrResolveNames(LPSPropTagArray lpPropTagArray, ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct propTagArray aPropTag;
	struct rowSet* lpsRowSet = NULL;
	struct flagArray aFlags;
	struct abResolveNamesResponse sResponse;
	convert_context	converter;

	LockSoap();

	aPropTag.__ptr = (unsigned int *)&lpPropTagArray->aulPropTag; // just a reference
	aPropTag.__size = lpPropTagArray->cValues;

	aFlags.__ptr = (unsigned int *)&lpFlagList->ulFlag;
	aFlags.__size = lpFlagList->cFlags;

	hr = CopyMAPIRowSetToSOAPRowSet((LPSRowSet)lpAdrList, &lpsRowSet, &converter);
	if(hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__abResolveNames(m_ecSessionId, &aPropTag, lpsRowSet, &aFlags, ulFlags, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL
	
	ASSERT(sResponse.aFlags.__size == lpFlagList->cFlags);
	ASSERT((ULONG)sResponse.sRowSet.__size == lpAdrList->cEntries);

	for (gsoap_size_t i = 0; i < sResponse.aFlags.__size; ++i) {
		// Set the resolved items
		if(lpFlagList->ulFlag[i] == MAPI_UNRESOLVED && sResponse.aFlags.__ptr[i] == MAPI_RESOLVED)
		{
			lpAdrList->aEntries[i].cValues = sResponse.sRowSet.__ptr[i].__size;
			ECFreeBuffer(lpAdrList->aEntries[i].rgPropVals);

			ECAllocateBuffer(sizeof(SPropValue)*lpAdrList->aEntries[i].cValues, (void**)&lpAdrList->aEntries[i].rgPropVals);

			hr = CopySOAPRowToMAPIRow(&sResponse.sRowSet.__ptr[i], lpAdrList->aEntries[i].rgPropVals, (void*)lpAdrList->aEntries[i].rgPropVals, &converter);
			if(hr != hrSuccess)
				goto exit;

			lpFlagList->ulFlag[i] = sResponse.aFlags.__ptr[i];
		}else { // MAPI_AMBIGUOUS or MAPI_UNRESOLVED
			// only set the flag, do nothing with the row
			lpFlagList->ulFlag[i] = sResponse.aFlags.__ptr[i];
		}		
	}

exit:
	UnLockSoap();

	if(lpsRowSet)
		FreeRowSet(lpsRowSet, true);

	return hr;
}

HRESULT WSTransport::HrSyncUsers(ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	unsigned int sResponse;
	entryId sCompanyId = {0};
	ULONG ulCompanyId = 0;

	LockSoap();

	if (lpCompanyId)
	{
		hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
		if (hr != hrSuccess)
			goto exit;
		ulCompanyId = ABEID_ID(lpCompanyId);
	}

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__syncUsers(m_ecSessionId, ulCompanyId, sCompanyId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = (ECRESULT) sResponse;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::GetQuota(ULONG cbUserId, LPENTRYID lpUserId,
    bool bGetUserDefault, ECQUOTA **lppsQuota)
{
	ECRESULT				er = erSuccess;
	HRESULT					hr = hrSuccess;
	struct quotaResponse	sResponse;
	ECQUOTA *lpsQuota =  NULL;
	entryId					sUserId = {0};
	
	LockSoap();
	
	if(lppsQuota == NULL || lpUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__GetQuota(m_ecSessionId, ABEID_ID(lpUserId), sUserId, bGetUserDefault, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = (ECRESULT) sResponse.er;
	}
	END_SOAP_CALL

	ECAllocateBuffer(sizeof(ECQUOTA), (void**)&lpsQuota);

	lpsQuota->bUseDefaultQuota = sResponse.sQuota.bUseDefaultQuota;
	lpsQuota->bIsUserDefaultQuota = sResponse.sQuota.bIsUserDefaultQuota;
	lpsQuota->llHardSize = sResponse.sQuota.llHardSize;
	lpsQuota->llSoftSize = sResponse.sQuota.llSoftSize;
	lpsQuota->llWarnSize = sResponse.sQuota.llWarnSize;

	*lppsQuota = lpsQuota;

  exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::SetQuota(ULONG cbUserId, LPENTRYID lpUserId,
    ECQUOTA *lpsQuota)
{
	ECRESULT				er = erSuccess;
	HRESULT					hr = hrSuccess;
	unsigned int			sResponse;
	struct quota			sQuota;
	entryId					sUserId = {0};
	
	LockSoap();

	if(lpsQuota == NULL || lpUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	sQuota.bUseDefaultQuota = lpsQuota->bUseDefaultQuota;
	sQuota.bIsUserDefaultQuota = lpsQuota->bIsUserDefaultQuota;
	sQuota.llHardSize = lpsQuota->llHardSize;
	sQuota.llSoftSize = lpsQuota->llSoftSize;
	sQuota.llWarnSize = lpsQuota->llWarnSize;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__SetQuota(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sQuota, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = (ECRESULT) sResponse;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::AddQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sCompanyId = {0};
	entryId	sRecipientId = {0};

	LockSoap();

	if (lpCompanyId == NULL || lpRecipientId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbRecipientId, lpRecipientId, &sRecipientId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__AddQuotaRecipient(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, ABEID_ID(lpRecipientId), sRecipientId, ulType, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();
	return hr;
}

HRESULT WSTransport::DeleteQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sCompanyId = {0};
	entryId sRecipientId = {0};

	LockSoap();

	if (lpCompanyId == NULL || lpRecipientId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbRecipientId, lpRecipientId, &sRecipientId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__DeleteQuotaRecipient(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, ABEID_ID(lpRecipientId), sRecipientId, ulType, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::GetQuotaRecipients(ULONG cbUserId, LPENTRYID lpUserId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;
	entryId		sUserId = {0};
	struct userListResponse sResponse;

	LockSoap();

	if(lpcUsers == NULL || lppsUsers == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	*lpcUsers = 0;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__GetQuotaRecipients(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcUsers, lppsUsers);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::GetQuotaStatus(ULONG cbUserId, LPENTRYID lpUserId,
    ECQUOTASTATUS **lppsQuotaStatus)
{
	ECRESULT				er = erSuccess;
	HRESULT					hr = hrSuccess;
	struct quotaStatus		sResponse;
	ECQUOTASTATUS *lpsQuotaStatus =  NULL;
	entryId					sUserId = {0};
	
	LockSoap();

	if(lppsQuotaStatus == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__GetQuotaStatus(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = (ECRESULT) sResponse.er;
	}
	END_SOAP_CALL

	ECAllocateBuffer(sizeof(ECQUOTASTATUS), (void**)&lpsQuotaStatus);

	lpsQuotaStatus->llStoreSize = sResponse.llStoreSize;
	lpsQuotaStatus->quotaStatus = (eQuotaStatus)sResponse.ulQuotaStatus;

	*lppsQuotaStatus = lpsQuotaStatus;

  exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrPurgeSoftDelete(ULONG ulDays)
{
    HRESULT						hr = hrSuccess;
    ECRESULT					er = erSuccess;
    
    LockSoap();
    
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__purgeSoftDelete(m_ecSessionId, ulDays, &er))
			er = KCERR_NETWORK_ERROR;
        
	}
	END_SOAP_CALL
    
exit:
    UnLockSoap();
    
    return hr;
}

HRESULT WSTransport::HrPurgeCache(ULONG ulFlags)
{
    HRESULT						hr = hrSuccess;
    ECRESULT					er = erSuccess;
    
    LockSoap();
    
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__purgeCache(m_ecSessionId, ulFlags, &er))
			er = KCERR_NETWORK_ERROR;
        
	}
	END_SOAP_CALL
    
exit:
    UnLockSoap();
    
    return hr;
}

HRESULT WSTransport::HrPurgeDeferredUpdates(ULONG *lpulRemaining)
{
    HRESULT						hr = hrSuccess;
    ECRESULT					er = erSuccess;
    struct purgeDeferredUpdatesResponse sResponse;
    
    LockSoap();
    
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__purgeDeferredUpdates(m_ecSessionId, &sResponse))
			er = KCERR_NETWORK_ERROR;
        else
            er = sResponse.er;
            
        *lpulRemaining = sResponse.ulDeferredRemaining;
        
	}
	END_SOAP_CALL
    
exit:
    UnLockSoap();
    
    return hr;
}

HRESULT WSTransport::HrResolvePseudoUrl(const char *lpszPseudoUrl, char **lppszServerPath, bool *lpbIsPeer)
{
	ECRESULT						er = erSuccess;
	HRESULT							hr = hrSuccess;
	struct resolvePseudoUrlResponse sResponse{__gszeroinit};
	char							*lpszServerPath = NULL;
	unsigned int					ulLen = 0;
	ECsResolveResult				*lpCachedResult = NULL;
	ECsResolveResult				cachedResult;

	if (lpszPseudoUrl == NULL || lppszServerPath == NULL) {
		return MAPI_E_INVALID_PARAMETER;
	}

	// First try the cache
	pthread_mutex_lock(&m_ResolveResultCacheMutex);
	er = m_ResolveResultCache.GetCacheItem(lpszPseudoUrl, &lpCachedResult);
	if (er == erSuccess) {
		hr = lpCachedResult->hr;
		if (hr == hrSuccess) {
			ulLen = lpCachedResult->serverPath.length() + 1;
			hr = ECAllocateBuffer(ulLen, (void**)&lpszServerPath);
			if (hr == hrSuccess) {
				memcpy(lpszServerPath, lpCachedResult->serverPath.c_str(), ulLen);
				*lppszServerPath = lpszServerPath;
				*lpbIsPeer = lpCachedResult->isPeer;
			}
		}
		pthread_mutex_unlock(&m_ResolveResultCacheMutex);
		return hr;	// Early exit
	}
	pthread_mutex_unlock(&m_ResolveResultCacheMutex);

	// Cache failed. Try the server
	LockSoap();

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__resolvePseudoUrl(m_ecSessionId, (char*)lpszPseudoUrl, &sResponse))
    		er = KCERR_NETWORK_ERROR;
    	else
    		er = (ECRESULT)sResponse.er;
    }
	END_SOAP_CALL

	cachedResult.hr = hr;
	if (hr == hrSuccess) {
		cachedResult.isPeer = sResponse.bIsPeer;
		cachedResult.serverPath = sResponse.lpszServerPath;
	}

	pthread_mutex_lock(&m_ResolveResultCacheMutex);
	m_ResolveResultCache.AddCacheItem(lpszPseudoUrl, cachedResult);
	pthread_mutex_unlock(&m_ResolveResultCacheMutex);

	ulLen = strlen(sResponse.lpszServerPath) + 1;
	hr = ECAllocateBuffer(ulLen, (void**)&lpszServerPath);
	if (hr != hrSuccess)
		goto exit;

	memcpy(lpszServerPath, sResponse.lpszServerPath, ulLen);
	*lppszServerPath = lpszServerPath;
	*lpbIsPeer = sResponse.bIsPeer;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetServerDetails(ECSVRNAMELIST *lpServerNameList,
    ULONG ulFlags, ECSERVERLIST **lppsServerList)
{
	ECRESULT						er = erSuccess;
	HRESULT							hr = hrSuccess;
	struct getServerDetailsResponse sResponse{__gszeroinit};
	struct mv_string8				*lpsSvrNameList = NULL;

	LockSoap();

	if (lpServerNameList == NULL || lppsServerList == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	
	hr = SvrNameListToSoapMvString8(lpServerNameList, ulFlags & MAPI_UNICODE, &lpsSvrNameList);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
    	if( SOAP_OK != m_lpCmd->ns__getServerDetails(m_ecSessionId, *lpsSvrNameList, ulFlags & ~MAPI_UNICODE, &sResponse))
    		er = KCERR_NETWORK_ERROR;
    	else
    		er = (ECRESULT)sResponse.er;
    }
    END_SOAP_CALL

	hr = SoapServerListToServerList(&sResponse.sServerList, ulFlags & MAPI_UNICODE, lppsServerList);
	if (hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();
	
	if (lpsSvrNameList)
		ECFreeBuffer(lpsSvrNameList);

	return hr;
}

HRESULT WSTransport::HrGetChanges(const std::string& sourcekey, ULONG ulSyncId, ULONG ulChangeId, ULONG ulSyncType, ULONG ulFlags, LPSRestriction lpsRestrict, ULONG *lpulMaxChangeId, ULONG* lpcChanges, ICSCHANGE **lppChanges){
	HRESULT						hr = hrSuccess;
	ECRESULT					er = erSuccess;
	struct icsChangeResponse	sResponse;
	ICSCHANGE *					lpChanges = NULL;
	struct xsd__base64Binary	sSourceKey;
	struct restrictTable		*lpsSoapRestrict = NULL;
	
	sSourceKey.__ptr = (unsigned char *)sourcekey.c_str();
	sSourceKey.__size = sourcekey.size();

	LockSoap();

	if(lpsRestrict) {
    	hr = CopyMAPIRestrictionToSOAPRestriction(&lpsSoapRestrict, lpsRestrict);
    	if(hr != hrSuccess)
	        goto exit;
    }

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getChanges(m_ecSessionId, sSourceKey, ulSyncId, ulChangeId, ulSyncType, ulFlags, lpsSoapRestrict, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = (ECRESULT) sResponse.er;

	}
	END_SOAP_CALL

	ECAllocateBuffer(sResponse.sChangesArray.__size * sizeof(ICSCHANGE), (void**)&lpChanges);

	for (gsoap_size_t i = 0; i < sResponse.sChangesArray.__size; ++i) {
		lpChanges[i].ulChangeId = sResponse.sChangesArray.__ptr[i].ulChangeId;
		lpChanges[i].ulChangeType = sResponse.sChangesArray.__ptr[i].ulChangeType;
		lpChanges[i].ulFlags = sResponse.sChangesArray.__ptr[i].ulFlags;

		if(sResponse.sChangesArray.__ptr[i].sSourceKey.__size > 0) {
			ECAllocateMore( sResponse.sChangesArray.__ptr[i].sSourceKey.__size, lpChanges, (void **)&lpChanges[i].sSourceKey.lpb);
			lpChanges[i].sSourceKey.cb = sResponse.sChangesArray.__ptr[i].sSourceKey.__size;
			memcpy(lpChanges[i].sSourceKey.lpb, sResponse.sChangesArray.__ptr[i].sSourceKey.__ptr, sResponse.sChangesArray.__ptr[i].sSourceKey.__size);
		}
		
		if(sResponse.sChangesArray.__ptr[i].sParentSourceKey.__size > 0) {
			ECAllocateMore( sResponse.sChangesArray.__ptr[i].sParentSourceKey.__size, lpChanges, (void **)&lpChanges[i].sParentSourceKey.lpb);
			lpChanges[i].sParentSourceKey.cb = sResponse.sChangesArray.__ptr[i].sParentSourceKey.__size;
			memcpy(lpChanges[i].sParentSourceKey.lpb, sResponse.sChangesArray.__ptr[i].sParentSourceKey.__ptr, sResponse.sChangesArray.__ptr[i].sParentSourceKey.__size);
		}
		
	}
	
	*lpulMaxChangeId = sResponse.ulMaxChangeId;
	*lpcChanges = sResponse.sChangesArray.__size;
	*lppChanges = lpChanges;

exit:
	UnLockSoap();
	
	if(lpsSoapRestrict)
	    FreeRestrictTable(lpsSoapRestrict);

	if(hr!=hrSuccess && lpChanges)
		ECFreeBuffer(lpChanges);

	return hr;
}

HRESULT WSTransport::HrSetSyncStatus(const std::string& sourcekey, ULONG ulSyncId, ULONG ulChangeId, ULONG ulSyncType, ULONG ulFlags, ULONG* lpulSyncId){
	HRESULT				hr = hrSuccess;
	ECRESULT			er = erSuccess;
	struct setSyncStatusResponse sResponse;
	struct xsd__base64Binary sSourceKey;
	
	sSourceKey.__size = sourcekey.size();
	sSourceKey.__ptr = (unsigned char *)sourcekey.c_str();

	LockSoap();

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__setSyncStatus(m_ecSessionId, sSourceKey, ulSyncId, ulChangeId, ulSyncType, ulFlags, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = (ECRESULT) sResponse.er;

	}
	END_SOAP_CALL

	*lpulSyncId = sResponse.ulSyncId;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrEntryIDFromSourceKey(ULONG cbStoreID, LPENTRYID lpStoreID, ULONG ulFolderSourceKeySize, BYTE * lpFolderSourceKey, ULONG ulMessageSourceKeySize, BYTE * lpMessageSourceKey, ULONG * lpcbEntryID, LPENTRYID * lppEntryID)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId		sStoreId;
	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	struct xsd__base64Binary	folderSourceKey;
	struct xsd__base64Binary	messageSourceKey;

	struct getEntryIDFromSourceKeyResponse sResponse;	
	

	LockSoap();

	if(ulFolderSourceKeySize == 0 || lpFolderSourceKey == NULL){
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	
	hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exit;

	sStoreId.__ptr = (unsigned char*)lpUnWrapStoreID;
	sStoreId.__size = cbUnWrapStoreID;

	folderSourceKey.__size = ulFolderSourceKeySize;
	folderSourceKey.__ptr = lpFolderSourceKey;

	messageSourceKey.__size = ulMessageSourceKeySize; // can be zero
	messageSourceKey.__ptr = lpMessageSourceKey;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getEntryIDFromSourceKey(m_ecSessionId, sStoreId, folderSourceKey, messageSourceKey, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = (ECRESULT) sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sEntryId, lpcbEntryID, lppEntryID, NULL);
	if(hr != hrSuccess)
		goto exit;

exit:
	UnLockSoap();

	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT WSTransport::HrGetSyncStates(const ECLISTSYNCID &lstSyncId, ECLISTSYNCSTATE *lplstSyncState)
{
	HRESULT							hr = hrSuccess;
	ECRESULT						er = erSuccess;
	mv_long							ulaSyncId = {0};
	getSyncStatesReponse sResponse{__gszeroinit};
	ECLISTSYNCID::const_iterator	iSyncId;
	SSyncState						sSyncState = {0};

	ASSERT(lplstSyncState != NULL);

	LockSoap();

	if (lstSyncId.empty())
		goto exit;

	ulaSyncId.__ptr = new unsigned int[lstSyncId.size()];
	for (iSyncId = lstSyncId.begin(); iSyncId != lstSyncId.end(); ++iSyncId)
		ulaSyncId.__ptr[ulaSyncId.__size++] = *iSyncId;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getSyncStates(m_ecSessionId, ulaSyncId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = (ECRESULT) sResponse.er;
	}
	END_SOAP_CALL

	for (gsoap_size_t i = 0; i < sResponse.sSyncStates.__size; ++i) {
		sSyncState.ulSyncId = sResponse.sSyncStates.__ptr[i].ulSyncId;
		sSyncState.ulChangeId = sResponse.sSyncStates.__ptr[i].ulChangeId;
		lplstSyncState->push_back(sSyncState);
	}

exit:
	UnLockSoap();
	delete[] ulaSyncId.__ptr;
	return hr;
}

const char* WSTransport::GetServerName()
{
	return m_sProfileProps.strServerPath.c_str();
}

bool WSTransport::IsConnected()
{
	return m_lpCmd != NULL;
}

HRESULT WSTransport::HrOpenMultiStoreTable(LPENTRYLIST lpMsgList, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID, ECMsgStore *lpMsgStore, WSTableView **lppTableView)
{
	HRESULT hr = hrSuccess;
	WSTableMultiStore *lpMultiStoreTable = NULL;

	if (!lpMsgList || lpMsgList->cValues == 0) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = WSTableMultiStore::Create(ulFlags, m_lpCmd, &m_hDataLock, m_ecSessionId, cbEntryID, lpEntryID, lpMsgStore, this, &lpMultiStoreTable);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMultiStoreTable->HrSetEntryIDs(lpMsgList);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMultiStoreTable->QueryInterface(IID_ECTableView, (void **)lppTableView);

exit:
	if (lpMultiStoreTable)
		lpMultiStoreTable->Release();

	return hr;
}

HRESULT WSTransport::HrOpenMiscTable(ULONG ulTableType, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID, ECMsgStore *lpMsgStore, WSTableView **lppTableView)
{
	HRESULT hr = hrSuccess;
	WSTableMisc *lpMiscTable = NULL;

	if (ulTableType != TABLETYPE_STATS_SYSTEM && ulTableType != TABLETYPE_STATS_SESSIONS &&
		ulTableType != TABLETYPE_STATS_USERS && ulTableType != TABLETYPE_STATS_COMPANY  &&
		ulTableType != TABLETYPE_USERSTORES && ulTableType != TABLETYPE_STATS_SERVERS)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = WSTableMisc::Create(ulTableType, ulFlags, m_lpCmd, &m_hDataLock, m_ecSessionId, cbEntryID, lpEntryID, lpMsgStore, this, &lpMiscTable);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMiscTable->QueryInterface(IID_ECTableView, (void **)lppTableView);

exit:
	if (lpMiscTable)
		lpMiscTable->Release();

	return hr;
}

HRESULT WSTransport::HrSetLockState(ULONG cbEntryID, LPENTRYID lpEntryID, bool bLocked)
{
	HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
	entryId eidMessage;

	if ((m_ulServerCapabilities & KOPANO_CAP_MSGLOCK) == 0)
		return hrSuccess;

	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &eidMessage, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if (SOAP_OK != m_lpCmd->ns__setLockState(m_ecSessionId, eidMessage, bLocked, &er))
			er = KCERR_NETWORK_ERROR;
		/* else: er is already set and good to use */
	}
	END_SOAP_CALL

exit:
    UnLockSoap();
    
	return hr;
}

HRESULT WSTransport::HrCheckCapabilityFlags(ULONG ulFlags, BOOL *lpbResult)
{
	if (lpbResult == NULL)
		return MAPI_E_INVALID_PARAMETER;

	*lpbResult = ((m_ulServerCapabilities & ulFlags) == ulFlags) ? TRUE : FALSE;
	return hrSuccess;
}

HRESULT WSTransport::HrLicenseAuth(unsigned char *lpData, unsigned int ulSize, unsigned char **lppResponseData, unsigned int *lpulSize)
{
    HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
    struct getLicenseAuthResponse sResponse;
    struct xsd__base64Binary sData;
    
    sData.__ptr = lpData;
    sData.__size = ulSize;
    
    LockSoap();
    
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getLicenseAuth(m_ecSessionId, sData, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
        
	}
	END_SOAP_CALL
        
    hr = MAPIAllocateBuffer(sResponse.sAuthResponse.__size, (void **) lppResponseData);
    if(hr != hrSuccess)
        goto exit;
        
    memcpy(*lppResponseData, sResponse.sAuthResponse.__ptr, sResponse.sAuthResponse.__size);
    *lpulSize = sResponse.sAuthResponse.__size;
    
exit:
    UnLockSoap();
    
    return hr;
}

HRESULT WSTransport::HrLicenseCapa(unsigned int ulServiceType, char ***lppszCapas, unsigned int * lpulSize)
{
    HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
    struct getLicenseCapaResponse sResponse;
    
    char **lpszCapas = NULL;
    
    LockSoap();
    
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getLicenseCapa(m_ecSessionId, ulServiceType, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
        
	}
	END_SOAP_CALL
        
    hr = MAPIAllocateBuffer(sResponse.sCapabilities.__size * sizeof(char *), (void **)&lpszCapas);
    if(hr != hrSuccess)
        goto exit;

    for (gsoap_size_t i = 0; i < sResponse.sCapabilities.__size; ++i) {
        if ((hr = MAPIAllocateMore(strlen(sResponse.sCapabilities.__ptr[i])+1, lpszCapas, (void **) &lpszCapas[i])) != hrSuccess)
		goto exit;
        strcpy(lpszCapas[i], sResponse.sCapabilities.__ptr[i]);
    }
    
    *lppszCapas = lpszCapas;
    *lpulSize = sResponse.sCapabilities.__size;
    
exit:
    UnLockSoap();
    
    return hr;
}

HRESULT WSTransport::HrLicenseUsers(unsigned int ulServiceType, unsigned int *lpulUsers)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct getLicenseUsersResponse sResponse;

	LockSoap();

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getLicenseUsers(m_ecSessionId, ulServiceType, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpulUsers = sResponse.ulUsers;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrTestPerform(char *szCommand, unsigned int ulArgs, char **lpszArgs)
{
    HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
    struct testPerformArgs sTestPerform;
    
    sTestPerform.__size = ulArgs;
    sTestPerform.__ptr = lpszArgs;
    
    LockSoap();
    
    START_SOAP_CALL
    {
        if(SOAP_OK != m_lpCmd->ns__testPerform(m_ecSessionId, szCommand, sTestPerform, &er))
            er = KCERR_NETWORK_ERROR;
    }
    END_SOAP_CALL;

exit:
    UnLockSoap();    
    
    return hr;
}

HRESULT WSTransport::HrTestSet(const char *szName, const char *szValue)
{
    HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
    
    LockSoap();
    
    START_SOAP_CALL
    {
        if (m_lpCmd->ns__testSet(m_ecSessionId, const_cast<char *>(szName),
            const_cast<char *>(szValue), &er) != SOAP_OK)
                er = KCERR_NETWORK_ERROR;
    }
    END_SOAP_CALL
    
exit:    
    UnLockSoap();
    
    return hr;
}

HRESULT WSTransport::HrTestGet(const char *szName, char **lpszValue)
{
    HRESULT hr = hrSuccess;

    ECRESULT er = erSuccess;
    char *szValue = NULL;
    struct testGetResponse sResponse;
    
    LockSoap();
    
    START_SOAP_CALL
    {
        if (m_lpCmd->ns__testGet(m_ecSessionId,
            const_cast<char *>(szName), &sResponse) != SOAP_OK)
                er = KCERR_NETWORK_ERROR;
        else
                er = sResponse.er;
    }
    END_SOAP_CALL
    
    hr = MAPIAllocateBuffer(strlen(sResponse.szValue)+1, (void **)&szValue);
    if(hr != hrSuccess)
        goto exit;
        
    strcpy(szValue, sResponse.szValue);

    *lpszValue = szValue;
exit:    
    UnLockSoap();
    return hr;    
}

HRESULT WSTransport::HrGetSessionId(ECSESSIONID *lpSessionId, ECSESSIONGROUPID *lpSessionGroupId)
{
	HRESULT hr = hrSuccess;

	if (lpSessionId)
		*lpSessionId = m_ecSessionId;
	if (lpSessionGroupId)
		*lpSessionGroupId = m_ecSessionGroupId;

    return hr;
}

sGlobalProfileProps WSTransport::GetProfileProps()
{
    return m_sProfileProps;
}

HRESULT WSTransport::GetLicenseFlags(unsigned long long *lpllFlags)
{
    *lpllFlags = m_llFlags;
    
    return hrSuccess;
}

HRESULT WSTransport::GetServerGUID(LPGUID lpsServerGuid)
{
	if (m_sServerGuid == GUID_NULL)
		return MAPI_E_NOT_FOUND;

	*lpsServerGuid = m_sServerGuid;
	return hrSuccess;
}

HRESULT WSTransport::AddSessionReloadCallback(void *lpParam, SESSIONRELOADCALLBACK callback, ULONG *lpulId)
{
	SESSIONRELOADLIST::mapped_type data(lpParam, callback);

	pthread_mutex_lock(&m_mutexSessionReload);
	m_mapSessionReload[m_ulReloadId] = data;
	if(lpulId)
		*lpulId = m_ulReloadId;
	++m_ulReloadId;
	pthread_mutex_unlock(&m_mutexSessionReload);

	return hrSuccess;
}

HRESULT WSTransport::RemoveSessionReloadCallback(ULONG ulId)
{
	HRESULT hr = hrSuccess;
	SESSIONRELOADLIST::const_iterator iter;
	
	pthread_mutex_lock(&m_mutexSessionReload);

	if (m_mapSessionReload.erase(ulId) == 0) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

exit:
	pthread_mutex_unlock(&m_mutexSessionReload);

	return hr;
}

SOAP_SOCKET WSTransport::RefuseConnect(struct soap* soap, const char* endpoint, const char* host, int port)
{
	soap->error = SOAP_TCP_ERROR;
	return SOAP_ERR;
}

HRESULT WSTransport::HrCancelIO()
{
	HRESULT hr = hrSuccess;

	/* This looks like an ugly hack, but in practice it works very well: we want
	 * to make sure that no blocking call is running or will run in this class instance
	 * after HrCancelIO() returns.
	 *
	 * To do this, we first disable sending new requests by override the fsend() function
	 * in gSOAP. If a request was already started but still 'before' the fsend() call
	 * then the call will return directly after the fsend() because it will return an error.
	 * On the other hand, if fsend() has already been called, then the next blocking part
	 * will be the frecv() call. We therefore shutdown the socket here as well, so that
	 * any blocking recv() call will return with an error immediately.
	 *
	 * Please note though, that this last part (shutdown() on a socket will cause other threads
	 * with a blocking recv() on the socket to return with an error), is not 100% portable. 
	 * Apparently (and this is untested), some UNIX variants don't return until some data
	 * is received on the socket. Bummer. It works in Linux though.
	 *
	 * Win32 is a whole different story, we can summarize it to: this function does nothing on win32.
	 * The long story is this:
	 *		fshutdownsocket() on a named pipe (offline server) is not implemented and thus will not
	 *		work. Microsoft documentation doesn't give any hints about shutting down a named pipe
	 *		without calling fclose().
	 *		fshutdownsocket() on TCP socket does not break off any blocking TCP send/recv calls
	 *		which means the socket will only be closed _after_ the last blocking call has been
	 *		finalized.
	 *		fstop() on a named pipe will wait until all blocking send/recv calls to the server
	 *		have been finalized. It will ignore SO_LINGER/SO_DONTLINGER settings on the socket,
	 *		this means it should not be used in this function since we don't want to wait
	 *		on the send/recv calls but we want to STOP them.
	 *		fstop() on a TCP socket will work as expected and shutsdown all blocking send/recv calls.
	 */

	// Override the SOAP connect (fopen) so that all new connections will fail with a network error
	m_lpCmd->soap->fopen = WSTransport::RefuseConnect;

	// If there is a socket currently open, close it now
	if(m_lpCmd && m_lpCmd->soap) {
		int s = m_lpCmd->soap->socket;
		if (s != SOAP_INVALID_SOCKET) {
			m_lpCmd->soap->fshutdownsocket(m_lpCmd->soap, (SOAP_SOCKET)s, 2);
		}
	}

	return hr;
}

HRESULT WSTransport::HrGetNotify(struct notificationArray **lppsArrayNotifications)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;

	struct notifyResponse sNotifications;
	
	LockSoap();

	if(SOAP_OK != m_lpCmd->ns__notifyGetItems(m_ecSessionId, &sNotifications))
		er = KCERR_NETWORK_ERROR;
	else
		er = sNotifications.er;	// hrSuccess or KCWARN_KEEP_ALIVE only

	hr = kcerr_to_mapierr(er);
	if(hr != erSuccess)
		goto exit;

	if(sNotifications.pNotificationArray != NULL) {
		*lppsArrayNotifications = new notificationArray;
		CopyNotificationArrayStruct(sNotifications.pNotificationArray, *lppsArrayNotifications);
	}else
		*lppsArrayNotifications = NULL;

exit:
	UnLockSoap();

	if(m_lpCmd->soap) {
		soap_destroy(m_lpCmd->soap);
		soap_end(m_lpCmd->soap);
	}

	return hr;
}

std::string WSTransport::GetAppName()
{
	if (!m_strAppName.empty())
		return m_strAppName;
	std::string procpath = "/proc/" + stringify(getpid()) + "/cmdline";
	std::string s;

	std::ifstream in(procpath.c_str());	

	if (!getline(in, s))
		m_strAppName = "<unknown>";
	else
		m_strAppName = basename((char *)s.c_str());
    return m_strAppName;
}

/**
 * Ensure that the session is still active
 *
 * This function simply calls a random transport request, which will check the session
 * validity. Since it doesn't matter which call we run, we'll use the normally-disabled
 * testGet function. Any failed session will automatically be reloaded by the autodetection
 * code. 
 */
HRESULT WSTransport::HrEnsureSession()
{
    HRESULT hr = hrSuccess;
    char *szValue = NULL;
    
    hr = HrTestGet("ensure_transaction", &szValue);
    if(hr != MAPI_E_NETWORK_ERROR && hr != MAPI_E_END_OF_SESSION)
        hr = hrSuccess;

    MAPIFreeBuffer(szValue);
    return hr;
}

HRESULT WSTransport::HrResetFolderCount(ULONG cbEntryId, LPENTRYID lpEntryId, ULONG *lpulUpdates)
{
	HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
	entryId eidFolder;
	resetFolderCountResponse sResponse{__gszeroinit};

	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &eidFolder, true);
	if (hr != hrSuccess)
		goto exit;

	START_SOAP_CALL
	{
		if (SOAP_OK != m_lpCmd->ns__resetFolderCount(m_ecSessionId, eidFolder, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if (lpulUpdates)
		*lpulUpdates = sResponse.ulUpdates;

exit:
    UnLockSoap();
    
	return hr;
}
