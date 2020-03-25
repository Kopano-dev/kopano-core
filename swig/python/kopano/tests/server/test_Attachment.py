from datetime import datetime

import pytest

from kopano import NotFoundError

from MAPI.Tags import PR_OBJECT_TYPE, PR_HASATTACH, PR_ATTACH_METHOD, PR_ATTACH_DATA_BIN


def test_create(item):
    title = 'new attachment'
    data = b'new attachment'
    attach = item.create_attachment(title, data, mimetype='image/png')

    assert len(list(item.attachments())) == 1
    assert attach.name == title
    assert attach.data == data
    assert attach.mimetype == 'image/png'
    assert not attach.embedded
    assert str(attach).startswith('Attachment(')


def test_attribute(attachment):
    assert attachment.number == 0
    assert attachment.mimetype == 'image/png'
    assert attachment.filename == 'kopano.png'
    assert attachment.name == 'kopano.png'

    # TODO: attach.size is 0 somehow, reopen?
    assert attachment.size == 0
    assert len(attachment) == 0


def test_prop(attachment):
    attachment.prop(PR_OBJECT_TYPE).value == 7
    assert list(attachment.props())


def test_delete(attachment):
    prop = attachment.prop(PR_ATTACH_DATA_BIN)
    attachment.delete(prop)

    with pytest.raises(NotFoundError) as excinfo:
        attachment.prop(PR_ATTACH_DATA_BIN)
    assert 'no such property' in str(excinfo)


def test_read(attachment):
    assert b'\x89PNG' in attachment.read()


def test_remove(item, attachment):
    item.delete(item.attachments())

    # Re-open the item, toe refresh the PR_ATTACH_NUM
    item = item.store.item(item.entryid)

    assert not list(item.attachments())
    assert not item.has_attachments


def test_entryid(attachment):
    assert attachment.entryid


def test_embedded(attachment):
    assert not attachment.embedded


def test_item(inbox, item):
    item.create_item(subject='embedded')

    # Re-open the item, toe refresh the PR_ATTACH_NUM
    item = item.store.item(item.entryid)

    assert list(item.attachments(embedded=True))
    attach = next(item.attachments(embedded=True))
    assert attach.embedded
    assert not attach.hidden
    assert attach.item


def test_hidden(attachment):
    assert not attachment.hidden


def test_lastmodified(item, attachment):
    # Re-open the item, toe refresh the last_modified
    item = item.store.item(item.entryid)
    attach = next(item.attachments(embedded=True))

    assert isinstance(attach.last_modified, datetime)
