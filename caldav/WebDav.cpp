/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <list>
#include <string>
#include <utility>
#include "WebDav.h"
#include <kopano/stringutil.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <libical/ical.h>

using namespace KC;

static inline const char *x2s(const xmlChar *s)
{
	return reinterpret_cast<const char *>(s);
}

static inline const xmlChar *s2x(const char *s)
{
	return reinterpret_cast<const xmlChar *>(s);
}

/**
 * @param[in]	lpRequest	Pointer to http Request object
 * @param[in]	lpSession	Pointer to mapi session of the user
 */
WebDav::WebDav(Http &lpRequest, IMAPISession *lpSession,
    const std::string &strSrvTz, const std::string &strCharset) :
	ProtocolBase(lpRequest, lpSession, strSrvTz, strCharset)
{
}

WebDav::~WebDav()
{
	if (m_lpXmlDoc)
		xmlFreeDoc(m_lpXmlDoc);
}

/**
 * Parse the xml request body
 * @return		HRESULT
 * @retval		MAPI_E_INVALID_PARAMETER		Unable to parse the xml data
 */
HRESULT WebDav::HrParseXml()
{
	std::string strBody;

	assert(m_lpXmlDoc == NULL);
	if (m_lpXmlDoc != NULL)
		return hrSuccess;
	m_lpRequest.HrGetBody(&strBody);
	m_lpXmlDoc = xmlReadMemory(strBody.c_str(), strBody.length(), "PROVIDE_BASE.xml", nullptr, XML_PARSE_NOBLANKS);
	return m_lpXmlDoc == nullptr ? MAPI_E_INVALID_PARAMETER : hrSuccess;
}

/**
 * Parse the PROPFIND request
 *
 * Example of PROPFIND request
 *
 * <D:propfind xmlns:D="DAV:" xmlns:CS="http://calendarserver.org/ns/">
 *		<D:prop>
 *			<D:resourcetype/>
 *			<D:owner/>
 *			<CS:getctag/>
 *		</D:prop>
 * </D:propfind>
 *
 * @return		HRESULT
 * @retval		MAPI_E_CORRUPT_DATA		Invalid xml data
 * @retval		MAPI_E_NOT_FOUND		Folder requested in url not found
 *
 */
HRESULT WebDav::HrPropfind()
{
	WEBDAVRESPONSE sDavResp;
	WEBDAVMULTISTATUS sDavMStatus;
	WEBDAVREQSTPROPS sDavReqsProps;
	WEBDAVPROP sDavPropRet;
	std::string strXml;
	xmlNode * lpXmlNode = NULL;

	// libxml parser parses the xml data and returns the DomTree pointer.
	auto hr = HrParseXml();
	if (hr != hrSuccess)
		goto exit;

	lpXmlNode = xmlDocGetRootElement(m_lpXmlDoc);
	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "propfind") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}
	lpXmlNode = lpXmlNode->children;
	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "prop") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	HrSetDavPropName(&(sDavPropRet.sPropName),lpXmlNode);
	for (lpXmlNode = lpXmlNode->children; lpXmlNode != nullptr;
	     lpXmlNode = lpXmlNode->next) {
		WEBDAVPROPERTY sProperty;

		HrSetDavPropName(&(sProperty.sPropName),lpXmlNode);
		sDavPropRet.lstProps.emplace_back(std::move(sProperty));
	}

	/*
	 * Call to CALDAV::HrHandlePropfind
	 * This function Retrieves data from server and adds it to the structure.
	 */
	sDavReqsProps.sProp = std::move(sDavPropRet);
	hr = HrHandlePropfind(&sDavReqsProps, &sDavMStatus);
	if (hr != hrSuccess)
		goto exit;

	// Convert WEBMULTISTATUS structure to xml data.
	hr = RespStructToXml(&sDavMStatus, &strXml);
	if (hr != hrSuccess)
	{
		hr_ldebug(hr, "Unable to convert response to XML");
		goto exit;
	}

exit:
	if(hr == hrSuccess)
	{
		m_lpRequest.HrResponseHeader(207, "Multi-Status");
		m_lpRequest.HrResponseHeader("Content-Type", "application/xml; charset=\"utf-8\"");
		m_lpRequest.HrResponseBody(strXml);
	}
	else if (hr == MAPI_E_NOT_FOUND)
		m_lpRequest.HrResponseHeader(404, "Not Found");
	else if (hr == MAPI_E_NO_ACCESS)
		m_lpRequest.HrResponseHeader(403, "Access Denied");
	else
		m_lpRequest.HrResponseHeader(500, "Internal Server Error");
	return hr;
}

/**
 * Converts WEBDAVMULTISTATUS response structure to xml data
 * @param[in]	sDavMStatus		Pointer to WEBDAVMULTISTATUS structure
 * @param[out]	strXml			Return string for xml data
 *
 * @return		HRESULT
 * @retval		MAPI_E_NOT_ENOUGH_MEMORY	Error allocating memory
 * @retval		MAPI_E_CALL_FAILED			Error initializing xml writer
 * @retval		MAPI_E_CALL_FAILED			Error writing xml data
 */
