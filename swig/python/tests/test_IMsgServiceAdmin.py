import os

import pytest

from MAPI.Struct import SPropValue, MAPIError
from MAPI.Tags import (PpropFindProp, PR_SERVICE_UID, PR_EC_PATH,
                       PR_EC_USERNAME, PR_EC_USERPASSWORD)

from conftest import kopanoserver


def test_createmsgservice_null(adminservice):
    with pytest.raises(MAPIError) as excinfo:
        adminservice.CreateMsgService(None, None, 0, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_createmsgservice_empty(adminservice):
    with pytest.raises(MAPIError) as excinfo:
        adminservice.CreateMsgService(b'', None, 0, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_createmsgservice_null2(adminservice):
    with pytest.raises(MAPIError) as excinfo:
        adminservice.CreateMsgService(b'ZARAFA6', None, 0, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_createmsgservice(adminservice):
    assert adminservice.CreateMsgService(b'ZARAFA6', b'Zarafa', 0, 0) is None


def test_createmsgservice_delete(adminservice):
    adminservice.CreateMsgService(b'ZARAFA6', b'Zarafa', 0, 0) is None
    table = adminservice.GetMsgServiceTable(0)
    rows = table.QueryRows(1, 0)
    prop = PpropFindProp(rows[0], PR_SERVICE_UID)

    adminservice.DeleteMsgService(prop.Value)
    table = adminservice.GetMsgServiceTable(0)
    rows = table.QueryRows(1, 0)
    assert len(rows) == 0


def test_getmsgservicetable(adminservice):
    assert adminservice.GetMsgServiceTable(0)


def test_deletemsgservice_null(adminservice):
    with pytest.raises(MAPIError) as excinfo:
        adminservice.DeleteMsgService(None)
    assert 'MAPI_E_NOT_FOUND' in str(excinfo.value)


def test_deletemsgservice_nonexistant(adminservice):
    with pytest.raises(MAPIError) as excinfo:
        adminservice.DeleteMsgService(b'1234567890123456')
    assert 'MAPI_E_NOT_FOUND' in str(excinfo.value)


def test_deletemsgservice_bad(adminservice):
    with pytest.raises(TypeError) as excinfo:
        adminservice.DeleteMsgService(b'1223')
    assert "argument 2 of type 'LPMAPIUID'" in str(excinfo.value)


def test_configuremsgservice_null(adminservice):
    with pytest.raises(MAPIError) as excinfo:
        adminservice.ConfigureMsgService(None, 0, 0, None)
    assert "MAPI_E_INVALID_PARAMETER" in str(excinfo)


def test_configuremsgservice_nullprops(adminservice):
    with pytest.raises(TypeError) as excinfo:
        adminservice.ConfigureMsgService(b'foo', 0, 0, None)
    assert "argument 2 of type 'LPMAPIUID'" in str(excinfo)


@kopanoserver
def test_configuremsgservice(adminservice):
    adminservice.CreateMsgService(b"ZARAFA6", b"Zarafa", 0, 0)
    table = adminservice.GetMsgServiceTable(0)
    rows = table.QueryRows(1, 0)
    prop = PpropFindProp(rows[0], PR_SERVICE_UID)
    uid = prop.Value

    props = [SPropValue(PR_EC_PATH, os.getenv('KOPANO_TEST_SERVER').encode()),
             SPropValue(PR_EC_USERNAME, os.getenv('KOPANO_TEST_USER').encode()),
             SPropValue(PR_EC_USERPASSWORD, os.getenv('KOPANO_TEST_PASSWORD').encode())]

    # TODO: Test if no exception is raised
    adminservice.ConfigureMsgService(uid, 0, 0, props)
