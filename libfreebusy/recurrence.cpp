/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <string>
#include <utility>
#include "recurrence.h"
#include <cmath>
#include <kopano/ECGetText.h>
#include <kopano/ECLogger.h>
#include <mapicode.h>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include <ctime>
#include <kopano/CommonUtil.h>
#include <mapiutil.h>
#include <kopano/mapiguidext.h>
#include <kopano/namedprops.h>
#include <iostream>
#include <algorithm>

namespace KC {

/**
 * Write the new recurrence blob.
 *
 * @param[out]	lppData	The blob will be returned in this pointer.
 * @param[out]	lpulLen	Length of returned data in lppData.
 * @param[in]	base	base pointer to MAPIAllocateMore() for lppData allocation. NULL to use MAPIAllocateBuffer().
 */
HRESULT recurrence::HrGetRecurrenceState(char **lppData, size_t *lpulLen, void *base)
{
	struct tm tm;

	// VALIDATION ONLY, not auto-correcting .. you should enter data correctly!
	if (m_sRecState.ulRecurFrequency != RF_DAILY &&
	    m_sRecState.ulRecurFrequency != RF_WEEKLY &&
	    m_sRecState.ulRecurFrequency != RF_MONTHLY &&
	    m_sRecState.ulRecurFrequency != RF_YEARLY)
		return MAPI_E_CORRUPT_DATA;

	if (m_sRecState.ulPatternType != PT_DAY &&
	    m_sRecState.ulPatternType != PT_WEEK &&
	    m_sRecState.ulPatternType != PT_MONTH &&
	    m_sRecState.ulPatternType != PT_MONTH_NTH &&
	    m_sRecState.ulPatternType != PT_MONTH_END &&
	    m_sRecState.ulPatternType != PT_HJ_MONTH &&
	    m_sRecState.ulPatternType != PT_HJ_MONTH_NTH &&
	    m_sRecState.ulPatternType != PT_HJ_MONTH_END)
		return MAPI_E_CORRUPT_DATA;

	if (m_sRecState.ulEndType != ET_DATE &&
	    m_sRecState.ulEndType != ET_NUMBER &&
	    m_sRecState.ulEndType != ET_NEVER)
		return MAPI_E_CORRUPT_DATA;

	// calculate ulFirstDateTime
	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		if (m_sRecState.ulPatternType == PT_WEEK)
			m_sRecState.ulFirstDateTime = 6 * 24 * 60;
		else
			m_sRecState.ulFirstDateTime = m_sRecState.ulStartDate % m_sRecState.ulPeriod;
		break;
	case RF_WEEKLY: {
		auto tStart = getStartDate();
		gmtime_safe(tStart, &tm);
		int daycount = 0, dayskip = -1;
		for (int j = 0; j < 7; ++j) {
			if (m_sRecState.ulWeekDays & (1<<((tm.tm_wday + j)%7))) {
				if (dayskip == -1)
					dayskip = j;
				++daycount;
			}
		}
		// dayskip is the number of days to skip from the startdate until the first occurrence
		// daycount is the number of days per week that an occurrence occurs
		int weekskip = 0;
		if ((tm.tm_wday < static_cast<int>(m_sRecState.ulFirstDOW) && dayskip > 0) || tm.tm_wday + dayskip > 6)
			weekskip = 1;
		// weekskip is the amount of weeks to skip from the startdate before the first occurrence

		// The real start is start + dayskip + weekskip-1 (since dayskip will already bring us into the next week)
		tStart += dayskip * 24 * 60 * 60 + weekskip * (m_sRecState.ulPeriod - 1) * 7 * 24 * 60 * 60;
		gmtime_safe(tStart, &tm);
		m_sRecState.ulFirstDateTime = UnixTimeToRTime(tStart) % (m_sRecState.ulPeriod * 7 * 24 * 60);
		m_sRecState.ulFirstDateTime -= (tm.tm_wday - 1) * 24 * 60; // php says -1, but it's already 0..6 ... err?
		break;
	}
	case RF_MONTHLY:
	case RF_YEARLY: {
		gmtime_safe(getStartDate(), &tm);
		LONG rStart = ((((12 % m_sRecState.ulPeriod) * ((tm.tm_year + 1900 - 1601) % m_sRecState.ulPeriod)) % m_sRecState.ulPeriod) + tm.tm_mon) % m_sRecState.ulPeriod;
		m_sRecState.ulFirstDateTime = 0;
		for (int i = 0; i < rStart; ++i)
			m_sRecState.ulFirstDateTime += MonthInSeconds(1601 + (i/12), (i%12)+1) / 60;
		break;
	}
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
	return m_sRecState.GetBlob(lppData, lpulLen, base);
}

void recurrence::HrGetHumanReadableString(std::string *lpstrHRS)
{
	struct tm tm;
	char bufstart[32], bufend[32], startocc[8], endocc[8];
	auto everyn = m_sRecState.ulPeriod;
	gmtime_safe(getStartDate(), &tm);
	strftime(bufstart, sizeof(bufstart), "%F", &tm);
	gmtime_safe(getEndDate(), &tm);
	strftime(bufend, sizeof(bufend), "%F", &tm);
	snprintf(startocc, sizeof(startocc), "%02u:%02u", getStartTimeOffset() / 3600, getStartTimeOffset() / 60 % 60);
	snprintf(endocc, sizeof(endocc), "%02u:%02u", getEndTimeOffset() / 3600, getEndTimeOffset() / 60 % 60);
	bool single_rank = true;
	auto time_range = getStartTimeOffset() != 0 && getEndTimeOffset() != 0;
	const char *type = nullptr;
	std::string pattern;

	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		if (everyn == 1) {
			type = KC_A("workday");
		} if (everyn == 24 * 60) {
			type = KC_A("day");
		} else {
			everyn /= 24 * 60;
			type = KC_A("days");
			single_rank = false;
		}
		break;
	case RF_WEEKLY:
		if (everyn == 1) {
			type = KC_A("week");
		} else {
			type = KC_A("weeks");
			single_rank = false;
		}
		break;
	case RF_MONTHLY:
		if (everyn == 1) {
			type = KC_A("month");
		} else {
			type = KC_A("months");
			single_rank = false;
		}
		break;
	case RF_YEARLY:
		if (everyn <= 12) {
			everyn = 1;
			type = KC_A("year");
		} else {
			everyn /= 12;
			type = KC_A("years");
			single_rank = false;
		}
		break;
	}

