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
#include <kopano/kcodes.h>
#include "soapH.h"
#include "ECClientUpdate.h"
#include "ECLicenseClient.h"
#include <kopano/stringutil.h>
#include <kopano/base64.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>

#include <boost/filesystem.hpp>
namespace bfs = boost::filesystem;

#include "SOAPUtils.h"
#include "SOAPHelpers.h"

#include "ECSessionManager.h"
#include "ECDatabase.h"
#include "ECStatsCollector.h"
#include <kopano/boost_compat.h>

/* class and add constructor params? */
extern ECRESULT GetBestServerPath(struct soap *soap, ECSession *lpecSession, const std::string &strServerName, std::string *lpstrServerPath);

extern ECLogger *g_lpLogger;
extern ECConfig *g_lpConfig;
extern ECSessionManager* g_lpSessionManager;
extern ECStatsCollector* g_lpStatsCollector;

static bool GetLatestVersionAtServer(const char *, unsigned int, ClientVersion *);

/*
 * Handles the HTTP GET command from soap, only the client update install may be downloaded.
 *
 * This function can only be called when client_update_enabled is set to yes.
 *
 * @note This function is only use for backward compatibility
 */
int HandleClientUpdate(struct soap *soap) 
{ 
	std::string strPath;

	int nRet = 404;				// default return file not found to soap
	const char *szClientUpdatePath = NULL;
	char *szCurrentVersion = NULL;
	char *szReq = NULL;
	char *szReqEnd = NULL;
	std::string strLicenseRequest;
	std::string strLicenseResponse;

	ECLicenseClient *lpLicenseClient = NULL;
	unsigned int ulLicenseResponse = 0;
	void *lpLicenseResponse = NULL;
	ECRESULT er = erSuccess;
	ClientVersion currentVersion = {0};
	ClientVersion latestVersion = {0};
	std::string strClientMSIName;
	FILE *fd = NULL;

	// Get the server.cfg setting
	szClientUpdatePath = g_lpConfig->GetSetting("client_update_path");

	if (!szClientUpdatePath || szClientUpdatePath[0] == 0) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: The configuration field 'client_update_path' is empty.");
		goto exit;
	}

	// if the version comes as "/autoupdate/6.20.1.1234?licreq", we need to pass the license request
	szReq = strrchr(soap->path, '?');
	if (szReq != NULL) {
		// since we have the ?, that's good enough
		szReq = strstr(soap->buf, "X-License: ");
		if (szReq == NULL) {
			g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Client update: Invalid license request, header not found.");
			goto exit;
		}
		szReq += strlen("X-License: ");
		szReqEnd = strstr(szReq, "\r\n"); // TODO: can be be split over multiple lines?
		if (szReqEnd == NULL) {
			g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Client update: Invalid license request, end of header not found.");
			goto exit;
		}
		strLicenseRequest = base64_decode(std::string(szReq, szReqEnd - szReq));

		lpLicenseClient = new ECLicenseClient(g_lpConfig->GetSetting("license_socket"),  atoui(g_lpConfig->GetSetting("license_timeout")));
		er = lpLicenseClient->Auth((unsigned char*)strLicenseRequest.c_str(), strLicenseRequest.length(), &lpLicenseResponse, &ulLicenseResponse);
		if (er != erSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Client update: Invalid license request, error: 0x%08X.", er);
			goto exit;
		}

		strLicenseResponse = base64_encode(static_cast<const unsigned char *>(lpLicenseResponse), ulLicenseResponse);

		soap->http_content = "binary";
		soap_response(soap, SOAP_FILE);
		nRet = soap_send_raw(soap, strLicenseResponse.c_str(), strLicenseResponse.length());
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Client update: Processing license request.");
		goto exit;
	}

	// the version comes as "/autoupdate/6.20.1.1234", convert it to "6.20.1.1234"
	szCurrentVersion = soap->path + strlen("/autoupdate");
	if (szCurrentVersion[0] == '/')
		++szCurrentVersion;

	if (szCurrentVersion[0] != '\0') {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: The current client version is %s.", szCurrentVersion);
		if (!GetVersionFromString(szCurrentVersion, &currentVersion))
		{
			g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: Failed in getting version from input data.");
			goto exit;
		}
	}

	if (!GetLatestVersionAtServer(szClientUpdatePath, 0, &latestVersion)) {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: No updates found on server.");
		goto exit;
	}

	if (szCurrentVersion[0] != '\0') {
		int res = CompareVersions(currentVersion, latestVersion);
		if (res == 0) {
			g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Client update: Client already has latest version.");
			goto exit;
		} else if (res > 0)	{
			g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Client update: Client has newer version than server.");
			goto exit;
		}
	}

	if (!GetClientMSINameFromVersion(latestVersion, &strClientMSIName)) {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: No suitable version available.");
		goto exit;
	}

	if (ConvertAndValidatePath(szClientUpdatePath, strClientMSIName, &strPath) != true) {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: Error in path conversion and validation.");
		goto exit;
	}

	fd = fopen(strPath.c_str(), "rb");
	if (!fd) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: Path not found %s.", strPath.c_str());
		goto exit;
	}

	g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: Sending client %s new installer %s", soap->host, strClientMSIName.c_str());

	// application/msi-installer ?
	soap->http_content = "binary";
	soap_response(soap, SOAP_FILE);

	while (true) {
		// FIXME: tmpbuf is only 1K, good enough?
		size_t nSize = fread(soap->tmpbuf, 1, sizeof(soap->tmpbuf), fd);

		if (!nSize)
			break;

		if (soap_send_raw(soap, soap->tmpbuf, nSize))
		{
			g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Client update: Error while sending client new installer");
			goto exit;
		}
	}

	nRet = SOAP_OK;

