import os
import imaplib

import pytest


@pytest.fixture
def imap():
    yield imaplib.IMAP4(os.getenv('KOPANO_TEST_IMAP_HOST'), os.getenv('KOPANO_TEST_IMAP_PORT'))


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
def message(imap, login):
    message = b"""Subject: test

    testmail
    """

    assert imap.append('inbox', None, None, message)[0] == 'OK'


@pytest.fixture
def testmsg():
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
