from kopano import Item


class Importer:
    def __init__(self):
        self.updated = []
        self.deleted = []

    def update(self, item, flags):
        self.updated.append(item)

    def delete(self, item, flags):
        self.deleted.append(item)


class UserImporter:
    def __init__(self):
        self.updated = []
        self.deleted = []

    def update(self, user):
        self.updated.append(user)

    def delete(self, user):
        self.deleted.append(user)


def test_ics(adminserver, user, create_item):  # server-wide syncing
    inbox = user.store.inbox
    state = adminserver.state
    importer = Importer()

    item = create_item(inbox, 'test')
    create_item(inbox, 'test')

    new_state = adminserver.sync(importer, state)

    assert len(importer.updated) == 2
    assert isinstance(importer.updated[0], Item)
    assert len(importer.deleted) == 0

    inbox.delete(item)
    assert inbox.count == 1

    importer = Importer()
    new_state = adminserver.sync(importer, new_state)

    assert len(importer.deleted) == 1
    assert isinstance(importer.deleted[0], Item)


def test_syncgab(adminserver):
    # initial
    importer = UserImporter()
    state = adminserver.sync_gab(importer, None)
    assert len(importer.updated) > 1
    assert len(importer.deleted) == 0

    # TODO: delete/update

    # nothing
    importer = UserImporter()
    state = adminserver.sync_gab(importer, state)
    assert not importer.deleted
    assert not importer.updated