exit:
	free(lpLicenseResponse);
	delete lpLicenseClient;

	if (fd)
		fclose(fd);

	return nRet;
}

bool ConvertAndValidatePath(const char *lpszClientUpdatePath, const std::string &strMSIName, std::string *lpstrDownloadFile)
{
	bool bRet = false;
	size_t nTempLen = 0;
	std::string strFile;
	char cPathSeparator = '/';

	if (lpstrDownloadFile == NULL || lpszClientUpdatePath == NULL)
		goto exit;

	strFile = lpszClientUpdatePath;
	nTempLen = strFile.length();

	// not 100% correct, but good enough
	if (strstr(strFile.c_str(), "/.."))
	{
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Client update: Update path contains invalid .. to previous path.");
		goto exit;
	}

	if (strFile[nTempLen - 1] != cPathSeparator)
		strFile += cPathSeparator;

	strFile += strMSIName;
	*lpstrDownloadFile = strFile;
	bRet = true;

exit:
	return bRet;
}

//<Major>.<Minor>.<Update>.<Build No.>
bool GetVersionFromString(char *szVersion, ClientVersion *lpClientVersion)
{
	ClientVersion cv;
	std::vector<std::string> vParts;

	if (szVersion == NULL)
		return false;

	vParts = tokenize(szVersion, '.');
	if (vParts.size() != 4)
		return false;

	cv.nMajorVersion = atoi(vParts[0].c_str());
	cv.nMinorVersion = atoi(vParts[1].c_str());
	cv.nUpdateNumber = atoi(vParts[2].c_str());
	cv.nBuildNumber = atoi(vParts[3].c_str());

	*lpClientVersion = cv;

	return true;
}
/**
 * Convert MSI version string to client version struct
 *
 * @param[in] szVersion		MSI filename write as <Major>.<Minor>.<Update>-<Build No.>.msi
 * @param[out]				Pointer a struct with client version information
 */
