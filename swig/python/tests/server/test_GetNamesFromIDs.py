import os

import pytest

from MAPI import MNID_ID, MNID_STRING, MAPI_CREATE
from MAPI.Struct import MAPINAMEID, PROP_TAG, PROP_TYPE, PROP_ID
from MAPI.Tags import PT_ERROR, PT_UNSPECIFIED


if not os.getenv('KOPANO_SOCKET'):
    pytest.skip('No kopano-server running', allow_module_level=True)


def test_createget(message):
    name = MAPINAMEID(b'1234567890123456', MNID_ID, 30)
    ids = message.GetIDsFromNames([name], MAPI_CREATE)

    ids = message.GetNamesFromIDs(ids, None, 0)
    assert len(ids) == 1
    assert repr(ids[0]) == repr(name)


def test_creategetstring(message):
    name = MAPINAMEID(b'1234567890123456', MNID_STRING, u'testCreateGetString')
    ids = message.GetIDsFromNames([name], MAPI_CREATE)

    ids = message.GetNamesFromIDs(ids, None, 0)
    assert len(ids) == 1
    assert repr(ids[0]) == repr(name)


def test_getnonexist(message):
    ids = [PROP_TAG(PT_UNSPECIFIED, 0x1000)]
    ids = message.GetNamesFromIDs(ids, None, 0)
    assert len(ids) == 1
    assert repr(ids[0]) == 'None'


def test_getmulti(message):
    name = MAPINAMEID(b'1234567890123456', MNID_ID, 40)
    ids = message.GetIDsFromNames([name], MAPI_CREATE)

    ids += [PROP_TAG(PT_UNSPECIFIED, 0x9000)]
    ids = message.GetNamesFromIDs(ids, None, 0)
    assert len(ids) == 2
    assert repr(ids[0]) == repr(name)
    assert repr(ids[1]) == 'None'
