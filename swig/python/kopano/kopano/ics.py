# SPDX-License-Identifier: AGPL-3.0-only
"""
Part of the high-level python bindings for Kopano

Copyright 2005 - 2016 Zarafa and its licensors (see LICENSE file)
Copyright 2016 - 2019 Kopano and its licensors (see LICENSE file)
"""

import os
import struct
import sys
import traceback
import time

from MAPI import (
    ECImportContentsChanges, SYNC_E_IGNORE, WrapStoreEntryID,
    SYNC_NORMAL, SYNC_ASSOCIATED, SYNC_CATCHUP, SYNC_UNICODE, IStream,
    STREAM_SEEK_SET, RELOP_GE, RELOP_LT, ECImportHierarchyChanges,
    ECImportAddressbookChanges, MAPI_MAILUSER, SYNC_READ_STATE,
    MSGFLAG_READ,
)
from MAPI.Defs import (
    PpropFindProp
)
from MAPI.Tags import (
    PR_ENTRYID, PR_STORE_ENTRYID, PR_EC_PARENT_HIERARCHYID,
    PR_EC_HIERARCHYID, PR_STORE_RECORD_KEY, PR_CONTENTS_SYNCHRONIZER,
    PR_MESSAGE_DELIVERY_TIME, PR_HIERARCHY_SYNCHRONIZER,
    IID_IECImportAddressbookChanges, IID_IECExportAddressbookChanges,
)
from MAPI.Tags import (
    IID_IExchangeImportContentsChanges, IID_IECImportContentsChanges,
    IID_IExchangeExportChanges, IID_IExchangeImportHierarchyChanges,
    IID_IECImportHierarchyChanges
)
from MAPI.Struct import (
    MAPIError, MAPIErrorNotFound, MAPIErrorNoAccess, SPropValue,
    SPropertyRestriction, SAndRestriction, MAPIErrorNetworkError,
)
from MAPI.Time import (
    unixtime
)

from .compat import benc as _benc, bdec as _bdec

from . import item as _item
try:
    from . import folder as _folder
except ImportError: # pragma: no cover
    _folder = sys.modules[__package__+'.folder']
try:
    from . import user as _user
except ImportError: # pragma: no cover
    _user = sys.modules[__package__ + '.user']
try:
    from . import store as _store
except ImportError: # pragma: no cover
    _store = sys.modules[__package__ + '.store']
try:
    from . import utils as _utils
except ImportError: # pragma: no cover
    _utils = sys.modules[__package__ + '.utils']

TESTING = False
if os.getenv('PYKO_TESTING'): # env variable used in testset
    TESTING = True

class TrackingHierarchyImporter(ECImportHierarchyChanges):
    def __init__(self, server, importer, stats):
        ECImportHierarchyChanges.__init__(self,
            [IID_IExchangeImportHierarchyChanges,
             IID_IECImportHierarchyChanges])

        self.server = server
        self.log = server.log
        self.importer = importer
        self.stats = stats

    def ImportFolderChange(self, props):
        if hasattr(self.importer, 'update'):
            eid = _benc(PpropFindProp(props, PR_ENTRYID).Value)
            folder = _folder.Folder(store=self.importer.store, entryid=eid)
            self.importer.update(folder)

    def ImportFolderDeletion(self, flags, sourcekeys):
        if hasattr(self.importer, 'delete'):
            for sourcekey in sourcekeys:
                folder = _folder.Folder(self.importer.store,
                    _check_mapiobj=False)
                folder._sourcekey = _benc(sourcekey)
                self.importer.delete(folder, flags)

    def UpdateState(self, stream):
        pass

