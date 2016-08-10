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

#include "recurrence.h"
#include <cmath>
#include <mapicode.h>
#include <kopano/stringutil.h>
#include <kopano/ECIConv.h>
#include <ctime>
#include <kopano/CommonUtil.h>
#include <mapiutil.h>
#include <kopano/mapiguidext.h>
#include <kopano/namedprops.h>

#include <iostream>
#include <algorithm>
using namespace std;

recurrence::recurrence() {
	m_ulMonth = 0x0;
}

/**
 * Load recurrence from blob property data
 *
 * @param[in]	lpData	Data from named property RecurrenceState
 * @param[in]	ulLen	Length of lpData
 * @param[in]	ulFlags	RECURRENCE_STATE_TASKS if the recurrence is from a task
 */
HRESULT recurrence::HrLoadRecurrenceState(char *lpData, unsigned int ulLen, ULONG ulFlags) {
	return m_sRecState.ParseBlob(lpData, ulLen, ulFlags);
}

/**
 * Write the new recurrence blob.
 *
 * @param[out]	lppData	The blob will be returned in this pointer.
 * @param[out]	lpulLen	Length of returned data in lppData.
 * @param[in]	base	base pointer to MAPIAllocateMore() for lppData allocation. NULL to use MAPIAllocateBuffer().
 */
HRESULT recurrence::HrGetRecurrenceState(char **lppData, unsigned int *lpulLen, void *base)
{
	HRESULT hr;
	time_t tStart;
	LONG rStart;
	struct tm tm;

	// VALIDATION ONLY, not auto-correcting .. you should enter data correctly!
	if (m_sRecState.ulRecurFrequency != 0x200A && m_sRecState.ulRecurFrequency != 0x200B &&
		m_sRecState.ulRecurFrequency != 0x200C && m_sRecState.ulRecurFrequency != 0x200D)
	{
        hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	if (m_sRecState.ulPatternType != 0x0000 && m_sRecState.ulPatternType != 0x0001 && 
		m_sRecState.ulPatternType != 0x0002 && m_sRecState.ulPatternType != 0x0003 && m_sRecState.ulPatternType != 0x0004 && 
		m_sRecState.ulPatternType != 0x000A && m_sRecState.ulPatternType != 0x000B && m_sRecState.ulPatternType != 0x000C)
	{
        hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	if (m_sRecState.ulEndType != 0x2021 && m_sRecState.ulEndType != 0x2022 && m_sRecState.ulEndType != 0x2023)
	{
        hr = MAPI_E_CORRUPT_DATA;
		goto exit;
	}

	// calculate ulFirstDateTime
	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		if (m_sRecState.ulPatternType == 1)
			m_sRecState.ulFirstDateTime = 6 * 24 * 60;
		else
			m_sRecState.ulFirstDateTime = m_sRecState.ulStartDate % m_sRecState.ulPeriod;
		break;
	case RF_WEEKLY:
		int daycount, dayskip;
		int weekskip;

		tStart = getStartDate();
		gmtime_safe(&tStart, &tm);

		daycount = 0;
		dayskip = -1;
		for (int j = 0; j < 7; ++j) {
			if (m_sRecState.ulWeekDays & (1<<((tm.tm_wday + j)%7))) {
				if (dayskip == -1)
					dayskip = j;
				++daycount;
			}
		}
		// dayskip is the number of days to skip from the startdate until the first occurrence
		// daycount is the number of days per week that an occurrence occurs

		weekskip = 0;
		if ((tm.tm_wday < (int)m_sRecState.ulFirstDOW && dayskip > 0) || (tm.tm_wday+dayskip) > 6)
			weekskip = 1;
		// weekskip is the amount of weeks to skip from the startdate before the first occurence

		// The real start is start + dayskip + weekskip-1 (since dayskip will already bring us into the next week)
		tStart = tStart + (dayskip * 24*60*60) + (weekskip * (m_sRecState.ulPeriod-1) * 7 * 24*60*60);
		gmtime_safe(&tStart, &tm);

		UnixTimeToRTime(tStart, &rStart);
		m_sRecState.ulFirstDateTime = rStart % (m_sRecState.ulPeriod*7*24*60);

		m_sRecState.ulFirstDateTime -= ((tm.tm_wday-1) * 24 * 60); // php says -1, but it's already 0..6 ... err?
		break;
	case RF_MONTHLY:
	case RF_YEARLY:
		tStart = getStartDate();
		gmtime_safe(&tStart, &tm);

		rStart = ((((12%m_sRecState.ulPeriod) * (( ((tm.tm_year + 1900)) - 1601)%m_sRecState.ulPeriod)) % m_sRecState.ulPeriod) + ( ((tm.tm_mon)) ))%m_sRecState.ulPeriod;

		m_sRecState.ulFirstDateTime = 0;
		for (int i = 0; i < rStart; ++i)
			m_sRecState.ulFirstDateTime += MonthInSeconds(1601 + (i/12), (i%12)+1) / 60;

		break;
	}

	// it's not really possible to set this from a program, so we fix this here
	if (m_sRecState.ulEndType == ET_NEVER)
		m_sRecState.ulEndDate = 0x5AE980DF;

	// lstModified must be sorted and unique
	sort(m_sRecState.lstModifiedInstanceDates.begin(), m_sRecState.lstModifiedInstanceDates.end());
	unique(m_sRecState.lstModifiedInstanceDates.begin(), m_sRecState.lstModifiedInstanceDates.end());
	m_sRecState.ulModifiedInstanceCount = m_sRecState.lstModifiedInstanceDates.size();

	// lstDeleted must be sorted and unique
	sort(m_sRecState.lstDeletedInstanceDates.begin(), m_sRecState.lstDeletedInstanceDates.end());
	unique(m_sRecState.lstDeletedInstanceDates.begin(), m_sRecState.lstDeletedInstanceDates.end());
	m_sRecState.ulDeletedInstanceCount = m_sRecState.lstDeletedInstanceDates.size();

	// exception info count is same as number of modified items
	m_sRecState.ulExceptionCount = m_sRecState.lstModifiedInstanceDates.size();

	hr = m_sRecState.GetBlob(lppData, lpulLen, base);

exit:
	return hr;
}

HRESULT recurrence::HrGetHumanReadableString(std::string *lpstrHRS)
{
	HRESULT hr = S_OK;
	std::string strHRS;

	strHRS = "This item is recurring";
	// @todo: make strings like outlook does, and probably make it std::wstring

	*lpstrHRS = strHRS;

	return hr;
}

recurrence::freq_type recurrence::getFrequency()
{
	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		return DAILY;
	case RF_WEEKLY:
		return WEEKLY;
	case RF_MONTHLY:
		return MONTHLY;
	case RF_YEARLY:
		return YEARLY;
	}
	return DAILY;
}

HRESULT recurrence::setFrequency(freq_type ft)
{
	HRESULT hr = S_OK;

	switch (ft) {
	case DAILY:
		m_sRecState.ulRecurFrequency = RF_DAILY;
		m_sRecState.ulPatternType = 0;
		m_sRecState.ulPeriod = 60*24; // stored in minutes
		break;
	case WEEKLY:
		m_sRecState.ulRecurFrequency = RF_WEEKLY;
		m_sRecState.ulPatternType = 1;
		m_sRecState.ulPeriod = 1;
		break;
	case MONTHLY:
		m_sRecState.ulRecurFrequency = RF_MONTHLY;
		m_sRecState.ulPatternType = 2;
		m_sRecState.ulPeriod = 1;
		break;
	case YEARLY:
		m_sRecState.ulRecurFrequency = RF_YEARLY;
		m_sRecState.ulPatternType = 2; // every Nth month
		m_sRecState.ulPeriod = 12;
		break;
	default:
		hr = E_INVALIDARG;
		break;
	}

	return hr;
}

time_t recurrence::getStartDate()
{
	time_t tStart = 0;
	RTimeToUnixTime(m_sRecState.ulStartDate, &tStart);
	return tStart;
}

HRESULT recurrence::setStartDate(time_t tStart)
{
	tStart = StartOfDay(tStart);
	return UnixTimeToRTime(tStart, (LONG*)&m_sRecState.ulStartDate);
}

time_t recurrence::getEndDate()
{
	time_t tEnd;
	RTimeToUnixTime(m_sRecState.ulEndDate, &tEnd);
	return tEnd;
}

HRESULT recurrence::setEndDate(time_t tEnd)
{
	tEnd = StartOfDay(tEnd);
	return UnixTimeToRTime(tEnd, (LONG*)&m_sRecState.ulEndDate);
}