bool GetVersionFromMSIName(const char *szVersion, ClientVersion *lpClientVersion)
{
	ClientVersion cv;
	std::vector<std::string> vParts;

	if (NULL == szVersion)
		return false;

	vParts = tokenize(szVersion, ".-");
	if (vParts.size() != 5)		// 5 because of the .msi at the end
		return false;

	cv.nMajorVersion = atoi(vParts[0].c_str());
	cv.nMinorVersion = atoi(vParts[1].c_str());
	cv.nUpdateNumber = atoi(vParts[2].c_str());
	cv.nBuildNumber = atoi(vParts[3].c_str());

	*lpClientVersion = cv;

	return true;
}

/*
  The return values are following:

  -n - Version1 <  Version2 (client should upgrade)
   0 - Version1 == Version2
   n - Version1 >  Version2
*/
int CompareVersions(ClientVersion Version1, ClientVersion Version2)
{
	if (Version1.nMajorVersion != Version2.nMajorVersion)
		return Version1.nMajorVersion - Version2.nMajorVersion;

	if (Version1.nMinorVersion != Version2.nMinorVersion)
		return Version1.nMinorVersion - Version2.nMinorVersion;

	if (Version1.nUpdateNumber != Version2.nUpdateNumber)
		return Version1.nUpdateNumber - Version2.nUpdateNumber;

	if (Version1.nBuildNumber != Version2.nBuildNumber)
		return Version1.nBuildNumber - Version2.nBuildNumber;

	return 0;
}

//zarafaclient-6.20-1234.msi
//zarafaclient-*.*-*.msi
static bool GetLatestVersionAtServer(const char *szUpdatePath,
    unsigned int ulTrackid, ClientVersion *lpLatestVersion)
{
	ClientVersion tempVersion = {0};
	ClientVersion latestVersion = {0};
	std::string strFileStart = "zarafaclient-";

	bool bRet = false;

	if (szUpdatePath == NULL)
		goto exit;

	try {
		bfs::path updatesdir = szUpdatePath;
		if (!bfs::exists(updatesdir)) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Unable to open client_update_path directory", ulTrackid);
			goto exit;
		}

		bfs::directory_iterator update_last;
		for (bfs::directory_iterator update(updatesdir);
		     update != update_last; ++update) {
			const bfs::file_type file_type = update->status().type();
			if (file_type != bfs::regular_file && file_type != bfs::symlink_file) {
				continue;
			}

			const std::string strFilename = filename_from_path(update->path());
			if (!kc_starts_with(strFilename, strFileStart)) {
				g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Client update: trackid: 0x%08X, Ignoring file %s for client update", ulTrackid, strFilename.c_str());
				continue;
			}

			g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: trackid: 0x%08X, Update Name: %s", ulTrackid, strFilename.c_str());

			const char *pTemp = strFilename.c_str() + strFileStart.length();
			if (!GetVersionFromMSIName(pTemp, &tempVersion))
			{
				g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Client update: trackid: 0x%08X, Failed in getting version from string '%s'", ulTrackid, pTemp);
				continue;
			}

			// first time, latestVersion will be 0, so always older
			if (CompareVersions(latestVersion, tempVersion) < 0) {
				bRet = true;
				latestVersion = tempVersion;
			}
		}
	} catch (const bfs::filesystem_error &e) {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: trackid: 0x%08X, Boost exception during certificate validation: %s", ulTrackid, e.what());
	} catch (const std::exception &e) {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: trackid: 0x%08X, STD exception during certificate validation: %s", ulTrackid, e.what());
	}

	if (bRet)
		*lpLatestVersion = latestVersion;

exit:
	return bRet;
}

/**
 * Convert clientversion struct to string
 */
static bool VersionToString(const ClientVersion &clientVersion,
    std::string *lpstrVersion)
{
	char szBuf[255];

	if (lpstrVersion == NULL)
		return false;

	snprintf(szBuf, 255, "%d.%d.%d-%d", clientVersion.nMajorVersion, clientVersion.nMinorVersion, clientVersion.nUpdateNumber, clientVersion.nBuildNumber);

	lpstrVersion->assign(szBuf);

	return true;
}

/**
 * Convert clientversion struct to MSI file name
 */
