import os

import pytest

from MAPI import PT_STRING8, MAPI_MODIFY, MAPI_UNICODE, MAPI_E_COMPUTED
from MAPI.Struct import (MAPIError, MAPIErrorInvalidParameter, SPropValue,
                         SPropertyRestriction, PROP_TYPE)
from MAPI.Tags import (PR_ENTRYID, PR_EMS_AB_PROXY_ADDRESSES, PR_ACCOUNT,
                       PR_TEST_LINE_SPEED, PR_EC_STATSTABLE_USERS, PR_EC_STATSTABLE_SESSIONS,
                       PR_EC_STATSTABLE_SYSTEM, PR_EC_STATSTABLE_COMPANY,
                       PR_MESSAGE_SIZE_EXTENDED, PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME,
                       PR_SUBJECT, PR_STORE_RECORD_KEY, PR_MESSAGE_CLASS, PR_NULL,
                       PR_OBJECT_TYPE,
                       IID_IMessage, IID_IMAPIFolder)
from MAPI.Util import PROP_TAG

PR_TEST_PROP = PROP_TAG(PT_STRING8, 0x6601)


if not os.getenv('KOPANO_SOCKET'):
    pytest.skip('No kopano-server running', allow_module_level=True)


def test_deleteprops_paramater(store):
    with pytest.raises(MAPIErrorInvalidParameter) as excinfo:
        store.DeleteProps(None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_compareentryids_parameter(store):
    with pytest.raises(MAPIErrorInvalidParameter) as excinfo:
        store.CompareEntryIDs(None, None, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_openproperty(store):
    with pytest.raises(MAPIErrorInvalidParameter) as excinfo:
        store.OpenProperty(0, None, 0, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_abortsubmit(store):
    with pytest.raises(MAPIErrorInvalidParameter) as excinfo:
        store.AbortSubmit(None, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_setlockstate(store):
    with pytest.raises(MAPIErrorInvalidParameter) as excinfo:
        store.SetLockState(None, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_finishedmsg(store):
    with pytest.raises(MAPIErrorInvalidParameter) as excinfo:
        store.FinishedMsg(0, None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_notifynewmail(store):
    with pytest.raises(MAPIErrorInvalidParameter) as excinfo:
        store.NotifyNewMail(None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_prophandler(store):
    proptags = [PR_TEST_LINE_SPEED, PR_EC_STATSTABLE_USERS, PR_EC_STATSTABLE_SESSIONS,
                PR_EC_STATSTABLE_SYSTEM, PR_EC_STATSTABLE_COMPANY, PR_MESSAGE_SIZE_EXTENDED]

    props = store.GetProps(proptags, 0)
    for i in range(0, len(proptags)):
        assert props[i].ulPropTag == proptags[i]


def test_timprops(store):
    props = store.GetProps([PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME], 0)
    assert props[0].ulPropTag == PR_LAST_MODIFICATION_TIME
    assert props[0].Value.unixtime > 0
    assert props[1].ulPropTag == PR_CREATION_TIME
    assert props[1].Value.unixtime > 0


def test_openentry_noaccess(root):
    with pytest.raises(MAPIError) as excinfo:
        root.SetProps([SPropValue(PR_TEST_PROP, b'Test')])
    assert 'MAPI_E_NO_ACCESS' in str(excinfo.value)


def test_openentry_not_supported(store):
    with pytest.raises(MAPIError) as excinfo:
        store.OpenEntry(None, IID_IMessage, MAPI_MODIFY)
    assert 'MAPI_E_INTERFACE_NOT_SUPPORTED' in str(excinfo.value)


def test_openentry(store):
    root = store.OpenEntry(None, IID_IMAPIFolder, MAPI_MODIFY)
    assert root

    root.SetProps([SPropValue(PR_TEST_PROP, b'Test')])
    root.SaveChanges(0)
    # no mapi errors


def test_openproperty():
    # TODO: requires admin user
    pass


def test_compareentryids(store, root):
    guid1 = b"\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x04"
    guid2 = b"\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x05"

    props = root.GetProps([PR_ENTRYID, PR_STORE_RECORD_KEY], 0)
    entryid = props[0].Value
    storeguid = props[1].Value

    assert not store.CompareEntryIDs(b"\x00\x00\x00\x00", b"\x00\x00\x00\x00", 0)
    assert not store.CompareEntryIDs(b"\x00\x00\x00\x00"+guid1, b"\x00\x00\x00\x00"+guid2, 0)
    assert not store.CompareEntryIDs(b"\x00\x00\x00\x00"+storeguid, b"\x00\x00\x00\x00"+storeguid, 0)
    assert store.CompareEntryIDs(entryid, entryid, 0)


def test_getreceivefolder(store):
    rfdata1 = store.GetReceiveFolder(u'IPM.Note', MAPI_UNICODE)
    rfdata2 = store.GetReceiveFolder(u'IPM', MAPI_UNICODE)
    rfdata3 = store.GetReceiveFolder(u'', MAPI_UNICODE)

    assert store.CompareEntryIDs(rfdata1[0], rfdata2[0], 0)
    assert store.CompareEntryIDs(rfdata2[0], rfdata3[0], 0)

    assert rfdata1[1] == 'IPM'
    assert rfdata2[1] == 'IPM'
    assert rfdata3[1] == ''


def test_setreceivefolder(store):
    root = store.OpenEntry(None, None, MAPI_MODIFY)
    entryid = root.GetProps([PR_ENTRYID], 0)[0].Value
    store.SetReceiveFolder(u'ÌPM.Ñotè.string'.encode('utf8'), 0, entryid)
    store.SetReceiveFolder(u'ÌPM.Ñotè.unicode', MAPI_UNICODE, entryid)

    rfdata = store.GetReceiveFolder(u'ÌPM.Ñotè.unicode', MAPI_UNICODE)
    result = store.CompareEntryIDs(entryid, rfdata[0], 0)

    assert result
    assert rfdata[1] == u'ÌPM.Ñotè.unicode'

    rfdata = store.GetReceiveFolder(u'ÌPM.Ñotè.string'.encode('utf8'), 0)
    result = store.CompareEntryIDs(entryid, rfdata[0], 0)

    assert result
    assert rfdata[1] == u'ÌPM.Ñotè.string'


def test_getreceivefoldertable(store):
    table = store.GetReceiveFolderTable(0)
    table.SetColumns([PR_MESSAGE_CLASS], 0)

    for row in table.QueryRows(0xFFF, 0):
        assert PROP_TYPE(row[0].ulPropTag) == PT_STRING8


def test_storelogoff(store):
    store.StoreLogoff(0)


def test_deleteprops(store):
    store.SetProps([SPropValue(0x6001001F, u'test')])
    problem = store.DeleteProps([0x6001001F])
    props = store.GetProps([0x6001001F], 0)

    assert props[0].ulPropTag == 0x6001000A
    assert problem is None


def test_deleteprops_problem(store):
    problem = store.DeleteProps([PR_ENTRYID, PR_NULL, PR_OBJECT_TYPE])

    assert problem[0].ulIndex == 0
    assert problem[0].ulPropTag == PR_ENTRYID
    assert problem[0].scode == MAPI_E_COMPUTED

    assert problem[1].ulIndex == 1
    assert problem[1].ulPropTag == PR_NULL
    assert problem[1].scode == MAPI_E_COMPUTED

    assert problem[2].ulIndex == 2
    assert problem[2].ulPropTag == PR_OBJECT_TYPE
    assert problem[2].scode == MAPI_E_COMPUTED


def test_getoutgoingqueue(store):
    table = store.GetOutgoingQueue(0)
    assert table.GetRowCount(0) == 0
