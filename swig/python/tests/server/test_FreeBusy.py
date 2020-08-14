import time

from MAPI.Tags import PR_MAILBOX_OWNER_ENTRYID
from MAPI.Time import unixtime, FileTime
from MAPI.Struct import FreeBusyBlock


def test_publish_fb(store, freebusy_user):
    eid = store.GetProps([PR_MAILBOX_OWNER_ENTRYID], 0)[0].Value
    update, status = freebusy_user.LoadFreeBusyUpdate([eid], None)
    assert status == 1
    assert update

    timestamp = int(time.time())
    update.PublishFreeBusy([FreeBusyBlock(0, timestamp, 1)])
    update.SaveChanges(unixtime(0), unixtime(timestamp))

    data, status = freebusy_user.LoadFreeBusyData([eid], None)
    start, end = data.GetFBPublishRange()
    assert start
    assert end

    enum = data.EnumBlocks(FileTime(0), FileTime(0xFFFFFFFFFFFFFFFF))
    blocks = enum.Next(100)
    assert blocks
    assert blocks[0].status == 1

    # Reset freebusy
    update.ResetPublishedFreeBusy()
    update.SaveChanges(unixtime(0), unixtime(timestamp))

    data, status = freebusy_user.LoadFreeBusyData([eid], None)
    enum = data.EnumBlocks(FileTime(0), FileTime(0xFFFFFFFFFFFFFFFF))
    blocks = enum.Next(100)
    assert not blocks



