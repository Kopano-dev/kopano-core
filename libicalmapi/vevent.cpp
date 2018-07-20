/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include "vevent.h"
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <kopano/CommonUtil.h>
#include "nameids.h"
#include "icaluid.h"
#include <kopano/stringutil.h>
#include "icalmem.hpp"

namespace KC {

/** 
 * VEvent constructor, implements VConverter
 */
VEventConverter::VEventConverter(LPADRBOOK lpAdrBook, timezone_map *mapTimeZones, LPSPropTagArray lpNamedProps, const std::string& strCharset, bool blCensor, bool bNoRecipients, IMailUser *lpMailUser)
	: VConverter(lpAdrBook, mapTimeZones, lpNamedProps, strCharset, blCensor, bNoRecipients, lpMailUser)
{
}

/** 
 * Entrypoint to convert an ical object to MAPI object.
 * 
 * @param[in]  lpEventRoot The root component (VCALENDAR top object)
 * @param[in]  lpEvent The VEVENT object to convert
 * @param[in]  lpPrevItem Optional previous (top) item to use when lpEvent describes an exception
 * @param[out] lppRet The icalitem struct to finalize into a MAPI object
 * 
 * @return MAPI error code
 */
HRESULT VEventConverter::HrICal2MAPI(icalcomponent *lpEventRoot, icalcomponent *lpEvent, icalitem *lpPrevItem, icalitem **lppRet)
{
	HRESULT hr = VConverter::HrICal2MAPI(lpEventRoot, lpEvent,
	             lpPrevItem, lppRet);
	if (hr != hrSuccess)
		return hr;
	(*lppRet)->eType = VEVENT;
	return hrSuccess;
}

/** 
 * The properties set here are all required base properties for
 * different calendar items and meeting requests.
 *
 * Finds the difference if we're handling this message as the
 * organiser or as an attendee. Uses that and the method to set the
 * correct response and meeting status. PR_MESSAGE_CLASS is only set
 * on the main message, not on exceptions.  For PUBLISH methods, it
 * will also set the appointment reply time property. Lastly, the icon
 * index (outlook icon displayed in list view) is set.
 *
 * @note We only handle the methods REQUEST, REPLY and CANCEL
 * according to dagent/spooler/gateway fashion (that is, meeting
 * requests in emails) and PUBLISH for iCal/CalDAV (that is, pure
 * calendar items). Meeting requests through the PUBLISH method (as
 * also described in the Microsoft documentations) is not supported.
 * 
 * @param[in]  icMethod Method of the ical event
 * @param[in]  lpicEvent The ical VEVENT to convert
 * @param[in]  base Used for the 'base' pointer for memory allocations
 * @param[in]  bisException Weather we're handling an exception or not
 * @param[in,out] lstMsgProps 
 * 
 * @return MAPI error code
 */
HRESULT VEventConverter::HrAddBaseProperties(icalproperty_method icMethod, icalcomponent *lpicEvent, void *base, bool bisException, std::list<SPropValue> *lstMsgProps)
{
	SPropValue sPropVal;
	bool bMeeting = false, bMeetingOrganised = false;
	std::wstring strEmail;

	auto icProp = icalcomponent_get_first_property(lpicEvent, ICAL_ORGANIZER_PROPERTY);
	if (icProp) 
	{
		const char *lpszProp = icalproperty_get_organizer(icProp);
		strEmail = m_converter.convert_to<std::wstring>(lpszProp, rawsize(lpszProp), m_strCharset.c_str());
		if (wcsncasecmp(strEmail.c_str(), L"mailto:", 7) == 0)
			strEmail.erase(0, 7);
		
		if (bIsUserLoggedIn(strEmail))
			bMeetingOrganised = true;
	}

	// @note setting PR_MESSAGE_CLASS must be the last entry in the case.
	switch (icMethod) {
	case ICAL_METHOD_REQUEST:
		bMeeting = true;

		sPropVal.ulPropTag = PR_RESPONSE_REQUESTED;
		sPropVal.Value.b = true;
		lstMsgProps->emplace_back(sPropVal);

		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RESPONSESTATUS], PT_LONG);
		sPropVal.Value.ul = respNotResponded;
		lstMsgProps->emplace_back(sPropVal);

