import pytest


from MAPI.Tags import PR_ENTRYID, PR_DISPLAY_NAME, PR_MESSAGE_SIZE_EXTENDED
from MAPI.Util import MAPIError


@pytest.mark.parametrize('storedn, mboxdn, flags', [(None, None, 0), (b'', None, 0), (b'', None, 0)])
def test_createstore_entryid(ema, storedn, mboxdn, flags):
    with pytest.raises(MAPIError) as excinfo:
        ema.CreateStoreEntryID(storedn, mboxdn, flags)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


@pytest.mark.parametrize('sourcekeyfolder,sourcekeymessage', [(None, None), (None, b'1234567890123456')])
def test_entryidfromsourcekey(ema, sourcekeyfolder, sourcekeymessage):
    with pytest.raises(MAPIError) as excinfo:
        ema.EntryIDFromSourceKey(sourcekeyfolder, sourcekeymessage)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


def test_entryidfromsourcekey_not_found(ema):
    with pytest.raises(MAPIError) as excinfo:
        ema.EntryIDFromSourceKey(b'1234567890123456', None)
    assert 'MAPI_E_NOT_FOUND' in str(excinfo)


def test_getrights(ema):
    with pytest.raises(MAPIError) as excinfo:
        ema.GetRights(None, None)
    assert 'MAPI_E_NOT_FOUND' in str(excinfo)


def test_getpublicfoldertable(ema):
    with pytest.raises(MAPIError) as excinfo:
        ema.GetPublicFolderTable(None, 0)
    assert 'MAPI_E_NOT_FOUND' in str(excinfo)


def test_getmailboxtable(ema):
    table = ema.GetMailboxTable(None, 0)
    table.SetColumns([PR_DISPLAY_NAME, PR_MESSAGE_SIZE_EXTENDED], 0)
    assert table.QueryRows(100, 0)


def test_getmailboxtable_invalidservername(ema):
    table = ema.GetMailboxTable(b'Unknown server test', 0)
    table.SetColumns([PR_DISPLAY_NAME, PR_MESSAGE_SIZE_EXTENDED], 0)
    assert table.QueryRows(100, 0)


def test_getmailboxtable_validservername(ema):
    table = ema.GetMailboxTable(b'server1', 0)
    table.SetColumns([PR_DISPLAY_NAME, PR_MESSAGE_SIZE_EXTENDED], 0)
    assert table.QueryRows(100, 0)


def test_usemailboxtableentryid(adminsession, ema):
    table = ema.GetMailboxTable(None, 0)
    table.SetColumns([PR_ENTRYID], 0)
    rows = table.QueryRows(1, 0)
    assert rows
    assert adminsession.OpenMsgStore(0, rows[0][0].Value, None, 0)
