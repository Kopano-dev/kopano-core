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

#include <kopano/platform.h>
#include <utility>
#include "vtimezone.h"
#include <mapidefs.h>
#include <mapicode.h>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include "icalcompat.hpp"

namespace KC {

/**
 * Converts icaltimetype to Unix timestamp.
 * Here server zone refers to timezone with which the server started,
 * not the config file option in ical.cfg
 *
 * @param[in]	tt		icaltimetype
 * @return		Unix timestamp
 */
time_t icaltime_as_timet_with_server_zone(const struct icaltimetype &tt)
{
	struct tm stm;

	/* If the time is the special null time, return 0. */
	if (icaltime_is_null_time(tt))
		return 0;

	/* Copy the icaltimetype to a struct tm. */
	memset (&stm, 0, sizeof (struct tm));

	if (icaltime_is_date(tt)) {
		stm.tm_sec = stm.tm_min = stm.tm_hour = 0;
	} else {
		stm.tm_sec = tt.second;
		stm.tm_min = tt.minute;
		stm.tm_hour = tt.hour;
	}

	stm.tm_mday = tt.day;
	stm.tm_mon = tt.month-1;
	stm.tm_year = tt.year-1900;
	stm.tm_isdst = -1;
	return mktime(&stm);
}

// time only, not date!
static time_t SystemTimeToUnixTime(const SYSTEMTIME &stime)
{
	return stime.wSecond + (stime.wMinute*60) + ((stime.wHour)*60*60);
}

static SYSTEMTIME TMToSystemTime(const struct tm &t)
{
	SYSTEMTIME stime = {0};
	stime.wYear = t.tm_year;
	stime.wMonth = t.tm_mon;
	stime.wDayOfWeek = t.tm_wday;
	stime.wDay = t.tm_mday;
	stime.wHour = t.tm_hour;
	stime.wMinute = t.tm_min;
	stime.wSecond = t.tm_sec;
	stime.wMilliseconds = 0;
	return stime;
}

/**
 * Converts icaltimetype to UTC Unix timestamp
 *
 * @param[in]	lpicRoot		root icalcomponent to get timezone
 * @param[in]	lpicProp		icalproperty containing time
 * @return		UTC Unix timestamp
 */
time_t ICalTimeTypeToUTC(icalcomponent *lpicRoot, icalproperty *lpicProp)
{
	const char *lpszTZID = NULL;
	icaltimezone *lpicTimeZone = NULL;

	auto lpicTZParam = icalproperty_get_first_parameter(lpicProp, ICAL_TZID_PARAMETER);
	if (lpicTZParam) {
		lpszTZID = icalparameter_get_tzid(lpicTZParam);
		lpicTimeZone = icalcomponent_get_timezone(lpicRoot, lpszTZID);
	}
	return icaltime_as_timet_with_zone(icalvalue_get_datetime(icalproperty_get_value(lpicProp)), lpicTimeZone);
}

/**
 * Converts icaltimetype to local Unix timestamp.
 * Here local refers to timezone with which the server started, 
 * not the config file option in ical.cfg
 *
 * @param[in]	lpicProp	icalproperty containing time
 * @return		local Unix timestamp
 */
time_t ICalTimeTypeToLocal(icalproperty *lpicProp)
{
	return icaltime_as_timet_with_server_zone(icalvalue_get_datetime(icalproperty_get_value(lpicProp)));
}

/**
 * Converts icaltimetype to tm structure
 *
 * @param[in]	tt		icaltimetype time
 * @return		tm structure
 */
static struct tm UTC_ICalTime2UnixTime(const icaltimetype &tt)
{
	struct tm stm = {0};

	memset(&stm, 0, sizeof(struct tm));

	if (icaltime_is_null_time(tt))
		return stm;

	stm.tm_sec = tt.second;
	stm.tm_min = tt.minute;
	stm.tm_hour = tt.hour;
	stm.tm_mday = tt.day;
	stm.tm_mon = tt.month-1;
	stm.tm_year = tt.year-1900;
	stm.tm_isdst = -1;