	if (m_sRecState.ulEndType == ET_NEVER) {
		if (time_range)
			pattern = single_rank ?
			          format(KC_A("Occurs every %s from %s to %s, effective %s."),
			                 type, startocc, endocc, bufstart) :
			          format(KC_A("Occurs every %u %s from %s to %s, effective %s."),
			                 everyn, type, startocc, endocc, bufstart);
		else
			pattern = single_rank ?
			          format(KC_A("Occurs every %s, effective %s."), type, bufstart) :
			          format(KC_A("Occurs every %u %s, effective %s."), everyn, type, bufstart);
	} else if (m_sRecState.ulEndType == ET_NUMBER) {
		if (time_range)
			pattern = single_rank ?
				  format(KC_A("Occurs every %s for %u occurence(s) from %s to %s, effective %s."),
				         type, getCount(), startocc, endocc, bufstart) :
				  format(KC_A("Occurs every %u %s for %u occurence(s) from %s to %s, effective %s."),
				         everyn, type, getCount(), startocc, endocc, bufstart);
		else
			pattern = single_rank ?
			          format(KC_A("Occurs every %s for %u occurence(s), effective %s."),
			                 type, getCount(), bufstart) :
			          format(KC_A("Occurs every %u %s for %u occurence(s), effective %s."),
			                 everyn, type, getCount(), bufstart);
	} else if (m_sRecState.ulEndType == ET_DATE) {
		if (time_range)
			pattern = single_rank ?
			          format(KC_A("Occurs every %s from %s to %s, effective %s until %s."),
			                 type, startocc, endocc, bufstart, bufend) :
			          format(KC_A("Occurs every %u %s from %s to %s, effective %s until %s."),
			                 everyn, type, startocc, endocc, bufstart, bufend);
		else
			pattern = single_rank ?
			          format(KC_A("Occurs every %s, effective %s until %s."), type, bufstart, bufend) :
			          format(KC_A("Occurs every %u %s, effective %s until %s."), everyn, type, bufstart, bufend);
	} else {
		pattern = KC_A("Occurs sometimes.");
	}
	*lpstrHRS = std::move(pattern);
}

recurrence::freq_type recurrence::getFrequency() const
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
	switch (ft) {
	case DAILY:
		m_sRecState.ulRecurFrequency = RF_DAILY;
		m_sRecState.ulPatternType = PT_DAY;
		m_sRecState.ulPeriod = 60*24; // stored in minutes
		break;
	case WEEKLY:
		m_sRecState.ulRecurFrequency = RF_WEEKLY;
		m_sRecState.ulPatternType = PT_WEEK;
		m_sRecState.ulPeriod = 1;
		break;
	case MONTHLY:
		m_sRecState.ulRecurFrequency = RF_MONTHLY;
		m_sRecState.ulPatternType = PT_MONTH;
		m_sRecState.ulPeriod = 1;
		break;
	case YEARLY:
		m_sRecState.ulRecurFrequency = RF_YEARLY;
		m_sRecState.ulPatternType = PT_MONTH; // every Nth month
		m_sRecState.ulPeriod = 12;
		break;
	default:
		return E_INVALIDARG;
	}
	return S_OK;
}

HRESULT recurrence::setStartTimeOffset(ULONG ulMinutesSinceMidnight)
{
	if (ulMinutesSinceMidnight >= 24 * 60)
		return E_INVALIDARG;
	m_sRecState.ulStartTimeOffset = ulMinutesSinceMidnight;
	return S_OK;
}

void recurrence::setStartDateTime(time_t t)
{
	time_t startDate = StartOfDay(t);
	m_sRecState.ulStartDate = UnixTimeToRTime(startDate);
	m_sRecState.ulStartTimeOffset = (t - startDate)/60;
}

void recurrence::setEndDateTime(time_t t)
{
	m_sRecState.ulEndDate = UnixTimeToRTime(StartOfDay(t));
	// end time in minutes since midnight of start of item
	m_sRecState.ulEndTimeOffset = (t - getStartDate())/60;
}

recurrence::term_type recurrence::getEndType() const
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

ULONG recurrence::getInterval() const
{
	if (m_sRecState.ulPatternType == PT_DAY)
		// day pattern type, period stored in minutes per day
		return m_sRecState.ulPeriod / (60 * 24);
	else if (getFrequency() == recurrence::YEARLY)
		// yearly stored in months
		return m_sRecState.ulPeriod / 12;
	// either weeks or months, no conversion required
	return m_sRecState.ulPeriod;
}