HRESULT WebDav::RespStructToXml(WEBDAVMULTISTATUS *sDavMStatus, std::string *strXml)
{
	HRESULT hr = hrSuccess;
	std::string strNs, strNsPrefix = "C";
	auto xmlBuff = xmlBufferCreate();

	if (xmlBuff == NULL)
	{
		ec_log_err("Error allocating memory to xmlBuffer");
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}

	//Initialize Xml-Writer
	auto xmlWriter = xmlNewTextWriterMemory(xmlBuff, 0);
	if (xmlWriter == NULL)
	{
		hr = MAPI_E_CALL_FAILED;
		ec_log_err("Error Initializing xmlWriter");
		goto exit;
	}

	// let xml use enters and spaces to make it somewhat readable
	// if indedentaion is not present, iCal.app does not show suggestionlist.
	if (xmlTextWriterSetIndent(xmlWriter, 1) < 0)
		goto xmlfail;
	//start xml-data i.e <xml version="1.0" encoding="utf-8"?>
	if (xmlTextWriterStartDocument(xmlWriter, nullptr, "UTF-8", nullptr) < 0)
		goto xmlfail;

	// @todo move this default to sDavMStatus constructor, never different.
	if(sDavMStatus->sPropName.strNS.empty())
		sDavMStatus->sPropName.strNS = "DAV:";
	if (sDavMStatus->sPropName.strPropname.empty())
		sDavMStatus->sPropName.strPropname = "multistatus";

	strNsPrefix = "C";
	m_mapNs[sDavMStatus->sPropName.strNS.c_str()] = "C";

	//<multistatus>
	if (xmlTextWriterStartElementNS(xmlWriter, s2x(strNsPrefix.c_str()),
	    s2x(sDavMStatus->sPropName.strPropname.c_str()),
	    s2x(sDavMStatus->sPropName.strNS.c_str())) < 0)
		goto xmlfail;

	//write all xmlname spaces in main tag.
	for (const auto &ns : m_mapNs) {
		std::string strprefix;

		strNs = ns.first;
		if(sDavMStatus->sPropName.strNS == strNs || strNs.empty())
			continue;
		RegisterNs(strNs, &strNsPrefix);
		strprefix = "xmlns:" + strNsPrefix;
		if (xmlTextWriterWriteAttribute(xmlWriter, s2x(strprefix.c_str()),
		    s2x(strNs.c_str())) < 0)
			goto xmlfail;
	}
	// <response>
	for (const auto &resp : sDavMStatus->lstResp) {
		hr = HrWriteSResponse(xmlWriter, &strNsPrefix, resp);
		if(hr != hrSuccess)
			goto exit;
	}

	//</multistatus>
	if (xmlTextWriterEndElement(xmlWriter) < 0)
		goto xmlfail;
	//EOF
	if (xmlTextWriterEndDocument(xmlWriter) < 0)
		goto xmlfail;
	// force write to buffer
	if (xmlTextWriterFlush(xmlWriter) < 0)
		goto xmlfail;
	strXml->assign(reinterpret_cast<char *>(xmlBuff->content), xmlBuff->use);

exit:
	if (xmlWriter)
		xmlFreeTextWriter(xmlWriter);
	if (xmlBuff)
		xmlBufferFree(xmlBuff);
	return hr;

xmlfail:
	hr = MAPI_E_CALL_FAILED;
	ec_log_err("Error writing xml data");
	goto exit;
}

/**
 * Parse the REPORT request, identify the type of report request
 *
 * @return	HRESULT
 * @retval	MAPI_E_CORRUPT_DATA		Invalid xml request
 *
 */
HRESULT WebDav::HrReport()
{
	auto hr = HrParseXml();
	if (hr != hrSuccess)
		return hr;
	auto lpXmlNode = xmlDocGetRootElement(m_lpXmlDoc);
	if (!lpXmlNode)
		return MAPI_E_CORRUPT_DATA;
	if (lpXmlNode->name == nullptr) {
		m_lpRequest.HrResponseHeader(500, "Internal Server Error");
		return hrSuccess;
	}
	if (strcmp(x2s(lpXmlNode->name), "calendar-query") == 0)
		//CALENDAR-QUERY
		//Retrieves the list of GUIDs
		return HrHandleRptCalQry();
	else if (strcmp(x2s(lpXmlNode->name), "calendar-multiget") == 0)
		//MULTIGET
		//Retrieves Ical data for each GUID that client requests
		return HrHandleRptMulGet();
	else if (strcmp(x2s(lpXmlNode->name), "principal-property-search") == 0)
		// suggestion list while adding attendees on mac iCal.
		return HrPropertySearch();
	else if (strcmp(x2s(lpXmlNode->name), "principal-search-property-set") == 0)
		// which all properties to be searched while searching for attendees.
		return HrPropertySearchSet();
	else if (strcmp(x2s(lpXmlNode->name), "expand-property") == 0)
		// ignore expand-property
		m_lpRequest.HrResponseHeader(200, "OK");
	else
		m_lpRequest.HrResponseHeader(500, "Internal Server Error");
	return hrSuccess;
}

/**
 * Parses the calendar-query REPORT request.
 *
 * The request is for retrieving list of calendar entries in folder
 * Example of the request
 *
 * <calendar-query xmlns:D="DAV:" xmlns="urn:ietf:params:xml:ns:caldav">
 *		<D:prop>
 *			<D:getetag/>
 *		</D:prop>
 *		<filter>
 *			<comp-filter name="VCALENDAR">
 *				<comp-filter name="VEVENT"/>
 *			</comp-filter>
 *		</filter>
 * </calendar-query>
 *
 * @return	HRESULT
 * @retval	MAPI_E_CORRUPT_DATA		Invalid xml data
 */
