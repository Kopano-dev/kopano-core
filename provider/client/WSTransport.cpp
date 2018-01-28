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
#include <kopano/lockhelper.hpp>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapitags.h>
#include <mapiutil.h>

#include <fstream>
#include <new>
#include <string>
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
#ifdef HAVE_GSSAPI
#	include <gssapi/gssapi.h>
#endif

using namespace KC;

/*
 *
 * This is the main WebServices transport object. All communications with the
 * web service server is done through this object. Also, this file is the 
 * coupling point between MAPI and our internal (network) formats, and
 * codes. This means that any classes communicating with this one either
 * use MAPI syntax (i.e. MAPI_E_NOT_ENOUGH_MEMORY) OR use the EC syntax
 * (i.e. EC_E_NOT_FOUND), but never BOTH.
 *
 */

#define START_SOAP_CALL retry: \
    if(m_lpCmd == NULL) { \
        hr = MAPI_E_NETWORK_ERROR; \
        goto exitm; \
    }
#define END_SOAP_CALL 	\
	if (er == KCERR_END_OF_SESSION && HrReLogon() == hrSuccess) \
		goto retry; \
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND); \
	if(hr != hrSuccess) \
		goto exitm;

WSTransport::WSTransport(ULONG ulUIFlags) :
	ECUnknown("WSTransport"), m_ulUIFlags(ulUIFlags),
	m_ResolveResultCache("ResolveResult", 4096, 300), m_has_session(false)
{
	memset(&m_sServerGuid, 0, sizeof(m_sServerGuid));
}

WSTransport::~WSTransport()
{
	if (m_lpCmd != NULL)
		this->HrLogOff();
}

HRESULT WSTransport::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE3(ECTransport, WSTransport, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSTransport::Create(ULONG ulUIFlags, WSTransport **lppTransport)
{
	return alloc_wrap<WSTransport>(ulUIFlags).put(lppTransport);
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

HRESULT WSTransport::LockSoap()
{
	m_hDataLock.lock();
	return erSuccess;
}

HRESULT WSTransport::UnLockSoap()
{
	//Clean up data create with soap_malloc
	if (m_lpCmd && m_lpCmd->soap) {
		soap_destroy(m_lpCmd->soap);
		soap_end(m_lpCmd->soap);
	}
	m_hDataLock.unlock();
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
	struct xsd__base64Binary sLicenseRequest;
	
	convert_context	converter;
	utf8string	strUserName = converter.convert_to<utf8string>(sProfileProps.strUserName);
	utf8string	strPassword = converter.convert_to<utf8string>(sProfileProps.strPassword);
	utf8string	strImpersonateUser = converter.convert_to<utf8string>(sProfileProps.strImpersonateUser);
	
	LockSoap();

	if (strncmp("file:", sProfileProps.strServerPath.c_str(), 5) == 0)
		bPipeConnection = true;
	else
		bPipeConnection = false;

	if (m_lpCmd != nullptr) {
		lpCmd = m_lpCmd;
	} else if (CreateSoapTransport(m_ulUIFlags, sProfileProps, &lpCmd) != hrSuccess) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	assert(!sProfileProps.strProfileName.empty());
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
	} else if (sProfileProps.ulProfileFlags & EC_PROFILE_FLAGS_NO_UID_AUTH) {
		ulLogonFlags |= KOPANO_LOGON_NO_UID_AUTH;
	}

	// try single signon logon
	er = TrySSOLogon(lpCmd, GetServerNameFromPath(sProfileProps.strServerPath.c_str()).c_str(), strUserName, strImpersonateUser, ulCapabilities, m_ecSessionGroupId, (char *)GetAppName().c_str(), &ecSessionId, &ulServerCapabilities, &m_llFlags, &m_sServerGuid, sProfileProps.strClientAppVersion, sProfileProps.strClientAppMisc);
	if (er == erSuccess)
		goto auth;
	
	// Login with username and password
	if (lpCmd->ns__logon(strUserName.c_str(), strPassword.c_str(),
	    strImpersonateUser.c_str(), PROJECT_VERSION, ulCapabilities,
	    ulLogonFlags, sLicenseRequest, m_ecSessionGroupId,
	    GetAppName().c_str(), sProfileProps.strClientAppVersion.c_str(),
	    sProfileProps.strClientAppMisc.c_str(), &sResponse) != SOAP_OK) {
		const char *d = soap_check_faultdetail(lpCmd->soap);
		ec_log_err("gsoap connect: %s", d == nullptr ? "()" : d);
		er = KCERR_SERVER_NOT_RESPONDING;
	} else {
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
	if (hr != hrSuccess && lpCmd != nullptr && lpCmd != m_lpCmd)
	    // UGLY FIX: due to the ugly code above that does lpCmd = m_lpCmd
	    // we need to check that we're not deleting our m_lpCmd. We also cannot
	    // set m_lpCmd to NULL since various functions in WSTransport rely on the
	    // fact that m_lpCmd is good after a successful HrLogon() call.
		DestroySoapTransport(lpCmd);
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
	object_ptr<WSTransport> lpTransport;
	sGlobalProfileProps	sProfileProps = m_sProfileProps;

	if (lppTransport == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	hr = WSTransport::Create(m_ulUIFlags, &~lpTransport);
	if (hr != hrSuccess)
		return hr;
	sProfileProps.strServerPath = szServer;

	hr = lpTransport->HrLogon(sProfileProps);
	if (hr != hrSuccess)
		return hr;
	*lppTransport = lpTransport.release();
	return hrSuccess;
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
	object_ptr<WSTransport> lpTransport;

	if (lppTransport == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	hr = WSTransport::Create(m_ulUIFlags, &~lpTransport);
	if (hr != hrSuccess)
		return hr;
	hr = lpTransport->HrLogon(m_sProfileProps);
	if (hr != hrSuccess)
		return hr;
	*lppTransport = lpTransport.release();
	return hrSuccess;
}

HRESULT WSTransport::HrReLogon()
{
	HRESULT hr = HrLogon(m_sProfileProps);
	if(hr != hrSuccess)
		return hr;

	// Notify new session to listeners
	scoped_rlock lock(m_mutexSessionReload);
	for (const auto &p : m_mapSessionReload)
		p.second.second(p.second.first, this->m_ecSessionId);
	return hrSuccess;
}

ECRESULT WSTransport::TrySSOLogon(KCmd* lpCmd, const char *szServer,
    const utf8string &strUsername, const utf8string &strImpersonateUser,
    unsigned int ulCapabilities, ECSESSIONGROUPID ecSessionGroupId,
    const char *szAppName, ECSESSIONID *lpSessionId,
    unsigned int *lpulServerCapabilities, unsigned long long *lpllFlags,
    GUID *lpsServerGuid, const std::string &appVersion,
    const std::string &appMisc)
{
#define KOPANO_GSS_SERVICE "kopano"
	ECRESULT er = KCERR_LOGON_FAILED;
#ifdef HAVE_GSSAPI
	OM_uint32 minor, major;
	gss_buffer_desc pr_buf;
	gss_name_t principal = GSS_C_NO_NAME;
	gss_ctx_id_t gss_ctx = GSS_C_NO_CONTEXT;
	gss_OID_desc mech_spnego = {6, const_cast<char *>("\053\006\001\005\005\002")};
	gss_buffer_desc secbufout, secbufin;
	struct xsd__base64Binary sso_data, licreq;
	struct ssoLogonResponse resp;

	pr_buf.value = const_cast<char *>(KOPANO_GSS_SERVICE);
	pr_buf.length = sizeof(KOPANO_GSS_SERVICE) - 1;
	/* GSSAPI automagically appends @server */
	major = gss_import_name(&minor, &pr_buf,
	        GSS_C_NT_HOSTBASED_SERVICE, &principal);
	if (GSS_ERROR(major))
		goto exit;

	resp.ulSessionId = 0;
	do {
		major = gss_init_sec_context(&minor, GSS_C_NO_CREDENTIAL,
		        &gss_ctx, principal, &mech_spnego, GSS_C_CONF_FLAG,
		        GSS_C_INDEFINITE, GSS_C_NO_CHANNEL_BINDINGS,
		        resp.ulSessionId == 0 ? GSS_C_NO_BUFFER : &secbufin,
		        nullptr, &secbufout, nullptr, nullptr);
		if (GSS_ERROR(major))
			goto exit;

		/* Send GSS state to kopano-server */
		sso_data.__ptr = reinterpret_cast<unsigned char *>(secbufout.value);
		sso_data.__size = secbufout.length;

		if (lpCmd->ns__ssoLogon(resp.ulSessionId, strUsername.c_str(),
		    strImpersonateUser.c_str(), &sso_data, PROJECT_VERSION,
		    ulCapabilities, licreq, ecSessionGroupId, szAppName,
		    appVersion.c_str(), appMisc.c_str(), &resp) != SOAP_OK)
			goto exit;
		if (resp.er != KCERR_SSO_CONTINUE)
			break;

		secbufin.value = static_cast<void *>(resp.lpOutput->__ptr);
		secbufin.length = resp.lpOutput->__size;
		gss_release_buffer(&minor, &secbufout);
		/* Return kopano-server response to GSS */
	} while (true);
	er = resp.er;
	if (er != erSuccess)
		goto exit;
	*lpSessionId = resp.ulSessionId;
	*lpulServerCapabilities = resp.ulCapabilities;
	if (resp.sServerGuid.__ptr != nullptr &&
	    resp.sServerGuid.__size == sizeof(*lpsServerGuid))
		memcpy(lpsServerGuid, resp.sServerGuid.__ptr, sizeof(*lpsServerGuid));
 exit:
	gss_release_buffer(&minor, &secbufout);
	gss_delete_sec_context(&minor, &gss_ctx, nullptr);
	gss_release_name(&minor, &principal);
#endif
	return er;
#undef KOPANO_GSS_SERVICE
}

HRESULT WSTransport::HrGetPublicStore(ULONG ulFlags, ULONG *lpcbStoreID,
    ENTRYID **lppStoreID, std::string *lpstrRedirServer)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct getStoreResponse sResponse;

	LockSoap();

	if ((ulFlags & ~EC_OVERRIDE_HOMESERVER) != 0) {
		hr = MAPI_E_UNKNOWN_FLAGS;
		goto exitm;
	}

	if(lppStoreID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getPublicStore(m_ecSessionId, ulFlags, &sResponse))
			er = KCERR_SERVER_NOT_RESPONDING;
		else
			er = sResponse.er;
	}
	//END_SOAP_CALL
	if (er == KCERR_END_OF_SESSION && HrReLogon() == hrSuccess)
		goto retry;
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND);
	if (hr == MAPI_E_UNABLE_TO_COMPLETE)
	{
		if (lpstrRedirServer)
			*lpstrRedirServer = sResponse.lpszServerPath;
		else
			hr = MAPI_E_NOT_FOUND;
	}
	if(hr != hrSuccess)
		goto exitm;

	// Create a client store entry, add the servername
	hr = WrapServerClientStoreEntry(sResponse.lpszServerPath ? sResponse.lpszServerPath : m_sProfileProps.strServerPath.c_str(), &sResponse.sStoreId, lpcbStoreID, lppStoreID);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetStore(ULONG cbMasterID, ENTRYID *lpMasterID,
    ULONG *lpcbStoreID, ENTRYID **lppStoreID, ULONG *lpcbRootID,
    ENTRYID **lppRootID, std::string *lpstrRedirServer)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sEntryId; // Do not free
	struct getStoreResponse sResponse;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	if(lpMasterID) {
		hr = UnWrapServerClientStoreEntry(cbMasterID, lpMasterID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
		if(hr != hrSuccess)
			goto exitm;
		sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
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
	if (er == KCERR_END_OF_SESSION && HrReLogon() == hrSuccess)
		goto retry;
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND);
	if (hr == MAPI_E_UNABLE_TO_COMPLETE)
	{
		if (lpstrRedirServer)
			*lpstrRedirServer = sResponse.lpszServerPath;
		else
			hr = MAPI_E_NOT_FOUND;
	}
	if(hr != hrSuccess)
		goto exitm;

	if(lppRootID && lpcbRootID) {
		hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sRootId, lpcbRootID, lppRootID);
		if(hr != hrSuccess)
			goto exitm;
	}

	if(lppStoreID && lpcbStoreID) {
		// Create a client store entry, add the servername
		hr = WrapServerClientStoreEntry(sResponse.lpszServerPath ? sResponse.lpszServerPath : m_sProfileProps.strServerPath.c_str(), &sResponse.sStoreId, lpcbStoreID, lppStoreID);
		if(hr != hrSuccess)
			goto exitm;
	}

 exitm:
	UnLockSoap();
	return hr;
}

HRESULT WSTransport::HrGetStoreName(ULONG cbStoreID, LPENTRYID lpStoreID, ULONG ulFlags, LPTSTR *lppszStoreName)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId		sEntryId; // Do not free
	struct getStoreNameResponse sResponse;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	if(lpStoreID == NULL || lppszStoreName == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	// Remove the servername
	hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
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
 exitm:
	UnLockSoap();
	return hr;
}

HRESULT WSTransport::HrGetStoreType(ULONG cbStoreID, LPENTRYID lpStoreID, ULONG *lpulStoreType)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId		sEntryId; // Do not free
	struct getStoreTypeResponse sResponse;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	if(lpStoreID == NULL || lpulStoreType == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	// Remove the servername
	hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
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
 exitm:
	UnLockSoap();
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
 exitm:
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
 exitm:
	UnLockSoap();
	return er;
}

HRESULT WSTransport::HrCheckExistObject(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulFlags)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId sEntryId; // Do not free
	LockSoap();

	if(cbEntryID == 0 || lpEntryID == NULL) {
		hr = MAPI_E_INVALID_ENTRYID;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__checkExistObject(m_ecSessionId, sEntryId, ulFlags, &er))
			er = KCERR_SERVER_NOT_RESPONDING;

	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrOpenPropStorage(ULONG cbParentEntryID,
    const ENTRYID *lpParentEntryID, ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulFlags, IECPropStorage **lppPropStorage)
{
	HRESULT hr = hrSuccess;
	object_ptr<WSMAPIPropStorage> lpPropStorage;
	ecmem_ptr<ENTRYID> lpUnWrapParentID, lpUnWrapEntryID;
	ULONG		cbUnWrapParentID = 0;
	ULONG		cbUnWrapEntryID = 0;

	if (lpParentEntryID) {
		hr = UnWrapServerClientStoreEntry(cbParentEntryID, lpParentEntryID, &cbUnWrapParentID, &~lpUnWrapParentID);
		if(hr != hrSuccess)
			return hr;
	}
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapEntryID, &~lpUnWrapEntryID);
	if(hr != hrSuccess)
		return hr;
	hr = WSMAPIPropStorage::Create(cbUnWrapParentID, lpUnWrapParentID,
	     cbUnWrapEntryID, lpUnWrapEntryID, ulFlags, m_lpCmd, m_hDataLock,
	     m_ecSessionId, this->m_ulServerCapabilities, this, &~lpPropStorage);
	if(hr != hrSuccess)
		return hr;
	return lpPropStorage->QueryInterface(IID_IECPropStorage, (void **)lppPropStorage);
}

