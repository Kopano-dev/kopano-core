import pytest

from datetime import datetime, timedelta

from kopano import ArgumentError


def test_create_daily(daily):
    rec = daily.recurrence

    assert rec.pattern == 'daily'
    assert rec.interval == 1
    assert rec.range_type == 'end_date'
    assert rec.start == datetime(2018, 7, 7)
    assert rec.end == datetime(2018, 7, 25)

    occs = list(daily.occurrences())
    assert len(occs) == 19

    assert occs[0].start == datetime(2018, 7, 7, 9)
    assert occs[0].end == datetime(2018, 7, 7, 9, 30)


def test_weekly(weekly):
    rec = weekly.recurrence
    assert rec.pattern == 'weekly'
    assert rec.interval == 2
    assert rec.range_type == 'end_date'
    assert rec.start == datetime(2018, 7, 7)
    assert rec.end == datetime(2019, 1, 1)

    occs = list(weekly.occurrences())
    assert len(occs) == 26


def test_monthly(monthly):
    rec = monthly.recurrence
    assert rec.pattern, 'monthly'
    assert rec.interval, 1
    assert rec.monthday, 21
    assert rec.range_type, 'end_date'
    assert rec.start == datetime(2018, 7, 7)
    assert rec.end == datetime(2019, 1, 1)

    occs = list(monthly.occurrences())
    assert len(occs) == 6
    for occ in occs:
        assert occ.start.day == 21


def test_monthly_relative(monthly_rel):
    rec = monthly_rel.recurrence
    assert rec.pattern == 'monthly_rel'
    assert rec.interval == 1
    assert rec.index == 'third'
    assert rec.weekdays == ['wednesday']
    assert rec.range_type == 'forever'
    assert rec.start == datetime(2018, 8, 15)

    occs = list(monthly_rel.occurrences(datetime(2018, 1, 1), datetime(2019, 1, 1)))
    assert len(occs) == 5


def test_yearly(yearly):
    rec = yearly.recurrence
    assert rec.pattern == 'yearly'
    assert rec.interval == 1
    assert rec.month == 8
    assert rec.monthday == 11
    assert rec.range_type == 'end_date'

    occs = list(yearly.occurrences(datetime(2010, 1, 1), datetime(2040, 1, 1)))
    assert len(occs) == 12


def test_weekly_relative(yearly_rel):
    rec = yearly_rel.recurrence
    assert rec.pattern == 'yearly_rel'
    assert rec.interval == 2
    assert rec.range_type == 'count'
    assert rec.count == 4
    assert rec.index == 'last'
    assert rec.weekdays == ['monday']
    assert rec.start == datetime(2018, 8, 27)

    occs = list(yearly_rel.occurrences())
    assert len(occs) == 4


def test_updateoccurence(daily):
    for occ in daily.occurrences():
        if occ.start == datetime(2018, 7, 12, 9):
            occ.subject = 'moved'
            occ.start = occ.start + timedelta(hours=1)
            occ.end = occ.end + timedelta(hours=1, minutes=30)
            break

    occs = list(daily.occurrences())
    assert len(occs) == 19

    for occ in occs:
        if occ.start == datetime(2018, 7, 12, 10):
            assert occ.subject == 'moved'
            assert occ.end == datetime(2018, 7, 12, 11)


def test_delete_occurrence(calendar, daily):
    for occ in daily.occurrences():
        if occ.start == datetime(2018, 7, 12, 9):
            daily.delete(occ)

    occs = list(daily.occurrences())
    assert len(occs) == 18

    for occ in daily.occurrences():
        if occ.start == datetime(2018, 7, 19, 9):
            calendar.delete(occ)

    occs = list(daily.occurrences())
    assert len(occs) == 17


def test_pattern(daily):
    rec = daily.recurrence
    assert rec.pattern == 'daily'

    rec.pattern = 'weekly'
    assert rec.pattern == 'weekly'

    with pytest.raises(ArgumentError):
        rec.pattern = 'konijn'


def test_indexj(monthly_rel):
    rec = monthly_rel.recurrence
    assert rec.index == 'third'

    rec.index = 'last'
    assert rec.index == 'last'

    with pytest.raises(ArgumentError):
        rec.index = 'sixth'


def test_rangetype(daily):
    rec = daily.recurrence
    assert rec.range_type == 'end_date'

    rec.range_type = 'forever'
    assert rec.range_type == 'forever'

    with pytest.raises(ArgumentError):
        rec.range_type = 'grappa'


def test_str(daily):
    assert str(daily.recurrence) == 'Recurrence()'
