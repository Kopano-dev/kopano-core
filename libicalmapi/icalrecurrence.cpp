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
#include "icalrecurrence.h"
#include "vconverter.h"
#include "nameids.h"
#include "valarm.h"
#include <mapicode.h>
#include <kopano/mapiext.h>
#include <mapix.h>
#include <mapiutil.h>
#include <cmath>
#include <algorithm>
#include "freebusy.h"
#include "icalcompat.hpp"

static bool operator ==(const SPropValue &spv, ULONG ulPropTag)
{
	return spv.ulPropTag == ulPropTag;
}

/**
 * Parses ical RRULE and convert it to mapi recurrence
 *
 * @param[in]	sTimeZone		Timezone structure	
 * @param[in]	lpicRootEvent	Ical VCALENDAR component
 * @param[in]	lpicEvent		Ical VEVENT component containing the RRULE
 * @param[in]	bIsAllday		Allday status of the event
 * @param[in]	lpNamedProps	Named property tag array
 * @param[in]	lpIcalItem		Structure in which mapi properties are set
 * @return		MAPI error code
 * @retval		MAPI_E_NOT_FOUND	Start or end time not found in ical data 
 */
HRESULT ICalRecurrence::HrParseICalRecurrenceRule(TIMEZONE_STRUCT sTimeZone, icalcomponent *lpicRootEvent, icalcomponent *lpicEvent,
												  bool bIsAllday, LPSPropTagArray lpNamedProps, icalitem *lpIcalItem)
{
	HRESULT hr = hrSuccess;
	recurrence *lpRec = NULL;
	icalproperty *lpicProp = NULL;
	icalrecurrencetype icRRule;
	int i = 0;
	ULONG ulWeekDays = 0;	
	time_t dtUTCStart = 0;
	time_t dtLocalStart = 0;
	time_t dtUTCEnd = 0;
	time_t dtUTCUntil = 0;
	time_t exUTCDate = 0;
	time_t exLocalDate = 0;
	SPropValue sPropVal = {0};
	struct tm tm = {0};

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_RRULE_PROPERTY);
	if (lpicProp == NULL)
		goto exit;

	icRRule = icalproperty_get_rrule(lpicProp);

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTSTART_PROPERTY);
	if (!lpicProp) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	
	// use localtime for calculating weekday as in UTC time 
	// the weekday can change to previous day for time 00:00 am
	dtLocalStart = icaltime_as_timet(icalproperty_get_dtstart(lpicProp));
	gmtime_safe(&dtLocalStart, &tm);
	
	dtUTCStart = ICalTimeTypeToUTC(lpicRootEvent, lpicProp);

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTEND_PROPERTY);
	if (!lpicProp) {
		// check for Task's DUE property.
		lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DUE_PROPERTY);
	}
	if (!lpicProp)
	{
		// check for duration property
		lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DURATION_PROPERTY);
		if (lpicProp) {
			dtUTCEnd = dtUTCStart + icaldurationtype_as_int(icalproperty_get_duration(lpicProp));
		} else {
			hr = MAPI_E_NOT_FOUND;
			goto exit;
		}
	} else {
		dtUTCEnd = ICalTimeTypeToUTC(lpicRootEvent, lpicProp);
	}

	lpRec = new recurrence;

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
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	};
	lpIcalItem->lstMsgProps.push_back(sPropVal);

	// since we know the frequency, this value can be set correctly
	lpRec->setInterval(icRRule.interval);

	// ulWeekDays, ulWeekNumber (monthly)
	if (icRRule.by_day[i] != ICAL_RECURRENCE_ARRAY_MAX) {
		ulWeekDays = 0;

		if (icRRule.by_day[0] < 0) {
			// outlook can only have _one_ last day of the month/year (not daily or weekly here!)
			ulWeekDays |= (1 << (abs(icRRule.by_day[0]%8) - 1));
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
				lpIcalItem->lstMsgProps.push_back(sPropVal);
			} else if (lpRec->getFrequency() == recurrence::YEARLY) {
				hr = MAPI_E_NO_SUPPORT;
				goto exit;
			}
		} else {
			// monthly, first sunday: 9, monday: 10
			ulWeekDays |= (1 << ((icRRule.by_day[0]%8) - 1));
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

		// outlook also sets 10, so this get's displayed in the recurrence dialog window
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
	lpIcalItem->lstMsgProps.push_back(sPropVal);

	// find EXDATE properties, add to delete exceptions
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_EXDATE_PROPERTY);
	while (lpicProp != NULL)
	{
		exUTCDate = ICalTimeTypeToUTC(lpicRootEvent, lpicProp);
		exLocalDate = UTCToLocal(exUTCDate, sTimeZone);

		lpRec->addDeletedException(exLocalDate);

		lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_EXDATE_PROPERTY);
	}

	// now that we have a full recurrence object, recalculate the end time, see ZCP-9143
	if (lpRec->getEndType() == recurrence::DATE)
	{
		OccrInfo *lpOccrInfo = NULL;
		ULONG cValues = 0;

		hr = lpRec->HrGetItems(dtUTCStart, dtUTCUntil, NULL, sTimeZone, 0, &lpOccrInfo, &cValues, true);
		if (hr == hrSuccess && cValues > 0) {
			dtUTCUntil = lpOccrInfo[cValues-1].tBaseDate;
			lpRec->setEndDate(UTCToLocal(dtUTCUntil, sTimeZone));
		}

		MAPIFreeBuffer(lpOccrInfo);
	}

	// set named prop 0x8510 to 353, needed for Outlook to ask for single or total recurrence when deleting
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_SIDEEFFECT], PT_LONG);
	sPropVal.Value.ul = 353;
	lpIcalItem->lstMsgProps.push_back(sPropVal);

	// Set 0x8235, also known as ClipStart in OutlookSpy
	UnixTimeToFileTime(LocalToUTC(recurrence::StartOfDay(lpRec->getStartDateTime()), sTimeZone), &sPropVal.Value.ft);
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCE_START], PT_SYSTIME);
	lpIcalItem->lstMsgProps.push_back(sPropVal);

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_RECURRENCEID_PROPERTY);
	if(!lpicProp) {
		UnixTimeToFileTime(LocalToUTC(lpRec->getStartDateTime(), sTimeZone), &sPropVal.Value.ft);

		// Set 0x820D / ApptStartWhole
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTSTARTWHOLE], PT_SYSTIME);
		lpIcalItem->lstMsgProps.push_back(sPropVal);

		// Set 0x8516 / CommonStart
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_COMMONSTART], PT_SYSTIME);
		lpIcalItem->lstMsgProps.push_back(sPropVal);

		UnixTimeToFileTime(LocalToUTC(lpRec->getStartDateTime() + (dtUTCEnd - dtUTCStart), sTimeZone), &sPropVal.Value.ft);

		// Set 0x820E / ApptEndWhole
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTENDWHOLE], PT_SYSTIME);
		lpIcalItem->lstMsgProps.push_back(sPropVal);

		// Set CommonEnd		
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_COMMONEND], PT_SYSTIME);
		lpIcalItem->lstMsgProps.push_back(sPropVal);
	}

	lpIcalItem->lpRecurrence = lpRec;
	lpRec = NULL;