// Note: Frequency must be set before the interval!
HRESULT recurrence::setInterval(ULONG i)
{
	if (i == 0)
		return E_INVALIDARG;
	m_sRecState.ulPeriod = m_sRecState.ulPeriod * i; // in setFrequency(), ulPeriod is set to "1" for each type
	return S_OK;
}


void recurrence::setWeekDays(UCHAR d)
{
	// if setWeekDays is called on a daily event, update the pattern type
	if (m_sRecState.ulPatternType == PT_DAY) {
		m_sRecState.ulPatternType = PT_WEEK;
		m_sRecState.ulPeriod = m_sRecState.ulPeriod / (24*60); // convert period from daily to "weekly"
	}
	m_sRecState.ulWeekDays = d & WD_MASK;
}

UCHAR recurrence::getDayOfMonth() const
{
	if ((m_sRecState.ulRecurFrequency == RF_YEARLY ||
	    m_sRecState.ulRecurFrequency == RF_MONTHLY) &&
	    (m_sRecState.ulPatternType == PT_MONTH ||
	    m_sRecState.ulPatternType == PT_MONTH_END))
		return m_sRecState.ulDayOfMonth;
	return 0;
}

/**
 * Get the month between 1...12
 * 1 = jan
 */
UCHAR recurrence::getMonth() const
{
	if (m_ulMonth > 0 && m_ulMonth < 13)
		return m_ulMonth;
	struct tm tmMonth;
	gmtime_safe(getStartDate(), &tmMonth);
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
UCHAR recurrence::getWeekNumber() const
{
	if (m_sRecState.ulPatternType != PT_MONTH_NTH &&
	    m_sRecState.ulPatternType != PT_HJ_MONTH_NTH)
		return 0;
	return m_sRecState.ulWeekNumber;
}

void recurrence::setWeekNumber(UCHAR s)
{
	// we should be handling monthly recurrence items here, calendar type 0xB (hijri) is not supported
	m_sRecState.ulPatternType = PT_MONTH_NTH;
	m_sRecState.ulWeekNumber = s;
}

// ------------
// handle exceptions
// ------------
std::list<time_t> recurrence::getDeletedExceptions() const
{
	time_t offset = getStartTimeOffset();
	std::list<time_t> lstDeletes;
	// make copy of struct info
	auto lstDeletedInstanceDates = m_sRecState.lstDeletedInstanceDates;

	for (const auto &exc : m_sRecState.lstExceptions) {
		// if startofday(exception.basedata) == present in lstDeletes, that's a move, so remove from deletes list
		auto d = find(lstDeletedInstanceDates.begin(),
		         lstDeletedInstanceDates.end(),
		         exc.ulOriginalStartDate - (exc.ulOriginalStartDate % 1440));
		if (d != lstDeletedInstanceDates.end())
			lstDeletedInstanceDates.erase(d);
	}
	for (const auto &d : lstDeletedInstanceDates)
		lstDeletes.emplace_back(RTimeToUnixTime(d) + offset);
	return lstDeletes;
}

std::list<time_t> recurrence::getModifiedOccurrences() const
{
	std::list<time_t> lstModified;
	for (const auto &exc : m_sRecState.lstExceptions)
		lstModified.emplace_back(RTimeToUnixTime(exc.ulOriginalStartDate));
	return lstModified;
}

ULONG recurrence::getModifiedFlags(ULONG id) const
{
	return id >= m_sRecState.ulModifiedInstanceCount ? 0 :
	       m_sRecState.lstExceptions[id].ulOverrideFlags;
}

time_t recurrence::getModifiedStartDateTime(ULONG id) const
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	return RTimeToUnixTime(m_sRecState.lstExceptions[id].ulStartDateTime);
}

time_t recurrence::getModifiedEndDateTime(ULONG id) const
{
	if (id >= m_sRecState.ulModifiedInstanceCount)
		return 0;
	return RTimeToUnixTime(m_sRecState.lstExceptions[id].ulEndDateTime);
}

std::wstring recurrence::getModifiedSubject(ULONG id) const
{
	return id >= m_sRecState.ulModifiedInstanceCount ? std::wstring() :
	       m_sRecState.lstExtendedExceptions[id].strWideCharSubject;
}

LONG recurrence::getModifiedReminderDelta(ULONG id) const
{
	return id >= m_sRecState.ulModifiedInstanceCount ? 0 :
	       m_sRecState.lstExceptions[id].ulReminderDelta;
}

ULONG recurrence::getModifiedReminder(ULONG id) const
{
	return id >= m_sRecState.ulModifiedInstanceCount ? 0 :
	       m_sRecState.lstExceptions[id].ulReminderSet;
}

std::wstring recurrence::getModifiedLocation(ULONG id) const
{
	return id >= m_sRecState.ulModifiedInstanceCount ? std::wstring() :
	       m_sRecState.lstExtendedExceptions[id].strWideCharLocation;
}

ULONG recurrence::getModifiedBusyStatus(ULONG id) const
{
	return id >= m_sRecState.ulModifiedInstanceCount ? 0 :
	       m_sRecState.lstExceptions[id].ulBusyStatus;
}

