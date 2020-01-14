import os

import pytest

from MAPI import RELOP_EQ
from MAPI.Struct import SPropertyRestriction, PpropFindProp
from MAPI.Tags import PR_ENTRYID, PR_SUBJECT, PR_DELETED_ON, SHOW_SOFT_DELETES


if not os.getenv('KOPANO_TEST_SERVER'):
    pytest.skip('No kopano-server running', allow_module_level=True)


def test_deletedontimestamp(root, message):
    eid = message.GetProps([PR_ENTRYID], 0)[0]
    root.DeleteMessages([eid.Value], 0, None, 0)

    table = root.GetContentsTable(SHOW_SOFT_DELETES)
    table.SetColumns([PR_SUBJECT, PR_ENTRYID, PR_DELETED_ON], 0)
    table.FindRow(SPropertyRestriction(RELOP_EQ, PR_ENTRYID, eid), 0, 0)
    rows = table.QueryRows(1, 0)

    assert PpropFindProp(rows[0], PR_DELETED_ON)
