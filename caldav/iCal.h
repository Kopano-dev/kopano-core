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

#ifndef _ICAL_H_
#define _ICAL_H_

#include <kopano/zcdefs.h>
#include "Http.h"
#include <mapi.h>
#include <kopano/CommonUtil.h>
#include "MAPIToICal.h"
#include "ICalToMAPI.h"
#include "CalDavProto.h"

class iCal _kc_final : public ProtocolBase {
public:
	iCal(Http &, IMAPISession *, const std::string &srv_tz, const std::string &charset);
	HRESULT HrHandleCommand(const std::string &strMethod) _kc_override;
	
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
