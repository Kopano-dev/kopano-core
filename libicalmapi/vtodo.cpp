/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include "vtodo.h"
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include "nameids.h"
#include <iostream>
#include "icalmem.hpp"

namespace KC {

/** 
 * VTodo constructor, implements VConverter
 */
VTodoConverter::VTodoConverter(LPADRBOOK lpAdrBook, timezone_map *mapTimeZones, LPSPropTagArray lpNamedProps, const std::string& strCharset, bool blCensor, bool bNoRecipients, IMailUser *lpMailUser)
	: VConverter(lpAdrBook, mapTimeZones, lpNamedProps, strCharset, blCensor, bNoRecipients, lpMailUser)
{
}

/** 
 * Entrypoint to convert an ical object to MAPI object.
 * 
 * @param[in]  lpEventRoot The root component (VCALENDAR top object)
 * @param[in]  lpEvent The VTODO object to convert
 * @param[in]  lpPrevItem Optional previous (top) item to use when lpEvent describes an exception
 * @param[out] lppRet The icalitem struct to finalize into a MAPI object
 * 
 * @return MAPI error code
 */
HRESULT VTodoConverter::HrICal2MAPI(icalcomponent *lpEventRoot, icalcomponent *lpEvent, icalitem *lpPrevItem, icalitem **lppRet)
{
	HRESULT hr = VConverter::HrICal2MAPI(lpEventRoot, lpEvent,
	             lpPrevItem, lppRet);
	if (hr != hrSuccess)
		return hr;
	(*lppRet)->eType = VTODO;
	return hrSuccess;
}

/** 
 * The properties set here are all required base properties for
 * different todo items and task requests.
 *
 * Finds the status of the message (e.g. complete, cancelled) according
 * to the matching properties, or possebly the completion (in percent)
 * of the task. Lastly, the icon index (outlook icon displayed in list
 * view) is set.
 * 
 * @param[in]  icMethod Method of the ical event
 * @param[in]  lpicEvent The ical VEVENT to convert
 * @param[in]  base Used for the 'base' pointer for memory allocations
 * @param[in]  bisException Weather we're handling an exception or not
 * @param[in,out] lstMsgProps 
 * 
 * @return MAPI error code
 */
HRESULT VTodoConverter::HrAddBaseProperties(icalproperty_method icMethod, icalcomponent *lpicEvent, void *base, bool bIsException, std::list<SPropValue> *lplstMsgProps)
{
	SPropValue sPropVal;
	bool bComplete = false;
	ULONG ulStatus = 0;

	// @todo fix exception message class
	HRESULT hr = HrCopyString(base, L"IPM.Task", &sPropVal.Value.lpszW);
	if (hr != hrSuccess)
		return hr;
	sPropVal.ulPropTag = PR_MESSAGE_CLASS_W;
	lplstMsgProps->emplace_back(sPropVal);

	// STATUS == COMPLETED
	// 0: olTaskNotStarted
	// 1: olTaskInProgress
	// 2: olTaskComplete
	// 3: olTaskWaiting (on someone else)
	// 4: olTaskDeferred
	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_STATUS_PROPERTY);
	if (lpicProp) {
		switch (icalproperty_get_status(lpicProp)) {
		case ICAL_STATUS_NEEDSACTION:
			ulStatus = 3;
			break;
		case ICAL_STATUS_INPROCESS:
			ulStatus = 1;
			break;
		case ICAL_STATUS_COMPLETED:
			bComplete = true;
			ulStatus = 2;
			break;
		case ICAL_STATUS_CANCELLED:
			ulStatus = 4;
			break;
		default:
			break;
		}
	}

	// ical.app 3.0.4 does not set status property, it only sets completed property
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_COMPLETED_PROPERTY);
	if (lpicProp) {
		ulStatus = 2;
		bComplete = true;
	}
	// find PERCENT-COMPLETE
	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_PERCENTCOMPLETE_PROPERTY);
	if (lpicProp)
		sPropVal.Value.dbl = ((double)icalproperty_get_percentcomplete(lpicProp) / 100.0);
	else
		sPropVal.Value.dbl = 0.0;
	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_PERCENTCOMPLETE], PT_DOUBLE);
	lplstMsgProps->emplace_back(sPropVal);

	if (sPropVal.Value.dbl == 1.0) {
		bComplete = true;
		ulStatus = 2;
	}

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_COMPLETE], PT_BOOLEAN);
	sPropVal.Value.b = bComplete;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_STATUS], PT_LONG);
	sPropVal.Value.ul = ulStatus;
	lplstMsgProps->emplace_back(sPropVal);

	sPropVal.ulPropTag = PR_ICON_INDEX;
	// 1280: task
	// 1281: recurring task
	// 1282: delegate accepted task (?)
	// 1283: delegated task
	sPropVal.Value.ul = 1280;
	lplstMsgProps->emplace_back(sPropVal);

	// TODO: task delegation
	//if (icalcomponent_get_first_property(lpicEvent, ICAL_ATTENDEE_PROPERTY) == NULL)
	return hrSuccess;
}

