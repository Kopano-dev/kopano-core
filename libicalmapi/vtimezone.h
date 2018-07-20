/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef TIMEZONE_TYPE_H
#define TIMEZONE_TYPE_H

#include "recurrence.h"
#include <mapidefs.h>
#include <string>
#include <map>
#include "TimeUtil.h"
#include <libical/ical.h>

namespace KC {

typedef std::map<std::string, TIMEZONE_STRUCT> timezone_map;
typedef std::map<std::string, TIMEZONE_STRUCT>::iterator timezone_map_iterator;

/* converts ical property like DTSTART to Unix timestamp in UTC */
time_t ICalTimeTypeToUTC(icalcomponent *lpicRoot, icalproperty *lpicProp);

/* Function to convert time to local - used for All day events*/
time_t ICalTimeTypeToLocal(icalproperty *lpicProp);


/* converts icaltimetype to local time_t */
extern time_t icaltime_as_timet_with_server_zone(const struct icaltimetype &tt);
HRESULT HrParseVTimeZone(icalcomponent* lpVTZ, std::string* strTZID, TIMEZONE_STRUCT* lpTimeZone);
extern HRESULT HrCreateVTimeZone(const std::string &tzid, const TIMEZONE_STRUCT &tz, icalcomponent **vtzcomp);

/* convert Olson timezone name (e.g. Europe/Amsterdam) to internal TIMEZONE_STRUCT */
HRESULT HrGetTzStruct(const std::string &strTimezone, TIMEZONE_STRUCT *tStruct);

} /* namespace */

#endif
