from queue import Queue

import pytest

from kopano import Folder, Item, NotSupportedError


class Sink:
    def __init__(self):
        self.queue = Queue()

    def update(self, notification):
        self.queue.put(notification)


def test_unsupported(inbox):
    with pytest.raises(NotSupportedError) as excinfo:
        inbox.subscribe(Sink())
    assert 'not support notifications' in str(excinfo)


def test_folderitems(notificationuser, create_item):
    sink = Sink()
    inbox = notificationuser.inbox

    inbox.subscribe(sink, object_types=['item'])

    # create item
    item = create_item(inbox, '')
    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'item'
    assert notif.event_type == 'created'

    assert isinstance(notif.object, Item)
    assert not notif.object.subject

    # update item
    item.subject = 'subject'
    notif = sink.queue.get(timeout=10)

    assert notif.object_type == 'item'
    assert notif.event_type == 'updated'

    assert isinstance(notif.object, Item)
    assert notif.object.subject == 'subject'

    # delete item
    inbox.delete(item)
    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'item'
    assert notif.event_type == 'deleted'

    # move item in (==create)
    item2 = create_item(notificationuser.sentmail, 'new')
    item3 = item2.move(inbox)
    notif = sink.queue.get(timeout=10)

    assert notif.object_type == 'item'
    assert notif.event_type == 'created'
    assert notif.object.subject == 'new'

    # move item out (==delete)
    item3.move(notificationuser.sentmail)
    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'item'
    assert notif.event_type == 'deleted'

    notificationuser.sentmail.delete(item3)

    inbox.unsubscribe(sink)


def test_storeitems(notificationuser, create_item):
    sink = Sink()
    store = notificationuser.store
    inbox = notificationuser.inbox
    store.subscribe(sink, object_types=['item'])

    # create item
    item = create_item(inbox, '')
    notif = sink.queue.get(timeout=10)

    assert notif.object_type == 'item'
    assert notif.event_type == 'created'
    assert isinstance(notif.object, Item)
    assert notif.object.subject == ''
    assert isinstance(notif.object.folder, Folder)
    assert notif.object.folder.name == 'Inbox'

    # TODO update, delete

    # move item
    store.inbox.move(item, store.sentmail)  # TODO item.move does copy/delete?
    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'item'
    assert notif.event_type == 'updated'

    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'item'
    assert notif.event_type == 'created'

    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'item'
    assert notif.event_type == 'deleted'

    store.unsubscribe(sink)


def test_storefolders(notificationuser, create_folder):
    sink = Sink()
    store = notificationuser.store
    inbox = notificationuser.inbox
    store.subscribe(sink, object_types=['folder'])

    # create folder
    folder = create_folder(inbox, 'subfolder')
    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'folder'
    assert notif.event_type == 'created'

    assert isinstance(notif.object, Folder)
    assert notif.object.name == folder.name

    # update folder
    folder.name = 'dinges'
    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'folder'
    assert notif.event_type == 'updated'

    assert isinstance(notif.object, Folder)
    assert notif.object.name == folder.name

    # delete folder
    store.delete(folder)
    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'folder'
    assert notif.event_type == 'updated'

    notif = sink.queue.get(timeout=10)
    assert notif.object_type == 'folder'
    assert notif.event_type == 'deleted'

    store.unsubscribe(sink)
