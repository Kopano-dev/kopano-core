/* SPDX-License-Identifier: AGPL-3.0-only */
%module RecurrenceState

%{
	/* parse header in the wrapper code */
#include <kopano/platform.h>
#include <mapidefs.h>
#include <mapix.h>
	/* local version without nested classes */
#include "RecurrenceState.swig.h"

%}

%include "std_string.i"
%include "cstring.i"
%include "cwstring.i"
%include <kopano/typemap.i>
%include "std_vector.i"
%include "std_wstring.i"

%cstring_output_allocate_size(char **lpData, size_t *lpulLen, MAPIFreeBuffer(*$1));

%typemap(in) (const char *lpData, unsigned int ulLen) (char *buf = NULL, Py_ssize_t size)
{
  if(PyBytes_AsStringAndSize($input, &buf, &size) == -1)
    %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);
  $1 = %reinterpret_cast(buf, $1_ltype);
  $2 = %numeric_cast(size, $2_ltype);
}

/* parse header file to generate wrappers */
%include "RecurrenceState.swig.h"

namespace std {
	%template(UIntVector) vector<unsigned int>;
	%template(ExceptionVector) vector<Exception>;
	%template(ExtendedExceptionVector) vector<ExtendedException>;
}

%pythoncode%{
import time

# actually functions from platform.h .. probably should move
def FileTimeToUnixTime(llFT):
    return int((llFT - 116444736000000000) / 10000000)
def RTimeToFileTime(ulRTime):
    return ulRTime * 600000000
def RTimeToUnixTime(ulRTime):
    return FileTimeToUnixTime(RTimeToFileTime(ulRTime))

def _weekdaystostring(ulWeekDays):
    days = []
    days.append('Sunday')    if (ulWeekDays & WD_SUNDAY) else ''
    days.append('Monday')    if (ulWeekDays & WD_MONDAY) else ''
    days.append('Tuesday')   if (ulWeekDays & WD_TUESDAY) else ''
    days.append('Wednesday') if (ulWeekDays & WD_WEDNESDAY) else ''
    days.append('Thursday')  if (ulWeekDays & WD_THURSDAY) else ''
    days.append('Friday')    if (ulWeekDays & WD_FRIDAY) else ''
    days.append('Saturday')  if (ulWeekDays & WD_SATURDAY) else ''
    return ', '.join(days)

def _arotostring(ulOverrideFlags):
    aro = []
    aro.append('Subject') if ulOverrideFlags & ARO_SUBJECT else ''
    aro.append('MeetingType') if ulOverrideFlags & ARO_MEETINGTYPE else ''
    aro.append('ReminderDelta') if ulOverrideFlags & ARO_REMINDERDELTA else ''
    aro.append('ReminderSet') if ulOverrideFlags & ARO_REMINDERSET else ''
    aro.append('Location') if ulOverrideFlags & ARO_LOCATION else ''
    aro.append('BusyStatus') if ulOverrideFlags & ARO_BUSYSTATUS else ''
    aro.append('Attachment') if ulOverrideFlags & ARO_ATTACHMENT else ''
    aro.append('SubType') if ulOverrideFlags & ARO_SUBTYPE else ''
    aro.append('AppointmentColor') if ulOverrideFlags & ARO_APPTCOLOR else ''
    aro.append('Body') if ulOverrideFlags & ARO_EXCEPTIONAL_BODY else ''
    return ', '.join(aro)

# print human readable version of recurrencestate class
def Summarize(rs):
    print("Versions, reader 0x%04x writer 0x%04x" % (rs.ulReaderVersion, rs.ulWriterVersion))
    if rs.ulRecurFrequency == RF_DAILY:
        print("Daily")
    elif rs.ulRecurFrequency == RF_WEEKLY:
        print("Weekly")
    elif rs.ulRecurFrequency == RF_MONTHLY:
        print("Monthly")
    elif rs.ulRecurFrequency == RF_YEARLY:
        print("Yearly")
    else:
        print("ERROR: Unknown ulRecurFrequency: 0x%x" % rs.ulRecurFrequency)
    # pattern type is info for reader
    # print("Patterntype: %d, ulCalendarType: %d" % (rs.ulPatternType, rs.ulCalendarType))
    print("FirstDateTime: %d" % rs.ulFirstDateTime)
    print("Period: %d" % rs.ulPeriod)
    print("ulSlidingFlag: %d" % rs.ulSlidingFlag)
    print("Pattern info, type %d:" % rs.ulPatternType)
    if rs.ulPatternType == 0:
        pass
    elif rs.ulPatternType == 1:
        print("  Weekdays: %s" % _weekdaystostring(rs.ulWeekDays))
    elif rs.ulPatternType in (2, 4, 0xa, 0xc):
        print("  Day of Month: %d" % rs.ulDayOfMonth)
    elif rs.ulPatternType in (3, 0xb):
        print("  Weekdays: %s" % _weekdaystostring(rs.ulWeekDays))
        print("  Weeknr  : %d" % rs.ulWeekNumber)
    else:
        print("ERROR: Invalid pattern type: %d" % rs.ulPatternType)
    if rs.ulEndType == ET_DATE:
        print("Ending recurrence by date, see EndDate below, end count is: %d" % rs.ulOccurrenceCount)
    elif rs.ulEndType == ET_NUMBER:
        print("Ending recurrence by count: %d" % rs.ulOccurrenceCount)
    elif rs.ulEndType == ET_NEVER:
        print("Never ending recurrence, end count is 10: %d" % rs.ulOccurrenceCount)
    else:
        print("ERROR: Invalid end type: 0x%x" % rs.ulEndType)
    print("First Day of Week: %d" % rs.ulFirstDOW)
    # for every modify, there is a delete too
    print("# of Deleted  Exceptions: %d" % (rs.ulDeletedInstanceCount - rs.ulModifiedInstanceCount))
    print("# of Modified Exceptions: %d" % rs.ulModifiedInstanceCount)
    print("First Occurrence: %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(rs.ulStartDate))))
    print("Last  Occurrence: %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(rs.ulEndDate))))
    print("Delete/Modify occurrence times:")
    # beware of the crash!
    d = rs.lstDeletedInstanceDates
    for i in d:
        print("  %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(i))))
    print("Modified occurrence times:")
    # beware of the crash!
    m = rs.lstModifiedInstanceDates
    for i in m:
        print("  %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(i))))
    if rs.ulReaderVersion2 == 0:
        print("Task done")
        return
    print("Versions2, reader 0x%04x writer 0x%04x" % (rs.ulReaderVersion2, rs.ulWriterVersion2))
    print("Start time offset: %s" % rs.ulStartTimeOffset)
    print("End   time offset: %s" % rs.ulEndTimeOffset)
    print("Modified exception count: %d" % rs.ulExceptionCount)
    # beware of the crash!
    le = rs.lstExceptions
    lee = rs.lstExtendedExceptions
    for n in range(0,rs.ulExceptionCount):
        print("Exception " + str(n))
        print("  SDT %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(le[n].ulStartDateTime))))
        print("  EDT %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(le[n].ulEndDateTime))))
        print("  OSD %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(le[n].ulOriginalStartDate))))
        if le[n].ulOverrideFlags & (ARO_SUBJECT | ARO_LOCATION):
            # should be the same
            print("  XSDT %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(lee[n].ulStartDateTime))))
            print("  XEDT %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(lee[n].ulEndDateTime))))
            print("  XOSD %s +0000" % time.asctime(time.gmtime(RTimeToUnixTime(lee[n].ulOriginalStartDate))))
        print("  ARO changes: %s" % _arotostring(le[n].ulOverrideFlags))
        if le[n].ulOverrideFlags & ARO_SUBJECT:
            print("  Subject: %s :: %s" % (le[n].strSubject, lee[n].strWideCharSubject))
        if le[n].ulOverrideFlags & ARO_MEETINGTYPE:
            print("  MeetingType: %d" % le[n].ulMeetingType)
        if le[n].ulOverrideFlags & ARO_REMINDERDELTA:
            print("  ReminderDelta: %d" % le[n].ulReminderDelta)
        if le[n].ulOverrideFlags & ARO_REMINDERSET:
            print("  ReminderSet: %d" % le[n].ulReminderSet)
        if le[n].ulOverrideFlags & ARO_LOCATION:
            print("  Location: %s :: %s" % (le[n].strLocation, lee[n].strWideCharLocation))
        if le[n].ulOverrideFlags & ARO_BUSYSTATUS:
            print("  BusyStatus: %d" % le[n].ulBusyStatus)
        if le[n].ulOverrideFlags & ARO_ATTACHMENT:
            print("  Attachment: %d" % le[n].ulAttachment)
        if le[n].ulOverrideFlags & ARO_SUBTYPE:
            print("  SubType %d" % le[n].ulSubType)
        if le[n].ulOverrideFlags & ARO_APPTCOLOR:
            print("  AppointmentColor %d" % le[n].ulAppointmentColor)
        if rs.ulWriterVersion2 >= 0x3009:
            print("  Extended, highlight: %d" % lee[n].ulChangeHighlightValue)
%}
