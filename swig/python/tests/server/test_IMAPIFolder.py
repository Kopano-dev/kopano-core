import time

import pytest

from MAPI import (FOLDER_ROOT, FOLDER_GENERIC, FOLDER_SEARCH, RELOP_EQ, CONVENIENT_DEPTH,
                  MAPI_UNICODE, OPEN_IF_EXISTS)
from MAPI.Struct import MAPIError
from MAPI.Tags import (PR_CONTAINER_CONTENTS, PR_FOLDER_ASSOCIATED_CONTENTS, PR_CONTAINER_HIERARCHY,
                       PR_COLLECTOR, PR_HIERARCHY_SYNCHRONIZER, PR_CONTENTS_SYNCHRONIZER, PR_ACL_TABLE,
                       PR_RULES_TABLE, PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME, PR_COMMENT,
                       PR_DISPLAY_NAME, PR_FOLDER_TYPE, PR_ENTRYID, PR_PARENT_ENTRYID,
                       PR_OBJECT_TYPE, PR_RECORD_KEY, PR_STORE_RECORD_KEY,
                       PR_STORE_ENTRYID, PR_EC_HIERARCHYID, PR_SUBFOLDERS,
                       PT_ERROR,
                       IID_IMAPITable, IID_IExchangeImportContentsChanges, IID_IExchangeImportHierarchyChanges,
                       IID_IExchangeModifyTable, IID_IExchangeExportChanges, IID_IMessage)
from MAPI.Util import PROP_TYPE, SPropValue, SPropertyRestriction