HRESULT WSTransport::HrOpenParentStorage(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage, IECPropStorage **lppPropStorage)
{
	HRESULT hr = hrSuccess;
	object_ptr<ECParentStorage> lpPropStorage;

	hr = ECParentStorage::Create(lpParentObject, ulUniqueId, ulObjId, lpServerStorage, &~lpPropStorage);
	if(hr != hrSuccess)
		return hr;
	return lpPropStorage->QueryInterface(IID_IECPropStorage,
	       reinterpret_cast<void **>(lppPropStorage));
}

HRESULT WSTransport::HrOpenABPropStorage(ULONG cbEntryID,
    const ENTRYID *lpEntryID, IECPropStorage **lppPropStorage)
{
	HRESULT			hr = hrSuccess;
	object_ptr<WSABPropStorage> lpPropStorage;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	hr = UnWrapServerClientABEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		return hr;
	hr = WSABPropStorage::Create(cbUnWrapStoreID, lpUnWrapStoreID, m_lpCmd,
	     m_hDataLock, m_ecSessionId, this, &~lpPropStorage);
	if(hr != hrSuccess)
		return hr;
	return lpPropStorage->QueryInterface(IID_IECPropStorage,
	       reinterpret_cast<void **>(lppPropStorage));
}

HRESULT WSTransport::HrOpenFolderOps(ULONG cbEntryID, const ENTRYID *lpEntryID,
    WSMAPIFolderOps **lppFolderOps)
{
	HRESULT hr = hrSuccess;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

//FIXME: create this function
//	hr = CheckEntryIDType(cbEntryID, lpEntryID, MAPI_FOLDER);
//	if( hr != hrSuccess)
		//return hr;
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		return hr;
	return WSMAPIFolderOps::Create(m_lpCmd, m_hDataLock, m_ecSessionId,
	       cbUnWrapStoreID, lpUnWrapStoreID, this, lppFolderOps);

}

HRESULT WSTransport::HrOpenTableOps(ULONG ulType, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID, ECMsgStore *lpMsgStore, WSTableView **lppTableOps)
{
	/*
	FIXME: Do a check ?
	if (peid->ulType != MAPI_FOLDER && peid->ulType != MAPI_MESSAGE)
		return MAPI_E_INVALID_ENTRYID;
	*/
	return WSStoreTableView::Create(ulType, ulFlags, m_lpCmd, m_hDataLock,
	       m_ecSessionId, cbEntryID, lpEntryID, lpMsgStore, this,
	       lppTableOps);
}

HRESULT WSTransport::HrOpenABTableOps(ULONG ulType, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID, ECABLogon* lpABLogon, WSTableView **lppTableOps)
{
	/*if (peid->ulType != MAPI_FOLDER && peid->ulType != MAPI_MESSAGE)
		return MAPI_E_INVALID_ENTRYID;
	*/
	return WSABTableView::Create(ulType, ulFlags, m_lpCmd, m_hDataLock,
	       m_ecSessionId, cbEntryID, lpEntryID, lpABLogon, this,
	       lppTableOps);
}

HRESULT WSTransport::HrOpenMailBoxTableOps(ULONG ulFlags, ECMsgStore *lpMsgStore, WSTableView **lppTableView)
{
	HRESULT hr = hrSuccess;
	object_ptr<WSTableMailBox> lpWSTable;
	
	hr = WSTableMailBox::Create(ulFlags, m_lpCmd, m_hDataLock,
	     m_ecSessionId, lpMsgStore, this, &~lpWSTable);
	if(hr != hrSuccess)
		return hr;
	return lpWSTable->QueryInterface(IID_ECTableView,
	       reinterpret_cast<void **>(lppTableView));
}

