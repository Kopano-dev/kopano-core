import pytest

from MAPI import MAPI_MESSAGE, fnevObjectCreated, fnevTableModified
from MAPI.Struct import SPropValue, PpropFindProp
from MAPI.Tags import PR_ENTRYID, PR_SUBJECT_A, PR_SUBJECT_W


def test_objectcreate(notifystore, notifyinboxid, notifymessage, sink):
    notifystore.Advise(None, fnevObjectCreated, sink)
    notifymessage.SaveChanges(0)
    eid = notifymessage.GetProps([PR_ENTRYID], 0)[0].Value
    notifs = sink.GetNotifications(False, 1000)

    assert len(notifs) == 1
    assert notifs[0].ulObjType == MAPI_MESSAGE
    assert notifs[0].ulEventType == fnevObjectCreated
    assert notifs[0].lpEntryID == eid
    assert notifs[0].lpParentID == notifyinboxid


def test_truncate(notifyinbox, notifymessage, sink):
    table = notifyinbox.GetContentsTable(0)
    table.Advise(fnevTableModified, sink)
    notifymessage.SetProps([SPropValue(PR_SUBJECT_A, b'a' * 1024)])
    notifymessage.SaveChanges(0)
    notifs = sink.GetNotifications(False, 1000)
    assert len(notifs) == 1
    subject = PpropFindProp(notifs[0].row, PR_SUBJECT_A)
    assert subject == SPropValue(PR_SUBJECT_A, b'a' * 255)


@pytest.mark.skip(reason="GetProps returns None")
def test_truncate_unicode(notifyinbox, notifymessage, sink):
    table = notifyinbox.GetContentsTable(0)
    table.Advise(fnevTableModified, sink)
    notifymessage.SetProps([SPropValue(PR_SUBJECT_W, 'ア' * 1024)])
    notifymessage.SaveChanges(0)
    notifs = sink.GetNotifications(False, 1000)
    subject = PpropFindProp(notifs[0].row, PR_SUBJECT_W)
    assert len(notifs) == 1
    subject = PpropFindProp(notifs[0].row, PR_SUBJECT_W)
    assert subject == SPropValue(PR_SUBJECT_W, 'ア' * 255)