bool GetClientMSINameFromVersion(const ClientVersion &clientVersion, std::string *lpstrMSIName)
{
	char szMSIName[MAX_PATH];

	if (lpstrMSIName == NULL)
		return false;

	snprintf(szMSIName, MAX_PATH, "zarafaclient-%d.%d.%d-%d.msi", clientVersion.nMajorVersion, clientVersion.nMinorVersion, clientVersion.nUpdateNumber, clientVersion.nBuildNumber);

	lpstrMSIName->assign(szMSIName);

	return true;
}

int ns__getClientUpdate(struct soap *soap, struct clientUpdateInfoRequest sClientUpdateInfo, struct clientUpdateResponse* lpsResponse)
{
	unsigned int er = erSuccess;
	ClientVersion sCurrentVersion = {0};
	ClientVersion sLatestVersion;
	unsigned int ulLicenseResponse = 0;
	void *lpLicenseResponse = NULL;
	ECLicenseClient *lpLicenseClient = NULL;
	std::string strClientMSIName;
	std::string strPath;
	FILE *fd = NULL;
	int res;
	unsigned int ulUserID = 0;
	ECSession *lpecSession = NULL;
	ECDatabase *lpDatabase = NULL;
	std::string strCurVersion;
	std::string strLatestVersion;
	std::string strQuery;
	time_t	tNow = 0;

	const char *lpszClientUpdatePath = g_lpConfig->GetSetting("client_update_path");
	unsigned int ulLogLevel = atoui(g_lpConfig->GetSetting("client_update_log_level"));

	if (!parseBool(g_lpConfig->GetSetting("client_update_enabled"))) {
		// do not set on high loglevel, since by default the client updater is installed, and this will be quite often in your log
		g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "Client update: trackid: 0x%08X, Config option 'client_update_enabled' has disabled this feature.", sClientUpdateInfo.ulTrackId);
		er = KCERR_NO_SUPPORT;
		goto exit;
	}

	// setup soap
	soap_set_imode(soap, SOAP_IO_KEEPALIVE | SOAP_C_UTFSTRING | SOAP_ENC_ZLIB | SOAP_ENC_MTOM);
	soap_set_omode(soap, SOAP_IO_KEEPALIVE | SOAP_C_UTFSTRING | SOAP_ENC_ZLIB | SOAP_ENC_MTOM | SOAP_IO_CHUNK);

	if (!lpszClientUpdatePath || lpszClientUpdatePath[0] == 0) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, The configuration field 'client_update_path' is empty.", sClientUpdateInfo.ulTrackId);
		er = KCERR_NO_ACCESS;
		goto exit;
	}

	er = g_lpSessionManager->CreateSessionInternal(&lpecSession);
	if(er != erSuccess)
		goto exit;

	// Lock the session
	lpecSession->Lock();

	er = lpecSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		goto exit;

