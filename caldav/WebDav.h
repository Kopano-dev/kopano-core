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

#ifndef _WEBDAV_H_
#define _WEBDAV_H_

#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <libxml/parser.h>

#include <mapi.h>
#include <map>

#include "ProtocolBase.h"
#include "Http.h"

struct WEBDAVPROPNAME {
	std::string strNS, strPropname, strPropAttribName, strPropAttribValue;
};

struct WEBDAVVALUE {
	WEBDAVPROPNAME sPropName;
	std::string strValue;
};

struct WEBDAVITEM {
	WEBDAVVALUE sDavValue;
	ULONG ulDepth;
};

struct WEBDAVPROPERTY {
	WEBDAVPROPNAME sPropName;
	std::list<WEBDAVVALUE> lstValues;
	std::string strValue;
	std::list<WEBDAVITEM> lstItems;
};

struct WEBDAVPROP {
	WEBDAVPROPNAME sPropName;
	std::list<WEBDAVPROPERTY> lstProps;
};

struct WEBDAVPROPSTAT {
	WEBDAVPROPNAME sPropName;	/* always propstat */
	WEBDAVPROP sProp;
	WEBDAVVALUE sStatus;		/* always status */
};

struct WEBDAVRESPONSE {
	WEBDAVPROPNAME sPropName;
	std::list<WEBDAVPROPSTAT> lstsPropStat;
	std::list<WEBDAVPROPERTY> lstProps;
	WEBDAVVALUE sHRef;
	WEBDAVVALUE sStatus;		/* possible on delete (but we don't use that) */
};

struct WEBDAVMULTISTATUS {
	WEBDAVPROPNAME sPropName;
	std::list<WEBDAVRESPONSE> lstResp;
};

struct WEBDAVFILTER {
	WEBDAVPROPNAME sPropName;
	std::list<std::string> lstFilters;
	time_t tStart;
};

struct WEBDAVREQSTPROPS {
	WEBDAVPROPNAME sPropName;
	WEBDAVPROP sProp;
	WEBDAVFILTER sFilter;
};

struct WEBDAVRPTMGET {
	WEBDAVPROPNAME sPropName;
	WEBDAVPROP sProp;
	std::list<WEBDAVVALUE> lstWebVal;
};

struct WEBDAVFBUSERINFO {
	std::string strUser, strIcal;
};

struct WEBDAVFBINFO {
	time_t tStart, tEnd;
	std::string strOrganiser, strUID;
	std::list<WEBDAVFBUSERINFO> lstFbUserInfo;
};

#define WEBDAVNS "DAV:"
#define CALDAVNS "urn:ietf:params:xml:ns:caldav"

class WebDav : public ProtocolBase {
public:
	WebDav(Http *, IMAPISession *, const std::string &srv_tz, const std::string &charset);
	virtual ~WebDav();

protected:
	/* preprocesses xml for HrHandle* functions */
	HRESULT HrPropfind();
	HRESULT HrReport();
	HRESULT HrPut();
	HRESULT HrMkCalendar();
	HRESULT HrPropPatch();
	HRESULT HrPostFreeBusy(WEBDAVFBINFO *lpsWebFbInfo);

	/* caldav implements real processing */
	virtual	HRESULT HrHandlePropfind(WEBDAVREQSTPROPS *sProp,WEBDAVMULTISTATUS *lpsDavMulStatus) = 0;
	virtual HRESULT HrListCalEntries(WEBDAVREQSTPROPS *sWebRCalQry,WEBDAVMULTISTATUS *sWebMStatus) = 0;
	virtual	HRESULT HrHandleReport(WEBDAVRPTMGET *sWebRMGet, WEBDAVMULTISTATUS *sWebMStatus) = 0;
	virtual HRESULT HrHandlePropPatch(WEBDAVPROP *lpsDavProp, WEBDAVMULTISTATUS *sWebMStatus) = 0;
	virtual HRESULT HrHandleMkCal(WEBDAVPROP *lpsDavProp) = 0;
	virtual HRESULT HrHandlePropertySearch(WEBDAVRPTMGET *sWebRMGet, WEBDAVMULTISTATUS *sWebMStatus) = 0;
	virtual HRESULT HrHandlePropertySearchSet(WEBDAVMULTISTATUS *sWebMStatus) = 0;
	virtual HRESULT HrHandleDelete() = 0;

private:
	xmlDoc *m_lpXmlDoc = nullptr;
	std::map <std::string,std::string> m_mapNs;

	HRESULT HrParseXml();

	/* more processing xml, but not as direct entrypoint */
	HRESULT HrHandleRptMulGet();
	HRESULT HrPropertySearch();
	HRESULT HrPropertySearchSet();
	HRESULT HrHandleRptCalQry();

	HRESULT RespStructToXml(WEBDAVMULTISTATUS *sDavMStatus, std::string *strXml);
	HRESULT GetNs(std::string *szPrefx, std::string *strNs);
	void RegisterNs(const std::string &strNs, std::string *strPrefix);
	HRESULT WriteData(xmlTextWriter *xmlWriter, const WEBDAVVALUE &sWebVal, std::string *szNsPrefix);
	HRESULT WriteNode(xmlTextWriter *xmlWriter, const WEBDAVPROPNAME &sWebPrName, std::string *szNsPrefix);
	HRESULT HrWriteSResponse(xmlTextWriter *xmlWriter, std::string *lpstrNsPrefix, const WEBDAVRESPONSE &sResponse);
	HRESULT HrWriteResponseProps(xmlTextWriter *xmlWriter, std::string *lpstrNsPrefix, std::list<WEBDAVPROPERTY> *lstProps);
	HRESULT HrWriteSPropStat(xmlTextWriter *xmlWriter, std::string *lpstrNsPrefix, const WEBDAVPROPSTAT &sPropStat);
	HRESULT HrWriteItems(xmlTextWriter *xmlWriter, std::string *lpstrNsPrefix, WEBDAVPROPERTY *lpsWebProprty);

	void HrSetDavPropName(WEBDAVPROPNAME *lpsDavPropName,xmlNode *lpXmlNode);
protected:
	void HrSetDavPropName(WEBDAVPROPNAME *lpsDavPropName, const std::string &strPropName, const std::string &strNs);
	void HrSetDavPropName(WEBDAVPROPNAME *lpsDavPropName, const std::string &strPropName, const std::string &strPropAttribName, const std::string &strPropAttribValue, const std::string &strNs);

};

#endif