	return stm;
}

/**
 * Converts icaltimetype to TIMEZONE_STRUCT structure
 *
 * @param[in]	kind				icalcomponent kind, either STD or DST time component (ICAL_XSTANDARD_COMPONENT, ICAL_XDAYLIGHT_COMPONENT)
 * @param[in]	lpVTZ				vtimezone icalcomponent
 * @param[out]	lpsTimeZone			returned TIMEZONE_STRUCT structure
 * @return		MAPI error code
 * @retval		MAPI_E_NOT_FOUND	icalcomponent kind not found in vtimezone component, or some part of timezone not found
 */
static HRESULT HrZoneToStruct(icalcomponent_kind kind, icalcomponent *lpVTZ,
    TIMEZONE_STRUCT *lpsTimeZone)
{
	icalcomponent *icComp = NULL;
	SYSTEMTIME *lpSysTime = NULL;
	SYSTEMTIME stRecurTime;

	/* Assumes that definitions are sorted on dtstart, in ascending order. */
	auto iterComp = icalcomponent_get_first_component(lpVTZ, kind);
	while (iterComp != NULL) {
		auto icTime = icalcomponent_get_dtstart(iterComp);
		kc_ical_utc(icTime, true);
		struct tm start = UTC_ICalTime2UnixTime(icTime);
		if (time(NULL) < mktime(&start) && icComp != nullptr)
			break;
		icComp = iterComp;
		iterComp = icalcomponent_get_next_component(lpVTZ, kind);
	}

	if (icComp == NULL)
		return MAPI_E_NOT_FOUND;
	auto dtStart = icalcomponent_get_first_property(icComp, ICAL_DTSTART_PROPERTY);
	auto tzFrom = icalcomponent_get_first_property(icComp, ICAL_TZOFFSETFROM_PROPERTY);
	auto tzTo = icalcomponent_get_first_property(icComp, ICAL_TZOFFSETTO_PROPERTY);
	auto rRule = icalcomponent_get_first_property(icComp, ICAL_RRULE_PROPERTY);
	//rDate = icalcomponent_get_first_property(icComp, ICAL_RDATE_PROPERTY);

	if (tzFrom == NULL || tzTo == NULL || dtStart == NULL)
		return MAPI_E_NOT_FOUND;
	auto icTime = icalcomponent_get_dtstart(icComp);
	kc_ical_utc(icTime, true);

	if (kind == ICAL_XSTANDARD_COMPONENT) {
		// this is set when we request the STD timezone part.
		lpsTimeZone->lBias    = -(icalproperty_get_tzoffsetto(tzTo) / 60); // STD time is set as bias for timezone
		lpsTimeZone->lStdBias = 0;
		lpsTimeZone->lDstBias =  (icalproperty_get_tzoffsetto(tzTo) - icalproperty_get_tzoffsetfrom(tzFrom)) / 60; // DST bias == standard from

		lpsTimeZone->wStdYear = 0;
		lpSysTime = &lpsTimeZone->stStdDate;
	} else {
		lpsTimeZone->wDstYear = 0;
		lpSysTime = &lpsTimeZone->stDstDate;
	}

	memset(lpSysTime, 0, sizeof(SYSTEMTIME));

	// e.g. Japan doesn't have daylight saving switches.
	if (!rRule) {
		stRecurTime = TMToSystemTime(UTC_ICalTime2UnixTime(icTime));
		lpSysTime->wMonth = stRecurTime.wMonth + 1; // fix for -1 in UTC_ICalTime2UnixTime, since TMToSystemTime doesn't do +1
		lpSysTime->wDayOfWeek = stRecurTime.wDayOfWeek;
		lpSysTime->wDay = int(stRecurTime.wDay / 7.0) + 1;
		lpSysTime->wHour = stRecurTime.wHour;
		lpSysTime->wMinute = stRecurTime.wMinute;
		return hrSuccess;
	}
	auto recur = icalproperty_get_rrule(rRule);
	// can daylight saving really be !yearly ??
	if (recur.freq != ICAL_YEARLY_RECURRENCE ||
	    recur.by_month[0] == ICAL_RECURRENCE_ARRAY_MAX ||
	    recur.by_month[1] != ICAL_RECURRENCE_ARRAY_MAX)
		return hrSuccess;

	stRecurTime = TMToSystemTime(UTC_ICalTime2UnixTime(icTime));
	lpSysTime->wHour = stRecurTime.wHour;
	lpSysTime->wMinute = stRecurTime.wMinute;
	lpSysTime->wMonth = recur.by_month[0];
	if (icalrecurrencetype_day_position(recur.by_day[0]) == -1)
		lpSysTime->wDay = 5;	// last day of month
	else
		lpSysTime->wDay = icalrecurrencetype_day_position(recur.by_day[0]); // 1..4
	lpSysTime->wDayOfWeek = icalrecurrencetype_day_day_of_week(recur.by_day[0]) - 1;
	return hrSuccess;
}

/**
 * Converts VTIMEZONE block in to TIMEZONE_STRUCT structure
 *
 * @param[in]	lpVTZ				VTIMEZONE icalcomponent
 * @param[out]	lpstrTZID			timezone string
 * @param[out]	lpTimeZone			returned TIMEZONE_STRUCT structure
 * @return		MAPI error code
 * @retval		MAPI_E_NOT_FOUND	standard component not found
 * @retval		MAPI_E_CALL_FAILED	TZID property not found
 */
HRESULT HrParseVTimeZone(icalcomponent* lpVTZ, std::string* lpstrTZID, TIMEZONE_STRUCT* lpTimeZone)
{
	TIMEZONE_STRUCT tzRet;

	memset(&tzRet, 0, sizeof(TIMEZONE_STRUCT));
	auto icProp = icalcomponent_get_first_property(lpVTZ, ICAL_TZID_PROPERTY);
	if (icProp == NULL)
		return MAPI_E_CALL_FAILED;

	std::string strTZID = icalproperty_get_tzid(icProp);
	if (strTZID.at(0) == '\"') {
		// strip "" around timezone name
		strTZID.erase(0, 1);
		strTZID.erase(strTZID.size()-1);
	}
	auto hr = HrZoneToStruct(ICAL_XSTANDARD_COMPONENT, lpVTZ, &tzRet);
	if (hr != hrSuccess)
		return hr;

	// if the timezone does no switching, daylight is not given, so we ignore the error (which is only MAPI_E_NOT_FOUND)
	HrZoneToStruct(ICAL_XDAYLIGHT_COMPONENT, lpVTZ, &tzRet);

	// unsupported case: only exceptions in the timezone switches, and no base rule (e.g. very old Asia/Kolkata timezone)
	icalproperty *tzSTDRule = NULL, *tzDSTRule = NULL;
	icalproperty *tzSTDDate = NULL, *tzDSTDate = NULL;
	auto icComp = icalcomponent_get_first_component(lpVTZ, ICAL_XSTANDARD_COMPONENT);
	if (icComp) {
		tzSTDRule = icalcomponent_get_first_property(icComp, ICAL_RRULE_PROPERTY);
		tzSTDDate = icalcomponent_get_first_property(icComp, ICAL_RDATE_PROPERTY);
	}
	icComp = icalcomponent_get_first_component(lpVTZ, ICAL_XDAYLIGHT_COMPONENT);
	if (icComp) {
		tzDSTRule = icalcomponent_get_first_property(icComp, ICAL_RRULE_PROPERTY);
		tzDSTDate = icalcomponent_get_first_property(icComp, ICAL_RDATE_PROPERTY);
	}
	if (tzSTDRule == NULL && tzDSTRule == NULL && tzSTDDate != NULL && tzDSTDate != NULL) {
		// clear rule data
		memset(&tzRet.stStdDate, 0, sizeof(SYSTEMTIME));
		memset(&tzRet.stDstDate, 0, sizeof(SYSTEMTIME));
	}

	if (lpstrTZID)
		*lpstrTZID = std::move(strTZID);
	if (lpTimeZone)
		*lpTimeZone = tzRet;
	return hrSuccess;
}

/**
 * Converts TIMEZONE_STRUCT structure to VTIMEZONE component
 *
 * @param[in]	strTZID		timezone string
 * @param[in]	tsTimeZone	TIMEZONE_STRUCT to be converted
 * @param[out]	lppVTZComp	returned VTIMEZONE component
 * @return		MAPI error code
 * @retval		MAPI_E_INVALID_PARAMETER timezone contains invalid data for a yearly daylightsaving
 */
HRESULT HrCreateVTimeZone(const std::string &strTZID,
    const TIMEZONE_STRUCT &tsTimeZone, icalcomponent **lppVTZComp)
{
	icalrecurrencetype icRec;

	// wDay in a timezone context means "week in month", 5 for last week in month
	if (tsTimeZone.stStdDate.wYear > 0 || tsTimeZone.stStdDate.wDay > 5 ||
	    tsTimeZone.stStdDate.wDayOfWeek > 7 ||
	    tsTimeZone.stDstDate.wYear > 0 || tsTimeZone.stDstDate.wDay > 5 ||
	    tsTimeZone.stDstDate.wDayOfWeek > 7)
		return MAPI_E_INVALID_PARAMETER;

	// make a new timezone
	auto icTZComp = icalcomponent_new(ICAL_VTIMEZONE_COMPONENT);
	icalcomponent_add_property(icTZComp, icalproperty_new_tzid(strTZID.c_str()));

	// STD
	auto icComp = icalcomponent_new_xstandard();
	auto icTime = icaltime_from_timet_with_zone(SystemTimeToUnixTime(tsTimeZone.stStdDate), 0, nullptr);
	icalcomponent_add_property(icComp, icalproperty_new_dtstart(icTime));
	if (tsTimeZone.lStdBias == tsTimeZone.lDstBias || tsTimeZone.stStdDate.wMonth == 0 || tsTimeZone.stDstDate.wMonth == 0) {
		// std == dst
		icalcomponent_add_property(icComp, icalproperty_new_tzoffsetfrom(-tsTimeZone.lBias *60));
		icalcomponent_add_property(icComp, icalproperty_new_tzoffsetto(-tsTimeZone.lBias *60));
	} else {
		icalcomponent_add_property(icComp, icalproperty_new_tzoffsetfrom( ((-tsTimeZone.lBias) + (-tsTimeZone.lDstBias)) *60) );
		icalcomponent_add_property(icComp, icalproperty_new_tzoffsetto(-tsTimeZone.lBias *60));

		// create rrule for STD zone
		icalrecurrencetype_clear(&icRec);
		icRec.freq = ICAL_YEARLY_RECURRENCE;
		icRec.interval = 1;

		icRec.by_month[0] = tsTimeZone.stStdDate.wMonth;
		icRec.by_month[1] = ICAL_RECURRENCE_ARRAY_MAX;

		icRec.week_start = ICAL_SUNDAY_WEEKDAY;

		// by_day[0] % 8 = weekday, by_day[0]/8 = Nth week, 0 is 'any', and -1 = last
		icRec.by_day[0] = tsTimeZone.stStdDate.wDay == 5 ? -1*(8+tsTimeZone.stStdDate.wDayOfWeek+1) : (tsTimeZone.stStdDate.wDay)*8+tsTimeZone.stStdDate.wDayOfWeek+1;
		icRec.by_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
		
		icalcomponent_add_property(icComp, icalproperty_new_rrule(icRec));
	}
	icalcomponent_add_component(icTZComp, icComp);

	// DST, optional
	if (tsTimeZone.lStdBias != tsTimeZone.lDstBias && tsTimeZone.stStdDate.wMonth != 0 && tsTimeZone.stDstDate.wMonth != 0) {
		icComp = icalcomponent_new_xdaylight();
		icTime = icaltime_from_timet_with_zone(SystemTimeToUnixTime(tsTimeZone.stDstDate), 0, nullptr);
		icalcomponent_add_property(icComp, icalproperty_new_dtstart(icTime));

		icalcomponent_add_property(icComp, icalproperty_new_tzoffsetfrom(-tsTimeZone.lBias *60));
		icalcomponent_add_property(icComp, icalproperty_new_tzoffsetto( ((-tsTimeZone.lBias) + (-tsTimeZone.lDstBias)) *60) );

		// create rrule for DST zone
		icalrecurrencetype_clear(&icRec);
		icRec.freq = ICAL_YEARLY_RECURRENCE;
		icRec.interval = 1;

		icRec.by_month[0] = tsTimeZone.stDstDate.wMonth;
		icRec.by_month[1] = ICAL_RECURRENCE_ARRAY_MAX;

		icRec.week_start = ICAL_SUNDAY_WEEKDAY;

		icRec.by_day[0] = tsTimeZone.stDstDate.wDay == 5 ? -1*(8+tsTimeZone.stDstDate.wDayOfWeek+1) : (tsTimeZone.stDstDate.wDay)*8+tsTimeZone.stDstDate.wDayOfWeek+1;
		icRec.by_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
		
		icalcomponent_add_property(icComp, icalproperty_new_rrule(icRec));

		icalcomponent_add_component(icTZComp, icComp);
	}

	*lppVTZComp = icTZComp;
	return hrSuccess;
}

/**
 * Returns TIMEZONE_STRUCT structure from the string Olson city name.
 * Function searches zoneinfo data in linux
 *
 * @param[in]	strTimezone					timezone string
 * @param[out]	ttTimeZone					TIMEZONE_STRUCT of the cityname
 * @return		MAPI error code
 * @retval		MAPI_E_INVALID_PARAMETER	strTimezone is empty
 * @retval		MAPI_E_NOT_FOUND			cannot find the timezone string in zoneinfo
 */
HRESULT HrGetTzStruct(const std::string &strTimezone, TIMEZONE_STRUCT *ttTimeZone)
{
	if (strTimezone.empty())
		return MAPI_E_INVALID_PARAMETER;
	auto lpicTimeZone = icaltimezone_get_builtin_timezone(strTimezone.c_str());
	if (lpicTimeZone == NULL)
		return MAPI_E_NOT_FOUND;
	auto lpicComponent = icaltimezone_get_component(lpicTimeZone);
	if (lpicComponent == NULL)
		return MAPI_E_NOT_FOUND;
	return HrParseVTimeZone(lpicComponent, NULL, ttTimeZone);
}

} /* namespace */
