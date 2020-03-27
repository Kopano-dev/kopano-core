import pytest

from kopano import (server, ArgumentError, Company, Error, Store,
                    NotFoundError, User)


def test_failconnect():
    with pytest.raises(Error) as excinfo:
        server(server_socket='file:///tmp/no/this/does/no')
    assert 'could not connect to server' in str(excinfo)


def test_guid(server):
    assert server.guid
    assert isinstance(server.guid, str)


def test_user(server, user):
    # username
    testuser = server.user(user.name)
    assert isinstance(testuser, User)
    assert testuser.name == user.name

    with pytest.raises(NotFoundError) as excinfo:
        server.user('notfound')
    assert 'no such user' in str(excinfo)

    # email
    testuser = server.user(email=user.email)
    assert isinstance(testuser, User)
    assert testuser.name == user.name

    with pytest.raises(NotFoundError) as excinfo:
        server.user(email='notfound@foo.com')
    assert 'no such user' in str(excinfo)

    # userid
    userid = user.userid
    testuser = server.user(userid=userid)
    assert isinstance(testuser, User)
    assert testuser.name == user.name

    with pytest.raises(ArgumentError) as excinfo:
        server.user(userid=user.userid[:-1])
    assert 'invalid entryid' in str(excinfo)


def test_getuser(server, user):
    testuser = server.get_user(user.name)
    assert testuser.name == user.name

    assert not server.get_user('noauser')


def test_store_guid(server, user):
    store = server.store(user.store.guid)
    assert isinstance(store, Store)
    assert user.store.guid == store.guid

    with pytest.raises(NotFoundError) as excinfo:
        server.store(32*'A')
    assert 'no such store' in str(excinfo)


@pytest.mark.parametrize('arg', ['12', 'ZZ', 32*'ZZ'])
def test_store_guid_invalid(server, arg):
    with pytest.raises(Error) as excinfo:
        server.store(arg)
    assert 'invalid store id' in str(excinfo)


def test_store_entryid(server, user):
    store = server.store(entryid=user.store.entryid)
    assert isinstance(store, Store)
    assert user.store.entryid == store.entryid

    with pytest.raises(NotFoundError) as excinfo:
        server.store(entryid='DEADBEEF')
    assert 'no store with' in str(excinfo)


def test_publicstore(server):
    public_store = server.store('public')
    assert public_store == server.public_store
    assert isinstance(public_store, Store)
    assert public_store.public


def test_getstore(server, user):
    teststore = server.get_store(user.store.guid)
    assert isinstance(teststore, Store)
    assert teststore.user.name == user.name


def test_stores(server):
    assert list(server.stores())


def test_state(adminserver):
    assert adminserver.state
    assert isinstance(adminserver.state, str)


def test_tables(adminserver):
    assert list(adminserver.tables())


def test_groups(server):
    assert list(server.groups())


def test_users(server):
    assert list(server.users())


def test_company(server):
    assert isinstance(server.company('Default'), Company)


def test_companies(server):
    assert list(server.companies())
    assert isinstance(next(server.companies()), Company)


def test_userfilter(server, user):
    server.options.users = [user.name]
    users = list(server.users())
    assert users
    assert users[0].name == user.name

    server.options.users = ['hakuho']
    with pytest.raises(NotFoundError) as excinfo:
        list(server.users())

    assert 'no such user' in str(excinfo)

    server.options.users = []


def test_companyfilter(server, user):
    server.options.companies = ['Default']
    companies = list(server.companies())
    assert companies
    assert companies[0].name == 'Default'

    server.options.companies = ['hakuho']
    with pytest.raises(NotFoundError) as excinfo:
        list(server.companies())

    assert 'no such company' in str(excinfo)

    server.options.companies = []


def test_storefilter(server, user):
    server.options.store = [user.guid]
    stores = list(server.stores())
    assert stores
    assert stores[0].guid

    server.options.stores = [user.guid[::-1]]
    with pytest.raises(NotFoundError) as excinfo:
        list(server.stores())

    assert 'no such store' in str(excinfo)

    server.options.stores = []


def test_purgesoftdeletes(adminserver):
    for x in reversed(range(5)):
        adminserver.purge_softdeletes(x)


def test_purgedeferred(adminserver):
    assert adminserver.purge_deferred()


def test_adminstore(server):
    assert isinstance(server.admin_store, Store)


def test_getcompany(server):
    assert isinstance(server.get_company('Default'), Company)
    assert not server.get_company('Planet Express')


def test_str(server):
    assert str(server) == 'Server({})'.format(server.server_socket)
