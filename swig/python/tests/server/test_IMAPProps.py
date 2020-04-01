'''
Tests for PR_EC_IMAP_ID generation, requires an IMAP user
'''

from MAPI import RELOP_EQ, KEEP_OPEN_READWRITE, MESSAGE_MOVE
from MAPI.Util import SPropValue, SPropertyRestriction
from MAPI.Tags import (PR_ENTRYID, PR_SUBJECT, PR_EC_IMAP_ID, PURGE_CACHE_CELL,
                       IID_IMAPIFolder)


def test_new(inbox, emptyemail, serviceadmin):
    emptyemail.SetProps([SPropValue(PR_SUBJECT, b'test')])
    emptyemail.SaveChanges(0)

    entryid = emptyemail.GetProps([PR_ENTRYID], 0)[0].Value
    table = inbox.GetContentsTable(0)
    table.SetColumns([PR_EC_IMAP_ID], 0)
    table.Restrict(SPropertyRestriction(RELOP_EQ, PR_ENTRYID, SPropValue(PR_ENTRYID, entryid)), 0)
    rows = table.QueryRows(1, 0)
    assert len(rows) == 1

    imap_id_0 = rows[0][0]
    assert imap_id_0.ulPropTag == PR_EC_IMAP_ID

    # Retry without cache for #7483 (Might purge the wrong cache in cluster tests)
    serviceadmin.PurgeCache(PURGE_CACHE_CELL)
    table = inbox.GetContentsTable(0)
    table.SetColumns([PR_EC_IMAP_ID], 0)
    table.Restrict(SPropertyRestriction(RELOP_EQ, PR_ENTRYID, SPropValue(PR_ENTRYID, entryid)), 0)
    rows = table.QueryRows(1, 0)
    assert len(rows) == 1

    imap_id_1 = rows[0][0]
    assert imap_id_1.ulPropTag == PR_EC_IMAP_ID
    assert imap_id_0 == imap_id_1


def test_move(store, inbox, folder, emptyemail, serviceadmin):
    emptyemail.SetProps([SPropValue(PR_SUBJECT, b'test')])
    emptyemail.SaveChanges(KEEP_OPEN_READWRITE)
    entryid = emptyemail.GetProps([PR_ENTRYID], 0)[0].Value
    emptyemail.SetProps([SPropValue(0x66010102, entryid)])
    emptyemail.SaveChanges(0)
    emptyemail = store.OpenEntry(entryid, None, 0)
    uid = emptyemail.GetProps([PR_EC_IMAP_ID], 0)[0].Value
    inbox.CopyMessages([entryid], IID_IMAPIFolder, folder, 0, None, MESSAGE_MOVE)
    table = folder.GetContentsTable(0)
    table.SetColumns([PR_EC_IMAP_ID], 0)
    table.Restrict(SPropertyRestriction(RELOP_EQ, 0x66010102, SPropValue(0x66010102, entryid)), 0)
    rows = table.QueryRows(1, 0)

    assert len(rows) == 1

    imap_id_0 = rows[0][0]
    assert imap_id_0.ulPropTag == PR_EC_IMAP_ID
    # PR_EC_IMAP_ID must be higher after move
    assert imap_id_0.Value > uid

    # Retry without cache for #7483 (Might purge the wrong cache in cluster tests)
    serviceadmin.PurgeCache(PURGE_CACHE_CELL)
    table = folder.GetContentsTable(0)
    table.SetColumns([PR_EC_IMAP_ID], 0)
    table.Restrict(SPropertyRestriction(RELOP_EQ, 0x66010102, SPropValue(0x66010102, entryid)), 0)
    rows = table.QueryRows(1, 0)
    assert len(rows) == 1

    imap_id_1 = rows[0][0]
    assert imap_id_1.ulPropTag == PR_EC_IMAP_ID
    # PR_EC_IMAP_ID must be higher after move
    assert imap_id_1.Value > uid
