import os
import datetime

import pytest

from MAPI import FOLDER_SEARCH
from MAPI.Tags import PR_FOLDER_TYPE, PR_EC_OUTOFOFFICE_MSG_W

from kopano import Folder, Item, Store, User, ArgumentError, DuplicateError, NotFoundError


ENTRYID_LENGTH = 96


def test_store(store):
    store2 = Store(store.guid)
    assert store2.guid == store.guid

    with pytest.raises(NotFoundError) as excinfo:
        Store(guid=len(store.guid)*'0')
    assert 'no such store' in str(excinfo)


def test_subtree(store):
    assert isinstance(store.subtree, Folder)


def test_size(store):
    assert isinstance(store.size, int)

    sz = 0
    for folder in [store.root] + list(store.root.folders()):
        if folder.prop(PR_FOLDER_TYPE).value == FOLDER_SEARCH:
            continue

        for item in folder:
            sz += item.size
        for item in folder.associated:
            sz += item.size

    assert store.size == sz


def test_folder(store):
    inbox = store.inbox
    inbox2 = store.folder(inbox.name)
    inbox3 = store.folder(entryid=inbox.entryid)
    assert inbox.entryid == inbox2.entryid == inbox3.entryid

    # Test recurse
    found = store.root.folder(store.inbox.name, recurse=True)
    assert found.entryid == store.inbox.entryid

    with pytest.raises(NotFoundError) as excinfo:
        store.root.folder(store.inbox.name, recurse=False)
    assert 'no such folder' in str(excinfo)


def test_folder_exceptions(store):
    with pytest.raises(NotFoundError) as excinfo:
        store.folder('lalalalala')
    assert 'no such folder' in str(excinfo)

    with pytest.raises(NotFoundError) as excinfo:
        store.folder(entryid='DEADBEEF')
    assert 'no folder with entryid' in str(excinfo)

    with pytest.raises(NotFoundError) as excinfo:
        store.folder(entryid=len(store.inbox.entryid)*'0')
    assert 'no folder with entryid' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        store.folder(entryid=None)
    assert 'missing argument to identify folder' in str(excinfo)

    # TODO: fix python-kopano
    #with pytest.raises(ArgumentError) as excinfo:
    #    store.folder(entryid='')
    #assert 'missing argument to identify folder' in str(excinfo)


def test_getfolder(store):
    assert store.get_folder('inbox').name == 'Inbox'
    assert not store.get_folder('doesnotexists')


def test_item(store, item):
    item2 = next(store.inbox.items())
    assert item2.entryid == item.entryid


def test_item_exceptions(store, item):
    with pytest.raises(NotFoundError) as excinfo:
        store.item(entryid=item.entryid + '00')
    assert 'no item with entryid' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        store.item(entryid=28)
    assert 'invalid entryid' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        store.item(entryid=28)
    assert 'invalid entryid' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        store.item(entryid='notvalid')
    assert 'invalid entryid' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        store.item(entryid='AA')
    assert 'invalid entryid' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        store.item()
    assert 'no guid or entryid specified' in str(excinfo)


def test_folders_cli(server, store, folder):
    # parse (check command-line option)
    try:
        server.options.folders = ['Inbox/{}'.format(folder.name)]
        folders = list(store.folders())
        assert len(folders) == 1
        folders = list(store.folders(parse=False))
        assert len(folders) > 1
    finally:
        server.options.folders = []

    # recurse
    folders = list(store.folders())
    assert len(folders) > 1
    assert len([f for f in folders if f.name == folder.name]) == 1
    folders = list(store.folders(recurse=False))
    assert len(folders) > 1
    assert len([f for f in folders if f.name == folder.name]) == 0


