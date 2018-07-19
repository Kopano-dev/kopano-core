/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef _CALDAVPROTO_H_
#define _CALDAVPROTO_H_

#include "WebDav.h"
#include "CalDavUtil.h"
#include <libxml/uri.h>

#include <kopano/zcdefs.h>
#include <kopano/mapiext.h>
#include "MAPIToICal.h"
#include "ICalToMAPI.h"
#include "icaluid.h"

#define FB_PUBLISH_DURATION 6

class CalDAV _kc_final : public WebDav {
public:
	CalDAV(Http &, IMAPISession *, const std::string &srv_tz, const std::string &charset);
	HRESULT HrHandleCommand(const std::string &strMethod) _kc_override;

protected:
	/* entry points in webdav class */
	/* HRESULT HrPropfind(); */
	/* HRESULT HrReport(); */
	/* HRESULT HrMkCalendar(); */
	/* HRESULT HrPropPatch(); */
	HRESULT HrPut();
	HRESULT HrMove();
	HRESULT HrHandleMeeting(KC::ICalToMapi *);
	HRESULT HrHandleFreebusy(KC::ICalToMapi *);
	virtual HRESULT HrHandlePropfind(WEBDAVREQSTPROPS *sDavProp, WEBDAVMULTISTATUS *lpsDavMulStatus) _kc_override;
	virtual HRESULT HrListCalEntries(WEBDAVREQSTPROPS *sWebRCalQry,WEBDAVMULTISTATUS *sWebMStatus) _kc_override; // Used By both PROPFIND & Report Calendar-query
	virtual	HRESULT HrHandleReport(WEBDAVRPTMGET *sWebRMGet, WEBDAVMULTISTATUS *sWebMStatus) _kc_override;
	virtual HRESULT HrHandlePropPatch(WEBDAVPROP *lpsDavProp, WEBDAVMULTISTATUS *sWebMStatus) _kc_override;
	virtual HRESULT HrHandleMkCal(WEBDAVPROP *lpsDavProp) _kc_override;
	virtual HRESULT HrHandlePropertySearch(WEBDAVRPTMGET *sWebRMGet, WEBDAVMULTISTATUS *sWebMStatus) _kc_override;
	virtual HRESULT HrHandlePropertySearchSet(WEBDAVMULTISTATUS *sWebMStatus) _kc_override;
	virtual HRESULT HrHandleDelete(void) _kc_override;
	HRESULT HrHandlePost();

private:
	HRESULT HrMoveEntry(const std::string &strGuid, LPMAPIFOLDER lpDestFolder);

	HRESULT HrHandlePropfindRoot(WEBDAVREQSTPROPS *sDavProp, WEBDAVMULTISTATUS *lpsDavMulStatus);	
	
	HRESULT CreateAndGetGuid(SBinary sbEid, ULONG ulPropTag, std::string *lpstrGuid);
	HRESULT HrListCalendar(WEBDAVREQSTPROPS *sDavProp, WEBDAVMULTISTATUS *lpsMulStatus);
	HRESULT HrConvertToIcal(const SPropValue *eid, KC::MapiToICal *, ULONG flags, std::string *out);
	HRESULT HrMapValtoStruct(IMAPIProp *obj, SPropValue *props, ULONG nprops, KC::MapiToICal *, ULONG flags, bool props_first, std::list<WEBDAVPROPERTY> *davprops, WEBDAVRESPONSE *);
	HRESULT	HrGetCalendarOrder(SBinary sbEid, std::string *lpstrCalendarOrder);
};

#endif
