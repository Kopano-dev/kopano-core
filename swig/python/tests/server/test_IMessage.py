import locale

import pytest

from MAPI import (MODRECIP_ADD, MODRECIP_MODIFY, MODRECIP_REMOVE, ATTACH_BY_VALUE, KEEP_OPEN_READWRITE,
                  MAPI_UNICODE, MAPI_SEND_NO_RICH_INFO, MAPI_TO, MAPI_MODIFY, MAPI_DEFERRED_ERRORS,
                  STGM_WRITE, MAPI_CREATE, WrapCompressedRTFStream,
                  MAPI_BEST_ACCESS)
from MAPI.Struct import MAPIError, SPropValue, PROP_TYPE
from MAPI.Tags import (PR_SUBJECT_A, PR_MESSAGE_CLASS_A, PR_DISPLAY_NAME_A,
                       PR_DISPLAY_NAME_W, PR_ATTACH_METHOD, PR_ATTACH_DATA_BIN,
                       PR_SEARCH_KEY, PR_ENTRYID, PR_EMAIL_ADDRESS_W,
                       PR_ADDRTYPE_A, PR_ADDRTYPE_W, PR_RECIPIENT_TYPE,
                       PR_SEND_RICH_INFO, PR_EMAIL_ADDRESS_A, PR_DISPLAY_TYPE,
                       PR_OBJECT_TYPE, PR_ROWID, PR_CREATION_TIME, PR_DISPLAY_CC,
                       PR_DISPLAY_TO, PR_DISPLAY_BCC, PR_ENTRYID,
                       PR_LAST_MODIFICATION_TIME, PR_MESSAGE_ATTACHMENTS,
                       PR_MESSAGE_CLASS, PR_MESSAGE_FLAGS, PR_MESSAGE_RECIPIENTS,
                       PR_NORMALIZED_SUBJECT, PR_PARENT_ENTRYID, PR_RECORD_KEY,
                       PR_STORE_ENTRYID, PR_STORE_RECORD_KEY, PR_EC_HIERARCHYID,
                       PR_SUBJECT, PT_ERROR, PR_BODY, PR_HTML,
                       PR_RTF_COMPRESSED, PR_RTF_IN_SYNC, PR_BODY_W,
                       PR_INTERNET_CPID, PR_ATTACH_DATA_OBJ,
                       IID_IMessage, IID_IStream)


def assert_attachment_count(message, copy):
    table = message.GetAttachmentTable(MAPI_DEFERRED_ERRORS)
    tablecopy = message.GetAttachmentTable(MAPI_DEFERRED_ERRORS)

    assert table.GetRowCount(0) == tablecopy.GetRowCount(0)


def assert_recipient_count(message, copy):
    table = message.GetRecipientTable(MAPI_DEFERRED_ERRORS)
    tablecopy = message.GetRecipientTable(MAPI_DEFERRED_ERRORS)

    assert table.GetRowCount(0) == tablecopy.GetRowCount(0)


@pytest.fixture
def make_recip_smtp_entry():
    def _make_recip_smtp_entry(abook, flag, recipname, recipemail, reciptype):
        props = []
        props.append(SPropValue(PR_SEND_RICH_INFO, False))
        props.append(SPropValue(PR_DISPLAY_TYPE, 0))
        props.append(SPropValue(PR_OBJECT_TYPE, 6))
        props.append(SPropValue(PR_RECIPIENT_TYPE, reciptype))
        if isinstance(recipemail, bytes):
            props.append(SPropValue(PR_SEARCH_KEY, b'SMTP:'+recipemail))
        else:
            props.append(SPropValue(PR_SEARCH_KEY, b'SMTP:'+recipemail.encode('ascii')))
        if flag & MAPI_UNICODE:
            props.append(SPropValue(PR_ENTRYID, abook.CreateOneOff(recipname, u"SMTP", recipemail, MAPI_SEND_NO_RICH_INFO | MAPI_UNICODE)))
        else:
            props.append(SPropValue(PR_ENTRYID, abook.CreateOneOff(recipname, b"SMTP", recipemail, MAPI_SEND_NO_RICH_INFO)))
        if (flag == MAPI_UNICODE):
            props.append(SPropValue(PR_EMAIL_ADDRESS_W, recipemail))
            props.append(SPropValue(PR_ADDRTYPE_W, u'SMTP'))
            props.append(SPropValue(PR_DISPLAY_NAME_W, recipname))
        else:
            props.append(SPropValue(PR_EMAIL_ADDRESS_A, recipemail))
            props.append(SPropValue(PR_ADDRTYPE_A, b'SMTP'))
            props.append(SPropValue(PR_DISPLAY_NAME_A, recipname))

        return props
    return _make_recip_smtp_entry


