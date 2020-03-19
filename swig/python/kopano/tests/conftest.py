import codecs
import os

import kopano
import pytest


KOPANO_PICTURE_NAME = 'kopano-meet-icon.png'


@pytest.fixture(scope="module")
def picture():
    dir_path = os.path.dirname(os.path.realpath(__file__))
    data = open('{}/attachments/kopano-meet-icon.png'.format(dir_path), 'rb').read()
    yield kopano.Picture(data, KOPANO_PICTURE_NAME)


@pytest.fixture()
def server():
    yield kopano.server(auth_user=os.getenv('KOPANO_TEST_USER'), auth_pass=os.getenv('KOPANO_TEST_PASSWORD'))


@pytest.fixture()
def user(server):
    yield server.user(os.getenv('KOPANO_TEST_USER'))


@pytest.fixture()
def user2(server):
    server = kopano.server(auth_user=os.getenv('KOPANO_TEST_USER2'), auth_pass=os.getenv('KOPANO_TEST_PASSWORD2'))
    yield server.user(os.getenv('KOPANO_TEST_USER2'))


@pytest.fixture()
def user3(server):
    server = kopano.server(auth_user=os.getenv('KOPANO_TEST_USER3'), auth_pass=os.getenv('KOPANO_TEST_PASSWORD3'))
    yield server.user(os.getenv('KOPANO_TEST_USER3'))


@pytest.fixture()
def store(user):
    yield user.store


@pytest.fixture()
def inbox(user):
    yield user.inbox


@pytest.fixture()
def calendar(user):
    yield user.calendar


@pytest.fixture()
def item(inbox):
    item = inbox.create_item()
    yield item
    inbox.delete(item)


@pytest.fixture()
def folder(inbox):
    folder = inbox.create_folder('test')
    yield folder
    inbox.delete(folder)


@pytest.fixture()
def outofoffice(user):
    oof = user.outofoffice
    yield oof
    # Reset after test passed
    oof.enable = False


@pytest.fixture()
def appointment(calendar):
    item = calendar.create_item()
    yield item
    calendar.delete(item)


@pytest.fixture()
def address(user):
    yield kopano.Address(user.server, "ZARAFA", user.name, user.email,
                         codecs.decode(user.userid, 'hex'))


@pytest.fixture()
def empty_address(server):
    yield kopano.Address(server)