// @todo, do not return MAPI_E_CORRUPT_DATA, but make a clean error report for the client.
// only return MAPI_E_CORRUPT_DATA when xml is invalid (which normal working clients won't send)
HRESULT WebDav::HrHandleRptCalQry()
{
	HRESULT hr = hrSuccess;
	xmlNode * lpXmlChildAttr = NULL;
	WEBDAVREQSTPROPS sReptQuery;
	WEBDAVMULTISTATUS sWebMStatus;
	std::string strXml;
	auto lpXmlNode = xmlDocGetRootElement(m_lpXmlDoc);
	if (!lpXmlNode)
	{
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	// REPORT calendar-query
	sReptQuery.sPropName.strPropname = x2s(lpXmlNode->name);
	sReptQuery.sFilter.tStart = 0;

	//HrSetDavPropName(&(sReptQuery.sPropName),lpXmlNode);
	for (lpXmlNode = lpXmlNode->children; lpXmlNode != nullptr;
	     lpXmlNode = lpXmlNode->next) {
		if (strcmp(x2s(lpXmlNode->name), "filter") == 0) {
			// @todo convert xml filter to mapi restriction
			// "old" code
			if (!lpXmlNode->children) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			HrSetDavPropName(&(sReptQuery.sFilter.sPropName),lpXmlNode);
			auto lpXmlChildNode = lpXmlNode->children;
			auto lpXmlAttr = lpXmlChildNode->properties;
			if (lpXmlAttr && lpXmlAttr->children)
				lpXmlChildAttr = lpXmlAttr->children;
			else
			{
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}

			if (lpXmlChildAttr == nullptr || lpXmlChildAttr->content == nullptr ||
			    strcmp(x2s(lpXmlChildAttr->content), "VCALENDAR") != 0) {
				hr = E_FAIL;
				goto exit;
			}

			sReptQuery.sFilter.lstFilters.emplace_back(x2s(lpXmlChildAttr->content));
			lpXmlChildNode = lpXmlChildNode->children;
			if (lpXmlChildNode == nullptr ||
			    lpXmlChildNode->properties == nullptr ||
			    lpXmlChildNode->properties->children == nullptr) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			lpXmlAttr = lpXmlChildNode->properties;
			lpXmlChildAttr = lpXmlAttr->children;
			if (lpXmlChildAttr == NULL || lpXmlChildAttr->content == NULL) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			if (strcmp(x2s(lpXmlChildAttr->content), "VTODO") != 0 &&
			    strcmp(x2s(lpXmlChildAttr->content), "VEVENT") != 0) {
				hr = MAPI_E_CORRUPT_DATA;
				goto exit;
			}
			sReptQuery.sFilter.lstFilters.emplace_back(x2s(lpXmlChildAttr->content));
			// filter not done here.., time-range in lpXmlChildNode->children.
			if (lpXmlChildNode->children) {
				for (lpXmlChildNode = lpXmlChildNode->children; lpXmlChildNode != NULL; lpXmlChildNode = lpXmlChildNode->next) {
					if (strcmp(x2s(lpXmlChildNode->name), "time-range") != 0)
						continue;
					if (lpXmlChildNode->properties == NULL || lpXmlChildNode->properties->children == NULL)
						continue;
					lpXmlChildAttr = lpXmlChildNode->properties->children;
					if (strcmp(x2s(lpXmlChildAttr->name), "start") != 0)
						// other lpXmlChildAttr->name .. like "end" maybe?
						continue;
					// timestamp from ical
					icaltimetype iTime = icaltime_from_string(x2s(lpXmlChildAttr->content));
					sReptQuery.sFilter.tStart = icaltime_as_timet(iTime);
					// @note this is still being ignored in CalDavProto::HrListCalEntries
				}
			}
		} else if (strcmp(x2s(lpXmlNode->name), "prop") == 0) {
			sReptQuery.sPropName.strNS = lpXmlNode->ns != nullptr && lpXmlNode->ns->href != nullptr ?
				x2s(lpXmlNode->ns->href) : WEBDAVNS;
			HrSetDavPropName(&(sReptQuery.sProp.sPropName),lpXmlNode);
			for (auto lpXmlChildNode = lpXmlNode->children;
			     lpXmlChildNode != nullptr;
			     lpXmlChildNode = lpXmlChildNode->next) {
				//properties requested by client.
				WEBDAVPROPERTY sWebProperty;

				HrSetDavPropName(&(sWebProperty.sPropName),lpXmlChildNode);
				sReptQuery.sProp.lstProps.emplace_back(std::move(sWebProperty));
			}
		} else {
			ec_log_debug("Skipping unknown XML element: %s", lpXmlNode->name);
		}
	}

	// @todo, load depth 0 ? .. see propfind version.

	//Retrieve Data from the server & return WEBMULTISTATUS structure.
	hr = HrListCalEntries(&sReptQuery, &sWebMStatus);
	if (hr != hrSuccess)
		goto exit;
	//Convert WEBMULTISTATUS structure to xml data.
	hr = RespStructToXml(&sWebMStatus, &strXml);
	if (hr != hrSuccess)
		 goto exit;
	//Respond to the client with xml data.
	m_lpRequest.HrResponseHeader(207, "Multi-Status");
	m_lpRequest.HrResponseHeader("Content-Type", "application/xml; charset=\"utf-8\"");
	m_lpRequest.HrResponseBody(strXml);
exit:
	if (hr != hrSuccess)
	{
		hr_ldebug(hr, "Unable to process report calendar query");
		m_lpRequest.HrResponseHeader(500, "Internal Server Error");
	}
	return hr;
}
/**
 * Parses the calendar-multiget REPORT request
 *
 * The request contains the list of guid and the response should have the
 * ical data
 * Example of the request
 * <calendar-multiget xmlns:D="DAV:" xmlns="urn:ietf:params:xml:ns:caldav">
 *		<D:prop>
 *			<D:getetag/>
 *			<calendar-data/>
 *		</D:prop>
 *		<D:href>/caldav/user/path-to-calendar/d8516650-b6fc-11dd-92a5-a494cf95cb3a.ics</D:href>
 * </calendar-multiget>
 *
 * @return	HRESULT
 * @retval	MAPI_E_CORRUPT_DATA		Invalid xml data in request
 */
HRESULT WebDav::HrHandleRptMulGet()
{
	HRESULT hr = hrSuccess;
	WEBDAVRPTMGET sRptMGet;
	WEBDAVMULTISTATUS sWebMStatus;
	std::string strXml;
	auto lpXmlNode = xmlDocGetRootElement(m_lpXmlDoc);
	if (!lpXmlNode)
	{
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	//REPORT Multiget Request.
	// xml data to structures
	HrSetDavPropName(&(sRptMGet.sPropName),lpXmlNode);
	lpXmlNode = lpXmlNode->children;
	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "prop") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	HrSetDavPropName(&(sRptMGet.sProp.sPropName),lpXmlNode);
	for (auto lpXmlChildNode = lpXmlNode->children; lpXmlChildNode != nullptr;
	     lpXmlChildNode = lpXmlChildNode->next) {
		//Reqeuested properties of the client.
		WEBDAVPROPERTY sWebProperty;

		HrSetDavPropName(&(sWebProperty.sPropName),lpXmlChildNode);
		sRptMGet.sProp.lstProps.emplace_back(std::move(sWebProperty));
	}

	if (lpXmlNode->next == nullptr) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	for (auto lpXmlChildNode = lpXmlNode->next; lpXmlChildNode != nullptr;
	     lpXmlChildNode = lpXmlChildNode->next) {
		//List of ALL GUIDs whose ical data is requested.
		WEBDAVVALUE sWebVal;
		std::string strGuid;
		size_t found = 0;
		auto lpXmlContentNode = lpXmlChildNode->children;

		HrSetDavPropName(&(sWebVal.sPropName),lpXmlChildNode);
		strGuid = x2s(lpXmlContentNode->content);
		found = strGuid.rfind("/");
		if (found == std::string::npos || found + 1 == strGuid.length())
			continue;
		++found;

		// strip url and .ics from guid, and convert %hex to real data
		strGuid.erase(0, found);
		strGuid.erase(strGuid.length() - 4);
		sWebVal.strValue = urlDecode(strGuid);
		sRptMGet.lstWebVal.emplace_back(std::move(sWebVal));
	}

	//Retrieve Data from the Server and return WEBMULTISTATUS structure.
	hr = HrHandleReport(&sRptMGet, &sWebMStatus);
	if (hr != hrSuccess)
		goto exit;
	//Convert WEBMULTISTATUS structure to xml data
	hr = RespStructToXml(&sWebMStatus, &strXml);
	if (hr != hrSuccess)
		goto exit;
	m_lpRequest.HrResponseHeader(207, "Multi-Status");
	m_lpRequest.HrResponseHeader("Content-Type", "application/xml; charset=\"utf-8\"");
	m_lpRequest.HrResponseBody(strXml);
exit:
	if(hr != hrSuccess)
	{
		hr_ldebug(hr, "Unable to process report multi-get");
		m_lpRequest.HrResponseHeader(500, "Internal Server Error");
	}
	return hr;
}

/**
 * Parses the property-search request and generates xml response
 *
 * The response contains list of users matching the request
 * for the attendeee suggestion list in mac
 *
 * @return	HRESULT
 */
HRESULT WebDav::HrPropertySearch()
{
	HRESULT hr = hrSuccess;
	WEBDAVRPTMGET sRptMGet;
	WEBDAVMULTISTATUS sWebMStatus;
	WEBDAVPROPERTY sWebProperty;
	WEBDAVVALUE sWebVal;
	xmlNode *lpXmlNode = NULL;
	xmlNode *lpXmlSubChildNode = NULL;
	std::string strXml;

	lpXmlNode = xmlDocGetRootElement(m_lpXmlDoc);
	if (!lpXmlNode)
	{
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}
	lpXmlNode = lpXmlNode->children;
	// <principal-property-search>
	HrSetDavPropName(&(sRptMGet.sPropName),lpXmlNode);

	//REPORT Multiget Request.
	// xml data to structures
	while (lpXmlNode) {
		// <property-search>
		if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
		    strcmp(x2s(lpXmlNode->name), "property-search") != 0)
			break;

		// <prop>
		auto lpXmlChildNode = lpXmlNode->children;
		if (lpXmlChildNode == nullptr || lpXmlChildNode->name == nullptr ||
		    strcmp(x2s(lpXmlChildNode->name), "prop") != 0) {
			hr = MAPI_E_CORRUPT_DATA;
			goto exit;;
		}
		HrSetDavPropName(&(sRptMGet.sProp.sPropName),lpXmlChildNode);

		// eg <diplayname>
		lpXmlSubChildNode = lpXmlChildNode->children;
		HrSetDavPropName(&(sWebVal.sPropName),lpXmlSubChildNode);

		// <match>
		if (!lpXmlChildNode->next) {
			hr = MAPI_E_CORRUPT_DATA;
			goto exit;
		}

		lpXmlChildNode = lpXmlChildNode->next;
		if(lpXmlChildNode->children->content)
			sWebVal.strValue = x2s(lpXmlChildNode->children->content);
		sRptMGet.lstWebVal.emplace_back(sWebVal);
		if(lpXmlNode->next)
			lpXmlNode = lpXmlNode->next;
	}

	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "prop") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}
	for (auto lpXmlChildNode = lpXmlNode->children;
	     lpXmlChildNode != nullptr; lpXmlChildNode = lpXmlChildNode->next) {
		HrSetDavPropName(&(sWebProperty.sPropName),lpXmlChildNode);
		sRptMGet.sProp.lstProps.emplace_back(sWebProperty);
	}

	//Retrieve Data from the Server and return WEBMULTISTATUS structure.
	hr = HrHandlePropertySearch(&sRptMGet, &sWebMStatus);
	if (hr != hrSuccess)
		goto exit;
	//Convert WEBMULTISTATUS structure to xml data
	hr = RespStructToXml(&sWebMStatus, &strXml);
	if (hr != hrSuccess)
		goto exit;
	m_lpRequest.HrResponseHeader(207 , "Multi-Status");
	m_lpRequest.HrResponseHeader("Content-Type", "application/xml; charset=\"utf-8\"");
	m_lpRequest.HrResponseBody(strXml);
