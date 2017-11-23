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
#include <algorithm>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <cstdint>
#include <kopano/ECChannel.h>
#include <kopano/MAPIErrors.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include "ECDatabaseUtils.h"
#include "ECSessionManager.h"
#include "ECPluginFactory.h"
#include "ECDBDef.h"
#include "ECDatabaseUpdate.h"
#include "ECLicenseClient.h"
#include <kopano/ECGuid.h>
#include "ECLockManager.h"

#include "soapH.h"

#include <mutex>
#include <unordered_map>
#include <mapidefs.h>
#include <mapitags.h>
#include <sys/times.h>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <set>
#include <deque>
#include <algorithm>
#include <cstdio>

#include <kopano/ECTags.h>
#include <kopano/stringutil.h>
#include "SOAPUtils.h"
#include <kopano/kcodes.h>
#include "KCmd.nsmap"
#include "ECFifoBuffer.h"
#include "ECSerializer.h"
#include "StreamUtil.h"
#include <kopano/CommonUtil.h>
#include "StorageUtil.h"

#include "ics.h"

#include "kcore.hpp"
#include "pcutil.hpp"
#include "ECAttachmentStorage.h"
#include "ECGenProps.h"
#include "ECUserManagement.h"
#include "ECSecurity.h"
#include "ECICS.h"
#include "ECMultiStoreTable.h"
#include "ECStatsCollector.h"
#include "ECStringCompat.h"
#include "ECTPropsPurge.h"
#include "versions.h"
#include "ECTestProtocol.h"

#include <kopano/ECDefs.h>
#include <kopano/EMSAbTag.h>
#include <edkmdb.h>
#include <kopano/ecversion.h>
#include <kopano/mapiext.h>

#include "../server/ECSoapServerConnection.h"

#include "cmdutil.hpp"
#include <kopano/ECThreadPool.h>
#include "cmd.hpp"
#include "logontime.hpp"

#if defined(HAVE_GPERFTOOLS_MALLOC_EXTENSION_H)
#	include <gperftools/malloc_extension_c.h>
#	define HAVE_TCMALLOC 1
#elif defined(HAVE_GOOGLE_MALLOC_EXTENSION_H)
#	include <google/malloc_extension_c.h>
#	define HAVE_TCMALLOC 1
#endif

#define STRIN_FIX(s) (bSupportUnicode ? (s) : ECStringCompat::WTF1252_to_UTF8(soap, (s)))
#define STROUT_FIX(s) (bSupportUnicode ? (s) : ECStringCompat::UTF8_to_WTF1252(soap, (s)))
#define STROUT_FIX_CPY(s) (bSupportUnicode ? s_strcpy(soap, (s)) : ECStringCompat::UTF8_to_WTF1252(soap, (s)))

#define LOG_SOAP_DEBUG(_msg, ...) \
	ec_log(EC_LOGLEVEL_DEBUG | EC_LOGLEVEL_SOAP, "soap: " _msg, ##__VA_ARGS__)

using namespace KCHL;

namespace KC {

class ECFifoSerializer _kc_final : public ECSerializer {
	public:
	enum eMode { serialize, deserialize };

	ECFifoSerializer(ECFifoBuffer *lpBuffer, eMode mode);
	virtual ~ECFifoSerializer(void);
	virtual ECRESULT SetBuffer(void *) _kc_override;
	virtual ECRESULT Write(const void *ptr, size_t size, size_t nmemb) _kc_override;
	virtual ECRESULT Read(void *ptr, size_t size, size_t nmemb) _kc_override;
	virtual ECRESULT Skip(size_t size, size_t nmemb) _kc_override;
	virtual ECRESULT Flush(void) _kc_override;
	virtual ECRESULT Stat(ULONG *have_read, ULONG *have_written) _kc_override;

	private:
	ECFifoBuffer *m_lpBuffer;
	eMode m_mode;
	ULONG m_ulRead = 0, m_ulWritten = 0;
};

extern ECSessionManager*	g_lpSessionManager;
extern ECStatsCollector*	g_lpStatsCollector;

// Hold the status of the softdelete purge system
static bool g_bPurgeSoftDeleteStatus = FALSE;

static ECRESULT CreateEntryId(GUID guidStore, unsigned int ulObjType,
    entryId **lppEntryId)
{
	entryId*	lpEntryId = NULL;
	EID			eid;

	if (lppEntryId == NULL)
		return KCERR_INVALID_PARAMETER;
	if (CoCreateGuid(&eid.uniqueId) != hrSuccess)
		return KCERR_CALL_FAILED;

	eid.guid = guidStore;
	eid.usType = ulObjType;
	lpEntryId = s_alloc<entryId>(nullptr);
	lpEntryId->__size = sizeof(EID);
	lpEntryId->__ptr = s_alloc<unsigned char>(nullptr, lpEntryId->__size);
	memcpy(lpEntryId->__ptr, &eid, lpEntryId->__size);

	*lppEntryId = lpEntryId;
	return erSuccess;
}

/**
 * Get the local user id based on the entryid or the user id for old clients.
 *
 * When an entryid is provided, the extern id is extracted and the local user id
 * is resolved based on that. If no entryid is provided the provided legacy user id
 * is used as local user is and the extern id is resolved based on that. Old clients
 * that are not multi server aware provide the legacy user id in stead of the entryid.
 *
 * @param[in]	sUserId			The entryid of the user for which to obtain the local id
 * @param[in]	ulLegacyUserId	The legacy user id, which will be used as the entryid when.
 *								no entryid is provided (old clients).
 * @param[out]	lpulUserId		The local user id.
 * @param[out]	lpsExternId		The extern id of the user. This can be NULL if the extern id
 *								is not required by the caller.
 *
 * @retval	KCERR_INVALID_PARAMATER	One or more parameters are invalid.
 * @retval	KCERR_NOT_FOUND			The local is is not found.
 */
static ECRESULT GetLocalId(entryId sUserId, unsigned int ulLegacyUserId,
    unsigned int *lpulUserId, objectid_t *lpsExternId)
{
	ECRESULT er = erSuccess;
	unsigned int	ulUserId = 0;
	objectid_t		sExternId;
	objectdetails_t	sDetails;

	if (lpulUserId == NULL)
		return KCERR_INVALID_PARAMETER;

	// If no entryid is present, use the 'current' user.
	if (ulLegacyUserId == 0 && sUserId.__size == 0) {
		// When lpsExternId is requested, the 'current' user will not be
		// requested in this way. However, to make sure a caller does expect a result in the future 
		// we'll return an error in that case.
		if (lpsExternId != NULL)
			er = KCERR_INVALID_PARAMETER;
		else
			*lpulUserId = 0;

		// TODO: return value in lpulUserId ?
		return er;
	}

	if (sUserId.__ptr) {
		// Extract the information from the entryid.
		er = ABEntryIDToID(&sUserId, &ulUserId, &sExternId, NULL);
		if (er != erSuccess)
			return er;

		// If an extern id is present, we should get an object based on that.
		if (!sExternId.id.empty())
			er = g_lpSessionManager->GetCacheManager()->GetUserObject(sExternId, &ulUserId, NULL, NULL);
	} else {
		// use user id from 6.20 and older clients
		ulUserId = ulLegacyUserId;
		if (lpsExternId)
			er = g_lpSessionManager->GetCacheManager()->GetUserObject(ulLegacyUserId, &sExternId, NULL, NULL);
	}
	if (er != erSuccess)
		return er;

	*lpulUserId = ulUserId;
	if (lpsExternId)
		*lpsExternId = std::move(sExternId);
	return erSuccess;
}

/**
 * Check if a user has a store of a particular type on the local server.
 *
 * On a single server configuration this function will return true for
 * all ECSTORE_TYPE_PRIVATE and ECSTORE_TYPE_PUBLIC requests and false otherwise.
 *
 * In single tennant mode, requests for ECSTORE_TYPE_PUBLIC will always return true,
 * regardless of the server on which the public should exist. This is actually wrong
 * but is the same behaviour as before.
 *
 * @param[in]	lpecSession			The ECSession object for the current session.
 * @param[in]	ulUserId			The user id of the user for which to check if a
 *									store is available.
 * @param[in]	ulStoreType			The store type to check for.
 * @param[out]	lpbHasLocalStore	The boolean that will contain the result on success.
 *
 * @retval	KCERR_INVALID_PARAMETER	One or more parameters are invalid.
 * @retval	KCERR_NOT_FOUND			The user specified by ulUserId was not found.
 */
static ECRESULT CheckUserStore(ECSession *lpecSession, unsigned ulUserId,
    unsigned ulStoreType, bool *lpbHasLocalStore)
{
	ECRESULT er;
	objectdetails_t	sDetails;
	bool bPrivateOrPublic;

	if (lpecSession == NULL || lpbHasLocalStore == NULL || !ECSTORE_TYPE_ISVALID(ulStoreType))
		return KCERR_INVALID_PARAMETER;

	bPrivateOrPublic = (ulStoreType == ECSTORE_TYPE_PRIVATE || ulStoreType == ECSTORE_TYPE_PUBLIC);

	if (g_lpSessionManager->IsDistributedSupported()) {
        er = lpecSession->GetUserManagement()->GetObjectDetails(ulUserId, &sDetails);
		if (er != erSuccess)
			return er;

		if (bPrivateOrPublic) {
			// @todo: Check if there's a define or constant for everyone.
			if (ulUserId == 2)	// Everyone, public in single tennant
				*lpbHasLocalStore = true;
			else
				*lpbHasLocalStore = (strcasecmp(sDetails.GetPropString(OB_PROP_S_SERVERNAME).c_str(), g_lpSessionManager->GetConfig()->GetSetting("server_name")) == 0);
		} else	// Archive store
			*lpbHasLocalStore = sDetails.PropListStringContains((property_key_t)PR_EC_ARCHIVE_SERVERS_A, g_lpSessionManager->GetConfig()->GetSetting("server_name"), true);
	} else	// Single tennant
		*lpbHasLocalStore = bPrivateOrPublic;

	return erSuccess;
}

static ECRESULT GetABEntryID(unsigned int ulUserId, soap *lpSoap,
    entryId *lpUserId)
{
	ECRESULT er;
	entryId sUserId;
	objectid_t			sExternId;

	if (lpSoap == NULL)
		return KCERR_INVALID_PARAMETER;
	if (ulUserId == KOPANO_UID_SYSTEM) {
		sExternId.objclass = ACTIVE_USER;
	} else if (ulUserId == KOPANO_UID_EVERYONE) {
		sExternId.objclass = DISTLIST_SECURITY;
	} else {
		er = g_lpSessionManager->GetCacheManager()->GetUserObject(ulUserId, &sExternId, NULL, NULL);
		if (er != erSuccess)
			return er;
	}

	er = ABIDToEntryID(lpSoap, ulUserId, sExternId, &sUserId);
	if (er != erSuccess)
		return er;

	*lpUserId = std::move(sUserId); // pointer (__ptr) is copied, not data
	return erSuccess;
}

static ECRESULT PeerIsServer(struct soap *soap,
    const std::string &strServerName, const std::string &strHttpPath,
    const std::string &strSslPath, bool *lpbResult)
{
	bool			bResult = false;

	if (soap == NULL || lpbResult == NULL)
		return KCERR_INVALID_PARAMETER;

	// First check if we are connecting through Unix socket/named pipe and if the request URL matches this server
	if (SOAP_CONNECTION_TYPE_NAMED_PIPE(soap) &&
	    strcasecmp(strServerName.c_str(), g_lpSessionManager->GetConfig()->GetSetting("server_name")) == 0) {
		*lpbResult = true;
		return hrSuccess;
	}

	const std::string *lpstrPath = &strHttpPath;
	if (lpstrPath->empty())
		lpstrPath = &strSslPath;
	if (lpstrPath->empty()) {
		*lpbResult = false;
		return erSuccess;
	}

	std::string strHost;
	std::string::size_type ulHostStart = 0;
	std::string::size_type ulHostEnd = 0;
	struct addrinfo	sHint = {0};
	struct addrinfo *lpsAddrInfo = NULL;
	struct addrinfo *lpsAddrIter = NULL;

	ulHostStart = lpstrPath->find("://");
	if (ulHostStart == std::string::npos)
		return KCERR_INVALID_PARAMETER;
	ulHostStart += 3;	// Skip the '://'
	ulHostEnd = lpstrPath->find(':', ulHostStart);
	if (ulHostEnd == std::string::npos)
		return KCERR_INVALID_PARAMETER;
	strHost = lpstrPath->substr(ulHostStart, ulHostEnd - ulHostStart);
	sHint.ai_family = AF_UNSPEC;
	sHint.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(strHost.c_str(), NULL, &sHint, &lpsAddrInfo) != 0)
		return KCERR_NOT_FOUND;

	for (lpsAddrIter = lpsAddrInfo; lpsAddrIter != nullptr && !bResult; lpsAddrIter = lpsAddrIter->ai_next) {
		if (soap->peerlen < sizeof(sockaddr) ||
		    lpsAddrIter->ai_family != reinterpret_cast<const struct sockaddr *>(&soap->peer)->sa_family)
			continue;
		switch (lpsAddrIter->ai_family) {
		case AF_INET:
		{
			auto lpsLeft = reinterpret_cast<struct sockaddr_in *>(lpsAddrIter->ai_addr);
			auto lpsRight = reinterpret_cast<struct sockaddr_in *>(&soap->peer);
			bResult = (memcmp(&lpsLeft->sin_addr, &lpsRight->sin_addr, sizeof(lpsLeft->sin_addr)) == 0);
			break;
		}
		case AF_INET6:
		{
			auto lpsLeft = reinterpret_cast<struct sockaddr_in6 *>(lpsAddrIter->ai_addr);
			auto lpsRight = reinterpret_cast<struct sockaddr_in6 *>(&soap->peer);
			bResult = (memcmp(&lpsLeft->sin6_addr, &lpsRight->sin6_addr, sizeof(lpsLeft->sin6_addr)) == 0);
			break;
		}
		default:
			break;
		}
	}
	freeaddrinfo(lpsAddrInfo);
	*lpbResult = bResult;
	return erSuccess;
}

ECFifoSerializer::ECFifoSerializer(ECFifoBuffer *lpBuffer, eMode mode) :
	m_mode(mode)
{
	SetBuffer(lpBuffer);
}

ECFifoSerializer::~ECFifoSerializer(void)
{
	if (m_lpBuffer == nullptr)
		return;
	ECFifoBuffer::close_flags flags = (m_mode == serialize ? ECFifoBuffer::cfWrite : ECFifoBuffer::cfRead);
	m_lpBuffer->Close(flags);
}

ECRESULT ECFifoSerializer::SetBuffer(void *lpBuffer)
{
	m_lpBuffer = static_cast<ECFifoBuffer *>(lpBuffer);
	return erSuccess;
}

ECRESULT ECFifoSerializer::Write(const void *ptr, size_t size, size_t nmemb)
{
	ECRESULT er = erSuccess;
	union {
		char c[8];
		short s;
		int i;
		long long ll;
	} tmp;

	if (m_mode != serialize)
		return KCERR_NO_SUPPORT;
	if (ptr == nullptr)
		return KCERR_INVALID_PARAMETER;

	switch (size) {
	case 1:
		er = m_lpBuffer->Write(ptr, nmemb, STR_DEF_TIMEOUT, NULL);
		break;
	case 2:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.s = htons(static_cast<const short *>(ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, STR_DEF_TIMEOUT, nullptr);
		}
		break;
	case 4:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.i = htonl(static_cast<const int *>(ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, STR_DEF_TIMEOUT, nullptr);
		}
		break;
	case 8:
		for (size_t x = 0; x < nmemb && er == erSuccess; ++x) {
			tmp.ll = cpu_to_be64(static_cast<const uint64_t *>(ptr)[x]);
			er = m_lpBuffer->Write(&tmp, size, STR_DEF_TIMEOUT, nullptr);
		}
		break;
	default:
		er = KCERR_INVALID_PARAMETER;
		break;
	}
	m_ulWritten += size * nmemb;
	return er;
}

ECRESULT ECFifoSerializer::Read(void *ptr, size_t size, size_t nmemb)
{
	ECRESULT er;
	ECFifoBuffer::size_type cbRead = 0;

	if (m_mode != deserialize)
		return KCERR_NO_SUPPORT;
	if (ptr == nullptr)
		return KCERR_INVALID_PARAMETER;
	er = m_lpBuffer->Read(ptr, size * nmemb, STR_DEF_TIMEOUT, &cbRead);
	if (er != erSuccess)
		return er;
	m_ulRead += cbRead;
	if (cbRead != size * nmemb)
		return KCERR_CALL_FAILED;

	switch (size) {
	case 1: break;
	case 2:
		for (size_t x = 0; x < nmemb; ++x) {
			uint16_t tmp;
			memcpy(&tmp, static_cast<uint16_t *>(ptr) + x, sizeof(tmp));
			tmp = ntohs(tmp);
			memcpy(static_cast<uint16_t *>(ptr) + x, &tmp, sizeof(tmp));
		}
		break;
	case 4:
		for (size_t x = 0; x < nmemb; ++x) {
			uint32_t tmp;
			memcpy(&tmp, static_cast<uint32_t *>(ptr) + x, sizeof(tmp));
			tmp = ntohl(tmp);
			memcpy(static_cast<uint32_t *>(ptr) + x, &tmp, sizeof(tmp));
		}
		break;
	case 8:
		for (size_t x = 0; x < nmemb; ++x) {
			uint64_t tmp;
			memcpy(&tmp, static_cast<uint64_t *>(ptr) + x, sizeof(tmp));
			tmp = be64_to_cpu(tmp);
			memcpy(static_cast<uint64_t *>(ptr) + x, &tmp, sizeof(tmp));
		}
		break;
	default:
		er = KCERR_INVALID_PARAMETER;
		break;
	}
	return er;
}

ECRESULT ECFifoSerializer::Skip(size_t size, size_t nmemb)
{
	std::unique_ptr<char[]> buf(new(std::nothrow) char[size*nmemb]);
	if (buf == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	return Read(buf.get(), size, nmemb);
}

ECRESULT ECFifoSerializer::Flush()
{
	ECRESULT er;
	size_t cbRead = 0;
	char buf[16384];

	while (true) {
		er = m_lpBuffer->Read(buf, sizeof(buf), STR_DEF_TIMEOUT, &cbRead);
		if (er != erSuccess)
			return er;
		m_ulRead += cbRead;
		if (cbRead < sizeof(buf))
			break;
	}
	return er;
}

ECRESULT ECFifoSerializer::Stat(ULONG *lpcbRead, ULONG *lpcbWrite)
{
	if (lpcbRead != nullptr)
		*lpcbRead = m_ulRead;
	if (lpcbWrite != nullptr)
		*lpcbWrite = m_ulWritten;
	return erSuccess;
}

/**
 * Get the best server path for a server
 *
 * This function will return the 'best' server path to redirect the client to. This is
 * done by examining the existing incoming connection, and choosing an appropriate access
 * method to the given server. Rules are as follows:
 *
 * - If bProxy is TRUE and the destination server has a proxy address, return the proxy address
 * - If bProxy is FALSE:
 *   - If existing connection is HTTP, return first available of: HTTP, HTTPS
 *   - If existing connection is HTTPS, return HTTPS
 *   - If existing connection is FILE, return first available of: (FILE,) HTTPS, HTTP
 *     (Only ever outputs FILE if it turns out we point to ourselves)
 *
 * @param[in] soap SOAP structure for incoming request
 * @param[in] lpecSession Session for the request
 * @param[in] strServerName Server name to get path for
 * @param[in] bProxy TRUE if we are requesting the proxy address for a server
 * @param[out] lpstrServerPath Output path of server (URL)
 * @return result
 */
ECRESULT GetBestServerPath(struct soap *soap, ECSession *lpecSession, const std::string &strServerName, std::string *lpstrServerPath)
{
	ECRESULT er;
	std::string	strServerPath;
	bool		bConnectPipe = false;
	serverdetails_t	sServerDetails;
	std::string		strFilePath;
	std::string		strHttpPath;
	std::string		strSslPath;
	std::string		strProxyPath;
	const char *szProxyHeader = lpecSession->GetSessionManager()->GetConfig()->GetSetting("proxy_header");

	if (soap == NULL || soap->user == NULL || lpstrServerPath == NULL)
		return KCERR_INVALID_PARAMETER;
	
	auto lpInfo = soap_info(soap);
	er = lpecSession->GetUserManagement()->GetServerDetails(strServerName, &sServerDetails);
	if (er != erSuccess)
		return er;

    strProxyPath = sServerDetails.GetProxyPath();
	strFilePath = sServerDetails.GetFilePath();
	strHttpPath = sServerDetails.GetHttpPath();
	strSslPath = sServerDetails.GetSslPath();

	// Always redirect if proxy_header is "*"
    if (!strcmp(szProxyHeader, "*") || lpInfo->bProxy) {
        if(!strProxyPath.empty()) {
			*lpstrServerPath = std::move(strProxyPath);
			return erSuccess;
        } else {
            ec_log_warn("Proxy path not set for server \"%s\"! falling back to direct address.", strServerName.c_str());
        }
    }    

	if (!strFilePath.empty())
	{
		er = PeerIsServer(soap, strServerName, strHttpPath, strSslPath, &bConnectPipe);
		if (er != erSuccess)
			return er;
	} else {
		// TODO: check if same server, and set strFilePath 'cause it's known

		bConnectPipe = false;
	}

	if (bConnectPipe)
		strServerPath = strFilePath;
	else
		switch (SOAP_CONNECTION_TYPE(soap))
		{
		case CONNECTION_TYPE_TCP:
			if (!strHttpPath.empty())
				strServerPath = strHttpPath;
			else if (!strSslPath.empty())
				strServerPath = strSslPath;
			break;

		case CONNECTION_TYPE_SSL:
			if (!strSslPath.empty())
				strServerPath = strSslPath;
			break;

		case CONNECTION_TYPE_NAMED_PIPE:
		case CONNECTION_TYPE_NAMED_PIPE_PRIORITY:
			if (!strSslPath.empty())
				strServerPath = strSslPath;
			else if (!strHttpPath.empty())
				strServerPath = strHttpPath;
			break;
		}

	if (strServerPath.empty())
		return KCERR_NOT_FOUND;
	*lpstrServerPath = std::move(strServerPath);
	return erSuccess;
}

} /* namespace */

// exception: This function does internal Begin + Commit/Rollback
static ECRESULT MoveObjects(ECSession *lpSession, ECDatabase *lpDatabase, ECListInt* lplObjectIds, unsigned int ulDestFolderId, unsigned int ulSyncId);
// these functions don't do Begin + Commit/Rollback
static ECRESULT WriteProps(struct soap *soap, ECSession *lpecSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, struct saveObject *lpsSaveObj, unsigned int ulObjId, bool fNewItem, unsigned int ulSyncId, struct saveObject *lpsReturnObj, bool *lpfHaveChangeKey, FILETIME *ftCreated, FILETIME *ftModified);
static ECRESULT DoNotifySubscribe(ECSession *lpecSession, unsigned long long ulSessionId, struct notifySubscribe *notifySubscribe);

using steady_clock = std::chrono::steady_clock;

/**
 * logon: log on and create a session with provided credentials
 */
int ns__logon(struct soap *soap, const char *user, const char *pass,
    const char *impersonate, const char *clientVersion, unsigned int clientCaps,
    unsigned int logonFlags, struct xsd__base64Binary sLicenseRequest,
    ULONG64 ullSessionGroup, const char *szClientApp,
    const char *szClientAppVersion, const char *szClientAppMisc,
    struct logonResponse *lpsResponse)
{
	ECRESULT	er = erSuccess;
	ECSession	*lpecSession = NULL;
	ECSESSIONID	sessionID = 0;
	GUID		sServerGuid = {0};
	struct timespec startTimes = {0}, endTimes = {0};
	auto dblStart = steady_clock::now();

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &startTimes);

	LOG_SOAP_DEBUG("%020llu: S logon", static_cast<unsigned long long>(sessionID));

    if ((clientCaps & KOPANO_CAP_UNICODE) == 0) {
		user = ECStringCompat::WTF1252_to_UTF8(soap, user);
		pass = ECStringCompat::WTF1252_to_UTF8(soap, pass);
		clientVersion = ECStringCompat::WTF1252_to_UTF8(soap, clientVersion);
		szClientApp = ECStringCompat::WTF1252_to_UTF8(soap, szClientApp);
	}

	lpsResponse->lpszVersion = const_cast<char *>("0," PROJECT_VERSION_COMMIFIED);
	lpsResponse->ulCapabilities = KOPANO_LATEST_CAPABILITIES;
	/*
	 * Client desires compression, so turn it on, but only if remote.
	 * Otherwise, clear the flag from clientCaps, because
	 * Create(Auth)Session remembers them, re-evaluates CAP_COMPRESSION,
	 * and would otherwise turn on compression again.
	 */
	if (zcp_peerfd_is_local(soap->socket) <= 0 && (clientCaps & KOPANO_CAP_COMPRESSION)) {
		lpsResponse->ulCapabilities |= KOPANO_CAP_COMPRESSION;
		// (ECSessionManager::ValidateSession() will do this for all other functions)
		soap_set_imode(soap, SOAP_ENC_ZLIB);	// also autodetected
		soap_set_omode(soap, SOAP_ENC_ZLIB | SOAP_IO_CHUNK);
	} else {
		clientCaps &= ~KOPANO_CAP_COMPRESSION;
	}

	// check username and password
	er = g_lpSessionManager->CreateSession(soap, user, pass, impersonate, clientVersion, szClientApp, szClientAppVersion, szClientAppMisc, clientCaps, ullSessionGroup, &sessionID, &lpecSession, true, (logonFlags & KOPANO_LOGON_NO_UID_AUTH) == 0, (logonFlags & KOPANO_LOGON_NO_REGISTER_SESSION) == 0);
	if(er != erSuccess){
		er = KCERR_LOGON_FAILED;
		goto exit;
	}

	// We allow Zarafa >=6 clients to connect to a Kopano server. However, anything below that will be
	// denied. We can't say what future clients may or may not be capable of. So we'll leave that to the
	// clients.
	if (lpecSession && (KOPANO_COMPARE_VERSION_TO_GENERAL(lpecSession->ClientVersion(), MAKE_KOPANO_GENERAL(6)) < 0)) {
		ec_log_warn("Rejected logon attempt from a %s version client.", clientVersion ? clientVersion : "<unknown>");
		er = KCERR_INVALID_VERSION;
		goto exit;
	}

	lpsResponse->ulSessionId = sessionID;
	if (clientCaps & KOPANO_CAP_MULTI_SERVER)
		lpsResponse->ulCapabilities |= KOPANO_CAP_MULTI_SERVER;
		
	if (clientCaps & KOPANO_CAP_ENHANCED_ICS && parseBool(g_lpSessionManager->GetConfig()->GetSetting("enable_enhanced_ics"))) {
		lpsResponse->ulCapabilities |= KOPANO_CAP_ENHANCED_ICS;
		soap_set_omode(soap, SOAP_ENC_MTOM | SOAP_IO_CHUNK);
		soap_set_imode(soap, SOAP_ENC_MTOM);
		soap_post_check_mime_attachments(soap);
	}

	if (clientCaps & KOPANO_CAP_UNICODE)
		lpsResponse->ulCapabilities |= KOPANO_CAP_UNICODE;

	if (clientCaps & KOPANO_CAP_MSGLOCK)
		lpsResponse->ulCapabilities |= KOPANO_CAP_MSGLOCK;

	er = g_lpSessionManager->GetServerGUID(&sServerGuid);
	if (er != erSuccess)
		goto exit;

	lpsResponse->sServerGuid.__ptr = (unsigned char*)s_memcpy(soap, (char*)&sServerGuid, sizeof(sServerGuid));
	lpsResponse->sServerGuid.__size = sizeof(sServerGuid);

    // Only save logon if credentials were supplied by the user; otherwise the logon is probably automated
    if (lpecSession && (lpecSession->GetAuthMethod() == ECSession::METHOD_USERPASSWORD || lpecSession->GetAuthMethod() == ECSession::METHOD_SSO))
		record_logon_time(lpecSession, true);
	
exit:
	if (lpecSession)
		lpecSession->Unlock();

	lpsResponse->er = er;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endTimes);

	LOG_SOAP_DEBUG("%020llu: E logon 0x%08x %f %f",
		static_cast<unsigned long long>(sessionID), er,
		timespec2dbl(endTimes) - timespec2dbl(startTimes),
		dur2dbl(decltype(dblStart)::clock::now() - dblStart));
	return SOAP_OK;
}

/**
 * logon: log on and create a session with provided credentials
 */
int ns__ssoLogon(struct soap *soap, ULONG64 ulSessionId, const char *szUsername,
    const char *szImpersonateUser, struct xsd__base64Binary *lpInput,
    const char *szClientVersion, unsigned int clientCaps,
    struct xsd__base64Binary sLicenseRequest, ULONG64 ullSessionGroup,
    const char *szClientApp, const char *szClientAppVersion,
    const char *szClientAppMisc, struct ssoLogonResponse *lpsResponse)
{
	ECRESULT		er = KCERR_LOGON_FAILED;
	ECAuthSession	*lpecAuthSession = NULL;
	ECSession		*lpecSession = NULL;
	ECSESSIONID		newSessionID = 0;
	GUID			sServerGuid = {0};
	xsd__base64Binary *lpOutput = NULL;
	const char *lpszEnabled = NULL;
	struct timespec startTimes = {0}, endTimes = {0};
	auto dblStart = steady_clock::now();

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &startTimes);

	LOG_SOAP_DEBUG("%020" PRIu64 ": S ssoLogon", ulSessionId);

	if (!lpInput || lpInput->__size == 0 || lpInput->__ptr == NULL || !szUsername || !szClientVersion)
		goto exit;

	lpszEnabled = g_lpSessionManager->GetConfig()->GetSetting("enable_sso");
	if (!(lpszEnabled && strcasecmp(lpszEnabled, "yes") == 0))
		goto nosso;
	lpsResponse->lpszVersion = const_cast<char *>("0," PROJECT_VERSION_COMMIFIED);
	lpsResponse->ulCapabilities = KOPANO_LATEST_CAPABILITIES;

	/* See ns__logon for comments. */
	if (zcp_peerfd_is_local(soap->socket) <= 0 && (clientCaps & KOPANO_CAP_COMPRESSION)) {
		lpsResponse->ulCapabilities |= KOPANO_CAP_COMPRESSION;
		// (ECSessionManager::ValidateSession() will do this for all other functions)
		soap_set_imode(soap, SOAP_ENC_ZLIB);	// also autodetected
		soap_set_omode(soap, SOAP_ENC_ZLIB | SOAP_IO_CHUNK);
	} else {
		clientCaps &= ~KOPANO_CAP_COMPRESSION;
	}

	if (ulSessionId == 0) {
		// new auth session
		er = g_lpSessionManager->CreateAuthSession(soap, clientCaps, &newSessionID, &lpecAuthSession, true, true);
		if (er != erSuccess) {
			er = KCERR_LOGON_FAILED;
			goto exit;
		}
		// when the first validate fails, remove the correct sessionid
		ulSessionId = newSessionID;
	} else {
		er = g_lpSessionManager->ValidateSession(soap, ulSessionId, &lpecAuthSession, true);
		if (er != erSuccess)
			goto exit;
	}

	lpecAuthSession->SetClientMeta(szClientAppVersion, szClientAppMisc);

    if ((clientCaps & KOPANO_CAP_UNICODE) == 0) {
		szUsername = ECStringCompat::WTF1252_to_UTF8(soap, szUsername);
		szClientVersion = ECStringCompat::WTF1252_to_UTF8(soap, szClientVersion);
		szClientApp = ECStringCompat::WTF1252_to_UTF8(soap, szClientApp);
	}

	er = lpecAuthSession->ValidateSSOData(soap, szUsername, szImpersonateUser, szClientVersion, szClientApp, szClientAppVersion, szClientAppMisc, lpInput, &lpOutput);
	if (er == KCERR_SSO_CONTINUE) {
		// continue validation exchange
		lpsResponse->lpOutput = lpOutput;
	} else if (er == erSuccess) {
		// done and logged in

		// create ecsession from ecauthsession, and place in session map
		er = g_lpSessionManager->RegisterSession(lpecAuthSession, ullSessionGroup, szClientVersion, szClientApp, szClientAppVersion, szClientAppMisc, &newSessionID, &lpecSession, true);
		if (er != erSuccess) {
			ec_log_err("User authenticated, but failed to create session. Error 0x%08X", er);
			goto exit;
		}

	// We allow Zarafa >=6 clients to connect to a Kopano server. However, anything below that will be
	// denied. We can't say what future clients may or may not be capable of. So we'll leave that to the
	// clients.
	if (KOPANO_COMPARE_VERSION_TO_GENERAL(lpecSession->ClientVersion(), MAKE_KOPANO_GENERAL(6)) < 0) {
		ec_log_warn("Rejected logon attempt from a %s version client.", szClientVersion ? szClientVersion : "<unknown>");
		er = KCERR_INVALID_VERSION;
		goto exit;
	}

		// delete authsession
		lpecAuthSession->Unlock();
		
		g_lpSessionManager->RemoveSession(ulSessionId);
		lpecAuthSession = NULL;

		// return ecsession number
		lpsResponse->lpOutput = NULL;

	} else {
		// delete authsession
		lpecAuthSession->Unlock();

		g_lpSessionManager->RemoveSession(ulSessionId);
		lpecAuthSession = NULL;

		er = KCERR_LOGON_FAILED;
		goto exit;
	}

	lpsResponse->ulSessionId = newSessionID;
	if (clientCaps & KOPANO_CAP_MULTI_SERVER)
		lpsResponse->ulCapabilities |= KOPANO_CAP_MULTI_SERVER;

	if (clientCaps & KOPANO_CAP_ENHANCED_ICS && parseBool(g_lpSessionManager->GetConfig()->GetSetting("enable_enhanced_ics"))) {
		lpsResponse->ulCapabilities |= KOPANO_CAP_ENHANCED_ICS;
		soap_set_omode(soap, SOAP_ENC_MTOM | SOAP_IO_CHUNK);
		soap_set_imode(soap, SOAP_ENC_MTOM);
		soap_post_check_mime_attachments(soap);
	}

	if (clientCaps & KOPANO_CAP_UNICODE)
		lpsResponse->ulCapabilities |= KOPANO_CAP_UNICODE;

	if (clientCaps & KOPANO_CAP_MSGLOCK)
		lpsResponse->ulCapabilities |= KOPANO_CAP_MSGLOCK;

    if(er != KCERR_SSO_CONTINUE) {
        // Don't reset er to erSuccess on SSO_CONTINUE, we don't need the server guid yet
    	er = g_lpSessionManager->GetServerGUID(&sServerGuid);
    	if (er != erSuccess)
    		goto exit;
    
    	lpsResponse->sServerGuid.__ptr = (unsigned char*)s_memcpy(soap, (char*)&sServerGuid, sizeof(sServerGuid));
    	lpsResponse->sServerGuid.__size = sizeof(sServerGuid);
    }
    
    if(lpecSession && (lpecSession->GetAuthMethod() == ECSession::METHOD_USERPASSWORD || lpecSession->GetAuthMethod() == ECSession::METHOD_SSO))
		record_logon_time(lpecSession, true);

exit:
	if (lpecAuthSession != NULL)
		lpecAuthSession->Unlock();
	if (lpecSession)
		lpecSession->Unlock();
        
	if (er == erSuccess)
		g_lpStatsCollector->Increment(SCN_LOGIN_SSO);
	else if (er != KCERR_SSO_CONTINUE)
		g_lpStatsCollector->Increment(SCN_LOGIN_DENIED);

nosso:
	lpsResponse->er = er;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endTimes);

	LOG_SOAP_DEBUG("%020" PRIu64 ": E ssoLogon 0x%08x %f %f",
		ulSessionId, er, timespec2dbl(endTimes) - timespec2dbl(startTimes),
		dur2dbl(decltype(dblStart)::clock::now() - dblStart));
	return SOAP_OK;
}

/**
 * logoff: invalidate the session and close all notifications and memory held by the session
 */
int ns__logoff(struct soap *soap, ULONG64 ulSessionId, unsigned int *result)
{
	ECRESULT	er = erSuccess;
	ECSession 	*lpecSession = NULL;
	struct timespec startTimes = {0}, endTimes = {0};
	auto dblStart = steady_clock::now();

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &startTimes);
	LOG_SOAP_DEBUG("%020" PRIu64 ": S logoff", ulSessionId);
	er = g_lpSessionManager->ValidateSession(soap, ulSessionId, &lpecSession, true);
	if(er != erSuccess)
		goto exit;

	if (lpecSession->GetAuthMethod() == ECSession::METHOD_USERPASSWORD ||
	    lpecSession->GetAuthMethod() == ECSession::METHOD_SSO)
		record_logon_time(lpecSession, false);

	lpecSession->Unlock();

    // lpecSession is discarded. It is not locked, so we can do that. We only did the 'validatesession'
    // call to see if the session id existed in the first place, and the request is coming from the correct
    // IP address. Another logoff() call called at the same time may remove the session *here*, in which case the following call
    // will fail. This makes sure people can't terminate each others sessions unless they have the same source IP.

	er = g_lpSessionManager->RemoveSession(ulSessionId);
exit:
    *result = er;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endTimes);
	LOG_SOAP_DEBUG("%020" PRIu64 ": E logoff 0x%08x %f %f",
		ulSessionId, 0, timespec2dbl(endTimes) - timespec2dbl(startTimes),
		dur2dbl(decltype(dblStart)::clock::now() - dblStart));
	return SOAP_OK;
}

#define SOAP_ENTRY_FUNCTION_HEADER(resultvar, fname) \
    ECRESULT		er = erSuccess; \
    struct timespec	startTimes = {0}, endTimes = {0};	\
	auto dblStart = steady_clock::now(); \
    ECSession		*lpecSession = NULL; \
    unsigned int 	*lpResultVar = &resultvar; \
	const char *szFname = #fname; \
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &startTimes); \
	LOG_SOAP_DEBUG("%020" PRIu64 ": S %s", ulSessionId, szFname); \
	er = g_lpSessionManager->ValidateSession(soap, ulSessionId, &lpecSession, true);\
	const bool bSupportUnicode = (er == erSuccess ? (lpecSession->GetCapabilities() & KOPANO_CAP_UNICODE) != 0 : false); \
	const ECStringCompat stringCompat(bSupportUnicode); \
	if(er != erSuccess) \
		goto __soapentry_exit; \
	soap_info(soap)->ulLastSessionId = ulSessionId; \
	soap_info(soap)->szFname = szFname; \
	lpecSession->AddBusyState(pthread_self(), #fname, soap_info(soap)->threadstart, soap_info(soap)->start);

#define SOAP_ENTRY_FUNCTION_FOOTER \
__soapentry_exit: \
    *lpResultVar = er; \
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endTimes); \
    if(lpecSession) { \
		LOG_SOAP_DEBUG("%020" PRIu64 ": E %s 0x%08x %f %f", ulSessionId, szFname, er, timespec2dbl(endTimes) - timespec2dbl(startTimes), \
			dur2dbl(decltype(dblStart)::clock::now() - dblStart)); \
		lpecSession->UpdateBusyState(pthread_self(), SESSION_STATE_SENDING); \
        lpecSession->Unlock(); \
    } \
    return SOAP_OK;

#define SOAP_ENTRY_START(fname,resultvar,...) \
int ns__##fname(struct soap *soap, ULONG64 ulSessionId, ##__VA_ARGS__) \
{ \
    SOAP_ENTRY_FUNCTION_HEADER(resultvar, fname) \
    er = [&]() -> int {

#define SOAP_ENTRY_END() \
        return er; \
    }(); \
    SOAP_ENTRY_FUNCTION_FOOTER \
}

#define ALLOC_DBRESULT() \
	DB_ROW 			UNUSED_VAR		lpDBRow = NULL; \
	DB_LENGTHS		UNUSED_VAR		lpDBLen = NULL; \
	DB_RESULT UNUSED_VAR lpDBResult; \
	std::string		UNUSED_VAR		strQuery;

#define USE_DATABASE() \
       ECDatabase*             lpDatabase = NULL; \
       ALLOC_DBRESULT(); \
    \
       er = lpecSession->GetDatabase(&lpDatabase); \
       if (er != erSuccess) { \
		ec_log_err(" GetDatabase failed"); \
               return KCERR_DATABASE_ERROR; \
       }

#define ROLLBACK_ON_ERROR() \
	if (lpDatabase && FAILED(er)) \
		lpDatabase->Rollback(); \

static ECRESULT PurgeSoftDelete(ECSession *lpecSession,
    unsigned int ulLifetime, unsigned int *lpulMessages,
    unsigned int *lpulFolders, unsigned int *lpulStores, bool *lpbExit)
{
	ECRESULT 		er = erSuccess;
	ECDatabase*		lpDatabase = NULL;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow = NULL;
	std::string		strQuery;
	FILETIME		ft;
	unsigned int	ulDeleteFlags = 0;
	time_t			ulTime = 0;
	ECListInt		lObjectIds;
	unsigned int	ulFolders = 0, ulMessages = 0;
	unsigned int	ulStores = 0;
	bool 			bExitDummy = false;

	if (g_bPurgeSoftDeleteStatus) {
		ec_log_err("Softdelete already running");
		er = KCERR_BUSY;
		goto exit;
	}
	
	g_bPurgeSoftDeleteStatus = TRUE;

	if (!lpbExit)
		lpbExit = &bExitDummy;

	er = lpecSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		goto exit;

	// Although it doesn't make sense for the message deleter to include EC_DELETE_FOLDERS, it doesn't hurt either, since they shouldn't be there
	// and we really want to delete all the softdeleted items anyway.
	ulDeleteFlags = EC_DELETE_CONTAINER | EC_DELETE_FOLDERS | EC_DELETE_MESSAGES | EC_DELETE_RECIPIENTS | EC_DELETE_ATTACHMENTS | EC_DELETE_HARD_DELETE;

	GetSystemTimeAsFileTime(&ft);
	FileTimeToUnixTime(ft, &ulTime);

	ulTime -= ulLifetime;

	UnixTimeToFileTime(ulTime, &ft);

	// Select softdeleted stores (ignore softdelete_lifetime setting because a store can't be restored anyway)
	strQuery = "SELECT id FROM hierarchy WHERE parent IS NULL AND (flags&"+stringify(MSGFLAG_DELETED)+")="+stringify(MSGFLAG_DELETED)+" AND type="+stringify(MAPI_STORE);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	ulStores = lpDBResult.get_num_rows();
	if(ulStores > 0)
	{
		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			if(lpDBRow == NULL || lpDBRow[0] == NULL)
				continue;
			lObjectIds.emplace_back(atoui(lpDBRow[0]));
		}
		// free before we call DeleteObjects()
		lpDBResult = DB_RESULT();
		if (*lpbExit) {
			er = KCERR_USER_CANCEL;
			goto exit;
		}

		ec_log_info("Start to purge %d stores", (int)lObjectIds.size());

		for (auto iterObjectId = lObjectIds.cbegin();
		     iterObjectId != lObjectIds.cend() && !*lpbExit;
		     ++iterObjectId)
		{
			ec_log_info(" purge store (%d)", *iterObjectId);

			er = DeleteObjects(lpecSession, lpDatabase, *iterObjectId, ulDeleteFlags|EC_DELETE_STORE, 0, false, false);
			if(er != erSuccess) {
				ec_log_err("Error while removing softdelete store objects, error code: 0x%x.", er);
				goto exit;
			}
		}

		ec_log_info("Store purge done");

	}
	if (*lpbExit) {
		er = KCERR_USER_CANCEL;
		goto exit;
	}

	// Select softdeleted folders
	strQuery = "SELECT h.id FROM hierarchy AS h JOIN properties AS p ON p.hierarchyid=h.id AND p.tag="+stringify(PROP_ID(PR_DELETED_ON))+" AND p.type="+stringify(PROP_TYPE(PR_DELETED_ON))+" WHERE (h.flags&"+stringify(MSGFLAG_DELETED)+")="+stringify(MSGFLAG_DELETED)+" AND p.val_hi<="+stringify(ft.dwHighDateTime)+" AND h.type="+stringify(MAPI_FOLDER);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	ulFolders = lpDBResult.get_num_rows();
	if(ulFolders > 0)
	{
		// Remove all items
		lObjectIds.clear();
		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			if(lpDBRow == NULL || lpDBRow[0] == NULL)
				continue;
			lObjectIds.emplace_back(atoui(lpDBRow[0]));
		}
		// free before we call DeleteObjects()
		lpDBResult = DB_RESULT();
		if (*lpbExit) {
			er = KCERR_USER_CANCEL;
			goto exit;
		}

		ec_log_info("Start to purge %d folders", (int)lObjectIds.size());

		er = DeleteObjects(lpecSession, lpDatabase, &lObjectIds, ulDeleteFlags, 0, false, false);
		if(er != erSuccess) {
			ec_log_err("Error while removing softdelete folder objects, error code: 0x%x.", er);
			goto exit;
		}

		ec_log_info("Folder purge done");

	}
	if (*lpbExit) {
		er = KCERR_USER_CANCEL;
		goto exit;
	}

	// Select softdeleted messages
	strQuery = "SELECT h.id FROM hierarchy AS h JOIN properties AS p ON p.hierarchyid=h.id AND p.tag="+stringify(PROP_ID(PR_DELETED_ON))+" AND p.type="+stringify(PROP_TYPE(PR_DELETED_ON))+" WHERE (h.flags&"+stringify(MSGFLAG_DELETED)+")="+stringify(MSGFLAG_DELETED)+" AND h.type="+stringify(MAPI_MESSAGE)+" AND p.val_hi<="+stringify(ft.dwHighDateTime);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	ulMessages = lpDBResult.get_num_rows();
	if(ulMessages > 0)
	{
		// Remove all items
		lObjectIds.clear();
		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			if(lpDBRow == NULL || lpDBRow[0] == NULL)
				continue;
			lObjectIds.emplace_back(atoui(lpDBRow[0]));
		}
		// free before we call DeleteObjects()
		lpDBResult = DB_RESULT();
		if (*lpbExit) {
			er = KCERR_USER_CANCEL;
			goto exit;
		}

		ec_log_info("Start to purge %d messages", (int)lObjectIds.size());

		er = DeleteObjects(lpecSession, lpDatabase, &lObjectIds, ulDeleteFlags, 0, false, false);
		if(er != erSuccess) {
			ec_log_err("Error while removing softdelete message objects, error code: 0x%x.", er);
			goto exit;
		}

		ec_log_info("Message purge done");
	}

	// these stats are only from toplevel objects
	if(lpulFolders)
		*lpulFolders = ulFolders;

	if(lpulMessages)
		*lpulMessages = ulMessages;

	if (lpulStores)
		*lpulStores = ulStores;

exit:
	if (er != KCERR_BUSY)
		g_bPurgeSoftDeleteStatus = FALSE;
	return er;
}

/**
 * getPublicStore: get the root entryid, the store entryid and the store GUID for the public store.
 * FIXME, GUID is duplicate
 */
SOAP_ENTRY_START(getPublicStore, lpsResponse->er, unsigned int ulFlags, struct getStoreResponse *lpsResponse)
{
    unsigned int		ulCompanyId = 0;
	std::string			strStoreName;
	std::string			strStoreServer;
	std::string			strServerPath;
	std::string			strCompanyName;
	const std::string	strThisServer = g_lpSessionManager->GetConfig()->GetSetting("server_name", "", "Unknown");
	objectdetails_t		details;
	auto sesmgr = lpecSession->GetSessionManager();
	auto sec = lpecSession->GetSecurity();
	auto usrmgt = lpecSession->GetUserManagement();
    USE_DATABASE();

	if (sesmgr->IsHostedSupported()) {
		/* Hosted support, Public store owner is company */
		er = sec->GetUserCompany(&ulCompanyId);
		if (er != erSuccess)
			return er;
		er = usrmgt->GetObjectDetails(ulCompanyId, &details);
        if(er != erSuccess)
			return er;
        strStoreServer = details.GetPropString(OB_PROP_S_SERVERNAME);
		strCompanyName = details.GetPropString(OB_PROP_S_FULLNAME);
	} else {
		ulCompanyId = KOPANO_UID_EVERYONE; /* No hosted support, Public store owner is Everyone */

		if (sesmgr->IsDistributedSupported()) {
			/*
			* GetObjectDetailsAndSync will return the group details for EVERYONE when called
			* with KOPANO_UID_EVERYONE. But we want the pseudo company for that contains the
			* public store.
			*/
			er = usrmgt->GetPublicStoreDetails(&details);
			if (er == KCERR_NO_SUPPORT) {
				/* Not supported: No MultiServer with this plugin, so we're good */
				strStoreServer = strThisServer;
				er = erSuccess;
			} else if (er == erSuccess)
				strStoreServer = details.GetPropString(OB_PROP_S_SERVERNAME);
			else
				return er;
		} else
			strStoreServer = strThisServer;
	}

	if (sesmgr->IsDistributedSupported()) {
		if (strStoreServer.empty()) {
			if (!strCompanyName.empty())
				ec_log_err("Company \"%s\" has no home server for its public store.", strCompanyName.c_str());
			else
				ec_log_err("Public store has no home server.");
			return KCERR_NOT_FOUND;
		}
	
		/* Do we own the store? */
		if (strcasecmp(strThisServer.c_str(), strStoreServer.c_str()) != 0 &&
		    (ulFlags & EC_OVERRIDE_HOMESERVER) == 0) {
			er = GetBestServerPath(soap, lpecSession, strStoreServer, &strServerPath);
			if (er != erSuccess)
				return er;
			lpsResponse->lpszServerPath = STROUT_FIX_CPY(strServerPath.c_str());
			ec_log_info("Redirecting request to \"%s\"", lpsResponse->lpszServerPath);
			g_lpStatsCollector->Increment(SCN_REDIRECT_COUNT, 1);
			return KCERR_UNABLE_TO_COMPLETE;
		}
	}

	/*
	 * The public store is stored in the database with the companyid as owner.
	 */
	strQuery =
		"SELECT hierarchy.id, stores.guid, stores.hierarchy_id "
		"FROM stores "
		"JOIN hierarchy on stores.hierarchy_id=hierarchy.parent "
		"WHERE stores.user_id = " + stringify(ulCompanyId) + " LIMIT 1";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	if (lpDBResult.get_num_rows() == 0)
		return KCERR_NOT_FOUND;
	lpDBRow = lpDBResult.fetch_row();
	lpDBLen = lpDBResult.fetch_row_lengths();
	if( lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL ||
		lpDBLen == NULL || lpDBLen[1] == 0)
	{
		ec_log_err("getPublicStore(): no rows from db");
		return KCERR_DATABASE_ERROR; // this should never happen
	}

	auto gcache = g_lpSessionManager->GetCacheManager();
	er = gcache->GetEntryIdFromObject(atoui(lpDBRow[0]), soap, 0, &lpsResponse->sRootId);
	if(er != erSuccess)
		return er;
	if ((ulFlags & EC_OVERRIDE_HOMESERVER) == 0)
		lpsResponse->lpszServerPath = STROUT_FIX_CPY(std::string("pseudo://" + strStoreServer).c_str());
	er = gcache->GetEntryIdFromObject(atoui(lpDBRow[2]), soap, ulFlags, &lpsResponse->sStoreId);
	if(er != erSuccess)
		return er;
	lpsResponse->guid.__size= lpDBLen[1];
	lpsResponse->guid.__ptr = s_alloc<unsigned char>(soap, lpDBLen[1]);

	memcpy(lpsResponse->guid.__ptr, lpDBRow[1], lpDBLen[1]);
	return erSuccess;
}
SOAP_ENTRY_END()

/**
 * getStore: get the root entryid, the store entryid and the GUID of a store specified with lpsEntryId
 * FIXME: output store entryid equals input store entryid ?
 * FIXME: output GUID is also in entryid
 */
SOAP_ENTRY_START(getStore, lpsResponse->er, entryId* lpsEntryId, struct getStoreResponse *lpsResponse)
{
	unsigned int	ulStoreId = 0;
	unsigned int	ulUserId = 0;
	objectdetails_t	sUserDetails;
	std::string strServerName, strServerPath;
    USE_DATABASE();

	if (!lpsEntryId) {
		ulUserId = lpecSession->GetSecurity()->GetUserId();

        // Check if the store should be available on this server
        auto usrmgt = lpecSession->GetUserManagement();
		if (lpecSession->GetSessionManager()->IsDistributedSupported() && 
            !usrmgt->IsInternalObject(ulUserId)) {
			er = usrmgt->GetObjectDetails(ulUserId, &sUserDetails);
			if (er != erSuccess)
				return er;
            strServerName = sUserDetails.GetPropString(OB_PROP_S_SERVERNAME);
			if (strServerName.empty())
				return er;
            if (strcasecmp(strServerName.c_str(), g_lpSessionManager->GetConfig()->GetSetting("server_name")) != 0)  {
                er = GetBestServerPath(soap, lpecSession, strServerName, &strServerPath);
                if (er != erSuccess)
					return er;
                lpsResponse->lpszServerPath = STROUT_FIX_CPY(strServerPath.c_str());
                ec_log_info("Redirecting request to \"%s\"", lpsResponse->lpszServerPath);
                g_lpStatsCollector->Increment(SCN_REDIRECT_COUNT, 1);
				return KCERR_UNABLE_TO_COMPLETE;
            }
		}
	}
    
    // If strServerName is empty, we're not running in distributed mode or we're dealing
    // with a local account. Just use the name from the configuration.
    if (strServerName.empty())
        strServerName = g_lpSessionManager->GetConfig()->GetSetting("server_name" ,"", "Unknown");
    
    // Always return a pseudo URL
    lpsResponse->lpszServerPath = STROUT_FIX_CPY(std::string("pseudo://" + strServerName).c_str());

	strQuery = "SELECT hierarchy.id, stores.guid, stores.hierarchy_id "
	           "FROM stores join hierarchy on stores.hierarchy_id=hierarchy.parent ";

	if(lpsEntryId) {
		er = lpecSession->GetObjectFromEntryId(lpsEntryId, &ulStoreId);
		if(er != erSuccess)
			return er;
		strQuery += "WHERE stores.hierarchy_id=" + stringify(ulStoreId);// FIXME: mysql query
	}else {
		strQuery += "WHERE stores.user_id=" + stringify(ulUserId) 
				 + " AND stores.type=" + stringify(ECSTORE_TYPE_PRIVATE);
	}
	strQuery += " LIMIT 1";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	if (lpDBResult.get_num_rows() == 0)
		return KCERR_NOT_FOUND;
	lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == NULL) {
		ec_log_err("getStore(): no rows from db");
		return KCERR_DATABASE_ERROR; // this should never happen
	}
	lpDBLen = lpDBResult.fetch_row_lengths();
	/*
	 * Avoid processing SQL NULL, or memory blocks that are not
	 * NUL-terminated.
	 * Ensure the GUID (lpDBRow[1]) is not empty. (Perhaps check for
	 * !=16 instead of ==0?)
	 */
	if (lpDBLen == NULL || lpDBRow[0] == NULL ||
	    lpDBRow[1] == NULL || lpDBRow[2] == NULL ||
	    memchr(lpDBRow[0], '\0', lpDBLen[0] + 1) == NULL ||
	    memchr(lpDBRow[2], '\0', lpDBLen[2] + 1) == NULL ||
	    lpDBLen[1] == 0) {
		ec_log_err("getStore(): received trash rows from db");
		return KCERR_DATABASE_ERROR;
	}

	auto gcache = g_lpSessionManager->GetCacheManager();
	er = gcache->GetEntryIdFromObject(atoui(lpDBRow[0]), soap, 0, &lpsResponse->sRootId);
	if(er != erSuccess)
		return er;
	er = gcache->GetEntryIdFromObject(atoui(lpDBRow[2]), soap, 0, &lpsResponse->sStoreId);
	if(er != erSuccess)
		return er;
	lpsResponse->guid.__size= lpDBLen[1];
	lpsResponse->guid.__ptr = s_alloc<unsigned char>(soap, lpDBLen[1]);

	memcpy(lpsResponse->guid.__ptr, lpDBRow[1], lpDBLen[1]);
	return erSuccess;
}
SOAP_ENTRY_END()

/**
 * getStoreName: get the PR_DISPLAY_NAME of the store specified in sEntryId
 */
SOAP_ENTRY_START(getStoreName, lpsResponse->er, entryId sEntryId, struct getStoreNameResponse *lpsResponse)
{
	unsigned int	ulObjId = 0;
	unsigned int	ulStoreType = 0;

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if(er != erSuccess)
		return er;
	er = lpecSession->GetSessionManager()->GetCacheManager()->GetStoreAndType(ulObjId, NULL, NULL, &ulStoreType);
	if (er != erSuccess)
		return er;
	er = ECGenProps::GetStoreName(soap, lpecSession, ulObjId, ulStoreType, &lpsResponse->lpszStoreName);
	if(er != erSuccess)
		return er;
	if (!bSupportUnicode)
		lpsResponse->lpszStoreName = STROUT_FIX(lpsResponse->lpszStoreName);
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getStoreType, lpsResponse->er, entryId sEntryId, struct getStoreTypeResponse *lpsResponse)
{
	unsigned int	ulObjId = 0;

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if (er != erSuccess)
		return er;
	return lpecSession->GetSessionManager()->GetCacheManager()->
	       GetStoreAndType(ulObjId, nullptr, nullptr,
	       &lpsResponse->ulStoreType);
}
SOAP_ENTRY_END()

static ECRESULT ReadProps(struct soap *soap, ECSession *lpecSession,
    unsigned int ulObjId, unsigned ulObjType, unsigned int ulObjTypeParent,
    CHILDPROPS sChildProps, struct propTagArray *lpsPropTag,
    struct propValArray *lpsPropVal)
{
	ECRESULT er = erSuccess;

	quotadetails_t	sDetails;
	unsigned int	ulCompanyId = 0;
	unsigned int	ulStoreOwner = 0;
	object_ptr<ECAttachmentStorage> lpAttachmentStorage;

	struct propVal sPropVal;
	ECStringCompat stringCompat(lpecSession->GetCapabilities() & KOPANO_CAP_UNICODE);

	USE_DATABASE();
        
	if(ulObjType == MAPI_STORE) //fimxe: except public stores
	{
		if (ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_USER_NAME, ulObjId, 0, ulObjId, 0, ulObjType, &sPropVal) == erSuccess) {
			er = FixPropEncoding(soap, stringCompat, Out, &sPropVal);
			if (er != erSuccess)
				return er;
		    sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
		    sChildProps.lpPropVals->AddPropVal(sPropVal);
        }

		if (ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_USER_ENTRYID, ulObjId, 0, ulObjId, 0, ulObjType, &sPropVal) == erSuccess) {
		    sChildProps.lpPropTags->AddPropTag(PR_USER_ENTRYID);
		    sChildProps.lpPropVals->AddPropVal(sPropVal);
        }

        er = lpecSession->GetSecurity()->GetStoreOwner(ulObjId, &ulStoreOwner);
        if (er != erSuccess)
			return er;

		// Quota information
		if (lpecSession->GetSecurity()->GetUserQuota(ulStoreOwner, false, &sDetails) == erSuccess)
		{
			// PR_QUOTA_WARNING_THRESHOLD
			sPropVal.ulPropTag = PR_QUOTA_WARNING_THRESHOLD;
			sPropVal.__union = SOAP_UNION_propValData_ul;
			sPropVal.Value.ul = (unsigned long)(sDetails.llWarnSize / 1024);

			sChildProps.lpPropTags->AddPropTag(PR_QUOTA_WARNING_THRESHOLD);
			sChildProps.lpPropVals->AddPropVal(sPropVal);
			    
			// PR_QUOTA_SEND_THRESHOLD
			sPropVal.ulPropTag = PR_QUOTA_SEND_THRESHOLD;
			sPropVal.__union = SOAP_UNION_propValData_ul;
			sPropVal.Value.ul = (unsigned long)(sDetails.llSoftSize / 1024);

			sChildProps.lpPropTags->AddPropTag(PR_QUOTA_SEND_THRESHOLD);
            sChildProps.lpPropVals->AddPropVal(sPropVal);

			// PR_QUOTA_RECEIVE_THRESHOLD
			sPropVal.ulPropTag = PR_QUOTA_RECEIVE_THRESHOLD;
			sPropVal.__union = SOAP_UNION_propValData_ul;
			sPropVal.Value.ul = (unsigned long)(sDetails.llHardSize  / 1024);

			sChildProps.lpPropTags->AddPropTag(PR_QUOTA_RECEIVE_THRESHOLD);
            sChildProps.lpPropVals->AddPropVal(sPropVal);
		}

		if (lpecSession->GetCapabilities() & KOPANO_CAP_MAILBOX_OWNER) {
			// get the companyid to which the logged in user belongs to.
			er = lpecSession->GetSecurity()->GetUserCompany(&ulCompanyId);
			if (er != erSuccess)
				return er;

			// 5.0 client knows how to handle the PR_MAILBOX_OWNER_* properties
			if(ulStoreOwner != KOPANO_UID_EVERYONE && ulStoreOwner != ulCompanyId )	{

				if (ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_MAILBOX_OWNER_NAME, ulObjId, 0, ulObjId, 0, ulObjType, &sPropVal) == erSuccess) {
					er = FixPropEncoding(soap, stringCompat, Out, &sPropVal);
					if (er != erSuccess)
						return er;
					sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
					sChildProps.lpPropVals->AddPropVal(sPropVal);
				}

				// Add PR_MAILBOX_OWNER_ENTRYID
				if (ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_MAILBOX_OWNER_ENTRYID, ulObjId, 0, ulObjId, 0, ulObjType, &sPropVal) == erSuccess) {
					sChildProps.lpPropTags->AddPropTag(PR_MAILBOX_OWNER_ENTRYID);
                    sChildProps.lpPropVals->AddPropVal(sPropVal);
				}
			}
		}

		// Add PR_DISPLAY_NAME
		if (ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_DISPLAY_NAME, ulObjId, 0, ulObjId, 0, ulObjType, &sPropVal) == erSuccess) {
			er = FixPropEncoding(soap, stringCompat, Out, &sPropVal);
			if (er != erSuccess)
				return er;
		    sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
		    sChildProps.lpPropVals->AddPropVal(sPropVal);
		}

		if(ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_MAPPING_SIGNATURE, ulObjId, 0, 0, 0, ulObjType, &sPropVal) == erSuccess) {
		    sChildProps.lpPropTags->AddPropTag(PR_MAPPING_SIGNATURE);
		    sChildProps.lpPropVals->AddPropVal(sPropVal);
		}

		if (!sChildProps.lpPropTags->HasPropTag(PR_SORT_LOCALE_ID)) {
			sPropVal.__union = SOAP_UNION_propValData_ul;
			sPropVal.ulPropTag = PR_SORT_LOCALE_ID;
			sPropVal.Value.ul = lpecSession->GetSessionManager()->GetSortLCID(ulObjId);

			sChildProps.lpPropTags->AddPropTag(PR_SORT_LOCALE_ID);
			sChildProps.lpPropVals->AddPropVal(sPropVal);
		}
	}

	//PR_PARENT_SOURCE_KEY for folders and messages
	if(ulObjType == MAPI_FOLDER || (ulObjType == MAPI_MESSAGE && ulObjTypeParent == MAPI_FOLDER))
	{
		if(ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_PARENT_SOURCE_KEY, ulObjId, 0, 0, 0, ulObjType, &sPropVal) == erSuccess)
		{
		    sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
		    sChildProps.lpPropVals->AddPropVal(sPropVal);
		}

		if(ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_SOURCE_KEY, ulObjId, 0, 0, 0, ulObjType, &sPropVal) == erSuccess)
		{
		    sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
		    sChildProps.lpPropVals->AddPropVal(sPropVal);
		}
	}

	if (ulObjType == MAPI_MESSAGE || ulObjType == MAPI_ATTACH) {
		ULONG ulPropTag = (ulObjType == MAPI_MESSAGE ? PR_EC_IMAP_EMAIL : PR_ATTACH_DATA_BIN);
		er = CreateAttachmentStorage(lpDatabase, &~lpAttachmentStorage);
		if (er != erSuccess)
			return er;
		if (lpAttachmentStorage->ExistAttachment(ulObjId, PROP_ID(ulPropTag)))
		    sChildProps.lpPropTags->AddPropTag(ulPropTag);
	}

	if ((ulObjType == MAPI_MAILUSER || ulObjType == MAPI_DISTLIST) &&
	    ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_INSTANCE_KEY, ulObjId, 0, 0, 0, ulObjType, &sPropVal) == erSuccess) {
		sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
		sChildProps.lpPropVals->AddPropVal(sPropVal);
	}

	// Set the PR_RECORD_KEY
	if ((ulObjType != MAPI_ATTACH || !sChildProps.lpPropTags->HasPropTag(PR_RECORD_KEY)) &&
	    ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_RECORD_KEY, ulObjId, 0, 0, 0, ulObjType, &sPropVal) == erSuccess) {
		sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
		sChildProps.lpPropVals->AddPropVal(sPropVal);
	}

	if (ulObjType == MAPI_FOLDER || ulObjType == MAPI_STORE || ulObjType == MAPI_MESSAGE) {
		unsigned int cached_parent = 0, cached_flags = 0, cached_type = 0;

		er = g_lpSessionManager->GetCacheManager()->GetObject(ulObjId, &cached_parent, NULL, &cached_flags, &cached_type);
		if (er != erSuccess)
			return er;

		// Get PARENT_ENTRYID
		if (cached_parent != CACHE_NO_PARENT &&
			ECGenProps::GetPropComputedUncached(soap, nullptr, lpecSession, PR_PARENT_ENTRYID, ulObjId, 0, 0, 0, ulObjType, &sPropVal) == erSuccess) {
			sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
			sChildProps.lpPropVals->AddPropVal(sPropVal);
		}

		// PR_RIGHTS
		if (cached_type == MAPI_FOLDER &&
			ECGenProps::GetPropComputedUncached(soap, nullptr, lpecSession, PR_RIGHTS, ulObjId, 0, 0, 0, ulObjType, &sPropVal) == erSuccess) {
			sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
			sChildProps.lpPropVals->AddPropVal(sPropVal);
		}

		// Set the flags PR_ACCESS and PR_ACCESS_LEVEL
		if (cached_parent != CACHE_NO_PARENT && (cached_type == MAPI_FOLDER || cached_type == MAPI_MESSAGE) &&
			ECGenProps::GetPropComputedUncached(soap, nullptr, lpecSession, PR_ACCESS, ulObjId, 0, 0, 0, ulObjType, &sPropVal) == erSuccess) {
			sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
			sChildProps.lpPropVals->AddPropVal(sPropVal);
		}
		if (ECGenProps::GetPropComputedUncached(soap, nullptr, lpecSession, PR_ACCESS_LEVEL, ulObjId, 0, 0, 0, ulObjType, &sPropVal) == erSuccess) {
			sChildProps.lpPropTags->AddPropTag(sPropVal.ulPropTag);
			sChildProps.lpPropVals->AddPropVal(sPropVal);
		}
	}
    
	er = sChildProps.lpPropTags->GetPropTagArray(lpsPropTag);
	if(er != erSuccess)
		return er;
	return sChildProps.lpPropVals->GetPropValArray(lpsPropVal);
}

/**
 * loadProp: Reads a single, large property from the database. No size limit.
 *   this can also be a complete attachment
 */
SOAP_ENTRY_START(loadProp, lpsResponse->er, entryId sEntryId, unsigned int ulObjId, unsigned int ulPropTag, struct loadPropResponse *lpsResponse)
{
	object_ptr<ECAttachmentStorage> lpAttachmentStorage;
	USE_DATABASE();

	if(ulObjId == 0) {
        er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId);
        if (er != erSuccess)
			return er;
    }
    
	// Check permission
	er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityRead);
	if(er != erSuccess)
		return er;
	er = lpDatabase->Begin();
	if(er != erSuccess)
		return er;

	if (ulPropTag != PR_ATTACH_DATA_BIN && ulPropTag != PR_EC_IMAP_EMAIL) {
		if (ulPropTag & MV_FLAG)
			strQuery = "SELECT " + (std::string)MVPROPCOLORDER + " FROM mvproperties WHERE hierarchyid="+stringify(ulObjId)+ " AND tag = " + stringify(PROP_ID(ulPropTag))+" GROUP BY hierarchyid, tag";
		else
			strQuery = "SELECT " PROPCOLORDER " FROM properties WHERE hierarchyid = " + stringify(ulObjId) + " AND tag = " + stringify(PROP_ID(ulPropTag));
		strQuery += " LIMIT 2";
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if(er != erSuccess)
			goto exit;
		if (lpDBResult.get_num_rows() != 1) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		lpDBRow = lpDBResult.fetch_row();
		lpDBLen = lpDBResult.fetch_row_lengths();
		if (lpDBRow == NULL || lpDBLen == NULL)
		{
			er = KCERR_DATABASE_ERROR;
			ec_log_err("loadProp(): no rows from db");
			goto exit;
		}

		lpsResponse->lpPropVal = s_alloc<propVal>(soap);
		memset(lpsResponse->lpPropVal,0,sizeof(struct propVal));

		er = CopyDatabasePropValToSOAPPropVal(soap, lpDBRow, lpDBLen, lpsResponse->lpPropVal);
		if (er != erSuccess)
			goto exit;
	} else {

		lpsResponse->lpPropVal = s_alloc<propVal>(soap);
		memset(lpsResponse->lpPropVal,0,sizeof(struct propVal));

		lpsResponse->lpPropVal->ulPropTag = ulPropTag;
		lpsResponse->lpPropVal->__union = SOAP_UNION_propValData_bin;
		lpsResponse->lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		memset(lpsResponse->lpPropVal->Value.bin, 0, sizeof(struct xsd__base64Binary));
		er = CreateAttachmentStorage(lpDatabase, &~lpAttachmentStorage);
		if (er != erSuccess)
			goto exit;

		size_t atsize = 0;
		er = lpAttachmentStorage->LoadAttachment(soap, ulObjId, PROP_ID(ulPropTag), &atsize, &lpsResponse->lpPropVal->Value.bin->__ptr);
		lpsResponse->lpPropVal->Value.bin->__size = atsize;
		if (er != erSuccess)
			goto exit;
	}

	er = lpDatabase->Commit();
	if(er != erSuccess)
		goto exit;

	if (!bSupportUnicode) {
		er = FixPropEncoding(soap, stringCompat, Out, lpsResponse->lpPropVal);
		if (er != erSuccess)
			goto exit;
	}

exit:
	ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

//TODO: flag to get size of normal folder or deleted folders
static ECRESULT GetFolderSize(ECDatabase *lpDatabase, unsigned int ulFolderId,
    long long *lpllFolderSize)
{

	ECRESULT		er = erSuccess;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow = NULL;
	long long		llSize = 0;
	long long		llSubSize = 0;
	std::string		strQuery;

	// sum size of all messages in a folder
	strQuery = "SELECT SUM(p.val_ulong) FROM hierarchy AS h JOIN properties AS p ON p.hierarchyid=h.id AND p.tag="+stringify(PROP_ID(PR_MESSAGE_SIZE))+" AND p.type="+stringify(PROP_TYPE(PR_MESSAGE_SIZE))+" WHERE h.parent="+stringify(ulFolderId)+" AND h.type="+stringify(MAPI_MESSAGE);
	// except the deleted items!
	strQuery += " AND h.flags & " + stringify(MSGFLAG_DELETED)+ "=0";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	lpDBRow = lpDBResult.fetch_row();
	if(lpDBRow == NULL || lpDBRow[0] == NULL)
		llSize = 0;
	else
		llSize = atoll(lpDBRow[0]);

	// Get the subfolders
	strQuery = "SELECT id FROM hierarchy WHERE parent=" + stringify(ulFolderId) + " AND type="+stringify(MAPI_FOLDER);
	// except the deleted items!
	strQuery += " AND flags & " + stringify(MSGFLAG_DELETED)+ "=0";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	if (lpDBResult.get_num_rows() > 0) {
		// Walk through the folder list
		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			if(lpDBRow[0] == NULL)
				continue; //Skip item

			er = GetFolderSize(lpDatabase, atoi(lpDBRow[0]), &llSubSize);
			if(er != erSuccess)
				return er;
			llSize += llSubSize;
		}
	}

	*lpllFolderSize = llSize;
	return erSuccess;
}

/**
 * Write properties to an object
 *
 * @param[in] soap 
 * @param[in] lpecSession Pointer to the session of the caller; Cannot be NULL.
 * @param[in] lpDatabase Pointer to the database handler of the caller; Cannot be NULL. 
 * @param[in] lpAttachmentStorage Pointer to an attachment storage object; Cannot be NULL.
 * @param[in] lpsSaveObj Data object which include the new propery information; Cannot be NULL.
 * @param[in] ulObjId Identify the database object to write the property data to the database.
 * @param[in] fNewItem false for an existing object, true for a new object.
 * @param[in] ulSyncId Client sync identifier.
 * @param[out] lpsReturnObj
 * @param[out] lpfHaveChangeKey
 * @param[out] lpftCreated Receives create time if new object (can be now or passed value in lpsSaveObj)
 * @param[out] lpftModified Receives modification time of written object (can be now or passed value in lpsSaveObj)
 *
 * \remarks
 * 		Check the permissions and quota before you call this function.
 *
 * @todo unclear comment -> sync id only to saveObject !
 */
static ECRESULT WriteProps(struct soap *soap, ECSession *lpecSession,
    ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage,
    struct saveObject *lpsSaveObj, unsigned int ulObjId, bool fNewItem,
    unsigned int ulSyncId, struct saveObject *lpsReturnObj,
    bool *lpfHaveChangeKey, FILETIME *lpftCreated, FILETIME *lpftModified)
{
	ECRESULT		er;
    std::string		strInsertQuery;
	std::string		strColName;
	std::string		strQuery;

	struct propValArray *lpPropValArray = &lpsSaveObj->modProps;

	unsigned int	ulParent = 0;
	unsigned int	ulGrandParent = 0;
	unsigned int	ulParentType = 0;
	unsigned int	ulObjType = 0;
	unsigned int	ulOwner = 0;
	unsigned int	ulFlags = 0;
	unsigned int	ulAffected = 0;
	unsigned int	ulPropInserts = 0;

	gsoap_size_t nMVItems;
	unsigned long long ullIMAP = 0;

	std::set<unsigned int>	setInserted;

	GUID sGuidServer;
	ULONG ulInstanceId = 0;
	ULONG ulInstanceTag = 0;
	bool bAttachmentStored = false;
	entryId sUserId;
	std::string strUsername;

	ECStringCompat stringCompat(lpecSession->GetCapabilities() & KOPANO_CAP_UNICODE);
	std::string	strColData;
	
	std::string strInsert;
	std::string strInsertTProp;
	
	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey;
	DB_RESULT lpDBResult;

	if (lpAttachmentStorage == nullptr)
		return KCERR_INVALID_PARAMETER;
	if (lpfHaveChangeKey)
		*lpfHaveChangeKey = false;

	er = g_lpSessionManager->GetServerGUID(&sGuidServer);
	if (er != erSuccess)
		return er;
	er = g_lpSessionManager->GetCacheManager()->GetObject(ulObjId, &ulParent, &ulOwner, &ulFlags, &ulObjType);
	if (er != erSuccess)
		return er;
	if(ulObjType != MAPI_STORE){
		er = g_lpSessionManager->GetCacheManager()->GetObject(ulParent, &ulGrandParent, NULL, NULL, &ulParentType);
		if(er != erSuccess)
			return er;
	}

	if(ulObjType == MAPI_FOLDER && !ulSyncId) {
		for (gsoap_size_t i = 0; i < lpPropValArray->__size; ++i) {
			// Check whether the requested folder name already exists
			if (lpPropValArray->__ptr[i].ulPropTag != PR_DISPLAY_NAME)
				continue;
			if(lpPropValArray->__ptr[i].Value.lpszA == NULL)
				break; // Name property found, but name isn't present. This is broken, so skip this.

			strQuery = "SELECT hierarchy.id FROM hierarchy JOIN properties ON hierarchy.id = properties.hierarchyid WHERE hierarchy.parent=" + stringify(ulParent) + " AND hierarchy.type="+stringify(MAPI_FOLDER)+" AND hierarchy.flags & " + stringify(MSGFLAG_DELETED)+ "=0 AND properties.tag=" + stringify(KOPANO_TAG_DISPLAY_NAME) + " AND properties.val_string = '" + lpDatabase->Escape(lpPropValArray->__ptr[i].Value.lpszA) + "' AND properties.type="+stringify(PT_STRING8)+" AND hierarchy.id!=" + stringify(ulObjId) + " LIMIT 1";
			er = lpDatabase->DoSelect(strQuery, &lpDBResult);
			if (er != erSuccess) {
				ec_log_err("WriteProps(): DoSelect failed %x", er);
				return er;
			}
			if (lpDBResult.get_num_rows() > 0) {
				ec_log_err("WriteProps(): Folder already exists while putting folder");
				return KCERR_COLLISION;
			}
			break;
		}// for(...)
	}

	// If we have a recipient, remove the old one. The client does not send a diff of props because ECMemTable cannot do this
	if (!fNewItem && (ulObjType == MAPI_MAILUSER || ulObjType == MAPI_DISTLIST)) {
		strQuery = "DELETE FROM properties WHERE hierarchyid=" + stringify(ulObjId);

		er = lpDatabase->DoDelete(strQuery);
		if(er != erSuccess)
			return er;

		strQuery = "DELETE FROM mvproperties WHERE hierarchyid=" + stringify (ulObjId);

		er = lpDatabase->DoDelete(strQuery);
		if(er != erSuccess)
			return er;
		if (ulObjType != lpsSaveObj->ulObjType) {
			strQuery = "UPDATE hierarchy SET type=" +stringify(lpsSaveObj->ulObjType) +" WHERE id=" + stringify (ulObjId);
			er = lpDatabase->DoUpdate(strQuery);
			if(er != erSuccess)
				return er;
			// Switch the type so the cache will be updated at the end.
			ulObjType = lpsSaveObj->ulObjType;
		}
	}

	/* FIXME: Support multiple InstanceIds */
	if (lpsSaveObj->lpInstanceIds && lpsSaveObj->lpInstanceIds->__size) {
		GUID sGuidTmp;

		if (lpsSaveObj->lpInstanceIds->__size > 1)
			return KCERR_UNKNOWN_INSTANCE_ID;
		er = SIEntryIDToID(&lpsSaveObj->lpInstanceIds->__ptr[0], &sGuidTmp, (unsigned int*)&ulInstanceId, (unsigned int*)&ulInstanceTag);
		if (er != erSuccess)
			return er;
		/* Server GUID must always match */
		if (memcmp(&sGuidTmp, &sGuidServer, sizeof(sGuidTmp)) != 0)
			return KCERR_UNKNOWN_INSTANCE_ID;
		/* The attachment should at least exist */
		if (!lpAttachmentStorage->ExistAttachmentInstance(ulInstanceId))
			return KCERR_UNKNOWN_INSTANCE_ID;
		/*
		 * Check if we have access to the instance which is being referenced,
		 * a user has access to an instance when he is administrator or owns at
		 * least one reference to the instance.
		 * For security we won't announce the difference between not finding the instance
		 * or not having access to it.
		 */
		if (lpecSession->GetSecurity()->GetAdminLevel() != ADMIN_LEVEL_SYSADMIN) {
			std::list<ext_siid> lstObjIds;
			er = lpAttachmentStorage->GetSingleInstanceParents(ulInstanceId, &lstObjIds);
			if (er != erSuccess)
				return er;
			er = KCERR_UNKNOWN_INSTANCE_ID;
			for (const auto &i : lstObjIds)
				if (lpecSession->GetSecurity()->CheckPermission(i.siid, ecSecurityRead) == erSuccess) {
						er = erSuccess;
						break;
				}

			if (er != erSuccess)
				return er;
		}

		er = lpAttachmentStorage->SaveAttachment(ulObjId, ulInstanceTag, !fNewItem, ulInstanceId, &ulInstanceId);
		if (er != erSuccess)
			return er;
		lpsReturnObj->lpInstanceIds = s_alloc<entryList>(soap);
		lpsReturnObj->lpInstanceIds->__size = 1;
		lpsReturnObj->lpInstanceIds->__ptr = s_alloc<entryId>(soap, lpsReturnObj->lpInstanceIds->__size);

		er = SIIDToEntryID(soap, &sGuidServer, ulInstanceId, ulInstanceTag, &lpsReturnObj->lpInstanceIds->__ptr[0]);
		if (er != erSuccess) {
			lpsReturnObj->lpInstanceIds = NULL;
			return er;
		}

		/* Either by instanceid or attachment data, we have stored the attachment */
		bAttachmentStored = true;
	}

	// Write the properties
	for (gsoap_size_t i = 0; i < lpPropValArray->__size; ++i) {
	    // Check if we already inserted this property tag. We only accept the first.
	    auto iterInserted = setInserted.find(lpPropValArray->__ptr[i].ulPropTag);
	    if (iterInserted != setInserted.cend())
	        continue;

        // Check if we actually need to write this property. The client may send us properties
        // that we generate, so we don't need to save them
        if(ECGenProps::IsPropRedundant(lpPropValArray->__ptr[i].ulPropTag, ulObjType) == erSuccess)
            continue;

		// Some properties may only be saved on the first save (fNewItem == TRUE) for folders, stores and messages
		if(!fNewItem && (ulObjType == MAPI_MESSAGE || ulObjType == MAPI_FOLDER || ulObjType == MAPI_STORE)) {
			switch(lpPropValArray->__ptr[i].ulPropTag) {
			case PR_LAST_MODIFICATION_TIME:
			case PR_MESSAGE_FLAGS:
			case PR_SEARCH_KEY:
			case PR_LAST_MODIFIER_NAME:
			case PR_LAST_MODIFIER_ENTRYID:
				if (ulSyncId == 0)
					// Only on first write, unless sent by ICS
					continue;
				break;
			case PR_SOURCE_KEY:
				if (ulParentType == MAPI_FOLDER)
					// Only on first write, unless message-in-message
					continue;
				break;
			}
		}
		if (lpPropValArray->__ptr[i].ulPropTag == PR_LAST_MODIFIER_NAME_W ||
		    lpPropValArray->__ptr[i].ulPropTag == PR_LAST_MODIFIER_ENTRYID)
			if(!fNewItem && ulSyncId == 0)
				continue;

		// Same goes for flags in PR_MESSAGE_FLAGS
		if(lpPropValArray->__ptr[i].ulPropTag == PR_MESSAGE_FLAGS) {
			if (ulSyncId == 0)
				// Normalize PR_MESSAGE_FLAGS so that the user cannot set things like MSGFLAG_ASSOCIATED 
				lpPropValArray->__ptr[i].Value.ul = (lpPropValArray->__ptr[i].Value.ul & (MSGFLAG_SETTABLE_BY_USER | MSGFLAG_SETTABLE_BY_SPOOLER)) | ulFlags;
			else
				// Normalize PR_MESSAGE_FLAGS so that the user cannot change flags that are also
				// stored in the hierarchy table.
				lpPropValArray->__ptr[i].Value.ul = (lpPropValArray->__ptr[i].Value.ul & ~MSGFLAG_UNSETTABLE) | ulFlags;
		}

		// Make sure we dont have a colliding PR_SOURCE_KEY. This can happen if a user imports an exported message for example.
		if(lpPropValArray->__ptr[i].ulPropTag == PR_SOURCE_KEY)
		{
		    // Remove any old (deleted) indexed property if it's there
		    er = RemoveStaleIndexedProp(lpDatabase, PR_SOURCE_KEY, lpPropValArray->__ptr[i].Value.bin->__ptr, lpPropValArray->__ptr[i].Value.bin->__size);
		    if(er != erSuccess) {
		        // Unable to remove the (old) sourcekey in use. This means that it is in use by some other object. We just skip
		        // the property so that it is generated later as a new random sourcekey
		        er = erSuccess;
				continue;
            }
				
			// Insert sourcekey, use REPLACE because createfolder already created a sourcekey.
			// Because there is a non-primary unique key on the
			// val_binary part of the table, it will fail if the source key is duplicate.
			strQuery = "REPLACE INTO indexedproperties(hierarchyid,tag,val_binary) VALUES ("+stringify(ulObjId)+","+stringify(PROP_ID(PR_SOURCE_KEY))+","+lpDatabase->EscapeBinary(lpPropValArray->__ptr[i].Value.bin->__ptr, lpPropValArray->__ptr[i].Value.bin->__size)+")";
			er = lpDatabase->DoInsert(strQuery);
			if(er != erSuccess)
				return er;

			setInserted.emplace(lpPropValArray->__ptr[i].ulPropTag);
			// Remember the source key in the cache
			g_lpSessionManager->GetCacheManager()->SetObjectProp(PROP_ID(PR_SOURCE_KEY), lpPropValArray->__ptr[i].Value.bin->__size, lpPropValArray->__ptr[i].Value.bin->__ptr, ulObjId);
			continue;
		}

		// attachments are in the blob too
		if (lpPropValArray->__ptr[i].ulPropTag == PR_ATTACH_DATA_BIN || lpPropValArray->__ptr[i].ulPropTag == PR_EC_IMAP_EMAIL) {
			/* 
			 * bAttachmentStored indicates we already processed the attachment.
			 * this could happen when the user provided the instance ID but as
			 * backup also send the PR_ATTACH_DATA_BIN.
			 */
			if (bAttachmentStored)
				continue;

			er = lpAttachmentStorage->SaveAttachment(ulObjId, PROP_ID(lpPropValArray->__ptr[i].ulPropTag), !fNewItem,
			     lpPropValArray->__ptr[i].Value.bin->__size,
			     lpPropValArray->__ptr[i].Value.bin->__ptr, &ulInstanceId);
			if (er != erSuccess)
				return er;

			lpsReturnObj->lpInstanceIds = s_alloc<entryList>(soap);
			lpsReturnObj->lpInstanceIds->__size = 1;
			lpsReturnObj->lpInstanceIds->__ptr = s_alloc<entryId>(soap, lpsReturnObj->lpInstanceIds->__size);

			er = SIIDToEntryID(soap, &sGuidServer, ulInstanceId, PROP_ID(lpPropValArray->__ptr[i].ulPropTag), &lpsReturnObj->lpInstanceIds->__ptr[0]);
			if (er != erSuccess) {
				lpsReturnObj->lpInstanceIds = NULL;
				return er;
			}

			continue;
		}
		
		// We have to return the values for PR_LAST_MODIFICATION_TIME and PR_CREATION_TIME
		if (lpPropValArray->__ptr[i].ulPropTag == PR_LAST_MODIFICATION_TIME) {
			lpftModified->dwHighDateTime = lpPropValArray->__ptr[i].Value.hilo->hi;
			lpftModified->dwLowDateTime = lpPropValArray->__ptr[i].Value.hilo->lo;
		}
		
		if (lpPropValArray->__ptr[i].ulPropTag == PR_CREATION_TIME) {
			lpftCreated->dwHighDateTime = lpPropValArray->__ptr[i].Value.hilo->hi;
			lpftCreated->dwLowDateTime = lpPropValArray->__ptr[i].Value.hilo->lo;
		}
		
		if (lpfHaveChangeKey && lpPropValArray->__ptr[i].ulPropTag == PR_CHANGE_KEY)
			*lpfHaveChangeKey = true;

		if (PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag) & MV_FLAG) {
			// Make sure string prop_types become PT_MV_STRING8
			if (PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag) == PT_MV_UNICODE)
				lpPropValArray->__ptr[i].ulPropTag = CHANGE_PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag, PT_MV_STRING8);

			//Write mv properties
			nMVItems = GetMVItemCount(&lpPropValArray->__ptr[i]);
			for (gsoap_size_t j = 0; j < nMVItems; ++j) {
				assert(PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag) != PT_MV_UNICODE);

				// Make sure string propvals are in UTF8
				if (PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag) == PT_MV_STRING8)
					lpPropValArray->__ptr[i].Value.mvszA.__ptr[j] = stringCompat.to_UTF8(soap, lpPropValArray->__ptr[i].Value.mvszA.__ptr[j]);

				er = CopySOAPPropValToDatabaseMVPropVal(&lpPropValArray->__ptr[i], j, strColName, strColData, lpDatabase);
				if(er != erSuccess)
					continue;

				strQuery = "REPLACE INTO mvproperties(hierarchyid,orderid,tag,type," + strColName + ") VALUES(" + stringify(ulObjId) + "," + stringify(j) + "," + stringify(PROP_ID(lpPropValArray->__ptr[i].ulPropTag)) + "," + stringify(PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag)) + "," + strColData + ")";
				er = lpDatabase->DoInsert(strQuery, NULL, &ulAffected);
				if(er != erSuccess)
					return er;

				// According to the MySQL documentation (http://dev.mysql.com/doc/refman/5.0/en/mysql-affected-rows.html) ulAffected rows
				// will be 2 if a row was replaced.
				// Interestingly, I (MSw) have observer in a consecutive call to the above replace query, where in both cases an old value
				// was replaced with a new value, that it returned 1 the first time and 2 the second time.
				// We'll allow both though.
				if(ulAffected != 1 && ulAffected != 2) {
					ec_log_err("Unable to update MVProperties during save: %d, object id: %d", ulAffected, ulObjId);
					return KCERR_DATABASE_ERROR;
				}
			}

			if(!fNewItem) {
				strQuery = "DELETE FROM mvproperties WHERE hierarchyid=" + stringify (ulObjId) +
							" AND tag=" + stringify(PROP_ID(lpPropValArray->__ptr[i].ulPropTag)) +
							" AND type=" + stringify(PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag)) +
							" AND orderid >= " + stringify(nMVItems);

				er = lpDatabase->DoDelete(strQuery);
				if(er != erSuccess)
					return er;
			}

		} else {
            // Make sure string propvals are in UTF8 with tag PT_STRING8
            if (PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag) == PT_STRING8 || PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag) == PT_UNICODE) {
            	lpPropValArray->__ptr[i].Value.lpszA = stringCompat.to_UTF8(soap, lpPropValArray->__ptr[i].Value.lpszA);
                lpPropValArray->__ptr[i].ulPropTag = CHANGE_PROP_TYPE(lpPropValArray->__ptr[i].ulPropTag, PT_STRING8);
		    }
                                                                    
			// Write the property to the database
			er = WriteSingleProp(lpDatabase, ulObjId, ulParent, &lpPropValArray->__ptr[i], false, lpDatabase->GetMaxAllowedPacket(), strInsert);
			if (er == KCERR_TOO_BIG) {
				er = lpDatabase->DoInsert(strInsert);
				if (er == erSuccess) {
					strInsert.clear();
					er = WriteSingleProp(lpDatabase, ulObjId, ulParent, &lpPropValArray->__ptr[i], false, lpDatabase->GetMaxAllowedPacket(), strInsert);
				}
			}
			if(er != erSuccess)
				return er;
			
			// Write the property to the table properties if needed (only on objects in folders (folders, messages), and if the property is being tracked here.
			if(ulParentType == MAPI_FOLDER) {
				// Cache the written value
				sObjectTableKey key(ulObjId,0);
				g_lpSessionManager->GetCacheManager()->SetCell(&key, lpPropValArray->__ptr[i].ulPropTag, &lpPropValArray->__ptr[i]);
			}
		}

		setInserted.emplace(lpPropValArray->__ptr[i].ulPropTag);
	} // for (i = 0; i < lpPropValArray->__size; ++i)

	if(!strInsert.empty()) {
		er = lpDatabase->DoInsert(strInsert);
		if(er != erSuccess)
			return er;
	}
	
	if(ulParentType == MAPI_FOLDER && ulParent != CACHE_NO_PARENT) {
		// Instead of writing directly to tproperties, save a delayed write request.
		er = ECTPropsPurge::AddDeferredUpdateNoPurge(lpDatabase, ulParent, 0, ulObjId);
		if (er != erSuccess)
			return er;
	}
		
	// Insert the properties
	if(ulPropInserts > 0) {
		er = lpDatabase->DoInsert(strInsertQuery, NULL, NULL);
		if(er != erSuccess)
			return er;
	}
	
	if(ulObjType == MAPI_MESSAGE) {
		auto iterInserted = setInserted.find(PR_LAST_MODIFIER_NAME_W);
		// update the PR_LAST_MODIFIER_NAME and PR_LAST_MODIFIER_ENTRYID
		if (iterInserted == setInserted.cend()) {
			er = GetABEntryID(lpecSession->GetSecurity()->GetUserId(), soap, &sUserId);
			if(er != erSuccess)
				return er;
			
			lpecSession->GetSecurity()->GetUsername(&strUsername);

			strQuery = "REPLACE INTO properties(hierarchyid, tag, type, val_string, val_binary) VALUES(" +
						stringify(ulObjId) + "," +
						stringify(PROP_ID(PR_LAST_MODIFIER_NAME_A)) + "," +
						stringify(PROP_TYPE(PR_LAST_MODIFIER_NAME_A)) + ",\"" +
						lpDatabase->Escape(strUsername) +
						"\", NULL), (" +
						stringify(ulObjId) + "," +
						stringify(PROP_ID(PR_LAST_MODIFIER_ENTRYID)) + "," +
						stringify(PROP_TYPE(PR_LAST_MODIFIER_ENTRYID)) +
						", NULL, " +
						lpDatabase->EscapeBinary(sUserId.__ptr, sUserId.__size) + ")";

			er = lpDatabase->DoInsert(strQuery);
			if(er != erSuccess)
				return er;

			sObjectTableKey key(ulObjId,0);
			struct propVal	sPropVal;

			sPropVal.ulPropTag = PR_LAST_MODIFIER_NAME_A;
			sPropVal.Value.lpszA = (char*)strUsername.c_str();
			sPropVal.__union = SOAP_UNION_propValData_lpszA;
            g_lpSessionManager->GetCacheManager()->SetCell(&key, PR_LAST_MODIFIER_NAME_A, &sPropVal);

			sPropVal.ulPropTag = PR_LAST_MODIFIER_ENTRYID;
			sPropVal.Value.bin = &sUserId;
			sPropVal.__union = SOAP_UNION_propValData_bin;
			g_lpSessionManager->GetCacheManager()->SetCell(&key, PR_LAST_MODIFIER_ENTRYID, &sPropVal);
		}
	}

	if(!fNewItem) {
		// Update, so write the modtime and clear UNMODIFIED flag
		if (setInserted.find(PR_LAST_MODIFICATION_TIME) == setInserted.cend()) {
		    struct propVal sProp;
		    struct hiloLong sHilo;
		    struct sObjectTableKey key;
		    
		    FILETIME ft;
		    
		    sProp.ulPropTag = PR_LAST_MODIFICATION_TIME;
		    sProp.Value.hilo = &sHilo;
		    sProp.__union = SOAP_UNION_propValData_hilo;
			UnixTimeToFileTime(time(NULL), &sProp.Value.hilo->hi, &sProp.Value.hilo->lo);
			
			ft.dwHighDateTime = sProp.Value.hilo->hi;
			ft.dwLowDateTime = sProp.Value.hilo->lo;
			
			er = WriteProp(lpDatabase, ulObjId, ulParent, &sProp);
			if (er != erSuccess)
				return er;
			
			*lpftModified = ft;
			
            // Add to cache
            key.ulObjId = ulObjId;
            key.ulOrderId = 0;
			if(ulParentType == MAPI_FOLDER)
                g_lpSessionManager->GetCacheManager()->SetCell(&key, PR_LAST_MODIFICATION_TIME, &sProp);
			
		}
		
		if(ulObjType == MAPI_MESSAGE) {
			// Unset MSGFLAG_UNMODIFIED
			strQuery = "UPDATE properties SET val_ulong=val_ulong&"+stringify(~MSGFLAG_UNMODIFIED) + " WHERE hierarchyid=" + stringify(ulObjId)+ " AND tag=" + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND type=" + stringify(PROP_TYPE(PR_MESSAGE_FLAGS));
			er = lpDatabase->DoUpdate(strQuery);
			if(er != erSuccess)
				return er;

			// Update cache	
			if(ulParentType == MAPI_FOLDER)
                g_lpSessionManager->GetCacheManager()->UpdateCell(ulObjId, PR_MESSAGE_FLAGS, (unsigned int)MSGFLAG_UNMODIFIED, 0);
		}
	} else if (setInserted.find(PR_LAST_MODIFICATION_TIME) == setInserted.cend() ||
	    setInserted.find(PR_CREATION_TIME) == setInserted.cend()) {
		// New item, make sure PR_CREATION_TIME and PR_LAST_MODIFICATION_TIME are available
		struct propVal sPropTime;
		FILETIME ft;
		unsigned int tags[] = {PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME};

		// Get current time
		UnixTimeToFileTime(time(NULL), &ft);

		sPropTime.Value.hilo = s_alloc<struct hiloLong>(soap);
		sPropTime.Value.hilo->hi = ft.dwHighDateTime;
		sPropTime.Value.hilo->lo = ft.dwLowDateTime;
		sPropTime.__union = SOAP_UNION_propValData_hilo;

		// Same thing for both PR_LAST_MODIFICATION_TIME and PR_CREATION_TIME
		for (size_t i = 0; i < ARRAY_SIZE(tags); ++i) {
			if (setInserted.find(tags[i]) != setInserted.cend())
				continue;
			sObjectTableKey key;
			sPropTime.ulPropTag = tags[i];
			strQuery.clear();
			WriteSingleProp(lpDatabase, ulObjId, ulParent, &sPropTime, false, 0, strQuery);
			er = lpDatabase->DoInsert(strQuery);
			if (er != erSuccess)
				return er;
			strQuery.clear();
			WriteSingleProp(lpDatabase, ulObjId, ulParent, &sPropTime, true, 0, strQuery);
			er = lpDatabase->DoInsert(strQuery);
			if (er != erSuccess)
				return er;
			if (tags[i] == PR_LAST_MODIFICATION_TIME)
				*lpftModified = ft;
			if (tags[i] == PR_CREATION_TIME)
				*lpftCreated = ft;

			// Add to cache
			key.ulObjId = ulObjId;
			key.ulOrderId = 0;
			if (ulParentType == MAPI_FOLDER)
				g_lpSessionManager->GetCacheManager()->SetCell(&key, tags[i], &sPropTime);
		}
	}

	if(fNewItem && ulObjType == MAPI_MESSAGE) {
        // Add PR_SOURCE_KEY to new messages without a given PR_SOURCE_KEY
        // This isn't for folders, this done in the createfolder function
		auto iterInserted = setInserted.find(PR_SOURCE_KEY);
		if (iterInserted == setInserted.cend()) {
			er = lpecSession->GetNewSourceKey(&sSourceKey);
			if (er != erSuccess)
				return er;

			strQuery = "INSERT INTO indexedproperties(hierarchyid,tag,val_binary) VALUES(" + stringify(ulObjId) + "," + stringify(PROP_ID(PR_SOURCE_KEY)) + "," + lpDatabase->EscapeBinary(sSourceKey, sSourceKey.size()) + ")";
			er = lpDatabase->DoInsert(strQuery);

			if(er != erSuccess)
				return er;

			g_lpSessionManager->GetCacheManager()->SetObjectProp(PROP_ID(PR_SOURCE_KEY), sSourceKey.size(), sSourceKey, ulObjId);
		}

		if(ulParentType == MAPI_FOLDER) {
			// Add a PR_EC_IMAP_ID to the newly created message
		    sObjectTableKey key(ulObjId, 0);
			struct propVal sProp;

			er = g_lpSessionManager->GetNewSequence(ECSessionManager::SEQ_IMAP, &ullIMAP);
			if(er != erSuccess)
				return er;
				
			strQuery = "INSERT INTO properties(hierarchyid, tag, type, val_ulong) VALUES(" +
						stringify(ulObjId) + "," +
						stringify(PROP_ID(PR_EC_IMAP_ID)) + "," +
						stringify(PROP_TYPE(PR_EC_IMAP_ID)) + "," +
						stringify(ullIMAP) +
						")";
			er = lpDatabase->DoInsert(strQuery);
			if(er != erSuccess)
				return er;

			sProp.ulPropTag = PR_EC_IMAP_ID;
			sProp.Value.ul = ullIMAP;
			sProp.__union = SOAP_UNION_propValData_ul;
			er = g_lpSessionManager->GetCacheManager()->SetCell(&key, PR_EC_IMAP_ID, &sProp);
			if (er != erSuccess)
				return er;
		}
	}

	if(fNewItem && ulParentType == MAPI_FOLDER)
        // Since we have written a new item, we know that the cache contains *all* properties for this object
        g_lpSessionManager->GetCacheManager()->SetComplete(ulObjId);
        
	// We know the values for the object cache, so add them here
	if(ulObjType == MAPI_MESSAGE || ulObjType == MAPI_FOLDER)
		g_lpSessionManager->GetCacheManager()->SetObject(ulObjId, ulParent, ulOwner, ulFlags, ulObjType);

	return erSuccess;
}

// You need to check the permissions before you call this function
static ECRESULT DeleteProps(ECSession *lpecSession, ECDatabase *lpDatabase,
    ULONG ulObjId, struct propTagArray *lpsPropTags,
    ECAttachmentStorage *at_storage)
{
	ECRESULT er;
	std::string		strQuery;
	sObjectTableKey key;
	struct propVal  sPropVal;
	// block removal of certain properties (per object type?), properties handled in WriteProps
	unsigned int ulPropTags[] = {PR_MESSAGE_FLAGS, PR_CREATION_TIME, PR_LAST_MODIFICATION_TIME, PR_LAST_MODIFIER_ENTRYID, PR_LAST_MODIFIER_NAME_W, PR_SOURCE_KEY};
	std::set<unsigned int> setNotDeletable(ulPropTags, ulPropTags + ARRAY_SIZE(ulPropTags));

	// Delete one or more properties of an object
	for (gsoap_size_t i = 0; i < lpsPropTags->__size; ++i) {
		if (setNotDeletable.find(lpsPropTags->__ptr[i]) != setNotDeletable.cend())
			continue;

		if((lpsPropTags->__ptr[i]&MV_FLAG) == 0)
			strQuery = "DELETE FROM properties WHERE hierarchyid="+stringify(ulObjId)+" AND tag="+stringify(PROP_ID(lpsPropTags->__ptr[i]));
		else // mvprops
			strQuery = "DELETE FROM mvproperties WHERE hierarchyid="+stringify(ulObjId)+" AND tag="+stringify(PROP_ID(lpsPropTags->__ptr[i]) );

		er = lpDatabase->DoDelete(strQuery);
		if(er != erSuccess)
			return er;
			
		// Remove from tproperties
		if((lpsPropTags->__ptr[i]&MV_FLAG) == 0) {
			strQuery = "DELETE FROM tproperties WHERE hierarchyid="+stringify(ulObjId)+" AND tag="+stringify(PROP_ID(lpsPropTags->__ptr[i]));

			er = lpDatabase->DoDelete(strQuery);
			if(er != erSuccess)
				return er;
		}			

		// Remove eml attachment
		if (lpsPropTags->__ptr[i] == PR_EC_IMAP_EMAIL) {
			std::list<ULONG> at_list;
			at_list.emplace_back(ulObjId);
			at_storage->DeleteAttachments(at_list);
		}

		// Update cache with NOT_FOUND for this property
		key.ulObjId = ulObjId;
		key.ulOrderId = 0;
		sPropVal.ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(lpsPropTags->__ptr[i]));
		sPropVal.Value.ul = KCERR_NOT_FOUND;
		sPropVal.__union = SOAP_UNION_propValData_ul;

		g_lpSessionManager->GetCacheManager()->SetCell(&key, lpsPropTags->__ptr[i], &sPropVal);
	}
	return erSuccess;
}

static unsigned int SaveObject(struct soap *soap, ECSession *lpecSession,
    ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage,
    unsigned int ulStoreId, unsigned int ulParentObjId,
    unsigned int ulParentType, unsigned int ulFlags, unsigned int ulSyncId,
    struct saveObject *lpsSaveObj, struct saveObject *lpsReturnObj,
    unsigned int ulLevel, bool *lpfHaveChangeKey = NULL)
{
	ECRESULT er = erSuccess;
	ALLOC_DBRESULT();
	int n;
	unsigned int ulParentObjType = 0;
	unsigned int ulSize = 0;
	unsigned int ulObjId = 0;
	bool fNewItem = false;
	bool fHasAttach = false;
	bool fGenHasAttach = false;
	ECListDeleteItems lstDeleteItems;
	ECListDeleteItems lstDeleted;
	FILETIME ftCreated = { 0,0 };
	FILETIME ftModified = { 0,0 };

	if (ulLevel <= 0)
		return KCERR_TOO_COMPLEX;

	// reset return object
	lpsReturnObj->__size = 0;
	lpsReturnObj->__ptr = NULL;
	lpsReturnObj->delProps.__size = 0;
	lpsReturnObj->delProps.__ptr = NULL;
	lpsReturnObj->modProps.__size = 0;
	lpsReturnObj->modProps.__ptr = NULL;
	lpsReturnObj->bDelete = false;
	lpsReturnObj->ulClientId = lpsSaveObj->ulClientId;
	lpsReturnObj->ulServerId = lpsSaveObj->ulServerId;
	lpsReturnObj->ulObjType = lpsSaveObj->ulObjType;
	lpsReturnObj->lpInstanceIds = NULL;

	if (lpsSaveObj->ulServerId == 0) {
		if (ulParentObjId == 0)
			return KCERR_INVALID_PARAMETER;
		er = CreateObject(lpecSession, lpDatabase, ulParentObjId, ulParentType, lpsSaveObj->ulObjType, ulFlags, &lpsReturnObj->ulServerId);
		if (er != erSuccess)
			return er;
		fNewItem = true;

		ulObjId = lpsReturnObj->ulServerId;
	} else {
	    ulObjId = lpsSaveObj->ulServerId;
    }

	if (lpsSaveObj->bDelete) {
		// make list of all children object IDs in std::list<int> ?
		ECListInt lstDel;
		lstDel.emplace_back(lpsSaveObj->ulServerId);

		// we always hard delete, because we can only delete submessages here
		// make sure we also delete message-in-message attachments, so all message related flags are on
		ULONG ulDelFlags = EC_DELETE_CONTAINER | EC_DELETE_MESSAGES | EC_DELETE_RECIPIENTS | EC_DELETE_ATTACHMENTS | EC_DELETE_HARD_DELETE;

		// Collect recursive parent objects, validate item and check the permissions
		er = ExpandDeletedItems(lpecSession, lpDatabase, &lstDel, ulDelFlags, false, &lstDeleteItems);
		if (er != erSuccess) {
			assert(false);
			goto exit;
		}

		er = DeleteObjectHard(lpecSession, lpDatabase, lpAttachmentStorage, ulDelFlags, lstDeleteItems, true, lstDeleted);
		if (er != erSuccess) {
			assert(false);
			goto exit;
		}

		er = DeleteObjectStoreSize(lpecSession, lpDatabase, ulDelFlags, lstDeleted);
		if (er != erSuccess) {
			assert(false);
			goto exit;
		}

		er = DeleteObjectCacheUpdate(lpecSession, ulDelFlags, lstDeleted);
		if (er != erSuccess) {
			assert(false);
			goto exit;
		}

		// object deleted, so we're done
		goto exit;
	}

	// ------
	// the following code is only for objects that still exist
	// ------
	{

		// Do not delete properties if this is a new object: this avoids any delete queries that cause unnecessary locks on the tables
		if (lpsSaveObj->delProps.__size > 0 && !fNewItem) {
			er = DeleteProps(lpecSession, lpDatabase, lpsReturnObj->ulServerId, &lpsSaveObj->delProps, lpAttachmentStorage);
			if (er != erSuccess)
				goto exit;
		}

		if (lpsSaveObj->modProps.__size > 0) {
			er = WriteProps(soap, lpecSession, lpDatabase, lpAttachmentStorage, lpsSaveObj, lpsReturnObj->ulServerId, fNewItem, ulSyncId, lpsReturnObj, lpfHaveChangeKey, &ftCreated, &ftModified);
			if (er != erSuccess)
				goto exit;
		}

		// check children
		if (lpsSaveObj->__size > 0) {
			lpsReturnObj->__size = lpsSaveObj->__size;
			lpsReturnObj->__ptr = s_alloc<struct saveObject>(soap, lpsReturnObj->__size);

			for (gsoap_size_t i = 0; i < lpsSaveObj->__size; ++i) {
				er = SaveObject(soap, lpecSession, lpDatabase, lpAttachmentStorage, ulStoreId, /*myself as parent*/lpsReturnObj->ulServerId, lpsReturnObj->ulObjType, 0, ulSyncId, &lpsSaveObj->__ptr[i], &lpsReturnObj->__ptr[i], lpsReturnObj->ulObjType == MAPI_MESSAGE ? ulLevel-1 : ulLevel);
				if (er != erSuccess)
					goto exit;
			}
		}
		
		if (lpsReturnObj->ulObjType == MAPI_MESSAGE) {
			// Generate properties that we need to generate (PR_HASATTACH, PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME)
			if (fNewItem) {
				// We have to write PR_HASTTACH since it is a new object
				fGenHasAttach = true;
				// We can generate PR_HASATTACH from the passed object data
				for (gsoap_size_t i = 0; i < lpsSaveObj->__size; ++i)
					if(lpsSaveObj->__ptr[i].ulObjType == MAPI_ATTACH) {
						fHasAttach = true;
						break;
					}
			} else {
				// Modified object. Only change PR_HASATTACH if something has changed
				for (gsoap_size_t i = 0; i < lpsSaveObj->__size; ++i)
					if(lpsSaveObj->__ptr[i].ulObjType == MAPI_ATTACH && (lpsSaveObj->__ptr[i].bDelete || lpsSaveObj->__ptr[i].ulServerId == 0)) {
						// An attachment was deleted or added in this call
						fGenHasAttach = true;
						break;
					}
				if(fGenHasAttach) {
					// An attachment was added or deleted, check the database to see if any attachments are left.
					strQuery = "SELECT id FROM hierarchy WHERE parent=" + stringify(lpsReturnObj->ulServerId) + " AND type=" + stringify(MAPI_ATTACH) + " LIMIT 1";
					
					er = lpDatabase->DoSelect(strQuery, &lpDBResult);
					if(er != erSuccess)
						goto exit;
					fHasAttach = lpDBResult.get_num_rows() > 0;
				}
			}
			
			if(fGenHasAttach) {
				// We have to generate/update PR_HASATTACH
				unsigned int ulParentTmp, ulOwnerTmp, ulFlagsTmp, ulTypeTmp;
				
				sObjectTableKey key(lpsReturnObj->ulServerId, 0);
				struct propVal sPropHasAttach;
				sPropHasAttach.ulPropTag = PR_HASATTACH;
				sPropHasAttach.Value.b = fHasAttach;
				sPropHasAttach.__union = SOAP_UNION_propValData_b;
		
				// Write in properties		
				strQuery.clear();
				WriteSingleProp(lpDatabase, lpsReturnObj->ulServerId, ulParentObjId, &sPropHasAttach, false, 0, strQuery);
				er = lpDatabase->DoInsert(strQuery);
				if(er != erSuccess)
					goto exit;
					
				// Write in tproperties
				strQuery.clear();
				WriteSingleProp(lpDatabase, lpsReturnObj->ulServerId, ulParentObjId, &sPropHasAttach, true, 0, strQuery);
				er = lpDatabase->DoInsert(strQuery);
				if(er != erSuccess)
					goto exit;
				
				// Update cache, since it may have been written before by WriteProps with a possibly wrong value
				g_lpSessionManager->GetCacheManager()->SetCell(&key, PR_HASATTACH, &sPropHasAttach);
				
				// Update MSGFLAG_HASATTACH in the same way. We can assume PR_MESSAGE_FLAGS is already available, so we
				// just do an update (instead of REPLACE INTO)
				strQuery = std::string("UPDATE properties SET val_ulong = val_ulong ") + (fHasAttach ? " | 16 " : " & ~16") + " WHERE hierarchyid = " + stringify(lpsReturnObj->ulServerId) + " AND tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS));
				er = lpDatabase->DoUpdate(strQuery);
				if(er != erSuccess)
					goto exit;
					
				// Update cache if it's actually in the cache
				if(g_lpSessionManager->GetCacheManager()->GetCell(&key, PR_MESSAGE_FLAGS, &sPropHasAttach, soap, false) == erSuccess) {
					sPropHasAttach.Value.ul &= ~MSGFLAG_HASATTACH;
					sPropHasAttach.Value.ul |= fHasAttach ? MSGFLAG_HASATTACH : 0;
					g_lpSessionManager->GetCacheManager()->SetCell(&key, PR_MESSAGE_FLAGS, &sPropHasAttach);
				}

				// More cache
				/* All this seems to be doing is getting the object into the cache (if not already there), or updating the cache entry timestamp. Suspicious. */
				if (g_lpSessionManager->GetCacheManager()->GetObject(lpsReturnObj->ulServerId, &ulParentTmp, &ulOwnerTmp, &ulFlagsTmp, &ulTypeTmp) == erSuccess)
					g_lpSessionManager->GetCacheManager()->SetObject(lpsReturnObj->ulServerId, ulParentTmp, ulOwnerTmp, ulFlagsTmp, ulTypeTmp);
			}
		}
		// 1. calc size of object, now that all children are saved.

		if (lpsReturnObj->ulObjType == MAPI_MESSAGE || lpsReturnObj->ulObjType == MAPI_ATTACH) {
			// Remove old size
			if (fNewItem != true && lpsReturnObj->ulObjType == MAPI_MESSAGE && ulParentType == MAPI_FOLDER) {
				if (GetObjectSize(lpDatabase, lpsReturnObj->ulServerId, &ulSize) == erSuccess)
					er = UpdateObjectSize(lpDatabase, ulStoreId, MAPI_STORE, UPDATE_SUB, ulSize);
				if (er != erSuccess)
					goto exit;
			}

			// Add new size
			if (CalculateObjectSize(lpDatabase, lpsReturnObj->ulServerId, lpsReturnObj->ulObjType, &ulSize) == erSuccess) {
				er = UpdateObjectSize(lpDatabase, lpsReturnObj->ulServerId, lpsReturnObj->ulObjType, UPDATE_SET, ulSize);
				if (er != erSuccess)
					goto exit;

				if (lpsReturnObj->ulObjType == MAPI_MESSAGE && ulParentType == MAPI_FOLDER) {
					er = UpdateObjectSize(lpDatabase, ulStoreId, MAPI_STORE, UPDATE_ADD, ulSize);
					if (er != erSuccess)
						goto exit;
				}
			} else {
				assert(false);
			}
		}

		// 2. find props to return

		// the server returns the following 4 properties, when the item is new:
		//   PR_CREATION_TIME, (PR_PARENT_SOURCE_KEY, PR_SOURCE_KEY /type==5|3) (PR_RECORD_KEY /type==7|5|3)
		// TODO: recipients: PR_INSTANCE_KEY
		// it always sends the following property:
		//   PR_LAST_MODIFICATION_TIME
		// currently, it always sends them all
		// we also can't send the PR_MESSAGE_SIZE and PR_MESSAGE_FLAGS, since the recursion is the wrong way around: attachments come later than the actual message
		// we can skip PR_ACCESS and PR_ACCESS_LEVEL because the client already inherited those from the parent
		// we need to alloc 2 properties for PR_CHANGE_KEY and PR_PREDECESSOR_CHANGE_LIST
		lpsReturnObj->delProps.__size = 8;
		lpsReturnObj->delProps.__ptr = s_alloc<unsigned int>(soap, lpsReturnObj->delProps.__size);
		lpsReturnObj->modProps.__size = 8;
		lpsReturnObj->modProps.__ptr = s_alloc<struct propVal>(soap, lpsReturnObj->modProps.__size);

		n = 0;

		// set the PR_RECORD_KEY
		// New clients generate the instance key, old clients don't. See if one was provided.
		if (lpsSaveObj->ulObjType == MAPI_ATTACH || lpsSaveObj->ulObjType == MAPI_MESSAGE || lpsSaveObj->ulObjType == MAPI_FOLDER) {
			bool bSkip = false;
			if (lpsSaveObj->ulObjType == MAPI_ATTACH) {
				for (gsoap_size_t i = 0; !bSkip && i < lpsSaveObj->modProps.__size; ++i)
					bSkip = lpsSaveObj->modProps.__ptr[i].ulPropTag == PR_RECORD_KEY;
				// @todo if we don't have a pr_record_key for an attachment, generate a guid for it like the client does
			}
			if (!bSkip && ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_RECORD_KEY, lpsSaveObj->ulServerId, 0, 0, ulParentObjId, lpsSaveObj->ulObjType, &lpsReturnObj->modProps.__ptr[n]) == erSuccess)
				lpsReturnObj->delProps.__ptr[n++] = PR_RECORD_KEY;
		}

		if (lpsSaveObj->ulObjType != MAPI_STORE) {
			er = g_lpSessionManager->GetCacheManager()->GetObject(ulParentObjId, NULL, NULL, NULL, &ulParentObjType);
			if(er != erSuccess)
				goto exit;
		}

		//PR_PARENT_SOURCE_KEY for folders and messages
		if (lpsSaveObj->ulObjType == MAPI_FOLDER || (lpsSaveObj->ulObjType == MAPI_MESSAGE && ulParentObjType == MAPI_FOLDER))
		{
			if(ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_PARENT_SOURCE_KEY, ulObjId, 0, 0, ulParentObjId, lpsSaveObj->ulObjType, &lpsReturnObj->modProps.__ptr[n]) == erSuccess)
				lpsReturnObj->delProps.__ptr[n++] = PR_PARENT_SOURCE_KEY;
			if(ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_SOURCE_KEY, ulObjId, 0, 0, ulParentObjId, lpsSaveObj->ulObjType, &lpsReturnObj->modProps.__ptr[n]) == erSuccess)
				lpsReturnObj->delProps.__ptr[n++] = PR_SOURCE_KEY;
		}

		// PR_LAST_MODIFICATION_TIME
		lpsReturnObj->delProps.__ptr[n] = PR_LAST_MODIFICATION_TIME;

		lpsReturnObj->modProps.__ptr[n].__union = SOAP_UNION_propValData_hilo;
		lpsReturnObj->modProps.__ptr[n].ulPropTag = PR_LAST_MODIFICATION_TIME;
		lpsReturnObj->modProps.__ptr[n].Value.hilo = s_alloc<hiloLong>(soap);

		lpsReturnObj->modProps.__ptr[n].Value.hilo->hi = ftModified.dwHighDateTime;
		lpsReturnObj->modProps.__ptr[n].Value.hilo->lo = ftModified.dwLowDateTime;
		++n;
		if (fNewItem)
		{
			lpsReturnObj->delProps.__ptr[n] = PR_CREATION_TIME;

			lpsReturnObj->modProps.__ptr[n].__union = SOAP_UNION_propValData_hilo;
			lpsReturnObj->modProps.__ptr[n].ulPropTag = PR_CREATION_TIME;
			lpsReturnObj->modProps.__ptr[n].Value.hilo = s_alloc<hiloLong>(soap);

			lpsReturnObj->modProps.__ptr[n].Value.hilo->hi = ftCreated.dwHighDateTime;
			lpsReturnObj->modProps.__ptr[n].Value.hilo->lo = ftCreated.dwLowDateTime;
			++n;
		}

		// set actual array size
		lpsReturnObj->delProps.__size = n;
		lpsReturnObj->modProps.__size = n;
	}

exit:
	FreeDeletedItems(&lstDeleteItems);

	return er;
}

SOAP_ENTRY_START(saveObject, lpsLoadObjectResponse->er, entryId sParentEntryId, entryId sEntryId, struct saveObject *lpsSaveObj, unsigned int ulFlags, unsigned int ulSyncId, struct loadObjectResponse *lpsLoadObjectResponse)
{
	USE_DATABASE();
	unsigned int	ulStoreId = 0;
	unsigned int	ulGrandParent = 0;
	unsigned int	ulGrandParentType = 0;
	unsigned int	ulParentObjId = 0;
	unsigned int	ulParentObjType = 0;
	unsigned int	ulObjType = lpsSaveObj->ulObjType;
	unsigned int	ulObjFlags = 0;
	unsigned int	ulPrevReadState = 0;
	unsigned int	ulNewReadState = 0;
	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey;
	struct saveObject sReturnObject;
	std::string		strChangeKey;
	std::string		strChangeList;

	BOOL			fNewItem = false;
	bool			fHaveChangeKey = false;
	unsigned int	ulObjId = 0;
	struct propVal	*pvCommitTime = NULL;
	object_ptr<ECAttachmentStorage> lpAttachmentStorage;

	er = CreateAttachmentStorage(lpDatabase, &~lpAttachmentStorage);
	if (er != erSuccess)
		return er;
	auto atx = lpAttachmentStorage->Begin(er);
	if (er != erSuccess)
		return er;
	er = lpDatabase->Begin();
	if (er != erSuccess)
		goto exit;
		
	if (!sParentEntryId.__ptr) {
		// saveObject is called on the store itself (doesn't have a parent)
		ulParentObjType = MAPI_STORE;
		ulStoreId = lpsSaveObj->ulServerId;
	} else {
		if (lpsSaveObj->ulServerId == 0) {
			// new object, parent entry id given by client
			er = lpecSession->GetObjectFromEntryId(&sParentEntryId, &ulParentObjId);
			if (er != erSuccess)
				goto exit;

			fNewItem = true;

			// Lock folder counters now
            strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid = " + stringify(ulParentObjId) + " FOR UPDATE";
            er = lpDatabase->DoSelect(strQuery, NULL);
			if (er != erSuccess)
			    goto exit;
		} else {
			// existing item, search parent ourselves because the client just sent its store entryid (see ECMsgStore::OpenEntry())
			er = g_lpSessionManager->GetCacheManager()->GetObject(lpsSaveObj->ulServerId, &ulParentObjId, NULL, &ulObjFlags, &ulObjType);
			if (er != erSuccess)
				goto exit;
				
            if (ulObjFlags & MSGFLAG_DELETED) {
                er = KCERR_OBJECT_DELETED;
                goto exit;
            }

			fNewItem = false;
			
			// Lock folder counters now
            strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid = " + stringify(ulParentObjId) + " FOR UPDATE";
            er = lpDatabase->DoSelect(strQuery, NULL);
			if (er != erSuccess)
			    goto exit;
            
            // We also need the old read flags so we can compare the new read flags to see if we need to update the unread counter. Note
            // that the read flags can only be modified through saveObject() when using ICS.
            
            strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid = " + stringify(lpsSaveObj->ulServerId) + " AND tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) + " LIMIT 1";
            er = lpDatabase->DoSelect(strQuery, &lpDBResult);
            if (er != erSuccess)
                goto exit;
			lpDBRow = lpDBResult.fetch_row();
            if (lpDBRow == nullptr || lpDBRow[0] == nullptr)
                ulPrevReadState = 0;
            else
                ulPrevReadState = atoui(lpDBRow[0]) & MSGFLAG_READ;
            
            ulNewReadState = ulPrevReadState; // Copy read state, may be updated by SaveObject() later
		}

		if (ulParentObjId != CACHE_NO_PARENT) {
			er = g_lpSessionManager->GetCacheManager()->GetObject(ulParentObjId, NULL, NULL, NULL, &ulParentObjType);
			if(er != erSuccess)
				goto exit;

			er = lpecSession->GetSessionManager()->GetCacheManager()->GetStore(ulParentObjId, &ulStoreId, NULL);
			if(er != erSuccess)
				goto exit;
		} else {
			// we get here on create store, but I'm not exactly sure why :|
			ulParentObjType = MAPI_STORE;
			ulStoreId = lpsSaveObj->ulServerId;
		}
	}

	if (ulParentObjId != 0 && ulParentObjType != MAPI_STORE) {
		er = g_lpSessionManager->GetCacheManager()->GetObject(ulParentObjId, &ulGrandParent, NULL, NULL, &ulGrandParentType);
		if(er != erSuccess)
			goto exit;
	}

	// Check permissions
	if (!fNewItem && ulObjType == MAPI_FOLDER)
		er = lpecSession->GetSecurity()->CheckPermission(lpsSaveObj->ulServerId, ecSecurityFolderAccess);
	else if(fNewItem)
		er = lpecSession->GetSecurity()->CheckPermission(ulParentObjId, ecSecurityCreate);
	else
		er = lpecSession->GetSecurity()->CheckPermission(lpsSaveObj->ulServerId, ecSecurityEdit);
	if(er != erSuccess)
		goto exit;
	
	// Quota check
	if(ulObjType == MAPI_MESSAGE) {
		er = CheckQuota(lpecSession, ulStoreId);
		if(er != erSuccess)
		    goto exit;
	
		// Update folder counts
		if(fNewItem) {
			er = UpdateFolderCounts(lpDatabase, ulParentObjId, ulFlags, &lpsSaveObj->modProps);
			if (er != erSuccess)
				goto exit;
		}
		else if(ulSyncId != 0) {
			// On modified appointments, unread flags may have changed (only possible during ICS import)
			for (gsoap_size_t i = 0; i < lpsSaveObj->modProps.__size; ++i)
				if(lpsSaveObj->modProps.__ptr[i].ulPropTag == PR_MESSAGE_FLAGS) {
					ulNewReadState = lpsSaveObj->modProps.__ptr[i].Value.ul & MSGFLAG_READ;
					break;
				}
			if (ulPrevReadState != ulNewReadState) {
				er = UpdateFolderCount(lpDatabase, ulParentObjId, PR_CONTENT_UNREAD, ulNewReadState == MSGFLAG_READ ? -1 : 1);
				if (er != erSuccess)
					goto exit;
			}
		}
	}

	er = SaveObject(soap, lpecSession, lpDatabase, lpAttachmentStorage,
	     ulStoreId, ulParentObjId, ulParentObjType, ulFlags, ulSyncId,
	     lpsSaveObj, &sReturnObject,
	     /* message itself occupies another level */
	     1 + atoui(g_lpSessionManager->GetConfig()->GetSetting("embedded_attachment_limit")),
	     &fHaveChangeKey);
	if (er == KCERR_TOO_COMPLEX)
		ec_log_debug("saveObject: refusing to store object \"%s\" (store %u): too many levels of attachments/subobjects",
			sEntryId.__ptr != nullptr ? base64_encode(sEntryId.__ptr, sEntryId.__size).c_str() : "", ulStoreId);
	if (er != erSuccess)
		goto exit;

	// update PR_LOCAL_COMMIT_TIME_MAX for disconnected clients who want to know if the folder contents changed
	if (ulObjType == MAPI_MESSAGE && ulParentObjType == MAPI_FOLDER) {
		er = WriteLocalCommitTimeMax(soap, lpDatabase, ulParentObjId, &pvCommitTime);
		if(er != erSuccess)
			goto exit;
	}

	if (lpsSaveObj->ulServerId == 0) {
		gsoap_size_t rki;
		er = MapEntryIdToObjectId(lpecSession, lpDatabase, sReturnObject.ulServerId, sEntryId);
		if (er != erSuccess)
			goto exit;

		ulObjId = sReturnObject.ulServerId;

		// now that we have an entry id, find the generated PR_RECORD_KEY from SaveObject and override it with the PR_ENTRYID value (fixme, ZCP-6706)
		for (rki = 0; rki < sReturnObject.modProps.__size; ++rki)
			if (sReturnObject.modProps.__ptr[rki].ulPropTag == PR_RECORD_KEY)
				break;
		// @note static alloc of 8 props in SaveObject. we did not find the record key: make it now
		if (rki == sReturnObject.modProps.__size && rki < 8) {
			ECGenProps::GetPropComputedUncached(soap, NULL, lpecSession, PR_RECORD_KEY, sReturnObject.ulServerId, 0, 0, ulParentObjId,
				lpsSaveObj->ulObjType, &sReturnObject.modProps.__ptr[rki]);
			++sReturnObject.modProps.__size;
			sReturnObject.delProps.__ptr[rki] = PR_RECORD_KEY;
			++sReturnObject.delProps.__size;
		}
	} else {
		ulObjId = lpsSaveObj->ulServerId;
	}

	// 3. pr_source_key magic
	if ((sReturnObject.ulObjType == MAPI_MESSAGE && ulParentObjType == MAPI_FOLDER) ||
		(sReturnObject.ulObjType == MAPI_FOLDER && !(ulFlags & FOLDER_SEARCH)))
	{
		GetSourceKey(sReturnObject.ulServerId, &sSourceKey);
		GetSourceKey(ulParentObjId, &sParentSourceKey);

		if (sReturnObject.ulObjType == MAPI_MESSAGE && ulParentObjType == MAPI_FOLDER) {
			if (lpsSaveObj->ulServerId == 0)
				AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, ICS_MESSAGE_NEW, 0, !fHaveChangeKey, &strChangeKey, &strChangeList);
			else
				AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, ICS_MESSAGE_CHANGE, 0, !fHaveChangeKey, &strChangeKey, &strChangeList);
		} else if (lpsSaveObj->ulObjType == MAPI_FOLDER && !(ulFlags & FOLDER_SEARCH)) {
			AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, ICS_FOLDER_CHANGE, 0, !fHaveChangeKey, &strChangeKey, &strChangeList);
		}

		if(!strChangeKey.empty()){
			sReturnObject.delProps.__ptr[sReturnObject.delProps.__size] = PR_CHANGE_KEY;
			++sReturnObject.delProps.__size;
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].ulPropTag = PR_CHANGE_KEY;
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].__union = SOAP_UNION_propValData_bin;
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].Value.bin = s_alloc<struct xsd__base64Binary>(soap);
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].Value.bin->__size = strChangeKey.size();
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].Value.bin->__ptr = s_alloc<unsigned char>(soap, strChangeKey.size());
			memcpy(sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].Value.bin->__ptr, strChangeKey.c_str(), strChangeKey.size());
			++sReturnObject.modProps.__size;
		}

		if(!strChangeList.empty()){
			sReturnObject.delProps.__ptr[sReturnObject.delProps.__size] = PR_PREDECESSOR_CHANGE_LIST;
			++sReturnObject.delProps.__size;
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].ulPropTag = PR_PREDECESSOR_CHANGE_LIST;
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].__union = SOAP_UNION_propValData_bin;
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].Value.bin = s_alloc<struct xsd__base64Binary>(soap);
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].Value.bin->__size = strChangeList.size();
			sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].Value.bin->__ptr = s_alloc<unsigned char>(soap, strChangeList.size());
			memcpy(sReturnObject.modProps.__ptr[sReturnObject.modProps.__size].Value.bin->__ptr, strChangeList.c_str(), strChangeList.size());
			++sReturnObject.modProps.__size;
		}
	}

	// 5. TODO: ulSyncId updates sync tables

	// 6. process MSGFLAG_SUBMIT if needed
	er = ProcessSubmitFlag(lpDatabase, ulSyncId, ulStoreId, ulObjId, fNewItem, &lpsSaveObj->modProps);
	if (er != erSuccess)
		goto exit;

	if (ulParentObjType == MAPI_FOLDER) {
		er = ECTPropsPurge::NormalizeDeferredUpdates(lpecSession, lpDatabase, ulParentObjId);
		if (er != erSuccess)
			goto exit;
	}

	er = atx.commit();
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->Commit();
	if (er != erSuccess)
		goto exit;

	// 7. notification
	// Only Notify on MAPI_MESSAGE, MAPI_FOLDER and MAPI_STORE
	// but don't nofity if parent object is a store and object type is attachment or message
	CreateNotifications(ulObjId, ulObjType, ulParentObjId, ulGrandParent, fNewItem, &lpsSaveObj->modProps, pvCommitTime);

	lpsLoadObjectResponse->sSaveObject = sReturnObject;

	g_lpStatsCollector->Increment(SCN_DATABASE_MWOPS);
	
exit:
	ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

static ECRESULT LoadObject(struct soap *soap, ECSession *lpecSession,
    unsigned int ulObjId, unsigned int ulObjType, unsigned int ulParentObjType,
    struct saveObject *lpsSaveObj,
    std::map<unsigned int, CHILDPROPS> *lpChildProps)
{
	ECRESULT 		er = erSuccess;
	object_ptr<ECAttachmentStorage> lpAttachmentStorage;
	ULONG			ulInstanceTag = 0;
	struct saveObject sSavedObject;
	GUID			sGuidServer;
	ChildPropsMap mapChildProps;
	ChildPropsMap::const_iterator iterProps;
	USE_DATABASE();
	CHILDPROPS		sEmptyProps;
	
	sEmptyProps.lpPropVals = new DynamicPropValArray(soap);
	sEmptyProps.lpPropTags = new DynamicPropTagArray(soap);

	memset(&sSavedObject, 0, sizeof(saveObject));

	// Check permission
	if (ulObjType == MAPI_STORE || (ulObjType == MAPI_FOLDER && ulParentObjType == MAPI_STORE))
		// Always read rights on the store and the root folder
		er = erSuccess;
	else if (ulObjType == MAPI_FOLDER)
		er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityFolderVisible);
	else if (ulObjType == MAPI_MESSAGE)
		er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityRead);
		
    // Allow reading MAPI_MAILUSER and MAPI_ATTACH since that is only called internally

	if (er != erSuccess)
		goto exit;

	sSavedObject.ulClientId = 0;
	sSavedObject.ulServerId = ulObjId;
	sSavedObject.ulObjType = ulObjType;
	
	if(lpChildProps == NULL) {
	    // We were not provided with a property list for this object, get our own now.
	    er = PrepareReadProps(soap, lpDatabase, true, lpecSession->GetCapabilities() & KOPANO_CAP_UNICODE, ulObjId, 0, MAX_PROP_SIZE, &mapChildProps, NULL);
	    if(er != erSuccess)
	        goto exit;
	        
        lpChildProps = &mapChildProps;
    }
	
	iterProps = lpChildProps->find(ulObjId);
	if (iterProps == lpChildProps->cend())
		er = ReadProps(soap, lpecSession, ulObjId, ulObjType, ulParentObjType, sEmptyProps, &sSavedObject.delProps, &sSavedObject.modProps);
	else
		er = ReadProps(soap, lpecSession, ulObjId, ulObjType, ulParentObjType, iterProps->second, &sSavedObject.delProps, &sSavedObject.modProps);

	if (er != erSuccess)
		goto exit;
		
    FreeChildProps(&mapChildProps);
    

	if (ulObjType == MAPI_MESSAGE || ulObjType == MAPI_ATTACH) {
        // Pre-load *all* properties of *all* subobjects for fast accessibility
        er = PrepareReadProps(soap, lpDatabase, true, lpecSession->GetCapabilities() & KOPANO_CAP_UNICODE, 0, ulObjId, MAX_PROP_SIZE, &mapChildProps, NULL);
        if (er != erSuccess)
            goto exit;

		// find subobjects
		strQuery = "SELECT id, type FROM hierarchy WHERE parent="+stringify(ulObjId);
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if(er != erSuccess)
			goto exit;
		sSavedObject.__size = lpDBResult.get_num_rows();
		sSavedObject.__ptr = s_alloc<saveObject>(soap, sSavedObject.__size);
		memset(sSavedObject.__ptr, 0, sizeof(saveObject) * sSavedObject.__size);

		for (gsoap_size_t i = 0; i < sSavedObject.__size; ++i) {
			lpDBRow = lpDBResult.fetch_row();
			lpDBLen = lpDBResult.fetch_row_lengths();
			if(lpDBRow == NULL || lpDBLen == NULL) {
				er = KCERR_DATABASE_ERROR; // this should never happen
				ec_log_err("LoadObject(): no rows from db");
				goto exit;
			}
			
   			LoadObject(soap, lpecSession, atoi(lpDBRow[0]), atoi(lpDBRow[1]), ulObjType, &sSavedObject.__ptr[i], &mapChildProps);
		}
    	FreeChildProps(&mapChildProps);
	}

	if (ulObjType == MAPI_MESSAGE || ulObjType == MAPI_ATTACH) {
		er = CreateAttachmentStorage(lpDatabase, &~lpAttachmentStorage);
		if (er != erSuccess)
			goto exit;

		er = g_lpSessionManager->GetServerGUID(&sGuidServer);
		if (er != erSuccess)
			goto exit;

		/* Not having a single instance ID is not critical, we might create it at a later time */
		// @todo this should be GetSingleInstanceIdsWithTags(ulObjId, map<tag, instanceid>);
		if (ulObjType == MAPI_MESSAGE)
			ulInstanceTag = PROP_ID(PR_EC_IMAP_EMAIL);
		else
			ulInstanceTag = PROP_ID(PR_ATTACH_DATA_BIN);
		ext_siid ulInstanceId;
		if (lpAttachmentStorage->GetSingleInstanceId(ulObjId, ulInstanceTag, &ulInstanceId) == erSuccess) {
			sSavedObject.lpInstanceIds = s_alloc<entryList>(soap);
			sSavedObject.lpInstanceIds->__size = 1;
			sSavedObject.lpInstanceIds->__ptr = s_alloc<entryId>(soap, sSavedObject.lpInstanceIds->__size);
			er = SIIDToEntryID(soap, &sGuidServer, ulInstanceId.siid, ulInstanceTag, &sSavedObject.lpInstanceIds->__ptr[0]);
			if (er != erSuccess) {
				sSavedObject.lpInstanceIds->__size = 0;
				goto exit;
			}
		}
	}

	if (ulObjType == MAPI_MESSAGE) {
		// @todo: Check if we can do this on the fly to avoid the additional lookup.
		auto lm = g_lpSessionManager->GetLockManager();
		for (gsoap_size_t i = 0; i < sSavedObject.modProps.__size; ++i) {
			if (sSavedObject.modProps.__ptr[i].ulPropTag != PR_SUBMIT_FLAGS)
				continue;
			if (lm->IsLocked(ulObjId, nullptr))
				sSavedObject.modProps.__ptr[i].Value.ul |= SUBMITFLAG_LOCKED;
			else
				sSavedObject.modProps.__ptr[i].Value.ul &= ~SUBMITFLAG_LOCKED;
		}
	}

	*lpsSaveObj = std::move(sSavedObject);
exit:
	delete sEmptyProps.lpPropVals;
	delete sEmptyProps.lpPropTags;
	return er;
}

SOAP_ENTRY_START(loadObject, lpsLoadObjectResponse->er, entryId sEntryId, struct notifySubscribe *lpsNotSubscribe, unsigned int ulFlags, struct loadObjectResponse *lpsLoadObjectResponse)
{
	unsigned int	ulObjId = 0;
	unsigned int	ulObjFlags = 0;
	unsigned int	ulObjType = 0;
	unsigned int	ulParentId = 0;
	unsigned int	ulParentObjType = 0;
	unsigned int	ulOwnerId = 0;
	unsigned int	ulEidFlags = 0;
	USE_DATABASE();

	struct saveObject sSavedObject;

	er = BeginLockFolders(lpDatabase, EntryId(sEntryId), LOCK_SHARED);
	if(er != erSuccess)
	    goto exit;
	
	/*
	 * 2 Reasons to send KCERR_UNABLE_TO_COMPLETE (and have the client try to open the store elsewhere):
	 *  1. We can't find the object based on the entryid.
	 *  2. The owner of the store is not supposed to have a store on this server.
	 */
	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId, &ulEidFlags);
	if ((ulEidFlags & OPENSTORE_OVERRIDE_HOME_MDB) == 0 &&
	    er == KCERR_NOT_FOUND &&
	    sEntryId.__size >= static_cast<int>(std::min(sizeof(EID), sizeof(EID_V0))) &&
	    reinterpret_cast<EID *>(sEntryId.__ptr)->usType == MAPI_STORE)
		er = KCERR_UNABLE_TO_COMPLETE;	// Reason 1
	if (er != erSuccess)
		goto exit;
		
	er = g_lpSessionManager->GetCacheManager()->GetObject(ulObjId, &ulParentId, &ulOwnerId, &ulObjFlags, &ulObjType);
	if (er != erSuccess)
		goto exit;
    
	if(ulObjType == MAPI_STORE) {
		if ((ulEidFlags & OPENSTORE_OVERRIDE_HOME_MDB) == 0 &&
		    lpecSession->GetSessionManager()->IsDistributedSupported() &&
		    !lpecSession->GetUserManagement()->IsInternalObject(ulOwnerId)) {
			objectdetails_t sUserDetails;

			if (lpecSession->GetUserManagement()->GetObjectDetails(ulOwnerId, &sUserDetails) == erSuccess) {
				unsigned int ulStoreType;
				er = lpecSession->GetSessionManager()->GetCacheManager()->GetStoreAndType(ulObjId, NULL, NULL, &ulStoreType);
				if (er != erSuccess)
					goto exit;

				if (ulStoreType == ECSTORE_TYPE_PRIVATE || ulStoreType == ECSTORE_TYPE_PUBLIC) {
					std::string strServerName = sUserDetails.GetPropString(OB_PROP_S_SERVERNAME);
					if (strServerName.empty()) {
						er = KCERR_NOT_FOUND;
						goto exit;
					}

					if (strcasecmp(strServerName.c_str(), g_lpSessionManager->GetConfig()->GetSetting("server_name")) != 0) {
						er = KCERR_UNABLE_TO_COMPLETE;	// Reason 2
						goto exit;
					}
				} else if (ulStoreType == ECSTORE_TYPE_ARCHIVE) {
					// We allow an archive store to be opened by sysadmins even if it's not supposed
					// to exist on this server for a particular user.
					if (lpecSession->GetSecurity()->GetAdminLevel() < ADMIN_LEVEL_SYSADMIN &&
					   !sUserDetails.PropListStringContains((property_key_t)PR_EC_ARCHIVE_SERVERS_A, g_lpSessionManager->GetConfig()->GetSetting("server_name"), true))
					{
						er = KCERR_NOT_FOUND;
						goto exit;
					}
				} else {
					er = KCERR_NOT_FOUND;
					goto exit;
				}
			} else if (lpecSession->GetSecurity()->GetAdminLevel() < ADMIN_LEVEL_SYSADMIN) {
				// unhooked store of a deleted user
				er = KCERR_NO_ACCESS;
				goto exit;
			}
		}
        ulParentObjType = 0;
	} else if(ulObjType == MAPI_MESSAGE) {
		// If the object is locked on another session, access should be denied
		ECSESSIONID ulLockedSessionId;

		if (g_lpSessionManager->GetLockManager()->IsLocked(ulObjId, &ulLockedSessionId) && ulLockedSessionId != ulSessionId) {
			er = KCERR_NO_ACCESS;
			goto exit;
		}

		ulParentObjType = MAPI_FOLDER;
	} else if(ulObjType == MAPI_FOLDER) {
		er = g_lpSessionManager->GetCacheManager()->GetObject(ulParentId, NULL, NULL, NULL, &ulParentObjType);
		if (er != erSuccess)
			goto exit;

		// avoid reminders from shared stores by detecting that we are opening non-owned reminders folder
		if((ulObjFlags & FOLDER_SEARCH) &&
		   (lpecSession->GetSecurity()->IsStoreOwner(ulObjId) == KCERR_NO_ACCESS) &&
		   (lpecSession->GetSecurity()->GetAdminLevel() == 0) &&
		   (!parseBool(g_lpSessionManager->GetConfig()->GetSetting("shared_reminders"))))
		{
			strQuery = "SELECT val_string FROM properties WHERE hierarchyid=" + stringify(ulObjId) + " AND tag = " + stringify(PROP_ID(PR_CONTAINER_CLASS)) + " LIMIT 1";
			er = lpDatabase->DoSelect(strQuery, &lpDBResult);
			if(er != erSuccess)
				goto exit;

			if (lpDBResult.get_num_rows() == 1) {
				lpDBRow = lpDBResult.fetch_row();
				if (lpDBRow == NULL || lpDBRow[0] == NULL ) {
					ec_log_err("ECSearchObjectTable::Load(): row or columns null");
					er = KCERR_DATABASE_ERROR;
					goto exit;
				}
				if(!strcmp(lpDBRow[0], "Outlook.Reminder")) {
					er = KCERR_NOT_FOUND;
					goto exit;
				}
			}
		}

		// Reset folder counts now (Note: runs in a DB transaction!). Note: we only update counts when lpNotSubscribe is not NULL; this
		// makes sure that we only reset folder counts on the first open of a folder, and not when the folder properties are updated (eg
		// due to counter changes)
		if(lpsNotSubscribe && ulObjFlags != FOLDER_SEARCH && parseBool(g_lpSessionManager->GetConfig()->GetSetting("counter_reset")))
			ResetFolderCount(lpecSession, ulObjId);
	}

	// check if flags were passed, older clients call checkExistObject
    if(ulFlags & 0x80000000) {
        // Flags passed by client, check object flags
        ulFlags = ulFlags & ~0x80000000;
        
        if((ulObjFlags & MSGFLAG_DELETED) != ulFlags) {
        	if (ulObjType == MAPI_STORE)
        		er = KCERR_UNABLE_TO_COMPLETE;
        	else
            	er = KCERR_NOT_FOUND;
            goto exit;
        }
    }

	// Subscribe for notification
	if (lpsNotSubscribe) {
		er = DoNotifySubscribe(lpecSession, ulSessionId, lpsNotSubscribe);
		if (er != erSuccess)
			goto exit;
	}

	er = LoadObject(soap, lpecSession, ulObjId, ulObjType, ulParentObjType, &sSavedObject, NULL);
	if (er != erSuccess)
		goto exit;

	lpsLoadObjectResponse->sSaveObject = sSavedObject;

	g_lpStatsCollector->Increment(SCN_DATABASE_MROPS);

exit:
    lpDatabase->Commit();
}
SOAP_ENTRY_END()

// if lpsNewEntryId is NULL this function create a new entryid
// if lpsOrigSourceKey is NULL this function creates a new sourcekey
static ECRESULT CreateFolder(ECSession *lpecSession, ECDatabase *lpDatabase,
    unsigned int ulParentId, entryId *lpsNewEntryId, unsigned int type,
    const char *name, const char *comment, bool openifexists, bool bNotify,
    unsigned int ulSyncId, const struct xsd__base64Binary *lpsOrigSourceKey,
    unsigned int *lpFolderId, bool *lpbExist)
{
	ECRESULT		er = erSuccess;
	ALLOC_DBRESULT();

	unsigned int	ulFolderId = 0;
	unsigned int	ulLastId = 0;
	unsigned int	ulStoreId = 0;
	unsigned int	ulGrandParent = 0;
	bool			bExist = false;
	bool			bFreeNewEntryId = false;
	GUID			guid;
	SOURCEKEY		sSourceKey;
	unsigned int	tags [] = { PR_CONTENT_COUNT, PR_CONTENT_UNREAD, PR_ASSOC_CONTENT_COUNT, PR_DELETED_MSG_COUNT, PR_DELETED_FOLDER_COUNT, PR_DELETED_ASSOC_MSG_COUNT, PR_FOLDER_CHILD_COUNT };
	unsigned int	timeTags [] = { PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME };
	time_t			now = 0;
	struct propVal  sProp;
    struct hiloLong sHilo;

	er = lpecSession->GetSessionManager()->GetCacheManager()->GetStore(ulParentId, &ulStoreId, &guid);
	if(er != erSuccess)
		return er;
	er = g_lpSessionManager->GetCacheManager()->GetParent(ulParentId, &ulGrandParent);
	if(er != erSuccess)
		return er;

	// Check whether the requested name already exists

	strQuery = "SELECT hierarchy.id, properties.val_string FROM hierarchy JOIN properties ON hierarchy.id = properties.hierarchyid WHERE hierarchy.parent=" + stringify(ulParentId) + " AND hierarchy.type="+stringify(MAPI_FOLDER)+" AND hierarchy.flags & " + stringify(MSGFLAG_DELETED)+ "=0 AND properties.tag=" + stringify(KOPANO_TAG_DISPLAY_NAME) + " AND properties.val_string = '" + lpDatabase->Escape(name) + "' AND properties.type="+stringify(PT_STRING8);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	while (!bExist && (lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL) {
			ec_log_err("CreateFolder(): columns null");
			return KCERR_DATABASE_ERROR;
		}

		if (strcasecmp(lpDBRow[1], name) == 0)
			bExist = true;
	}

	if(bExist && !ulSyncId) {
		// Check folder read access
		er = lpecSession->GetSecurity()->CheckPermission(ulParentId, ecSecurityFolderVisible);
		if(er != erSuccess)
			return er;

		// Object exists
		if (!openifexists) {
			ec_log_err("CreateFolder(): folder already exists");
			return KCERR_COLLISION;
		}
		
		ulFolderId = atoi(lpDBRow[0]);
	}
	else {
		// Check write permission of the folder destination
		er = lpecSession->GetSecurity()->CheckPermission(ulParentId, ecSecurityCreateFolder);
		if(er != erSuccess)
			return er;

		// Create folder
		strQuery = "INSERT INTO hierarchy (parent, type, flags, owner) values(" + stringify(ulParentId) + "," + stringify(KOPANO_OBJTYPE_FOLDER) + ", " + stringify(type) + ", "+stringify(lpecSession->GetSecurity()->GetUserId(ulParentId))+")";
		er = lpDatabase->DoInsert(strQuery, &ulLastId);
		if(er != erSuccess)
			return er;

		if(lpsNewEntryId == NULL) {
			er = CreateEntryId(guid, MAPI_FOLDER, &lpsNewEntryId);
			if(er != erSuccess)
				return er;
			bFreeNewEntryId = true;
		}

		//Create entryid, 0x0FFF = PR_ENTRYID
		er = RemoveStaleIndexedProp(lpDatabase, PR_ENTRYID, lpsNewEntryId->__ptr, lpsNewEntryId->__size);
		if(er != erSuccess)
		    goto exit;
		strQuery = "INSERT INTO indexedproperties (hierarchyid,tag,val_binary) VALUES(" + stringify(ulLastId) + ", 4095, " + lpDatabase->EscapeBinary(lpsNewEntryId->__ptr, lpsNewEntryId->__size) + ")";
		er = lpDatabase->DoInsert(strQuery);
		if(er != erSuccess)
			goto exit;

		// Create Displayname
		strQuery = "INSERT INTO properties (hierarchyid, tag, type, val_string) values(" + stringify(ulLastId) + "," + stringify(KOPANO_TAG_DISPLAY_NAME) + "," + stringify(PT_STRING8) + ",'" + lpDatabase->Escape(name) + "')";
		er = lpDatabase->DoInsert(strQuery);
		if(er != erSuccess)
			goto exit;

		// Create Displayname
		strQuery = "INSERT INTO tproperties (hierarchyid, tag, type, folderid, val_string) values(" + stringify(ulLastId) + "," + stringify(KOPANO_TAG_DISPLAY_NAME) + "," + stringify(PT_STRING8) + "," + stringify(ulParentId) + ",'" + lpDatabase->Escape(name) + "')";
		er = lpDatabase->DoInsert(strQuery);
		if(er != erSuccess)
			goto exit;
			
		// Create counters
		for (size_t i = 0; i < ARRAY_SIZE(tags); ++i) {
			sProp.ulPropTag = tags[i];
			sProp.__union = SOAP_UNION_propValData_ul;
			sProp.Value.ul = 0;
			
			er = WriteProp(lpDatabase, ulLastId, ulParentId, &sProp);
			if(er != erSuccess)
				goto exit;
		}
		
		// Create PR_SUBFOLDERS
		sProp.ulPropTag = PR_SUBFOLDERS;
		sProp.__union = SOAP_UNION_propValData_b;
		sProp.Value.b = false;
		
		er = WriteProp(lpDatabase, ulLastId, ulParentId, &sProp);
		if(er != erSuccess)
			goto exit;
			
		// Create PR_FOLDERTYPE
		sProp.ulPropTag = PR_FOLDER_TYPE;
		sProp.__union = SOAP_UNION_propValData_ul;
		sProp.Value.ul = type;

		er = WriteProp(lpDatabase, ulLastId, ulParentId, &sProp);
		if(er != erSuccess)
			goto exit;

        // Create PR_COMMENT			
		if (comment) {
		    sProp.ulPropTag = PR_COMMENT_A;
		    sProp.__union = SOAP_UNION_propValData_lpszA;
			sProp.Value.lpszA = const_cast<char *>(comment);
		    er = WriteProp(lpDatabase, ulLastId, ulParentId, &sProp);
			if(er != erSuccess)
				goto exit;
		}
		
		// Create PR_LAST_MODIFICATION_TIME and PR_CREATION_TIME
		now = time(NULL);
		for (size_t i = 0; i < ARRAY_SIZE(timeTags); ++i) {
		    sProp.ulPropTag = timeTags[i];
		    sProp.__union = SOAP_UNION_propValData_hilo;
		    sProp.Value.hilo = &sHilo;
		    UnixTimeToFileTime(now, &sProp.Value.hilo->hi, &sProp.Value.hilo->lo);
		    
		    er = WriteProp(lpDatabase, ulLastId, ulParentId, &sProp);
			if(er != erSuccess)
				goto exit;
		}

		// Create SourceKey
		if (lpsOrigSourceKey && lpsOrigSourceKey->__size > (int)sizeof(GUID) && lpsOrigSourceKey->__ptr){
			sSourceKey = SOURCEKEY(lpsOrigSourceKey->__size, (char*)lpsOrigSourceKey->__ptr);
		}else{
			er = lpecSession->GetNewSourceKey(&sSourceKey);
			if(er != erSuccess)
				goto exit;
		}

		er = RemoveStaleIndexedProp(lpDatabase, PR_SOURCE_KEY, sSourceKey, sSourceKey.size());
		if(er != erSuccess)
		    goto exit;
		    
		strQuery = "INSERT INTO indexedproperties(hierarchyid,tag,val_binary) VALUES(" + stringify(ulLastId) + "," + stringify(PROP_ID(PR_SOURCE_KEY)) + "," + lpDatabase->EscapeBinary(sSourceKey, sSourceKey.size()) + ")";
		er = lpDatabase->DoInsert(strQuery);
		if(er != erSuccess)
			goto exit;

		ulFolderId = ulLastId;
		
		er = UpdateFolderCount(lpDatabase, ulParentId, PR_SUBFOLDERS, 1);
		if(er != erSuccess)
			goto exit;
		er = UpdateFolderCount(lpDatabase, ulParentId, PR_FOLDER_CHILD_COUNT, 1);
		if(er != erSuccess)
			goto exit;
	}

	if(bExist == false && !(type & FOLDER_SEARCH)){
		SOURCEKEY sParentSourceKey;
		
		GetSourceKey(ulParentId, &sParentSourceKey);
		AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, ICS_FOLDER_NEW);
	}

	// Notify that the folder has been created
	if(bExist == false && bNotify == true) {
		g_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, ulParentId);
		g_lpSessionManager->NotificationCreated(MAPI_FOLDER, ulFolderId, ulParentId);
		g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulParentId);

		// Update all tables viewing this folder
		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD, 0, ulParentId, ulFolderId, MAPI_FOLDER);

		// Update notification, grandparent of the mainfolder
		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulGrandParent, ulParentId, MAPI_FOLDER);
	}

	if(lpFolderId)
		*lpFolderId = ulFolderId;

	if(lpbExist)
		*lpbExist = bExist;

exit:
	if(bFreeNewEntryId == true && lpsNewEntryId)
		FreeEntryId(lpsNewEntryId, true);

	return er;
}

/**
 * createFolder: Create a folder object in the hierarchy table, and add a 'PR_DISPLAY_NAME' property.
 *
 * The data model actually supports multiple folders having the same PR_DISPLAY_NAME, however, MAPI does not,
 * so we have to give the engine the knowledge of the PR_DISPLAY_NAME property here, one of the few properties
 * that the backend engine actually knows about.
 *
 * Of course, the frontend could also enforce this constraint, but that would require 2 server accesses (1. check
 * existing, 2. create folder), and we're trying to keep the amount of server accesses as low as possible.
 *
 */

SOAP_ENTRY_START(createFolder, lpsResponse->er, entryId sParentId,
    entryId *lpsNewEntryId, unsigned int ulType, const char *szName,
    const char *szComment, bool fOpenIfExists, unsigned int ulSyncId,
    struct xsd__base64Binary sOrigSourceKey,
    struct createFolderResponse *lpsResponse)
{
	unsigned int	ulParentId = 0;
	unsigned int	ulFolderId = 0;
	USE_DATABASE();

	if (szName == nullptr)
		return KCERR_INVALID_PARAMETER;
	// Fixup the input strings
	szName = STRIN_FIX(szName);
	szComment = STRIN_FIX(szComment);

	er = lpDatabase->Begin();
	if (er != erSuccess)
		return er;
	er = lpecSession->GetObjectFromEntryId(&sParentId, &ulParentId);
	if (er != erSuccess)
		goto exit;

	er = CreateFolder(lpecSession, lpDatabase, ulParentId, lpsNewEntryId, ulType, szName, szComment, fOpenIfExists, true, ulSyncId, &sOrigSourceKey, &ulFolderId, NULL);
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->Commit();
	if (er != erSuccess)
		goto exit;

	er = g_lpSessionManager->GetCacheManager()->GetEntryIdFromObject(ulFolderId, soap, 0, &lpsResponse->sEntryId);
	if (er != erSuccess)
		goto exit;

exit:
	ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

/**
 * tableOpen: Open a mapi table
 *
 * @param[in]	lpecSession	server session object, cannot be NULL
 * @param[in]	sEntryId	entryid data
 * @param[in]	ulTableType	the type of table to open:
 *	TABLETYPE_MS
 *		For all tables of a messagestore.
 *	TABLETYPE_AB
 *		For the addressbook tables.
 *	TABLETYPE_SPOOLER
 *		For the spooler tables, the sEntryId must always the store entryid.
 *		ulType and ulFlags are ignored, reserved for future use.
 *	TABLE_TYPE_MULTISTORE
 *		Special Kopano-only table to have given objects from different stores in one table view.
 *	TABLE_TYPE_USERSTORES
 *		Special Kopano-only table. Lists all combinations of users and stores on this server. (Used for the orphan management in kopano-admin).
 *	TABLE_TYPE_STATS_*
 *		Special Kopano-only tables. Used for various statistics and other uses.
 * @param[in]	ulType		the type of the object you want to open.
 * @param[in]	ulFlags		ulFlags from the client
 *	MAPI_ASSOCIATED
 *		List associated messages/folders instead of normal messages/folders.
 *	MAPI_UNICODE
 *		Default and all columns will contain _W string properties, otherwise _A strings are used.
 *	CONVENIENT_DEPTH
 *		Returns a convenient depth (flat list) of all folders.
 *	SHOW_SOFT_DELETES
 *		List deleted items in this table, rewritten to MSGFLAG_DELETED.
 *	EC_TABLE_NOCAP
 *		Do not cap string entries to 255 bytes, an extension of ours.
 *
 * @param[out]	lpulTableId	Server table id for this new table.
 */
static ECRESULT OpenTable(ECSession *lpecSession, entryId sEntryId,
    unsigned int ulTableType, unsigned int ulType, unsigned int ulFlags,
    unsigned int *lpulTableId)
{
	ECRESULT er;
	objectid_t	sExternId;
	unsigned int	ulTableId = 0;
	unsigned int	ulId = 0;
	unsigned int	ulTypeId = 0;

	switch (ulTableType) {
	case TABLETYPE_MS:
		if (ulFlags & SHOW_SOFT_DELETES)
		{
			ulFlags &=~SHOW_SOFT_DELETES;
			ulFlags |= MSGFLAG_DELETED;
		}
		er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulId);
		if (er != erSuccess)
			return er;
		er = lpecSession->GetTableManager()->OpenGenericTable(ulId, ulType, ulFlags, &ulTableId);
		if (er != erSuccess)
			return er;
		break;
	case TABLETYPE_AB:
		er = ABEntryIDToID(&sEntryId, &ulId, &sExternId, &ulTypeId);
		if (er != erSuccess)
			return er;

		// If an extern id is present, we should get an object based on that.
		if (!sExternId.id.empty()) {
			er = g_lpSessionManager->GetCacheManager()->GetUserObject(sExternId, &ulId, NULL, NULL);
			if (er != erSuccess)
				return er;
		}
		er = lpecSession->GetTableManager()->OpenABTable(ulId, ulTypeId, ulType, ulFlags, &ulTableId);
		if (er != erSuccess)
			return er;
		break;
	case TABLETYPE_SPOOLER:
		// sEntryId must be a store entryid or zero for all stores
		if (sEntryId.__size > 0) {
			er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulId);
			if (er != erSuccess)
				return er;
		} else
			ulId = 0; //All stores

		er = lpecSession->GetTableManager()->OpenOutgoingQueueTable(ulId, &ulTableId);
		if (er != erSuccess)
			return er;
		break;
	case TABLETYPE_MULTISTORE:
		er = lpecSession->GetTableManager()->OpenMultiStoreTable(ulType, ulFlags, &ulTableId);
		if (er != erSuccess)
			return er;
		break;
	case TABLETYPE_USERSTORES:
		er = lpecSession->GetTableManager()->OpenUserStoresTable(ulFlags, &ulTableId);
		if (er != erSuccess)
			return er;
		break;
	case TABLETYPE_STATS_SYSTEM:
	case TABLETYPE_STATS_SESSIONS:
	case TABLETYPE_STATS_USERS:
	case TABLETYPE_STATS_COMPANY:
	case TABLETYPE_STATS_SERVERS:
		er = lpecSession->GetTableManager()->OpenStatsTable(ulTableType, ulFlags, &ulTableId);
		if (er != erSuccess)
			return er;
		break;
	case TABLETYPE_MAILBOX:
		er = lpecSession->GetTableManager()->OpenMailBoxTable(ulFlags, &ulTableId);
		if (er != erSuccess)
			return er;
		break;
	default:
		return KCERR_BAD_VALUE;
		break; //Happy compiler
	} // switch (ulTableType)

	*lpulTableId = ulTableId;
	return erSuccess;
}

SOAP_ENTRY_START(tableOpen, lpsTableOpenResponse->er, entryId sEntryId, unsigned int ulTableType, unsigned ulType, unsigned int ulFlags, struct tableOpenResponse *lpsTableOpenResponse)
{
	unsigned int ulTableId = 0;
    
	er = OpenTable(lpecSession, sEntryId, ulTableType, ulType, ulFlags, &ulTableId);
	if (er != erSuccess)
		return er;
	lpsTableOpenResponse->ulTableId = ulTableId;
	return erSuccess;
}
SOAP_ENTRY_END()

/**
 * tableClose: close the table with the specified table ID
 */
SOAP_ENTRY_START(tableClose, *result, unsigned int ulTableId, unsigned int *result)
{
	return lpecSession->GetTableManager()->CloseTable(ulTableId);
}
SOAP_ENTRY_END()

/**
 * tableSetSearchCritieria: set search criteria for a searchfolder
 */
SOAP_ENTRY_START(tableSetSearchCriteria, *result, entryId sEntryId, struct restrictTable *lpRestrict, struct entryList *lpFolders, unsigned int ulFlags, unsigned int *result)
{
	unsigned int	ulStoreId = 0;
	unsigned int	ulParent = 0;

	if (!(ulFlags & STOP_SEARCH) &&
	    (lpRestrict == nullptr || lpFolders == nullptr))
		return KCERR_INVALID_PARAMETER;
	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulParent);
	if(er != erSuccess)
		return er;
	er = lpecSession->GetSessionManager()->GetCacheManager()->GetStore(ulParent, &ulStoreId, NULL);
	if(er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->CheckPermission(ulParent, ecSecurityEdit);
	if(er != erSuccess)
		return er;

	// If a STOP was requested, then that's all we need to do
	if(ulFlags & STOP_SEARCH) {
		return lpecSession->GetSessionManager()->GetSearchFolders()->SetSearchCriteria(ulStoreId, ulParent, nullptr);
	} else {
		struct searchCriteria sSearchCriteria;

		if (!bSupportUnicode) {
			er = FixRestrictionEncoding(soap, stringCompat, In, lpRestrict);
			if (er != erSuccess)
				return er;
		}

		sSearchCriteria.lpRestrict = lpRestrict;
		sSearchCriteria.lpFolders = lpFolders;
		sSearchCriteria.ulFlags = ulFlags;
		return lpecSession->GetSessionManager()->GetSearchFolders()->SetSearchCriteria(ulStoreId, ulParent, &sSearchCriteria);
	}
}
SOAP_ENTRY_END()

/**
 * tableGetSearchCriteria: get the search criteria for a searchfolder previously called with tableSetSearchCriteria
 */
SOAP_ENTRY_START(tableGetSearchCriteria, lpsResponse->er,  entryId sEntryId, struct tableGetSearchCriteriaResponse *lpsResponse)
{
	unsigned int	ulFlags = 0;
	unsigned int	ulStoreId = 0;
	unsigned int	ulId = 0;
	struct searchCriteria *lpSearchCriteria = NULL;

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulId);
	if(er != erSuccess)
		return er;
	er = g_lpSessionManager->GetCacheManager()->GetStore(ulId, &ulStoreId, NULL);
	 if(er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->CheckPermission(ulId, ecSecurityRead);
	if(er != erSuccess)
		return er;
	er = lpecSession->GetSessionManager()->GetSearchFolders()->GetSearchCriteria(ulStoreId, ulId, &lpSearchCriteria, &ulFlags);
	if(er != erSuccess)
		return er;
	er = CopyRestrictTable(soap, lpSearchCriteria->lpRestrict, &lpsResponse->lpRestrict);
	if(er != erSuccess)
		goto exit;

	if (!bSupportUnicode) {
		er = FixRestrictionEncoding(soap, stringCompat, Out, lpsResponse->lpRestrict);
		if (er != erSuccess)
			goto exit;
	}

	er = CopyEntryList(soap, lpSearchCriteria->lpFolders, &lpsResponse->lpFolderIDs);
	if(er != erSuccess)
		goto exit;

	lpsResponse->ulFlags = ulFlags;

exit:
	FreeSearchCriteria(lpSearchCriteria);
}
SOAP_ENTRY_END()

/**
 * tableSetColumns: called from IMAPITable::SetColumns()
 */
SOAP_ENTRY_START(tableSetColumns, *result, unsigned int ulTableId, struct propTagArray *aPropTag, unsigned int *result)
{
	object_ptr<ECGenericObjectTable> lpTable;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	return lpTable->SetColumns(aPropTag, false);
}
SOAP_ENTRY_END()

/**
 * tableQueryColumns: called from IMAPITable::GetColumns()
 */
SOAP_ENTRY_START(tableQueryColumns, lpsResponse->er, unsigned int ulTableId, unsigned int ulFlags, struct tableQueryColumnsResponse *lpsResponse)
{
	object_ptr<ECGenericObjectTable> lpTable;
	struct propTagArray *lpPropTags = NULL;

	// Init
	lpsResponse->sPropTagArray.__size = 0;
	lpsResponse->sPropTagArray.__ptr = NULL;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	er = lpTable->GetColumns(soap, ulFlags, &lpPropTags);

	if(er != erSuccess)
		return er;
	lpsResponse->sPropTagArray.__size = lpPropTags->__size;
	lpsResponse->sPropTagArray.__ptr = lpPropTags->__ptr;
	return erSuccess;
}
SOAP_ENTRY_END()

/**
 * tableRestrict: called from IMAPITable::Restrict()
 */
SOAP_ENTRY_START(tableRestrict, *result, unsigned int ulTableId, struct restrictTable *lpsRestrict, unsigned int *result)
{
	object_ptr<ECGenericObjectTable> lpTable;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	if (!bSupportUnicode) {
		er = FixRestrictionEncoding(soap, stringCompat, In, lpsRestrict);
		if (er != erSuccess)
			return er;
	}
	return lpTable->Restrict(lpsRestrict);
}
SOAP_ENTRY_END()

/**
 * tableSort: called from IMAPITable::Sort()
 */
SOAP_ENTRY_START(tableSort, *result, unsigned int ulTableId, struct sortOrderArray *lpSortOrder, unsigned int ulCategories, unsigned int ulExpanded, unsigned int *result)
{
	object_ptr<ECGenericObjectTable> lpTable;

	if (lpSortOrder == nullptr)
		return KCERR_INVALID_PARAMETER;
	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	return lpTable->SetSortOrder(lpSortOrder, ulCategories, ulExpanded);
}
SOAP_ENTRY_END()

/**
 * tableQueryRows: called from IMAPITable::QueryRows()
 */
SOAP_ENTRY_START(tableQueryRows, lpsResponse->er, unsigned int ulTableId, unsigned int ulRowCount, unsigned int ulFlags, struct tableQueryRowsResponse *lpsResponse)
{
	object_ptr<ECGenericObjectTable> lpTable;
	struct rowSet	*lpRowSet = NULL;

	lpsResponse->sRowSet.__ptr = NULL;
	lpsResponse->sRowSet.__size = 0;

	// Get the table
	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;

	// FIXME: Check permission

	er = lpTable->QueryRows(soap, ulRowCount, ulFlags, &lpRowSet);
	if(er != erSuccess)
		return er;
	if (!bSupportUnicode) {
		er = FixRowSetEncoding(soap, stringCompat, Out, lpRowSet);
		if (er != erSuccess)
			return er;
	}

	lpsResponse->sRowSet.__ptr = lpRowSet->__ptr;
	lpsResponse->sRowSet.__size = lpRowSet->__size;
	return erSuccess;
}
SOAP_ENTRY_END()

/**
 * tableGetRowCount: called from IMAPITable::GetRowCount()
 */
SOAP_ENTRY_START(tableGetRowCount, lpsResponse->er, unsigned int ulTableId, struct tableGetRowCountResponse *lpsResponse)
{
	object_ptr<ECGenericObjectTable> lpTable;

	//FIXME: security? give rowcount 0 is failed ?
	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	return lpTable->GetRowCount(&lpsResponse->ulCount, &lpsResponse->ulRow);
}
SOAP_ENTRY_END()

/**
 * tableSeekRow: called from IMAPITable::SeekRow()
 */
SOAP_ENTRY_START(tableSeekRow, lpsResponse->er, unsigned int ulTableId , unsigned int ulBookmark, int lRows, struct tableSeekRowResponse *lpsResponse)
{
	object_ptr<ECGenericObjectTable> lpTable;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	return lpTable->SeekRow(ulBookmark, lRows, &lpsResponse->lRowsSought);
}
SOAP_ENTRY_END()

/**
 * tableFindRow: called from IMAPITable::FindRow()
 */
SOAP_ENTRY_START(tableFindRow, *result, unsigned int ulTableId ,unsigned int ulBookmark, unsigned int ulFlags, struct restrictTable *lpsRestrict, unsigned int *result)
{
	object_ptr<ECGenericObjectTable> lpTable;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	if (!bSupportUnicode) {
		er = FixRestrictionEncoding(soap, stringCompat, In, lpsRestrict);
		if (er != erSuccess)
			return er;
	}
	return lpTable->FindRow(lpsRestrict, ulBookmark, ulFlags);
}
SOAP_ENTRY_END()

/**
 * tableCreateBookmark: called from IMAPITable::CreateBookmark()
 */
SOAP_ENTRY_START(tableCreateBookmark, lpsResponse->er, unsigned int ulTableId, struct tableBookmarkResponse *lpsResponse)
{
	object_ptr<ECGenericObjectTable> lpTable;
	unsigned int ulbkPosition = 0;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	er = lpTable->CreateBookmark(&ulbkPosition);
	if(er != erSuccess)
		return er;
	lpsResponse->ulbkPosition = ulbkPosition;
	return erSuccess;
}
SOAP_ENTRY_END()

/**
 * tableCreateBookmark: called from IMAPITable::FreeBookmark()
 */
SOAP_ENTRY_START(tableFreeBookmark, *result, unsigned int ulTableId, unsigned int ulbkPosition, unsigned int *result)
{
	object_ptr<ECGenericObjectTable> lpTable;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	return lpTable->FreeBookmark(ulbkPosition);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(tableExpandRow, lpsResponse->er, unsigned int ulTableId, xsd__base64Binary sInstanceKey, unsigned int ulRowCount, unsigned int ulFlags, tableExpandRowResponse* lpsResponse)
{
	object_ptr<ECGenericObjectTable> lpTable;
	struct rowSet	*lpRowSet = NULL;
	unsigned int ulMoreRows = 0;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	er = lpTable->ExpandRow(soap, sInstanceKey, ulRowCount, ulFlags, &lpRowSet, &ulMoreRows);
	if(er != erSuccess)
		return er;
	if (!bSupportUnicode) {
		er = FixRowSetEncoding(soap, stringCompat, Out, lpRowSet);
		if (er != erSuccess)
			return er;
	}

    lpsResponse->ulMoreRows = ulMoreRows;
    lpsResponse->rowSet = *lpRowSet;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(tableCollapseRow, lpsResponse->er, unsigned int ulTableId, xsd__base64Binary sInstanceKey, unsigned int ulFlags, tableCollapseRowResponse* lpsResponse)
{
	object_ptr<ECGenericObjectTable> lpTable;
	unsigned int ulRows = 0;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
	er = lpTable->CollapseRow(sInstanceKey, ulFlags, &ulRows);
	if(er != erSuccess)
		return er;
    lpsResponse->ulRows = ulRows;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(tableGetCollapseState, lpsResponse->er, unsigned int ulTableId, struct xsd__base64Binary sBookmark, tableGetCollapseStateResponse *lpsResponse)
{
	object_ptr<ECGenericObjectTable> lpTable;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
    if(er != erSuccess)
		return er;
	return lpTable->GetCollapseState(soap, sBookmark, &lpsResponse->sCollapseState);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(tableSetCollapseState, lpsResponse->er, unsigned int ulTableId, struct xsd__base64Binary sCollapseState, struct tableSetCollapseStateResponse *lpsResponse);
{
	object_ptr<ECGenericObjectTable> lpTable;

	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
    if(er != erSuccess)
		return er;
	return lpTable->SetCollapseState(sCollapseState, &lpsResponse->ulBookmark);
}
SOAP_ENTRY_END()

/**
 * tableSetMultiStoreEntryIDs: client sets content for MultiStoreTable
 */
SOAP_ENTRY_START(tableSetMultiStoreEntryIDs, *result, unsigned int ulTableId, struct entryList *lpEntryList, unsigned int *result)
{
	object_ptr<ECGenericObjectTable> lpTable;
	ECMultiStoreTable		*lpMultiStoreTable = NULL;
	ECListInt	lObjectList;

	if (lpEntryList == nullptr)
		return KCERR_INVALID_PARAMETER;
	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if (er != erSuccess)
		return er;

	// ignore errors
	g_lpSessionManager->GetCacheManager()->GetEntryListToObjectList(lpEntryList, &lObjectList);

	lpMultiStoreTable = dynamic_cast<ECMultiStoreTable *>(lpTable.get());
	if (lpMultiStoreTable == nullptr)
		return KCERR_INVALID_PARAMETER;
	return lpMultiStoreTable->SetEntryIDs(&lObjectList);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(tableMulti, lpsResponse->er, struct tableMultiRequest sRequest, struct tableMultiResponse *lpsResponse)
{
    unsigned int ulTableId = sRequest.ulTableId;
	object_ptr<ECGenericObjectTable> lpTable;
    struct rowSet *lpRowSet = NULL;
    
    if(sRequest.lpOpen) {
        er = OpenTable(lpecSession, sRequest.lpOpen->sEntryId, sRequest.lpOpen->ulTableType, sRequest.lpOpen->ulType, sRequest.lpOpen->ulFlags, &lpsResponse->ulTableId);
		if(er != erSuccess)
			return er;
        ulTableId = lpsResponse->ulTableId;
    }
    
	er = lpecSession->GetTableManager()->GetTable(ulTableId, &~lpTable);
	if(er != erSuccess)
		return er;
    if(sRequest.lpSort) {
        er = lpTable->SetSortOrder(&sRequest.lpSort->sSortOrder, sRequest.lpSort->ulCategories, sRequest.lpSort->ulExpanded);
        if(er != erSuccess)
			return er;
    }

    if(sRequest.lpSetColumns) {
        er = lpTable->SetColumns(sRequest.lpSetColumns, false);
        if(er != erSuccess)
			return er;
    }
    
    if(sRequest.lpRestrict || (sRequest.ulFlags&TABLE_MULTI_CLEAR_RESTRICTION)) {
		if (!bSupportUnicode && sRequest.lpRestrict) {
			er = FixRestrictionEncoding(soap, stringCompat, In, sRequest.lpRestrict);
			if (er != erSuccess)
				return er;
		}

        er = lpTable->Restrict(sRequest.lpRestrict);
        if(er != erSuccess)
			return er;
    }
    
    if(sRequest.lpQueryRows) {
        er = lpTable->QueryRows(soap, sRequest.lpQueryRows->ulCount, sRequest.lpQueryRows->ulFlags, &lpRowSet);
        if(er != erSuccess)
			return er;
		if (!bSupportUnicode) {
			er = FixRowSetEncoding(soap, stringCompat, Out, lpRowSet);
			if (er != erSuccess)
				return er;
		}
                
        lpsResponse->sRowSet.__ptr = lpRowSet->__ptr;
        lpsResponse->sRowSet.__size = lpRowSet->__size;
    }
	return erSuccess;
}
SOAP_ENTRY_END()

// Delete a set of messages, recipients, or attachments
SOAP_ENTRY_START(deleteObjects, *result, unsigned int ulFlags, struct entryList *lpEntryList, unsigned int ulSyncId, unsigned int *result)
{
	ECListInt	lObjectList;
	unsigned int ulDeleteFlags = EC_DELETE_ATTACHMENTS | EC_DELETE_RECIPIENTS | EC_DELETE_CONTAINER | EC_DELETE_MESSAGES;
	USE_DATABASE();

	if (lpEntryList == nullptr)
		return KCERR_INVALID_PARAMETER;
	if(ulFlags & DELETE_HARD_DELETE)
		ulDeleteFlags |= EC_DELETE_HARD_DELETE;

	// ignore errors
	g_lpSessionManager->GetCacheManager()->GetEntryListToObjectList(lpEntryList, &lObjectList);
	return DeleteObjects(lpecSession, lpDatabase, &lObjectList, ulDeleteFlags, ulSyncId, false, true);
}
SOAP_ENTRY_END()

// Delete everything in a folder, but not the folder itself
// Quirk: this works with messages also, deleting attachments and recipients, but not the message itself.
//FIXME: michel? what with associated messages ?
SOAP_ENTRY_START(emptyFolder, *result, entryId sEntryId, unsigned int ulFlags, unsigned int ulSyncId, unsigned int *result)
{
	unsigned int		ulDeleteFlags = 0;
	unsigned int		ulId = 0;
	ECListInt			lObjectIds;
	USE_DATABASE();

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulId);
	if(er != erSuccess)
		return er;

	// Check Rights set permission
	er = lpecSession->GetSecurity()->CheckPermission(ulId, ecSecurityDelete);
	if(er != erSuccess)
		return er;

	// Add object into the list
	lObjectIds.emplace_back(ulId);
	ulDeleteFlags = EC_DELETE_MESSAGES | EC_DELETE_FOLDERS | EC_DELETE_RECIPIENTS | EC_DELETE_ATTACHMENTS;
	if (ulFlags & DELETE_HARD_DELETE)
		ulDeleteFlags |= EC_DELETE_HARD_DELETE;

	if((ulFlags&DEL_ASSOCIATED) == 0)
		ulDeleteFlags |= EC_DELETE_NOT_ASSOCIATED_MSG;
	return DeleteObjects(lpecSession, lpDatabase, &lObjectIds, ulDeleteFlags, ulSyncId, false, true);
}
SOAP_ENTRY_END()

/* FIXME
 *
 * Currently, when deleteFolders is called with DEL_FOLDERS but without DEL_MESSAGES, it will return an error
 * when a subfolder of the specified folder contains messages. I don't think this is up to spec. DeleteObjects
 * should therefore be changed so that the check is only done against messages and folders directly under the
 * top-level object.
 */

// Deletes a complete folder, with optional recursive subfolder and submessage deletion
SOAP_ENTRY_START(deleteFolder, *result,  entryId sEntryId, unsigned int ulFlags, unsigned int ulSyncId, unsigned int *result)
{
	unsigned int		ulDeleteFlags = 0;
	unsigned int		ulId = 0;
	unsigned int		ulFolderFlags = 0;
	ECListInt			lObjectIds;
	USE_DATABASE();

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulId);
	if(er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->CheckPermission(ulId, ecSecurityFolderAccess);
	if(er != erSuccess)
		return er;
	er = g_lpSessionManager->GetCacheManager()->GetObjectFlags(ulId, &ulFolderFlags);
	if(er != erSuccess)
		return er;

	// insert objectid into the delete list
	lObjectIds.emplace_back(ulId);
	ulDeleteFlags = EC_DELETE_CONTAINER;

	if(ulFlags & DEL_FOLDERS)
		ulDeleteFlags |= EC_DELETE_FOLDERS;

	if(ulFlags & DEL_MESSAGES)
		ulDeleteFlags |= EC_DELETE_MESSAGES | EC_DELETE_RECIPIENTS | EC_DELETE_ATTACHMENTS;

	if( (ulFlags & DELETE_HARD_DELETE) || ulFolderFlags == FOLDER_SEARCH)
		ulDeleteFlags |= EC_DELETE_HARD_DELETE;
	return DeleteObjects(lpecSession, lpDatabase, &lObjectIds, ulDeleteFlags, ulSyncId, false, true);
}
SOAP_ENTRY_END()

static ECRESULT DoNotifySubscribe(ECSession *lpecSession,
    unsigned long long ulSessionId, struct notifySubscribe *notifySubscribe)
{
	ECRESULT er = erSuccess;
	unsigned int ulKey = 0;
	object_ptr<ECGenericObjectTable> lpTable;

	//NOTE: An sKey with size 4 is a table notification id
	if(notifySubscribe->sKey.__size == 4) {
		 memcpy(&ulKey, notifySubscribe->sKey.__ptr, 4);
	}else {
		er = lpecSession->GetObjectFromEntryId(&notifySubscribe->sKey, &ulKey);
		if(er != erSuccess)
			return er;
		// Check permissions
		er = lpecSession->GetSecurity()->CheckPermission(ulKey, ecSecurityFolderVisible);
		if(er != erSuccess)
			return er;
	}
	
	if(notifySubscribe->ulEventMask & fnevTableModified) {
	    // An advise has been done on a table. The table ID is in 'ulKey' in this case. When this is done
	    // we have to populate the table first since row modifications would otherwise be wrong until the
	    // table is populated; if the table is unpopulated and a row changes, the row will be added into the table
	    // whenever it is modified, producing a TABLE_ROW_ADDED for that row instead of the correct TABLE_ROW_MODIFIED.
		er = lpecSession->GetTableManager()->GetTable(ulKey, &~lpTable);
	    if(er != erSuccess)
			return er;
        er = lpTable->Populate();
        if(er != erSuccess)
			return er;
	}

	return lpecSession->AddAdvise(notifySubscribe->ulConnection, ulKey, notifySubscribe->ulEventMask);
}

SOAP_ENTRY_START(notifySubscribe, *result,  struct notifySubscribe *notifySubscribe, unsigned int *result)
{
	if (notifySubscribe == nullptr)
		return KCERR_INVALID_PARAMETER;
	if (notifySubscribe->ulEventMask == fnevKopanoIcsChange)
		return lpecSession->AddChangeAdvise(notifySubscribe->ulConnection, &notifySubscribe->sSyncState);
	else
		return DoNotifySubscribe(lpecSession, ulSessionId, notifySubscribe);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(notifySubscribeMulti, *result, struct notifySubscribeArray *notifySubscribeArray, unsigned int *result)
{
	if (notifySubscribeArray == nullptr)
		return KCERR_INVALID_PARAMETER;

	for (gsoap_size_t i = 0; i < notifySubscribeArray->__size; ++i) {
		if (notifySubscribeArray->__ptr[i].ulEventMask == fnevKopanoIcsChange)
			er = lpecSession->AddChangeAdvise(notifySubscribeArray->__ptr[i].ulConnection, &notifySubscribeArray->__ptr[i].sSyncState);

		else
			er = DoNotifySubscribe(lpecSession, ulSessionId, &notifySubscribeArray->__ptr[i]);

		if (er != erSuccess) {
			for (gsoap_size_t j = 0; j < i; ++j)
				lpecSession->DelAdvise(notifySubscribeArray->__ptr[j].ulConnection);

			break;
		}
	}
	return er;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(notifyUnSubscribe, *result, unsigned int ulConnection, unsigned int *result)
{
	return lpecSession->DelAdvise(ulConnection);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(notifyUnSubscribeMulti, *result, struct mv_long *ulConnectionArray, unsigned int *result)
{
	unsigned int erTmp = erSuccess;
	unsigned int erFirst = erSuccess;

	if (ulConnectionArray == nullptr)
		return KCERR_INVALID_PARAMETER;

	for (gsoap_size_t i = 0; i < ulConnectionArray->__size; ++i) {
		erTmp = lpecSession->DelAdvise(ulConnectionArray->__ptr[i]);
		if (erTmp != erSuccess && erFirst == erSuccess)
			erFirst = erTmp;
	}

	// return first seen error (if any).
	return erFirst;
}
SOAP_ENTRY_END()

/*
 * Gets notifications queued for the session group that the specified session is attached to; you can access
 * all notifications of a session group via any session on that group. The request itself is handled by the
 * ECNotificationManager class since you don't want to block the calling thread while waiting for notifications.
 */
int ns__notifyGetItems(struct soap *soap, ULONG64 ulSessionId, struct notifyResponse *notifications)
{
	ECRESULT er = erSuccess;
	ECSession *lpSession = NULL;
	
	// Check if the session exists, and discard result
	er = g_lpSessionManager->ValidateSession(soap, ulSessionId, &lpSession, true);
	if(er != erSuccess) {
		// Directly return with error in er
		notifications->er = er;
		// SOAP call itself succeeded
		return SOAP_OK;
	}
	
	// discard lpSession
	lpSession->Unlock();
	lpSession = NULL;
	
    g_lpSessionManager->DeferNotificationProcessing(ulSessionId, soap);

    // Return SOAP_NULL so that the caller does *nothing* with the soap struct since we have passed it to the session
    // manager for deferred processing
    throw SOAP_NULL;
}

SOAP_ENTRY_START(getRights, lpsRightResponse->er, entryId sEntryId, int ulType, struct rightsResponse *lpsRightResponse)
{
	unsigned int	ulobjid = 0;

	struct rightsArray *lpsRightArray = NULL;

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulobjid);
	if(er != erSuccess)
		return er;
	lpsRightArray = s_alloc<rightsArray>(nullptr);
	memset(lpsRightArray, 0, sizeof(struct rightsArray));

	er = lpecSession->GetSecurity()->GetRights(ulobjid, ulType, lpsRightArray);
	if(er != erSuccess)
		goto exit;
	er = CopyRightsArrayToSoap(soap, lpsRightArray, &lpsRightResponse->pRightsArray);
	if (er != erSuccess)
		goto exit;

exit:
	if (lpsRightArray) {
		for (gsoap_size_t i = 0; i < lpsRightArray->__size; ++i)
			s_free(nullptr, lpsRightArray->__ptr[i].sUserId.__ptr);
		s_free(nullptr, lpsRightArray->__ptr);
		s_free(nullptr, lpsRightArray);
	}
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(setRights, *result, entryId sEntryId, struct rightsArray *lpsRightsArray, unsigned int *result)
{
	unsigned int	ulObjId = 0;

	if (lpsRightsArray == nullptr)
		return KCERR_INVALID_PARAMETER;
	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if(er != erSuccess)
		return er;

	// Check Rights set permission
	er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityFolderAccess);
	if(er != erSuccess)
		return er;
	return lpecSession->GetSecurity()->SetRights(ulObjId, lpsRightsArray);
}
SOAP_ENTRY_END()

int ns__getUserObjectList(struct soap *, ULONG64, unsigned int, entryId, int,
    struct userobjectResponse *r)
{
	/* Deprecated. This is only accessable through IECSecurity, and nobody calls this anymore! */
	// TODO: if we just remove this handler -- same result?!
	r->er = KCERR_NO_SUPPORT;
	return SOAP_OK;
}

SOAP_ENTRY_START(getOwner, lpsResponse->er, entryId sEntryId, struct getOwnerResponse *lpsResponse)
{
	unsigned int	ulobjid = 0;

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulobjid);
	if(er != erSuccess)
		return er;
	er = lpecSession->GetSecurity()->GetOwner(ulobjid, &lpsResponse->ulOwner);
	if(er != erSuccess)
		return er;
	return GetABEntryID(lpsResponse->ulOwner, soap, &lpsResponse->sOwner);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getIDsFromNames, lpsResponse->er,  struct namedPropArray *lpsNamedProps, unsigned int ulFlags, struct getIDsFromNamesResponse *lpsResponse)
{
	std::string		strEscapedString;
	std::string		strEscapedGUID;
	unsigned int	ulLastId = 0;
    USE_DATABASE();

	if (lpsNamedProps == nullptr)
		return KCERR_INVALID_PARAMETER;
	lpsResponse->lpsPropTags.__ptr = s_alloc<unsigned int>(soap, lpsNamedProps->__size);
	lpsResponse->lpsPropTags.__size = 0;

	strQuery = "SELECT id, nameid, namestring, guid FROM names WHERE ";
	for (gsoap_size_t i = 0; i < lpsNamedProps->__size; ++i) {
		strQuery += "(";

		// ID, then add ID where clause
		if(lpsNamedProps->__ptr[i].lpId != NULL) {
			strQuery += "nameid=" + stringify(*lpsNamedProps->__ptr[i].lpId) + " ";
		}

		// String, then add STRING where clause
		else if(lpsNamedProps->__ptr[i].lpString != NULL) {
			strEscapedString = lpDatabase->Escape(lpsNamedProps->__ptr[i].lpString);

			strQuery += "namestring='" + strEscapedString + "' ";
		}
		/* else { Handle this, it will break the SQL query, iff guid is present } */

		// Add a GUID specifier if there
		if(lpsNamedProps->__ptr[i].lpguid != NULL) {
			strEscapedGUID = lpDatabase->EscapeBinary(lpsNamedProps->__ptr[i].lpguid->__ptr, lpsNamedProps->__ptr[i].lpguid->__size);

			strQuery += "AND guid=" + strEscapedGUID;
		}

		strQuery += ")";
		if (i != lpsNamedProps->__size - 1)
			strQuery += " OR ";
	}

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	for (gsoap_size_t i = 0; i < lpsNamedProps->__size; ++i)
		lpsResponse->lpsPropTags.__ptr[i] = 0;

	for (size_t i = 0; i < lpDBResult.get_num_rows(); ++i) {
		lpDBRow = lpDBResult.fetch_row();
		if (lpDBRow == nullptr)
			continue;
		for (gsoap_size_t i = 0; i < lpsNamedProps->__size; ++i) {
			std::string nameid, namestring;

			if (lpsNamedProps->__ptr[i].lpId != nullptr)
				nameid = stringify(*lpsNamedProps->__ptr[i].lpId);
			else if (lpsNamedProps->__ptr[i].lpString != nullptr)
				namestring = lpDatabase->Escape(lpsNamedProps->__ptr[i].lpString);

			if (lpsNamedProps->__ptr[i].lpguid == nullptr ||  lpDBRow[3] == nullptr ||
			    memcmp(lpsNamedProps->__ptr[i].lpguid->__ptr, lpDBRow[3], lpsNamedProps->__ptr[i].lpguid->__size) != 0)
				continue;
			if ((nameid.size() > 0 && lpDBRow[1] && nameid.compare(lpDBRow[1]) == 0) ||
			    (namestring.size() > 0 && lpDBRow[2] && namestring.compare(lpDBRow[2]) == 0)) {
				lpsResponse->lpsPropTags.__ptr[i] = atoi(lpDBRow[0]) + 1;
				break;
			}
		}
	}

	if (!(ulFlags & MAPI_CREATE)) {
		lpsResponse->lpsPropTags.__size = lpsNamedProps->__size;
		return erSuccess;
	}
	er = lpDatabase->Begin();
	if (er != erSuccess)
		return er;

	for (gsoap_size_t i = 0; i < lpsNamedProps->__size; ++i) {
		if (lpsResponse->lpsPropTags.__ptr[i] != 0)
			continue;
		if (lpsNamedProps->__ptr[i].lpguid == nullptr) {
			er = KCERR_NO_ACCESS;
			goto exit;
		}

		strQuery = "INSERT INTO names (nameid, namestring, guid) VALUES(";
		if (lpsNamedProps->__ptr[i].lpId != 0)
			strQuery += stringify(*lpsNamedProps->__ptr[i].lpId);
		else
			strQuery += "null";

		strQuery += ",";
		if (lpsNamedProps->__ptr[i].lpString != nullptr) {
			strEscapedString = lpDatabase->Escape(lpsNamedProps->__ptr[i].lpString);
			strQuery += "'" + strEscapedString + "'";
		} else {
			strQuery += "null";
		}

		strQuery += ",";
		strEscapedGUID = lpDatabase->EscapeBinary(lpsNamedProps->__ptr[i].lpguid->__ptr, lpsNamedProps->__ptr[i].lpguid->__size);
		strQuery += strEscapedGUID;
		strQuery += ")";
		er = lpDatabase->DoInsert(strQuery, &ulLastId);
		if (er != erSuccess)
			goto exit;
		lpsResponse->lpsPropTags.__ptr[i] = ulLastId+1; // offset one because 0 is 'not found'
	}

	er = lpDatabase->Commit();
	if (er != erSuccess)
		goto exit;
	// Everything is done, now set the size
	lpsResponse->lpsPropTags.__size = lpsNamedProps->__size;

exit:
	ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getNamesFromIDs, lpsResponse->er, struct propTagArray *lpPropTags, struct getNamesFromIDsResponse *lpsResponse)
{
	struct namedPropArray lpsNames;
	USE_DATABASE();

	if (lpPropTags == nullptr)
		return KCERR_INVALID_PARAMETER;
	er = GetNamesFromIDs(soap, lpDatabase, lpPropTags, &lpsNames);
	if (er != erSuccess)
		return er;
    lpsResponse->lpsNames = lpsNames;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getReceiveFolder, lpsReceiveFolder->er, entryId sStoreId,
    const char *msg_class, struct receiveFolderResponse *lpsReceiveFolder)
{
	const char *lpszMessageClass = msg_class;
	unsigned int	ulStoreid = 0;
	const char *lpDest;
	USE_DATABASE();

	lpszMessageClass = STRIN_FIX(lpszMessageClass);

	er = lpecSession->GetObjectFromEntryId(&sStoreId, &ulStoreid);
	if(er != erSuccess)
		return er;

	// Check for default store
	if(lpszMessageClass == NULL)
		lpszMessageClass = "";

	strQuery = "SELECT objid, messageclass FROM receivefolder WHERE storeid="+stringify(ulStoreid)+" AND (";

	strQuery += "messageclass='"+lpDatabase->Escape(lpszMessageClass)+"'";

	lpDest = lpszMessageClass;
	do {
		lpDest = strchr(lpDest, '.');

		if(lpDest){
			strQuery += " OR messageclass='" + lpDatabase->Escape(std::string(lpszMessageClass, lpDest - lpszMessageClass)) + "'";
			++lpDest;
		}

	}while(lpDest);

	if(strlen(lpszMessageClass) != 0)
		strQuery += " OR messageclass=''";

	strQuery += ") ORDER BY length(messageclass) DESC LIMIT 1";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	if (lpDBResult.get_num_rows() != 1)
		/* items not found */
		return KCERR_NOT_FOUND;
	lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL) {
		ec_log_err("getReceiveFolder(): row or columns null");
		return KCERR_DATABASE_ERROR;
	}
	er = g_lpSessionManager->GetCacheManager()->GetEntryIdFromObject(atoui(lpDBRow[0]), soap, 0, &lpsReceiveFolder->sReceiveFolder.sEntryId);
	if (er != erSuccess)
		return er;
	lpsReceiveFolder->sReceiveFolder.lpszAExplicitClass = STROUT_FIX_CPY(lpDBRow[1]);
	return erSuccess;
}
SOAP_ENTRY_END()

// FIXME: should be able to delete an entry too
SOAP_ENTRY_START(setReceiveFolder, *result, entryId sStoreId,
    entryId *lpsEntryId, const char *msg_class, unsigned int *result)
{
	const char *lpszMessageClass = msg_class;
	bool			bIsUpdate = false;
	unsigned int	ulCheckStoreId = 0;
	unsigned int	ulStoreid = 0;
	unsigned int	ulId = 0;
	USE_DATABASE();

	lpszMessageClass = STRIN_FIX(lpszMessageClass);

	er = lpDatabase->Begin();
	if (er != erSuccess)
		return er;

	// Check, lpsEntryId and lpszMessageClass can't both be empty or 0
	if(lpsEntryId == NULL && (lpszMessageClass == NULL || *lpszMessageClass == '\0')){
		er = KCERR_INVALID_TYPE;
		goto exit;
	}

	er = lpecSession->GetObjectFromEntryId(&sStoreId, &ulStoreid);
	if(er != erSuccess)
	    goto exit;

	// an empty lpszMessageClass is the default folder
	if(lpszMessageClass == NULL)
		lpszMessageClass = "";

	// If the lpsEntryId parameter is set to NULL then replace the current receive folder with the message store's default.
	if(lpsEntryId)
	{
		// Check if object really exist and the relation between storeid and ulId

		er = lpecSession->GetObjectFromEntryId(lpsEntryId, &ulId);
		if(er != erSuccess)
			goto exit;

		// Check if storeid and ulId have a relation
		er = lpecSession->GetSessionManager()->GetCacheManager()->GetStore(ulId, &ulCheckStoreId, NULL);
		if(er != erSuccess){
			er = KCERR_INVALID_ENTRYID;
			goto exit;
		}

		if(ulStoreid != ulCheckStoreId) {
			er = KCERR_INVALID_ENTRYID;
			goto exit;
		}

		er = lpecSession->GetSecurity()->CheckDeletedParent(ulId);
		if (er != erSuccess)
			goto exit;

	} else {
		// Set MessageClass with the default of the store (that's the empty MessageClass)
		strQuery = "SELECT objid FROM receivefolder WHERE storeid="+stringify(ulStoreid)+" AND messageclass='' LIMIT 2";
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if(er != erSuccess)
			goto exit;

		if (lpDBResult.get_num_rows() == 1) {
			lpDBRow = lpDBResult.fetch_row();
			if(lpDBRow == NULL || lpDBRow[0] == NULL){
				er = KCERR_DATABASE_ERROR;
				ec_log_err("setReceiveFolder(): row or columns null");
				goto exit;
			}

			//Set the default folder
			ulId = atoi(lpDBRow[0]);
		}else{
			er = KCERR_DATABASE_ERROR; //FIXME: no default error ?
			ec_log_err("setReceiveFolder(): unexpected row count");
			goto exit;
		}
	}

	strQuery = "SELECT objid, id FROM receivefolder WHERE storeid="+stringify(ulStoreid)+" AND messageclass='"+lpDatabase->Escape(lpszMessageClass)+"' LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	bIsUpdate = false;
	// If ok, item already exists, return ok
	if (lpDBResult.get_num_rows() == 1) {
		lpDBRow = lpDBResult.fetch_row();
		if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL){
			er = KCERR_DATABASE_ERROR;
			ec_log_err("setReceiveFolder(): row or columns null");
			goto exit;
		}

		// Item exists
		if(ulId == atoui(lpDBRow[0])){
			er = lpDatabase->Rollback();	// Nothing changed, so Commit() would also do.
			goto exit;
		}

		bIsUpdate = true;
	}

	// Check permission
	//FIXME: also on delete?
	if(bIsUpdate)
		er = lpecSession->GetSecurity()->CheckPermission(ulStoreid, ecSecurityEdit);
	else
		er = lpecSession->GetSecurity()->CheckPermission(ulStoreid, ecSecurityCreate);
	if(er != erSuccess)
		goto exit;

	if(bIsUpdate) {
		strQuery = "UPDATE receivefolder SET objid="+stringify(ulId);
		strQuery+= " WHERE storeid="+stringify(ulStoreid)+" AND messageclass='"+lpDatabase->Escape(lpszMessageClass)+"'";
		er = lpDatabase->DoUpdate(strQuery);
	}else{
		strQuery = "INSERT INTO receivefolder (storeid, objid, messageclass) VALUES (";
		strQuery += stringify(ulStoreid)+", "+stringify(ulId)+", '"+lpDatabase->Escape(lpszMessageClass)+"')";
		er = lpDatabase->DoInsert(strQuery);
	}

	if(er != erSuccess)
		goto exit;

	er = lpDatabase->Commit();
	if (er != erSuccess)
		goto exit;

exit:
	ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

/*
 * WARNING
 *
 * lpsEntryID != NULL && lpMessageList != NULL: messages in lpMessageList must be set, lpsEntryId MUST BE IGNORED (may be entryid of search folder)
 * lpsEntryID == NULL && lpMessageList != NULL: called from IMessage::SetReadFlag, lpMessageList->__size == 1
 * lpsEntryID != NULL && lpMessageList == NULL: 'mark all messages as (un)read'
 *
 * Items are assumed to all be in the same store.
 *
 */
SOAP_ENTRY_START(setReadFlags, *result, unsigned int ulFlags, entryId* lpsEntryId, struct entryList *lpMessageList, unsigned int ulSyncId, unsigned int *result)
{
	std::list<unsigned int> lHierarchyIDs;
	std::list<std::pair<unsigned int, unsigned int>	> lObjectIds;
	std::string		strQueryCache;
	USE_DATABASE();

	unsigned int	i = 0;
	unsigned int	ulParent = 0;
	unsigned int	ulGrandParent = 0;
	unsigned int	ulFlagsNotify = 0;
	unsigned int	ulFlagsRemove = 0;
	unsigned int	ulFlagsAdd = 0;
	unsigned int	ulFolderId = 0;
	size_t			cObjectSize = 0;
	
	// List of unique parents
	std::map<unsigned int, int> mapParents;
	std::set<unsigned int> setParents;

	//NOTE: either lpMessageList may be NULL or lpsEntryId may be NULL

	if(ulFlags & GENERATE_RECEIPT_ONLY)
		return er;
	if (lpMessageList == nullptr && lpsEntryId == nullptr)
        // Bad input
		return KCERR_INVALID_PARAMETER;
	if (lpMessageList != nullptr)
		// Ignore errors
		g_lpSessionManager->GetCacheManager()->GetEntryListToObjectList(lpMessageList, &lHierarchyIDs);

	strQuery = "UPDATE properties SET ";

	if ((ulFlags & CLEAR_NRN_PENDING) || (ulFlags & SUPPRESS_RECEIPT) || (ulFlags & GENERATE_RECEIPT_ONLY) )
		ulFlagsRemove |= MSGFLAG_NRN_PENDING;

	if ((ulFlags & CLEAR_RN_PENDING) || (ulFlags & SUPPRESS_RECEIPT) || (ulFlags & GENERATE_RECEIPT_ONLY) )
		ulFlagsRemove |= MSGFLAG_RN_PENDING;

    if (!(ulFlags & GENERATE_RECEIPT_ONLY) && (ulFlags & CLEAR_READ_FLAG))
        ulFlagsRemove |= MSGFLAG_READ;
	else if( !(ulFlags & GENERATE_RECEIPT_ONLY) )
        ulFlagsAdd |= MSGFLAG_READ;

	if(ulFlagsRemove != 0)
		strQuery += "val_ulong=val_ulong & ~" + stringify(ulFlagsRemove);

	if(ulFlagsAdd != 0) {
		strQuery += (ulFlagsRemove!=0)?",":"";
		strQuery += "val_ulong=val_ulong | " + stringify(ulFlagsAdd);
	}
	if (ulFlagsRemove == 0 && ulFlagsAdd == 0)
		// Nothing to update
		return er;
	er = lpDatabase->Begin();
	if (er != erSuccess)
		return er;
	if(lpMessageList == NULL) {
	    // No message list passed, so 'mark all items (un)read'
        er = lpecSession->GetObjectFromEntryId(lpsEntryId, &ulFolderId);
        if(er != erSuccess)
            goto exit;
            
        er = lpDatabase->DoSelect("SELECT val_ulong FROM properties WHERE hierarchyid=" + stringify(ulFolderId) + " FOR UPDATE", NULL);
        if(er != erSuccess)
            goto exit;

        // Check permission
        er = lpecSession->GetSecurity()->CheckPermission(ulFolderId, ecSecurityRead);
        if(er != erSuccess)
            goto exit;

		// Purge changes
		ECTPropsPurge::PurgeDeferredTableUpdates(lpDatabase, ulFolderId);

		// Get all items MAPI_MESSAGE exclude items with flags MSGFLAG_DELETED AND MSGFLAG_ASSOCIATED of which we will be changing flags
		
		// Note we use FOR UPDATE which locks the records in the hierarchy (and in tproperties as a sideeffect), which serializes access to the rows, avoiding deadlocks
		strQueryCache = "SELECT id, tproperties.val_ulong FROM hierarchy FORCE INDEX(parenttypeflags) JOIN tproperties FORCE INDEX(PRIMARY) ON tproperties.hierarchyid=hierarchy.id AND tproperties.tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND tproperties.type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) + " WHERE parent="+ stringify(ulFolderId) + " AND hierarchy.type=5 AND flags = 0 AND (tproperties.val_ulong & " + stringify(ulFlagsRemove) + " OR tproperties.val_ulong & " + stringify(ulFlagsAdd) + " != " + stringify(ulFlagsAdd) + ") AND tproperties.folderid = " + stringify(ulFolderId) + " FOR UPDATE";
		er = lpDatabase->DoSelect(strQueryCache, &lpDBResult);
		if(er != erSuccess)
			goto exit;

		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			if(lpDBRow[0] == NULL || lpDBRow[1] == NULL){
				er = KCERR_DATABASE_ERROR;
				ec_log_err("setReadFlags(): columns null");
				goto exit;
			}
			lObjectIds.emplace_back(atoui(lpDBRow[0]), atoui(lpDBRow[1]));
			++i;
		}

		ulParent = ulFolderId;
	} else {
		if(lHierarchyIDs.empty()) {
			// Nothing to do
			lpDatabase->Commit();
			goto exit;
		}
		
	    // Because the messagelist can contain messages from all over the place, we have to check permissions for all the parent folders of the items
	    // we are setting 'read' or 'unread'
		for (auto hier_id : lHierarchyIDs) {
			// Get the parent object. Note that the cache will hold this information so the loop below with GetObject() will
			// be done directly from the cache (assuming it's not too large)
			if (g_lpSessionManager->GetCacheManager()->GetObject(hier_id, &ulParent, NULL, NULL) != erSuccess)
			    continue;
			setParents.emplace(ulParent);
        }

        // Lock parent folders        
        for (auto parent_id : setParents) {
            er = lpDatabase->DoSelect("SELECT val_ulong FROM properties WHERE hierarchyid=" + stringify(parent_id) + " FOR UPDATE", NULL);
            if(er != erSuccess)
                goto exit;
        }

        // Check permission
        for (auto parent_id : setParents) {
            er = lpecSession->GetSecurity()->CheckPermission(parent_id, ecSecurityRead);
            if(er != erSuccess)
                goto exit;
        }
        
        // Now find all messages that will actually change
		strQueryCache = "SELECT id, properties.val_ulong FROM hierarchy JOIN properties ON hierarchy.id=properties.hierarchyid AND properties.tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND properties.type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) + " WHERE hierarchy.type=5 AND flags = 0 AND (properties.val_ulong & " + stringify(ulFlagsRemove) + " OR properties.val_ulong & " + stringify(ulFlagsAdd) + " != " + stringify(ulFlagsAdd) + ") AND hierarchyid IN (";
		for (auto hier = lHierarchyIDs.cbegin();
		     hier != lHierarchyIDs.cend(); ++hier) {
			if (hier != lHierarchyIDs.cbegin())
				strQueryCache += ",";
			strQueryCache += stringify(*hier);
		}
		strQueryCache += ") FOR UPDATE"; // See comment above about FOR UPDATE
		er = lpDatabase->DoSelect(strQueryCache, &lpDBResult);
		if(er != erSuccess)
			goto exit;

		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			if(lpDBRow[0] == NULL || lpDBRow[1] == NULL){
				er = KCERR_DATABASE_ERROR;
				ec_log_err("setReadFlags(): columns null(2)");
				goto exit;
			}
			lObjectIds.emplace_back(atoui(lpDBRow[0]), atoui(lpDBRow[1]));
			++i;
		}
	}

	// Security passed, and we have a list of all the items that must be changed, and the records are locked
	
	// Check if there is anything to do
	if(lObjectIds.empty()) {
		lpDatabase->Commit();
		goto exit;
	}

    strQuery += " WHERE properties.hierarchyid IN(";

    lHierarchyIDs.clear();
    for (auto iObjectid = lObjectIds.cbegin(); iObjectid != lObjectIds.cend(); ++iObjectid) {

        if (iObjectid != lObjectIds.cbegin())
            strQuery += ",";

        strQuery += stringify(iObjectid->first);
		lHierarchyIDs.emplace_back(iObjectid->first);
    }
    strQuery += ")";

	strQuery += " AND properties.tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + "  AND properties.type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS));

   	// Update the database
   	er = lpDatabase->DoUpdate(strQuery);
   	if(er != erSuccess)
    	goto exit;

	er = UpdateTProp(lpDatabase, PR_MESSAGE_FLAGS, ulParent, &lHierarchyIDs); // FIXME ulParent is not constant for all lHierarchyIDs
	if(er != erSuccess)
		goto exit;
    
    // Add changes to ICS
    for (const auto &op : lObjectIds) {
		bool read = (ulFlagsRemove & MSGFLAG_READ) ||
		            (ulFlagsAdd & MSGFLAG_READ);
		if (!read)
			continue;
            // Only save ICS change when the actual readflag has changed
            SOURCEKEY		sSourceKey;
            SOURCEKEY		sParentSourceKey;

		if (g_lpSessionManager->GetCacheManager()->GetObject(op.first, &ulParent, NULL, NULL) != erSuccess)
			    continue;
		GetSourceKey(op.first, &sSourceKey);
            GetSourceKey(ulParent, &sParentSourceKey);

            // Because we know that ulFlagsRemove && MSGFLAG_READ || ulFlagsAdd & MSGFLAG_READ and we assume
            // that they are never both TRUE, we can ignore ulFlagsRemove and just look at ulFlagsAdd for the new
            // readflag state
            AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, ICS_MESSAGE_FLAG, ulFlagsAdd & MSGFLAG_READ);
    }

    // Update counters, by counting the number of changes per folder
	for (const auto &op : lObjectIds) {
		er = g_lpSessionManager->GetCacheManager()->GetObject(op.first, &ulParent, NULL, NULL);
		if (er != erSuccess)
			goto exit;
		mapParents.emplace(ulParent, 0);
		if (ulFlagsAdd & MSGFLAG_READ &&
		    (op.second & MSGFLAG_READ) == 0)
			--mapParents[ulParent]; // Decrease unread count
		if (ulFlagsRemove & MSGFLAG_READ && op.second & MSGFLAG_READ)
			++mapParents[ulParent]; // Increase unread count
	}
	
	for (const auto &p : mapParents) {
		if (p.second == 0)
			continue;
		er = g_lpSessionManager->GetCacheManager()->GetParent(p.first, &ulGrandParent);
		if(er != erSuccess)
			goto exit;
		er = UpdateFolderCount(lpDatabase, p.first, PR_CONTENT_UNREAD, p.second);
		if (er != erSuccess)
			goto exit;
	}
	
	er = lpDatabase->Commit();
    if(er != erSuccess)
	    goto exit;

	// Now, update cache and send the notifications

	cObjectSize = lObjectIds.size();

    // Loop through the messages, updating each
	for (const auto &op : lObjectIds) {
		// Remove the item from the cache 
		g_lpSessionManager->GetCacheManager()->UpdateCell(op.first,
			PR_MESSAGE_FLAGS,
			(ulFlagsAdd | ulFlagsRemove) & MSGFLAG_READ,
			ulFlagsAdd & MSGFLAG_READ);

		if (g_lpSessionManager->GetCacheManager()->GetObject(op.first,
		    &ulParent, NULL, &ulFlagsNotify) != erSuccess) {
            ulParent = 0;
            ulFlagsNotify = 0;
		}

        // Update the message itself in tables and object notification
		g_lpSessionManager->NotificationModified(MAPI_MESSAGE,
			op.first, ulParent);

        if(ulParent &&  cObjectSize < EC_TABLE_CHANGE_THRESHOLD)
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY,
				ulFlagsNotify & MSGFLAG_NOTIFY_FLAGS, ulParent,
				op.first, MAPI_MESSAGE);
    }

    // Loop through all the parent folders of the objects, sending notifications for them
	for (const auto &p : mapParents) {
        // The parent has changed its PR_CONTENT_UNREAD
		g_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, p.first);
		g_lpSessionManager->NotificationModified(MAPI_FOLDER, p.first);

        // The grand parent's table view of the parent has changed
		if (g_lpSessionManager->GetCacheManager()->GetObject(p.first,
		    &ulGrandParent, NULL, &ulFlagsNotify) == erSuccess)
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY,
				ulFlagsNotify & MSGFLAG_NOTIFY_FLAGS,
				ulGrandParent, p.first, MAPI_FOLDER);

        if(cObjectSize >= EC_TABLE_CHANGE_THRESHOLD)
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE,
				ulFlagsNotify & MSGFLAG_NOTIFY_FLAGS,
				p.first, 0, MAPI_MESSAGE);
    }

exit:
    ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(createUser, lpsUserSetResponse->er, struct user *lpsUser, struct setUserResponse *lpsUserSetResponse)
{
	unsigned int		ulUserId = 0;
	objectdetails_t		details(ACTIVE_USER); // should this function also be able to createContact?

	if (lpsUser == NULL || lpsUser->lpszUsername == NULL || lpsUser->lpszFullName == NULL || lpsUser->lpszMailAddress == NULL ||
		(lpsUser->lpszPassword == nullptr && lpsUser->ulIsNonActive == 0))
		return KCERR_INVALID_PARAMETER;

	if (!bSupportUnicode) {
		er = FixUserEncoding(soap, stringCompat, In, lpsUser);
		if (er != erSuccess)
			return er;
	}

	er = CopyUserDetailsFromSoap(lpsUser, NULL, &details, soap);
	if (er != erSuccess)
		return er;
	auto usrmgt = lpecSession->GetUserManagement();
	er = usrmgt->UpdateUserDetailsFromClient(&details);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(details.GetPropInt(OB_PROP_I_COMPANYID));
	if(er != erSuccess)
		return er;

    // Create user and sync
	er = usrmgt->CreateObjectAndSync(details, &ulUserId);
	if(er != erSuccess)
		return er;
	er = GetABEntryID(ulUserId, soap, &lpsUserSetResponse->sUserId);
	if (er != erSuccess)
		return er;
	lpsUserSetResponse->ulUserId = ulUserId;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(setUser, *result, struct user *lpsUser, unsigned int *result)
{
	objectdetails_t		details;
	objectdetails_t		oldDetails;
	unsigned int		ulUserId = 0;
	objectid_t			sExternId;

	if (lpsUser == nullptr)
		return KCERR_INVALID_PARAMETER;
	if (!bSupportUnicode) {
		er = FixUserEncoding(soap, stringCompat, In, lpsUser);
		if (er != erSuccess)
			return er;
	}

	if (lpsUser->sUserId.__size > 0 && lpsUser->sUserId.__ptr != NULL)
	{
		er = GetLocalId(lpsUser->sUserId, lpsUser->ulUserId, &ulUserId, &sExternId);
		if (er != erSuccess) 
			return er;
	}
	else
		ulUserId = lpsUser->ulUserId;

	if(ulUserId) {
		er = lpecSession->GetUserManagement()->GetObjectDetails(ulUserId, &oldDetails);
		if (er != erSuccess)
			return er;
	}

	// Check security
	// @todo add check on anonymous (mv)properties
	auto sec = lpecSession->GetSecurity();
	if (sec->IsAdminOverUserObject(ulUserId) == erSuccess) {
		// admins can update anything of a user
		// FIXME: prevent the user from removing admin rights from itself?
		er = erSuccess;
	} else if (sec->GetUserId() == ulUserId) {
		// you're only allowed to set your password, force the lpsUser struct to only contain that update
		if (lpsUser->lpszUsername && oldDetails.GetPropString(OB_PROP_S_LOGIN) != lpsUser->lpszUsername) {
			ec_log_warn("Disallowing user \"%s\" to update their username to \"%s\"",
												 oldDetails.GetPropString(OB_PROP_S_LOGIN).c_str(), lpsUser->lpszUsername);
			lpsUser->lpszUsername = NULL;
		}

		// leave lpszPassword

		if (lpsUser->lpszMailAddress && oldDetails.GetPropString(OB_PROP_S_EMAIL) != lpsUser->lpszMailAddress) {
			ec_log_warn("Disallowing user \"%s\" to update their mail address to \"%s\"",
												 oldDetails.GetPropString(OB_PROP_S_LOGIN).c_str(), lpsUser->lpszMailAddress);
			lpsUser->lpszMailAddress = NULL;
		}

		if (lpsUser->lpszFullName && oldDetails.GetPropString(OB_PROP_S_FULLNAME) != lpsUser->lpszFullName) {
			ec_log_warn("Disallowing user \"%s\" to update their fullname to \"%s\"",
												 oldDetails.GetPropString(OB_PROP_S_LOGIN).c_str(), lpsUser->lpszFullName);
			lpsUser->lpszFullName = NULL;
		}

		if (lpsUser->lpszServername && oldDetails.GetPropString(OB_PROP_S_SERVERNAME) != lpsUser->lpszServername) {
			ec_log_warn("Disallowing user \"%s\" to update their home server to \"%s\"",
												 oldDetails.GetPropString(OB_PROP_S_LOGIN).c_str(), lpsUser->lpszServername);
			lpsUser->lpszServername = NULL;
		}

		// FIXME: check OB_PROP_B_NONACTIVE too? NOTE: ulIsNonActive is now ignored.
		if (lpsUser->ulObjClass != (ULONG)-1 && oldDetails.GetClass() != (objectclass_t)lpsUser->ulObjClass) {
			ec_log_warn("Disallowing user \"%s\" to update their active flag to %d",
												 oldDetails.GetPropString(OB_PROP_S_LOGIN).c_str(), lpsUser->ulObjClass);
			lpsUser->ulObjClass = (ULONG)-1;
		}

		if (lpsUser->ulIsAdmin != (ULONG)-1 && oldDetails.GetPropInt(OB_PROP_I_ADMINLEVEL) != lpsUser->ulIsAdmin) {
			ec_log_warn("Disallowing user \"%s\" to update their admin flag to %d",
												 oldDetails.GetPropString(OB_PROP_S_LOGIN).c_str(), lpsUser->ulIsAdmin);
			lpsUser->ulIsAdmin = (ULONG)-1;
		}
	} else {
		// you cannot set any details if you're not an admin or not yourself
		ec_log_warn(
			"Disallowing user \"%s\" to update details of user \"%s\"",
			oldDetails.GetPropString(OB_PROP_S_LOGIN).c_str(),
			lpsUser->lpszUsername);
		return KCERR_NO_ACCESS;
	}

	details = objectdetails_t(ACTIVE_USER);

	// construct new details
	er = CopyUserDetailsFromSoap(lpsUser, &sExternId.id, &details, soap);
	if (er != erSuccess)
		return er;
	auto usrmgt = lpecSession->GetUserManagement();
	er = usrmgt->UpdateUserDetailsFromClient(&details);
	if (er != erSuccess)
		return er;
	return usrmgt->SetObjectDetailsAndSync(ulUserId, details, nullptr);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getUser, lpsGetUserResponse->er, unsigned int ulUserId, entryId sUserId, struct getUserResponse *lpsGetUserResponse)
{
	objectdetails_t	details;
	entryId sTmpUserId;

	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		return er;

	/* Check if we are able to view the returned userobject */
	er = lpecSession->GetSecurity()->IsUserObjectVisible(ulUserId);
	if (er != erSuccess)
		return er;

	lpsGetUserResponse->lpsUser = s_alloc<user>(soap);
	memset(lpsGetUserResponse->lpsUser, 0, sizeof(struct user));

	if (ulUserId == 0)
		ulUserId = lpecSession->GetSecurity()->GetUserId();

	er = lpecSession->GetUserManagement()->GetObjectDetails(ulUserId, &details);
	if (er != erSuccess)
		return er;
	if (OBJECTCLASS_TYPE(details.GetClass()) != OBJECTTYPE_MAILUSER)
		return KCERR_NOT_FOUND;
	er = GetABEntryID(ulUserId, soap, &sTmpUserId);
	if (er == erSuccess)
		er = CopyUserDetailsToSoap(ulUserId, &sTmpUserId, details, lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON, soap, lpsGetUserResponse->lpsUser);
	if (er != erSuccess)
		return er;

	// 6.40.0 stores the object class in the IsNonActive field
	if (lpecSession->ClientVersion() == ZARAFA_VERSION_6_40_0)
		lpsGetUserResponse->lpsUser->ulIsNonActive = lpsGetUserResponse->lpsUser->ulObjClass;

	if (!bSupportUnicode) {
		er = FixUserEncoding(soap, stringCompat, Out, lpsGetUserResponse->lpsUser);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getUserList, lpsUserList->er, unsigned int ulCompanyId, entryId sCompanyId, struct userListResponse *lpsUserList)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpUsers;
	entryId sUserEid;

	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;

	/* Input check, if ulCompanyId is 0, we want the user's company,
	 * otherwise we must check if the requested company is visible for the user. */
	auto sec = lpecSession->GetSecurity();
	if (ulCompanyId == 0)
		er = sec->GetUserCompany(&ulCompanyId);
	else
		er = sec->IsUserObjectVisible(ulCompanyId);
	if (er != erSuccess)
		return er;
	er = lpecSession->GetUserManagement()->GetCompanyObjectListAndSync(OBJECTCLASS_USER, ulCompanyId, &unique_tie(lpUsers), 0);
	if(er != erSuccess)
		return er;

    lpsUserList->sUserArray.__size = 0;
    lpsUserList->sUserArray.__ptr = s_alloc<user>(soap, lpUsers->size());

	for (const auto &user : *lpUsers) {
		if (OBJECTCLASS_TYPE(user.GetClass()) != OBJECTTYPE_MAILUSER ||
		    user.GetClass() == NONACTIVE_CONTACT)
			continue;
		er = GetABEntryID(user.ulId, soap, &sUserEid);
		if (er != erSuccess)
			return er;
		er = CopyUserDetailsToSoap(user.ulId, &sUserEid, user,
		     lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON,
		     soap, &lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size]);
		if (er != erSuccess)
			return er;

		// 6.40.0 stores the object class in the IsNonActive field
		if (lpecSession->ClientVersion() == ZARAFA_VERSION_6_40_0)
			lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulIsNonActive = lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulObjClass;

		if (!bSupportUnicode) {
			er = FixUserEncoding(soap, stringCompat, Out, lpsUserList->sUserArray.__ptr + lpsUserList->sUserArray.__size);
			if (er != erSuccess)
				return er;
		}

		++lpsUserList->sUserArray.__size;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getSendAsList, lpsUserList->er, unsigned int ulUserId, entryId sUserId, struct userListResponse *lpsUserList)
{
	objectdetails_t userDetails, senderDetails;
	entryId sSenderEid;

	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		return er;
	auto sec = lpecSession->GetSecurity();
	er = sec->IsUserObjectVisible(ulUserId);
	if (er != erSuccess)
		return er;
	auto usrmgt = lpecSession->GetUserManagement();
	er = usrmgt->GetObjectDetails(ulUserId, &userDetails);
	if (er != erSuccess)
		return er;

	auto userIds = userDetails.GetPropListInt(OB_PROP_LI_SENDAS);
	lpsUserList->sUserArray.__size = 0;
	lpsUserList->sUserArray.__ptr = s_alloc<user>(soap, userIds.size());

	for (auto user_id : userIds) {
		if (sec->IsUserObjectVisible(user_id) != erSuccess)
			continue;
		er = usrmgt->GetObjectDetails(user_id, &senderDetails);
		if (er == KCERR_NOT_FOUND)
			continue;
		if (er != erSuccess)
			return er;
		er = GetABEntryID(user_id, soap, &sSenderEid);
		if (er != erSuccess)
			return er;
		er = CopyUserDetailsToSoap(user_id, &sSenderEid, senderDetails,
		     lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON,
		     soap, &lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size]);
		if (er != erSuccess)
			return er;

		// 6.40.0 stores the object class in the IsNonActive field
		if (lpecSession->ClientVersion() == ZARAFA_VERSION_6_40_0)
			lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulIsNonActive = lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulObjClass;

		if (!bSupportUnicode) {
			er = FixUserEncoding(soap, stringCompat, Out, lpsUserList->sUserArray.__ptr + lpsUserList->sUserArray.__size);
			if (er != erSuccess)
				return er;
		}
		++lpsUserList->sUserArray.__size;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(addSendAsUser, *result, unsigned int ulUserId, entryId sUserId, unsigned int ulSenderId, entryId sSenderId, unsigned int *result)
{
	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess) {
		ec_log_err("addSendAsUser(): GetLocalId(ulUserId) failed %x", er);
		return er;
	}

	er = GetLocalId(sSenderId, ulSenderId, &ulSenderId, NULL);
	if (er != erSuccess) {
		ec_log_err("addSendAsUser(): GetLocalId(ulSenderId) failed %x", er);
		return er;
	}

	if (ulUserId == ulSenderId) {
		ec_log_err("addSendAsUser(): ulUserId == ulSenderId");
		return KCERR_COLLISION;
	}

	// Check security, only admins can set sendas users, not the user itself
	auto sec = lpecSession->GetSecurity();
	if (sec->IsAdminOverUserObject(ulUserId) != erSuccess) {
		ec_log_err("addSendAsUser(): IsAdminOverUserObject failed %x", er);
		return KCERR_NO_ACCESS;
	}

	// needed?
	er = sec->IsUserObjectVisible(ulUserId);
	if (er != erSuccess) {
		ec_log_err("addSendAsUser(): IsUserObjectVisible failed %x", er);
		return er;
	}

	er = lpecSession->GetUserManagement()->AddSubObjectToObjectAndSync(OBJECTRELATION_USER_SENDAS, ulUserId, ulSenderId);
	if (er != erSuccess)
		ec_log_err("addSendAsUser(): AddSubObjectToObjectAndSync failed %x", er);
	return er;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(delSendAsUser, *result, unsigned int ulUserId, entryId sUserId, unsigned int ulSenderId, entryId sSenderId, unsigned int *result)
{
	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		return er;
	er = GetLocalId(sSenderId, ulSenderId, &ulSenderId, NULL);
	if (er != erSuccess)
		return er;
	if (ulUserId == ulSenderId) {
		ec_log_err("delSendAsUser(): ulUserId == ulSenderId");
		return KCERR_COLLISION;
	}

	// Check security, only admins can set sendas users, not the user itself
	auto sec = lpecSession->GetSecurity();
	if (sec->IsAdminOverUserObject(ulUserId) != erSuccess)
		return KCERR_NO_ACCESS;

	// needed ?
	er = sec->IsUserObjectVisible(ulUserId);
	if (er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->DeleteSubObjectFromObjectAndSync(OBJECTRELATION_USER_SENDAS, ulUserId, ulSenderId);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(purgeSoftDelete, *result, unsigned int ulDays, unsigned int *result)
{
    unsigned int	ulFolders = 0;
    unsigned int	ulMessages = 0;
	unsigned int	ulStores = 0;

    // Only system-admins may run this
    if (lpecSession->GetSecurity()->GetAdminLevel() < ADMIN_LEVEL_SYSADMIN)
		return KCERR_NO_ACCESS;

    ec_log_info("Start forced softdelete clean up");

    er = PurgeSoftDelete(lpecSession, ulDays * 24 * 60 * 60, &ulMessages, &ulFolders, &ulStores, NULL);

    if (er == erSuccess)
		ec_log_info("Softdelete done: removed %d stores, %d folders, and %d messages", ulStores, ulFolders, ulMessages);
    else if (er == KCERR_BUSY)
		ec_log_info("Softdelete already running");
	else
		ec_log_info("Softdelete failed: removed %d stores, %d folders, and %d messages", ulStores, ulFolders, ulMessages);
	return er;
}
SOAP_ENTRY_END()

static inline void kc_purge_cache_tcmalloc(void)
{
#ifdef HAVE_TCMALLOC
	auto rfm = reinterpret_cast<decltype(MallocExtension_ReleaseFreeMemory) *>
		(dlsym(NULL, "MallocExtension_ReleaseFreeMemory"));
	if (rfm != NULL)
		rfm();
#endif
}

SOAP_ENTRY_START(purgeCache, *result, unsigned int ulFlags, unsigned int *result)
{
    if (lpecSession->GetSecurity()->GetAdminLevel() < ADMIN_LEVEL_SYSADMIN)
		return KCERR_NO_ACCESS;
    er = g_lpSessionManager->GetCacheManager()->PurgeCache(ulFlags);
	kc_purge_cache_tcmalloc();
	g_lpStatsCollector->SetTime(SCN_SERVER_LAST_CACHECLEARED, time(NULL));
	return er;
}
SOAP_ENTRY_END()

//Create a store
// Info: Userid can also be a group id ('everyone' for public store)
SOAP_ENTRY_START(createStore, *result, unsigned int ulStoreType, unsigned int ulUserId, entryId sUserId, entryId sStoreId, entryId sRootId, unsigned int ulFlags, unsigned int *result)
{
	unsigned int	ulStoreId = 0;
	unsigned int	ulRootMapId = 0;
	objectdetails_t userDetails;
	unsigned int	ulCompanyId = 0;
	bool			bHasLocalStore = false;

	SOURCEKEY		sSourceKey;
	GUID			guidStore;
	time_t			now;
	unsigned int	timeProps[] = { PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME };
	struct propVal 	sProp;
	struct hiloLong sHilo;

	struct rightsArray srightsArray;
	USE_DATABASE();

	memset(&srightsArray, 0 , sizeof(srightsArray));

	if((unsigned int)sStoreId.__size < sizeof(EID_V0)) {
	    er = KCERR_INVALID_PARAMETER;
	    goto exit;
    }
    
    // Normalize flags
	((EID_V0 *)sStoreId.__ptr)->usFlags = 0;

	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		goto exit;

	er = CheckUserStore(lpecSession, ulUserId, ulStoreType, &bHasLocalStore);
	if (er != erSuccess)
		goto exit;

	if (!bHasLocalStore && (ulFlags & EC_OVERRIDE_HOMESERVER) == 0) {
		ec_log_err("Create store requested, but store is not on this server, or server property not set for object %d", ulUserId);
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	ec_log_info("Started to create store (userid=%d, type=%d)", ulUserId, ulStoreType);

	er = lpDatabase->Begin();
	if (er != erSuccess)
		goto exit;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulUserId);
	if (er != erSuccess)
		goto exit;

	// Get object details, and resolve company
	er = lpecSession->GetUserManagement()->GetObjectDetails(ulUserId, &userDetails);
	if (er != erSuccess)
		goto exit;

	if (lpecSession->GetSessionManager()->IsHostedSupported())
		ulCompanyId = userDetails.GetPropInt(OB_PROP_I_COMPANYID);

	// Validate store entryid
	if(ValidateZEntryId(sStoreId.__size, sStoreId.__ptr, MAPI_STORE) == false) {
		er = KCERR_INVALID_ENTRYID;
		goto exit;
	}

	// Validate root entryid
	if(ValidateZEntryId(sRootId.__size, sRootId.__ptr, MAPI_FOLDER) == false) {
		er = KCERR_INVALID_ENTRYID;
		goto exit;
	}

	er = GetStoreGuidFromEntryId(sStoreId.__size, sStoreId.__ptr, &guidStore);
	if(er != erSuccess)
		goto exit;

	// Check if there's already a store for the user or group
	strQuery = "SELECT 0 FROM stores WHERE (type=" + stringify(ulStoreType) + " AND user_id=" + stringify(ulUserId) + ") OR guid=" + lpDatabase->EscapeBinary(&guidStore, sizeof(GUID)) + " LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;
	if (lpDBResult.get_num_rows() > 0) {
		er = KCERR_COLLISION;
		ec_log_err("createStore(): already exists");
		goto exit;
	}

	// Create Toplevel of the store
	strQuery = "INSERT INTO hierarchy(parent, type, owner) VALUES(NULL, "+stringify(MAPI_STORE)+", "+ stringify(ulUserId)+")";
	er = lpDatabase->DoInsert(strQuery, &ulStoreId);
	if(er != erSuccess)
		goto exit;

	// Create the rootfolder of a store
	strQuery = "INSERT INTO hierarchy(parent, type, owner) VALUES("+stringify(ulStoreId)+", "+stringify(MAPI_FOLDER)+ ", "+ stringify(ulUserId)+")";
	er = lpDatabase->DoInsert(strQuery, &ulRootMapId);
	if(er != erSuccess)
		goto exit;

	//Init storesize
	er = UpdateObjectSize(lpDatabase, ulStoreId, MAPI_STORE, UPDATE_SET, 0);
	if (er != erSuccess)
		goto exit;

	// Add SourceKey

	er = lpecSession->GetNewSourceKey(&sSourceKey);
	if(er != erSuccess)
		goto exit;
		
	er = RemoveStaleIndexedProp(lpDatabase, PR_SOURCE_KEY, sSourceKey, sSourceKey.size());
	if (er != erSuccess)
		goto exit;
        
	strQuery = "INSERT INTO indexedproperties(hierarchyid,tag,val_binary) VALUES(" + stringify(ulRootMapId) + "," + stringify(PROP_ID(PR_SOURCE_KEY)) + "," + lpDatabase->EscapeBinary(sSourceKey, sSourceKey.size()) + ")";
	er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		goto exit;

	// Add store entryid: 0x0FFF = PR_ENTRYID
	er = RemoveStaleIndexedProp(lpDatabase, PR_ENTRYID, sStoreId.__ptr, sStoreId.__size);
	if (er != erSuccess)
		goto exit;
        
	strQuery = "INSERT INTO indexedproperties (hierarchyid,tag,val_binary) VALUES(" + stringify(ulStoreId) + ", 4095, " + lpDatabase->EscapeBinary(sStoreId.__ptr, sStoreId.__size) + ")";
	er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		goto exit;

	// Add rootfolder entryid: 0x0FFF = PR_ENTRYID
	er = RemoveStaleIndexedProp(lpDatabase, PR_ENTRYID, sRootId.__ptr, sRootId.__size);
	if (er != erSuccess)
		goto exit;
        
	strQuery = "INSERT INTO indexedproperties (hierarchyid,tag,val_binary) VALUES(" + stringify(ulRootMapId) + ", 4095, " + lpDatabase->EscapeBinary(sRootId.__ptr, sRootId.__size) + ")";
	er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		goto exit;

	// Add rootfolder type: 0x3601 = FOLDER_ROOT (= 0)
	strQuery = "INSERT INTO properties (tag,type,hierarchyid,val_ulong) VALUES(13825, 3, " + stringify(ulRootMapId) + ", 0)";
	er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		goto exit;
		
    now = time(NULL);
    for (size_t i = 0; i < ARRAY_SIZE(timeProps); ++i) {
        sProp.ulPropTag = timeProps[i];
        sProp.__union = SOAP_UNION_propValData_hilo;
        sProp.Value.hilo = &sHilo;
        
        UnixTimeToFileTime(now, &sProp.Value.hilo->hi, &sProp.Value.hilo->lo);
        
        WriteProp(lpDatabase, ulStoreId, 0, &sProp);
        if(er != erSuccess)
            goto exit;
            
        WriteProp(lpDatabase, ulRootMapId, 0, &sProp);
        if(er != erSuccess)
            goto exit;
    }

	// Couple store with user
	strQuery = "INSERT INTO stores(hierarchy_id, user_id, type, user_name, company, guid) VALUES(" +
		stringify(ulStoreId) + ", " +
		stringify(ulUserId) + ", " +
		stringify(ulStoreType) + ", " +
		"'" + lpDatabase->Escape(userDetails.GetPropString(OB_PROP_S_LOGIN)) + "', " +
		stringify(ulCompanyId) + ", " +
		lpDatabase->EscapeBinary(&guidStore, sizeof(GUID)) + ")";
	er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		goto exit;

	/* Set ACLs on public store */
	if(ulStoreType == ECSTORE_TYPE_PUBLIC) {
		// ulUserId == a group
		// ulUserId 1 = group everyone
		srightsArray.__ptr = s_alloc<rights>(nullptr, 1);
		srightsArray.__ptr[0].ulRights = ecRightsDefaultPublic;
		srightsArray.__ptr[0].ulUserid = ulUserId;
		srightsArray.__ptr[0].ulState = RIGHT_NEW|RIGHT_AUTOUPDATE_DENIED;
		srightsArray.__ptr[0].ulType = ACCESS_TYPE_GRANT;
		memset(&srightsArray.__ptr[0].sUserId, 0, sizeof(entryId));

		srightsArray.__size = 1;
		er = lpecSession->GetSecurity()->SetRights(ulStoreId, &srightsArray);
		if(er != erSuccess)
			goto exit;
	}

	er = lpDatabase->Commit();
	if (er != erSuccess)
		goto exit;

	ec_log_info("Finished create store (userid=%d, storeid=%d, type=%d)", ulUserId, ulStoreId, ulStoreType);

exit:
	if(er == KCERR_NO_ACCESS)
		ec_log_err("Failed to create store access denied");
	else if(er != erSuccess)
		ec_log_err("Failed to create store (id=%d), errorcode=0x%08X", ulUserId, er);
	s_free(nullptr, srightsArray.__ptr);
	ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(createGroup, lpsSetGroupResponse->er, struct group *lpsGroup, struct setGroupResponse *lpsSetGroupResponse)
{
	unsigned int			ulGroupId = 0;
	objectdetails_t			details(DISTLIST_SECURITY); // DB plugin wants to be able to set permissions on groups

	if (lpsGroup == nullptr || lpsGroup->lpszGroupname == nullptr ||
	    lpsGroup->lpszFullname == nullptr)
		return KCERR_INVALID_PARAMETER;
	if (!bSupportUnicode) {
		er = FixGroupEncoding(soap, stringCompat, In, lpsGroup);
		if (er != erSuccess)
			return er;
	}

	er = CopyGroupDetailsFromSoap(lpsGroup, NULL, &details, soap);
	if (er != erSuccess)
		return er;
	auto usrmgt = lpecSession->GetUserManagement();
	er = usrmgt->UpdateUserDetailsFromClient(&details);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(details.GetPropInt(OB_PROP_I_COMPANYID));
	if(er != erSuccess)
		return er;
	er = usrmgt->CreateObjectAndSync(details, &ulGroupId);
	if (er != erSuccess)
		return er;
	er = GetABEntryID(ulGroupId, soap, &lpsSetGroupResponse->sGroupId);
	if (er != erSuccess)
		return er;

	lpsSetGroupResponse->ulGroupId = ulGroupId;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(setGroup, *result, struct group *lpsGroup, unsigned int *result)
{
	objectdetails_t details;
	unsigned int	ulGroupId = 0;
	objectid_t		sExternId;
	
	if (lpsGroup == nullptr)
		return KCERR_INVALID_PARAMETER;
	if (!bSupportUnicode) {
		er = FixGroupEncoding(soap, stringCompat, In, lpsGroup);
		if (er != erSuccess)
			return er;
	}

	if (lpsGroup->sGroupId.__size > 0 && lpsGroup->sGroupId.__ptr != NULL)
	{
		er = GetLocalId(lpsGroup->sGroupId, lpsGroup->ulGroupId, &ulGroupId, &sExternId);
		if (er != erSuccess) 
			return er;
	}
	else
		ulGroupId = lpsGroup->ulGroupId;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulGroupId);
	if(er != erSuccess)
		return er;

	details = objectdetails_t(DISTLIST_GROUP);

	er = CopyGroupDetailsFromSoap(lpsGroup, &sExternId.id, &details, soap);
	if (er != erSuccess)
		return er;
	auto usrmgt = lpecSession->GetUserManagement();
	er = usrmgt->UpdateUserDetailsFromClient(&details);
	if (er != erSuccess)
		return er;
	return usrmgt->SetObjectDetailsAndSync(ulGroupId, details, nullptr);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getGroup, lpsResponse->er, unsigned int ulGroupId, entryId sGroupId, struct getGroupResponse *lpsResponse)
{
	objectdetails_t details;
	entryId sTmpGroupId;

	er = GetLocalId(sGroupId, ulGroupId, &ulGroupId, NULL);
	if (er != erSuccess)
		return er;

	/* Check if we are able to view the returned userobject */
	er = lpecSession->GetSecurity()->IsUserObjectVisible(ulGroupId);
	if (er != erSuccess)
		return er;
	er = lpecSession->GetUserManagement()->GetObjectDetails(ulGroupId, &details);
	if (er != erSuccess)
		return er;
	if (OBJECTCLASS_TYPE(details.GetClass()) != OBJECTTYPE_DISTLIST)
		return KCERR_NOT_FOUND;

	lpsResponse->lpsGroup = s_alloc<group>(soap);

	er = GetABEntryID(ulGroupId, soap, &sTmpGroupId);
	if (er == erSuccess)
		er = CopyGroupDetailsToSoap(ulGroupId, &sTmpGroupId, details, lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON, soap, lpsResponse->lpsGroup);
	if (er != erSuccess)
		return er;
	if (!bSupportUnicode) {
		er = FixGroupEncoding(soap, stringCompat, Out, lpsResponse->lpsGroup);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getGroupList, lpsGroupList->er, unsigned int ulCompanyId, entryId sCompanyId, struct groupListResponse *lpsGroupList)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpGroups;
	entryId	sGroupEid;

	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;

	/* Input check, if ulCompanyId is 0, we want the user's company,
	 * otherwise we must check if the requested company is visible for the user. */
	auto sec = lpecSession->GetSecurity();
	if (ulCompanyId == 0)
		er = sec->GetUserCompany(&ulCompanyId);
	else
		er = sec->IsUserObjectVisible(ulCompanyId);
	if (er != erSuccess)
		return er;
	er = lpecSession->GetUserManagement()->GetCompanyObjectListAndSync(OBJECTCLASS_DISTLIST, ulCompanyId, &unique_tie(lpGroups), 0);
	if (er != erSuccess)
		return er;

	lpsGroupList->sGroupArray.__size = 0;
	lpsGroupList->sGroupArray.__ptr = s_alloc<group>(soap, lpGroups->size());
	for (const auto &grp : *lpGroups) {
		if (OBJECTCLASS_TYPE(grp.GetClass()) != OBJECTTYPE_DISTLIST)
			continue;
		er = GetABEntryID(grp.ulId, soap, &sGroupEid);
		if (er != erSuccess)
			return er;
		er = CopyGroupDetailsToSoap(grp.ulId, &sGroupEid, grp,
		     lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON,
		     soap, &lpsGroupList->sGroupArray.__ptr[lpsGroupList->sGroupArray.__size]);
		if (er != erSuccess)
			return er;
		if (!bSupportUnicode) {
			er = FixGroupEncoding(soap, stringCompat, Out, lpsGroupList->sGroupArray.__ptr + lpsGroupList->sGroupArray.__size);
			if (er != erSuccess)
				return er;
		}
		++lpsGroupList->sGroupArray.__size;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(groupDelete, *result, unsigned int ulGroupId, entryId sGroupId, unsigned int *result)
{
	er = GetLocalId(sGroupId, ulGroupId, &ulGroupId, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulGroupId);
	if(er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->DeleteObjectAndSync(ulGroupId);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(resolveUsername, lpsResponse->er, const char *lpszUsername,
    struct resolveUserResponse *lpsResponse)
{
	unsigned int		ulUserId = 0;

	if (lpszUsername == nullptr)
		return KCERR_INVALID_PARAMETER;
	er = lpecSession->GetUserManagement()->ResolveObjectAndSync(OBJECTCLASS_USER, STRIN_FIX(lpszUsername), &ulUserId);
	if (er != erSuccess)
		return er;

	/* Check if we are able to view the returned userobject */
	er = lpecSession->GetSecurity()->IsUserObjectVisible(ulUserId);
	if (er != erSuccess)
		return er;
	er = GetABEntryID(ulUserId, soap, &lpsResponse->sUserId);
	if (er != erSuccess)
		return er;
	lpsResponse->ulUserId = ulUserId;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(resolveGroupname, lpsResponse->er, const char *lpszGroupname,
    struct resolveGroupResponse *lpsResponse)
{
	unsigned int	ulGroupId = 0;

	if (lpszGroupname == nullptr)
		return KCERR_INVALID_PARAMETER;
	er = lpecSession->GetUserManagement()->ResolveObjectAndSync(OBJECTCLASS_DISTLIST, STRIN_FIX(lpszGroupname), &ulGroupId);
	if (er != erSuccess)
		return er;

	/* Check if we are able to view the returned userobject */
	er = lpecSession->GetSecurity()->IsUserObjectVisible(ulGroupId);
	if (er != erSuccess)
		return er;
	er = GetABEntryID(ulGroupId, soap, &lpsResponse->sGroupId);
	if (er != erSuccess)
		return er;
	lpsResponse->ulGroupId = ulGroupId;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(deleteGroupUser, *result, unsigned int ulGroupId, entryId sGroupId, unsigned int ulUserId, entryId sUserId, unsigned int *result)
{
	er = GetLocalId(sGroupId, ulGroupId, &ulGroupId, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulGroupId);
	if(er != erSuccess)
		return er;
	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->DeleteSubObjectFromObjectAndSync(OBJECTRELATION_GROUP_MEMBER, ulGroupId, ulUserId);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(addGroupUser, *result, unsigned int ulGroupId, entryId sGroupId, unsigned int ulUserId, entryId sUserId, unsigned int *result)
{
	er = GetLocalId(sGroupId, ulGroupId, &ulGroupId, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulGroupId);
	if(er != erSuccess)
		return er;
	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->AddSubObjectToObjectAndSync(OBJECTRELATION_GROUP_MEMBER, ulGroupId, ulUserId);
}
SOAP_ENTRY_END()

// not only returns users of a group anymore
// TODO resolve group in group here on the fly?
SOAP_ENTRY_START(getUserListOfGroup, lpsUserList->er, unsigned int ulGroupId, entryId sGroupId, struct userListResponse *lpsUserList)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpUsers;
	entryId sUserEid;

	er = GetLocalId(sGroupId, ulGroupId, &ulGroupId, NULL);
	if (er != erSuccess)
		return er;
	auto sec = lpecSession->GetSecurity();
	er = sec->IsUserObjectVisible(ulGroupId);
	if (er != erSuccess)
		return er;
    er = lpecSession->GetUserManagement()->GetSubObjectsOfObjectAndSync(OBJECTRELATION_GROUP_MEMBER, ulGroupId, &unique_tie(lpUsers));
    if(er != erSuccess)
		return er;
    lpsUserList->sUserArray.__size = 0;
    lpsUserList->sUserArray.__ptr = s_alloc<user>(soap, lpUsers->size());

	for (const auto &user : *lpUsers) {
		if (sec->IsUserObjectVisible(user.ulId) != erSuccess)
			continue;
		er = GetABEntryID(user.ulId, soap, &sUserEid);
		if (er != erSuccess)
			return er;

		// @todo Whoops, we can have group-in-groups. But since details of a group are almost identical to user details (e.g. name, fullname, email)
		// this copy will succeed without any problems ... but it's definitly not correct.
		er = CopyUserDetailsToSoap(user.ulId, &sUserEid, user,
		     lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON,
		     soap, &lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size]);
		if (er != erSuccess)
			return er;

		// 6.40.0 stores the object class in the IsNonActive field
		if (lpecSession->ClientVersion() == ZARAFA_VERSION_6_40_0)
			lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulIsNonActive = lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulObjClass;

		if (!bSupportUnicode) {
			er = FixUserEncoding(soap, stringCompat, Out, lpsUserList->sUserArray.__ptr + lpsUserList->sUserArray.__size);
			if (er != erSuccess)
				return er;
		}
		++lpsUserList->sUserArray.__size;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getGroupListOfUser, lpsGroupList->er, unsigned int ulUserId, entryId sUserId, struct groupListResponse *lpsGroupList)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpGroups;
	entryId sGroupEid;

	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		return er;
	auto sec = lpecSession->GetSecurity();
	er = sec->IsUserObjectVisible(ulUserId);
	if (er != erSuccess)
		return er;
	er = lpecSession->GetUserManagement()->GetParentObjectsOfObjectAndSync(OBJECTRELATION_GROUP_MEMBER, ulUserId, &unique_tie(lpGroups));
	if(er != erSuccess)
		return er;

	lpsGroupList->sGroupArray.__size = 0;
	lpsGroupList->sGroupArray.__ptr = s_alloc<group>(soap, lpGroups->size());
	for (const auto &grp : *lpGroups) {
		if (sec->IsUserObjectVisible(grp.ulId) != erSuccess)
			continue;
		er = GetABEntryID(grp.ulId, soap, &sGroupEid);
		if (er != erSuccess)
			return er;
		er = CopyGroupDetailsToSoap(grp.ulId, &sGroupEid, grp,
		     lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON,
		     soap, &lpsGroupList->sGroupArray.__ptr[lpsGroupList->sGroupArray.__size]);
		if (er != erSuccess)
			return er;
		if (!bSupportUnicode) {
			er = FixGroupEncoding(soap, stringCompat, Out, lpsGroupList->sGroupArray.__ptr + lpsGroupList->sGroupArray.__size);
			if (er != erSuccess)
				return er;
		}
		++lpsGroupList->sGroupArray.__size;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(createCompany, lpsResponse->er, struct company *lpsCompany, struct setCompanyResponse *lpsResponse)
{
	unsigned int ulCompanyId = 0;
	objectdetails_t details(CONTAINER_COMPANY);

	if (lpsCompany == nullptr)
		return KCERR_INVALID_PARAMETER;
	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;

	// Check permission, only the system user is allowed to create or delete a company
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(KOPANO_UID_SYSTEM);
	if(er != erSuccess)
		return er;
	if (!bSupportUnicode) {
		er = FixCompanyEncoding(soap, stringCompat, In, lpsCompany);
		if (er != erSuccess)
			return er;
	}

	er = CopyCompanyDetailsFromSoap(lpsCompany, NULL, KOPANO_UID_SYSTEM, &details, soap);
	if (er != erSuccess)
		return er;
	auto usrmgt = lpecSession->GetUserManagement();
	er = usrmgt->UpdateUserDetailsFromClient(&details);
	if (er != erSuccess)
		return er;
	er = usrmgt->CreateObjectAndSync(details, &ulCompanyId);
	if(er != erSuccess)
		return er;
	er = GetABEntryID(ulCompanyId, soap, &lpsResponse->sCompanyId);
	if (er != erSuccess)
		return er;
	lpsResponse->ulCompanyId = ulCompanyId;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(deleteCompany, *result, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result)
{
	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;

	// Check permission, only the system user is allowed to create or delete a company
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(KOPANO_UID_SYSTEM);
	if(er != erSuccess)
		return er;
	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->DeleteObjectAndSync(ulCompanyId);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(setCompany, *result, struct company *lpsCompany, unsigned int *result)
{
	objectdetails_t details;
	unsigned int	ulCompanyId = 0;
	unsigned int	ulAdministrator = 0;
	objectid_t		sExternId;
	
	if (lpsCompany == nullptr)
		return KCERR_INVALID_PARAMETER;
	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	if (!bSupportUnicode) {
		er = FixCompanyEncoding(soap, stringCompat, In, lpsCompany);
		if (er != erSuccess)
			return er;
	}

	if (lpsCompany->sCompanyId.__size > 0 && lpsCompany->sCompanyId.__ptr != NULL)
	{
		er = GetLocalId(lpsCompany->sCompanyId, lpsCompany->ulCompanyId, &ulCompanyId, &sExternId);
		if (er != erSuccess) 
			return er;
	}
	else
		ulCompanyId = lpsCompany->ulCompanyId;

	er = GetLocalId(lpsCompany->sAdministrator, lpsCompany->ulAdministrator, &ulAdministrator, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulCompanyId);
	if(er != erSuccess)
		return er;

	details = objectdetails_t(CONTAINER_COMPANY);

	er = CopyCompanyDetailsFromSoap(lpsCompany, &sExternId.id, ulAdministrator, &details, soap);
	if (er != erSuccess)
		return er;
	auto usrmgt = lpecSession->GetUserManagement();
	er = usrmgt->UpdateUserDetailsFromClient(&details);
	if (er != erSuccess)
		return er;
	return usrmgt->SetObjectDetailsAndSync(ulCompanyId, details, nullptr);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getCompany, lpsResponse->er, unsigned int ulCompanyId, entryId sCompanyId, struct getCompanyResponse *lpsResponse)
{
	objectdetails_t details;
	unsigned int ulAdmin = 0;
	entryId sAdminEid, sTmpCompanyId;

	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;

	/* Input check, if ulCompanyId is 0, we want the user's company,
	 * otherwise we must check if the requested company is visible for the user. */
	auto sec = lpecSession->GetSecurity();
	if (ulCompanyId == 0)
		er = sec->GetUserCompany(&ulCompanyId);
	else
		er = sec->IsUserObjectVisible(ulCompanyId);
	if (er != erSuccess)
		return er;
	er = lpecSession->GetUserManagement()->GetObjectDetails(ulCompanyId, &details);
	if(er != erSuccess)
		return er;
	if (details.GetClass() != CONTAINER_COMPANY)
		return KCERR_NOT_FOUND;

	ulAdmin = details.GetPropInt(OB_PROP_I_SYSADMIN);
	er = sec->IsUserObjectVisible(ulAdmin);
	if (er != erSuccess)
		return er;
	er = GetABEntryID(ulAdmin, soap, &sAdminEid);
	if (er != erSuccess)
		return er;
	er = GetABEntryID(ulCompanyId, soap, &sTmpCompanyId);
	if (er != erSuccess)
		return er;

	lpsResponse->lpsCompany = s_alloc<company>(soap);
	memset(lpsResponse->lpsCompany, 0, sizeof(company));
	er = CopyCompanyDetailsToSoap(ulCompanyId, &sTmpCompanyId, ulAdmin, &sAdminEid, details, lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON, soap, lpsResponse->lpsCompany);
	if (er != erSuccess)
		return er;
	if (!bSupportUnicode) {
		er = FixCompanyEncoding(soap, stringCompat, Out, lpsResponse->lpsCompany);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(resolveCompanyname, lpsResponse->er,
    const char *lpszCompanyname, struct resolveCompanyResponse *lpsResponse)
{
	unsigned int ulCompanyId = 0;

	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	if (lpszCompanyname == nullptr)
		return KCERR_INVALID_PARAMETER;
	er = lpecSession->GetUserManagement()->ResolveObjectAndSync(CONTAINER_COMPANY, STRIN_FIX(lpszCompanyname), &ulCompanyId);
	if(er != erSuccess)
		return er;

	/* Check if we are able to view the returned userobject */
	er = lpecSession->GetSecurity()->IsUserObjectVisible(ulCompanyId);
	if (er != erSuccess)
		return er;
	er = GetABEntryID(ulCompanyId, soap, &lpsResponse->sCompanyId);
	if (er != erSuccess)
		return er;
	lpsResponse->ulCompanyId = ulCompanyId;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getCompanyList, lpsCompanyList->er, struct companyListResponse *lpsCompanyList)
{
	unsigned int	ulAdmin = 0;
	entryId sCompanyEid, sAdminEid;
	std::unique_ptr<std::list<localobjectdetails_t> > lpCompanies;

	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	auto sec = lpecSession->GetSecurity();
	er = sec->GetViewableCompanyIds(0, &unique_tie(lpCompanies));
	if(er != erSuccess)
		return er;

	lpsCompanyList->sCompanyArray.__size = 0;
	lpsCompanyList->sCompanyArray.__ptr = s_alloc<company>(soap, lpCompanies->size());
	for (const auto &com : *lpCompanies) {
		ulAdmin = com.GetPropInt(OB_PROP_I_SYSADMIN);
		er = sec->IsUserObjectVisible(ulAdmin);
		if (er != erSuccess)
			return er;
		er = GetABEntryID(com.ulId, soap, &sCompanyEid);
		if (er != erSuccess)
			return er;
		er = GetABEntryID(ulAdmin, soap, &sAdminEid);
		if (er != erSuccess)
			return er;
		er = CopyCompanyDetailsToSoap(com.ulId, &sCompanyEid,
		     ulAdmin, &sAdminEid, com,
		     lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON,
		     soap, &lpsCompanyList->sCompanyArray.__ptr[lpsCompanyList->sCompanyArray.__size]);
		if (er != erSuccess)
			return er;
		if (!bSupportUnicode) {
			er = FixCompanyEncoding(soap, stringCompat, Out, lpsCompanyList->sCompanyArray.__ptr + lpsCompanyList->sCompanyArray.__size);
			if (er != erSuccess)
				return er;
		}
		++lpsCompanyList->sCompanyArray.__size;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(addCompanyToRemoteViewList, *result, unsigned int ulSetCompanyId, entryId sSetCompanyId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result)
{
	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulCompanyId);
	if(er != erSuccess)
		return er;
	er = GetLocalId(sSetCompanyId, ulSetCompanyId, &ulSetCompanyId, NULL);
	if (er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->AddSubObjectToObjectAndSync(OBJECTRELATION_COMPANY_VIEW, ulCompanyId, ulSetCompanyId);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(delCompanyFromRemoteViewList, *result, unsigned int ulSetCompanyId, entryId sSetCompanyId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result)
{
	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulCompanyId);
	if(er != erSuccess)
		return er;
	er = GetLocalId(sSetCompanyId, ulSetCompanyId, &ulSetCompanyId, NULL);
	if (er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->DeleteSubObjectFromObjectAndSync(OBJECTRELATION_COMPANY_VIEW, ulCompanyId, ulSetCompanyId);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getRemoteViewList, lpsCompanyList->er, unsigned int ulCompanyId, entryId sCompanyId, struct companyListResponse *lpsCompanyList)
{
	unsigned int	ulAdmin = 0;
	entryId sCompanyEid, sAdminEid;
	std::unique_ptr<std::list<localobjectdetails_t> > lpCompanies;

	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;

	/* Input check, if ulCompanyId is 0, we want the user's company,
	 * otherwise we must check if the requested company is visible for the user. */
	auto sec = lpecSession->GetSecurity();
	if (ulCompanyId == 0)
		er = sec->GetUserCompany(&ulCompanyId);
	else
		er = sec->IsUserObjectVisible(ulCompanyId);
	if (er != erSuccess)
		return er;
	er = lpecSession->GetUserManagement()->GetSubObjectsOfObjectAndSync(OBJECTRELATION_COMPANY_VIEW, ulCompanyId, &unique_tie(lpCompanies));
	if(er != erSuccess)
		return er;

	lpsCompanyList->sCompanyArray.__size = 0;
	lpsCompanyList->sCompanyArray.__ptr = s_alloc<company>(soap, lpCompanies->size());

	for (const auto &com : *lpCompanies) {
		if (sec->IsUserObjectVisible(com.ulId) != erSuccess)
			continue;
		ulAdmin = com.GetPropInt(OB_PROP_I_SYSADMIN);
		er = sec->IsUserObjectVisible(ulAdmin);
		if (er != erSuccess)
			return er;
		er = GetABEntryID(com.ulId, soap, &sCompanyEid);
		if (er != erSuccess)
			return er;
		er = GetABEntryID(ulAdmin, soap, &sAdminEid);
		if (er != erSuccess)
			return er;
		er = CopyCompanyDetailsToSoap(com.ulId, &sCompanyEid,
		     ulAdmin, &sAdminEid, com,
		     lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON,
		     soap, &lpsCompanyList->sCompanyArray.__ptr[lpsCompanyList->sCompanyArray.__size]);
		if (er != erSuccess)
			return er;
		if (!bSupportUnicode) {
			er = FixCompanyEncoding(soap, stringCompat, Out, lpsCompanyList->sCompanyArray.__ptr + lpsCompanyList->sCompanyArray.__size);
			if (er != erSuccess)
				return er;
		}
		++lpsCompanyList->sCompanyArray.__size;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(addUserToRemoteAdminList, *result, unsigned int ulUserId, entryId sUserId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result)
{
	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulCompanyId);
	if(er != erSuccess)
		return er;
	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->AddSubObjectToObjectAndSync(OBJECTRELATION_COMPANY_ADMIN, ulCompanyId, ulUserId);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(delUserFromRemoteAdminList, *result, unsigned int ulUserId, entryId sUserId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result)
{
	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulCompanyId);
	if(er != erSuccess)
		return er;
	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->DeleteSubObjectFromObjectAndSync(OBJECTRELATION_COMPANY_ADMIN, ulCompanyId, ulUserId);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getRemoteAdminList, lpsUserList->er, unsigned int ulCompanyId, entryId sCompanyId, struct userListResponse *lpsUserList)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpUsers;
	entryId sUserEid;

	if (!g_lpSessionManager->IsHostedSupported())
		return KCERR_NO_SUPPORT;
	er = GetLocalId(sCompanyId, ulCompanyId, &ulCompanyId, NULL);
	if (er != erSuccess)
		return er;

	/* Input check, if ulCompanyId is 0, we want the user's company,
	 * otherwise we must check if the requested company is visible for the user. */
	auto sec = lpecSession->GetSecurity();
	if (ulCompanyId == 0)
		er = sec->GetUserCompany(&ulCompanyId);
	else
		er = sec->IsUserObjectVisible(ulCompanyId);
	if (er != erSuccess)
		return er;

	// only users can be admins, nonactive users make no sense.
	er = lpecSession->GetUserManagement()->GetSubObjectsOfObjectAndSync(OBJECTRELATION_COMPANY_ADMIN, ulCompanyId, &unique_tie(lpUsers));
	if(er != erSuccess)
		return er;

	lpsUserList->sUserArray.__size = 0;
	lpsUserList->sUserArray.__ptr = s_alloc<user>(soap, lpUsers->size());
	for (const auto &user : *lpUsers) {
		if (sec->IsUserObjectVisible(user.ulId) != erSuccess)
			continue;
		er = GetABEntryID(user.ulId, soap, &sUserEid);
		if (er != erSuccess)
			return er;
		er = CopyUserDetailsToSoap(user.ulId, &sUserEid, user,
		     lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON,
		     soap, &lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size]);
		if (er != erSuccess)
			return er;

		// 6.40.0 stores the object class in the IsNonActive field
		if (lpecSession->ClientVersion() == ZARAFA_VERSION_6_40_0)
			lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulIsNonActive = lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulObjClass;

		if (!bSupportUnicode) {
			er = FixUserEncoding(soap, stringCompat, Out, lpsUserList->sUserArray.__ptr + lpsUserList->sUserArray.__size);
			if (er != erSuccess)
				return er;
		}
		++lpsUserList->sUserArray.__size;
		if (sUserEid.__ptr)
		{
			// sUserEid is placed in userdetails, no need to free
			sUserEid.__ptr = NULL;
			sUserEid.__size = 0;
		}
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(submitMessage, *result, entryId sEntryId, unsigned int ulFlags, unsigned int *result)
{
	unsigned int	ulParentId	= 0;
	unsigned int	ulObjId = 0;
	unsigned int	ulMsgFlags 	= 0;
	unsigned int	ulStoreId = 0;
	unsigned int	ulStoreOwner = 0;
	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey;
	bool			bMessageChanged = false;

	eQuotaStatus	QuotaStatus;
	long long		llStoreSize = 0;
	objectdetails_t details;
	auto cache = lpecSession->GetSessionManager()->GetCacheManager();
	auto sec = lpecSession->GetSecurity();
	
	USE_DATABASE();

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if(er != erSuccess)
		return er;
	er = cache->GetStore(ulObjId, &ulStoreId, nullptr);
	if(er != erSuccess)
		return er;
    er = cache->GetObject(ulObjId, &ulParentId, nullptr, &ulMsgFlags, nullptr);
    if(er != erSuccess)
		return er;
    er = cache->GetObject(ulStoreId, nullptr, &ulStoreOwner, nullptr, nullptr);
    if(er != erSuccess)
		return er;
    er = lpecSession->GetUserManagement()->GetObjectDetails(ulStoreOwner, &details);
    if(er != erSuccess)
		return er;
         
    // Cannot submit a message in a public store   
	if (OBJECTCLASS_TYPE(details.GetClass()) != OBJECTTYPE_MAILUSER)
		return KCERR_NO_ACCESS;

	// Check permission
	er = sec->CheckPermission(ulStoreId, ecSecurityOwner);
	if(er != erSuccess)
		return er;

	// Quota check
	er = sec->GetStoreSize(ulStoreId, &llStoreSize);
	if(er != erSuccess)
		return er;
	er = sec->CheckQuota(ulStoreId, llStoreSize, &QuotaStatus);
	if(er != erSuccess)
		return er;
	if (QuotaStatus == QUOTA_SOFTLIMIT || QuotaStatus == QUOTA_HARDLIMIT)
		return KCERR_STORE_FULL;
	er = lpDatabase->Begin();
	if(er != erSuccess)
		return er;

	// Set PR_MESSAGE_FLAGS to MSGFLAG_SUBMIT|MSGFLAG_UNSENT
	if(!(ulFlags & EC_SUBMIT_MASTER)) {
	    // Set the submit flag (because it has just been submitted), and set it to UNSENT, as it has definitely
	    // not been sent if the user has just submitted it.
		strQuery = "UPDATE properties SET val_ulong=val_ulong|"+stringify(MSGFLAG_SUBMIT|MSGFLAG_UNSENT)+" where hierarchyid="+stringify(ulObjId) + " and tag=" + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " and type=" + stringify(PROP_TYPE(PR_MESSAGE_FLAGS));
		er = lpDatabase->DoUpdate(strQuery);
		if(er != erSuccess)
			goto exit;

		// Add change to ICS
		GetSourceKey(ulObjId, &sSourceKey);
		GetSourceKey(ulParentId, &sParentSourceKey);

		AddChange(lpecSession, 0, sSourceKey, sParentSourceKey, ICS_MESSAGE_CHANGE);

		// Mask for notification
		bMessageChanged = true;
	}
	
	er = UpdateTProp(lpDatabase, PR_MESSAGE_FLAGS, ulParentId, ulObjId);
	if(er != erSuccess)
		goto exit;

	// Insert the message into the outgoing queue
	strQuery = "INSERT IGNORE INTO outgoingqueue (store_id, hierarchy_id, flags) VALUES("+stringify(ulStoreId)+", "+stringify(ulObjId)+","+stringify(ulFlags)+")";
	er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		goto exit;

	er = lpDatabase->Commit();
	if(er != erSuccess)
		goto exit;

	if (bMessageChanged) {
		// Update cache
		auto gcache = g_lpSessionManager->GetCacheManager();
		gcache->Update(fnevObjectModified, ulObjId);
		gcache->Update(fnevObjectModified, ulParentId);

		// Notify
		g_lpSessionManager->NotificationModified(MAPI_MESSAGE, ulObjId, ulParentId);
		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParentId, ulObjId, MAPI_MESSAGE);
	}
	
	g_lpSessionManager->UpdateOutgoingTables(ECKeyTable::TABLE_ROW_ADD, ulStoreId, ulObjId, ulFlags, MAPI_MESSAGE);

exit:
    ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(finishedMessage, *result,  entryId sEntryId, unsigned int ulFlags, unsigned int *result)
{
	unsigned int	ulParentId	= 0;
	unsigned int	ulGrandParentId = 0;
	unsigned int	ulAffectedRows = 0;
	unsigned int	ulStoreId = (unsigned int)-1; // not 0 security issue
	unsigned int	ulObjId = 0;
	unsigned int	ulPrevFlags = 0;
	bool			bMessageChanged = false;
	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey;
	auto gcache = g_lpSessionManager->GetCacheManager();
	auto cache = lpecSession->GetSessionManager()->GetCacheManager();

	USE_DATABASE();

	er = lpDatabase->Begin();
	if(er != erSuccess)
		return er;
	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if(er != erSuccess)
	    goto exit;
	er = gcache->GetParent(ulObjId, &ulParentId);
	if(er != erSuccess)
		goto exit;

	//Get storeid
	er = cache->GetStore(ulObjId, &ulStoreId, NULL);
	switch (er) {
	case erSuccess:
		break;
	case KCERR_NOT_FOUND:
		// ulObjId should be in outgoingtable, but the ulStoreId cannot be retrieved
		// because ulObjId does not exist in the hierarchy table, so we remove the message
		// fix table and notify and pass error to caller

		ec_log_warn("Unable to find store for hierarchy id %d", ulObjId);
		ulStoreId = 0;
		goto table;
	default:
		goto exit;				// database error
	}

	// Check permission
	er = lpecSession->GetSecurity()->CheckPermission(ulStoreId, ecSecurityOwner);
	if(er != erSuccess)
		goto exit;
		
    strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid="+stringify(ulObjId) + " AND tag=" + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND type=" + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) + " FOR UPDATE";
    er = lpDatabase->DoSelect(strQuery, &lpDBResult);
    if(er != erSuccess)
		goto exit;

	lpDBRow = lpDBResult.fetch_row();
    if(lpDBRow == NULL || lpDBRow[0] == NULL) {
	        er = KCERR_DATABASE_ERROR;
		ec_log_err("finishedMessages(): row/col null");
	        goto exit;
    }
    
    ulPrevFlags = atoui(lpDBRow[0]);

	strQuery = "UPDATE properties ";

	if (!(ulFlags & EC_SUBMIT_MASTER))
        // Removing from local queue; remove submit flag and unsent flag
	    strQuery += " SET val_ulong=val_ulong&~"+stringify(MSGFLAG_SUBMIT|MSGFLAG_UNSENT);
	else if (ulFlags & EC_SUBMIT_DOSENTMAIL)
        // Removing from master queue
            // Spooler sent message and moved, remove submit flag and unsent flag
    	    strQuery += " SET val_ulong=val_ulong&~" +stringify(MSGFLAG_SUBMIT|MSGFLAG_UNSENT);
        else
            // Spooler only sent message
            strQuery += " SET val_ulong=val_ulong&~" +stringify(MSGFLAG_UNSENT);

    // Always set message read
    strQuery += ", val_ulong=val_ulong|" + stringify(MSGFLAG_READ) + " WHERE hierarchyid="+stringify(ulObjId) + " AND tag=" + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND type=" + stringify(PROP_TYPE(PR_MESSAGE_FLAGS));

   	er = lpDatabase->DoUpdate(strQuery);
   	if(er != erSuccess)
   		goto exit;
   		
	er = UpdateTProp(lpDatabase, PR_MESSAGE_FLAGS, ulParentId, ulObjId);
	if(er != erSuccess)
		goto exit;

    if(!(ulPrevFlags & MSGFLAG_READ)) {
        // The item has been set read, decrease the unread counter for the folder
        er = UpdateFolderCount(lpDatabase, ulParentId, PR_CONTENT_UNREAD, -1);
        if(er != erSuccess)
            goto exit;
    }

	GetSourceKey(ulObjId, &sSourceKey);
	GetSourceKey(ulParentId, &sParentSourceKey);

	AddChange(lpecSession, 0, sSourceKey, sParentSourceKey, ICS_MESSAGE_CHANGE);
    
	// NOTE: Unlock message is done in client

	// Mark for notification
	bMessageChanged = true;

table:

	// delete the message from the outgoing queue
	strQuery = "DELETE FROM outgoingqueue WHERE hierarchy_id="+stringify(ulObjId) + " AND flags & 1=" + stringify(ulFlags & 1);
	er = lpDatabase->DoDelete(strQuery, &ulAffectedRows);
	if(er != erSuccess)
		goto exit;

	er = lpDatabase->Commit();
	if(er != erSuccess)
		goto exit;

	// Remove messge from the outgoing queue
	g_lpSessionManager->UpdateOutgoingTables(ECKeyTable::TABLE_ROW_DELETE, ulStoreId, ulObjId, ulFlags, MAPI_MESSAGE);

	// The flags have changed, so we have to send a modified 
	if (bMessageChanged) {
		cache->Update(fnevObjectModified, ulObjId);
		g_lpSessionManager->NotificationModified(MAPI_MESSAGE, ulObjId, ulParentId);

		if (gcache->GetParent(ulObjId, &ulParentId) == erSuccess) {
			gcache->Update(fnevObjectModified, ulParentId);
			g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulParentId);
	        
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParentId, ulObjId, MAPI_MESSAGE);
			if (gcache->GetParent(ulParentId, &ulGrandParentId) == erSuccess)
				g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulGrandParentId, ulParentId, MAPI_FOLDER);
		}
	}

exit:
    ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(abortSubmit, *result, entryId sEntryId, unsigned int *result)
{
	unsigned int	ulParentId	= 0;
	unsigned int	ulStoreId	= 0;
	unsigned int	ulSubmitFlags = 0;
	unsigned int	ulGrandParentId = 0;
	unsigned int	ulObjId = 0;
	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey;
	auto gcache = g_lpSessionManager->GetCacheManager();

	USE_DATABASE();

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if(er != erSuccess)
		return er;
	er = gcache->GetParent(ulObjId, &ulParentId);
	if(er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityOwner);
	if(er != erSuccess)
		return er;

	//Get storeid
	er = lpecSession->GetSessionManager()->GetCacheManager()->GetStore(ulObjId, &ulStoreId, NULL);
	if(er != erSuccess)
		return er;
	er = lpDatabase->Begin();
	if(er != erSuccess)
		return er;

	// Get storeid and check if the message into the queue
	strQuery = "SELECT store_id, flags FROM outgoingqueue WHERE hierarchy_id="+stringify(ulObjId) + " LIMIT 2";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;
// FIXME: can be also more than 2??
	if (lpDBResult.get_num_rows() != 1) {
		er = KCERR_NOT_IN_QUEUE;
		goto exit;
	}
	lpDBRow = lpDBResult.fetch_row();
	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("abortSubmit(): row/col null");
		goto exit;
	}

	ulStoreId = atoui(lpDBRow[0]);
	ulSubmitFlags = atoi(lpDBRow[1]);

	// delete the message from the outgoing queue
	strQuery = "DELETE FROM outgoingqueue WHERE hierarchy_id="+stringify(ulObjId);
	er = lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		goto exit;

	// remove in property PR_MESSAGE_FLAGS the MSGFLAG_SUBMIT flag
	strQuery = "UPDATE properties SET val_ulong=val_ulong& ~"+stringify(MSGFLAG_SUBMIT)+" WHERE hierarchyid="+stringify(ulObjId)+ " AND tag=" + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND type=" + stringify(PROP_TYPE(PR_MESSAGE_FLAGS));
	er = lpDatabase->DoUpdate(strQuery);
	if(er != erSuccess)
		goto exit;
		
	er = UpdateTProp(lpDatabase, PR_MESSAGE_FLAGS, ulParentId, ulObjId);
	if(er != erSuccess)
		goto exit;
	
	// Update ICS system
	GetSourceKey(ulObjId, &sSourceKey);
	GetSourceKey(ulParentId, &sParentSourceKey);

	AddChange(lpecSession, 0, sSourceKey, sParentSourceKey, ICS_MESSAGE_CHANGE);
    
	er = lpDatabase->Commit();
	if(er != erSuccess)
		goto exit;

	g_lpSessionManager->UpdateOutgoingTables(ECKeyTable::TABLE_ROW_DELETE, ulStoreId, ulObjId, ulSubmitFlags, MAPI_MESSAGE);
	if (gcache->GetParent(ulObjId, &ulParentId) == erSuccess) {
		gcache->Update(fnevObjectModified, ulObjId);
		g_lpSessionManager->NotificationModified(MAPI_MESSAGE, ulObjId, ulParentId);
		gcache->Update(fnevObjectModified, ulParentId);
		g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulParentId);

		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParentId, ulObjId, MAPI_MESSAGE);
		if (gcache->GetParent(ulParentId, &ulGrandParentId) == erSuccess)
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulGrandParentId, ulParentId, MAPI_FOLDER);
	}

exit:
    ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(resolveStore, lpsResponse->er, struct xsd__base64Binary sStoreGuid, struct resolveUserStoreResponse *lpsResponse)
{
	USE_DATABASE();

	if (sStoreGuid.__ptr == nullptr || sStoreGuid.__size == 0)
		return KCERR_INVALID_PARAMETER;

	auto strStoreGuid = lpDatabase->EscapeBinary(sStoreGuid.__ptr, sStoreGuid.__size);
	// @todo: Check if this is supposed to work with public stores.
	strQuery =
		"SELECT u.id, s.hierarchy_id, s.guid, s.company "
		"FROM stores AS s "
		"LEFT JOIN users AS u "
			"ON s.user_id = u.id "
		"WHERE s.guid=" + strStoreGuid + " LIMIT 2";
	if(lpDatabase->DoSelect(strQuery, &lpDBResult) != erSuccess) {
		ec_log_err("resolveStore(): select failed %x", er);
		return KCERR_DATABASE_ERROR;
	}
	if (lpDBResult.get_num_rows() != 1)
		return KCERR_NOT_FOUND;
	lpDBRow = lpDBResult.fetch_row();
	lpDBLen = lpDBResult.fetch_row_lengths();
	if (lpDBRow == nullptr || lpDBRow[1] == nullptr ||
	    lpDBRow[2] == nullptr || lpDBRow[3] == nullptr ||
	    lpDBLen == nullptr)
		return KCERR_NOT_FOUND;
	if (lpDBRow[0] == NULL) {
		// check if we're admin over the store object
		er = lpecSession->GetSecurity()->IsAdminOverUserObject(atoi(lpDBRow[3]));
		if (er != erSuccess)
			return er;
		lpsResponse->ulUserId = 0;
		lpsResponse->sUserId.__size = 0;
		lpsResponse->sUserId.__ptr = NULL;
	} else {
		lpsResponse->ulUserId = atoi(lpDBRow[0]);

		er = GetABEntryID(lpsResponse->ulUserId, soap, &lpsResponse->sUserId);
		if (er != erSuccess)
			return er;
	}

	er = g_lpSessionManager->GetCacheManager()->GetEntryIdFromObject(atoui(lpDBRow[1]), soap, OPENSTORE_OVERRIDE_HOME_MDB, &lpsResponse->sStoreId);
	if(er != erSuccess)
		return er;
	lpsResponse->guid.__size = lpDBLen[2];
	lpsResponse->guid.__ptr = s_alloc<unsigned char>(soap, lpDBLen[2]);
	memcpy(lpsResponse->guid.__ptr, lpDBRow[2], lpDBLen[2]);
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(resolveUserStore, lpsResponse->er, const char *szUserName,
    unsigned int ulStoreTypeMask, unsigned int ulFlags,
    struct resolveUserStoreResponse *lpsResponse)
{
	unsigned int		ulObjectId = 0;
	objectdetails_t		sUserDetails;
	std::string strServerName;

	USE_DATABASE();

	if (szUserName == nullptr)
		return KCERR_INVALID_PARAMETER;

	szUserName = STRIN_FIX(szUserName);

	if (ulStoreTypeMask == 0)
		ulStoreTypeMask = ECSTORE_TYPE_MASK_PRIVATE | ECSTORE_TYPE_MASK_PUBLIC;

	auto usrmgt = lpecSession->GetUserManagement();
	er = usrmgt->ResolveObjectAndSync(OBJECTCLASS_USER, szUserName, &ulObjectId);
	if ((er == KCERR_NOT_FOUND || er == KCERR_INVALID_PARAMETER) && lpecSession->GetSessionManager()->IsHostedSupported())
		// FIXME: this function is being misused, szUserName can also be a company name
		er = usrmgt->ResolveObjectAndSync(CONTAINER_COMPANY, szUserName, &ulObjectId);
	if (er != erSuccess)
		return er;

	/* If we are allowed to view the user, we are allowed to know the store exists */
	auto sec = lpecSession->GetSecurity();
	er = sec->IsUserObjectVisible(ulObjectId);
	if (er != erSuccess)
		return er;
	er = usrmgt->GetObjectDetails(ulObjectId, &sUserDetails);
	if (er != erSuccess)
		return er;

	/* Only users and companies have a store */
	if ((OBJECTCLASS_TYPE(sUserDetails.GetClass()) == OBJECTTYPE_MAILUSER && sUserDetails.GetClass() == NONACTIVE_CONTACT) ||
		(OBJECTCLASS_TYPE(sUserDetails.GetClass()) != OBJECTTYPE_MAILUSER && sUserDetails.GetClass() != CONTAINER_COMPANY))
		return KCERR_NOT_FOUND;

	auto cfg = g_lpSessionManager->GetConfig();
	if (lpecSession->GetSessionManager()->IsDistributedSupported() && 
	    !usrmgt->IsInternalObject(ulObjectId)) 
	{
		if (ulStoreTypeMask & (ECSTORE_TYPE_MASK_PRIVATE | ECSTORE_TYPE_MASK_PUBLIC)) {
			/* Check if this is the correct server for its store */
			auto strServerName = sUserDetails.GetPropString(OB_PROP_S_SERVERNAME);
			if (strServerName.empty())
				return KCERR_NOT_FOUND;

			if (strcasecmp(strServerName.c_str(), cfg->GetSetting("server_name")) != 0) {
				if ((ulFlags & OPENSTORE_OVERRIDE_HOME_MDB) == 0) {
					std::string strServerPath;

					er = GetBestServerPath(soap, lpecSession, strServerName, &strServerPath);
					if (er != erSuccess)
						return er;

					lpsResponse->lpszServerPath = STROUT_FIX_CPY(strServerPath.c_str());
					ec_log_info("Redirecting request to \"%s\'", lpsResponse->lpszServerPath);
					g_lpStatsCollector->Increment(SCN_REDIRECT_COUNT, 1);
					return KCERR_UNABLE_TO_COMPLETE;
                }
			}
		}

		else if (ulStoreTypeMask & ECSTORE_TYPE_MASK_ARCHIVE) {
			// We allow an archive store to be resolved by sysadmins even if it's not supposed
			// to exist on this server for a particular user.
			if (sec->GetAdminLevel() < ADMIN_LEVEL_SYSADMIN &&
				!sUserDetails.PropListStringContains((property_key_t)PR_EC_ARCHIVE_SERVERS_A, cfg->GetSetting("server_name"), true))
				// No redirect with archive stores because there can be multiple archive stores.
				return KCERR_NOT_FOUND;
		}

		else {
			assert(false);
			return KCERR_NOT_FOUND;
		}
	}

	strQuery = "SELECT hierarchy_id, guid FROM stores WHERE user_id = " + stringify(ulObjectId) + " AND (1 << type) & " + stringify(ulStoreTypeMask) + " LIMIT 1";
	if ((er = lpDatabase->DoSelect(strQuery, &lpDBResult)) != erSuccess) {
		ec_log_err("resolveUserStore(): select failed %x", er);
		return KCERR_DATABASE_ERROR;
	}
	lpDBRow = lpDBResult.fetch_row();
	lpDBLen = lpDBResult.fetch_row_lengths();
    if (lpDBRow == nullptr)
		return KCERR_NOT_FOUND;
    if (lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBLen == NULL || lpDBLen[1] == 0) {
		ec_log_err("resolveUserStore(): row/col null");
		return KCERR_DATABASE_ERROR;
    }

    /* We found the store, so we don't need to check if this is the correct server. */
    strServerName = cfg->GetSetting("server_name", "", "Unknown");
    // Always return the pseudo URL.
    lpsResponse->lpszServerPath = STROUT_FIX_CPY(std::string("pseudo://" + strServerName).c_str());

    er = g_lpSessionManager->GetCacheManager()->GetEntryIdFromObject(atoui(lpDBRow[0]), soap, ulFlags & OPENSTORE_OVERRIDE_HOME_MDB, &lpsResponse->sStoreId);

	if(er != erSuccess)
		return er;
	er = GetABEntryID(ulObjectId, soap, &lpsResponse->sUserId);
	if (er != erSuccess)
		return er;
	lpsResponse->ulUserId = ulObjectId;
	lpsResponse->guid.__size = lpDBLen[1];
	lpsResponse->guid.__ptr = s_alloc<unsigned char>(soap, lpDBLen[1]);
	memcpy(lpsResponse->guid.__ptr, lpDBRow[1], lpDBLen[1]);
	return erSuccess;
}
SOAP_ENTRY_END()

struct COPYITEM {
	unsigned int ulId;
	unsigned int ulType;
	unsigned int ulParent;
	unsigned int ulNewId;
	unsigned int ulFlags;
	unsigned int ulMessageFlags;
	unsigned int ulOwner;
	SOURCEKEY 	 sSourceKey;
	SOURCEKEY	 sParentSourceKey;
	SOURCEKEY	 sNewSourceKey;
	EntryId	 	 sOldEntryId;
	EntryId	 	 sNewEntryId;
	bool		 bMoved;
};

// Move one or more messages and/or moved a softdeleted message to a normal message
static ECRESULT MoveObjects(ECSession *lpSession, ECDatabase *lpDatabase,
    ECListInt* lplObjectIds, unsigned int ulDestFolderId,
    unsigned int ulSyncId)
{
	ECRESULT		er = erSuccess;
	bool			bPartialCompletion = false;
	unsigned int	ulGrandParent = 0;
	COPYITEM		sItem;
	unsigned int	ulItemSize = 0;
	unsigned int	ulSourceStoreId = 0;
	unsigned int	ulDestStoreId = 0;
	long long		llStoreSize;
	eQuotaStatus	QuotaStatus;
	bool			bUpdateDeletedSize = false;
	ECListIntIterator	iObjectId;
	size_t			cCopyItems = 0;
	FILETIME ft;

	unsigned long long ullIMAP = 0;

	std::list<unsigned int> lstParent;
	std::list<unsigned int> lstGrandParent;
	std::list<COPYITEM> lstCopyItems;
	SOURCEKEY	sDestFolderSourceKey;

    std::map<unsigned int, PARENTINFO> mapFolderCounts;

	entryId*	lpsNewEntryId = NULL;
	entryId*	lpsOldEntryId = NULL;
	GUID		guidStore;
	if(lplObjectIds == NULL) {
		ec_log_err("MoveObjects: no list of objects given");
		return KCERR_INVALID_PARAMETER;
	}

	auto cache = lpSession->GetSessionManager()->GetCacheManager();
	auto gcache = g_lpSessionManager->GetCacheManager();
	auto sec = lpSession->GetSecurity();
	ALLOC_DBRESULT();
	if(lplObjectIds->empty())
		goto exit; // Nothing to do

	GetSystemTimeAsFileTime(&ft);

	// Check permission, Destination folder
	er = sec->CheckPermission(ulDestFolderId, ecSecurityCreate);
	if (er != erSuccess) {
		ec_log_err("MoveObjects: failed checking permissions on %u: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
		goto exit;
	}
	er = cache->GetStore(ulDestFolderId, &ulDestStoreId, &guidStore);
	if (er != erSuccess) {
		ec_log_err("MoveObjects: failed retrieving store of %u: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
		goto exit;
	}

	GetSourceKey(ulDestFolderId, &sDestFolderSourceKey);

	// Get all items for the object list
	strQuery = "SELECT h.id, h.parent, h.type, h.flags, h.owner, p.val_ulong, p2.val_ulong FROM hierarchy AS h LEFT JOIN properties AS p ON p.hierarchyid=h.id AND p.tag="+stringify(PROP_ID(PR_MESSAGE_SIZE))+" AND p.type="+stringify(PROP_TYPE(PR_MESSAGE_SIZE)) + 
			   " LEFT JOIN properties AS p2 ON p2.hierarchyid=h.id AND p2.tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND p2.type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) + " WHERE h.id IN(";

	for (iObjectId = lplObjectIds->begin();
	     iObjectId != lplObjectIds->end(); ++iObjectId) {
		if(iObjectId != lplObjectIds->begin())
			strQuery += ",";

		strQuery += stringify(*iObjectId);
	}
	strQuery += ")";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("MoveObjects: failed retrieving list objects from database: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// First, put all the root objects in the list
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if(lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL || lpDBRow[4] == NULL) // no id, type or parent folder?
			continue;

        sItem.bMoved 	= false;
		sItem.ulId		= atoi(lpDBRow[0]);
		sItem.ulParent	= atoi(lpDBRow[1]);
		sItem.ulType	= atoi(lpDBRow[2]);
		sItem.ulFlags	= atoi(lpDBRow[3]);
		sItem.ulOwner	= atoi(lpDBRow[4]);
		sItem.ulMessageFlags = lpDBRow[6] ? atoi(lpDBRow[6]) : 0;

		if (sItem.ulType != MAPI_MESSAGE) {
			bPartialCompletion = true;
			continue;
		}
		
		GetSourceKey(sItem.ulId, &sItem.sSourceKey);
		GetSourceKey(sItem.ulParent, &sItem.sParentSourceKey);

		// Check permission, source messages
		er = sec->CheckPermission(sItem.ulId, ecSecurityDelete);
		if (er != erSuccess) {
			bPartialCompletion = true;
			er = erSuccess;
			continue;
		}

		// Check if the source and dest the same store
		er = cache->GetStore(sItem.ulId, &ulSourceStoreId, nullptr);
		if (er != erSuccess || ulSourceStoreId != ulDestStoreId) {
			bPartialCompletion = true;
			er = erSuccess;
			continue;
		}
		lstCopyItems.emplace_back(sItem);
		ulItemSize += (lpDBRow[5] != NULL)? atoi(lpDBRow[5]) : 0;
		// check if it a deleted item
		if (lpDBRow[3] != NULL && atoi(lpDBRow[3]) & MSGFLAG_DELETED)
			bUpdateDeletedSize = true;
	}
	
	// Check the quota size when the item is a softdelete item
	if(bUpdateDeletedSize == true)
	{
		// Quota check
		er = sec->GetStoreSize(ulDestFolderId, &llStoreSize);
		if (er != erSuccess) {
			ec_log_err("MoveObjects: GetStoreSize(%u) failed: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
			goto exit;
		}

		// substract itemsize and check
		llStoreSize -= (llStoreSize >= (long long)ulItemSize)?(long long)ulItemSize:0;
		er = sec->CheckQuota(ulDestFolderId, llStoreSize, &QuotaStatus);
		if (er != erSuccess) {
			ec_log_err("MoveObjects: CheckQuota(%u) failed: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
			goto exit;
		}

		if(QuotaStatus == QUOTA_HARDLIMIT) {
			er = KCERR_STORE_FULL;
			goto exit;
		}

	}

	cCopyItems = lstCopyItems.size();

	// Move the messages to another folder
	for (auto &cop : lstCopyItems) {
		sObjectTableKey key(cop.ulId, 0);
		struct propVal sPropIMAPId;

		// Check whether it is a move to the same parent, and if so, skip them.
		if (cop.ulParent == ulDestFolderId &&
		    (cop.ulFlags & MSGFLAG_DELETED) == 0)
			continue;

		er = gcache->GetEntryIdFromObject(cop.ulId, nullptr, 0, &lpsOldEntryId);
		if(er != erSuccess) {
			// FIXME isn't this an error?
			ec_log_err("MoveObjects: problem retrieving entry id of object %u: %s (%x)",
				cop.ulId, GetMAPIErrorMessage(er), er);
			bPartialCompletion = true;
			er = erSuccess;
			// FIXME: Delete from list: cop
			continue;
		}
		cop.sOldEntryId = EntryId(lpsOldEntryId);
		FreeEntryId(lpsOldEntryId, true);
		lpsOldEntryId = NULL;

		er = CreateEntryId(guidStore, MAPI_MESSAGE, &lpsNewEntryId);
		if (er != erSuccess) {
			ec_log_err("MoveObjects: CreateEntryID for type MAPI_MESSAGE failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}
		cop.sNewEntryId = EntryId(lpsNewEntryId);
		FreeEntryId(lpsNewEntryId, true);
		lpsNewEntryId = NULL;

		// Update entryid (changes on move)
		strQuery = "REPLACE INTO indexedproperties(hierarchyid,tag,val_binary) VALUES (" +
			stringify(cop.ulId) + ", 4095, " +
			lpDatabase->EscapeBinary(cop.sNewEntryId, cop.sNewEntryId.size()) + ")";
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess) {
			ec_log_err("MoveObjects: problem setting new entry id: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		er = lpSession->GetNewSourceKey(&cop.sNewSourceKey);
		if (er != erSuccess) {
			ec_log_err("MoveObjects: GetNewSourceKey failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}
			
		// Update source key (changes on move)
		strQuery = "REPLACE INTO indexedproperties(hierarchyid,tag,val_binary) VALUES (" +
			stringify(cop.ulId) + "," +
			stringify(PROP_ID(PR_SOURCE_KEY)) + "," +
			lpDatabase->EscapeBinary(cop.sNewSourceKey, cop.sNewSourceKey.size()) + ")";
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess) {
			ec_log_err("MoveObjects: Update source key for %u failed: %s (%x)",
				cop.ulId, GetMAPIErrorMessage(er), er);
			goto exit;
		}

		// Update IMAP ID (changes on move)
		er = g_lpSessionManager->GetNewSequence(ECSessionManager::SEQ_IMAP, &ullIMAP);
		if (er != erSuccess) {
			ec_log_err("MoveObjects: problem retrieving new IMAP ID: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

        strQuery = "INSERT INTO properties(hierarchyid, tag, type, val_ulong) VALUES(" +
                    stringify(cop.ulId) + "," +
                    stringify(PROP_ID(PR_EC_IMAP_ID)) + "," +
                    stringify(PROP_TYPE(PR_EC_IMAP_ID)) + "," +
                    stringify(ullIMAP) +
                    ") ON DUPLICATE KEY UPDATE val_ulong=" +
                    stringify(ullIMAP);

		er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess) {
			ec_log_err("MoveObjects: problem updating new IMAP ID for %u to %llu: %s (%x)",
				cop.ulId, ullIMAP, GetMAPIErrorMessage(er), er);
			goto exit;
		}

		sPropIMAPId.ulPropTag = PR_EC_IMAP_ID;
		sPropIMAPId.Value.ul = ullIMAP;
		sPropIMAPId.__union = SOAP_UNION_propValData_ul;
		er = gcache->SetCell(&key, PR_EC_IMAP_ID, &sPropIMAPId);
		if (er != erSuccess) {
			ec_log_err("MoveObjects: problem cache sell for IMAP ID %llu: %s (%x)", ullIMAP, GetMAPIErrorMessage(er), er);
			goto exit;
		}

		strQuery = "UPDATE hierarchy SET parent=" +
			stringify(ulDestFolderId) + ", flags=flags&" +
			stringify(~MSGFLAG_DELETED) + " WHERE id=" +
			stringify(cop.ulId);
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess) {
			// FIXME isn't this an error?
			ec_log_debug("MoveObjects: problem updating hierarchy id for %u in %u: %s (%x)",
				cop.ulId, ulDestFolderId,
				GetMAPIErrorMessage(er), er);
			bPartialCompletion = true;
			er = erSuccess;
			// FIXME: Delete from list: cop
			continue;
		}

		// update last modification time
		// PR_LAST_MODIFICATION_TIME (ZCP-11897)
		strQuery = "INSERT INTO properties(hierarchyid, tag, type, val_lo, val_hi) VALUES(" +
			stringify(cop.ulId) + "," +
			stringify(PROP_ID(PR_LAST_MODIFICATION_TIME)) + "," +
			stringify(PROP_TYPE(PR_LAST_MODIFICATION_TIME)) + "," +
			stringify(ft.dwLowDateTime) + "," +
			stringify(ft.dwHighDateTime) +
			") ON DUPLICATE KEY UPDATE val_lo=" +
			stringify(ft.dwLowDateTime)  + ", val_hi=" +
			stringify(ft.dwHighDateTime);
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			goto exit;

		gcache->Update(fnevObjectModified, cop.ulId);

		// remove PR_DELETED_ON, This is on a softdeleted message
		strQuery = "DELETE FROM properties WHERE hierarchyid=" +
			stringify(cop.ulId) + " AND tag=" +
			stringify(PROP_ID(PR_DELETED_ON)) + " AND type=" +
			stringify(PROP_TYPE(PR_DELETED_ON));
		er = lpDatabase->DoDelete(strQuery);
		if(er != erSuccess) {
			ec_log_debug("MoveObjects: problem removing PR_DELETED_ON for %u: %s (%x)",
				cop.ulId, GetMAPIErrorMessage(er), er);
			bPartialCompletion = true;
			er = erSuccess; //ignore error // FIXME WHY?!
		}

		// a move is a delete in the originating folder and a new in the destination folder except for softdelete that is a change
		if (cop.ulParent != ulDestFolderId) {
			AddChange(lpSession, ulSyncId, cop.sSourceKey, cop.sParentSourceKey, ICS_MESSAGE_HARD_DELETE);
			AddChange(lpSession, ulSyncId, cop.sNewSourceKey, sDestFolderSourceKey, ICS_MESSAGE_NEW);
		} else if (cop.ulFlags & MSGFLAG_DELETED) {
			// Restore a softdeleted message
			AddChange(lpSession, ulSyncId, cop.sNewSourceKey, sDestFolderSourceKey, ICS_MESSAGE_NEW);
		}

		er = ECTPropsPurge::AddDeferredUpdate(lpSession, lpDatabase,
		     ulDestFolderId, cop.ulParent, cop.ulId);
		if (er != erSuccess) {
			ec_log_debug("MoveObjects: ECTPropsPurge::AddDeferredUpdate failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		// Track folder count changes
		if (cop.ulType == MAPI_MESSAGE) {
			if (cop.ulFlags & MSGFLAG_DELETED) {
				// Undelete
				if (cop.ulFlags & MAPI_ASSOCIATED) {
					// Associated message undeleted
					--mapFolderCounts[cop.ulParent].lDeletedAssoc;
					++mapFolderCounts[ulDestFolderId].lAssoc;
				} else {
					// Message undeleted
					--mapFolderCounts[cop.ulParent].lDeleted;
					++mapFolderCounts[ulDestFolderId].lItems;
					if ((cop.ulMessageFlags & MSGFLAG_READ) == 0)
						// Undeleted message was unread
						++mapFolderCounts[ulDestFolderId].lUnread;
				}
			} else {
				// Move
				--mapFolderCounts[cop.ulParent].lItems;
				++mapFolderCounts[ulDestFolderId].lItems;
				if ((cop.ulMessageFlags & MSGFLAG_READ) == 0) {
					--mapFolderCounts[cop.ulParent].lUnread;
					++mapFolderCounts[ulDestFolderId].lUnread;
				}
			}
		} 
		
		cop.bMoved = true;
	}
    
	er = ApplyFolderCounts(lpDatabase, mapFolderCounts);
	if (er != erSuccess) {
		ec_log_debug("MoveObjects: ApplyFolderCounts failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// change the size if it is a soft delete item
	if(bUpdateDeletedSize == true) {
		er = UpdateObjectSize(lpDatabase, ulDestStoreId, MAPI_STORE, UPDATE_ADD, ulItemSize);
		if (er != erSuccess) {
			ec_log_debug("MoveObjects: UpdateObjectSize(store %u) failed: %s (%x)", ulDestStoreId, GetMAPIErrorMessage(er), er);
			goto exit;
		}
	}

	for (const auto &cop : lstCopyItems) {
		if (!cop.bMoved)
			continue;
		// Cache update for object
		gcache->SetObject(cop.ulId, ulDestFolderId, cop.ulOwner,
			cop.ulFlags & ~MSGFLAG_DELETED /* possible undelete */,
			cop.ulType);
		// Remove old sourcekey and entryid and add them
		gcache->RemoveIndexData(cop.ulId);
		gcache->SetObjectProp(PROP_ID(PR_SOURCE_KEY),
			cop.sNewSourceKey.size(), cop.sNewSourceKey, cop.ulId);
		gcache->SetObjectProp(PROP_ID(PR_ENTRYID),
			cop.sNewEntryId.size(), cop.sNewEntryId, cop.ulId);
	}
    
	er = lpDatabase->Commit();
	if (er != erSuccess) {
		ec_log_debug("MoveObjects: database commit failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	for (auto &cop : lstCopyItems) {
		if (!cop.bMoved)
			continue;
		// update destenation folder after PR_ENTRYID update
		if (cCopyItems < EC_TABLE_CHANGE_THRESHOLD) {
			// Update messages
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_DELETE,
				0, cop.ulParent, cop.ulId, cop.ulType);
			// Update destenation folder
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD,
				0, ulDestFolderId, cop.ulId, cop.ulType);
		}

		// Update Store object
		g_lpSessionManager->NotificationMoved(cop.ulType, cop.ulId,
			ulDestFolderId, cop.ulParent, cop.sOldEntryId);
		lstParent.emplace_back(cop.ulParent);
	}

	lstParent.sort();
	lstParent.unique();

	//Update message folders
	for (auto pa_id : lstParent) {
		if (cCopyItems >= EC_TABLE_CHANGE_THRESHOLD) 
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE,
				0, pa_id, 0, MAPI_MESSAGE);

		// update the source parent folder for disconnected clients
		WriteLocalCommitTimeMax(NULL, lpDatabase, pa_id, NULL);
		// ignore error, no need to set partial even.

		// Get the grandparent
		gcache->GetParent(pa_id, &ulGrandParent);
		gcache->Update(fnevObjectModified, pa_id);
		g_lpSessionManager->NotificationModified(MAPI_FOLDER, pa_id);
		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY,
			0, ulGrandParent, pa_id, MAPI_FOLDER);
	}

	// update the destination folder for disconnected clients
	WriteLocalCommitTimeMax(NULL, lpDatabase, ulDestFolderId, NULL);
	// ignore error, no need to set partial even.

    if(cCopyItems >= EC_TABLE_CHANGE_THRESHOLD) 
		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE, 0, ulDestFolderId, 0, MAPI_MESSAGE);

	//Update destination folder
	gcache->Update(fnevObjectModified, ulDestFolderId);
	g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulDestFolderId);

	// Update the grandfolder of dest. folder
	gcache->GetParent(ulDestFolderId, &ulGrandParent);
	g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulGrandParent, ulDestFolderId, MAPI_FOLDER);

	if(bPartialCompletion && er == erSuccess)
		er = KCWARN_PARTIAL_COMPLETION;
exit:
	if(lpDatabase && er != erSuccess && er != KCWARN_PARTIAL_COMPLETION)
		lpDatabase->Rollback();
	if(lpsNewEntryId)
		FreeEntryId(lpsNewEntryId, true);

	if(lpsOldEntryId)
		FreeEntryId(lpsOldEntryId, true);

	return er;
}

/**
 * Copy one message with his parent data like attachments and recipient
 *
 * @param[in] lpecSession Pointer to a session object; cannot be NULL.
 * @param[in] lpAttachmentStorage Pointer to an attachment storage object. If NULL is passed in lpAttachmentStorage, 
 * 									a default storage object with transaction enabled will be used.
 * @param[in] ulObjId Source object that identify the message, recipient or attachment to copy.
 * @param[in] ulDestFolderId Destenation object to received the copied message, recipient or attachment.
 * @param[in] bIsRoot Identify the root object; For callers this should be true;
 * @param[in] bDoNotification true if you want to send object notifications.
 * @param[in] bDoTableNotification true if you want to send table notifications.
 * @param[in] ulSyncId Client sync identify.
 *
 * @FIXME It is possible to send notifications before a commit, this can give issues with the cache! 
 * 			This function should be refactored
 */
static ECRESULT CopyObject(ECSession *lpecSession,
    ECAttachmentStorage *lpAttachmentStorage, unsigned int ulObjId,
    unsigned int ulDestFolderId, bool bIsRoot, bool bDoNotification,
    bool bDoTableNotification, unsigned int ulSyncId)
{
	ECRESULT		er = erSuccess;
	ECDatabase		*lpDatabase = NULL;
	std::string		strQuery;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow;
	unsigned int	ulNewObjectId = 0;
	std::string		strExclude;
	long long		llStoreSize;
	unsigned int	ulStoreId = 0;
	unsigned int	ulSize;
	unsigned int	ulObjType;
	unsigned int	ulParent = 0;
	unsigned int	ulFlags = 0;
	GUID			guidStore;
	eQuotaStatus QuotaStatus;
	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey;
	entryId*		lpsNewEntryId = NULL;
	unsigned long long ullIMAP = 0;
	object_ptr<ECAttachmentStorage> lpInternalAttachmentStorage;

	er = lpecSession->GetDatabase(&lpDatabase);
	if (er != erSuccess) {
		ec_log_err("CopyObject: cannot retrieve database: %s (%x)", GetMAPIErrorMessage(er), er);
		return er;
	}

	auto cache = lpecSession->GetSessionManager()->GetCacheManager();
	kd_trans atx;
	if (!lpAttachmentStorage) {
		if (!bIsRoot) {
			ec_log_err("CopyObject: \"!attachmentstore && !isroot\" clause failed: %s (%x)", GetMAPIErrorMessage(er), er);
			return KCERR_INVALID_PARAMETER;
		}
		er = CreateAttachmentStorage(lpDatabase, &~lpInternalAttachmentStorage);
		if (er != erSuccess) {
			ec_log_err("CopyObject: CreateAttachmentStorage failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		lpAttachmentStorage = lpInternalAttachmentStorage;
		// Hack, when lpInternalAttachmentStorage exist your are in a transaction!
	}

	er = cache->GetStore(ulDestFolderId, &ulStoreId, &guidStore);
	if (er != erSuccess) {
		ec_log_err("CopyObject: GetStore(destination folder %u) failed: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// Check permission
	if(bIsRoot == true)
	{
		auto sec = lpecSession->GetSecurity();
		er = sec->CheckPermission(ulObjId, ecSecurityRead);
		if (er != erSuccess) {
			ec_log_err("CopyObject: check permissions of %u failed: %s (%x)", ulObjId, GetMAPIErrorMessage(er), er);
			goto exit;
		}

		// Quota check
		er = sec->GetStoreSize(ulDestFolderId, &llStoreSize);
		if (er != erSuccess) {
			ec_log_err("CopyObject: store size of dest folder %u failed: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
			goto exit;
		}
		er = sec->CheckQuota(ulDestFolderId, llStoreSize, &QuotaStatus);
		if (er != erSuccess) {
			ec_log_err("CopyObject: check quota of dest folder %u failed: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
			goto exit;
		}

		if(QuotaStatus == QUOTA_HARDLIMIT) {
			er = KCERR_STORE_FULL;
			goto exit;
		}

		// Start transaction
		if (lpInternalAttachmentStorage) {
			atx = lpInternalAttachmentStorage->Begin(er);
			if (er != erSuccess) {
				ec_log_err("CopyObject: starting transaction in attachment storage failed: %s (%x)", GetMAPIErrorMessage(er), er);
				goto exit;
			}

			er = lpDatabase->Begin();
			if (er != erSuccess) {
				ec_log_err("CopyObject: starting transaction in database failed: %s (%x)", GetMAPIErrorMessage(er), er);
				goto exit;
			}
		}
	}

	// Get the hierarchy messageroot but not the deleted items
	strQuery = "SELECT h.parent, h.type, p.val_ulong FROM hierarchy AS h LEFT JOIN properties AS p ON h.id = p.hierarchyid AND p.tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND p.type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) + " WHERE h.flags & " + stringify(MSGFLAG_DELETED) + " = 0 AND id="+stringify(ulObjId) + " LIMIT 1";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("CopyObject: failed retrieving hierarchy message root: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}
	if (lpDBResult.get_num_rows() < 1) {
		er = KCERR_NOT_FOUND;// FIXME: right error?
		goto exit;
	}
	lpDBRow = lpDBResult.fetch_row();
	if( lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	ulObjType		= atoui(lpDBRow[1]);
	ulParent		= atoui(lpDBRow[0]);
	if (lpDBRow[2])
		ulFlags		= atoui(lpDBRow[2]);

	if (bIsRoot == true && ulObjType != MAPI_MESSAGE) {
		ec_log_err("CopyObject: \"isRoot && != MAPI_MESSAGE\" fail");
		er = KCERR_INVALID_ENTRYID;
		goto exit;
	}

	//FIXME: Why do we always use the mod and create time of the old object? Create time can always be NOW
	//Create new message (Only valid flag in hierarchy is MSGFLAG_ASSOCIATED)
	strQuery = "INSERT INTO hierarchy(parent, type, flags, owner) VALUES(" +
		stringify(ulDestFolderId) + ", " +
		std::string(lpDBRow[1]) + ", " +
		stringify(ulFlags) + "&" + stringify(MSGFLAG_ASSOCIATED) + "," +
		stringify(lpecSession->GetSecurity()->GetUserId()) + ") ";
	er = lpDatabase->DoInsert(strQuery, &ulNewObjectId);
	if (er != erSuccess) {
		ec_log_err("CopyObject: failed inserting entry in hierarchy table: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	if(bIsRoot == true) {
		sObjectTableKey key(ulNewObjectId, 0);
		propVal sProp;

		// Create message entry
		er = CreateEntryId(guidStore, MAPI_MESSAGE, &lpsNewEntryId);
		if (er != erSuccess) {
			ec_log_err("CopyObject: CreateEntryId failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		//0x0FFF = PR_ENTRYID
		strQuery = "INSERT INTO indexedproperties (hierarchyid,tag,val_binary) VALUES(" + stringify(ulNewObjectId) + ", 4095, " + lpDatabase->EscapeBinary(lpsNewEntryId->__ptr, lpsNewEntryId->__size) + ")";
		er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess) {
			ec_log_err("CopyObject: PR_ENTRYID property insert failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		// Add a PR_EC_IMAP_ID
		er = g_lpSessionManager->GetNewSequence(ECSessionManager::SEQ_IMAP, &ullIMAP);
		if (er != erSuccess) {
			ec_log_err("CopyObject: retrieving new seqnr for PR_EC_IMAP_ID failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		strQuery = "INSERT INTO properties(hierarchyid, tag, type, val_ulong) VALUES(" +
					stringify(ulNewObjectId) + "," +
					stringify(PROP_ID(PR_EC_IMAP_ID)) + "," +
					stringify(PROP_TYPE(PR_EC_IMAP_ID)) + "," +
					stringify(ullIMAP) +
					")";

		er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess) {
			ec_log_err("CopyObject: PR_EC_IMAP_ID property insert failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		sProp.ulPropTag = PR_EC_IMAP_ID;
		sProp.Value.ul = ullIMAP;
		sProp.__union = SOAP_UNION_propValData_ul;
		er = g_lpSessionManager->GetCacheManager()->SetCell(&key, PR_EC_IMAP_ID, &sProp);
		if (er != erSuccess) {
			ec_log_err("CopyObject: updating PR_EC_IMAP_ID sell in cache failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}
	}

	if (lpsNewEntryId != NULL) {
		FreeEntryId(lpsNewEntryId, true);
		lpsNewEntryId = NULL;
	}

	// Get child items of the message like , attachment, recipient...
	strQuery = "SELECT id FROM hierarchy WHERE parent="+stringify(ulObjId);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("CopyObject: failed retrieving child items of message: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	if (lpDBResult.get_num_rows() > 0) {
		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			if(lpDBRow[0] == NULL)
				continue; // FIXME: Skip, give an error/warning ?

			er = CopyObject(lpecSession, lpAttachmentStorage, atoui(lpDBRow[0]), ulNewObjectId, false, false, false, ulSyncId);
			if (er != erSuccess && er != KCERR_NOT_FOUND) {
				ec_log_err("CopyObject: CopyObject(%s) failed: %s (%x)", lpDBRow[0], GetMAPIErrorMessage(er), er);
				goto exit;
			} else {
				er = erSuccess;
			}
		}
	}

	// Exclude properties
	// PR_DELETED_ON
	strExclude = " AND NOT (tag="+stringify(PROP_ID(PR_DELETED_ON))+" AND type="+stringify(PROP_TYPE(PR_DELETED_ON))+")";

	//Exclude PR_SOURCE_KEY, PR_CHANGE_KEY, PR_PREDECESSOR_CHANGE_LIST
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_SOURCE_KEY))+" AND type="+stringify(PROP_TYPE(PR_SOURCE_KEY))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_CHANGE_KEY))+" AND type="+stringify(PROP_TYPE(PR_CHANGE_KEY))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_PREDECESSOR_CHANGE_LIST))+" AND type="+stringify(PROP_TYPE(PR_PREDECESSOR_CHANGE_LIST))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_EC_IMAP_ID))+" AND type="+stringify(PROP_TYPE(PR_EC_IMAP_ID))+")";
	// because of #7699, messages contain PR_LOCAL_COMMIT_TIME_MAX
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_LOCAL_COMMIT_TIME_MAX))+" AND type="+stringify(PROP_TYPE(PR_LOCAL_COMMIT_TIME_MAX))+")";

	// Copy properties...
	strQuery = "INSERT INTO properties (hierarchyid, tag, type, val_ulong, val_string, val_binary,val_double,val_longint,val_hi,val_lo) SELECT "+stringify(ulNewObjectId)+", tag,type,val_ulong,val_string,val_binary,val_double,val_longint,val_hi,val_lo FROM properties WHERE hierarchyid ="+stringify(ulObjId)+strExclude;
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) {
		ec_log_err("CopyObject: copy properties failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}
		
	// Copy MVproperties...
	strQuery = "INSERT INTO mvproperties (hierarchyid, orderid, tag, type, val_ulong, val_string, val_binary,val_double,val_longint,val_hi,val_lo) SELECT "+stringify(ulNewObjectId)+", orderid, tag,type,val_ulong,val_string,val_binary,val_double,val_longint,val_hi,val_lo FROM mvproperties WHERE hierarchyid ="+stringify(ulObjId);
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) {
		ec_log_err("CopyObject: copy MVproperties failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// Copy large objects... if present
	er = lpAttachmentStorage->CopyAttachment(ulObjId, ulNewObjectId);
	if (er != erSuccess && er != KCERR_NOT_FOUND) {
		ec_log_err("CopyObject: CopyAttachment(%u -> %u) failed: %s (%x)", ulObjId, ulNewObjectId, GetMAPIErrorMessage(er), er);
		goto exit;
	}
	er = erSuccess;

	if(bIsRoot == true)
	{
		// Create indexedproperties, Add new PR_SOURCE_KEY
		er = lpecSession->GetNewSourceKey(&sSourceKey);
		if (er != erSuccess) {
			ec_log_err("CopyObject: GetNewSourceKey failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		strQuery = "INSERT INTO indexedproperties(hierarchyid,tag,val_binary) VALUES(" + stringify(ulNewObjectId) + "," + stringify(PROP_ID(PR_SOURCE_KEY)) + "," + lpDatabase->EscapeBinary(sSourceKey, sSourceKey.size()) + ")";
		er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess) {
			ec_log_err("CopyObject: insert %u in indexedproperties failed: %s (%x)", ulNewObjectId, GetMAPIErrorMessage(er), er);
			goto exit;
		}

		// Track folder count changes
		// Can we copy deleted items?
		if(ulFlags & MAPI_ASSOCIATED) {
			// Associated message undeleted
			er = UpdateFolderCount(lpDatabase, ulDestFolderId, PR_ASSOC_CONTENT_COUNT, 1);
		} else {
			// Message undeleted
			er = UpdateFolderCount(lpDatabase, ulDestFolderId, PR_CONTENT_COUNT, 1);
			if (er == erSuccess && (ulFlags & MSGFLAG_READ) == 0)
				// Undeleted message was unread
				er = UpdateFolderCount(lpDatabase, ulDestFolderId, PR_CONTENT_UNREAD, 1);
		}
		if (er != erSuccess) {
			ec_log_err("CopyObject: UpdateFolderCount (%u) failed: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
			goto exit;
		}

		// Update ICS system
		GetSourceKey(ulDestFolderId, &sParentSourceKey);
		AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, ICS_MESSAGE_NEW);

		// Hack, when lpInternalAttachmentStorage exist your are in a transaction!
		if (lpInternalAttachmentStorage) {
			// Deferred tproperties
			er = ECTPropsPurge::AddDeferredUpdate(lpecSession, lpDatabase, ulDestFolderId, 0, ulNewObjectId);
			if (er != erSuccess) {
				ec_log_err("CopyObject: ECTPropsPurge::AddDeferredUpdate(%u): %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
				goto exit;
			}
			er = atx.commit();
			if (er != erSuccess) {
				ec_log_err("CopyObject: attachmentstorage commit failed: %s (%x)", GetMAPIErrorMessage(er), er);
				goto exit;
			}

			er = lpDatabase->Commit();
			if (er != erSuccess) {
				ec_log_err("CopyObject: database commit failed: %s (%x)", GetMAPIErrorMessage(er), er);
				goto exit;
			}
		} else {
			// Deferred tproperties, let the caller handle the purge so we won't purge every 20 messages on a copy
			// of a complete folder.
			er = ECTPropsPurge::AddDeferredUpdateNoPurge(lpDatabase, ulDestFolderId, 0, ulNewObjectId);
			if(er != erSuccess) {
				ec_log_err("CopyObject: ECTPropsPurge::AddDeferredUpdateNoPurge(%u, %u) failed: %s (%x)", ulDestFolderId, ulNewObjectId, GetMAPIErrorMessage(er), er);
				goto exit;
			}
		}

		g_lpSessionManager->GetCacheManager()->SetObjectProp(PROP_ID(PR_SOURCE_KEY), sSourceKey.size(), sSourceKey, ulNewObjectId);

		// Update Size
		if (GetObjectSize(lpDatabase, ulNewObjectId, &ulSize) == erSuccess &&
		    cache->GetStore(ulNewObjectId, &ulStoreId, nullptr) == erSuccess) {
			er = UpdateObjectSize(lpDatabase, ulStoreId, MAPI_STORE, UPDATE_ADD, ulSize);
			if (er != erSuccess) {
				ec_log_err("CopyObject: UpdateObjectSize(store %u) failed: %s (%x)", ulStoreId, GetMAPIErrorMessage(er), er);
				goto exit;
			}
		}
	}

	g_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, ulDestFolderId);

	if(bDoNotification){
		// Update destenation folder
		if (bDoTableNotification)
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD, 0, ulDestFolderId, ulNewObjectId, MAPI_MESSAGE);

		g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulDestFolderId);

		// Notify object is copied
		g_lpSessionManager->NotificationCopied(MAPI_MESSAGE, ulNewObjectId, ulDestFolderId, ulObjId, ulParent);
	}

exit:
	if (er != erSuccess)
		lpDatabase->Rollback();
	if(lpsNewEntryId)
		FreeEntryId(lpsNewEntryId, true);

	return er;
}

/**
 * Copy folder and his childs
 *
 * @note please check the object type before you call this function, the type should be MAPI_FOLDER
 */
static ECRESULT CopyFolderObjects(struct soap *soap, ECSession *lpecSession,
    unsigned int ulFolderFrom, unsigned int ulDestFolderId,
    const char *lpszNewFolderName, bool bCopySubFolder, unsigned int ulSyncId)
{
	ECRESULT		er = erSuccess;
	ECDatabase		*lpDatabase = NULL;
	std::string		strQuery, strSubQuery, strExclude;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow;

	unsigned int	ulNewDestFolderId = 0;
	unsigned int	ulItems = 0;
	unsigned int	ulGrandParent = 0;

	unsigned int	ulDestStoreId = 0;
	unsigned int	ulSourceStoreId = 0;

	bool			bPartialCompletion = false;
	long long		llStoreSize = 0;
	eQuotaStatus QuotaStatus;

	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey;
	object_ptr<ECAttachmentStorage> lpAttachmentStorage;

	if(lpszNewFolderName == NULL) {
		ec_log_err("CopyFolderObjects: \"new folder name\" missing");
		return KCERR_INVALID_PARAMETER;
	}

	er = lpecSession->GetDatabase(&lpDatabase);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: cannot retrieve database: %s (%x)", GetMAPIErrorMessage(er), er);
		return er;
	}
	auto gcache = g_lpSessionManager->GetCacheManager();
	auto cache = lpecSession->GetSessionManager()->GetCacheManager();
	auto sec = lpecSession->GetSecurity();
	er = CreateAttachmentStorage(lpDatabase, &~lpAttachmentStorage);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: CreateAttachmentStorage failed: %s (%x)", GetMAPIErrorMessage(er), er);
		return er;
	}
	er = cache->GetStore(ulDestFolderId, &ulDestStoreId, nullptr);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: GetStore for %u (from cache) failed: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
		return er;
	}
	er = cache->GetStore(ulFolderFrom, &ulSourceStoreId, nullptr);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: GetStore for %u (from cache) failed: %s (%x)", ulFolderFrom, GetMAPIErrorMessage(er), er);
		return er;
	}
	auto atx = lpAttachmentStorage->Begin(er);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: Begin() on attachment storage failed: %s (%x)", GetMAPIErrorMessage(er), er);
		return er;
	}

	er = lpDatabase->Begin();
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: Begin() on database failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// Quota check
	er = sec->GetStoreSize(ulDestFolderId, &llStoreSize);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: GetStoreSize failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}
	er = sec->CheckQuota(ulDestFolderId, llStoreSize, &QuotaStatus);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: CheckQuota failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	if(QuotaStatus == QUOTA_HARDLIMIT) {
		er = KCERR_STORE_FULL;
		goto exit;
	}

	// Create folder (with a sourcekey)
	er = CreateFolder(lpecSession, lpDatabase, ulDestFolderId, NULL, FOLDER_GENERIC, lpszNewFolderName, NULL, false, true, ulSyncId, NULL, &ulNewDestFolderId, NULL);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: CreateFolder \"%s\" in %u failed: %s (%x)", lpszNewFolderName, ulDestFolderId, GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// Always use the string version if you want to exclude properties
	strExclude = " AND NOT (tag="+stringify(PROP_ID(PR_DELETED_ON))+" AND type="+stringify(PROP_TYPE(PR_DELETED_ON))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_DISPLAY_NAME_A))+" AND type="+stringify(PROP_TYPE(PR_DISPLAY_NAME_A))+")";

	//Exclude PR_SOURCE_KEY, PR_CHANGE_KEY, PR_PREDECESSOR_CHANGE_LIST
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_SOURCE_KEY))+" AND type="+stringify(PROP_TYPE(PR_SOURCE_KEY))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_CHANGE_KEY))+" AND type="+stringify(PROP_TYPE(PR_CHANGE_KEY))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_PREDECESSOR_CHANGE_LIST))+" AND type="+stringify(PROP_TYPE(PR_PREDECESSOR_CHANGE_LIST))+")";

	// Exclude the counters
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_CONTENT_COUNT))+" AND type="+stringify(PROP_TYPE(PR_CONTENT_COUNT))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_CONTENT_UNREAD))+" AND type="+stringify(PROP_TYPE(PR_CONTENT_UNREAD))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_FOLDER_CHILD_COUNT))+" AND type="+stringify(PROP_TYPE(PR_FOLDER_CHILD_COUNT))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_ASSOC_CONTENT_COUNT))+" AND type="+stringify(PROP_TYPE(PR_ASSOC_CONTENT_COUNT))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_DELETED_MSG_COUNT))+" AND type="+stringify(PROP_TYPE(PR_DELETED_MSG_COUNT))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_DELETED_FOLDER_COUNT))+" AND type="+stringify(PROP_TYPE(PR_DELETED_FOLDER_COUNT))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_DELETED_ASSOC_MSG_COUNT))+" AND type="+stringify(PROP_TYPE(PR_DELETED_ASSOC_MSG_COUNT))+")";
	strExclude += " AND NOT (tag="+stringify(PROP_ID(PR_SUBFOLDERS))+" AND type="+stringify(PROP_TYPE(PR_SUBFOLDER))+")";

	// Copy properties...
	strQuery = "REPLACE INTO properties (hierarchyid, tag, type, val_ulong, val_string, val_binary,val_double,val_longint,val_hi,val_lo) SELECT "+stringify(ulNewDestFolderId)+", tag,type,val_ulong,val_string,val_binary,val_double,val_longint,val_hi,val_lo FROM properties WHERE hierarchyid ="+stringify(ulFolderFrom)+strExclude;
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: copy properties step failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// Copy MVproperties...
	strQuery = "REPLACE INTO mvproperties (hierarchyid, orderid, tag, type, val_ulong, val_string, val_binary,val_double,val_longint,val_hi,val_lo) SELECT "+stringify(ulNewDestFolderId)+", orderid, tag,type,val_ulong,val_string,val_binary,val_double,val_longint,val_hi,val_lo FROM mvproperties WHERE hierarchyid ="+stringify(ulFolderFrom);
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: copy mvproperties step failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// Copy large objects... if present .. probably not, on a folder
	er = lpAttachmentStorage->CopyAttachment(ulFolderFrom, ulNewDestFolderId);
	if (er != erSuccess && er != KCERR_NOT_FOUND) {
		ec_log_err("CopyFolderObjects: copy attachment step failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}
	er = erSuccess;

	// update ICS sytem with a change
	GetSourceKey(ulDestFolderId, &sParentSourceKey);
	GetSourceKey(ulNewDestFolderId, &sSourceKey);

	AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, ICS_FOLDER_CHANGE);

	//Select all Messages of the home folder
	// Skip deleted and associated items
	strQuery = "SELECT id FROM hierarchy WHERE parent="+stringify(ulFolderFrom)+ " AND type="+stringify(MAPI_MESSAGE)+" AND flags & " + stringify(MSGFLAG_DELETED|MSGFLAG_ASSOCIATED) + " = 0";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: retrieving list of messages from home folder failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	ulItems = lpDBResult.get_num_rows();

	// Walk through the messages list
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if(lpDBRow[0] == NULL)
			continue; //FIXME: error show ???

		er = CopyObject(lpecSession, lpAttachmentStorage, atoui(lpDBRow[0]), ulNewDestFolderId, true, false, false, ulSyncId);
		// FIXME: handle KCERR_STORE_FULL
		if(er == KCERR_NOT_FOUND) {
			bPartialCompletion = true;
		} else if (er != erSuccess) {
			ec_log_err("CopyFolderObjects: CopyObject %s failed failed: %s (%x)", lpDBRow[0], GetMAPIErrorMessage(er), er);
			goto exit;
		}
	}

	// update the destination folder for disconnected clients
	er = WriteLocalCommitTimeMax(NULL, lpDatabase, ulNewDestFolderId, NULL);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: WriteLocalCommitTimeMax failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	er = ECTPropsPurge::AddDeferredUpdate(lpecSession, lpDatabase, ulDestFolderId, 0, ulNewDestFolderId);
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: ECTPropsPurge::AddDeferredUpdate failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	er = lpDatabase->Commit();
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: database commit failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}
	er = atx.commit();
	if (er != erSuccess) {
		ec_log_err("CopyFolderObjects: attachment storage commit failed: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// Notifications
	if(ulItems > 0)
	{
		//Update destenation folder
		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE, 0, ulNewDestFolderId, 0, MAPI_MESSAGE);

		// Update the grandfolder of dest. folder
		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulDestFolderId, ulNewDestFolderId, MAPI_FOLDER);

		//Update destination folder
		gcache->Update(fnevObjectModified, ulNewDestFolderId);
		g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulNewDestFolderId);

	}

	gcache->GetParent(ulFolderFrom ,&ulGrandParent);
	g_lpSessionManager->NotificationCopied(MAPI_FOLDER, ulNewDestFolderId, ulDestFolderId, ulFolderFrom, ulGrandParent);

	if(bCopySubFolder) {
		//Select all folders of the home folder
		// Skip deleted folders
		strQuery = "SELECT hierarchy.id, properties.val_string FROM hierarchy JOIN properties ON hierarchy.id = properties.hierarchyid WHERE hierarchy.parent=" + stringify(ulFolderFrom) +" AND hierarchy.type="+stringify(MAPI_FOLDER)+" AND (flags & " + stringify(MSGFLAG_DELETED) + ") = 0 AND properties.tag=" + stringify(KOPANO_TAG_DISPLAY_NAME) + " AND properties.type="+stringify(PT_STRING8);
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess) {
			ec_log_err("CopyFolderObjects: retrieving list of folders from home folder failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		if (lpDBResult.get_num_rows() > 0) {
			// Walk through the folder list
			while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
				if(lpDBRow[0] == NULL || lpDBRow[1] == NULL)
					continue; // ignore

				// Create SubFolder with messages. This object type checking is done in the where of the query
				er = CopyFolderObjects(soap, lpecSession, atoui(lpDBRow[0]), ulNewDestFolderId, lpDBRow[1], true, ulSyncId);
				if (er == KCWARN_PARTIAL_COMPLETION) {
					bPartialCompletion = true;
				} else if (er != erSuccess) {
					ec_log_err("CopyFolderObjects: CopyFolderObjects %s failed failed: %s (%x)", lpDBRow[0], GetMAPIErrorMessage(er), er);
					goto exit;
				}
			}
		}
	}

	if(bPartialCompletion && er == erSuccess)
		er = KCWARN_PARTIAL_COMPLETION;

exit:
    if(lpDatabase && er != erSuccess && er != KCWARN_PARTIAL_COMPLETION) {
        lpDatabase->Rollback();
		atx.rollback();
	}
	return er;

}

/**
 * Copy one or more messages to a destenation
 */
SOAP_ENTRY_START(copyObjects, *result, struct entryList *aMessages, entryId sDestFolderId, unsigned int ulFlags, unsigned int ulSyncId, unsigned int *result)
{
	bool			bPartialCompletion = false;
	unsigned int	ulGrandParent=0;
	unsigned int	ulDestFolderId = 0;
	ECListInt			lObjectIds;
	ECListIntIterator	iObjectId;
	size_t				cObjectItems = 0;
	std::set<EntryId> setEntryIds;

	USE_DATABASE();

	const EntryId dstEntryId(&sDestFolderId);

	if(aMessages == NULL) {
		ec_log_err("SOAP::copyObjects: list of messages (entryList) missing");
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}
	
	for (unsigned int i = 0; i < aMessages->__size; ++i)
		setEntryIds.emplace(aMessages->__ptr[i]);
	setEntryIds.emplace(sDestFolderId);
	er = BeginLockFolders(lpDatabase, setEntryIds, LOCK_EXCLUSIVE);
	if (er != erSuccess) {
		ec_log_err("SOAP::copyObjects: failed locking folders: %s (%x)", GetMAPIErrorMessage(er), er);
		goto exit;
	}

	er = lpecSession->GetObjectFromEntryId(&sDestFolderId, &ulDestFolderId);
	if (er != erSuccess) {
		std::string dstEntryIdStr = dstEntryId;
		ec_log_err("SOAP::copyObjects: failed obtaining object by entry id (%s): %s (%x)", dstEntryIdStr.c_str(), GetMAPIErrorMessage(er), er);
		goto exit;
	}

	// Check permission, Destination folder
	er = lpecSession->GetSecurity()->CheckPermission(ulDestFolderId, ecSecurityCreate);
	if (er != erSuccess) {
		ec_log_err("SOAP::copyObjects: failed checking permissions for folder id %u: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
		goto exit;
	}

	if(g_lpSessionManager->GetCacheManager()->GetEntryListToObjectList(aMessages, &lObjectIds) != erSuccess)
		bPartialCompletion = true;

	// @note The object type checking wille be done in MoveObjects or CopyObject

	//check copy or a move
	if(ulFlags & FOLDER_MOVE ) { // A move
		er = MoveObjects(lpecSession, lpDatabase, &lObjectIds, ulDestFolderId, ulSyncId);
		if (er != erSuccess) {
			ec_log_err("SOAP::copyObjects: MoveObjects failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}
	}else { // A copy

		cObjectItems = lObjectIds.size();
		for (iObjectId = lObjectIds.begin();
		     iObjectId != lObjectIds.end(); ++iObjectId) {
			er = CopyObject(lpecSession, NULL, *iObjectId, ulDestFolderId, true, true, cObjectItems < EC_TABLE_CHANGE_THRESHOLD, ulSyncId);
			if(er != erSuccess) {
				ec_log_err("SOAP::copyObjects: failed copying object %u: %s (%x)", *iObjectId, GetMAPIErrorMessage(er), er);
				bPartialCompletion = true;
				er = erSuccess;
			}
		}

		// update the destination folder for disconnected clients
		er = WriteLocalCommitTimeMax(NULL, lpDatabase, ulDestFolderId, NULL);
		if (er != erSuccess) {
			ec_log_err("SOAP::copyObjects: WriteLocalCommitTimeMax failed: %s (%x)", GetMAPIErrorMessage(er), er);
			goto exit;
		}

		if(cObjectItems >= EC_TABLE_CHANGE_THRESHOLD)
		    g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE, 0, ulDestFolderId, 0, MAPI_MESSAGE);

		// Update the grandfolder of dest. folder
		auto gcache = g_lpSessionManager->GetCacheManager();
		gcache->GetParent(ulDestFolderId, &ulGrandParent);
		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulGrandParent, ulDestFolderId, MAPI_FOLDER);

		//Update destination folder
		gcache->Update(fnevObjectModified, ulDestFolderId);
		g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulDestFolderId);

	}

	if(bPartialCompletion && er == erSuccess)
		er = KCWARN_PARTIAL_COMPLETION;
exit:
    lpDatabase->Commit();
    
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(copyFolder, *result, entryId sEntryId, entryId sDestFolderId,
    const char *lpszNewFolderName, unsigned int ulFlags, unsigned int ulSyncId,
    unsigned int *result)
{
	unsigned int	ulAffRows = 0;
	unsigned int	ulOldParent = 0;
	unsigned int	ulGrandParent = 0;
	unsigned int	ulParentCycle = 0;
	unsigned int	ulDestStoreId = 0;
	unsigned int	ulSourceStoreId = 0;
	unsigned int	ulObjFlags = 0;
	unsigned int	ulOldGrandParent = 0;
	unsigned int	ulFolderId = 0;
	unsigned int	ulDestFolderId = 0;
	unsigned int	ulSourceType = 0;
	unsigned int	ulDestType = 0;
	long long		llFolderSize = 0;

	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey; // Old parent
	SOURCEKEY		sDestSourceKey; // New parent

	std::string 	strSubQuery;
	USE_DATABASE();

	const EntryId srcEntryId(&sEntryId);
	const EntryId dstEntryId(&sDestFolderId);
	std::string name;

	// NOTE: lpszNewFolderName can be NULL
	if (lpszNewFolderName)
		lpszNewFolderName = STRIN_FIX(lpszNewFolderName);

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulFolderId);
	if (er != erSuccess) {
		const std::string srcEntryIdStr = srcEntryId;
		ec_log_err("SOAP::copyFolder GetObjectFromEntryId failed for %s: %s (%x)", srcEntryIdStr.c_str(), GetMAPIErrorMessage(er), er);
		return er;
	}

	// Get source store
	auto gcache = g_lpSessionManager->GetCacheManager();
	er = gcache->GetStore(ulFolderId, &ulSourceStoreId, nullptr);
	if (er != erSuccess) {
		ec_log_err("SOAP::copyFolder GetStore failed for folder id %ul: %s (%x)", ulFolderId, GetMAPIErrorMessage(er), er);
		return er;
	}

	er = lpecSession->GetObjectFromEntryId(&sDestFolderId, &ulDestFolderId);
	if (er != erSuccess) {
		const std::string dstEntryIdStr = dstEntryId;
		ec_log_err("SOAP::copyFolder GetObjectFromEntryId failed for %s: %s (%x)", dstEntryIdStr.c_str(), GetMAPIErrorMessage(er), er);
		return er;
	}

	// Get dest store
	er = gcache->GetStore(ulDestFolderId, &ulDestStoreId, nullptr);
	if (er != erSuccess) {
		ec_log_err("SOAP::copyFolder GetStore for folder %d failed: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
		return er;
	}

	if(ulDestStoreId != ulSourceStoreId) {
		ec_log_err("SOAP::copyFolder copy from/to different stores (from %u to %u) is not supported", ulSourceStoreId, ulDestStoreId);
		assert(false);
		return KCERR_NO_SUPPORT;
	}

	// Check permission
	auto sec = lpecSession->GetSecurity();
	er = sec->CheckPermission(ulDestFolderId, ecSecurityCreateFolder);
	if (er != erSuccess) {
		ec_log_debug("SOAP::copyFolder copy folder (to %u) is not allowed: %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
		return er;
	}

	if(ulFlags & FOLDER_MOVE ) // is the folder editable?
		er = sec->CheckPermission(ulFolderId, ecSecurityFolderAccess);
	else // is the folder readable
		er = sec->CheckPermission(ulFolderId, ecSecurityRead);

	if (er != erSuccess) {
		ec_log_debug("SOAP::copyFolder folder (%u) is not editable: %s (%x)", ulFolderId, GetMAPIErrorMessage(er), er);
		return er;
	}

	// Check MAPI_E_FOLDER_CYCLE
	if(ulFolderId == ulDestFolderId) {
		ec_log_err("SOAP::copyFolder target folder (%u) cannot be the same as source folder", ulDestFolderId);
		return KCERR_FOLDER_CYCLE;
	}

	// Get the parent id, for notification and copy
	er = gcache->GetObject(ulFolderId, &ulOldParent, nullptr, &ulObjFlags, &ulSourceType);
	if (er != erSuccess) {
		ec_log_err("SOAP::copyFolder cannot get parent folder id for %u: %s (%x)", ulFolderId, GetMAPIErrorMessage(er), er);
		return er;
	}
	er = gcache->GetObject(ulDestFolderId, nullptr, nullptr, nullptr, &ulDestType);
	if (er != erSuccess) {
		ec_log_err("SOAP::copyFolder cannot get type of destination folder (%u): %s (%x)", ulDestFolderId, GetMAPIErrorMessage(er), er);
		return er;
	}

	if (ulSourceType != MAPI_FOLDER || ulDestType != MAPI_FOLDER) {
		const std::string srcEntryIdStr = srcEntryId;
		const std::string dstEntryIdStr = dstEntryId;
		ec_log_err("SOAP::copyFolder source (%u) or destination (%u) is not a folder, invalid entry id (%s / %s)", ulSourceType, ulDestType, srcEntryIdStr.c_str(), dstEntryIdStr.c_str());
		return KCERR_INVALID_ENTRYID;
	}

	// Check folder and dest folder are the same
	if (!(ulObjFlags & MSGFLAG_DELETED) && (ulFlags & FOLDER_MOVE) &&
	    ulDestFolderId == ulOldParent) {
		ec_log_debug("SOAP::copyFolder destination (%u) == source", ulDestFolderId);
		return erSuccess; // Do nothing... folder already on the right place
	}

	ulParentCycle = ulDestFolderId;
	while (gcache->GetParent(ulParentCycle, &ulParentCycle) == erSuccess) {
		if(ulFolderId == ulParentCycle)
		{
			ec_log_debug("SOAP::copyFolder infinite loop detected for %u", ulDestFolderId);
			return KCERR_FOLDER_CYCLE;
		}
	}

	// Check whether the requested name already exists
	strQuery = "SELECT hierarchy.id FROM hierarchy JOIN properties ON hierarchy.id = properties.hierarchyid WHERE parent=" + stringify(ulDestFolderId) + " AND (hierarchy.flags & " + stringify(MSGFLAG_DELETED) + ") = 0 AND hierarchy.type="+stringify(MAPI_FOLDER)+" AND properties.tag=" + stringify(KOPANO_TAG_DISPLAY_NAME) + " AND properties.type="+stringify(PT_STRING8);
	if(lpszNewFolderName) {
		name = lpszNewFolderName;
		strQuery+= " AND properties.val_string = '" + lpDatabase->Escape(lpszNewFolderName) + "'";
	} else {
		name = format("%u", ulFolderId);
		strSubQuery = "SELECT properties.val_string FROM hierarchy JOIN properties ON hierarchy.id = properties.hierarchyid WHERE hierarchy.id=" + stringify(ulFolderId) + " AND properties.tag=" + stringify(KOPANO_TAG_DISPLAY_NAME) + " AND properties.type=" + stringify(PT_STRING8);
		strQuery+= " AND properties.val_string = ("+strSubQuery+")";
	}
	strQuery += " LIMIT 1";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_debug("SOAP::copyFolder check for existing name (%s) failed: %s (%x)", name.c_str(), GetMAPIErrorMessage(er), er);
		return er;
	}
	if (lpDBResult.get_num_rows() > 0 && ulSyncId == 0) {
		ec_log_err("SOAP::copyFolder(): target name (%s) already exists", name.c_str());
		return KCERR_COLLISION;
	}

	if(lpszNewFolderName == NULL)
	{
		strQuery = "SELECT properties.val_string FROM hierarchy JOIN properties ON hierarchy.id = properties.hierarchyid WHERE hierarchy.id=" + stringify(ulFolderId) + " AND properties.tag=" + stringify(KOPANO_TAG_DISPLAY_NAME) + " AND properties.type=" + stringify(PT_STRING8) + " LIMIT 1";
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess) {
			ec_log_err("SOAP::copyFolder(): problem retrieving source name for %u: %s (%x)", ulFolderId, GetMAPIErrorMessage(er), er);
			return er;
		}
		lpDBRow = lpDBResult.fetch_row();
		if( lpDBRow == NULL || lpDBRow[0] == NULL) {
			ec_log_err("SOAP::copyFolder(): source name (%s) not known", name.c_str());
			return KCERR_NOT_FOUND;
		}

		auto newname = s_alloc<char>(soap, strlen(lpDBRow[0]) + 1);
		memcpy(newname, lpDBRow[0], strlen(lpDBRow[0]) + 1);
		lpszNewFolderName = newname;
	}

	//check copy or a move
	if (!(ulFlags & FOLDER_MOVE)) {
		er = CopyFolderObjects(soap, lpecSession, ulFolderId, ulDestFolderId, lpszNewFolderName, !!(ulFlags&COPY_SUBFOLDERS), ulSyncId);
		if (er != erSuccess)
			ec_log_err("SOAP::copyFolder(): CopyFolderObjects (src folder: %u, dest folder: %u, new name: \"%s\") failed: %s (%x)", ulFolderId, ulDestFolderId, lpszNewFolderName, GetMAPIErrorMessage(er), er);
		return er;
	}
	if (ulObjFlags & MSGFLAG_DELETED) {
		/*
		 * The folder we are moving used to be deleted. This
		 * effictively makes this call an un-delete. We need to
		 * get the folder size for quota management.
		 */
		er = GetFolderSize(lpDatabase, ulFolderId, &llFolderSize);
		if (er != erSuccess) {
			ec_log_err("SOAP::copyFolder(): cannot find size of folder %u: %s (%x)", ulFolderId, GetMAPIErrorMessage(er), er);
			return er;
		}
	}

	// Get grandParent of the old folder
	gcache->GetParent(ulOldParent, &ulOldGrandParent);
	er = lpDatabase->Begin();
	if (er != erSuccess) {
		ec_log_err("SOAP::copyFolder(): cannot start transaction: %s (%x)", GetMAPIErrorMessage(er), er);
		return er;
	}

	// Move the folder to the dest. folder
	// FIXME update modtime
	strQuery = "UPDATE hierarchy SET parent="+stringify(ulDestFolderId)+", flags=flags&"+stringify(~MSGFLAG_DELETED)+" WHERE id="+stringify(ulFolderId);
	if ((er = lpDatabase->DoUpdate(strQuery, &ulAffRows)) != erSuccess) {
		lpDatabase->Rollback();
		ec_log_err("SOAP::copyFolder(): update of modification time failed: %s (%x)", GetMAPIErrorMessage(er), er);
		return KCERR_DATABASE_ERROR;
	}

	if(ulAffRows != 1) {
	    lpDatabase->Rollback();
		ec_log_err("SOAP::copyFolder(): unexpected number of affected rows (expected: 1, got: %u)", ulAffRows);
		return KCERR_DATABASE_ERROR;
	}

	// Update the folder to the destination folder
	//Info: Always an update, It's not faster first check and than update/or not
	strQuery = "UPDATE properties SET val_string = '" + lpDatabase->Escape(lpszNewFolderName) + "' WHERE tag=" + stringify(KOPANO_TAG_DISPLAY_NAME) + " AND hierarchyid="+stringify(ulFolderId) + " AND type=" + stringify(PT_STRING8);
	if ((er = lpDatabase->DoUpdate(strQuery, &ulAffRows)) != erSuccess) {
		ec_log_err("SOAP::copyFolder(): actual move of folder %s failed: %s (%x)", lpszNewFolderName, GetMAPIErrorMessage(er), er);
		lpDatabase->Rollback();
		return KCERR_DATABASE_ERROR;
	}

	// remove PR_DELETED_ON, as the folder is a softdelete folder
	strQuery = "DELETE FROM properties WHERE hierarchyid=" + stringify(ulFolderId) + " AND tag=" + stringify(PROP_ID(PR_DELETED_ON)) + " AND type=" + stringify(PROP_TYPE(PR_DELETED_ON));
	er = lpDatabase->DoDelete(strQuery);
	if (er != erSuccess) {
		ec_log_err("SOAP::copyFolder(): cannot remove PR_DELETED_ON property for %u: %s (%x)", ulFolderId, GetMAPIErrorMessage(er), er);
		lpDatabase->Rollback();
		return KCERR_DATABASE_ERROR;
	}

	// Update the store size if we did an undelete. Note ulSourceStoreId == ulDestStoreId.
	if (llFolderSize > 0) {
		er = UpdateObjectSize(lpDatabase, ulSourceStoreId, MAPI_STORE, UPDATE_ADD, llFolderSize);
		if (er != erSuccess) {
			ec_log_err("SOAP::copyFolder(): problem updating store (%u) size: %s (%x)", ulSourceStoreId, GetMAPIErrorMessage(er), er);
			return er;
		}
	}

	// ICS
	GetSourceKey(ulFolderId, &sSourceKey);
	GetSourceKey(ulDestFolderId, &sDestSourceKey);
	GetSourceKey(ulOldParent, &sParentSourceKey);
	AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, ICS_FOLDER_CHANGE);
	AddChange(lpecSession, ulSyncId, sSourceKey, sDestSourceKey, ICS_FOLDER_CHANGE);

	// Update folder counters
	if (ulObjFlags & MSGFLAG_DELETED) {
		// Undelete
		er = UpdateFolderCount(lpDatabase, ulOldParent, PR_DELETED_FOLDER_COUNT, -1);
		if (er == erSuccess)
			er = UpdateFolderCount(lpDatabase, ulDestFolderId, PR_SUBFOLDERS, 1);
		if (er == erSuccess)
			er = UpdateFolderCount(lpDatabase, ulDestFolderId, PR_FOLDER_CHILD_COUNT, 1);
	} else {
		// Move
		er = UpdateFolderCount(lpDatabase, ulOldParent, PR_SUBFOLDERS, -1);
		if (er == erSuccess)
			er = UpdateFolderCount(lpDatabase, ulOldParent, PR_FOLDER_CHILD_COUNT, -1);
		if (er == erSuccess)
			er = UpdateFolderCount(lpDatabase, ulDestFolderId, PR_SUBFOLDERS, 1);
		if (er == erSuccess)
			er = UpdateFolderCount(lpDatabase, ulDestFolderId, PR_FOLDER_CHILD_COUNT, 1);
	}
	if (er != erSuccess) {
		ec_log_err("SOAP::copyFolder(): updating folder counts failed: %s (%x)", GetMAPIErrorMessage(er), er);
		return er;
	}
	er = ECTPropsPurge::AddDeferredUpdate(lpecSession, lpDatabase, ulDestFolderId, ulOldParent, ulFolderId);
	if (er != erSuccess) {
		ec_log_err("SOAP::copyFolder(): ECTPropsPurge::AddDeferredUpdate failed: %s (%x)", GetMAPIErrorMessage(er), er);
		return er;
	}
	er = lpDatabase->Commit();
	if(er != erSuccess) {
		ec_log_err("SOAP::copyFolder(): database commit failed: %s (%x)", GetMAPIErrorMessage(er), er);
		lpDatabase->Rollback();
		return er;
	}

	// Cache update for objects
	gcache->Update(fnevObjectMoved, ulFolderId);
	// Notify that the folder has moved
	g_lpSessionManager->NotificationMoved(MAPI_FOLDER, ulFolderId, ulDestFolderId, ulOldParent);

	// Update the old folder
	gcache->Update(fnevObjectModified, ulOldParent);
	g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_DELETE, 0, ulOldParent, ulFolderId, MAPI_FOLDER);
	g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulOldParent);

	// Update the old folder's parent
	g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulOldGrandParent, ulOldParent, MAPI_FOLDER);

	// Update the destination folder
	gcache->Update(fnevObjectModified, ulDestFolderId);
	g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD, 0, ulDestFolderId, ulFolderId, MAPI_FOLDER);
	g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulDestFolderId);

	// Update the destination's parent
	gcache->GetParent(ulDestFolderId, &ulGrandParent);
	g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulGrandParent, ulDestFolderId, MAPI_FOLDER);
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(notify, *result, struct notification sNotification, unsigned int *result)
{
	unsigned int ulKey = 0;
	USE_DATABASE();

	// You are only allowed to send newmail notifications at the moment. This could currently
	// only be misused to send other users new mail popup notification for e-mails that aren't
	// new at all ...
	if (sNotification.ulEventType != fnevNewMail)
		return KCERR_NO_ACCESS;
    if (sNotification.newmail == nullptr ||
        sNotification.newmail->pParentId == nullptr ||
        sNotification.newmail->pEntryId == nullptr)
		return KCERR_INVALID_PARAMETER;
	if (!bSupportUnicode && sNotification.newmail->lpszMessageClass != NULL)
		sNotification.newmail->lpszMessageClass = STRIN_FIX(sNotification.newmail->lpszMessageClass);

	er = lpecSession->GetObjectFromEntryId(sNotification.newmail->pParentId, &ulKey);
	if(er != erSuccess)
		return er;

	sNotification.ulConnection = ulKey;
	return g_lpSessionManager->AddNotification(&sNotification, ulKey);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getReceiveFolderTable, lpsReceiveFolderTable->er,
    entryId sStoreId, struct receiveFolderTableResponse *lpsReceiveFolderTable)
{
	int				ulRows = 0;
	int				i;
	unsigned int	ulStoreid = 0;
	USE_DATABASE();

	er = lpecSession->GetObjectFromEntryId(&sStoreId, &ulStoreid);
	if(er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->CheckPermission(ulStoreid, ecSecurityRead);
	if(er != erSuccess)
		return er;

	strQuery = "SELECT objid, messageclass FROM receivefolder WHERE storeid="+stringify(ulStoreid);

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	ulRows = lpDBResult.get_num_rows();
	lpsReceiveFolderTable->sFolderArray.__ptr = s_alloc<receiveFolder>(soap, ulRows);
	lpsReceiveFolderTable->sFolderArray.__size = 0;

	i = 0;
	auto gcache = g_lpSessionManager->GetCacheManager();
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL){
			ec_log_err("getReceiveFolderTable(): row or col null");
			return KCERR_DATABASE_ERROR;
		}
		er = gcache->GetEntryIdFromObject(atoui(lpDBRow[0]), soap, 0, &lpsReceiveFolderTable->sFolderArray.__ptr[i].sEntryId);
		if(er != erSuccess){
			er = erSuccess;
			continue;
		}

		lpsReceiveFolderTable->sFolderArray.__ptr[i].lpszAExplicitClass = STROUT_FIX_CPY(lpDBRow[1]);
		++i;
	}

	lpsReceiveFolderTable->sFolderArray.__size = i;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(deleteUser, *result, unsigned int ulUserId, entryId sUserId, unsigned int *result)
{
	er = GetLocalId(sUserId, ulUserId, &ulUserId, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulUserId);
	if(er != erSuccess)
		return er;
	return lpecSession->GetUserManagement()->DeleteObjectAndSync(ulUserId);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(unhookStore, *result, unsigned int ulStoreType, entryId sUserId, unsigned int ulSyncId, unsigned int *result)
{
	unsigned int	ulUserId = 0;
	unsigned int	ulAffected = 0;
	std::string		strGUID = "Unknown";

	USE_DATABASE();

	// do not use GetLocalId since the user may exist on a different server,
	// but will be migrated here and we need to remove the previous store with different guid.
	er = ABEntryIDToID(&sUserId, &ulUserId, NULL, NULL);
	if(er != erSuccess)
		goto exit;

	if (ulUserId == 0 || ulUserId == KOPANO_UID_SYSTEM || !ECSTORE_TYPE_ISVALID(ulStoreType))
		return KCERR_INVALID_PARAMETER;
	er = lpDatabase->Begin();
	if (er != erSuccess)
		return er;
	strQuery = "SELECT guid FROM stores WHERE user_id=" + stringify(ulUserId) + " AND type=" + stringify(ulStoreType) + " LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		goto exit;
	lpDBRow = lpDBResult.fetch_row();
	lpDBLen = lpDBResult.fetch_row_lengths();
	if (lpDBRow == NULL || lpDBRow[0] == NULL) {
		// store not on this server
		er = KCERR_NOT_FOUND;
		goto exit;
	} 
	strGUID = bin2hex(lpDBLen[0], lpDBRow[0]);

	strQuery = "UPDATE stores SET user_id=0 WHERE user_id=" + stringify(ulUserId) + " AND type=" + stringify(ulStoreType);
	er = lpDatabase->DoUpdate(strQuery, &ulAffected);
	if (er != erSuccess)
		goto exit;

	// ulAffected == 0: The user was already orphaned 
	// ulAffected == 1: correctly disowned owner of store
	if (ulAffected > 1) {
		er = KCERR_COLLISION;
		ec_log_err("unhookStore(): more than expected");
		goto exit;
	}

	er = lpDatabase->Commit();
	if (er != erSuccess)
		goto exit;

exit:
	if (er != erSuccess)
		ec_log_err("Unhook of store (type %d) with userid %d and GUID %s failed with error code 0x%x",  ulStoreType, ulUserId, strGUID.c_str(), er);
	else
		ec_log_err("Unhook of store (type %d) with userid %d and GUID %s succeeded",  ulStoreType, ulUserId, strGUID.c_str());
		
	ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(hookStore, *result, unsigned int ulStoreType, entryId sUserId, struct xsd__base64Binary sStoreGuid, unsigned int ulSyncId, unsigned int *result)
{
	unsigned int	ulUserId = 0;
	unsigned int	ulAffected = 0;
	objectdetails_t sUserDetails;

	USE_DATABASE();

	// do not use GetLocalId since the user may exist on a different server,
	// but will be migrated here and we need to hook an old store with specified guid.
	er = ABEntryIDToID(&sUserId, &ulUserId, NULL, NULL);
	if(er != erSuccess)
		goto exit;

	if (ulUserId == 0 || ulUserId == KOPANO_UID_SYSTEM || !ECSTORE_TYPE_ISVALID(ulStoreType))
	{
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	// get user details, see if this is the correct server
	er = lpecSession->GetUserManagement()->GetObjectDetails(ulUserId, &sUserDetails);
	if (er != erSuccess)
		goto exit;

	// check if store currently is owned and the correct type
	strQuery = "SELECT users.id, stores.id, stores.user_id, stores.hierarchy_id, stores.type FROM stores LEFT JOIN users ON stores.user_id = users.id WHERE guid = ";
	strQuery += lpDatabase->EscapeBinary(sStoreGuid.__ptr, sStoreGuid.__size);
	strQuery += " LIMIT 1";
	
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		goto exit;
	lpDBRow = lpDBResult.fetch_row();
	lpDBLen = lpDBResult.fetch_row_lengths();
	if(lpDBRow == NULL || lpDBLen == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	if (lpDBRow[4] == NULL) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("hookStore(): col null");
		goto exit;
	}

	if (lpDBRow[0]) {
		// this store already belongs to a user
		er = KCERR_COLLISION;
		ec_log_err("hookStore(): store already belongs to a user");
		goto exit;
	}

	if (atoui(lpDBRow[4]) != ulStoreType) {
		ec_log_err("Requested store type is %u, actual store type is %s", ulStoreType, lpDBRow[4]);
		er = KCERR_INVALID_TYPE;
		goto exit;
	}

	ec_log_info("Hooking store \"%s\" to user %d", lpDBRow[1], ulUserId);

	// lpDBRow[2] is the old user id, which is now orphaned. We'll use this id to make the other store orphaned, so we "trade" user IDs.

	// update user with new store id
	er = lpDatabase->Begin();
	if (er != erSuccess)
		goto exit;

	// remove previous user of store
	strQuery = "UPDATE stores SET user_id = " + std::string(lpDBRow[2]) + " WHERE user_id = " + stringify(ulUserId) + " AND type = " + stringify(ulStoreType);
	er = lpDatabase->DoUpdate(strQuery, &ulAffected);
	if (er != erSuccess)
		goto exit;

	// ulAffected == 0: The user was already orphaned 
	// ulAffected == 1: correctly disowned previous owner of store
	if (ulAffected > 1) {
		er = KCERR_COLLISION;
		ec_log_err("hookStore(): owned by multiple users");
		goto exit;
	}

	// set new store
	strQuery = "UPDATE stores SET user_id = " + stringify(ulUserId) + " WHERE guid = ";
	strQuery += lpDatabase->EscapeBinary(sStoreGuid.__ptr, sStoreGuid.__size);

	er = lpDatabase->DoUpdate(strQuery, &ulAffected);
	if (er != erSuccess)
		goto exit;

	// we can't have one store being owned by multiple users
	if (ulAffected != 1) {
		er = KCERR_COLLISION;
		ec_log_err("hookStore(): owned by multiple users (2)");
		goto exit;
	}

	// update owner of store
	strQuery = "UPDATE hierarchy SET owner = " + stringify(ulUserId) + " WHERE id = " + lpDBRow[3];
	er = lpDatabase->DoUpdate(strQuery, &ulAffected);
	if (er != erSuccess)
		goto exit;

	// one store has only one entry point in the hierarchy
	// (may be zero, when the user returns to its original store, so the owner field stays the same)
	if (ulAffected > 1) {
		er = KCERR_COLLISION;
		ec_log_err("hookStore(): owned by multiple users (3)");
		goto exit;
	}

	er = lpDatabase->Commit();
	if (er != erSuccess)
		goto exit;

	// remove store cache item
	g_lpSessionManager->GetCacheManager()->Update(fnevObjectMoved, atoi(lpDBRow[3]));

exit:
	if (er != erSuccess)
		ec_log_err("Hook of store failed: 0x%x", er);
	ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

int ns__deleteStore(struct soap *, ULONG64, unsigned int, unsigned int, unsigned int *result)
{
	/*
	 * Used to move the store to the public, but this is not
	 * possible due to multi-server setups. This was also really
	 * slow because of the storeid column in the properties
	 * table.
	 */
	*result = KCERR_NOT_IMPLEMENTED;
	return SOAP_OK;
}

// softdelete the store from the database, so this function returns quickly
SOAP_ENTRY_START(removeStore, *result, struct xsd__base64Binary sStoreGuid, unsigned int ulSyncId, unsigned int *result)
{
	unsigned int	ulCompanyId = 0;
	unsigned int	ulStoreHierarchyId = 0;
	objectdetails_t sObjectDetails;
	std::string		strUsername;

	USE_DATABASE();

	// find store id and company of guid
	strQuery = "SELECT users.id, stores.guid, stores.hierarchy_id, stores.company, stores.user_name FROM stores LEFT JOIN users ON stores.user_id = users.id WHERE stores.guid = ";
	strQuery += lpDatabase->EscapeBinary(sStoreGuid.__ptr, sStoreGuid.__size);
	strQuery += " LIMIT 1";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		goto exit;
	lpDBRow = lpDBResult.fetch_row();
	lpDBLen = lpDBResult.fetch_row_lengths();
	if(lpDBRow == NULL || lpDBLen == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	// if users.id != NULL, user still present .. log a warning admin is doing something that might not have been the action it wanted to do.
	if (lpDBRow[0] != NULL) {
		// trying to remove store from existing user
		strUsername = lpDBRow[4];
		if (lpecSession->GetUserManagement()->GetObjectDetails(atoi(lpDBRow[0]), &sObjectDetails) == erSuccess)
			strUsername = sObjectDetails.GetPropString(OB_PROP_S_LOGIN); // fullname?

		ec_log_err("Unable to remove store: store is in use by user \"%s\"", strUsername.c_str());
		er = KCERR_COLLISION;
		goto exit;
	}

	// these are all 'not null' columns
	ulStoreHierarchyId = atoi(lpDBRow[2]);
	ulCompanyId = atoi(lpDBRow[3]);

	// Must be administrator over the company to be able to remove the store
	er = lpecSession->GetSecurity()->IsAdminOverUserObject(ulCompanyId);
	if (er != erSuccess)
		goto exit;
	ec_log_info("Started to remove store (%s) with storename \"%s\"", bin2hex(lpDBLen[1], lpDBRow[1]).c_str(), lpDBRow[4]);
	er = lpDatabase->Begin();
	if(er != hrSuccess)
		goto exit;

	// Soft delete store
	er = MarkStoreAsDeleted(lpecSession, lpDatabase, ulStoreHierarchyId, ulSyncId);
	if(er != erSuccess)
		goto exit;

	// Remove the store entry
	strQuery = "DELETE FROM stores WHERE guid=" + lpDatabase->EscapeBinary(lpDBRow[1], lpDBLen[1]);
	er = lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		goto exit;

	// Remove receivefolder entries
	strQuery = "DELETE FROM receivefolder WHERE storeid="+stringify(ulStoreHierarchyId);
	er = lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		goto exit;

	// Remove the acls
	strQuery = "DELETE FROM acl WHERE hierarchy_id="+stringify(ulStoreHierarchyId);
	er = lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		goto exit;
	// TODO: acl cache!

	er = lpDatabase->Commit();
	if(er != erSuccess)
		goto exit;
	ec_log_info("Finished remove store (%s)", bin2hex(lpDBLen[1], lpDBRow[1]).c_str());
exit:
	if(er == KCERR_NO_ACCESS)
		ec_log_err("Failed to remove store access denied");
	else if(er != erSuccess) {
		lpDatabase->Rollback();
		ec_log_err("Failed to remove store, errorcode=0x%08X", er);
	}
}
SOAP_ENTRY_END()

namespace KC {

void *SoftDeleteRemover(void *lpTmpMain)
{
	kcsrv_blocksigs();
	ECRESULT		er = erSuccess;
	ECRESULT*		lper = NULL;
	const char *lpszSetting = NULL;
	unsigned int	ulDeleteTime = 0;
	unsigned int	ulFolders = 0;
	unsigned int	ulStores = 0;
	unsigned int	ulMessages = 0;
	ECSession		*lpecSession = NULL;

	lpszSetting = g_lpSessionManager->GetConfig()->GetSetting("softdelete_lifetime");

	if(lpszSetting)
		ulDeleteTime = atoi(lpszSetting) * 24 * 60 * 60;

	if(ulDeleteTime == 0)
		goto exit;

	er = g_lpSessionManager->CreateSessionInternal(&lpecSession);
	if(er != erSuccess)
		goto exit;

	// Lock the session
	lpecSession->Lock();

	ec_log_info("Start scheduled softdelete clean up");

	er = PurgeSoftDelete(lpecSession, ulDeleteTime, &ulMessages, &ulFolders, &ulStores, (bool*)lpTmpMain);

exit:
	if(ulDeleteTime > 0) {
		if (er == erSuccess)
			ec_log_info("Softdelete done: removed %d stores, %d folders, and %d messages", ulStores, ulFolders, ulMessages);
		else if (er == KCERR_BUSY)
			ec_log_info("Softdelete already running");
		else
			ec_log_info("Softdelete failed: removed %d stores, %d folders, and %d messages", ulStores, ulFolders, ulMessages);
	}

	if(lpecSession) {
		lpecSession->Unlock(); 
		g_lpSessionManager->RemoveSessionInternal(lpecSession);
	}

	// Exit with the error result
	lper = new ECRESULT;
	*lper = er;

	return (void *)lper;
}

} /* namespace */

SOAP_ENTRY_START(checkExistObject, *result, entryId sEntryId, unsigned int ulFlags, unsigned int *result)
{
	unsigned int	ulObjId = 0;
	unsigned int	ulObjType = 0;
	unsigned int	ulDBFlags = 0;
	
	USE_DATABASE();

	er = BeginLockFolders(lpDatabase, EntryId(sEntryId), LOCK_SHARED);
	if(er != erSuccess)
	    goto exit;
	
	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if(er != erSuccess)
	    goto exit;

	er = g_lpSessionManager->GetCacheManager()->GetObject(ulObjId, NULL, NULL, &ulDBFlags, &ulObjType);
	if(er != erSuccess)
	    goto exit;

	if(ulFlags & SHOW_SOFT_DELETES) {
		if(!(ulDBFlags & MSGFLAG_DELETED)) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
	} else {
		if(ulDBFlags & MSGFLAG_DELETED) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
	}
exit:
    lpDatabase->Commit();
    
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(readABProps, readPropsResponse->er, entryId sEntryId, struct readPropsResponse *readPropsResponse)
{
	// FIXME: when props are PT_ERROR, they should not be sent to the client
	// now we have properties in the client which are MAPI_E_NOT_ENOUGH_MEMORY,
	// while they shouldn't be present (or at least MAPI_E_NOT_FOUND)

	// These properties must be of type PT_UNICODE for string properties
	unsigned int sProps[] = {
		/* Don't touch the order of the first 7 elements!!! */
		PR_ENTRYID, PR_CONTAINER_FLAGS, PR_DEPTH, PR_EMS_AB_CONTAINERID, PR_DISPLAY_NAME, PR_EMS_AB_IS_MASTER, PR_EMS_AB_PARENT_ENTRYID,
		PR_EMAIL_ADDRESS, PR_OBJECT_TYPE, PR_DISPLAY_TYPE, PR_SEARCH_KEY, PR_PARENT_ENTRYID, PR_ADDRTYPE, PR_RECORD_KEY, PR_ACCOUNT,
		PR_SMTP_ADDRESS, PR_TRANSMITABLE_DISPLAY_NAME, PR_EMS_AB_HOME_MDB, PR_EMS_AB_HOME_MTA, PR_EMS_AB_PROXY_ADDRESSES,
		PR_EC_ADMINISTRATOR, PR_EC_NONACTIVE, PR_EC_COMPANY_NAME, PR_EMS_AB_X509_CERT, PR_AB_PROVIDER_ID, PR_EMS_AB_HIERARCHY_PATH,
		PR_EC_SENDAS_USER_ENTRYIDS, PR_EC_HOMESERVER_NAME, PR_DISPLAY_TYPE_EX, CHANGE_PROP_TYPE(PR_EMS_AB_IS_MEMBER_OF_DL, PT_MV_BINARY),
		PR_EC_ENABLED_FEATURES, PR_EC_DISABLED_FEATURES, PR_EC_ARCHIVE_SERVERS, PR_EC_ARCHIVE_COUPLINGS, PR_EMS_AB_ROOM_CAPACITY, PR_EMS_AB_ROOM_DESCRIPTION,
		PR_ASSISTANT
	};
	static constexpr unsigned int sPropsContainerRoot[] = {
		/* Don't touch the order of the first 7 elements!!! */
		PR_ENTRYID, PR_CONTAINER_FLAGS, PR_DEPTH, PR_EMS_AB_CONTAINERID, PR_DISPLAY_NAME, PR_EMS_AB_IS_MASTER, PR_EMS_AB_PARENT_ENTRYID,
		PR_OBJECT_TYPE, PR_DISPLAY_TYPE, PR_SEARCH_KEY, PR_RECORD_KEY, PR_PARENT_ENTRYID, PR_AB_PROVIDER_ID, PR_EMS_AB_HIERARCHY_PATH, PR_ACCOUNT,
		PR_EC_HOMESERVER_NAME, PR_EC_COMPANY_NAME
	};

	struct propTagArray ptaProps;
	unsigned int		ulId = 0;
	unsigned int    	ulTypeId = 0;
	ECDatabase*			lpDatabase = NULL;
	objectid_t			sExternId;
	abprops_t lExtraProps;
	const unsigned int *lpProps = nullptr;
	unsigned int		ulProps = 0;
	int		i = 0;

	er = lpecSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	er = ABEntryIDToID(&sEntryId, &ulId, &sExternId, &ulTypeId);
	if(er != erSuccess)
		return er;

	// A v1 EntryID would return a non-empty extern id string.
	if (!sExternId.id.empty())
	{
		er = lpecSession->GetSessionManager()->GetCacheManager()->GetUserObject(sExternId, &ulId, NULL, NULL);
		if (er != erSuccess)
			return er;
	}

	er = lpecSession->GetSecurity()->IsUserObjectVisible(ulId);
	if (er != erSuccess)
		return er;

	if (ulTypeId == MAPI_ABCONT) {
		lpProps = sPropsContainerRoot;
		ulProps = ARRAY_SIZE(sPropsContainerRoot);
	} else if (ulTypeId == MAPI_MAILUSER || ulTypeId == MAPI_DISTLIST) {
		lpProps = sProps;
		ulProps = ARRAY_SIZE(sProps);
	} else {
		return KCERR_INVALID_PARAMETER;
	}

	/* Load the additional addressbook properties */
	try {
		UserPlugin *lpPlugin = NULL;
		if (GetThreadLocalPlugin(g_lpSessionManager->GetPluginFactory(), &lpPlugin) == erSuccess)
			lExtraProps = lpPlugin->getExtraAddressbookProperties();
	} catch (...) { }

	ptaProps.__size = ulProps;
	ptaProps.__size += lExtraProps.size();
	ptaProps.__ptr = s_alloc<unsigned int>(soap, ptaProps.__size);

	/* Copy fixed properties */
	memcpy(ptaProps.__ptr, lpProps, ulProps * sizeof(unsigned int));
	i = ulProps;

	/* Copy extra properties */
	for (const auto &prop : lExtraProps) {
		ptaProps.__ptr[i] = prop;
		/* The client requires some properties with non-standard types */
		switch (PROP_ID(ptaProps.__ptr[i])) {
		case PROP_ID(PR_MANAGER_NAME):
		case PROP_ID(PR_EMS_AB_MANAGER):
			/* Rename PR_MANAGER_NAME to PR_EMS_AB_MANAGER and provide the PT_BINARY version with the entryid */
			ptaProps.__ptr[i] = CHANGE_PROP_TYPE(PR_EMS_AB_MANAGER, PT_BINARY);
			break;
		case PROP_ID(PR_EMS_AB_REPORTS):
			ptaProps.__ptr[i] = CHANGE_PROP_TYPE(PR_EMS_AB_REPORTS, PT_MV_BINARY);
			break;
		case PROP_ID(PR_EMS_AB_OWNER):
			/* Also provide the PT_BINARY version with the entryid */
			ptaProps.__ptr[i] = CHANGE_PROP_TYPE(PR_EMS_AB_OWNER, PT_BINARY);
			break;
		default:
			// @note plugin most likely returns PT_STRING8 and PT_MV_STRING8 types
			// Since CopyDatabasePropValToSOAPPropVal() always returns PT_UNICODE types, we will convert these here too
			// Therefore, the sProps / sPropsContainerRoot must contain PT_UNICODE types only!
			if (PROP_TYPE(ptaProps.__ptr[i]) == PT_MV_STRING8)
				ptaProps.__ptr[i] = CHANGE_PROP_TYPE(ptaProps.__ptr[i], PT_MV_UNICODE);
			else if (PROP_TYPE(ptaProps.__ptr[i]) == PT_STRING8)
				ptaProps.__ptr[i] = CHANGE_PROP_TYPE(ptaProps.__ptr[i], PT_UNICODE);
			break;
		}
		++i;
	}

	/* Update the total size, the previously set value might not be accurate */
	ptaProps.__size = i;

	/* Read properties */
	auto usrmgt = lpecSession->GetUserManagement();
	if (ulTypeId == MAPI_ABCONT) {
		er = usrmgt->GetContainerProps(soap, ulId, &ptaProps, &readPropsResponse->aPropVal);
		if (er != erSuccess)
			return er;
	} else {
		er = usrmgt->GetProps(soap, ulId, &ptaProps, &readPropsResponse->aPropVal);
		if (er != erSuccess)
			return er;
	}

	/* Copy properties which have been correctly read to tag array */
	readPropsResponse->aPropTag.__size = 0;
	readPropsResponse->aPropTag.__ptr = s_alloc<unsigned int>(soap, ptaProps.__size);

	for (gsoap_size_t i = 0; i < readPropsResponse->aPropVal.__size; ++i) {
		if (!bSupportUnicode) {
			er = FixPropEncoding(soap, stringCompat, Out, readPropsResponse->aPropVal.__ptr + i);
			if (er != erSuccess)
				return er;
		}

		if(PROP_TYPE(readPropsResponse->aPropVal.__ptr[i].ulPropTag) != PT_ERROR)
			readPropsResponse->aPropTag.__ptr[readPropsResponse->aPropTag.__size++] = readPropsResponse->aPropVal.__ptr[i].ulPropTag;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

int ns__writeABProps(struct soap *, ULONG64, entryId, struct propValArray *,
    unsigned int *r)
{
	*r = KCERR_NOT_FOUND;
	return SOAP_OK;
}

int ns__deleteABProps(struct soap *, ULONG64, entryId, struct propTagArray *,
    unsigned int *r)
{
	*r = KCERR_NOT_FOUND;
	return SOAP_OK;
}

int ns__loadABProp(struct soap *, ULONG64, entryId, unsigned int,
    struct loadPropResponse *)
{
	return SOAP_OK;
}

/**
 * ns__abResolveNames
 *
 * @param[in]	lpaPropTag	SOAP proptag array containing requested properties.
 * @param[in]	lpsRowSet	Rows with possible search request, if matching flag in lpaFlags is MAPI_UNRESOLVED.
 * @param[in]	lpaFlags	Status of row.
 * @param[in]	ulFlags		Client ulFlags for IABContainer::ResolveNames()
 * @param[out]	lpsABResolveNames	copies of new rows and flags.
 */
SOAP_ENTRY_START(abResolveNames, lpsABResolveNames->er, struct propTagArray* lpaPropTag, struct rowSet* lpsRowSet, struct flagArray* lpaFlags, unsigned int ulFlags, struct abResolveNamesResponse* lpsABResolveNames)
{
	unsigned int	ulFlag = 0;
	unsigned int    ulObjectId = 0;
	char*			search = NULL;
	struct propValArray sPropValArrayDst;
	struct propVal *lpDisplayName = NULL;

	lpsABResolveNames->aFlags.__size = lpaFlags->__size;
	lpsABResolveNames->aFlags.__ptr = s_alloc<unsigned int>(soap, lpaFlags->__size);
	memset(lpsABResolveNames->aFlags.__ptr, 0, sizeof(unsigned int) * lpaFlags->__size);

	lpsABResolveNames->sRowSet.__size = lpsRowSet->__size;
	lpsABResolveNames->sRowSet.__ptr = s_alloc<propValArray>(soap, lpsRowSet->__size);
	memset(lpsABResolveNames->sRowSet.__ptr, 0, sizeof(struct propValArray) * lpsRowSet->__size);

	if (!bSupportUnicode) {
		er = FixRowSetEncoding(soap, stringCompat, In, lpsRowSet);
		if (er != erSuccess)
			return er;
	}

	auto usrmgt = lpecSession->GetUserManagement();
	for (gsoap_size_t i = 0; i < lpsRowSet->__size; ++i) {
		lpsABResolveNames->aFlags.__ptr[i] = lpaFlags->__ptr[i];

		if(lpaFlags->__ptr[i] == MAPI_RESOLVED)
			continue; // Client knows the information

		lpDisplayName = FindProp(&lpsRowSet->__ptr[i], CHANGE_PROP_TYPE(PR_DISPLAY_NAME, PT_UNSPECIFIED));
		if(lpDisplayName == NULL || (PROP_TYPE(lpDisplayName->ulPropTag) != PT_STRING8 && PROP_TYPE(lpDisplayName->ulPropTag) != PT_UNICODE))
			continue; // No display name

		search = lpDisplayName->Value.lpszA;

		/* NOTE: ECUserManagement is responsible for calling ECSecurity::IsUserObjectVisible(ulObjectId) for found objects */
		switch (usrmgt->SearchObjectAndSync(search, ulFlags, &ulObjectId)) {
		case KCERR_COLLISION:
			ulFlag = MAPI_AMBIGUOUS;
			break;
		case erSuccess:
			ulFlag = MAPI_RESOLVED;
			break;
		case KCERR_NOT_FOUND:
		default:
			ulFlag = MAPI_UNRESOLVED;
			break;
		}

		lpsABResolveNames->aFlags.__ptr[i] = ulFlag;

		if(lpsABResolveNames->aFlags.__ptr[i] == MAPI_RESOLVED) {
			er = usrmgt->GetProps(soap, ulObjectId, lpaPropTag, &sPropValArrayDst);
			if(er != erSuccess)
				return er;
			er = MergePropValArray(soap, &lpsRowSet->__ptr[i], &sPropValArrayDst, &lpsABResolveNames->sRowSet.__ptr[i]);
			if(er != erSuccess)
				return er;
		}
	}

	if (!bSupportUnicode) {
		er = FixRowSetEncoding(soap, stringCompat, Out, &lpsABResolveNames->sRowSet);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

/** 
 * Syncs a new list of companies, and for each company syncs the users.
 * 
 * @param[in] ulCompanyId unused, id of company to sync
 * @param[in] sCompanyId unused, entryid of company to sync
 * @param[out] result kopano error code
 * 
 * @return soap error code
 */
SOAP_ENTRY_START(syncUsers, *result, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result)
{
	er = lpecSession->GetUserManagement()->SyncAllObjects();
}
SOAP_ENTRY_END()

// Quota
SOAP_ENTRY_START(GetQuota, lpsQuota->er, unsigned int ulUserid, entryId sUserId, bool bGetUserDefault, struct quotaResponse* lpsQuota)
{
	quotadetails_t	quotadetails;

	er = GetLocalId(sUserId, ulUserid, &ulUserid, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	auto sec = lpecSession->GetSecurity();
	if (sec->IsAdminOverUserObject(ulUserid) != erSuccess &&
	    sec->GetUserId() != ulUserid)
		return KCERR_NO_ACCESS;
	er = sec->GetUserQuota(ulUserid, bGetUserDefault, &quotadetails);
	if(er != erSuccess)
		return er;
	lpsQuota->sQuota.bUseDefaultQuota = quotadetails.bUseDefaultQuota;
	lpsQuota->sQuota.bIsUserDefaultQuota = quotadetails.bIsUserDefaultQuota;
	lpsQuota->sQuota.llHardSize = quotadetails.llHardSize;
	lpsQuota->sQuota.llSoftSize = quotadetails.llSoftSize;
	lpsQuota->sQuota.llWarnSize = quotadetails.llWarnSize;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(SetQuota, *result, unsigned int ulUserid, entryId sUserId, struct quota* lpsQuota, unsigned int *result)
{
	quotadetails_t	quotadetails;

	er = GetLocalId(sUserId, ulUserid, &ulUserid, NULL);
	if (er != erSuccess)
		return er;

	// Check permission
	auto sec = lpecSession->GetSecurity();
	if (sec->IsAdminOverUserObject(ulUserid) != erSuccess &&
	    sec->GetUserId() != ulUserid)
		return KCERR_NO_ACCESS;

	quotadetails.bUseDefaultQuota = lpsQuota->bUseDefaultQuota;
	quotadetails.bIsUserDefaultQuota = lpsQuota->bIsUserDefaultQuota;
	quotadetails.llHardSize = lpsQuota->llHardSize;
	quotadetails.llSoftSize = lpsQuota->llSoftSize;
	quotadetails.llWarnSize = lpsQuota->llWarnSize;
	return lpecSession->GetUserManagement()->SetQuotaDetailsAndSync(ulUserid, quotadetails);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(AddQuotaRecipient, *result, unsigned int ulCompanyid, entryId sCompanyId, unsigned int ulRecipientId, entryId sRecipientId, unsigned int ulType, unsigned int *result);
{
	er = GetLocalId(sCompanyId, ulCompanyid, &ulCompanyid, NULL);
	if (er != erSuccess)
		return er;
	if (lpecSession->GetSecurity()->IsAdminOverUserObject(ulCompanyid) != erSuccess)
		return KCERR_NO_ACCESS;
	er = GetLocalId(sRecipientId, ulRecipientId, &ulRecipientId, NULL);
	if (er != erSuccess)
		return er;
	if (OBJECTCLASS_TYPE(ulType) == OBJECTTYPE_MAILUSER)
		er = lpecSession->GetUserManagement()->AddSubObjectToObjectAndSync(OBJECTRELATION_QUOTA_USERRECIPIENT, ulCompanyid, ulRecipientId);
	else if (ulType == CONTAINER_COMPANY)
		er = lpecSession->GetUserManagement()->AddSubObjectToObjectAndSync(OBJECTRELATION_QUOTA_COMPANYRECIPIENT, ulCompanyid, ulRecipientId);
	else
		er = KCERR_INVALID_TYPE;
	return er;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(DeleteQuotaRecipient, *result, unsigned int ulCompanyid, entryId sCompanyId, unsigned int ulRecipientId, entryId sRecipientId, unsigned int ulType, unsigned int *result);
{
	er = GetLocalId(sCompanyId, ulCompanyid, &ulCompanyid, NULL);
	if (er != erSuccess)
		return er;
	if (lpecSession->GetSecurity()->IsAdminOverUserObject(ulCompanyid) != erSuccess)
		return KCERR_NO_ACCESS;
	er = GetLocalId(sRecipientId, ulRecipientId, &ulRecipientId, NULL);
	if (er != erSuccess)
		return er;
	if (OBJECTCLASS_TYPE(ulType) == OBJECTTYPE_MAILUSER)
		er = lpecSession->GetUserManagement()->DeleteSubObjectFromObjectAndSync(OBJECTRELATION_QUOTA_USERRECIPIENT, ulCompanyid, ulRecipientId);
	else if (ulType == CONTAINER_COMPANY)
		er = lpecSession->GetUserManagement()->DeleteSubObjectFromObjectAndSync(OBJECTRELATION_QUOTA_COMPANYRECIPIENT, ulCompanyid, ulRecipientId);
	else
		er = KCERR_INVALID_TYPE;
	return er;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(GetQuotaRecipients, lpsUserList->er, unsigned int ulUserid, entryId sUserId, struct userListResponse *lpsUserList)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpUsers;
	objectid_t sExternId;
	objectdetails_t details;
	userobject_relation_t relation;
	unsigned int ulCompanyId;
	bool bHasLocalStore = false;
	entryId sUserEid;

	// does not return full class in sExternId.objclass
	er = GetLocalId(sUserId, ulUserid, &ulUserid, &sExternId);
	if (er != erSuccess)
		return er;

	// re-evaluate userid to externid to get the full class (mapi clients only know MAPI_ABCONT for companies)
	er = g_lpSessionManager->GetCacheManager()->GetUserObject(ulUserid, &sExternId, NULL, NULL);
	if (er != erSuccess)
		return er;
	er = CheckUserStore(lpecSession, ulUserid, ECSTORE_TYPE_PRIVATE, &bHasLocalStore);
	if (er != erSuccess)
		return er;
	if (!bHasLocalStore)
		return KCERR_NOT_FOUND;

	//Check permission
	auto sec = lpecSession->GetSecurity();
	if (sec->IsAdminOverUserObject(ulUserid) != erSuccess)
		return KCERR_NO_ACCESS;

	/* Not all objectclasses support quota */
	if ((sExternId.objclass == NONACTIVE_CONTACT) ||
		(OBJECTCLASS_TYPE(sExternId.objclass) == OBJECTTYPE_DISTLIST) ||
		(sExternId.objclass == CONTAINER_ADDRESSLIST))
		return KCERR_INVALID_TYPE;
	auto usrmgt = lpecSession->GetUserManagement();
	er = usrmgt->GetObjectDetails(ulUserid, &details);
	if (er != erSuccess)
		return er;

	if (OBJECTCLASS_TYPE(details.GetClass())== OBJECTTYPE_MAILUSER) {
		ulCompanyId = details.GetPropInt(OB_PROP_I_COMPANYID);
		relation = OBJECTRELATION_QUOTA_USERRECIPIENT;
	} else if (details.GetClass() == CONTAINER_COMPANY) {
		ulCompanyId = ulUserid;
		relation = OBJECTRELATION_QUOTA_COMPANYRECIPIENT;
	} else {
		return KCERR_INVALID_TYPE;
	}

	/* When uLCompanyId is 0 then there are no recipient relations we could request,
	 * in that case we should manually allocate the list so it is safe to add the user
	 * to the list. */
	if (ulCompanyId != 0) {
		er = usrmgt->GetSubObjectsOfObjectAndSync(relation, ulCompanyId, &unique_tie(lpUsers));
		if (er != erSuccess)
			return er;
	} else
		lpUsers.reset(new std::list<localobjectdetails_t>);

	if (OBJECTCLASS_TYPE(details.GetClass())== OBJECTTYPE_MAILUSER) {
		/* The main recipient (the user over quota) must be the first entry */
		lpUsers->emplace_front(ulUserid, details);
	} else if (details.GetClass() == CONTAINER_COMPANY) {
		/* Append the system administrator for the company */
		unsigned int ulSystem;
		objectdetails_t systemdetails;

		ulSystem = details.GetPropInt(OB_PROP_I_SYSADMIN);
		er = sec->IsUserObjectVisible(ulSystem);
		if (er != erSuccess)
			return er;
		er = usrmgt->GetObjectDetails(ulSystem, &systemdetails);
		if (er != erSuccess)
			return er;
		lpUsers->emplace_front(ulSystem, systemdetails);
		/* The main recipient (the company's public store) must be the first entry */
		lpUsers->emplace_front(ulUserid, details);
	}

	lpsUserList->sUserArray.__size = 0;
	lpsUserList->sUserArray.__ptr = s_alloc<user>(soap, lpUsers->size());

	for (const auto &user : *lpUsers) {
		if ((OBJECTCLASS_TYPE(user.GetClass()) != OBJECTTYPE_MAILUSER) ||
			(details.GetClass() == NONACTIVE_CONTACT))
				continue;
		if (sec->IsUserObjectVisible(user.ulId) != erSuccess)
			continue;
		er = GetABEntryID(user.ulId, soap, &sUserEid);
		if (er != erSuccess)
			return er;
		er = CopyUserDetailsToSoap(user.ulId, &sUserEid, user,
		     lpecSession->GetCapabilities() & KOPANO_CAP_EXTENDED_ANON,
		     soap, &lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size]);
		if (er != erSuccess)
			return er;

		// 6.40.0 stores the object class in the IsNonActive field
		if (lpecSession->ClientVersion() == ZARAFA_VERSION_6_40_0)
			lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulIsNonActive = lpsUserList->sUserArray.__ptr[lpsUserList->sUserArray.__size].ulObjClass;

		if (!bSupportUnicode) {
			er = FixUserEncoding(soap, stringCompat, Out, lpsUserList->sUserArray.__ptr + lpsUserList->sUserArray.__size);
			if (er != erSuccess)
				return er;
		}
		++lpsUserList->sUserArray.__size;
		if (sUserEid.__ptr)
		{
			// sUserEid is placed in userdetails, no need to free
			sUserEid.__ptr = NULL;
			sUserEid.__size = 0;
		}
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(GetQuotaStatus, lpsQuotaStatus->er, unsigned int ulUserid, entryId sUserId, struct quotaStatus* lpsQuotaStatus)
{
	quotadetails_t	quotadetails;
	long long		llStoreSize = 0;
	objectid_t		sExternId;
	bool			bHasLocalStore = false;

	eQuotaStatus QuotaStatus;

	//Set defaults
	lpsQuotaStatus->llStoreSize = 0;

	// does not return full class in sExternId.objclass
	er = GetLocalId(sUserId, ulUserid, &ulUserid, &sExternId);
	if (er != erSuccess)
		return er;

	// re-evaluate userid to externid to get the full class (mapi clients only know MAPI_ABCONT for companies)
	er = g_lpSessionManager->GetCacheManager()->GetUserObject(ulUserid, &sExternId, NULL, NULL);
	if (er != erSuccess)
		return er;
	er = CheckUserStore(lpecSession, ulUserid, ECSTORE_TYPE_PRIVATE, &bHasLocalStore);
	if (er != erSuccess)
		return er;
	if (!bHasLocalStore)
		return KCERR_NOT_FOUND;

	// Check permission
	if(lpecSession->GetSecurity()->IsAdminOverUserObject(ulUserid) != erSuccess &&
		(lpecSession->GetSecurity()->GetUserId() != ulUserid))
		return KCERR_NO_ACCESS;

	/* Not all objectclasses support quota */
	if ((sExternId.objclass == NONACTIVE_CONTACT) ||
		(OBJECTCLASS_TYPE(sExternId.objclass) == OBJECTTYPE_DISTLIST) ||
		(sExternId.objclass == CONTAINER_ADDRESSLIST))
		return KCERR_INVALID_TYPE;

	if (OBJECTCLASS_TYPE(sExternId.objclass) == OBJECTTYPE_MAILUSER || sExternId.objclass == CONTAINER_COMPANY) {
		er = lpecSession->GetSecurity()->GetUserSize(ulUserid, &llStoreSize);
		if(er != erSuccess)
			return er;
	} else {
		return KCERR_INVALID_PARAMETER;
	}

	// check the store quota status
	er = lpecSession->GetSecurity()->CheckUserQuota(ulUserid, llStoreSize, &QuotaStatus);
	if(er != erSuccess)
		return er;
	lpsQuotaStatus->llStoreSize = llStoreSize;
	lpsQuotaStatus->ulQuotaStatus = (unsigned int)QuotaStatus;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getMessageStatus, lpsStatus->er, entryId sEntryId, unsigned int ulFlags, struct messageStatus* lpsStatus)
{
	unsigned int	ulMsgStatus = 0;
	unsigned int	ulId = 0;
	USE_DATABASE();

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulId);
	if(er != erSuccess)
		return er;

	//Check security
	er = lpecSession->GetSecurity()->CheckPermission(ulId, ecSecurityRead);
	if(er != erSuccess)
		return er;

	// Get the old flags
	strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid="+stringify(ulId)+" AND tag=3607 AND type=3 LIMIT 2";
	if ((er = lpDatabase->DoSelect(strQuery, &lpDBResult)) != erSuccess) {
		ec_log_err("getMessageStatus(): select failed %x", er);
		return KCERR_DATABASE_ERROR;
	}
	if (lpDBResult.get_num_rows() == 1) {
		lpDBRow = lpDBResult.fetch_row();
		if(lpDBRow == NULL || lpDBRow[0] == NULL) {
			ec_log_err("getMessageStatus(): row or col null");
			return KCERR_DATABASE_ERROR;
		}

		ulMsgStatus = atoui(lpDBRow[0]);
	}

	lpsStatus->ulMessageStatus = ulMsgStatus;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(setMessageStatus, lpsOldStatus->er, entryId sEntryId, unsigned int ulNewStatus, unsigned int ulNewStatusMask, unsigned int ulSyncId, struct messageStatus* lpsOldStatus)
{
	unsigned int	ulOldMsgStatus = 0;
	unsigned int	ulNewMsgStatus = 0;
	unsigned int	ulRows = 0;
	unsigned int	ulId = 0;
	unsigned int	ulParent = 0;
	unsigned int	ulObjFlags = 0;
	
	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey;

	USE_DATABASE();

	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulId);
	if(er != erSuccess)
		return er;

	//Check security
	er = lpecSession->GetSecurity()->CheckPermission(ulId, ecSecurityEdit);
	if(er != erSuccess)
		return er;
	er = lpDatabase->Begin();
	if(er != erSuccess)
		return er;
	er = g_lpSessionManager->GetCacheManager()->GetObject(ulId, &ulParent, NULL, &ulObjFlags);
	if(er != erSuccess)
		goto exit;

	// Get the old flags (PR_MSG_STATUS)
	strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid="+stringify(ulId)+" AND tag=3607 AND type=3 LIMIT 2";
	if((er = lpDatabase->DoSelect(strQuery, &lpDBResult)) != erSuccess) {
		ec_log_err("setMessageStatus(): select failed %x", er);
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}

	ulRows = lpDBResult.get_num_rows();
	if (ulRows == 1) {
		lpDBRow = lpDBResult.fetch_row();
		if(lpDBRow == NULL || lpDBRow[0] == NULL) {
			er = KCERR_DATABASE_ERROR;
			ec_log_err("setMessageStatus(): row or col null");
			goto exit;
		}

		ulOldMsgStatus = atoui(lpDBRow[0]);
	}

	// Set the new flags
	ulNewMsgStatus = (ulOldMsgStatus &~ulNewStatusMask) | (ulNewStatusMask & ulNewStatus);

	if(ulRows > 0){
		strQuery = "UPDATE properties SET val_ulong="+stringify(ulNewMsgStatus)+" WHERE  hierarchyid="+stringify(ulId)+" AND tag=3607 AND type=3";
		er = lpDatabase->DoUpdate(strQuery);
	}else {
		strQuery = "INSERT INTO properties(hierarchyid, tag, type, val_ulong) VALUES("+stringify(ulId)+", 3607, 3,"+stringify(ulNewMsgStatus)+")";
		er = lpDatabase->DoInsert(strQuery);
	}

	if(er != erSuccess) {
		ec_log_err("setMessageStatus(): query failed");
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}

	lpsOldStatus->ulMessageStatus = ulOldMsgStatus;

	GetSourceKey(ulId, &sSourceKey);
	GetSourceKey(ulParent, &sParentSourceKey);

	AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, ICS_MESSAGE_CHANGE);

	er = lpDatabase->Commit();
	if(er != erSuccess)
		goto exit;

	// Now, send the notifications
	g_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, ulId);
	g_lpSessionManager->NotificationModified(MAPI_MESSAGE, ulId, ulParent);
	g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, ulObjFlags&MSGFLAG_NOTIFY_FLAGS, ulParent, ulId, MAPI_MESSAGE);

exit:
    ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getChanges, lpsChangesResponse->er, struct xsd__base64Binary sSourceKeyFolder, unsigned int ulSyncId, unsigned int ulChangeId, unsigned int ulChangeType, unsigned int ulFlags, struct restrictTable *lpsRestrict, struct icsChangeResponse* lpsChangesResponse)
{
	icsChangesArray *lpChanges = NULL;
	SOURCEKEY		sSourceKey(sSourceKeyFolder.__size, (char *)sSourceKeyFolder.__ptr);

	er = GetChanges(soap, lpecSession, sSourceKey, ulSyncId, ulChangeId, ulChangeType, ulFlags, lpsRestrict, &lpsChangesResponse->ulMaxChangeId, &lpChanges);
	if(er != erSuccess)
		return er;
	lpsChangesResponse->sChangesArray = *lpChanges;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(setSyncStatus, lpsResponse->er, struct xsd__base64Binary sSourceKeyFolder, unsigned int ulSyncId, unsigned int ulChangeId, unsigned int ulChangeType, unsigned int ulFlags, struct setSyncStatusResponse *lpsResponse)
{
	SOURCEKEY		sSourceKey(sSourceKeyFolder.__size, (char *)sSourceKeyFolder.__ptr);
	unsigned int	ulFolderId = 0, dummy = 0;
	USE_DATABASE();

	er = lpDatabase->Begin();
	if (er != erSuccess)
		return er;
    if(sSourceKey.size()) {
    	er = g_lpSessionManager->GetCacheManager()->GetObjectFromProp(PROP_ID(PR_SOURCE_KEY), sSourceKey.size(), sSourceKey, &ulFolderId);
    	if(er != erSuccess)
	        goto exit;
    } else {
        ulFolderId = 0;
    }
    
    if(ulFolderId == 0) {
        if(lpecSession->GetSecurity()->GetAdminLevel() != ADMIN_LEVEL_SYSADMIN)
            er = KCERR_NO_ACCESS;
    }
	// Check security
	else if (ulChangeType == ICS_SYNC_CONTENTS)
            er = lpecSession->GetSecurity()->CheckPermission(ulFolderId, ecSecurityRead);
	else if (ulChangeType == ICS_SYNC_HIERARCHY)
            er = lpecSession->GetSecurity()->CheckPermission(ulFolderId, ecSecurityFolderVisible);
	else
            er = KCERR_INVALID_TYPE;
	if(er != erSuccess)
		goto exit;

	if(ulSyncId == 0){
	    // SyncID is 0, which means the client will be requesting an initial sync from this new sync. The change_id will
		// be updated in another call done when the synchronization is complete.
		strQuery = "INSERT INTO syncs (change_id, sourcekey, sync_type, sync_time) VALUES (1, "+lpDatabase->EscapeBinary(sSourceKey, sSourceKey.size())+", '" + stringify(ulChangeType) + "', FROM_UNIXTIME("+stringify(time(NULL))+"))";
		er = lpDatabase->DoInsert(strQuery, &ulSyncId);
		if (er == erSuccess) {
			er = lpDatabase->Commit();
			lpsResponse->ulSyncId = ulSyncId;
		}
		goto exit;
	}

	strQuery = "SELECT sourcekey, change_id, sync_type FROM syncs WHERE id ="+stringify(ulSyncId)+" FOR UPDATE";
	//TODO check existing sync
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;
	if (lpDBResult.get_num_rows() == 0) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}
	lpDBRow = lpDBResult.fetch_row();
	lpDBLen = lpDBResult.fetch_row_lengths();
	if( lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL){
		er = KCERR_DATABASE_ERROR; // this should never happen
		ec_log_err("setSyncStatus(): row/col NULL");
		goto exit;
	}

	if(lpDBLen[0] != sSourceKey.size() || memcmp(lpDBRow[0], sSourceKey, sSourceKey.size()) != 0){
		er = KCERR_COLLISION;
		ec_log_err("setSyncStatus(): collision");
		goto exit;
	}

	if((dummy = atoui(lpDBRow[2])) != ulChangeType){
		er = KCERR_COLLISION;
		ec_log_err("SetSyncStatus(): unexpected change type %u/%u", dummy, ulChangeType);
		goto exit;
	}

	strQuery = "UPDATE syncs SET change_id = "+stringify(ulChangeId)+", sync_time = FROM_UNIXTIME("+stringify(time(NULL))+") WHERE id = "+stringify(ulSyncId);
	er = lpDatabase->DoUpdate(strQuery);
	if(er != erSuccess)
		goto exit;

	er = lpDatabase->Commit();
	if (er != erSuccess)
		goto exit;

	lpsResponse->ulSyncId = ulSyncId;
exit:
	ROLLBACK_ON_ERROR();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getEntryIDFromSourceKey, lpsResponse->er, entryId sStoreId, struct xsd__base64Binary folderSourceKey, struct xsd__base64Binary messageSourceKey, struct getEntryIDFromSourceKeyResponse *lpsResponse)
{
	unsigned int	ulObjType = 0;
	unsigned int	ulObjId = 0;
	unsigned int	ulMessageId = 0;
	unsigned int	ulFolderId = 0;
	unsigned int	ulParent = 0;
	unsigned int	ulStoreId = 0;
	unsigned int	ulStoreFound = 0;
	EID				eid;
	
	USE_DATABASE();

	er = BeginLockFolders(lpDatabase, SOURCEKEY(folderSourceKey), LOCK_SHARED);
	if(er != erSuccess)
	    goto exit;
	
	er = lpecSession->GetObjectFromEntryId(&sStoreId, &ulStoreId);
	if(er != erSuccess)
	    goto exit;

	er = g_lpSessionManager->GetCacheManager()->GetObjectFromProp(PROP_ID(PR_SOURCE_KEY), folderSourceKey.__size, folderSourceKey.__ptr, &ulFolderId);
	if(er != erSuccess)
		goto exit;
		
	if(messageSourceKey.__size != 0) {
		er = g_lpSessionManager->GetCacheManager()->GetObjectFromProp(PROP_ID(PR_SOURCE_KEY), messageSourceKey.__size, messageSourceKey.__ptr, &ulMessageId);
		if(er != erSuccess)
			goto exit;

        // Check if given sourcekey is in the given parent sourcekey
		er = g_lpSessionManager->GetCacheManager()->GetParent(ulMessageId, &ulParent);
		if(er != erSuccess || ulFolderId != ulParent) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		
		
		ulObjId = ulMessageId;
		ulObjType = MAPI_MESSAGE;

	} else {
		ulObjId = ulFolderId;
		ulObjType = MAPI_FOLDER;
	}

	// Check if the folder given is actually in the store we're working on (may not be so if cache
	// is out-of-date during a re-import of a store that has been deleted and re-imported). In this case
	// we return NOT FOUND, which really is true since we cannot found the given sourcekey in this store.
    er = g_lpSessionManager->GetCacheManager()->GetStore(ulFolderId, &ulStoreFound, NULL);
    if(er != erSuccess || ulStoreFound != ulStoreId) {
        er = KCERR_NOT_FOUND;
        goto exit;
    }

	// Check security
	if (ulObjType == MAPI_FOLDER)
		er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityFolderVisible);
	else
		er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityRead);
	if(er != erSuccess)
		goto exit;

	er = g_lpSessionManager->GetCacheManager()->GetEntryIdFromObject(ulObjId, soap, 0, &lpsResponse->sEntryId);
	if(er != erSuccess)
		goto exit;

exit:
    lpDatabase->Commit();
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getSyncStates, lpsResponse->er, struct mv_long ulaSyncId, struct getSyncStatesReponse *lpsResponse)
{
	er = GetSyncStates(soap, lpecSession, ulaSyncId, &lpsResponse->sSyncStates);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getLicenseAuth, r->er, struct xsd__base64Binary, struct getLicenseAuthResponse *r)
{
	r->sAuthResponse.__ptr = nullptr;
	r->sAuthResponse.__size = 0;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getLicenseCapa, lpsResponse->er, unsigned int ulServiceType, struct getLicenseCapaResponse *lpsResponse)
{
    std::vector<std::string> lstCapabilities;

	er = ECLicenseClient().GetCapabilities(ulServiceType, lstCapabilities);
    if(er != erSuccess)
		return er;
        
    lpsResponse->sCapabilities.__size = lstCapabilities.size();
    lpsResponse->sCapabilities.__ptr = s_alloc<char *>(soap, lstCapabilities.size());
    
	for (unsigned int i = 0; i < lstCapabilities.size(); ++i)
		lpsResponse->sCapabilities.__ptr[i] = s_strcpy(soap, lstCapabilities[i].c_str());
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getLicenseUsers, lpsResponse->er, unsigned int ulServiceType, struct getLicenseUsersResponse *lpsResponse)
{
	unsigned int ulUsers = 0;
	std::vector<std::string> lstCapabilities;

	er = ECLicenseClient().GetInfo(ulServiceType, &ulUsers);
	if(er != erSuccess)
		return er;

	lpsResponse->ulUsers = ulUsers;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(resolvePseudoUrl, lpsResponse->er, const char *lpszPseudoUrl,
    struct resolvePseudoUrlResponse *lpsResponse)
{
	std::string		strServerPath;

	if (!lpecSession->GetSessionManager()->IsDistributedSupported())
	{
		/**
		 * Non-distributed environments do issue pseudo URLs, but merely to
		 * make upgrading later possible. We will just return the passed pseudo URL
		 * and say that we're the peer. That would cause the client to keep on
		 * using the current connection.
         **/
		lpsResponse->lpszServerPath = lpszPseudoUrl;
		lpsResponse->bIsPeer = true;
		return erSuccess;
	}

	lpszPseudoUrl = STRIN_FIX(lpszPseudoUrl);

	if (strncmp(lpszPseudoUrl, "pseudo://", 9))
		return KCERR_INVALID_PARAMETER;
	er = GetBestServerPath(soap, lpecSession, lpszPseudoUrl + 9, &strServerPath);
	if (er != erSuccess)
		return er;

	lpsResponse->lpszServerPath = STROUT_FIX_CPY(strServerPath.c_str());
	lpsResponse->bIsPeer = strcasecmp(g_lpSessionManager->GetConfig()->GetSetting("server_name"), lpszPseudoUrl + 9) == 0;
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getServerDetails, lpsResponse->er, struct mv_string8 szaSvrNameList, unsigned int ulFlags, struct getServerDetailsResponse* lpsResponse)
{
	serverdetails_t	sDetails;
	std::string		strServerPath;
	std::string		strPublicServer;
	objectdetails_t	details;

	if (!lpecSession->GetSessionManager()->IsDistributedSupported())
		/**
		 * We want to pretend the method doesn't exist. The best way to do that is to return
		 * SOAP_NO_METHOD. But that doesn't fit well in the macro famework. And since the
		 * client would translate that to a KCERR_NETWORK_ERROR anyway, we'll just return
		 * that instead.
		 **/
		return KCERR_NETWORK_ERROR;

	// lookup server which contains the public
	auto usrmgt = lpecSession->GetUserManagement();
	if (usrmgt->GetPublicStoreDetails(&details) == erSuccess)
		strPublicServer = details.GetPropString(OB_PROP_S_SERVERNAME);
	if (ulFlags & ~(EC_SERVERDETAIL_NO_NAME | EC_SERVERDETAIL_FILEPATH |
	    EC_SERVERDETAIL_HTTPPATH | EC_SERVERDETAIL_SSLPATH |
	    EC_SERVERDETAIL_PREFEREDPATH))
		return KCERR_UNKNOWN_FLAGS;
	if (ulFlags == 0)
		ulFlags = EC_SERVERDETAIL_FILEPATH|EC_SERVERDETAIL_HTTPPATH|EC_SERVERDETAIL_SSLPATH	|EC_SERVERDETAIL_PREFEREDPATH;
	
	if (szaSvrNameList.__size > 0 && szaSvrNameList.__ptr != NULL) {
		lpsResponse->sServerList.__size = szaSvrNameList.__size;
		lpsResponse->sServerList.__ptr = s_alloc<struct server>(soap, szaSvrNameList.__size);
		memset(lpsResponse->sServerList.__ptr, 0, szaSvrNameList.__size * sizeof *lpsResponse->sServerList.__ptr);
		
		for (gsoap_size_t i = 0; i < szaSvrNameList.__size; ++i) {
			er = usrmgt->GetServerDetails(STRIN_FIX(szaSvrNameList.__ptr[i]), &sDetails);
			if (er != erSuccess)
				return er;
			if (strcasecmp(sDetails.GetServerName().c_str(), g_lpSessionManager->GetConfig()->GetSetting("server_name")) == 0)
				lpsResponse->sServerList.__ptr[i].ulFlags |= EC_SDFLAG_IS_PEER;

			// note: "contains a public of a company" is also a possibility
			if (!strPublicServer.empty() && strcasecmp(sDetails.GetServerName().c_str(), strPublicServer.c_str()) == 0)
				lpsResponse->sServerList.__ptr[i].ulFlags |= EC_SDFLAG_HAS_PUBLIC;
			if (!(ulFlags & EC_SERVERDETAIL_NO_NAME))
				lpsResponse->sServerList.__ptr[i].lpszName = STROUT_FIX_CPY(sDetails.GetServerName().c_str());
			if (ulFlags & EC_SERVERDETAIL_FILEPATH)
				lpsResponse->sServerList.__ptr[i].lpszFilePath = STROUT_FIX_CPY(sDetails.GetFilePath().c_str());
			if (ulFlags & EC_SERVERDETAIL_HTTPPATH)
				lpsResponse->sServerList.__ptr[i].lpszHttpPath = STROUT_FIX_CPY(sDetails.GetHttpPath().c_str());
			if (ulFlags & EC_SERVERDETAIL_SSLPATH)
				lpsResponse->sServerList.__ptr[i].lpszSslPath = STROUT_FIX_CPY(sDetails.GetSslPath().c_str());
			if (ulFlags & EC_SERVERDETAIL_PREFEREDPATH &&
			    GetBestServerPath(soap, lpecSession, sDetails.GetServerName(), &strServerPath) == erSuccess)
				lpsResponse->sServerList.__ptr[i].lpszPreferedPath = STROUT_FIX_CPY(strServerPath.c_str());
		}
	}
	return erSuccess;
}
SOAP_ENTRY_END()

// legacy calls required for 6.30 clients
int ns__getServerBehavior(struct soap *, ULONG64,
    struct getServerBehaviorResponse *r)
{ 
	r->ulBehavior = 1;
	r->er = 0;
	return SOAP_OK;
} 
 
int ns__setServerBehavior(struct soap *, ULONG64, unsigned int, unsigned int *r)
{ 
	*r = KCERR_NO_SUPPORT;
	return SOAP_OK;
} 

typedef ECDeferredFunc<ECRESULT, ECRESULT(*)(void*), void*> task_type;
struct MTOMStreamInfo;

struct MTOMSessionInfo {
	ECSession		*lpecSession;
	ECDatabase		*lpSharedDatabase;
	ECDatabase		*lpDatabase;
	ECAttachmentStorage *lpAttachmentStorage;
	ECRESULT er;
	ECThreadPool	*lpThreadPool;
	MTOMStreamInfo	*lpCurrentWriteStream; /* This is only tracked for cleanup at session exit */
	MTOMStreamInfo	*lpCurrentReadStream; /* This is only tracked for cleanup at session exit */
};

struct MTOMStreamInfo {
	ECFifoBuffer	*lpFifoBuffer;
	unsigned int	ulObjectId;
	unsigned int	ulStoreId;
	bool			bNewItem;
	unsigned long long ullIMAP;
	GUID			sGuid;
    ULONG			ulFlags;
	task_type 		*lpTask;
	struct propValArray *lpPropValArray;
	MTOMSessionInfo *lpSessionInfo;
};

typedef MTOMStreamInfo * LPMTOMStreamInfo;

static ECRESULT SerializeObject(void *arg)
{
	ECRESULT            er = erSuccess;
	LPMTOMStreamInfo	lpStreamInfo = NULL;
	ECSerializer		*lpSink = NULL;

	lpStreamInfo = (LPMTOMStreamInfo)arg;
	assert(lpStreamInfo != NULL);
	lpStreamInfo->lpSessionInfo->lpSharedDatabase->ThreadInit();

	lpSink = new ECFifoSerializer(lpStreamInfo->lpFifoBuffer, ECFifoSerializer::serialize);
	er = SerializeMessage(lpStreamInfo->lpSessionInfo->lpecSession, lpStreamInfo->lpSessionInfo->lpSharedDatabase, lpStreamInfo->lpSessionInfo->lpAttachmentStorage, NULL, lpStreamInfo->ulObjectId, MAPI_MESSAGE, lpStreamInfo->ulStoreId, &lpStreamInfo->sGuid, lpStreamInfo->ulFlags, lpSink, true);

	delete lpSink;

	lpStreamInfo->lpSessionInfo->lpSharedDatabase->ThreadEnd();

	lpStreamInfo->lpSessionInfo->er = er;

	return er;
}

static void *MTOMReadOpen(struct soap *soap, void *handle, const char *id,
    const char* /*type*/, const char* /*options*/)
{
	LPMTOMStreamInfo	lpStreamInfo = NULL;

	lpStreamInfo = (LPMTOMStreamInfo)handle;
	assert(lpStreamInfo != NULL);
	if (lpStreamInfo->lpSessionInfo->er != erSuccess) {
		soap->error = SOAP_FATAL_ERROR;
		return NULL;
	}

	lpStreamInfo->lpFifoBuffer = new ECFifoBuffer();

	if (strncmp(id, "emcas-", 6) == 0) {
		std::unique_ptr<task_type> ptrTask(new task_type(SerializeObject, lpStreamInfo));
		if (ptrTask->dispatchOn(lpStreamInfo->lpSessionInfo->lpThreadPool) == false) {
			ec_log_err("Failed to dispatch serialization task for \"%s\"", id);
			soap->error = SOAP_FATAL_ERROR;

			delete lpStreamInfo->lpFifoBuffer;
			lpStreamInfo->lpFifoBuffer = NULL;
			return NULL;
		}
		lpStreamInfo->lpTask = ptrTask.release();
	} else {
		ec_log_err("Got stream request for unknown ID \"%s\"", id);
		soap->error = SOAP_FATAL_ERROR;

		delete lpStreamInfo->lpFifoBuffer;
		lpStreamInfo->lpFifoBuffer = NULL;
		return NULL;
	}
	
	lpStreamInfo->lpSessionInfo->lpCurrentReadStream = lpStreamInfo; // Track currently opened stream info
	
	return (void *)lpStreamInfo;
}

static size_t MTOMRead(struct soap * /*soap*/, void *handle,
    char *buf, size_t len)
{
	ECRESULT			er = erSuccess;
	LPMTOMStreamInfo		lpStreamInfo = (LPMTOMStreamInfo)handle;
	ECFifoBuffer::size_type	cbRead = 0;
	
	assert(lpStreamInfo->lpFifoBuffer != NULL);
	er = lpStreamInfo->lpFifoBuffer->Read(buf, len, STR_DEF_TIMEOUT, &cbRead);
	if (er != erSuccess)
		ec_log_err("Failed to read data. er=%s", stringify(er).c_str());
	return cbRead;
}

static void MTOMReadClose(struct soap *soap, void *handle)
{ 
	LPMTOMStreamInfo		lpStreamInfo = (LPMTOMStreamInfo)handle;
	
	assert(lpStreamInfo->lpFifoBuffer != NULL);
	lpStreamInfo->lpSessionInfo->lpCurrentReadStream = NULL; // Cleanup done

	// We get here when the last call to MTOMRead returned 0 OR when
	// an error occurred within gSOAP's bowels. In the latter case, we need
	// to close the FIFO to make sure the writing thread will not lock up.
	// Since gSOAP will not be reading from the FIFO in any case once we
	// read this point, it is safe to just close the FIFO.
	lpStreamInfo->lpFifoBuffer->Close(ECFifoBuffer::cfRead);
	if (lpStreamInfo->lpTask) {
		lpStreamInfo->lpTask->wait();	 // Todo: use result() to wait and get result
		delete lpStreamInfo->lpTask;
	}
	delete lpStreamInfo->lpFifoBuffer;
	lpStreamInfo->lpFifoBuffer = NULL;
}

static void MTOMWriteClose(struct soap *soap, void *handle);

static void MTOMSessionDone(struct soap *soap, void *param)
{
	auto lpInfo = static_cast<MTOMSessionInfo *>(param);

	if (lpInfo->lpCurrentWriteStream != NULL)
	    // Apparently a write stream was opened but not closed by gSOAP by calling MTOMWriteClose. Do it now.
	    MTOMWriteClose(soap, lpInfo->lpCurrentWriteStream);
	else if (lpInfo->lpCurrentReadStream != NULL)
        // Same but for MTOMReadClose()
		MTOMReadClose(soap, lpInfo->lpCurrentReadStream);

    // We can now safely remove sessions, etc since nobody is using them.
	lpInfo->lpAttachmentStorage->Release();
	lpInfo->lpecSession->Unlock();
	
	delete lpInfo->lpSharedDatabase;
	delete lpInfo->lpThreadPool;
	delete lpInfo;
}

SOAP_ENTRY_START(exportMessageChangesAsStream, lpsResponse->er, unsigned int ulFlags, struct propTagArray sPropTags, struct sourceKeyPairArray sSourceKeyPairs, unsigned int ulPropTag, exportMessageChangesAsStreamResponse *lpsResponse)
{
	LPMTOMStreamInfo	lpStreamInfo = NULL;
	object_ptr<ECAttachmentStorage> lpAttachmentStorage;
	unsigned int		ulObjectId = 0;
	unsigned int		ulParentId = 0;
	unsigned int		ulParentCheck = 0;
	unsigned int		ulObjFlags = 0;
	unsigned int		ulStoreId = 0;
	unsigned long		ulObjCnt = 0;
	GUID				sGuid;
	ECObjectTableList	rows;
	struct rowSet		*lpRowSet = NULL; // Do not free, used in response data
	ECODStore			ecODStore;
	ECDatabase 			*lpBatchDB;
	unsigned int		ulDepth = 20;
	unsigned int		ulMode = 0;
	MTOMSessionInfo		*lpMTOMSessionInfo = NULL;
	bool				bUseSQLMulti = parseBool(g_lpSessionManager->GetConfig()->GetSetting("enable_sql_procedures"));

	// Backward compat, old clients do not send ulPropTag
	if(!ulPropTag)
	    ulPropTag = PR_SOURCE_KEY;
	
	if(ulFlags & SYNC_BEST_BODY)
	  ulMode = 1;
	else if(ulFlags & SYNC_LIMITED_IMESSAGE)
	  ulMode = 2;

	auto gcache = g_lpSessionManager->GetCacheManager();	
	auto sec = lpecSession->GetSecurity();
	USE_DATABASE();

	if(ulPropTag == PR_ENTRYID) {
		std::set<EntryId>	setEntryIDs;

	for (gsoap_size_t i = 0; i < sSourceKeyPairs.__size; ++i)
			setEntryIDs.emplace(sSourceKeyPairs.__ptr[i].sObjectKey);
    	er = BeginLockFolders(lpDatabase, setEntryIDs, LOCK_SHARED);
	} else if (ulPropTag == PR_SOURCE_KEY) {
		std::set<SOURCEKEY> setParentSourcekeys;

	for (gsoap_size_t i = 0; i < sSourceKeyPairs.__size; ++i)
			setParentSourcekeys.emplace(sSourceKeyPairs.__ptr[i].sParentKey);
    	er = BeginLockFolders(lpDatabase, setParentSourcekeys, LOCK_SHARED);
    } else
        er = KCERR_INVALID_PARAMETER;
    
    if (er == KCERR_NOT_FOUND) {
		// BeginLockFolders returns KCERR_NOT_FOUND when there's no
		// folder to lock, which can only happen if none of the passed
		// objects exist.
		// This is not an error as that's perfectly valid when performing
		// a selective export. So we'll just return an empty batch, which
		// will be interpreted by the caller as 'all message are deleted'.
		er = erSuccess;
		goto exit;
	} else if (er != erSuccess)
		goto exit;

	ulDepth = atoui(lpecSession->GetSessionManager()->GetConfig()->GetSetting("embedded_attachment_limit")) + 1;
	er = lpecSession->GetAdditionalDatabase(&lpBatchDB);
	if (er != erSuccess)
	    goto exit;
	    
	if ((lpecSession->GetCapabilities() & KOPANO_CAP_ENHANCED_ICS) == 0) {
		er = KCERR_NO_SUPPORT;
		goto exit;
	}
	er = CreateAttachmentStorage(lpDatabase, &~lpAttachmentStorage);
	if (er != erSuccess)
		goto exit;

	lpMTOMSessionInfo = new MTOMSessionInfo;
	lpMTOMSessionInfo->lpCurrentWriteStream = NULL;
	lpMTOMSessionInfo->lpCurrentReadStream = NULL;
	lpMTOMSessionInfo->lpAttachmentStorage = lpAttachmentStorage;
	lpMTOMSessionInfo->lpAttachmentStorage->AddRef();
	lpMTOMSessionInfo->lpecSession = lpecSession; // Should be unlocked after MTOM is done
	lpMTOMSessionInfo->lpecSession->Lock();
	lpMTOMSessionInfo->lpSharedDatabase = lpBatchDB;
	lpMTOMSessionInfo->er = erSuccess;
	lpMTOMSessionInfo->lpThreadPool = new ECThreadPool(1);
	soap_info(soap)->fdone = MTOMSessionDone;
	soap_info(soap)->fdoneparam = lpMTOMSessionInfo;
	lpsResponse->sMsgStreams.__ptr = s_alloc<messageStream>(soap, sSourceKeyPairs.__size);

	for (gsoap_size_t i = 0; i < sSourceKeyPairs.__size; ++i) {
		// Progress information
		lpsResponse->sMsgStreams.__ptr[ulObjCnt].ulStep = i;			

		// Find the correct object
		er = gcache->GetObjectFromProp(PROP_ID(ulPropTag), sSourceKeyPairs.__ptr[i].sObjectKey.__size, sSourceKeyPairs.__ptr[i].sObjectKey.__ptr, &ulObjectId);
		if(er != erSuccess) {
		    er = erSuccess;
			continue;
        }
        
        if(sSourceKeyPairs.__ptr[i].sParentKey.__size) {
			er = gcache->GetObjectFromProp(PROP_ID(ulPropTag), sSourceKeyPairs.__ptr[i].sParentKey.__size, sSourceKeyPairs.__ptr[i].sParentKey.__ptr, &ulParentId);
            if(er != erSuccess) {
                er = erSuccess;
				continue;
            }
			er = gcache->GetObject(ulObjectId, &ulParentCheck, nullptr, &ulObjFlags, nullptr);
            if (er != erSuccess) {
                er = erSuccess;
				continue;
            }
            
            if (ulParentId != ulParentCheck) {
                assert(false);
				continue;
            }
        }
        		
		if ((ulObjFlags & MSGFLAG_DELETED) != (ulFlags & MSGFLAG_DELETED))
			continue;
		
		// Check security
		er = sec->CheckPermission(ulObjectId, ecSecurityRead);
		if (er != erSuccess) {
		    er = erSuccess;
			continue;
        }
        
		// Get store
		er = gcache->GetStore(ulObjectId, &ulStoreId, &sGuid);
		if(er != erSuccess) {
		    er = erSuccess;
			continue;
        }
        

		lpStreamInfo = (MTOMStreamInfo *)soap_malloc(soap, sizeof(MTOMStreamInfo));
		lpStreamInfo->ulObjectId = ulObjectId;
		lpStreamInfo->ulStoreId = ulStoreId;
		lpStreamInfo->bNewItem = false;
		lpStreamInfo->ullIMAP = 0;
		lpStreamInfo->sGuid = sGuid;
		lpStreamInfo->ulFlags = ulFlags;
		lpStreamInfo->lpPropValArray = NULL;
		lpStreamInfo->lpTask = NULL;
		lpStreamInfo->lpSessionInfo = lpMTOMSessionInfo;

		if(bUseSQLMulti)
			strQuery += "call StreamObj(" + stringify(ulObjectId) + "," + stringify(ulDepth) + ", " + stringify(ulMode) + ");";
		
		// Setup the MTOM Attachments
		memset(&lpsResponse->sMsgStreams.__ptr[ulObjCnt].sStreamData, 0, sizeof(lpsResponse->sMsgStreams.__ptr[ulObjCnt].sStreamData));
		lpsResponse->sMsgStreams.__ptr[ulObjCnt].sStreamData.xop__Include.__ptr = (unsigned char*)lpStreamInfo;
		lpsResponse->sMsgStreams.__ptr[ulObjCnt].sStreamData.xop__Include.type = s_strcpy(soap, "application/binary");
		lpsResponse->sMsgStreams.__ptr[ulObjCnt].sStreamData.xop__Include.id = s_strcpy(soap, ("emcas-" + stringify(ulObjCnt, false)).c_str());
		++ulObjCnt;
		// Remember the object ID since we need it later
		rows.emplace_back(ulObjectId, 0);
	}
	lpsResponse->sMsgStreams.__size = ulObjCnt;
                    
    // The results of this query will be consumed by the MTOMRead function
    if(!strQuery.empty()) {
        er = lpBatchDB->DoSelectMulti(strQuery);
        if(er != erSuccess)
            goto exit;
    }
                 
    memset(&ecODStore, 0, sizeof(ECODStore));
	ecODStore.ulObjType = MAPI_MESSAGE;
	
	// Get requested properties for all rows
	er = ECStoreObjectTable::QueryRowData(NULL, soap, lpecSession, &rows, &sPropTags, &ecODStore, &lpRowSet, true, true);
	if (er != erSuccess)
	    goto exit;
	    
	assert(lpRowSet->__size == static_cast<gsoap_size_t>(ulObjCnt));
	for (gsoap_size_t i = 0; i < lpRowSet->__size; ++i)
		lpsResponse->sMsgStreams.__ptr[i].sPropVals = lpRowSet->__ptr[i];

	soap->fmimereadopen = &MTOMReadOpen;
	soap->fmimeread = &MTOMRead;
	soap->fmimereadclose = &MTOMReadClose;

	g_lpStatsCollector->Increment(SCN_DATABASE_MROPS, (int)ulObjCnt);
	
exit:
    lpDatabase->Commit();
	if (er != erSuccess)
		// Do not output any streams
		lpsResponse->sMsgStreams.__size = 0;
    soap->mode &= ~SOAP_XML_TREE;
    soap->omode &= ~SOAP_XML_TREE;
}
SOAP_ENTRY_END()

static ECRESULT DeserializeObject(void *arg)
{
	LPMTOMStreamInfo	lpStreamInfo = NULL;
	ECSerializer		*lpSource = NULL;
	ECRESULT			er = erSuccess;

	lpStreamInfo = (LPMTOMStreamInfo)arg;
	assert(lpStreamInfo != NULL);
	lpSource = new ECFifoSerializer(lpStreamInfo->lpFifoBuffer, ECFifoSerializer::deserialize);
	er = DeserializeObject(lpStreamInfo->lpSessionInfo->lpecSession, lpStreamInfo->lpSessionInfo->lpDatabase, lpStreamInfo->lpSessionInfo->lpAttachmentStorage, NULL, lpStreamInfo->ulObjectId, lpStreamInfo->ulStoreId, &lpStreamInfo->sGuid, lpStreamInfo->bNewItem, lpStreamInfo->ullIMAP, lpSource, &lpStreamInfo->lpPropValArray);
	delete lpSource;

	return er;
}

static void *MTOMWriteOpen(struct soap *soap, void *handle,
    const char * /*id*/, const char * /*type*/, const char * /*description*/,
    enum soap_mime_encoding /*encoding*/)
{
	LPMTOMStreamInfo	lpStreamInfo = NULL;
	lpStreamInfo = (LPMTOMStreamInfo)handle;

	// Just return the handle (needed for gsoap to operate properly
	lpStreamInfo->lpFifoBuffer = new ECFifoBuffer();

	std::unique_ptr<task_type> ptrTask(new task_type(DeserializeObject, lpStreamInfo));
	if (ptrTask->dispatchOn(lpStreamInfo->lpSessionInfo->lpThreadPool) == false) {
		ec_log_err("Failed to dispatch deserialization task");
		lpStreamInfo->lpSessionInfo->er = KCERR_UNABLE_TO_COMPLETE;
		soap->error = SOAP_FATAL_ERROR;
		return NULL;
	}
	lpStreamInfo->lpTask = ptrTask.release();
	lpStreamInfo->lpSessionInfo->lpCurrentWriteStream = lpStreamInfo; // Remember that MTOMWriteOpen was called, and that a cleanup is needed
	return handle;
}

static int MTOMWrite(struct soap *soap, void *handle,
    const char *buf, size_t len)
{
	ECRESULT			er = erSuccess;
	LPMTOMStreamInfo	lpStreamInfo = NULL;
	
	lpStreamInfo = (LPMTOMStreamInfo)handle;
	assert(lpStreamInfo != NULL);

	// Only write data if a reader thread is available
	if (lpStreamInfo->lpTask) {
		er = lpStreamInfo->lpFifoBuffer->Write(buf, len, STR_DEF_TIMEOUT, NULL);
		if (er != erSuccess) {
			lpStreamInfo->lpSessionInfo->er = er;
			return SOAP_EOF;
		}
	}

	return SOAP_OK;
}

static void MTOMWriteClose(struct soap *soap, void *handle)
{
	ECRESULT er = erSuccess;

	LPMTOMStreamInfo	lpStreamInfo = NULL;
	lpStreamInfo = (LPMTOMStreamInfo)handle;
	assert(lpStreamInfo != NULL);
	lpStreamInfo->lpSessionInfo->lpCurrentWriteStream = NULL; // Since we are cleaning up ourselves, another cleanup is not necessary

	lpStreamInfo->lpFifoBuffer->Close(ECFifoBuffer::cfWrite);
	if (lpStreamInfo->lpTask) {
		lpStreamInfo->lpTask->wait();	// Todo: use result() to wait and get result
		er = lpStreamInfo->lpTask->result();
		delete lpStreamInfo->lpTask;
	}
	delete lpStreamInfo->lpFifoBuffer;
	lpStreamInfo->lpFifoBuffer = NULL;

	// Signal error to caller
	if(er != erSuccess) {
		lpStreamInfo->lpSessionInfo->er = er;
		soap->error = SOAP_FATAL_ERROR;
	}
}

SOAP_ENTRY_START(importMessageFromStream, *result, unsigned int ulFlags, unsigned int ulSyncId, entryId sFolderEntryId, entryId sEntryId, bool bIsNew, struct propVal *lpsConflictItems, struct xsd__Binary sStreamData, unsigned int *result)
{
	MTOMStreamInfo	*lpsStreamInfo = NULL;
	unsigned int	ulObjectId = 0;
	unsigned int	ulParentId = 0;
	unsigned int	ulParentType = 0;
	unsigned int	ulGrandParentId = 0;
	unsigned int	ulStoreId = 0;
	unsigned int	ulOwner = 0;
	unsigned long long ullIMAP = 0;
	GUID			sGuid = {0};
	SOURCEKEY		sSourceKey;
	SOURCEKEY		sParentSourceKey;
	ECListInt		lObjectList;
	gsoap_size_t nMVItems = 0;
	std::string		strColName;
	std::string		strColData;
	unsigned int	ulAffected = 0;
	unsigned int	ulDeleteFlags = EC_DELETE_ATTACHMENTS | EC_DELETE_RECIPIENTS | EC_DELETE_CONTAINER | EC_DELETE_MESSAGES | EC_DELETE_HARD_DELETE;
	ECListDeleteItems lstDeleteItems;
	ECListDeleteItems lstDeleted;
	object_ptr<ECAttachmentStorage> lpAttachmentStorage;
	MTOMSessionInfo		*lpMTOMSessionInfo = NULL;

	USE_DATABASE();
	kd_trans atx;

	er = CreateAttachmentStorage(lpDatabase, &~lpAttachmentStorage);
	if (er != erSuccess)
		goto exit;

	lpMTOMSessionInfo = new MTOMSessionInfo;
	lpMTOMSessionInfo->lpCurrentWriteStream = NULL;
	lpMTOMSessionInfo->lpCurrentReadStream = NULL;
	lpMTOMSessionInfo->lpAttachmentStorage = lpAttachmentStorage;
	lpMTOMSessionInfo->lpAttachmentStorage->AddRef();
	lpMTOMSessionInfo->lpecSession = lpecSession; // Should be unlocked after MTOM is done
	lpMTOMSessionInfo->lpecSession->Lock();
	lpMTOMSessionInfo->lpDatabase = lpDatabase;
	lpMTOMSessionInfo->lpSharedDatabase = NULL;
	lpMTOMSessionInfo->er = erSuccess;
	lpMTOMSessionInfo->lpThreadPool = new ECThreadPool(1);
	soap_info(soap)->fdone = MTOMSessionDone;
	soap_info(soap)->fdoneparam = lpMTOMSessionInfo;

	atx = lpAttachmentStorage->Begin(er);
	if (er != erSuccess)
		goto exit;
	er = lpDatabase->Begin();
	if (er != erSuccess)
		goto exit;

	// Get the parent object id.
	er = lpecSession->GetObjectFromEntryId(&sFolderEntryId, &ulParentId);
	if (er != erSuccess)
		goto exit;
		
	// Lock the parent folder
	strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid = " + stringify(ulParentId) + " FOR UPDATE";
	er = lpDatabase->DoSelect(strQuery, NULL);
	if (er != erSuccess)
		goto exit;

	if (!bIsNew) {
		// Delete the existing message and recreate it
		er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjectId);
		if (er != erSuccess)
			goto exit;

		// When a message is update the flags are not passed. So obtain the old flags before
		// deleting so we can pass them to CreateObject.
		er = g_lpSessionManager->GetCacheManager()->GetObjectFlags(ulObjectId, &ulFlags);
		if (er != erSuccess)
			goto exit;

		// Get the original IMAP ID
		strQuery = "SELECT val_ulong FROM properties WHERE"
						" hierarchyid=" + stringify(ulObjectId) +
						" and tag=" + stringify(PROP_ID(PR_EC_IMAP_ID)) +
						" and type=" + stringify(PROP_TYPE(PR_EC_IMAP_ID)) +
						" LIMIT 1";
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess)
			goto exit;
		lpDBRow = lpDBResult.fetch_row();
		if (lpDBRow == NULL || lpDBRow[0] == NULL)
		    // Items created in previous versions of Kopano will not have a PR_EC_IMAP_ID. The rule
		    // is that an item has a PR_EC_IMAP_ID that is equal to the hierarchyid in that case.
		    ullIMAP = ulObjectId;
		else
    		// atoui return a unsigned int at best, but since PR_EC_IMAP_ID is a PT_LONG, the same conversion
	    	// will be done when getting the property through MAPI.
			ullIMAP = atoui(lpDBRow[0]);
		
		lObjectList.emplace_back(ulObjectId);

		// Collect recursive parent objects, validate item and check the permissions
		er = ExpandDeletedItems(lpecSession, lpDatabase, &lObjectList, ulDeleteFlags, true, &lstDeleteItems);
		if (er != erSuccess) {
			assert(false);
			goto exit;
		}

		// Delete the items hard
		er = DeleteObjectHard(lpecSession, lpDatabase, lpAttachmentStorage, ulDeleteFlags, lstDeleteItems, true, lstDeleted);
		if (er != erSuccess) {
			assert(false);
			goto exit;
		}

		// Update storesize
		er = DeleteObjectStoreSize(lpecSession, lpDatabase, ulDeleteFlags, lstDeleted);
		if (er != erSuccess) {
			assert(false);
			goto exit;
		}

		// Update cache
		er = DeleteObjectCacheUpdate(lpecSession, ulDeleteFlags, lstDeleted);
		if (er != erSuccess) {
			assert(false);
			goto exit;
		}

	}

	// Create the message
	if (bIsNew) {
		er = CreateObject(lpecSession, lpDatabase, ulParentId, MAPI_FOLDER, MAPI_MESSAGE, ulFlags, &ulObjectId);
		if (er != erSuccess)
			goto exit;
	} else {
		ulOwner = lpecSession->GetSecurity()->GetUserId(ulParentId); // Owner of object is either the current user or the owner of the folder

		// Reinsert the entry in the hierarchy table with the same id so the change notification later doesn't
		// become a add notification because the id is different.
		strQuery = "INSERT INTO hierarchy (id, parent, type, flags, owner) values("+stringify(ulObjectId)+", "+stringify(ulParentId)+", "+stringify(MAPI_MESSAGE)+", "+stringify(ulFlags)+", "+stringify(ulOwner)+")";
		er = lpDatabase->DoInsert(strQuery);
		if(er != erSuccess)
			goto exit;
	}

	// Get store
	er = g_lpSessionManager->GetCacheManager()->GetStore(ulObjectId, &ulStoreId, &sGuid);
	if(er != erSuccess)
		goto exit;

	// Quota check
	er = CheckQuota(lpecSession, ulStoreId);
	if (er != erSuccess)
		goto exit;

	// Map entryId <-> ulObjectId
	er = MapEntryIdToObjectId(lpecSession, lpDatabase, ulObjectId, sEntryId);
	if (er != erSuccess)
		goto exit;

	// Deserialize the streamed message
	soap->fmimewriteopen = &MTOMWriteOpen;
	soap->fmimewrite = &MTOMWrite;
	soap->fmimewriteclose= &MTOMWriteClose;

	// We usually do not pass database objects to other threads. However, since
	// we want to be able to perform a complete rollback we need to pass it
	// to thread that processes the data and puts it in the database.
	lpsStreamInfo = (MTOMStreamInfo *)soap_malloc(soap, sizeof(MTOMStreamInfo));
	lpsStreamInfo->ulObjectId = ulObjectId;
	lpsStreamInfo->ulStoreId = ulStoreId;
	lpsStreamInfo->bNewItem = bIsNew;
	lpsStreamInfo->ullIMAP = ullIMAP;
	lpsStreamInfo->sGuid = sGuid;
	lpsStreamInfo->ulFlags = ulFlags;
	lpsStreamInfo->lpPropValArray = NULL;
	lpsStreamInfo->lpTask = NULL;
	lpsStreamInfo->lpSessionInfo = lpMTOMSessionInfo;

	if (soap_check_mime_attachments(soap)) {
		struct soap_multipart *content;
		
		content = soap_get_mime_attachment(soap, (void*)lpsStreamInfo);
		if (!content) {
			er = (lpMTOMSessionInfo->er ? lpMTOMSessionInfo->er : KCERR_CALL_FAILED);
			goto exit;
		}
		
		// Flush remaining attachments (that shouldn't even be there)
		while (true) {
			content = soap_get_mime_attachment(soap, (void*)lpsStreamInfo);
			if (!content)
				break;
		};
	}

	er = g_lpSessionManager->GetCacheManager()->GetObject(ulParentId, &ulGrandParentId, NULL, NULL, &ulParentType);
	if (er != erSuccess)
		goto exit;

	// pr_source_key magic
	if (ulParentType == MAPI_FOLDER) {
		GetSourceKey(ulObjectId, &sSourceKey);
		GetSourceKey(ulParentId, &sParentSourceKey);

		AddChange(lpecSession, ulSyncId, sSourceKey, sParentSourceKey, bIsNew ? ICS_MESSAGE_NEW : ICS_MESSAGE_CHANGE);
	}

	// Update the folder counts
	er = UpdateFolderCounts(lpDatabase, ulParentId, ulFlags, lpsStreamInfo->lpPropValArray);
	if (er != erSuccess)
		goto exit;

	// Set PR_CONFLICT_ITEMS if available
	if (lpsConflictItems != NULL && lpsConflictItems->ulPropTag == PR_CONFLICT_ITEMS) {

		// Delete to be sure
		strQuery = "DELETE FROM mvproperties WHERE hierarchyid=" + stringify(ulObjectId) + " AND tag=" + stringify(PROP_ID(PR_CONFLICT_ITEMS)) + " AND type=" + stringify(PROP_TYPE(PR_CONFLICT_ITEMS));
		er = lpDatabase->DoDelete(strQuery);
		if (er != erSuccess)
			goto exit;

		nMVItems = GetMVItemCount(lpsConflictItems);
		for (gsoap_size_t i = 0; i < nMVItems; ++i) {
			er = CopySOAPPropValToDatabaseMVPropVal(lpsConflictItems, i, strColName, strColData, lpDatabase);
			if (er != erSuccess)
				goto exit;

			strQuery = "INSERT INTO mvproperties(hierarchyid,orderid,tag,type," + strColName + ") VALUES(" + stringify(ulObjectId) + "," + stringify(i) + "," + stringify(PROP_ID(PR_CONFLICT_ITEMS)) + "," + stringify(PROP_TYPE(PR_CONFLICT_ITEMS)) + "," + strColData + ")";
			er = lpDatabase->DoInsert(strQuery, NULL, &ulAffected);
			if (er != erSuccess)
				goto exit;
			if (ulAffected != 1) {
				er = KCERR_DATABASE_ERROR;
				ec_log_err("importMessageFromStream(): affected row count != 1");
				goto exit;
			}
		}
	}

	// Process MSGFLAG_SUBMIT
	// If the messages was saved by an ICS syncer, then we need to sync the PR_MESSAGE_FLAGS for MSGFLAG_SUBMIT if it
	// was included in the save.
	er = ProcessSubmitFlag(lpDatabase, ulSyncId, ulStoreId, ulObjectId, bIsNew, lpsStreamInfo->lpPropValArray);
	if (er != erSuccess)
		goto exit;

	if (ulParentType == MAPI_FOLDER) {
		er = ECTPropsPurge::NormalizeDeferredUpdates(lpecSession, lpDatabase, ulParentId);
		if (er != erSuccess)
			goto exit;
	}

	er = atx.commit();
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->Commit();
	if (er != erSuccess)
		goto exit;

	// Notification
	CreateNotifications(ulObjectId, MAPI_MESSAGE, ulParentId, ulGrandParentId, bIsNew, lpsStreamInfo->lpPropValArray, NULL);

	g_lpStatsCollector->Increment(SCN_DATABASE_MWOPS);

exit:
	if (lpsStreamInfo != NULL && lpsStreamInfo->lpPropValArray != NULL)
		FreePropValArray(lpsStreamInfo->lpPropValArray, true);
	FreeDeletedItems(&lstDeleteItems);
	ROLLBACK_ON_ERROR();
	if (er != erSuccess) {
		// remove from cache, else we can get sync issue, with missing messages offline
		auto cache = lpecSession->GetSessionManager()->GetCacheManager();
		cache->RemoveIndexData(ulObjectId);
		cache->Update(fnevObjectDeleted, ulObjectId);
	}
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(getChangeInfo, lpsResponse->er, entryId sEntryId, struct getChangeInfoResponse *lpsResponse)
{
	unsigned int	ulObjId = 0;

	USE_DATABASE();

	// Get object
	er = lpecSession->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if (er != erSuccess)
		return er;
	
	// Check security
	er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityRead);
	if (er != erSuccess)
		return er;

	// Get the Change Key
	strQuery = "SELECT val_binary FROM properties "
				"WHERE tag = " + stringify(PROP_ID(PR_CHANGE_KEY)) +
				" AND type = " + stringify(PROP_TYPE(PR_CHANGE_KEY)) +
				" AND hierarchyid = " + stringify(ulObjId) + " LIMIT 1";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	if (lpDBResult.get_num_rows() > 0) {
		lpDBRow = lpDBResult.fetch_row();
		lpDBLen = lpDBResult.fetch_row_lengths();
		lpsResponse->sPropCK.ulPropTag = PR_CHANGE_KEY;
		lpsResponse->sPropCK.__union = SOAP_UNION_propValData_bin;
		lpsResponse->sPropCK.Value.bin = s_alloc<xsd__base64Binary>(soap, 1);
		lpsResponse->sPropCK.Value.bin->__size = lpDBLen[0];
		lpsResponse->sPropCK.Value.bin->__ptr = s_alloc<unsigned char>(soap, lpDBLen[0]);
		memcpy(lpsResponse->sPropCK.Value.bin->__ptr, lpDBRow[0], lpDBLen[0]);
	} else {
		return KCERR_NOT_FOUND;
	}

	// Get the Predecessor Change List
	strQuery = "SELECT val_binary FROM properties "
				"WHERE tag = " + stringify(PROP_ID(PR_PREDECESSOR_CHANGE_LIST)) +
				" AND type = " + stringify(PROP_TYPE(PR_PREDECESSOR_CHANGE_LIST)) +
				" AND hierarchyid = " + stringify(ulObjId) + " LIMIT 1";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	if (lpDBResult.get_num_rows() > 0) {
		lpDBRow = lpDBResult.fetch_row();
		lpDBLen = lpDBResult.fetch_row_lengths();
		lpsResponse->sPropPCL.ulPropTag = PR_PREDECESSOR_CHANGE_LIST;
		lpsResponse->sPropPCL.__union = SOAP_UNION_propValData_bin;
		lpsResponse->sPropPCL.Value.bin = s_alloc<xsd__base64Binary>(soap, 1);
		lpsResponse->sPropPCL.Value.bin->__size = lpDBLen[0];
		lpsResponse->sPropPCL.Value.bin->__ptr = s_alloc<unsigned char>(soap, lpDBLen[0]);
		memcpy(lpsResponse->sPropPCL.Value.bin->__ptr, lpDBRow[0], lpDBLen[0]);
	} else {
		return KCERR_NOT_FOUND;
	}
	return erSuccess;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(purgeDeferredUpdates, lpsResponse->er, struct purgeDeferredUpdatesResponse *lpsResponse)
{
    unsigned int ulFolderId = 0;
    USE_DATABASE();
    
    // Only system-admins may run this
    if (lpecSession->GetSecurity()->GetAdminLevel() < ADMIN_LEVEL_SYSADMIN)
		return KCERR_NO_ACCESS;
    er = ECTPropsPurge::GetLargestFolderId(lpDatabase, &ulFolderId);
    if(er == KCERR_NOT_FOUND) {
        // Nothing to purge
        lpsResponse->ulDeferredRemaining = 0;
		return er;
    }
    
    if(er != erSuccess)
		return er;
    er = ECTPropsPurge::PurgeDeferredTableUpdates(lpDatabase, ulFolderId);
    if(er != erSuccess)
		return er;
	return ECTPropsPurge::GetDeferredCount(lpDatabase, &lpsResponse->ulDeferredRemaining);
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(testPerform, *result, const char *szCommand,
    struct testPerformArgs sPerform, unsigned int *result)
{
    if(parseBool(g_lpSessionManager->GetConfig()->GetSetting("enable_test_protocol")))
        er = TestPerform(soap, lpecSession, szCommand, sPerform.__size, sPerform.__ptr);
    else
        er = KCERR_NO_ACCESS;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(testSet, *result, const char *szVarName, const char *szValue,
    unsigned int *result)
{
    if(parseBool(g_lpSessionManager->GetConfig()->GetSetting("enable_test_protocol")))
        er = TestSet(soap, lpecSession, szVarName, szValue);
    else
        er = KCERR_NO_ACCESS;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(testGet, lpsResponse->er, const char *szVarName,
    struct testGetResponse *lpsResponse)
{
    if(parseBool(g_lpSessionManager->GetConfig()->GetSetting("enable_test_protocol")))
        er = TestGet(soap, lpecSession, szVarName, &lpsResponse->szValue);
    else
        er = KCERR_NO_ACCESS;
}
SOAP_ENTRY_END()

SOAP_ENTRY_START(setLockState, *result, entryId sEntryId, bool bLocked, unsigned int *result)
{
	unsigned int ulObjId = 0;
	unsigned int ulOwner = 0;
	unsigned int ulObjType = 0;

	er = g_lpSessionManager->GetCacheManager()->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if (er != erSuccess)
		return er;
	er = g_lpSessionManager->GetCacheManager()->GetObject(ulObjId, NULL, &ulOwner, NULL, &ulObjType);
	if (er != erSuccess)
		return er;
	if (ulObjType != MAPI_MESSAGE)
		return KCERR_NO_SUPPORT;

	// Do we need to be owner?
	er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityOwner);
	if (er != erSuccess)
		return er;
	if (bLocked) {
		er = lpecSession->LockObject(ulObjId);
		if (er == KCERR_NO_ACCESS)
			er = KCERR_SUBMITTED;
	} else {
		er = lpecSession->UnlockObject(ulObjId);
	}
	return er;
}
SOAP_ENTRY_END()

int ns__getUserClientUpdateStatus(struct soap *, ULONG64, entryId,
    struct userClientUpdateStatusResponse *r)
{
	r->er = KCERR_NO_SUPPORT;
	return SOAP_OK;
}

int ns__removeAllObjects(struct soap *, ULONG64, entryId, unsigned int *r)
{
	*r = KCERR_NO_SUPPORT;
	return SOAP_OK;
}

SOAP_ENTRY_START(resetFolderCount, lpsResponse->er, entryId sEntryId, struct resetFolderCountResponse *lpsResponse)
{
	unsigned int ulObjId = 0;
	unsigned int ulOwner = 0;
	unsigned int ulObjType = 0;

	er = g_lpSessionManager->GetCacheManager()->GetObjectFromEntryId(&sEntryId, &ulObjId);
	if (er != erSuccess)
		return er;
	er = g_lpSessionManager->GetCacheManager()->GetObject(ulObjId, NULL, &ulOwner, NULL, &ulObjType);
	if (er != erSuccess)
		return er;
	if (ulObjType != MAPI_FOLDER)
		return KCERR_INVALID_TYPE;
	er = lpecSession->GetSecurity()->CheckPermission(ulObjId, ecSecurityOwner);
	if (er != erSuccess)
		return er;
	return ResetFolderCount(lpecSession, ulObjId, &lpsResponse->ulUpdates);
}
SOAP_ENTRY_END()

int ns__getClientUpdate(struct soap *, struct clientUpdateInfoRequest,
    struct clientUpdateResponse *r)
{
	r->er = KCERR_NO_SUPPORT;
	return SOAP_OK;
}

int ns__setClientUpdateStatus(struct soap *, struct clientUpdateStatusRequest,
    struct clientUpdateStatusResponse *r)
{
	r->er = KCERR_NO_SUPPORT;
	return SOAP_OK;
}
