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
    MAPI_MESSAGE, MAPIAdviseSink, fnevObjectModified, fnevObjectCreated,
    fnevObjectMoved, fnevObjectCopied, fnevObjectDeleted,
)

from MAPI.Struct import (
    MAPIErrorNoSupport,
)

from MAPI.Tags import (
    IID_IMAPIAdviseSink,
)

from .compat import benc as _benc, bdec as _bdec
from .errors import NotSupportedError

if sys.hexversion >= 0x03000000:
    from . import folder as _folder
    from . import item as _item
else:
    import folder as _folder
    import item as _item

class Notification:
    def __init__(self, store, mapiobj):
        self.store = store
        self.mapiobj = mapiobj

    @property
    def object(self):
        if self.mapiobj.ulObjType == MAPI_MESSAGE:
            item = _item.Item()
            item.store = self.store
            item.server = self.store.server
            item._entryid = self.mapiobj.lpEntryID
            return item

    @property
    def old_object(self):
        if self.mapiobj.ulObjType == MAPI_MESSAGE and self.mapiobj.lpOldID:
            item = _item.Item()
            item.store = self.store
            item.server = self.store.server
            item._entryid = self.mapiobj.lpOldID
            return item

    @property
    def parent(self):
        if self.mapiobj.ulObjType == MAPI_MESSAGE:
            return _folder.Folder(
                store=self.store, entryid=_benc(self.mapiobj.lpParentID)
            )

    @property
    def old_parent(self):
        if self.mapiobj.ulObjType == MAPI_MESSAGE and self.mapiobj.lpOldParentID:
            return _folder.Folder(
                store=self.store, entryid=_benc(self.mapiobj.lpOldParentID)
            )

    @property
    def event_type(self):
        return {
            fnevObjectCreated: 'create',
            fnevObjectCopied: 'copy',
            fnevObjectMoved: 'move',
            fnevObjectModified: 'update',
            fnevObjectDeleted: 'delete',
        }[self.mapiobj.ulEventType]

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

class AdviseSinkQueue(MAPIAdviseSink):
    def __init__(self, q):
        MAPIAdviseSink.__init__(self, [IID_IMAPIAdviseSink])
        self.q = q

    def OnNotify(self, notifications):
        for n in notifications:
            self.q.put(n)
        return 0

def subscribe(store, folder, sink):
    flags = fnevObjectModified | fnevObjectCreated \
        | fnevObjectMoved | fnevObjectCopied | fnevObjectDeleted

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
        | fnevObjectMoved | fnevObjectCopied | fnevObjectDeleted

    q = Queue()
    sink = AdviseSinkQueue(q)

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