exit:
	delete lpRec;
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
HRESULT ICalRecurrence::HrMakeMAPIException(icalcomponent *lpEventRoot, icalcomponent *lpicEvent, icalitem *lpIcalItem, bool bIsAllDay, LPSPropTagArray lpNamedProps, std::string& strCharset, icalitem::exception *lpEx)
{
	HRESULT hr = hrSuccess;
	icalproperty *lpicProp = NULL;
	time_t ttStartLocalTime = 0;
	time_t ttEndLocalTime = 0;
	time_t ttStartUtcTime = 0;
	time_t ttEndUtcTime = 0;
	time_t ttOriginalUtcTime = 0;
	time_t ttOriginalLocalTime = 0;
	ULONG ulId = 0;
	ULONG i = 0;
	SPropValue sPropVal;
	std::wstring strIcalProp;
	bool bXMS = false;
	icalcomponent *lpicAlarm = NULL;
	LONG ulRemindBefore = 0;
	time_t ttReminderTime = 0;
	bool bReminderSet = false;
	std::list<SPropValue>::iterator iProp;
	convert_context converter;
	const char *lpszProp;

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

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_RECURRENCEID_PROPERTY);
	if (!lpicProp) {
		// you tricked me! it's not an exception at all!
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	ttOriginalUtcTime = ICalTimeTypeToUTC(lpEventRoot, lpicProp);
	ttOriginalLocalTime = icaltime_as_timet(icalvalue_get_datetime(icalproperty_get_value(lpicProp)));
	
	lpEx->tBaseDate = ttOriginalUtcTime;

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTSTART_PROPERTY);
	if (lpicProp == NULL) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	ttStartUtcTime = ICalTimeTypeToUTC(lpEventRoot, lpicProp);
	ttStartLocalTime = icaltime_as_timet(icalvalue_get_datetime(icalproperty_get_value(lpicProp)));

	lpEx->tStartDate = ttStartUtcTime;

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTEND_PROPERTY);
	if (lpicProp == NULL) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	ttEndUtcTime = ICalTimeTypeToUTC(lpEventRoot, lpicProp);
	ttEndLocalTime = icaltime_as_timet(icalvalue_get_datetime(icalproperty_get_value(lpicProp)));

	hr = lpIcalItem->lpRecurrence->addModifiedException(ttStartLocalTime, ttEndLocalTime, ttOriginalLocalTime, &ulId);
	if (hr != hrSuccess)
		goto exit;

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRINGBASE], PT_SYSTIME);
	UnixTimeToFileTime(ttOriginalUtcTime, &sPropVal.Value.ft);
	lpEx->lstMsgProps.push_back(sPropVal);

	sPropVal.ulPropTag = PR_EXCEPTION_STARTTIME;
	UnixTimeToFileTime(ttStartLocalTime, &sPropVal.Value.ft);
	lpEx->lstAttachProps.push_back(sPropVal);

	sPropVal.ulPropTag = PR_EXCEPTION_ENDTIME;
	UnixTimeToFileTime(ttEndLocalTime, &sPropVal.Value.ft);
	lpEx->lstAttachProps.push_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTSTARTWHOLE], PT_SYSTIME);
	UnixTimeToFileTime(ttStartUtcTime, &sPropVal.Value.ft);	
	lpEx->lstMsgProps.push_back(sPropVal);
	
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_COMMONSTART], PT_SYSTIME);
	lpEx->lstMsgProps.push_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCE_START], PT_SYSTIME);
	lpEx->lstMsgProps.push_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_APPTENDWHOLE], PT_SYSTIME);
	UnixTimeToFileTime(ttEndUtcTime, &sPropVal.Value.ft);	
	lpEx->lstMsgProps.push_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_COMMONEND], PT_SYSTIME);
	lpEx->lstMsgProps.push_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCE_END], PT_SYSTIME);
	lpEx->lstMsgProps.push_back(sPropVal);

	sPropVal.ulPropTag = PR_EXCEPTION_ENDTIME;
	UnixTimeToFileTime(ttEndLocalTime, &sPropVal.Value.ft);
	lpEx->lstAttachProps.push_back(sPropVal);

	sPropVal.ulPropTag = PR_DISPLAY_NAME_W;
	HrCopyString(lpIcalItem->base, L"Untitled", &sPropVal.Value.lpszW);
	lpEx->lstAttachProps.push_back(sPropVal);

	sPropVal.ulPropTag = PR_ATTACH_METHOD;
	sPropVal.Value.ul = ATTACH_EMBEDDED_MSG;
	lpEx->lstAttachProps.push_back(sPropVal);

	sPropVal.ulPropTag = PR_ATTACH_FLAGS;
	sPropVal.Value.ul = 0;
	lpEx->lstAttachProps.push_back(sPropVal);

	sPropVal.ulPropTag = PR_ATTACHMENT_LINKID;
	sPropVal.Value.ul = 0;
	lpEx->lstAttachProps.push_back(sPropVal);

	sPropVal.ulPropTag = PR_ATTACHMENT_HIDDEN;
	sPropVal.Value.b = TRUE;
	lpEx->lstAttachProps.push_back(sPropVal);
	
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_HIDE_ATTACH], PT_BOOLEAN);;
	sPropVal.Value.b = TRUE;
	lpIcalItem->lstMsgProps.push_back(sPropVal);
	
	sPropVal.ulPropTag = PR_ATTACHMENT_FLAGS;
	sPropVal.Value.ul = 2;
	lpEx->lstAttachProps.push_back(sPropVal);

	sPropVal.ulPropTag = PR_MESSAGE_CLASS_W;
	HrCopyString(lpIcalItem->base, L"IPM.OLE.CLASS.{00061055-0000-0000-C000-000000000046}", &sPropVal.Value.lpszW);
	lpEx->lstMsgProps.push_back(sPropVal);

	// copy properties to exception and test if changed
	for (iProp = lpIcalItem->lstMsgProps.begin();
	     iProp != lpIcalItem->lstMsgProps.end(); ++iProp) {
		for (i = 0; i < sptaCopy.cValues; ++i) {
			if (sptaCopy.aulPropTag[i] == (*iProp).ulPropTag) {
				abOldPresent[i] = true;
				if (sptaCopy.aulPropTag[i] != PR_BODY) // no need to copy body
					lpEx->lstMsgProps.push_back(*iProp);
				if (sptaCopy.aulPropTag[i] == CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN))
					bOldIsAllDay = iProp->Value.b; // remember allday event status
				break;
			}
		}
	}

	// find exceptional properties
	// TODO: should actually look at original message, and check for differences
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_ANY_PROPERTY);
	while (lpicProp) {

		switch (icalproperty_isa(lpicProp)) {
		case ICAL_SUMMARY_PROPERTY:
			lpszProp = icalproperty_get_summary(lpicProp);
			strIcalProp = converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), strCharset.c_str());
			hr = lpIcalItem->lpRecurrence->setModifiedSubject(ulId, strIcalProp.c_str());
			if (hr != hrSuccess)
				goto exit;
			sPropVal.ulPropTag = PR_SUBJECT_W;
			HrCopyString(lpIcalItem->base, strIcalProp.c_str(), &sPropVal.Value.lpszW);
			lpEx->lstMsgProps.push_back(sPropVal);
			abNewPresent[0] = true;
			break;
		case ICAL_LOCATION_PROPERTY:
			lpszProp = icalproperty_get_location(lpicProp);
			strIcalProp = converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), strCharset.c_str());
			hr = lpIcalItem->lpRecurrence->setModifiedLocation(ulId, strIcalProp.c_str());
			if (hr != hrSuccess)
				goto exit;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_LOCATION], PT_UNICODE);
			HrCopyString(lpIcalItem->base, strIcalProp.c_str(), &sPropVal.Value.lpszW);
			lpEx->lstMsgProps.push_back(sPropVal);
			abNewPresent[1] = true;
			break;
		case ICAL_TRANSP_PROPERTY:
			if (!bXMS) {
				ULONG ulBusyStatus = 2;	// default busy
				switch(icalproperty_get_transp(lpicProp)){
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
					goto exit;

				sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_BUSYSTATUS], PT_LONG);
				sPropVal.Value.ul = ulBusyStatus;
				lpEx->lstMsgProps.push_back(sPropVal);
				abNewPresent[7] = true;
			}
			break;
		case ICAL_X_PROPERTY:
			if (strcmp(icalproperty_get_x_name(lpicProp), "X-MICROSOFT-CDO-BUSYSTATUS") == 0) {
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
					goto exit;

				sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_BUSYSTATUS], PT_LONG);
				sPropVal.Value.ul = ulBusyStatus;
				lpEx->lstMsgProps.push_back(sPropVal);

				bXMS = true;
				abNewPresent[7] = true;
			}
			break;
		case ICAL_DESCRIPTION_PROPERTY:
			lpszProp = icalproperty_get_description(lpicProp);
			strIcalProp = converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), strCharset.c_str());
			hr = lpIcalItem->lpRecurrence->setModifiedBody(ulId);
			if (hr != hrSuccess)
				goto exit;
			sPropVal.ulPropTag = PR_BODY_W;
			HrCopyString(lpIcalItem->base, strIcalProp.c_str(), &sPropVal.Value.lpszW);
			lpEx->lstMsgProps.push_back(sPropVal);
			abNewPresent[2] = true;
			break;
		default:
			// ignore property
			break;
		};

		lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_ANY_PROPERTY);
	}

	// make sure these are not removed :| (body, label, reminderset, reminder minutes)
	abNewPresent[2] = abNewPresent[3] = abNewPresent[4] = abNewPresent[5] = true;
	// test if properties were just removed
	for (i = 0; i < sptaCopy.cValues; ++i) {
		if (abOldPresent[i] == true && abNewPresent[i] == false) {
			iProp = find(lpEx->lstMsgProps.begin(), lpEx->lstMsgProps.end(), sptaCopy.aulPropTag[i]);
			if (iProp != lpEx->lstMsgProps.end()) {
				lpEx->lstMsgProps.erase(iProp);
				switch (i) {
				case 0:
					// subject
					hr = lpIcalItem->lpRecurrence->setModifiedSubject(ulId, std::wstring());
					if (hr != hrSuccess)
						goto exit;
					sPropVal.ulPropTag = PR_SUBJECT_W;
					sPropVal.Value.lpszW = const_cast<wchar_t *>(L"");
					lpEx->lstMsgProps.push_back(sPropVal);
					break;
				case 1:
					// location
					hr = lpIcalItem->lpRecurrence->setModifiedLocation(ulId, std::wstring());
					if (hr != hrSuccess)
						goto exit;
					sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_LOCATION], PT_UNICODE);
					sPropVal.Value.lpszW = const_cast<wchar_t *>(L"");
					lpEx->lstMsgProps.push_back(sPropVal);
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
					if (bIsAllDay != bOldIsAllDay) {
						// flip all day status
						hr = lpIcalItem->lpRecurrence->setModifiedSubType(ulId, 1);
						if (hr != hrSuccess)
							goto exit;

						sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN);
						sPropVal.Value.ul = !bOldIsAllDay;
						lpEx->lstMsgProps.push_back(sPropVal);
					}
					break;
				case 7:
					// busy status, default: busy
					hr = lpIcalItem->lpRecurrence->setModifiedBusyStatus(ulId, 1);
					if (hr != hrSuccess)
						goto exit;

					sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_BUSYSTATUS], PT_LONG);
					sPropVal.Value.ul = 1;
					lpEx->lstMsgProps.push_back(sPropVal);
					break;
				};
			}
		}
	}
	

	// reminderset (flip reminder on/off) and reminderdelta (offset time change) in vevent component of this item
	lpicAlarm = icalcomponent_get_first_component(lpicEvent, ICAL_VALARM_COMPONENT);
	if (lpicAlarm) {
		hr = HrParseVAlarm(lpicAlarm, &ulRemindBefore, &ttReminderTime, &bReminderSet);
		if (hr == hrSuccess) {
			// abOldPresent[4] == reminderset marker in original message
			if (bReminderSet != abOldPresent[4]) {
				hr = lpIcalItem->lpRecurrence->setModifiedReminder(ulId, bReminderSet);
				if (hr != hrSuccess)
					goto exit;

				sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERSET], PT_BOOLEAN);
				sPropVal.Value.b = bReminderSet;
				lpEx->lstMsgProps.push_back(sPropVal);

				if (ttReminderTime == 0)
					ttReminderTime = ttStartLocalTime;

				sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERTIME], PT_SYSTIME);
				UnixTimeToFileTime(ttReminderTime, &sPropVal.Value.ft);
				lpEx->lstMsgProps.push_back(sPropVal);
			}

			hr = lpIcalItem->lpRecurrence->setModifiedReminderDelta(ulId, ulRemindBefore);
			if (hr != hrSuccess)
				goto exit;

			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERMINUTESBEFORESTART], PT_LONG);
			sPropVal.Value.ul = ulRemindBefore;
			lpEx->lstMsgProps.push_back(sPropVal);
		}
	} else {
		if (abOldPresent[4]) {
			// disable reminder in attachment
			hr = lpIcalItem->lpRecurrence->setModifiedReminder(ulId, 0);
			if (hr != hrSuccess)
				goto exit;

			sPropVal.ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_REMINDERSET], PT_BOOLEAN);
			sPropVal.Value.b = FALSE;
			lpEx->lstMsgProps.push_back(sPropVal);
		}
	}

