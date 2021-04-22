/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include <map>
#include <string>
#include <mapidefs.h>
#include <mapicode.h>
#include <kopano/stringutil.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECChannel.h>
#include <memory>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <libxml/xmlmemory.h>
#include <libxml/uri.h>
#include <libxml/globals.h>
#include <kopano/stringutil.h>

#define HTTP_CHUNK_SIZE 10000
#define SERVICE_UNKNOWN	0x00
#define SERVICE_ICAL	0x01
#define SERVICE_CALDAV	0x02
#define REQ_PUBLIC		0x04
#define REQ_COLLECTION	0x08

HRESULT HrParseURL(const std::string &stUrl, ULONG *lpulFlag, std::string *lpstrUrlUser = NULL, std::string *lpstrFolder = NULL);

namespace KC { class Config; };

class Http final {
public:
	Http(KC::ECChannel *, std::shared_ptr<KC::Config>);
	HRESULT HrReadHeaders();
	HRESULT HrValidateReq();
	HRESULT HrReadBody();
	HRESULT HrGetHeaderValue(const std::string &strHeader, std::string *strValue);

	/* @todo, remove and use HrGetHeaderValue() */
	HRESULT HrGetMethod(std::string *strMethod);
	HRESULT HrGetUser(std::wstring *strUser);
	HRESULT HrGetPass(std::wstring *strPass);
	HRESULT HrGetRequestUrl(std::string *strURL);
	HRESULT HrGetUrl(std::string *strURL);
	HRESULT HrGetBody(std::string *strBody);
	HRESULT HrGetDepth(ULONG *ulDepth);
	HRESULT HrGetCharSet(std::string *strCharset);
	HRESULT HrGetDestination(std::string *strDestination);
	HRESULT HrGetUserAgent(std::string *strUserAgent);
	HRESULT HrGetUserAgentVersion(std::string *strUserAgentVersion);
	HRESULT HrToHTTPCode(HRESULT hr);
	HRESULT HrResponseHeader(unsigned int code, const std::string &response);
	HRESULT HrResponseHeader(const std::string &header, const std::string &value);
	HRESULT HrRequestAuth(const std::string &msg);
	HRESULT HrResponseBody(const std::string &response);
	HRESULT HrSetKeepAlive(int ulKeepAlive);
	HRESULT HrFinalize();
	bool CheckIfMatch(LPMAPIPROP lpProp);

private:
	KC::ECChannel *m_lpChannel;
	std::shared_ptr<KC::Config> m_lpConfig;

	/* request */
	std::string m_strAction;	//!< full 1st-line
	std::string m_strMethod;	//!< HTTP method, e.g. GET, PROPFIND, etc.
	std::string m_strURL;		//!< original action url
	std::string m_strPath;		//!< decoded url
	std::string m_strHttpVer;
	std::map<std::string, std::string, KC::strcasecmp_comparison> mapHeaders;
	std::string m_strUser, m_strPass, m_strReqBody, m_strCharSet;
	std::string m_strUserAgent, m_strUserAgentVersion;

	/* response */
	std::string m_strRespHeader;			//!< first header with http status code
	std::list<std::string> m_lstHeaders;	//!< other headers
	std::string m_strRespBody;
	unsigned int m_ulRetCode = 0;
	int m_ulKeepAlive = 0;

	HRESULT HrParseHeaders();
	HRESULT HrFlushHeaders();
	HRESULT X2W(const std::string &strIn, std::wstring *lpstrOut);
};