//@TODO change loglevel?
	g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, computername: %s, username: %s, clientversion: %s, windowsversion: %s, iplist: %s, soapip: %s", 
		sClientUpdateInfo.ulTrackId,
		(sClientUpdateInfo.szComputerName) ? sClientUpdateInfo.szComputerName : "-",
		(sClientUpdateInfo.szUsername) ? sClientUpdateInfo.szUsername : "-",
		(sClientUpdateInfo.szClientVersion) ? sClientUpdateInfo.szClientVersion : "-",
		(sClientUpdateInfo.szWindowsVersion) ? sClientUpdateInfo.szWindowsVersion : "-",
		(sClientUpdateInfo.szClientIPList) ? sClientUpdateInfo.szClientIPList : "-",
		soap->host);

	if (!sClientUpdateInfo.szComputerName)
		sClientUpdateInfo.szComputerName = const_cast<char *>(""); //Client has no name?

	if(!sClientUpdateInfo.szUsername) {
		er = KCERR_NO_ACCESS;
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Client did not send a username", sClientUpdateInfo.ulTrackId);
	}

	// validate user name
	er = lpecSession->GetUserManagement()->SearchObjectAndSync(sClientUpdateInfo.szUsername, 0x01/*EMS_AB_ADDRESS_LOOKUP*/, &ulUserID);
	if (er != erSuccess) {
		er = KCERR_NO_ACCESS;
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, unknown username '%s'", sClientUpdateInfo.ulTrackId, sClientUpdateInfo.szUsername);
	}

	if(lpecSession->GetUserManagement()->IsInternalObject(ulUserID)) {
		er = KCERR_NO_ACCESS;
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Wrong user data. User name '%s' is a reserved user", sClientUpdateInfo.ulTrackId, sClientUpdateInfo.szUsername);
		goto exit;
	}

	// Check if the user connect to the right server, else redirect
	if (lpecSession->GetSessionManager()->IsDistributedSupported() ) 
	{
		objectdetails_t sUserDetails;

		er = lpecSession->GetUserManagement()->GetObjectDetails(ulUserID, &sUserDetails);
		if (er != erSuccess)
			goto exit;

		/* Check if this is the correct server */
		string strServerName = sUserDetails.GetPropString(OB_PROP_S_SERVERNAME);
		if (strServerName.empty()) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, User '%s' has no default server", sClientUpdateInfo.ulTrackId, sClientUpdateInfo.szUsername);
			er = KCERR_NO_ACCESS;
			goto exit;
		}

		if (strcasecmp(strServerName.c_str(), g_lpSessionManager->GetConfig()->GetSetting("server_name")) != 0) {
			string	strServerPath;

			er = GetBestServerPath(soap, lpecSession, strServerName, &strServerPath);
			if (er != erSuccess)
				goto exit;

			lpsResponse->lpszServerPath = s_strcpy(soap, strServerPath.c_str());// Server Path must always utf8 (also in 6.40.x)
			ec_log_info("Client update: trackid: 0x%08X, User \"%s\" is redirected to \"%s\"", sClientUpdateInfo.ulTrackId, sClientUpdateInfo.szUsername, lpsResponse->lpszServerPath);
			g_lpStatsCollector->Increment(SCN_REDIRECT_COUNT, 1);
			er = KCERR_UNABLE_TO_COMPLETE;
			goto exit;
		}
	}

	lpLicenseClient = new ECLicenseClient(g_lpConfig->GetSetting("license_socket"),  atoui(g_lpConfig->GetSetting("license_timeout")));
	er = lpLicenseClient->Auth(sClientUpdateInfo.sLicenseReq.__ptr, sClientUpdateInfo.sLicenseReq.__size, &lpLicenseResponse, &ulLicenseResponse);
	if (er != erSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Invalid license request, error: 0x%08X.", sClientUpdateInfo.ulTrackId, er);
		goto exit;
	}

	if (sClientUpdateInfo.szClientVersion == NULL || sClientUpdateInfo.szClientVersion[0] == '\0') {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: trackid: 0x%08X, The client did not sent the current version number.", sClientUpdateInfo.ulTrackId);
	} else if (!GetVersionFromString(sClientUpdateInfo.szClientVersion, &sCurrentVersion)) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Failed in getting version from input data.", sClientUpdateInfo.ulTrackId);
		goto exit; //@fixme can we give the latest?
	}

	if (!GetLatestVersionAtServer(lpszClientUpdatePath, sClientUpdateInfo.ulTrackId, &sLatestVersion)) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, No updates found on server.", sClientUpdateInfo.ulTrackId);
		er = KCERR_NO_ACCESS;
		goto exit;
	}

	res = CompareVersions(sCurrentVersion, sLatestVersion);
	if (res == 0) {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: trackid: 0x%08X, Client already has the latest version.", sClientUpdateInfo.ulTrackId);
		goto ok;
	} else if (res > 0) {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: trackid: 0x%08X, Client has newer version than server.", sClientUpdateInfo.ulTrackId);
		goto ok;
	}

	if (!GetClientMSINameFromVersion(sLatestVersion, &strClientMSIName)) {
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Client update: trackid: 0x%08X, No suitable version available.", sClientUpdateInfo.ulTrackId);
		er = KCERR_NO_ACCESS;
		goto exit;
	}

	if (ConvertAndValidatePath(lpszClientUpdatePath, strClientMSIName, &strPath) != true) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Error in path conversion and validation.", sClientUpdateInfo.ulTrackId);
		er = KCERR_NO_ACCESS;
		goto exit;
	}

	fd = fopen(strPath.c_str(), "rb");
	if (!fd) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Path not found %s.", sClientUpdateInfo.ulTrackId, strPath.c_str());
		er = KCERR_NO_ACCESS;
		goto exit;
	}

	// Update auto update client status
	VersionToString(sCurrentVersion, &strCurVersion);
	VersionToString(sLatestVersion, &strLatestVersion);

	tNow = time(NULL); // Get current time

	strQuery = "REPLACE INTO clientupdatestatus(userid, trackid, updatetime, currentversion, latestversion, computername, status) VALUES ("+
				stringify(ulUserID)+", "+stringify(sClientUpdateInfo.ulTrackId)+", FROM_UNIXTIME("+
				stringify(tNow) + "), \"" + strCurVersion + "\", \"" + strLatestVersion + "\", \"" +
				lpDatabase->Escape(sClientUpdateInfo.szComputerName).c_str()+"\", "+ stringify(UPDATE_STATUS_PENDING) + ")";

	// ignore error in database tracking, SQL error logged in server, still send new client
	lpDatabase->DoUpdate(strQuery);

	soap->fmimereadopen = &mime_file_read_open;
	soap->fmimeread = &mime_file_read;
	soap->fmimereadclose = &mime_file_read_close;

	// Setup the MTOM Attachments
	lpsResponse->sStreamData.xop__Include.__ptr = (unsigned char*)fd;
	lpsResponse->sStreamData.xop__Include.__size = 0;
	lpsResponse->sStreamData.xop__Include.type = s_strcpy(soap, "application/binary");
	lpsResponse->sStreamData.xop__Include.id = s_strcpy(soap, "zarafaclient");

	g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Sending new installer %s", sClientUpdateInfo.ulTrackId, strClientMSIName.c_str());