exit:
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
	HRESULT hr = hrSuccess;
	char *lpRecBlob = NULL;
	unsigned int ulRecBlob = 0;
	LPSPropValue lpPropVal = NULL;
	LPSPropValue lpsPropRecPattern = NULL;
	std::string strHRS;
	ULONG i = 0;

	hr = lpRecurrence->HrGetRecurrenceState(&lpRecBlob, &ulRecBlob);
	if (hr != hrSuccess)
		goto exit;

	hr = lpRecurrence->HrGetHumanReadableString(&strHRS);
	if (hr != hrSuccess)
		goto exit;

	// adjust number of props
	if ((hr = MAPIAllocateBuffer(sizeof(SPropValue)*4, (void**)&lpPropVal)) != hrSuccess)
		goto exit;

	lpPropVal[i].ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRING], PT_BOOLEAN);
	lpPropVal[i].Value.b = TRUE;
	++i;

	// TODO: combine with icon index in vevent .. the item may be a meeting request (meeting+recurring==1027)
	lpPropVal[i].ulPropTag = PR_ICON_INDEX;
	lpPropVal[i].Value.ul = ICON_APPT_RECURRING;
	++i;

	lpPropVal[i].ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCESTATE], PT_BINARY);
	lpPropVal[i].Value.bin.lpb = (BYTE*)lpRecBlob;
	lpPropVal[i].Value.bin.cb = ulRecBlob;
	++i;

	hr = HrGetOneProp(lpMessage, CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCEPATTERN], PT_STRING8), &lpsPropRecPattern);
	if(hr != hrSuccess)
	{
		lpPropVal[i].ulPropTag = CHANGE_PROP_TYPE(lpNamedProps->aulPropTag[PROP_RECURRENCEPATTERN], PT_STRING8);
		lpPropVal[i].Value.lpszA = (char*)strHRS.c_str();
		++i;
	}

	hr = lpMessage->SetProps(i, lpPropVal, NULL);
	if (FAILED(hr))
		goto exit;

