/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __ICAL_RECURRENCE_H
#define __ICAL_RECURRENCE_H

#include <kopano/zcdefs.h>
#include "recurrence.h"
#include "vtimezone.h"
#include "icalitem.h"
#include <mapidefs.h>
#include <libical/ical.h>

namespace KC {

/**
 * Conversion class for recurrence & exceptions (mapi <-> ical)
 */
class ICalRecurrence _kc_final {
public:
	/* ical -> mapi */
	HRESULT HrParseICalRecurrenceRule(const TIMEZONE_STRUCT &, icalcomponent *root_event, icalcomponent *event, bool all_day, const SPropTagArray *named_props, icalitem *);
	HRESULT HrMakeMAPIException(icalcomponent *event_root, icalcomponent *event, icalitem *, bool all_day, SPropTagArray *named_props, const std::string &charset, icalitem::exception *);
	HRESULT HrMakeMAPIRecurrence(recurrence *lpRecurrence, LPSPropTagArray lpNamedProps, LPMESSAGE lpMessage);
	bool HrValidateOccurrence(icalitem *lpItem, icalitem::exception lpEx);

	/* mapi -> ical */
	HRESULT HrCreateICalRecurrence(const TIMEZONE_STRUCT &, bool all_day, recurrence *, icalcomponent *event);
	HRESULT HrMakeICalException(icalcomponent *lpicEvent, icalcomponent **lppicException);

private:
	HRESULT HrCreateICalRecurrenceType(const TIMEZONE_STRUCT &, bool all_day, recurrence *, icalrecurrencetype *rrule);
	HRESULT WeekDaysToICalArray(ULONG ulWeekDays, struct icalrecurrencetype *lpRec);
};

} /* namespace */

#endif