ok: // Client is already up to date
	lpsResponse->sLicenseResponse.__size = ulLicenseResponse;
	lpsResponse->sLicenseResponse.__ptr = (unsigned char *)s_memcpy(soap, (const char *)lpLicenseResponse, ulLicenseResponse);
	
	lpsResponse->ulLogLevel = ulLogLevel; // 0 = none, 1 = on errors, 2 = always
	
exit:
	if(lpecSession) {
		lpecSession->Unlock(); 
		g_lpSessionManager->RemoveSessionInternal(lpecSession);
	}

	lpsResponse->er = er;
	free(lpLicenseResponse);
	delete lpLicenseClient;

	if (er && fd)
		fclose(fd);

	soap->mode &= ~SOAP_XML_TREE;
	soap->omode &= ~SOAP_XML_TREE;

	return SOAP_OK;
}

int ns__setClientUpdateStatus(struct soap *soap, struct clientUpdateStatusRequest sClientUpdateStatus, struct clientUpdateStatusResponse* lpsResponse)
{
	unsigned int er = erSuccess;
	ECSession   *lpecSession = NULL;
	ECDatabase  *lpDatabase = NULL;
	std::string strQuery;

	const char *lpszClientUpdatePath = g_lpConfig->GetSetting("client_update_path");
	const char *lpszLogPath = g_lpConfig->GetSetting("client_update_log_path");

	if (!parseBool(g_lpConfig->GetSetting("client_update_enabled"))) {
		er = KCERR_NO_SUPPORT;
		goto exit;
	}

	if (!lpszClientUpdatePath || lpszClientUpdatePath[0] == 0) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, The configuration field 'client_update_path' is empty.", sClientUpdateStatus.ulTrackId);
		er = KCERR_NO_ACCESS;
		goto exit;
	}

	if (!lpszLogPath || lpszLogPath[0] == 0) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, The configuration field 'client_update_log_path' is empty.", sClientUpdateStatus.ulTrackId);
		er = KCERR_NO_ACCESS;
		goto exit;
	}

	er = g_lpSessionManager->CreateSessionInternal(&lpecSession);
	if(er != erSuccess)
		goto exit;

	// Lock the session
	lpecSession->Lock();

	er = lpecSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		goto exit;

	soap->fmimewriteopen = mime_file_write_open;
	soap->fmimewriteclose = mime_file_write_close;
	soap->fmimewrite = mime_file_write;

	if (sClientUpdateStatus.ulLastErrorCode){
		//@fixme if we know errors we can add a user friendly error message
		if( sClientUpdateStatus.ulLastErrorAction == 2 /*ACTION_VALIDATE_CLIENT*/)
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Installation failed, can not validate MSI file, error code 0x%08X", sClientUpdateStatus.ulTrackId, sClientUpdateStatus.ulLastErrorCode);
		else if (sClientUpdateStatus.ulLastErrorAction == 3 /*ACTION_INSTALL_CLIENT*/)
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Installation failed, installer returned error code 0x%08X", sClientUpdateStatus.ulTrackId, sClientUpdateStatus.ulLastErrorCode);
		else
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Installation failed, error code 0x%08X", sClientUpdateStatus.ulTrackId, sClientUpdateStatus.ulLastErrorCode);
	} else {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Installed successfully updated", sClientUpdateStatus.ulTrackId);
	}

	if (soap_check_mime_attachments(soap)) {
		// attachments are present, channel is still open
		struct soap_multipart *content;
		std::string strFile;
		std::string strFilePath;
		
		// Path to store the log files
		strFilePath = lpszLogPath;
		strFilePath+= "/";
		strFilePath+= stringify(sClientUpdateStatus.ulTrackId, true) + "/";

		if(CreatePath((char *)strFilePath.c_str()) != 0) {
			g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Client update: trackid: 0x%08X, Unable to create directory '%s'!", sClientUpdateStatus.ulTrackId, strFilePath.c_str());
			er = KCERR_NO_ACCESS;
			goto exit;
		}

		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Client update: trackid: 0x%08X, Log files saved in '%s'", sClientUpdateStatus.ulTrackId, strFilePath.c_str());

		gsoap_size_t ulFile = 0;
		while (true) {

			if (ulFile >= sClientUpdateStatus.sFiles.__size)
				break;

			strFile = strFilePath;
			// Check if this not a hack from someone!
			if(sClientUpdateStatus.sFiles.__ptr[ulFile].lpszAttachmentName && strstr(sClientUpdateStatus.sFiles.__ptr[ulFile].lpszAttachmentName, "..") == NULL)
				strFile += sClientUpdateStatus.sFiles.__ptr[ulFile].lpszAttachmentName;
			else
				strFile += stringify(rand_mt()) + ".tmp";

			content = soap_get_mime_attachment(soap, (void*)strFile.c_str());
			if (!content)
				break;
			++ulFile;
		}

		if (soap->error) {
			er = KCERR_NO_ACCESS;
			goto exit;
		}
	}

	strQuery = "UPDATE clientupdatestatus SET status="+stringify((sClientUpdateStatus.ulLastErrorCode)?UPDATE_STATUS_FAILED : UPDATE_STATUS_SUCCESS)+" WHERE trackid=" + stringify(sClientUpdateStatus.ulTrackId);

	er = lpDatabase->DoUpdate(strQuery);
	if (er != erSuccess)
		goto exit;

exit:
	if(lpecSession) {
		lpecSession->Unlock();
		g_lpSessionManager->RemoveSessionInternal(lpecSession);
	}

	lpsResponse->er = er;

	return SOAP_OK;
}