exit:
	MAPIFreeBuffer(lpPropVal);
	MAPIFreeBuffer(lpRecBlob);
	MAPIFreeBuffer(lpsPropRecPattern);
	return hr;
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
	OccrInfo *lpFBBlocksAll = NULL;
	ULONG cValues = 0;
	bool bIsValid = false;
	time_t tBaseDateStart = LocalToUTC(lpItem->lpRecurrence->StartOfDay(UTCToLocal(lpEx.tBaseDate, lpItem->tTZinfo)), lpItem->tTZinfo);
	time_t tStartDateStart = LocalToUTC(lpItem->lpRecurrence->StartOfDay(UTCToLocal(lpEx.tStartDate, lpItem->tTZinfo)), lpItem->tTZinfo);

	if (tBaseDateStart < tStartDateStart) {

		hr = lpItem->lpRecurrence->HrGetItems(tBaseDateStart, tStartDateStart + 1439 * 60 , NULL, lpItem->tTZinfo, lpItem->ulFbStatus, &lpFBBlocksAll, &cValues);
	} else {
		hr = lpItem->lpRecurrence->HrGetItems(tStartDateStart, tBaseDateStart + 1439 * 60 , NULL, lpItem->tTZinfo, lpItem->ulFbStatus, &lpFBBlocksAll, &cValues);
	}

	if (hr != hrSuccess)
		goto exit;

	if(cValues == 1)
		bIsValid = true;

