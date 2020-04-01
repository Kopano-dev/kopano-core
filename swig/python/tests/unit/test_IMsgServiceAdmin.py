import pytest

from MAPI.Struct import MAPIError


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
