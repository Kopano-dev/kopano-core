/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <string>
#include <utility>
#include <vector>
#include "Http.h"
#include <kopano/mapi_ptr.h>
#include <kopano/stringutil.h>
#include <kopano/timeutil.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/ECConfig.h>

using namespace KC;

/**
 * Parse the incoming URL into known pieces:
 *
 * /<service>/[username][/foldername][/uid.ics]
 * service: ical or caldav, mandatory
 * username: open store of this user, "public" for public folders. optional: default comes from HTTP Authenticate header.
 * foldername: folder name in store to open. multiple forms possible (normal name, prefix_guid, prefix_entryid).
 *
 * @param[in] strUrl incoming url
 * @param[out] lpulFlag url flags (service type and public marker)
 * @param[out] lpstrUrlUser owner of lpstrFolder
 * @param[out] lpstrFolder folder id or name
 *
 * @return
 */
HRESULT HrParseURL(const std::string &strUrl, ULONG *lpulFlag, std::string *lpstrUrlUser, std::string *lpstrFolder)
{
	HRESULT hr = hrSuccess;
	std::string strService, strFolder, strUrlUser;
	ULONG ulFlag = 0;
	auto vcUrlTokens = tokenize(strUrl, L'/', true);
	decltype(vcUrlTokens)::const_iterator iterToken;
	if (vcUrlTokens.empty())
		// root should be present, no flags are set. mostly used on OPTIONS command
		goto exit;

	if (vcUrlTokens.back().rfind(".ics") != std::string::npos)
		// Guid is retrieved using StripGuid().
		vcUrlTokens.pop_back();
	else
		// request is for folder not a calendar entry
		ulFlag |= REQ_COLLECTION;
	if (vcUrlTokens.empty())
		goto exit;
	if (vcUrlTokens.size() > 3) {
		// sub folders are not allowed
		hr = MAPI_E_TOO_COMPLEX;
		goto exit;
	}

	iterToken = vcUrlTokens.cbegin();
	//change case of Service name ICAL -> ical CALDaV ->caldav
	strService = strToLower(*iterToken++);
	if (!strService.compare("ical"))
		ulFlag |= SERVICE_ICAL;
	else if (!strService.compare("caldav"))
		ulFlag |= SERVICE_CALDAV;
	else
		ulFlag |= SERVICE_UNKNOWN;
	if (iterToken == vcUrlTokens.cend())
		goto exit;

	//change case of folder owner USER -> user, UseR -> user
	strUrlUser = strToLower(*iterToken++);
	// check if the request is for public folders and set the bool flag
	// @note: request for public folder not have user's name in the url
	if (!strUrlUser.compare("public"))
		ulFlag |= REQ_PUBLIC;
	if (iterToken == vcUrlTokens.cend())
		goto exit;
	// @todo subfolder/folder/ is not allowed! only subfolder/item.ics
	for (; iterToken != vcUrlTokens.end(); ++iterToken)
		strFolder = strFolder + *iterToken + "/";
	strFolder.erase(strFolder.length() - 1);
exit:
	if (lpulFlag)
		*lpulFlag = ulFlag;
	if (lpstrUrlUser)
		*lpstrUrlUser = std::move(strUrlUser);
	if (lpstrFolder)
		*lpstrFolder = std::move(strFolder);
	return hr;
}

Http::Http(ECChannel *lpChannel, std::shared_ptr<ECConfig> lpConfig) :
	m_lpChannel(lpChannel), m_lpConfig(std::move(lpConfig))
{}

/**
 * Reads the http headers from the channel and Parses them
 *
 * @return	HRESULT
 * @retval	MAPI_E_INVALID_PARAMETER	The http hearders are invalid
 */