		HrCopyString(base, L"IPM.Schedule.Meeting.Request", &sPropVal.Value.lpszW);
		break;

	case ICAL_METHOD_COUNTER:
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COUNTERPROPOSAL], PT_BOOLEAN);
		sPropVal.Value.b = true;
		lstMsgProps->emplace_back(sPropVal);

		// Fall through to REPLY
	case ICAL_METHOD_REPLY: {
		// This value with respAccepted/respDeclined/respTentative is only for imported items through the PUBLISH method, which we do not support.
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RESPONSESTATUS], PT_LONG);
		sPropVal.Value.ul = respNone;
		lstMsgProps->emplace_back(sPropVal);
		
		// skip the meetingstatus property
		bMeeting = false;

		// A reply message must have only one attendee, the attendee replying
		if (icalcomponent_count_properties(lpicEvent, ICAL_ATTENDEE_PROPERTY) != 1)
			return MAPI_E_CALL_FAILED;
		icProp = icalcomponent_get_first_property(lpicEvent, ICAL_ATTENDEE_PROPERTY);
		if (icProp == NULL)
			return MAPI_E_CALL_FAILED;
		auto icParam = icalproperty_get_first_parameter(icProp, ICAL_PARTSTAT_PARAMETER);
		if (icParam == NULL)
			return MAPI_E_CALL_FAILED;

		switch (icalparameter_get_partstat(icParam)) {
		case ICAL_PARTSTAT_ACCEPTED:
			HrCopyString(base, L"IPM.Schedule.Meeting.Resp.Pos", &sPropVal.Value.lpszW);
			break;

		case ICAL_PARTSTAT_DECLINED:
			HrCopyString(base, L"IPM.Schedule.Meeting.Resp.Neg", &sPropVal.Value.lpszW);
			break;

		case ICAL_PARTSTAT_TENTATIVE:
			HrCopyString(base, L"IPM.Schedule.Meeting.Resp.Tent", &sPropVal.Value.lpszW);
			break;

		default:
			return MAPI_E_TYPE_NO_SUPPORT;
		}
		break;
	}
	case ICAL_METHOD_CANCEL:
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RESPONSESTATUS], PT_LONG);
		sPropVal.Value.ul = respNotResponded;
		lstMsgProps->emplace_back(sPropVal);

		// make sure the cancel flag gets set
		bMeeting = true;

		HrCopyString(base, L"IPM.Schedule.Meeting.Canceled", &sPropVal.Value.lpszW);
		break;

	case ICAL_METHOD_PUBLISH:
	default: {
		// set ResponseStatus to 0 fix for BlackBerry.
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_RESPONSESTATUS], PT_LONG);
		if (m_ulUserStatus != 0)
			sPropVal.Value.ul = m_ulUserStatus;
		else if (bMeetingOrganised)
			sPropVal.Value.ul = 1;
		else
			sPropVal.Value.ul = 0;
		lstMsgProps->emplace_back(sPropVal);
		
		// time(NULL) returns UTC time as libical sets application to UTC time.
		auto tNow = time(nullptr);
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTREPLYTIME], PT_SYSTIME);
		sPropVal.Value.ft  = UnixTimeToFileTime(tNow);
		lstMsgProps->emplace_back(sPropVal);

		// Publish is used when mixed events are in the vcalendar
		// we should determine on other properties if this is a meeting request related item
		HrCopyString(base, L"IPM.Appointment", &sPropVal.Value.lpszW);

		// if we don't have attendees, skip the meeting request props
		// if Attendee is present, then set the PROP_MEETINGSTATUS property
		bMeeting = icalcomponent_get_first_property(lpicEvent, ICAL_ATTENDEE_PROPERTY) != nullptr;
		break;
	}
	}

	if (!bisException) {
		sPropVal.ulPropTag = PR_MESSAGE_CLASS_W;
		lstMsgProps->emplace_back(sPropVal);
	}

	if (m_lpMailUser != nullptr) {
		memory_ptr<SPropValue> tmp;
		auto hr = HrGetOneProp(m_lpMailUser, PR_DISPLAY_NAME_W, &~tmp);
		if (hr == hrSuccess) {
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_8230], PT_UNICODE);
			HrCopyString(base, tmp->Value.lpszW, &sPropVal.Value.lpszW);
			lstMsgProps->emplace_back(sPropVal);
		}
	}

	if (icMethod == ICAL_METHOD_CANCEL || icMethod == ICAL_METHOD_REQUEST)
	{
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REQUESTSENT], PT_BOOLEAN); 
		sPropVal.Value.b = true;
		lstMsgProps->emplace_back(sPropVal);
	} else if (icMethod == ICAL_METHOD_REPLY || icMethod == ICAL_METHOD_COUNTER) {
		// This is only because outlook and the examples say so in [MS-OXCICAL].pdf
		// Otherwise, it's completely contradictionary to what the documentation describes.
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REQUESTSENT], PT_BOOLEAN); 
		sPropVal.Value.b = false;
		lstMsgProps->emplace_back(sPropVal);
	} else {
		// PUBLISH method, depends on if we're the owner of the object
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_REQUESTSENT], PT_BOOLEAN); 
		sPropVal.Value.b = bMeetingOrganised;
		lstMsgProps->emplace_back(sPropVal);
	}

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_MEETINGSTATUS], PT_LONG);
	if (bMeeting) {
		// Set meeting status flags
		sPropVal.Value.ul = 1 | 2; // this-is-a-meeting-object flag + received flag
		if (icMethod == ICAL_METHOD_CANCEL)
			sPropVal.Value.ul |= 4; // cancelled flag
		else if(bMeetingOrganised)
			sPropVal.Value.ul = 1;
	} else {
		sPropVal.Value.ul = 0; // this-is-a-meeting-object flag off
	}
	lstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_ICON_INDEX;
	if (!bMeeting)
		sPropVal.Value.ul = ICON_APPT_APPOINTMENT;
	else if (icMethod == ICAL_METHOD_CANCEL)
		sPropVal.Value.ul = ICON_APPT_MEETING_CANCEL;
	else
		sPropVal.Value.ul = ICON_APPT_MEETING_SINGLE;

	// 1024: normal calendar item
	// 1025: recurring item
	// 1026: meeting request
	// 1027: recurring meeting request
	lstMsgProps->emplace_back(sPropVal);
	return hrSuccess;
}