ULONG recurrence::getStartTimeOffset()
{
	return m_sRecState.ulStartTimeOffset*60;
}

HRESULT recurrence::setStartTimeOffset(ULONG ulMinutesSinceMidnight)
{
	if (ulMinutesSinceMidnight >= 24 * 60)
		return E_INVALIDARG;

	m_sRecState.ulStartTimeOffset = ulMinutesSinceMidnight;

	return S_OK;
}

ULONG recurrence::getEndTimeOffset()
{
	return m_sRecState.ulEndTimeOffset*60;
}

HRESULT recurrence::setEndTimeOffset(ULONG ulMinutesSinceMidnight)
{
	m_sRecState.ulEndTimeOffset = ulMinutesSinceMidnight;

	return S_OK;
}

time_t recurrence::getStartDateTime()
{
	time_t tStart;

	RTimeToUnixTime(m_sRecState.ulStartDate, &tStart);
	return tStart + (m_sRecState.ulStartTimeOffset*60);
}

HRESULT recurrence::setStartDateTime(time_t t)
{
	HRESULT hr;
	time_t startDate = StartOfDay(t);

	hr = UnixTimeToRTime(startDate, (LONG*)&m_sRecState.ulStartDate);
	m_sRecState.ulStartTimeOffset = (t - startDate)/60;

	return hr;
}

time_t recurrence::getEndDateTime()
{
	time_t tStart;

	RTimeToUnixTime(m_sRecState.ulEndDate, &tStart);
	return tStart + (m_sRecState.ulEndTimeOffset*60);
}

HRESULT recurrence::setEndDateTime(time_t t)
{
	HRESULT hr;

	hr = UnixTimeToRTime(StartOfDay(t), (LONG*)&m_sRecState.ulEndDate);
	// end time in minutes since midnight of start of item
	m_sRecState.ulEndTimeOffset = (t - getStartDate())/60;

	return hr;
}

ULONG recurrence::getCount()
{
	return m_sRecState.ulOccurrenceCount;
}

HRESULT recurrence::setCount(ULONG ulCount)
{
	m_sRecState.ulOccurrenceCount = ulCount;
	return S_OK;
}

recurrence::term_type recurrence::getEndType()
{
	if (m_sRecState.ulEndType == ET_DATE)
		return DATE;
	else if (m_sRecState.ulEndType == ET_NUMBER)
		return NUMBER;
	return NEVER;
}

HRESULT recurrence::setEndType(term_type t)
{
	switch (t) {
	case DATE:
		m_sRecState.ulEndType = ET_DATE;
		break;
	case NUMBER:
		m_sRecState.ulEndType = ET_NUMBER;
		break;
	case NEVER:
	default:
		m_sRecState.ulEndType = ET_NEVER;
		break;
	};
	return S_OK;
}

ULONG recurrence::getInterval()
{
	ULONG rv;

	if (m_sRecState.ulPatternType == 0) {
		// day pattern type, period stored in minutes per day
		rv = m_sRecState.ulPeriod / (60*24);
	} else if (getFrequency() == recurrence::YEARLY) {
		// yearly stored in months
		rv = m_sRecState.ulPeriod / 12;
	} else {
		// either weeks or months, no conversion required
		rv = m_sRecState.ulPeriod;
	}

	return rv;
}

// Note: Frequency must be set before the interval!
HRESULT recurrence::setInterval(ULONG i)
{
	if (i == 0)
		return E_INVALIDARG;
	m_sRecState.ulPeriod = m_sRecState.ulPeriod * i; // in setFrequency(), ulPeriod is set to "1" for each type
	return S_OK;
}

HRESULT recurrence::setSlidingFlag(ULONG s)
{
	m_sRecState.ulSlidingFlag = s;
	return S_OK;
}

ULONG recurrence::getFirstDOW()
{
	return m_sRecState.ulFirstDOW;
}

HRESULT recurrence::setFirstDOW(ULONG ulFirstDOW)
{
	m_sRecState.ulFirstDOW = ulFirstDOW;
	return S_OK;
}

UCHAR recurrence::getWeekDays()
{
	// valid ulPatternTypes: 1 2 4 a c
	if (m_sRecState.ulPatternType == 0)
		return 0;
	return m_sRecState.ulWeekDays;
}

HRESULT recurrence::setWeekDays(UCHAR d)
{
	// if setWeekDays is called on a daily event, update the pattern type
	if (m_sRecState.ulPatternType == 0) {
		m_sRecState.ulPatternType = 1;
		m_sRecState.ulPeriod = m_sRecState.ulPeriod / (24*60); // convert period from daily to "weekly"
	}

	m_sRecState.ulWeekDays = d & WD_MASK;
	return S_OK;
}

UCHAR recurrence::getDayOfMonth()
{
	if ( m_sRecState.ulRecurFrequency != RF_YEARLY && m_sRecState.ulRecurFrequency != RF_MONTHLY && (m_sRecState.ulPatternType != 2 || m_sRecState.ulPatternType != 4))
		return 0;
	return m_sRecState.ulDayOfMonth;
}

HRESULT recurrence::setDayOfMonth(UCHAR d)
{
	m_sRecState.ulDayOfMonth = d;
	return S_OK;
}

/**
 * Get the month between 1...12
 * 1 = jan
 */
UCHAR recurrence::getMonth()
{
	if (m_ulMonth > 0 && m_ulMonth < 13) {
		return m_ulMonth;
	}

	struct tm tmMonth;
	time_t tStart = getStartDate();
	gmtime_safe(&tStart, &tmMonth);

	return tmMonth.tm_mon+1;
}

HRESULT recurrence::setMonth(UCHAR m)
{
	if(m < 1 || m > 12)
		return MAPI_E_CALL_FAILED;

	m_ulMonth = m;
	return hrSuccess;
}

// only valid in monthly type 0x3 and 0xb
UCHAR recurrence::getWeekNumber()
{
	if (m_sRecState.ulPatternType != 3 && m_sRecState.ulPatternType != 0xB)
		return 0;
	return m_sRecState.ulWeekNumber;
}

HRESULT recurrence::setWeekNumber(UCHAR s)
{
	// we should be handling monthly recurrence items here, calendar type 0xB (hijri) is not supported
	m_sRecState.ulPatternType = 3;
	m_sRecState.ulWeekNumber = s;
	return S_OK;
}

// ------------
// handle exceptions
// ------------

HRESULT recurrence::addDeletedException(time_t tDelete)
{
	HRESULT hr = S_OK;
	time_t tDayDelete = StartOfDay(tDelete);
	ULONG rtime;

	hr = UnixTimeToRTime(tDayDelete, (LONG*)&rtime);

	m_sRecState.lstDeletedInstanceDates.push_back(rtime);

	return hr;
}

std::list<time_t> recurrence::getDeletedExceptions() {
	time_t tDayDelete;
	time_t offset = getStartTimeOffset();
	std::list<time_t> lstDeletes;
	std::vector<unsigned int> lstDeletedInstanceDates;
	std::vector<unsigned int>::iterator d;
	std::vector<RecurrenceState::Exception>::const_iterator iExceptions;

	// make copy of struct info
	lstDeletedInstanceDates = m_sRecState.lstDeletedInstanceDates;

	for (iExceptions = m_sRecState.lstExceptions.begin(); iExceptions != m_sRecState.lstExceptions.end(); ++iExceptions) {
		// if startofday(exception.basedata) == present in lstDeletes, that's a move, so remove from deletes list
		d = find(lstDeletedInstanceDates.begin(), lstDeletedInstanceDates.end(), iExceptions->ulOriginalStartDate - (iExceptions->ulOriginalStartDate % 1440));
		if (d != lstDeletedInstanceDates.end())
			lstDeletedInstanceDates.erase(d);
	}

	for (d = lstDeletedInstanceDates.begin();
	     d != lstDeletedInstanceDates.end(); ++d) {
		RTimeToUnixTime(*d, &tDayDelete);
		lstDeletes.push_back(tDayDelete + offset);
	}

	return lstDeletes;
}

std::list<time_t> recurrence::getModifiedOccurrences() {
	time_t tDayModified;
	std::list<time_t> lstModified;
	std::vector<unsigned int> lstModifiedInstanceDates;
	std::vector<unsigned int>::const_iterator d;
	std::vector<RecurrenceState::Exception>::const_iterator iExceptions;

	// make copy of struct info
	lstModifiedInstanceDates = m_sRecState.lstModifiedInstanceDates;

	for (iExceptions = m_sRecState.lstExceptions.begin();
	     iExceptions != m_sRecState.lstExceptions.end(); ++iExceptions) {
		RTimeToUnixTime(iExceptions->ulOriginalStartDate, &tDayModified);
		lstModified.push_back(tDayModified);
	}

	return lstModified;
}
ULONG recurrence::getModifiedCount()
{
	return m_sRecState.ulModifiedInstanceCount;
}

