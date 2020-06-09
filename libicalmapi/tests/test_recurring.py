import os

import icalmapi
from MAPI.Struct import MAPIError
import RecurrenceState as RS

import pytest

from conftest import assert_get_glob_from_ical, getrecurrencestate


dir_path = os.path.dirname(os.path.realpath(__file__))


def test_converticalobject_all_null():
    with pytest.raises(MAPIError) as excinfo:
        icalmapi.CreateICalToMapi(None, None, False)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


def test_convertmapiobjectall_null():
    with pytest.raises(MAPIError) as excinfo:
        icalmapi.CreateMapiToICal(None, '')
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


def test_converticalobject_store_null(ab):
    with pytest.raises(MAPIError) as excinfo:
        icalmapi.CreateICalToMapi(None, ab, False)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


def test_converticalobject_ical_null(store, ab):
    icm = icalmapi.CreateICalToMapi(store, ab, False)
    with pytest.raises(MAPIError) as excinfo:
        icm.ParseICal(b'', 'utf-8', '', None, 0)
    assert 'MAPI_E_CALL_FAILED' in str(excinfo)


def test_daily(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/daily.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_DAILY
    assert rs.ulPeriod == 60*24
    assert rs.ulPatternType == 0
    assert rs.ulEndType == RS.ET_NEVER
    assert RS.RTimeToUnixTime(rs.ulStartDate) == 1327622400  # @todo, less hard-coded? start_of_day(DTSTART)
    assert rs.ulEndDate == 0x5AE980DF

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')
    assert method == b'PUBLISH'
    assert b'RRULE:FREQ=DAILY' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120127T143000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120127T150000' in ma


def test_everyotherday_countstop(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/everyotherday_countstop.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_DAILY
    assert rs.ulPeriod == 60*24*2
    assert rs.ulPatternType == 0
    assert rs.ulEndType == RS.ET_NUMBER
    assert rs.ulOccurrenceCount == 7
    assert rs.ulWeekDays == 0

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')
    assert b'RRULE:FREQ=DAILY;COUNT=7;INTERVAL=2' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120127T143000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120127T150000' in ma


def test_weekly(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/weekly.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_WEEKLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 1
    assert rs.ulEndType == RS.ET_NEVER
    assert rs.ulWeekDays == RS.WD_FRIDAY

    # revert
    mapitoical.AddMessage(message, '', 0)
    (method, mical) = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert b'RRULE:FREQ=WEEKLY;BYDAY=FR' in ma  # hmm, BYDAY=FR is being added .. @todo, fix?
    assert b'DTSTART;TZID=Europe/Amsterdam:20120127T143000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120127T150000' in ma


def test_everyotherweek_datestop(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/everyotherweek_datestop.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_WEEKLY
    assert rs.ulPeriod == 2
    assert rs.ulPatternType == 1
    assert rs.ulEndType == RS.ET_DATE
    assert rs.ulEndDate == 216322560  # recalculated start of day in UNTIL field, in RTime
    assert rs.ulWeekDays == RS.WD_FRIDAY

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    # also contains the BYDAY=FR like above, recalculated to last occurrence: 20 april 2012
    assert b'RRULE:FREQ=WEEKLY;UNTIL=20120420T143000;INTERVAL=2;BYDAY=FR' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120127T143000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120127T150000' in ma


def test_weeklymultidays(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/weekly_multidays.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_WEEKLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 1
    assert rs.ulEndType == RS.ET_NEVER
    assert rs.ulWeekDays == RS.WD_WEDNESDAY | RS.WD_FRIDAY
    # revert
    mapitoical.AddMessage(message, '', 0)
    (method, mical) = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')
    assert b'RRULE:FREQ=WEEKLY;BYDAY=WE,FR' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120127T143000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120127T150000' in ma


def test_monthly(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/monthly.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency, RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_NEVER
    assert rs.ulDayOfMonth == 27

    # revert
    mapitoical.AddMessage(message, '', 0)
    (method, mical) = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    # BYMONTHDAY=27 is being added
    assert b'RRULE:FREQ=MONTHLY;BYMONTHDAY=27' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120127T143000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120127T150000' in ma


def test_monthly_datestop(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/monthly_datestop.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_DATE
    assert rs.ulDayOfMonth == 27

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    # BYMONTHDAY=27 is being added
    assert b'RRULE:FREQ=MONTHLY;UNTIL=20130127T143000;BYMONTHDAY=27' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120127T143000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120127T150000' in ma


def test_monthly_31th(icaltomapi, mapitoical, message):
    # outlook will show the item every month on the last day, caldav every month with 31 days
    ical = open(os.path.join(dir_path, 'ics/monthly_31th.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_MONTHLY
    assert rs.ulPeriod == 1
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_NEVER
    assert rs.ulDayOfMonth == 31

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert b'RRULE:FREQ=MONTHLY;BYMONTHDAY=31' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120531T150000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120531T160000' in ma


def test_yearly(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/yearly.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_NEVER
    assert rs.ulDayOfMonth == 27

    # revert
    mapitoical.AddMessage(message, '', 0)
    method, mical = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert b'RRULE:FREQ=YEARLY;BYMONTHDAY=27;BYMONTH=1' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120127T143000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120127T150000' in ma


def test_yearly_datestop(icaltomapi, mapitoical, message):
    ical = open(os.path.join(dir_path, 'ics/yearly_datestop.ics'), 'rb').read()
    blob = assert_get_glob_from_ical(icaltomapi, message, ical, 1)
    rs = getrecurrencestate(blob)

    assert rs.ulRecurFrequency == RS.RF_YEARLY
    assert rs.ulPeriod == 12
    assert rs.ulPatternType == 2
    assert rs.ulEndType == RS.ET_DATE
    assert rs.ulDayOfMonth == 27

    # revert
    mapitoical.AddMessage(message, '', 0)
    (method, mical) = mapitoical.Finalize(icalmapi.M2IC_NO_VTIMEZONE)
    ma = mical.split(b'\r\n')

    assert b'RRULE:FREQ=YEARLY;UNTIL=20140127T143000;BYMONTHDAY=27;BYMONTH=1' in ma
    assert b'DTSTART;TZID=Europe/Amsterdam:20120127T143000' in ma
    assert b'DTEND;TZID=Europe/Amsterdam:20120127T150000' in ma
