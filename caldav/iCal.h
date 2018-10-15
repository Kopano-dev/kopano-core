/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef _ICAL_H_
#define _ICAL_H_

#include "Http.h"
#include <mapi.h>
#include <kopano/CommonUtil.h>
#include "MAPIToICal.h"
#include "ICalToMAPI.h"
#include "CalDavProto.h"

class iCal final : public ProtocolBase {
public:
	iCal(Http &, IMAPISession *, const std::string &srv_tz, const std::string &charset);
	HRESULT HrHandleCommand(const std::string &method) override;

private:
	HRESULT HrHandleIcalGet(const std::string &strMethod);
	HRESULT HrHandleIcalPost();
	HRESULT HrDelFolder();
	HRESULT HrGetContents(IMAPITable **lppTable);
	HRESULT HrGetIcal(LPMAPITABLE lpTable, bool blCensorPrivate, std::string *strIcal);
	HRESULT HrModify(KC::ICalToMapi *, SBinary srv_eid, ULONG pos, bool censor);
	HRESULT HrAddMessage(KC::ICalToMapi *, ULONG pos);
	HRESULT HrDelMessage(SBinary sbEid, bool blCensor);
};

#endif
