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
#include <memory>
#include "icalrecurrence.h"
#include "vconverter.h"
#include "nameids.h"
#include "valarm.h"
#include <mapicode.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <mapix.h>
#include <mapiutil.h>
#include <cmath>
#include <algorithm>
#include "freebusy.h"
#include "icalcompat.hpp"

using namespace KCHL;

static bool operator ==(const SPropValue &spv, ULONG ulPropTag)
{
	return spv.ulPropTag == ulPropTag;
}

namespace KC {

/**
 * Parses ical RRULE and convert it to mapi recurrence
 *
 * @param[in]	sTimeZone		Timezone structure	
 * @param[in]	lpicRootEvent	Ical VCALENDAR component
 * @param[in]	lpicEvent		Ical VEVENT component containing the RRULE
 * @param[in]	bIsAllday		Allday status of the event
 * @param[in]	lpNamedProps	Named property tag array
 * @param[out]	lpIcalItem		Structure in which mapi properties are set
 * @return		MAPI error code
 * @retval		MAPI_E_NOT_FOUND	Start or end time not found in ical data 
 */
HRESULT ICalRecurrence::HrParseICalRecurrenceRule(const TIMEZONE_STRUCT &sTimeZone,
    icalcomponent *lpicRootEvent, icalcomponent *lpicEvent, bool bIsAllday,
    const SPropTagArray *lpNamedProps, icalitem *lpIcalItem)
{
	HRESULT hr = hrSuccess;
	int i = 0;
	ULONG ulWeekDays = 0;	
	time_t dtUTCEnd = 0;
	time_t dtUTCUntil = 0;
	time_t exUTCDate = 0;
	time_t exLocalDate = 0;
	SPropValue sPropVal = {0};
	struct tm tm = {0};

	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_RRULE_PROPERTY);
	if (lpicProp == NULL)
		return hr;
	auto icRRule = icalproperty_get_rrule(lpicProp);
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTSTART_PROPERTY);
	if (lpicProp == nullptr)
		return MAPI_E_NOT_FOUND;
	
	// use localtime for calculating weekday as in UTC time 
	// the weekday can change to previous day for time 00:00 am
	auto dtLocalStart = icaltime_as_timet(icalproperty_get_dtstart(lpicProp));
	gmtime_safe(&dtLocalStart, &tm);
	auto dtUTCStart = ICalTimeTypeToUTC(lpicRootEvent, lpicProp);

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTEND_PROPERTY);
	if (lpicProp == nullptr)
		/* check for the task's DUE property. */
		lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DUE_PROPERTY);
	if (!lpicProp)
	{
		// check for duration property
		lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DURATION_PROPERTY);
		if (lpicProp == nullptr)
			return MAPI_E_NOT_FOUND;
		dtUTCEnd = dtUTCStart + icaldurationtype_as_int(icalproperty_get_duration(lpicProp));
	} else {
		dtUTCEnd = ICalTimeTypeToUTC(lpicRootEvent, lpicProp);
	}

	std::unique_ptr<recurrence> lpRec(new recurrence);
	// recurrence class contains LOCAL times only, so convert UTC -> LOCAL
	lpRec->setStartDateTime(dtLocalStart);

	// default 1st day of week is sunday, except in weekly recurrences
	lpRec->setFirstDOW(0);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCETYPE], PT_LONG);
	switch (icRRule.freq) {
	case ICAL_DAILY_RECURRENCE:
		sPropVal.Value.ul = 1;

		lpRec->setFrequency(recurrence::DAILY);
		break;

	case ICAL_WEEKLY_RECURRENCE:
		sPropVal.Value.ul = 2;

		lpRec->setFrequency(recurrence::WEEKLY);
		// assume this weekly item is exactly on the start time day
		lpRec->setWeekDays(1 << tm.tm_wday);
		// Strange little thing for the recurrence type "every workday"
		lpRec->setFirstDOW(1);
		break;

	case ICAL_MONTHLY_RECURRENCE:
		sPropVal.Value.ul = 3;

		lpRec->setFrequency(recurrence::MONTHLY);
		break;

	case ICAL_YEARLY_RECURRENCE:
		sPropVal.Value.ul = 4;

		lpRec->setFrequency(recurrence::YEARLY);
		lpRec->setDayOfMonth(tm.tm_mday);
		if (icRRule.by_month[0] != ICAL_RECURRENCE_ARRAY_MAX) 
			lpRec->setMonth(icRRule.by_month[0]);
		break;
	default:
		return MAPI_E_INVALID_PARAMETER;
	};
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// since we know the frequency, this value can be set correctly
	lpRec->setInterval(icRRule.interval);

	// ulWeekDays, ulWeekNumber (monthly)
	if (icRRule.by_day[i] != ICAL_RECURRENCE_ARRAY_MAX) {
		ulWeekDays = 0;

		if (icRRule.by_day[0] < 0) {
			// outlook can only have _one_ last day of the month/year (not daily or weekly here!)
			auto dy = abs(icRRule.by_day[0] % 8);
			if (dy != 0)
				ulWeekDays = 1 << (dy - 1);
			else
				ulWeekDays = 0b1111111;
			// next call also changes pattern to 3!
			lpRec->setWeekNumber(5);
		} else if (icRRule.by_day[0] >= 1 && icRRule.by_day[0] <= 7) {
			// weekly, normal days
			i = 0;
			while (icRRule.by_day[i] != ICAL_RECURRENCE_ARRAY_MAX) {
				ulWeekDays |= (1 << (icRRule.by_day[i] - 1));
				++i;
			}
			// handle the BYSETPOS value
			if (icRRule.by_set_pos[0] != ICAL_RECURRENCE_ARRAY_MAX) {
				// Monthly every Nth [mo/to/we/th/fr/sa/su] day
				lpRec->setWeekNumber(icRRule.by_set_pos[0]);
			} else if (lpRec->getFrequency() == recurrence::MONTHLY) {
				// A monthly every [mo/tu/we/th/fr/sa/su]day is not supported in outlook. but this is the same as 
				// weekly x day so convert this item to weekly x day

				lpRec->setFrequency(recurrence::WEEKLY);
				// assume this weekly item is exactly on the start time day
				lpRec->setWeekDays(1 << tm.tm_wday);
				// Strange little thing for the recurrence type "every workday"
				lpRec->setFirstDOW(1);

				sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCETYPE], PT_LONG);
				sPropVal.Value.ul = 2;
				lpIcalItem->lstMsgProps.emplace_back(sPropVal);
			} else if (lpRec->getFrequency() == recurrence::YEARLY) {
				return MAPI_E_NO_SUPPORT;
			}
		} else {
			// monthly, first sunday: 9, monday: 10
			auto dy = icRRule.by_day[0] % 8;
			if (dy != 0)
				ulWeekDays = 1 << (dy - 1);
			else
				ulWeekDays = 0b1111111;
			lpRec->setWeekNumber((int)(icRRule.by_day[0]/8)); // 1..4
		}
		lpRec->setWeekDays(ulWeekDays);
	} else if (icRRule.by_month_day[0] != ICAL_RECURRENCE_ARRAY_MAX) {
		// outlook can only have _one_ day in the month/year, while ical can do multiple
		// do we need to create multiple items here ?? :(
		lpRec->setDayOfMonth(icRRule.by_month_day[0]);
	}else if (icRRule.freq == ICAL_MONTHLY_RECURRENCE || icRRule.freq == ICAL_YEARLY_RECURRENCE) {
		// When RRULE:FREQ=MONTHLY;INTERVAL=1 is set then day of the month is set from the start date.		
		lpRec->setDayOfMonth(tm.tm_mday);
	}

	lpRec->setStartDateTime(lpRec->calcStartDate());

	if (icRRule.count != 0) {
		// count limit
		lpRec->setEndType(recurrence::NUMBER);
		lpRec->setCount(icRRule.count);

		// calculate end
		lpRec->setEndDate(lpRec->calcEndDate());
		dtUTCUntil = lpRec->getEndDate();
	} else if (icRRule.until.year != 0) {
		// date limit
		lpRec->setEndType(recurrence::DATE);
		// enddate is the date/time of the LAST occurrence, do not add duration
		dtUTCUntil = icaltime_as_timet_with_zone(icRRule.until, NULL);
		lpRec->setEndDate(UTCToLocal(dtUTCUntil, sTimeZone));

		// calculate number
		lpRec->setCount(lpRec->calcCount());
	} else {
		// never ending story
		lpRec->setEndType(recurrence::NEVER);
		/* Outlook also sets 10, so this gets displayed in the recurrence dialog window. */
		lpRec->setCount(10);
		// 1. dtUTCEnd -> end time offset (set later)
		// (date will overridden by recurrence.cpp when writing the blob)

		// 2. dtUTCUntil = clipend == start of month of item in 4500
		dtUTCUntil = 0x7FFFFFFF; // incorrect, but good enough
	}

	// offset in minutes after midnight of the start time
	lpRec->setEndTimeOffset((lpRec->getStartTimeOffset() + dtUTCEnd - dtUTCStart) / 60);

	// Set 0x8236, also known as ClipEnd in OutlookSpy
	UnixTimeToFileTime(dtUTCUntil, &sPropVal.Value.ft);
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCE_END], PT_SYSTIME);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// find EXDATE properties, add to delete exceptions
	for (lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_EXDATE_PROPERTY);
	     lpicProp != nullptr; lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_EXDATE_PROPERTY))
	{
		exUTCDate = ICalTimeTypeToUTC(lpicRootEvent, lpicProp);
		exLocalDate = UTCToLocal(exUTCDate, sTimeZone);

		lpRec->addDeletedException(exLocalDate);
	}

	// now that we have a full recurrence object, recalculate the end time, see ZCP-9143
	if (lpRec->getEndType() == recurrence::DATE)
	{
		memory_ptr<OccrInfo> lpOccrInfo;
		ULONG cValues = 0;

		hr = lpRec->HrGetItems(dtUTCStart, dtUTCUntil, sTimeZone, 0, &~lpOccrInfo, &cValues, true);
		if (hr == hrSuccess && cValues > 0) {
			dtUTCUntil = lpOccrInfo[cValues-1].tBaseDate;
			lpRec->setEndDate(UTCToLocal(dtUTCUntil, sTimeZone));
		}
	}

	// set named prop 0x8510 to 353, needed for Outlook to ask for single or total recurrence when deleting
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_SIDEEFFECT], PT_LONG);
	sPropVal.Value.ul = 353;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// Set 0x8235, also known as ClipStart in OutlookSpy
	UnixTimeToFileTime(LocalToUTC(recurrence::StartOfDay(lpRec->getStartDateTime()), sTimeZone), &sPropVal.Value.ft);
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCE_START], PT_SYSTIME);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_RECURRENCEID_PROPERTY);
	if(!lpicProp) {
		UnixTimeToFileTime(LocalToUTC(lpRec->getStartDateTime(), sTimeZone), &sPropVal.Value.ft);

		// Set 0x820D / ApptStartWhole
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTSTARTWHOLE], PT_SYSTIME);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);

		// Set 0x8516 / CommonStart
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_COMMONSTART], PT_SYSTIME);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
		UnixTimeToFileTime(LocalToUTC(lpRec->getStartDateTime() + (dtUTCEnd - dtUTCStart), sTimeZone), &sPropVal.Value.ft);

		// Set 0x820E / ApptEndWhole
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTENDWHOLE], PT_SYSTIME);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);

		// Set CommonEnd		
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_COMMONEND], PT_SYSTIME);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	}
	lpIcalItem->lpRecurrence = std::move(lpRec);
	return hr;
}
/**
 * Parses ical exception and sets Mapi propertis in icalitem::exception structure
 * 
 * @param[in]		lpEventRoot		ical VCALENDAR component
 * @param[in]		lpicEvent		ical VEVENT component
 * @param[in,out]	lpIcalItem		icalitem in which recurrence structure is set
 * @param[in]		bIsAllDay		Allday status of the exception
 * @param[in]		lpNamedProps	named property tag array
 * @param[in]		strCharset		charset to convert to
 * @param[out]		lpEx			exception structure in which mapi properties are set
 *
 * @return			MAPI error code
 * @retval			MAPI_E_NOT_FOUND	start time or end time of event is not set in ical data
 */