/**
 * Set time properties in icalitem from the ical data
 *
 * @param[in]	lpicEventRoot	ical VCALENDAR component to set the timezone
 * @param[in]	lpicEvent		ical VEVENT component
 * @param[in]	bIsAllday		set times for normal or allday event
 * @param[out]	lpIcalItem		icalitem structure in which mapi properties are set
 * @return		MAPI error code
 * @retval		MAPI_E_INVALID_PARAMETER	start time or timezone not present in ical data
 */
HRESULT VEventConverter::HrAddTimes(icalproperty_method icMethod, icalcomponent *lpicEventRoot, icalcomponent *lpicEvent, bool bIsAllday, icalitem *lpIcalItem)
{
	SPropValue sPropVal;
	icalproperty *lpicOrigDTStartProp = nullptr, *lpicOrigDTEndProp = nullptr;
	std::unique_ptr<icalproperty, icalmapi_delete> lpicFreeDTStartProp, lpicFreeDTEndProp;
	time_t timeDTStartUTC = 0, timeDTEndUTC = 0;
	time_t timeDTStartLocal = 0, timeDTEndLocal = 0;
	time_t timeEndOffset = 0, timeStartOffset = 0;
	icalproperty* lpicDurationProp = NULL;

	auto lpicDTStartProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTSTART_PROPERTY);
	auto lpicDTEndProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTEND_PROPERTY);
	// DTSTART must be available
	if (lpicDTStartProp == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = HrAddTimeZone(lpicDTStartProp, lpIcalItem);
	if (hr != hrSuccess)
		return hr;

	if (icMethod == ICAL_METHOD_COUNTER) {
		// dtstart contains proposal, X-MS-OLK-ORIGINALSTART optionally contains previous DTSTART
		for (auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_X_PROPERTY);
		     lpicProp != nullptr;
		     lpicProp = icalcomponent_get_next_property(lpicEvent, ICAL_X_PROPERTY))
			if (strcmp(icalproperty_get_x_name(lpicProp), "X-MS-OLK-ORIGINALSTART") == 0)
				lpicOrigDTStartProp = lpicProp;
			else if (strcmp(icalproperty_get_x_name(lpicProp), "X-MS-OLK-ORIGINALEND") == 0)
				lpicOrigDTEndProp = lpicProp;

		if (lpicOrigDTStartProp && lpicOrigDTEndProp) {
			// No support for DTSTART +DURATION and X-MS-OLK properties. Exchange will not send that either.
			if (lpicDTEndProp == nullptr)
				return MAPI_E_INVALID_PARAMETER;

			// set new proposal start
			timeDTStartUTC = ICalTimeTypeToUTC(lpicEventRoot, lpicDTStartProp);
			sPropVal.Value.ft  = UnixTimeToFileTime(timeDTStartUTC);
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PROPOSEDSTART], PT_SYSTIME);
			lpIcalItem->lstMsgProps.emplace_back(sPropVal);

			// set new proposal end
			timeDTEndUTC = ICalTimeTypeToUTC(lpicEventRoot, lpicDTEndProp);
			sPropVal.Value.ft  = UnixTimeToFileTime(timeDTEndUTC);
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PROPOSEDEND], PT_SYSTIME);
			lpIcalItem->lstMsgProps.emplace_back(sPropVal);

			// rebuild properties, so libical has the right value type in the property.
			std::string strTmp;
			strTmp = icalproperty_as_ical_string_r(lpicOrigDTStartProp);
			strTmp.erase(0,strlen("X-MS-OLK-ORIGINAL"));
			strTmp.insert(0,"DT");
			lpicFreeDTStartProp.reset(icalproperty_new_from_string(strTmp.c_str()));
			lpicDTStartProp = lpicFreeDTStartProp.get();

			strTmp = icalproperty_as_ical_string_r(lpicOrigDTEndProp);
			strTmp.erase(0,strlen("X-MS-OLK-ORIGINAL"));
			strTmp.insert(0,"DT");
			lpicFreeDTEndProp.reset(icalproperty_new_from_string(strTmp.c_str()));
			lpicDTEndProp = lpicFreeDTEndProp.get();
		}
	}

	// get timezone of DTSTART
	timeDTStartUTC = ICalTimeTypeToUTC(lpicEventRoot, lpicDTStartProp);
	timeDTStartLocal = ICalTimeTypeToLocal(lpicDTStartProp);
	timeStartOffset = timeDTStartUTC - timeDTStartLocal;

	if (bIsAllday)
		sPropVal.Value.ft = UnixTimeToFileTime(timeDTStartLocal);
	else
		sPropVal.Value.ft = UnixTimeToFileTime(timeDTStartUTC);

	// Set 0x820D / ApptStartWhole
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTSTARTWHOLE], PT_SYSTIME);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// Set 0x8516 / CommonStart
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COMMONSTART], PT_SYSTIME);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// Set PR_START_DATE
	sPropVal.ulPropTag = PR_START_DATE;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// Set 0x8215 / AllDayEvent
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN);
	sPropVal.Value.b = bIsAllday;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// Set endtime / DTEND
	if (lpicDTEndProp) {
		timeDTEndUTC = ICalTimeTypeToUTC(lpicEventRoot, lpicDTEndProp);
		timeDTEndLocal = ICalTimeTypeToLocal(lpicDTEndProp);
	} else {
		// @note not so sure if the following comment is 100% true. It may also be used to complement a missing DTSTART or DTEND, according to MS specs.
		// When DTEND is not in the ical, it should be a recurring message, which never ends!

		// use duration for "end"
		lpicDurationProp = icalcomponent_get_first_property(lpicEvent, ICAL_DURATION_PROPERTY);
		if (lpicDurationProp == nullptr)
			// and then we get here when it never ends??
			return MAPI_E_INVALID_PARAMETER;

		icaldurationtype dur = icalproperty_get_duration(lpicDurationProp);
		timeDTEndLocal = timeDTStartLocal + icaldurationtype_as_int(dur);
		timeDTEndUTC = timeDTStartUTC + icaldurationtype_as_int(dur);
	}
	timeEndOffset = timeDTEndUTC - timeDTEndLocal;

	if (bIsAllday)
		sPropVal.Value.ft = UnixTimeToFileTime(timeDTEndLocal);
	else
		sPropVal.Value.ft = UnixTimeToFileTime(timeDTEndUTC);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTENDWHOLE], PT_SYSTIME);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	
	// Set 0x8517 / CommonEnd
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COMMONEND], PT_SYSTIME);
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// Set PR_END_DATE
	sPropVal.ulPropTag = PR_END_DATE;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);

	// Set duration
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTDURATION], PT_LONG);
	sPropVal.Value.ul = timeDTEndUTC - timeDTStartUTC;
	/**
	 * @note This isn't what you think. The MAPI duration has a very
	 * complicated story, when timezone compensation should be applied
	 * and when not. This is a simplified version, which seems to work ... for now.
	 *
	 * See chapter 3.1.5.5 of [MS-OXOCAL].pdf for a more detailed story.
	 */
	if (!bIsAllday)
		sPropVal.Value.ul += (timeEndOffset - timeStartOffset);

	// Convert from seconds to minutes.
	sPropVal.Value.ul /= 60;
	lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	// @todo add flags not to add these props ?? or maybe use a exclude filter in ICalToMAPI::GetItem()
	// Set submit time / DTSTAMP
	return hrSuccess;
}