ULONG recurrence::getModifiedFlags(ULONG id)
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	return m_sRecState.lstExceptions[id].ulOverrideFlags;
}

time_t recurrence::getModifiedStartDateTime(ULONG id)
{
	time_t tDayModified = 0;

	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	
	RTimeToUnixTime(m_sRecState.lstExceptions[id].ulStartDateTime, &tDayModified);
	return tDayModified;
}

time_t recurrence::getModifiedEndDateTime(ULONG id)
{
	time_t tDayModified = 0;

	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	
	RTimeToUnixTime(m_sRecState.lstExceptions[id].ulEndDateTime, &tDayModified);
	return tDayModified;
}

time_t recurrence::getModifiedOriginalDateTime(ULONG id)
{
	time_t tDayModified = 0;

	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	
	RTimeToUnixTime(m_sRecState.lstExceptions[id].ulOriginalStartDate, &tDayModified);
	return tDayModified;
}

std::wstring recurrence::getModifiedSubject(ULONG id)
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return wstring();
	return m_sRecState.lstExtendedExceptions[id].strWideCharSubject;
}

ULONG recurrence::getModifiedMeetingType(ULONG id)
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	return m_sRecState.lstExceptions[id].ulApptStateFlags;
}

LONG recurrence::getModifiedReminderDelta(ULONG id)
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	return m_sRecState.lstExceptions[id].ulReminderDelta;
}

ULONG recurrence::getModifiedReminder(ULONG id)
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	return m_sRecState.lstExceptions[id].ulReminderSet;
}

std::wstring recurrence::getModifiedLocation(ULONG id)
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return wstring();
	return m_sRecState.lstExtendedExceptions[id].strWideCharLocation;
}

ULONG recurrence::getModifiedBusyStatus(ULONG id)
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	return m_sRecState.lstExceptions[id].ulBusyStatus;
}

ULONG recurrence::getModifiedAttachment(ULONG id)
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	return m_sRecState.lstExceptions[id].ulAttachment;
}

ULONG recurrence::getModifiedSubType(ULONG id)
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	return m_sRecState.lstExceptions[id].ulSubType;
}

HRESULT recurrence::addModifiedException(time_t tStart, time_t tEnd, time_t tOriginalStart, ULONG *lpid)
{
	HRESULT hr = S_OK;
	LONG rStart, rEnd, rOrig, rDayStart;
	vector<ULONG>::reverse_iterator i;
	ULONG id = 0;
	RecurrenceState::Exception sException = {0};
	RecurrenceState::ExtendedException sExtException = {0};

	UnixTimeToRTime(tStart, &rStart);
	UnixTimeToRTime(tEnd, &rEnd);
	UnixTimeToRTime(tOriginalStart, &rOrig);

	// this is not thread safe, but since this code is not (yet)
	// called in a thread unsafe manner I could not care less at
	// the moment
	id = m_sRecState.lstModifiedInstanceDates.size();

	// move is the exception day start
	UnixTimeToRTime(StartOfDay(tStart), &rDayStart);
	m_sRecState.lstModifiedInstanceDates.push_back(rDayStart);

	// every modify is also a delete in the blob
	// delete is the original start
	UnixTimeToRTime(StartOfDay(tOriginalStart), &rDayStart);
	m_sRecState.lstDeletedInstanceDates.push_back(rDayStart);

	sExtException.ulStartDateTime     = sException.ulStartDateTime     = rStart;
	sExtException.ulEndDateTime       = sException.ulEndDateTime       = rEnd;
	sExtException.ulOriginalStartDate = sException.ulOriginalStartDate = rOrig;
	sExtException.ulChangeHighlightValue = 0;

	m_sRecState.lstExceptions.push_back(sException);
	m_sRecState.lstExtendedExceptions.push_back(sExtException);

	*lpid = id;

	return hr;
}

HRESULT recurrence::setModifiedSubject(ULONG id, std::wstring strSubject)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_SUBJECT;
	m_sRecState.lstExceptions[id].strSubject = convert_to<string>(strSubject);
	m_sRecState.lstExtendedExceptions[id].strWideCharSubject = strSubject;

	return S_OK;
}

HRESULT recurrence::setModifiedMeetingType(ULONG id, ULONG type)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_MEETINGTYPE;
	m_sRecState.lstExceptions[id].ulApptStateFlags = type;

	return S_OK;
}

HRESULT recurrence::setModifiedReminderDelta(ULONG id, LONG delta)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_REMINDERDELTA;
	m_sRecState.lstExceptions[id].ulReminderDelta = delta;

	return S_OK;
}

HRESULT recurrence::setModifiedReminder(ULONG id, ULONG set)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_REMINDERSET;
	m_sRecState.lstExceptions[id].ulReminderSet = set;

	return S_OK;
}

HRESULT recurrence::setModifiedLocation(ULONG id, std::wstring strLocation)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_LOCATION;
	m_sRecState.lstExceptions[id].strLocation = convert_to<string>(strLocation);
	m_sRecState.lstExtendedExceptions[id].strWideCharLocation = strLocation;

	return S_OK;
}

HRESULT recurrence::setModifiedBusyStatus(ULONG id, ULONG status)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_BUSYSTATUS;
	m_sRecState.lstExceptions[id].ulBusyStatus = status;

	return S_OK;
}

HRESULT recurrence::setModifiedAttachment(ULONG id)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_ATTACHMENT;

	return S_OK;
}

HRESULT recurrence::setModifiedSubType(ULONG id, ULONG subtype)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_SUBTYPE;
	m_sRecState.lstExceptions[id].ulSubType = subtype;

	return S_OK;
}

HRESULT recurrence::setModifiedApptColor(ULONG id, ULONG color)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_APPTCOLOR;
	m_sRecState.lstExceptions[id].ulAppointmentColor = color;

	return S_OK;
}

HRESULT recurrence::setModifiedBody(ULONG id)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;

	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_EXCEPTIONAL_BODY;

	return S_OK;
}