HRESULT ICalRecurrence::HrMakeMAPIException(icalcomponent *lpEventRoot,
    icalcomponent *lpicEvent, icalitem *lpIcalItem, bool bIsAllDay,
    SPropTagArray *lpNamedProps, const std::string &strCharset,
    icalitem::exception *lpEx)
{
	ULONG ulId = 0;
	SPropValue sPropVal;
	bool bXMS = false;
	LONG ulRemindBefore = 0;
	time_t ttReminderTime = 0;
	bool bReminderSet = false;
	convert_context converter;
	bool abOldPresent[8] = {false};
	bool abNewPresent[8] = {false};
	SizedSPropTagArray(8, sptaCopy) = { 8, {
			PR_SUBJECT,
			CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_LOCATION], PT_STRING8),
			PR_BODY,
			CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_LABEL], PT_LONG),
			CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERSET], PT_BOOLEAN),
			CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERMINUTESBEFORESTART], PT_LONG),
			CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN),
			CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_BUSYSTATUS], PT_LONG)
	} };
	bool bOldIsAllDay = false;

	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_RECURRENCEID_PROPERTY);
	if (lpicProp == NULL)
		// you tricked me! it's not an exception at all!
		return MAPI_E_NOT_FOUND;
	auto ttOriginalUtcTime = ICalTimeTypeToUTC(lpEventRoot, lpicProp);
	auto ttOriginalLocalTime = icaltime_as_timet(icalvalue_get_datetime(icalproperty_get_value(lpicProp)));

	lpEx->tBaseDate = ttOriginalUtcTime;

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTSTART_PROPERTY);
	if (lpicProp == NULL)
		return MAPI_E_NOT_FOUND;
	auto ttStartUtcTime = ICalTimeTypeToUTC(lpEventRoot, lpicProp);
	auto ttStartLocalTime = icaltime_as_timet(icalvalue_get_datetime(icalproperty_get_value(lpicProp)));

	lpEx->tStartDate = ttStartUtcTime;

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTEND_PROPERTY);
	if (lpicProp == NULL)
		return MAPI_E_NOT_FOUND;
	auto ttEndUtcTime = ICalTimeTypeToUTC(lpEventRoot, lpicProp);
	auto ttEndLocalTime = icaltime_as_timet(icalvalue_get_datetime(icalproperty_get_value(lpicProp)));
	auto hr = lpIcalItem->lpRecurrence->addModifiedException(ttStartLocalTime, ttEndLocalTime, ttOriginalLocalTime, &ulId);
	if (hr != hrSuccess)
		return hr;

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRINGBASE], PT_SYSTIME);
	UnixTimeToFileTime(ttOriginalUtcTime, &sPropVal.Value.ft);
	lpEx->lstMsgProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_EXCEPTION_STARTTIME;
	UnixTimeToFileTime(ttStartLocalTime, &sPropVal.Value.ft);
	lpEx->lstAttachProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_EXCEPTION_ENDTIME;
	UnixTimeToFileTime(ttEndLocalTime, &sPropVal.Value.ft);
	lpEx->lstAttachProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTSTARTWHOLE], PT_SYSTIME);
	UnixTimeToFileTime(ttStartUtcTime, &sPropVal.Value.ft);	
	lpEx->lstMsgProps.emplace_back(sPropVal);
	
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_COMMONSTART], PT_SYSTIME);
	lpEx->lstMsgProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCE_START], PT_SYSTIME);
	lpEx->lstMsgProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTENDWHOLE], PT_SYSTIME);
	UnixTimeToFileTime(ttEndUtcTime, &sPropVal.Value.ft);	
	lpEx->lstMsgProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_COMMONEND], PT_SYSTIME);
	lpEx->lstMsgProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCE_END], PT_SYSTIME);
	lpEx->lstMsgProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_EXCEPTION_ENDTIME;
	UnixTimeToFileTime(ttEndLocalTime, &sPropVal.Value.ft);
	lpEx->lstAttachProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_DISPLAY_NAME_W;
	HrCopyString(lpIcalItem->base, L"Untitled", &sPropVal.Value.lpszW);
	lpEx->lstAttachProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_ATTACH_METHOD;
	sPropVal.Value.ul = ATTACH_EMBEDDED_MSG;
	lpEx->lstAttachProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_ATTACH_FLAGS;
	sPropVal.Value.ul = 0;
	lpEx->lstAttachProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_ATTACHMENT_LINKID;
	sPropVal.Value.ul = 0;
	lpEx->lstAttachProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_ATTACHMENT_HIDDEN;
	sPropVal.Value.b = TRUE;
	lpEx->lstAttachProps.emplace_back(sPropVal);
	
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_HIDE_ATTACH], PT_BOOLEAN);;
	sPropVal.Value.b = TRUE;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	
	sPropVal.ulPropTag = PR_ATTACHMENT_FLAGS;
	sPropVal.Value.ul = 2;
	lpEx->lstAttachProps.emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_MESSAGE_CLASS_W;
	HrCopyString(lpIcalItem->base, L"IPM.OLE.CLASS.{00061055-0000-0000-C000-000000000046}", &sPropVal.Value.lpszW);
	lpEx->lstMsgProps.emplace_back(sPropVal);

	// copy properties to exception and test if changed
	for (const auto &prop : lpIcalItem->lstMsgProps)
		for (ULONG i = 0; i < sptaCopy.cValues; ++i) {
			if (sptaCopy.aulPropTag[i] != prop.ulPropTag)
				continue;
			abOldPresent[i] = true;
			if (sptaCopy.aulPropTag[i] != PR_BODY) // no need to copy body
				lpEx->lstMsgProps.emplace_back(prop);
			if (sptaCopy.aulPropTag[i] == CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN))
				bOldIsAllDay = prop.Value.b; // remember allday event status
			break;
		}

	// find exceptional properties
	// TODO: should actually look at original message, and check for differences
	for (lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_ANY_PROPERTY);
	     lpicProp != nullptr;
	     lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_ANY_PROPERTY))
	{
		switch (icalproperty_isa(lpicProp)) {
		case ICAL_SUMMARY_PROPERTY: {
			auto lpszProp = icalproperty_get_summary(lpicProp);
			auto strIcalProp = converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), strCharset.c_str());
			hr = lpIcalItem->lpRecurrence->setModifiedSubject(ulId, strIcalProp.c_str());
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = PR_SUBJECT_W;
			HrCopyString(lpIcalItem->base, strIcalProp.c_str(), &sPropVal.Value.lpszW);
			lpEx->lstMsgProps.emplace_back(sPropVal);
			abNewPresent[0] = true;
			break;
		}
		case ICAL_LOCATION_PROPERTY: {
			auto lpszProp = icalproperty_get_location(lpicProp);
			auto strIcalProp = converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), strCharset.c_str());
			hr = lpIcalItem->lpRecurrence->setModifiedLocation(ulId, strIcalProp.c_str());
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_LOCATION], PT_UNICODE);
			HrCopyString(lpIcalItem->base, strIcalProp.c_str(), &sPropVal.Value.lpszW);
			lpEx->lstMsgProps.emplace_back(sPropVal);
			abNewPresent[1] = true;
			break;
		}
		case ICAL_TRANSP_PROPERTY: {
			if (bXMS)
				break;
			ULONG ulBusyStatus = 2;	// default busy
			switch (icalproperty_get_transp(lpicProp)) {
			case ICAL_TRANSP_TRANSPARENT:
				ulBusyStatus = 0;
				break;
			case ICAL_TRANSP_OPAQUE:
				ulBusyStatus = 2;
				break;
			default:
				break;
			}
			hr = lpIcalItem->lpRecurrence->setModifiedBusyStatus(ulId, ulBusyStatus);
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_BUSYSTATUS], PT_LONG);
			sPropVal.Value.ul = ulBusyStatus;
			lpEx->lstMsgProps.emplace_back(sPropVal);
			abNewPresent[7] = true;
			break;
		}
		case ICAL_X_PROPERTY: {
			if (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-BUSYSTATUS") != 0)
				break;
			ULONG ulBusyStatus = 2;	// default busy
			const char *lpszIcalProp = icalproperty_get_x(lpicProp);
			if (strcmp(lpszIcalProp, "FREE") == 0)
				ulBusyStatus = 0;
			else if (strcmp(lpszIcalProp, "TENTATIVE") == 0)
				ulBusyStatus = 1;
			else if (strcmp(lpszIcalProp, "OOF") == 0)
				ulBusyStatus = 3;
			hr = lpIcalItem->lpRecurrence->setModifiedBusyStatus(ulId, ulBusyStatus);
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_BUSYSTATUS], PT_LONG);
			sPropVal.Value.ul = ulBusyStatus;
			lpEx->lstMsgProps.emplace_back(sPropVal);
			bXMS = true;
			abNewPresent[7] = true;
			break;
		}
		case ICAL_DESCRIPTION_PROPERTY: {
			auto lpszProp = icalproperty_get_description(lpicProp);
			auto strIcalProp = converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), strCharset.c_str());
			hr = lpIcalItem->lpRecurrence->setModifiedBody(ulId);
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = PR_BODY_W;
			HrCopyString(lpIcalItem->base, strIcalProp.c_str(), &sPropVal.Value.lpszW);
			lpEx->lstMsgProps.emplace_back(sPropVal);
			abNewPresent[2] = true;
			break;
		}
		default:
			// ignore property
			break;
		};
	}

	// make sure these are not removed :| (body, label, reminderset, reminder minutes)
	abNewPresent[2] = abNewPresent[3] = abNewPresent[4] = abNewPresent[5] = true;
	// test if properties were just removed
	for (ULONG i = 0; i < sptaCopy.cValues; ++i) {
		if (!abOldPresent[i] || abNewPresent[i])
			continue;
		auto iProp = find(lpEx->lstMsgProps.begin(), lpEx->lstMsgProps.end(), sptaCopy.aulPropTag[i]);
		if (iProp == lpEx->lstMsgProps.cend())
			continue;
		lpEx->lstMsgProps.erase(iProp);
		switch (i) {
		case 0:
			// subject
			hr = lpIcalItem->lpRecurrence->setModifiedSubject(ulId, std::wstring());
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = PR_SUBJECT_W;
			sPropVal.Value.lpszW = const_cast<wchar_t *>(L"");
			lpEx->lstMsgProps.emplace_back(sPropVal);
			break;
		case 1:
			// location
			hr = lpIcalItem->lpRecurrence->setModifiedLocation(ulId, std::wstring());
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_LOCATION], PT_UNICODE);
			sPropVal.Value.lpszW = const_cast<wchar_t *>(L"");
			lpEx->lstMsgProps.emplace_back(sPropVal);
			break;
		case 2:
			// body, ignore!
			break;
		case 3:
			// label, ignore
			break;
		case 4:
			// reminder set, ignore
			break;
		case 5:
			// reminder minutes, ignore
			break;
		case 6:
			// allday event
			if (bIsAllDay == bOldIsAllDay)
				break;
			// flip all day status
			hr = lpIcalItem->lpRecurrence->setModifiedSubType(ulId, 1);
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN);
			sPropVal.Value.ul = !bOldIsAllDay;
			lpEx->lstMsgProps.emplace_back(sPropVal);
			break;
		case 7:
			// busy status, default: busy
			hr = lpIcalItem->lpRecurrence->setModifiedBusyStatus(ulId, 1);
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_BUSYSTATUS], PT_LONG);
			sPropVal.Value.ul = 1;
			lpEx->lstMsgProps.emplace_back(sPropVal);
			break;
		}
	}

	// reminderset (flip reminder on/off) and reminderdelta (offset time change) in vevent component of this item
	auto lpicAlarm = icalcomponent_get_first_component(lpicEvent, ICAL_VALARM_COMPONENT);
	if (lpicAlarm) {
		hr = HrParseVAlarm(lpicAlarm, &ulRemindBefore, &ttReminderTime, &bReminderSet);
		if (hr == hrSuccess) {
			// abOldPresent[4] == reminderset marker in original message
			if (bReminderSet != abOldPresent[4]) {
				hr = lpIcalItem->lpRecurrence->setModifiedReminder(ulId, bReminderSet);
				if (hr != hrSuccess)
					return hr;

				sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERSET], PT_BOOLEAN);
				sPropVal.Value.b = bReminderSet;
				lpEx->lstMsgProps.emplace_back(sPropVal);
				if (ttReminderTime == 0)
					ttReminderTime = ttStartLocalTime;

				sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERTIME], PT_SYSTIME);
				UnixTimeToFileTime(ttReminderTime, &sPropVal.Value.ft);
				lpEx->lstMsgProps.emplace_back(sPropVal);
			}

			hr = lpIcalItem->lpRecurrence->setModifiedReminderDelta(ulId, ulRemindBefore);
			if (hr != hrSuccess)
				return hr;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERMINUTESBEFORESTART], PT_LONG);
			sPropVal.Value.ul = ulRemindBefore;
			lpEx->lstMsgProps.emplace_back(sPropVal);
		}
	} else if (abOldPresent[4]) {
		// disable reminder in attachment
		hr = lpIcalItem->lpRecurrence->setModifiedReminder(ulId, 0);
		if (hr != hrSuccess)
			return hr;
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERSET], PT_BOOLEAN);
		sPropVal.Value.b = FALSE;
		lpEx->lstMsgProps.emplace_back(sPropVal);
	}
	return hr;
}

