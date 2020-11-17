# SPDX-License-Identifier: LGPL-3.0-or-later

import os

from MAPI.Tags import PR_SUBJECT_W


def test_normal_mail(lmtpclient, inbox, sink, create_test_email):
    receiver = os.getenv('KOPANO_TEST_EMAIL')
    sender = 'test@kopano.demo'
    subject = 'plaintext'
    body = 'empty body'
    msg = create_test_email(sender, receiver, subject, body)
    lmtpclient.sendmail(sender, receiver, msg.as_string())

    sink.WaitForNotification(60)

    table = inbox.GetContentsTable(0)
    rowcount = table.GetRowCount(0)
    assert rowcount == 1

    table.SetColumns([PR_SUBJECT_W], 0)
    rows = table.QueryRows(1, 0)
    assert rows[0][0].Value == subject


def test_html_mail(lmtpclient, inbox, sink, create_test_email):
    receiver = os.getenv('KOPANO_TEST_EMAIL')
    sender = 'test@kopano.demo'
    subject = 'html'
    body = '<p>empty body</p>'
    msg = create_test_email(sender, receiver, subject, body, True)
    lmtpclient.sendmail(sender, receiver, msg.as_string())

    sink.WaitForNotification(60)

    table = inbox.GetContentsTable(0)
    rowcount = table.GetRowCount(0)
    assert rowcount == 1

    table.SetColumns([PR_SUBJECT_W], 0)
    rows = table.QueryRows(1, 0)
    assert rows[0][0].Value == subject
