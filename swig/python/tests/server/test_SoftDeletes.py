from MAPI import RELOP_EQ, KEEP_OPEN_READWRITE
from MAPI.Struct import SPropertyRestriction, PpropFindProp, SPropValue
from MAPI.Tags import PR_ENTRYID, PR_SUBJECT, PR_DELETED_ON, SHOW_SOFT_DELETES


def test_deletedontimestamp(root, message):
    message.SetProps([SPropValue(PR_SUBJECT, b'Test')])
    message.SaveChanges(KEEP_OPEN_READWRITE)
    eid = message.GetProps([PR_ENTRYID], 0)[0]
    root.DeleteMessages([eid.Value], 0, None, 0)

    table = root.GetContentsTable(SHOW_SOFT_DELETES)
    table.SetColumns([PR_SUBJECT, PR_ENTRYID, PR_DELETED_ON], 0)
    table.FindRow(SPropertyRestriction(RELOP_EQ, PR_ENTRYID, eid), 0, 0)
    rows = table.QueryRows(1, 0)

    assert PpropFindProp(rows[0], PR_DELETED_ON)

    # Clean up message
    root.DeleteMessages([rows[0][1].Value], 0, None, 0)
