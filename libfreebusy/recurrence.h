/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef RECURRENCE_H
#define RECURRENCE_H

#include <kopano/zcdefs.h>
#include <kopano/RecurrenceState.h>
#include <mapidefs.h>
#include <mapix.h>
#include <kopano/Util.h>
#include <list>
#include <kopano/timeutil.hpp>
#include "freebusy.h"
#include "freebusyutil.h"

namespace KC {

class _kc_export recurrence KC_FINAL {
public:
	HRESULT HrLoadRecurrenceState(const char *data, size_t len, ULONG flags) { return m_sRecState.ParseBlob(data, len, flags); }
	HRESULT HrGetRecurrenceState(char **lppData, size_t *lpulLen, void *base = NULL);
	void HrGetHumanReadableString(std::string *);
	HRESULT HrGetItems(time_t start, time_t end, const TIMEZONE_STRUCT &ttZinfo, ULONG ulBusyStatus, OccrInfo **lppFbBlock, ULONG *lpcValues, bool last = false);
	enum freq_type { DAILY, WEEKLY, MONTHLY, YEARLY };
	enum term_type { DATE, NUMBER, NEVER };

	freq_type getFrequency() const;
	HRESULT setFrequency(freq_type ft);
	_kc_hidden time_t getStartDate() const { return RTimeToUnixTime(m_sRecState.ulStartDate); }
	_kc_hidden void setStartDate(time_t t) { m_sRecState.ulStartDate = UnixTimeToRTime(StartOfDay(t)); }
	_kc_hidden time_t getEndDate() const { return RTimeToUnixTime(m_sRecState.ulEndDate); }
	_kc_hidden void setEndDate(time_t t) { m_sRecState.ulEndDate = UnixTimeToRTime(StartOfDay(t)); }
	_kc_hidden ULONG getStartTimeOffset() const { return m_sRecState.ulStartTimeOffset * 60; }
	_kc_hidden HRESULT setStartTimeOffset(ULONG minutes_since_midnight);
	_kc_hidden ULONG getEndTimeOffset(void) const { return m_sRecState.ulEndTimeOffset * 60; }
	_kc_hidden void setEndTimeOffset(ULONG min) { m_sRecState.ulEndTimeOffset = min; }
	_kc_hidden time_t getStartDateTime() const { return RTimeToUnixTime(m_sRecState.ulStartDate) + m_sRecState.ulStartTimeOffset * 60; }

	void setStartDateTime(time_t);
	_kc_hidden time_t getEndDateTime() const { return RTimeToUnixTime(m_sRecState.ulEndDate) + m_sRecState.ulEndTimeOffset * 60; }
	_kc_hidden void setEndDateTime(time_t);
	_kc_hidden ULONG getCount() const { return m_sRecState.ulOccurrenceCount; }
	_kc_hidden void setCount(ULONG n) { m_sRecState.ulOccurrenceCount = n; }
	term_type getEndType() const;
	HRESULT setEndType(term_type);
	ULONG getInterval() const;
	HRESULT setInterval(ULONG);
	_kc_hidden ULONG getFirstDOW() const { return m_sRecState.ulFirstDOW; }
	_kc_hidden void setFirstDOW(ULONG d) { m_sRecState.ulFirstDOW = d; }

	_kc_hidden UCHAR getWeekDays() const { return m_sRecState.ulPatternType == PT_DAY ? 0 : m_sRecState.ulWeekDays; }
	void setWeekDays(UCHAR);
	UCHAR getDayOfMonth() const;
	_kc_hidden void setDayOfMonth(UCHAR d) { m_sRecState.ulDayOfMonth = d; }
	UCHAR getMonth() const;
 	HRESULT setMonth(UCHAR);
	UCHAR getWeekNumber() const; /* 1..4 and 5 (last) */
	void setWeekNumber(UCHAR);

	/* exception handling */
	_kc_hidden void addDeletedException(time_t t) { m_sRecState.lstDeletedInstanceDates.emplace_back(UnixTimeToRTime(StartOfDay(t))); }
	std::list<time_t> getDeletedExceptions() const;
	_kc_hidden ULONG getModifiedCount() const { return m_sRecState.ulModifiedInstanceCount; }
	ULONG getModifiedFlags(ULONG id) const; /* 0..getModifiedCount() */
	time_t getModifiedStartDateTime(ULONG id) const;
	time_t getModifiedEndDateTime(ULONG id) const;
	_kc_hidden time_t getModifiedOriginalDateTime(ULONG id) const; /* used as recurrence-id */
	std::wstring getModifiedSubject(ULONG id) const;
	LONG getModifiedReminderDelta(ULONG id) const;
	ULONG getModifiedReminder(ULONG id) const;
	std::wstring getModifiedLocation(ULONG id) const;
	ULONG getModifiedBusyStatus(ULONG id) const;

	void addModifiedException(time_t tStart, time_t tEnd, time_t tOriginalStart, ULONG *id);
	HRESULT setModifiedSubject(ULONG id, const std::wstring &strSubject);
	HRESULT setModifiedReminderDelta(ULONG id, LONG delta);
	HRESULT setModifiedReminder(ULONG id, ULONG set);
	HRESULT setModifiedLocation(ULONG id, const std::wstring &strLocation);
	HRESULT setModifiedBusyStatus(ULONG id, ULONG status);
	HRESULT setModifiedSubType(ULONG id, ULONG subtype);
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
	_kc_hidden static time_t MonthInSeconds(ULONG y, ULONG m) { return DaysInMonth(y, m) * 24 * 60 * 60; }
	_kc_hidden static ULONG Time2Minutes(time_t t) { return (t / 60) + (NANOSECS_BETWEEN_EPOCHS / 600000000); }
	static time_t StartOfDay(time_t);
	_kc_hidden static time_t StartOfMonth(time_t);
	_kc_hidden static time_t StartOfYear(time_t);
	_kc_hidden static bool isLeapYear(ULONG y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
	_kc_hidden static ULONG DaysInMonth(ULONG);
	_kc_hidden static ULONG DaysInMonth(ULONG, ULONG);
	_kc_hidden static ULONG MonthFromTime(time_t);
	_kc_hidden static ULONG YearFromTime(time_t);
	_kc_hidden static ULONG AllMonthsFromTime(time_t);
	_kc_hidden static ULONG WeekDayFromTime(time_t);

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
