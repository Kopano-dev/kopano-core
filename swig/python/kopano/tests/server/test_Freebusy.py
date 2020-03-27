from datetime import datetime, timedelta


START = datetime(2018, 7, 1)
END = datetime(2018, 8, 1)


def test_blocks(freebusy, weekly):
    blocks = list(freebusy.blocks(START, END))
    assert len(blocks) == 0

    freebusy.publish(START, END)
    blocks = list(freebusy.blocks(START, END))
    assert len(blocks) == 4
    assert blocks[0].status == 'busy'
    assert str(blocks[0]) == 'FreeBusyBlock()'

    # iterate over fb, no start, no end
    blocks = list(freebusy.blocks())
    assert len(blocks) == 4
    assert blocks[0].status == 'busy'


def test_outofrange(freebusy, tentative):
    freebusy.publish(START + timedelta(days=30), END + timedelta(days=30))
    blocks = list(freebusy.blocks(START, END))
    assert len(blocks) == 0


def test_tentative(freebusy, tentative):
    freebusy.publish(START, END)
    blocks = list(freebusy.blocks(START, END))
    assert len(blocks) == 1
    assert blocks[0].status == 'tentative'


def test_tentative_exception(freebusy, weekly):
    occ = next(weekly.occurrences())
    occ.busystatus = 'outofoffice'

    freebusy.publish(START, END)
    blocks = list(freebusy.blocks(START, END))
    assert len(blocks) == 4
    assert blocks[0].status == 'outofoffice'
    assert blocks[1].status == 'busy'


def test_str(user):
    assert str(user.freebusy) == 'FreeBusy()'