exit:
	if(hr != hrSuccess)
	{
		hr_ldebug(hr, "Unable to process report multi-get");
		m_lpRequest.HrResponseHeader(500, "Internal Server Error");
	}
	return hr;
}
/**
 * Handles the property-search-set request
 *
 * The response contains list of property that the client can request while
 * requesting attendees in suggestion list
 *
 * @return	HRESULT
 */
HRESULT WebDav::HrPropertySearchSet()
{
	WEBDAVMULTISTATUS sDavMStatus;
	std::string strXml;

	auto hr = HrHandlePropertySearchSet(&sDavMStatus);
	if (hr != hrSuccess)
		return hr;
	hr = RespStructToXml(&sDavMStatus, &strXml);
	if (hr != hrSuccess)
		return hr;
	m_lpRequest.HrResponseHeader(200, "OK");
	m_lpRequest.HrResponseHeader("Content-Type", "application/xml; charset=\"utf-8\"");
	m_lpRequest.HrResponseBody(strXml);
	return hrSuccess;
}
/**
 * Generates xml response for POST request to view freebusy information
 *
 * @param[in]	lpsWebFbInfo	WEBDAVFBINFO structure, contains all freebusy information
 * @return		HRESULT
 */
HRESULT WebDav::HrPostFreeBusy(WEBDAVFBINFO *lpsWebFbInfo)
{
	WEBDAVMULTISTATUS sWebMStatus;
	std::string strXml;

	HrSetDavPropName(&sWebMStatus.sPropName,"schedule-response", CALDAVNS);

	for (const auto &ui : lpsWebFbInfo->lstFbUserInfo) {
		WEBDAVPROPERTY sWebProperty;
		WEBDAVVALUE sWebVal;
		WEBDAVRESPONSE sWebResPonse;

		HrSetDavPropName(&sWebResPonse.sPropName,"response", CALDAVNS);
		HrSetDavPropName(&sWebProperty.sPropName,"recipient", CALDAVNS);
		HrSetDavPropName(&sWebVal.sPropName,"href", WEBDAVNS);

		sWebVal.strValue = "mailto:" + ui.strUser;
		sWebProperty.lstValues.emplace_back(sWebVal);
		sWebResPonse.lstProps.emplace_back(sWebProperty);
		sWebProperty.lstValues.clear();

		HrSetDavPropName(&sWebProperty.sPropName,"request-status", CALDAVNS);
		sWebProperty.strValue = ui.strIcal.empty() ? "3.8;No authority" : "2.0;Success";
		sWebResPonse.lstProps.emplace_back(sWebProperty);

		if (!ui.strIcal.empty()) {
			HrSetDavPropName(&sWebProperty.sPropName,"calendar-data", CALDAVNS);
			sWebProperty.strValue = ui.strIcal;
			sWebResPonse.lstProps.emplace_back(sWebProperty);
		}
		sWebMStatus.lstResp.emplace_back(std::move(sWebResPonse));
	}

	auto hr = RespStructToXml(&sWebMStatus, &strXml);
	if (hr == hrSuccess) {
		m_lpRequest.HrResponseHeader(200, "OK");
		m_lpRequest.HrResponseHeader("Content-Type", "application/xml; charset=\"utf-8\"");
		m_lpRequest.HrResponseBody(strXml);
	} else {
		m_lpRequest.HrResponseHeader(404, "Not found");
	}
	return hr;
}