void recurrence::addModifiedException(time_t tStart, time_t tEnd,
    time_t tOriginalStart, ULONG *lpid)
{
	RecurrenceState::Exception sException = {0};
	RecurrenceState::ExtendedException sExtException = {0};

	// this is not thread safe, but since this code is not (yet)
	// called in a thread unsafe manner I could not care less at
	// the moment
	ULONG id = m_sRecState.lstModifiedInstanceDates.size();

	// move is the exception day start
	auto rDayStart = UnixTimeToRTime(StartOfDay(tStart));
	m_sRecState.lstModifiedInstanceDates.emplace_back(rDayStart);

	// every modify is also a delete in the blob
	// delete is the original start
	rDayStart = UnixTimeToRTime(StartOfDay(tOriginalStart));
	m_sRecState.lstDeletedInstanceDates.emplace_back(rDayStart);

	sExtException.ulStartDateTime     = sException.ulStartDateTime     = UnixTimeToRTime(tStart);
	sExtException.ulEndDateTime       = sException.ulEndDateTime       = UnixTimeToRTime(tEnd);
	sExtException.ulOriginalStartDate = sException.ulOriginalStartDate = UnixTimeToRTime(tOriginalStart);
	sExtException.ulChangeHighlightValue = 0;

	m_sRecState.lstExceptions.emplace_back(std::move(sException));
	m_sRecState.lstExtendedExceptions.emplace_back(std::move(sExtException));
	*lpid = id;
}

HRESULT recurrence::setModifiedSubject(ULONG id, const std::wstring &strSubject)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;
	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_SUBJECT;
	m_sRecState.lstExceptions[id].strSubject = convert_to<std::string>(strSubject);
	m_sRecState.lstExtendedExceptions[id].strWideCharSubject = strSubject;
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

HRESULT recurrence::setModifiedLocation(ULONG id,
    const std::wstring &strLocation)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;
	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_LOCATION;
	m_sRecState.lstExceptions[id].strLocation = convert_to<std::string>(strLocation);
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

HRESULT recurrence::setModifiedSubType(ULONG id, ULONG subtype)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;
	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_SUBTYPE;
	m_sRecState.lstExceptions[id].ulSubType = subtype;
	return S_OK;
}

HRESULT recurrence::setModifiedBody(ULONG id)
{
	if (id >= m_sRecState.lstExceptions.size())
		return S_FALSE;
	m_sRecState.lstExceptions[id].ulOverrideFlags |= ARO_EXCEPTIONAL_BODY;
	return S_OK;
}

time_t recurrence::calcStartDate() const
{
	time_t tStart = getStartDateTime();
	struct tm tm;

	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		// Use the default start date.
		break;
	case RF_WEEKLY: {
		gmtime_safe(tStart, &tm);
		int daycount = 0, dayskip = -1;
		for (int j = 0; j < 7; ++j) {
			if (m_sRecState.ulWeekDays & (1<<((tm.tm_wday + j)%7))) {
				if (dayskip == -1)
					dayskip = j;
				++daycount;
			}
		}
		// dayskip is the number of days to skip from the startdate until the first occurrence
		// daycount is the number of days per week that an occurrence occurs
		int weekskip = 0;
		if ((tm.tm_wday < (int)m_sRecState.ulFirstDOW && dayskip > 0) || (tm.tm_wday+dayskip) > 6)
			weekskip = 1;
		// weekskip is the amount of weeks to skip from the startdate before the first occurrence

		// The real start is start + dayskip + weekskip-1 (since dayskip will already bring us into the next week)
		tStart += dayskip * 24 * 60 * 60 + weekskip * (m_sRecState.ulPeriod - 1) * 7 * 24 * 60 * 60;
		gmtime_safe(tStart, &tm);
		break;
	}
	case RF_MONTHLY:
	case RF_YEARLY:
		gmtime_safe(tStart, &tm);

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
					// Yearly, go to next occurrence in 'everyn' months minus difference in first occurrence and original date
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
				gmtime_safe(tStart, &tm);
				if (tm.tm_mday < static_cast<int>(m_sRecState.ulDayOfMonth))
					tStart -= tm.tm_mday * 24 * 60 *60;
			}
			break;
		}
		if (m_sRecState.ulPatternType != PT_MONTH_NTH)
			break;

		// seek to the begin of the month
		tStart -= (tm.tm_mday - 1) * 24 * 60 * 60;
		// See to the end of the month when every last n Day of the month
		if (m_sRecState.ulWeekNumber == 5)
			tStart += MonthInSeconds(tm.tm_year + 1900, tm.tm_mon + 1) - (24 * 60 * 60);

		// Find the first valid day (from the original start date)
		int day = -1;
		bool bMoveMonth = false;
		for (int i = 0; i < 7; ++i) {
			if (m_sRecState.ulWeekNumber == 5 && (1 << (tm.tm_wday - i) % 7) & m_sRecState.ulWeekDays) {
				day = DaysInMonth(tm.tm_year + 1900, tm.tm_mon + 1) - i;
				if (day < tm.tm_mday)
					 bMoveMonth = true;
				break;
			} else if (m_sRecState.ulWeekNumber != 5 && (1 << (tm.tm_wday + i) % 7) & m_sRecState.ulWeekDays) {
				int maxweekday = m_sRecState.ulWeekNumber * 7;
				day = tm.tm_mday + i;
				if (day > maxweekday)
					bMoveMonth = true;
				break;
			}
		}

		// Move to the right month
		if (m_sRecState.ulRecurFrequency == RF_YEARLY) {
			unsigned int count = 0;
			if (getMonth() - 1 < tm.tm_mon || (getMonth() - 1 == tm.tm_mon && bMoveMonth))
				count = 12 - tm.tm_mon + (getMonth() - 1);
			else
				count = (getMonth() - 1) - tm.tm_mon;

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
		} else if (bMoveMonth) {
			// Check you exist in the right month
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

		// Seek to the right day (tStart should be the first day or the last day of the month.
		gmtime_safe(tStart, &tm);
		for (int i = 0; i < 7; ++i) {
			if (m_sRecState.ulWeekNumber == 5 && (1<< (tm.tm_wday - i)%7) & m_sRecState.ulWeekDays) {
				tStart -= i * 24 * 60 *60;
				break;
			} else if (m_sRecState.ulWeekNumber != 5 && (1<< (tm.tm_wday + i)%7) & m_sRecState.ulWeekDays) {
				tStart += (((m_sRecState.ulWeekNumber-1) * 7 + (i+1))- 1) * 24 * 60 *60;
				break;
			}
		}
		break;
	}
	return tStart;
}