class TrackingContentsImporter(ECImportContentsChanges):
    def __init__(self, server, importer, stats):
        ECImportContentsChanges.__init__(self,
            [IID_IExchangeImportContentsChanges,
             IID_IECImportContentsChanges])

        self.server = server
        self.log = server.log
        self.importer = importer
        self.stats = stats
        self.skip = False

    def ImportMessageChangeAsAStream(self, props, flags):
        self.ImportMessageChange(props, flags)

    def ImportMessageChange(self, props, flags):
        if self.skip:
            raise MAPIError(SYNC_E_IGNORE)
        if not hasattr(self.importer, 'update'):
            return
        try:
            entryid = PpropFindProp(props, PR_ENTRYID)
            if self.importer.store:
                mapistore = self.importer.store.mapiobj
            else:
                store_entryid = PpropFindProp(props, PR_STORE_ENTRYID).Value
                store_entryid = WrapStoreEntryID(0, b'zarafa6client.dll',
                    store_entryid[:-4]) + self.server.pseudo_url + b'\x00'
                mapistore = self.server._store2(store_entryid)
            item = _item.Item()
            item.server = self.server
            item.store = _store.Store(mapiobj=mapistore, server=self.server)
            try:
                item.mapiobj = _utils.openentry_raw(
                    mapistore, entryid.Value, 0)
                # TODO properties don't exist?
                props = item.mapiobj.GetProps([PR_EC_HIERARCHYID,
                    PR_EC_PARENT_HIERARCHYID, PR_STORE_RECORD_KEY], 0)
                item.docid = props[0].Value
                item.storeid = _benc(props[2].Value)
                self.importer.update(item, flags)
            # TODO mail already deleted, can we do this in a cleaner way?
            except (MAPIErrorNotFound, MAPIErrorNoAccess):
                self.log.debug('received change for entryid %s, but it could \
not be opened', _benc(entryid.Value))
        except Exception:
            self.log.error('could not process change for entryid %s (%r):',
                _benc(entryid.Value), props)
            self.log.error(traceback.format_exc())
            if self.stats:
                self.stats['errors'] += 1
        raise MAPIError(SYNC_E_IGNORE)

    def ImportMessageDeletion(self, flags, entries):
        if self.skip or not hasattr(self.importer, 'delete'):
            return
        try:
            for entry in entries:
                item = _item.Item()
                item.server = self.server
                item._sourcekey = _benc(entry)
                self.importer.delete(item, flags)
        except Exception:
            self.log.error('could not process delete for entries: %s',
                [_benc(entry) for entry in entries])
            self.log.error(traceback.format_exc())
            if self.stats:
                self.stats['errors'] += 1

    def ImportPerUserReadStateChange(self, states):
        if self.skip or not hasattr(self.importer, 'read'):
            return
        try:
            for state in states:
                item = _item.Item()
                item.server = self.server
                if self.importer.store: # TODO system-wide?
                    item.store = self.importer.store
                item._sourcekey = _benc(state.SourceKey)
                self.importer.read(item, bool(state.ulFlags & MSGFLAG_READ))

        except Exception:
            self.log.error('could not process readstate change')
            self.log.error(traceback.format_exc())
            if self.stats:
                self.stats['errors'] += 1

    def UpdateState(self, stream):
        pass

class TrackingGABImporter(ECImportAddressbookChanges):
    def __init__(self, server, importer):
        ECImportAddressbookChanges.__init__(self,
            [IID_IECImportAddressbookChanges])
        self.server = server
        self.importer = importer

    def ImportABChange(self, type_, entryid):
        if hasattr(self.importer, 'update') and type_ == MAPI_MAILUSER:
            user = self.server.user(userid=_benc(entryid))
            self.importer.update(user)

    def ImportABDeletion(self, type_, entryid):
        if hasattr(self.importer, 'delete') and type_ == MAPI_MAILUSER:
            user = _user.User(server=self.server)
            user._userid = _benc(entryid)
            self.importer.delete(user)

    def UpdateState(self, stream):
        pass

def state(mapiobj, associated=False):
    exporter = mapiobj.OpenProperty(
        PR_CONTENTS_SYNCHRONIZER,IID_IExchangeExportChanges, 0, 0)
    if associated:
        exporter.Config(None, SYNC_NORMAL | SYNC_ASSOCIATED | SYNC_CATCHUP,
            None, None, None, None, 0)
    else:
        exporter.Config(None, SYNC_NORMAL | SYNC_CATCHUP,
            None, None, None, None, 0)
    steps, step = None, 0
    while steps != step:
        steps, step = exporter.Synchronize(step)
    stream = IStream()
    exporter.UpdateState(stream)
    stream.Seek(0, STREAM_SEEK_SET)
    return _benc(stream.Read(0xFFFFF))

def sync_hierarchy(server, syncobj, importer, state, stats=None):
    importer = TrackingHierarchyImporter(server, importer, stats)
    exporter = syncobj.OpenProperty(PR_HIERARCHY_SYNCHRONIZER,
        IID_IExchangeExportChanges, 0, 0)

    stream = IStream()
    stream.Write(_bdec(state))
    stream.Seek(0, STREAM_SEEK_SET)

    flags = SYNC_NORMAL | SYNC_UNICODE
    exporter.Config(stream, flags, importer, None, None, None, 0)

    step = 0
    while True:
        (steps, step) = exporter.Synchronize(step)
        if (steps == step):
            break

    exporter.UpdateState(stream)

    stream.Seek(0, STREAM_SEEK_SET)
    return _benc(stream.Read(0xFFFFF))

