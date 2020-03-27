import codecs
import os

from datetime import datetime

import kopano
import pytest

from MAPI import RELOP_EQ
from MAPI.Util import SPropValue, SPropertyRestriction
from MAPI.Tags import PR_SUBJECT_W, PR_EC_STATSTABLE_USERS, PR_EC_STATSTABLE_SYSTEM, PR_RULES_TABLE


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
def notificationuser(server):
    server = kopano.server(auth_user=os.getenv('KOPANO_TEST_USER'),
                           auth_pass=os.getenv('KOPANO_TEST_PASSWORD'),
                           notifications=True)
    yield server.user(os.getenv('KOPANO_TEST_USER'))


@pytest.fixture()
def user2():
    server = kopano.server(auth_user=os.getenv('KOPANO_TEST_USER2'), auth_pass=os.getenv('KOPANO_TEST_PASSWORD2'))
    yield server.user(os.getenv('KOPANO_TEST_USER2'))


@pytest.fixture()
def user3():
    server = kopano.server(auth_user=os.getenv('KOPANO_TEST_USER3'), auth_pass=os.getenv('KOPANO_TEST_PASSWORD3'))
    yield server.user(os.getenv('KOPANO_TEST_USER3'))


@pytest.fixture()
def useradmin():
    server = kopano.server(auth_user=os.getenv('KOPANO_TEST_ADMIN'), auth_pass=os.getenv('KOPANO_TEST_ADMIN_PASSWORD'))
    yield server.user(os.getenv('KOPANO_TEST_ADMIN'))


@pytest.fixture()
def delegate(user, user2):
    delegate = user.delegation(user2, create=True)
    yield delegate
    user.store.delete(user.delegations())


@pytest.fixture()
def store(user):
    yield user.store


@pytest.fixture()
def root(user):
    yield user.root


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
def daily_appointment(calendar):
    item = calendar.create_item(subject='once',
                                start=datetime(2018, 9, 7, 9),
                                end=datetime(2018, 9, 7, 10)
                                )
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
    # Clear the rules table
    inbox.mapiobj.DeleteProps([PR_RULES_TABLE])


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
def company(server):
    yield server.company('Default')


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


@pytest.fixture()
def create_obj_permission():
    objects = []

    def _create_obj_permission(obj, member):
        objects.append(obj)
        return obj.permission(member, create=True)

    yield _create_obj_permission

    for obj in objects:
        obj.delete(obj.permissions())


@pytest.fixture()
def create_item():
    items = []

    def _create_item(destfolder, subject):
        item = destfolder.create_item(subject=subject)
        items.append(item)
        return item

    yield _create_item

    for item in items:
        try:
            item.folder.delete(item)
        except Exception:
            pass


@pytest.fixture()
def create_folder():
    folders = []

    def _create_folder(destfolder, name, path=None):
        if name:
            folder = destfolder.create_folder(name=name)
            folders.append(folder)
        else:
            orig_folder = destfolder.create_folder(path=path)
            folder = orig_folder
            while folder.name != 'Inbox':
                folders.append(folder)
                folder = folder.parent
            folder = orig_folder
        return folder

    yield _create_folder

    for folder in folders:
        try:
            folder.parent.delete(folder)
        except Exception:
            pass


@pytest.fixture()
def daily(calendar):
    item = calendar.create_item(subject='daily',
                                start=datetime(2018, 7, 7, 9),
                                end=datetime(2018, 7, 7, 9, 30),
                                timezone='Europe/Amsterdam',  # TODO: recurrence.timezone?
                                )
    item.recurring = True
    rec = item.recurrence
    print(rec)

    rec.pattern = 'daily'
    rec.range_type = 'end_date'  # TODO not needed to set?
    rec.start = datetime(2018, 7, 7)
    rec.end = datetime(2018, 7, 25)  # TODO include end date?

    rec._save()  # TODO should not be necessary by default?

    yield item

    calendar.delete(item)


