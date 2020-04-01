import os

import pytest

from MAPI.Tags import PR_ENTRYID, PR_EMS_AB_PROXY_ADDRESSES


if not os.getenv('KOPANO_SOCKET'):
    pytest.skip('No kopano-server running', allow_module_level=True)


def test_proxyaddresses(addressbook, gab):
    table = gab.GetContentsTable(0)
    table.SetColumns([PR_ENTRYID], 0)
    rows = table.QueryRows(1, 0)

    assert len(rows) == 1

    entryid = rows[0][0].Value
    mailuser = addressbook.OpenEntry(entryid, None, 0)
    props = mailuser.GetProps([PR_EMS_AB_PROXY_ADDRESSES], 0)
    assert len(props) == 1
