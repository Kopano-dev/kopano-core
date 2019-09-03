/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapitags.h>
#include <mapiutil.h>
#include <fstream>
#include <new>
#include <string>
#include <kopano/ECLogger.h>
#include "WSTransport.h"
#include "ProviderUtil.h"
#include "soapH.h"
#include "pcutil.hpp"
#include <kopano/kcodes.h>
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

static ECRESULT KCOIDCLogon(KCmdProxy2 *, const utf8string &user, const utf8string &imp_user, const utf8string &password, unsigned int caps, ECSESSIONGROUPID, const char *app_name, ECSESSIONID *, unsigned int *srv_caps, GUID *srv_guid, const std::string &cl_app_ver, const std::string &cl_app_misc);
static ECRESULT TrySSOLogon(KCmdProxy2 *, const utf8string &user, const utf8string &imp_user, unsigned int caps, ECSESSIONGROUPID, const char *app_name, ECSESSIONID *, unsigned int *srv_caps, GUID *srv_guid, const std::string &cl_app_ver, const std::string &cl_app_misc);

namespace KC {

template<> size_t GetCacheAdditionalSize(const ECsResolveResult &v)
{
	return MEMORY_USAGE_STRING(v.serverPath);
}

}

WSTransport::WSTransport() :
	ECUnknown("WSTransport"),
	m_ResolveResultCache("ResolveResult", 4096, 300), m_has_session(false)
{
	memset(&m_sServerGuid, 0, sizeof(m_sServerGuid));
}

WSTransport::~WSTransport()
{
	if (m_lpCmd != NULL)
		HrLogOff();
}

HRESULT WSTransport::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE3(ECTransport, WSTransport, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSTransport::Create(WSTransport **lppTransport)
{
	return alloc_wrap<WSTransport>().put(lppTransport);
}

/* Creates a transport working on the same session and session group as this transport */
HRESULT WSTransport::HrClone(WSTransport **lppTransport)
{
	WSTransport *lpTransport = NULL;
	auto hr = WSTransport::Create(&lpTransport);
	if(hr != hrSuccess)
		return hr;
	hr = CreateSoapTransport(m_sProfileProps, &unique_tie(lpTransport->m_lpCmd));
	if(hr != hrSuccess)
		return hr;
	lpTransport->m_ecSessionId = m_ecSessionId;
	lpTransport->m_ecSessionGroupId = m_ecSessionGroupId;
	*lppTransport = lpTransport;
	return hrSuccess;
}

HRESULT WSTransport::HrLogon2(const struct sGlobalProfileProps &sProfileProps)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	unsigned int ulCapabilities = KOPANO_CAP_GIFN32, ulLogonFlags = 0;
	unsigned int ulServerCapabilities = 0, ulServerVersion = 0;
	ECSESSIONID	ecSessionId = 0;
	std::unique_ptr<KCmdProxy2> new_cmd;
	KCmdProxy2 *lpCmd = nullptr;
	auto bPipeConnection = strncmp("file:", sProfileProps.strServerPath.c_str(), 5) == 0;
	struct logonResponse sResponse;
	convert_context	converter;
	utf8string	strUserName = converter.convert_to<utf8string>(sProfileProps.strUserName);
	utf8string	strPassword = converter.convert_to<utf8string>(sProfileProps.strPassword);
	utf8string	strImpersonateUser = converter.convert_to<utf8string>(sProfileProps.strImpersonateUser);
	soap_lock_guard spg(*this);

	if (m_lpCmd != nullptr) {
		lpCmd = m_lpCmd.get();
	} else if (CreateSoapTransport(sProfileProps, &unique_tie(new_cmd)) != hrSuccess) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	} else {
		lpCmd = new_cmd.get();
	}

	assert(!sProfileProps.strProfileName.empty());
	// Attach session to sessiongroup
	m_ecSessionGroupId = g_ecSessionManager.GetSessionGroupId(sProfileProps);

	ulCapabilities |= KOPANO_CAP_MAILBOX_OWNER | KOPANO_CAP_MULTI_SERVER | KOPANO_CAP_ENHANCED_ICS | KOPANO_CAP_UNICODE | KOPANO_CAP_MSGLOCK | KOPANO_CAP_MAX_ABCHANGEID | KOPANO_CAP_EXTENDED_ANON;

	if (sizeof(ECSESSIONID) == 8)
		ulCapabilities |= KOPANO_CAP_LARGE_SESSIONID;
	if (!bPipeConnection) {
		/*
		 * All connections except pipes request compression. The server
		 * can still reject the request.
		 */
#ifdef WITH_ZLIB
		if(! (sProfileProps.ulProfileFlags & EC_PROFILE_FLAGS_NO_COMPRESSION))
			ulCapabilities |= KOPANO_CAP_COMPRESSION; // only to remote server .. windows?
#endif
	} else if (sProfileProps.ulProfileFlags & EC_PROFILE_FLAGS_NO_UID_AUTH) {
		ulLogonFlags |= KOPANO_LOGON_NO_UID_AUTH;
	}

	if (sProfileProps.ulProfileFlags & EC_PROFILE_FLAGS_OIDC) {
		er = KCOIDCLogon(lpCmd, strUserName, strImpersonateUser,
		     strPassword, ulCapabilities, m_ecSessionGroupId,
		     GetAppName().c_str(), &ecSessionId, &ulServerCapabilities,
		     &m_sServerGuid, sProfileProps.strClientAppVersion,
		     sProfileProps.strClientAppMisc);
		if (er == erSuccess)
			goto auth;
		else
			goto failed;
	}

	// try single signon logon
	er = TrySSOLogon(lpCmd, strUserName, strImpersonateUser, ulCapabilities,
	     m_ecSessionGroupId, GetAppName().c_str(), &ecSessionId,
	     &ulServerCapabilities, &m_sServerGuid,
	     sProfileProps.strClientAppVersion, sProfileProps.strClientAppMisc);
	if (er == erSuccess)
		goto auth;

	// Login with username and password
	if (lpCmd->logon(strUserName.c_str(), strPassword.c_str(),
	    strImpersonateUser.c_str(), PROJECT_VERSION, ulCapabilities,
	    ulLogonFlags, {}, m_ecSessionGroupId,
	    GetAppName().c_str(), sProfileProps.strClientAppVersion.c_str(),
	    sProfileProps.strClientAppMisc.c_str(), &sResponse) != SOAP_OK) {
		auto d = soap_fault_detail(lpCmd->soap);
		ec_log_err("gsoap connect: %s", d == nullptr ? "()" : d);
		er = KCERR_SERVER_NOT_RESPONDING;
	} else {
		er = sResponse.er;
	}

failed: // Logon failed
	hr = kcerr_to_mapierr(er, MAPI_E_LOGON_FAILED);
	if (hr != hrSuccess)
		goto exit;

	/*
	 * Version is retrieved but not analyzed because we want to be able to
	 * connect to old servers for development.
	 */
	if (sResponse.lpszVersion == nullptr) {
		/* turn ParseKopanoVersion to take const char * in next ABI */
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	er = ParseKopanoVersion(sResponse.lpszVersion, &m_server_version, &ulServerVersion);
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

#ifdef WITH_ZLIB
	if (ulServerCapabilities & KOPANO_CAP_COMPRESSION) {
		/*
		 * GSOAP autodetects incoming compression, so even if not
		 * explicitly enabled, it will be accepted.
		 */
		soap_set_imode(lpCmd->soap, SOAP_ENC_ZLIB);
		soap_set_omode(lpCmd->soap, SOAP_ENC_ZLIB | SOAP_IO_CHUNK);
	}
#endif

	m_sProfileProps = sProfileProps;
	m_ulServerCapabilities = ulServerCapabilities;
	m_ecSessionId = ecSessionId;
	m_has_session = true;
	if (new_cmd != nullptr)
		m_lpCmd = std::move(new_cmd);
exit:
	spg.unlock();
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
	if (m_lpCmd == nullptr)
		return MAPI_E_NOT_INITIALIZED;
	m_lpCmd->soap->recv_timeout = ulSeconds;
	return hrSuccess;
}

