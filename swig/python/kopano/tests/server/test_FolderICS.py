import os

from datetime import datetime, timedelta

from kopano import Error


class Importer:
    def __init__(self, broken=False):
        self.updated = []
        self.deleted = []
        self.broken = broken

    def update(self, item, flags):
        if self.broken:
            raise Error('broken')
        self.updated.append(item)

    def delete(self, item, flags):
        if self.broken:
            raise Error('broken')
        self.deleted.append(item)


class HierarchyImporter:
    def __init__(self):
        self.updated = []
        self.deleted = []

    def update(self, folder):
        self.updated.append(folder)

    def delete(self, folder, flags):
        self.deleted.append(folder)


def test_sync(inbox, create_item):
    stats = {'changes': 0, 'deletes': 0, 'errors': 0}
    now = datetime.now()

    item1 = create_item(inbox, 'item1')
    item1.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now
    item2 = create_item(inbox, 'item2')
    item2.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now

    importer = Importer()
    inbox.sync(importer, None, stats=stats)
    assert len(importer.updated) == 2

    importer = Importer()
    inbox.sync(importer, None, stats=stats, begin=now + timedelta(days=1))
    assert len(importer.updated) == 0

    importer = Importer()
    inbox.sync(importer, None, stats=stats, end=now - timedelta(days=1))
    assert len(importer.updated) == 0

    importer = Importer()
    inbox.sync(importer, None, stats=stats, end=now + timedelta(days=30))
    assert len(importer.updated) == 2

    importer = Importer()
    inbox.sync(importer, None, stats=stats, begin=now - timedelta(days=1), end=now + timedelta(days=1))
    assert len(importer.updated) == 2

    # TODO incremental

    assert stats['errors'] == 0


def test_missingstate(inbox, create_item):
    stats = {'changes': 0, 'deletes': 0, 'errors': 0}
    now = datetime.now()

    item1 = create_item(inbox, 'item1')
    item1.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now
    item2 = create_item(inbox, 'item2')
    item2.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now

    # get state
    importer = Importer()
    new_state = inbox.sync(importer, stats=stats)
    assert len(importer.updated) == 2
    assert stats['errors'] == 0

    # state missing/purged by server
    os.environ['PYKO_TEST_NOT_FOUND'] = 'yes'

    item3 = create_item(inbox, 'item2')
    item3.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now

    importer = Importer()
    new_state = inbox.sync(importer, state=new_state, stats=stats)
    assert len(importer.updated) == 1
    assert stats['errors'] == 0

    del os.environ['PYKO_TEST_NOT_FOUND']


def test_syncretry(inbox, create_item):  # TOOD: test sleeps due to retries in ics takes 1 min. in total
    return

    stats = {'changes': 0, 'deletes': 0, 'errors': 0}
    now = datetime.now()

    item1 = create_item(inbox, 'item1')
    item1.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now
    item2 = create_item(inbox, 'item2')
    item2.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now

    # skip two items
    importer = Importer()
    os.environ['PYKO_TEST_NETWORK_ERROR'] = 'yes'
    new_state = inbox.sync(importer, stats=stats)
    assert len(importer.updated) == 0
    assert stats['errors'] == 2
    del os.environ['PYKO_TEST_NETWORK_ERROR']

    # continue normally
    stats['errors'] = 0
    importer = Importer()
    item3 = create_item(inbox, 'item3')
    item3.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now
    new_state = inbox.sync(importer, state=new_state, stats=stats)

    assert len(importer.updated) == 1
    assert stats['errors'] == 0


def test_importerexception(inbox, create_item):
    stats = {'changes': 0, 'deletes': 0, 'errors': 0}
    now = datetime.now()

    item1 = create_item(inbox, 'item1')
    item1.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now
    item2 = create_item(inbox, 'item2')
    item2.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now

    # skip two items
    importer = Importer(broken=True)
    new_state = inbox.sync(importer, stats=stats)
    assert len(importer.updated) == 0
    assert stats['errors'] == 2

    # continue normally
    stats['errors'] = 0
    importer = Importer()
    item3 = create_item(inbox, 'item3')
    item3.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now

    new_state = inbox.sync(importer, state=new_state, stats=stats)
    assert len(importer.updated) == 1
    assert stats['errors'] == 0

    # delete item
    importer = Importer(broken=True)
    inbox.delete(item1)
    new_state = inbox.sync(importer, state=new_state, stats=stats)
    assert len(importer.deleted) == 0
    assert stats['errors'] == 1

    # continue normally
    stats['errors'] = 0
    importer = Importer()
    item4 = create_item(inbox, 'item4')
    item4.prop('PR_MESSAGE_DELIVERY_TIME', create=True).value = now
    new_state = inbox.sync(importer, state=new_state, stats=stats)
    assert len(importer.updated) == 1
    assert stats['errors'] == 0


def test_hierarchy(user, create_folder):
    subtree = user.subtree

    stats = {'changes': 0, 'deletes': 0, 'errors': 0}

    # initial sync: all folders
    importer = HierarchyImporter()
    new_state = subtree.sync_hierarchy(importer, None, stats=stats)
    assert len(importer.updated)
    assert not len(importer.deleted)

    # create folder
    folder = create_folder(user.inbox, 'new folder')
    importer = HierarchyImporter()
    new_state = subtree.sync_hierarchy(importer, new_state, stats=stats)
    assert len(importer.updated) == 1
    assert importer.updated[0].name == folder.name
    assert not importer.deleted

    # rename folder
    folder.name = 'New Folder'
    importer = HierarchyImporter()
    new_state = subtree.sync_hierarchy(importer, new_state, stats=stats)
    assert len(importer.updated) == 1
    assert importer.updated[0].name == folder.name
    assert not importer.deleted

    # delete folder (note that subfolders are skipped!)
    sourcekey = folder.sourcekey
    user.inbox.delete(folder)
    importer = HierarchyImporter()
    new_state = subtree.sync_hierarchy(importer, new_state, stats=stats)
    assert not importer.updated
    assert len(importer.deleted) == 1
    assert importer.deleted[0].sourcekey == sourcekey

    # nothing
    importer = HierarchyImporter()
    new_state = subtree.sync_hierarchy(importer, new_state, stats=stats)
    assert not importer.updated
    assert not importer.deleted

    # all good
    assert stats['errors'] == 0
