import os

import icalmapi
import RecurrenceState as RS

from conftest import assert_get_glob_from_ical, assert_item_count_from_ical, getrecurrencestate

dir_path = os.path.dirname(os.path.realpath(__file__))

# Tests a valid component with an invalid property
def test_invalid_property(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/invalid_property.ics'), 'rb').read()
    assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    assert icaltomapi.GetNumInvalidProperties() == 1
    assert icaltomapi.GetNumInvalidComponents() == 0

# Tests an invalid component
def test_invalid_component(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/invalid_event.ics'), 'rb').read()
    assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    assert icaltomapi.GetNumInvalidProperties() == 0
    assert icaltomapi.GetNumInvalidComponents() == 1

# Tests an valid component due to a mandatory invalid property
def test_invalid_component_and_property(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/invalid_event_due_to_property.ics'), 'rb').read()
    assert_item_count_from_ical(icaltomapi, ical, 0)
    assert icaltomapi.GetNumInvalidProperties() == 1
    assert icaltomapi.GetNumInvalidComponents() == 1

# Tests an invalid timezone (different component logic)
def test_invalid_timezone(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/invalid_timezone.ics'), 'rb').read()
    assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    assert icaltomapi.GetNumInvalidProperties() == 1
    assert icaltomapi.GetNumInvalidComponents() == 1

def test_double_occurencehit_1(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9143.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_DAILY
    assert rs.ulPeriod == 60*24
    assert rs.ulPatternType == 0
    assert rs.ulEndType == RS.ET_DATE
    assert rs.ulEndDate == 216250560  # start of day in UNTIL field, in RTime .. less hardcoded?

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    # we write the UNTIL as ((end date + local start time) => GMT), not ((end date + local day end time - 1s) => GMT)
    assert b'RRULE:FREQ=DAILY;UNTIL=20120301T120000' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120125T120000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120125T130000' in ma


def test_double_occurencehit_2(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9143-2.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_DAILY
    assert rs.ulPeriod == 60*24
    assert rs.ulPatternType == 0
    assert rs.ulEndType == RS.ET_DATE
    assert rs.ulEndDate == rs.ulStartDate

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    # we write the UNTIL as ((end date + local start time) => GMT), not ((end date + local day end time - 1s) => GMT)
    assert b'RRULE:FREQ=DAILY;UNTIL=20120125T120000' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120125T120000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120125T130000' in ma


def test_invaliddates_thunderbird_weekly1(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_WEEKLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 1
    assert rs.ulEndType == RS.ET_NEVER
    assert rs.ulEndDate == 0x5AE980DF
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1337644800

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=WEEKLY;BYDAY=TU' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120522T150000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120522T160000' in ma


def test_invaliddates_thunderbird_monthly_bymonthday1(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-2.ics'), 'rb').read()

    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 2
    assert rs.ulEndType, RS.ET_NEVER
    assert rs.ulDayOfMonth == 22
    assert rs.ulEndDate == 0x5AE980DF
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1337644800

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=MONTHLY;BYMONTHDAY=22' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120522T170000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120522T180000' in ma


def test_invaliddates_thunderbird_monthly_bymonthday2(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-3.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)
    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_NEVER
    assert rs.ulDayOfMonth == 22
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1340323200
    assert rs.ulEndDate == 0x5AE980DF
    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=MONTHLY;BYMONTHDAY=22' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120622T170000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120622T180000' in ma


def test_invaliddates_thunderbird_monthly_bymonthday_31th(icaltomapi, mapitoical, message):
    # outlook will show the item every month on the last day, caldav every month with 31 days
    # This is a issue?
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-4.ics'), 'rb').read()

    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)
    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_NEVER
    assert rs.ulDayOfMonth == 31
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1335744000
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=MONTHLY;BYMONTHDAY=31' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120430T160000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120430T170000' in ma


def test_invaliddates_thunderbird_monthly_byday1(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-5.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 3
    assert rs.ulWeekNumber == 3
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1332115200
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=MONTHLY;BYDAY=3MO' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120319T170000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120319T180000' in ma


def test_invaliddates_thunderbird_monthly_byday2(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-6.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 3
    assert rs.ulWeekNumber == 3
    assert rs.ulEndType, RS.ET_NEVER
    #assert RS.RTimeToUnixTime(rs.ulStartDate) == 1334534400
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=MONTHLY;BYDAY=3MO' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120416T170000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120416T180000' in ma


def test_invaliddates_thunderbird_monthly_byday3(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-7.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 3
    assert rs.ulWeekNumber == 4
    assert rs.ulEndType == RS.ET_NUMBER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1337817600
    assert RS.RTimeToUnixTime(rs.ulEndDate) == 1343260800

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=MONTHLY;COUNT=3;BYDAY=4TH' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120524T200000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120524T210000' in ma


def test_invaliddates_thunderbird_monthly_byday_last(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP10052.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)
    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 3
    assert rs.ulWeekNumber == 5
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1340582400
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=MONTHLY;BYDAY=-1MO' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120625T140000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120625T150000' in ma


def test_thunderbird_monthly_every_day(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP10050.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)
    assert rs.ulRecurFrequency == RS.RF_WEEKLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 1
    assert rs.ulWeekNumber == 0
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1337990400
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=WEEKLY;BYDAY=SA' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120526T200000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120526T210000' in ma


def test_invaliddates_thunderbird_yearly_bymonthday1(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-8.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 2
    assert rs.ulEndType, RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1337731200
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYMONTHDAY=23;BYMONTH=5' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120523T120000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120523T130000' in ma


def test_invaliddates_thunderbird_yearly_bymonthday2(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-9.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)
    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1337817600
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYMONTHDAY=24;BYMONTH=5' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120524T120000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120524T130000' in ma


def test_invaliddates_thunderbird_yearly_bymonthday3(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-10.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)
    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1369180800
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYMONTHDAY=22;BYMONTH=5' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20130522T120000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20130522T130000' in ma


def test_invaliddates_thunderbird_yearly_bymonthday_leapyear1(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-11.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_NEVER
    assert rs.ulDayOfMonth == 29
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1298851200
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYMONTHDAY=29;BYMONTH=2' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20110228T170000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20110228T180000' in ma


def test_invaliddates_thunderbird_yearly_byday_bymonth1(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-12.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 3
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1362960000
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYDAY=2MO;BYMONTH=3' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20130311T100000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20130311T110000' in ma


def test_invaliddates_thunderbird_yearly_byday_bymonth2(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-13.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 3
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1368403200
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYDAY=2MO;BYMONTH=5' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20130513T100000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20130513T110000' in ma


def test_invaliddates_thunderbird_yearly_byday_bymonth3(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-14.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 3
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1399852800
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYDAY=2MO;BYMONTH=5' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20140512T100000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20140512T110000' in ma


def test_invaliddates_thunderbird_yearly_byday_bymonth4(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-15.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 3
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1338336000
    assert rs.ulEndDate == 0x5AE980DF
    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYDAY=-1WE;BYMONTH=5' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120530T150000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120530T160000' in ma


def test_invaliddates_thunderbird_yearly_byday_bymonth5(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-16.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 3
    assert rs.ulEndType, RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1338336000
    assert rs.ulEndDate == 0x5AE980DF
    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYDAY=-1WE;BYMONTH=5' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120530T150000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120530T160000' in ma


def test_invaliddates_thunderbird_yearly_byday_bymonth6(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP9771-17.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 3
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1369785600
    assert rs.ulEndDate == 0x5AE980DF
    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=YEARLY;BYDAY=-1WE;BYMONTH=5' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20130529T150000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20130529T160000' in ma


def test_monthly_bysetpos_byday(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'issues/ZCP10916.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    message.SaveChanges(0)
    rs = getrecurrencestate(blob)
    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 3
    assert rs.ulWeekNumber == 1
    assert rs.ulEndType, RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1338508800
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=MONTHLY;BYMONTHDAY=1' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120601T150000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120601T160000' in ma