/**
 * Converts WEBDAVVALUE structure to xml data
 * @param[in]	xmlWriter	xml writer to write xml data
 * @param[in]	sWebVal		WEBDAVVALUE structure to be converted to xml
 * @param[in]	szNsPrefix	Current Namespace prefix
 * @return		HRESULT
 * @retval		MAPI_E_CALL_FAILED	Unable to write xml data
 */
HRESULT WebDav::WriteData(xmlTextWriter *xmlWriter, const WEBDAVVALUE &sWebVal,
    std::string *szNsPrefix)
{
	auto strNs = sWebVal.sPropName.strNS;
	if(strNs.empty())
	{
		if (sWebVal.sPropName.strPropname.empty())
			return hrSuccess;
		return xmlTextWriterWriteElement(xmlWriter,
		       s2x(sWebVal.sPropName.strPropname.c_str()),
		       s2x(sWebVal.strValue.c_str())) < 0 ?
		       MAPI_E_CALL_FAILED : hrSuccess;
	}
	// Retrieve the namespace prefix if present in map.
	auto hr = GetNs(szNsPrefix, &strNs);
	// if namespace is not present in the map then insert it into map.
	if (hr != hrSuccess)
		RegisterNs(strNs, szNsPrefix);

	/*Write xml none of the form
	 *	<D:href>/caldav/user/calendar/entryGUID.ics</D:href>
	 */
	return xmlTextWriterWriteElementNS(xmlWriter, s2x(szNsPrefix->c_str()),
	       s2x(sWebVal.sPropName.strPropname.c_str()),
	       s2x((strNs.empty() ? nullptr : strNs.c_str())),
	       s2x(sWebVal.strValue.c_str())) < 0 ?
	       MAPI_E_CALL_FAILED : hrSuccess;
}
/**
 * Converts WEBDAVPROPNAME	to xml data
 * @param[in]	xmlWriter		xml writer to write xml data
 * @param[in]	sWebPropName	sWebPropName structure to be written into xml
 * @param[in]	lpstrNsPrefix	current namespace prefix
 * @return		HRESULT
 */
HRESULT WebDav::WriteNode(xmlTextWriter *xmlWriter,
    const WEBDAVPROPNAME &sWebPropName, std::string *lpstrNsPrefix)
{
	auto strNs = sWebPropName.strNS;
	if(strNs.empty())
		return xmlTextWriterStartElement(xmlWriter,
		       s2x(sWebPropName.strPropname.c_str())) < 0 ?
		       MAPI_E_CALL_FAILED : hrSuccess;

	auto hr = GetNs(lpstrNsPrefix, &strNs);
	if (hr != hrSuccess)
		RegisterNs(strNs, lpstrNsPrefix);

	/*Write Xml data of the form
	 * <D:propstat>
	 * the end tag </D:propstat> is written by "xmlTextWriterEndElement(xmlWriter)"
	 */
	if (xmlTextWriterStartElementNS(xmlWriter,
	    s2x(lpstrNsPrefix->c_str()),
	    s2x(sWebPropName.strPropname.c_str()),
	    s2x(strNs.empty() ? nullptr : strNs.c_str())) < 0)
		return MAPI_E_CALL_FAILED;
	if (!sWebPropName.strPropAttribName.empty()) {
		if (xmlTextWriterWriteAttribute(xmlWriter,
		    s2x(sWebPropName.strPropAttribName.c_str()),
		    s2x(sWebPropName.strPropAttribValue.c_str())) < 0)
			return MAPI_E_CALL_FAILED;
	}
	return hrSuccess;
}
/**
 * Adds namespace prefix into map of namespaces
 * @param[in]	strNs			Namespace name
 * @param[in]	lpstrNsPrefix	Namespace prefix
 * @return		HRESULT			Always returns hrSuccess
 */
void WebDav::RegisterNs(const std::string &strNs, std::string *lpstrNsPrefix)
{
	(*lpstrNsPrefix)[0]++;
	m_mapNs[strNs] = *lpstrNsPrefix;
}

/**
 * Returns the namespace prefix for the corresponding namespace name
 * @param[in,out]	lpstrPrefx	Return string for namespace prefix
 * @param[in,out]	lpstrNs		Namespace name, is set to empty string if namespace prefix found
 * @return			HRESULT
 * @retval			MAPI_E_NOT_FOUND	Namespace prefix not found
 */
HRESULT WebDav::GetNs(std::string * lpstrPrefx, std::string *lpstrNs)
{
	auto itMpNs = m_mapNs.find(*lpstrNs);
	if (itMpNs == m_mapNs.cend())
		return MAPI_E_NOT_FOUND;
	lpstrPrefx->assign(itMpNs->second);
	lpstrNs->clear();
	return hrSuccess;
}
/**
 * Converts WEBDAVRESPONSE structure to xml data
 *
 * @param[in]	xmlWriter		xml writer to write xml data
 * @param[in]	lpstrNsPrefix	Pointer to string containing the current namespace prefix
 * @param[in]	sResponse		WEBDAVRESPONSE structure to be converted to xml data
 * @return		HRESULT
 */
