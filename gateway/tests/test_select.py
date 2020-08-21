import imaplib

import pytest


def test_selectbad(imap, login):
    assert imap.select('bad')[0] == 'NO'


def test_multiselect(imap, login):
    assert imap.select('inbox')[0] == 'OK'
    assert imap.select('inbox')[0] == 'OK'


def test_selectdefaultfolder(imap, login):
    assert imap.select('inbox')[0] == 'OK'


def test_selectandclose(imap, login):
    assert imap.select('inbox')[0] == 'OK'
    assert imap.close()[0] == 'OK'


def test_selectandcheck(imap, login):
    assert imap.select('inbox')[0] == 'OK'
    assert imap.check()[0] == 'OK'


def test_selectandnoop(imap, login):
    assert imap.noop()[0] == 'OK'
    assert imap.select('inbox')[0] == 'OK'
    assert imap.noop()[0] == 'OK'


def test_selectfetchclose(imap, login, message):
    assert imap.select('inbox')[0] == 'OK'

    assert imap.fetch('1:*', '(FLAGS)')[0] == 'OK'
    assert imap.close()[0] == 'OK'

    imap.state = 'SELECTED'
    with pytest.raises(imaplib.IMAP4.error) as excinfo:
        assert imap.fetch('1:*', '(FLAGS)')[0] == 'BAD'
    assert 'FETCH command error' in str(excinfo)


def test_closenotopen(imap, login):
    imap.state = 'SELECTED'
    assert imap.close()[0] == 'NO'


def test_checknotopen(imap, login):
    imap.state = 'SELECTED'
    with pytest.raises(imaplib.IMAP4.error) as excinfo:
        imap.check()[0] == 'BAD'
    assert 'CHECK command error' in str(excinfo)


def test_statusvsselect(imap, login):
    assert imap.select('inbox')[0] == 'OK'
    result = imap.status('INBOX', '(MESSAGES RECENT UIDNEXT UIDVALIDITY UNSEEN)')
    assert result[0] == 'OK'

    folder = result[1][0][:7]
    assert folder == b'"INBOX"'

    status = result[1][0][9:].rstrip(b')').split(b' ')
    assert status[0] == b'MESSAGES'
    assert status[2] == b'RECENT'
    assert status[4] == b'UIDNEXT'
    assert status[6] == b'UIDVALIDITY'
    assert status[8] == b'UNSEEN'


def test_idle(imap, login):
    assert imap.select('inbox')[0] == 'OK'

    tag = imap._new_tag()
    imap.send(b'%s IDLE\r\n' % tag)
    result = imap.readline().strip()
    assert result == b'+ waiting for notifications'

    imap.send(b'DONE\r\n')
    result = imap.readline().strip()
    assert result == b'%s OK IDLE complete' % tag


def test_emptyidle(imap, login):
    tag = imap._new_tag()
    imap.send(b'%s IDLE\r\n' % tag)
    result = imap.readline().strip()
    assert result == b"+ Can't open selected folder to idle in"


def test_readonly(imap, login, imapinbox):
    assert imap.select('inbox', True)[0] == 'OK'
    assert imap.store('1', '+FLAGS', '(\\Seen)')[0] == 'NO'