/*
HRESULT recurrence::setDeletedOccurrence(time_t t)
{
	if (isException(t) || !isOccurrence(t))
		return S_FALSE;

	// FIXME: should only do deleted_list handling .. only the blob is weird with del/mod
	exceptions.push_back(DayStartOf(t));
	deleted_occurrences.push_back(DayStartOf(t));

	return S_OK;
}

HRESULT recurrence::removeDeletedOccurrence(time_t t)
{
	if (!isDeletedOccurrence(t))
		return S_FALSE;

	// FIXME: should only do deleted_list handling .. only the blob is weird with del/mod
	exceptions.remove(DayStartOf(t));
	deleted_occurrences.remove(DayStartOf(t));

	return S_OK;
}

std::list<time_t> recurrence::getDeletedOccurrences()
{
	return deleted_occurrences;
}

// ?
HRESULT recurrence::getChangedOccurrence(time_t t, changed_occurrence_type *c)
{
	for (std::list<changed_occurrence_type>::const_iterator i = changed_occurrences.begin();
	     i != changed_occurrences.end(); ++i)
		if (DayStartOf(i->basedate) == DayStartOf(t))
		{
			*c = *i;
			return S_OK;
		}
	return S_FALSE;
}

HRESULT recurrence::setChangedOccurrence(changed_occurrence_type c){
	if(isException(c.basedate) || !isOccurrence(c.basedate))
		return MAPI_E_CALL_FAILED;

	exceptions.push_back(DayStartOf(c.basedate));
	changed_occurrences.push_back(c);
	return S_OK;
}

HRESULT recurrence::removeChangedOccurrence(time_t t){
	if(!isChangedOccurrence(t))
		return MAPI_E_CALL_FAILED;
	for (std::list<changed_occurrence_type>::const_iterator i = changed_occurrences.begin();
	     i != changed_occurrences.end(); ++i)
		if(DayStartOf(i->basedate) == DayStartOf(t)){
			changed_occurrences.erase(i);
			return S_OK;
		}
	return MAPI_E_NOT_FOUND;
}

list<recurrence::changed_occurrence_type> recurrence::getChangedOccurrences(){
	return changed_occurrences;
}

bool recurrence::isException(time_t t){
	for (std::list<time_t>::const_iterator i = exceptions.begin();
	     i != exceptions.end(); ++i)
		if(DayStartOf(*i) == DayStartOf(t))
			return true;
	return false;
}

list<time_t> recurrence::getExceptions(){
	return exceptions;
}

bool recurrence::isOccurrence(time_t time){
	calcEndDate();
	if(this->term != NEVER && time > (this->enddate + this->starttime*60))
		return false;

	if(isDeletedOccurrence(time))
		return false;

	//loop through changed occurrences
	for (std::list<changed_occurrence_type>::const_iterator change = changed_occurrences.begin();
	     change != changed_occurrences.end(); ++change)
		if(change->startdate == time)
			return true;
	return isRuleOccurrence(time);
}

bool recurrence::isRuleOccurrence(time_t time){
	time_t ulStart;
	ULONGLONG diff; //number of days between start and occurrence
	ULONG ulUlPeriod;
	ULONG ulMonth;

	if(time < this->startdate)
		return false;

	calcEndDate();
	if(this->term != NEVER && time > (this->enddate + this->starttime*60))
		return false;

	if(this->freq == DAILY){
		diff = (time - this->startdate)/(24*60*60);
		return diff % m_sRecState.ulPeriod == 0;
	}else if(this->freq == WEEKLY){
		int a = WeekDayFromTime(time);
		if(!(((1 << WeekDayFromTime(time)) & this->weekdays)))
			return false;
		ulStart = WeekStartOf(this->startdate);
		diff = (time - this->startdate)/(24*60*60);
		return (((ULONGLONG)(diff / 7)) % m_sRecState.ulPeriod == 0);
	}else{ //monthly or yearly
		ulUlPeriod = m_sRecState.ulPeriod;
		if(this->freq == YEARLY)
			ulUlPeriod *= 12;

		if(this->nday>0){
			if(!(((1 << WeekDayFromTime(time)) & this->weekdays)))
				return false;
			diff = AllMonthsFromTime(time) - AllMonthsFromTime(this->startdate);
			if(diff % ulUlPeriod != 0)
				return false;
			ulMonth = MonthFromTime(time);
			if(this->nday == 5 && ulMonth != MonthFromTime(time + 7*24*60*60))
				return true;
			if(ulMonth != MonthFromTime(time - 7*24*60*60))
				return this->nday == 1;
			if(ulMonth != MonthFromTime(time - 14*24*60*60))
				return this->nday == 2;
			if(ulMonth != MonthFromTime(time - 21*24*60*60))
				return this->nday == 3;
			if(ulMonth != MonthFromTime(time - 28*24*60*60))
				return this->nday == 4;
			return false;
		}else{
			ULONG ulDays = DaysInMonth(MonthFromTime(time));
			if(MonthDayFromTime(time) != this->monthday && !(this->monthday > ulDays && MonthDayFromTime(time) == ulDays))
				return false; //the day of the month is wrong
			diff = AllMonthsFromTime(time) - AllMonthsFromTime(this->startdate);
			return diff % ulUlPeriod == 0;
		}
	}

	return false;
}

bool recurrence::isDeletedOccurrence(time_t t){
	for (std::list<time_t>::const_iterator i = deleted_occurrences.begin();
	     i != deleted_occurrences.end(); ++i)
		if(DayStartOf(*i) == DayStartOf(t))
			return true;
	return false;
}

bool recurrence::isChangedOccurrence(time_t t){
	for (std::list<changed_occurrence_type>::const_iterator i = changed_occurrences.begin();
	     i != changed_occurrences.end(); ++i)
		if(DayStartOf(i->basedate) == DayStartOf(t))
			return true;
	return false;
}

bool recurrence::isAfter(time_t tStamp)
{
	struct tm tmStamp;
	gmtime_safe(&tStamp, &tmStamp);

	switch (getFrequency()) {
	case DAILY:
		break;
	case WEEKLY:
		for (char i = 0; i < 7; ++i) {
			if (m_sRecState.ulWeekDays & (1<<i))
			{
				if (tmStamp.tm_wday < i)
					return true;
				if (tmStamp.tm_wday > i)
					return false;
				break;			// break? what about other days? think!
			}
		}
		break;

	case YEARLY:
		if (tmStamp.tm_mon < getMonth())
			return true;
		if (tmStamp.tm_mon > getMonth())
			return false;
		break;					// added
	case MONTHLY:
		if (getWeekNumber() == 5)
		{
			char d = DaysInMonth(tmStamp.tm_mon + 1, tmStamp.tm_year + 1900) - tmStamp.tm_mday;
			if (d >= 7)
				return true;
			char i = tmStamp.tm_wday - (6 - d);
			if (i < 0)
				i+=7;
			while (i != tmStamp.tm_wday) {
				if (getWeekDays() & (1<<i))
					return false;
				i = (i + 1) % 7;
			}
			if (!(getWeekDays() & (1 << tmStamp.tm_wday)))
				return true;
		}
		else if (getWeekNumber() > 0)
		{
			if (getWeekNumber() < tmStamp.tm_mday / 7)
				return true;
			if (getWeekNumber() < tmStamp.tm_mday / 7)
				return false;
			char i = tmStamp.tm_wday - ((tmStamp.tm_mday - 1) % 7);
			if (i < 0)
				i += 7;
			while (i != tmStamp.tm_wday) {
				if (getWeekDays() & (1<<i))
					return false;
				i = (i + 1) % 7;
			}
			if (!(getWeekDays() & (1 << tmStamp.tm_wday)))
				return true;
		}
		else
		{
			if (tmStamp.tm_mday < getDayOfMonth())
				return true;
			if (tmStamp.tm_mday > getDayOfMonth())
				return true;
		}
	}

	if (tmStamp.tm_hour * 60 + tmStamp.tm_min < getStartDateTime())
		return true;

	return false;
}

list<time_t> recurrence::getOccurrencesBetween(time_t begin, time_t end){
	list<time_t> occurrences;
	if(begin > end)
		return occurrences;

	//first add all changed_occurrences with startdate between begin and end
	for (std::list<changed_occurrence_type>::const_iterator changed = changed_occurrences.begin();
	     changed != changed_occurrences.end(); ++changed)
		if(changed->startdate > begin && changed->startdate < end)
			occurrences.push_back(changed->startdate);

	//check startdate & enddate
	if(end < this->startdate)
		return occurrences;
	if(this->term != NEVER && this->enddate < begin)
		return occurrences;

	//TODO: loop through occurrences and add every occurrence that's not an exception

	return occurrences;
}
*/

