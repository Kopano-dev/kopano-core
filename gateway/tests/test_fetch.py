import imaplib

import pytest


# contains all frustrating data for envelope, but still valid rfc822 data
allbugs = b"""Date: Thu, 14 Oct 2010 17:12:27 +0200
Subject: some "special" subject
From: Jimmy "Two Fingers" Capone <jimmy@no-taxes-for-me.com>
Sender: "Nobody" <unknown@localhost.net>
Reply-To: Reply To "Me" <replies@localhost.net>
In-Reply-To: <ref-to-other-unique-string@localhost.net>
To: Test "user" <user1@localhost.org>
Cc: User "One" <user_one@localhost.com>, User "Two" <user_two@localhost.com>
Bcc: "Hidden User" <hidden.user@localhost.xxx>
Message-Id: <some-unique-string-here@localhost.net>
"""

envelopmsg = b"""Date: Thu, 14 Oct 2010 17:12:27 +0200
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


def test_fetch_empty_pre(imap, folder):
    imap.select(folder)
    assert imap.fetch('1:*', 'flags')[0] == 'NO'


def test_fetch_empty_preuid(imap, folder):
    imap.select(folder)
    imap.uid('fetch', '1:*', 'flags')[0] == 'OK'


def test_fetch_empty_post(imap, folder):
    imap.select(folder)
    assert imap.fetch('*:1', 'flags')[0] == 'NO'


def test_fetch_empty_postuid(imap, folder):
    imap.select(folder)
    assert imap.uid('fetch', '*:1', 'flags')[0] == 'OK'


def test_fetch_empty_single(imap, folder):
    imap.select(folder)
    assert imap.fetch('1', 'flags')[0] == 'NO'


def test_fetch_empty_single_uid(imap, folder):
    imap.select(folder)
    assert imap.uid('fetch', '1', 'flags')[0] == 'OK'


def test_fetch_empty_double(imap, folder):
    imap.select(folder)
    assert imap.fetch('1,2', 'flags')[0] == 'NO'


def test_fetch_empty_doubleuid(imap, folder):
    imap.select(folder)
    assert imap.uid('fetch', '1,2', 'flags')[0] == 'OK'


def test_fetch_uids(imap, folder, envelop_message):
    imap.select(folder)
    assert imap.fetch('1', '(UID FLAGS)') == imap.uid('fetch', '1:*', '(UID FLAGS)')


def test_fetch_shortcut_full(imap, folder, envelop_message):
    imap.select(folder)
    result = imap.fetch('1', 'full')
    assert result[1][0].find(b'  ') == -1
    assert result[1][0].find(b'FLAGS') > 0
    assert result[1][0].find(b'INTERNALDATE') > 0
    assert result[1][0].find(b'RFC822.SIZE') > 0
    assert result[1][0].find(b'ENVELOPE') > 0
    assert result[1][0].find(b'BODY') > 0


def test_fetch_shortcut_all(imap, folder, envelop_message):
    imap.select(folder)
    result = imap.fetch('1', 'all')
    assert result[1][0].find(b'  ') == -1
    assert result[1][0].find(b'FLAGS') > 0
    assert result[1][0].find(b'INTERNALDATE') > 0
    assert result[1][0].find(b'RFC822.SIZE') > 0
    assert result[1][0].find(b'ENVELOPE') > 0


def test_fetch_shortcut_fast(imap, folder, envelop_message):
    imap.select(folder)
    result = imap.fetch('1', 'fast')
    assert result[1][0].find(b'  ') == -1
    assert result[1][0].find(b'NIL') == -1
    assert result[1][0].find(b'FLAGS') > 0
    assert result[1][0].find(b'INTERNALDATE') > 0
    assert result[1][0].find(b'RFC822.SIZE') > 0


def test_fetch_envelope(imap, folder):
    assert imap.append(folder, None, None, envelopmsg)[0] == 'OK'
    imap.select(folder)
    result = imap.fetch('1', '(ENVELOPE)')
    assert result[0] == 'OK'
    assert result[1] == [b'1 (ENVELOPE ("Thu, 14 Oct 2010 17:12:27 +0200" "klein mailtje" (("Internet user" NIL "internet" "localhost.net")) (("Another user" NIL "another" "localhost.net")) (("Reply To Me" NIL "replies" "localhost.net")) (("Test user" NIL "user1" "localhost.org")) (("User One" NIL "user_one" "localhost.com")("User Two" NIL "user_two" "localhost.com")) (("Hidden User" NIL "hidden.user" "localhost.xxx")) "<ref-to-other-unique-string@localhost.net>" "<some-unique-string-here@localhost.net>"))']


def test_fetch_envelope_all_bugs(imap, folder):
    assert imap.append(folder, None, None, allbugs)[0] == 'OK'
    imap.select(folder)
    result = imap.fetch('1', '(ENVELOPE)')
    assert result[0] == 'OK'
    assert result[1] == [b'1 (ENVELOPE ("Thu, 14 Oct 2010 17:12:27 +0200" "some \\"special\\" subject" (("Jimmy \\"Two Fingers\\" Capone" NIL "jimmy" "no-taxes-for-me.com")) (("Nobody" NIL "unknown" "localhost.net")) (("Reply To \\"Me\\"" NIL "replies" "localhost.net")) (("Test \\"user\\"" NIL "user1" "localhost.org")) (("User \\"One\\"" NIL "user_one" "localhost.com")("User \\"Two\\"" NIL "user_two" "localhost.com")) (("Hidden User" NIL "hidden.user" "localhost.xxx")) "<ref-to-other-unique-string@localhost.net>" "<some-unique-string-here@localhost.net>"))']


def test_fetch_envelope_all_nill(imap, folder):
    assert imap.append(folder, None, None, b'X-Mailer: no email here')[0] == 'OK'
    imap.select(folder)
    result = imap.fetch('1', '(ENVELOPE)')
    assert result[0] == 'OK'
    # dovecot and courier return NIL for the Date header (but not allowed according to the Note part of 7.4.2 of RFC-3501)
    # vmime always returns a date because RFC-2822 enforces its presence
    assert result[1] == [b'1 (ENVELOPE ("Thu, 1 Jan 1970 00:00:00 +0000" NIL NIL NIL NIL NIL NIL NIL NIL NIL))']


def test_body_contents_unixenter(imap, folder):
    msg = b"""Subject: Unix enter mail