HRESULT Http::HrReadHeaders()
{
	HRESULT hr;
	std::string strBuffer;
	ULONG n = 0;
	std::map<std::string, std::string>::iterator iHeader = mapHeaders.end();

	ec_log_debug("Receiving headers:");
	do
	{
		hr = m_lpChannel->HrReadLine(strBuffer);
		if (hr != hrSuccess)
			return hr;
		if (strBuffer.empty())
			break;

		if (n == 0) {
			m_strAction = strBuffer;
		} else {
			std::string::size_type pos = strBuffer.find(':');
			std::string::size_type start = 0;

			if (strBuffer[0] == ' ' || strBuffer[0] == '\t') {
				if (iHeader == mapHeaders.end())
					continue;
				// continue header
				while (strBuffer[start] == ' ' || strBuffer[start] == '\t')
					++start;
				iHeader->second += strBuffer.substr(start);
			} else {
				// new header
				auto r = mapHeaders.emplace(strBuffer.substr(0, pos), strBuffer.substr(pos + 2));
				iHeader = r.first;
			}
		}

		if (strBuffer.find("Authorization") != std::string::npos)
			ec_log_debug("< Authorization: <value hidden>");
		else
			ec_log_debug("< "+strBuffer);
		++n;
	} while(hr == hrSuccess);

	hr = HrParseHeaders();
	if (hr != hrSuccess)
		ec_log_debug("parsing headers failed: %s (%x)", GetMAPIErrorMessage(hr), hr);
	return hr;
}

/* deduplicate in master */
/**
 * Parse the http headers
 * @return	HRESULT
 * @retval	MAPI_E_INVALID_PARAMETER	The http headers are invalid
 */
// @todo this does way too much.
HRESULT Http::HrParseHeaders()
{
	HRESULT hr;
	std::string strAuthdata;
	std::string strUserAgent;
	size_t colon_pos;

	auto items = tokenize(m_strAction, ' ', true);
	if (items.size() != 3) {
		ec_log_debug("HrParseHeaders invalid != 3 tokens");
		return MAPI_E_INVALID_PARAMETER;
	}
	m_strMethod = items[0];
	m_strURL = items[1];
	m_strHttpVer = items[2];
	// converts %20 -> ' '
	m_strPath = urlDecode(m_strURL);

	// find the content-type
	// Content-Type: text/xml;charset=UTF-8
	hr = HrGetHeaderValue("Content-Type", &m_strCharSet);
	if (hr == hrSuccess)
		m_strCharSet = content_type_get_charset(m_strCharSet.c_str(), m_lpConfig->GetSetting("default_charset"));
	else
		m_strCharSet = m_lpConfig->GetSetting("default_charset"); // really should be UTF-8

	hr = HrGetHeaderValue("User-Agent", &strUserAgent);
	if (hr == hrSuccess) {
		size_t space = strUserAgent.find(" ");

		if (space != std::string::npos) {
			m_strUserAgent = strUserAgent.substr(0, space);
			m_strUserAgentVersion = strUserAgent.substr(space + 1);
		}
		else {
			m_strUserAgent = strUserAgent;
		}
	}

	// find the Authorisation data (Authorization: Basic wr8y273yr2y3r87y23ry7=)
	hr = HrGetHeaderValue("Authorization", &strAuthdata);
	if (hr != hrSuccess) {
		hr = HrGetHeaderValue("WWW-Authenticate", &strAuthdata);
		if (hr != hrSuccess)
			return S_OK; /* ignore empty Authorization */
	}

	items = tokenize(strAuthdata, ' ', true);
	// we only support basic authentication
	if (items.size() != 2 || items[0].compare("Basic") != 0) {
		ec_log_debug("HrParseHeaders login failed");
		return MAPI_E_LOGON_FAILED;
	}
	auto user_pass = base64_decode(items[1]);
	if((colon_pos = user_pass.find(":")) == std::string::npos) {
		ec_log_debug("HrParseHeaders password missing");
		return MAPI_E_LOGON_FAILED;
	}

	m_strUser = user_pass.substr(0, colon_pos);
	m_strPass = user_pass.substr(colon_pos+1, std::string::npos);
	return hrSuccess;
}

/**
 * Returns the user name set in the request
 * @param[in]	strUser		Return string for username in request
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	No username set in the request
 */
HRESULT Http::HrGetUser(std::wstring *strUser)
{
	if (m_strUser.empty())
		return MAPI_E_NOT_FOUND;
	return X2W(m_strUser, strUser);
}

/**
 * Returns the method set in the url
 * @param[out]	strMethod	Return string for method set in request
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Empty method in request
 */
HRESULT Http::HrGetMethod(std::string *strMethod)
{
	if (m_strMethod.empty())
		return MAPI_E_NOT_FOUND;
	strMethod->assign(m_strMethod);
	return hrSuccess;
}

/**
 * Returns the password sent by user
 * @param[out]	strPass		The password is returned
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Empty password in request
 */