/**
 * Set time properties in icalitem from the ical data. Sets the start, due and completed dates.
 *
 * @param[in]	lpicEventRoot	ical VCALENDAR component to set the timezone
 * @param[in]	lpicEvent		ical VTODO component
 * @param[in]	bIsAllday		set times for normal or allday event (unused for tasks)
 * @param[out]	lpIcalItem		icalitem structure in which mapi properties are set
 * @return		MAPI error code
 * @retval		MAPI_E_INVALID_PARAMETER	start time or timezone not present in ical data
 */
HRESULT VTodoConverter::HrAddTimes(icalproperty_method icMethod, icalcomponent *lpicEventRoot, icalcomponent *lpicEvent, bool bIsAllday, icalitem *lpIcalItem)
{
	SPropValue sPropVal;

	auto lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DTSTART_PROPERTY);
	if (lpicProp) {
		// Take the timezone from DTSTART and set that as the item timezone
		auto hr = HrAddTimeZone(lpicProp, lpIcalItem);
		if (hr != hrSuccess)
			return hr;

		// localStartTime
		auto timeDTStart = icaltime_as_timet(icalproperty_get_dtstart(lpicProp));

		// Set 0x820D / TaskStartDate
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_STARTDATE], PT_SYSTIME);
		sPropVal.Value.ft  = UnixTimeToFileTime(timeDTStart);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
		
		// utc starttime
		timeDTStart = ICalTimeTypeToUTC(lpicEventRoot, lpicProp);

		// Set 0x8516 / CommonStart
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COMMONSTART], PT_SYSTIME);
		sPropVal.Value.ft  = UnixTimeToFileTime(timeDTStart);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	} else {
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_STARTDATE], PT_SYSTIME));
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COMMONSTART], PT_SYSTIME));
	}

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_DUE_PROPERTY);
	if (lpicProp) {
		// Take the timezone from DUE and set that as the item timezone
		auto hr = HrAddTimeZone(lpicProp, lpIcalItem);
		if (hr != hrSuccess)
			return hr;

		// localduetime
		auto timeDue = icaltime_as_timet(icalproperty_get_due(lpicProp));
		// Set 0x820D / TaskDueDate
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_DUEDATE], PT_SYSTIME);
		sPropVal.Value.ft  = UnixTimeToFileTime(timeDue);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
		
		// utc duetime
		timeDue = ICalTimeTypeToUTC(lpicEventRoot, lpicProp);

		// Set 0x8516 / CommonEnd
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COMMONEND], PT_SYSTIME);
		sPropVal.Value.ft  = UnixTimeToFileTime(timeDue);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	} else {
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_DUEDATE], PT_SYSTIME));
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COMMONEND], PT_SYSTIME));
	}

	lpicProp = icalcomponent_get_first_property(lpicEvent, ICAL_COMPLETED_PROPERTY);
	if (lpicProp) {
		auto timecomplete = icaltime_as_timet(icalproperty_get_due(lpicProp));
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_COMPLETED_DATE], PT_SYSTIME);
		sPropVal.Value.ft  = UnixTimeToFileTime(timecomplete);
		lpIcalItem->lstMsgProps.emplace_back(sPropVal);
	} else {
		lpIcalItem->lstDelPropTags.emplace_back(CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_COMPLETED_DATE], PT_SYSTIME));
	}
	return hrSuccess;
}

/** 
 * Create a new ical VTODO component and set all ical properties in
 * the returned object.
 * 
 * @param[in]  lpMessage The message to convert
 * @param[out] lpicMethod The ical method of the top VCALENDAR object (hint, can differ when mixed methods are present in one VCALENDAR)
 * @param[out] lppicTZinfo ical timezone struct, describes all times used in this ical component
 * @param[out] lpstrTZid The name of the timezone
 * @param[out] lppEvent The ical task event
 * 
 * @return MAPI error code
 */