/**
 * Finalizes recurrence struct with exceptions into the mapi message
 *
 * @param[in]		lpRecurrence	recurrence object
 * @param[in]		lpNamedProps	Named property tag array
 * @param[in,out]	lpMessage		The mapi message in which the recurrence is stored
 * @return			MAPI error code
 * @retval			MAPI_E_CORRUPT_DATA		Invalid recurrence state	
 */
HRESULT ICalRecurrence::HrMakeMAPIRecurrence(recurrence *lpRecurrence, LPSPropTagArray lpNamedProps, LPMESSAGE lpMessage)
{
	memory_ptr<char> lpRecBlob;
	unsigned int ulRecBlob = 0;
	memory_ptr<SPropValue> lpsPropRecPattern;
	std::string strHRS;

	auto hr = lpRecurrence->HrGetRecurrenceState(&~lpRecBlob, &ulRecBlob);
	if (hr != hrSuccess)
		return hr;
	hr = lpRecurrence->HrGetHumanReadableString(&strHRS);
	if (hr != hrSuccess)
		return hr;
	// adjust number of props
	SPropValue pv[4];
	pv[0].ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRING], PT_BOOLEAN);
	pv[0].Value.b = TRUE;

	// TODO: combine with icon index in vevent .. the item may be a meeting request (meeting+recurring==1027)
	pv[1].ulPropTag = PR_ICON_INDEX;
	pv[1].Value.ul = ICON_APPT_RECURRING;
	pv[2].ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCESTATE], PT_BINARY);
	pv[2].Value.bin.lpb = reinterpret_cast<BYTE *>(lpRecBlob.get());
	pv[2].Value.bin.cb = ulRecBlob;

	unsigned int i = 3;
	hr = HrGetOneProp(lpMessage, CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCEPATTERN], PT_STRING8), &~lpsPropRecPattern);
	if(hr != hrSuccess)
	{
		pv[i].ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCEPATTERN], PT_STRING8);
		pv[i].Value.lpszA = const_cast<char *>(strHRS.c_str());
		++i;
	}
	return lpMessage->SetProps(i, pv, nullptr);
}

