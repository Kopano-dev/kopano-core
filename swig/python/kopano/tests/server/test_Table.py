import csv
from io import StringIO

from MAPI.Tags import PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE, PR_EC_USERNAME


def test_header(statstable):
    assert statstable.header


def test_dict(statstable):
    d = statstable.dict_(PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE)
    assert isinstance(d, dict)
    assert int(d[b'cache_cell_maxsz']) > 0


def test_index(usertable):
    assert usertable.index(PR_EC_USERNAME)


def test_data(statstable):
    data = statstable.data()
    assert data
    assert data[0]


def test_dataheader(usertable):
    # List of lists with data, first entry is the header
    data = usertable.data(header=True)
    assert data
    assert 'PR_EC_USERNAME' in data[0]


def test_text(statstable):
    assert statstable.text()


def test_textborders(statstable):
    assert statstable.text(borders=True)


def test_csv(statstable):
    data = statstable.csv(delimiter=';')
    assert data

    reader = csv.reader(StringIO(data), delimiter=';')
    assert next(reader)


def test_sort(statstable):
    statstable.sort(PR_DISPLAY_NAME)
    statstable.sort((PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_VALUE))

    # TODO check sorting results


def test_str(usertable):
    assert str(usertable) == 'Table(PR_EC_STATSTABLE_USERS)'