HRESULT VTodoConverter::HrMAPI2ICal(LPMESSAGE lpMessage, icalproperty_method *lpicMethod, icaltimezone **lppicTZinfo, std::string *lpstrTZid, icalcomponent **lppEvent)
{
	icalcomp_ptr lpEvent(icalcomponent_new(ICAL_VTODO_COMPONENT));
	HRESULT hr = VConverter::HrMAPI2ICal(lpMessage, lpicMethod, lppicTZinfo,
	             lpstrTZid, lpEvent.get());
	if (hr != hrSuccess)
		return hr;
	if (lppEvent)
		*lppEvent = lpEvent.release();
	return hrSuccess;
}

/** 
 * Extends the VConverter version to set 'common start', 'common end'
 * and 'task completed' named properties.
 * 
 * @param[in]  lpMsgProps All (required) properties from the message to convert time properties
 * @param[in]  ulMsgProps number of properties in lpMsgProps
 * @param[in]  lpicTZinfo ical timezone object to set times in
 * @param[in]  strTZid name of the given ical timezone
 * @param[in,out] lpEvent The Ical object to modify
 * 
 * @return MAPI error code.
 */
HRESULT VTodoConverter::HrSetTimeProperties(LPSPropValue lpMsgProps, ULONG ulMsgProps, icaltimezone *lpicTZinfo, const std::string &strTZid, icalcomponent *lpEvent)
{
	HRESULT hr = VConverter::HrSetTimeProperties(lpMsgProps, ulMsgProps,
	             lpicTZinfo, strTZid, lpEvent);
	if (hr != hrSuccess)
		return hr;

	// vtodo extra
	// Uses CommonStart/CommonEnd as its stores UTC time

	// Set start time / DTSTART	
	auto lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COMMONSTART], PT_SYSTIME));
	if (lpPropVal != NULL) {
		hr = HrSetTimeProperty(FileTimeToUnixTime(lpPropVal->Value.ft),
		     false, lpicTZinfo, strTZid, ICAL_DTSTART_PROPERTY, lpEvent);
		if (hr != hrSuccess)
			return hr;
	}

	// Set end time / DUE
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_COMMONEND], PT_SYSTIME));
	if (lpPropVal) {
		hr = HrSetTimeProperty(FileTimeToUnixTime(lpPropVal->Value.ft),
		     false, lpicTZinfo, strTZid, ICAL_DUE_PROPERTY, lpEvent);
		if (hr != hrSuccess)
			return hr;
	}
	// else duration ?

	// Set Completion time
	lpPropVal = PCpropFindProp(lpMsgProps, ulMsgProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_COMPLETED_DATE], PT_SYSTIME));
	if (lpPropVal) {
		hr = HrSetTimeProperty(FileTimeToUnixTime(lpPropVal->Value.ft),
		     false, lpicTZinfo, strTZid, ICAL_COMPLETED_PROPERTY, lpEvent);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

/** 
 * Sets some task only specific properties in the ical object. Sets
 * the following ical properties:
 * - PERCENT-COMPLETE
 * - STATUS
 * 
 * @param[in]  ulProps Number of properties in lpProps
 * @param[in]  lpProps Properties of the message to convert
 * @param[in,out] lpicEvent The ical object to modify
 * 
 * @return Always return hrSuccess
 */
HRESULT VTodoConverter::HrSetItemSpecifics(ULONG ulProps, LPSPropValue lpProps, icalcomponent *lpicEvent)
{
	double pc = 0.0;

	auto lpPropVal = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_PERCENTCOMPLETE], PT_DOUBLE));
	if (lpPropVal)
		pc = lpPropVal->Value.dbl;
	icalcomponent_add_property(lpicEvent, icalproperty_new_percentcomplete(pc * 100));

	lpPropVal = PCpropFindProp(lpProps, ulProps, CHANGE_PROP_TYPE(m_lpNamedProps->aulPropTag[PROP_TASK_STATUS], PT_LONG));
	ULONG ulStatus = lpPropVal != nullptr ? lpPropVal->Value.ul : 0;

	switch (ulStatus) {
	case 1:	// olTaskInProgress
		icalcomponent_add_property(lpicEvent, icalproperty_new_status(ICAL_STATUS_INPROCESS));
		break;
	case 2:	// olTaskCompleted
		icalcomponent_add_property(lpicEvent, icalproperty_new_status(ICAL_STATUS_COMPLETED));
		break;
	case 3: // olTaskNeedsAction
		icalcomponent_add_property(lpicEvent, icalproperty_new_status(ICAL_STATUS_NEEDSACTION));
		break;
	case 4:	// olTaskDeferred
		icalcomponent_add_property(lpicEvent, icalproperty_new_status(ICAL_STATUS_CANCELLED));
		break;
	}
	return hrSuccess;
}

} /* namespace */
