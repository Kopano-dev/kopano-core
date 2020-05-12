from datetime import datetime, timedelta

import pytest


BEFORE = datetime.now() - timedelta(days=1)
AFTER = datetime.now() + timedelta(days=1)
FORMAT = '%d-%b-%Y'

TESTDATA = [
        (b'SUBJECT: test', '(SUBJECT "test")', 1),
        (b'SUBJECT: test', '(HEADER Subject "test")', 0),  # TODO: only works with kopano-search?
        (b'SUBJECT: test', 'RECENT', 1),
        (b'SUBJECT: test', 'NEW', 1),
        (b'SUBJECT: test', 'UNANSWERED', 1),
        (b'SUBJECT: test', 'OLD', 0),
        # (b'SUBJECT: test\r\n\r\nline one\r\n', '(BODY pager)', 1),
        # (b'SUBJECT: test\r\n\r\nline one\r\n', '(BODY pag)', 1),
        # (b'SUBJECT: test\r\n\r\nline one\r\n', '(BODY screen)', 1),
        (b'SUBJECT: test', '(BEFORE "{}")'.format(BEFORE.strftime(FORMAT)), 0),
        (b'SUBJECT: test', '(BEFORE "{}")'.format(AFTER.strftime(FORMAT)), 1),
        (b'SUBJECT: test', '( OR (SUBJECT "foo") (SUBJECT "bar") )', 0),

        ]


@pytest.mark.parametrize('msg,criteria,results', TESTDATA)
def test_subject(imap, folder, msg, criteria, results):
    assert imap.append(folder, None, None, msg)[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'

    result = imap.search(None, criteria)
    assert result[0] == 'OK'
    assert len(result[1][0].split()) == results
