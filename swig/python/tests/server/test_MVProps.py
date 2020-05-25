from MAPI import KEEP_OPEN_READWRITE, MAPI_MODIFY
from MAPI.Struct import SPropValue
from MAPI.Tags import PR_ENTRYID


def test_setprops_more(root, message):
    init_mvprop = SPropValue(0x66011003, [1, 2, 3])
    message.SetProps([init_mvprop])
    message.SaveChanges(KEEP_OPEN_READWRITE)
    messageid = message.GetProps([PR_ENTRYID], 0)[0].Value

    mvprop = SPropValue(0x66011003, [1, 2, 3, 4])
    message.SetProps([mvprop])
    message.SaveChanges(0)
    message = root.OpenEntry(messageid, None, MAPI_MODIFY)

    assert len(message.GetProps([0x66011003], 0)[0].Value) == 4


def test_setprops_less(root, message):
    init_mvprop = SPropValue(0x66011003, [1, 2, 3])
    message.SetProps([init_mvprop])
    message.SaveChanges(KEEP_OPEN_READWRITE)
    messageid = message.GetProps([PR_ENTRYID], 0)[0].Value

    mvprop = SPropValue(0x66011003, [1, 2])
    message.SetProps([mvprop])
    message.SaveChanges(0)
    message = root.OpenEntry(messageid, None, MAPI_MODIFY)

    assert len(message.GetProps([0x66011003], 0)[0].Value) == 2


def test_setnone(root, message):
    init_mvprop = SPropValue(0x66011003, [1, 2, 3])
    message.SetProps([init_mvprop])
    message.SaveChanges(KEEP_OPEN_READWRITE)
    messageid = message.GetProps([PR_ENTRYID], 0)[0].Value

    mvprop = SPropValue(0x66011003, [])
    message.SetProps([mvprop])
    message.SaveChanges(0)
    message = root.OpenEntry(messageid, None, MAPI_MODIFY)
    assert message.GetProps([0x66011003], 0)[0].Value == [1, 2, 3]
