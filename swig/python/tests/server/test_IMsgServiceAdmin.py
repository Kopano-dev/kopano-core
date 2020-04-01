import os

from MAPI.Struct import SPropValue
from MAPI.Tags import (PpropFindProp, PR_SERVICE_UID, PR_EC_PATH,
                       PR_EC_USERNAME, PR_EC_USERPASSWORD)


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


def test_configuremsgservice(adminservice):
    adminservice.CreateMsgService(b"ZARAFA6", b"Zarafa", 0, 0)
    table = adminservice.GetMsgServiceTable(0)
    rows = table.QueryRows(1, 0)
    prop = PpropFindProp(rows[0], PR_SERVICE_UID)
    uid = prop.Value

    props = [SPropValue(PR_EC_PATH, os.getenv('KOPANO_SOCKET').encode()),
             SPropValue(PR_EC_USERNAME, os.getenv('KOPANO_TEST_USER').encode()),
             SPropValue(PR_EC_USERPASSWORD, os.getenv('KOPANO_TEST_PASSWORD').encode())]

    # TODO: Test if no exception is raised
    adminservice.ConfigureMsgService(uid, 0, 0, props)
