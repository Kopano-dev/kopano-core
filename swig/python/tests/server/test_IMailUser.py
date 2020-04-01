import os

import pytest

from MAPI import RELOP_EQ, MAPI_E_NOT_FOUND
from MAPI.Util import SPropertyRestriction, SPropValue
from MAPI.Tags import (PR_ENTRYID, PR_EMS_AB_PROXY_ADDRESSES, PR_EMS_AB_PROXY_ADDRESSES_A,
                       PR_EMS_AB_PROXY_ADDRESSES_W, PR_ACCOUNT, PR_EMAIL_ADDRESS)


def get_smtp_address():
    username = os.getenv('KOPANO_TEST_USER')
    fqdn = os.getenv('FQDN')
    return 'SMTP:%s@%s' % (username, fqdn)


def set_restriction(table):
    username = os.getenv('KOPANO_TEST_USER').encode()
    table.Restrict(SPropertyRestriction(RELOP_EQ, PR_ACCOUNT, SPropValue(PR_ACCOUNT, username)), 0)


def test_proxyaddresses(addressbook, gabtable):
    gabtable.SetColumns([PR_ENTRYID], 0)
    rows = gabtable.QueryRows(1, 0)

    assert len(rows) == 1

    entryid = rows[0][0].Value
    mailuser = addressbook.OpenEntry(entryid, None, 0)
    props = mailuser.GetProps([PR_EMS_AB_PROXY_ADDRESSES], 0)
    assert props


def test_proxyaddresses_wrongtype(gabtable):
    gabtable.SetColumns([0x800f001e], 0)

    set_restriction(gabtable)
    rows = gabtable.QueryRows(1, 0)
    assert rows[0][0] == SPropValue(0x800f000a, MAPI_E_NOT_FOUND)


@pytest.mark.parametrize("proptag,value", [(PR_EMS_AB_PROXY_ADDRESSES_W, get_smtp_address()), (PR_EMS_AB_PROXY_ADDRESSES_A, get_smtp_address().encode())])
def test_proxyaddresses_unicode(gabtable, proptag, value):
    gabtable.SetColumns([proptag], 0)
    set_restriction(gabtable)
    rows = gabtable.QueryRows(1, 0)
    # All test users have kopanoAliasses
    assert value in rows[0][0].Value


def test_props(gabtable):
    username = os.getenv('KOPANO_TEST_USER').encode()

    set_restriction(gabtable)
    gabtable.SetColumns([PR_ACCOUNT, PR_EMAIL_ADDRESS], 0)
    rows = gabtable.QueryRows(1, 0)
    assert rows[0][0] == SPropValue(PR_ACCOUNT, username)
    assert rows[0][1] == SPropValue(PR_EMAIL_ADDRESS, username)
