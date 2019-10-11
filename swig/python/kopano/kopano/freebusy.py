# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Part of the high-level python bindings for Kopano

Copyright 2017 - 2019 Kopano and its licensors (see LICENSE file)
"""

import datetime
import time
try:
    import libfreebusy
except ImportError: # pragma: no cover
    pass

from .defs import (
    STATUS_FB,
)

from MAPI.Time import (
    FileTime,
)

import MAPI.Struct

from .compat import (
    bdec as _bdec
    )

# TODO to utils.py?
NANOSECS_BETWEEN_EPOCH = 116444736000000000
def datetime_to_filetime(d):
    return FileTime(int(time.mktime(d.timetuple())) * 10000000 + \
        NANOSECS_BETWEEN_EPOCH)

def datetime_to_rtime(d):
    return datetime_to_filetime(d).filetime / 600000000

def rtime_to_datetime(r):
    return datetime.datetime.fromtimestamp(FileTime(r * 600000000).unixtime)


CODE_STATUS = {
    0: 'free',
    1: 'tentative',
    2: 'busy',
    3: 'outofoffice',
}

class FreeBusyBlock(object):
    """FreeBusyBlock class"""

    def __init__(self, block):
        #: Freebusy block status (free, tentative, busy or outofoffice)
        self.status = CODE_STATUS[block.status]
        #: Freebusy block start
        self.start = rtime_to_datetime(block.start)
        #: Freebusy block end
        self.end = rtime_to_datetime(block.end)

    def __unicode__(self):
        return 'FreeBusyBlock()'

    def __repr__(self):
        return self.__unicode__()

class FreeBusy(object):
    """FreeBusy class

    Freebusy data consists of high level, privacy insensitive, information
    about user availability. This is mostly useful for scheduling meetings,
    without requiring full access to other users' calendars.
    """

    def __init__(self, store):
        self.store = store

    def blocks(self, start=None, end=None):
        """Return all :class:`freebusy blocks <FreeBusyBlock>` for the
        given period.

        :param start: start of period
        :param end: end of period
        """
        eid = _bdec(self.store.user.userid)
        if start:
            ftstart = datetime_to_filetime(start)
        else:
            ftstart = FileTime(0)
        if end:
            ftend = datetime_to_filetime(end)
        else:
            ftend = FileTime(0xFFFFFFFFFFFFFFFF)

        fb = libfreebusy.IFreeBusySupport()
        fb.Open(self.store.server.mapisession, self.store.mapiobj, False)
        fbdata = fb.LoadFreeBusyData([eid], None)
        if fbdata in (0, 1): # TODO what?
            return
        data, status = fbdata
        fb.Close()

        enum = data.EnumBlocks(ftstart, ftend)
        while True:
            blocks = enum.Next(100)
            if blocks:
                for block in blocks:
                    yield FreeBusyBlock(block)
            else:
                break

    def publish(self, start=None, end=None):
        """Publish freebusy information for the given period.

        :param start: start of period
        :param end: end of period
        """
        eid = _bdec(self.store.user.userid) # TODO merge with blocks
        ftstart = datetime_to_filetime(start) # TODO tz?
        ftend = datetime_to_filetime(end) # TODO tz?

        fb = libfreebusy.IFreeBusySupport()
        fb.Open(self.store.server.mapisession, self.store.mapiobj, False)
        update, status = fb.LoadFreeBusyUpdate([eid], None)

        blocks = []
        for occ in self.store.calendar.occurrences(start, end):
            start = datetime_to_rtime(occ.start)
            end = datetime_to_rtime(occ.end)
            # Fall back on busy, same as WebApp
            blocks.append(MAPI.Struct.FreeBusyBlock(start, end,
                STATUS_FB[occ.busystatus] if occ.busystatus else 2))

        update.PublishFreeBusy(blocks)
        update.SaveChanges(ftstart, ftend)
        fb.Close()

    def __iter__(self):
        return self.blocks()

    def __unicode__(self):
        return 'FreeBusy()'

    def __repr__(self):
        return _repr(self)
