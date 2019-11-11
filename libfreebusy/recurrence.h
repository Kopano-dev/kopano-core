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

class KC_EXPORT recurrence KC_FINAL {
public:
	HRESULT HrLoadRecurrenceState(const char *data, size_t len, ULONG flags) { return m_sRecState.ParseBlob(data, len, flags); }
	HRESULT HrGetRecurrenceState(char **lppData, size_t *lpulLen, void *base = NULL);
	void HrGetHumanReadableString(std::string *);
	HRESULT HrGetItems(time_t start, time_t end, const TIMEZONE_STRUCT &ttZinfo, ULONG ulBusyStatus, OccrInfo **lppFbBlock, ULONG *lpcValues, bool last = false);
	enum freq_type { DAILY, WEEKLY, MONTHLY, YEARLY };
	enum term_type { DATE, NUMBER, NEVER };

	freq_type getFrequency() const;
	HRESULT setFrequency(freq_type ft);
	KC_HIDDEN time_t getStartDate() const { return RTimeToUnixTime(m_sRecState.ulStartDate); }
	KC_HIDDEN void setStartDate(time_t t) { m_sRecState.ulStartDate = UnixTimeToRTime(StartOfDay(t)); }
	KC_HIDDEN time_t getEndDate() const { return RTimeToUnixTime(m_sRecState.ulEndDate); }
	KC_HIDDEN void setEndDate(time_t t) { m_sRecState.ulEndDate = UnixTimeToRTime(StartOfDay(t)); }
	KC_HIDDEN unsigned int getStartTimeOffset() const { return m_sRecState.ulStartTimeOffset * 60; }
	KC_HIDDEN HRESULT setStartTimeOffset(unsigned int minutes_since_midnight);
	KC_HIDDEN unsigned int getEndTimeOffset() const { return m_sRecState.ulEndTimeOffset * 60; }
	KC_HIDDEN void setEndTimeOffset(unsigned int min) { m_sRecState.ulEndTimeOffset = min; }
	KC_HIDDEN time_t getStartDateTime() const { return RTimeToUnixTime(m_sRecState.ulStartDate) + m_sRecState.ulStartTimeOffset * 60; }

	void setStartDateTime(time_t);
	KC_HIDDEN time_t getEndDateTime() const { return RTimeToUnixTime(m_sRecState.ulEndDate) + m_sRecState.ulEndTimeOffset * 60; }
	KC_HIDDEN void setEndDateTime(time_t);
	KC_HIDDEN unsigned int getCount() const { return m_sRecState.ulOccurrenceCount; }
	KC_HIDDEN void setCount(unsigned int n) { m_sRecState.ulOccurrenceCount = n; }
	term_type getEndType() const;
	HRESULT setEndType(term_type);
	ULONG getInterval() const;
	HRESULT setInterval(ULONG);
	KC_HIDDEN unsigned int getFirstDOW() const { return m_sRecState.ulFirstDOW; }
	KC_HIDDEN void setFirstDOW(unsigned int d) { m_sRecState.ulFirstDOW = d; }

	KC_HIDDEN unsigned char getWeekDays() const { return m_sRecState.ulPatternType == PT_DAY ? 0 : m_sRecState.ulWeekDays; }
	void setWeekDays(unsigned char);
	UCHAR getDayOfMonth() const;
	KC_HIDDEN void setDayOfMonth(unsigned char d) { m_sRecState.ulDayOfMonth = d; }
	UCHAR getMonth() const;
 	HRESULT setMonth(UCHAR);
	UCHAR getWeekNumber() const; /* 1..4 and 5 (last) */
	void setWeekNumber(UCHAR);

	/* exception handling */
	KC_HIDDEN void addDeletedException(time_t t) { m_sRecState.lstDeletedInstanceDates.emplace_back(UnixTimeToRTime(StartOfDay(t))); }
	std::list<time_t> getDeletedExceptions() const;
	KC_HIDDEN unsigned int getModifiedCount() const { return m_sRecState.ulModifiedInstanceCount; }
	ULONG getModifiedFlags(ULONG id) const; /* 0..getModifiedCount() */
	time_t getModifiedStartDateTime(ULONG id) const;
	time_t getModifiedEndDateTime(ULONG id) const;
	KC_HIDDEN time_t getModifiedOriginalDateTime(unsigned int id) const; /* used as recurrence-id */
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
	KC_HIDDEN HRESULT AddValidOccr(time_t occr_start, time_t occr_end, unsigned int busy_status, OccrInfo **fbblocksall, unsigned int *nvals);
	KC_HIDDEN bool isOccurrenceValid(time_t period_start, time_t period_end, time_t new_occ) const;
	KC_HIDDEN bool isDeletedOccurrence(time_t occ_date) const;
	KC_HIDDEN bool isException(time_t occ_date) const;
	KC_HIDDEN unsigned int countDaysOfMonth(time_t date) const;
	KC_HIDDEN unsigned int DaysTillMonth(time_t date, unsigned int month) const;
	KC_HIDDEN std::list<time_t> getModifiedOccurrences() const;
	time_t calcStartDate() const;
	time_t calcEndDate() const;
	ULONG calcCount() const;
	KC_HIDDEN static time_t MonthInSeconds(unsigned int y, unsigned int m) { return DaysInMonth(y, m) * 24 * 60 * 60; }
	KC_HIDDEN static unsigned int Time2Minutes(time_t t) { return (t / 60) + (NANOSECS_BETWEEN_EPOCHS / 600000000); }
	static time_t StartOfDay(time_t);
	KC_HIDDEN static time_t StartOfMonth(time_t);
	KC_HIDDEN static time_t StartOfYear(time_t);
	KC_HIDDEN static bool isLeapYear(unsigned int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
	KC_HIDDEN static unsigned int DaysInMonth(unsigned int);
	KC_HIDDEN static unsigned int DaysInMonth(unsigned int, unsigned int);
	KC_HIDDEN static unsigned int MonthFromTime(time_t);
	KC_HIDDEN static unsigned int YearFromTime(time_t);
	KC_HIDDEN static unsigned int AllMonthsFromTime(time_t);
	KC_HIDDEN static unsigned int WeekDayFromTime(time_t);

private:
	RecurrenceState m_sRecState;
	unsigned int m_ulMonth = 0; // yearly, 1...12 (Not stored in the struct)
	std::vector<std::wstring> vExceptionsSubject;
	std::vector<std::wstring> vExceptionsLocation;

	KC_HIDDEN unsigned int calcBits(unsigned int x) const;
	KC_HIDDEN bool CheckAddValidOccr(time_t now, time_t start, time_t end, const TIMEZONE_STRUCT &, unsigned int busy_status, OccrInfo **occrinfoall, unsigned int *nvals);
};

} /* namespace */

#endif