/** 
 * Create a new ical VEVENT component and set all ical properties in
 * the returned object.
 * 
 * @param[in]  lpMessage The message to convert
 * @param[out] lpicMethod The ical method of the top VCALENDAR object (hint, can differ when mixed methods are present in one VCALENDAR)
 * @param[out] lppicTZinfo ical timezone struct, describes all times used in this ical component
 * @param[out] lpstrTZid The name of the timezone
 * @param[out] lppEvent The ical calendar event
 * 
 * @return MAPI error code
 */
HRESULT VEventConverter::HrMAPI2ICal(LPMESSAGE lpMessage, icalproperty_method *lpicMethod, icaltimezone **lppicTZinfo, std::string *lpstrTZid, icalcomponent **lppEvent)
{
	icalcomp_ptr lpEvent(icalcomponent_new(ICAL_VEVENT_COMPONENT));
	HRESULT hr = VConverter::HrMAPI2ICal(lpMessage, lpicMethod, lppicTZinfo, lpstrTZid, lpEvent.get());
	if (hr != hrSuccess)
		return hr;
	if (lppEvent)
		*lppEvent = lpEvent.release();
	return hrSuccess;
}

/** 
 * Extends the VConverter version to set 'appt start whole' and 'appt end whole' named properties.
 * This also adds the counter proposal times on a propose new time meeting request.
 * 
 * @param[in]  lpMsgProps All (required) properties from the message to convert time properties
 * @param[in]  ulMsgProps number of properties in lpMsgProps
 * @param[in]  lpicTZinfo ical timezone object to set times in
 * @param[in]  strTZid name of the given ical timezone
 * @param[in,out] lpEvent The Ical object to modify
 * 
 * @return MAPI error code.
 */
