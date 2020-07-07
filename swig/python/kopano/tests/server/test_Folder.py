import pytest

from kopano import Folder, User, ArgumentError, NotFoundError


def test_createfolder(inbox, create_folder):
    folder = create_folder(inbox, 'test')
    assert isinstance(folder, Folder)
    assert folder.name == 'test'
    assert folder.path == 'Inbox/test'

    folder = create_folder(inbox, None, path='test/path')
    assert isinstance(folder, Folder)
    assert folder.name == 'path'
    assert folder.path == 'Inbox/test/path'


def test_folders(root, inbox):
    assert list(root.folders())
    assert not list(inbox.folders())

    # Recurse
    assert len(list(root.folders())) > len(list(root.folders(recurse=False)))


def test_parent(root, inbox):
    assert root == inbox.parent.parent


def test_name(inbox, create_folder):
    folder = create_folder(inbox, 'test')
    folder.name = 'new'

    assert folder.name == 'new'


def test_size(inbox):
    assert inbox.size == 0


def test_hierarchyid(inbox):
    assert isinstance(inbox.hierarchyid, int)


def test_count(inbox, create_item):
    assert inbox.count == 0
    create_item(inbox, 'test')
    assert inbox.count == 1


def test_delete(inbox, create_item):
    item = create_item(inbox, 'delete')
    assert inbox.count == 1
    inbox.delete([item])
    assert inbox.count == 0


def test_delete_exceptions(inbox):
    with pytest.raises(ArgumentError) as excinfo:
        inbox.delete(18)
    assert 'invalid argument' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        inbox.delete([18])
    assert 'invalid argument' in str(excinfo)


def test_empty(inbox, create_folder, create_item):
    create_folder(inbox, '', path='parent/child')
    parent = inbox.folder('parent')
    child = inbox.folder(path='parent/child')

    create_item(parent, 'test')
    create_item(child, 'test')

    assert parent.count == 1
    assert child.count == 1

    parent.empty(recurse=False)
    assert parent.count == 0
    assert list(parent.folders())
    assert child.count == 1

    # recursive
    create_item(parent, 'test1')
    parent.empty()

    assert parent.count == 0
    assert not list(parent.folders())


def test_mbox(tmp_path, inbox, create_folder, create_item):
    mboxfile = tmp_path / 'mbox'
    folder = create_folder(inbox, 'data')
    create_item(folder, 'test')

    # export to mbox
    folder.mbox(mboxfile)

    newfolder = create_folder(inbox, 'mbox')
    newfolder.readmbox(mboxfile)
    assert newfolder.count


def test_deletefolder(inbox, create_folder):
    folder = create_folder(inbox, 'test')
    entryid = folder.entryid

    inbox.delete(folder)

    with pytest.raises(NotFoundError) as excinfo:
        inbox.folder('test')
    assert 'no such folder' in str(excinfo)

    with pytest.raises(NotFoundError) as excinfo:
        inbox.folder(entryid=entryid)
    assert 'no folder with entryid' in str(excinfo)


def test_associated(inbox):
    assert isinstance(inbox.associated, Folder)


def test_create_message_associated(inbox, create_item):
    item = create_item(inbox.associated, 'test')
    found_item = next(inbox.associated.items())
    assert found_item.subject == item.subject


def test_deleted(inbox, create_item):
    item = create_item(inbox, 'delete me')
    inbox.delete(item, soft=True)

    assert isinstance(inbox.deleted, Folder)
    assert inbox.deleted.count
    assert next(inbox.deleted).subject == 'delete me'


def test_containerclass(inbox, calendar, create_folder):
    assert calendar.container_class == 'IPF.Appointment'

    folder = create_folder(inbox, 'test')
    folder.container_class = 'IPF.Contact'
    assert folder.container_class == 'IPF.Contact'


def test_item(inbox):
    pass


def test_subfolder_count(inbox):
    assert not inbox.subfolder_count


def test_move(inbox, create_folder, create_item):
    folder = create_folder(inbox, 'target')
    create_item(inbox, 'test')

    inbox.move(inbox.items(), folder)
    assert not inbox.count
    assert folder.count


def test_copy(user, inbox, create_folder, create_item):
    folder = create_folder(inbox, 'target')
    create_item(inbox, 'test')

    inbox.copy(inbox.items(), folder)
    assert inbox.count
    assert folder.count


def test_folder(user, inbox):
    entryid = inbox.entryid
    assert user.subtree.folder('Inbox').entryid == entryid
    assert user.subtree.folder(entryid=entryid).entryid == entryid


def test_folder_exceptions(inbox):
    with pytest.raises(NotFoundError) as excinfo:
        inbox.folder(entryid=96*'A')
    assert 'no folder with entryid' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        inbox.folder(entryid='entryid')
    assert 'invalid entryid' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        inbox.folder(entryid=4)
    assert 'invalid entryid' in str(excinfo)

    with pytest.raises(ArgumentError) as excinfo:
        inbox.folder()
    assert 'missing argument to' in str(excinfo)


def test_path(inbox, create_folder):
    assert inbox.path == inbox.name

    create_folder(inbox, 'ininbox')
    folder = inbox.folder('ininbox')
    assert isinstance(folder, Folder)
    assert folder.path == inbox.name + '/' + folder.name


def test_sourcekey(inbox):
    assert inbox.sourcekey


def test_props(inbox):
    props = list(inbox.props())
    assert props

    # find PR_ENTRYID
    entryid = [prop for prop in props if prop.strid == 'PR_ENTRYID']
    assert entryid
    assert entryid[0].strval == inbox.entryid


def test_state(inbox):
    assert isinstance(inbox.state, str)
    assert len(inbox.state) == 16


def test_permissions(user, user2, inbox):
    inbox.permission(user2, create=True).rights = ['read_items']
    perms = list(inbox.permissions())

    assert len(perms) == 1
    perm = perms[0]

    assert isinstance(perm.member, User)
    assert perm.member.userid == user2.userid
    assert perm.rights == ['read_items']

    inbox.delete(perm)


def test_occurrences(calendar, daily):
    assert list(calendar.occurrences())


def test_guid(inbox):
    assert inbox.guid == inbox.entryid[56:88]


def test_mapiobj_setter(inbox, create_folder):
    folder1 = create_folder(inbox, '1')
    folder2 = create_folder(inbox, '2')
    folder1.mapiobj = folder2.mapiobj
    assert folder1.mapiobj == folder2.mapiobj


def test_empty_associated(inbox, create_item):
    create_item(inbox.associated, 'test')
    assert inbox.associated.count == 1

    inbox.associated.empty(associated=True)
    assert inbox.associated.count == 0


def test_tables(inbox):
    assert list(inbox.tables())


def test_hidden(user, create_folder):
    assert not user.inbox.hidden
    assert user.subtree.folder('Conversation Action Settings').hidden

    folder = create_folder(user.inbox, 'test')
    folder.hidden = True
    assert folder.hidden


def test_str(inbox):
    assert str(inbox) == 'Folder(Inbox)'
