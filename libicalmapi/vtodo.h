/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include "vconverter.h"

namespace KC {

class VTodoConverter final : public VConverter {
public:
	/* lpNamedProps must be the GetIDsFromNames() of the array in nameids.h */
	VTodoConverter(LPADRBOOK lpAdrBook, timezone_map *mapTimeZones, LPSPropTagArray lpNamedProps, const std::string& strCharset, bool blCensor, bool bNoRecipients, IMailUser *lpImailUser);

	HRESULT HrICal2MAPI(icalcomponent *event_root /* in */, icalcomponent *event /* in */, icalitem *prev /* in */, icalitem **out) override;

private:
	/* overrides, ical -> mapi */
	HRESULT HrAddBaseProperties(icalproperty_method, icalcomponent *event, void *base, bool is_exception, std::list<SPropValue> *msg_props) override;
	HRESULT HrAddTimes(icalproperty_method, icalcomponent *event_root, icalcomponent *event, bool is_allday, icalitem *) override;

	/* overrides, mapi -> ical */
	HRESULT HrMAPI2ICal(IMessage *, icalproperty_method *, icaltimezone **tz_info, std::string *tz_id, icalcomponent **event) override;
	HRESULT HrSetTimeProperties(SPropValue *props, unsigned int nprops, icaltimezone *tz_info, const std::string &tz_id, icalcomponent *event) override;
	HRESULT HrSetItemSpecifics(unsigned int nprop, SPropValue *props, icalcomponent *event) override;
};

} /* namespace */
