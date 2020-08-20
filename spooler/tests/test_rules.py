import os

from MAPI import ROW_ADD, FL_SUBSTRING, FL_IGNORECASE
from MAPI.Util import SPropValue, SContentRestriction, PpropFindProp
from MAPI.Struct import ACTION, ACTIONS, ROWENTRY, actMoveCopy, actReply, actFwdDelegate
from MAPI.Tags import (PR_RULE_LEVEL, PR_RULE_NAME, PR_RULE_PROVIDER,
                       PR_RULE_STATE, PR_RULE_SEQUENCE, PR_RULE_CONDITION,
                       PR_RULE_ACTIONS, EDK_RULES_VERSION, ST_ENABLED,
                       PR_ENTRYID, PR_EMAIL_ADDRESS, PR_RECIPIENT_TYPE,
                       PR_SUBJECT, ACTTYPE, MAPI_TO, PR_ADDRTYPE, PR_DISPLAY_NAME,
                       PR_OBJECT_TYPE, PR_SMTP_ADDRESS, PR_SEARCH_KEY)


def add_rule(rules, condition, ruleaction):
    rules.ModifyTable(0, [ROWENTRY(ROW_ADD,
                                   [SPropValue(PR_RULE_LEVEL, 0),
                                    SPropValue(PR_RULE_NAME, b'test_rule'),
                                    SPropValue(PR_RULE_PROVIDER, b'RuleOrganizer'),
                                    SPropValue(PR_RULE_STATE, ST_ENABLED),
                                    SPropValue(PR_RULE_SEQUENCE, 1),
                                    SPropValue(PR_RULE_CONDITION, condition),
                                    SPropValue(PR_RULE_ACTIONS, ruleaction)
                                    ])])


def create_copymove_rule(rules, action, subject, storeid, wasteid):
    condition = SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, subject.encode()))
    ruleaction = ACTIONS(EDK_RULES_VERSION, [ACTION(action, 0x00000000, None, None, 0x00000000, actMoveCopy(storeid, wasteid))])
    add_rule(rules, condition, ruleaction)


def create_forward_rule(rules, subject, gab_user, flavor=0x00000000):
    condition = SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, subject.encode()))
    ruleaction = ACTIONS(EDK_RULES_VERSION, [ACTION(ACTTYPE.OP_FORWARD, flavor, None, None, 0x00000000, actFwdDelegate(gab_user))])
    add_rule(rules, condition, ruleaction)


def test_move_rule_match(lmtpclient, inbox, storeid, wasteid, rules, waste, wastesink, create_test_email):
    subject = 'move wastebasket'
    create_copymove_rule(rules, ACTTYPE.OP_MOVE, subject, storeid, wasteid)

    sender = 'test@kopano.demo'
    receiver = os.getenv('KOPANO_TEST_EMAIL')
    msg = create_test_email(sender, receiver, subject, '')
    lmtpclient.sendmail(sender, receiver, msg.as_string())

    wastesink.WaitForNotification(60)

    table = inbox.GetContentsTable(0)
    assert not table.GetRowCount(0)

    table = waste.GetContentsTable(0)
    assert table.GetRowCount(0) == 1


def test_move_rule_mismatch(lmtpclient, inbox, storeid, wasteid, rules, waste, sink, create_test_email):
    subject = 'move wastebasket'
    create_copymove_rule(rules, ACTTYPE.OP_MOVE, 'none', storeid, wasteid)

    sender = 'test@kopano.demo'
    receiver = os.getenv('KOPANO_TEST_EMAIL')
    msg = create_test_email(sender, receiver, subject, '')
    lmtpclient.sendmail(sender, receiver, msg.as_string())

    sink.WaitForNotification(60)

    table = inbox.GetContentsTable(0)
    assert table.GetRowCount(0)

    table = waste.GetContentsTable(0)
    assert not table.GetRowCount(0)


def test_copy_rule_match(lmtpclient, inbox, storeid, wasteid, rules, waste, sink, create_test_email):
    subject = 'copy wastebasket'
    create_copymove_rule(rules, ACTTYPE.OP_COPY, subject, storeid, wasteid)

    sender = 'test@kopano.demo'
    receiver = os.getenv('KOPANO_TEST_EMAIL')
    msg = create_test_email(sender, receiver, subject, '')
    lmtpclient.sendmail(sender, receiver, msg.as_string())

    sink.WaitForNotification(60)

    table = inbox.GetContentsTable(0)
    assert table.GetRowCount(0)

    table = waste.GetContentsTable(0)
    assert table.GetRowCount(0)


