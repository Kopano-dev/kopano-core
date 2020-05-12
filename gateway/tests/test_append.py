import time

import pytest


def test_appendonly(imap, login, testmsg):
    imap.append('INBOX', None, None, testmsg)[0] == 'OK'


def test_appendonly_withflags(imap, login, testmsg):
    imap.append('INBOX', '(\\Seen)', None, testmsg)[0] == 'OK'


def test_appendonly_withdate(imap, login, testmsg):
    imap.append('INBOX', None, time.localtime(), testmsg)[0] == 'OK'


def test_appendonly_withflagsanddate(imap, login, testmsg):
    imap.append('INBOX', '(\\Seen)', time.localtime(), testmsg)[0] == 'OK'


@pytest.mark.parametrize('flag', ['$Forwarded', '\\Answered', '\\Seen', '\\Deleted', '\\Flagged'])
def test_flags(imap, login, testmsg, flag):
    assert imap.select('INBOX')[0] == 'OK'
    imap.append('INBOX', None, None, testmsg)[0] == 'OK'
    num = imap.select('INBOX')[1][0].decode()

    assert flag.encode('ascii') not in imap.fetch(num, '(FLAGS)')[1][0]
    assert flag.encode('ascii') in imap.store(num, '+FLAGS', '(' + flag + ')')[1][0]


def test_multiflags(imap, login, testmsg):
    assert imap.select('INBOX')[0] == 'OK'
    imap.append('INBOX', None, None, testmsg)[0] == 'OK'
    num = imap.select('INBOX')[1][0].decode()

    flags = imap.fetch(num, '(FLAGS)')[1][0]
    assert b'$Forwarded' not in flags
    assert b'\\Seen' not in flags
    assert b'\\Flagged' not in flags

    flags = imap.store(num, '+FLAGS', '($Forwarded \\Seen)')[1][0]
    assert b'$Forwarded' in flags
    assert b'\\Seen' in flags

    flags = imap.store(num, 'FLAGS', '(\\Flagged)')[1][0]
    assert b'$Forwarded' not in flags
    assert b'\\Seen' not in flags
    assert b'\\Flagged' in flags

    flags = imap.store(num, '-FLAGS', '(\\Flagged)')[1][0]
    assert b'$Forwarded' not in flags
    assert b'\\Seen' not in flags
    assert b'\\Flagged' not in flags