time_t recurrence::calcStartDate()
{
	time_t tStart = getStartDateTime();
	struct tm tm;

	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		// Use the default start date.
		break;
	case RF_WEEKLY:
		int daycount, dayskip;
		int weekskip;

		gmtime_safe(&tStart, &tm);

		daycount = 0;
		dayskip = -1;
		for (int j = 0; j < 7; ++j) {
			if (m_sRecState.ulWeekDays & (1<<((tm.tm_wday + j)%7))) {
				if (dayskip == -1)
					dayskip = j;
				++daycount;
			}
		}
		// dayskip is the number of days to skip from the startdate until the first occurrence
		// daycount is the number of days per week that an occurrence occurs

		weekskip = 0;
		if ((tm.tm_wday < (int)m_sRecState.ulFirstDOW && dayskip > 0) || (tm.tm_wday+dayskip) > 6)
			weekskip = 1;
		// weekskip is the amount of weeks to skip from the startdate before the first occurence

		// The real start is start + dayskip + weekskip-1 (since dayskip will already bring us into the next week)
		tStart = tStart + (dayskip * 24*60*60) + (weekskip * (m_sRecState.ulPeriod-1) * 7 * 24*60*60);
		gmtime_safe(&tStart, &tm);

		break;
	case RF_MONTHLY:
	case RF_YEARLY:
		gmtime_safe(&tStart, &tm);

		if (m_sRecState.ulPatternType == PT_MONTH) {
			unsigned int count = 0;

			// Go the beginning of the month
			tStart -= (tm.tm_mday-1) * 24*60*60;

			// Go the the correct month day
			tStart += (m_sRecState.ulDayOfMonth-1) * 24*60*60;

			// If the previous calculation gave us a start date *before* the original start date, then we need to skip to the next occurrence
			if (m_sRecState.ulRecurFrequency == RF_MONTHLY &&
			    static_cast<int>(m_sRecState.ulDayOfMonth) < tm.tm_mday) {
				// Monthly, go to next occurrence in 'everyn' months
				count = m_sRecState.ulPeriod;
			} else if (m_sRecState.ulRecurFrequency == RF_YEARLY) {

				if (getMonth() - 1 < tm.tm_mon || (getMonth() - 1 == tm.tm_mon && static_cast<int>(m_sRecState.ulDayOfMonth) < tm.tm_mday))
					// Yearly, go to next occurrence in 'everyn' months minus difference in first occurence and original date
					count = (m_sRecState.ulPeriod - (tm.tm_mon - (getMonth()-1)));
				else if (getMonth()-1 > tm.tm_mon)
					count = (getMonth()-1) - tm.tm_mon;
			}

			int curmonth = tm.tm_mon + 1;
			int curyear = tm.tm_year + 1900;
			for (unsigned int i = 0; i < count; ++i) {
				tStart += MonthInSeconds(curyear, curmonth); 
				if (curmonth == 12) {
					curmonth = 0;
					++curyear;
				}
				++curmonth;
			}
			// "start" is now pointing to the first occurrence, except that it will overshoot if the
            // month in which it occurs has less days than specified as the day of the month. So 31st
            // of each month will overshoot in february (29 days). We compensate for that by checking
            // if the day of the month we got is wrong, and then back up to the last day of the previous
            // month.
			if ( m_sRecState.ulDayOfMonth >= 28 &&  m_sRecState.ulDayOfMonth <=31) {
				gmtime_safe(&tStart, &tm);
				if (tm.tm_mday < static_cast<int>(m_sRecState.ulDayOfMonth))
					tStart -= tm.tm_mday * 24 * 60 *60;
			}
		} else if (m_sRecState.ulPatternType == PT_MONTH_NTH) {

			// seek to the begin of the month
			tStart -= (tm.tm_mday-1) * 24*60*60;
			
			// See to the end of the month when every last n Day of the month
			if (m_sRecState.ulWeekNumber == 5) 
				tStart += MonthInSeconds(tm.tm_year + 1900, tm.tm_mon+1) - (24*60*60);
		
			// Find the first valid day (from the original start date)
			int day = -1;
			bool bMoveMonth = false;
			for (int i = 0; i < 7; ++i) {
				if (m_sRecState.ulWeekNumber == 5 && (1<< (tm.tm_wday - i)%7) & m_sRecState.ulWeekDays) {
					day = DaysInMonth(tm.tm_year+1900, tm.tm_mon+1) - i;
					if (day < tm.tm_mday)
						 bMoveMonth = true;
					break;
				} else if (m_sRecState.ulWeekNumber != 5 && (1<< (tm.tm_wday + i)%7) & m_sRecState.ulWeekDays) {
					int maxweekday = m_sRecState.ulWeekNumber * 7;
					day = tm.tm_mday+i;
					if (day > maxweekday)
						bMoveMonth = true;
					break;
				}
			}

			// Move to the right month
			if (m_sRecState.ulRecurFrequency == RF_YEARLY) {
				unsigned int count = 0;

				if (getMonth()-1 < tm.tm_mon || (getMonth()-1 == tm.tm_mon && bMoveMonth) ) {
					count = 12 - tm.tm_mon + (getMonth()-1);
				} else {
					count = (getMonth()-1) - tm.tm_mon;
				}

				int curmonth = tm.tm_mon + 1;
				int curyear = tm.tm_year + 1900;
				for (unsigned int i = 0; i < count; ++i) {
					tStart += MonthInSeconds(curyear, curmonth); 
					if (curmonth == 12) {
						curmonth = 0;
						++curyear;
					}
					++curmonth;
				}

			} else {
				// Check you exist in the right month
				if(bMoveMonth) {
		            int curmonth = tm.tm_mon + 1;
		            int curyear = tm.tm_year + 1900;
					if (m_sRecState.ulWeekNumber == 5) {
						if (curmonth == 12) {
							curmonth = 0;
							++curyear;
						}
						++curmonth;
					}

					for (unsigned int i = 0; i < m_sRecState.ulPeriod; ++i) {
						tStart += MonthInSeconds(curyear, curmonth);
						if (curmonth == 12) {
							curmonth = 0;
							++curyear;
						}
						++curmonth;
					}
				}

			}

			// Seek to the right day (tStart should be the first day or the last day of the month.
			gmtime_safe(&tStart, &tm);
			for (int i = 0; i < 7; ++i) {
				if (m_sRecState.ulWeekNumber == 5 && (1<< (tm.tm_wday - i)%7) & m_sRecState.ulWeekDays) {
					tStart -= i * 24 * 60 *60;
					break;
				} else if (m_sRecState.ulWeekNumber != 5 && (1<< (tm.tm_wday + i)%7) & m_sRecState.ulWeekDays) {
					tStart += (((m_sRecState.ulWeekNumber-1) * 7 + (i+1))- 1) * 24 * 60 *60;
					break;
				}
			}
		}

		break;
	}
	return tStart;
}

time_t recurrence::calcEndDate()
{
	time_t tStart, tEnd;
	struct tm tm;

	if (m_sRecState.ulEndType != ET_NUMBER)
		return getEndDateTime();

	tStart = getStartDateTime();
	tEnd = tStart;

	int fwd;
	int rest;
	int daycount;

	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		if (m_sRecState.ulPatternType == 0) {
			// really daily, not every weekday
			// -1 because the first day already counts (from 1-1-1980 to 1-1-1980 is 1 occurrence)
			tEnd += ((m_sRecState.ulPeriod * 60) * (m_sRecState.ulOccurrenceCount - 1) );
			break;
		}
	case RF_WEEKLY:
		// $forwardcount is the maximum number of week occurrences we can go ahead after the first occurrence that
		// is still inside the recurrence. We subtract one to make sure that the last week is never forwarded over
		// (eg when numoccur = 2, and daycount = 1)
		daycount = calcBits(m_sRecState.ulWeekDays);
		fwd = floor((double)(m_sRecState.ulOccurrenceCount-1) / daycount);
		rest = m_sRecState.ulOccurrenceCount - (fwd*daycount) - 1;
		fwd *= m_sRecState.ulPeriod;

		tEnd += fwd * 7 * 24 * 60 * 60;
		gmtime_safe(&tEnd, &tm);

		for (int j = 1; m_sRecState.ulWeekDays != 0 && rest > 0; ++j) {
			if ((tm.tm_wday + j)%7 == (int)getFirstDOW())
				tEnd += (m_sRecState.ulPeriod-1) * 7 *24*60*60;

			// If this is a matching day, once less occurrence to process
			if(m_sRecState.ulWeekDays & (1<<((tm.tm_wday+j)%7)) )
				--rest;

			// next day
			tEnd += 24*60*60;
		}
		
		break;
	case RF_MONTHLY:
	case RF_YEARLY:
		gmtime_safe(&tStart, &tm);
		tm.tm_year += 1900;		// 1900 based
		++tm.tm_mon; // 1-based

		fwd = (m_sRecState.ulOccurrenceCount-1) * m_sRecState.ulPeriod;

		while (fwd > 0) {
			tEnd += MonthInSeconds(tm.tm_year, tm.tm_mon);
			if (tm.tm_mon%12 == 0) {
				++tm.tm_year;
				tm.tm_mon = 1;
			} else {
				++tm.tm_mon;
			}
			--fwd;
		}

		gmtime_safe(&tEnd, &tm);
		tm.tm_year += 1900;
		++tm.tm_mon;

		if (m_sRecState.ulPatternType == 2) {
			// month, (monthend?)

			// compensation between 28 and 31
			if (m_sRecState.ulDayOfMonth >= 28 && m_sRecState.ulDayOfMonth <= 31 && tm.tm_mday < (int)m_sRecState.ulDayOfMonth) {
				if (tm.tm_mday < 28)
					tEnd -= tm.tm_mday * 24 * 60 * 60;
				else
					tEnd += (DaysInMonth(tm.tm_year, tm.tm_mon) - tm.tm_mday) * 24 * 60 * 60;
			}
		} else if (m_sRecState.ulPatternType == 3) {
			// month Nth
			if (m_sRecState.ulWeekNumber == 5) {
				// last day of month
				tEnd += (DaysInMonth(tm.tm_year, tm.tm_mon) - tm.tm_mday) * 24 * 60 * 60;
			} else {
				tEnd -= (tm.tm_mday-1) * 24 * 60 * 60;
			}

			for (daycount = 0; daycount < 7; ++daycount) {
				gmtime_safe(&tEnd, &tm);
				tm.tm_year += 1900;
				++tm.tm_mon;

				if (m_sRecState.ulWeekNumber == 5 && (1<<(tm.tm_wday - daycount)%7) & m_sRecState.ulWeekDays) {
					tEnd -= tm.tm_mday * 24 * 60 * 60;
				} else if (m_sRecState.ulWeekNumber != 5 && (1<<(tm.tm_wday + daycount)%7) & m_sRecState.ulWeekDays) {
					tEnd += (daycount + ((m_sRecState.ulWeekNumber-1)*7)) * 24 * 60 * 60;
				}
			}
		}

		break;
	}
	
	return tEnd;
}