@pytest.fixture()
def weekly(calendar):
    item = calendar.create_item(subject='weekly',
                                start=datetime(2018, 7, 7, 9),
                                end=datetime(2018, 7, 7, 9, 30),
                                timezone='Europe/Amsterdam',  # TODO: recurrence.timezone?
                                )
    item.recurring = True
    rec = item.recurrence

    rec.interval = 2
    rec.pattern = 'weekly'
    rec.weekdays = ['thursday', 'friday']
    rec.range_type = 'end_date'  # TODO not needed to set?
    rec.start = datetime(2018, 7, 7)
    rec.end = datetime(2019, 1, 1)

    rec._save()  # TODO should not be necessary by default?

    yield item

    calendar.delete(item)


@pytest.fixture()
def monthly(calendar):
    item = calendar.create_item(subject='monthly',
                                start=datetime(2018, 7, 7, 9),
                                end=datetime(2018, 7, 7, 9, 30),
                                timezone='Europe/Amsterdam',  # TODO: recurrence.timezone?
                                )
    item.recurring = True
    rec = item.recurrence

    rec.monthday = 21
    rec.pattern = 'monthly'
    rec.range_type = 'end_date'  # TODO not needed to set?
    rec.start = datetime(2018, 7, 7)
    rec.end = datetime(2019, 1, 1)

    rec._save()  # TODO should not be necessary by default?

    yield item

    calendar.delete(item)


@pytest.fixture()
def monthly_rel(calendar):
    item = calendar.create_item(subject='monthly_rel',
                                start=datetime(2018, 8, 15, 9),
                                end=datetime(2018, 8, 15, 9, 30),
                                timezone='Europe/Amsterdam',  # TODO: recurrence.timezone?
                                )
    item.recurring = True
    rec = item.recurrence

    rec.pattern = 'monthly_rel'
    rec.index = 'third'
    rec.weekdays = ['wednesday']
    rec.range_type = 'forever'
    rec.start = datetime(2018, 8, 15)

    rec._save()  # TODO should not be necessary by default?

    yield item

    calendar.delete(item)


@pytest.fixture()
def yearly(calendar):
    item = calendar.create_item(subject='yearly',
                                start=datetime(2018, 7, 7, 9),
                                end=datetime(2018, 7, 7, 9, 30),
                                timezone='Europe/Amsterdam',  # TODO: recurrence.timezone?
                                )
    item.recurring = True
    rec = item.recurrence


    rec.pattern = 'yearly'
    rec.range_type = 'end_date'
    rec.start = datetime(2018, 1, 1)
    rec.end = datetime(2030, 1, 1)

    rec.month = 8
    rec.monthday = 11

    rec._save()  # TODO should not be necessary by default?

    yield item

    calendar.delete(item)


@pytest.fixture()
def yearly_rel(calendar):
    item = calendar.create_item(subject='yearly_rel',
                                start=datetime(2018, 8, 27, 9),
                                end=datetime(2018, 8, 27, 9, 30),
                                timezone='Europe/Amsterdam',  # TODO: recurrence.timezone?
                                )
    item.recurring = True
    rec = item.recurrence

    rec.pattern = 'yearly_rel'
    rec.range_type = 'count'
    rec.start = datetime(2018, 8, 27)
    rec.count = 4
    rec.end = datetime(2026, 1, 1)

    rec.interval = 2
    rec.month = 8
    rec.index = 'last'
    rec.weekdays = ['monday']

    rec._save()  # TODO should not be necessary by default?

    yield item

    calendar.delete(item)


@pytest.fixture()
def tentative(calendar):
    item = calendar.create_item(subject='tentative',
                                text=' test',
                                start=datetime(2018, 7, 7, 9),
                                end=datetime(2018, 7, 7, 9, 30),
                                busystatus='tentative',
                                )

    yield item

    calendar.delete(item)


@pytest.fixture()
def freebusy(user):
    yield user.freebusy
    # TODO: reset freebusy, just clear data
    user.freebusy.publish(datetime(2030, 1, 1), datetime(2030, 1, 1))