def test_openproperty_exc(folder):
    with pytest.raises(MAPIError) as excinfo:
        folder.OpenProperty(0, None, 0, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


def test_deleteprops_exc(folder):
    with pytest.raises(MAPIError) as excinfo:
        folder.DeleteProps(None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


def test_setsearchcriteria_exc(folder):
    with pytest.raises(MAPIError) as excinfo:
        folder.SetSearchCriteria(None, None, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


def test_createfolder_exc(folder):
    with pytest.raises(MAPIError) as excinfo:
        folder.CreateFolder(0, None, None, None, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


def test_getmessagestatus_exc(folder):
    with pytest.raises(MAPIError) as excinfo:
        folder.GetMessageStatus(None, 0)
    assert 'MAPI_E_INVALID_ENTRYID' in str(excinfo)


def test_setmessagestatus_exc(folder):
    with pytest.raises(MAPIError) as excinfo:
        folder.SetMessageStatus(None, 0, 0)
    assert 'MAPI_E_INVALID_ENTRYID' in str(excinfo)


def test_openproperty(folder):
    objectprops = [{'tag': PR_CONTAINER_CONTENTS, 'iid': IID_IMAPITable},
                   {'tag': PR_FOLDER_ASSOCIATED_CONTENTS, 'iid': IID_IMAPITable},
                   {'tag': PR_CONTAINER_HIERARCHY, 'iid': IID_IMAPITable},
                   {'tag': PR_COLLECTOR, 'iid': IID_IExchangeImportHierarchyChanges},
                   {'tag': PR_COLLECTOR, 'iid': IID_IExchangeImportContentsChanges},
                   {'tag': PR_HIERARCHY_SYNCHRONIZER, 'iid': IID_IExchangeExportChanges},
                   {'tag': PR_CONTENTS_SYNCHRONIZER, 'iid': IID_IExchangeExportChanges},
                   {'tag': PR_ACL_TABLE, 'iid': IID_IExchangeModifyTable},
                   {'tag': PR_RULES_TABLE, 'iid': IID_IExchangeModifyTable}]

    for prop in objectprops:
        assert folder.OpenProperty(prop['tag'], prop['iid'], 0, 0)


def test_props(folder):
    props = folder.GetProps([PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME, PR_COMMENT, PR_DISPLAY_NAME], 0)
    now = time.time()

    assert props[0].ulPropTag == PR_LAST_MODIFICATION_TIME
    assert now > props[0].Value.unixtime > (now - 60)

    assert props[1].ulPropTag == PR_CREATION_TIME
    assert now > props[1].Value.unixtime > (now - 60)

    assert props[0].Value == props[1].Value

    assert not props[2].Value
    assert props[3].Value == b'subfolder'


def test_root(root):
    assert root.GetProps([PR_FOLDER_TYPE], 0)[0].Value == FOLDER_ROOT


def test_props_update(root, folder):
    oldprops = folder.GetProps([PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME, PR_ENTRYID], 0)
    folderid = oldprops[2].Value

    folder.SetProps([SPropValue(0x6601001e, b'test')])
    folder = root.OpenEntry(folderid, None, 0)
    props = folder.GetProps([PR_LAST_MODIFICATION_TIME, PR_CREATION_TIME], 0)
    assert props[0].ulPropTag == PR_LAST_MODIFICATION_TIME
    assert props[0].Value >= oldprops[0].Value
    assert props[1].ulPropTag == PR_CREATION_TIME
    assert props[1].Value == oldprops[1].Value


def test_nontransacted():
    # TODO: requires notifications
    pass


def test_required_properties(folder):
    proptags = [
                PR_DISPLAY_NAME,
                PR_ENTRYID,
                PR_FOLDER_TYPE,
                PR_OBJECT_TYPE,
                PR_PARENT_ENTRYID,
                PR_RECORD_KEY,
                PR_STORE_ENTRYID,
                PR_STORE_RECORD_KEY,
                PR_CONTAINER_CONTENTS,
                PR_CONTAINER_HIERARCHY,
                PR_EC_HIERARCHYID]

    for prop in folder.GetProps(proptags, 0):
        assert PROP_TYPE(prop.ulPropTag) != PT_ERROR


def test_subfolder(store, inbox, folder):
    assert not folder.GetProps([PR_SUBFOLDERS], 0)[0].Value
    assert not inbox.GetProps([PR_SUBFOLDERS], 0)[0].Value

    # Open inbox, to retrieve new value for PR_SUBFOLDERS
    inboxeid = inbox.GetProps([PR_ENTRYID], 0)[0].Value
    inbox = store.OpenEntry(inboxeid, None, 0)
    assert inbox.GetProps([PR_SUBFOLDERS], 0)[0].Value


def test_createmesage(store, message, assocmessage):
    entryid = message.GetProps([PR_ENTRYID], 0)[0].Value
    message.SaveChanges(0)
    assert store.OpenEntry(entryid, IID_IMessage, 0)

    entryidass = assocmessage.GetProps([PR_ENTRYID], 0)[0].Value
    assocmessage.SaveChanges(0)
    assert store.OpenEntry(entryidass, IID_IMessage, 0)


def test_createfolder_collision(inbox, folder):
    props = folder.GetProps([PR_DISPLAY_NAME, PR_COMMENT], 0)
    with pytest.raises(MAPIError) as excinfo:
        inbox.CreateFolder(FOLDER_GENERIC, props[0].Value, props[1].Value, None, 0)
    assert 'MAPI_E_COLLISION' in str(excinfo)


def test_createfolder_open_if_exists(inbox, folder):
    props = folder.GetProps([PR_DISPLAY_NAME, PR_COMMENT, PR_ENTRYID], 0)
    folder1 = inbox.CreateFolder(FOLDER_GENERIC, props[0].Value, props[1].Value, None, OPEN_IF_EXISTS)

    assert folder1.GetProps([PR_ENTRYID], 0)[0].Value == props[2].Value


# Assert FOLDER_TYPE set correctly in HierarchyTable and Properties
def assert_folder_type(root, folderid, foldertype):
    proptags = [PR_FOLDER_TYPE]
    folder = root.OpenEntry(folderid, None, 0)
    assert folder.GetProps(proptags, 0)[0].Value == foldertype

    table = root.GetHierarchyTable(CONVENIENT_DEPTH)
    table.Restrict(SPropertyRestriction(RELOP_EQ, PR_ENTRYID, SPropValue(PR_ENTRYID, folderid)), 0)
    table.SetColumns(proptags, 0)
    rows = table.QueryRows(10, 0)
    assert rows[0][0].Value == foldertype


def test_generic_foldertype(root, folder):
    folderid = folder.GetProps([PR_ENTRYID], 0)[0].Value
    assert_folder_type(root, folderid, FOLDER_GENERIC)


def test_search_foldertype(root, searchfolder):
    folderid = searchfolder.GetProps([PR_ENTRYID], 0)[0].Value
    assert_folder_type(root, folderid, FOLDER_SEARCH)