ULONG recurrence::calcBits(ULONG x)
{
	ULONG n = 0;
	while (x) {
		n += x&1;
		x >>= 1;
	}
	return n;
}

/** 
 * Returns the number of occurrences in this DATE ending recurring
 * item. It doesn't really matter what is returned, since this value
 * is only used for display in a Recurrence Dialog window.
 * 
 * @return number of occurrences
 */
ULONG recurrence::calcCount()
{
	ULONG ulCount = 0;

	if (m_sRecState.ulEndType != ET_DATE)
		return m_sRecState.ulOccurrenceCount;

	if (m_sRecState.ulPeriod == 0)
		return 0;

	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		if (m_sRecState.ulPatternType == 1) {
			// every weekday item
			// ulPeriod is stored in days (so this is always 1, right?)
			ulCount = (getEndDate() - getStartDate()) / (24*60*60) / m_sRecState.ulPeriod;
			// this one isn't one short?
			--ulCount;
		} else {
			// ulPeriod is stored in minutes
			ulCount = (getEndDate() - getStartDate()) / 60 / m_sRecState.ulPeriod;
		}
		break;
	case RF_WEEKLY:
		ulCount = (getEndDate() - getStartDate()) / (7*24*60*60) / m_sRecState.ulPeriod * calcBits(m_sRecState.ulWeekDays);
		break;
	case RF_MONTHLY:
		ulCount = (AllMonthsFromTime(getEndDate()) - AllMonthsFromTime(getStartDate())) / m_sRecState.ulPeriod;
		break;
	case RF_YEARLY:
		// ulPeriod is stored in months
		ulCount = (AllMonthsFromTime(getEndDate()) - AllMonthsFromTime(getStartDate())) / m_sRecState.ulPeriod;
		break;
	}

	// FIXME: some seem 1 short ??
	return ulCount +1;
}

time_t recurrence::MonthInSeconds(ULONG year, ULONG month)
{
	return DaysInMonth(year, month) * 24 * 60 * 60;
}

time_t recurrence::MonthsInSeconds(ULONG months)
{
	ULONG year = 1601;
	ULONG days = 0;
	for (ULONG m = 0; m < months; ++m) {
		days += DaysInMonth((m+1)%12, year);
		if (m%12)
			++year;
	}
	return days * 24 * 60 * 60;
}

time_t recurrence::Minutes2Time(ULONG minutes)
{
	return (minutes - NANOSECS_BETWEEN_EPOCHS/600000000) * 60;
}

ULONG recurrence::Time2Minutes(time_t time)
{
	return (time / 60) + (NANOSECS_BETWEEN_EPOCHS/600000000);
}

ULONG recurrence::Minutes2Month(ULONG minutes)
{
	return (ULONG)ceil((double)minutes / (31.0*24.0*60.0)) + 1;
}

time_t recurrence::StartOfDay(time_t t)
{
	struct tm sTM;
	gmtime_safe(&t, &sTM);

	return t - (sTM.tm_hour * 60 * 60 + sTM.tm_min * 60 + sTM.tm_sec);
}

time_t recurrence::StartOfWeek(time_t t)
{
	struct tm sTM;
	gmtime_safe(&t, &sTM);
	return t - (sTM.tm_wday * 24 * 60 * 60 + sTM.tm_hour * 60 * 60 + sTM.tm_min * 60 + sTM.tm_sec);
}

time_t recurrence::StartOfMonth(time_t t)
{
	struct tm sTM;
	gmtime_safe(&t, &sTM);
	return t - ((sTM.tm_mday -1) * 24 * 60 * 60 + sTM.tm_hour * 60 * 60 + sTM.tm_min * 60 + sTM.tm_sec);
}

time_t recurrence::StartOfYear(time_t t)
{
	struct tm sTM;
	gmtime_safe(&t, &sTM);
	return t - (sTM.tm_yday * 24 * 60 * 60 + sTM.tm_hour * 60 * 60 + sTM.tm_min * 60 + sTM.tm_sec);
}

bool recurrence::isLeapYear(ULONG year)
{
	return ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
}

ULONG recurrence::DaysInYear(ULONG year)
{
	return isLeapYear(year) ? 366 : 365;
}

ULONG recurrence::DaysInMonth(ULONG month)
{
	UCHAR days[13]={0,31,28,31,30,31,30,31,31,30,31,30,31};

	if (month>12)
		return 0;

	return days[month];
}

ULONG recurrence::DaysInMonth(ULONG year, ULONG month)
{
	return DaysInMonth(month) + (month == 2 && isLeapYear(year) ? 1 : 0);
}

ULONG recurrence::MonthFromTime(time_t t)
{
	struct tm lpT;
	gmtime_safe(&t, &lpT);

	return lpT.tm_mon + 1;
}

ULONG recurrence::YearFromTime(time_t t)
{
	struct tm lpT;
	gmtime_safe(&t, &lpT);

	return lpT.tm_year + 1900;
}

ULONG recurrence::AllMonthsFromTime(time_t t)
{
	struct tm lpT;
	gmtime_safe(&t, &lpT);

	return lpT.tm_mon + (lpT.tm_year + 1900 - 1601) * 12;
}

ULONG recurrence::WeekDayFromTime(time_t t)
{
	struct tm lpT;
	gmtime_safe(&t, &lpT);

	return lpT.tm_wday;
}

ULONG recurrence::MonthDayFromTime(time_t t)
{
	struct tm lpT;
	gmtime_safe(&t, &lpT);

	return lpT.tm_mday;
}

bool recurrence::CheckAddValidOccr(time_t tsNow, time_t tsStart, time_t tsEnd, ECLogger *lpLogger, TIMEZONE_STRUCT ttZinfo, ULONG ulBusyStatus, OccrInfo **lppOccrInfoAll, ULONG *lpcValues) {
	time_t tsOccStart = 0;
	time_t tsOccEnd = 0;
        lpLogger->Log(EC_LOGLEVEL_DEBUG, "Testing match: %lu ==> %s", tsNow, ctime(&tsNow));
        if(isOccurrenceValid(UTCToLocal(tsStart, ttZinfo), UTCToLocal(tsEnd, ttZinfo) , tsNow + getStartTimeOffset())) {
                tsOccStart = LocalToUTC(tsNow + getStartTimeOffset(), ttZinfo);
                tsOccEnd = LocalToUTC(tsNow + getEndTimeOffset(), ttZinfo);
                lpLogger->Log(EC_LOGLEVEL_DEBUG, "Adding match: %lu ==> %s", tsOccStart, ctime(&tsOccStart));
                AddValidOccr(tsOccStart, tsOccEnd, ulBusyStatus, lppOccrInfoAll, lpcValues);
                return true;
        } else {
                lpLogger->Log(EC_LOGLEVEL_DEBUG, "Skipping match: %lu ==> %s", tsNow, ctime(&tsNow));
                return false;
        }
}

/**
 * Calculates occurrences of a recurrence between a specified period
 * @param[in]	tsStart			starting time of period
 * @param[in]	tsEnd			ending time of period
 * @param[in]	lpLogger		optional logger
 * @param[in]	ttZinfo			timezone struct of the recurrence
 * @param[in]	ulBusyStatus	freebusy status of the recurrence
 * @param[in]	last	        only return last occurrence (fast)
 * @param[out]	lppOccrInfo		array of occurrences
 * @param[out]	lpcValues		number of occurrences in lppOccrInfo
 *
 * @return		HRESULT
 */
