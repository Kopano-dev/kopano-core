import os
import imaplib

import pytest


# TODO: test user with IMAP feature enabled


def test_login(imap):
    res = imap.login(os.getenv('KOPANO_TEST_USER'), os.getenv('KOPANO_TEST_PASSWORD'))
    assert res[0] == 'OK'


def test_logout(imap, login):
    assert imap.logout()[0] == 'BYE'


def test_login_badpass(imap):
    with pytest.raises(imaplib.IMAP4.error) as excinfo:
        imap.login(os.getenv('KOPANO_TEST_USER'), 'bad')
    assert 'wrong username or password' in str(excinfo)


def test_login_baduser(imap):
    with pytest.raises(imaplib.IMAP4.error) as excinfo:
        imap.login('bad', os.getenv('KOPANO_TEST_PASSWORD'))
    assert 'wrong username or password' in str(excinfo)


def test_namespace(imap, login):
    assert imap.namespace()[0] == 'OK'


@pytest.mark.skip(reason='When not authenticated gateway should return BAD not OK')
def test_namespace_noauth(imap):
    imap.state = 'SELECTED'
    assert imap.namespace()[0] == 'BAD'
