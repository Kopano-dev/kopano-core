from kopano import Company, Group, NotFoundError, Store, User

import pytest


def test_name(company):
    assert company.name == 'Default'


def test_user(user, company):
    testuser = company.user(user.name)
    assert testuser == user
    assert isinstance(testuser, User)

    with pytest.raises(NotFoundError) as excinfo:
        company.user('lalalalalal')
    assert 'no such user' in str(excinfo)


def test_getuser(user, company):
    guid = user.store.guid
    store = company.store(guid)
    assert isinstance(store, Store)
    assert store.guid == guid
    assert not store.public

    pubstore = company.store('public')
    assert isinstance(pubstore, Store)
    assert pubstore.public


def test_users(company):
    users = list(company.users())
    assert users
    assert isinstance(users[0], User)
    assert isinstance(users[0].company, Company)


def test_user_filtering(server, company, user):
    server.options.users = [user.name]
    users = list(company.users())
    assert len(users) == 1

    server.options.users = []


def test_stores(company):
    stores = list(company.stores())
    assert stores
    assert isinstance(stores[0], Store)


def test_group(company):
    assert isinstance(company.group('Everyone'), Group)

    with pytest.raises(NotFoundError) as excinfo:
        company.group('groupno')
    assert 'no such group' in str(excinfo)


def test_groups(company):
    groups = list(company.groups())
    assert groups
    assert isinstance(groups[0], Group)


def test_quota(company):
    assert company.quota


def test_cmp(company):
    assert company == company


def test_str(company):
    assert str(company) == "Company('Default')"
