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
	HRESULT HrParseICalRecurrenceRule(TIMEZONE_STRUCT sTimeZone, icalcomponent *lpicRootEvent, icalcomponent *lpicEvent,
									  bool bIsAllday, LPSPropTagArray lpNamedProps, icalitem *lpIcalItem);
	HRESULT HrMakeMAPIException(icalcomponent *lpEventRoot, icalcomponent *lpicEvent, icalitem *lpIcalItem, bool bIsAllday, LPSPropTagArray lpNamedProps, std::string& strCharset, icalitem::exception *lpEx);
	HRESULT HrMakeMAPIRecurrence(recurrence *lpRecurrence, LPSPropTagArray lpNamedProps, LPMESSAGE lpMessage);
	bool HrValidateOccurrence(icalitem *lpItem, icalitem::exception lpEx);

	/* mapi -> ical */
	HRESULT HrCreateICalRecurrence(TIMEZONE_STRUCT sTimeZone, bool bIsAllDay, recurrence *lpRecurrence, icalcomponent *lpicEvent);
	HRESULT HrMakeICalException(icalcomponent *lpicEvent, icalcomponent **lppicException);

private:
	HRESULT HrCreateICalRecurrenceType(TIMEZONE_STRUCT sTimeZone, bool bIsAllday, recurrence *lpRecurrence, icalrecurrencetype *lpicRRule);

	HRESULT WeekDaysToICalArray(ULONG ulWeekDays, struct icalrecurrencetype *lpRec);
};

} /* namespace */

#endif