HRESULT WebDav::HrWriteSResponse(xmlTextWriter *xmlWriter,
    std::string *lpstrNsPrefix, const WEBDAVRESPONSE &sResponse)
{
	const auto &sWebResp = sResponse;
	// <response>
	auto hr = WriteNode(xmlWriter, sWebResp.sPropName, lpstrNsPrefix);
	if (hr != hrSuccess)
		return hr;
	// <href>xxxxxxxxxxxxxxxx</href>
	if (!sWebResp.sHRef.sPropName.strPropname.empty()) {
		hr = WriteData(xmlWriter, sWebResp.sHRef, lpstrNsPrefix);
		if (hr != hrSuccess)
			return hr;
	}
	// Only set for broken calendar entries
	// <D:status>HTTP/1.1 404 Not Found</D:status>
	if (!sWebResp.sStatus.sPropName.strPropname.empty()) {
		hr = WriteData(xmlWriter,sWebResp.sStatus , lpstrNsPrefix);
		if (hr != hrSuccess)
			return hr;
	}

	for (const auto &stat : sWebResp.lstsPropStat) {
		hr = HrWriteSPropStat(xmlWriter, lpstrNsPrefix, stat);
		if (hr != hrSuccess)
			return hr;
	}

	if (!sWebResp.lstProps.empty())
	{
		hr = HrWriteResponseProps(xmlWriter, lpstrNsPrefix, sWebResp.lstProps);
		if (hr != hrSuccess)
			return hr;
	}
	//</response>
	return xmlTextWriterEndElement(xmlWriter) < 0 ?
	       MAPI_E_CALL_FAILED : hrSuccess;
}

/**
 * Converts WEBDAVPROPERTY list to xml data
 *
 * @param[in]	xmlWriter		xml writer to write xml data
 * @param[in]	lpstrNsPrefix	Pointer to string containing the current namespace prefix
 * @param[in]	lplstProps		WEBDAVPROPERTY list to be converted to xml data
 * @return		HRESULT
 */
HRESULT WebDav::HrWriteResponseProps(xmlTextWriter *xmlWriter,
    std::string *lpstrNsPrefix, const std::list<WEBDAVPROPERTY> &lplstProps)
{
	for (const auto &iterProp : lplstProps) {
		auto sWebProperty = iterProp;
		HRESULT hr;
		if (!sWebProperty.strValue.empty())
		{
			WEBDAVVALUE sWebVal;
			sWebVal.sPropName = sWebProperty.sPropName;
			sWebVal.strValue = sWebProperty.strValue;
			//<getctag xmlns="xxxxxxxxxxx">xxxxxxxxxxxxxxxxx</getctag>
			hr = WriteData(xmlWriter, std::move(sWebVal), lpstrNsPrefix);
		}
		else
		{	//<resourcetype>
			hr = WriteNode(xmlWriter, sWebProperty.sPropName,lpstrNsPrefix);
		}
		if (hr != hrSuccess)
			return hr;

		//loop for sub properties
		for (int k = 0; !sWebProperty.lstValues.empty(); ++k) {
			const auto &sWebVal = sWebProperty.lstValues.front();
			//<collection/>
			if (!sWebVal.strValue.empty()) {
				hr = WriteData(xmlWriter, sWebVal, lpstrNsPrefix);
				if (hr != hrSuccess)
					return hr;
			} else {
				hr = WriteNode(xmlWriter, sWebVal.sPropName, lpstrNsPrefix);
				if (hr != hrSuccess)
					return hr;
				if (xmlTextWriterEndElement(xmlWriter) < 0)
					return MAPI_E_CALL_FAILED;
			}
			sWebProperty.lstValues.pop_front();
		}
		if (sWebProperty.strValue.empty() &&
		    xmlTextWriterEndElement(xmlWriter) < 0)
			return MAPI_E_CALL_FAILED;
	}

	return hrSuccess;
}
/**
 * Converts WEBDAVPROPSTAT structure to xml data
 *
 * @param[in]	xmlWriter		xml writer to write xml data
 * @param[in]	lpstrNsPrefix	Pointer to string containing the current namespace prefix
 * @param[in]	lpsPropStat		WEBDAVPROPSTAT structure to be converted to xml data
 * @return		HRESULT
 */
HRESULT WebDav::HrWriteSPropStat(xmlTextWriter *xmlWriter,
    std::string *lpstrNsPrefix, const WEBDAVPROPSTAT &lpsPropStat)
{
	const auto &sWebPropStat = lpsPropStat;
	//<propstat>
	auto hr = WriteNode(xmlWriter, sWebPropStat.sPropName,lpstrNsPrefix);
	if (hr != hrSuccess)
		return hr;

	const auto &sWebProp = sWebPropStat.sProp;

	//<prop>
	hr = WriteNode(xmlWriter, sWebProp.sPropName,lpstrNsPrefix);
	if (hr != hrSuccess)
		return hr;

	//loop	for properties list
	for (const auto &iterProp : sWebProp.lstProps) {
		auto sWebProperty = iterProp;

		if (!sWebProperty.strValue.empty())
		{
			WEBDAVVALUE sWebVal;
			sWebVal.sPropName = sWebProperty.sPropName;
			sWebVal.strValue = sWebProperty.strValue;
			//<getctag xmlns="xxxxxxxxxxx">xxxxxxxxxxxxxxxxx</getctag>
			hr = WriteData(xmlWriter, std::move(sWebVal), lpstrNsPrefix);
		}
		else
		{	//<resourcetype>
			hr = WriteNode(xmlWriter, sWebProperty.sPropName,lpstrNsPrefix);
		}
		if (hr != hrSuccess)
			return hr;

		if (!sWebProperty.lstItems.empty()) {
			hr = HrWriteItems(xmlWriter, lpstrNsPrefix, &sWebProperty);
			if (hr != hrSuccess)
				return hr;
		}

		//loop for sub properties
		for (int k = 0; !sWebProperty.lstValues.empty(); ++k)
		{
			const auto &sWebVal = sWebProperty.lstValues.front();
			//<collection/>
			if (!sWebVal.strValue.empty()) {
				hr = WriteData(xmlWriter,sWebVal,lpstrNsPrefix);
				if (hr != hrSuccess)
					return hr;
			} else {
				hr = WriteNode(xmlWriter, sWebVal.sPropName, lpstrNsPrefix);
				if (hr != hrSuccess)
					return hr;
				if (xmlTextWriterEndElement(xmlWriter) < 0)
					return MAPI_E_CALL_FAILED;
			}
			sWebProperty.lstValues.pop_front();
		}
		//end tag if started
		//</resourcetype>
		if (sWebProperty.strValue.empty() &&
		    xmlTextWriterEndElement(xmlWriter) < 0)
			return MAPI_E_CALL_FAILED;
	}

	//</prop>
	if (xmlTextWriterEndElement(xmlWriter) < 0)
		return MAPI_E_CALL_FAILED;
	//<status xmlns="xxxxxxx">HTTP/1.1 200 OK</status>
	hr = WriteData(xmlWriter, sWebPropStat.sStatus, lpstrNsPrefix);
	if (hr != hrSuccess)
		return hr;
	// ending the function here on !hrSuccess breaks several tests.
	//</propstat>
	return xmlTextWriterEndElement(xmlWriter) < 0 ?
	       MAPI_E_CALL_FAILED : hrSuccess;
}