HRESULT WSTransport::HrOpenTableOutGoingQueueOps(ULONG cbStoreEntryID, LPENTRYID lpStoreEntryID, ECMsgStore *lpMsgStore, WSTableOutGoingQueue **lppTableOutGoingQueueOps)
{
	HRESULT hr = hrSuccess;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	// lpStoreEntryID == null for master queue
	if(lpStoreEntryID) {
		hr = UnWrapServerClientStoreEntry(cbStoreEntryID, lpStoreEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
		if(hr != hrSuccess)
			return hr;
	}
	return WSTableOutGoingQueue::Create(m_lpCmd, m_hDataLock, m_ecSessionId,
	       cbUnWrapStoreID, lpUnWrapStoreID, lpMsgStore, this,
	       lppTableOutGoingQueueOps);
}

HRESULT WSTransport::HrDeleteObjects(ULONG ulFlags, LPENTRYLIST lpMsgList, ULONG ulSyncId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct entryList sEntryList;

	LockSoap();
	if(lpMsgList->cValues == 0)
		goto exitm;
	hr = CopyMAPIEntryListToSOAPEntryList(lpMsgList, &sEntryList);
	if(hr != hrSuccess)
		goto exitm;
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__deleteObjects(m_ecSessionId, ulFlags, &sEntryList, ulSyncId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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

	LockSoap();

	//FIMXE: also notify other types ?
	if(lpNotification == NULL || lpNotification->ulEventType != fnevNewMail)
	{
		hr = MAPI_E_NO_ACCESS;
		goto exitm;
	}

	sNotification.ulConnection = 0;// The connection id should be calculate on the server side

	sNotification.ulEventType = lpNotification->ulEventType;
	sNotification.newmail = s_alloc<notificationNewMail>(nullptr);
	memset(sNotification.newmail, 0, sizeof(notificationNewMail));

	hr = CopyMAPIEntryIdToSOAPEntryId(lpNotification->info.newmail.cbEntryID, (LPENTRYID)lpNotification->info.newmail.lpEntryID, &sNotification.newmail->pEntryId);
	if(hr != hrSuccess)
		goto exitm;

	hr = CopyMAPIEntryIdToSOAPEntryId(lpNotification->info.newmail.cbParentID, (LPENTRYID)lpNotification->info.newmail.lpParentID, &sNotification.newmail->pParentId);
	if(hr != hrSuccess)
		goto exitm;
	
	if(lpNotification->info.newmail.lpszMessageClass){
		utf8string strMessageClass = convstring(lpNotification->info.newmail.lpszMessageClass, lpNotification->info.newmail.ulFlags);
		ulSize = strMessageClass.size() + 1;
		sNotification.newmail->lpszMessageClass = s_alloc<char>(nullptr, ulSize);
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
 exitm:
	UnLockSoap();

	FreeNotificationStruct(&sNotification, false);

	return hr;
}

HRESULT WSTransport::HrSubscribe(ULONG cbKey, LPBYTE lpKey, ULONG ulConnection, ULONG ulEventMask)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	notifySubscribe notSubscribe;

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
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrSubscribe(ULONG ulSyncId, ULONG ulChangeId, ULONG ulConnection, ULONG ulEventMask)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	notifySubscribe notSubscribe;

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
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrSubscribeMulti(const ECLISTSYNCADVISE &lstSyncAdvises, ULONG ulEventMask)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	notifySubscribeArray notSubscribeArray;
	unsigned	i = 0;
	
	LockSoap();

	notSubscribeArray.__size = lstSyncAdvises.size();
	hr = MAPIAllocateBuffer(notSubscribeArray.__size * sizeof *notSubscribeArray.__ptr, (void**)&notSubscribeArray.__ptr);
	if (hr != hrSuccess)
		goto exitm;
	memset(notSubscribeArray.__ptr, 0, notSubscribeArray.__size * sizeof *notSubscribeArray.__ptr);
	
	for (const auto &adv : lstSyncAdvises) {
		notSubscribeArray.__ptr[i].ulConnection = adv.ulConnection;
		notSubscribeArray.__ptr[i].sSyncState.ulSyncId = adv.sSyncState.ulSyncId;
		notSubscribeArray.__ptr[i].sSyncState.ulChangeId = adv.sSyncState.ulChangeId;
		notSubscribeArray.__ptr[i].ulEventMask = ulEventMask;
		++i;
	}

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__notifySubscribeMulti(m_ecSessionId, &notSubscribeArray, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL
 exitm:
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
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrUnSubscribeMulti(const ECLISTCONNECTION &lstConnections)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	mv_long ulConnArray;
	unsigned i = 0;

	ulConnArray.__size = lstConnections.size();
	ulConnArray.__ptr = s_alloc<unsigned int>(nullptr, ulConnArray.__size);
	LockSoap();
	for (const auto &p : lstConnections)
		ulConnArray.__ptr[i++] = p.second;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__notifyUnSubscribeMulti(m_ecSessionId, &ulConnArray, &er) )
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();
	s_free(nullptr, ulConnArray.__ptr);
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
HRESULT WSTransport::HrExportMessageChangesAsStream(ULONG ulFlags,
    ULONG ulPropTag, const ICSCHANGE *lpChanges, ULONG ulStart,
    ULONG ulChanges, const SPropTagArray *lpsProps,
    WSMessageStreamExporter **lppsStreamExporter)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	memory_ptr<sourceKeyPairArray> ptrsSourceKeyPairs;
	WSMessageStreamExporterPtr ptrStreamExporter;
	propTagArray sPropTags = {0, 0};
	exportMessageChangesAsStreamResponse sResponse;

	if (lpChanges == NULL || lpsProps == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	if ((m_ulServerCapabilities & KOPANO_CAP_ENHANCED_ICS) == 0) {
		hr = MAPI_E_NO_SUPPORT;
		goto exitm;
	}

	hr = CopyICSChangeToSOAPSourceKeys(ulChanges, lpChanges + ulStart, &~ptrsSourceKeyPairs);
	if (hr != hrSuccess)
		goto exitm;
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
		goto exitm;
	}
	hr = WSMessageStreamExporter::Create(ulStart, ulChanges, sResponse.sMsgStreams, this, &~ptrStreamExporter);
	if (hr != hrSuccess)
		goto exitm;
	*lppsStreamExporter = ptrStreamExporter.release();
 exitm:
	return hr;
}

HRESULT WSTransport::HrGetMessageStreamImporter(ULONG ulFlags, ULONG ulSyncId, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG cbFolderEntryID, LPENTRYID lpFolderEntryID, bool bNewMessage, LPSPropValue lpConflictItems, WSMessageStreamImporter **lppStreamImporter)
{
	HRESULT hr;
	WSMessageStreamImporterPtr ptrStreamImporter;

	if ((m_ulServerCapabilities & KOPANO_CAP_ENHANCED_ICS) == 0)
		return MAPI_E_NO_SUPPORT;
	hr = WSMessageStreamImporter::Create(ulFlags, ulSyncId, cbEntryID, lpEntryID, cbFolderEntryID, lpFolderEntryID, bNewMessage, lpConflictItems, this, &~ptrStreamImporter);
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
	er = ECAllocateBuffer(sizeof(struct namedProp) * cNames, reinterpret_cast<void **>(&sNamedProps.__ptr));
	if (er != erSuccess)
		goto exitm;
	memset(sNamedProps.__ptr, 0 , sizeof(struct namedProp) * cNames);

	for (i = 0; i < cNames; ++i) {	
		switch(lppPropNames[i]->ulKind) {
		case MNID_ID:
			er = ECAllocateMore(sizeof(unsigned int), sNamedProps.__ptr, reinterpret_cast<void **>(&sNamedProps.__ptr[i].lpId));
			if (er != erSuccess)
				goto exitm;
			*sNamedProps.__ptr[i].lpId = lppPropNames[i]->Kind.lID;
			break;
		case MNID_STRING: {
			// The string is actually UTF-8, not windows-1252. This enables full support for wide char strings.
			utf8string strNameUTF8 = convertContext.convert_to<utf8string>(lppPropNames[i]->Kind.lpwstrName);

			er = ECAllocateMore(strNameUTF8.length() + 1, sNamedProps.__ptr, reinterpret_cast<void **>(&sNamedProps.__ptr[i].lpString));
			if (er != erSuccess)
				goto exitm;
			strcpy(sNamedProps.__ptr[i].lpString, strNameUTF8.c_str());
			break;
		}
		default:
			hr = MAPI_E_INVALID_PARAMETER;
			goto exitm;
		}

		if(lppPropNames[i]->lpguid) {
			er = ECAllocateMore(sizeof( xsd__base64Binary) , sNamedProps.__ptr, reinterpret_cast<void **>(&sNamedProps.__ptr[i].lpguid));
			if (er != erSuccess)
				goto exitm;
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
		goto exitm;
	}

	hr = ECAllocateBuffer(sizeof(ULONG) * sResponse.lpsPropTags.__size, reinterpret_cast<void **>(lpServerIDs));
	if (hr != hrSuccess)
		goto exitm;
	memcpy(*lpServerIDs, sResponse.lpsPropTags.__ptr, sizeof(ULONG) * sResponse.lpsPropTags.__size);
 exitm:
	UnLockSoap();

	if(sNamedProps.__ptr)
		ECFreeBuffer(sNamedProps.__ptr);

	return hr;
}

HRESULT WSTransport::HrGetNamesFromIDs(SPropTagArray *lpsPropTags,
    MAPINAMEID ***lpppNames, ULONG *lpcResolved)
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

	er = ECAllocateBuffer(sizeof(LPMAPINAMEID) * sResponse.lpsNames.__size, reinterpret_cast<void **>(&lppNames));
	if (er != erSuccess)
		goto exitm;

	// Loop through all the returned names, and put it into the return value
	for (gsoap_size_t i = 0; i < sResponse.lpsNames.__size; ++i) {
		// Each MAPINAMEID must be allocated
		er = ECAllocateMore(sizeof(MAPINAMEID), lppNames, reinterpret_cast<void **>(&lppNames[i]));
		if (er != erSuccess)
			goto exitm;
		if(sResponse.lpsNames.__ptr[i].lpguid && sResponse.lpsNames.__ptr[i].lpguid->__ptr) {
			er = ECAllocateMore(sizeof(GUID), lppNames, reinterpret_cast<void **>(&lppNames[i]->lpguid));
			if (er != erSuccess)
				goto exitm;
			memcpy(lppNames[i]->lpguid, sResponse.lpsNames.__ptr[i].lpguid->__ptr, sizeof(GUID));
		}
		if(sResponse.lpsNames.__ptr[i].lpId) {
			lppNames[i]->Kind.lID = *sResponse.lpsNames.__ptr[i].lpId;
			lppNames[i]->ulKind = MNID_ID;
		} else if(sResponse.lpsNames.__ptr[i].lpString) {
			std::wstring strNameW = convertContext.convert_to<std::wstring>(sResponse.lpsNames.__ptr[i].lpString, rawsize(sResponse.lpsNames.__ptr[i].lpString), "UTF-8");

			er = ECAllocateMore((strNameW.size() + 1) * sizeof(wchar_t), lppNames,
			     reinterpret_cast<void **>(&lppNames[i]->Kind.lpwstrName));
			if (er != erSuccess)
				goto exitm;
			memcpy(lppNames[i]->Kind.lpwstrName, strNameW.c_str(), (strNameW.size() + 1) * sizeof(WCHAR));	// Also copy the trailing '\0'
			lppNames[i]->ulKind = MNID_STRING;
		} else {
			// not found by server, we have actually allocated memory but it doesn't really matter
			lppNames[i] = NULL;
		}
	}

	*lpcResolved = sResponse.lpsNames.__size;
	*lpppNames = lppNames;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetReceiveFolderTable(ULONG ulFlags,
    ULONG cbStoreEntryID, const ENTRYID *lpStoreEntryID, SRowSet **lppsRowSet)
{
	struct receiveFolderTableResponse sReceiveFolders;
	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;
	LPSRowSet	lpsRowSet = NULL;
	ULONG		ulRowId = 0;
	int			nLen = 0;
	entryId sEntryId; // Do not free
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;
	std::wstring unicode;
	convert_context converter;

	LockSoap();

	hr = UnWrapServerClientStoreEntry(cbStoreEntryID, lpStoreEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
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

	er = ECAllocateBuffer(CbNewSRowSet(sReceiveFolders.sFolderArray.__size), reinterpret_cast<void **>(&lpsRowSet));
	if (er != erSuccess)
		goto exitm;
	lpsRowSet->cRows = 0;
	for (gsoap_size_t i = 0; i < sReceiveFolders.sFolderArray.__size; ++i) {
		ulRowId = i+1;

		lpsRowSet->aRow[i].cValues = NUM_RFT_PROPS;
		er = ECAllocateBuffer(sizeof(SPropValue) * NUM_RFT_PROPS, reinterpret_cast<void **>(&lpsRowSet->aRow[i].lpProps));
		if (er != erSuccess)
			goto exitm;
		++lpsRowSet->cRows;
		memset(lpsRowSet->aRow[i].lpProps, 0, sizeof(SPropValue)*NUM_RFT_PROPS);
		
		lpsRowSet->aRow[i].lpProps[RFT_ROWID].ulPropTag = PR_ROWID;
		lpsRowSet->aRow[i].lpProps[RFT_ROWID].Value.ul = ulRowId;

		lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].ulPropTag = PR_INSTANCE_KEY;
		lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.cb = 4; //fixme: maybe fix, normal 8 now
		er = ECAllocateMore(lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.cb, lpsRowSet->aRow[i].lpProps,
		     reinterpret_cast<void **>(&lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.lpb));
		if (er != erSuccess)
			goto exitm;
		memset(lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.lpb, 0, lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.cb);
		memcpy(lpsRowSet->aRow[i].lpProps[RFT_INST_KEY].Value.bin.lpb, &ulRowId, sizeof(ulRowId));
		
		lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].ulPropTag = PR_ENTRYID;
		lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.cb = sReceiveFolders.sFolderArray.__ptr[i].sEntryId.__size;
		er = ECAllocateMore(lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.cb, lpsRowSet->aRow[i].lpProps,
		     reinterpret_cast<void **>(&lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.lpb));
		if (er != erSuccess)
			goto exitm;
		memcpy(lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.lpb, sReceiveFolders.sFolderArray.__ptr[i].sEntryId.__ptr, lpsRowSet->aRow[i].lpProps[RFT_ENTRYID].Value.bin.cb);

		// Use the entryid for record key
		lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].ulPropTag = PR_RECORD_KEY;
		lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.cb = sReceiveFolders.sFolderArray.__ptr[i].sEntryId.__size;
		er = ECAllocateMore(lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.cb, lpsRowSet->aRow[i].lpProps,
		     reinterpret_cast<void **>(&lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.lpb));
		if (er != erSuccess)
			goto exitm;
		memcpy(lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.lpb, sReceiveFolders.sFolderArray.__ptr[i].sEntryId.__ptr, lpsRowSet->aRow[i].lpProps[RFT_RECORD_KEY].Value.bin.cb);

		if (ulFlags & MAPI_UNICODE) {
			lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].ulPropTag = PR_MESSAGE_CLASS_W;
			unicode = converter.convert_to<std::wstring>(sReceiveFolders.sFolderArray.__ptr[i].lpszAExplicitClass);
			er = ECAllocateMore((unicode.length() + 1) * sizeof(wchar_t), lpsRowSet->aRow[i].lpProps, reinterpret_cast<void **>(&lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].Value.lpszW));
			if (er != erSuccess)
				goto exitm;
			memcpy(lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].Value.lpszW, unicode.c_str(), (unicode.length()+1)*sizeof(WCHAR));
		} else {
			lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].ulPropTag = PR_MESSAGE_CLASS_A;
			nLen = strlen(sReceiveFolders.sFolderArray.__ptr[i].lpszAExplicitClass)+1;
			er = ECAllocateMore(nLen, lpsRowSet->aRow[i].lpProps, reinterpret_cast<void **>(&lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].Value.lpszA));
			if (er != erSuccess)
				goto exitm;
			memcpy(lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].Value.lpszA, sReceiveFolders.sFolderArray.__ptr[i].lpszAExplicitClass, nLen);
		}	
	}

	*lppsRowSet = lpsRowSet;
 exitm:
	UnLockSoap();
	return hr;
}