line one
line two
line three
"""

    assert imap.append(folder, None, None, msg)[0] == 'OK'
    imap.select(folder)
    assert imap.fetch('1', '(BODY.PEEK[TEXT])')[1][0][0] == b'1 (BODY[TEXT] {32}'


def test_body_contents_dosenter(imap, folder):
    msg = b"""Subject: DOS enter mail\r
\r
line one\r
line two\r
line three\r
"""

    assert imap.append(folder, None, None, msg)[0] == 'OK'
    imap.select(folder)
    assert imap.fetch('1', '(BODY.PEEK[TEXT])')[1][0][0] == b'1 (BODY[TEXT] {32}'


def test_body_contents_bodypartwithoutboundary(imap, folder):
    assert imap.append(folder, None, None, envelopmsg)[0] == 'OK'
    imap.select(folder)
    assert imap.fetch('1', '(BODY.PEEK[TEXT])')[1][0][0] == b'1 (BODY[TEXT] {28}'


def test_body_contents_paritalcompletebody(imap, folder):
    assert imap.append(folder, None, None, envelopmsg)[0] == 'OK'
    imap.select(folder)
    assert imap.fetch('1', '(BODY.PEEK[1]<0>)')[1][0][0] == b'1 (BODY[1]<0> {28}'


def test_body_contents_paritallimitedbody(imap, folder):
    assert imap.append(folder, None, None, envelopmsg)[0] == 'OK'
    imap.select(folder)
    assert imap.fetch('1', '(BODY.PEEK[1]<10>)')[1][0][0] == b'1 (BODY[1]<10> {18}'


def test_body_contents_paritalstartlimitedbody(imap, folder):
    assert imap.append(folder, None, None, envelopmsg)[0] == 'OK'
    imap.select(folder)
    assert imap.fetch('1', '(BODY.PEEK[1]<0.10>)')[1][0][0] == b'1 (BODY[1]<0> {10}'


def test_body_contents_apple_iphone_7293(imap, folder):
    assert imap.append(folder, None, None, envelopmsg)[0] == 'OK'
    imap.select(folder)
    assert imap.fetch('1', '(BODY.PEEK[HEADER] BODY.PEEK[TEXT])')[1][1][0] == b' BODY[TEXT] {28}'


# TODO: move to regressions?
def test_fetch_flags_ZCP10012(imap, folder, secondfolder):
    # make different folders, mark read different, and see if the
    # changes are correct when selecting different folders, while
    # not changing column set in fetch command
    imap.append(folder, '()', None, envelopmsg)
    imap.append(folder, '(\\Seen)', None, envelopmsg)
    imap.append(secondfolder, '(\\Seen)', None, envelopmsg)
    imap.append(secondfolder, '()', None, envelopmsg)
    # open folder 1 unread contents, was being cached the whole session
    imap.select(folder)
    results1 = imap.fetch('1:*', 'flags')
    assert results1[0] == 'OK'
    assert results1[1] == [b'1 (FLAGS (\\Recent))', b'2 (FLAGS (\\Seen \\Recent))']
    # get folder 2 read contents, should show as read
    imap.select(secondfolder)
    results2 = imap.fetch('1:*', 'flags')
    assert results2[0] == 'OK'
    assert results2[1] == [b'1 (FLAGS (\\Seen \\Recent))', b'2 (FLAGS (\\Recent))']
    # TODO: flags could be in any order, and recent may or maynot have been set depending on imap server


def test_error_fetch(imap, login):
    # ZCP-11367, a fetch on a closed folder returns an error, but come clients loop on the same commands
    try:
        imap.close()
    except:
        pass

    imap.state = 'SELECTED'
    for i in range(1, 10):
        with pytest.raises(imaplib.IMAP4.error) as excinfo:
            imap.fetch('1:*', 'flags')
        assert 'FETCH command error' in str(excinfo)