def test_reply_rule(lmtpclient, inbox, outbox, rules, sink, reply_template, create_test_email):
    '''Reply to a mail matching subject to KOPANO_TEST_USER with a template message in the inbox assoicated messages'''
    subject = 'reply rule'
    sender = 'test@kopano.demo'
    receiver = os.getenv('KOPANO_TEST_EMAIL')

    condition = SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, subject.encode()))
    ruleaction = ACTIONS(EDK_RULES_VERSION, [ACTION(ACTTYPE.OP_REPLY, 0x00000000, None, None, 0x00000000, actReply(reply_template, b'\0'*16))])
    add_rule(rules, condition, ruleaction)

    msg = create_test_email(sender, receiver, subject, '')
    lmtpclient.sendmail(sender, receiver, msg.as_string())

    sink.WaitForNotification(60)

    table = inbox.GetContentsTable(0)
    rowcount = table.GetRowCount(0)
    assert rowcount == 1

    table = outbox.GetContentsTable(0)
    rowcount = table.GetRowCount(0)
    assert rowcount == 1


def test_forward_rule(lmtpclient, inbox, outbox, gab_user, rules, sink, reply_template, create_test_email):
    '''Forward mail matching subject to KOPANO_TEST_USER'''
    subject = 'reply rule'
    sender = 'test@kopano.demo'
    receiver = os.getenv('KOPANO_TEST_EMAIL')

    create_forward_rule(rules, subject, gab_user)

    msg = create_test_email(sender, receiver, subject, '')
    lmtpclient.sendmail(sender, receiver, msg.as_string())

    sink.WaitForNotification(60)
    table = inbox.GetContentsTable(0)
    rowcount = table.GetRowCount(0)
    assert rowcount == 1

    table = outbox.GetContentsTable(0)
    rowcount = table.GetRowCount(0)
    assert rowcount == 1


def test_redirect_rule(lmtpclient, inbox, outbox, gab_user, rules, sink, create_test_email):
    '''Redirect mail matching subject to KOPANO_TEST_USER'''

    subject = 'forward rule'
    sender = 'test@kopano.demo'
    receiver = os.getenv('KOPANO_TEST_EMAIL')

    create_forward_rule(rules, subject, gab_user, 0x3)  # 3 = FWD_PRESERVE_SENDER | FWD_DO_NOT_MUNGE_MSG)

    msg = create_test_email(sender, receiver, subject, '')
    lmtpclient.sendmail(sender, receiver, msg.as_string())

    sink.WaitForNotification(60)
    table = inbox.GetContentsTable(0)
    rowcount = table.GetRowCount(0)
    assert rowcount == 1

    table = outbox.GetContentsTable(0)
    table.SetColumns([PR_ENTRYID], 0)
    rowcount = table.GetRowCount(0)
    assert rowcount == 1

    rows = table.QueryRows(1, 0)
    assert rows

    item = outbox.OpenEntry(rows[0][0].Value, None, 0)
    reciptable = item.GetRecipientTable(0)

    recips = reciptable.QueryRows(-1, 0)
    assert PpropFindProp(recips[0], PR_RECIPIENT_TYPE).Value == MAPI_TO
    # Sender has no recipient type set
    assert not PpropFindProp(recips[1], PR_RECIPIENT_TYPE)
    assert PpropFindProp(recips[0], PR_EMAIL_ADDRESS).Value == PpropFindProp(recips[1], PR_EMAIL_ADDRESS).Value


def test_forward_external_rule(lmtpclient, inbox, outbox, rules, sink, reply_template, create_test_email):
    '''Forward mail matching subject to test@kopano.io, should be disallowed with an reject in inbox'''
    subject = 'forward external rule'
    sender = 'test@kopano.demo'
    external = 'test@kopano.io'
    receiver = os.getenv('KOPANO_TEST_EMAIL')

    external = [SPropValue(PR_ADDRTYPE, b'SMTP'), SPropValue(PR_EMAIL_ADDRESS, external.encode()), SPropValue(PR_SMTP_ADDRESS, external.encode()),
                SPropValue(PR_DISPLAY_NAME, 'external contractor'.encode()), SPropValue(PR_OBJECT_TYPE, 6), SPropValue(PR_RECIPIENT_TYPE, MAPI_TO),
                SPropValue(PR_SEARCH_KEY, b'SMTP:' + external.encode())]

    create_forward_rule(rules, subject, [external])

    msg = create_test_email(sender, receiver, subject, '')
    lmtpclient.sendmail(sender, receiver, msg.as_string())

    sink.WaitForNotification(60)
    table = inbox.GetContentsTable(0)
    rowcount = table.GetRowCount(0)
    assert rowcount == 2

    rows = table.QueryRows(2, 0)
    assert subject.encode() == PpropFindProp(rows[0], PR_SUBJECT).Value
    assert b'rule not forwarded (administratively blocked)' in PpropFindProp(rows[1], PR_SUBJECT).Value

    table = outbox.GetContentsTable(0)
    rowcount = table.GetRowCount(0)
    assert not rowcount