HRESULT WSTransport::HrGetReceiveFolder(ULONG cbStoreEntryID,
    const ENTRYID *lpStoreEntryID, const utf8string &strMessageClass,
    ULONG *lpcbEntryID, ENTRYID **lppEntryID, utf8string *lpstrExplicitClass)
{
	struct receiveFolderResponse sReceiveFolderTable;

	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;
	entryId sEntryId; // Do not free
	ULONG		cbEntryID = 0;
	ecmem_ptr<ENTRYID> lpEntryID, lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	hr = UnWrapServerClientStoreEntry(cbStoreEntryID, lpStoreEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sEntryId.__size = cbUnWrapStoreID;

	if(lpstrExplicitClass)
		lpstrExplicitClass->clear();

	// Get ReceiveFolder information from the server
	START_SOAP_CALL
	{
		if (m_lpCmd->ns__getReceiveFolder(m_ecSessionId, sEntryId,
		    strMessageClass.c_str(), &sReceiveFolderTable) != SOAP_OK)
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
		goto exitm;
	}
	hr = CopySOAPEntryIdToMAPIEntryId(&sReceiveFolderTable.sReceiveFolder.sEntryId,
	     &cbEntryID, &~lpEntryID, nullptr);
	if(hr != hrSuccess)
		goto exitm;
	if(er != KCERR_NOT_FOUND && lpstrExplicitClass != NULL)
		*lpstrExplicitClass = utf8string::from_string(sReceiveFolderTable.sReceiveFolder.lpszAExplicitClass);

	*lppEntryID = lpEntryID.release();
	*lpcbEntryID = cbEntryID;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrSetReceiveFolder(ULONG cbStoreID,
    const ENTRYID *lpStoreID, const utf8string &strMessageClass,
    ULONG cbEntryID, const ENTRYID *lpEntryID)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	unsigned int result;
	entryId sStoreId, sEntryId; // Do not free
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sStoreId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sStoreId.__size = cbUnWrapStoreID;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if (hr != hrSuccess)
		goto exitm;
	START_SOAP_CALL
	{
		if (m_lpCmd->ns__setReceiveFolder(m_ecSessionId, sStoreId,
		    lpEntryID != nullptr ? &sEntryId : nullptr,
		    strMessageClass.c_str(), &result) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = result;
	}
	END_SOAP_CALL
 exitm:
    UnLockSoap();
	return hr;
}

HRESULT WSTransport::HrSetReadFlag(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, ULONG ulSyncId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;

	struct entryList sEntryList;
	entryId sEntryId;

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
 exitm:
	UnLockSoap();

	return hr;

}

HRESULT WSTransport::HrSubmitMessage(ULONG cbMessageID, LPENTRYID lpMessageID, ULONG ulFlags)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId sEntryId; // Do not free
	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbMessageID, lpMessageID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__submitMessage(m_ecSessionId, sEntryId, ulFlags, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrFinishedMessage(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	entryId sEntryId; // Do not free
	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exitm;
	
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__finishedMessage(m_ecSessionId, sEntryId, ulFlags, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAbortSubmit(ULONG cbEntryID, const ENTRYID *lpEntryID)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	entryId sEntryId; // Do not free
	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__abortSubmit(m_ecSessionId, sEntryId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrResolveStore(const GUID *lpGuid, ULONG *lpulUserID,
    ULONG *lpcbStoreID, ENTRYID **lppStoreID)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct resolveUserStoreResponse sResponse;
	struct xsd__base64Binary sStoreGuid;
	LockSoap();

	if (!lpGuid){
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
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
			goto exitm;
	}
 exitm:
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
		goto exitm;
	}

	START_SOAP_CALL
	{
		if (m_lpCmd->ns__resolveUserStore(m_ecSessionId, strUserName.c_str(),
		    ECSTORE_TYPE_MASK_PRIVATE | ECSTORE_TYPE_MASK_PUBLIC,
		    ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	//END_SOAP_CALL
	if (er == KCERR_END_OF_SESSION && HrReLogon() == hrSuccess)
		goto retry;
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND);
	if (hr == MAPI_E_UNABLE_TO_COMPLETE)
	{
		if (lpstrRedirServer)
			*lpstrRedirServer = sResponse.lpszServerPath;
		else
			hr = MAPI_E_NOT_FOUND;
	}
	if(hr != hrSuccess)
		goto exitm;
	if (lpulUserID != nullptr)
		*lpulUserID = sResponse.ulUserId;
	if(lpcbStoreID && lppStoreID) {

		// Create a client store entry, add the servername
		hr = WrapServerClientStoreEntry(sResponse.lpszServerPath ? sResponse.lpszServerPath : m_sProfileProps.strServerPath.c_str(), &sResponse.sStoreId, lpcbStoreID, lppStoreID);
		if(hr != hrSuccess)
			goto exitm;
	}
 exitm:
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
		goto exitm;
	}

	START_SOAP_CALL
	{
		if (m_lpCmd->ns__resolveUserStore(m_ecSessionId,
		    strUserName.c_str(), 1 << ulStoreType, 0,
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if(lpcbStoreID && lppStoreID) {
		// Create a client store entry, add the servername
		hr = WrapServerClientStoreEntry(sResponse.lpszServerPath ? sResponse.lpszServerPath : m_sProfileProps.strServerPath.c_str(), &sResponse.sStoreId, lpcbStoreID, lppStoreID);
		if(hr != hrSuccess)
			goto exitm;
	}
 exitm:
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
	struct user sUser;
	struct setUserResponse sResponse;
	convert_context converter;

	LockSoap();

	if(lpECUser == NULL || lpcbUserId == NULL || lppUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	sUser.lpszUsername		= TO_UTF8_DEF((char *)lpECUser->lpszUsername);
	sUser.lpszPassword		= TO_UTF8_DEF((char *)lpECUser->lpszPassword);
	sUser.lpszMailAddress	= TO_UTF8_DEF((char *)lpECUser->lpszMailAddress);
	sUser.ulUserId			= 0;
	sUser.ulObjClass		= lpECUser->ulObjClass;
	sUser.ulIsAdmin			= lpECUser->ulIsAdmin;
	sUser.lpszFullName		= TO_UTF8_DEF((char *)lpECUser->lpszFullName);
	sUser.ulIsABHidden		= lpECUser->ulIsABHidden;
	sUser.ulCapacity		= lpECUser->ulCapacity;
	sUser.lpsPropmap		= NULL;
	sUser.lpsMVPropmap		= NULL;

	hr = CopyABPropsToSoap(&lpECUser->sPropmap, &lpECUser->sMVPropmap, ulFlags,
						   &sUser.lpsPropmap, &sUser.lpsMVPropmap);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__createUser(m_ecSessionId, &sUser, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sUserId, sResponse.ulUserId, lpcbUserId, lppUserId);
 exitm:
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
HRESULT WSTransport::HrGetUser(ULONG cbUserID, const ENTRYID *lpUserID,
    ULONG ulFlags, ECUSER **lppECUser)
{
	HRESULT	hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct getUserResponse	sResponse;
	ecmem_ptr<ECUSER> lpECUser;
	entryId	sUserId;
	ULONG ulUserId = 0;

	LockSoap();

	if (lppECUser == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	if (lpUserID)
		ulUserId = ABEID_ID(lpUserID);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserID, lpUserID, &sUserId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getUser(m_ecSessionId, ulUserId, sUserId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserToUser(sResponse.lpsUser, ulFlags, &~lpECUser);
	if(hr != hrSuccess)
		goto exitm;
	*lppECUser = lpECUser.release();
 exitm:
	UnLockSoap();
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
	struct user sUser;
	unsigned int result = 0;
	convert_context	converter;

	LockSoap();

	if(lpECUser == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	sUser.lpszUsername		= TO_UTF8_DEF(lpECUser->lpszUsername);
	sUser.lpszPassword		= TO_UTF8_DEF(lpECUser->lpszPassword);
	sUser.lpszMailAddress	= TO_UTF8_DEF(lpECUser->lpszMailAddress);
	sUser.ulUserId			= ABEID_ID(lpECUser->sUserId.lpb);
	sUser.ulObjClass		= lpECUser->ulObjClass;
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
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__setUser(m_ecSessionId, &sUser, &result))
			er = KCERR_NETWORK_ERROR;
		else
			er = result;

	}
	END_SOAP_CALL
 exitm:
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
HRESULT WSTransport::HrCreateStore(ULONG ulStoreType, ULONG cbUserID,
    const ENTRYID *lpUserID, ULONG cbStoreID, const ENTRYID *lpStoreID,
    ULONG cbRootID, const ENTRYID *lpRootID, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	entryId sUserId, sStoreId, sRootId;
	LockSoap();

	if(lpUserID == NULL || lpStoreID == NULL || lpRootID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserID, lpUserID, &sUserId, true);
	if(hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbStoreID, lpStoreID, &sStoreId, true);
	if(hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbRootID, lpRootID, &sRootId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
	  if(SOAP_OK != m_lpCmd->ns__createStore(m_ecSessionId, ulStoreType, ABEID_ID(lpUserID), sUserId, sStoreId, sRootId, ulFlags, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrHookStore(ULONG ulStoreType, ULONG cbUserId,
    const ENTRYID *lpUserId, const GUID *lpGuid, ULONG ulSyncId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId sUserId;
	struct xsd__base64Binary sStoreGuid;
	LockSoap();

	if (cbUserId == 0 || lpUserId == NULL || lpGuid == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if(hr != hrSuccess)
		goto exitm;

	sStoreGuid.__ptr = (unsigned char*)lpGuid;
	sStoreGuid.__size = sizeof(GUID);
	
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__hookStore(m_ecSessionId, ulStoreType, sUserId, sStoreGuid, ulSyncId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrUnhookStore(ULONG ulStoreType, ULONG cbUserId,
    const ENTRYID *lpUserId, ULONG ulSyncId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId sUserId;
	LockSoap();

	if (cbUserId == 0 || lpUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__unhookStore(m_ecSessionId, ulStoreType, sUserId, ulSyncId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrRemoveStore(const GUID *lpGuid, ULONG ulSyncId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	struct xsd__base64Binary sStoreGuid;
	LockSoap();

	if (lpGuid == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	sStoreGuid.__ptr = (unsigned char*)lpGuid;
	sStoreGuid.__size = sizeof(GUID);
	
	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__removeStore(m_ecSessionId, sStoreGuid, ulSyncId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDeleteUser(ULONG cbUserId, LPENTRYID lpUserId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId;
	LockSoap();
	
	if(cbUserId < CbNewABEID("") || lpUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__deleteUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &er))
			er = KCERR_NETWORK_ERROR;
	
	}
	END_SOAP_CALL
 exitm:
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
HRESULT WSTransport::HrGetUserList(ULONG cbCompanyId,
    const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *lpcUsers,
    ECUSER **lppsUsers)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sCompanyId;
	struct userListResponse sResponse;

	LockSoap();

	if(lpcUsers == NULL || lppsUsers == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	if (cbCompanyId > 0 && lpCompanyId != NULL)
	{
		hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
		if (hr != hrSuccess)
			goto exitm;
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
		goto exitm;
 exitm:
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
	struct group sGroup;
	struct setGroupResponse sResponse;
	convert_context converter;

	LockSoap();

	if(lpECGroup == NULL || lpcbGroupId == NULL || lppGroupId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
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
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__createGroup(m_ecSessionId, &sGroup, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sGroupId, sResponse.ulGroupId, lpcbGroupId, lppGroupId);
 exitm:
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
	struct group sGroup;

	LockSoap();

	if(lpECGroup == NULL || lpECGroup->lpszGroupname == NULL || lpECGroup->lpszFullname == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
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
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__setGroup(m_ecSessionId, &sGroup, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL
 exitm:
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
HRESULT WSTransport::HrGetGroup(ULONG cbGroupID, const ENTRYID *lpGroupID,
    ULONG ulFlags, ECGROUP **lppECGroup)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	ECGROUP *lpGroup = NULL;
	entryId sGroupId;
	struct getGroupResponse sResponse;
	
	LockSoap();

	if (lpGroupID == NULL || lppECGroup == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupID, lpGroupID, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;

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
		goto exitm;
	*lppECGroup = lpGroup;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDeleteGroup(ULONG cbGroupId, LPENTRYID lpGroupId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sGroupId;
	LockSoap();
	
	if(cbGroupId < CbNewABEID("") || lpGroupId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__groupDelete(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
	entryId sUserId;
	LockSoap();
	
	if(cbUserId < CbNewABEID("") || lpUserId == NULL || lpcSenders == NULL || lppSenders == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

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
		goto exitm;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAddSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId, sSenderId;
	LockSoap();

	if (cbUserId < CbNewABEID("") || lpUserId == NULL || cbSenderId < CbNewABEID("") || lpSenderId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbSenderId, lpSenderId, &sSenderId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__addSendAsUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpSenderId), sSenderId, &er))
			er = KCERR_NETWORK_ERROR;
	
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDelSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId, sSenderId;
	LockSoap();

	if (cbUserId < CbNewABEID("") || lpUserId == NULL || cbSenderId < CbNewABEID("") || lpSenderId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbSenderId, lpSenderId, &sSenderId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__delSendAsUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpSenderId), sSenderId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetUserClientUpdateStatus(ULONG cbUserId,
    LPENTRYID lpUserId, ULONG ulFlags, ECUSERCLIENTUPDATESTATUS **lppECUCUS)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId;
	struct userClientUpdateStatusResponse sResponse;

    LockSoap();

    if (cbUserId < CbNewABEID("") || lpUserId == NULL) {
        hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
    }

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getUserClientUpdateStatus(m_ecSessionId, sUserId, &sResponse) )
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

	hr = CopyUserClientUpdateStatusFromSOAP(sResponse, ulFlags, lppECUCUS);
	if (hr != hrSuccess)
		goto exitm;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrRemoveAllObjects(ULONG cbUserId, LPENTRYID lpUserId)
{
    ECRESULT er = erSuccess;
    HRESULT hr = hrSuccess;
	entryId sUserId;
    LockSoap();
    
	if (cbUserId < CbNewABEID("") || lpUserId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__removeAllObjects(m_ecSessionId, sUserId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
		goto exitm;
	}

	//Resolve userid from username
	START_SOAP_CALL
	{
		if (m_lpCmd->ns__resolveUsername(m_ecSessionId,
		    convstring(lpszUserName, ulFlags).u8_str(),
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sUserId, sResponse.ulUserId, lpcbUserId, lppUserId);
 exitm:
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
		goto exitm;
	}

	//Resolve groupid from groupname
	START_SOAP_CALL
	{
		if (m_lpCmd->ns__resolveGroupname(m_ecSessionId,
		    convstring(lpszGroupName, ulFlags).u8_str(),
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sGroupId, sResponse.ulGroupId, lpcbGroupId, lppGroupId);
 exitm:
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
HRESULT WSTransport::HrGetGroupList(ULONG cbCompanyId,
    const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *lpcGroups,
    ECGROUP **lppsGroups)
{
	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;

 	struct groupListResponse sResponse;
	entryId sCompanyId;
	LockSoap();

	if(lpcGroups == NULL || lppsGroups == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}
	
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;
	
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
		goto exitm;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDeleteGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sGroupId, sUserId;
	LockSoap();

	if (!lpGroupId || !lpUserId) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	// Remove group
	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__deleteGroupUser(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, ABEID_ID(lpUserId), sUserId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAddGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sGroupId, sUserId;
	LockSoap();

	if (!lpGroupId || !lpUserId) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	// Remove group
	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__addGroupUser(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, ABEID_ID(lpUserId), sUserId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
	entryId sGroupId;
	LockSoap();

	if(lpGroupId == NULL || lpcUsers == NULL || lppsUsers == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;

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
		goto exitm;
 exitm:
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
	entryId sUserId;
	LockSoap();

	if(lpcGroup == NULL || lpUserId == NULL || lppsGroups == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

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
		goto exitm;
 exitm:
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
	struct company sCompany;
	struct setCompanyResponse sResponse;
	convert_context	converter;

	LockSoap();

	if(lpECCompany == NULL || lpcbCompanyId == NULL || lppCompanyId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	sCompany.ulAdministrator = 0;
	sCompany.lpszCompanyname = TO_UTF8_DEF(lpECCompany->lpszCompanyname);
	sCompany.ulIsABHidden = lpECCompany->ulIsABHidden;
	sCompany.lpsPropmap = NULL;
	sCompany.lpsMVPropmap = NULL;

	hr = CopyABPropsToSoap(&lpECCompany->sPropmap, &lpECCompany->sMVPropmap, ulFlags,
						   &sCompany.lpsPropmap, &sCompany.lpsMVPropmap);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__createCompany(m_ecSessionId, &sCompany, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sCompanyId, sResponse.ulCompanyId, MAPI_ABCONT, lpcbCompanyId, lppCompanyId);
 exitm:
	UnLockSoap();

	FreeABProps(sCompany.lpsPropmap, sCompany.lpsMVPropmap);

	return hr;
}

HRESULT WSTransport::HrDeleteCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sCompanyId;
	LockSoap();
	
	if(cbCompanyId < CbNewABEID("") || lpCompanyId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__deleteCompany(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
	struct company sCompany;
	convert_context converter;

	LockSoap();

	if(lpECCompany == NULL || lpECCompany->lpszCompanyname == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
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
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__setCompany(m_ecSessionId, &sCompany, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
HRESULT WSTransport::HrGetCompany(ULONG cbCompanyId, const ENTRYID *lpCompanyId,
    ULONG ulFlags, ECCOMPANY **lppECCompany)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	ECCOMPANY *lpCompany = NULL;
	struct getCompanyResponse sResponse;
	entryId sCompanyId;
	LockSoap();

	if (lpCompanyId == NULL || lppECCompany == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

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
		goto exitm;
	
	*lppECCompany = lpCompany;
 exitm:
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
		goto exitm;
	}

	//Resolve companyid from companyname
	START_SOAP_CALL
	{
		if (m_lpCmd->ns__resolveCompanyname(m_ecSessionId,
		    convstring(lpszCompanyName, ulFlags).u8_str(),
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sCompanyId, sResponse.ulCompanyId, MAPI_ABCONT, lpcbCompanyId, lppCompanyId);
 exitm:
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
		goto exitm;
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
		goto exitm;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAddCompanyToRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sSetCompanyId, sCompanyId;
	LockSoap();

	if (lpSetCompanyId == NULL || lpCompanyId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbSetCompanyId, lpSetCompanyId, &sSetCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__addCompanyToRemoteViewList(m_ecSessionId, ABEID_ID(lpSetCompanyId), sSetCompanyId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDelCompanyFromRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sSetCompanyId, sCompanyId;
	LockSoap();

	if (lpSetCompanyId == NULL || lpCompanyId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbSetCompanyId, lpSetCompanyId, &sSetCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__delCompanyFromRemoteViewList(m_ecSessionId, ABEID_ID(lpSetCompanyId), sSetCompanyId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
	entryId sCompanyId;
	LockSoap();

	if(lpcCompanies == NULL || lpCompanyId == NULL || lppsCompanies == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

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
		goto exitm;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrAddUserToRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sUserId, sCompanyId;
	LockSoap();

	if (lpUserId == NULL || lpCompanyId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__addUserToRemoteAdminList(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrDelUserFromRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sUserId, sCompanyId;
	LockSoap();

	if (lpUserId == NULL || lpCompanyId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__delUserFromRemoteAdminList(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpCompanyId), sCompanyId, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
	entryId sCompanyId;
	LockSoap();

	if(lpcUsers == NULL || lpCompanyId == NULL || lppsUsers == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

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
		goto exitm;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetPermissionRules(int ulType, ULONG cbEntryID,
    LPENTRYID lpEntryID, ULONG *lpcPermissions,
    ECPERMISSION **lppECPermissions)
{
	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;
	entryId sEntryId; // Do not free
	ecmem_ptr<ECPERMISSION> lpECPermissions;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG			cbUnWrapStoreID = 0;

	struct rightsResponse sRightResponse;

	LockSoap();

	if(lpcPermissions == NULL || lppECPermissions == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	// Remove servername, always
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;

	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sEntryId.__size = cbUnWrapStoreID;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getRights(m_ecSessionId, sEntryId, ulType, &sRightResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sRightResponse.er;

	}
	END_SOAP_CALL

	hr = ECAllocateBuffer(sizeof(ECPERMISSION) * sRightResponse.pRightsArray->__size,
	     &~lpECPermissions);
	if (hr != erSuccess)
		goto exitm;
	for (gsoap_size_t i = 0; i < sRightResponse.pRightsArray->__size; ++i) {
		lpECPermissions[i].ulRights	= sRightResponse.pRightsArray->__ptr[i].ulRights;
		lpECPermissions[i].ulState	= sRightResponse.pRightsArray->__ptr[i].ulState;
		lpECPermissions[i].ulType	= sRightResponse.pRightsArray->__ptr[i].ulType;

		hr = CopySOAPEntryIdToMAPIEntryId(&sRightResponse.pRightsArray->__ptr[i].sUserId, sRightResponse.pRightsArray->__ptr[i].ulUserid, MAPI_MAILUSER, (ULONG*)&lpECPermissions[i].sUserId.cb, (LPENTRYID*)&lpECPermissions[i].sUserId.lpb, lpECPermissions);
		if (hr != hrSuccess)
			goto exitm;
	}

	*lppECPermissions = lpECPermissions.release();
	*lpcPermissions = sRightResponse.pRightsArray->__size;
	lpECPermissions = NULL;
 exitm:
	UnLockSoap();
	return hr;
}

HRESULT WSTransport::HrSetPermissionRules(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG cPermissions,
    const ECPERMISSION *lpECPermissions)
{
	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;
	entryId sEntryId; // Do not free
	int				nChangedItems = 0;
	unsigned int	i,
					nItem;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG			cbUnWrapStoreID = 0;
	
	struct rightsArray rArray;
	
	LockSoap();

	if(cPermissions == 0 || lpECPermissions == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	// Remove servername, always
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;

	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
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
				goto exitm;
			++nItem;
		}
	}

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__setRights(m_ecSessionId, sEntryId, &rArray, &er))
			er = KCERR_NETWORK_ERROR;

	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();
	return hr;
}

HRESULT WSTransport::HrGetOwner(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG *lpcbOwnerId, LPENTRYID *lppOwnerId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sEntryId; // Do not free
	struct getOwnerResponse sResponse;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	LockSoap();

	if (lpcbOwnerId == NULL || lppOwnerId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	// Remove servername, always
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;

	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
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
 exitm:
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
HRESULT WSTransport::HrResolveNames(const SPropTagArray *lpPropTagArray,
    ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList)
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
		goto exitm;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__abResolveNames(m_ecSessionId, &aPropTag, lpsRowSet, &aFlags, ulFlags, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;

	}
	END_SOAP_CALL
	
	assert(sResponse.aFlags.__size == lpFlagList->cFlags);
	assert(static_cast<ULONG>(sResponse.sRowSet.__size) == lpAdrList->cEntries);
	for (gsoap_size_t i = 0; i < sResponse.aFlags.__size; ++i) {
		// Set the resolved items
		if(lpFlagList->ulFlag[i] == MAPI_UNRESOLVED && sResponse.aFlags.__ptr[i] == MAPI_RESOLVED)
		{
			lpAdrList->aEntries[i].cValues = sResponse.sRowSet.__ptr[i].__size;
			ECFreeBuffer(lpAdrList->aEntries[i].rgPropVals);

			hr = ECAllocateBuffer(sizeof(SPropValue) * lpAdrList->aEntries[i].cValues,
			     reinterpret_cast<void **>(&lpAdrList->aEntries[i].rgPropVals));
			if (hr != hrSuccess)
				goto exitm;
			hr = CopySOAPRowToMAPIRow(&sResponse.sRowSet.__ptr[i], lpAdrList->aEntries[i].rgPropVals, (void*)lpAdrList->aEntries[i].rgPropVals, &converter);
			if(hr != hrSuccess)
				goto exitm;

			lpFlagList->ulFlag[i] = sResponse.aFlags.__ptr[i];
		}else { // MAPI_AMBIGUOUS or MAPI_UNRESOLVED
			// only set the flag, do nothing with the row
			lpFlagList->ulFlag[i] = sResponse.aFlags.__ptr[i];
		}		
	}
 exitm:
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
	entryId sCompanyId;
	ULONG ulCompanyId = 0;

	LockSoap();

	if (lpCompanyId)
	{
		hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
		if (hr != hrSuccess)
			goto exitm;
		ulCompanyId = ABEID_ID(lpCompanyId);
	}

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__syncUsers(m_ecSessionId, ulCompanyId, sCompanyId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse;
	}
	END_SOAP_CALL
 exitm:
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
	entryId sUserId;
	LockSoap();
	
	if(lppsQuota == NULL || lpUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__GetQuota(m_ecSessionId, ABEID_ID(lpUserId), sUserId, bGetUserDefault, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	er = ECAllocateBuffer(sizeof(ECQUOTA), reinterpret_cast<void **>(&lpsQuota));
	if (er != erSuccess)
		goto exitm;
	lpsQuota->bUseDefaultQuota = sResponse.sQuota.bUseDefaultQuota;
	lpsQuota->bIsUserDefaultQuota = sResponse.sQuota.bIsUserDefaultQuota;
	lpsQuota->llHardSize = sResponse.sQuota.llHardSize;
	lpsQuota->llSoftSize = sResponse.sQuota.llSoftSize;
	lpsQuota->llWarnSize = sResponse.sQuota.llWarnSize;

	*lppsQuota = lpsQuota;
 exitm:
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
	entryId sUserId;
	LockSoap();

	if(lpsQuota == NULL || lpUserId == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

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
			er = sResponse;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::AddQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sCompanyId, sRecipientId;
	LockSoap();

	if (lpCompanyId == NULL || lpRecipientId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbRecipientId, lpRecipientId, &sRecipientId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__AddQuotaRecipient(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, ABEID_ID(lpRecipientId), sRecipientId, ulType, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();
	return hr;
}

HRESULT WSTransport::DeleteQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sCompanyId, sRecipientId;
	LockSoap();

	if (lpCompanyId == NULL || lpRecipientId == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbRecipientId, lpRecipientId, &sRecipientId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__DeleteQuotaRecipient(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, ABEID_ID(lpRecipientId), sRecipientId, ulType, &er))
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::GetQuotaRecipients(ULONG cbUserId, LPENTRYID lpUserId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;
	entryId sUserId;
	struct userListResponse sResponse;

	LockSoap();

	if(lpcUsers == NULL || lppsUsers == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

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
		goto exitm;
 exitm:
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
	entryId sUserId;
	LockSoap();

	if(lppsQuotaStatus == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if(SOAP_OK != m_lpCmd->ns__GetQuotaStatus(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	er = ECAllocateBuffer(sizeof(ECQUOTASTATUS), reinterpret_cast<void **>(&lpsQuotaStatus));
	if (er != erSuccess)
		goto exitm;
	lpsQuotaStatus->llStoreSize = sResponse.llStoreSize;
	lpsQuotaStatus->quotaStatus = (eQuotaStatus)sResponse.ulQuotaStatus;

	*lppsQuotaStatus = lpsQuotaStatus;
 exitm:
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
 exitm:
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
 exitm:
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
 exitm:
    UnLockSoap();
    
    return hr;
}

HRESULT WSTransport::HrResolvePseudoUrl(const char *lpszPseudoUrl, char **lppszServerPath, bool *lpbIsPeer)
{
	ECRESULT						er = erSuccess;
	HRESULT							hr = hrSuccess;
	struct resolvePseudoUrlResponse sResponse;
	char							*lpszServerPath = NULL;
	unsigned int					ulLen = 0;
	ECsResolveResult				*lpCachedResult = NULL;
	ECsResolveResult				cachedResult;

	if (lpszPseudoUrl == NULL || lppszServerPath == NULL)
		return MAPI_E_INVALID_PARAMETER;

	// First try the cache
	ulock_rec l_cache(m_ResolveResultCacheMutex);
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
		return hr;	// Early exit
	}
	l_cache.unlock();

	// Cache failed. Try the server
	LockSoap();

	START_SOAP_CALL
	{
		if (m_lpCmd->ns__resolvePseudoUrl(m_ecSessionId, lpszPseudoUrl,
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	cachedResult.hr = hr;
	if (hr == hrSuccess) {
		cachedResult.isPeer = sResponse.bIsPeer;
		cachedResult.serverPath = sResponse.lpszServerPath;
	}

	{
		scoped_rlock lock(m_ResolveResultCacheMutex);
		m_ResolveResultCache.AddCacheItem(lpszPseudoUrl, cachedResult);
	}

	ulLen = strlen(sResponse.lpszServerPath) + 1;
	hr = ECAllocateBuffer(ulLen, (void**)&lpszServerPath);
	if (hr != hrSuccess)
		goto exitm;

	memcpy(lpszServerPath, sResponse.lpszServerPath, ulLen);
	*lppszServerPath = lpszServerPath;
	*lpbIsPeer = sResponse.bIsPeer;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrGetServerDetails(ECSVRNAMELIST *lpServerNameList,
    ULONG ulFlags, ECSERVERLIST **lppsServerList)
{
	ECRESULT						er = erSuccess;
	HRESULT							hr = hrSuccess;
	struct getServerDetailsResponse sResponse;
	ecmem_ptr<struct mv_string8> lpsSvrNameList;

	LockSoap();

	if (lpServerNameList == NULL || lppsServerList == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}
	hr = SvrNameListToSoapMvString8(lpServerNameList, ulFlags & MAPI_UNICODE, &~lpsSvrNameList);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
    	if( SOAP_OK != m_lpCmd->ns__getServerDetails(m_ecSessionId, *lpsSvrNameList, ulFlags & ~MAPI_UNICODE, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapServerListToServerList(&sResponse.sServerList, ulFlags & MAPI_UNICODE, lppsServerList);
	if (hr != hrSuccess)
		goto exitm;
 exitm:
	UnLockSoap();
	return hr;
}

HRESULT WSTransport::HrGetChanges(const std::string& sourcekey, ULONG ulSyncId, ULONG ulChangeId, ULONG ulSyncType, ULONG ulFlags, LPSRestriction lpsRestrict, ULONG *lpulMaxChangeId, ULONG* lpcChanges, ICSCHANGE **lppChanges){
	HRESULT						hr = hrSuccess;
	ECRESULT					er = erSuccess;
	struct icsChangeResponse	sResponse;
	ecmem_ptr<ICSCHANGE> lpChanges;
	struct xsd__base64Binary	sSourceKey;
	struct restrictTable		*lpsSoapRestrict = NULL;
	
	sSourceKey.__ptr = (unsigned char *)sourcekey.c_str();
	sSourceKey.__size = sourcekey.size();

	LockSoap();

	if(lpsRestrict) {
    	hr = CopyMAPIRestrictionToSOAPRestriction(&lpsSoapRestrict, lpsRestrict);
    	if(hr != hrSuccess)
	        goto exitm;
    }

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getChanges(m_ecSessionId, sSourceKey, ulSyncId, ulChangeId, ulSyncType, ulFlags, lpsSoapRestrict, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	er = ECAllocateBuffer(sResponse.sChangesArray.__size * sizeof(ICSCHANGE), &~lpChanges);
	if (er != erSuccess)
		goto exitm;

	for (gsoap_size_t i = 0; i < sResponse.sChangesArray.__size; ++i) {
		lpChanges[i].ulChangeId = sResponse.sChangesArray.__ptr[i].ulChangeId;
		lpChanges[i].ulChangeType = sResponse.sChangesArray.__ptr[i].ulChangeType;
		lpChanges[i].ulFlags = sResponse.sChangesArray.__ptr[i].ulFlags;

		if(sResponse.sChangesArray.__ptr[i].sSourceKey.__size > 0) {
			er = ECAllocateMore(sResponse.sChangesArray.__ptr[i].sSourceKey.__size,
			     lpChanges, reinterpret_cast<void **>(&lpChanges[i].sSourceKey.lpb));
			if (er != erSuccess)
				goto exitm;
			lpChanges[i].sSourceKey.cb = sResponse.sChangesArray.__ptr[i].sSourceKey.__size;
			memcpy(lpChanges[i].sSourceKey.lpb, sResponse.sChangesArray.__ptr[i].sSourceKey.__ptr, sResponse.sChangesArray.__ptr[i].sSourceKey.__size);
		}
		
		if(sResponse.sChangesArray.__ptr[i].sParentSourceKey.__size > 0) {
			er = ECAllocateMore( sResponse.sChangesArray.__ptr[i].sParentSourceKey.__size,
			     lpChanges, reinterpret_cast<void **>(&lpChanges[i].sParentSourceKey.lpb));
			if (er != erSuccess)
				goto exitm;
			lpChanges[i].sParentSourceKey.cb = sResponse.sChangesArray.__ptr[i].sParentSourceKey.__size;
			memcpy(lpChanges[i].sParentSourceKey.lpb, sResponse.sChangesArray.__ptr[i].sParentSourceKey.__ptr, sResponse.sChangesArray.__ptr[i].sParentSourceKey.__size);
		}
		
	}
	
	*lpulMaxChangeId = sResponse.ulMaxChangeId;
	*lpcChanges = sResponse.sChangesArray.__size;
	*lppChanges = lpChanges.release();
 exitm:
	UnLockSoap();
	
	if(lpsSoapRestrict)
	    FreeRestrictTable(lpsSoapRestrict);
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
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpulSyncId = sResponse.ulSyncId;
 exitm:
	UnLockSoap();

	return hr;
}

HRESULT WSTransport::HrEntryIDFromSourceKey(ULONG cbStoreID, LPENTRYID lpStoreID, ULONG ulFolderSourceKeySize, BYTE * lpFolderSourceKey, ULONG ulMessageSourceKeySize, BYTE * lpMessageSourceKey, ULONG * lpcbEntryID, LPENTRYID * lppEntryID)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId		sStoreId;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	struct xsd__base64Binary	folderSourceKey;
	struct xsd__base64Binary	messageSourceKey;

	struct getEntryIDFromSourceKeyResponse sResponse;	
	

	LockSoap();

	if(ulFolderSourceKeySize == 0 || lpFolderSourceKey == NULL){
		hr = MAPI_E_INVALID_PARAMETER;
		goto exitm;
	}
	hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;

	sStoreId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
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
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sEntryId, lpcbEntryID, lppEntryID, NULL);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
	UnLockSoap();
	return hr;
}

HRESULT WSTransport::HrGetSyncStates(const ECLISTSYNCID &lstSyncId, ECLISTSYNCSTATE *lplstSyncState)
{
	HRESULT							hr = hrSuccess;
	ECRESULT						er = erSuccess;
	mv_long ulaSyncId;
	getSyncStatesReponse sResponse;
	SSyncState						sSyncState = {0};

	assert(lplstSyncState != NULL);
	LockSoap();

	if (lstSyncId.empty())
		goto exitm;
	ulaSyncId.__ptr = s_alloc<unsigned int>(nullptr, lstSyncId.size());
	for (auto sync_id : lstSyncId)
		ulaSyncId.__ptr[ulaSyncId.__size++] = sync_id;

	START_SOAP_CALL
	{
		if(SOAP_OK != m_lpCmd->ns__getSyncStates(m_ecSessionId, ulaSyncId, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	for (gsoap_size_t i = 0; i < sResponse.sSyncStates.__size; ++i) {
		sSyncState.ulSyncId = sResponse.sSyncStates.__ptr[i].ulSyncId;
		sSyncState.ulChangeId = sResponse.sSyncStates.__ptr[i].ulChangeId;
		lplstSyncState->emplace_back(std::move(sSyncState));
	}
 exitm:
	UnLockSoap();
	s_free(nullptr, ulaSyncId.__ptr);
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
	object_ptr<WSTableMultiStore> lpMultiStoreTable;

	if (lpMsgList == nullptr || lpMsgList->cValues == 0)
		return MAPI_E_INVALID_PARAMETER;
	hr = WSTableMultiStore::Create(ulFlags, m_lpCmd, m_hDataLock,
	     m_ecSessionId, cbEntryID, lpEntryID, lpMsgStore, this,
	     &~lpMultiStoreTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpMultiStoreTable->HrSetEntryIDs(lpMsgList);
	if (hr != hrSuccess)
		return hr;
	return lpMultiStoreTable->QueryInterface(IID_ECTableView,
	       reinterpret_cast<void **>(lppTableView));
}

HRESULT WSTransport::HrOpenMiscTable(ULONG ulTableType, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID, ECMsgStore *lpMsgStore, WSTableView **lppTableView)
{
	HRESULT hr = hrSuccess;
	object_ptr<WSTableMisc> lpMiscTable;

	if (ulTableType != TABLETYPE_STATS_SYSTEM && ulTableType != TABLETYPE_STATS_SESSIONS &&
		ulTableType != TABLETYPE_STATS_USERS && ulTableType != TABLETYPE_STATS_COMPANY  &&
		ulTableType != TABLETYPE_USERSTORES && ulTableType != TABLETYPE_STATS_SERVERS)
		return MAPI_E_INVALID_PARAMETER;
	hr = WSTableMisc::Create(ulTableType, ulFlags, m_lpCmd, m_hDataLock,
	     m_ecSessionId, cbEntryID, lpEntryID, lpMsgStore, this,
	     &~lpMiscTable);
	if (hr != hrSuccess)
		return hr;
	return lpMiscTable->QueryInterface(IID_ECTableView,
	       reinterpret_cast<void **>(lppTableView));
}

HRESULT WSTransport::HrSetLockState(ULONG cbEntryID, const ENTRYID *lpEntryID, bool bLocked)
{
	HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
	entryId eidMessage;

	if ((m_ulServerCapabilities & KOPANO_CAP_MSGLOCK) == 0)
		return hrSuccess;

	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &eidMessage, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (SOAP_OK != m_lpCmd->ns__setLockState(m_ecSessionId, eidMessage, bLocked, &er))
			er = KCERR_NETWORK_ERROR;
		/* else: er is already set and good to use */
	}
	END_SOAP_CALL
 exitm:
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

HRESULT WSTransport::HrTestPerform(const char *szCommand, unsigned int ulArgs,
    char **lpszArgs)
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
 exitm:
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
		if (m_lpCmd->ns__testSet(m_ecSessionId, szName, szValue, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
    }
    END_SOAP_CALL
 exitm:
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
		if (m_lpCmd->ns__testGet(m_ecSessionId, szName, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
        else
                er = sResponse.er;
    }
    END_SOAP_CALL
    
    hr = MAPIAllocateBuffer(strlen(sResponse.szValue)+1, (void **)&szValue);
    if(hr != hrSuccess)
		goto exitm;
        
    strcpy(szValue, sResponse.szValue);

    *lpszValue = szValue;
 exitm:
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
	scoped_rlock lock(m_mutexSessionReload);

	m_mapSessionReload[m_ulReloadId] = data;
	if(lpulId)
		*lpulId = m_ulReloadId;
	++m_ulReloadId;
	return hrSuccess;
}

HRESULT WSTransport::RemoveSessionReloadCallback(ULONG ulId)
{
	scoped_rlock lock(m_mutexSessionReload);
	return m_mapSessionReload.erase(ulId) == 0 ? MAPI_E_NOT_FOUND : hrSuccess;
}

SOAP_SOCKET WSTransport::RefuseConnect(struct soap* soap, const char* endpoint, const char* host, int port)
{
	soap->error = SOAP_TCP_ERROR;
	return SOAP_ERR;
}

HRESULT WSTransport::HrCancelIO()
{
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
	 */
	if (m_lpCmd == NULL || m_lpCmd->soap == NULL)
		return hrSuccess;

	// Override the SOAP connect (fopen) so that all new connections will fail with a network error
	m_lpCmd->soap->fopen = WSTransport::RefuseConnect;

	// If there is a socket currently open, close it now
	int s = m_lpCmd->soap->socket;
	if (s != SOAP_INVALID_SOCKET)
		m_lpCmd->soap->fshutdownsocket(m_lpCmd->soap, (SOAP_SOCKET)s, 2);
	return hrSuccess;
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
		*lppsArrayNotifications = s_alloc<notificationArray>(nullptr);
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

HRESULT WSTransport::HrResetFolderCount(ULONG cbEntryId, LPENTRYID lpEntryId, ULONG *lpulUpdates)
{
	HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
	entryId eidFolder;
	resetFolderCountResponse sResponse;

	LockSoap();

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &eidFolder, true);
	if (hr != hrSuccess)
		goto exitm;

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

 exitm:
    UnLockSoap();
    
	return hr;
}