HRESULT Http::HrGetPass(std::wstring *strPass)
{
	if (m_strPass.empty())
		return MAPI_E_NOT_FOUND;
	return X2W(m_strPass, strPass);
}

/**
 * return the original, non-decoded, url
 *
 * @param strURL us-ascii encoded url
 *
 * @return
 */
HRESULT Http::HrGetRequestUrl(std::string *strURL)
{
	strURL->assign(m_strURL);
	return hrSuccess;
}

/**
 * Returns the full decoded path of request(e.g. /caldav/user name/folder)
 * (e.g. %20 is converted to ' ')
 * @param[out]	strReqPath		Return string for path
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Empty path in request
 */
HRESULT Http::HrGetUrl(std::string *strUrl)
{
	if (m_strPath.empty())
		return MAPI_E_NOT_FOUND;
	strUrl->assign(urlDecode(m_strPath));
	return hrSuccess;
}

/**
 * Returns body of the request
 * @param[in]	strBody		Return string for  body of the request
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	No body present in request
 */
HRESULT Http::HrGetBody(std::string *strBody)
{
	if (m_strReqBody.empty())
		return MAPI_E_NOT_FOUND;
	strBody->assign(m_strReqBody);
	return hrSuccess;
}

/**
 * Returns the Depth set in request
 * @param[out]	ulDepth		Return string for depth set in request
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	No depth value set in request
 */
HRESULT Http::HrGetDepth(ULONG *ulDepth)
{
	std::string strDepth;
	/*
	 * Valid input: [0, 1, infinity]
	 */
	auto hr = HrGetHeaderValue("Depth", &strDepth);
	if (hr != hrSuccess)
		*ulDepth = 0; /* Default is no subfolders. Default should become a parameter. It is action dependent. */
	else if (strDepth.compare("infinity") == 0)
		*ulDepth = 2;
	else {
		*ulDepth = atoi(strDepth.c_str());
		if (*ulDepth > 1)
			*ulDepth = 1;
	}
	return hr;
}

/**
 * Checks the etag of a MAPI object against If-(None)-Match headers
 *
 * @param[in] lpProp Object to check etag (PR_LAST_MODIFICATION_TIME) to
 *
 * @return continue request or return 412
 * @retval true continue request
 * @retval false return 412 to client
 */
bool Http::CheckIfMatch(LPMAPIPROP lpProp)
{
	bool ret = false, invert = false;
	std::string strIf, strValue;
	SPropValuePtr ptrLastModTime;

	if (lpProp != nullptr &&
	    HrGetOneProp(lpProp, PR_LAST_MODIFICATION_TIME, &~ptrLastModTime) == hrSuccess)
		strValue = stringify_int64(FileTimeToUnixTime(ptrLastModTime->Value.ft), false);

	if (HrGetHeaderValue("If-Match", &strIf) == hrSuccess) {
		if (strIf.compare("*") == 0 && !ptrLastModTime)
			// we have an object without a last mod time, not allowed
			return false;
	} else if (HrGetHeaderValue("If-None-Match", &strIf) == hrSuccess) {
		if (strIf.compare("*") == 0 && !!ptrLastModTime)
			// we have an object which has a last mod time, not allowed
			return false;
		invert = true;
	} else {
		return true;
	}

	// check all etags for a match
	for (auto &i : tokenize(strIf, ',', true)) {
		if (i.at(0) == '"' || i.at(0) == '\'')
			i.assign(i.begin() + 1, i.end() - 1);
		if (i.compare(strValue) == 0) {
			ret = true;
			break;
		}
	}
	if (invert)
		ret = !ret;
	return ret;
}

/**
 * Returns Charset of the request
 * @param[out]	strCharset	Return string Charset of the request
 *
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	No charset set in request
 */
HRESULT Http::HrGetCharSet(std::string *strCharset)
{
	if (m_strCharSet.empty())
		return MAPI_E_NOT_FOUND;
	strCharset->assign(m_strCharSet);
	return hrSuccess;
}

/**
 * Returns the Destination value found in the header
 *
 * Specifies the destination of entry in MOVE request,
 * to move mapi message from one folder to another
 * for e.g.
 *
 * Destination: https://kopano.com:8080/caldav/USER/FOLDER-ID/ENTRY-GUID.ics
 *
 * @param[out]	strDestination	Return string destination of the request
 *
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	No destination set in request
 */