@pytest.fixture
def make_embedded_attachment():
    def _make_embedded(message, level):
        if level == 0:
            return
        attach = message.CreateAttach(None, 0)[1]
        attach.SetProps([SPropValue(PR_ATTACH_METHOD, 5)])
        sub = attach.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, STGM_WRITE, MAPI_CREATE | MAPI_MODIFY)
        sub.SetProps([SPropValue(PR_SUBJECT, b'test')])
        _make_embedded(sub, level-1)
        sub.SaveChanges(0)
        attach.SaveChanges(0)
    return _make_embedded


def test_modifyrecipients(message):
    with pytest.raises(MAPIError) as excinfo:
        message.ModifyRecipients(0, None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
        message.ModifyRecipients(MODRECIP_ADD, None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
        message.ModifyRecipients(MODRECIP_MODIFY, None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)

    with pytest.raises(MAPIError) as excinfo:
        message.ModifyRecipients(MODRECIP_REMOVE, None)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo)


@pytest.fixture
def defaultmessage(addressbook, message, make_recip_smtp_entry):
    locale.setlocale(locale.LC_ALL, 'C')
    propsm = [SPropValue(PR_SUBJECT_A, b'test'), SPropValue(PR_MESSAGE_CLASS_A, b'IPM.Note')]
    message.SetProps(propsm)

    attachments = [
                   [SPropValue(PR_ATTACH_METHOD, ATTACH_BY_VALUE), SPropValue(PR_ATTACH_DATA_BIN, b'Test data 1'), SPropValue(PR_DISPLAY_NAME_W, 'ザラファ1')],
                   [SPropValue(PR_ATTACH_METHOD, ATTACH_BY_VALUE), SPropValue(PR_ATTACH_DATA_BIN, b'Test data 2'), SPropValue(PR_DISPLAY_NAME_W, 'ザラファ2')],
                   [SPropValue(PR_ATTACH_METHOD, ATTACH_BY_VALUE), SPropValue(PR_ATTACH_DATA_BIN, b'Test data 3'), SPropValue(PR_DISPLAY_NAME_A, b'display 3')],
    ]

    for attach in attachments:
        _, attobj = message.CreateAttach(None, 0)
        attobj.SetProps(attach)
        attobj.SaveChanges(0)

    recipients = [make_recip_smtp_entry(addressbook, MAPI_UNICODE, u'ザラファ', u'user1@test.com', MAPI_TO),
                  make_recip_smtp_entry(addressbook, 0, b'user2', b'user@test.com', MAPI_TO)]

    message.ModifyRecipients(MODRECIP_ADD, recipients)

    message.SaveChanges(KEEP_OPEN_READWRITE)
    yield message
    locale.setlocale(locale.LC_ALL, '')


@pytest.mark.parametrize('flags', [0, MAPI_UNICODE])
def test_attachmen_table(defaultmessage, flags):
    table = defaultmessage.GetAttachmentTable(flags)
    table.SetColumns([PR_DISPLAY_NAME_A, PR_DISPLAY_NAME_W], 0)
    rows = table.QueryRows(10, 0)
    rowsresult = [[SPropValue(PR_DISPLAY_NAME_A, b'????1'), SPropValue(PR_DISPLAY_NAME_W, u'ザラファ1')],
                  [SPropValue(PR_DISPLAY_NAME_A, b'????2'), SPropValue(PR_DISPLAY_NAME_W, u'ザラファ2')],
                  [SPropValue(PR_DISPLAY_NAME_A, b'display 3'), SPropValue(PR_DISPLAY_NAME_W, u'display 3')]]

    assert rows == rowsresult


@pytest.mark.parametrize('flags', [0, MAPI_UNICODE])
def test_recipienttable(defaultmessage, flags):
    table = defaultmessage.GetRecipientTable(flags);
    table.SetColumns([PR_DISPLAY_NAME_A, PR_DISPLAY_NAME_W], 0)
    rows = table.QueryRows(10, 0)
    rowsresult = [[SPropValue(PR_DISPLAY_NAME_A, b'????'), SPropValue(PR_DISPLAY_NAME_W, u'ザラファ')],
                  [SPropValue(PR_DISPLAY_NAME_A, b'user2'), SPropValue(PR_DISPLAY_NAME_W, u'user2')]]

    assert rows == rowsresult


def test_recipienttable_addmore(addressbook, defaultmessage, make_recip_smtp_entry):
    recipients = [make_recip_smtp_entry(addressbook, 0, b'user4', b'user4@test.com', MAPI_TO),
                  make_recip_smtp_entry(addressbook, 0, b'user5', b'user5@test.com', MAPI_TO)]

    defaultmessage.ModifyRecipients(MODRECIP_ADD, recipients)
    defaultmessage.SaveChanges(0)

    table = defaultmessage.GetRecipientTable(0)
    table.SetColumns([PR_DISPLAY_NAME_A, PR_DISPLAY_NAME_W], 0)
    rows = table.QueryRows(10, 0)
    rowsresult = [[SPropValue(PR_DISPLAY_NAME_A, b'????'), SPropValue(PR_DISPLAY_NAME_W, u'ザラファ')],
                  [SPropValue(PR_DISPLAY_NAME_A, b'user2'), SPropValue(PR_DISPLAY_NAME_W, u'user2')],
                  [SPropValue(PR_DISPLAY_NAME_A, b'user4'), SPropValue(PR_DISPLAY_NAME_W, u'user4')],
                  [SPropValue(PR_DISPLAY_NAME_A, b'user5'), SPropValue(PR_DISPLAY_NAME_W, u'user5')]]
    assert rows == rowsresult


def test_recipienttable_delete(defaultmessage):
    defaultmessage.ModifyRecipients(MODRECIP_REMOVE, [[SPropValue(PR_ROWID, 1)]])
    defaultmessage.SaveChanges(0)

    table = defaultmessage.GetRecipientTable(0)
    table.SetColumns([PR_DISPLAY_NAME_A, PR_DISPLAY_NAME_W], 0)
    rows = table.QueryRows(10, 0)
    rowsresult = [[SPropValue(PR_DISPLAY_NAME_A, b'????'), SPropValue(PR_DISPLAY_NAME_W, u'ザラファ')]]
    assert rows == rowsresult


def test_recipienttable_modify(addressbook, defaultmessage, make_recip_smtp_entry):
    modifyuser = make_recip_smtp_entry(addressbook, 0, b'moduser2', b'moduser2@test.com', MAPI_TO)
    modifyuser.append(SPropValue(PR_ROWID, 1))
    defaultmessage.ModifyRecipients(MODRECIP_MODIFY, [modifyuser])
    defaultmessage.SaveChanges(0)

    table = defaultmessage.GetRecipientTable(0)
    table.SetColumns([PR_DISPLAY_NAME_A, PR_DISPLAY_NAME_W], 0)
    rows = table.QueryRows(10, 0)
    rowsresult = [[SPropValue(PR_DISPLAY_NAME_A, b'????'), SPropValue(PR_DISPLAY_NAME_W, u'ザラファ')],
                  [SPropValue(PR_DISPLAY_NAME_A, b'moduser2'), SPropValue(PR_DISPLAY_NAME_W, u'moduser2')]]
    assert rows == rowsresult


def test_required_properties(defaultmessage):
    proptags = [
                PR_CREATION_TIME,
                PR_DISPLAY_BCC,
                PR_DISPLAY_CC,
                PR_DISPLAY_TO,
                PR_ENTRYID,
                PR_LAST_MODIFICATION_TIME,
                PR_MESSAGE_ATTACHMENTS,
                PR_MESSAGE_CLASS,
                PR_MESSAGE_FLAGS,
                PR_MESSAGE_RECIPIENTS,
                #  TODO:  Not exist on a open new message, with savechanges
                #  PR_MESSAGE_SIZE,
                #  PR_EC_IMAP_ID,
                PR_NORMALIZED_SUBJECT,
                PR_PARENT_ENTRYID,
                PR_RECORD_KEY,
                PR_SEARCH_KEY,
                PR_STORE_ENTRYID,
                PR_STORE_RECORD_KEY,
                PR_EC_HIERARCHYID
                ]

    results = defaultmessage.GetProps(proptags, 0)
    for i in results:
        assert PROP_TYPE(i.ulPropTag) != PT_ERROR


def test_copyto_notexclude(defaultmessage, copy):
    defaultmessage.CopyTo([], [], 0, None, IID_IMessage, copy, 0)
    assert_attachment_count(defaultmessage, copy)
    assert_recipient_count(defaultmessage, copy)


def test_copyto_exclude_attach(defaultmessage, copy):
    defaultmessage.CopyTo([], [PR_MESSAGE_ATTACHMENTS], 0, None, IID_IMessage, copy, 0)
    table = copy.GetAttachmentTable(MAPI_DEFERRED_ERRORS)

    assert not table.GetRowCount(0)
    assert_recipient_count(defaultmessage, copy)


def test_exclude_recip(defaultmessage, copy):
    defaultmessage.CopyTo([], [PR_MESSAGE_RECIPIENTS], 0, None, IID_IMessage, copy, 0)
    table = copy.GetRecipientTable(MAPI_DEFERRED_ERRORS)

    assert not table.GetRowCount(0)
    assert_attachment_count(defaultmessage, copy)


def test_exclude_attach_recip(defaultmessage, copy):
    defaultmessage.CopyTo([], [PR_MESSAGE_RECIPIENTS, PR_MESSAGE_ATTACHMENTS], 0, None, IID_IMessage, copy, 0)
    reciptable = copy.GetRecipientTable(MAPI_DEFERRED_ERRORS)
    attachtable = copy.GetRecipientTable(MAPI_DEFERRED_ERRORS)

    assert not reciptable.GetRowCount(0)
    assert not attachtable.GetRowCount(0)


def test_copyto_self(store, message):
    with pytest.raises(MAPIError) as excinfo:
        message.CopyTo([], [], 0, None, IID_IMessage, message, 0)
    assert 'MAPI_E_NO_ACCESS' in str(excinfo)
    # reopen self from server so we can't compare memory pointers
    message.SaveChanges(0)
    eid = message.GetProps([PR_ENTRYID], 0)[0].Value
    same = store.OpenEntry(eid, None, MAPI_BEST_ACCESS)
    with pytest.raises(MAPIError) as excinfo:
        message.CopyTo([], [], 0, None, IID_IMessage, same, 0)
    assert 'MAPI_E_NO_ACCESS' in str(excinfo)


def test_recursive_embedded_attach(message, make_embedded_attachment):
    make_embedded_attachment(message, 25)
    with pytest.raises(MAPIError) as excinfo:
        message.SaveChanges(0)
    assert 'MAPI_E_TOO_COMPLEX' in str(excinfo)


def test_recursive_embedded_ok(message, make_embedded_attachment):
    make_embedded_attachment(message, 19)
    message.SaveChanges(0)
    table = message.GetAttachmentTable(0)
    # Recursive attachments
    assert table.GetRowCount(0) == 1


def test_openproperty_resetstring(message):
    message.SetProps([SPropValue(PR_SUBJECT, b'test')])
    message.OpenProperty(PR_SUBJECT, IID_IStream, STGM_WRITE, MAPI_MODIFY | MAPI_CREATE)
    assert message.GetProps([PR_SUBJECT], 0)[0] == SPropValue(PR_SUBJECT, b'')


def test_openproperty_resetbin(message):
    # TODO: Get PR_PROPERTY
    message.SetProps([SPropValue(0x67000102, b't1')])
    message.OpenProperty(0x67000102, IID_IStream, STGM_WRITE, MAPI_MODIFY | MAPI_CREATE)
    assert message.GetProps([0x67000102], 0)[0] == SPropValue(0x67000102, b'')


def test_openproperty_createbin(message):
    message.OpenProperty(0x67000102, IID_IStream, STGM_WRITE, MAPI_MODIFY | MAPI_CREATE)
    assert message.GetProps([0x67000102], 0)[0] == SPropValue(0x67000102, b'')
