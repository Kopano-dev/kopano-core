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
time_t icaltime_as_timet_with_server_zone(const struct icaltimetype tt);

HRESULT HrParseVTimeZone(icalcomponent* lpVTZ, std::string* strTZID, TIMEZONE_STRUCT* lpTimeZone);
HRESULT HrCreateVTimeZone(const std::string &strTZID, TIMEZONE_STRUCT &tsTimeZone, icalcomponent** lppVTZComp);

/* convert Olson timezone name (eg. Europe/Amsterdam) to internal TIMEZONE_STRUCT */
HRESULT HrGetTzStruct(const std::string &strTimezone, TIMEZONE_STRUCT *tStruct);

} /* namespace */

#endif
