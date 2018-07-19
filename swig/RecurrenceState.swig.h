/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/* Copy of the full common/RecurrenceState.h file, but without the
   nested classes in the main class. Since swig can't handle those.
 */
#include <vector>
#include <string>

#define RECURRENCE_STATE_CALENDAR	0x01
#define RECURRENCE_STATE_TASKS		0x02

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

// valid ulFirstDOW values
#define DOW_SUNDAY		0x00
#define DOW_MONDAY		0x01
#define DOW_TUESDAY		0x02
#define DOW_WEDNESDAY	0x03
#define DOW_THURSDAY	0x04
#define DOW_FRIDAY		0x05
#define DOW_SATURDAY	0x06

// ulWeekDays bit mask
#define WD_SUNDAY		0x01
#define WD_MONDAY		0x02
#define WD_TUESDAY		0x04
#define WD_WEDNESDAY	0x08
#define WD_THURSDAY		0x10
#define WD_FRIDAY		0x20
#define WD_SATURDAY		0x40
#define WD_MASK			0x7F

// ulPatternType values
#define PT_DAY				0x0
#define PT_WEEK				0x1
#define PT_MONTH			0x2
#define PT_MONTH_END		0x4
#define PT_MONTH_NTH		0x3
#define PT_HJ_MONTH			0xA
#define PT_HJ_MONTH_NTH		0xB
#define PT_HJ_MONTH_END		0xC

namespace KC {

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
	HRESULT ParseBlob(const char *lpData, unsigned int ulLen, ULONG ulFlags);
    HRESULT GetBlob(char **lpData, unsigned int *lpulLen, void *base = NULL);

	/* not inlined for swig */

	unsigned int ulReaderVersion = 0x3004;
	unsigned int ulWriterVersion = 0x3004;
	unsigned int ulRecurFrequency = 0; /* "invalid" */
	unsigned int ulPatternType = PT_DAY;
	unsigned int ulCalendarType = 0;
	unsigned int ulFirstDateTime = 0;
	unsigned int ulPeriod = 0;
	unsigned int ulSlidingFlag = 0;

	// pattern type specific:
	unsigned int ulWeekDays = 0; // weekly, which day of week (see: WD_* bitmask)
	unsigned int ulDayOfMonth = 0; // monthly, day in month
	unsigned int ulWeekNumber = 0; // monthly, 1-4 or 5 for last

	unsigned int ulEndType = 0;
	unsigned int ulOccurrenceCount = 0;
	unsigned int ulFirstDOW = DOW_MONDAY; /* default Outlook */
	unsigned int ulDeletedInstanceCount = 0;
	std::vector<unsigned int> lstDeletedInstanceDates;
	unsigned int ulModifiedInstanceCount = 0;
	std::vector<unsigned int> lstModifiedInstanceDates;
	unsigned int ulStartDate = 0;
	unsigned int ulEndDate = 0;

	unsigned int ulReaderVersion2 = 0x3006;
	unsigned int ulWriterVersion2 = 0x3008; /* can also be 3009, but Outlook (2003) sets 3008 */
	unsigned int ulStartTimeOffset = 0; /* max 1440-1 */
	unsigned int ulEndTimeOffset = 0; /* max 1440-1 */

	unsigned int ulExceptionCount = 0;
	std::vector<Exception> lstExceptions;
	std::string strReservedBlock1;
	std::vector<ExtendedException> lstExtendedExceptions;
	std::string strReservedBlock2;
};

} /* namespace */
