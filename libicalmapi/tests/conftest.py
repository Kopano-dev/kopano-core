import os

import pytest

import icalmapi
import RecurrenceState as RS

from MAPI import MAPI_MODIFY, MAPI_UNICODE
from MAPI.Util import OpenECSession, GetDefaultStore, DELETE_HARD_DELETE
from MAPI.Tags import PR_IPM_APPOINTMENT_ENTRYID


@pytest.fixture
def session():
    user = os.getenv('KOPANO_TEST_USER')
    password = os.getenv('KOPANO_TEST_PASSWORD')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket)


@pytest.fixture
def store(session):
    return GetDefaultStore(session)


@pytest.fixture
def root(store):
    return store.OpenEntry(None, None, MAPI_MODIFY)


@pytest.fixture
def ab(session):
    return session.OpenAddressBook(0, None, MAPI_UNICODE)


@pytest.fixture
def calendar(store, root):
    aeid = root.GetProps([PR_IPM_APPOINTMENT_ENTRYID], 0)
    calendar = store.OpenEntry(aeid[0].Value, None, MAPI_MODIFY)
    yield calendar
    calendar.EmptyFolder(DELETE_HARD_DELETE, None, 0)


@pytest.fixture
def message(calendar):
    return calendar.CreateMessage(None, 0)


@pytest.fixture
def icaltomapi(store, ab):
    return icalmapi.CreateICalToMapi(store, ab, False)


@pytest.fixture
def mapitoical(ab):
    return icalmapi.CreateMapiToICal(ab, 'utf-8')

def assert_item_count_from_ical(icaltomapi, ical, N):
    icaltomapi.ParseICal(ical, 'utf-8', 'UTC', None, 0)
    assert icaltomapi.GetItemCount() == N

def assert_get_glob_from_ical(icaltomapi, message, ical, N):
    assert_item_count_from_ical(icaltomapi, ical, N)
    icaltomapi.GetItem(0, 0, message)
    # static range named prop
    return message.GetProps([0x80160102], 0)[0].Value


def getrecurrencestate(blob):
    rs = RS.RecurrenceState()
    rs.ParseBlob(blob, 0)
    return rs

