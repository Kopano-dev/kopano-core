import os
import imaplib
import poplib

import pytest

from MAPI import DELETE_HARD_DELETE, MAPI_MODIFY
from MAPI.Util import OpenECSession, GetDefaultStore, SPropValue
from MAPI.Tags import (PR_SUBJECT, PR_ENTRYID, PR_BODY, PR_MESSAGE_CLASS,
                       PR_IPM_PUBLIC_FOLDERS_ENTRYID)


@pytest.fixture
def session():
    user = os.getenv('KOPANO_TEST_POP3_USERNAME')
    password = os.getenv('KOPANO_TEST_POP3_PASSWORD')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket)


@pytest.fixture
def store(session):
    return GetDefaultStore(session)


@pytest.fixture
def inbox(store):
    inboxeid = store.GetReceiveFolder(b'IPM', 0)[0]
    inbox = store.OpenEntry(inboxeid, None, MAPI_MODIFY)
    yield inbox
    inbox.EmptyFolder(DELETE_HARD_DELETE, None, 0)


@pytest.fixture
def imapsession():
    user = os.getenv('KOPANO_TEST_USER')
    password = os.getenv('KOPANO_TEST_PASSWORD')
    socket = os.getenv('KOPANO_SOCKET')

    return OpenECSession(user, password, socket)


@pytest.fixture
def imapstore(imapsession):
    return GetDefaultStore(imapsession)


@pytest.fixture
def imapinbox(imapstore):
    inboxeid = imapstore.GetReceiveFolder(b'IPM', 0)[0]
    inbox = imapstore.OpenEntry(inboxeid, None, MAPI_MODIFY)
    yield inbox
    inbox.EmptyFolder(DELETE_HARD_DELETE, None, 0)


@pytest.fixture
def ipmnote(inbox):
    message = inbox.CreateMessage(None, 0)

    message.SetProps([SPropValue(PR_SUBJECT, b'test mail'),
                      SPropValue(PR_BODY, b'test body'),
                      SPropValue(PR_MESSAGE_CLASS, b'IPM.Note'),
                      ])

    message.SaveChanges(0)

    yield message

    eid = message.GetProps([PR_ENTRYID], 0)[0]
    inbox.DeleteMessages([eid.Value], 0, None, DELETE_HARD_DELETE)


@pytest.fixture
def pop3():
    yield poplib.POP3(os.getenv('KOPANO_TEST_POP3_HOST'), os.getenv('KOPANO_TEST_POP3_PORT', 110))


@pytest.fixture
def pop3login(pop3):
    assert pop3.user(os.getenv('KOPANO_TEST_POP3_USERNAME'))[:3] == b'+OK'
    assert pop3.pass_(os.getenv('KOPANO_TEST_POP3_PASSWORD'))[:3] == b'+OK'


@pytest.fixture
def reloginpop3():
    def _reloginpop3():
        pop3 = poplib.POP3(os.getenv('KOPANO_TEST_POP3_HOST'), os.getenv('KOPANO_TEST_POP3_PORT'))
        assert pop3.user(os.getenv('KOPANO_TEST_POP3_USERNAME'))[:3] == b'+OK'
        assert pop3.pass_(os.getenv('KOPANO_TEST_POP3_PASSWORD'))[:3] == b'+OK'
        return pop3

    return _reloginpop3


@pytest.fixture
def imap():
    yield imaplib.IMAP4(os.getenv('KOPANO_TEST_IMAP_HOST'), os.getenv('KOPANO_TEST_IMAP_PORT', 143))


@pytest.fixture
def login(imap):
    assert imap.login(os.getenv('KOPANO_TEST_USER'), os.getenv('KOPANO_TEST_PASSWORD'))[0] == 'OK'


@pytest.fixture
def imap2():
    yield imaplib.IMAP4(os.getenv('KOPANO_TEST_IMAP_HOST'), os.getenv('KOPANO_TEST_IMAP_PORT'))


@pytest.fixture
def login2(imap2):
    assert imap2.login(os.getenv('KOPANO_TEST_USER'), os.getenv('KOPANO_TEST_PASSWORD'))[0] == 'OK'


@pytest.fixture
def delim(imap, login):
    folder = imap.list('""', '*')[1][0].decode('ascii')
    return folder[folder.find('"')+1]


@pytest.fixture
def message(imap, login, inbox):
    message = b"""Subject: test

    testmail
    """

    assert imap.append('inbox', None, None, message)[0] == 'OK'


@pytest.fixture
def testmsg(imapinbox):
    yield b"""Date: Thu, 14 Oct 2010 17:12:27 +0200
    Subject: smaill email
    From: Internet user <internet@localhost>
    To: Test user <user1@localhost>

    And only a plaintext body.
    """


@pytest.fixture
def folder(imap, login):
    folder = imap.list('""', '*')[1][0].decode('ascii')
    pos = folder.find('"')
    testfolder = folder[pos+1] . join(('INBOX', 'empty'))
    imap.create(testfolder)

    yield testfolder

    imap.delete(testfolder)


@pytest.fixture
def secondfolder(imap, login):
    folder = imap.list('""', '*')[1][0].decode('ascii')
    pos = folder.find('"')
    testfolder = folder[pos+1] . join(('INBOX', 'second'))
    imap.create(testfolder)

    yield testfolder

    imap.delete(testfolder)


@pytest.fixture
def envelop_message(imap, folder):
    message = b"""
    Date: Thu, 14 Oct 2010 17:12:27 +0200
    Subject: klein mailtje
    From: Internet user <internet@localhost.net>
    Sender: Another user <another@localhost.net>
    Reply-To: Reply To Me <replies@localhost.net>
    In-Reply-To: <ref-to-other-unique-string@localhost.net>
    To: Test user <user1@localhost.org>
    Cc: User One <user_one@localhost.com>, User Two <user_two@localhost.com>
    Bcc: Hidden User <hidden.user@localhost.xxx>
    Message-Id: <some-unique-string-here@localhost.net>

    And only a plaintext body.
    """

    imap.append(folder, None, None, message)
