from datetime import datetime, timedelta


def test_set(outofoffice):
    begin_time = datetime.now().replace(microsecond=0)
    end_time = begin_time + timedelta(days=5)
    subject = 'Vacation'
    message = 'I am out of office'

    outofoffice.enabled = True
    outofoffice.message = message
    outofoffice.subject = subject
    outofoffice.start = begin_time
    outofoffice.end = end_time

    assert outofoffice.enabled
    assert outofoffice.message == message
    assert outofoffice.subject == subject
    assert outofoffice.start == begin_time
    assert outofoffice.end == end_time


def test_active(outofoffice):
    outofoffice.enabled = True
    outofoffice.start = datetime(2000, 1, 1)
    outofoffice.end = None
    assert outofoffice.active

    outofoffice.start = datetime(2030, 1, 1)
    assert not outofoffice.active

    outofoffice.start = datetime(2000, 1, 1)
    outofoffice.end = datetime(2010, 1, 1)
    assert not outofoffice.active

    outofoffice.start = datetime(2000, 1, 1)
    outofoffice.enabled = False
    assert not outofoffice.active


def test_perioddesc(outofoffice):
    outofoffice.enabled = True
    outofoffice.start = datetime(2000, 1, 1)
    outofoffice.end = None
    assert outofoffice.period_desc == 'from 2000-01-01 00:00:00'

    outofoffice.start = None
    outofoffice.end = datetime(2000, 1, 1)
    assert outofoffice.period_desc == 'until 2000-01-01 00:00:00'

    outofoffice.start = datetime(2000, 1, 1)
    outofoffice.end = datetime(2010, 1, 1)
    assert outofoffice.period_desc == 'from 2000-01-01 00:00:00 until 2010-01-01 00:00:00'

    outofoffice.start = None
    outofoffice.end = None
    assert outofoffice.period_desc == ''


def test_message(user, outofoffice):
    outofoffice.message = 'ahoi'
    assert outofoffice.message == 'ahoi'

    prop = user.store.prop('PR_EC_OUTOFOFFICE_MSG_W')
    user.store.delete(prop)
    assert outofoffice.message == ''


def testUpdate(outofoffice):
    outofoffice.update(
        message='baba ganoush',
        enabled=False,
        start=datetime(2012, 1, 1)
    )

    assert not outofoffice.enabled
    assert outofoffice.message == 'baba ganoush'
    assert outofoffice.start == datetime(2012, 1, 1)


def testStr(outofoffice):
    outofoffice.subject = 'I am out of office'
    assert str(outofoffice) == 'OutOfOffice(I am out of office)'