/**
 * Converts Items List to xml data
 *
 * @param[in]	xmlWriter		xml writer to write xml data
 * @param[in]	lpstrNsPrefix	Pointer to string containing the current namespace prefix
 * @param[in]	lpsWebProperty	WEBDAVPROPERTY structure containing the list of items
 * @return		HRESULT			Always returns hrSuccess
 */
HRESULT WebDav::HrWriteItems(xmlTextWriter *xmlWriter,
    std::string *lpstrNsPrefix, WEBDAVPROPERTY *lpsWebProperty)
{
	ULONG ulDepthPrev = 0;
	ULONG ulDepthCur = 0;
	bool blFirst = true;

	ulDepthPrev = lpsWebProperty->lstItems.front().ulDepth;
	while(!lpsWebProperty->lstItems.empty())
	{
		auto sDavItem = lpsWebProperty->lstItems.front();
		ulDepthCur = sDavItem.ulDepth;
		HRESULT hr;

		if(ulDepthCur >= ulDepthPrev)
		{
			if(ulDepthCur == ulDepthPrev && !blFirst && sDavItem.sDavValue.sPropName.strPropname != "ace")
				if (xmlTextWriterEndElement(xmlWriter) < 0)
					return MAPI_E_CALL_FAILED;
			blFirst = false;
			if (!sDavItem.sDavValue.strValue.empty())
			{
				hr = WriteData(xmlWriter, sDavItem.sDavValue, lpstrNsPrefix);
				sDavItem.ulDepth = sDavItem.ulDepth - 1;
			}
			else
			{
				//<resourcetype>
				hr = WriteNode(xmlWriter, sDavItem.sDavValue.sPropName, lpstrNsPrefix);
			}
			if (hr != hrSuccess)
				return MAPI_E_CALL_FAILED;
		}
		else
		{
			for (ULONG i = ulDepthCur; i <= ulDepthPrev; ++i)
				if (xmlTextWriterEndElement(xmlWriter) < 0)
					return MAPI_E_CALL_FAILED;

			if (!sDavItem.sDavValue.strValue.empty())
			{
				hr = WriteData(xmlWriter, sDavItem.sDavValue, lpstrNsPrefix);
				sDavItem.ulDepth = sDavItem.ulDepth - 1;
			}
			else
			{
				//<resourcetype>
				hr = WriteNode(xmlWriter, sDavItem.sDavValue.sPropName, lpstrNsPrefix);
			}
			if (hr != hrSuccess)
				return hr;
		}
		ulDepthPrev = sDavItem.ulDepth;
		lpsWebProperty->lstItems.pop_front();
	}

	for (ULONG i = 0; i <= ulDepthPrev; ++i)
		if (xmlTextWriterEndElement(xmlWriter) < 0)
			return MAPI_E_CALL_FAILED;
	return hrSuccess;
}

/**
 * Set Name and namespace to WEBDAVPROPNAME structure from xmlNode
 *
 * The structure contains information about a xml element
 *
 * @param[in]	lpsDavPropName		Strutcture to which all the values are set
 * @param[in]	lpXmlNode			Pointer to xmlNode object, it contains name and namespace
 *
 * @return		HRESULT		Always returns hrsuccess
 */
void WebDav::HrSetDavPropName(WEBDAVPROPNAME *lpsDavPropName, xmlNode *lpXmlNode)
{
	lpsDavPropName->strPropname = x2s(lpXmlNode->name);
	if (lpXmlNode->ns != NULL && lpXmlNode->ns->href != NULL)
		lpsDavPropName->strNS = x2s(lpXmlNode->ns->href);
	else
		lpsDavPropName->strNS.clear();
	if(!lpsDavPropName->strNS.empty())
		m_mapNs[lpsDavPropName->strNS].clear();
	lpsDavPropName->strPropAttribName.clear();
	lpsDavPropName->strPropAttribValue.clear();
}

/**
 * Set Name and namespace to WEBDAVPROPNAME structure
 *
 * The structure contains information about a xml element
 *
 * @param[in]	lpsDavPropName		Strutcture to which all the values are set
 * @param[in]	strPropName			Name of the element
 * @param[in]	strNs				Namespace of the element
 *
 * @return		HRESULT		Always returns hrsuccess
 */
void WebDav::HrSetDavPropName(WEBDAVPROPNAME *lpsDavPropName,
    const std::string &strPropName, const std::string &strNs)
{
	lpsDavPropName->strPropname = strPropName;
	lpsDavPropName->strNS = strNs;
	if (!lpsDavPropName->strNS.empty())
		m_mapNs[lpsDavPropName->strNS].clear();
	lpsDavPropName->strPropAttribName.clear();
	lpsDavPropName->strPropAttribValue.clear();
}

/**
 * Set Name, attribute and namespace to WEBDAVPROPNAME structure
 *
 * The structure contains information about a xml element
 *
 * @param[in]	lpsDavPropName		Strutcture to which all the values are set
 * @param[in]	strPropName			Name of the element
 * @param[in]	strPropAttribName	Attribute's name to be set in the element
 * @param[in]	strPropAttribValue	Attribute's value to be set in the element
 * @param[in]	strNs				Namespace of the element
 *
 * @return		HRESULT		Always returns hrsuccess
 */
void WebDav::HrSetDavPropName(WEBDAVPROPNAME *lpsDavPropName,
    const std::string &strPropName, const std::string &strPropAttribName,
    const std::string &strPropAttribValue, const std::string &strNs)
{
	lpsDavPropName->strPropname = strPropName;
	lpsDavPropName->strNS = strNs;
	lpsDavPropName->strPropAttribName = strPropAttribName;
	lpsDavPropName->strPropAttribValue = strPropAttribValue;
	if (!lpsDavPropName->strNS.empty())
		m_mapNs[lpsDavPropName->strNS].clear();
}

/**
 * Parse the PROPPATCH xml request
 *
 * @return	HRESULT
 * @retval	MAPI_E_CORRUPT_DATA		Invalid xml data in request
 * @retval	MAPI_E_NO_ACCESS		Not enough permissions to edit folder names or comments
 * @retval	MAPI_E_COLLISION		A folder with same name already exists
 */
