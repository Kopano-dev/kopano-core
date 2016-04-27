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

/* Copy of the full common/RecurrenceState.h file, but without the
   nested classes in the main class. Since swig can't handle those.
 */
#include <vector>
#include <string>

#define RECURRENCE_STATE_CALENDAR	0x01
#define RECURRENCE_STATE_TASKS		0x02

/* do not inline for swig */
class Exception {
public:
    unsigned int ulStartDateTime;
    unsigned int ulEndDateTime;
    unsigned int ulOriginalStartDate;
    unsigned int ulOverrideFlags;
    std::string strSubject;
    unsigned int ulMeetingType;
    unsigned int ulReminderDelta;
    unsigned int ulReminderSet;
    std::string strLocation;
    unsigned int ulBusyStatus;
    unsigned int ulAttachment;
    unsigned int ulSubType;
    unsigned int ulAppointmentColor;
};

class ExtendedException {
public:
    unsigned int ulChangeHighlightValue;
    std::string strReserved;
    std::string strReservedBlock1;
    unsigned int ulStartDateTime;
    unsigned int ulEndDateTime;
    unsigned int ulOriginalStartDate;
    std::wstring strWideCharSubject;
    std::wstring strWideCharLocation;
    std::string strReservedBlock2;
};


class RecurrenceState {
public:
    RecurrenceState();
    ~RecurrenceState();

    HRESULT ParseBlob(char *lpData, unsigned int ulLen, ULONG ulFlags);
    HRESULT GetBlob(char **lpData, unsigned int *lpulLen, void *base = NULL);

	/* not inlined for swig */

    unsigned int ulReaderVersion;
    unsigned int ulWriterVersion;
    unsigned int ulRecurFrequency;
    unsigned int ulPatternType;
    unsigned int ulCalendarType;
    unsigned int ulFirstDateTime;
    unsigned int ulPeriod;
    unsigned int ulSlidingFlag;

	// pattern type specific:
    unsigned int ulWeekDays;	// weekly, which day of week (see: WD_* bitmask)
    unsigned int ulDayOfMonth;	// monthly, day in month
    unsigned int ulWeekNumber;	// monthly, 1-4 or 5 for last

    unsigned int ulEndType;
    unsigned int ulOccurrenceCount;
    unsigned int ulFirstDOW;
    unsigned int ulDeletedInstanceCount;
    std::vector<unsigned int> lstDeletedInstanceDates;
    unsigned int ulModifiedInstanceCount;
    std::vector<unsigned int> lstModifiedInstanceDates;
    unsigned int ulStartDate;
    unsigned int ulEndDate;

    unsigned int ulReaderVersion2;
    unsigned int ulWriterVersion2;
    unsigned int ulStartTimeOffset;
    unsigned int ulEndTimeOffset;

    unsigned int ulExceptionCount;
	std::vector<Exception> lstExceptions;
    
    std::string strReservedBlock1;
    std::vector<ExtendedException> lstExtendedExceptions;
    
    std::string strReservedBlock2;
};

#define ARO_SUBJECT			0x0001
#define ARO_MEETINGTYPE 	0x0002
#define ARO_REMINDERDELTA 	0x0004
#define ARO_REMINDERSET		0x0008
#define ARO_LOCATION		0x0010
#define ARO_BUSYSTATUS		0x0020
#define ARO_ATTACHMENT		0x0040
#define ARO_SUBTYPE			0x0080
#define ARO_APPTCOLOR		0x0100
#define ARO_EXCEPTIONAL_BODY 0x0200

// valid ulRecurFrequency values
#define RF_DAILY	0x200A
#define RF_WEEKLY	0x200B
#define RF_MONTHLY	0x200C
#define RF_YEARLY	0x200D

// valid ulEndType values
#define ET_DATE		0x2021
#define ET_NUMBER	0x2022
#define ET_NEVER	0x2023

// ulWeekDays bit mask
#define WD_SUNDAY		0x01
#define WD_MONDAY		0x02
#define WD_TUESDAY		0x04
#define WD_WEDNESDAY	0x08
#define WD_THURSDAY		0x10
#define WD_FRIDAY		0x20
#define WD_SATURDAY		0x40
#define WD_MASK			0x7F