def test_specialfolders(server, store):
    assert isinstance(store.inbox, Folder)
    assert isinstance(store.junk, Folder)
    assert isinstance(store.outbox, Folder)
    assert isinstance(store.calendar, Folder)
    assert isinstance(store.contacts, Folder)
    assert isinstance(store.sentmail, Folder)
    assert isinstance(store.wastebasket, Folder)
    assert isinstance(store.subtree, Folder)
    assert isinstance(store.drafts, Folder)
    assert isinstance(store.root, Folder)
    assert isinstance(store.findroot, Folder)
    assert isinstance(store.journal, Folder)
    assert isinstance(store.notes, Folder)
    assert isinstance(store.tasks, Folder)
    assert isinstance(store.suggested_contacts, Folder)
    assert isinstance(store.rss, Folder)
    assert isinstance(store.common_views, Folder)

    # public store
    assert isinstance(server.public_store.subtree, Folder)

    # TODO: test deleted root and subtree


def test_guid(store):
    assert isinstance(store.guid, str)
    assert len(store.guid) == 32


def test_entryid(store):
    assert isinstance(store.entryid, str)
    assert len(store.entryid) >= 200


def test_user(store):
    assert isinstance(store.user, User)
    assert store.user.name == os.getenv('KOPANO_TEST_USER')


def test_public(server, store):
    assert not store.public
    assert server.public_store.public


def test_hierarchyid(store):
    assert isinstance(store.hierarchyid, int)


def test_delete(user, store):
    prop = store.create_prop(PR_EC_OUTOFOFFICE_MSG_W, u'test')
    store.user.delete(prop)
    with pytest.raises(NotFoundError) as excinfo:
        user.store.prop(PR_EC_OUTOFOFFICE_MSG_W)
    assert 'no such property' in str(excinfo)


def test_company(server, store):
    assert store.company.name == 'Default'
    assert server.public_store.company.name == 'Default'


def test_oprhan(user, store):
    assert not store.orphan

    # TODO: requires admin user
    '''
    user.unhook()

    try:
        assert store.orphan
    finally:
        user.hook(store)
    '''


def test_delegation(user2, user3, store):
    store.delete(store.delegations())
    assert not list(store.delegations())

    delegation = store.delegation(user2, create=True)
    delegation.see_private = False

    delegations = list(store.delegations())
    assert len(delegations) == 1
    assert delegations[0].user.name == user2.name
    assert not delegations[0].see_private

    store.delete(delegations[0])
    assert not list(store.delegations())

    with pytest.raises(NotFoundError) as excinfo:
        store.delete(delegations[0])
    assert 'no delegation for user' in str(excinfo)

    with pytest.raises(NotFoundError) as excinfo:
        store.delegation(user3)
    assert 'no delegation for user' in str(excinfo)


def test_permissions(user2, store):
    store.delete(store.permissions())
    assert not list(store.permissions())

    permissions = ['create_items', 'edit_all']
    store.permission(user2, create=True).rights = permissions

    perms = list(store.permissions())
    assert len(perms)
    assert perms[0].member.name == user2.name
    assert set(perms[0].rights) == set(permissions)

    store.delete(store.permissions())
    assert not list(store.permissions())


def test_webappsettings(store):
    assert not store.webapp_settings


def test_reminders(store):
    assert not store.reminders


def test_homeserver(store):
    assert store.home_server != ''


def test_favorites(store, folder):
    assert not list(store.favorites())

    store.add_favorite(folder)
    with pytest.raises(DuplicateError) as excinfo:
        store.add_favorite(folder)
    assert 'already in favorites' in str(excinfo)

    favs = list(store.favorites())
    assert favs
    assert favs[0].name == folder.name


def test_configitem(store):
    item = store.config_item('candy')
    assert isinstance(item, Item)
    assert item.subject == 'candy'

    # existing
    item = store.config_item('candy')
    assert isinstance(item, Item)
    assert item.subject == 'candy'


def test_lastlogon(store):
    dt = datetime.datetime(2018, 1, 1)
    store.last_logon = dt
    assert store.last_logon == dt


def test_lastlogoff(store):
    dt = datetime.datetime(2018, 1, 1)
    store.last_logoff = dt
    assert store.last_logoff == dt


def test_iter(store):
    assert isinstance(list(store)[0], Folder)


def testStr(store):
    assert str(store) == "Store('{}')".format(store.guid)
