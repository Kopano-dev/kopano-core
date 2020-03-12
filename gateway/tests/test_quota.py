import pytest


def test_qoutaroot(imap, login):
    assert imap.getquotaroot('INBOX')[0] == 'OK'


@pytest.mark.skip(reason='Gateway should return NO when the folder does not exists')
def test_qoutaroot_invalid(imap, login):
    assert imap.getquotaroot('nope-not-folder')[0] == 'NO'


def test_getquota(imap, login):
    assert imap.getquota('""')[0] == 'OK'


def test_getquota_invalidrootname(imap, login):
    assert imap.getquota('wrong-quota-rootname')[0] == 'NO'


def test_setquota(imap, login):  # not supported
    assert imap.setquota('name', '1')[0] == 'NO'
