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

#ifndef RECURRENCE_H
#define RECURRENCE_H

#include <kopano/zcdefs.h>
#include <kopano/RecurrenceState.h>
#include <mapidefs.h>
#include <mapix.h>
#include <kopano/Util.h>
#include <list>
#include "TimeUtil.h"
#include "freebusy.h"
#include "freebusyutil.h"

namespace KC {

class _kc_export recurrence _kc_final {
public:
	HRESULT HrLoadRecurrenceState(const char *lpData, unsigned int ulLen, ULONG ulFlags);
	HRESULT HrGetRecurrenceState(char **lppData, unsigned int *lpulLen, void *base = NULL);
	void HrGetHumanReadableString(std::string *);
	HRESULT HrGetItems(time_t start, time_t end, const TIMEZONE_STRUCT &ttZinfo, ULONG ulBusyStatus, OccrInfo **lppFbBlock, ULONG *lpcValues, bool last = false);
	enum freq_type { DAILY, WEEKLY, MONTHLY, YEARLY };
	enum term_type { DATE, NUMBER, NEVER };

	freq_type getFrequency() const;
	HRESULT setFrequency(freq_type ft);
	_kc_hidden time_t getStartDate() const;
	_kc_hidden void setStartDate(time_t);
	time_t getEndDate() const;
	void setEndDate(time_t tEnd);
	ULONG getStartTimeOffset() const;
	_kc_hidden HRESULT setStartTimeOffset(ULONG minutes_since_midnight);
	_kc_hidden ULONG getEndTimeOffset(void) const;
	void setEndTimeOffset(ULONG min);
	time_t getStartDateTime() const;
	void setStartDateTime(time_t tStart);
	_kc_hidden time_t getEndDateTime() const;
	_kc_hidden void setEndDateTime(time_t);
	ULONG getCount() const;
	void setCount(ULONG ulCount);
	term_type getEndType() const;
	HRESULT setEndType(term_type);
	ULONG getInterval() const;
	HRESULT setInterval(ULONG);
	_kc_hidden ULONG getSlidingFlag() const { return m_sRecState.ulSlidingFlag; }
	_kc_hidden void setSlidingFlag(ULONG);
	_kc_hidden ULONG getFirstDOW() const;
	void setFirstDOW(ULONG);
	UCHAR getWeekDays() const;
	void setWeekDays(UCHAR);
	UCHAR getDayOfMonth() const;
	void setDayOfMonth(UCHAR);
	UCHAR getMonth() const;
 	HRESULT setMonth(UCHAR);
	UCHAR getWeekNumber() const; /* 1..4 and 5 (last) */
	void setWeekNumber(UCHAR);

	/* exception handling */
	void addDeletedException(time_t);
	std::list<time_t> getDeletedExceptions() const;
	ULONG getModifiedCount() const;
	ULONG getModifiedFlags(ULONG id) const; /* 0..getModifiedCount() */
	time_t getModifiedStartDateTime(ULONG id) const;
	time_t getModifiedEndDateTime(ULONG id) const;
	_kc_hidden time_t getModifiedOriginalDateTime(ULONG id) const; /* used as recurrence-id */
	std::wstring getModifiedSubject(ULONG id) const;
	_kc_hidden ULONG getModifiedMeetingType(ULONG id) const;
	LONG getModifiedReminderDelta(ULONG id) const;
	ULONG getModifiedReminder(ULONG id) const;
	std::wstring getModifiedLocation(ULONG id) const;
	ULONG getModifiedBusyStatus(ULONG id) const;
	_kc_hidden ULONG getModifiedAttachment(ULONG id) const;
	ULONG getModifiedSubType(ULONG id) const;

	void addModifiedException(time_t tStart, time_t tEnd, time_t tOriginalStart, ULONG *id);
	HRESULT setModifiedSubject(ULONG id, const std::wstring &strSubject);
	_kc_hidden HRESULT setModifiedMeetingType(ULONG id, ULONG type);
	HRESULT setModifiedReminderDelta(ULONG id, LONG delta);
	HRESULT setModifiedReminder(ULONG id, ULONG set);
	HRESULT setModifiedLocation(ULONG id, const std::wstring &strLocation);
	HRESULT setModifiedBusyStatus(ULONG id, ULONG status);
	_kc_hidden HRESULT setModifiedAttachment(ULONG id);
	HRESULT setModifiedSubType(ULONG id, ULONG subtype);
	_kc_hidden HRESULT setModifiedApptColor(ULONG id, ULONG color);
	HRESULT setModifiedBody(ULONG id);
	_kc_hidden HRESULT AddValidOccr(time_t occr_start, time_t occr_end, ULONG busy_status, OccrInfo **fbblocksall, ULONG *nvals);
	_kc_hidden bool isOccurrenceValid(time_t period_start, time_t period_end, time_t new_occ) const;
	_kc_hidden bool isDeletedOccurrence(time_t occ_date) const;
	_kc_hidden bool isException(time_t occ_date) const;
	_kc_hidden ULONG countDaysOfMonth(time_t date) const;
	_kc_hidden ULONG DaysTillMonth(time_t date, ULONG month) const;
	_kc_hidden std::list<time_t> getModifiedOccurrences() const;
	time_t calcStartDate() const;
	time_t calcEndDate() const;
	ULONG calcCount() const;
	_kc_hidden static time_t MonthInSeconds(ULONG year, ULONG month);
	_kc_hidden static time_t MonthsInSeconds(ULONG months);
	_kc_hidden static time_t Minutes2Time(ULONG);
	_kc_hidden static ULONG Time2Minutes(time_t);
	_kc_hidden static ULONG Minutes2Month(ULONG);
	static time_t StartOfDay(time_t);
	_kc_hidden static time_t StartOfWeek(time_t);
	_kc_hidden static time_t StartOfMonth(time_t);
	_kc_hidden static time_t StartOfYear(time_t);
	_kc_hidden static bool isLeapYear(ULONG year);
	_kc_hidden static ULONG DaysInMonth(ULONG);
	_kc_hidden static ULONG DaysInMonth(ULONG, ULONG);
	_kc_hidden static ULONG DaysInYear(ULONG);
	_kc_hidden static ULONG MonthFromTime(time_t);
	_kc_hidden static ULONG YearFromTime(time_t);
	_kc_hidden static ULONG AllMonthsFromTime(time_t);
	_kc_hidden static ULONG WeekDayFromTime(time_t);
	_kc_hidden static ULONG MonthDayFromTime(time_t);

private:
	RecurrenceState m_sRecState;
	unsigned int m_ulMonth = 0; // yearly, 1...12 (Not stored in the struct)
	std::vector<std::wstring> vExceptionsSubject;
	std::vector<std::wstring> vExceptionsLocation;

	_kc_hidden ULONG calcBits(ULONG x) const;
	_kc_hidden bool CheckAddValidOccr(time_t now, time_t start, time_t end, const TIMEZONE_STRUCT &, ULONG busy_status, OccrInfo **occrinfoall, ULONG *nvals);
};

} /* namespace */

#endif
