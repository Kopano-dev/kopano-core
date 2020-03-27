from kopano import Occurrence


def test_str(daily, daily_appointment):
    occ = next(daily.occurrences())
    assert isinstance(occ, Occurrence)
    assert str(occ) == 'Occurrence(daily)'

    occ = next(daily_appointment.occurrences())
    assert isinstance(occ, Occurrence)
    assert str(occ) == 'Occurrence(once)'


def test_cancel(daily, daily_appointment):
    for item in (daily, daily_appointment):
        occ = next(item.occurrences())
        assert not occ.canceled
        occ.cancel()
        assert occ.canceled


def test_color(daily, daily_appointment):
    for item in (daily, daily_appointment):
        occ = next(item.occurrences())
        assert not occ.color
        occ.color = 'grey'
        assert occ.color == 'grey'