HRESULT WSTransport::CreateAndLogonAlternate(LPCSTR szServer, WSTransport **lppTransport) const
{
	if (lppTransport == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<WSTransport> lpTransport;
	sGlobalProfileProps	sProfileProps = m_sProfileProps;
	auto hr = WSTransport::Create(&~lpTransport);
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
	if (lppTransport == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<WSTransport> lpTransport;
	auto hr = WSTransport::Create(&~lpTransport);
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
		p.second.second(p.second.first, m_ecSessionId);
	return hrSuccess;
}

static ECRESULT KCOIDCLogon(KCmdProxy2 *cmd, const utf8string &user,
    const utf8string &imp_user, const utf8string &password, unsigned int caps,
    ECSESSIONGROUPID ses_grp_id, const char *app_name, ECSESSIONID *ses_id,
    unsigned int *srv_caps, GUID *srv_guid, const std::string &cl_app_ver,
    const std::string &cl_app_misc)
{
	struct xsd__base64Binary sso_data;
	struct ssoLogonResponse resp;

	resp.ulSessionId = 0;
	auto token = "KCOIDC" + std::string(password.c_str());

	sso_data.__ptr = reinterpret_cast<unsigned char *>(const_cast<char *>(token.c_str()));
	sso_data.__size = token.length();

	if (cmd->ssoLogon(resp.ulSessionId, user.c_str(),
	    imp_user.c_str(), &sso_data, PROJECT_VERSION,
	    caps, {}, ses_grp_id, app_name,
	    cl_app_ver.c_str(), cl_app_misc.c_str(), &resp) != SOAP_OK)
		return KCERR_LOGON_FAILED;

	*ses_id = resp.ulSessionId;
	*srv_caps = resp.ulCapabilities;

	if (resp.sServerGuid.__ptr != nullptr &&
	    resp.sServerGuid.__size == sizeof(*srv_guid))
		memcpy(srv_guid, resp.sServerGuid.__ptr, sizeof(*srv_guid));

	return resp.er;
}

static ECRESULT TrySSOLogon(KCmdProxy2 *lpCmd, const utf8string &strUsername,
    const utf8string &strImpersonateUser, unsigned int ulCapabilities,
    ECSESSIONGROUPID ecSessionGroupId, const char *szAppName,
    ECSESSIONID *lpSessionId, unsigned int *lpulServerCapabilities,
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
	struct xsd__base64Binary sso_data;
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

		if (lpCmd->ssoLogon(resp.ulSessionId, strUsername.c_str(),
		    strImpersonateUser.c_str(), &sso_data, PROJECT_VERSION,
		    ulCapabilities, {}, ecSessionGroupId, szAppName,
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
	if ((ulFlags & ~EC_OVERRIDE_HOMESERVER) != 0)
		return MAPI_E_UNKNOWN_FLAGS;
	if (lppStoreID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct getStoreResponse sResponse;
	soap_lock_guard spg(*this);
	START_SOAP_CALL
	{
		if (m_lpCmd->getPublicStore(m_ecSessionId, ulFlags, &sResponse) != SOAP_OK)
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
	return hr;
}

HRESULT WSTransport::HrGetStore(ULONG cbMasterID, const ENTRYID *lpMasterID,
    ULONG *lpcbStoreID, ENTRYID **lppStoreID, ULONG *lpcbRootID,
    ENTRYID **lppRootID, std::string *lpstrRedirServer)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sEntryId; // Do not free
	struct getStoreResponse sResponse;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;
	soap_lock_guard spg(*this);

	if(lpMasterID) {
		hr = UnWrapServerClientStoreEntry(cbMasterID, lpMasterID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
		if(hr != hrSuccess)
			goto exitm;
		sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
		sEntryId.__size = cbUnWrapStoreID;
	}

	START_SOAP_CALL
	{
		if (m_lpCmd->getStore(m_ecSessionId, lpMasterID != nullptr ? &sEntryId : nullptr, &sResponse) != SOAP_OK)
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
	return hr;
}

HRESULT WSTransport::HrGetStoreName(ULONG cbStoreID, const ENTRYID *lpStoreID,
    ULONG ulFlags, TCHAR **lppszStoreName)
{
	if (lpStoreID == nullptr || lppszStoreName == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	entryId		sEntryId; // Do not free
	struct getStoreNameResponse sResponse;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;
	soap_lock_guard spg(*this);

	// Remove the servername
	auto hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sEntryId.__size = cbUnWrapStoreID;

	START_SOAP_CALL
	{
		if (m_lpCmd->getStoreName(m_ecSessionId, sEntryId, &sResponse) != SOAP_OK)
			er = KCERR_SERVER_NOT_RESPONDING;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = Utf8ToTString(sResponse.lpszStoreName, ulFlags, NULL, NULL, lppszStoreName);
 exitm:
	return hr;
}

HRESULT WSTransport::HrGetStoreType(ULONG cbStoreID, const ENTRYID *lpStoreID,
    ULONG *lpulStoreType)
{
	if (lpStoreID == nullptr || lpulStoreType == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	entryId		sEntryId; // Do not free
	struct getStoreTypeResponse sResponse;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;
	soap_lock_guard spg(*this);

	// Remove the servername
	auto hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sEntryId.__size = cbUnWrapStoreID;

	START_SOAP_CALL
	{
		if (m_lpCmd->getStoreType(m_ecSessionId, sEntryId, &sResponse) != SOAP_OK)
			er = KCERR_SERVER_NOT_RESPONDING;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpulStoreType = sResponse.ulStoreType;
 exitm:
	return hr;
}

HRESULT WSTransport::HrLogOff()
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	soap_lock_guard spg(*this);

	START_SOAP_CALL
	{
		if (m_lpCmd->logoff(m_ecSessionId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			m_has_session = false;
		m_lpCmd.reset();
	}
	END_SOAP_CALL
 exitm:
	return hrSuccess; // NOTE hrSuccess, never fails since we don't really mind that it failed.
}

HRESULT WSTransport::logoff_nd(void)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	soap_lock_guard spg(*this);

	START_SOAP_CALL
	{
		if (m_lpCmd->logoff(m_ecSessionId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			m_has_session = false;
	}
	END_SOAP_CALL
 exitm:
	return er;
}

HRESULT WSTransport::HrCheckExistObject(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulFlags)
{
	if (cbEntryID == 0 || lpEntryID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT	er = erSuccess;
	entryId sEntryId; // Do not free
	soap_lock_guard spg(*this);
	auto hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->checkExistObject(m_ecSessionId, sEntryId, ulFlags, &er) != SOAP_OK)
			er = KCERR_SERVER_NOT_RESPONDING;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrOpenPropStorage(ULONG cbParentEntryID,
    const ENTRYID *lpParentEntryID, ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulFlags, IECPropStorage **lppPropStorage)
{
	object_ptr<WSMAPIPropStorage> lpPropStorage;
	ecmem_ptr<ENTRYID> lpUnWrapParentID, lpUnWrapEntryID;
	unsigned int cbUnWrapParentID = 0, cbUnWrapEntryID = 0;

	if (lpParentEntryID) {
		auto hr = UnWrapServerClientStoreEntry(cbParentEntryID, lpParentEntryID, &cbUnWrapParentID, &~lpUnWrapParentID);
		if(hr != hrSuccess)
			return hr;
	}
	auto hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapEntryID, &~lpUnWrapEntryID);
	if(hr != hrSuccess)
		return hr;
	hr = WSMAPIPropStorage::Create(cbUnWrapParentID, lpUnWrapParentID,
	     cbUnWrapEntryID, lpUnWrapEntryID, ulFlags, m_ecSessionId,
	     m_ulServerCapabilities, this, &~lpPropStorage);
	if(hr != hrSuccess)
		return hr;
	return lpPropStorage->QueryInterface(IID_IECPropStorage, reinterpret_cast<void **>(lppPropStorage));
}

HRESULT WSTransport::HrOpenParentStorage(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage, IECPropStorage **lppPropStorage)
{
	object_ptr<ECParentStorage> lpPropStorage;
	auto hr = ECParentStorage::Create(lpParentObject, ulUniqueId, ulObjId, lpServerStorage, &~lpPropStorage);
	if(hr != hrSuccess)
		return hr;
	return lpPropStorage->QueryInterface(IID_IECPropStorage,
	       reinterpret_cast<void **>(lppPropStorage));
}

HRESULT WSTransport::HrOpenABPropStorage(ULONG cbEntryID,
    const ENTRYID *lpEntryID, IECPropStorage **lppPropStorage)
{
	object_ptr<WSABPropStorage> lpPropStorage;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	auto hr = UnWrapServerClientABEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		return hr;
	hr = WSABPropStorage::Create(cbUnWrapStoreID, lpUnWrapStoreID,
	     m_ecSessionId, this, &~lpPropStorage);
	if(hr != hrSuccess)
		return hr;
	return lpPropStorage->QueryInterface(IID_IECPropStorage,
	       reinterpret_cast<void **>(lppPropStorage));
}

HRESULT WSTransport::HrOpenFolderOps(ULONG cbEntryID, const ENTRYID *lpEntryID,
    WSMAPIFolderOps **lppFolderOps)
{
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

//FIXME: create this function
//	hr = CheckEntryIDType(cbEntryID, lpEntryID, MAPI_FOLDER);
//	if( hr != hrSuccess)
		//return hr;
	auto hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		return hr;
	return WSMAPIFolderOps::Create(m_ecSessionId, cbUnWrapStoreID,
	       lpUnWrapStoreID, this, lppFolderOps);
}

HRESULT WSTransport::HrOpenTableOps(ULONG ulType, ULONG ulFlags,
    ULONG cbEntryID, const ENTRYID *lpEntryID, ECMsgStore *lpMsgStore,
    WSTableView **lppTableOps)
{
	/*
	FIXME: Do a check ?
	if (peid->ulType != MAPI_FOLDER && peid->ulType != MAPI_MESSAGE)
		return MAPI_E_INVALID_ENTRYID;
	*/
	return WSStoreTableView::Create(ulType, ulFlags, m_ecSessionId,
	       cbEntryID, lpEntryID, lpMsgStore, this, lppTableOps);
}

HRESULT WSTransport::HrOpenABTableOps(ULONG ulType, ULONG ulFlags,
    ULONG cbEntryID, const ENTRYID *lpEntryID, ECABLogon *lpABLogon,
    WSTableView **lppTableOps)
{
	/*if (peid->ulType != MAPI_FOLDER && peid->ulType != MAPI_MESSAGE)
		return MAPI_E_INVALID_ENTRYID;
	*/
	return WSABTableView::Create(ulType, ulFlags, m_ecSessionId, cbEntryID,
	       lpEntryID, lpABLogon, this, lppTableOps);
}

HRESULT WSTransport::HrOpenMailBoxTableOps(ULONG ulFlags, ECMsgStore *lpMsgStore, WSTableView **lppTableView)
{
	object_ptr<WSTableMailBox> lpWSTable;
	auto hr = WSTableMailBox::Create(ulFlags, m_ecSessionId, lpMsgStore,
	          this, &~lpWSTable);
	if(hr != hrSuccess)
		return hr;
	return lpWSTable->QueryInterface(IID_ECTableView,
	       reinterpret_cast<void **>(lppTableView));
}

HRESULT WSTransport::HrOpenTableOutGoingQueueOps(ULONG cbStoreEntryID,
    const ENTRYID *lpStoreEntryID, ECMsgStore *lpMsgStore,
    WSTableOutGoingQueue **lppTableOutGoingQueueOps)
{
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	// lpStoreEntryID == null for master queue
	if(lpStoreEntryID) {
		auto hr = UnWrapServerClientStoreEntry(cbStoreEntryID, lpStoreEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
		if(hr != hrSuccess)
			return hr;
	}
	return WSTableOutGoingQueue::Create(m_ecSessionId,
	       cbUnWrapStoreID, lpUnWrapStoreID, lpMsgStore, this,
	       lppTableOutGoingQueueOps);
}

HRESULT WSTransport::HrDeleteObjects(ULONG ulFlags, const ENTRYLIST *lpMsgList, ULONG ulSyncId)
{
	if (lpMsgList->cValues == 0)
		return hrSuccess;

	ECRESULT er = erSuccess;
	struct entryList sEntryList;
	soap_lock_guard spg(*this);
	auto hr = CopyMAPIEntryListToSOAPEntryList(lpMsgList, &sEntryList);
	if(hr != hrSuccess)
		goto exitm;
	START_SOAP_CALL
	{
		if (m_lpCmd->deleteObjects(m_ecSessionId, ulFlags, &sEntryList, ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	soap_del_entryList(&sEntryList);
	return hr;
}

HRESULT WSTransport::HrNotify(const NOTIFICATION *lpNotification)
{
	/* FIMXE: also notify other types? */
	if (lpNotification == nullptr || lpNotification->ulEventType != fnevNewMail)
		return MAPI_E_NO_ACCESS;

	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct notification sNotification;
	int ulSize = 0;
	soap_lock_guard spg(*this);

	sNotification.ulConnection = 0;// The connection id should be calculate on the server side

	sNotification.ulEventType = lpNotification->ulEventType;
	sNotification.newmail = s_alloc<notificationNewMail>(nullptr);

	hr = CopyMAPIEntryIdToSOAPEntryId(lpNotification->info.newmail.cbEntryID, (LPENTRYID)lpNotification->info.newmail.lpEntryID, &sNotification.newmail->pEntryId);
	if(hr != hrSuccess)
		goto exitm;

	hr = CopyMAPIEntryIdToSOAPEntryId(lpNotification->info.newmail.cbParentID, (LPENTRYID)lpNotification->info.newmail.lpParentID, &sNotification.newmail->pParentId);
	if(hr != hrSuccess)
		goto exitm;

	if(lpNotification->info.newmail.lpszMessageClass){
		utf8string strMessageClass = convstring(lpNotification->info.newmail.lpszMessageClass, lpNotification->info.newmail.ulFlags);
		ulSize = strMessageClass.size() + 1;
		sNotification.newmail->lpszMessageClass = soap_strdup(nullptr, strMessageClass.c_str());
	}
	sNotification.newmail->ulMessageFlags = lpNotification->info.newmail.ulMessageFlags;

	START_SOAP_CALL
	{
		if (m_lpCmd->notify(m_ecSessionId, sNotification, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	spg.unlock();
	FreeNotificationStruct(&sNotification, false);

	return hr;
}

HRESULT WSTransport::HrSubscribe(ULONG cbKey, LPBYTE lpKey, ULONG ulConnection, ULONG ulEventMask)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	notifySubscribe notSubscribe;
	soap_lock_guard spg(*this);

	notSubscribe.ulConnection = ulConnection;
	notSubscribe.sKey.__size = cbKey;
	notSubscribe.sKey.__ptr = lpKey;
	notSubscribe.ulEventMask = ulEventMask;

	START_SOAP_CALL
	{
		if (m_lpCmd->notifySubscribe(m_ecSessionId, &notSubscribe, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrSubscribe(ULONG ulSyncId, ULONG ulChangeId, ULONG ulConnection, ULONG ulEventMask)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	notifySubscribe notSubscribe;
	soap_lock_guard spg(*this);

	notSubscribe.ulConnection = ulConnection;
	notSubscribe.sSyncState.ulSyncId = ulSyncId;
	notSubscribe.sSyncState.ulChangeId = ulChangeId;
	notSubscribe.ulEventMask = ulEventMask;

	START_SOAP_CALL
	{
		if (m_lpCmd->notifySubscribe(m_ecSessionId, &notSubscribe, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrSubscribeMulti(const ECLISTSYNCADVISE &lstSyncAdvises, ULONG ulEventMask)
{
	ECRESULT	er = erSuccess;
	notifySubscribeArray notSubscribeArray;
	unsigned	i = 0;
	soap_lock_guard spg(*this);

	notSubscribeArray.__size = lstSyncAdvises.size();
	auto hr = MAPIAllocateBuffer(notSubscribeArray.__size * sizeof(*notSubscribeArray.__ptr), reinterpret_cast<void **>(&notSubscribeArray.__ptr));
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
		if (m_lpCmd->notifySubscribeMulti(m_ecSessionId, &notSubscribeArray, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	MAPIFreeBuffer(notSubscribeArray.__ptr);
	return hr;
}

HRESULT WSTransport::HrUnSubscribe(ULONG ulConnection)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	soap_lock_guard spg(*this);

	START_SOAP_CALL
	{
		if (m_lpCmd->notifyUnSubscribe(m_ecSessionId, ulConnection, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrUnSubscribeMulti(const ECLISTCONNECTION &lstConnections)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	mv_long ulConnArray;
	unsigned i = 0;

	ulConnArray.__size = lstConnections.size();
	ulConnArray.__ptr  = soap_new_unsignedInt(nullptr, ulConnArray.__size);

	soap_lock_guard spg(*this);
	for (const auto &p : lstConnections)
		ulConnArray.__ptr[i++] = p.second;

	START_SOAP_CALL
	{
		if (m_lpCmd->notifyUnSubscribeMulti(m_ecSessionId, &ulConnArray, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	spg.unlock();
	soap_del_mv_long(&ulConnArray);
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
	if (lpChanges == nullptr || lpsProps == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if ((m_ulServerCapabilities & KOPANO_CAP_ENHANCED_ICS) == 0)
		return MAPI_E_NO_SUPPORT;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	memory_ptr<sourceKeyPairArray> ptrsSourceKeyPairs;
	WSMessageStreamExporterPtr ptrStreamExporter;
	propTagArray sPropTags = {0, 0};
	exportMessageChangesAsStreamResponse sResponse;

	hr = CopyICSChangeToSOAPSourceKeys(ulChanges, lpChanges + ulStart, &~ptrsSourceKeyPairs);
	if (hr != hrSuccess)
		goto exitm;
	sPropTags.__size = lpsProps->cValues;
	sPropTags.__ptr = (unsigned int*)lpsProps->aulPropTag;

	// Make sure to get the mime attachments ourselves
	soap_post_check_mime_attachments(m_lpCmd->soap);

	START_SOAP_CALL
	{
		if (m_lpCmd->exportMessageChangesAsStream(m_ecSessionId, ulFlags, sPropTags, *ptrsSourceKeyPairs, ulPropTag, &sResponse) != SOAP_OK)
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

HRESULT WSTransport::HrGetMessageStreamImporter(ULONG ulFlags, ULONG ulSyncId,
    ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG cbFolderEntryID,
    const ENTRYID *lpFolderEntryID, bool bNewMessage,
    const SPropValue *lpConflictItems,
    WSMessageStreamImporter **lppStreamImporter)
{
	WSMessageStreamImporterPtr ptrStreamImporter;

	if ((m_ulServerCapabilities & KOPANO_CAP_ENHANCED_ICS) == 0)
		return MAPI_E_NO_SUPPORT;
	auto hr = WSMessageStreamImporter::Create(ulFlags, ulSyncId, cbEntryID, lpEntryID, cbFolderEntryID, lpFolderEntryID, bNewMessage, lpConflictItems, this, &~ptrStreamImporter);
	if (hr != hrSuccess)
		return hr;

	*lppStreamImporter = ptrStreamImporter.release();
	return hrSuccess;
}

HRESULT WSTransport::HrGetIDsFromNames(LPMAPINAMEID *lppPropNames, ULONG cNames, ULONG ulFlags, ULONG **lpServerIDs)
{
	HRESULT hr = hrSuccess;
	struct namedPropArray sNamedProps;
	struct getIDsFromNamesResponse sResponse;
	convert_context convertContext;
	soap_lock_guard spg(*this);

	// Convert our data into a structure that the server can take
	sNamedProps.__size = cNames;
	auto er = ECAllocateBuffer(sizeof(struct namedProp) * cNames, reinterpret_cast<void **>(&sNamedProps.__ptr));
	if (er != erSuccess)
		goto exitm;
	memset(sNamedProps.__ptr, 0 , sizeof(struct namedProp) * cNames);

	for (unsigned int i = 0; i < cNames; ++i) {
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
		if (m_lpCmd->getIDsFromNames(m_ecSessionId, &sNamedProps, ulFlags, &sResponse) != SOAP_OK)
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
	spg.unlock();
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

	soap_lock_guard spg(*this);
	START_SOAP_CALL
	{
		if (m_lpCmd->getNamesFromIDs(m_ecSessionId, &sPropTags, &sResponse) != SOAP_OK)
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
			/* Also copy the trailing '\0' */
			memcpy(lppNames[i]->Kind.lpwstrName, strNameW.c_str(), (strNameW.size() + 1) * sizeof(wchar_t));
			lppNames[i]->ulKind = MNID_STRING;
		} else {
			// not found by server, we have actually allocated memory but it doesn't really matter
			lppNames[i] = NULL;
		}
	}

	*lpcResolved = sResponse.lpsNames.__size;
	*lpppNames = lppNames;
 exitm:
	return hr;
}

HRESULT WSTransport::HrGetReceiveFolderTable(ULONG ulFlags,
    ULONG cbStoreEntryID, const ENTRYID *lpStoreEntryID, SRowSet **lppsRowSet)
{
	struct receiveFolderTableResponse sReceiveFolders;
	ECRESULT	er = erSuccess;
	LPSRowSet	lpsRowSet = NULL;
	ULONG ulRowId = 0, cbUnWrapStoreID = 0;
	int			nLen = 0;
	entryId sEntryId; // Do not free
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	std::wstring unicode;
	convert_context converter;
	soap_lock_guard spg(*this);

	auto hr = UnWrapServerClientStoreEntry(cbStoreEntryID, lpStoreEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sEntryId.__size = cbUnWrapStoreID;

	// Get ReceiveFolder information from the server
	START_SOAP_CALL
	{
		if (m_lpCmd->getReceiveFolderTable(m_ecSessionId, sEntryId, &sReceiveFolders) != SOAP_OK)
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
			memcpy(lpsRowSet->aRow[i].lpProps[RFT_MSG_CLASS].Value.lpszW, unicode.c_str(), (unicode.length() + 1) * sizeof(wchar_t));
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
	return hr;
}

HRESULT WSTransport::HrGetReceiveFolder(ULONG cbStoreEntryID,
    const ENTRYID *lpStoreEntryID, const utf8string &strMessageClass,
    ULONG *lpcbEntryID, ENTRYID **lppEntryID, utf8string *lpstrExplicitClass)
{
	struct receiveFolderResponse sReceiveFolderTable;

	ECRESULT	er = erSuccess;
	entryId sEntryId; // Do not free
	ULONG cbEntryID = 0, cbUnWrapStoreID = 0;
	ecmem_ptr<ENTRYID> lpEntryID, lpUnWrapStoreID;
	soap_lock_guard spg(*this);

	auto hr = UnWrapServerClientStoreEntry(cbStoreEntryID, lpStoreEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sEntryId.__size = cbUnWrapStoreID;

	if(lpstrExplicitClass)
		lpstrExplicitClass->clear();

	// Get ReceiveFolder information from the server
	START_SOAP_CALL
	{
		if (m_lpCmd->getReceiveFolder(m_ecSessionId, sEntryId,
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
	return hr;
}

HRESULT WSTransport::HrSetReceiveFolder(ULONG cbStoreID,
    const ENTRYID *lpStoreID, const utf8string &strMessageClass,
    ULONG cbEntryID, const ENTRYID *lpEntryID)
{
	ECRESULT er = erSuccess;
	unsigned int result;
	entryId sStoreId, sEntryId; // Do not free
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;
	soap_lock_guard spg(*this);

	auto hr = UnWrapServerClientStoreEntry(cbStoreID, lpStoreID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;
	sStoreId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sStoreId.__size = cbUnWrapStoreID;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if (hr != hrSuccess)
		goto exitm;
	START_SOAP_CALL
	{
		if (m_lpCmd->setReceiveFolder(m_ecSessionId, sStoreId,
		    lpEntryID != nullptr ? &sEntryId : nullptr,
		    strMessageClass.c_str(), &result) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = result;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrSetReadFlag(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulFlags, ULONG ulSyncId)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;

	struct entryList sEntryList;
	entryId sEntryId;

	sEntryId.__ptr = (unsigned char*)lpEntryID;
	sEntryId.__size = cbEntryID;

	sEntryList.__size = 1;
	sEntryList.__ptr = &sEntryId;

	soap_lock_guard spg(*this);
	START_SOAP_CALL
	{
		if (m_lpCmd->setReadFlags(m_ecSessionId, ulFlags, nullptr, &sEntryList, ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrSubmitMessage(ULONG cbMessageID,
    const ENTRYID *lpMessageID, ULONG ulFlags)
{
	ECRESULT	er = erSuccess;
	entryId sEntryId; // Do not free
	soap_lock_guard spg(*this);
	auto hr = CopyMAPIEntryIdToSOAPEntryId(cbMessageID, lpMessageID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->submitMessage(m_ecSessionId, sEntryId, ulFlags, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrFinishedMessage(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulFlags)
{
	ECRESULT er = erSuccess;
	entryId sEntryId; // Do not free
	soap_lock_guard spg(*this);
	auto hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->finishedMessage(m_ecSessionId, sEntryId, ulFlags, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrAbortSubmit(ULONG cbEntryID, const ENTRYID *lpEntryID)
{
	ECRESULT er = erSuccess;
	entryId sEntryId; // Do not free
	soap_lock_guard spg(*this);
	auto hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->abortSubmit(m_ecSessionId, sEntryId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrResolveUserStore(const utf8string &strUserName, ULONG ulFlags, ULONG *lpulUserID, ULONG* lpcbStoreID, LPENTRYID* lppStoreID, std::string *lpstrRedirServer)
{
	if (strUserName.empty())
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct resolveUserStoreResponse sResponse;
	soap_lock_guard spg(*this);

	START_SOAP_CALL
	{
		if (m_lpCmd->resolveUserStore(m_ecSessionId, strUserName.c_str(),
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
	/* Currently, only archive stores are supported. */
	if (ulStoreType != ECSTORE_TYPE_ARCHIVE || lpcbStoreID == nullptr || lppStoreID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct resolveUserStoreResponse sResponse;
	soap_lock_guard spg(*this);

	START_SOAP_CALL
	{
		if (m_lpCmd->resolveUserStore(m_ecSessionId,
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
	if (lpECUser == nullptr || lpcbUserId == nullptr || lppUserId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT	hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct user sUser;
	struct setUserResponse sResponse;
	convert_context converter;
	soap_lock_guard spg(*this);

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
		if (m_lpCmd->createUser(m_ecSessionId, &sUser, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sUserId, sResponse.ulUserId, lpcbUserId, lppUserId);
 exitm:
	spg.unlock();
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
	if (lppECUser == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT	hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct getUserResponse	sResponse;
	ecmem_ptr<ECUSER> lpECUser;
	entryId	sUserId;
	ULONG ulUserId = 0;
	soap_lock_guard spg(*this);

	if (lpUserID)
		ulUserId = ABEID_ID(lpUserID);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserID, lpUserID, &sUserId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->getUser(m_ecSessionId, ulUserId, sUserId, &sResponse) != SOAP_OK)
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
	if (lpECUser == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT	hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct user sUser;
	unsigned int result = 0;
	convert_context	converter;
	soap_lock_guard spg(*this);

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
		if (m_lpCmd->setUser(m_ecSessionId, &sUser, &result) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = result;
	}
	END_SOAP_CALL
 exitm:
	spg.unlock();
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
	if (lpUserID == nullptr || lpStoreID == nullptr || lpRootID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	entryId sUserId, sStoreId, sRootId;
	soap_lock_guard spg(*this);

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
		if (m_lpCmd->createStore(m_ecSessionId, ulStoreType, ABEID_ID(lpUserID), sUserId, sStoreId, sRootId, ulFlags, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrHookStore(ULONG ulStoreType, ULONG cbUserId,
    const ENTRYID *lpUserId, const GUID *lpGuid, ULONG ulSyncId)
{
	if (cbUserId == 0 || lpUserId == nullptr || lpGuid == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId sUserId;
	struct xsd__base64Binary sStoreGuid;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if(hr != hrSuccess)
		goto exitm;

	sStoreGuid.__ptr = (unsigned char*)lpGuid;
	sStoreGuid.__size = sizeof(GUID);

	START_SOAP_CALL
	{
		if (m_lpCmd->hookStore(m_ecSessionId, ulStoreType, sUserId, sStoreGuid, ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrUnhookStore(ULONG ulStoreType, ULONG cbUserId,
    const ENTRYID *lpUserId, ULONG ulSyncId)
{
	if (cbUserId == 0 || lpUserId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId sUserId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->unhookStore(m_ecSessionId, ulStoreType, sUserId, ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrRemoveStore(const GUID *lpGuid, ULONG ulSyncId)
{
	if (lpGuid == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	struct xsd__base64Binary sStoreGuid;
	soap_lock_guard spg(*this);

	sStoreGuid.__ptr = (unsigned char*)lpGuid;
	sStoreGuid.__size = sizeof(GUID);

	START_SOAP_CALL
	{
		if (m_lpCmd->removeStore(m_ecSessionId, sStoreGuid, ulSyncId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrDeleteUser(ULONG cbUserId, const ENTRYID *lpUserId)
{
	if (cbUserId < CbNewABEID("") || lpUserId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->deleteUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
	if (lpcUsers == nullptr || lppsUsers == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sCompanyId;
	struct userListResponse sResponse;
	soap_lock_guard spg(*this);

	if (cbCompanyId > 0 && lpCompanyId != NULL)
	{
		hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
		if (hr != hrSuccess)
			goto exitm;
	}
	*lpcUsers = 0;

	START_SOAP_CALL
	{
		if (m_lpCmd->getUserList(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcUsers, lppsUsers);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
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
	if (lpECGroup == nullptr || lpcbGroupId == nullptr || lppGroupId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct group sGroup;
	struct setGroupResponse sResponse;
	convert_context converter;
	soap_lock_guard spg(*this);

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
		if (m_lpCmd->createGroup(m_ecSessionId, &sGroup, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sGroupId, sResponse.ulGroupId, lpcbGroupId, lppGroupId);
 exitm:
	spg.unlock();
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
	if (lpECGroup == nullptr || lpECGroup->lpszGroupname == nullptr ||
	    lpECGroup->lpszFullname == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	convert_context converter;
	struct group sGroup;
	soap_lock_guard spg(*this);

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
		if (m_lpCmd->setGroup(m_ecSessionId, &sGroup, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	spg.unlock();
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
	if (lpGroupID == nullptr || lppECGroup == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	ECGROUP *lpGroup = NULL;
	entryId sGroupId;
	struct getGroupResponse sResponse;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupID, lpGroupID, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->getGroup(m_ecSessionId, ABEID_ID(lpGroupID), sGroupId, &sResponse) != SOAP_OK)
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
	return hr;
}

HRESULT WSTransport::HrDeleteGroup(ULONG cbGroupId, const ENTRYID *lpGroupId)
{
	if (cbGroupId < CbNewABEID("") || lpGroupId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sGroupId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->groupDelete(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
HRESULT WSTransport::HrGetSendAsList(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG ulFlags, ULONG *lpcSenders, ECUSER **lppSenders)
{
	if (cbUserId < CbNewABEID("") || lpUserId == nullptr ||
	    lpcSenders == nullptr || lppSenders == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct userListResponse sResponse;
	entryId sUserId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->getSendAsList(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcSenders, lppSenders);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
	return hr;
}

HRESULT WSTransport::HrAddSendAsUser(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG cbSenderId, const ENTRYID *lpSenderId)
{
	if (cbUserId < CbNewABEID("") || lpUserId == nullptr ||
	    cbSenderId < CbNewABEID("") || lpSenderId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId, sSenderId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbSenderId, lpSenderId, &sSenderId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->addSendAsUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpSenderId), sSenderId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrDelSendAsUser(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG cbSenderId, const ENTRYID *lpSenderId)
{
	if (cbUserId < CbNewABEID("") || lpUserId == nullptr ||
	    cbSenderId < CbNewABEID("") || lpSenderId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sUserId, sSenderId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbSenderId, lpSenderId, &sSenderId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->delSendAsUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpSenderId), sSenderId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
	if (lpszUserName == nullptr || lpcbUserId == nullptr || lppUserId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct resolveUserResponse sResponse;
	soap_lock_guard spg(*this);

	//Resolve userid from username
	START_SOAP_CALL
	{
		if (m_lpCmd->resolveUsername(m_ecSessionId,
		    convstring(lpszUserName, ulFlags).u8_str(),
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sUserId, sResponse.ulUserId, lpcbUserId, lppUserId);
 exitm:
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
	if (lpszGroupName == nullptr || lpcbGroupId == nullptr || lppGroupId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct resolveGroupResponse sResponse;
	soap_lock_guard spg(*this);

	//Resolve groupid from groupname
	START_SOAP_CALL
	{
		if (m_lpCmd->resolveGroupname(m_ecSessionId,
		    convstring(lpszGroupName, ulFlags).u8_str(),
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sGroupId, sResponse.ulGroupId, lpcbGroupId, lppGroupId);
 exitm:
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
	if (lpcGroups == nullptr || lppsGroups == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;

 	struct groupListResponse sResponse;
	entryId sCompanyId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	*lpcGroups = 0;

	START_SOAP_CALL
	{
		if (m_lpCmd->getGroupList(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapGroupArrayToGroupArray(&sResponse.sGroupArray, ulFlags, lpcGroups, lppsGroups);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
	return hr;
}

HRESULT WSTransport::HrDeleteGroupUser(ULONG cbGroupId,
    const ENTRYID *lpGroupId, ULONG cbUserId, const ENTRYID *lpUserId)
{
	if (lpGroupId == nullptr || lpUserId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sGroupId, sUserId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	// Remove group
	START_SOAP_CALL
	{
		if (m_lpCmd->deleteGroupUser(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, ABEID_ID(lpUserId), sUserId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrAddGroupUser(ULONG cbGroupId, const ENTRYID *lpGroupId,
    ULONG cbUserId, const ENTRYID *lpUserId)
{
	if (lpGroupId == nullptr || lpUserId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sGroupId, sUserId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	// Remove group
	START_SOAP_CALL
	{
		if (m_lpCmd->addGroupUser(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, ABEID_ID(lpUserId), sUserId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
HRESULT WSTransport::HrGetUserListOfGroup(ULONG cbGroupId,
    const ENTRYID *lpGroupId, ULONG ulFlags, ULONG *lpcUsers,
    ECUSER **lppsUsers)
{
	if (lpGroupId == nullptr || lpcUsers == nullptr || lppsUsers == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct userListResponse sResponse;
	entryId sGroupId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbGroupId, lpGroupId, &sGroupId, true);
	if (hr != hrSuccess)
		goto exitm;

	// Get an userlist of a group
	START_SOAP_CALL
	{
		if (m_lpCmd->getUserListOfGroup(m_ecSessionId, ABEID_ID(lpGroupId), sGroupId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcUsers, lppsUsers);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
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
HRESULT WSTransport::HrGetGroupListOfUser(ULONG cbUserId,
    const ENTRYID *lpUserId, ULONG ulFlags, ULONG *lpcGroup,
    ECGROUP **lppsGroups)
{
	if (lpcGroup == nullptr || lpUserId == nullptr || lppsGroups == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct groupListResponse sResponse;
	entryId sUserId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	// Get a grouplist of an user
	START_SOAP_CALL
	{
		if (m_lpCmd->getGroupListOfUser(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapGroupArrayToGroupArray(&sResponse.sGroupArray, ulFlags, lpcGroup, lppsGroups);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
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
	if (lpECCompany == nullptr || lpcbCompanyId == nullptr || lppCompanyId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct company sCompany;
	struct setCompanyResponse sResponse;
	convert_context	converter;
	soap_lock_guard spg(*this);

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
		if (m_lpCmd->createCompany(m_ecSessionId, &sCompany, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sCompanyId, sResponse.ulCompanyId, MAPI_ABCONT, lpcbCompanyId, lppCompanyId);
 exitm:
	spg.unlock();
	FreeABProps(sCompany.lpsPropmap, sCompany.lpsMVPropmap);

	return hr;
}

HRESULT WSTransport::HrDeleteCompany(ULONG cbCompanyId, const ENTRYID *lpCompanyId)
{
	if (cbCompanyId < CbNewABEID("") || lpCompanyId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sCompanyId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->deleteCompany(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
	if (lpECCompany == nullptr || lpECCompany->lpszCompanyname == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct company sCompany;
	convert_context converter;
	soap_lock_guard spg(*this);

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
		if (m_lpCmd->setCompany(m_ecSessionId, &sCompany, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	spg.unlock();
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
	if (lpCompanyId == nullptr || lppECCompany == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	ECCOMPANY *lpCompany = NULL;
	struct getCompanyResponse sResponse;
	entryId sCompanyId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->getCompany(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse) != SOAP_OK)
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
	if (lpszCompanyName == nullptr || lpcbCompanyId == nullptr ||
	    lppCompanyId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct resolveCompanyResponse sResponse;
	soap_lock_guard spg(*this);

	//Resolve companyid from companyname
	START_SOAP_CALL
	{
		if (m_lpCmd->resolveCompanyname(m_ecSessionId,
		    convstring(lpszCompanyName, ulFlags).u8_str(),
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sCompanyId, sResponse.ulCompanyId, MAPI_ABCONT, lpcbCompanyId, lppCompanyId);
 exitm:
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
	if (lpcCompanies == nullptr || lppsCompanies == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct companyListResponse sResponse;
	soap_lock_guard spg(*this);

	*lpcCompanies = 0;

	START_SOAP_CALL
	{
		if (m_lpCmd->getCompanyList(m_ecSessionId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapCompanyArrayToCompanyArray(&sResponse.sCompanyArray, ulFlags, lpcCompanies, lppsCompanies);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
	return hr;
}

HRESULT WSTransport::HrAddCompanyToRemoteViewList(ULONG cbSetCompanyId,
    const ENTRYID *lpSetCompanyId, ULONG cbCompanyId,
    const ENTRYID *lpCompanyId)
{
	if (lpSetCompanyId == nullptr || lpCompanyId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sSetCompanyId, sCompanyId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbSetCompanyId, lpSetCompanyId, &sSetCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->addCompanyToRemoteViewList(m_ecSessionId, ABEID_ID(lpSetCompanyId), sSetCompanyId, ABEID_ID(lpCompanyId), sCompanyId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrDelCompanyFromRemoteViewList(ULONG cbSetCompanyId,
    const ENTRYID *lpSetCompanyId, ULONG cbCompanyId,
    const ENTRYID *lpCompanyId)
{
	if (lpSetCompanyId == nullptr || lpCompanyId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sSetCompanyId, sCompanyId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbSetCompanyId, lpSetCompanyId, &sSetCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->delCompanyFromRemoteViewList(m_ecSessionId, ABEID_ID(lpSetCompanyId), sSetCompanyId, ABEID_ID(lpCompanyId), sCompanyId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
    const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *lpcCompanies,
    ECCOMPANY **lppsCompanies)
{
	if (lpcCompanies == nullptr || lpCompanyId == nullptr ||
	    lppsCompanies == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct companyListResponse sResponse;
	entryId sCompanyId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	*lpcCompanies = 0;

	START_SOAP_CALL
	{
		if (m_lpCmd->getRemoteViewList(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapCompanyArrayToCompanyArray(&sResponse.sCompanyArray, ulFlags, lpcCompanies, lppsCompanies);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
	return hr;
}

HRESULT WSTransport::HrAddUserToRemoteAdminList(ULONG cbUserId,
    const ENTRYID *lpUserId, ULONG cbCompanyId, const ENTRYID *lpCompanyId)
{
	if (lpUserId == nullptr || lpCompanyId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sUserId, sCompanyId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->addUserToRemoteAdminList(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpCompanyId), sCompanyId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrDelUserFromRemoteAdminList(ULONG cbUserId,
    const ENTRYID *lpUserId, ULONG cbCompanyId, const ENTRYID *lpCompanyId)
{
	if (lpUserId == nullptr || lpCompanyId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sUserId, sCompanyId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->delUserFromRemoteAdminList(m_ecSessionId, ABEID_ID(lpUserId), sUserId, ABEID_ID(lpCompanyId), sCompanyId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
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
    const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *lpcUsers,
    ECUSER **lppsUsers)
{
	if (lpcUsers == nullptr || lpCompanyId == nullptr || lppsUsers == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct userListResponse sResponse;
	entryId sCompanyId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;

	*lpcUsers = 0;

	START_SOAP_CALL
	{
		if (m_lpCmd->getRemoteAdminList(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcUsers, lppsUsers);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
	return hr;
}

HRESULT WSTransport::HrGetPermissionRules(int ulType, ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG *lpcPermissions,
    ECPERMISSION **lppECPermissions)
{
	if (lpcPermissions == nullptr || lppECPermissions == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;
	entryId sEntryId; // Do not free
	ecmem_ptr<ECPERMISSION> lpECPermissions;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG			cbUnWrapStoreID = 0;

	struct rightsResponse sRightResponse;
	soap_lock_guard spg(*this);

	// Remove servername, always
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;

	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sEntryId.__size = cbUnWrapStoreID;

	START_SOAP_CALL
	{
		if (m_lpCmd->getRights(m_ecSessionId, sEntryId, ulType, &sRightResponse) != SOAP_OK)
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
	return hr;
}

HRESULT WSTransport::HrSetPermissionRules(ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG cPermissions,
    const ECPERMISSION *lpECPermissions)
{
	if (cPermissions == 0 || lpECPermissions == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;
	entryId sEntryId; // Do not free
	int				nChangedItems = 0;
	unsigned int	i,
					nItem;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG			cbUnWrapStoreID = 0;

	struct rightsArray rArray;
	soap_lock_guard spg(*this);

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

	rArray.__ptr  = soap_new_rights(m_lpCmd->soap, nChangedItems);
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
		if (m_lpCmd->setRights(m_ecSessionId, sEntryId, &rArray, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrGetOwner(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG *lpcbOwnerId, ENTRYID **lppOwnerId)
{
	if (lpcbOwnerId == nullptr || lppOwnerId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sEntryId; // Do not free
	struct getOwnerResponse sResponse;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;
	soap_lock_guard spg(*this);

	// Remove servername, always
	hr = UnWrapServerClientStoreEntry(cbEntryID, lpEntryID, &cbUnWrapStoreID, &~lpUnWrapStoreID);
	if(hr != hrSuccess)
		goto exitm;

	sEntryId.__ptr = reinterpret_cast<unsigned char *>(lpUnWrapStoreID.get());
	sEntryId.__size = cbUnWrapStoreID;

	START_SOAP_CALL
	{
		if (m_lpCmd->getOwner(m_ecSessionId, sEntryId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sOwner, sResponse.ulOwner, lpcbOwnerId, lppOwnerId);
 exitm:
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
	soap_lock_guard spg(*this);

	aPropTag.__ptr = (unsigned int *)&lpPropTagArray->aulPropTag; // just a reference
	aPropTag.__size = lpPropTagArray->cValues;

	aFlags.__ptr = (unsigned int *)&lpFlagList->ulFlag;
	aFlags.__size = lpFlagList->cFlags;

	hr = CopyMAPIRowSetToSOAPRowSet((LPSRowSet)lpAdrList, &lpsRowSet, &converter);
	if(hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->abResolveNames(m_ecSessionId, &aPropTag, lpsRowSet, &aFlags, ulFlags, &sResponse) != SOAP_OK)
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
			hr = CopySOAPRowToMAPIRow(&sResponse.sRowSet.__ptr[i], lpAdrList->aEntries[i].rgPropVals, lpAdrList->aEntries[i].rgPropVals, &converter);
			if(hr != hrSuccess)
				goto exitm;

			lpFlagList->ulFlag[i] = sResponse.aFlags.__ptr[i];
		}else { // MAPI_AMBIGUOUS or MAPI_UNRESOLVED
			// only set the flag, do nothing with the row
			lpFlagList->ulFlag[i] = sResponse.aFlags.__ptr[i];
		}
	}
 exitm:
	spg.unlock();
	soap_del_PointerTorowSet(&lpsRowSet);
	return hr;
}

HRESULT WSTransport::HrSyncUsers(ULONG cbCompanyId, const ENTRYID *lpCompanyId)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	unsigned int sResponse;
	entryId sCompanyId;
	ULONG ulCompanyId = 0;
	soap_lock_guard spg(*this);

	if (lpCompanyId)
	{
		hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
		if (hr != hrSuccess)
			goto exitm;
		ulCompanyId = ABEID_ID(lpCompanyId);
	}

	START_SOAP_CALL
	{
		if (m_lpCmd->syncUsers(m_ecSessionId, ulCompanyId, sCompanyId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::GetQuota(ULONG cbUserId, const ENTRYID *lpUserId,
    bool bGetUserDefault, ECQUOTA **lppsQuota)
{
	if (lppsQuota == nullptr || lpUserId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT				er = erSuccess;
	HRESULT					hr = hrSuccess;
	struct quotaResponse	sResponse;
	ECQUOTA *lpsQuota =  NULL;
	entryId sUserId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->GetQuota(m_ecSessionId, ABEID_ID(lpUserId), sUserId, bGetUserDefault, &sResponse) != SOAP_OK)
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
	return hr;
}

HRESULT WSTransport::SetQuota(ULONG cbUserId, const ENTRYID *lpUserId,
    ECQUOTA *lpsQuota)
{
	if (lpsQuota == nullptr || lpUserId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT				er = erSuccess;
	HRESULT					hr = hrSuccess;
	unsigned int			sResponse;
	struct quota			sQuota;
	entryId sUserId;
	soap_lock_guard spg(*this);

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
		if (m_lpCmd->SetQuota(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sQuota, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::AddQuotaRecipient(ULONG cbCompanyId,
    const ENTRYID *lpCompanyId, ULONG cbRecipientId,
    const ENTRYID *lpRecipientId, ULONG ulType)
{
	if (lpCompanyId == nullptr || lpRecipientId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId	sCompanyId, sRecipientId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbRecipientId, lpRecipientId, &sRecipientId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->AddQuotaRecipient(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, ABEID_ID(lpRecipientId), sRecipientId, ulType, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::DeleteQuotaRecipient(ULONG cbCompanyId,
    const ENTRYID *lpCompanyId, ULONG cbRecipientId,
    const ENTRYID *lpRecipientId, ULONG ulType)
{
	if (lpCompanyId == nullptr || lpRecipientId == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	entryId sCompanyId, sRecipientId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbCompanyId, lpCompanyId, &sCompanyId, true);
	if (hr != hrSuccess)
		goto exitm;
	hr = CopyMAPIEntryIdToSOAPEntryId(cbRecipientId, lpRecipientId, &sRecipientId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->DeleteQuotaRecipient(m_ecSessionId, ABEID_ID(lpCompanyId), sCompanyId, ABEID_ID(lpRecipientId), sRecipientId, ulType, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::GetQuotaRecipients(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	if (lpcUsers == nullptr || lppsUsers == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;
	entryId sUserId;
	struct userListResponse sResponse;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	*lpcUsers = 0;

	START_SOAP_CALL
	{
		if (m_lpCmd->GetQuotaRecipients(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapUserArrayToUserArray(&sResponse.sUserArray, ulFlags, lpcUsers, lppsUsers);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
	return hr;
}

HRESULT WSTransport::GetQuotaStatus(ULONG cbUserId, const ENTRYID *lpUserId,
    ECQUOTASTATUS **lppsQuotaStatus)
{
	if (lppsQuotaStatus == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT				er = erSuccess;
	HRESULT					hr = hrSuccess;
	struct quotaStatus		sResponse;
	ECQUOTASTATUS *lpsQuotaStatus =  NULL;
	entryId sUserId;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbUserId, lpUserId, &sUserId, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->GetQuotaStatus(m_ecSessionId, ABEID_ID(lpUserId), sUserId, &sResponse) != SOAP_OK)
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
	return hr;
}

HRESULT WSTransport::HrPurgeSoftDelete(ULONG ulDays)
{
    HRESULT						hr = hrSuccess;
    ECRESULT					er = erSuccess;
	soap_lock_guard spg(*this);

	START_SOAP_CALL
	{
		if (m_lpCmd->purgeSoftDelete(m_ecSessionId, ulDays, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
    return hr;
}

HRESULT WSTransport::HrPurgeCache(ULONG ulFlags)
{
    HRESULT						hr = hrSuccess;
    ECRESULT					er = erSuccess;
	soap_lock_guard spg(*this);

	START_SOAP_CALL
	{
		if (m_lpCmd->purgeCache(m_ecSessionId, ulFlags, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
 exitm:
    return hr;
}

HRESULT WSTransport::HrPurgeDeferredUpdates(ULONG *lpulRemaining)
{
    HRESULT						hr = hrSuccess;
    ECRESULT					er = erSuccess;
    struct purgeDeferredUpdatesResponse sResponse;
	soap_lock_guard spg(*this);

	START_SOAP_CALL
	{
		if (m_lpCmd->purgeDeferredUpdates(m_ecSessionId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
        else
            er = sResponse.er;

        *lpulRemaining = sResponse.ulDeferredRemaining;
	}
	END_SOAP_CALL
 exitm:
    return hr;
}

HRESULT WSTransport::HrResolvePseudoUrl(const char *lpszPseudoUrl, char **lppszServerPath, bool *lpbIsPeer)
{
	if (lpszPseudoUrl == nullptr || lppszServerPath == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT						er = erSuccess;
	HRESULT							hr = hrSuccess;
	struct resolvePseudoUrlResponse sResponse;
	char							*lpszServerPath = NULL;
	unsigned int					ulLen = 0;
	ECsResolveResult				*lpCachedResult = NULL;
	ECsResolveResult				cachedResult;

	// First try the cache
	ulock_rec l_cache(m_ResolveResultCacheMutex);
	er = m_ResolveResultCache.GetCacheItem(lpszPseudoUrl, &lpCachedResult);
	if (er == erSuccess) {
		hr = lpCachedResult->hr;
		if (hr == hrSuccess) {
			ulLen = lpCachedResult->serverPath.length() + 1;
			hr = ECAllocateBuffer(ulLen, reinterpret_cast<void **>(&lpszServerPath));
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
	soap_lock_guard spg(*this);
	START_SOAP_CALL
	{
		if (m_lpCmd->resolvePseudoUrl(m_ecSessionId, lpszPseudoUrl, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	cachedResult.hr = hr;
	if (hr == hrSuccess) {
		cachedResult.isPeer = sResponse.bIsPeer;
		if (sResponse.lpszServerPath != nullptr) {
			cachedResult.serverPath = sResponse.lpszServerPath;
			ulLen = strlen(sResponse.lpszServerPath) + 1;
		}
	}

	{
		scoped_rlock lock(m_ResolveResultCacheMutex);
		m_ResolveResultCache.AddCacheItem(lpszPseudoUrl, std::move(cachedResult));
	}

	hr = ECAllocateBuffer(ulLen, reinterpret_cast<void **>(&lpszServerPath));
	if (hr != hrSuccess)
		goto exitm;

	memcpy(lpszServerPath, sResponse.lpszServerPath, ulLen);
	*lppszServerPath = lpszServerPath;
	*lpbIsPeer = sResponse.bIsPeer;
 exitm:
	return hr;
}

HRESULT WSTransport::HrGetServerDetails(ECSVRNAMELIST *lpServerNameList,
    ULONG ulFlags, ECSERVERLIST **lppsServerList)
{
	if (lpServerNameList == nullptr || lppsServerList == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT						er = erSuccess;
	HRESULT							hr = hrSuccess;
	struct getServerDetailsResponse sResponse;
	ecmem_ptr<struct mv_string8> lpsSvrNameList;
	soap_lock_guard spg(*this);

	hr = SvrNameListToSoapMvString8(lpServerNameList, ulFlags & MAPI_UNICODE, &~lpsSvrNameList);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->getServerDetails(m_ecSessionId, *lpsSvrNameList, ulFlags & ~MAPI_UNICODE, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = SoapServerListToServerList(&sResponse.sServerList, ulFlags & MAPI_UNICODE, lppsServerList);
	if (hr != hrSuccess)
		goto exitm;
 exitm:
	return hr;
}

HRESULT WSTransport::HrGetChanges(const std::string &sourcekey, ULONG ulSyncId,
    ULONG ulChangeId, ULONG ulSyncType, ULONG ulFlags,
    const SRestriction *lpsRestrict, ULONG *lpulMaxChangeId, ULONG *lpcChanges,
    ICSCHANGE **lppChanges)
{
	HRESULT						hr = hrSuccess;
	ECRESULT					er = erSuccess;
	struct icsChangeResponse	sResponse;
	ecmem_ptr<ICSCHANGE> lpChanges;
	struct xsd__base64Binary	sSourceKey;
	struct restrictTable		*lpsSoapRestrict = NULL;

	sSourceKey.__ptr = (unsigned char *)sourcekey.c_str();
	sSourceKey.__size = sourcekey.size();

	soap_lock_guard spg(*this);
	if(lpsRestrict) {
    	hr = CopyMAPIRestrictionToSOAPRestriction(&lpsSoapRestrict, lpsRestrict);
    	if(hr != hrSuccess)
	        goto exitm;
    }

	START_SOAP_CALL
	{
		if (m_lpCmd->getChanges(m_ecSessionId, sSourceKey, ulSyncId, ulChangeId, ulSyncType, ulFlags, lpsSoapRestrict, &sResponse) != SOAP_OK)
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
	spg.unlock();
	soap_del_PointerTorestrictTable(&lpsSoapRestrict);
	return hr;
}

HRESULT WSTransport::HrSetSyncStatus(const std::string& sourcekey, ULONG ulSyncId, ULONG ulChangeId, ULONG ulSyncType, ULONG ulFlags, ULONG* lpulSyncId){
	HRESULT				hr = hrSuccess;
	ECRESULT			er = erSuccess;
	struct setSyncStatusResponse sResponse;
	struct xsd__base64Binary sSourceKey;

	sSourceKey.__size = sourcekey.size();
	sSourceKey.__ptr = (unsigned char *)sourcekey.c_str();

	soap_lock_guard spg(*this);
	START_SOAP_CALL
	{
		if (m_lpCmd->setSyncStatus(m_ecSessionId, sSourceKey, ulSyncId, ulChangeId, ulSyncType, ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpulSyncId = sResponse.ulSyncId;
 exitm:
	return hr;
}

HRESULT WSTransport::HrEntryIDFromSourceKey(ULONG cbStoreID,
    const ENTRYID *lpStoreID, ULONG ulFolderSourceKeySize,
    BYTE *lpFolderSourceKey, ULONG ulMessageSourceKeySize,
    BYTE *lpMessageSourceKey, ULONG *lpcbEntryID, ENTRYID **lppEntryID)
{
	if (ulFolderSourceKeySize == 0 || lpFolderSourceKey == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT		hr = hrSuccess;
	ECRESULT	er = erSuccess;
	entryId		sStoreId;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	struct xsd__base64Binary	folderSourceKey;
	struct xsd__base64Binary	messageSourceKey;

	struct getEntryIDFromSourceKeyResponse sResponse;
	soap_lock_guard spg(*this);

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
		if (m_lpCmd->getEntryIDFromSourceKey(m_ecSessionId, sStoreId, folderSourceKey, messageSourceKey, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPEntryIdToMAPIEntryId(&sResponse.sEntryId, lpcbEntryID, lppEntryID, NULL);
	if(hr != hrSuccess)
		goto exitm;
 exitm:
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
	soap_lock_guard spg(*this);
	if (lstSyncId.empty())
		goto exitm;
	ulaSyncId.__ptr = soap_new_unsignedInt(nullptr, lstSyncId.size());
	for (auto sync_id : lstSyncId)
		ulaSyncId.__ptr[ulaSyncId.__size++] = sync_id;

	START_SOAP_CALL
	{
		if (m_lpCmd->getSyncStates(m_ecSessionId, ulaSyncId, &sResponse) != SOAP_OK)
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
	spg.unlock();
	soap_del_mv_long(&ulaSyncId);
	return hr;
}

const char *WSTransport::GetServerName() const
{
	return m_sProfileProps.strServerPath.c_str();
}

HRESULT WSTransport::HrOpenMiscTable(ULONG ulTableType, ULONG ulFlags,
    ULONG cbEntryID, const ENTRYID *lpEntryID, ECMsgStore *lpMsgStore,
    WSTableView **lppTableView)
{
	if (ulTableType != TABLETYPE_STATS_SYSTEM && ulTableType != TABLETYPE_STATS_SESSIONS &&
	    ulTableType != TABLETYPE_STATS_USERS && ulTableType != TABLETYPE_STATS_COMPANY &&
	    ulTableType != TABLETYPE_USERSTORES && ulTableType != TABLETYPE_STATS_SERVERS)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = hrSuccess;
	object_ptr<WSTableMisc> lpMiscTable;

	hr = WSTableMisc::Create(ulTableType, ulFlags, m_ecSessionId,
	     cbEntryID, lpEntryID, lpMsgStore, this, &~lpMiscTable);
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
	soap_lock_guard spg(*this);
	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &eidMessage, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->setLockState(m_ecSessionId, eidMessage, bLocked, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		/* else: er is already set and good to use */
	}
	END_SOAP_CALL
 exitm:
	return hr;
}

HRESULT WSTransport::HrCheckCapabilityFlags(ULONG ulFlags, BOOL *lpbResult)
{
	if (lpbResult == NULL)
		return MAPI_E_INVALID_PARAMETER;
	*lpbResult = (m_ulServerCapabilities & ulFlags) == ulFlags;
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

	soap_lock_guard spg(*this);
    START_SOAP_CALL
    {
		if (m_lpCmd->testPerform(m_ecSessionId, szCommand, sTestPerform, &er) != SOAP_OK)
            er = KCERR_NETWORK_ERROR;
    }
    END_SOAP_CALL;
 exitm:
    return hr;
}

HRESULT WSTransport::HrTestSet(const char *szName, const char *szValue)
{
    HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
	soap_lock_guard spg(*this);

    START_SOAP_CALL
    {
		if (m_lpCmd->testSet(m_ecSessionId, szName, szValue, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
    }
    END_SOAP_CALL
 exitm:
    return hr;
}

HRESULT WSTransport::HrTestGet(const char *szName, char **lpszValue)
{
    HRESULT hr = hrSuccess;

    ECRESULT er = erSuccess;
    char *szValue = NULL;
    struct testGetResponse sResponse;
	soap_lock_guard spg(*this);

    START_SOAP_CALL
    {
		if (m_lpCmd->testGet(m_ecSessionId, szName, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
        else
                er = sResponse.er;
    }
    END_SOAP_CALL

	hr = MAPIAllocateBuffer(strlen(sResponse.szValue) + 1, reinterpret_cast<void **>(&szValue));
    if(hr != hrSuccess)
		goto exitm;

    strcpy(szValue, sResponse.szValue);

    *lpszValue = szValue;
 exitm:
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

const sGlobalProfileProps &WSTransport::GetProfileProps() const
{
    return m_sProfileProps;
}

HRESULT WSTransport::GetServerGUID(GUID *lpsServerGuid) const
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

static SOAP_SOCKET RefuseConnect(struct soap* soap, const char* endpoint, const char* host, int port)
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
	m_lpCmd->soap->fopen = RefuseConnect;

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
	soap_lock_guard spg(*this);

	if (m_lpCmd->notifyGetItems(m_ecSessionId, &sNotifications) != SOAP_OK)
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
	spg.unlock();
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

HRESULT WSTransport::HrResetFolderCount(ULONG cbEntryId,
    const ENTRYID *lpEntryId, ULONG *lpulUpdates)
{
	HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
	entryId eidFolder;
	resetFolderCountResponse sResponse;
	soap_lock_guard spg(*this);

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &eidFolder, true);
	if (hr != hrSuccess)
		goto exitm;

	START_SOAP_CALL
	{
		if (m_lpCmd->resetFolderCount(m_ecSessionId, eidFolder, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if (lpulUpdates)
		*lpulUpdates = sResponse.ulUpdates;

 exitm:
	return hr;
}