HRESULT recurrence::HrGetItems(time_t tsStart, time_t tsEnd, ECLogger *lpLogger, TIMEZONE_STRUCT ttZinfo, ULONG ulBusyStatus, OccrInfo **lppOccrInfo, ULONG *lpcValues, bool last)
{
	HRESULT hr = 0;
	ECLogger *lpNullLogger = new ECLogger_Null();
	time_t tsNow = 0;
	time_t tsDayNow = 0;
	time_t tsOccStart = 0;
	time_t tsOccEnd = 0;
	time_t tsDayEnd = 0;
	time_t tsDayStart = 0;
	time_t tsRangeEnd = 0;	
    time_t remainder = 0;
	ULONG ulWday = 0;
	OccrInfo *lpOccrInfoAll = *lppOccrInfo;
	
	std::vector<RecurrenceState::Exception> lstExceptions;
	RecurrenceState::Exception lpException;

	if (lpLogger == NULL)
		lpLogger = lpNullLogger;

	tsDayStart = getStartDate();

	if(getEndType() == NEVER || tsEnd < getEndDateTime())
		tsRangeEnd = tsEnd;
	else
		tsRangeEnd = LocalToUTC(getEndDateTime(), ttZinfo);
	
	tsDayEnd = StartOfDay(UTCToLocal(tsRangeEnd, ttZinfo));

	lpLogger->Log(EC_LOGLEVEL_DEBUG,"DURATION START TIME : %lu ==> %s", tsStart, ctime(&tsStart));
	lpLogger->Log(EC_LOGLEVEL_DEBUG,"DURATIION END TIME : %lu ==> %s", tsEnd, ctime(&tsEnd));
	
	lpLogger->Log(EC_LOGLEVEL_DEBUG,"Rec Start TIME : %lu ==> %s", tsDayStart, ctime(&tsDayStart));
	lpLogger->Log(EC_LOGLEVEL_DEBUG,"Rec End TIME : %lu ==> %s", tsDayEnd, ctime(&tsDayEnd));
	
	switch (getFrequency())
	{
	case DAILY:
		if(m_sRecState.ulPeriod <= 0)
			m_sRecState.ulPeriod = 1440;

		if(m_sRecState.ulPatternType == 0)
		{
                        if (last) {
                                remainder = (tsDayEnd-tsDayStart) % (m_sRecState.ulPeriod * 60);
                                for(tsNow = tsDayEnd-remainder; tsNow >= tsDayStart; tsNow -= m_sRecState.ulPeriod * 60) {
                                        if(CheckAddValidOccr(tsNow, tsStart, tsEnd, lpLogger, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues)) {
                                            break;
                                        }
                                }
                        } else {
                                for(tsNow = tsDayStart ; tsNow <= tsDayEnd ; tsNow += (m_sRecState.ulPeriod *60)) { 
                                        CheckAddValidOccr(tsNow, tsStart, tsEnd, lpLogger, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues);
                                }
                        }
		} else {
			// daily, but every weekday (outlook)
                        if (last) {
                                remainder = (tsDayEnd-tsDayStart) % (60 * 1440); // shouldn't this be m_sRecState.ulPeriod * 60? (see above)
                                for(tsNow = tsDayEnd-remainder; tsNow >= tsDayStart; tsNow -= 60 * 1440) { //604800 = 60*60*24*7 
                                        tm sTm;
                                        gmtime_safe(&tsNow, &sTm);

                                        if(sTm.tm_wday > 0 && sTm.tm_wday < 6) {
                                                if(CheckAddValidOccr(tsNow, tsStart, tsEnd, lpLogger, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues)) {
                                                        break;
                                                }
                                        }
                                }
                        } else {
                                for(tsNow = tsDayStart ;tsNow <= tsDayEnd; tsNow += 60 * 1440) { //604800 = 60*60*24*7 
                                        tm sTm;
                                        gmtime_safe(&tsNow, &sTm);
                            
                                        if(sTm.tm_wday > 0 && sTm.tm_wday < 6)
                                                CheckAddValidOccr(tsNow, tsStart, tsEnd, lpLogger, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues);
                                        }
                                }
			}
		break;// CASE : DAILY

	case WEEKLY:
		if(m_sRecState.ulPeriod <= 0)
			m_sRecState.ulPeriod = 1;
			
                if(last) {
                        bool found = false;
                        remainder = (tsDayEnd-tsDayStart) % (m_sRecState.ulPeriod * 604800);
                        for(tsNow = tsDayEnd-remainder; tsNow >= tsDayStart; tsNow -= (m_sRecState.ulPeriod * 604800)) { //604800 = 60*60*24*7 
                               // Loop through the whole following week to the first occurrence of the week, add each day that is specified
                                for (int i = 6; i >= 0; --i) {
                                        ULONG ulWday = 0;
                    
                                        tsDayNow = tsNow + i * 1440 * 60; // 60 * 60 * 24 = 1440
                                        lpLogger->Log(EC_LOGLEVEL_DEBUG,"Checking for weekly tsDayNow : %s", ctime(&tsDayNow));
                                        ulWday = WeekDayFromTime(tsDayNow);
                    
                                        if(m_sRecState.ulWeekDays & (1 << ulWday)) {
                                                if(CheckAddValidOccr(tsDayNow, tsStart, tsEnd, lpLogger, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues)) {
                                                        found=true;
                                                        break;
                                                }
                                        }
                                }
                                if(found) {
                                        break;
                                }
                        }
                } else {
                        for(tsNow = tsDayStart; tsNow <= tsDayEnd; tsNow += (m_sRecState.ulPeriod * 604800)) { //604800 = 60*60*24*7 
                                // Loop through the whole following week to the first occurrence of the week, add each day that is specified
                                for (int i = 0; i < 7; ++i) {
                                        ULONG ulWday = 0;
                            
                                        tsDayNow = tsNow + i * 1440 * 60; // 60 * 60 * 24 = 1440
                                        lpLogger->Log(EC_LOGLEVEL_DEBUG,"Checking for weekly tsDayNow : %s", ctime(&tsDayNow));
                                        ulWday = WeekDayFromTime(tsDayNow);
                            
                                        if(m_sRecState.ulWeekDays & (1 << ulWday)) {
                                                CheckAddValidOccr(tsDayNow, tsStart, tsEnd, lpLogger, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues);
                                        }
                                }
                        }            
		}
		break;// CASE : WEEKLY

	case MONTHLY:
		if(m_sRecState.ulPeriod <= 0)
			m_sRecState.ulPeriod = 1;
		
		tsNow = StartOfMonth(tsDayStart);
		lpLogger->Log(EC_LOGLEVEL_DEBUG, "Monthly Recurrence");

		while(tsNow < tsDayEnd) {
			ULONG ulDiffrence = 0;
			ULONG ulDaysOfMonths = 0;
			ULONG ulDayCounter = 0;
			ULONG ulValidDay = 0;
			
			
			ulDaysOfMonths = countDaysOfMonth(tsNow);
			if(m_sRecState.ulDayOfMonth != 0) {
				
				ulDiffrence = 1;

				if(m_sRecState.ulWeekDays == 0 && m_sRecState.ulDayOfMonth > ulDaysOfMonths)
					ulDiffrence = m_sRecState.ulDayOfMonth - ulDaysOfMonths + 1;
				
				tsDayNow = tsNow + (m_sRecState.ulDayOfMonth - ulDiffrence) *24*60*60;

			} else if( m_sRecState.ulWeekNumber != 0 && m_sRecState.ulWeekDays != 0) {
				if (m_sRecState.ulWeekNumber < 5) {
					ulDayCounter = 0;
					for (ULONG ulDay = 0; ulDay < DaysInMonth(YearFromTime(tsNow), MonthFromTime(tsNow)); ++ulDay) {

						tsDayNow = tsNow + ulDay * 60 * 60 * 24;
						ulWday = WeekDayFromTime(tsDayNow);
						
						if (m_sRecState.ulWeekDays & ( 1<< ulWday))
							++ulDayCounter;

						if (m_sRecState.ulWeekNumber == ulDayCounter) {
							ulValidDay = ulDay;
							break;
						}
					}
					tsDayNow = tsNow + ulValidDay * 60 * 60 * 24;
				} else {
					ulDaysOfMonths = DaysInMonth(YearFromTime(tsNow), MonthFromTime(tsNow));
					tsDayNow = tsNow + (ulDaysOfMonths - 1) * 60 * 60 * 24;

					while((m_sRecState.ulWeekDays & ( 1<< WeekDayFromTime(tsDayNow))) == 0) {
						tsDayNow -= 86400; // deduct 1 day
					}
				}
			}

			if(isOccurrenceValid(tsStart, tsEnd, tsDayNow + getStartTimeOffset())){
				tsOccStart =  LocalToUTC(tsDayNow + getStartTimeOffset(), ttZinfo);
				tsOccEnd = LocalToUTC(tsDayNow + getEndTimeOffset(), ttZinfo);				
				AddValidOccr(tsOccStart, tsOccEnd, ulBusyStatus, &lpOccrInfoAll, lpcValues);
			}
		
			tsNow += DaysTillMonth(tsNow, m_sRecState.ulPeriod) * 60 * 60 * 24;			
		}
		break;// CASE : MONTHLY
	case YEARLY:
		
		if(m_sRecState.ulPeriod <= 0)
			m_sRecState.ulPeriod = 12;
		tsNow = StartOfYear(tsDayStart);
		
		lpLogger->Log(EC_LOGLEVEL_DEBUG, "Recurrence Type Yearly");

		while(tsNow < tsDayEnd) {

			ULONG ulMonthDay = 0;
			time_t tMonthStart = 0;
			time_t tsMonthNow = 0;
			ULONG ulValidDay = 0;

			if(m_sRecState.ulDayOfMonth != 0) {
				
				ulMonthDay = m_sRecState.ulDayOfMonth;
				tMonthStart = tsNow + DaysTillMonth(tsNow, getMonth()-1) * 24 * 60 *60;

				if( ulMonthDay > DaysInMonth(YearFromTime(tMonthStart),MonthFromTime(tMonthStart)))
					ulMonthDay = DaysInMonth(YearFromTime(tMonthStart),MonthFromTime(tMonthStart));

				tsDayNow = tMonthStart + (ulMonthDay -1) * 24 * 60 * 60;				
				
			} else if( m_sRecState.ulWeekNumber != 0 && m_sRecState.ulWeekDays != 0) {
				
				tsMonthNow = tsNow + DaysTillMonth(tsNow, getMonth()-1) * 24 * 60 * 60;
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "Checking yearly nth Weekday Occrrence ulMonthNow: %s",ctime(&tsMonthNow));		
				for (int ulDay = 0; ulDay < 7; ++ulDay) {

					tsDayNow = tsMonthNow + ulDay * 60 * 60 * 24;
					ulWday = WeekDayFromTime(tsDayNow);

					if (m_sRecState.ulWeekDays & ( 1<< ulWday)){
						ulValidDay = ulDay;
						break;
					}
				}

				tsDayNow = tsMonthNow + (ulValidDay + (getWeekNumber() - 1) * 7) * 60 * 60 * 24;
			
				while(StartOfMonth(tsDayNow) != StartOfMonth(tsMonthNow)) {
					tsDayNow -= 7 * 24 * 60 * 60;
				}
			}

			if(isOccurrenceValid(tsStart, tsEnd, tsDayNow + getStartTimeOffset())){
				tsOccStart =  LocalToUTC(tsDayNow + getStartTimeOffset(), ttZinfo);
				tsOccEnd = LocalToUTC(tsDayNow + getEndTimeOffset(), ttZinfo);				
				AddValidOccr(tsOccStart, tsOccEnd, ulBusyStatus, &lpOccrInfoAll, lpcValues);
			}
	
			tsNow += DaysTillMonth(tsNow, m_sRecState.ulPeriod) * 60 * 60 * 24;
		
		}
		break;
	}
	
	lstExceptions = m_sRecState.lstExceptions;
	
	while(lstExceptions.size() != 0)
	{
		OccrInfo sOccrInfo;

		lpException = lstExceptions.back();
		// APPT_STARTWHOLE
		RTimeToUnixTime(lpException.ulStartDateTime, &tsOccStart);	// tsOccStart == localtime
		tsOccStart = LocalToUTC(tsOccStart, ttZinfo);
		if(tsOccStart > tsEnd) {									// tsStart, tsEnd == gmtime
			lpLogger->Log(EC_LOGLEVEL_DEBUG, "Skipping exception start match: %lu ==> %s", tsOccStart, ctime(&tsOccStart));
			goto next;
		}
		UnixTimeToRTime(tsOccStart, &sOccrInfo.fbBlock.m_tmStart);	// gmtime in rtime, is this correct?

		// APPT_ENDWHOLE
		RTimeToUnixTime(lpException.ulEndDateTime, &tsOccEnd);
		tsOccEnd = LocalToUTC(tsOccEnd, ttZinfo);
		if(tsOccEnd < tsStart) {
			lpLogger->Log(EC_LOGLEVEL_DEBUG, "Skipping exception end match: %lu ==> %s", tsOccEnd, ctime(&tsOccEnd));
			goto next;
		}
		UnixTimeToRTime(tsOccEnd, &sOccrInfo.fbBlock.m_tmEnd);

		// APPT_FBSTATUS
		sOccrInfo.fbBlock.m_fbstatus = (FBStatus)lpException.ulBusyStatus;

		// Freebusy status
		RTimeToUnixTime(lpException.ulOriginalStartDate, &sOccrInfo.tBaseDate);

		lpLogger->Log(EC_LOGLEVEL_DEBUG, "Adding exception match: %lu ==> %s", sOccrInfo.tBaseDate, ctime(&sOccrInfo.tBaseDate));

		hr = HrAddFBBlock(sOccrInfo, &lpOccrInfoAll, lpcValues);
next:
		lstExceptions.pop_back();
	}

	*lppOccrInfo = lpOccrInfoAll;

	lpNullLogger->Release();

	return hr;
}

