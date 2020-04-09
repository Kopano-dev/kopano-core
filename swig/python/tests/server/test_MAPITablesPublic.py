from MAPI import MAPI_UNICODE
from MAPI.Struct import SPropValue
from MAPI.Tags import (PR_OBJECT_TYPE, PR_ENTRYID, PR_DISPLAY_NAME, PR_DEPTH,
                       PR_INSTANCE_KEY, PR_DISPLAY_TYPE, PR_CONTENT_UNREAD,
                       PR_CONTENT_COUNT, PR_FOLDER_TYPE, PR_IPM_SUBTREE_ENTRYID)


def assert_required_table_props(folder):
    table = folder.GetHierarchyTable(MAPI_UNICODE)
    proptag = [PR_OBJECT_TYPE, PR_ENTRYID, PR_DISPLAY_NAME, PR_DEPTH, PR_DISPLAY_TYPE,
               PR_INSTANCE_KEY, PR_CONTENT_UNREAD, PR_CONTENT_COUNT, PR_FOLDER_TYPE]

    table.SetColumns(proptag, 0)
    for row in table.QueryRows(0xFFFF, 0):
        for i in range(0, len(proptag)):
            assert row[i].ulPropTag == proptag[i]


def test_rootprops(publicroot):
    assert_required_table_props(publicroot)


def test_ipmsubtreeprops(publicsubtree):
    assert_required_table_props(publicsubtree)


def test_tables(publicsubtree):
    table = publicsubtree.GetHierarchyTable(MAPI_UNICODE)
    table.SetColumns([PR_DISPLAY_NAME], 0)
    rows = table.QueryRows(0xFFFF, 0)

    assert rows[0][0] == SPropValue(PR_DISPLAY_NAME, b'Favorites')
    assert rows[1][0] == SPropValue(PR_DISPLAY_NAME, b'Public Folders')