/**
 * Check if the exception is valid and according to outlook behaviour
 *
 * @param[in]	lpItem		structure containing mapi properties
 * @param[in]	lpEx		Structure containing mapi properties of exception 
 * @return		True if the occurrence is valid else false
 */
bool ICalRecurrence::HrValidateOccurrence(icalitem *lpItem, icalitem::exception lpEx)
{
	HRESULT hr = hrSuccess;
	memory_ptr<OccrInfo> lpFBBlocksAll;
	ULONG cValues = 0;
	time_t tBaseDateStart = LocalToUTC(lpItem->lpRecurrence->StartOfDay(UTCToLocal(lpEx.tBaseDate, lpItem->tTZinfo)), lpItem->tTZinfo);
	time_t tStartDateStart = LocalToUTC(lpItem->lpRecurrence->StartOfDay(UTCToLocal(lpEx.tStartDate, lpItem->tTZinfo)), lpItem->tTZinfo);

	if (tBaseDateStart < tStartDateStart)
		hr = lpItem->lpRecurrence->HrGetItems(tBaseDateStart, tStartDateStart + 1439 * 60, lpItem->tTZinfo, lpItem->ulFbStatus, &~lpFBBlocksAll, &cValues);
	else
		hr = lpItem->lpRecurrence->HrGetItems(tStartDateStart, tBaseDateStart + 1439 * 60, lpItem->tTZinfo, lpItem->ulFbStatus, &~lpFBBlocksAll, &cValues);
	if (hr != hrSuccess)
		return false;
	return cValues == 1;
}