HRESULT recurrence::AddValidOccr(time_t tsOccrStart, time_t tsOccrEnd, ULONG ulBusyStatus, OccrInfo **lpFBBlocksAll, ULONG *lpcValues)
{
	HRESULT hr = hrSuccess;
	OccrInfo sOccrInfo;

	// APPT_STARTWHOLE
	UnixTimeToRTime(tsOccrStart, &sOccrInfo.fbBlock.m_tmStart);
	sOccrInfo.tBaseDate = tsOccrStart;

	// APPT_ENDWHOLE
	UnixTimeToRTime(tsOccrEnd, &sOccrInfo.fbBlock.m_tmEnd);

	sOccrInfo.fbBlock.m_fbstatus = (FBStatus)ulBusyStatus;

	hr = HrAddFBBlock(sOccrInfo, lpFBBlocksAll, lpcValues);
	if (hr != hrSuccess)
		goto exit;

exit:
	return hr;
}

bool recurrence::isOccurrenceValid(time_t tsPeriodStart, time_t tsPeriodEnd, time_t tsNewOcc)
{
	bool IsValid = true;

	if(isException(tsNewOcc)) {
		IsValid = false;
		goto exit;
	}

	if(tsNewOcc < tsPeriodStart || tsNewOcc > tsPeriodEnd) {
		IsValid = false;
		goto exit;
	}

	if(isDeletedOccurrence(tsNewOcc)) {
		IsValid = false;
		goto exit;
	}

exit:
	return IsValid;
}

/**
 * checks if the Occurrence is deleted.
 * @param	tsOccDate	Occurrence unix timestamp
 * @return	bool
 */
bool recurrence::isDeletedOccurrence(time_t tsOccDate)
{
	std::list<time_t> lstDeletedOcc;
	std::list<time_t>::const_iterator iterLstOcc;
	lstDeletedOcc = getDeletedExceptions();
	
	for (iterLstOcc = lstDeletedOcc.begin();
	     iterLstOcc != lstDeletedOcc.end(); ++iterLstOcc)
		if(tsOccDate == *iterLstOcc)
			return true;
	return false;
}

/**
 * checks if the Occurrence is a modified ocurrence
 * @param	tsOccDate	Occurrence unix timestamp
 * @return	bool
 */
bool recurrence::isException(time_t tsOccDate)
{
	std::list<time_t> lstModifiedOcc;
	std::list<time_t>::const_iterator iterLstOcc;
	lstModifiedOcc = getModifiedOccurrences();
	
	for (iterLstOcc = lstModifiedOcc.begin();
	     iterLstOcc != lstModifiedOcc.end(); ++iterLstOcc)
		if(StartOfDay(tsOccDate) == StartOfDay(*iterLstOcc))
			return true;
	return false;
}

ULONG recurrence::countDaysOfMonth(time_t tsDate)
{
	ULONG ulYear = 0;
	ULONG ulMonth = 0;
	ULONG ulDays = 0;
	ULONG ulDaysArray[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	
	ulYear = this->YearFromTime(tsDate);
	ulMonth = this->MonthFromTime(tsDate);
	

	if(this->isLeapYear(ulYear)  && ulMonth == 2 )
		ulDays = 29;
	else
		ulDays = ulDaysArray[ulMonth -1];

	return ulDays;
}
ULONG recurrence::DaysTillMonth(time_t tsDate, ULONG ulMonth)
{
	ULONG ulDays = 0;

	for (ULONG ul = MonthFromTime(tsDate);
	     ul < ulMonth + MonthFromTime(tsDate); ++ul)
		ulDays += DaysInMonth(YearFromTime(tsDate), ul);

	return ulDays;
}
