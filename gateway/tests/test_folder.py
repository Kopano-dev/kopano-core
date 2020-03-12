import pytest


@pytest.mark.parametrize("name", ['INBOX', 'inbox'])
def test_create_existing(imap, login, name):
    assert imap.create(name)[0] == 'NO'


def test_create_bad(imap, login):
    assert imap.create('""')[0] == 'NO'


def test_create_double(imap, folder):
    assert imap.create(folder)[0] == 'NO'


def test_delete_notfound(imap, login):
    assert imap.delete('nope')[0] == 'NO'


def test_delete_inbox(imap, login):
    assert imap.delete('INBOX')[0] == 'NO'


def test_delete_selected(imap, login):
    folder = 'delete_test_folder'
    assert imap.create(folder)[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'
    assert imap.delete(folder)[0] == 'OK'


def test_create_public_folder(imap, delim):
    publicfolder = '"' + delim . join(('Public folders', 'test'))+'"'

    assert imap.create(publicfolder)[0] == 'OK'
    assert imap.delete(publicfolder)[0] == 'OK'


def test_create_trailing_leading_slash(imap, delim):
    assert imap.create(delim . join((delim + 'INBOX', 'slash' + delim)))[0] == 'NO'
    assert imap.create(delim . join(('INBOX', 'slash' + delim)))[0] == 'NO'


def test_create_accent_folder(imap, delim):
    folder = ('"' + delim.join(('INBOX', 't<E9>st')) + '"').encode('utf8')
    assert imap.create(folder)[0] == 'OK'
    assert imap.delete(folder)[0] == 'OK'


def test_greate_than_sign_folder(imap, delim):
    folder = '"' + delim.join(('INBOX', 'Greater > Than')) + '"'
    assert imap.create(folder)[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'
    assert any([folder for folder in imap.list('""', '*')[1] if b"INBOX/Greater > Than" in folder])
    assert imap.delete(folder)[0] == 'OK'


def test_japanese_folder(imap, delim):
    folder = delim.join(('INBOX', '&MFMwkzBrMGEwbw-'))
    imap.create(folder)[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'
    assert imap.delete(folder)[0] == 'OK'


def test_rename(imap, delim):
    origfolder = delim.join(('INBOX', 'aaa'))
    destfolder = delim.join(('INBOX', 'bbb'))
    assert imap.create(origfolder)[0] == 'OK'
    assert imap.rename(origfolder, destfolder)[0] == 'OK'
    assert imap.select(destfolder)[0] == 'OK'
    assert imap.delete(destfolder)[0] == 'OK'


def test_rename_identical(imap, delim):
    folder = delim.join(('INBOX', 'aaa'))
    assert imap.create(folder)[0] == 'OK'
    assert imap.rename(folder, folder)[0] == 'NO'
    assert imap.delete(folder)[0] == 'OK'


def test_rename_recursive(imap, delim):
    origfolder = delim.join(('INBOX', 'aaa'))
    destfolder = delim.join(('INBOX', 'aaa', 'bbb'))
    assert imap.create(origfolder)[0] == 'OK'
    assert imap.rename(origfolder, destfolder)[0] == 'NO'
    assert imap.delete(origfolder)[0] == 'OK'


def test_move_public(imap, delim):
    folder = 'aaa'
    destfolder = '"' + delim.join(('Public folders', 'aaa')) + '"'

    assert imap.create(folder)[0] == 'OK'
    assert imap.rename(folder, destfolder)[0] == 'OK'

    imap.delete(folder)
    imap.delete(destfolder)


def test_move_existing(imap, delim):
    folder1 = 'exists'
    folder2 = 'newfolder'

    imap.create(folder1)
    imap.create(folder2)

    assert imap.rename(folder1, folder2)[0] == 'NO'

    imap.delete(folder1)
    imap.delete(folder2)


def test_multisession_create_delete(delim, imap, imap2, login, login2):
    imap.create(delim.join(('INBOX', 'find')))
    assert imap2.select(delim.join(('INBOX', 'find')))[0] == 'OK'
    imap2.delete(delim.join(('INBOX', 'find')))
    assert imap.select(delim.join(('INBOX', 'find')))[0] == 'NO'



def test_expunge(imap, folder):
    assert imap.select(folder)[0] == 'OK'

    message = b"""Subject: test

    testmail
    """

    # save message twice
    assert imap.append(folder, None, None, message)[0] == 'OK'
    assert imap.append(folder, None, None, message)[0] == 'OK'
    assert imap.store('1', '+FLAGS', '(\\Deleted)')[0] == 'OK'

    # select different folders
    assert imap.select('inbox')[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'

    # check still present
    response = imap.fetch('1', '(FLAGS)')
    assert response[0] == 'OK'
    assert response[1][0][9:19] == b'(\\Deleted)'

    # expunge from store
    response = imap.expunge()
    assert response[0] == 'OK'
    assert response[1] == [b'1']

    # check messages present
    response = imap.status(folder, '(MESSAGES)')
    assert response[0] == 'OK'
    status = response[1][0][len(folder)+4:].rstrip(b')').split(b' ')
    assert status[0] == b'MESSAGES'

    # check presents of 1 message
    assert int(status[1]) == 1


def test_close_expunge(imap, folder):
    assert imap.select(folder)[0] == 'OK'

    message = b"""Subject: test

    testmail
    """

    # save message twice
    assert imap.append(folder, None, None, message)[0] == 'OK'
    assert imap.append(folder, None, None, message)[0] == 'OK'
    assert imap.store('1', '+FLAGS', '(\\Deleted)')[0] == 'OK'

    # select different folders
    assert imap.select('inbox')[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'

    # check still present
    response = imap.fetch('1', '(FLAGS)')
    assert response[0] == 'OK'
    assert response[1][0][9:19] == b'(\\Deleted)'

    # expunge from store
    assert imap.close()[0] == 'OK'

    # check messages present
    response = imap.status(folder, '(MESSAGES)')
    assert response[0] == 'OK'
    status = response[1][0][len(folder)+4:].rstrip(b')').split(b' ')
    assert status[0] == b'MESSAGES'

    # check presents of 1 message
    assert int(status[1]) == 1


def test_subscribe(imap, delim):
    subfldtest = ('() "' + delim + '" "subway"').encode('ascii')
    # create, lsub -> no, subscribe, lsub -> yes, unscribe, lsub -> no, delete
    imap.create('subway')

    lsub1 = imap.lsub('""', '*')
    assert lsub1[1].count(subfldtest) == 0
    assert imap.subscribe('subway')[0] == 'OK'

    lsub2 = imap.lsub('""', '*')
    assert lsub2[1].count(subfldtest) == 1
    assert lsub1 != lsub2
    assert imap.unsubscribe('subway')[0] == 'OK'

    lsub3 = imap.lsub('""', '*')
    assert lsub3[1].count(subfldtest) == 0
    assert lsub1 == lsub3
    assert imap.delete('subway')[0] == 'OK'


def test_subscribe_by_delete(imap, delim):
    subfldtest = ('() "' + delim + '" "subway"').encode('ascii')
    # create, lsub -> no, subscribe, lsub -> yes, delete, lsub -> no
    imap.create('subway')

    lsub1 = imap.lsub('""', '*')
    assert lsub1[1].count(subfldtest) == 0
    assert imap.subscribe('subway')[0] == 'OK'

    lsub2 = imap.lsub('""', '*')
    assert lsub2[1].count(subfldtest) == 1
    assert lsub1 != lsub2
    assert imap.delete('subway')[0] == 'OK'

    lsub3 = imap.lsub('""', '*')
    assert lsub3[1].count(subfldtest) == 0
    assert lsub1 == lsub3
    # should still be possible to unsubscribe
    assert imap.unsubscribe('subway')[0] == 'OK'


def test_unsubscribe_by_rename(imap, delim):
    subfldtest = ('() "' + delim + '" "subway"').encode('ascii')
    subfldtest2 = ('() "' + delim + '" "renamed"').encode('ascii')
    # create, lsub -> no, subscribe, lsub -> yes, delete, lsub -> no
    imap.create('subway')

    lsub1 = imap.lsub('""', '*')
    assert lsub1[1].count(subfldtest) == 0
    assert imap.subscribe('subway')[0] == 'OK'

    lsub2 = imap.lsub('""', '*')
    assert lsub2[1].count(subfldtest) == 1
    assert lsub1 != lsub2
    assert imap.rename('subway', 'renamed')[0] == 'OK'

    lsub3 = imap.lsub('""', '*')
    assert lsub3[1].count(subfldtest2) == 1
    assert imap.delete('renamed')[0] == 'OK'