/**
 * Sets recurrence in ical data and exdate for exceptions
 *
 * @param[in]		sTimezone		timezone structure 
 * @param[in]		bIsAllDay		boolean to specify if event is allday or not
 * @param[in]		lpRecurrence	Mapi recurrence strucuture
 * @param[in,out]	lpicEvent		ical component to which recurrence and exdates are added
 * @return			MAPI error code
 * @retval			MAPI_E_INVALID_PARAMETER	Invalid recurrence type is set in mapi recurrence
 */
HRESULT ICalRecurrence::HrCreateICalRecurrence(const TIMEZONE_STRUCT &sTimeZone,
    bool bIsAllDay, recurrence *lpRecurrence, icalcomponent *lpicEvent)
{
	icalrecurrencetype icRRule;
	icaltimetype ittExDate;
	TIMEZONE_STRUCT sTZgmt = {0};

	HRESULT hr = HrCreateICalRecurrenceType(sTimeZone, bIsAllDay, lpRecurrence, &icRRule);
	if (hr != hrSuccess)
		return hr;

	icalcomponent_add_property(lpicEvent, icalproperty_new_rrule(icRRule));

	// all delete exceptions are in the delete list,
	auto lstExceptions = lpRecurrence->getDeletedExceptions();
	if (lstExceptions.empty())
		return hrSuccess;

	// add EXDATE props
	for (const auto &exc : lstExceptions) {
		if (bIsAllDay)
			ittExDate = icaltime_from_timet_with_zone(LocalToUTC(exc, sTZgmt), bIsAllDay, nullptr);
		else
			ittExDate = icaltime_from_timet_with_zone(LocalToUTC(exc, sTimeZone), 0, nullptr);
		kc_ical_utc(ittExDate, true);
		icalcomponent_add_property(lpicEvent, icalproperty_new_exdate(ittExDate));
	}
	// modified exceptions are done by the caller because of the attachments with info
	return hrSuccess;
}

