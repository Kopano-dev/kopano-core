import os

import pytest
import inetmapi

from MAPI import MAPI_MODIFY
from MAPI.Util import OpenECSession, GetDefaultStore


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
def message(root):
    message = root.CreateMessage(None, 0)

    yield message


@pytest.fixture
def dopt():
    dopt = inetmapi.delivery_options()
    dopt.ascii_upgrade = "utf-8"
    return dopt
