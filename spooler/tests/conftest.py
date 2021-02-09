# SPDX-License-Identifier: LGPL-3.0-or-later

import os
import smtplib
import threading

from email.message import EmailMessage
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

import pytest

from MAPI import (DEL_ASSOCIATED, DELETE_HARD_DELETE, MAPI_MODIFY, MAPIAdviseSink,
                  MAPI_BEST_ACCESS, MAPI_E_TIMEOUT, MAPI_ASSOCIATED, BOOKMARK_BEGINNING,
                  RELOP_EQ, fnevNewMail, fnevObjectModified)
from MAPI.Util import OpenECSession, GetDefaultStore
from MAPI.Struct import SPropertyRestriction, SPropValue, MAPIError
from MAPI.Tags import (PR_RULES_TABLE, PR_IPM_WASTEBASKET_ENTRYID, PR_SUBJECT, PR_BODY, PR_ENTRYID,
                       PR_ACCOUNT_W, PR_IPM_OUTBOX_ENTRYID, SHOW_SOFT_DELETES,
                       PR_STORE_ENTRYID, IID_IMAPIAdviseSink, IID_IExchangeModifyTable)


class AdviseSink(MAPIAdviseSink):
    def __init__(self):
        MAPIAdviseSink.__init__(self, [IID_IMAPIAdviseSink])

        self.count = 0
        self.assocCount = 0
        self.event = threading.Event()

    def OnNotify(self, notifications):
        self.event.set()
        return 0

    def WaitForNotification(self, timeout):
        if not self.event.wait(timeout) or not self.event.isSet():
            raise MAPIError(MAPI_E_TIMEOUT)
        self.event.clear()


@pytest.fixture
def lmtpclient():
    return smtplib.LMTP(os.getenv('KOPANO_TEST_DAGENT_HOST'), os.getenv('KOPANO_TEST_DAGENT_PORT', 2003))


@pytest.fixture
def session():
    user = os.getenv('KOPANO_TEST_USER')
    password = os.getenv('KOPANO_TEST_PASSWORD')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket, flags=0)


@pytest.fixture
def store(session):
    return GetDefaultStore(session)


@pytest.fixture
def storeid(store):
    return store.GetProps([PR_STORE_ENTRYID], 0)[0].Value


@pytest.fixture
def root(store):
    return store.OpenEntry(None, None, 0)


@pytest.fixture
def inboxid(store):
    return store.GetReceiveFolder(b'IPM', 0)[0]


@pytest.fixture
def inbox(store, inboxid):
    inbox = store.OpenEntry(inboxid, None, MAPI_MODIFY)
    yield inbox
    inbox.EmptyFolder(DELETE_HARD_DELETE | DEL_ASSOCIATED, None, 0)
    table = inbox.GetContentsTable(SHOW_SOFT_DELETES)
    table.SetColumns([PR_ENTRYID], 0)
    rows = table.QueryRows(-1, 0)
    inbox.DeleteMessages([row[0].Value for row in rows], 0, None, DELETE_HARD_DELETE)


@pytest.fixture
def wasteid(store):
    return store.GetProps([PR_IPM_WASTEBASKET_ENTRYID], 0)[0].Value


@pytest.fixture
def outbox(store):
    outboxid = store.GetProps([PR_IPM_OUTBOX_ENTRYID], 0)[0].Value
    outbox = store.OpenEntry(outboxid, None, MAPI_BEST_ACCESS)
    yield outbox
    outbox.EmptyFolder(DELETE_HARD_DELETE | DEL_ASSOCIATED, None, 0)


@pytest.fixture
def waste(store, wasteid):
    waste = store.OpenEntry(wasteid, None, MAPI_BEST_ACCESS)
    yield waste
    waste.EmptyFolder(DELETE_HARD_DELETE | DEL_ASSOCIATED, None, 0)


@pytest.fixture
def wastesink(store, wasteid):
    sink = AdviseSink()
    connection = store.Advise(wasteid, fnevNewMail | fnevObjectModified, sink)
    yield sink
    store.Unadvise(connection)


@pytest.fixture
def sink(store, inbox):
    sink = AdviseSink()
    inboxeid = store.GetReceiveFolder(b'IPM', 0)[0]
    connection = store.Advise(inboxeid, fnevNewMail, sink)
    yield sink
    store.Unadvise(connection)


@pytest.fixture
def rules(inbox):
    rules = inbox.OpenProperty(PR_RULES_TABLE, IID_IExchangeModifyTable, 0, 0)
    yield rules
    inbox.DeleteProps([PR_RULES_TABLE])


@pytest.fixture
def addressbook(session):
    return session.OpenAddressBook(0, None, 0)


@pytest.fixture
def gab(addressbook):
    defaultdir = addressbook.GetDefaultDir()
    return addressbook.OpenEntry(defaultdir, None, 0)


@pytest.fixture
def gab_user(gab):
    username = os.getenv('KOPANO_TEST_USER')
    table = gab.GetContentsTable(0)
    table.FindRow(SPropertyRestriction(RELOP_EQ, PR_ACCOUNT_W, SPropValue(PR_ACCOUNT_W, username)), BOOKMARK_BEGINNING, 0)
    row = table.QueryRows(1, 0)
    return row


@pytest.fixture
def create_test_email():
    def _create_test_email(sender, recipient, subject, body, html=False):
        if html:
            msg = MIMEMultipart()
            msg.attach(MIMEText(body, 'html'))
        else:
            msg = EmailMessage()
            msg.set_content(body)

        msg['Subject'] = subject
        msg['From'] = sender
        msg['To'] = recipient

        return msg
    return _create_test_email


@pytest.fixture
def reply_template(inbox):
    msg = inbox.CreateMessage(None, MAPI_ASSOCIATED)
    msg.SetProps([SPropValue(PR_SUBJECT, b'reply template'), SPropValue(PR_BODY, b'body')])
    msg.SaveChanges(0)
    eid = msg.GetProps([PR_ENTRYID], 0)[0].Value
    yield eid
    inbox.DeleteMessages([eid], 0, None, DELETE_HARD_DELETE)