HRESULT Http::HrGetDestination(std::string *strDestination)
{
	std::string strHost, strDest;

	// example:  Host: server:port
	auto hr = HrGetHeaderValue("Host", &strHost);
	if(hr != hrSuccess) {
		ec_log_debug("Http::HrGetDestination host header missing");
		return hr;
	}
	// example:  Destination: http://server:port/caldav/username/folderid/entry.ics
	hr = HrGetHeaderValue("Destination", &strDest);
	if (hr != hrSuccess) {
		ec_log_debug("Http::HrGetDestination destination header missing");
		return hr;
	}
	auto pos = strDest.find(strHost);
	if (pos == std::string::npos) {
		ec_log_err("Refusing to move calendar item from %s to different host on url %s", strHost.c_str(), strDest.c_str());
		return MAPI_E_CALL_FAILED;
	}
	strDest.erase(0, pos + strHost.length());
	*strDestination = std::move(strDest);
	return hrSuccess;
}

/**
 * Reads request body from the channel
 * @return		HRESULT
 * @retval		MAPI_E_NOT_FOUND	Empty body
 */
HRESULT Http::HrReadBody()
{
	std::string strLength;

	// find the Content-Length
	if (HrGetHeaderValue("Content-Length", &strLength) != hrSuccess) {
		ec_log_debug("Http::HrReadBody content-length missing");
		return MAPI_E_NOT_FOUND;
	}
	auto ulContLength = atoi(strLength.c_str());
	if (ulContLength <= 0) {
		ec_log_debug("Http::HrReadBody content-length invalid %d", ulContLength);
		return MAPI_E_NOT_FOUND;
	}
	auto hr = m_lpChannel->HrReadBytes(&m_strReqBody, ulContLength);
	if (!m_strUser.empty())
		ec_log_debug("Request body:\n%s", m_strReqBody.c_str());
	return hr;
}

/**
 * Check for errors in the http request
 * @return		HRESULT
 * @retval		MAPI_E_INVALID_PARAMETER	Unsupported http request
 */
