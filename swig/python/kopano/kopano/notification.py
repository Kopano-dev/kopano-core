"""
Part of the high-level python bindings for Kopano

Copyright 2017 - Kopano and its licensors (see LICENSE file for details)
"""

import sys
try:
    from queue import Queue
except ImportError:
    from Queue import Queue

from MAPI import (
    MAPI_MESSAGE, MAPI_FOLDER,
    MAPIAdviseSink, fnevObjectModified, fnevObjectCreated,
    fnevObjectMoved, fnevObjectDeleted,
)

from MAPI.Struct import (
    MAPIErrorNoSupport,
)

from MAPI.Tags import (
    IID_IMAPIAdviseSink,
)

from .compat import  benc as _benc

if sys.hexversion >= 0x03000000:
    from . import folder as _folder
    from . import item as _item
else:
    import folder as _folder
    import item as _item

class AdviseSink(MAPIAdviseSink):
    def __init__(self, q):
        MAPIAdviseSink.__init__(self, [IID_IMAPIAdviseSink])
        self.q = q

    def OnNotify(self, notifications):
        for n in notifications:
            self.q.put(n)

        return 0

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
                store=self.store, entryid=_benc(self._parent_entryid)
            )

    @property
    def object(self):
        # TODO support more types
        if self.object_type == MAPI_MESSAGE:
            return self.store.item(entryid=_benc(self._entryid))

        elif self.object_type == MAPI_FOLDER:
            return self.store.folder(entryid=_benc(self._entryid))

class AdviseSink(MAPIAdviseSink):
    def __init__(self, store, sink):
        MAPIAdviseSink.__init__(self, [IID_IMAPIAdviseSink])
        self.store = store
        self.sink = sink

    def OnNotify(self, notifications):
        if hasattr(self.sink, 'update'):
            for n in notifications:
                self.sink.update(Notification(self.store, n))
        return 0

def subscribe(store, folder, sink):
    flags = fnevObjectModified | fnevObjectCreated \
        | fnevObjectMoved | fnevObjectDeleted

    sink._store = store
    sink2 = AdviseSink(store, sink)

    if folder:
        sink._conn = store.mapiobj.Advise(_bdec(folder.entryid), flags, sink2)
    else:
        sink._conn = store.mapiobj.Advise(None, flags, sink2)

def unsubscribe(store, sink):
    store.mapiobj.Unadvise(sink._conn)

def _notifications(store, entryid):
    flags = fnevObjectModified | fnevObjectCreated \
        | fnevObjectMoved | fnevObjectDeleted # TODO more?

    q = Queue()
    sink = AdviseSink(q)

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

    while True:
        yield Notification(store, q.get())
