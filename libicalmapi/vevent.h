/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ICALMAPI_VEVENT_H
#define ICALMAPI_VEVENT_H

#include <kopano/zcdefs.h>
#include "vconverter.h"

namespace KC {

class VEventConverter final : public VConverter {
public:
	/* lpNamedProps must be the GetIDsFromNames() of the array in nameids.h */
	VEventConverter(LPADRBOOK lpAdrBook, timezone_map *mapTimeZones, LPSPropTagArray lpNamedProps, const std::string& strCharset, bool blCensor, bool bNoRecipients, IMailUser *lpImailUser);
	HRESULT HrICal2MAPI(icalcomponent *event_root /* in */, icalcomponent *event /* in */, icalitem *prev /* in */, icalitem **out) override;

private:
	/* overrides, ical -> mapi */
	HRESULT HrAddBaseProperties(icalproperty_method, icalcomponent *event, void *base, bool is_exception, std::list<SPropValue> *msg_props) override;
	HRESULT HrAddTimes(icalproperty_method, icalcomponent *event_root, icalcomponent *event, bool is_allday, icalitem *) override;

	/* overrides, mapi -> ical */
	HRESULT HrMAPI2ICal(IMessage *, icalproperty_method *, icaltimezone **tz_info, std::string *tz_id, icalcomponent **event) override;
	HRESULT HrSetTimeProperties(SPropValue *props, unsigned int nprops, icaltimezone *tz_info, const std::string &tz_id, icalcomponent *event) override;
};

} /* namespace */

#endif
