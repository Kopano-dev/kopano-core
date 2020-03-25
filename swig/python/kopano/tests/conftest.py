import codecs
import os

import kopano
import pytest

from MAPI import RELOP_EQ
from MAPI.Util import SPropValue, SPropertyRestriction
from MAPI.Tags import PR_SUBJECT_W, PR_EC_STATSTABLE_USERS, PR_EC_STATSTABLE_SYSTEM


ATTACHMENT_PATH = os.path.dirname(os.path.realpath(__file__)) + '/attachments/{}'
KOPANO_PICTURE_NAME = 'kopano-meet-icon.png'
CONTACT_VCF = 'contact.vcf'
EML = 'GPL.eml'


@pytest.fixture(scope="module")
def picture():
    data = open(ATTACHMENT_PATH.format(KOPANO_PICTURE_NAME), 'rb').read()
    yield kopano.Picture(data, KOPANO_PICTURE_NAME)


@pytest.fixture()
def server():
    yield kopano.server(auth_user=os.getenv('KOPANO_TEST_USER'), auth_pass=os.getenv('KOPANO_TEST_PASSWORD'))


@pytest.fixture()
def adminserver():
    # Authenticated based on user uid
    yield kopano.server()


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
def delegate(user, user2):
    delegate = user.delegation(user2, create=True)
    yield delegate
    user.store.delete(user.delegations())


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
def email(inbox):
    item = inbox.create_item(eml=open(ATTACHMENT_PATH.format(EML), 'rb').read())
    yield item
    inbox.delete(item)


@pytest.fixture()
def attachment(item):
    data = open(ATTACHMENT_PATH.format(KOPANO_PICTURE_NAME), 'rb').read()
    attach = item.create_attachment('kopano.png', data, mimetype='image/png')
    yield attach


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
def rule(inbox):
    target = inbox.folder('target', create=True)

    restriction = kopano.Restriction(
        SPropertyRestriction(RELOP_EQ, PR_SUBJECT_W, SPropValue(PR_SUBJECT_W, 'test'))
    )
    rule = inbox.create_rule('test', restriction)
    rule.create_action('move', target)
    # reload rule
    rule = next(inbox.rules())

    yield rule
    inbox.delete(target)
    try:
        inbox.delete(inbox.rules())
    except Exception:
        pass


@pytest.fixture()
def empty_address(server):
    yield kopano.Address(server)


@pytest.fixture()
def quota(user):
    yield user.quota


@pytest.fixture()
def usertable(adminserver):
    yield adminserver.table(PR_EC_STATSTABLE_USERS)


@pytest.fixture()
def statstable(adminserver):
    yield adminserver.table(PR_EC_STATSTABLE_SYSTEM)


@pytest.fixture()
def group(server):
    yield server.group('Everyone')


@pytest.fixture()
def contact(user):
    contact = user.contacts.create_item(message_class='IPM.Contact')
    yield contact
    user.contacts.delete(contact)


@pytest.fixture()
def vcfcontact(user):
    data = open(ATTACHMENT_PATH.format(CONTACT_VCF), 'rb').read()
    contact = user.contacts.create_item(vcf=data)
    yield contact
    user.contacts.delete(contact)


@pytest.fixture()
def item_user_toccbcc(user):
    return [(1, user, [user.name], [user.email]),
            (2, [user, user], [user.name, user.name], [user.email, user.email])]
