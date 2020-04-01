import pytest

from MAPI import MAPI_BEST_ACCESS
from MAPI.Tags import (PR_OBJECT_TYPE, MAPI_ADDRBOOK, MAPI_UNICODE,
                       MAPI_SEND_NO_RICH_INFO, PR_DISPLAY_NAME,
                       MAPI_ABCONT, PR_DISPLAY_NAME_A,
                       PR_DISPLAY_NAME_W, PR_ADDRTYPE_A, PR_ADDRTYPE_W,
                       PR_EMAIL_ADDRESS_A, PR_EMAIL_ADDRESS_W,
                       IID_IDistList)
from MAPI.Util import SPropValue, MAPIError, PpropFindProp



def test_checkobject(addressbook):
    props = [SPropValue(PR_OBJECT_TYPE, MAPI_ADDRBOOK)]
    assert addressbook.GetProps([PR_OBJECT_TYPE], 0) == props


def test_createoneoff(addressbook):
    entryid1 = addressbook.CreateOneOff(b"username", b"SMTP", b"username@test.com", MAPI_SEND_NO_RICH_INFO)
    entryid2 = addressbook.CreateOneOff(u"username", u"SMTP", u"username@test.com", MAPI_SEND_NO_RICH_INFO | MAPI_UNICODE)

    # TODO: validate the output


def test_createoneoff_parameters(addressbook):
    with pytest.raises(MAPIError) as excinfo:
        addressbook.CreateOneOff(None, None, None, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
       addressbook.CreateOneOff(b'Username', None, None, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
       addressbook.CreateOneOff(b'Username', b'SMTP', None, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
       addressbook.CreateOneOff(None, None, b'username@test.com', 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


def test_resolvename(addressbook):
    assert addressbook.ResolveName(0, 0, None, [[SPropValue(PR_DISPLAY_NAME, b'user10')]])

    with pytest.raises(MAPIError) as excinfo:
        addressbook.ResolveName(0, 0, None, [[SPropValue(PR_DISPLAY_NAME, b'user')]])
    assert 'MAPI_E_AMBIGUOUS_RECIP' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
        addressbook.ResolveName(0, 0, None, [[SPropValue(PR_DISPLAY_NAME, b'notfound')]])
    assert 'MAPI_E_NOT_FOUND' in str(excinfo)


def test_openentry_root(addressbook):
    propsmatch = [SPropValue(PR_OBJECT_TYPE, MAPI_ABCONT)]

    rootitem = addressbook.OpenEntry(None, None, MAPI_BEST_ACCESS)
    props = rootitem.GetProps([PR_OBJECT_TYPE], 0)
    assert props == propsmatch


def test_openentry_oneoff(addressbook):
    matchpropsA = [SPropValue(PR_DISPLAY_NAME_A, b'username'), SPropValue(PR_ADDRTYPE_A, b'SMTP'), SPropValue(PR_EMAIL_ADDRESS_A, b'username@test.com')]
    matchpropsW = [SPropValue(PR_DISPLAY_NAME_W, u'username'), SPropValue(PR_ADDRTYPE_W, u'SMTP'), SPropValue(PR_EMAIL_ADDRESS_W, u'username@test.com')]

    entryid1 = addressbook.CreateOneOff(b"username", b"SMTP", b"username@test.com", MAPI_SEND_NO_RICH_INFO)

    item = addressbook.OpenEntry(entryid1, None, MAPI_BEST_ACCESS)

    props = item.GetProps([PR_DISPLAY_NAME_A, PR_ADDRTYPE_A, PR_EMAIL_ADDRESS_A], 0)
    assert props == matchpropsA

    props = item.GetProps([PR_DISPLAY_NAME_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W], 0)
    assert props == matchpropsW

    entryid2 = addressbook.CreateOneOff(u"username", u"SMTP", u"username@test.com", MAPI_SEND_NO_RICH_INFO | MAPI_UNICODE)

    item = addressbook.OpenEntry(entryid2, None, MAPI_BEST_ACCESS)

    props = item.GetProps([PR_DISPLAY_NAME_A, PR_ADDRTYPE_A, PR_EMAIL_ADDRESS_A], 0)
    assert props == matchpropsA

    props = item.GetProps([PR_DISPLAY_NAME_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W], 0)
    assert props == matchpropsW


def test_openentry_fail(addressbook):
    with pytest.raises(MAPIError) as excinfo:
        addressbook.OpenEntry(None, IID_IDistList, MAPI_BEST_ACCESS)
    assert 'MAPI_E_INTERFACE_NOT_SUPPORTED' in str(excinfo)


def test_get_searchpath(addressbook):
    searchpath = addressbook.GetSearchPath(MAPI_UNICODE)
    # should only contain Global Address Book
    assert len(searchpath) == 1
    assert PpropFindProp(searchpath[0], PR_DISPLAY_NAME_W) == SPropValue(0x3001001F, 'Global Address Book')