HRESULT Http::HrValidateReq()
{
	static const char *const lpszMethods[] = {
		"ACL", "GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS",
		"PROPFIND", "REPORT", "MKCALENDAR", "PROPPATCH", "MOVE", NULL,
	};
	bool bFound = false;

	if (m_strMethod.empty())
		return kc_perror("HTTP request method is empty", MAPI_E_INVALID_PARAMETER);
	if (!parseBool(m_lpConfig->GetSetting("enable_ical_get")) && m_strMethod == "GET") {
		ec_log_err("Denying iCalendar GET since it is disabled");
		return MAPI_E_NO_ACCESS;
	}
	for (unsigned int i = 0; lpszMethods[i] != nullptr; ++i) {
		if (m_strMethod.compare(lpszMethods[i]) == 0) {
			bFound = true;
			break;
		}
	}

	if (!bFound) {
		static const HRESULT hr = MAPI_E_INVALID_PARAMETER;
		ec_log_err("HTTP request \"%s\" not implemented: %s (%x)", m_strMethod.c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	// validate authentication data
	if (m_strUser.empty() || m_strPass.empty())
		// hr still success, since http request is valid
		ec_log_debug("Request missing authorization data");
	return hrSuccess;
}

/**
 * Sets keep-alive time for the http response
 *
 * @param[in]	ulKeepAlive		Numerical value set as keep-alive time of http connection
 * @return		HRESULT			Always set as hrSuccess
 */
HRESULT Http::HrSetKeepAlive(int ulKeepAlive)
{
	m_ulKeepAlive = ulKeepAlive;
	return hrSuccess;
}

/**
 * Flush all headers and body to client(i.e Send all data to client)
 *
 * Sends the data in chunked http if the data is large and client uses http 1.1
 * Example of Chunked http
 * 1A[CRLF]					- size in hex
 * xxxxxxxxxxxx..[CRLF]		- data
 * 1A[CRLF]					- size in hex
 * xxxxxxxxxxxx..[CRLF]		- data
 * 0[CRLF]					- end of response
 * [CRLF]
 *
 * @return	HRESULT
 * @retval	MAPI_E_END_OF_SESSION	States that client as set connection type as closed
 */
HRESULT Http::HrFinalize()
{
	HRESULT hr = hrSuccess;

	HrResponseHeader("Content-Length", stringify(m_strRespBody.length()));

	// force chunked http for long size response, should check version >= 1.1 to disable chunking
	if (m_strRespBody.size() < HTTP_CHUNK_SIZE || m_strHttpVer.compare("1.1") != 0)
	{
		hr = HrFlushHeaders();
		if (hr != hrSuccess && hr != MAPI_E_END_OF_SESSION) {
			ec_log_debug("Http::HrFinalize flush fail %d", hr);
			m_ulRetCode = 0;
			return hr;
		}
		if (!m_strRespBody.empty()) {
			m_lpChannel->HrWriteString(m_strRespBody);
			ec_log_debug("Response body:\n%s", m_strRespBody.c_str());
		}
	}
	else
	{
		const char *lpstrBody = m_strRespBody.data();
		char lpstrLen[10];
		std::string::size_type szBodyLen = m_strRespBody.size();	// length of data to be sent to the client
		std::string::size_type szBodyWritten = 0;					// length of data sent to client
		unsigned int szPart = HTTP_CHUNK_SIZE;						// default lenght of chunk data to be written

		HrResponseHeader("Transfer-Encoding", "chunked");
		hr = HrFlushHeaders();
		if (hr != hrSuccess && hr != MAPI_E_END_OF_SESSION) {
			ec_log_debug("Http::HrFinalize flush fail(2) %d", hr);
			m_ulRetCode = 0;
			return hr;
		}

		while (szBodyWritten < szBodyLen)
		{
			if ((szBodyWritten + HTTP_CHUNK_SIZE) > szBodyLen)
				szPart = szBodyLen - szBodyWritten;				// change length of data for last chunk
			// send hex length of data and data part
			snprintf(lpstrLen, sizeof(lpstrLen), "%X", szPart);
			m_lpChannel->HrWriteLine(lpstrLen);
			m_lpChannel->HrWriteLine((char*)lpstrBody, szPart);
			szBodyWritten += szPart;
			lpstrBody += szPart;
		}

		// end of response
		snprintf(lpstrLen, 10, "0\r\n");
		m_lpChannel->HrWriteLine(lpstrLen);
		// just the first part of the body in the log. header shows it's chunked.
		ec_log_debug("%s", m_strRespBody.c_str());
	}

	// if http_log_enable?
	char szTime[32];
	time_t now = time(NULL);
	tm local;
	std::string strAgent;
	localtime_r(&now, &local);
	// @todo we're in C LC_TIME locale to get the correct (month) format, but the timezone will be GMT, which is not wanted.
	strftime(szTime, ARRAY_SIZE(szTime), "%d/%b/%Y:%H:%M:%S %z", &local);
	HrGetHeaderValue("User-Agent", &strAgent);
	ec_log_notice("%s - %s [%s] \"%s\" %d %d \"-\" \"%s\"", m_lpChannel->peer_addr(), m_strUser.empty() ? "-" : m_strUser.c_str(), szTime, m_strAction.c_str(), m_ulRetCode, (int)m_strRespBody.length(), strAgent.c_str());
	m_ulRetCode = 0;
	return hr;
}

/**
 * Converts common hr codes to http error codes
 *
 * @param hr HRESULT
 *
 * @return Error from HrResponseHeader(unsigned int, string)
 */
HRESULT Http::HrToHTTPCode(HRESULT hr)
{
	if (hr == hrSuccess)
		return HrResponseHeader(200, "Ok");
	else if (hr == MAPI_E_NO_ACCESS)
		return HrResponseHeader(403, "Forbidden");
	else if (hr == MAPI_E_NOT_FOUND)
		return HrResponseHeader(404, "Not Found");
	// @todo other codes?
	return HrResponseHeader(500, "Unhanded error " + stringify_hex(hr));
}

/**
 * Sets http response headers
 * @param[in]	ulCode			Http header status code
 * @param[in]	strResponse		Http header status string
 *
 * @return		HRESULT
 * @retval		MAPI_E_CALL_FAILED	The status is already set
 *
 */
HRESULT Http::HrResponseHeader(unsigned int ulCode, const std::string &strResponse)
{
	m_ulRetCode = ulCode;
	// do not set headers if once set
	if (!m_strRespHeader.empty())
		return MAPI_E_CALL_FAILED;
	m_strRespHeader = "HTTP/1.1 " + stringify(ulCode) + " " + strResponse;
	return hrSuccess;
}

/**
 * Adds response header to the list of headers
 * @param[in]	strHeader	Name of the header e.g. Connection, Host, Date
 * @param[in]	strValue	Value of the header to be set
 * @return		HRESULT		Always set to hrSuccess
 */
HRESULT Http::HrResponseHeader(const std::string &strHeader, const std::string &strValue)
{
	m_lstHeaders.emplace_back(strHeader + ": " + strValue);
	return hrSuccess;
}

/**
 * Add string to http response body
 * @param[in]	strResponse		The string to be added to http response body
 * return		HRESULT			Always set to hrSuccess
 */
HRESULT Http::HrResponseBody(const std::string &strResponse)
{
	m_strRespBody += strResponse;
	// data send in HrFinalize()
	return hrSuccess;
}

/**
 * Request authorization information from the client
 *
 * @param[in]	strMsg	Message to be shown to the client
 * @return		HRESULT
 */
HRESULT Http::HrRequestAuth(const std::string &strMsg)
{
	HRESULT hr = HrResponseHeader(401, "Unauthorized");
	if (hr != hrSuccess)
		return hr;
	return HrResponseHeader("WWW-Authenticate", "Basic realm=\"" + strMsg + "\"");
}

/**
 * Write all http headers to ECChannel
 * @retrun	HRESULT
 * @retval	MAPI_E_END_OF_SESSION	If Connection type is set to close then the mapi session is ended
 */
HRESULT Http::HrFlushHeaders()
{
	HRESULT hr = hrSuccess;
	std::string strOutput, strConnection;
	char lpszChar[128];

	HrGetHeaderValue("Connection", &strConnection);
	// Add misc. headers
	HrResponseHeader("Server","Kopano");
	struct tm dummy;
	strftime(lpszChar, 127, "%a, %d %b %Y %H:%M:%S GMT", gmtime_safe(time(nullptr), &dummy));
	HrResponseHeader("Date", lpszChar);
	if (m_ulKeepAlive != 0 && strcasecmp(strConnection.c_str(), "keep-alive") == 0) {
		HrResponseHeader("Connection", "Keep-Alive");
		HrResponseHeader("Keep-Alive", stringify(m_ulKeepAlive));
	}
	else
	{
		HrResponseHeader("Connection", "close");
		hr = MAPI_E_END_OF_SESSION;
	}

	// create headers packet
	assert(m_ulRetCode != 0);
	if (m_ulRetCode == 0)
		HrResponseHeader(500, "Request handled incorrectly");
	ec_log_debug("> " + m_strRespHeader);
	strOutput += m_strRespHeader + "\r\n";
	m_strRespHeader.clear();
	for (const auto &h : m_lstHeaders) {
		ec_log_debug("> " + h);
		strOutput += h + "\r\n";
	}
	m_lstHeaders.clear();
	//as last line has a CRLF. The HrWriteLine adds one more CRLF.
	//this means the End of headder.
	m_lpChannel->HrWriteLine(strOutput);
	return hr;
}

HRESULT Http::X2W(const std::string &strIn, std::wstring *lpstrOut)
{
	const char *lpszCharset = (m_strCharSet.empty() ? "UTF-8" : m_strCharSet.c_str());
	return TryConvert(m_converter, strIn, rawsize(strIn), lpszCharset, *lpstrOut);
}

HRESULT Http::HrGetHeaderValue(const std::string &strHeader, std::string *strValue)
{
	auto iHeader = mapHeaders.find(strHeader);
	if (iHeader == mapHeaders.cend())
		return MAPI_E_NOT_FOUND;
	*strValue = iHeader->second;
	return hrSuccess;
}

HRESULT Http::HrGetUserAgent(std::string *strUserAgent)
{
	if (m_strUserAgent.empty())
		return MAPI_E_NOT_FOUND;
	strUserAgent -> assign(m_strUserAgent);
	return hrSuccess;
}

HRESULT Http::HrGetUserAgentVersion(std::string *strUserAgentVersion)
{
	if (m_strUserAgentVersion.empty())
		return MAPI_E_NOT_FOUND;
	strUserAgentVersion -> assign(m_strUserAgentVersion);
	return hrSuccess;
}
