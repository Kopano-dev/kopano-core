import os

import pytest

from MAPI import (MAPIAdminProfiles, KEEP_OPEN_READWRITE, DELETE_HARD_DELETE, MAPI_MODIFY)
from MAPI.Util import OpenECSession, GetDefaultStore, SPropValue
from MAPI.Tags import IID_IECTestProtocol, PR_SUBJECT, PR_ENTRYID


kopanoserver = pytest.mark.skipif(
        not os.getenv('KOPANO_TEST_SERVER'),
        reason='No kopano-server running'
)


@pytest.fixture
def adminprof():
    return MAPIAdminProfiles(0)


@pytest.fixture
def adminservice(adminprof):
    name = b't1'
    adminprof.CreateProfile(name, None, 0, 0)

    yield adminprof.AdminServices(name, None, 0, 0)

    adminprof.DeleteProfile(name, 0)


@pytest.fixture
def session():
    user = os.getenv('KOPANO_TEST_USER')
    password = os.getenv('KOPANO_TEST_PASSWORD')
    socket = os.getenv('KOPANO_TEST_SERVER')

    if not user or not password or not socket:
        raise ValueError('Tests expect user/password/server to be configured')

    return OpenECSession(user, password, socket)


@pytest.fixture
def store(session):
    return GetDefaultStore(session)


@pytest.fixture
def root(store):
    return store.OpenEntry(None, None, 0)


@pytest.fixture
def addressbook(session):
    return session.OpenAddressBook(0, None, 0)


@pytest.fixture
def gab(addressbook):
    defaultdir = addressbook.GetDefaultDir()
    return addressbook.OpenEntry(defaultdir, None, 0)


@pytest.fixture
def message(store):
    root = store.OpenEntry(None, None, MAPI_MODIFY)
    message = root.CreateMessage(None, 0)
    message.SetProps([SPropValue(PR_SUBJECT, b'Test')])
    message.SaveChanges(KEEP_OPEN_READWRITE)

    yield message

    eid = message.GetProps([PR_ENTRYID], 0)[0]
    root.DeleteMessages([eid.Value], 0, None, DELETE_HARD_DELETE)