time_t recurrence::calcEndDate() const
{
	if (m_sRecState.ulEndType != ET_NUMBER)
		return getEndDateTime();

	auto tStart = getStartDateTime();
	auto tEnd = tStart;
	struct tm tm;

	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		if (m_sRecState.ulPatternType == PT_DAY)
			// really daily, not every weekday
			// -1 because the first day already counts (from 1-1-1980 to 1-1-1980 is 1 occurrence)
			tEnd += ((m_sRecState.ulPeriod * 60) * (m_sRecState.ulOccurrenceCount - 1) );
		break;
	case RF_WEEKLY: {
		// $forwardcount is the maximum number of week occurrences we can go ahead after the first occurrence that
		// is still inside the recurrence. We subtract one to make sure that the last week is never forwarded over
		// (eg when numoccur = 2, and daycount = 1)
		int daycount = calcBits(m_sRecState.ulWeekDays);
		if (daycount == 0) {
			/*
			 * If the recurrence never occurs, the event
			 * will never finish. Just abort it.
			 */
			tEnd = tStart;
			break;
		}
		int fwd = floor((double)(m_sRecState.ulOccurrenceCount - 1) / daycount);
		int rest = m_sRecState.ulOccurrenceCount - fwd * daycount - 1;
		fwd *= m_sRecState.ulPeriod;
		tEnd += fwd * 7 * 24 * 60 * 60;
		gmtime_safe(tEnd, &tm);

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
	}
	case RF_MONTHLY:
	case RF_YEARLY: {
		gmtime_safe(tStart, &tm);
		tm.tm_year += 1900;		// 1900 based
		++tm.tm_mon; // 1-based
		int fwd = (m_sRecState.ulOccurrenceCount - 1) * m_sRecState.ulPeriod;

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

		gmtime_safe(tEnd, &tm);
		tm.tm_year += 1900;
		++tm.tm_mon;

		if (m_sRecState.ulPatternType == PT_MONTH) {
			// month, (monthend?)
			// compensation between 28 and 31
			if (m_sRecState.ulDayOfMonth >= 28 && m_sRecState.ulDayOfMonth <= 31 && tm.tm_mday < (int)m_sRecState.ulDayOfMonth) {
				if (tm.tm_mday < 28)
					tEnd -= tm.tm_mday * 24 * 60 * 60;
				else
					tEnd += (DaysInMonth(tm.tm_year, tm.tm_mon) - tm.tm_mday) * 24 * 60 * 60;
			}
			break;
		}
		if (m_sRecState.ulPatternType != PT_MONTH_NTH)
			break;
		// month Nth
		if (m_sRecState.ulWeekNumber == 5)
			// last day of month
			tEnd += (DaysInMonth(tm.tm_year, tm.tm_mon) - tm.tm_mday) * 24 * 60 * 60;
		else
			tEnd -= (tm.tm_mday-1) * 24 * 60 * 60;

		for (int daycount = 0; daycount < 7; ++daycount) {
			gmtime_safe(tEnd, &tm);
			tm.tm_year += 1900;
			++tm.tm_mon;
			if (m_sRecState.ulWeekNumber == 5 && (1 << (tm.tm_wday - daycount) % 7) & m_sRecState.ulWeekDays)
				tEnd -= tm.tm_mday * 24 * 60 * 60;
			else if (m_sRecState.ulWeekNumber != 5 && (1 << (tm.tm_wday + daycount) % 7) & m_sRecState.ulWeekDays)
				tEnd += (daycount + ((m_sRecState.ulWeekNumber - 1) * 7)) * 24 * 60 * 60;
		}
		break;
	}
	}

	return tEnd;
}

