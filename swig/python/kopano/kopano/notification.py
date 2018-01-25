"""
Part of the high-level python bindings for Kopano

Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

import sys
from .compat import  hex as _hex

from MAPI import (
    MAPI_MESSAGE,
    MAPINotifSink, fnevObjectModified, fnevObjectCreated,
    fnevObjectMoved, fnevObjectDeleted,
)
from MAPI.Struct import (
    MAPIErrorNoSupport,
)

if sys.hexversion >= 0x03000000:
    from . import folder as _folder
    from . import item as _item
else:
    import folder as _folder
    import item as _item

from .compat import bdec as _bdec

class Notification:
    def __init__(self, store, mapiobj):
        self.store = store
        self.event_type = mapiobj.ulEventType
        self.object_type = mapiobj.ulObjType
        self._parent_entryid = mapiobj.lpParentID
        self._entryid = mapiobj.lpEntryID

    @property
    def parent(self):
        # TODO support more types
        if self.object_type == MAPI_MESSAGE:
            return _folder.Folder(
                store=self.store, entryid=_hex(self._parent_entryid)
            )

        return None

    @property
    def object(self):
        # TODO support more types
        if self.object_type == MAPI_MESSAGE:
            return _item.Item(parent=self.store, entryid=self._entryid)

        return None

def _notifications(store, entryid, time):
    flags = fnevObjectModified | fnevObjectCreated \
        | fnevObjectMoved | fnevObjectDeleted

    sink = MAPINotifSink()
    try:
        if entryid:
            store.mapiobj.Advise(_bdec(entryid), flags, sink)
        else:
            store.mapiobj.Advise(None, flags, sink)
    except MAPIErrorNoSupport:
        raise NotSupportedError(
            "No support for advise, please use"
            "kopano.Server(notifications=True)"
        )

    mapi_notifications = sink.GetNotifications(False, time*1000)
    for notification in mapi_notifications:
        yield Notification(store, notification)
