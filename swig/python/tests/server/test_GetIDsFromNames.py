import os

import pytest

from MAPI import MNID_ID, MNID_STRING, MAPI_CREATE
from MAPI.Struct import MAPINAMEID, PROP_TAG, PROP_TYPE, PROP_ID
from MAPI.Tags import PT_ERROR, PT_UNSPECIFIED


if not os.getenv('KOPANO_SOCKET'):
    pytest.skip('No kopano-server running', allow_module_level=True)


def test_badid1(message):
    with pytest.raises(RuntimeError) as excinfo:
        message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_STRING, b'test')], 0)
    assert 'Must pass unicode string for MNID_STRING' in str(excinfo)


def test_badid2(message):
    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_ID, 1)], 0)
    assert len(ids) == 1
    assert ids[0] == PROP_TAG(PT_ERROR, 0)


def test_badid3(message):
    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_STRING, u'nonexist')], 0)
    assert len(ids) == 1
    assert ids[0] == PROP_TAG(PT_ERROR, 0)


def test_createnew(message):
    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_STRING, u'test')], MAPI_CREATE)
    assert len(ids) == 1
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_TYPE(ids[0]) == PT_UNSPECIFIED

    tag = ids[0]
    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_STRING, u'test')], MAPI_CREATE)
    assert len(ids) == 1
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_TYPE(ids[0]) == PT_UNSPECIFIED
    assert tag == ids[0]

    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_STRING, u'test2')], MAPI_CREATE)
    assert len(ids) == 1
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_TYPE(ids[0]) == PT_UNSPECIFIED
    assert tag != ids[0]


def test_createnew2(message):
    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_ID, 5)], MAPI_CREATE)
    assert len(ids) == 1
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_TYPE(ids[0]) == PT_UNSPECIFIED

    tag = ids[0]
    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_ID, 5)], MAPI_CREATE)
    assert len(ids) == 1
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_TYPE(ids[0]) == PT_UNSPECIFIED
    assert tag == ids[0]

    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_ID, 6)], MAPI_CREATE)
    assert len(ids) == 1
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_TYPE(ids[0]) == PT_UNSPECIFIED
    assert tag != ids[0]


def test_createnewmulti(message):
    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_ID, 10), MAPINAMEID(b'1234567890123456', MNID_ID,11)], MAPI_CREATE)
    assert len(ids) == 2
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_ID(ids[1]) >= 0x8500

    tag = ids[1]
    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_ID, 11), MAPINAMEID(b'1234567890123456', MNID_ID,12)], MAPI_CREATE)

    assert len(ids) == 2
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_ID(ids[1]) >= 0x8500
    assert tag == ids[0]


def test_get(message):
    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_ID, 20)], MAPI_CREATE)
    assert len(ids) == 1
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_TYPE(ids[0]) == PT_UNSPECIFIED

    ids = message.GetIDsFromNames([MAPINAMEID(b'1234567890123456', MNID_ID, 20)], 0)
    assert len(ids) == 1
    assert PROP_ID(ids[0]) >= 0x8500
    assert PROP_TYPE(ids[0]) == PT_UNSPECIFIED