def sync(server, syncobj, importer, state, max_changes, associated=False,
        window=None, begin=None, end=None, stats=None):
    log = server.log

    importer = TrackingContentsImporter(server, importer, stats)
    exporter = syncobj.OpenProperty(
        PR_CONTENTS_SYNCHRONIZER, IID_IExchangeExportChanges, 0, 0)

    stream = IStream()
    stream.Write(_bdec(state))
    stream.Seek(0, STREAM_SEEK_SET)

    restriction = None
    if window:
        # sync window of last N seconds
        propval = SPropValue(PR_MESSAGE_DELIVERY_TIME,
            unixtime(int(time.time()) - window))
        restriction = SPropertyRestriction(
            RELOP_GE, PR_MESSAGE_DELIVERY_TIME, propval)

    elif begin or end:
        restrs = []
        if begin:
            propval = SPropValue(PR_MESSAGE_DELIVERY_TIME,
                unixtime(time.mktime(begin.timetuple())))
            restrs.append(SPropertyRestriction(
                RELOP_GE, PR_MESSAGE_DELIVERY_TIME, propval))
        if end:
            propval = SPropValue(PR_MESSAGE_DELIVERY_TIME,
                unixtime(time.mktime(end.timetuple())))
            restrs.append(SPropertyRestriction(
                RELOP_LT, PR_MESSAGE_DELIVERY_TIME, propval))
        if len(restrs) == 1:
            restriction = restrs[0]
        else:
            restriction = SAndRestriction(restrs)

    flags = SYNC_NORMAL | SYNC_UNICODE | SYNC_READ_STATE
    if associated:
        flags |= SYNC_ASSOCIATED
    try:
        if TESTING and os.getenv('PYKO_TEST_NOT_FOUND'):
            raise MAPIErrorNotFound()
        exporter.Config(stream, flags, importer, restriction, None, None, 0)
    except MAPIErrorNotFound:
        # syncid purged because of 'sync_lifetime' option in server.cfg:
        # get new syncid.
        log.warn("Sync state does not exist on server (anymore); \
requesting new one")

        syncid, changeid = struct.unpack('<II', _bdec(state))
        stream = IStream()
        stream.Write(struct.pack('<II', 0, changeid))
        stream.Seek(0, STREAM_SEEK_SET)

        exporter.Config(stream, flags, importer, restriction, None, None, 0)

    step = retry = changes = 0
    sleep_time = 0.4

    while True:
        try:
            try:
                if (TESTING and \
                    os.getenv('PYKO_TEST_NETWORK_ERROR') and \
                    not importer.skip):
                    raise MAPIErrorNetworkError()
                (steps, step) = exporter.Synchronize(step)
            finally:
                importer.skip = False

            changes += 1
            retry = 0

            if (steps == step) or (max_changes and changes >= max_changes):
                break

        except MAPIError as e:
            log.warn("Received a MAPI error or timeout (error=0x%x, \
retry=%d/5)", e.hr, retry)

            time.sleep(sleep_time)

            if sleep_time < 5.0:
                sleep_time *= 2.0

            if retry < 5:
                retry += 1
            else:
                log.error("Too many retries, skipping change")
                if stats is not None:
                    stats['errors'] += 1

                # in case of a timeout or other issue, try to skip
                # the change after trying several times
                importer.skip = True

                retry = 0

    exporter.UpdateState(stream)

    stream.Seek(0, STREAM_SEEK_SET)

    state = stream.Read(0xFFFFF)

    # because changes may be reordered for efficiency, we are not always
    # linearly following the change journal. so the current state cannot
    # always be represented as a single change id. instead, the current state
    # may contain changes which have been synced, relative to a certain
    # change id (so we have synced until this change id, plus these changes).

    # in pyko though, we always sync until there are no further changes,
    # so this should normally not occur.

    # TODO add an ICS flag to disable reordering!

    if len(state) != 8:
        log.error('sync state %d bytes, expect problems', len(state))

    return _benc(state)

def sync_gab(server, mapistore, importer, state):
    stream = IStream()
    stream.Write(_bdec(state))
    stream.Seek(0, STREAM_SEEK_SET)

    importer = TrackingGABImporter(server, importer)
    exporter = mapistore.OpenProperty(
        PR_CONTENTS_SYNCHRONIZER, IID_IECExportAddressbookChanges, 0, 0)

    exporter.Config(stream, 0, importer)
    steps, step = None, 0
    while steps != step:
        steps, step = exporter.Synchronize(step)

    stream = IStream()
    exporter.UpdateState(stream)

    stream.Seek(0, STREAM_SEEK_SET)
    return _benc(stream.Read(0xFFFFF))