/**
 * Creates ical recurrence		structure from mapi recurrence.
 *
 * @param[in]	sTimeZone		Timezone structure, unused currently
 * @param[in]	bIsAllday		Flag that specifies if the recurrence is all day
 * @param[in]	lpRecurrence	mapi recurrence structure to be converted
 * @param[out]	lpicRRule		ical recurrence structure to be returned
 * @return		MAPI error code
 * @retval		MAPI_E_INVALID_PARAMETER	invalid recurrence type is set in mapi recurrence structure
 */
HRESULT ICalRecurrence::HrCreateICalRecurrenceType(const TIMEZONE_STRUCT &sTimeZone,
    bool bIsAllday, recurrence *lpRecurrence, icalrecurrencetype *lpicRRule)
{
	struct icalrecurrencetype icRec;

	icalrecurrencetype_clear(&icRec);

	switch (lpRecurrence->getFrequency()) {
	case recurrence::DAILY:
		icRec.freq = ICAL_DAILY_RECURRENCE;
		// only weekdays selected in outlook
		if (!lpRecurrence->getWeekDays())
			break;
		// iCal.app does not have daily-weekday type of recurrence
		// so Daily-weekdays is converted to weekly recurrence
		icRec.freq = ICAL_WEEKLY_RECURRENCE;
		WeekDaysToICalArray(lpRecurrence->getWeekDays(), &icRec);
		break;
	case recurrence::WEEKLY:
		icRec.freq = ICAL_WEEKLY_RECURRENCE;
		WeekDaysToICalArray(lpRecurrence->getWeekDays(), &icRec);
		break;
	case recurrence::MONTHLY:
		icRec.freq = ICAL_MONTHLY_RECURRENCE;
		if (lpRecurrence->getWeekNumber() == 0) {
			// mapi patterntype == 2
			icRec.by_month_day[0] = lpRecurrence->getDayOfMonth();
			icRec.by_month_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
			break;
		}
		// mapi patterntype == 3
		// only 1 day should be set!
		if (lpRecurrence->getWeekDays() == 127) {
			// All Weekdays are set for recurrence type "second" "day" of month.
			// SU,MO,TU,WE,TH,FR,SA -> weekdays = 127.
			icRec.by_month_day[0] = lpRecurrence->getWeekNumber() == 5 ? -1 : lpRecurrence->getWeekNumber(); // hack to handle nth day month type of rec.
			icRec.by_month_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
			break;
		} else if (lpRecurrence->getWeekDays() == 62 || lpRecurrence->getWeekDays() == 65) {
			// Recurrence of type '3rd weekday'/'last weekend'
			// MO,TU,WE,TH,FR -> 62 and SU,SA -> 65
			icRec.by_set_pos[0] = lpRecurrence->getWeekNumber();
			icRec.by_set_pos[1] = ICAL_RECURRENCE_ARRAY_MAX;
			WeekDaysToICalArray(lpRecurrence->getWeekDays(), &icRec);
		} else if (lpRecurrence->getWeekNumber() == 5) {
			icRec.by_day[0] = (round(log((double)lpRecurrence->getWeekDays()) / log(2.0)) + 8 + 1) * -1;  // corrected last weekday
			icRec.by_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
		} else {
			icRec.by_day[0] = round(log((double)lpRecurrence->getWeekDays()) / log(2.0)) + (8 * lpRecurrence->getWeekNumber()) + 1; // +1 because outlook starts on sunday
			icRec.by_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
		}
		break;
	case recurrence::YEARLY:
		icRec.freq = ICAL_YEARLY_RECURRENCE;
		if (lpRecurrence->getWeekNumber() == 0) {
			// mapi patterntype == 2
			icRec.by_month_day[0] = lpRecurrence->getDayOfMonth();
			icRec.by_month_day[1] = ICAL_RECURRENCE_ARRAY_MAX;

			icRec.by_month[0] = lpRecurrence->getMonth();
			icRec.by_month[1] = ICAL_RECURRENCE_ARRAY_MAX;
			break;
		}
		// mapi patterntype == 3
		// only 1 day should be set!
		if (lpRecurrence->getWeekNumber() == 5)
			icRec.by_day[0] = ((log((double)lpRecurrence->getWeekDays())/log(2.0)) + 8 + 1 ) * -1;
		else
			icRec.by_day[0] = (int)(log((double)lpRecurrence->getWeekDays())/log(2.0)) + (8 * lpRecurrence->getWeekNumber() ) +1; // +1 because outlook starts on sunday
		icRec.by_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
		icRec.by_month[0] = lpRecurrence->getMonth();
		icRec.by_month[1] = ICAL_RECURRENCE_ARRAY_MAX;
		break;
	default:
		return MAPI_E_INVALID_PARAMETER;
	}

	icRec.interval = lpRecurrence->getInterval();

	switch (lpRecurrence->getEndType()) {
	case recurrence::DATE:
		/* From the RFC:
		   
		   The UNTIL rule part defines a date-time value which bounds the
		   recurrence rule in an inclusive manner. If the value specified by
		   UNTIL is synchronized with the specified recurrence, this date or
		   date-time becomes the last instance of the recurrence. If specified
		   as a date-time value, then it MUST be specified in an UTC time
		   format.
		*/
		icRec.count = 0;
		// if untiltime is saved as UTC it breaks last occurrence.
		icRec.until = icaltime_from_timet_with_zone(lpRecurrence->getEndDate() + lpRecurrence->getStartTimeOffset(), bIsAllday, nullptr);
		kc_ical_utc(icRec.until, false);
		break;
	case recurrence::NUMBER:
		icRec.count = lpRecurrence->getCount();
		icRec.until = icaltime_null_time();
		break;
	case recurrence::NEVER:
		icRec.count = 0;
		icRec.until = icaltime_null_time();
		break;
	};

	*lpicRRule = icRec;
	return hrSuccess;
}

