# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Part of the high-level python bindings for Kopano

Copyright 2017 - 2019 Kopano and its licensors (see LICENSE file)
"""

import copy
import sys
import weakref

from MAPI import (
    MAPI_MESSAGE, MAPI_FOLDER, MAPI_STORE, MAPIAdviseSink, fnevObjectModified,
    fnevObjectCreated, fnevObjectMoved, fnevObjectCopied, fnevObjectDeleted,
)

from MAPI.Tags import (
    IID_IMAPIAdviseSink,
)

from .compat import benc as _benc, bdec as _bdec
from .errors import NotSupportedError

from . import folder as _folder
from . import item as _item

fnevObjTypeMessage = 0x00010000 # TODO to defs?
fnevObjTypeFolder = 0x00020000
fnevIgnoreCounters = 0x00040000

OBJECT_TYPES = ['folder', 'item']
FOLDER_TYPES = ['mail', 'contacts', 'calendar']
EVENT_TYPES = ['created', 'updated', 'deleted']

TRACER = sys.gettrace()

class Notification(object):
    """Notification class

    A notification instance indicates a change to :class:`items <Item>`
    or :class:`folders <Folder>`.
    """
    def __init__(self, mapiobj):
        #: The underlying MAPI notification object
        self.mapiobj = mapiobj
        #: The changed :class:`item <Item>` or :class:`folder <Folder>`.
        self.object = None
        #: The type of the changed object (*item*, *folder*).
        self.object_type = None
        #: The type of change (*created*, *updated*, *deleted*)
        self.event_type = None

def _split(mapiobj, store):
    notif = Notification(mapiobj)

    # object
    if mapiobj.ulObjType == MAPI_MESSAGE:
        item = _item.Item()
        item.store = store
        item.server = store.server
        item._entryid = mapiobj.lpEntryID

        notif.object_type = 'item'
        notif.object = item

    elif mapiobj.ulObjType == MAPI_FOLDER:
        folder = _folder.Folder(store=store, entryid=_benc(mapiobj.lpEntryID),
            _check_mapiobj=False)

        notif.object = folder
        notif.object_type = 'folder'

    elif mapiobj.ulObjType == MAPI_STORE:
        notif.object = store
        notif.object_type = 'store'

    # event
    if mapiobj.ulEventType in (fnevObjectCreated, fnevObjectCopied):
        notif.event_type = 'created'
        yield notif

    elif mapiobj.ulEventType == fnevObjectModified:
        notif.event_type = 'updated'
        yield notif

    elif mapiobj.ulEventType == fnevObjectDeleted:
        if mapiobj.ulObjType == MAPI_MESSAGE:
            item._folder = store.folder(entryid=_benc(mapiobj.lpParentID))

        notif.event_type = 'deleted'
        yield notif

    # TODO test with folders/add test
    elif mapiobj.ulEventType == fnevObjectMoved:
        notif.event_type = 'created'
        yield notif

        notif = copy.copy(notif)

        if mapiobj.ulObjType == MAPI_MESSAGE:
            item = _item.Item()
            item.store = store
            item.server = store.server
            item._entryid = mapiobj.lpOldID
            notif.object = item
            item._folder = store.folder(entryid=_benc(mapiobj.lpOldParentID))

        elif mapiobj.ulObjType == MAPI_FOLDER: # TODO mapiobj.lpOldID not set?
            folder = _folder.Folder(store=store,
                entryid=_benc(mapiobj.lpEntryID), _check_mapiobj=False)
            notif.object = folder

        notif.event_type = 'deleted'
        yield notif

# TODO can't server filter all this?
def _filter(notifs, folder, event_types, folder_types):
    for notif in notifs:
        if notif.event_type not in event_types:
            continue

        if (folder and \
            notif.object_type == 'item' and \
            notif.object.folder != folder):
            continue

        if (notif.object_type == 'item' and \
            notif.object.folder.type_ not in folder_types):
            continue

        if (notif.object_type == 'folder' and \
            notif.object.type_ not in folder_types):
            continue

        yield notif

class AdviseSink(MAPIAdviseSink):
    def __init__(self, store, folder, event_types, folder_types, delegate):
        MAPIAdviseSink.__init__(self, [IID_IMAPIAdviseSink])

        self.store = weakref.ref(store) if store is not None else None
        self.folder = weakref.ref(folder) if folder is not None else None
        self.delegate = delegate
        self.event_types = event_types
        self.folder_types = folder_types

    def OnNotify(self, notifications): # pragma: no cover
        # called from a thread created from C, and as tracing is set for each
        # thread separately from Python, tracing doesn't work here (so coverage
        # also doesn't work).
        #
        # threading.settrace won't help, as the thread is created from C,
        # so we just 'inherit' any used tracer from the 'global' python thread.
        if TRACER:
            sys.settrace(TRACER)
        return self._on_notify(notifications) # method call to start tracing

    def _on_notify(self, notifications):
        if not hasattr(self.delegate, 'update'):
            return 0
        store  = self.store() if self.store else None
        folder = self.folder() if self.folder else None
        for n in notifications:
            for m in _filter(_split(n, store), folder,
                    self.event_types, self.folder_types):
                self.delegate.update(m)
        return 0

def _flags(object_types, event_types):
    flags = fnevObjectModified | \
            fnevObjectCreated | \
            fnevObjectMoved | \
            fnevObjectCopied | \
            fnevObjectDeleted | \
            fnevIgnoreCounters

    if 'folder' in object_types:
        flags |= fnevObjTypeFolder
    if 'item' in object_types:
        flags |= fnevObjTypeMessage

    return flags

def subscribe(store, folder, delegate, object_types=None, folder_types=None,
        event_types=None):

    if not store.server.notifications:
        raise NotSupportedError('server connection does not support \
notifications (try Server(notifications=True))')

    object_types = object_types or OBJECT_TYPES
    folder_types = folder_types or FOLDER_TYPES
    event_types = event_types or EVENT_TYPES

    flags = _flags(object_types, event_types)
    sink = AdviseSink(store, folder, event_types, folder_types, delegate)

    if folder:
        delegate._conn = store.mapiobj.Advise(_bdec(folder.entryid), flags, sink)
    else:
        delegate._conn = store.mapiobj.Advise(None, flags, sink)

def unsubscribe(store, delegate):
    store.mapiobj.Unadvise(delegate._conn)