HRESULT VEventConverter::HrSetTimeProperties(LPSPropValue lpMsgProps, ULONG ulMsgProps, icaltimezone *lpicTZinfo, const std::string &strTZid, icalcomponent *lpEvent)
{
	bool bIsAllDay = false, bCounterProposal = false;
	ULONG ulStartIndex = PROP_APPTSTARTWHOLE, ulEndIndex = PROP_APPTENDWHOLE;

	HRESULT hr = VConverter::HrSetTimeProperties(lpMsgProps, ulMsgProps,
	             lpicTZinfo, strTZid, lpEvent);
	if (hr != hrSuccess)
		return hr;

	// vevent extra
	auto lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, m_lpNamedProps->aulPropTag[PROP_COUNTERPROPOSAL]);
	if(lpPropVal && PROP_TYPE(lpPropVal->ulPropTag) == PT_BOOLEAN && lpPropVal->Value.b) {
		bCounterProposal = true;
	        if(PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PROPOSEDSTART], PT_SYSTIME)) &&
		   PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_PROPOSEDEND], PT_SYSTIME))) {
			ulStartIndex = PROP_PROPOSEDSTART;
			ulEndIndex = PROP_PROPOSEDEND;
		}
	}
 	
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_ALLDAYEVENT], PT_BOOLEAN));
	if (lpPropVal)
		bIsAllDay = (lpPropVal->Value.b == TRUE);
	// @note If bIsAllDay == true, the item is an allday event "in the timezone it was created in" (and the user selected the allday event option)
	// In another timezone, Outlook will display the item as a 24h event with times (and the allday event option disabled)
	// However, in ICal, you cannot specify a timezone for a date, so ICal will show this as an allday event in every timezone your client is in.

	// Set start time / DTSTART
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[ulStartIndex], PT_SYSTIME));
	if (lpPropVal == NULL)
		// do not create calendar items without start/end date, which is invalid.
		return MAPI_E_CORRUPT_DATA;
	auto ttTime = FileTimeToUnixTime(lpPropVal->Value.ft);
	hr = HrSetTimeProperty(ttTime, bIsAllDay, lpicTZinfo, strTZid, ICAL_DTSTART_PROPERTY, lpEvent);
	if (hr != hrSuccess)
		return hr;

	// Set end time / DTEND
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[ulEndIndex], PT_SYSTIME));
	if (lpPropVal == nullptr)
		// do not create calendar items without start/end date, which is invalid.
		return MAPI_E_CORRUPT_DATA;
	ttTime = FileTimeToUnixTime(lpPropVal->Value.ft);
	hr = HrSetTimeProperty(ttTime, bIsAllDay, lpicTZinfo, strTZid, ICAL_DTEND_PROPERTY, lpEvent);
	if (hr != hrSuccess)
		return hr;
	// @note we never set the DURATION property: MAPI objects always should have the end property 

	if (!bCounterProposal)
		return hrSuccess;

	// set the original times in X properties
	icalproperty *lpProp = NULL;

	// Set original start time / DTSTART
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTSTARTWHOLE], PT_SYSTIME));
	if (lpPropVal == nullptr)
		// do not create calendar items without start/end date, which is invalid.
		return MAPI_E_CORRUPT_DATA;
	ttTime = FileTimeToUnixTime(lpPropVal->Value.ft);
	lpProp = icalproperty_new_x("overwrite-me");
	icalproperty_set_x_name(lpProp, "X-MS-OLK-ORIGINALSTART");
	hr = HrSetTimeProperty(ttTime, bIsAllDay, lpicTZinfo, strTZid, ICAL_DTSTART_PROPERTY, lpProp);
	if (hr != hrSuccess)
		return hr;
	icalcomponent_add_property(lpEvent, lpProp);

	// Set original end time / DTEND
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_APPTENDWHOLE], PT_SYSTIME));
	if (lpPropVal == nullptr)
		// do not create calendar items without start/end date, which is invalid.
		return MAPI_E_CORRUPT_DATA;
	ttTime = FileTimeToUnixTime(lpPropVal->Value.ft);
	lpProp = icalproperty_new_x("overwrite-me");
	icalproperty_set_x_name(lpProp, "X-MS-OLK-ORIGINALEND");
	hr = HrSetTimeProperty(ttTime, bIsAllDay, lpicTZinfo, strTZid, ICAL_DTEND_PROPERTY, lpProp);
	if (hr != hrSuccess)
		return hr;
	icalcomponent_add_property(lpEvent, lpProp);
	return hrSuccess;
}

} /* namespace */