HRESULT WebDav::HrPropPatch()
{
	WEBDAVPROP sDavProp;
	WEBDAVMULTISTATUS sDavMStatus;
	std::string strXml;
	xmlNode * lpXmlNode = NULL;

	//libxml parser parses the xml data and returns the DomTree pointer.
	auto hr = HrParseXml();
	if (hr != hrSuccess)
		goto exit;

	lpXmlNode = xmlDocGetRootElement(m_lpXmlDoc);
	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "propertyupdate") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}
	lpXmlNode = lpXmlNode->children;
	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "set") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}
	lpXmlNode = lpXmlNode->children;
	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "prop") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	HrSetDavPropName(&sDavProp.sPropName, "prop",
		lpXmlNode->ns != nullptr && lpXmlNode->ns->href != nullptr ?
		x2s(lpXmlNode->ns->href) : WEBDAVNS);

	for (lpXmlNode = lpXmlNode->children; lpXmlNode != nullptr;
	     lpXmlNode = lpXmlNode->next) {
		WEBDAVPROPERTY sProperty;

		HrSetDavPropName(&sProperty.sPropName, x2s(lpXmlNode->name),
			lpXmlNode->ns != nullptr && lpXmlNode->ns->href != nullptr ?
			x2s(lpXmlNode->ns->href) : WEBDAVNS);
		if (lpXmlNode->children != nullptr &&
		    lpXmlNode->children->content != nullptr)
			sProperty.strValue = x2s(lpXmlNode->children->content);

		sDavProp.lstProps.emplace_back(std::move(sProperty));
	}

	hr = HrHandlePropPatch(&sDavProp, &sDavMStatus);
	if (hr != hrSuccess)
		goto exit;
	// Convert WEBMULTISTATUS structure to xml data.
	hr = RespStructToXml(&sDavMStatus, &strXml);
	if (hr != hrSuccess)
	{
		hr_ldebug(hr, "Unable to convert response to XML");
		goto exit;
	}
exit:
	if (hr != hrSuccess) {
		// this is important for renaming your calendar folder
		if (hr == MAPI_E_COLLISION)
			m_lpRequest.HrResponseHeader(409, "Conflict");
		else
			m_lpRequest.HrResponseHeader(403, "Forbidden");
	} else {
		m_lpRequest.HrResponseHeader(207 , "Multi-Status");
		m_lpRequest.HrResponseHeader("Content-Type", "application/xml; charset=\"utf-8\"");
		m_lpRequest.HrResponseBody(strXml);
	}
	return hr;
}
/**
 * Handle xml parsing for MKCALENDAR request
 *
 * @return	HRESULT
 * @retval	MAPI_E_CORRUPT_DATA		Invalid xml request
 * @retval	MAPI_E_COLLISION		Folder with same name exists
 * @retval	MAPI_E_NO_ACCESS		Not enough permissions to create a folder
 */
// input url: /caldav/username/7F2A8EB0-5E2C-4EB7-8B46-6ECBFE91BA3F/
/* input xml:
	  <x0:mkcalendar xmlns:x1="DAV:" xmlns:x2="http://apple.com/ns/ical/" xmlns:x0="urn:ietf:params:xml:ns:caldav">
	   <x1:set>
	    <x1:prop>
		 <x1:displayname>Untitled</x1:displayname>
		 <x2:calendar-color>#492BA1FF</x2:calendar-color>
		 <x2:calendar-order>9</x2:calendar-order>
		 <x0:calendar-free-busy-set><YES/></x0:calendar-free-busy-set>
		 <x0:calendar-timezone> ... ical timezone data snipped ... </x0:calendar-timezone>
		</x1:prop>
	   </x1:set>
	  </x0:mkcalendar>
*/
HRESULT WebDav::HrMkCalendar()
{
	xmlNode * lpXmlNode = NULL;
	WEBDAVPROP sDavProp;

	auto hr = HrParseXml();
	if(hr != hrSuccess)
	{
		ec_log_err("Parsing Error For MKCALENDAR");
		goto exit;
	}

	lpXmlNode = xmlDocGetRootElement(m_lpXmlDoc);
	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "mkcalendar") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}
	lpXmlNode = lpXmlNode->children;
	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "set") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}
	lpXmlNode = lpXmlNode->children;
	if (lpXmlNode == nullptr || lpXmlNode->name == nullptr ||
	    strcmp(x2s(lpXmlNode->name), "prop") != 0) {
		hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	HrSetDavPropName(&sDavProp.sPropName, x2s(lpXmlNode->name),
		lpXmlNode->ns != nullptr && lpXmlNode->ns->href != nullptr ?
		x2s(lpXmlNode->ns->href) : WEBDAVNS);

	for (lpXmlNode = lpXmlNode->children; lpXmlNode != nullptr;
	     lpXmlNode = lpXmlNode->next) {
		WEBDAVPROPERTY sProperty;

		HrSetDavPropName(&sProperty.sPropName, x2s(lpXmlNode->name),
			lpXmlNode->ns != nullptr && lpXmlNode->ns->href != nullptr ?
			x2s(lpXmlNode->ns->href) : WEBDAVNS);
		if (lpXmlNode->children && lpXmlNode->children->content)
			sProperty.strValue = x2s(lpXmlNode->children->content);

		// @todo we should have a generic xml to structs converter, this is *way* too hackish
		if (sProperty.sPropName.strPropname == "supported-calendar-component-set")
			for (auto lpXmlChild = lpXmlNode->children;
			     lpXmlChild != nullptr; lpXmlChild = lpXmlChild->next)
				if (lpXmlChild->type == XML_ELEMENT_NODE &&
				    strcmp(x2s(lpXmlChild->name), "comp") == 0 &&
				    lpXmlChild->properties != nullptr &&
				    lpXmlChild->properties->children != nullptr &&
				    lpXmlChild->properties->children->content != nullptr)
					sProperty.strValue = x2s(lpXmlChild->properties->children->content);
		sDavProp.lstProps.emplace_back(std::move(sProperty));
	}

	hr = HrHandleMkCal(&sDavProp);
exit:
	if(hr == MAPI_E_COLLISION)
	{
		m_lpRequest.HrResponseHeader(409,"CONFLICT");
		m_lpRequest.HrResponseBody("Folder with same name already exists");
	}
	else if(hr == MAPI_E_NO_ACCESS)
		m_lpRequest.HrResponseHeader(403 ,"Forbidden");
	else if(hr == hrSuccess)
		m_lpRequest.HrResponseHeader(201,"Created");
	else
		m_lpRequest.HrResponseHeader(500,"Bad Request");
	return hr;
}