exit:	
	MAPIFreeBuffer(lpFBBlocksAll);
	return bIsValid;
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
HRESULT ICalRecurrence::HrCreateICalRecurrence(TIMEZONE_STRUCT sTimeZone, bool bIsAllDay, recurrence *lpRecurrence, icalcomponent *lpicEvent)
{
	HRESULT hr = hrSuccess;
	icalrecurrencetype icRRule;
	std::list<time_t> lstExceptions;
	std::list<time_t>::const_iterator iException;
	icaltimetype ittExDate;
	TIMEZONE_STRUCT sTZgmt = {0};

	hr = HrCreateICalRecurrenceType(sTimeZone, bIsAllDay, lpRecurrence, &icRRule);
	if (hr != hrSuccess)
		goto exit;

	icalcomponent_add_property(lpicEvent, icalproperty_new_rrule(icRRule));

	// all delete exceptions are in the delete list,
	lstExceptions = lpRecurrence->getDeletedExceptions();
	if (!lstExceptions.empty()) {
		// add EXDATE props
		for (iException = lstExceptions.begin();
		     iException != lstExceptions.end(); ++iException) {
			if(bIsAllDay)
			{
				ittExDate = icaltime_from_timet_with_zone(LocalToUTC(*iException, sTZgmt), bIsAllDay, nullptr);
			}
			else
				ittExDate = icaltime_from_timet_with_zone(LocalToUTC(*iException, sTimeZone), 0, nullptr);

			kc_ical_utc(ittExDate, true);
			icalcomponent_add_property(lpicEvent, icalproperty_new_exdate(ittExDate));
		}
	}

	// modified exceptions are done by the caller because of the attachments with info

exit:
	return hr;
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
HRESULT ICalRecurrence::HrCreateICalRecurrenceType(TIMEZONE_STRUCT sTimeZone, bool bIsAllday, recurrence *lpRecurrence, icalrecurrencetype *lpicRRule)
{
	HRESULT hr = hrSuccess;
	struct icalrecurrencetype icRec;

	icalrecurrencetype_clear(&icRec);

	switch (lpRecurrence->getFrequency()) {
	case recurrence::DAILY:
		icRec.freq = ICAL_DAILY_RECURRENCE;
		// only weekdays selected in outlook
		if (lpRecurrence->getWeekDays()) {
			// iCal.app does not have daily-weekday type of recurrence
			// so Daily-weekdays is converted to weekly recurrence
			icRec.freq = ICAL_WEEKLY_RECURRENCE;
			WeekDaysToICalArray(lpRecurrence->getWeekDays(), &icRec);
		}
		break;
	case recurrence::WEEKLY:
		icRec.freq = ICAL_WEEKLY_RECURRENCE;
		WeekDaysToICalArray(lpRecurrence->getWeekDays(), &icRec);
		break;
	case recurrence::MONTHLY:
		icRec.freq = ICAL_MONTHLY_RECURRENCE;
		if (lpRecurrence->getWeekNumber() != 0) {
			// mapi patterntype == 3
			// only 1 day should be set!
			if(lpRecurrence->getWeekDays() == 127) {
				// All Weekdays are set for recurrence type "second" "day" of month.
				// SU,MO,TU,WE,TH,FR,SA -> weekdays = 127.
				icRec.by_month_day[0] = lpRecurrence->getWeekNumber() == 5 ? -1: lpRecurrence->getWeekNumber(); // hack to handle nth day month type of rec.
				icRec.by_month_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
				break;
			} else if (lpRecurrence->getWeekDays() == 62 || lpRecurrence->getWeekDays() == 65){
				// Recurrence of type '3rd weekday'/'last weekend'
				// MO,TU,WE,TH,FR -> 62 and SU,SA -> 65 
				icRec.by_set_pos[0] = lpRecurrence->getWeekNumber();
				icRec.by_set_pos[1] = ICAL_RECURRENCE_ARRAY_MAX;
				WeekDaysToICalArray(lpRecurrence->getWeekDays(), &icRec);
			} else if (lpRecurrence->getWeekNumber() == 5) {
				icRec.by_day[0] = (round(log((double)lpRecurrence->getWeekDays())/log(2.0)) + 8 + 1 ) * -1;  // corrected last weekday
				icRec.by_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
			} else {
				icRec.by_day[0] =  round(log((double)lpRecurrence->getWeekDays())/log(2.0)) + (8 * lpRecurrence->getWeekNumber() ) +1; // +1 because outlook starts on sunday
				icRec.by_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
			}

		} else {
			// mapi patterntype == 2
			icRec.by_month_day[0] = lpRecurrence->getDayOfMonth();
			icRec.by_month_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
		}
		break;
	case recurrence::YEARLY:
		icRec.freq = ICAL_YEARLY_RECURRENCE;
		if (lpRecurrence->getWeekNumber() != 0) {
			// mapi patterntype == 3
			// only 1 day should be set!
			if (lpRecurrence->getWeekNumber() == 5)
				icRec.by_day[0] = ((log((double)lpRecurrence->getWeekDays())/log(2.0)) + 8 + 1 ) * -1; 
			else
				icRec.by_day[0] = (int)(log((double)lpRecurrence->getWeekDays())/log(2.0)) + (8 * lpRecurrence->getWeekNumber() ) +1; // +1 because outlook starts on sunday

			icRec.by_day[1] = ICAL_RECURRENCE_ARRAY_MAX;

			icRec.by_month[0] = lpRecurrence->getMonth();
			icRec.by_month[1] = ICAL_RECURRENCE_ARRAY_MAX;
		} else {
			// mapi patterntype == 2
			icRec.by_month_day[0] = lpRecurrence->getDayOfMonth();
			icRec.by_month_day[1] = ICAL_RECURRENCE_ARRAY_MAX;

			icRec.by_month[0] = lpRecurrence->getMonth();
			icRec.by_month[1] = ICAL_RECURRENCE_ARRAY_MAX;
		}

		break;
	default:
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
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

exit:
	return hr;
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
	int i = 0, j = 0;

	for (i = 0; i < 7; ++i)
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
	HRESULT hr = hrSuccess;
	icalcomponent *lpicException = NULL;
	icalproperty *lpicProp = NULL;
	
	lpicException = icalcomponent_new_clone(lpicEvent);

	// these are always different in an exception
	lpicProp = icalcomponent_get_first_property(lpicException, ICAL_DTSTART_PROPERTY);
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

	return hr;
}