ULONG recurrence::calcBits(ULONG x) const
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
ULONG recurrence::calcCount() const
{
	ULONG ulCount = 0;

	if (m_sRecState.ulEndType != ET_DATE)
		return m_sRecState.ulOccurrenceCount;
	if (m_sRecState.ulPeriod == 0)
		return 0;

	switch (m_sRecState.ulRecurFrequency) {
	case RF_DAILY:
		if (m_sRecState.ulPatternType == PT_WEEK) {
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

time_t recurrence::StartOfDay(time_t t)
{
	struct tm sTM;
	gmtime_safe(t, &sTM);
	return t - (sTM.tm_hour * 60 * 60 + sTM.tm_min * 60 + sTM.tm_sec);
}

time_t recurrence::StartOfMonth(time_t t)
{
	struct tm sTM;
	gmtime_safe(t, &sTM);
	return t - ((sTM.tm_mday -1) * 24 * 60 * 60 + sTM.tm_hour * 60 * 60 + sTM.tm_min * 60 + sTM.tm_sec);
}

time_t recurrence::StartOfYear(time_t t)
{
	struct tm sTM;
	gmtime_safe(t, &sTM);
	return t - (sTM.tm_yday * 24 * 60 * 60 + sTM.tm_hour * 60 * 60 + sTM.tm_min * 60 + sTM.tm_sec);
}

ULONG recurrence::DaysInMonth(ULONG month)
{
	static const uint8_t days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
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
	gmtime_safe(t, &lpT);
	return lpT.tm_mon + 1;
}

ULONG recurrence::YearFromTime(time_t t)
{
	struct tm lpT;
	gmtime_safe(t, &lpT);
	return lpT.tm_year + 1900;
}

ULONG recurrence::AllMonthsFromTime(time_t t)
{
	struct tm lpT;
	gmtime_safe(t, &lpT);
	return lpT.tm_mon + (lpT.tm_year + 1900 - 1601) * 12;
}

ULONG recurrence::WeekDayFromTime(time_t t)
{
	struct tm lpT;
	gmtime_safe(t, &lpT);
	return lpT.tm_wday;
}

bool recurrence::CheckAddValidOccr(time_t tsNow, time_t tsStart, time_t tsEnd,
    const TIMEZONE_STRUCT &ttZinfo, ULONG ulBusyStatus,
    OccrInfo **lppOccrInfoAll, ULONG *lpcValues)
{
	ec_log_debug("Testing match: %lu ==> %s", tsNow, ctime(&tsNow));
	if (!isOccurrenceValid(UTCToLocal(tsStart, ttZinfo), UTCToLocal(tsEnd, ttZinfo), tsNow + getStartTimeOffset())) {
		ec_log_debug("Skipping match: %lu ==> %s", tsNow, ctime(&tsNow));
		return false;
	}
	auto tsOccStart = LocalToUTC(tsNow + getStartTimeOffset(), ttZinfo);
	auto tsOccEnd = LocalToUTC(tsNow + getEndTimeOffset(), ttZinfo);
	ec_log_debug("Adding match: %lu ==> %s", tsOccStart, ctime(&tsOccStart));
	AddValidOccr(tsOccStart, tsOccEnd, ulBusyStatus, lppOccrInfoAll, lpcValues);
	return true;
}

/**
 * Calculates occurrences of a recurrence between a specified period
 * @param[in]	tsStart			starting time of period
 * @param[in]	tsEnd			ending time of period
 * @param[in]	ttZinfo			timezone struct of the recurrence
 * @param[in]	ulBusyStatus	freebusy status of the recurrence
 * @param[in]	last	        only return last occurrence (fast)
 * @param[out]	lppOccrInfo		array of occurrences
 * @param[out]	lpcValues		number of occurrences in lppOccrInfo
 * @return		HRESULT
 */
HRESULT recurrence::HrGetItems(time_t tsStart, time_t tsEnd,
    const TIMEZONE_STRUCT &ttZinfo, ULONG ulBusyStatus, OccrInfo **lppOccrInfo,
    ULONG *lpcValues, bool last)
{
	HRESULT hr = 0;
	OccrInfo *lpOccrInfoAll = *lppOccrInfo;
	std::vector<RecurrenceState::Exception> lstExceptions;
	RecurrenceState::Exception lpException;
	auto tsDayStart = getStartDate();
	auto tsRangeEnd = (getEndType() == NEVER || tsEnd < getEndDateTime()) ?
	                  tsEnd : LocalToUTC(getEndDateTime(), ttZinfo);
	auto tsDayEnd = StartOfDay(UTCToLocal(tsRangeEnd, ttZinfo));

	ec_log_debug("DURATION START TIME: %lu ==> %s", tsStart, ctime(&tsStart));
	ec_log_debug("DURATIION END TIME: %lu ==> %s", tsEnd, ctime(&tsEnd));
	ec_log_debug("Rec Start TIME: %lu ==> %s", tsDayStart, ctime(&tsDayStart));
	ec_log_debug("Rec End TIME: %lu ==> %s", tsDayEnd, ctime(&tsDayEnd));
	switch (getFrequency())
	{
	case DAILY:
		if(m_sRecState.ulPeriod <= 0)
			m_sRecState.ulPeriod = 1440;

		if (m_sRecState.ulPatternType == PT_DAY) {
                        if (last) {
				time_t remainder = (tsDayEnd - tsDayStart) % (m_sRecState.ulPeriod * 60);
				for (time_t tsNow = tsDayEnd - remainder; tsNow >= tsDayStart; tsNow -= m_sRecState.ulPeriod * 60)
					if (CheckAddValidOccr(tsNow, tsStart, tsEnd, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues))
						break;
                        } else {
				for (time_t tsNow = tsDayStart; tsNow <= tsDayEnd; tsNow += m_sRecState.ulPeriod * 60)
					CheckAddValidOccr(tsNow, tsStart, tsEnd, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues);
                        }
                        break;
		}
		// daily, but every weekday (outlook)
		else if (last) {
			time_t remainder = (tsDayEnd - tsDayStart) % (60 * 1440); // shouldn't this be m_sRecState.ulPeriod * 60? (see above)
			for (time_t tsNow = tsDayEnd - remainder; tsNow >= tsDayStart; tsNow -= 60 * 1440) { //604800 = 60*60*24*7
				tm sTm;
				gmtime_safe(tsNow, &sTm);
				if (sTm.tm_wday > 0 && sTm.tm_wday < 6 &&
				    CheckAddValidOccr(tsNow, tsStart, tsEnd, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues))
					break;
			}
			break;
		}
		for (time_t tsNow = tsDayStart ;tsNow <= tsDayEnd; tsNow += 60 * 1440) { //604800 = 60*60*24*7
			tm sTm;
			gmtime_safe(tsNow, &sTm);
			if (sTm.tm_wday > 0 && sTm.tm_wday < 6)
				CheckAddValidOccr(tsNow, tsStart, tsEnd, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues);
		}
		break;// CASE : DAILY

	case WEEKLY:
		if(m_sRecState.ulPeriod <= 0)
			m_sRecState.ulPeriod = 1;

                if(last) {
                        bool found = false;
			time_t remainder = (tsDayEnd - tsDayStart) % (m_sRecState.ulPeriod * 604800);
			for (time_t tsNow = tsDayEnd - remainder; tsNow >= tsDayStart; tsNow -= m_sRecState.ulPeriod * 604800) { //604800 = 60*60*24*7
                               // Loop through the whole following week to the first occurrence of the week, add each day that is specified
                                for (int i = 6; i >= 0; --i) {
					auto tsDayNow = tsNow + i * 1440 * 60; // 60 * 60 * 24 = 1440
					ec_log_debug("Checking for weekly tsDayNow: %s", ctime(&tsDayNow));
					if (m_sRecState.ulWeekDays & (1 << WeekDayFromTime(tsDayNow)) &&
					    CheckAddValidOccr(tsDayNow, tsStart, tsEnd, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues)) {
						found = true;
						break;
					}
                                }
				if (found)
					break;
                        }
                        break;
                }
		for (time_t tsNow = tsDayStart; tsNow <= tsDayEnd; tsNow += m_sRecState.ulPeriod * 604800) { //604800 = 60*60*24*7
			// Loop through the whole following week to the first occurrence of the week, add each day that is specified
			for (int i = 0; i < 7; ++i) {
				auto tsDayNow = tsNow + i * 1440 * 60; // 60 * 60 * 24 = 1440
				ec_log_debug("Checking for weekly tsDayNow: %s", ctime(&tsDayNow));
				if (m_sRecState.ulWeekDays & (1 << WeekDayFromTime(tsDayNow)))
					CheckAddValidOccr(tsDayNow, tsStart, tsEnd, ttZinfo, ulBusyStatus, &lpOccrInfoAll, lpcValues);
			}
		}
		break;// CASE : WEEKLY
	case MONTHLY: {
		if(m_sRecState.ulPeriod <= 0)
			m_sRecState.ulPeriod = 1;
		auto tsNow = StartOfMonth(tsDayStart);
		ec_log_debug("Monthly Recurrence");
		while(tsNow < tsDayEnd) {
			ULONG ulDiffrence = 0, ulDaysOfMonths = countDaysOfMonth(tsNow), ulDayCounter = 0, ulValidDay = 0;
			time_t tsDayNow = 0;

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
						if (m_sRecState.ulWeekDays & (1 << WeekDayFromTime(tsDayNow)))
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
					while ((m_sRecState.ulWeekDays & ( 1<< WeekDayFromTime(tsDayNow))) == 0)
						tsDayNow -= 86400; // deduct 1 day
				}
			}

			if(isOccurrenceValid(tsStart, tsEnd, tsDayNow + getStartTimeOffset())){
				auto tsOccStart =  LocalToUTC(tsDayNow + getStartTimeOffset(), ttZinfo);
				auto tsOccEnd = LocalToUTC(tsDayNow + getEndTimeOffset(), ttZinfo);
				AddValidOccr(tsOccStart, tsOccEnd, ulBusyStatus, &lpOccrInfoAll, lpcValues);
			}

			tsNow += DaysTillMonth(tsNow, m_sRecState.ulPeriod) * 60 * 60 * 24;
		}
		break;// CASE : MONTHLY
	}
	case YEARLY: {
		if(m_sRecState.ulPeriod <= 0)
			m_sRecState.ulPeriod = 12;
		auto tsNow = StartOfYear(tsDayStart);
		ec_log_debug("Recurrence Type Yearly");
		while(tsNow < tsDayEnd) {
			ULONG ulValidDay = 0;
			time_t tsDayNow = 0, tMonthStart = 0, tsMonthNow = 0;

			if(m_sRecState.ulDayOfMonth != 0) {
				ULONG ulMonthDay = m_sRecState.ulDayOfMonth;
				tMonthStart = tsNow + DaysTillMonth(tsNow, getMonth()-1) * 24 * 60 *60;

				if( ulMonthDay > DaysInMonth(YearFromTime(tMonthStart),MonthFromTime(tMonthStart)))
					ulMonthDay = DaysInMonth(YearFromTime(tMonthStart),MonthFromTime(tMonthStart));
				tsDayNow = tMonthStart + (ulMonthDay -1) * 24 * 60 * 60;
			} else if( m_sRecState.ulWeekNumber != 0 && m_sRecState.ulWeekDays != 0) {
				tsMonthNow = tsNow + DaysTillMonth(tsNow, getMonth()-1) * 24 * 60 * 60;
				ec_log_debug("Checking yearly nth Weekday Occrrence ulMonthNow: %s", ctime(&tsMonthNow));
				for (int ulDay = 0; ulDay < 7; ++ulDay) {
					tsDayNow = tsMonthNow + ulDay * 60 * 60 * 24;
					if (m_sRecState.ulWeekDays & (1 << WeekDayFromTime(tsDayNow))) {
						ulValidDay = ulDay;
						break;
					}
				}

				tsDayNow = tsMonthNow + (ulValidDay + (getWeekNumber() - 1) * 7) * 60 * 60 * 24;
				while (StartOfMonth(tsDayNow) != StartOfMonth(tsMonthNow))
					tsDayNow -= 7 * 24 * 60 * 60;
			}

			if(isOccurrenceValid(tsStart, tsEnd, tsDayNow + getStartTimeOffset())){
				auto tsOccStart = LocalToUTC(tsDayNow + getStartTimeOffset(), ttZinfo);
				auto tsOccEnd = LocalToUTC(tsDayNow + getEndTimeOffset(), ttZinfo);
				AddValidOccr(tsOccStart, tsOccEnd, ulBusyStatus, &lpOccrInfoAll, lpcValues);
			}

			tsNow += DaysTillMonth(tsNow, m_sRecState.ulPeriod) * 60 * 60 * 24;
		}
		break;
	}
	}

	for (lstExceptions = m_sRecState.lstExceptions; lstExceptions.size() != 0; lstExceptions.pop_back()) {
		OccrInfo sOccrInfo;

		lpException = lstExceptions.back();
		// APPT_STARTWHOLE
		auto tsOccStart = RTimeToUnixTime(lpException.ulStartDateTime); /* tsOccStart is localtime */
		tsOccStart = LocalToUTC(tsOccStart, ttZinfo);
		if(tsOccStart > tsEnd) {									// tsStart, tsEnd == gmtime
			ec_log_debug("Skipping exception start match: %lu ==> %s", tsOccStart, ctime(&tsOccStart));
			continue;
		}
		sOccrInfo.fbBlock.m_tmStart = UnixTimeToRTime(tsOccStart);

		// APPT_ENDWHOLE
		auto tsOccEnd = RTimeToUnixTime(lpException.ulEndDateTime);
		tsOccEnd = LocalToUTC(tsOccEnd, ttZinfo);
		if(tsOccEnd < tsStart) {
			ec_log_debug("Skipping exception end match: %lu ==> %s", tsOccEnd, ctime(&tsOccEnd));
			continue;
		}
		sOccrInfo.fbBlock.m_tmEnd = UnixTimeToRTime(tsOccEnd);

		// APPT_FBSTATUS
		sOccrInfo.fbBlock.m_fbstatus = (FBStatus)lpException.ulBusyStatus;

		// Freebusy status
		sOccrInfo.tBaseDate = RTimeToUnixTime(lpException.ulOriginalStartDate);
		ec_log_debug("Adding exception match: %lu ==> %s", sOccrInfo.tBaseDate, ctime(&sOccrInfo.tBaseDate));
		hr = HrAddFBBlock(sOccrInfo, &lpOccrInfoAll, lpcValues);
	}

	*lppOccrInfo = lpOccrInfoAll;
	return hr;
}

HRESULT recurrence::AddValidOccr(time_t tsOccrStart, time_t tsOccrEnd, ULONG ulBusyStatus, OccrInfo **lpFBBlocksAll, ULONG *lpcValues)
{
	OccrInfo sOccrInfo;

	// APPT_STARTWHOLE
	sOccrInfo.fbBlock.m_tmStart = UnixTimeToRTime(tsOccrStart);
	sOccrInfo.tBaseDate = tsOccrStart;

	// APPT_ENDWHOLE
	sOccrInfo.fbBlock.m_tmEnd = UnixTimeToRTime(tsOccrEnd);
	sOccrInfo.fbBlock.m_fbstatus = (FBStatus)ulBusyStatus;
	return HrAddFBBlock(sOccrInfo, lpFBBlocksAll, lpcValues);
}

bool recurrence::isOccurrenceValid(time_t tsPeriodStart, time_t tsPeriodEnd,
    time_t tsNewOcc) const
{
	if (isException(tsNewOcc))
		return false;
	if (tsNewOcc < tsPeriodStart || tsNewOcc > tsPeriodEnd)
		return false;
	if (isDeletedOccurrence(tsNewOcc))
		return false;
	return true;
}

/**
 * checks if the Occurrence is deleted.
 * @param	tsOccDate	Occurrence Unix timestamp
 * @return	bool
 */
bool recurrence::isDeletedOccurrence(time_t tsOccDate) const
{
	for (const auto oc : getDeletedExceptions())
		if (tsOccDate == oc)
			return true;
	return false;
}

/**
 * checks if the Occurrence is a modified occurrence
 * @param	tsOccDate	Occurrence Unix timestamp
 * @return	bool
 */
bool recurrence::isException(time_t tsOccDate) const
{
	for (const auto oc : getModifiedOccurrences())
		if (StartOfDay(tsOccDate) == StartOfDay(oc))
			return true;
	return false;
}

ULONG recurrence::countDaysOfMonth(time_t tsDate) const
{
	static const ULONG ulDaysArray[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	auto ulYear = YearFromTime(tsDate);
	auto ulMonth = MonthFromTime(tsDate);
	if (isLeapYear(ulYear) && ulMonth == 2)
		return 29;
	return ulDaysArray[ulMonth -1];
}

ULONG recurrence::DaysTillMonth(time_t tsDate, ULONG ulMonth) const
{
	ULONG ulDays = 0;

	for (ULONG ul = MonthFromTime(tsDate);
	     ul < ulMonth + MonthFromTime(tsDate); ++ul)
		ulDays += DaysInMonth(YearFromTime(tsDate), ul);
	return ulDays;
}

} /* namespace */
