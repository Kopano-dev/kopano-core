import pytest

from kopano import User, Group
from kopano.errors import NotFoundError


def test_folder_member(inbox, user2, group, create_obj_permission):
    p = create_obj_permission(inbox, user2)
    assert isinstance(p.member, User)

    p = create_obj_permission(inbox, group)
    assert isinstance(p.member, Group)


def test_store_member(store, user2, group, create_obj_permission):
    p = create_obj_permission(store, user2)
    assert isinstance(p.member, User)

    p = create_obj_permission(store, group)
    assert isinstance(p.member, Group)


def test_folder_rights(inbox, user2, group, create_obj_permission):
    p = create_obj_permission(inbox, user2)

    rights = ['create_items', 'create_subfolders', 'edit_all']
    p.rights = rights
    p = inbox.permission(user2)  # XXX should work without reload
    assert set(p.rights) == set(rights)

    with pytest.raises(NotFoundError) as excinfo:
        p.rights += ['fiets']
    assert 'no such' in str(excinfo)


def test_store_rights(store, user2, group, create_obj_permission):
    p = create_obj_permission(store, user2)

    rights = ['create_items', 'create_subfolders', 'edit_all']
    p.rights = rights
    p = store.permission(user2)  # XXX should work without reload
    assert set(p.rights) == set(rights)

    with pytest.raises(NotFoundError) as excinfo:
        p.rights += ['fiets']
    assert 'no such' in str(excinfo)


def test_str(inbox, user2, create_obj_permission):
    p = create_obj_permission(inbox, user2)
    assert str(p) == "Permission('{}')".format(user2.name)