/**
 * Sets weekdays in ical recurrence from mapi weekdays
 *
 * e.g input: 0x3E == 011 1110, output: MO,TU,WE,TH,FR
 * @param[in]		ulWeekDays		mapi weekdays
 * @param[in,out]	lpRec			ical recurrence in which weekday are set
 * @return			Always returns hrSuccess
 */
HRESULT ICalRecurrence::WeekDaysToICalArray(ULONG ulWeekDays, struct icalrecurrencetype *lpRec)
{
	int j = 0;

	for (int i = 0; i < 7; ++i)
		if ((ulWeekDays >> i) & 1)
			lpRec->by_day[j++] = i+1;
	lpRec->by_day[j] = ICAL_RECURRENCE_ARRAY_MAX;
	return hrSuccess;
}

/**
 * Clones the ical component and removes the properties that differ in exception
 *
 * @param[in]	lpicEvent		The ical component
 * @param[out]	lppicException	The cloned component with some properties removed
 * @return		Always returns hrSuccess 
 */
HRESULT ICalRecurrence::HrMakeICalException(icalcomponent *lpicEvent, icalcomponent **lppicException)
{
	auto lpicException = icalcomponent_new_clone(lpicEvent);
	// these are always different in an exception
	auto lpicProp = icalcomponent_get_first_property(lpicException, ICAL_DTSTART_PROPERTY);
	if (lpicProp) {
		icalcomponent_remove_property(lpicException, lpicProp);
		icalproperty_free(lpicProp);
	}

	lpicProp = icalcomponent_get_first_property(lpicException, ICAL_DTEND_PROPERTY);
	if (lpicProp) {
		icalcomponent_remove_property(lpicException, lpicProp);
		icalproperty_free(lpicProp);
	}

	// exceptions don't have the rrule again
	lpicProp = icalcomponent_get_first_property(lpicException, ICAL_RRULE_PROPERTY);
	if (lpicProp) {
		icalcomponent_remove_property(lpicException, lpicProp);
		icalproperty_free(lpicProp);
	}
	// exceptions don't have the deleted exceptions anymore
	while ((lpicProp = icalcomponent_get_first_property(lpicException, ICAL_EXDATE_PROPERTY)) != NULL) {
		icalcomponent_remove_property(lpicException, lpicProp);
		icalproperty_free(lpicProp);
	}

	// exceptions can't be different in privacy
	lpicProp = icalcomponent_get_first_property(lpicException, ICAL_CLASS_PROPERTY);
	if (lpicProp) {
		icalcomponent_remove_property(lpicException, lpicProp);
		icalproperty_free(lpicProp);
	}

	*lppicException = lpicException;
	return hrSuccess;
}

} /* namespace */
